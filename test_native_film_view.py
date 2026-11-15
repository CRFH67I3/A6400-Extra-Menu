from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parent


class NativeFilmViewTests(unittest.TestCase):
    def test_native_enum_input_render_handoff_and_settings(self):
        self.assertTrue((ROOT/'camera/film_native_view.c').exists(),
                        'Native film view is not implemented')
        with tempfile.TemporaryDirectory() as directory:
            binary = Path(directory)/'view-tests'
            subprocess.run(['cc', '-std=c11', '-Wall', '-Wextra', '-Werror',
                            '-g', '-O1', '-fsanitize=address,undefined',
                            str(ROOT/'camera/test_film_native_view.c'),
                            str(ROOT/'camera/film_native_view.c'),
                            str(ROOT/'camera/film_menu.c'), '-o', str(binary)], check=True)
            result = subprocess.run([str(binary)], text=True, capture_output=True)
            self.assertEqual(result.returncode, 0, result.stdout+result.stderr)
            self.assertIn('NATIVE_FILM_VIEW_OK', result.stdout)


if __name__ == '__main__':
    unittest.main(verbosity=2)
