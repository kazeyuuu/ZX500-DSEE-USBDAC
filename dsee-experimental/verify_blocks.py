"""Offline ABI regression on the target device; never opens sound devices."""
from pathlib import Path
import argparse,hashlib,json,math,shlex,struct,subprocess,sys
p=argparse.ArgumentParser();p.add_argument('--serial',required=True);a=p.parse_args()
root=Path(__file__).resolve().parent.parent;adb=root/'tools/platform-tools/adb.exe'
dest=root/'private/dsee-research/contract-fixed';dest.mkdir(parents=True,exist_ok=True)
subprocess.run([sys.executable,str(root/'dsee-experimental/build.py'),'block_contract','arm'],check=True)
def run(args):
    r=subprocess.run([str(adb),*args],capture_output=True,timeout=40)
    if r.returncode:raise RuntimeError(r.stdout.decode(errors='replace')+r.stderr.decode(errors='replace'))
    return r
run(['-s',a.serial,'push',str(root/'images/dsee-experimental/block_contract-arm'),'/data/local/tmp/dsee-block-contract'])
command='mkdir -p /data/local/tmp/dsee-contract; chmod 755 /data/local/tmp/dsee-block-contract; LD_LIBRARY_PATH=/vendor/lib:/vendor/lib/hw:/system/lib/vndk-28:/system/lib /data/local/tmp/dsee-block-contract /data/local/tmp/dsee-contract'
r=run(['-s',a.serial,'shell','su -c '+shlex.quote(command)]);log=(r.stdout+r.stderr).decode(errors='replace').replace('\r','')
(dest/'run.log').write_text(log,encoding='utf-8')
cases=[]
for line in log.splitlines():
    if not line.startswith('CASE '):continue
    c=dict(x.split('=',1) for x in line.split()[1:]);cases.append(c)
    assert int(c['overrun'])==0,c
    if c['kind']=='legacy-nan' and int(c['rate'])>=88200:assert int(c['nonfinite'])==65536,c
    else:assert int(c['nonfinite'])==0 and int(c['unchanged'])==0,c
assert len(cases)==28,'Incomplete probe run'
summary={}
for rate in (44100,48000,88200,96000):
    outputs=[]
    for kind in ('zero','sentinel','nan'):
        name=f'{rate}-{kind}.f32'
        b=run(['-s',a.serial,'exec-out','su','-c','cat /data/local/tmp/dsee-contract/'+name]).stdout
        assert len(b)==256*256*2*4 and all(math.isfinite(v[0]) for v in struct.iter_unpack('<f',b))
        (dest/name).write_bytes(b);outputs.append(b)
    assert outputs[0]==outputs[1]==outputs[2],f'Output depends on uninitialized storage at {rate}'
    summary[rate]={'all_output_fills_identical':True,'sha256':hashlib.sha256(outputs[0]).hexdigest()}
    baseline=root/f'private/dsee-research/block-contract/{rate}-zero.f32'
    if rate<88200 and baseline.exists():
        assert outputs[0]==baseline.read_bytes(),f'Low-rate regression at {rate}'
        summary[rate]['matches_pre_fix_output']=True
(dest/'verification.json').write_text(json.dumps({'cases':cases,'rates':summary},indent=2),encoding='utf-8')
print('PASS: 28 cases; complete writes, bounds, fill independence, and reproducible legacy failure')
