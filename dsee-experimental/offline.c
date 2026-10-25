#include "sony_dsx.h"
#include <math.h>
#include <time.h>
static double ns(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC,&t); return t.tv_sec*1e9+t.tv_nsec; }
int main(int argc,char **argv) {
    if (argc!=5) { fprintf(stderr,"usage: offline input-rate output-rate input.s32le output.s32le\n");return 2; }
    unsigned ir=(unsigned)strtoul(argv[1],NULL,10),o_rate=(unsigned)strtoul(argv[2],NULL,10);
    struct sony_dsx s;
    if (sony_dsx_open(&s,ir,o_rate)) return 3;
    FILE *f=fopen(argv[3],"rb"),*o=fopen(argv[4],"wb");
    if (!f||!o) {perror("PCM file");return 4;}
    float *l=dsx_guarded(s.input_frames*4),*r=dsx_guarded(s.input_frames*4);
    float *ol=dsx_guarded(s.output_frames*4),*or_=dsx_guarded(s.output_frames*4);
    int32_t *in=calloc(s.input_frames*2,4),*out=calloc(s.output_frames*2,4);
    if (!l||!r||!ol||!or_||!in||!out) return 5;
    size_t blocks=0,total_in=0;double total_ns=0,max_ns=0,peak=0;
    for (;;) {
        size_t n=fread(in,8,s.input_frames,f);if (!n) break;
        total_in+=n;
        for(unsigned j=0;j<s.input_frames;++j) { l[j]=j<n?(float)(in[2*j]/2147483648.0):0; r[j]=j<n?(float)(in[2*j+1]/2147483648.0):0; }
        double a=ns();int rc=sony_dsx_process(&s,l,r,ol,or_);double cost=ns()-a;
        if(rc) {fprintf(stderr,"execute rc=%d block=%zu\n",rc,blocks);return 6;}
        total_ns+=cost;if(cost>max_ns)max_ns=cost;
        for(unsigned j=0;j<s.output_frames;++j) {
            float v[2]={ol[j],or_[j]};
            for(unsigned c=0;c<2;++c) {if(!isfinite(v[c])||fabs(v[c])>8) {fprintf(stderr,"invalid output\n");return 7;} if(fabs(v[c])>peak)peak=fabs(v[c]); double x=fmax(-1.0,fmin(0.999999999,v[c]));out[j*2+c]=(int32_t)llrint(x*2147483648.0);}
        }
        if(fwrite(out,8,s.output_frames,o)!=s.output_frames)return 8;
        ++blocks;
    }
    if(ferror(f)||fclose(o))return 9;
    fprintf(stderr,"DONE blocks=%zu input_frames=%zu process_ms=%.3f worst_block_ms=%.3f block_budget_ms=%.3f peak=%.6f\n",blocks,total_in,total_ns/1e6,max_ns/1e6,s.input_frames*1000.0/ir,peak);
    sony_dsx_close(&s);
    return 0;
}
