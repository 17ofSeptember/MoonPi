"""Linux bundle, staged install and doctor acceptance; no root or real service changes."""
import importlib.util
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile

root = Path(__file__).resolve().parent.parent
sys.dont_write_bytecode = True
bundle = Path(sys.argv[1]).resolve()
spec = importlib.util.spec_from_file_location('deploy', bundle / 'tools/deploy.py')
deploy = importlib.util.module_from_spec(spec)
spec.loader.exec_module(deploy)

with tempfile.TemporaryDirectory(prefix='moonpi-deploy-test-') as temp:
    temp = Path(temp)
    metadata = deploy.verify(bundle)
    result = subprocess.run([str(bundle / 'moonpi'), '--doctor', '--data', str(temp / 'untouched')], cwd=bundle, text=True, capture_output=True, timeout=10)
    assert result.returncode == 0, result.stderr
    assert json.loads(result.stdout)['read_only']
    assert not (temp / 'untouched').exists()
    result = deploy.install(bundle, temp / 'target')
    assert result['staged'] and not result['service_started']
    target = temp / 'target'
    config = target / 'etc/moonpi/config.json'
    environment = target / 'etc/moonpi/environment'
    assert json.loads(config.read_text())['mode'] == 'simulation'
    assert json.loads(config.read_text())['bind'] == '127.0.0.1'
    assert environment.stat().st_mode & 0o777 == 0o600
    token = environment.read_text()
    project = target / 'var/lib/moonpi/project.moonpi.json'
    project.write_text('{"preserve":"existing user project"}')
    original = json.loads(config.read_text()); original['port'] = 8081
    config.write_text(json.dumps(original))
    deploy.install(bundle, target)
    assert json.loads(config.read_text())['port'] == 8081
    assert environment.read_text() == token
    assert project.read_text() == '{"preserve":"existing user project"}'
    assert list((target / 'etc/systemd/system').glob('moonpi.service.previous-*'))
    unit = (target / 'etc/systemd/system/moonpi.service').read_text()
    assert 'User=moonpi' in unit and 'PrivateDevices=no' in unit and 'Restart=on-failure' in unit
    assert '@RELEASE@' not in unit and token.strip() not in unit
    # Verify service syntax without installing it or depending on a real moonpi account/path.
    fixture = temp / 'moonpi-test.service'
    unit = '\n'.join('ExecStart=/usr/bin/true' if line.startswith('ExecStart=') else 'WorkingDirectory=/tmp' if line.startswith('WorkingDirectory=') else line for line in unit.splitlines())
    fixture.write_text(unit)
    checked = subprocess.run(['systemd-analyze', 'verify', str(fixture)], text=True, capture_output=True, timeout=15)
    assert checked.returncode == 0, checked.stderr
    tampered = temp / 'tampered'
    shutil.copytree(bundle, tampered)
    (tampered / 'README.md').write_text('modified')
    try:
        deploy.install(tampered, temp / 'must-not-exist')
        raise AssertionError('Tampered package accepted')
    except ValueError:
        assert not (temp / 'must-not-exist').exists()
    shutil.copy2(bundle / 'README.md', tampered / 'README.md')
    (tampered / 'unexpected.txt').write_text('extra file')
    try:
        deploy.verify(tampered)
        raise AssertionError('Unlisted file accepted')
    except ValueError:
        pass
    (tampered / 'unexpected.txt').unlink()
    entries = json.loads((tampered / 'manifest.json').read_text())
    entries[0]['path'] = '../outside-file'
    (tampered / 'manifest.json').write_text(json.dumps(entries))
    try:
        deploy.verify(tampered)
        raise AssertionError('Manifest traversal accepted')
    except ValueError:
        pass
    outside = temp / 'outside'; outside.mkdir()
    unsafe = temp / 'unsafe'; unsafe.mkdir(); (unsafe / 'etc').symlink_to(outside, target_is_directory=True)
    try:
        deploy.install(bundle, unsafe)
        raise AssertionError('Symlink installation accepted')
    except ValueError:
        assert not list(outside.iterdir())
    print('PASS bundle integrity, read-only doctor, staged service syntax, credentials, preserved config/data, repeat install, tamper and symlink rejection')
