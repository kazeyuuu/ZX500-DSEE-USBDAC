"""Export only the public files declared in SOURCE-MANIFEST.json."""
from pathlib import Path
import hashlib, json, zipfile
root = Path(__file__).resolve().parent.parent
manifest = json.loads((root/'SOURCE-MANIFEST.json').read_text(encoding='utf-8'))
output = root/'images/release'
output.mkdir(parents=True, exist_ok=True)
archive = output/f"ZX-DSEE-{manifest['version']}-source.zip"
names = sorted(set(manifest['files_sha256']) | {'SOURCE-MANIFEST.json', 'SHA256SUMS'})
with zipfile.ZipFile(archive, 'w', zipfile.ZIP_DEFLATED) as z:
    for name in names:
        path = root/name
        assert path.resolve().is_relative_to(root.resolve())
        if name in manifest['files_sha256']:
            assert hashlib.sha256(path.read_bytes()).hexdigest() == manifest['files_sha256'][name], name
        z.write(path, f"zx-dsee/{name}")
print(archive)
