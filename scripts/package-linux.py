#!/usr/bin/env python3
"""Build an offline Linux tarball from an existing native CMake build."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import platform
import re
import subprocess
import tarfile
import tempfile


def sha(path):
    with path.open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build', type=Path, default=Path('build-pi'))
    parser.add_argument('--output', type=Path, default=Path('release'))
    parser.add_argument('--cmake', default='cmake')
    args = parser.parse_args()
    if platform.system() != 'Linux':
        parser.error('Package Linux executables on Linux (WSL is supported)')
    args.output.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix='moonpi-package-') as temp:
        bundle = Path(temp) / 'MoonPi'
        subprocess.run([args.cmake, '--install', str(args.build), '--config', 'Release', '--prefix', str(bundle)], check=True)
        executable = bundle / 'moonpi'
        with executable.open('rb') as stream:
            header = stream.read(20)
        if header[:6] != b'\x7fELF\x02\x01':
            raise ValueError('Not a supported 64-bit Linux executable')
        arch = {62: 'x64', 183: 'arm64'}.get(int.from_bytes(header[18:20], 'little'))
        if not arch:
            raise ValueError('Unsupported architecture')
        # Never execute a foreign-architecture binary to identify its version.
        version_header = args.build / 'generated/moonpi/version.hpp'
        version = re.search(r'version = "(\d+\.\d+\.\d+)"', version_header.read_text())[1]
        if not (bundle / 'web/index.html').is_file():
            raise ValueError('Build frontend/dist before packaging')
        (bundle / 'release.json').write_text(json.dumps(dict(version=version, architecture=arch,
            platform='Linux', build_system=platform.platform(), libc=list(platform.libc_ver())), indent=2) + '\n')
        paths = sorted(p for p in bundle.rglob('*') if p.is_file())
        manifest = [dict(path=p.relative_to(bundle).as_posix(), bytes=p.stat().st_size, sha256=sha(p)) for p in paths]
        (bundle / 'manifest.json').write_text(json.dumps(manifest, indent=2) + '\n')
        filename = 'MoonPi-linux-' + arch + '.tar.gz'
        # Stage on the destination filesystem for atomic final replacement.
        fd, temporary = tempfile.mkstemp(prefix='.moonpi-', suffix='.tar.gz', dir=args.output)
        os.close(fd)
        try:
            with tarfile.open(temporary, 'w:gz') as archive:
                archive.add(bundle, arcname='MoonPi')
            os.replace(temporary, args.output / filename)
        finally:
            if Path(temporary).exists():
                Path(temporary).unlink()
        (args.output / (filename + '.sha256')).write_text(sha(args.output / filename) + '  ' + filename + '\n')
        print(str(args.output / filename))


if __name__ == '__main__':
    main()
