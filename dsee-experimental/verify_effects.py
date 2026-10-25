"""Offline EQ/DC and exact realtime-transform contracts; never opens ALSA."""
from pathlib import Path
import argparse,json,math,shlex,subprocess,sys
root=Path(__file__).resolve().parent.parent
p=argparse.ArgumentParser();p.add_argument('--serial',required=True);a=p.parse_args()
adb=root/'tools/platform-tools/adb.exe';out=root/'private/dsee-research';out.mkdir(parents=True,exist_ok=True)
def run(args,timeout=60):
 r=subprocess.run([str(adb),'-s',a.serial,*args],capture_output=True,timeout=timeout)
 text=(r.stdout+r.stderr).decode('utf8',errors='replace').replace('\r','')
 if r.returncode:raise RuntimeError(text[-3000:])
 return text
logs={}
for name in ('effects_offline','bridge_contract'):
 subprocess.run([sys.executable,str(root/'dsee-experimental/build.py'),name,'arm'],check=True)
 target='/data/local/tmp/zx-verify-'+name
 run(['push',str(root/'images/dsee-experimental'/f'{name}-arm'),target])
 command=f'chmod 755 {target}; LD_LIBRARY_PATH=/vendor/lib:/vendor/lib/hw:/system/lib/vndk-28:/system/lib {target}'
 log=run(['shell','su -c '+shlex.quote(command)])
 (out/(name+'-verification.log')).write_text(log,encoding='utf8');logs[name]=log
cases=[dict(v.split('=',1) for v in line.split()[1:]) for line in logs['effects_offline'].splitlines() if line.startswith('CASE ')]
assert len(cases)==86
checks={}
for rate in (44100,48000,88200,96000,176400,192000):
 x={int(c['mode']):c for c in cases if int(c['rate'])==rate};assert len(x)==14
 gain=lambda mode:20*math.log10(float(x[mode]['rms'])/float(x[1]['rms']))
 assert abs(gain(2)-6)<.15 and abs(gain(3)+6)<.15
 assert len({x[m]['phase20'] for m in range(4,13)})==9
 for mode in range(4,13):assert math.isfinite(float(x[mode]['mag100'])) and 0<float(x[mode]['mag100'])<4
 checks[rate]={'eq_plus6_db':gain(2),'eq_minus6_db':gain(3),'dc_modes_distinct':True}
for rate in (352800,384000):assert f'HIGH_RATE_REJECTED rate={rate}' in logs['effects_offline']
assert 'PASS 64 exact realtime-transform cases' in logs['bridge_contract']
assert logs['bridge_contract'].count('CHAIN rate=')==64
(out/'effects-verification.json').write_text(json.dumps({'cases':cases,'checks':checks,'chain_cases':64,'high_rate_negative_controls':True},indent=2),encoding='utf8')
print('PASS: 86 offline cases, EQ gains/DC responses, two high-rate rejection controls, 64 exact-transform hot-reload cases')
