/* Exercise the exact generated realtime transform, without running its ALSA main. */
#define FX_FLAG "/data/local/tmp/zx-fx-contract.flag"
#define FX_PREFS "/data/local/tmp/zx-fx-contract.xml"
#define FX_STATUS "/data/local/tmp/zx-fx-contract.status"
#define DSEE_FLAG "/data/local/tmp/zx-dsee-contract.flag"
#define main upstream_main
#include "../images/dsee-experimental/uac2bridge-dsee.c"
#undef main
static void prefs(int mode,int direct){
 FILE *f=fopen(FX_PREFS,"w");
 fprintf(f,"time=%ld\neq=%d\ndc=%d\ndirect=%d\ntype=4\n",(long)time(NULL),!!(mode&1),!!(mode&2),direct);
 for(int i=0;i<10;++i)fprintf(f,"gain%d=%d\n",i,i==5?12:0);
 fprintf(f,"END\n");fclose(f);
}
int main(void){
 const unsigned rates[]={44100,48000,88200,96000,176400,192000,352800,384000};
 int32_t in[4096*2],out[8192*2+2];unsigned cases=0;
 for(unsigned r=0;r<8;++r)for(int ds=0;ds<2;++ds)for(int mode=0;mode<4;++mode){
  if(ds){FILE *f=fopen(DSEE_FLAG,"w");fclose(f);}else unlink(DSEE_FLAG);
  FILE *f=fopen(FX_FLAG,"w");fclose(f);prefs(mode,0);
  unsigned output=dsee_rate(rates[r]);if(fx_reset(output))return 2;
  /* Missing/stale service data must retain the last valid processing state. */
  struct sony_fx_config saved=fx_config;
  f=fopen(FX_PREFS,"w");fprintf(f,"time=0\neq=\nEND\n");fclose(f);
  if(fx_poll(1)||!fx_unavailable||memcmp(&saved,&fx_config,sizeof(saved)))return 12;
  prefs(mode,0);if(fx_poll(1))return 13;
  unsigned frames=correction_span_frames(rates[r]);if(frames*dsx_ratio>8192)return 3;
  double energy_before=0,energy_after=0;
  for(unsigned b=0;b<24;++b){
   if(b==12){prefs(mode^1,0);if(fx_poll(1))return 9;}
   for(unsigned j=0;j<frames;++j){in[j*2]=(int32_t)(100000*sinf(2*3.141592653589793*1000*(b*frames+j)/rates[r]));in[j*2+1]=j==0&&b==0?100000:0;}
   unsigned n=frames*dsx_ratio*2;for(unsigned j=0;j<n;++j)out[j]=INT32_MIN;out[n]=1234567;out[n+1]=-1234567;
   if(dsee_transform((uint8_t*)in,frames,(uint8_t*)out))return 4;
   if(dsx_enabled||fx_active){for(unsigned j=0;j<n;++j)if(out[j]==INT32_MIN)return 5;}
   const int32_t *actual=dsx_enabled||fx_active?out:in;
   for(unsigned j=0;j<n;j+=2){double x=actual[j];if(b>=6&&b<12)energy_before+=x*x;if(b>=18)energy_after+=x*x;}
   if(out[n]!=1234567||out[n+1]!=-1234567)return 6;
  }
  /* A stock Direct Source toggle must bypass EQ/DC while preserving DSEE. */
  prefs(mode,1);if(fx_poll(1))return 7;
  for(unsigned k=0;k<10;++k)if(dsee_transform((uint8_t*)in,frames,(uint8_t*)out))return 8;
  if(fx_active)return 10;
  double ratio=sqrt(energy_after/energy_before);
  if(output<=192000&&((mode&1)?ratio<1.4:ratio>.75)){fprintf(stderr,"CHAIN unchanged/wrong gain ratio=%f rate=%u mode=%d dsee=%d\n",ratio,rates[r],mode,ds);return 11;}
  ++cases;printf("CHAIN rate=%u dsee=%d mode=%d output=%u ratio=%.5f PASS\n",rates[r],ds,mode,output,ratio);fflush(stdout);
 }
 sony_dsx_close(&bridge_dsx);sony_fx_close(&bridge_fx);unlink(FX_FLAG);unlink(DSEE_FLAG);unlink(FX_PREFS);unlink(FX_STATUS);
 printf("PASS %u exact realtime-transform cases; no ALSA devices opened\n",cases);return 0;
}
