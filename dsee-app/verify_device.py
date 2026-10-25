"""Exercise the installed app's switch and lifecycle using observed UI bounds.
Requires the human to have granted com.zx.dsee root in KernelSU first.
Leaves the user's initial DSEE preference restored, even on a test failure.
"""
import argparse,json,pathlib,re,shlex,subprocess,time,xml.etree.ElementTree as E
p=argparse.ArgumentParser();p.add_argument('--serial',required=True);a=p.parse_args()
root=pathlib.Path(__file__).resolve().parent.parent;adb=root/'tools/platform-tools/adb.exe'
out=root/'private/dsee-research';results={};control='/data/local/tmp/dsee-experimental/dsee-mode.sh'
out.mkdir(parents=True,exist_ok=True)
def cmd(command,as_root=False):
    r=subprocess.run([str(adb),'-s',a.serial,'shell','su -c '+shlex.quote(command) if as_root else command],capture_output=True,timeout=25)
    if r.returncode:raise RuntimeError(r.stdout.decode(errors='replace')+r.stderr.decode(errors='replace'))
    return r.stdout.decode(errors='replace').replace('\r','')
def status():return dict(l.split('=',1) for l in cmd(control+' appstatus',True).splitlines() if '=' in l)
def wake():
    cmd('input keyevent KEYCODE_WAKEUP');cmd('wm dismiss-keyguard');cmd('am start -n com.zx.dsee/.MainActivity');time.sleep(1)
def xml():
    cmd('uiautomator dump /data/local/tmp/zx-dsee-ui.xml');return cmd('cat /data/local/tmp/zx-dsee-ui.xml')
def switch_node():
    for _ in range(8):
        raw=xml();node=next((n for n in E.fromstring(raw).iter('node') if n.attrib.get('class')=='android.widget.Switch'),None)
        if node is not None and node.attrib['enabled']=='true':return raw,node
        time.sleep(.5)
    raise RuntimeError('App switch not ready')
def tap(expected):
    wake();raw,node=switch_node();assert node.attrib['checked']==str(expected).lower()
    x1,y1,x2,y2=map(int,re.findall(r'\d+',node.attrib['bounds']))
    cmd(f'input tap {(x1+x2)//2} {(y1+y2)//2}');time.sleep(3)
def snapshot(label):
    wake();raw,node=switch_node();v=status();v['ui_switch']=node.attrib['checked']
    (out/f'app-{label}.xml').write_text(raw,encoding='utf-8')
    r=subprocess.run([str(adb),'-s',a.serial,'exec-out','screencap','-p'],capture_output=True,timeout=20)
    (out/f'app-{label}.png').write_bytes(r.stdout)
    results[label]=v;print(label,json.dumps(v,indent=2),flush=True);return v
initial=status()['REQUESTED'];completed=False
try:
    v=snapshot('before-toggle');assert v['STATE']=='active','Use a supported rate and play music for this test'
    tap(True);v=snapshot('off');assert v['REQUESTED']=='0' and v['STATE']=='off' and v['OUTPUT_RATE']==v['INPUT_RATE'] and v['ui_switch']=='false'
    tap(False);v=snapshot('on');assert v['REQUESTED']=='1' and v['STATE']=='active' and v['ui_switch']=='true'
    pid=v['PROCESS_PID'];cmd('am force-stop com.zx.dsee');time.sleep(2);v=status()
    assert v['PROCESS_PID']==pid and v['STATE']=='active';results['app_closed']=v
    print('APP CLOSED: audio process unchanged',pid,flush=True)
    cmd('am start -n com.zx.dsee/.MainActivity');time.sleep(2);v=snapshot('reopened');assert v['STATE']=='active' and v['PROCESS_PID']==pid
    (out/'app-ui-verification.json').write_text(json.dumps(results,ensure_ascii=False,indent=2),encoding='utf-8')
    completed=True;print('APP SWITCH AND LIFECYCLE PASS',flush=True)
finally:
    if not completed or status()['REQUESTED']!=initial:
        cmd(control+(' on' if initial=='1' else ' off'),True)
