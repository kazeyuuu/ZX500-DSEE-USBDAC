"""Fetch only the pinned upstream fallback binary; never fetch Sony firmware.

The source candidate already contains the bridge source and patched tinyalsa.
--original PATH permits a verified offline copy instead of a network request.
"""
import argparse, hashlib, pathlib, shutil, urllib.request

UPSTREAM = 'af3a7fba2e9043739f406d88690052c2a86416a6'
EXPECTED = 'd80136c9136ada8fdf829801880e26d7817c3ba554ea1124f0ad2494c1f6d336'
root = pathlib.Path(__file__).resolve().parent.parent
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--original', type=pathlib.Path)
args = parser.parse_args()
dest = root / 'ZX500-USB-DAC/bridge/uac2bridge'
if args.original:
    data = args.original.read_bytes()
elif dest.exists():
    data = dest.read_bytes()
else:
    url = f'https://raw.githubusercontent.com/remove/ZX500-USB-DAC/{UPSTREAM}/bridge/uac2bridge'
    with urllib.request.urlopen(url, timeout=60) as response:
        data = response.read()
if hashlib.sha256(data).hexdigest() != EXPECTED:
    raise SystemExit('Fallback SHA256 mismatch; no file installed')
dest.parent.mkdir(parents=True, exist_ok=True)
dest.write_bytes(data)
print('Verified pinned upstream fallback. No firmware or Sony library downloaded.')
