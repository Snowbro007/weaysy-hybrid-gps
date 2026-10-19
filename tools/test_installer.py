"""Exercise real setup in isolated fixtures. Never mutate the supplied game."""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
PACKAGE = ROOT / 'dist/Weaysy-Hybrid-GPS-ATS-0.1.0-preview.1'
FILES = ('WeaysyRenderProbe.addon64', 'plugins/WeaysyTimeReader.dll', 'plugins/WeaysyTimeReader.ini')
sha = lambda path: hashlib.sha256(path.read_bytes()).hexdigest()

def main():
    args = argparse.ArgumentParser()
    args.add_argument('--game-exe', type=Path, required=True)
    args.add_argument('--loader', type=Path, required=True)
    options = args.parse_args()
    assert sha(options.game_exe) == 'c60e8ee02cc97b3cc6ef8c6298bb5c3c23c4ffd330b8185b646fb1b608a3c7b4'
    assert sha(options.loader) == '0cee63f9c9f13f3ac909c5b4903f4dbb4b719a7ab3b4f13b0deaf83c814b94f7'
    original_inputs = (sha(options.game_exe), sha(options.loader))
    cases = []
    with tempfile.TemporaryDirectory(prefix='installer-', dir=ROOT / 'build') as temp:
        fixture = Path(temp).resolve()
        assert fixture.is_relative_to((ROOT / 'build').resolve())
        binary = fixture / 'bin/win_x64'
        binary.mkdir(parents=True)
        def run(mode, success, text):
            result = subprocess.run([str(PACKAGE / 'WeaysySetup.exe'), mode, str(fixture)],
                capture_output=True, text=True, timeout=30)
            assert (result.returncode == 0) == success, (mode, result.stdout, result.stderr)
            assert text.lower() in (result.stdout + result.stderr).lower(), (text, result.stdout, result.stderr)
        def matches():
            return all(sha(binary / name) == sha(PACKAGE / 'payload' / name) for name in FILES)
        game = binary / 'amtrucks.exe'
        game.write_bytes(b'unsupported fixture')
        run('--install', False, 'Unsupported ATS build')
        assert not (binary / 'Weaysy-backups').exists()
        cases.append('unsupported build refuses before mutations')
        shutil.copy2(options.game_exe, game)
        run('--install', False, 'loader required')
        assert not (binary / 'Weaysy-backups').exists()
        cases.append('missing loader refuses before mutations')
        loader = binary / 'dxgi.dll'
        loader.write_bytes(b'unknown loader')
        run('--install', False, 'Unrecognized ReShade')
        cases.append('unknown loader preserved and refused')
        shutil.copy2(options.loader, loader)
        unrelated = binary / 'unrelated-mod.txt'
        unrelated.write_bytes(b'preserve me')
        run('--check', True, 'installed_matching=0')
        assert not (binary / 'Weaysy-backups').exists()
        cases.append('check is read only')
        run('--install', True, 'SUCCESS installed')
        assert matches()
        cases.append('fresh installation matches all payload hashes')
        run('--install', True, 'SUCCESS installed')
        assert matches()
        cases.append('repeat installation succeeds')
        config = binary / FILES[2]
        custom = config.read_text().replace('NightStart=20:00', 'NightStart=21:00')
        config.write_text(custom)
        run('--install', True, 'SUCCESS installed')
        assert config.read_text() == custom
        run('--disable', True, 'SUCCESS disabled')
        assert all(not (binary / name).exists() and Path(str(binary / name) + '.weaysy-disabled').exists() for name in FILES)
        run('--install', True, 'SUCCESS installed')
        assert config.read_text() == custom
        cases.append('custom INI survives repeat install and disable/re-enable')
        addon = binary / FILES[0]
        saved = addon.read_bytes()
        addon.write_bytes(b'unknown user addon')
        run('--install', False, 'unfamiliar')
        assert addon.read_bytes() == b'unknown user addon'
        addon.write_bytes(saved)
        cases.append('unknown existing addon preserved and refused')
        pending = Path(str(addon) + '.weaysy-new')
        pending.write_bytes(b'pending user file')
        run('--install', False, 'pending update')
        assert pending.read_bytes() == b'pending user file'
        pending.unlink()
        cases.append('pending file preserved and refused')
        old_incident = binary / 'WeaysyIncidentMarkers.addon64'
        old_incident.write_bytes(b'old incident research fixture')
        run('--install', False, 'Old incident research plugins')
        assert old_incident.read_bytes() == b'old incident research fixture'
        old_incident.unlink()
        cases.append('old incident plugins require explicit removal')
        # Block a later commit and verify rollback keeps every prior byte.
        before = {name: sha(binary / name) for name in FILES}
        locked = binary / FILES[1]
        locked.chmod(0o444)
        try:
            run('--install', False, 'Cannot commit update')
            assert {name: sha(binary / name) for name in FILES} == before
        finally:
            locked.chmod(0o666)
        assert not list(binary.rglob('*.weaysy-new'))
        cases.append('failed commit rolls back all prior bytes')
        game.write_bytes(b'updated unsupported game fixture')
        run('--disable', True, 'SUCCESS disabled')
        cases.append('disable remains usable after a game update')
        assert unrelated.read_bytes() == b'preserve me' and sha(loader) == original_inputs[1]
        cases.append('other mods and loader untouched')
    assert (sha(options.game_exe), sha(options.loader)) == original_inputs
    report = dict(passed=True, cases=cases, actual_game_modified=False)
    (ROOT / 'build/installer-tests.json').write_text(json.dumps(report, indent=2), encoding='utf8')
    print(json.dumps(report, indent=2))

if __name__ == '__main__':
    main()
