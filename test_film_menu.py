"""Run the actual portable C controller under memory/undefined-behavior checks."""
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parent


class FilmMenuTests(unittest.TestCase):
    def test_chord_touch_preset_changes_and_persistence(self):
        self.assertTrue((ROOT / 'camera/film_menu.c').exists(),
                        'LUT menu controller is not implemented')
        with tempfile.TemporaryDirectory() as directory:
            binary = Path(directory) / 'film-menu-tests'
            subprocess.run(['cc', '-std=c11', '-g', '-O1', '-Wall', '-Wextra',
                            '-Werror', '-fsanitize=address,undefined',
                            str(ROOT / 'camera/test_film_menu.c'),
                            str(ROOT / 'camera/film_menu.c'), '-o', str(binary)], check=True)
            result = subprocess.run([str(binary)], capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            self.assertIn('FILM_MENU_TESTS_OK', result.stdout)


if __name__ == '__main__':
    unittest.main()
