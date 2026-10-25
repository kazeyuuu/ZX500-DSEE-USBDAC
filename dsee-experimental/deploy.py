"""Stage the reversible experiment. No mode switch unless explicitly requested."""
import argparse,hashlib,json,pathlib,shlex,subprocess
p=argparse.ArgumentParser();p.add_argument('--serial',required=True);p.add_argument('--mode',choices=['stage','trial','on','off','commit','status'],default='stage');a=p.parse_args()
root=pathlib.Path(__file__).resolve().parent.parent;adb=root/'tools/platform-tools/adb.exe';base='/data/local/tmp/dsee-experimental'
def run(args,binary=False):
    r=subprocess.run([str(adb),*args],capture_output=True,timeout=45)
    if r.returncode:raise RuntimeError(r.stderr.decode(errors='replace')+r.stdout.decode(errors='replace'))
    return r.stdout if binary else r.stdout.decode(errors='replace').replace('\r','')
def shell(cmd):return run(['-s',a.serial,'shell','su -c '+shlex.quote(cmd)])
print(run(['connect',a.serial]).strip())
assert shell('id -u').strip()=='0'
if a.mode=='stage':
    assert shell('getprop ro.build.display.id').strip()=='4.08.00'
    original=root/'ZX500-USB-DAC/bridge/uac2bridge';orig_hash=hashlib.sha256(original.read_bytes()).hexdigest()
    current=shell('sha256sum /data/local/tmp/uac2bridge').split()[0]
    if current!=orig_hash:
        backup=shell('sha256sum '+base+'/original/uac2bridge').split()[0]
        assert backup==orig_hash,'Original bridge backup missing or mismatched'
    assert shell('sha256sum /vendor/lib/hw/audio.primary.icx1295.so').split()[0]=='dfd3481e5be322f306acef4fac08ce85b204821570597e72c541a1978ce305b8','Unsupported firmware HAL'
    print(shell('mkdir -p '+base+'/original; chmod 700 '+base))
    files=[(original,base+'/original/uac2bridge'),(root/'images/dsee-experimental/bridge-arm',base+'/uac2bridge'),(root/'dsee-experimental/dsee-mode.sh',base+'/dsee-mode.sh')]
    manifest={}
    for src,dst in files:
        upload='/data/local/tmp/zx-dsee-upload.next'
        print(run(['-s',a.serial,'push',str(src),upload]).strip())
        expected=hashlib.sha256(src.read_bytes()).hexdigest();actual=shell('sha256sum '+upload).split()[0];assert expected==actual
        shell('chmod 755 '+upload+' && mv -f '+upload+' '+dst);manifest[dst]=expected
    (root/'images/dsee-experimental/deployed.json').write_text(json.dumps(manifest,indent=2),encoding='utf-8')
    print('Staged; running DAC bridge unchanged')
else:print(shell(base+'/dsee-mode.sh '+a.mode))
