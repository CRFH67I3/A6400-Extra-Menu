#!/usr/bin/env python3
"""Build only: fixed local persistent bundle, with no startup Sony/SD IPC."""
import json
from pathlib import Path
import sys

import build_film_runtime_package as base
from build_film_native_boot_package import bundle_bytes

BUNDLE = 'persistent/a6400-film-boot-v15.tar'
SOURCE = '/tmp/a6400-film-boot-v15.tar'


def build(spec_path, out):
    spec = json.loads(spec_path.read_text())
    artifacts, pins, capture_pins = base.load_artifacts(spec, spec_path.parent)
    base.require(not out.exists() and not out.is_symlink(), 'output already exists')
    template_pins = {name: base.digest((base.TEMPLATES/name).read_bytes())
                     for name in ('bootstrap-local.sh', 'prepare.sh', 'runtime-start.sh')}
    identity = {'schema': 4, 'persistent_source': SOURCE,
                'artifacts': {k: v['sha256'] for k, v in sorted(artifacts.items())},
                'module_pins': pins, 'capture_pins': capture_pins, 'templates': template_pins}
    package_id = base.digest(json.dumps(identity, sort_keys=True).encode())[:16]
    rows = '\n'.join(f"{v['dest']} {v['dest']} {v['sha256']} {v['mode']:o}"
                     for v in artifacts.values())
    values = {'PACKAGE_ID': package_id, 'ARTIFACT_ROWS': rows,
              'GUARD_SHA': artifacts['guard']['sha256'], 'CHECK_SHA': artifacts['check']['sha256'],
              'INSTALL_SHA': artifacts['install']['sha256']}
    runtime = base.render('runtime-start.sh', values)
    stage_rows = rows+'\n'+f'runtime-start.sh a6400-film-runtime-start.sh {base.digest(runtime)} 755'
    prepare = base.render('prepare.sh', dict(values, ROWS=stage_rows))
    old_root = b'ROOT=/mnt/A6400/RUNTIME\n'
    base.require(prepare.count(old_root) == 1, 'unexpected prepare source root')
    prepare = prepare.replace(old_root, b'ROOT=/tmp_bt/a6400-film-boot-stage\n')
    entries = {v['dest']: (v['data'], v['mode']) for v in artifacts.values()}
    entries.update({'runtime-start.sh': (runtime, 0o755), 'prepare.sh': (prepare, 0o755)})
    bundle = bundle_bytes(entries)
    bootstrap = base.render('bootstrap-local.sh', dict(values, BUNDLE_BYTES=len(bundle),
        BUNDLE_SHA=base.digest(bundle), PREPARE_SHA=base.digest(prepare), RUNTIME_SHA=base.digest(runtime)))
    files = {'bootstrap-local.sh': (bootstrap, 0o755), BUNDLE: (bundle, 0o644)}
    manifest = {'schema': 4, 'package_id': package_id,
        'deployment_status': 'BUILT_ONLY_NOT_INSTALLED', 'persistent_source': SOURCE,
        'native_sd_read': False, 'raw_sd_mount': False, 'default_enabled': False,
        'capture_persistent': True, 'natural_touch_release_required': True,
        'photo_sd_io_outside_service_validated': False,
        'module_pins': pins, 'capture_pins': capture_pins, 'template_pins': template_pins,
        'disable': {'persistent_root_marker': '/tmp/disable_hook',
                    'ram_markers': ['/tmp_bt/a6400-film.DISABLE', '/tmp_bt/a6400-film-capture.stop'],
                    'sd_markers_supported': False,
                    'note': 'Removing the SD bundle does not disable this local bootstrap. Use the persistent root marker before the next cold boot.'},
        'files': [{'path': n, 'bytes': len(d), 'sha256': base.digest(d), 'mode': f'{m:o}'}
                  for n, (d, m) in sorted(files.items())],
        'bundle_entries': [{'path': n, 'bytes': len(d), 'sha256': base.digest(d), 'mode': f'{m:o}'}
                          for n, (d, m) in sorted(entries.items())],
        'artifacts': [{k: v for k, v in item.items() if k != 'data'} | {'role': role}
                      for role, item in artifacts.items()]}
    out.mkdir()
    for name, (data, mode) in files.items():
        path = out/name;path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(data);path.chmod(mode)
    (out/'manifest.json').write_text(json.dumps(manifest, indent=2, sort_keys=True)+'\n')
    print(f'FILM_LOCAL_BOOT_PACKAGE_BUILT package={package_id} bytes={len(bundle)} out={out}')


def main():
    if len(sys.argv) != 3:
        print('usage: build_film_local_boot_package.py ARTIFACTS.json NEW_OUTPUT_DIRECTORY', file=sys.stderr)
        return 2
    try:
        build(Path(sys.argv[1]).resolve(), Path(sys.argv[2]).absolute())
    except (ValueError, KeyError, TypeError, OSError) as error:
        print('FILM_LOCAL_BOOT_PACKAGE_FAIL '+str(error), file=sys.stderr)
        return 1
    return 0


if __name__ == '__main__':
    sys.exit(main())
