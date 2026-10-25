"""Apply small, checked hooks to the unchanged upstream bridge source."""
from pathlib import Path
import hashlib
root=Path(__file__).resolve().parent.parent
source=root/'ZX500-USB-DAC/bridge/uac2bridge.c'
s=source.read_text(encoding='utf-8')
def replace(old,new):
    global s
    if s.count(old)!=1:raise RuntimeError('Upstream anchor changed: '+old[:90])
    s=s.replace(old,new)
replace('int main(int argc, char **argv) {','#include "bridge_dsee.h"\n\nint main(int argc, char **argv) {\n    dsee_bootstrap(argv);')
replace('open(ctl_path, O_RDONLY | O_NONBLOCK)','open(ctl_path, O_RDONLY | O_NONBLOCK | O_CLOEXEC)')
replace('static void sync_host_controls(struct mixer_ctl *rate_ctl,','#include "sony_controls.h"\n\nstatic void sync_host_controls(struct mixer_ctl *rate_ctl,')
replace('            apply_host_controls(hw, mute, last_hw, last_mute);','            apply_host_controls(hw, mute, last_hw, last_mute);\n        sony_publish_controls(hw, hw_max, mute);')
replace('unsigned pcount = production_pcount((unsigned)rate);','unsigned output_rate=dsee_rate((unsigned)rate);\n        unsigned pcount = production_pcount((unsigned)rate);')
replace('unsigned output_rate=dsee_rate((unsigned)rate);','unsigned output_rate=dsee_rate((unsigned)rate);\n        if(fx_reset(output_rate))dsee_crash_fallback(0);')
replace('cout=cin; cout.format=play_fmt;','cout=cin; cout.format=play_fmt; cout.rate=output_rate; cout.period_count=production_pcount(output_rate);\n        cout.stop_threshold=(unsigned long)cout.period_size*cout.period_count;')
replace('uint8_t *pb = malloc((size_t)corr_span * play_bpf());','uint8_t *pb = malloc((size_t)corr_span * play_bpf());\n        uint8_t *dsp_pb = malloc((size_t)corr_span * play_bpf() * dsx_ratio);')
replace('if(!cb || !win || !outw || !pb){','if(!cb || !win || !outw || !pb || !dsp_pb){')
# Every error path and normal re-handshake releases the extra output buffer.
s=s.replace('free(pb);','free(pb);free(dsp_pb);')
replace('if(pcm_write(pout,write_buf,out_frames*play_bpf())!=0){','if(dsee_transform(write_buf,out_frames,dsp_pb)) {\n                fprintf(stderr,"DSEE invalid output: falling back to original bridge\\n");\n                pcm_close(pin);pcm_close(pout);dsee_crash_fallback(0);\n            }\n            if(dsx_enabled)write_buf=dsp_pb;\n            if(pcm_write(pout,write_buf,out_frames*dsx_ratio*play_bpf())!=0){')
replace('if(dsx_enabled)write_buf=dsp_pb;','if(dsx_enabled||fx_active)write_buf=dsp_pb;')
replace('hreset(&hr); hreset(&hw);\n                n_drop=', 'fprintf(stderr,"DSEE STAT active=%d ratio=%u input_frames=%lu process_ms=%.2f worst_ms=%.3f clips=%lu\\n",dsx_enabled,dsx_ratio,dsx_frames,dsx_ms,dsx_worst,dsx_clips);fflush(stderr);dsee_log_bound();\n                hreset(&hr); hreset(&hw);\n                n_drop=')
replace('recovery_attempts++;\n                    DLOG("XRUN_STORM:', 'recovery_attempts++;\n                    if(dsx_enabled && recovery_attempts>=3) { pcm_close(pin);pcm_close(pout);dsee_crash_fallback(0); }\n                    DLOG("XRUN_STORM:')
replace('if(dsx_enabled && recovery_attempts>=3)', 'if((dsx_enabled||fx_active) && recovery_attempts>=3)')
out=root/'images/dsee-experimental/uac2bridge-dsee.c';out.parent.mkdir(parents=True,exist_ok=True);out.write_text(s,encoding='utf-8')
print('Upstream SHA256',hashlib.sha256(source.read_bytes()).hexdigest());print(out)
