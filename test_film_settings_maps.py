"""Validate exact module paths, mapping offsets and complete readable state."""
import ctypes
from pathlib import Path
import subprocess
import tempfile
import unittest
ROOT=Path(__file__).resolve().parent
class Map(ctypes.Structure):
    _fields_=[('base',ctypes.c_uint),('state',ctypes.c_uint),
              ('image_count',ctypes.c_uint),('state_count',ctypes.c_uint)]
class FilmSettingsMapsTests(unittest.TestCase):
    def setUp(self):
        self.assertTrue((ROOT/'camera/film_settings_maps.c').exists(),
                        'exact module mapping validator is not implemented')
        self.directory=tempfile.TemporaryDirectory();self.addCleanup(self.directory.cleanup)
        lib=Path(self.directory.name)/'maps.dylib'
        subprocess.run(['cc','-std=c11','-Wall','-Wextra','-Werror','-shared','-fPIC',
            str(ROOT/'camera/film_settings_maps.c'),'-o',str(lib)],check=True)
        self.api=ctypes.CDLL(str(lib)).film_settings_map_line
        self.api.argtypes=[ctypes.POINTER(Map),ctypes.c_char_p,ctypes.c_char_p,ctypes.c_uint]
        self.path=b'/tmp_bt/a6400-film-ui-v3-relay.so';self.map=Map()
    def line(self,s):self.api(ctypes.byref(self.map),s,self.path,82028)
    def test_real_map_bias_and_bss_cover_entire_snapshot(self):
        self.line(b'0ecbe000-0ecc2000 r-xp 00000000 00:01 123 /tmp_bt/a6400-film-ui-v3-relay.so\n')
        self.line(b'0ecd2000-0ecd3000 rw-p 00004000 00:01 123 /tmp_bt/a6400-film-ui-v3-relay.so\n')
        self.assertEqual((self.map.base,self.map.state,self.map.image_count,self.map.state_count),
                         (0x0ecbe000,0x0ecd206c,1,1))
    def test_other_suffix_deleted_image_and_short_region_are_rejected(self):
        self.line(b'0ecbe000-0ecc2000 r-xp 00000000 00:01 123 /tmp_bt/a6400-film-ui-v3-relay.so.old\n')
        self.line(b'0ecbe000-0ecc2000 r-xp 00000000 00:01 123 /tmp_bt/a6400-film-ui-v3-relay.so (deleted)\n')
        self.assertEqual(self.map.image_count,0)
        self.line(b'0ecbe000-0ecc2000 r-xp 00000000 00:01 123 /tmp_bt/a6400-film-ui-v3-relay.so\n')
        self.line(b'0ecd2000-0ecd2100 rw-p 00004000 00:01 123 /tmp_bt/a6400-film-ui-v3-relay.so\n')
        self.assertEqual(self.map.state_count,0)
    def test_duplicate_image_is_not_accepted_as_unique(self):
        for _ in range(2):
            self.line(b'0ecbe000-0ecc2000 r-xp 00000000 00:01 123 /tmp_bt/a6400-film-ui-v3-relay.so\n')
        self.assertEqual(self.map.image_count,2)
if __name__=='__main__':unittest.main(verbosity=2)
