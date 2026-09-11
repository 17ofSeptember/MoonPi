#!/usr/bin/env python3
"""Verify or stage/install a Moon Pi Linux bundle. Never starts a service."""
import argparse
import hashlib
import json
import os
from pathlib import Path, PurePosixPath
import platform
import re
import secrets
import shutil
import subprocess
import sys


def digest(path):
    with path.open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()


def verify(bundle):
    bundle = Path(bundle).resolve()
    manifest = bundle / 'manifest.json'
    if manifest.is_symlink() or manifest.stat().st_size > 2 * 1024 * 1024:
        raise ValueError('Invalid manifest file')
    entries = json.loads(manifest.read_text(encoding='utf-8-sig'))
    if not isinstance(entries, list) or len(entries) > 10000:
        raise ValueError('Invalid manifest entries')
    names = set()
    for entry in entries:
        name = entry['path']
        parts = PurePosixPath(name)
        if (not isinstance(name, str) or not name or parts.is_absolute() or
                any(p in ('..', '.') for p in name.split('/')) or '\\' in name or ':' in name or
                name in names or name == 'manifest.json'):
            raise ValueError('Unsafe or duplicate manifest path')
        names.add(name)
        path = bundle / name
        if not path.resolve().is_relative_to(bundle) or path.is_symlink() or not path.is_file():
            raise ValueError('Manifest file missing or unsafe: ' + name)
        if path.stat().st_size != entry['bytes'] or digest(path) != entry['sha256']:
            raise ValueError('Manifest integrity mismatch: ' + name)
    actual = set()
    for path in bundle.rglob('*'):
        if path.is_symlink():
            raise ValueError('Bundle contains a symlink')
        if path.is_file():
            actual.add(path.relative_to(bundle).as_posix())
    if actual != names | {'manifest.json'}:
        raise ValueError('Bundle has unlisted or missing files; use a clean extraction')
    metadata = json.loads((bundle / 'release.json').read_text())
    if not re.fullmatch(r'\d+\.\d+\.\d+', metadata['version']):
        raise ValueError('Invalid release version')
    with (bundle / 'moonpi').open('rb') as binary:
        header = binary.read(20)
    machines = {62: 'x64', 183: 'arm64'}
    if len(header) != 20 or header[:6] != b'\x7fELF\x02\x01':
        raise ValueError('Expected a little-endian 64-bit Linux executable')
    arch = machines.get(int.from_bytes(header[18:20], 'little'))
    if arch is None or metadata['architecture'] != arch:
        raise ValueError('Release architecture does not match executable')
    return metadata


def safe_target(root, relative):
    target = root / relative
    for path in [target, *target.parents]:
        if path == root.parent:
            break
        if path.is_symlink():
            raise ValueError('Refusing installation through a symlink: ' + str(path))
    if not target.resolve().is_relative_to(root):
        raise ValueError('Installation target escapes root')
    return target


def atomic_text(path, text, mode=0o644):
    path.parent.mkdir(parents=True, exist_ok=True)
    temp = path.with_name(path.name + '.tmp-' + secrets.token_hex(6))
    with temp.open('x', encoding='utf-8') as stream:
        os.chmod(temp, mode)
        stream.write(text)
        stream.flush()
        os.fsync(stream.fileno())
    os.replace(temp, path)


def install(bundle, root):
    metadata = verify(bundle)
    bundle = Path(bundle).resolve()
    root = Path(root).absolute()
    if root.is_symlink():
        raise ValueError('Installation root must not be a symlink')
    root = root.resolve()
    live = root == Path('/')
    if live and (sys.platform != 'linux' or os.geteuid() != 0):
        raise ValueError('A live install requires Linux and root; use --root DIR to stage')
    if live and {'x86_64': 'x64', 'aarch64': 'arm64'}.get(platform.machine()) != metadata['architecture']:
        raise ValueError('This bundle is for a different CPU architecture')
    release_name = metadata['version'] + '-' + digest(bundle / 'manifest.json')[:12]
    release = safe_target(root, 'opt/moonpi/releases/' + release_name)
    config_path = safe_target(root, 'etc/moonpi/config.json')
    environment = safe_target(root, 'etc/moonpi/environment')
    unit = safe_target(root, 'etc/systemd/system/moonpi.service')
    data = safe_target(root, 'var/lib/moonpi')
    if release.exists():
        verify(release)
    else:
        release.parent.mkdir(parents=True, exist_ok=True)
        shutil.copytree(bundle, release)
        for path in release.rglob('*'):
            os.chmod(path, 0o755 if path.is_dir() or path.name == 'moonpi' else 0o644)
        os.chmod(release / 'moonpi', 0o755)
    groups = []
    if live:
        import grp
        import pwd
        try:
            account = pwd.getpwnam('moonpi')
        except KeyError:
            subprocess.run(['useradd', '--system', '--user-group', '--home-dir', '/var/lib/moonpi', '--shell', '/usr/sbin/nologin', 'moonpi'], check=True)
            account = pwd.getpwnam('moonpi')
        if account.pw_uid == 0 or grp.getgrnam('moonpi').gr_gid != account.pw_gid:
            raise ValueError('Existing moonpi account must be non-root with primary group moonpi')
        for name in ('gpio', 'i2c'):
            try:
                grp.getgrnam(name)
                groups.append(name)
            except KeyError:
                pass
        data.mkdir(parents=True, exist_ok=True)
        os.chown(data, account.pw_uid, account.pw_gid)
        os.chmod(data, 0o750)
    else:
        data.mkdir(parents=True, exist_ok=True)
    if not config_path.exists():
        atomic_text(config_path, json.dumps(dict(version=1, mode='simulation', bind='127.0.0.1', port=8080,
                   resources='resources', schemas='schemas', web='web', data='/var/lib/moonpi', gpio_chip='/dev/gpiochip0'), indent=2) + '\n')
    if not environment.exists():
        atomic_text(environment, 'MOONPI_TOKEN=' + secrets.token_urlsafe(32) + '\n', 0o600)
    code_path = '/opt/moonpi/releases/' + release_name
    template = (bundle / 'tools/moonpi.service.in').read_text()
    service = template.replace('@RELEASE@', code_path).replace('@GROUPS@', 'SupplementaryGroups=' + ' '.join(groups) if groups else '# No optional device groups detected')
    if unit.exists():
        shutil.copy2(unit, unit.with_name('moonpi.service.previous-' + secrets.token_hex(6)))
    atomic_text(unit, service)
    return dict(version=metadata['version'], architecture=metadata['architecture'], staged=not live,
                release=str(release), service=str(unit), config=str(config_path), service_started=False,
                next='Review config, then run systemctl daemon-reload and systemctl enable --now moonpi on the target. No service was started or restarted.')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('action', choices=['verify', 'install'])
    parser.add_argument('--bundle', type=Path, default=Path(__file__).resolve().parent.parent)
    parser.add_argument('--root', type=Path, help='Required for install: / for live install, another directory to stage')
    args = parser.parse_args()
    try:
        if args.action == 'install' and args.root is None:
            parser.error('install requires an explicit --root')
        print(json.dumps(verify(args.bundle) if args.action == 'verify' else install(args.bundle, args.root), indent=2))
    except (ValueError, KeyError, OSError, subprocess.CalledProcessError) as error:
        print(json.dumps({'error': str(error)}), file=sys.stderr)
        return 1
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
