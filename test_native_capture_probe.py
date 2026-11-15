"""Real scanner/process tests; only the unavailable Sony monitor is replaced."""
import hashlib
import os
import re
from pathlib import Path
import subprocess
import tempfile
import threading
import time
import unittest

ROOT = Path(__file__).resolve().parent
SOURCE = ROOT/'camera/native_capture_probe.c'
MONITOR = r'''#!/usr/bin/env python3
import fnmatch,os,sys,time
from pathlib import Path
root=Path(os.environ['CAPTURE_FIXTURE']);fault=os.environ.get('CAPTURE_FAULT','')
(root/'monitor.pid').write_text(str(os.getpid()))
stream_number=0
if fault=='restart_partial_overflow':
 marker=root/'monitor-streams';stream_number=int(marker.read_text())+1 if marker.exists() else 1
 marker.write_text(str(stream_number))
items=[];index=0
print('### monio start ###\nMON>',flush=True)
if fault=='startup_recorded_history':
 data=(root/'recorded-history').read_bytes()
 for i in range(0,len(data),384):
  time.sleep(.008);sys.stdout.buffer.write(data[i:i+384]);sys.stdout.buffer.flush()
if fault in ('startup_late_history','startup_stale_reply'):
 for i in range(3):
  time.sleep(.04);print('[DCF] historic native diagnostic',flush=True)
 if fault=='startup_stale_reply':
  print('MprFileIf : FFirst [pathname]\nNo more Files.\nec = 0x00001008\nMON>',flush=True)
 (root/'last-startup-noise').write_text(str(time.monotonic()))
if fault=='startup_never_quiet':
 for i in range(45):
  print('[MSV] startup diagnostic',flush=True);time.sleep(.03)
for command in sys.stdin:
 (root/'command-at').write_text(str(time.monotonic()))
 args=command.strip().split();verb=args[2]
 header='MprFileIf : '+('FFirst [pathname]' if verb=='FFirst' else "FNext ['c' to cancel]")
 if fault=='restart_partial_overflow' and stream_number==2:print('FLOW')
 if fault=='retry_recorded' and verb=='FFirst' and args[3]=='ms:/DCIM/*':
  items=[('10061115',True,0)];index=0
  sys.stdout.buffer.write((root/'recorded-retry').read_bytes());sys.stdout.buffer.flush();continue
 if fault=='prompt_recorded_async' and verb=='FFirst' and args[3]=='ms:/DCIM/10061115/*':
  items=[('A6409683.JPG',False,10092544)];index=0
  sys.stdout.buffer.write((root/'recorded-async').read_bytes());sys.stdout.buffer.flush();continue
 if fault=='prompt_recorded_joined_async' and verb=='FNext' and index+1>=len(items):
  index+=1
  sys.stdout.buffer.write((root/'recorded-joined-async').read_bytes());sys.stdout.buffer.flush();continue
 if fault=='prompt_recorded_space_async' and verb=='FFirst' and args[3]=='ms:/DCIM/*':
  items=[('10061115',True,0)];index=0
  sys.stdout.buffer.write((root/'recorded-space-async').read_bytes());sys.stdout.buffer.flush();continue
 if fault in ('preamble_noise','fragmented_header'):
  print(('[DCF] ChangeRecDir 10061115 [MSV] RcsBackup [CSS] state\n')*400,flush=True)
 if fault=='preamble_capacity':print('x'*140000+'\n',flush=True);continue
 if fault=='preamble_overflow':print('###OVERFLOW###',flush=True)
 if fault=='overlong_header_suffix':
  print('x'*9000+header+'\nNo more Files.\nec = 0x00001008\nMON>',flush=True);continue
 if fault=='header_substring_only':
  print('diagnostic mentions '+header+'\nNo more Files.\nec = 0x00001008\nMON>',flush=True);continue
 if fault=='fragmented_header':
  sys.stdout.write(header[:11]);sys.stdout.flush();time.sleep(.02)
  print(header[11:])
 else:
  print(header)
 if fault in ('retry_success','retry_missing_success','retry_missing_metadata') and verb=='FFirst':print('FindFirst Retry.')
 if fault=='retry_wrong_verb':print('FindNext Retry.')
 if fault=='retry_unknown_text':print('FindFirst Retry..')
 if fault=='retry_duplicate':print('FindFirst Retry.\nFindFirst Retry.')
 if fault.startswith('status_'):
  replies={
   'status_mixed_end':'No more Files.\nec = 0x00001008\nec = 0x00001005',
   'status_prefix_end':'No more Files.\nec = 0x000010080',
   'status_duplicate_end':'No more Files.\nec = 0x00001008\nec = 0x00001008',
   'status_end_metadata':'No more Files.\nec = 0x00001008\nA6400001.JPG\nkind : FILE\nfile length = 1\nmodification time = 1 : 2 : 3\nFindFirst OK.',
   'status_mixed_unmounted':'[AVIMMGR][MS1]GetDeviceInfo : ER_SEQUENCE  DEVICE Unmounted\nec = 0x0000100B\nec = 0x00001005',
   'status_unmounted_metadata':'[AVIMMGR][MS1]GetDeviceInfo : ER_SEQUENCE  DEVICE Unmounted\nec = 0x0000100B\nA6400001.JPG\nkind : FILE\nfile length = 1\nmodification time = 1 : 2 : 3',
   'status_unmounted_prefix':'[AVIMMGR][MS1]GetDeviceInfo : ER_SEQUENCE  DEVICE Unmounted\nec = 0x0000100B0',
   'status_duplicate_success':'10061115\nkind : DIR\nfile length = 0\nmodification time = 1 : 2 : 3\nFindFirst OK.\nFindFirst OK.',
   'status_opposite_success':'10061115\nkind : DIR\nfile length = 0\nmodification time = 1 : 2 : 3\nFindFirst OK.\nFindNext OK.',
   'status_retry_end':'FindFirst Retry.\nNo more Files.\nec = 0x00001008',
   'status_retry_end_wrong_verb':'FindNext Retry.\nNo more Files.\nec = 0x00001008',
   'status_retry_end_duplicate':'FindFirst Retry.\nFindFirst Retry.\nNo more Files.\nec = 0x00001008',
   'status_retry_end_late':'No more Files.\nFindFirst Retry.\nec = 0x00001008',
   'status_retry_unmounted':'FindFirst Retry.\n[AVIMMGR][MS1]GetDeviceInfo : ER_SEQUENCE  DEVICE Unmounted\nec = 0x0000100B',
   'status_retry_unmounted_wrong_verb':'FindNext Retry.\n[AVIMMGR][MS1]GetDeviceInfo : ER_SEQUENCE  DEVICE Unmounted\nec = 0x0000100B',
   'status_retry_unmounted_duplicate':'FindFirst Retry.\nFindFirst Retry.\n[AVIMMGR][MS1]GetDeviceInfo : ER_SEQUENCE  DEVICE Unmounted\nec = 0x0000100B',
   'status_retry_unmounted_late':'[AVIMMGR][MS1]GetDeviceInfo : ER_SEQUENCE  DEVICE Unmounted\nFindFirst Retry.\nec = 0x0000100B',
  }
  print(replies[fault]+'\nMON>',flush=True);continue
 if verb=='FFirst' and args[3]=='ms:/DCIM/*' and ((root/'media-unmounted').exists() or fault in ('media_unmounted','unmounted_wrong_code')):
  print('[MMGR][AVI] ## ERROR(MMGR/FileIf):0x0EF10001 ## avimmgr/AviMMgrFileIf.cpp(588) FindFirst()')
  print('[AVIMMGR][MS1]GetDeviceInfo : ER_SEQUENCE  DEVICE Unmounted')
  print('ec = '+('0x00001005' if fault=='unmounted_wrong_code' else '0x0000100B'))
  print('MON>',flush=True);continue
 if fault=='overflow':
  print('native\x1b[31m'+('x'*600)+'OVERFLOW',flush=True);continue
 if fault=='response_capacity':
  print('x'*9000,flush=True);continue
 if verb=='FFirst':
  pattern=args[3][4:];parent,glob=pattern.rsplit('/',1)
  p=root/parent.lstrip('/')
  items=[]
  for x in sorted(p.iterdir()) if p.is_dir() else []:
   if not fnmatch.fnmatch(x.name,glob):continue
   try:st=x.stat()
   except FileNotFoundError:continue
   items.append((x.name,x.is_dir(),st.st_size))
  index=0
 elif len(args)>3 and args[3]=='c':
  items=[];index=0;print('FindNext Fin.\nMON>',flush=True);continue
 else:index+=1
 if index>=len(items):print('No more Files.\nec = 0x00001008')
 elif fault in ('native_error','startup_stale_reply','startup_recorded_history'):print('ec = 0x00001005')
 else:
  filename,is_dir,size=items[index];name='../ESCAPE.JPG' if fault=='unsafe_name' else filename
  print(name)
  if fault=='retry_after_name':print('FindFirst Retry.')
  if fault=='retry_unknown_after_name':print('FindFirst Retry..')
  print('kind           : '+('DIR' if is_dir else 'FILE'))
  print('file length = '+('-1' if fault=='bad_size' else str(size)))
  if fault!='retry_missing_metadata':print('modification time = 15 : 36 : 16')
  if fault!='retry_missing_success':print('FindFirst OK.' if verb=='FFirst' else 'FindNext OK.')
 if fault=='embedded_prompt_then_error':
  print('diagnostic payload_MON>_is_not_a_prompt\nec = 0x00001005\nMON>',flush=True)
 elif fault=='embedded_prompt':print('diagnostic payload_MON>_is_not_a_prompt',flush=True)
 elif fault.startswith('prompt_joined'):
  sys.stdout.flush()
  data=(root/'joined-prompt').read_bytes()
  if fault=='prompt_joined_bytewise':
   for value in data:
    sys.stdout.buffer.write(bytes([value]));sys.stdout.buffer.flush();time.sleep(.001)
  elif fault=='prompt_joined_delayed_partial':
   sys.stdout.buffer.write(data[:9]);sys.stdout.buffer.flush();time.sleep(.06)
   sys.stdout.buffer.write(data[9:]);sys.stdout.buffer.flush()
  elif fault=='prompt_joined_split_overflow':
   sys.stdout.buffer.write(data[:-5]);sys.stdout.buffer.flush();time.sleep(.005)
   sys.stdout.buffer.write(data[-5:]);sys.stdout.buffer.flush()
  else:sys.stdout.buffer.write(data);sys.stdout.buffer.flush()
  if fault=='prompt_joined_eof':sys.exit(0)
 elif fault=='prompt_suffix':print('MON>still busy/error',flush=True)
 elif fault=='prompt_delayed_suffix':
  sys.stdout.write('MON>');sys.stdout.flush();time.sleep(.005)
  print('still busy/error',flush=True)
 elif fault=='prompt_newline_async':
  print('MON>\r\n[CACHE]BUFFER SIZE TOO SMALL\n[MprIf]CallbackEnterStillPb',flush=True)
 elif fault=='prompt_split_newline_async':
  sys.stdout.write('MON>\r');sys.stdout.flush();time.sleep(.005)
  print('\n[CACHE]BUFFER SIZE TOO SMALL\n[MprIf]CallbackEnterStillPb',flush=True)
 elif fault=='prompt_async_next_block':
  print('MON>',flush=True);time.sleep(.005)
  print('[CACHE]BUFFER SIZE TOO SMALL\n[MprIf]CallbackEnterStillPb',flush=True)
 elif fault=='prompt_newline_overflow':print('MON>\r\n[CACHE]BUFFER SIZE TOO SMALL\nOVERFLOW',flush=True)
 elif fault=='prompt_split_overflow':
  sys.stdout.write('MON>\nOVER');sys.stdout.flush();time.sleep(.005)
  print('FLOW',flush=True)
 elif fault=='restart_partial_overflow' and stream_number==1:
  sys.stdout.write('MON>\nOVER');sys.stdout.flush()
 elif fault=='prompt_no_newline':sys.stdout.write('MON>');sys.stdout.flush()
 elif fault!='no_prompt':print('MON>',flush=True)
 else:sys.stdout.flush()
'''

def fnv(data):
    h=2166136261
    for b in data:h=((h^b)*16777619)&0xffffffff
    return h

class CaptureProbeTests(unittest.TestCase):
    def setUp(self):
        self.assertTrue(SOURCE.is_file(),'native read-only new-JPEG observer is missing')
        self.tmp=tempfile.TemporaryDirectory();self.addCleanup(self.tmp.cleanup)
        self.base=Path(self.tmp.name);self.dcim=self.base/'DCIM/10061115';self.dcim.mkdir(parents=True)
        (self.dcim/'A6400001.JPG').write_bytes(b'baseline JPEG stand-in')
        self.monitor=self.base/'monitor';self.monitor.write_text(MONITOR);self.monitor.chmod(0o700)
        header=self.base/'capture_probe_build.h'
        header.write_text(f'#define CAPTURE_MONITOR "{self.monitor}"\n'
                          f'#define CAPTURE_LOCK "{self.base}/capture.lock"\n'
                          f'#define CAPTURE_STOP_FILE "{self.base}/stop"\n'
                          f'#define CAPTURE_MONITOR_BYTES {self.monitor.stat().st_size}u\n'
                          f'#define CAPTURE_MONITOR_FNV 0x{fnv(self.monitor.read_bytes()):08x}u\n'
                          '#define CAPTURE_STARTUP_MS 500u\n#define CAPTURE_COMMAND_MS 1000u\n'
                          '#define CAPTURE_QUIET_MS 50u\n#define CAPTURE_STARTUP_MAX_MS 1000u\n'
                          '#define CAPTURE_PROMPT_QUIET_MS 20u\n'
                          '#define CAPTURE_WAIT_MS 40u\n#define CAPTURE_ACTIVE_MS 750u\n'
                          '#define CAPTURE_INTERVAL_MS 25u\n#define CAPTURE_STABLE_MS 120u\n')
        self.binary=self.base/'capture'
        subprocess.run(['cc','-std=c11','-O2','-Wall','-Wextra','-Werror','-I',str(self.base),
                        str(SOURCE),'-o',str(self.binary)],check=True,capture_output=True)

    def run_probe(self,mode='scan',fault='',events=()):
        env=dict(os.environ,CAPTURE_FIXTURE=str(self.base),CAPTURE_FAULT=fault)
        p=subprocess.Popen([str(self.binary),mode],stdout=subprocess.PIPE,stderr=subprocess.PIPE,env=env,text=True)
        lines=[];baseline=threading.Event();cancel=threading.Event()
        def consume():
            for line in p.stdout:
                lines.append(line)
                if line.startswith('CAPTURE_BASELINE '):baseline.set()
        reader=threading.Thread(target=consume);reader.start();threads=[]
        for delay,action in events:
            def act(delay=delay,action=action):
                if baseline.wait(3) and not cancel.wait(delay):action()
            t=threading.Thread(target=act);t.start();threads.append(t)
        try:
            p.wait(timeout=5)
        finally:
            if p.poll() is None:p.kill();p.wait()
            cancel.set()
            for t in threads:t.join()
            reader.join()
            stderr=p.stderr.read()
            p.stdout.close()
            p.stderr.close()
        return p.returncode,''.join(lines)+stderr

    def test_scan_inventory_has_real_native_path(self):
        rc,out=self.run_probe();self.assertEqual(rc,0,out)
        self.assertIn('ms:/DCIM/10061115/A6400001.JPG',out)
        self.assertNotIn('CAPTURE_CANDIDATE',out)

    def test_matching_ffirst_retry_before_entry_keeps_complete_metadata(self):
        rc,out=self.run_probe(fault='retry_success')
        self.assertEqual(rc,0,out)
        self.assertIn('ms:/DCIM/10061115/A6400001.JPG',out)
        self.assertNotIn('CAPTURE_DIAG ',out)


    def test_retry_wrong_verb_unknown_duplicate_or_after_name_is_rejected(self):
        for fault in ('retry_wrong_verb','retry_unknown_text','retry_duplicate','retry_after_name','retry_unknown_after_name'):
            with self.subTest(fault=fault):
                rc,out=self.run_probe(fault=fault)
                self.assertNotEqual(rc,0,out)
                self.assertNotIn('CAPTURE_BASELINE ',out)

    def test_valid_ffirst_retry_can_precede_strict_end_or_unmounted_outcome(self):
        rc,out=self.run_probe(fault='status_retry_end')
        self.assertEqual(rc,0,out)
        self.assertIn('CAPTURE_BASELINE count=0 ',out)
        rc,out=self.run_probe(fault='status_retry_unmounted')
        self.assertNotEqual(rc,0,out)
        self.assertEqual(self.diagnostic(out)['reason'],'baseline_media_unavailable',out)
        self.assertNotIn('CAPTURE_BASELINE ',out)

    def test_invalid_retry_is_rejected_before_end_or_unmounted_status(self):
        for outcome in ('end','unmounted'):
            for kind in ('wrong_verb','duplicate','late'):
                with self.subTest(outcome=outcome,kind=kind):
                    rc,out=self.run_probe(fault=f'status_retry_{outcome}_{kind}')
                    self.assertNotEqual(rc,0,out)
                    self.assertEqual(self.diagnostic(out)['reason'],'invalid_retry',out)
                    self.assertNotIn('CAPTURE_BASELINE ',out)

    def test_retry_does_not_replace_success_marker_or_metadata(self):
        for fault in ('retry_missing_success','retry_missing_metadata'):
            with self.subTest(fault=fault):
                rc,out=self.run_probe(fault=fault)
                self.assertNotEqual(rc,0,out)
                self.assertNotIn('CAPTURE_BASELINE ',out)

    def test_existing_jpeg_is_never_reported_as_new(self):
        rc,out=self.run_probe('watch');self.assertEqual(rc,0,out)
        self.assertNotIn('CAPTURE_CANDIDATE',out)

    def test_new_stable_jpeg_is_reported_exactly_once(self):
        new=self.dcim/'A6400002.JPG'
        rc,out=self.run_probe('watch',events=[(.25,lambda:new.write_bytes(b'new photo'))])
        self.assertEqual(rc,0,out);self.assertEqual(out.count('CAPTURE_CANDIDATE'),1,out)
        self.assertIn('ms:/DCIM/10061115/A6400002.JPG',out)

    def test_changing_size_restarts_stability_window(self):
        new=self.dcim/'A6400002.JPG'
        rc,out=self.run_probe('watch',events=[(.25,lambda:new.write_bytes(b'a')),
                       (.31,lambda:new.write_bytes(b'ab')),(.37,lambda:new.write_bytes(b'abcde'))])
        self.assertEqual(rc,0,out);self.assertEqual(out.count('CAPTURE_CANDIDATE'),1,out)
        self.assertIn('size=5',out)

    def test_disappearing_candidate_is_not_reported(self):
        new=self.dcim/'A6400002.JPG'
        rc,out=self.run_probe('watch',events=[(.25,lambda:new.write_bytes(b'new')),(.31,new.unlink)])
        self.assertEqual(rc,0,out);self.assertNotIn('CAPTURE_CANDIDATE',out)

    def test_non_jpeg_is_ignored(self):
        (self.dcim/'A6400002.ARW').write_bytes(b'raw')
        rc,out=self.run_probe();self.assertEqual(rc,0,out);self.assertNotIn('.ARW',out)

    def test_invalid_metadata_stops(self):
        for fault in ('bad_size','unsafe_name','native_error','no_prompt'):
            with self.subTest(fault=fault):
                rc,out=self.run_probe(fault=fault);self.assertNotEqual(rc,0,out)
                self.assertNotIn('CAPTURE_CANDIDATE',out)

    def diagnostic(self,out,command='MPR FileIf FFirst ms:/DCIM/*',sequence='1'):
        lines=[line for line in out.splitlines() if line.startswith('CAPTURE_DIAG ')]
        self.assertEqual(len(lines),1,out)
        fields=dict(part.split('=',1) for part in lines[0].split()[1:])
        self.assertLessEqual(len(fields['response_tail_hex']),768,out)
        self.assertEqual(bytes.fromhex(fields['command_hex']).decode(),command)
        self.assertEqual(fields['command_seq'],sequence,out)
        self.assertRegex(fields['command_elapsed_ms'],r'^\d+$')
        self.assertFalse((self.base/'capture.lock').exists())
        pid=int((self.base/'monitor.pid').read_text())
        with self.assertRaises(ProcessLookupError):os.kill(pid,0)
        return fields

    def test_native_status_failure_preserves_response_and_command(self):
        rc,out=self.run_probe(fault='native_error');self.assertNotEqual(rc,0,out)
        fields=self.diagnostic(out)
        self.assertEqual(fields['reason'],'native_status',out)
        self.assertIn(b'ec = 0x00001005',bytes.fromhex(fields['response_tail_hex']))
        self.assertNotIn('CAPTURE_CANDIDATE',out)

    def test_missing_prompt_reports_timeout_and_partial_header(self):
        rc,out=self.run_probe(fault='no_prompt');self.assertNotEqual(rc,0,out)
        fields=self.diagnostic(out)
        self.assertEqual(fields['reason'],'command_timeout',out)
        self.assertEqual(fields['header_seen'],'1',out)
        self.assertEqual(fields['prompt_after_header'],'0',out)
        self.assertIn(b'FindFirst OK.',bytes.fromhex(fields['response_tail_hex']))

    def test_monitor_overflow_diagnostic_is_bounded_and_escaped(self):
        rc,out=self.run_probe(fault='overflow');self.assertNotEqual(rc,0,out)
        fields=self.diagnostic(out)
        self.assertEqual(fields['reason'],'ring_overflow',out)
        self.assertIn(b'OVERFLOW',bytes.fromhex(fields['response_tail_hex']))
        self.assertNotIn('\x1b',out)
        self.assertLess(len(out),2500)

    def test_excess_response_is_distinguished_from_timeout(self):
        rc,out=self.run_probe(fault='response_capacity');self.assertNotEqual(rc,0,out)
        fields=self.diagnostic(out)
        self.assertEqual(fields['reason'],'response_capacity',out)
        self.assertGreater(int(fields['response_bytes']),7000,out)

    def test_large_preamble_noise_does_not_consume_metadata_capacity(self):
        rc,out=self.run_probe(fault='preamble_noise')
        self.assertEqual(rc,0,out)
        self.assertIn('ms:/DCIM/10061115/A6400001.JPG',out)
        self.assertNotIn('CAPTURE_DIAG',out)

    def test_header_can_be_split_after_large_bounded_preamble(self):
        rc,out=self.run_probe(fault='fragmented_header')
        self.assertEqual(rc,0,out)
        self.assertIn('ms:/DCIM/10061115/A6400001.JPG',out)

    def test_preamble_header_substring_and_overlong_suffix_are_not_frames(self):
        for fault in ('header_substring_only','overlong_header_suffix'):
            with self.subTest(fault=fault):
                rc,out=self.run_probe(fault=fault)
                self.assertNotEqual(rc,0,out)
                fields=self.diagnostic(out)
                self.assertEqual(fields['reason'],'command_timeout',out)
                self.assertEqual(fields['header_seen'],'0',out)

    def test_embedded_prompt_does_not_complete_native_command(self):
        rc,out=self.run_probe(fault='embedded_prompt')
        self.assertNotEqual(rc,0,out)
        fields=self.diagnostic(out)
        self.assertEqual(fields['reason'],'command_timeout',out)
        self.assertEqual(fields['header_seen'],'1',out)
        self.assertEqual(fields['prompt_after_header'],'0',out)

    def test_embedded_prompt_cannot_hide_native_error_before_framed_prompt(self):
        rc,out=self.run_probe(fault='embedded_prompt_then_error')
        self.assertNotEqual(rc,0,out)
        fields=self.diagnostic(out)
        self.assertEqual(fields['reason'],'native_status',out)
        self.assertEqual(fields['prompt_after_header'],'1',out)
        self.assertIn(b'ec = 0x00001005',bytes.fromhex(fields['response_tail_hex']))

    def test_complete_prompt_ends_reply_before_arbitrary_background_text(self):
        for fault in ('prompt_suffix','prompt_delayed_suffix'):
            with self.subTest(fault=fault):
                rc,out=self.run_probe(fault=fault)
                self.assertEqual(rc,0,out)
                self.assertIn('path=ms:/DCIM/10061115/A6400001.JPG',out)
                self.assertNotIn('CAPTURE_DIAG ',out)

    def test_bare_prompt_needs_no_newline_to_complete(self):
        rc,out=self.run_probe(fault='prompt_no_newline')
        self.assertEqual(rc,0,out)
        self.assertIn('ms:/DCIM/10061115/A6400001.JPG',out)

    def test_newline_delimited_prompt_allows_async_diagnostics(self):
        for fault in ('prompt_newline_async','prompt_split_newline_async','prompt_async_next_block'):
            with self.subTest(fault=fault):
                rc,out=self.run_probe(fault=fault)
                self.assertEqual(rc,0,out)
                self.assertIn('ms:/DCIM/10061115/A6400001.JPG',out)
                self.assertNotIn('CAPTURE_DIAG ',out)

    def test_newline_delimited_prompt_does_not_hide_overflow(self):
        for fault in ('prompt_newline_overflow','prompt_split_overflow'):
            with self.subTest(fault=fault):
                rc,out=self.run_probe(fault=fault)
                self.assertNotEqual(rc,0,out)
                self.assertIn('reason=ring_overflow ',out)
                self.assertNotIn('CAPTURE_BASELINE ',out)




    def test_prompt_token_survives_bytewise_reads_and_later_background_text(self):
        for fault in ('prompt_joined','prompt_joined_bytewise','prompt_joined_delayed_partial'):
            with self.subTest(fault=fault):
                (self.base/'joined-prompt').write_bytes(b'MON>MprIf::MakeWriteStillImage() \r\n[MSL] native diagnostic\n')
                rc,out=self.run_probe(fault=fault)
                self.assertEqual(rc,0,out)
                self.assertIn('path=ms:/DCIM/10061115/A6400001.JPG',out)
                self.assertNotIn('CAPTURE_DIAG ',out)

    def test_background_suffix_contents_do_not_change_completed_reply(self):
        for suffix in (b'MprIf::Other() \n',b'MprIf::MakeWriteStillImage()\n',
                       b'MprIf::MakeWriteStillImage() extra\n',b'MprIf::MakeWriteStillImage() \rX\n',
                       b'MprIf::MakeWrite\n',b'MprIf::Make\rWriteStillImage() \n',
                       b'MprIf::MakeWriteStillImage() '+b'x'*9000+b'\n'):
            with self.subTest(suffix=suffix[:60]):
                (self.base/'joined-prompt').write_bytes(b'MON>'+suffix)
                rc,out=self.run_probe(fault='prompt_joined')
                self.assertEqual(rc,0,out)
                self.assertIn('path=ms:/DCIM/10061115/A6400001.JPG',out)
                self.assertNotIn('CAPTURE_DIAG ',out)

    def test_incomplete_background_line_does_not_delay_complete_prompt(self):
        for suffix in (b'MprIf::Make',b'MprIf::MakeWriteStillImage() '):
            with self.subTest(suffix=suffix):
                (self.base/'joined-prompt').write_bytes(b'MON>'+suffix)
                rc,out=self.run_probe(fault='prompt_joined')
                self.assertEqual(rc,0,out)
                self.assertIn('path=ms:/DCIM/10061115/A6400001.JPG',out)

    def test_partial_prompt_eof_is_not_completion(self):
        for partial in (b'M',b'MO',b'MON'):
            with self.subTest(partial=partial):
                (self.base/'joined-prompt').write_bytes(partial)
                rc,out=self.run_probe(fault='prompt_joined_eof')
                self.assertNotEqual(rc,0,out)
                self.assertEqual(self.diagnostic(out)['reason'],'read_eof',out)
                self.assertNotIn('CAPTURE_BASELINE ',out)

    def test_partial_or_noncontiguous_prompt_does_not_complete_after_quiet(self):
        for partial in (b'M',b'MO',b'MON',b'MONX',b'MON\r>',b'MO\nN>'):
            with self.subTest(partial=partial):
                (self.base/'joined-prompt').write_bytes(partial)
                rc,out=self.run_probe(fault='prompt_joined')
                self.assertNotEqual(rc,0,out)
                self.assertEqual(self.diagnostic(out)['reason'],'command_timeout',out)
                self.assertNotIn('CAPTURE_BASELINE ',out)

    def test_joined_async_prompt_cannot_hide_overflow_in_same_or_next_read(self):
        for fault in ('prompt_joined','prompt_joined_split_overflow'):
            with self.subTest(fault=fault):
                (self.base/'joined-prompt').write_bytes(b'MON>MprIf::MakeWriteStillImage() \r\nOVERFLOW\n')
                rc,out=self.run_probe(fault=fault)
                self.assertNotEqual(rc,0,out)
                command='MPR FileIf FNext' if fault=='prompt_joined_split_overflow' else 'MPR FileIf FFirst ms:/DCIM/*'
                sequence='2' if fault=='prompt_joined_split_overflow' else '1'
                self.assertEqual(self.diagnostic(out,command,sequence)['reason'],'ring_overflow',out)
                self.assertNotIn('CAPTURE_BASELINE ',out)

    def test_joined_async_prompt_does_not_hide_error_or_mixed_strict_end(self):
        suffix=b'MON>MprIf::MakeWriteStillImage() \r\n[MSL] native diagnostic\n'
        for status in (b'ec = 0x00001005\r\n',b'ec = 0x00001008\r\nec = 0x00001005\r\n'):
            with self.subTest(status=status):
                response=b"MprFileIf : FNext ['c' to cancel]\r\nNo more Files.\r\n"+status+suffix
                (self.base/'recorded-joined-async').write_bytes(response)
                rc,out=self.run_probe(fault='prompt_recorded_joined_async')
                self.assertNotEqual(rc,0,out)
                self.assertEqual(self.diagnostic(out,'MPR FileIf FNext','2')['reason'],'native_status',out)
                self.assertNotIn('CAPTURE_BASELINE ',out)

    def test_overflow_prefix_does_not_cross_new_monitor_stream(self):
        harness=self.base/'stream-restart.c'
        harness.write_text('#define main capture_probe_application_main\n'
                           f'#include "{SOURCE}"\n#undef main\n'+r'''
int main(void) {
    struct transport t={.pid=-1,.in=-1,.out=-1};char response[RESPONSE_BYTES];int result=1;
    setvbuf(stdout,NULL,_IONBF,0);signal(SIGPIPE,SIG_IGN);
    if(transport_start(&t))goto done;
    t.scan_started=now_ms();
    if(exchange(&t,"MPR FileIf FFirst ms:/DCIM/*","MprFileIf : FFirst [pathname]",response,sizeof(response)))goto done;
    if(t.overflow_match!=4u){printf("HARNESS_FAIL expected_partial_overflow\n");goto done;}
    transport_stop(&t);
    if(transport_start(&t))goto done;
    t.scan_started=now_ms();
    if(exchange(&t,"MPR FileIf FFirst ms:/DCIM/*","MprFileIf : FFirst [pathname]",response,sizeof(response)))goto done;
    printf("CAPTURE_STREAM_RESTART_OK\n");result=0;
done:
    transport_stop(&t);return result;
}
''')
        binary=self.base/'stream-restart'
        subprocess.run(['cc','-std=c11','-O2','-Wall','-Wextra','-Werror','-I',str(self.base),
                        str(harness),'-o',str(binary)],check=True,capture_output=True)
        env=dict(os.environ,CAPTURE_FIXTURE=str(self.base),CAPTURE_FAULT='restart_partial_overflow')
        result=subprocess.run([str(binary)],env=env,text=True,capture_output=True,timeout=5)
        out=result.stdout+result.stderr
        self.assertEqual(result.returncode,0,out)
        self.assertIn('CAPTURE_STREAM_RESTART_OK',out)
        self.assertEqual((self.base/'monitor-streams').read_text(),'2')
        self.assertNotIn('CAPTURE_DIAG ',out)
        pid=int((self.base/'monitor.pid').read_text())
        with self.assertRaises(ProcessLookupError):os.kill(pid,0)

    def test_normal_end_status_must_be_exact_unique_and_exclusive(self):
        for fault in ('status_mixed_end','status_prefix_end','status_duplicate_end','status_end_metadata'):
            with self.subTest(fault=fault):
                rc,out=self.run_probe(fault=fault)
                self.assertNotEqual(rc,0,out)
                self.assertEqual(self.diagnostic(out)['reason'],'native_status',out)
                self.assertNotIn('CAPTURE_BASELINE ',out)

    def test_unmounted_status_must_be_exact_unique_and_exclusive(self):
        for fault in ('status_mixed_unmounted','status_unmounted_metadata','status_unmounted_prefix'):
            with self.subTest(fault=fault):
                rc,out=self.run_probe(fault=fault)
                self.assertNotEqual(rc,0,out)
                self.assertEqual(self.diagnostic(out)['reason'],'native_status',out)
                self.assertNotIn('baseline_media_unavailable',out)
                self.assertNotIn('CAPTURE_BASELINE ',out)

    def test_success_marker_must_be_unique_and_match_command_verb(self):
        for fault in ('status_duplicate_success','status_opposite_success'):
            with self.subTest(fault=fault):
                rc,out=self.run_probe(fault=fault)
                self.assertNotEqual(rc,0,out)
                self.assertEqual(self.diagnostic(out)['reason'],'native_status',out)
                self.assertNotIn('CAPTURE_BASELINE ',out)

    def test_preamble_has_its_own_bounded_capacity(self):
        rc,out=self.run_probe(fault='preamble_capacity')
        self.assertNotEqual(rc,0,out)
        fields=self.diagnostic(out)
        self.assertEqual(fields['reason'],'preamble_capacity',out)
        self.assertEqual(fields['header_seen'],'0',out)
        self.assertGreater(int(fields['preamble_bytes']),131072,out)

    def test_overflow_after_send_is_fatal_even_before_header(self):
        rc,out=self.run_probe(fault='preamble_overflow')
        self.assertNotEqual(rc,0,out)
        self.assertEqual(self.diagnostic(out)['reason'],'ring_overflow',out)

    def test_startup_waits_for_quiet_after_late_native_history(self):
        rc,out=self.run_probe(fault='startup_late_history')
        self.assertEqual(rc,0,out)
        sent=float((self.base/'command-at').read_text())
        noise=float((self.base/'last-startup-noise').read_text())
        self.assertGreaterEqual(sent-noise,.045)

    def test_startup_stale_complete_ffirst_is_drained_not_used_as_baseline(self):
        rc,out=self.run_probe(fault='startup_stale_reply')
        self.assertNotEqual(rc,0,out)
        fields=self.diagnostic(out)
        self.assertEqual(fields['reason'],'native_status',out)
        self.assertEqual(fields['command_seq'],'1',out)
        self.assertNotIn('CAPTURE_BASELINE ',out)


    def test_continuously_busy_startup_refuses_before_any_command(self):
        rc,out=self.run_probe(fault='startup_never_quiet')
        self.assertNotEqual(rc,0,out)
        self.assertFalse((self.base/'command-at').exists())
        self.assertNotIn('CAPTURE_DIAG ',out)

    def test_unmounted_media_recovers_without_duplicate_candidate(self):
        new=self.dcim/'A6400002.JPG';flag=self.base/'media-unmounted'
        rc,out=self.run_probe('watch',events=[(.12,lambda:new.write_bytes(b'photo')),
                       (.40,lambda:flag.write_text('unmounted')),(.54,flag.unlink)])
        self.assertEqual(rc,0,out)
        self.assertEqual(out.count('CAPTURE_CANDIDATE '),1,out)
        self.assertEqual(out.count('CAPTURE_MEDIA_WAIT '),1,out)
        self.assertEqual(out.count('CAPTURE_MEDIA_RESUME '),1,out)
        self.assertIn('reason=expired',out)
        self.assertIn('media_resumes=1',out)

    def test_unavailable_interval_resets_unreported_stability(self):
        new=self.dcim/'A6400002.JPG';flag=self.base/'media-unmounted'
        rc,out=self.run_probe('watch',events=[(.25,lambda:new.write_bytes(b'photo')),
                       (.30,lambda:flag.write_text('unmounted')),(.70,flag.unlink)])
        self.assertEqual(rc,0,out)
        self.assertIn('CAPTURE_MEDIA_RESUME ',out)
        self.assertNotIn('CAPTURE_CANDIDATE ',out)

    def test_unmounted_media_expiry_is_incomplete_and_logs_once(self):
        flag=self.base/'media-unmounted'
        rc,out=self.run_probe('watch',events=[(.25,lambda:flag.write_text('unmounted'))])
        self.assertEqual(rc,21,out)
        self.assertIn('reason=media_unavailable_expired',out)
        self.assertEqual(out.count('CAPTURE_MEDIA_WAIT '),1,out)
        self.assertEqual(out.count('CAPTURE_DIAG '),1,out)
        self.assertIn('media_unavailable=1',out)
        self.assertFalse((self.base/'capture.lock').exists())
        pid=int((self.base/'monitor.pid').read_text())
        with self.assertRaises(ProcessLookupError):os.kill(pid,0)

    def test_owner_stop_while_unmounted_does_not_claim_success(self):
        flag=self.base/'media-unmounted'
        rc,out=self.run_probe('watch',events=[(.25,lambda:flag.write_text('unmounted')),
                       (.50,lambda:(self.base/'stop').write_text('stop'))])
        self.assertEqual(rc,21,out)
        self.assertIn('reason=stop_file',out)
        self.assertIn('media_unavailable=1',out)

    def test_unmounted_label_with_wrong_status_is_fatal(self):
        rc,out=self.run_probe('watch',fault='unmounted_wrong_code')
        self.assertEqual(rc,1,out)
        self.assertNotIn('CAPTURE_MEDIA_WAIT ',out)
        self.assertIn('reason=native_status',out)

    def test_unmounted_baseline_is_not_accepted_as_empty_card(self):
        rc,out=self.run_probe('watch',fault='media_unmounted')
        self.assertEqual(rc,1,out)
        self.assertIn('CAPTURE_FAIL baseline',out)
        self.assertNotIn('CAPTURE_BASELINE ',out)
        self.assertNotIn('CAPTURE_ACTIVE ',out)

    def test_catalogue_capacity_is_not_silently_truncated(self):
        for i in range(2,131):(self.dcim/f'A640{i:04d}.JPG').write_bytes(b'jpeg')
        rc,out=self.run_probe();self.assertNotEqual(rc,0,out)

    def test_wrong_monitor_pin_refuses_execution(self):
        self.monitor.write_text(MONITOR+'\n# changed\n')
        rc,out=self.run_probe();self.assertNotEqual(rc,0,out)
        self.assertFalse((self.base/'monitor.pid').exists())

    def test_monitor_child_is_reaped_after_scan(self):
        rc,out=self.run_probe();self.assertEqual(rc,0,out)
        pid=int((self.base/'monitor.pid').read_text())
        with self.assertRaises(ProcessLookupError):os.kill(pid,0)

    def test_watch_records_snapshot_times_and_expiry(self):
        rc,out=self.run_probe('watch');self.assertEqual(rc,0,out)
        samples=re.findall(r'CAPTURE_SNAPSHOT seq=(\d+) uptime_ms=(\d+) elapsed_ms=(\d+) count=(\d+)',out)
        self.assertGreaterEqual(len(samples),2,out)
        samples=[tuple(map(int,s)) for s in samples]
        self.assertEqual([s[0] for s in samples],list(range(1,len(samples)+1)))
        self.assertTrue(all(b[1]>=a[1] and b[2]>=a[2] for a,b in zip(samples,samples[1:])),out)
        self.assertTrue(all(s[3]==1 for s in samples),out)
        self.assertRegex(out,r'CAPTURE_STOP .*reason=expired scans=\d+ uptime_ms=\d+ elapsed_ms=\d+')

    def test_stop_marker_ends_watch_and_releases_resources(self):
        rc,out=self.run_probe('watch',events=[(.25,lambda:(self.base/'stop').write_text('stop'))])
        self.assertEqual(rc,0,out);self.assertIn('reason=stop_file',out)
        self.assertTrue((self.base/'stop').is_file())
        self.assertFalse((self.base/'capture.lock').exists())
        pid=int((self.base/'monitor.pid').read_text())
        with self.assertRaises(ProcessLookupError):os.kill(pid,0)

    def test_stale_stop_marker_refuses_watch_before_monitor_start(self):
        (self.base/'stop').write_text('previous run')
        rc,out=self.run_probe('watch');self.assertNotEqual(rc,0,out)
        self.assertIn('stale_stop',out)
        self.assertFalse((self.base/'monitor.pid').exists())
        self.assertEqual((self.base/'stop').read_text(),'previous run')

if __name__=='__main__':unittest.main()
