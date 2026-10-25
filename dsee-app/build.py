"""Manual Windows Android APK build. Signing key stays in ignored private/."""
from pathlib import Path
import hashlib,json,os,shutil,subprocess,zipfile,xml.etree.ElementTree as ET
root=Path(__file__).resolve().parent.parent
app=root/'dsee-app';out=root/'images/dsee-app';out.mkdir(parents=True,exist_ok=True)
java=Path(os.environ['JAVA_HOME']) if 'JAVA_HOME' in os.environ else next((root/'tools/java').glob('jdk-17*'))
sdk=root/'tools/android-app-sdk';bt=Path(os.environ.get('ZX_ANDROID_BUILD_TOOLS',str(sdk/'android-15')));jar=Path(os.environ.get('ZX_ANDROID_JAR',str(sdk/'android-34-ext12/android.jar')))
env=dict(os.environ,JAVA_HOME=str(java),TEMP=str(out),TMP=str(out));env['PATH']=str(java/'bin')+os.pathsep+env.get('PATH','')
def run(args):
    # Google's Windows aapt cannot open explicit non-ASCII archive paths.
    # Keep all file arguments relative to the workspace's ASCII child folders.
    argv=[str(args[0])]+[os.path.relpath(x,out) if isinstance(x,Path) else str(x) for x in args[1:]]
    subprocess.run(argv,check=True,env=env,cwd=out)
payload=out/'assets/payload';payload.mkdir(parents=True,exist_ok=True)
files={'uac2bridge':root/'images/dsee-experimental/bridge-arm','original/uac2bridge':root/'ZX500-USB-DAC/bridge/uac2bridge','dsee-mode.sh':root/'dsee-experimental/dsee-mode.sh','install.sh':app/'install.sh'}
hashes={}
for name,src in files.items():
    dest=payload/name;dest.parent.mkdir(parents=True,exist_ok=True);shutil.copyfile(src,dest);hashes[name]=hashlib.sha256(dest.read_bytes()).hexdigest()
sums=''.join(f'{hashes[n]}  {n}\n' for n in files if n!='install.sh')
(payload/'SHA256SUMS').write_bytes(sums.encode('ascii'));hashes['SHA256SUMS']=hashlib.sha256((payload/'SHA256SUMS').read_bytes()).hexdigest()
(out/'assets/payload-hashes.properties').write_text(''.join(f'{n}={h}\n' for n,h in hashes.items()),encoding='ascii')
licenses=out/'assets/licenses';licenses.mkdir(exist_ok=True)
shutil.copyfile(root/'ZX500-USB-DAC/LICENSE',licenses/'ZX500-USB-DAC-GPL-2.0.txt')
shutil.copyfile(root/'tools/dsee-tinyalsa/NOTICE',licenses/'tinyalsa-NOTICE.txt')
# Each source file may carry additional copyright holders or BSD wording.
notices=[]
for src in sorted((root/'tools/dsee-tinyalsa').rglob('*')):
    if src.suffix in ('.c','.h') and src.is_file():
        text=src.read_text(encoding='utf-8')
        if text.lstrip().startswith('/*') and 'Redistribution' in text.split('*/',1)[0]:
            notices.append(str(src.relative_to(root/'tools/dsee-tinyalsa'))+'\n'+text.split('*/',1)[0]+'*/\n')
(licenses/'tinyalsa-source-notices.txt').write_text('\n'.join(notices),encoding='utf-8')
classes=out/'classes';gen=out/'gen';dex=out/'dex'
for d in [classes,gen,dex]:
    assert d.resolve().parent==out.resolve(), 'Generated directory escaped build output'
    if d.exists():shutil.rmtree(d)
    d.mkdir()
run([bt/'aapt.exe','package','-f','-m','-M',app/'AndroidManifest.xml','-S',app/'res','-A',out/'assets','-I',jar,'-J',gen,'-F',out/'app-res.apk'])
sources=list((app/'src').rglob('*.java'))+list(gen.rglob('*.java'))
run([java/'bin/javac.exe','-encoding','UTF-8','-classpath',jar,'-source','8','-target','8','-g:none','-d',classes,*sources])
run([java/'bin/java.exe','-cp',bt/'lib/d8.jar','com.android.tools.r8.D8','--release','--lib',jar,'--min-api','28','--output',dex,*classes.rglob('*.class')])
unsigned=out/'app-unsigned.apk';shutil.copyfile(out/'app-res.apk',unsigned)
with zipfile.ZipFile(unsigned,'a',zipfile.ZIP_DEFLATED) as z:z.write(dex/'classes.dex','classes.dex')
run([bt/'zipalign.exe','-f','4',unsigned,out/'app-aligned.apk'])
keydir=root/'private/android-signing';keydir.mkdir(parents=True,exist_ok=True)
keystore=keydir/'zx-dsee.jks';passfile=keydir/'storepass.txt'
if not passfile.exists():
    import secrets
    passfile.write_text(secrets.token_urlsafe(32),encoding='ascii')
if not keystore.exists():
    run([java/'bin/keytool.exe','-genkeypair','-keystore',keystore,'-storepass:file',passfile,'-keypass:file',passfile,'-alias','zx-dsee','-keyalg','RSA','-keysize','3072','-validity','10000','-dname','CN=ZX DSEE Local Build'])
version=ET.parse(app/'AndroidManifest.xml').getroot().attrib['{http://schemas.android.com/apk/res/android}versionName']
apk=out/f'ZX-DSEE-{version}.apk'
run([java/'bin/java.exe','-jar',bt/'lib/apksigner.jar','sign','--ks',keystore,'--ks-key-alias','zx-dsee','--ks-pass','file:'+str(passfile),'--out',apk,out/'app-aligned.apk'])
run([java/'bin/java.exe','-jar',bt/'lib/apksigner.jar','verify','--verbose','--print-certs',apk])
run([bt/'zipalign.exe','-c','4',apk])
manifest={'apk_sha256':hashlib.sha256(apk.read_bytes()).hexdigest(),'payload_sha256':hashes,'package':'com.zx.dsee','version':version}
(out/'build-manifest.json').write_text(json.dumps(manifest,indent=2),encoding='utf-8')
print('APK',apk)
