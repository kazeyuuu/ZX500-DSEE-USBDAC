/* ZX505 CHN 4.08 ARM32: optional original EQ10 + DC phase processor.
 * Only ABI declarations and setting names; no Sony code or coefficients.
 */
#ifndef SONY_FX_H
#define SONY_FX_H
#include <dlfcn.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
struct sony_fx_config { int eq,dc,type,gain[10]; };
struct sony_fx_buffer { void *data; uint32_t capacity,size; };
struct sony_fx {
 void *lib,*object;
 int (*set)(void*,const char*);
 int (*open)(void*,const void*,const void*);
 int (*process)(void*,const void*,void*,void*);
 int (*close)(void*);
 void (*destroy)(void*);
 void (*buffer_destroy)(void*);
 struct sony_fx_buffer output;
 unsigned rate;
};
static const char *fx_bands[]={"31hz","62hz","125hz","250hz","500hz","1khz","2khz","4khz","8khz","16khz"};
static const char *fx_dc_types[]={"type_a_low","type_a_normal","type_a_high","type_b_low","type_b_normal","type_b_high","type_c_low","type_c_normal","type_c_high"};
static void sony_fx_close(struct sony_fx *s){
 if(s->output.data&&s->buffer_destroy)s->buffer_destroy(&s->output);
 if(s->object&&s->destroy){if(s->close)s->close(s->object);s->destroy(s->object);}
 if(s->lib)dlclose(s->lib);
 memset(s,0,sizeof(*s));
}
static int sony_fx_set(struct sony_fx *s,const struct sony_fx_config *c){
 if(!s->object||c->type<0||c->type>8)return -1;
 char text[1024];int n=snprintf(text,sizeof(text),"izm_eq_10=%s;izm_dc_phase_linearizer=%s;izm_dc_phase_linearizer_type=%s",c->eq?"on":"off",c->dc?"on":"off",fx_dc_types[c->type]);
 for(unsigned i=0;i<10;++i){if(c->gain[i]<-20||c->gain[i]>20)return -1;n+=snprintf(text+n,sizeof(text)-(size_t)n,";izm_eq_10_band_%s=%d",fx_bands[i],c->gain[i]);}
 if(n<0||(size_t)n>=sizeof(text))return -1;
 return s->set(s->object,text);
}
static int sony_fx_open(struct sony_fx *s,unsigned rate,const struct sony_fx_config *config){
 _Static_assert(sizeof(void*)==4,"Sony FX ABI requires ARM32");
 memset(s,0,sizeof(*s));s->rate=rate;
 s->lib=dlopen("/vendor/lib/hw/audio.primary.icx1295.so",RTLD_NOW|RTLD_LOCAL);
 if(!s->lib)return -1;
#define FX_LOAD(field,symbol) do {*(void**)(&s->field)=dlsym(s->lib,symbol);if(!s->field)goto fail;} while(0)
 FX_LOAD(set,"_ZN3izm7android15effectprocessor18IzmEffectProcessor9SetParamsEPKc");
 FX_LOAD(open,"_ZN3izm7android15effectprocessor18IzmEffectProcessor4OpenERKNS0_11AudioConfigES5_");
 FX_LOAD(process,"_ZN3izm7android15effectprocessor18IzmEffectProcessor7ProcessERKNS0_9BufferPtrEPNS0_12UniqueBufferEPNS0_11AudioConfigE");
 FX_LOAD(close,"_ZN3izm7android15effectprocessor18IzmEffectProcessor5CloseEv");
 FX_LOAD(destroy,"_ZN3izm7android15effectprocessor18IzmEffectProcessorD0Ev");
 FX_LOAD(buffer_destroy,"_ZN3izm7android12UniqueBufferD1Ev");
#undef FX_LOAD
 void (*create)(void*,const void*,const void*)=dlsym(s->lib,"_ZN3izm7android19IIzmEffectProcessor6CreateERKNSt3__16vectorIPKcNS2_9allocatorIS5_EEEERKNS1_10CreateArgsE");
 void (*fixed)(void*,const unsigned*)=dlsym(s->lib,"_ZN3izm7android8IepTypes12VolumeConfig14UseFixedVolumeERKj");
 void (*defaults)(void*)=dlsym(s->lib,"_ZN3izm7android8IepTypes11ParamConfig10UseDefaultEv");
 void (*args_ctor)(void*,const void*,const void*)=dlsym(s->lib,"_ZN3izm7android19IIzmEffectProcessor10CreateArgsC1ERKNS0_8IepTypes12VolumeConfigERKNS3_11ParamConfigE");
 void (*vol_destroy)(void*)=dlsym(s->lib,"_ZN3izm7android8IepTypes12VolumeConfigD1Ev");
 void (*param_destroy)(void*)=dlsym(s->lib,"_ZN3izm7android8IepTypes11ParamConfigD1Ev");
 if(!create||!fixed||!defaults||!args_ctor||!vol_destroy||!param_destroy)goto fail;
 const char *ids[]={"E:Eq10","E:DcPhaseLinearizer"};
 /* libc++ ARM32 vector<const char*> and unique_ptr return slot. */
 const void *vector[]={ids,ids+2,ids+2};void *result[4]={0};
 uint64_t volume[32]={0},params[32]={0},args[64]={0};unsigned level=120;
 fixed(volume,&level);defaults(params);
 /* Fixed-volume convenience defaults to half-strength EQ (unityGain=0.5).
  * Use 1.0, matching the stock FollowToMasterVolume configuration, without
  * adding a master-volume filter or changing the hardware volume. */
 *(float*)((char*)volume+12)=1.0f;
 args_ctor(args,volume,params);create(result,vector,args);s->object=result[0];
 param_destroy((char*)args+112);vol_destroy(args);param_destroy(params);vol_destroy(volume);
 if(!s->object||sony_fx_set(s,config))goto fail;
 /* AudioConfig is {rate, format, channels}; 0x120 = interleaved float32.
  * Verified against the library's audio_format_t -> Format mapping. */
 uint32_t ac[]={rate,0x120,2};
 if(s->open(s->object,ac,ac))goto fail;
 return 0;
fail:sony_fx_close(s);return -1;
}
static int sony_fx_process(struct sony_fx *s,const float *input,unsigned frames,float *output){
 if(!s->object||frames>8192)return -1;
 uint32_t bytes=frames*2u*sizeof(float);
 struct {const void *data;uint32_t size,offset;}in={input,bytes,0};
 uint32_t config[3]={0};
 int rc=s->process(s->object,&in,&s->output,config);
 if(rc||s->output.size!=bytes||s->output.capacity<bytes||!s->output.data||config[0]!=s->rate||config[1]!=0x120||config[2]!=2){fprintf(stderr,"FX invalid rc=%d size=%u expected=%u capacity=%u format=%u/%x/%u\n",rc,s->output.size,bytes,s->output.capacity,config[0],config[1],config[2]);return -1;}
 const float *p=s->output.data;
 for(unsigned i=0;i<frames*2;++i)if(!isfinite(p[i])||fabsf(p[i])>16)return -1;
 memcpy(output,p,bytes);return 0;
}
#endif
