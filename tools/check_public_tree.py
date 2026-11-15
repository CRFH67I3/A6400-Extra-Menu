#!/usr/bin/env python3
"""檢查 Git 已追蹤的公開檔案，不讀取憑證或連接任何服務。"""
from pathlib import Path
import re
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
FORBIDDEN_SUFFIXES = {
    '.elf', '.so', '.dylib', '.o', '.a', '.bin', '.dump', '.req',
    '.tar', '.gz', '.zip', '.img', '.dat', '.log', '.sock', '.pem', '.key',
    '.jpg', '.jpeg', '.png', '.mp3', '.wma', '.cube', '.pyc',
}
PATTERNS = [
    (re.compile(rb'/(?:Users|home)/[^\s/]+/'), '私人絕對路徑'),
    (re.compile(rb'gh[pousr]_[A-Za-z0-9]{20,}'), 'GitHub token'),
    (re.compile(rb'github_pat_[A-Za-z0-9_]{20,}'), 'GitHub token'),
    (re.compile(rb'-----BEGIN [A-Z ]*PRIVATE KEY-----'), '私鑰'),
]


def main():
    tracked = subprocess.run(['git', 'ls-files', '-z'], cwd=ROOT,
                             check=True, capture_output=True).stdout
    names = [n.decode('utf-8') for n in tracked.split(b'\0') if n]
    if not names:
        print('尚未有已追蹤檔案；請先建立待發布的 Git 索引。')
        return 1
    errors = []
    for name in names:
        p = ROOT / name
        if p.is_symlink() or not p.is_file():
            errors.append((name, '不是一般檔案'))
            continue
        if p.suffix.lower() in FORBIDDEN_SUFFIXES or p.name.startswith('.env'):
            errors.append((name, '不應公開的檔案類型'))
        data = p.read_bytes()
        if b'\0' in data or len(data) > 100000:
            errors.append((name, '二進位內容或超出公開原始碼大小限制'))
        try:
            data.decode('utf-8')
        except UnicodeDecodeError:
            errors.append((name, '不是 UTF-8 文字'))
        for pattern, reason in PATTERNS:
            if pattern.search(data):
                errors.append((name, reason))
    for name, reason in errors:
        print(f'{name}: {reason}')
    if errors:
        return 1
    print(f'公開檔案檢查通過：{len(names)} 個文字檔案。')
    print('這是有限範圍的自動檢查，不取代來源及授權審閱。')
    return 0


if __name__ == '__main__':
    sys.exit(main())
