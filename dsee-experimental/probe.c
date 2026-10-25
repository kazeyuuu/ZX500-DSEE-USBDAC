#include <dlfcn.h>
#include <stdio.h>
#include <stdint.h>
int main(int argc, char **argv) {
    const char *path = argc > 1 ? argv[1] : "/vendor/lib64/hw/audio.primary.icx1295.so";
    void *h = dlopen(path, RTLD_NOW | RTLD_LOCAL);
    if (!h) { fprintf(stderr, "dlopen: %s\n", dlerror()); return 2; }
    const char *names[] = {"EffectInitDPFDSX", "EffectSetParamDPFDSX", "EffectExecuteDPFDSX", "EffectGetDelaySizeDPFDSX", "dsx_get_handle_size", "check_dsx_param"};
    for (unsigned i = 0; i < sizeof(names)/sizeof(*names); ++i) {
        void *p = dlsym(h, names[i]);
        printf("%s: %s\n", names[i], p ? "available" : "missing");
        if (!p) return 3;
    }
    uint32_t (*get_size)(void) = dlsym(h, "dsx_get_handle_size");
    printf("dsx handle size: %u\n", get_size());
    return 0;
}
