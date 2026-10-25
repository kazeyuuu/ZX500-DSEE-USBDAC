"""Build Android 9-compatible experimental programs with local NDK r27c."""
from pathlib import Path
import subprocess, sys, os
root = Path(__file__).resolve().parent.parent
ndk = Path(os.environ.get('ZX_NDK_TOOLCHAIN', str(root / 'tools/android-ndk-r27c/toolchains/llvm/prebuilt/windows-x86_64')))
out = root / 'images/dsee-experimental'
out.mkdir(parents=True, exist_ok=True)
name = sys.argv[1] if len(sys.argv) > 1 else 'probe'
abi = sys.argv[2] if len(sys.argv) > 2 else 'arm64'
target = {'arm64': 'aarch64-linux-android28', 'arm': 'armv7a-linux-androideabi22'}[abi]
sources=[str(root/'dsee-experimental'/f'{name}.c')]
extra=[]
if name in ('bridge','bridge_contract'):
    subprocess.run([sys.executable,str(root/'dsee-experimental/generate_bridge.py')],check=True)
    tiny=root/'tools/dsee-tinyalsa'
    sources=[str(out/'uac2bridge-dsee.c') if name=='bridge' else str(root/'dsee-experimental/bridge_contract.c')]+[str(tiny/'src'/f'{x}.c') for x in ['pcm','pcm_hw','pcm_plugin','mixer','mixer_hw','mixer_plugin','limits','snd_card_plugin']]
    # A fault handler execs the original bridge; sound-card descriptors must
    # close atomically at exec so the fallback can reopen those same devices.
    for unit,old,new in [('pcm_hw','open(fn, O_RDWR | O_NONBLOCK)','open(fn, O_RDWR | O_NONBLOCK | O_CLOEXEC)'),('mixer_hw','open(fn, O_RDWR)','open(fn, O_RDWR | O_CLOEXEC)')]:
        src=tiny/'src'/f'{unit}.c';text=src.read_text()
        assert text.count(old)==1,'tinyalsa open flags changed'
        generated=out/f'{unit}_cloexec.c';generated.write_text(text.replace(old,new))
        sources[sources.index(str(src))]=str(generated)
    extra=['-I'+str(root/'dsee-experimental'),'-I'+str(tiny/'include'),'-I'+str(tiny/'src'),'-DBRIDGE_DIAG','-Wno-deprecated-declarations']
args = [str(ndk/'bin/clang.exe'), '--target='+target, '-std=c11', '-D_GNU_SOURCE', '-O2', '-g', '-Wall', '-Wextra', '-Werror', '-fPIE', '-pie', *extra, *sources, '-o', str(out/f'{name}-{abi}'), '-ldl', '-lm']
env = dict(os.environ, TMP=str(out), TEMP=str(out))
subprocess.run(args, check=True, env=env)
print(out/f'{name}-{abi}')
