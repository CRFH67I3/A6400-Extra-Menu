"""Host tests for V4.3 Sound Lite UI/SDS tools and bit-exact author_film blur."""
import hashlib
import math
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
import unittest
import wave

ROOT = Path(__file__).resolve().parent
TOOLS = ROOT / 'tools'
if str(TOOLS) not in sys.path:
    sys.path.insert(0, str(TOOLS))


class SoundLiteTests(unittest.TestCase):
    def test_sound_lite_sds_analyze_and_slot_ordering_in_c(self):
        with tempfile.TemporaryDirectory() as td:
            d = Path(td)
            snd_dir = d / 'sounds'
            snd_dir.mkdir()

            import encode_sony_sds_adpcm as sds
            sr = sds.SAMPLE_RATE
            s1, _ = sds.encode_samples_to_sds([int(16000 * math.sin(2 * math.pi * 440 * i / sr)) for i in range(sr // 5)], 186)
            s2, _ = sds.encode_samples_to_sds([int(20000 * math.sin(2 * math.pi * 880 * i / sr)) for i in range(sr // 4)], 240)
            (snd_dir / 'beta_click.pcm').write_bytes(s1)
            (snd_dir / 'alpha_機械.pcm').write_bytes(s2)
            (snd_dir / '.hidden.pcm').write_bytes(s1)
            (snd_dir / 'ignore.wav').write_bytes(b'RIFF')

            hdr_txt = (ROOT / 'camera/sound_lite_ui.h').read_text(encoding='utf-8')
            patched = hdr_txt.replace('/tmp_bt/a6400-sounds', str(snd_dir)).replace(
                '/tmp_bt/a6400-charo-shutter.pcm', str(d / 'missing-base.pcm')
            )
            test_h = d / 'sound_lite_ui_test.h'
            test_h.write_text(patched, encoding='utf-8')

            harness = d / 'harness.c'
            harness.write_text(
                '#include <assert.h>\n'
                '#include <stdio.h>\n'
                '#include <stdlib.h>\n'
                '#include <string.h>\n'
                'static struct { void *background; } ui;\n'
                'static int dummy_w[64];\n'
                'static void *mk(int kind) { (void)kind; return &dummy_w[0]; }\n'
                'static void place(void *w, int a, int x, int y, int bw, int bh) { (void)w;(void)a;(void)x;(void)y;(void)bw;(void)bh; }\n'
                'static void paint(void *w, const unsigned char *c) { (void)w;(void)c; }\n'
                'static void write_lbl(void *w, const char *s) { (void)w;(void)s; }\n'
                '#include "sound_lite_ui_test.h"\n'
                'int main(void) {\n'
                '  assert(sl_build_widgets(mk) == 0);\n'
                '  assert(sl.count == 1);\n'
                '  sl_scan_slots();\n'
                '  assert(sl.count == 3);\n'
                '  assert(strcmp(sl.slots[1].name, "alpha_機械") == 0);\n'
                '  assert(strcmp(sl.slots[2].name, "beta_click") == 0);\n'
                '  assert(sl.slots[1].sound_id == sl_sd_beep_ids[0]);\n'
                '  assert(sl.slots[2].sound_id == sl_sd_beep_ids[1]);\n'
                '  for (int b = 0; b < SL_WAVE_BARS; ++b) {\n'
                '    assert(sl.slots[1].wave[b] >= 8 && sl.slots[1].wave[b] <= 54);\n'
                '  }\n'
                '  assert(sl_activate_slot(0) == 0);\n'
                '  assert(sl_activate_slot(2) == sl_sd_beep_ids[1]);\n'
                '  assert(sl_current_sound_id() == sl_sd_beep_ids[1]);\n'
                '  sl_draw_deck();\n'
                '  sl_hide_widgets();\n'
                '  puts("SOUND_LITE_C_OK");\n'
                '  return 0;\n'
                '}\n',
                encoding='utf-8',
            )
            bin_p = d / 'sl_test'
            subprocess.run(
                ['cc', '-std=c11', '-Wall', '-Wextra', '-Werror', '-I', str(d), str(harness), '-o', str(bin_p)],
                check=True,
                capture_output=True,
                text=True,
            )
            res = subprocess.run([str(bin_p)], check=True, capture_output=True, text=True)
            self.assertIn('SOUND_LITE_C_OK', res.stdout)

    def test_sound_path_and_probe_component_rules(self):
        cmd_src = (ROOT / 'camera/film_native_command.c').read_text(encoding='utf-8')
        probe_src = (ROOT / 'camera/native_capture_probe.c').read_text(encoding='utf-8')
        sp_fn = 'static int sound_path(' + cmd_src.split('static int sound_path(', 1)[1].split('static int usr_ro_mounted', 1)[0]
        sc_fn = 'static int sound_component(' + probe_src.split('static int sound_component(', 1)[1].split('static int number(', 1)[0]

        with tempfile.TemporaryDirectory() as td:
            c_p = Path(td) / 'rules.c'
            b_p = Path(td) / 'rules'
            c_p.write_text(
                '#include <assert.h>\n#include <stdio.h>\n#include <string.h>\n'
                + sp_fn + '\n' + sc_fn + '\n'
                'int main(void) {\n'
                '  assert(sound_path("A:/A6400/CHARO.PCM") == 1);\n'
                '  assert(sound_path("A:/A6400/SOUNDS/leica_m.pcm") == 1);\n'
                '  assert(sound_path("A:/A6400/SOUNDS/清脆快門.PCM") == 1);\n'
                '  assert(sound_path("A:/A6400/SOUNDS/.dot.pcm") == 0);\n'
                '  assert(sound_path("A:/A6400/SOUNDS/a;rm.pcm") == 0);\n'
                '  assert(sound_path("A:/A6400/SOUNDS/a$b.pcm") == 0);\n'
                '  assert(sound_path("A:/A6400/SOUNDS/aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa.pcm") == 0);\n'
                '  assert(sound_component("CONTEX~1.PCM") == 1);\n'
                '  assert(sound_component("清脆快門.pcm") == 1);\n'
                '  assert(sound_component("bad space.pcm") == 0);\n'
                '  puts("RULES_OK");\n'
                '  return 0;\n'
                '}\n',
                encoding='utf-8',
            )
            subprocess.run(['cc', '-std=c11', '-Wall', '-Wextra', '-Werror', str(c_p), '-o', str(b_p)], check=True)
            res = subprocess.run([str(b_p)], check=True, capture_output=True, text=True)
            self.assertIn('RULES_OK', res.stdout)

    def test_encode_sds_sound_lite_sanitization_and_inspect(self):
        import encode_sony_sds_adpcm as sds

        stem = sds.sanitize_sound_lite_stem('Leica M6 (快門聲測試超長檔名測試一定要截斷超過四十位元組)!$1')
        pcm_name = f'{stem}.pcm'
        self.assertLessEqual(len(pcm_name.encode('utf-8')), 44)
        self.assertNotIn('$', pcm_name)
        self.assertNotIn(' ', pcm_name)

        sr = sds.SAMPLE_RATE
        samples = [int(15000 * math.sin(2 * math.pi * 520 * i / sr)) for i in range(sr // 5)]
        with tempfile.TemporaryDirectory() as td:
            d = Path(td)
            wav_p = d / 'my (clip) $1.wav'
            with wave.open(str(wav_p), 'wb') as wf:
                wf.setnchannels(1)
                wf.setsampwidth(2)
                wf.setframerate(sr)
                wf.writeframes(struct.pack(f'<{len(samples)}h', *samples))
            pcm_out = d / f'{sds.sanitize_sound_lite_stem(wav_p.stem)}.pcm'
            subprocess.run(
                [sys.executable, str(TOOLS / 'encode_sony_sds_adpcm.py'), str(wav_p), '--slot', 'sound-lite', '-o', str(pcm_out)],
                check=True,
                capture_output=True,
                text=True,
            )
            self.assertTrue(pcm_out.is_file())
            insp = subprocess.run(
                [sys.executable, str(TOOLS / 'encode_sony_sds_adpcm.py'), str(pcm_out), '--inspect'],
                check=True,
                capture_output=True,
                text=True,
            )
            self.assertIn('INSPECT_SDS_OK', insp.stdout)
            self.assertIn('bad_hdr=0', insp.stdout)

    def test_author_film_tiled_blur_is_bit_exact_with_naive_reference(self):
        with tempfile.TemporaryDirectory() as td:
            c_p = Path(td) / 'blur_eq.c'
            b_p = Path(td) / 'blur_eq'
            c_p.write_text(
                '#include <assert.h>\n'
                '#include <stdio.h>\n'
                '#include <stdlib.h>\n'
                '#include <string.h>\n'
                '#include "author_film.h"\n'
                'static void naive_blur(unsigned char *mask, unsigned char *tmp, int gw, int gh) {\n'
                '  static const unsigned blur_recip[8] = {0u,65536u,32768u,21846u,16384u,13108u,10923u,9363u};\n'
                '  for (int pass = 0; pass < 2; ++pass) {\n'
                '    for (int y = 0; y < gh; ++y) {\n'
                '      for (int x = 0; x < gw; ++x) {\n'
                '        int x0 = x < 3 ? 0 : x - 3, x1 = x + 3 >= gw ? gw - 1 : x + 3, s = 0;\n'
                '        for (int k = x0; k <= x1; ++k) s += mask[y * gw + k];\n'
                '        tmp[y * gw + x] = (unsigned char)((unsigned)(s * (int)blur_recip[x1 - x0 + 1]) >> 16);\n'
                '      }\n'
                '    }\n'
                '    for (int y = 0; y < gh; ++y) {\n'
                '      int y0 = y < 3 ? 0 : y - 3, y1 = y + 3 >= gh ? gh - 1 : y + 3;\n'
                '      for (int x = 0; x < gw; ++x) {\n'
                '        int s = 0;\n'
                '        for (int k = y0; k <= y1; ++k) s += tmp[k * gw + x];\n'
                '        mask[y * gw + x] = (unsigned char)((unsigned)(s * (int)blur_recip[y1 - y0 + 1]) >> 16);\n'
                '      }\n'
                '    }\n'
                '  }\n'
                '}\n'
                'int main(void) {\n'
                '  const int dims[][2] = {{1,1},{5,3},{8,8},{64,17},{65,19},{131,37},{404,90}};\n'
                '  for (unsigned d = 0; d < sizeof(dims)/sizeof(dims[0]); ++d) {\n'
                '    int gw = dims[d][0], gh = dims[d][1];\n'
                '    size_t sz = (size_t)gw * (size_t)gh;\n'
                '    unsigned char *a = malloc(sz), *b = malloc(sz), *ta = malloc(sz), *tb = malloc(sz);\n'
                '    for (size_t i = 0; i < sz; ++i) { a[i] = b[i] = (unsigned char)((i * 73u + 19u) & 255u); }\n'
                '    film_mask_blur(a, ta, gw, gh);\n'
                '    naive_blur(b, tb, gw, gh);\n'
                '    assert(memcmp(a, b, sz) == 0);\n'
                '    free(a); free(b); free(ta); free(tb);\n'
                '  }\n'
                '  puts("BLUR_BIT_EXACT_OK");\n'
                '  return 0;\n'
                '}\n',
                encoding='utf-8',
            )
            subprocess.run(
                ['cc', '-O2', '-std=c11', '-Wall', '-Wextra', '-Werror', '-I', str(ROOT / 'camera'),
                 str(c_p), str(ROOT / 'camera/author_film.c'), '-o', str(b_p)],
                check=True,
            )
            res = subprocess.run([str(b_p)], check=True, capture_output=True, text=True)
            self.assertIn('BLUR_BIT_EXACT_OK', res.stdout)


if __name__ == '__main__':
    unittest.main()
