"""Validate the allowlisted distribution, record hashes and produce a release ZIP."""
from pathlib import Path
import hashlib
import json
import re
import zipfile

ROOT = Path(__file__).resolve().parents[1]
PACKAGE = ROOT / 'dist/Weaysy-Hybrid-GPS-ATS-0.1.0-preview.1'
ALLOWED_NATIVE = {'WeaysySetup.exe','payload/WeaysyRenderProbe.addon64','payload/plugins/WeaysyTimeReader.dll'}
def main():
    for name in ('clock-tests.json','installer-tests.json','shader-tests.json'):
        assert json.loads((ROOT / 'build' / name).read_text(encoding='utf-8-sig'))['passed'], name
    for name in ('README.md','NOTICE.md','LICENSE','docs/INSTALL.md','docs/INSTALL_NL.md','docs/VALIDATION.md',
                 'licenses/map_assets/LICENSE_MATERIAL_ICONS.txt','licenses/third_party/SCS-SDK.txt',
                 'licenses/third_party/ReShade-API.txt','DISABLE.cmd','CHECK.cmd'):
        assert (PACKAGE / name).is_file(), name
    files = [p for p in PACKAGE.rglob('*') if p.is_file()]
    assert {p.relative_to(PACKAGE).as_posix() for p in files if p.suffix.lower() in ('.exe','.dll','.addon64')} == ALLOWED_NATIVE
    assert not any(p.suffix.lower() in ('.pdb','.log') or 'incident' in p.name.lower() for p in files)
    patterns = [rb'(?i)[A-Z]:[\\/]Users[\\/]', rb'github_pat_[A-Za-z0-9_]{30,}',rb'gh[pousr]_[A-Za-z0-9]{30,}']
    for path in files + [p for p in ROOT.rglob('*') if p.is_file() and 'build' not in p.relative_to(ROOT).parts and 'dist' not in p.relative_to(ROOT).parts and '.git' not in p.relative_to(ROOT).parts]:
        data = path.read_bytes()
        assert not any(re.search(pattern, data) for pattern in patterns), str(path)
    with zipfile.ZipFile(PACKAGE / 'weaysy_ats_hybrid_gps.scs') as z:
        assert z.testzip() is None
        names = z.namelist()
        assert all(not n.startswith('/') and '..' not in Path(n).parts for n in names)
        assert not any(n.lower().endswith(('.exe','.dll','.addon64','.pdb','.log')) for n in names)
        assert all(not re.search(pattern, z.read(n)) for pattern in patterns for n in names)
        assert b'Weaysy Hybrid GPS - Day and Night (ATS Preview)' in z.read('manifest.sii')
        assert b'SwitchEveryGameMinutes=0' in (PACKAGE / 'payload/plugins/WeaysyTimeReader.ini').read_bytes()
        assert '@include' not in z.read('def/map_data.sii').decode()
    checksums = {p.relative_to(PACKAGE).as_posix(): hashlib.sha256(p.read_bytes()).hexdigest() for p in files if p.name not in ('SHA256SUMS.txt','package-manifest.json')}
    report = dict(version='0.1.0-preview.1',game='ATS 1.61.3.18 Windows x64 DX11',
        scope='Theme only; no incident detector or redistributed ReShade/game binaries',
        files=checksums, fresh_in_game_drive_this_release=False)
    (PACKAGE / 'package-manifest.json').write_text(json.dumps(report, indent=2), encoding='utf8')
    (PACKAGE / 'SHA256SUMS.txt').write_text(''.join(f'{digest}  {name}\n' for name,digest in sorted(checksums.items())), encoding='utf8')
    archive = PACKAGE.parent / (PACKAGE.name + '.zip')
    with zipfile.ZipFile(archive, 'w', compression=zipfile.ZIP_DEFLATED) as z:
        for p in sorted(PACKAGE.rglob('*')):
            if p.is_file():
                z.write(p, PACKAGE.name + '/' + p.relative_to(PACKAGE).as_posix())
    with zipfile.ZipFile(archive) as z:
        assert z.testzip() is None
    (archive.parent / (archive.name + '.sha256')).write_text(hashlib.sha256(archive.read_bytes()).hexdigest() + '  ' + archive.name + '\n', encoding='utf8')
    print(json.dumps(dict(passed=True,files=len(checksums),zip=str(archive),bytes=archive.stat().st_size), indent=2))

if __name__ == '__main__':
    main()
