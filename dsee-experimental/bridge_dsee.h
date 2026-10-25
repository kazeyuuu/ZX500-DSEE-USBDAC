/* User-space DSP hook for the upstream adaptive bridge. */
#include "sony_dsx.h"
#include <math.h>
#include <signal.h>
#include <sys/prctl.h>
#include <sys/stat.h>
#ifndef DSEE_FLAG
#define DSEE_FLAG "/data/adb/usbdac.dsee"
#endif
#define DSEE_ORIGINAL "/data/local/tmp/dsee-experimental/original/uac2bridge"
#include <sys/wait.h>
static struct sony_dsx bridge_dsx;
static unsigned dsx_ratio=1;
static unsigned long dsx_frames=0,dsx_clips=0;
static double dsx_ms=0,dsx_worst=0;
static int dsx_enabled=0;
#include "bridge_fx.h"
static void dsee_log_bound(void) {
    struct stat st;
    if(!fstat(2,&st)&&S_ISREG(st.st_mode)&&st.st_size>131072) {ftruncate(2,0);lseek(2,0,SEEK_SET);}
}
static void dsee_crash_fallback(int sig) {
    (void)sig;
    static const char msg[]="DSEE fault: reverting to original bridge\n";
    write(2,msg,sizeof(msg)-1);
    unlink(DSEE_FLAG);
    unlink(FX_FLAG);
    char *const args[]={"uac2bridge",NULL};
    execv(DSEE_ORIGINAL,args);
    _exit(99);
}
static void dsee_bootstrap(char **argv) {
    /* Library search paths are read by Bionic at exec time. Preserve the PID and
     * upstream's comm-based lifecycle checks through one self-exec. */
    if(!getenv("ZX_DSEE_LINKER_READY")) {
        setenv("LD_LIBRARY_PATH","/vendor/lib:/vendor/lib/hw:/system/lib/vndk-28:/system/lib",1);
        setenv("ZX_DSEE_LINKER_READY","1",1);
        execv("/proc/self/exe",argv);
        perror("DSEE self-exec");_exit(97);
    }
    prctl(PR_SET_NAME,"uac2bridge",0,0,0);
    /* A registered Android service can bind Sony's manager. It publishes
     * snapshots even while the activity is closed; PCM remains native. */
    pid_t child=fork();
    if(child==0){
        pid_t daemon=fork();if(daemon!=0)_exit(daemon<0?1:0);
        setsid();int fd=open("/dev/null",O_RDWR);if(fd>=0){dup2(fd,0);dup2(fd,1);dup2(fd,2);if(fd>2)close(fd);}
        unsetenv("LD_LIBRARY_PATH");
        execl("/system/bin/am","am","start-foreground-service","--user","0","-n","com.zx.dsee/.SonyControlService",(char*)NULL);_exit(1);
    }
    if(child>0){while(waitpid(child,NULL,0)<0&&errno==EINTR){}}
    struct sigaction sa;memset(&sa,0,sizeof(sa));sa.sa_handler=dsee_crash_fallback;sigemptyset(&sa.sa_mask);
    sigaction(SIGSEGV,&sa,NULL);sigaction(SIGBUS,&sa,NULL);sigaction(SIGABRT,&sa,NULL);sigaction(SIGFPE,&sa,NULL);
}
static unsigned dsee_rate(unsigned rate) {
    dsee_log_bound();
    sony_dsx_close(&bridge_dsx);dsx_enabled=0;dsx_ratio=1;
    dsx_frames=dsx_clips=0;dsx_ms=dsx_worst=0;
    if(access(DSEE_FLAG,F_OK)) {fprintf(stderr,"DSEE BYPASS: disabled\n");return rate;}
    unsigned output=rate;
    if(rate==44100||rate==88200)output=176400;
    else if(rate==48000||rate==96000)output=192000;
    else {fprintf(stderr,"DSEE BYPASS: %u Hz has no validated active mode\n",rate);return rate;}
    if(sony_dsx_open(&bridge_dsx,rate,output) || bridge_dsx.input_frames>128 || bridge_dsx.output_frames>256 || 512%bridge_dsx.input_frames) {
        fprintf(stderr,"DSEE BYPASS: initialization/capability failure\n");sony_dsx_close(&bridge_dsx);return rate;
    }
    dsx_enabled=1;dsx_ratio=output/rate;
    fprintf(stderr,"DSEE ACTIVE: %u->%u Hz ratio=%u flags=%u,%u\n",rate,output,dsx_ratio,dsx_word(&bridge_dsx,0x1d5a0),dsx_word(&bridge_dsx,0x1d5a4));
    return output;
}
/* Host clock correction has already produced a whole number of 512-frame
 * periods. Each corrected period can be consumed by fixed 64/128-frame DSP
 * blocks; the corresponding DAC write expands by exactly the integer ratio. */
static int dsee_transform(const uint8_t *in,unsigned frames,uint8_t *out) {
    if(fx_poll(0))return -1;
    if(frames*dsx_ratio>8192)return -1;
    if(!dsx_enabled){
        if(!fx_active)return 0;
        const int32_t *raw=(const int32_t*)in;
        for(unsigned j=0;j<frames*2;++j)fx_pcm[j]=raw[j]/8388608.0f;
        return fx_process_pcm(frames,(int32_t*)out);
    }
    float l[128],r[128],ol[256],or_[256];
    const int32_t *src=(const int32_t *)in;int32_t *dst=(int32_t *)out;
    if(frames%bridge_dsx.input_frames)return -1;
    double begin=now_ms();
    for(unsigned base=0;base<frames;base+=bridge_dsx.input_frames) {
        for(unsigned j=0;j<bridge_dsx.input_frames;++j) {l[j]=src[(base+j)*2]/8388608.0f;r[j]=src[(base+j)*2+1]/8388608.0f;}
        /* Any unwritten output is an error, not whatever happened to occupy
         * this stack slot. Never forward a partial DSP block to headphones. */
        for(unsigned j=0;j<bridge_dsx.output_frames;++j)ol[j]=or_[j]=NAN;
        if(sony_dsx_process(&bridge_dsx,l,r,ol,or_))return -1;
        for(unsigned j=0;j<bridge_dsx.output_frames;++j) {
            float v[2]={ol[j],or_[j]};
            for(unsigned c=0;c<2;++c) {
                if(!isfinite(v[c])||fabsf(v[c])>8)return -1;
                if(fx_active){fx_pcm[(base*dsx_ratio+j)*2+c]=v[c];continue;}
                if(v[c]>=1||v[c]<-1)++dsx_clips;
                double a=fmax(-1,fmin(8388607.0/8388608.0,v[c]));
                dst[(base*dsx_ratio+j)*2+c]=(int32_t)llrint(a*8388608.0);
            }
        }
    }
    double cost=now_ms()-begin;dsx_ms+=cost;if(cost>dsx_worst)dsx_worst=cost;dsx_frames+=frames;
    if(fx_active)return fx_process_pcm(frames*dsx_ratio,dst);
    return 0;
}
