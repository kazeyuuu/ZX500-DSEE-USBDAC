#include "sony_fx.h"
#include <time.h>
/* All processing is offline. Each case creates a fresh native filter chain. */
int main(int argc,char **argv){
 if(argc>2)return 2;
 const unsigned rates[]={44100,48000,88200,96000,176400,192000,352800,384000};
 for(unsigned r=0;r<8;++r)for(int mode=0;mode<14;++mode){
  if(rates[r]>192000&&mode){printf("BYPASS_POLICY rate=%u mode=%d\n",rates[r],mode);continue;}
  struct sony_fx_config c={0};
  if(mode==1)c.eq=1;
  if(mode==2){c.eq=1;c.gain[5]=12;}
  if(mode==3){c.eq=1;c.gain[5]=-12;}
  if(mode>=4&&mode<=12){c.dc=1;c.type=mode-4;}
  if(mode==13){c.eq=c.dc=1;c.gain[5]=12;c.type=4;}
  struct sony_fx fx;
  if(sony_fx_open(&fx,rates[r],&c)){printf("UNSUPPORTED rate=%u mode=%d\n",rates[r],mode);fflush(stdout);continue;}
  FILE *f=NULL;if(argc==2){char name[256];snprintf(name,sizeof(name),"%s/%u-%d.f32",argv[1],rates[r],mode);f=fopen(name,"wb");if(!f)return 3;}
  float in[1024],out[1026];double peak=0,square=0,re20=0,im20=0,re100=0,im100=0;
  for(unsigned block=0;block<192;++block){
   for(unsigned j=0;j<512;++j){unsigned t=block*512+j;in[2*j]=.05f*sinf(2*3.141592653589793*1000*t/rates[r]);in[2*j+1]=t==0?.1f:0;}
   for(unsigned j=0;j<1026;++j)out[j]=NAN;out[1024]=12345;out[1025]=-12345;
   if(sony_fx_process(&fx,in,512,out)||out[1024]!=12345||out[1025]!=-12345)return 4;
   for(unsigned j=0;j<1024;++j){if(!isfinite(out[j]))return 5;if(fabs(out[j])>peak)peak=fabs(out[j]);}
   for(unsigned j=0;j<512;++j){double t=(block*512+j)/(double)rates[r];if(block>=96)square+=out[2*j]*out[2*j];re20+=out[2*j+1]*cos(2*3.141592653589793*20*t);im20-=out[2*j+1]*sin(2*3.141592653589793*20*t);re100+=out[2*j+1]*cos(2*3.141592653589793*100*t);im100-=out[2*j+1]*sin(2*3.141592653589793*100*t);}
   if(f&&fwrite(out,sizeof(float),1024,f)!=1024)return 6;
  }
  if(f)fclose(f);sony_fx_close(&fx);printf("CASE rate=%u mode=%d peak=%.7f rms=%.9f mag20=%.8f phase20=%.5f mag100=%.8f phase100=%.5f\n",rates[r],mode,peak,sqrt(square/(96*512)),hypot(re20,im20)/.1,atan2(im20,re20)*180/3.141592653589793,hypot(re100,im100)/.1,atan2(im100,re100)*180/3.141592653589793);fflush(stdout);
 }
 /* Negative capability control: native EQ must not be presented as active at
  * 352.8/384 kHz when the stock chain cannot return a complete float block. */
 for(unsigned r=6;r<8;++r){
  struct sony_fx_config c={.eq=1};struct sony_fx fx;float in[1024]={0},out[1024];
  int opened=sony_fx_open(&fx,rates[r],&c);
  int processed=opened?-1:sony_fx_process(&fx,in,512,out);
  sony_fx_close(&fx);
  if(!opened&&!processed){fprintf(stderr,"High-rate capability changed; review policy\n");return 7;}
  printf("HIGH_RATE_REJECTED rate=%u open=%d process=%d\n",rates[r],opened,processed);
 }
 return 0;
}
