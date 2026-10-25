#include <tinyalsa/pcm.h>
#include <tinyalsa/mixer.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdint.h>
#include <time.h>
#include <pthread.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <sys/ioctl.h>
#include <sound/asound.h>

/* ZX500 USB-DAC production bridge.
 *
 * Follows the host-selected sample rate through the kernel's "Capture Current
 * Rate" control and corrects host/DAC clock drift one frame at a time. The
 * release kernel and this bridge are one matched product.
 *
 * Auto-rate state machine:
 *   1. read "Capture Current Rate" from card2
 *   2. open card2 capture at that rate
 *   3. block on the FIRST pcm_read()                 (host is streaming)
 *   4. re-read the rate; unless valid AND unchanged, close capture and retry
 *   5. open card1 playback at that rate
 *   6. DROP + PREPARE capture (discard the pcm_open(card1) backlog)
 *   7. fine-drift bridge loop (below)
 *   on READ_FAIL / HOST_STALL / XRUN-storm / rate change -> close both, go to 1
 *
 * Fine drift: the adaptive USB sink and DAC use independent clocks, so the
 * capture ring slowly fills/empties. A water-level controller (capture avail,
 * low-passed, dead-band,
 * credit/rate-limited) consumes exactly one frame more/less than it produces. The
 * one-frame phase change is spread over a rate-derived 8-12ms correction block
 * with a smooth phase ramp and cubic interpolation. A normal 512-frame period
 * takes a sample-exact fast path. Capture stops at one full ring instead
 * of tinyalsa's permissive 10-ring default; a high-water observation or capture
 * XRUN therefore causes a clean re-handshake, never a late audible period drop or
 * silent forwarding after ring overwrite.
 *
 * All controller state lives inside the outer loop, so every re-handshake / rate
 * change starts from a clean filter/credit/direction -- 44.1k state never leaks into
 * 384k. Production derives period_count from a 40ms buffer target and rounds up to
 * a tested power of two (yielding 4/8/16/32 across the eight rates). Every band top
 * gets the same ~42.7ms ring, i.e. >=16ms of worst-case
 * scheduler-stall margin below the emergency mark (the release kernel allows a
 * 128KB gadget ring to admit 32 periods; both cards were verified to
 * accept 512x32 at 384k). Water marks are fractions of that actual buffer. The
 * 150ms tick and 5s grace are wall-clock, not rate-derived.
 *
 * Build -DBRIDGE_DIAG for the diagnostic binary (10 s stats to stderr); production
 * is silent and the launcher sends output to /dev/null (no flash writes).
 *
 * The release layout is fixed: card2 capture -> card1 playback, device 0,
 * stereo 24-bit. In BRIDGE_DIAG only, an optional argument -1/+1 forces
 * expand/compress about once per second using the current correction span.
 */

#ifdef BRIDGE_DIAG
#define DLOG(...)  fprintf(stderr, __VA_ARGS__)
#define DFLUSH()   fflush(stderr)
#else
#define DLOG(...)  do {} while (0)
#define DFLUSH()   do {} while (0)
#endif

#define STALL_MS          100.0
#define PERIOD            512u

/* Rate-independent drift-control policy. Frame counts and event rates are derived
 * from time/ppm below, so all eight sample rates use the same control standard. */
#define BUFFER_TARGET_US  40000u  /* ~=42.7/46.4 ms after power-of-two rounding */
#define CORR_SPAN_US      8000u   /* one-frame correction spread over >=8 ms */
#define CONTROL_TICK_MS   150.0
#define EMERG_POLL_MS     50.0
#define WL_FILTER_DIV     8.0
#define CORR_MILD_PPM     40.0
#define CORR_STRONG_PPM   80.0
#define CORR_CREDIT_CAP   1.0     /* never release a post-stall burst of corrections */

/* Water-level controller marks are derived from the actual ALSA buffer
 * (PERIOD*period_count frames) at runtime, as buffer fractions, so any period_count
 * works and one controller serves all eight rates. Fractions: low 1/8, target 3/8,
 * high 1/2, emergency 7/8 (at pcount=4: 256/768/1024/1792). Corrections engage at
 * low/high and, via hysteresis, persist until the level returns to target. */
#define WL_GRACE_MS 5000.0

#define RATE_CTL_NAME "Capture Current Rate"
/* The eight rates offered by the release kernel. */
#define RATE_OK(r) ((r)==44100 || (r)==48000 || (r)==88200 || (r)==96000 || \
                    (r)==176400 || (r)==192000 || (r)==352800 || (r)==384000)

static double now_ms(void){
    struct timespec ts; clock_gettime(CLOCK_MONOTONIC,&ts);
    return ts.tv_sec*1000.0 + ts.tv_nsec/1e6;
}

static unsigned production_pcount(unsigned rate){
    uint64_t denom = (uint64_t)PERIOD * 1000000u;
    uint64_t numer = (uint64_t)rate * BUFFER_TARGET_US;
    unsigned need = (unsigned)((numer + denom - 1) / denom);
    unsigned pcount = 4;
    while(pcount < need && pcount < 32) pcount <<= 1;
    return pcount;
}

static unsigned correction_span_frames(unsigned rate){
    uint64_t wanted = (uint64_t)rate * CORR_SPAN_US;
    unsigned frames = (unsigned)((wanted + 999999u) / 1000000u);
    unsigned periods = (frames + PERIOD - 1) / PERIOD;
    if(periods < 1) periods = 1;
    if(periods > 8) periods = 8; /* 384k needs six; bound allocations defensively */
    return periods * PERIOD;
}

static double correction_frames_per_sec(unsigned rate, double ppm){
    return (double)rate * ppm / 1000000.0;
}

#ifdef BRIDGE_DIAG
struct hist { unsigned long n10,n20,n23,n30,n46; double max; };
static void hadd(struct hist *h, double ms){
    if(ms>h->max) h->max=ms;
    if(ms>10) h->n10++;
    if(ms>20) h->n20++;
    if(ms>23) h->n23++;
    if(ms>30) h->n30++;
    if(ms>46) h->n46++;
}
static void hreset(struct hist *h){ memset(h,0,sizeof *h); }
#endif

/* Host and hardware control names. */
#define VOL_CTL_NAME  "Capture Host Volume"
#define MUTE_CTL_NAME "Capture Host Mute"
#define HW_VOL_NAME   "master volume"    /* card1 int 0..120 (Sony UI/table index) */
#define HW_MUTE_NAME  "playback mute"    /* card1 bool */
/* The host volume range is fixed at the bottom and configurable at the top: the
 * user picks the ceiling, usb-dac.sh writes it to configfs, and the kernel then
 * advertises it both in the Feature Unit and as this control's range maximum. The
 * bridge reads it from the control instead of keeping a second copy that could
 * disagree. Q8.8 dB throughout; one 0.5 dB step is one Sony hardware index. */
#define VOL_Q8_MIN    (-15360)  /* -60 dB = hardware index 0 */
#define VOL_Q8_STEP   128       /* 0.5 dB = one index */
#define VOL_Q8_CEIL   0         /* 0 dB = index 120, Sony full scale */
#define VOL_Q8_SAFE   (-5120)   /* -20 dB; used when the range reads implausible */
#define CONTROL_RETRY_US 200000
#define CONTROL_RESYNC_MS 2000

/* All host controls stay off the PCM thread: a mixer ioctl can take longer than
 * one 384k period. One event-driven watcher owns the card2 control handle and
 * publishes the rate atomically; the audio loop only performs a non-blocking load.
 * Volume/mute hardware writes also run here, using short-lived card1 handles. */
static int g_current_rate = 0;
static unsigned g_mixer_card = 2;
static unsigned g_play_card = 1;         /* cxd3778gf DAC (hardware vol/mute) */

static int current_rate_load(void){
    return __atomic_load_n(&g_current_rate, __ATOMIC_ACQUIRE);
}

/* Apply one already-validated host volume/mute target. The ordering is fail-safe:
 * mute before a level change, and level before unmute. On failure last_* remains
 * stale, so the 2s resync retries the same target without a busy loop. */
static void apply_host_controls(int hw, int mute, int *last_hw, int *last_mute){
    int ok = 0;
    struct mixer *m1 = mixer_open(g_play_card);
    struct mixer_ctl *hv = m1 ? mixer_get_ctl_by_name(m1, HW_VOL_NAME) : NULL;
    struct mixer_ctl *hm = m1 ? mixer_get_ctl_by_name(m1, HW_MUTE_NAME) : NULL;

    if(m1 && hv && hm){
        ok = 1;
        if(mute == 1){
            if(mixer_ctl_set_value(hm, 0, 1) == 0) *last_mute = 1; else ok = 0;
            if(ok && hw != *last_hw){
                if(mixer_ctl_set_value(hv, 0, hw) == 0) *last_hw = hw; else ok = 0;
            }
        } else {
            if(hw != *last_hw){
                if(mixer_ctl_set_value(hv, 0, hw) == 0) *last_hw = hw; else ok = 0;
            }
            if(ok && *last_mute != 0){
                if(mixer_ctl_set_value(hm, 0, 0) == 0) *last_mute = 0; else ok = 0;
            }
        }
    }
    if(m1) mixer_close(m1);
}

/* Top of the host volume range, taken from the control the kernel publishes it on.
 * A value that is out of range or off the 0.5 dB grid cannot be trusted to convert
 * to a hardware index, so fall back to the conservative -20 dB rather than to
 * whatever the read returned. */
static int host_vol_ceiling(struct mixer_ctl *host_vol){
    int vmax = mixer_ctl_get_range_max(host_vol);
    if(vmax < VOL_Q8_MIN || vmax > VOL_Q8_CEIL ||
       ((vmax - VOL_Q8_MIN) % VOL_Q8_STEP) != 0)
        return VOL_Q8_SAFE;
    return vmax;
}

/* Read all three controls after an ALSA event (or the low-frequency liveness
 * timeout). Reading them together makes event coalescing harmless. */
static void sync_host_controls(struct mixer_ctl *rate_ctl,
                               struct mixer_ctl *host_vol,
                               struct mixer_ctl *host_mute,
                               int *last_hw, int *last_mute){
    int rate = mixer_ctl_get_value(rate_ctl, 0);
    int vq8 = mixer_ctl_get_value(host_vol, 0);
    int mute = mixer_ctl_get_value(host_mute, 0);

    __atomic_store_n(&g_current_rate, RATE_OK(rate) ? rate : 0,
                     __ATOMIC_RELEASE);

    /* tinyalsa returns -1 on a failed read. Only accept the kernel's 0.5dB
     * grid; an anomalous read must never become a loud target. */
    int vq8_max = host_vol_ceiling(host_vol);
    if(vq8 >= VOL_Q8_MIN && vq8 <= vq8_max &&
       ((vq8 - VOL_Q8_MIN) % VOL_Q8_STEP) == 0 && (mute == 0 || mute == 1)){
        int hw = (vq8 - VOL_Q8_MIN) / VOL_Q8_STEP;
        int hw_max = (vq8_max - VOL_Q8_MIN) / VOL_Q8_STEP;
        if(hw > hw_max) hw = hw_max;
        if(hw != *last_hw || mute != *last_mute)
            apply_host_controls(hw, mute, last_hw, last_mute);
    }
}

/* One event-driven watcher replaces the former 10ms rate poller and 100ms
 * volume/mute poller. Kernel snd_ctl_notify() wakes it immediately on a real
 * value change. A 2s timeout is only a recovery/resync guard, not control
 * polling. The launcher stops the bridge before a gadget rebind, so this
 * process-lifetime card2 handle cannot pin a recreated gadget card. */
static void *control_event_thread(void *arg){
    int last_hw = -1, last_mute = -1;
    (void)arg;

    for(;;){
        char ctl_path[48];
        int subscribe = 1;
        struct mixer *m2 = mixer_open(g_mixer_card);
        struct mixer_ctl *rate_ctl = m2 ? mixer_get_ctl_by_name(m2, RATE_CTL_NAME) : NULL;
        struct mixer_ctl *host_vol = m2 ? mixer_get_ctl_by_name(m2, VOL_CTL_NAME) : NULL;
        struct mixer_ctl *host_mute = m2 ? mixer_get_ctl_by_name(m2, MUTE_CTL_NAME) : NULL;
        snprintf(ctl_path, sizeof(ctl_path), "/dev/snd/controlC%u", g_mixer_card);
        int event_fd = open(ctl_path, O_RDONLY | O_NONBLOCK);

        if(!m2 || !rate_ctl || !host_vol || !host_mute || event_fd < 0 ||
           ioctl(event_fd, SNDRV_CTL_IOCTL_SUBSCRIBE_EVENTS, &subscribe) != 0){
            if(event_fd >= 0) close(event_fd);
            if(m2) mixer_close(m2);
            __atomic_store_n(&g_current_rate, 0, __ATOMIC_RELEASE);
            usleep(CONTROL_RETRY_US);
            continue;
        }

        /* Subscribe before the first snapshot so a concurrent SET_CUR cannot be
         * lost between initial state acquisition and event monitoring. */
        sync_host_controls(rate_ctl, host_vol, host_mute, &last_hw, &last_mute);

        for(;;){
            struct pollfd pfd;
            int wait_rc;
            pfd.fd = event_fd;
            pfd.events = POLLIN | POLLERR | POLLHUP | POLLNVAL;
            pfd.revents = 0;
            do {
                wait_rc = poll(&pfd, 1, CONTROL_RESYNC_MS);
            } while(wait_rc < 0 && errno == EINTR);
            if(wait_rc < 0 || (pfd.revents & (POLLERR | POLLHUP | POLLNVAL)))
                break;
            if(wait_rc > 0){
                struct snd_ctl_event event;
                ssize_t n;
                /* Drain the non-blocking ALSA event fd. State is read once below,
                 * so a burst of rate+volume+mute notifications coalesces safely. */
                do {
                    n = read(event_fd, &event, sizeof(event));
                } while(n == (ssize_t)sizeof(event));
                if(n < 0 && errno != EAGAIN && errno != EWOULDBLOCK)
                    break;
            }
            /* On an event this is the immediate update path; on timeout it is
             * the bounded lost-event/card-liveness safety sync. */
            sync_host_controls(rate_ctl, host_vol, host_mute,
                               &last_hw, &last_mute);
        }

        subscribe = 0;
        ioctl(event_fd, SNDRV_CTL_IOCTL_SUBSCRIBE_EVENTS, &subscribe);
        close(event_fd);
        mixer_close(m2);
        __atomic_store_n(&g_current_rate, 0, __ATOMIC_RELEASE);
        usleep(CONTROL_RETRY_US);
    }
    return NULL;
}

/* Read the host-selected rate. Returns >0 for a valid rate, or 0 on any failure
 * so the caller backs off and retries; the bridge never guesses a fixed rate. */
static int read_current_rate(void){
    return current_rate_load();
}

static void backoff(unsigned *fails){
    unsigned n = ++(*fails);
    unsigned ms = 200u << (n > 4 ? 4 : n - 1);
    if(ms > 2000u) ms = 2000u;
    usleep(ms * 1000u);
}

static unsigned g_ch;

/* ---- sample unpack/pack (native magnitude int32; ch-interleaved) ---- */
static inline int32_t unpack1(const uint8_t *p){
    int32_t v=p[0]|(p[1]<<8)|(p[2]<<16);
    if(v&0x800000)v|=0xFF000000;
    return v;
}
static inline void pack_play(int32_t v, uint8_t *p){
    p[0]=v; p[1]=v>>8; p[2]=v>>16; p[3]=v>>24;
}
static inline int cap_bpf(void){ return 3 * g_ch; }
static inline int play_bpf(void){ return 4 * g_ch; }

static int32_t clip_native(int64_t v){
    if(v >  0x7FFFFF) v =  0x7FFFFF;
    if(v < -0x800000) v = -0x800000;
    return (int32_t)v;
}

/* Four-point Catmull-Rom interpolation. At the two input edges, where four real
 * neighbours do not exist, use the linear two-point form. Exact integer phases
 * always return the original sample, so the correction block joins the untouched
 * fast path without a boundary value discontinuity. */
static int32_t interp_cubic(const int32_t *in, unsigned in_frames,
                            unsigned ch, unsigned c, uint32_t src_q16){
    unsigned f = src_q16 >> 16;
    uint32_t t = src_q16 & 0xFFFFu;
    if(f >= in_frames) f = in_frames - 1;
    if(t == 0 || f + 1 >= in_frames)
        return in[f*ch+c];
    if(f == 0 || f + 2 >= in_frames){
        int64_t a = in[f*ch+c], b = in[(f+1)*ch+c];
        return clip_native((a*(int64_t)(65536u-t) + b*(int64_t)t) >> 16);
    }
    {
        int64_t p0=in[(f-1)*ch+c], p1=in[f*ch+c];
        int64_t p2=in[(f+1)*ch+c], p3=in[(f+2)*ch+c];
        int64_t q = -p0 + 3*p1 - 3*p2 + p3;
        q = ((q * t) >> 16) + (2*p0 - 5*p1 + 4*p2 - p3);
        q = ((q * t) >> 16) + (-p0 + p2);
        q = ((q * t) >> 16) + 2*p1;
        q /= 2;
        /* Plain Catmull-Rom can overshoot between adjacent full-scale samples.
         * Keep the cubic shape but make it range-preserving: drift correction
         * must never create a peak louder than either bracketing source sample. */
        if(q < (p1 < p2 ? p1 : p2)) q = (p1 < p2 ? p1 : p2);
        if(q > (p1 > p2 ? p1 : p2)) q = (p1 > p2 ? p1 : p2);
        return clip_native(q);
    }
}

/* Consume out_frames +/- one input frame and produce exactly out_frames. The
 * phase offset follows smoothstep (3x^2-2x^3): its slope is zero at both ends,
 * so source-time velocity is exactly 1 before and after the correction block.
 * The block ends at an integer source phase and therefore needs no permanent
 * resampler state; the next untouched period resumes sample-exactly. */
static void resample_correction(const int32_t *in, unsigned in_frames,
                                int32_t *out, unsigned out_frames, unsigned ch){
    int delta = (int)in_frames - (int)out_frames; /* exactly +1 or -1 */
    uint32_t phase = 0, phase_rem = 0;
    uint32_t phase_step, phase_step_rem, phase_den;
    if(delta == 0){
        memcpy(out, in, (size_t)out_frames*ch*sizeof(int32_t));
        return;
    }
    phase_den = out_frames - 1;
    phase_step = 65536u / phase_den;
    phase_step_rem = 65536u % phase_den;
    for(unsigned k=0;k<out_frames;k++){
        uint32_t x = phase;
        uint32_t x2 = (uint32_t)(((uint64_t)x * x) >> 16);
        uint32_t smooth = (uint32_t)(((uint64_t)x2 * (3u*65536u - 2u*x)) >> 16);
        int64_t src = ((int64_t)k << 16) + (int64_t)delta * smooth;
        uint32_t src_q16 = src < 0 ? 0 : (uint32_t)src;
        for(unsigned c=0;c<ch;c++){
            out[k*ch+c] = interp_cubic(in,in_frames,ch,c,src_q16);
        }
        phase += phase_step;
        phase_rem += phase_step_rem;
        if(phase_rem >= phase_den){ phase++; phase_rem -= phase_den; }
    }
}

int main(int argc, char **argv) {
    const unsigned cc=2, cd=0, pc=1, pd=0, ch=2;
#ifdef BRIDGE_DIAG
    int force_dir=0;
    if(argc >= 2){
        int force=atoi(argv[1]);
        if(force == -1) force_dir=-1;
        else if(force == 1) force_dir=1;
        else return 2;
    }
#else
    (void)argv;
    if(argc != 1) return 2;
#endif
    g_mixer_card = cc;     /* gadget card carries rate + host volume/mute controls */
    g_play_card = pc;      /* card1 (DAC) carries the hardware volume/mute controls */

    /* All host control I/O is isolated from the real-time PCM loop. */
    {
        pthread_t ct;
        if(pthread_create(&ct, NULL, control_event_thread, NULL) != 0)
            return 3;
        pthread_detach(ct);
    }

    const enum pcm_format cap_fmt=PCM_FORMAT_S24_3LE;
    const enum pcm_format play_fmt=PCM_FORMAT_S24_LE;

    g_ch=ch;
    unsigned fails=0;
#ifdef BRIDGE_DIAG
    unsigned long recovery_attempts=0, rate_switches=0, highwater_recoveries=0;
#endif

    for(;;){
        /* 1. what rate does the host want right now? Never guess a fixed rate. */
        int rate = read_current_rate();
        if(rate <= 0){
            DLOG("rate read failed; backing off\n"); DFLUSH();
            backoff(&fails); continue;
        }
        /* Keep one nominal buffer duration at every rate. Round period_count up to
         * a tested power of two; this reproduces 4/8/16/32 without rate bands. */
        unsigned pcount = production_pcount((unsigned)rate);
        unsigned corr_span = correction_span_frames((unsigned)rate);

        /* Water marks are fractions of the actual ALSA buffer. */
        int wl_buf   = PERIOD * (int)pcount;
        int wl_low   = wl_buf / 8;
        int wl_target= wl_buf * 3 / 8;
        /* Engage compression at 1/2 (was 5/8): the fast-host resting level then
         * cycles between target (3/8) and high (1/2) instead of parking at 5/8,
         * which left only ~1/4 buffer to the emergency mark -- a 5-12ms scheduler
         * stall was enough to force an audible whole-period drop (or, worse, a
         * silent u_audio ring overwrite). Dead band target..high is still buf/8
         * wide, so ppm-level drift cannot make the controller chatter. */
        int wl_high  = wl_buf / 2;
        int wl_emerg = wl_buf * 7 / 8;
        int wl_hi2   = wl_high + wl_buf / 8;
        int wl_lo2   = wl_low  - wl_buf / 16;

        struct pcm_config cin, cout;
        memset(&cin,0,sizeof cin); memset(&cout,0,sizeof cout);
        cin.channels=ch; cin.rate=rate; cin.period_size=PERIOD; cin.period_count=pcount; cin.format=cap_fmt;
        /* tinyalsa otherwise gives capture a stop_threshold of ten complete
         * rings. u_audio can overwrite old samples long before that threshold,
         * so stop at the first full ring and surface EPIPE to the state machine.
         * This is the actual overwrite guard; user-space high-water sampling is
         * only an early warning and cannot run while the task is descheduled. */
        cin.stop_threshold = (unsigned long)cin.period_size * cin.period_count;
        cout=cin; cout.format=play_fmt;
        cout.start_threshold = (unsigned long)cout.period_size * cout.period_count;

        /* 2. open capture at that rate */
        struct pcm *pin=pcm_open(cc,cd,PCM_IN|PCM_NORESTART,&cin);
        if(!pin||!pcm_is_ready(pin)){
            DLOG("capture open fail @%d: %s\n",rate,pin?pcm_get_error(pin):"null"); DFLUSH();
            if(pin) pcm_close(pin);
            backoff(&fails); continue;
        }
        /* Correction blocks are an integer number of periods and consume one
         * extra/less input frame. Fast periods reuse the same buffers. */
        uint8_t *cb = malloc((size_t)(corr_span+1) * cap_bpf());
        int32_t *win = malloc((size_t)(corr_span+1) * ch * sizeof(int32_t));
        int32_t *outw = malloc((size_t)corr_span * ch * sizeof(int32_t));
        uint8_t *pb = malloc((size_t)corr_span * play_bpf());
        if(!cb || !win || !outw || !pb){
            free(cb);free(win);free(outw);free(pb);pcm_close(pin);backoff(&fails);continue;
        }

        /* 3. block until the host actually streams one period */
        DLOG("capture open @%d; waiting for host to stream...\n",rate); DFLUSH();
        if(pcm_read(pin,cb,(unsigned)PERIOD*cap_bpf())!=0){
            DLOG("READ_FAIL(waiting for host): %s\n",pcm_get_error(pin)); DFLUSH();
            free(cb);free(win);free(outw);free(pb); pcm_close(pin); backoff(&fails); continue;
        }

        /* 4. re-check the rate: only proceed on a valid, unchanged rate. */
        {
            int rate_now = read_current_rate();
            if(rate_now <= 0 || rate_now != rate){
                DLOG("rate recheck %d->%d; reopening\n", rate, rate_now); DFLUSH();
#ifdef BRIDGE_DIAG
                if(rate_now > 0) rate_switches++;
#endif
                free(cb);free(win);free(outw);free(pb); pcm_close(pin);
                if(rate_now <= 0) backoff(&fails);
                continue;
            }
        }

        /* 5. host live, rate settled -> open playback at that rate */
        struct pcm *pout=pcm_open(pc,pd,PCM_OUT,&cout);
        if(!pout||!pcm_is_ready(pout)){
            DLOG("playback open fail @%d: %s\n",rate,pout?pcm_get_error(pout):"null"); DFLUSH();
            if(pout) pcm_close(pout);
            free(cb);free(win);free(outw);free(pb); pcm_close(pin); backoff(&fails); continue;
        }
        /* 6. drop the backlog that piled up during pcm_open(card1) */
        if(pcm_stop(pin)!=0){ DLOG("DRAIN_FAIL(stop): %s\n",pcm_get_error(pin)); DFLUSH();
            free(cb);free(win);free(outw);free(pb); pcm_close(pin); pcm_close(pout); backoff(&fails); continue; }
        if(pcm_prepare(pin)!=0){ DLOG("DRAIN_FAIL(prepare): %s\n",pcm_get_error(pin)); DFLUSH();
            free(cb);free(win);free(outw);free(pb); pcm_close(pin); pcm_close(pout); backoff(&fails); continue; }
        fails = 0;

        DLOG("uac2bridge %u.%u->%u.%u @%dHz 24bit ch%u pcount=%u buffer=%.2fms corr_span=%u(%.3fms) wl_target=%d wl_emerg=%d\n",
             cc,cd,pc,pd,rate,ch,pcount,
             1000.0 * PERIOD * pcount / rate, corr_span, 1000.0 * corr_span / rate,
             wl_target,wl_emerg);
        DFLUSH();

        /* 7. controller state -- re-initialized every handshake / rate change, so no
         * state (filter, credit, direction, timers) leaks across a rate switch. */
        double filt = wl_target;
        double cred = 0.0;
        double corr_fps = 0.0;
        int    dir  = 0;
        double wl_last = now_ms();
        double corr_last = wl_last;
        double emerg_last = wl_last;
        double wl_grace = now_ms() + WL_GRACE_MS;
#ifdef BRIDGE_DIAG
        unsigned long loops=0;
        struct hist hr, hw; hreset(&hr); hreset(&hw);
        unsigned long n_drop=0, n_ins=0;
        double wl_min=9e9, wl_max=0, last_w=0, t_report=now_ms();
        double force_last=t_report;
        int xo_prev=0;
#endif
        #define WD_SLOTS 16
        double wd_time[WD_SLOTS]; int wd_tot[WD_SLOTS];
        int wd_count=0, wd_idx=0; double wd_last=0, wd_grace=now_ms()+1000.0;

        while(1){
            /* A host-selected rate change is a state transition, never audio at
             * the old PCM rate.  Close both sides and re-handshake immediately. */
            {
                int rate_now = current_rate_load();
                if(rate_now <= 0 || rate_now != rate){
#ifdef BRIDGE_DIAG
                    if(rate_now > 0) rate_switches++;
#endif
                    break;
                }
            }

            /* Accumulate correction credit continuously, not in controller-tick
             * batches. This spaces single-frame corrections evenly in real time. */
            double tc=now_ms();
            {
                double dt = tc - corr_last;
                corr_last = tc;
                if(tc >= wl_grace && dir != 0 && corr_fps > 0.0){
                    cred += corr_fps * dt / 1000.0;
                    if(cred > CORR_CREDIT_CAP) cred = CORR_CREDIT_CAP;
                } else {
                    cred = 0.0;
                }
            }

            /* One ALSA water sample serves both controller and emergency checks
             * when their timers coincide; never issue duplicate ioctls in a loop. */
            int control_due = (tc - wl_last >= CONTROL_TICK_MS);
            int emerg_due = (tc - emerg_last >= EMERG_POLL_MS);
            unsigned int av=0; struct timespec ats;
            int have_av = 0;
            if(control_due || emerg_due)
                have_av = (pcm_get_htimestamp(pin,&av,&ats)==0);
            if(have_av && av >= (unsigned)wl_emerg){
                /* At 7/8 only ~5-6ms remains at every supported rate. Waiting
                 * hundreds of milliseconds cannot validate a spike; it merely
                 * leaves the stream exposed to the next scheduler stall. Exit
                 * immediately. cin.stop_threshold prevents silent overwrite if
                 * a stall crosses the full-ring boundary before this sample. */
                DLOG("CAPTURE_HIGH_WATER: avail=%u emergency=%d buffer=%d; re-handshaking\n",
                     av,wl_emerg,wl_buf);
                DFLUSH();
#ifdef BRIDGE_DIAG
                highwater_recoveries++;
#endif
                break;
            }

            /* ---- controller tick: normalized water level -> ppm correction ---- */
            if(control_due){
                if(have_av){
                    filt += ((double)av - filt) / WL_FILTER_DIV;
                    double corr_ppm=0.0; int cdir=0;
                    /* Hysteresis: once a correction direction engages, keep
                     * correcting until the level is back AT TARGET, not merely
                     * back inside the outer thresholds. Without this the system
                     * rested exactly on wl_high/wl_low -- the thinnest-margin
                     * points -- and routine Android scheduler stalls tipped it
                     * over the emergency mark (audible skip). */
                    if(tc < wl_grace){ corr_ppm=0.0; cdir=0; }
                    else if(filt > wl_high || (dir > 0 && filt > wl_target)){
                        cdir=+1; corr_ppm=(filt > wl_hi2 ? CORR_STRONG_PPM : CORR_MILD_PPM);
                    }
                    else if(filt < wl_low || (dir < 0 && filt < wl_target)){
                        cdir=-1; corr_ppm=(filt < wl_lo2 ? CORR_STRONG_PPM : CORR_MILD_PPM);
                    }
                    else { cdir=0; corr_ppm=0.0; }
                    if(cdir != dir){
                        cred = 0.0;  /* never spend credit accumulated for the other direction */
                        corr_last = tc;
                    }
                    dir = cdir;
                    corr_fps = correction_frames_per_sec((unsigned)rate, corr_ppm);
#ifdef BRIDGE_DIAG
                    if(av < wl_min) wl_min=av;
                    if(av > wl_max) wl_max=av;
#endif
                }
                wl_last = tc;
            }

            /* The emergency timer only schedules the shared htimestamp sample.
             * High water is handled above by a clean re-handshake; no whole
             * period is ever discarded into the audible stream. */
            if(emerg_due){
                emerg_last = tc;
            }

            /* ---- decide input/output block size from credit + direction ---- */
            unsigned out_frames = PERIOD, read_frames = PERIOD;
#ifdef BRIDGE_DIAG
            if(force_dir && tc - force_last >= 1000.0){
                out_frames=corr_span;
                read_frames=(unsigned)((int)corr_span + force_dir);
                force_last=tc;
                if(force_dir>0) n_drop++; else n_ins++;
            } else
#endif
            if(tc >= wl_grace && cred >= 1.0 && dir>0){
                out_frames=corr_span; read_frames=corr_span+1; cred-=1.0;
#ifdef BRIDGE_DIAG
                n_drop++;
#endif
            } else if(tc >= wl_grace && cred >= 1.0 && dir<0){
                out_frames=corr_span; read_frames=corr_span-1; cred-=1.0;
#ifdef BRIDGE_DIAG
                n_ins++;
#endif
            }

            double t0=now_ms();
            if(pcm_read(pin,cb,read_frames*cap_bpf())!=0){ DLOG("READ_FAIL: %s\n",pcm_get_error(pin)); DFLUSH(); break; }
            double read_ms=now_ms()-t0;
#ifdef BRIDGE_DIAG
            hadd(&hr, read_ms);
#endif
            if(read_ms > STALL_MS){ DLOG("HOST_STALL: %.0fms; re-reading rate\n", read_ms); DFLUSH(); break; }

            /* Fast path: no correction this period. */
            uint8_t *write_buf;
            if(read_frames == out_frames){
                unsigned n=out_frames*ch, i;
                for(i=0;i<n;i++){
                    int32_t v = cb[3*i] | (cb[3*i+1]<<8) | (cb[3*i+2]<<16);
                    if(v & 0x800000) v |= 0xFF000000;
                    pb[4*i]=v; pb[4*i+1]=v>>8; pb[4*i+2]=v>>16; pb[4*i+3]=v>>24;
                }
                write_buf = pb;
            } else {
                unsigned n=read_frames*ch, i;
                for(i=0;i<n;i++) win[i]=unpack1(cb + i*3);
                resample_correction(win,read_frames,outw,out_frames,ch);
                { unsigned m=out_frames*ch, j;
                  for(j=0;j<m;j++) pack_play(outw[j], pb + j*4); }
                write_buf = pb;
            }

            if(pcm_write(pout,write_buf,out_frames*play_bpf())!=0){ DLOG("WRITE_FAIL: %s\n",pcm_get_error(pout)); DFLUSH(); break; }
            double t2=now_ms();
#ifdef BRIDGE_DIAG
            if(last_w>0) hadd(&hw, t2-last_w);
            last_w=t2;
#endif
#ifdef BRIDGE_DIAG
            loops++;
#endif

            /* ---- XRUN-storm watchdog ---- */
            if(t2 - wd_last >= 150.0){
                int xo = pcm_get_xruns(pout);
                int base = xo, found=0, k;
                for(k=0;k<wd_count;k++){ int j=(wd_idx-1-k+2*WD_SLOTS)%WD_SLOTS;
                    if(t2 - wd_time[j] >= 2000.0){ base=wd_tot[j]; found=1; break; } }
                wd_time[wd_idx]=t2; wd_tot[wd_idx]=xo; wd_idx=(wd_idx+1)%WD_SLOTS;
                if(wd_count<WD_SLOTS) wd_count++;
                wd_last=t2;
                if(t2>=wd_grace && found && (xo-base)>=3){
#ifdef BRIDGE_DIAG
                    recovery_attempts++;
                    DLOG("XRUN_STORM: %d/2s rate=%d recovery_attempts=%lu; re-handshaking\n", xo-base, rate, recovery_attempts);
                    DFLUSH();
#endif
                    break;
                }
            }

#ifdef BRIDGE_DIAG
            if(t2-t_report >= 10000.0){
                int xo=pcm_get_xruns(pout);
                DLOG("[STAT %.0fs] rate=%d loops=%lu wl_filt=%.0f wl=[%.0f..%.0f] dir=%d corr=%.2ffps drop=%lu ins=%lu highwater_recoveries=%lu XRUN=%d switches=%lu read_ms.max=%.1f wgap.max=%.1f\n",
                     (t2-t_report)/1000.0, rate, loops, filt, wl_min, wl_max, dir, corr_fps,
                     n_drop, n_ins, highwater_recoveries, xo-xo_prev, rate_switches, hr.max, hw.max);
                DFLUSH();
                hreset(&hr); hreset(&hw);
                n_drop=0; n_ins=0; wl_min=9e9; wl_max=0; xo_prev=xo; t_report=t2;
            }
#endif
        }
        free(cb); free(win); free(outw); free(pb);
        pcm_close(pin); pcm_close(pout); usleep(100000);
    }
    return 0;
}
