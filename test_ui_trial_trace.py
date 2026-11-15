"""Check the actual bounded C trace protocol before any camera UI trial."""
from pathlib import Path
import struct
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).parent


class TrialTraceTests(unittest.TestCase):
    def test_ring_bounds_commit_and_wrap(self):
        source = r'''
#include <stdio.h>
#include "ui_trial_trace.h"
struct { unsigned before[4]; struct ui_trial_trace t; unsigned after[4]; } memory;
int main(void) {
  for(unsigned i=0;i<4;i++)memory.before[i]=memory.after[i]=0xdeadbeef;
  for(unsigned i=1;i<=30;i++)
    ui_trial_record(&memory.t,"OPEN_BEFORE",100+i,380,i,2*i,3*i,1);
  return fwrite(&memory,1,sizeof(memory),stdout)!=sizeof(memory);
}
'''
        with tempfile.TemporaryDirectory() as folder:
            p=Path(folder);(p/'test.c').write_text(source)
            subprocess.run(['cc','-std=c11','-Wall','-Wextra','-Werror',
                            '-I',str(ROOT/'camera'),str(p/'test.c'),'-o',str(p/'test')],check=True)
            raw=subprocess.check_output([str(p/'test')])
        self.assertEqual(len(raw),16+1104+16)
        self.assertEqual(raw[:16],struct.pack('<4I',*([0xdeadbeef]*4)))
        self.assertEqual(raw[-16:],raw[:16])
        header=struct.unpack_from('<12I',raw,16)
        self.assertEqual(header[:3],(0x33544955,1,30))
        self.assertEqual(header[7:9],(22,48))
        for seq in range(9,31):
            record=struct.unpack_from('<12I',raw,16+48+((seq-1)%22)*48)
            self.assertEqual(record[0],seq*2)
            self.assertEqual(record[11],record[0])
            self.assertEqual(record[2:8],(100+seq,380,seq,2*seq,3*seq,1))
            self.assertNotEqual(record[1],0)


if __name__ == '__main__': unittest.main(verbosity=2)
