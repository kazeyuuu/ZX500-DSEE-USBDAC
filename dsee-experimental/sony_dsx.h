/* Experimental ABI adapter for ZX505 CHN 4.08, 32-bit vendor HAL only.
 * No Sony code or parameters are embedded. Calls the installed vendor library.
 * Layouts recovered from the firmware's own call sites; not a public Sony API.
 */
#ifndef SONY_DSX_H
#define SONY_DSX_H
#include <dlfcn.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

struct sony_dsx {
    void *lib, *state, *scratch;
    void *handle[2];
    int (*execute)(void *, const float *const *, float *const *);
    unsigned input_frames, output_frames, delay, sub_blocks;
};
static void *dsx_guarded(size_t size) {
    size_t page = (size_t)sysconf(_SC_PAGESIZE);
    size = (size + page - 1) & ~(page - 1);
    char *p = mmap(NULL, size + 2*page, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (p == MAP_FAILED || mprotect(p + page, size, PROT_READ | PROT_WRITE)) return NULL;
    return p + page;
}
static int dsx_read_exact(const char *path, void *buf, size_t bytes) {
    FILE *f = fopen(path, "rb");
    if (!f) { perror(path); return -1; }
    int ok = fread(buf, 1, bytes, f) == bytes && fgetc(f) == EOF;
    fclose(f);
    return ok ? 0 : -1;
}
static unsigned dsx_word(struct sony_dsx *s, unsigned offset) {
    return *(uint32_t *)((char *)s->state + offset);
}
static void sony_dsx_close(struct sony_dsx *s) {
    size_t page=(size_t)sysconf(_SC_PAGESIZE);
    if(s->state) munmap((char *)s->state-page,2*1024*1024+2*page);
    if(s->scratch) munmap((char *)s->scratch-page,2*1024*1024+2*page);
    if(s->lib) dlclose(s->lib);
    memset(s,0,sizeof(*s));
}
static int sony_dsx_open(struct sony_dsx *s, unsigned input_rate, unsigned output_rate) {
    _Static_assert(sizeof(void *) == 4, "Vendor adapter requires ARM32");
    memset(s, 0, sizeof(*s));
    s->lib = dlopen("/vendor/lib/hw/audio.primary.icx1295.so", RTLD_NOW | RTLD_LOCAL);
    if (!s->lib) { fprintf(stderr,"dlopen: %s\n",dlerror()); return -1; }
    int (*init)(void *, void *, unsigned, void *, unsigned) = dlsym(s->lib, "EffectInitDPFDSX");
    int (*set)(void *, const void *, unsigned, unsigned) = dlsym(s->lib, "EffectSetParamDPFDSX");
    int (*check)(const void *) = dlsym(s->lib, "check_dsx_param");
    unsigned (*delay)(void *) = dlsym(s->lib, "EffectGetDelaySizeDPFDSX");
    s->execute = dlsym(s->lib, "dsx_execute_func");
    if (!init || !set || !check || !delay || !s->execute) return -1;
    s->state = dsx_guarded(2*1024*1024);
    s->scratch = dsx_guarded(2*1024*1024);
    if (!s->state || !s->scratch) return -1;
    int32_t *param = calloc(1, 0xeaa8);
    int32_t dcfg[5];
    if (!param) return -1;
    if (dsx_read_exact("/vendor/etc/DseeAi_ICX1295.dcfg",dcfg,sizeof(dcfg)) ||
        dsx_read_exact("/vendor/etc/DseeAi_ICX1295.bin",(char *)param+0x48,60000)) { free(param); return -1; }
    param[0] = 3; /* PCM scalar representation selected by Sony's DsxParam builder */
    param[1] = 2;
    param[2] = (int32_t)input_rate;
    param[3] = (int32_t)output_rate;
    param[4] = -1; /* Unknown source codec: USB carries decoded PCM */
    param[5] = (int32_t)input_rate;
    param[6] = -1; param[7] = -1;
    param[8] = dcfg[0]; param[9] = dcfg[1]; param[10] = dcfg[2];
    param[11] = 0; param[12] = dcfg[3]; param[13] = dcfg[4];
    int r = check(param);
    fprintf(stderr,"DSX param check=%d input=%u output=%u codec=unknown\n",r,input_rate,output_rate);
    if (!r) r = init(s->handle,s->state,2*1024*1024,s->scratch,2*1024*1024);
    fprintf(stderr,"DSX init=%d\n",r);
    if (!r) r = set(s->handle,param,0xeaa8,1);
    free(param);
    fprintf(stderr,"DSX configure=%d\n",r);
    if (r) return -1;
    s->input_frames = dsx_word(s,0x1e73c);
    s->output_frames = dsx_word(s,0x1e740);
    s->sub_blocks = dsx_word(s,0x1e738);
    /* The exported raw executor processes 64-frame sub-blocks. Its initializer
     * leaves the caller's batch count at 1 even when it reports 128->256.
     * The high_pow executor has an explicit ratio=2, batch=2 path (two calls to
     * dsx_process(...,64,...)). Without this descriptor it writes only 128 of
     * the advertised 256 output frames. Verified by NaN/sentinel probes.
     * This is a caller-owned work descriptor, not a patch to Sony's ELF code.
     */
    if((input_rate==88200||input_rate==96000)&&output_rate==input_rate*2&&
       s->input_frames==128&&s->output_frames==256&&s->sub_blocks==1){
        *(uint32_t *)((char *)s->state+0x1e738)=2;
        s->sub_blocks=2;
    }
    s->delay = delay(s->handle);
    fprintf(stderr,"DSX block=%u->%u delay=%u channels=%u sub_blocks=%u flags=%u,%u\n",s->input_frames,s->output_frames,s->delay,dsx_word(s,0x1d594),s->sub_blocks,dsx_word(s,0x1d5a0),dsx_word(s,0x1d5a4));
    if (!s->input_frames || s->input_frames > 8192 || !s->output_frames || s->output_frames > 32768 ||
        (uint64_t)s->output_frames*input_rate != (uint64_t)s->input_frames*output_rate || dsx_word(s,0x1d594)!=2) return -1;
    return 0;
}
static int sony_dsx_process(struct sony_dsx *s, const float *left, const float *right, float *ol, float *or_) {
    const float *in[24] = {left,right}; float *out[24] = {ol,or_};
    return s->execute((char *)s->state+0x1d550,in,out);
}
#endif
