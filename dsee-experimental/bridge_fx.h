/* Follow the stock EQ/DC preferences inside the detached USB audio process. */
#include "sony_fx.h"
#include <unistd.h>
#include <sys/stat.h>
#ifndef FX_FLAG
#define FX_FLAG "/data/adb/usbdac.effects"
#endif
#ifndef FX_PREFS
#define FX_PREFS "/data/user/0/com.zx.dsee/files/sony-settings"
#endif
#ifndef FX_STATUS
#define FX_STATUS "/dev/.zx-effects-status"
#endif
static struct sony_fx bridge_fx,fx_previous;
static struct sony_fx_config fx_config;
static unsigned fx_rate;
static int fx_active,fx_requested,fx_direct,fx_unavailable;
static double fx_last_poll;
static unsigned fx_fade_position,fx_fade_total;
static unsigned long fx_meter_frames;
static double fx_meter_input,fx_meter_output;
static float fx_pcm[8192*2],fx_result[512*2],fx_old_result[512*2];
static long fx_value(const char *text,const char *name,long fallback){
 char key[64];snprintf(key,sizeof(key),"\n%s=",name);
 const char *p=strstr(text,key);if(!p)return fallback;
 char *end;long value=strtol(p+strlen(key),&end,10);
 return end!=p+strlen(key)&&*end=='\n'?value:fallback;
}
static int fx_preferences(struct sony_fx_config *c,int *direct){
 FILE *f=fopen(FX_PREFS,"rb");if(!f)return -1;
 char text[2048];text[0]='\n';size_t n=fread(text+1,1,sizeof(text)-2,f);int complete=feof(f);fclose(f);text[n+1]=0;
 if(!complete||n<4||strcmp(text+n-3,"END\n"))return -1;
 long age=(long)time(NULL)-fx_value(text,"time",0);if(age<0||age>5)return -1;
 memset(c,0,sizeof(*c));*direct=(int)fx_value(text,"direct",-1);
 c->eq=(int)fx_value(text,"eq",-1);c->dc=(int)fx_value(text,"dc",-1);
 if(*direct<0||*direct>1||c->eq<0||c->eq>1||c->dc<0||c->dc>1)return -1;
 if(*direct)c->eq=c->dc=0;
 c->type=(int)fx_value(text,"type",-1);if(c->type<0||c->type>8)return -1;
 for(unsigned i=0;i<10;++i){char key[32];snprintf(key,sizeof(key),"gain%u",i);c->gain[i]=(int)fx_value(text,key,100);if(c->gain[i]<-20||c->gain[i]>20)return -1;}
 return 0;
}
static void fx_write_status(void){
 FILE *f=fopen(FX_STATUS ".next","w");if(!f)return;
 fprintf(f,"FX_PID=%d\nFX_RATE=%u\nFX_REQUESTED=%d\nEQ_ACTIVE=%d\nDC_ACTIVE=%d\nDC_TYPE=%d\nFX_DIRECT=%d\nFX_UNAVAILABLE=%d\n",getpid(),fx_rate,fx_requested,fx_active&&fx_config.eq,fx_active&&fx_config.dc,fx_config.type,fx_direct,fx_unavailable);
 fclose(f);rename(FX_STATUS ".next",FX_STATUS);
}
static int fx_poll(int force){
 if(!fx_fade_total&&!bridge_fx.object)fx_active=0;
 double now=now_ms();if(!force&&now-fx_last_poll<1000)return 0;fx_last_poll=now;
 int requested=!access(FX_FLAG,F_OK),direct=0;
 struct sony_fx_config next={0};
 if(requested&&fx_preferences(&next,&direct)){
  if(!fx_unavailable||force){fx_unavailable=1;fx_requested=requested;fx_write_status();}
  return 0; /* An incomplete atomic preference update is retried next poll. */
 }
 if(!force&&!fx_unavailable&&requested==fx_requested&&direct==fx_direct&&!memcmp(&next,&fx_config,sizeof(next)))return 0;
 fx_requested=requested;fx_direct=direct;fx_unavailable=0;
 int enabled=requested&&(next.eq||next.dc);
 if(enabled&&fx_rate>192000){enabled=0;fx_unavailable=1;}
 /* Updating the stock chain in place can return an empty transition block.
  * Create a fresh configured chain and crossfade its full blocks instead. */
 struct sony_fx replacement={0};
 if(enabled&&sony_fx_open(&replacement,fx_rate,&next))return -1;
 sony_fx_close(&fx_previous);fx_previous=bridge_fx;bridge_fx=replacement;
 fx_fade_position=0;fx_fade_total=(bridge_fx.object||fx_previous.object)?fx_rate/50:0;
 fx_active=bridge_fx.object||fx_previous.object;
 fx_config=next;fx_write_status();
 fx_meter_frames=0;fx_meter_input=fx_meter_output=0;
 fprintf(stderr,"FX settings rate=%u requested=%d eq=%d dc=%d type=%d direct=%d unavailable=%d\n",fx_rate,requested,fx_active&&next.eq,fx_active&&next.dc,next.type,direct,fx_unavailable);
 return 0;
}
static int fx_reset(unsigned rate){
 sony_fx_close(&bridge_fx);sony_fx_close(&fx_previous);fx_fade_total=0;fx_active=0;fx_rate=rate;memset(&fx_config,0,sizeof(fx_config));return fx_poll(1);
}
static int fx_process_pcm(unsigned frames,int32_t *out){
 if(frames>8192||frames%512)return -1;
 for(unsigned base=0;base<frames;base+=512){
  if(bridge_fx.object){if(sony_fx_process(&bridge_fx,fx_pcm+base*2,512,fx_result))return -1;}
  else memcpy(fx_result,fx_pcm+base*2,sizeof(fx_result));
  if(fx_fade_total){
   if(fx_previous.object){if(sony_fx_process(&fx_previous,fx_pcm+base*2,512,fx_old_result))return -1;}
   else memcpy(fx_old_result,fx_pcm+base*2,sizeof(fx_old_result));
   for(unsigned j=0;j<512;++j){
    double t=fmin(1,(double)(fx_fade_position+j)/fx_fade_total);double w=.5-.5*cos(3.141592653589793*t);
    for(unsigned c=0;c<2;++c)fx_result[2*j+c]=(float)(fx_old_result[2*j+c]*(1-w)+fx_result[2*j+c]*w);
   }
   fx_fade_position+=512;if(fx_fade_position>=fx_fade_total){fx_fade_total=0;sony_fx_close(&fx_previous);}
  }
  for(unsigned j=0;j<1024;j+=16){double a=fx_pcm[base*2+j],b=fx_result[j];fx_meter_input+=a*a;fx_meter_output+=b*b;}
  fx_meter_frames+=512;
  if(fx_meter_frames>=fx_rate*5){
   double db=fx_meter_input>1e-20?10*log10(fmax(1e-30,fx_meter_output)/fx_meter_input):-999;
   fprintf(stderr,"FX POWER frames=%lu input_energy=%.9g output_energy=%.9g change_db=%.3f eq=%d dc=%d type=%d gains=%d,%d,%d,%d,%d,%d,%d,%d,%d,%d\n",fx_meter_frames,fx_meter_input,fx_meter_output,db,fx_config.eq,fx_config.dc,fx_config.type,fx_config.gain[0],fx_config.gain[1],fx_config.gain[2],fx_config.gain[3],fx_config.gain[4],fx_config.gain[5],fx_config.gain[6],fx_config.gain[7],fx_config.gain[8],fx_config.gain[9]);
   fx_meter_frames=0;fx_meter_input=fx_meter_output=0;
  }
  for(unsigned j=0;j<1024;++j){
   double x=fx_result[j];if(x>=1||x< -1)++dsx_clips;
   x=fmax(-1,fmin(8388607.0/8388608.0,x));out[base*2+j]=(int32_t)llrint(x*8388608.0);
  }
 }
 return 0;
}
