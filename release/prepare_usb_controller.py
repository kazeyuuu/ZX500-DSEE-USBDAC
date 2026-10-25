"""Generate the ZX505 4.08 controller without connecting to a device."""
from pathlib import Path
import argparse,hashlib,sys

root=Path(__file__).resolve().parent.parent
sys.path.insert(0,str(root))
from prepare_controller import fix_headphone_route,fix_usb_identity

p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--allow-network-adb',action='store_true',help='Retain automatic LAN ADB when Android USB debugging is enabled')
p.add_argument('--output',type=Path,default=root/'images/controller/usb-dac.sh')
a=p.parse_args()
source=root/'ZX500-USB-DAC/usb-dac.sh'
expected='9aa461cf09eff5142055e5aa75dfdd072b4d9c5a4f7718baf42f9c0da15d732f'
if hashlib.sha256(source.read_bytes()).hexdigest()!=expected:
    raise SystemExit('Pinned upstream controller hash mismatch')
s=fix_usb_identity(fix_headphone_route(source.read_text(encoding='utf8')))
if not a.allow_network_adb:
    start=s.index('restart_adbd_tcp_after_gadget(){')
    end=s.index('\n}\n',start)+3
    s=s[:start]+'restart_adbd_tcp_after_gadget(){\n  # LAN debugging is not enabled automatically by this build.\n  return 0\n}\n'+s[end:]
    assert 'setprop persist.adb.tcp.port' not in s
a.output.parent.mkdir(parents=True,exist_ok=True)
a.output.write_bytes(s.encode('utf8'))
print(a.output)
print('SHA256',hashlib.sha256(a.output.read_bytes()).hexdigest())
print('Automatic LAN ADB:',a.allow_network_adb)
