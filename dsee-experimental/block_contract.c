/* Offline-only ABI regression probe; never opens an ALSA device. */
#include "sony_dsx.h"
#include <math.h>
static int run(unsigned rate,unsigned output,const char *kind,const char *dir){
    struct sony_dsx s;if(sony_dsx_open(&s,rate,output))return 1;
    if(!strncmp(kind,"legacy-",7)&&rate>=88200) *(uint32_t *)((char *)s.state+0x1e738)=1;
    float *l=dsx_guarded(s.output_frames*4),*r=dsx_guarded(s.output_frames*4);
    float *ol=dsx_guarded(s.output_frames*4),*or_=dsx_guarded(s.output_frames*4);
    unsigned nonfinite=0,unchanged=0,overrun=0;double peak=0;char path[512];
    snprintf(path,sizeof(path),"%s/%u-%s.f32",dir,rate,kind);FILE *file=fopen(path,"wb");if(!file)return 2;
    for(unsigned block=0;block<256;++block){
        float fill=strstr(kind,"nan")?NAN:strstr(kind,"sentinel")?.25f:0;
        for(unsigned j=0;j<s.output_frames;++j)ol[j]=or_[j]=fill;
        memset((char *)ol+s.output_frames*4,0xa5,4096-s.output_frames*4);
        memset((char *)or_+s.output_frames*4,0xa5,4096-s.output_frames*4);
        for(unsigned j=0;j<s.input_frames;++j){double t=(block*s.input_frames+j)/(double)rate;l[j]=(float)(.1*sin(2*M_PI*997*t)+.05*sin(2*M_PI*5003*t));r[j]=(float)(.08*sin(2*M_PI*1709*t));}
        int rc;
        if(!strncmp(kind,"alias",5)){
            memcpy(ol,l,s.input_frames*4);memcpy(or_,r,s.input_frames*4);
            rc=sony_dsx_process(&s,ol,or_,ol,or_);
        }else rc=sony_dsx_process(&s,l,r,ol,or_);
        if(rc){fprintf(stderr,"PROCESS_FAIL %d\n",rc);return 3;}
        for(unsigned j=s.output_frames*4;j<4096;++j)if(((unsigned char *)ol)[j]!=0xa5||((unsigned char *)or_)[j]!=0xa5)++overrun;
        for(unsigned j=0;j<s.output_frames;++j){float v[2]={ol[j],or_[j]};for(unsigned c=0;c<2;++c){if(!isfinite(v[c]))++nonfinite;else if(fabs(v[c])>peak)peak=fabs(v[c]);if(!strcmp(kind,"sentinel")&&v[c]==fill)++unchanged;}if(fwrite(v,4,2,file)!=2)return 4;}
    }
    fclose(file);printf("CASE rate=%u output=%u kind=%s nonfinite=%u unchanged=%u overrun=%u peak=%.8f\n",rate,output,kind,nonfinite,unchanged,overrun,peak);fflush(stdout);
    sony_dsx_close(&s);return 0;
}
int main(int argc,char **argv){
    if(argc!=2)return 2;
    unsigned rates[]={44100,48000,88200,96000};const char *kind[]={"zero","sentinel","nan","alias","alias-sentinel","alias-nan","legacy-nan"};
    for(unsigned i=0;i<4;++i)for(unsigned j=0;j<7;++j)if(run(rates[i],rates[i]%44100==0?176400:192000,kind[j],argv[1]))return 3;
    return 0;
}
