#define _GNU_SOURCE
#include <errno.h>
#include <math.h>
#include <pthread.h>
#include <sched.h>
#include <semaphore.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "host/plugin_api_v1.h"
#include "sts_playback_engine.h"

#define STS_CHANNELS 2
#define STS_SLOTS 10
#define STS_STEPS 16
#define STS_PATH_MAX 512
#define STS_DEFAULT_BPM 120.0
#define STS_USER_SAMPLE_ROOT "/data/UserData/UserLibrary/Samples/STS Lite"
#define STS_SILENCE_FLOOR 0.0001f /* -80 dBFS: do not amplify near-silent noise */
#define STS_ONSET_RATIO 0.001f    /* -60 dB below this sample's peak */
#define STS_NORMALIZED_PEAK 0.98f /* interpolation headroom */
/* Longer files are cut to their first five seconds. Every slot is resident,
 * and the loader lives inside MoveOriginal, so this bounds the worst case at
 * 20 slots x 5 s rather than at whatever someone drops in the folder. */
#define STS_MAX_SECONDS 5u
#define STS_MIN_SOURCE_RATE 1000u
#define STS_MAX_SOURCE_RATE 384000u
#define STS_READ_CHUNK_FRAMES 4096u
/* ~1.5 ms: a retrigger, a gate cut, a sample end or a transport stop ramps
 * instead of jumping, so no 1/16 boundary is a click. */
#define STS_DECLICK_FRAMES 64

typedef struct {
    float *data;
    uint32_t frames;
    uint32_t sample_rate;
} sts_sample_t;

typedef struct {
    _Atomic(sts_sample_t *) sample;
    atomic_uint request_gen;
    atomic_uint loaded_gen;
    atomic_uint path_seq;
    atomic_int default_path;
    /* Seqlock payload. Relaxed atomic bytes rather than a plain array: the
     * reader copies while the writer may be mid-update, and the retry is what
     * makes the copy usable -- with plain chars that overlap is undefined. */
    atomic_uchar path[STS_PATH_MAX];
} sts_slot_t;

typedef struct {
    int selected_sample;
    int pending_sample;
    int sequence[STS_STEPS];
    int gates[STS_STEPS];
    int touch_lease;
    int random_source;
    int input_pitch;
    int touched;
    int current_step;
    int active;
    int volume;
    sts_playback_engine_t playback;
    /* The voice a retrigger or cut replaced, ramped out over
     * STS_DECLICK_FRAMES. It reads its slot afresh every frame, exactly as
     * the main voice does, so a sample swap can never strand it on freed
     * memory. */
    sts_playback_engine_t fade;
    int fade_slot;
    int fade_left;
} sts_channel_t;

typedef struct {
    const host_api_v1_t *host;
    int output_rate;
    char module_dir[STS_PATH_MAX];
    sts_slot_t slots[STS_CHANNELS][STS_SLOTS];
    sts_channel_t channel[STS_CHANNELS];
    int base_pitch;
    int pitch_range;
    int root_note;
    int scale;
    int pitch_mode;
    int edit_channel;
    int steps;
    uint32_t rng;
    int clock_step;
    int last_abs_step;
    double free_beat;
    /* Non-zero while render_block may hold a sample pointer. One counter per
     * BLOCK, not per frame: the loader frees a replaced sample only after
     * seeing it zero, and seq_cst on both sides is what makes that sound
     * (each side stores, then loads the other's location). */
    atomic_uint render_readers;
    pthread_t loader_thread;
    sem_t loader_sem;
    atomic_int loader_running;
    int loader_started;
} sts_lite_t;

static const host_api_v1_t *g_host;

static const char *const SCALE_NAMES[] = {
    "chromatic", "major", "minor", "dorian", "phrygian",
    "lydian", "mixolydian", "locrian", "pentatonic"
};
static const uint16_t SCALE_MASKS[] = {
    0x0fff,
    (1u<<0)|(1u<<2)|(1u<<4)|(1u<<5)|(1u<<7)|(1u<<9)|(1u<<11),
    (1u<<0)|(1u<<2)|(1u<<3)|(1u<<5)|(1u<<7)|(1u<<8)|(1u<<10),
    (1u<<0)|(1u<<2)|(1u<<3)|(1u<<5)|(1u<<7)|(1u<<9)|(1u<<10),
    (1u<<0)|(1u<<1)|(1u<<3)|(1u<<5)|(1u<<7)|(1u<<8)|(1u<<10),
    (1u<<0)|(1u<<2)|(1u<<4)|(1u<<6)|(1u<<7)|(1u<<9)|(1u<<11),
    (1u<<0)|(1u<<2)|(1u<<4)|(1u<<5)|(1u<<7)|(1u<<9)|(1u<<10),
    (1u<<0)|(1u<<1)|(1u<<3)|(1u<<5)|(1u<<6)|(1u<<8)|(1u<<10),
    (1u<<0)|(1u<<2)|(1u<<4)|(1u<<7)|(1u<<9)
};

static int clampi(int v, int lo, int hi) {
    return v < lo ? lo : (v > hi ? hi : v);
}

static int floor_mod(int v, int n) {
    int r = v % n;
    return r < 0 ? r + n : r;
}

static int pitch_min(const sts_lite_t *s) {
    return clampi(s->base_pitch - s->pitch_range / 2, -60, 60);
}

static int pitch_max(const sts_lite_t *s) {
    return clampi(s->base_pitch + s->pitch_range / 2, -60, 60);
}

static int pitch_allowed(const sts_lite_t *s, int pitch) {
    int pc = floor_mod(pitch - s->root_note, 12);
    return (SCALE_MASKS[s->scale] & (1u << pc)) != 0;
}

static int quantize_pitch(const sts_lite_t *s, int pitch) {
    int lo = pitch_min(s);
    int hi = pitch_max(s);
    pitch = clampi(pitch, lo, hi);
    for (int distance = 0; distance <= 12; ++distance) {
        int down = pitch - distance;
        int up = pitch + distance;
        if (down >= lo && pitch_allowed(s, down)) return down;
        if (up <= hi && pitch_allowed(s, up)) return up;
    }
    return pitch;
}

static int move_scale_steps(const sts_lite_t *s, int pitch, int delta) {
    pitch = quantize_pitch(s, pitch);
    delta = clampi(delta, -64, 64);
    int direction = delta < 0 ? -1 : 1;
    int count = delta < 0 ? -delta : delta;
    int lo = pitch_min(s);
    int hi = pitch_max(s);
    while (count-- > 0) {
        int candidate = pitch;
        do {
            candidate += direction;
        } while (candidate >= lo && candidate <= hi && !pitch_allowed(s, candidate));
        if (candidate < lo || candidate > hi) break;
        pitch = candidate;
    }
    return pitch;
}

static uint16_t le16(const uint8_t *p) {
    return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}

static uint32_t le32(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static float pcm_value(const uint8_t *p, int format, int bits) {
    if (format == 3 && bits == 32) {
        float v;
        memcpy(&v, p, sizeof(v));
        return isfinite(v) ? v : 0.0f;
    }
    if (format != 1) return 0.0f;
    if (bits == 16) return (float)(int16_t)le16(p) / 32768.0f;
    if (bits == 24) {
        int32_t v = (int32_t)p[0] | ((int32_t)p[1] << 8) | ((int32_t)p[2] << 16);
        if (v & 0x800000) v |= ~0xffffff;
        return (float)v / 8388608.0f;
    }
    if (bits == 32) return (float)(int32_t)le32(p) / 2147483648.0f;
    return 0.0f;
}

/* Preprocess once on the loader thread, never on the audio callback. Work on
 * the mono signal that will actually play, so stereo cancellation is honored.
 * The absolute floor rejects digital noise; the relative threshold still
 * finds the onset of a quiet but usable recording. */
static void prepare_sample(float *data, uint32_t *frames) {
    if (!data || !frames || !*frames) return;
    float peak = 0.0f;
    for (uint32_t i = 0; i < *frames; ++i) {
        float magnitude = fabsf(data[i]);
        if (magnitude > peak) peak = magnitude;
    }
    if (peak < STS_SILENCE_FLOOR) return;

    float threshold = fmaxf(STS_SILENCE_FLOOR, peak * STS_ONSET_RATIO);
    uint32_t onset = 0;
    while (onset < *frames && fabsf(data[onset]) < threshold) ++onset;
    if (onset) {
        *frames -= onset;
        memmove(data, data + onset, (size_t)*frames * sizeof(*data));
    }
    float gain = STS_NORMALIZED_PEAK / peak;
    for (uint32_t i = 0; i < *frames; ++i) data[i] *= gain;
}

static int loader_cancelled(const atomic_int *running) {
    return running && !atomic_load_explicit(running, memory_order_acquire);
}

/* `running` may be NULL (tests). When it drops to 0 the load is abandoned
 * between chunks, so destroy_instance never waits on a whole file. */
static sts_sample_t *load_wav(const char *path, const atomic_int *running) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    uint8_t head[12];
    if (fread(head, 1, sizeof(head), f) != sizeof(head) ||
        memcmp(head, "RIFF", 4) || memcmp(head + 8, "WAVE", 4)) {
        fclose(f);
        return NULL;
    }
    int format = 0, channels = 0, bits = 0;
    uint32_t sample_rate = 0, data_size = 0;
    long data_offset = 0;
    for (;;) {
        uint8_t chunk[8];
        if (fread(chunk, 1, sizeof(chunk), f) != sizeof(chunk)) break;
        uint32_t size = le32(chunk + 4);
        if (!memcmp(chunk, "fmt ", 4)) {
            uint8_t fmt[40] = {0};
            size_t take = size < sizeof(fmt) ? size : sizeof(fmt);
            if (fread(fmt, 1, take, f) != take) break;
            if (size > take && fseek(f, (long)(size - take), SEEK_CUR)) break;
            if (take >= 16) {
                format = le16(fmt);
                channels = le16(fmt + 2);
                sample_rate = le32(fmt + 4);
                bits = le16(fmt + 14);
                if (format == 0xfffe && take >= 40) format = le16(fmt + 24);
            }
        } else if (!memcmp(chunk, "data", 4)) {
            data_offset = ftell(f);
            data_size = size;
            /* 0 / 0xffffffff is a recorder that never patched its header:
             * the audio runs to end of file and nothing usable follows. */
            if (size == 0 || size == 0xffffffffu) break;
            if (fseek(f, (long)size, SEEK_CUR)) break;
        } else if (fseek(f, (long)size, SEEK_CUR)) {
            break;
        }
        if (size & 1u) fseek(f, 1, SEEK_CUR);
    }
    int bytes = bits / 8;
    if (data_offset <= 0 || sample_rate < STS_MIN_SOURCE_RATE ||
        sample_rate > STS_MAX_SOURCE_RATE || channels < 1 || channels > 2 ||
        bytes < 2 || bytes > 4 || !((format == 1 && (bits == 16 || bits == 24 || bits == 32)) ||
                                    (format == 3 && bits == 32))) {
        fclose(f);
        return NULL;
    }
    uint32_t frame_bytes = (uint32_t)channels * (uint32_t)bytes;
    uint32_t max_frames = sample_rate * STS_MAX_SECONDS;
    uint32_t want = (data_size == 0 || data_size == 0xffffffffu)
        ? max_frames : data_size / frame_bytes;
    if (want > max_frames) want = max_frames;
    sts_sample_t *sample = want ? calloc(1, sizeof(*sample)) : NULL;
    float *mono = want ? malloc((size_t)want * sizeof(float)) : NULL;
    if (!sample || !mono || fseek(f, data_offset, SEEK_SET)) {
        free(sample); free(mono); fclose(f);
        return NULL;
    }
    /* Decode in chunks: no whole-file raw buffer, and a cancellation point
     * between chunks. A data chunk that claims more than the file holds keeps
     * what was actually there instead of rejecting the whole sample. */
    uint8_t raw[STS_READ_CHUNK_FRAMES * 8u];
    uint32_t got = 0;
    while (got < want) {
        if (loader_cancelled(running)) {
            free(sample); free(mono); fclose(f);
            return NULL;
        }
        uint32_t n = want - got;
        if (n > STS_READ_CHUNK_FRAMES) n = STS_READ_CHUNK_FRAMES;
        size_t r = fread(raw, frame_bytes, n, f);
        for (size_t i = 0; i < r; ++i) {
            const uint8_t *p = raw + i * frame_bytes;
            float v = pcm_value(p, format, bits);
            if (channels == 2) v = 0.5f * (v + pcm_value(p + bytes, format, bits));
            mono[got + i] = v;
        }
        got += (uint32_t)r;
        if (r < n) break;
    }
    fclose(f);
    if (!got) {
        free(sample); free(mono);
        return NULL;
    }
    prepare_sample(mono, &got);
    sample->data = mono;
    sample->frames = got;
    sample->sample_rate = sample_rate;
    return sample;
}

static void free_sample(sts_sample_t *sample) {
    if (!sample) return;
    free(sample->data);
    free(sample);
}

static void snapshot_path(sts_slot_t *slot, char *out) {
    unsigned before, after;
    do {
        before = atomic_load_explicit(&slot->path_seq, memory_order_acquire);
        if (before & 1u) continue;
        for (int i = 0; i < STS_PATH_MAX; ++i)
            out[i] = (char)atomic_load_explicit(&slot->path[i], memory_order_relaxed);
        /* Seqlock read side: the copy must complete before the re-check. An
         * acquire LOAD only orders what comes after it. */
        atomic_thread_fence(memory_order_acquire);
        after = atomic_load_explicit(&slot->path_seq, memory_order_relaxed);
    } while (before != after || (after & 1u));
    out[STS_PATH_MAX - 1] = '\0';
}

static void *loader_main(void *arg) {
    sts_lite_t *s = arg;
    struct sched_param sp = {.sched_priority = 0};
    sched_setscheduler(0, SCHED_OTHER, &sp);
    cpu_set_t cpus;
    CPU_ZERO(&cpus);
    CPU_SET(0, &cpus); CPU_SET(1, &cpus); CPU_SET(2, &cpus);
    sched_setaffinity(0, sizeof(cpus), &cpus);
    while (atomic_load_explicit(&s->loader_running, memory_order_acquire)) {
        while (sem_wait(&s->loader_sem) && errno == EINTR) {}
        /* Checked per slot, not per wake: create_instance queues all twenty,
         * and destroy_instance joins this thread from the SPI callback. */
        for (int ch = 0; ch < STS_CHANNELS; ++ch) {
            for (int n = 0; n < STS_SLOTS; ++n) {
                if (loader_cancelled(&s->loader_running)) return NULL;
                sts_slot_t *slot = &s->slots[ch][n];
                unsigned request = atomic_load_explicit(&slot->request_gen, memory_order_acquire);
                if (request == atomic_load_explicit(&slot->loaded_gen, memory_order_relaxed)) continue;
                char path[STS_PATH_MAX];
                snapshot_path(slot, path);
                sts_sample_t *fresh = path[0] ? load_wav(path, &s->loader_running) : NULL;
                if (loader_cancelled(&s->loader_running)) {
                    free_sample(fresh);
                    return NULL;
                }
                if (!fresh && atomic_load_explicit(&slot->default_path, memory_order_acquire)) {
                    char bundled[STS_PATH_MAX + 64];
                    snprintf(bundled, sizeof(bundled), "%s/samples/%s/%02d.wav",
                             s->module_dir, ch == 0 ? "left" : "right", n + 1);
                    fresh = load_wav(bundled, &s->loader_running);
                }
                if (path[0] && !fresh) {
                    atomic_store_explicit(&slot->loaded_gen, request, memory_order_release);
                    continue;
                }
                sts_sample_t *old = atomic_exchange_explicit(&slot->sample, fresh, memory_order_seq_cst);
                atomic_store_explicit(&slot->loaded_gen, request, memory_order_release);
                /* A block is ~2.9 ms and render holds the counter for
                 * microseconds of it, so this is rarely more than one pass. */
                while (atomic_load_explicit(&s->render_readers, memory_order_seq_cst) != 0)
                    usleep(500);
                free_sample(old);
            }
        }
    }
    return NULL;
}

static void request_load(sts_lite_t *s, int ch, int slot_number, const char *path,
                         int default_path) {
    if (ch < 0 || ch >= STS_CHANNELS || slot_number < 0 || slot_number >= STS_SLOTS) return;
    sts_slot_t *slot = &s->slots[ch][slot_number];
    atomic_fetch_add_explicit(&slot->path_seq, 1, memory_order_relaxed);
    atomic_thread_fence(memory_order_release);
    const char *source = path ? path : "";
    size_t path_len = strlen(source);
    if (path_len >= STS_PATH_MAX) path_len = STS_PATH_MAX - 1;
    for (size_t i = 0; i < path_len; ++i)
        atomic_store_explicit(&slot->path[i], (unsigned char)source[i], memory_order_relaxed);
    atomic_store_explicit(&slot->path[path_len], 0, memory_order_relaxed);
    atomic_store_explicit(&slot->default_path, default_path ? 1 : 0, memory_order_release);
    atomic_fetch_add_explicit(&slot->path_seq, 1, memory_order_release);
    atomic_fetch_add_explicit(&slot->request_gen, 1, memory_order_release);
    if (s->loader_started) sem_post(&s->loader_sem);
}

static int parse_int_value(const char *text) {
    if (!text) return 0;
    while (*text == ' ' || *text == '\t') ++text;
    int sign = 1;
    if (*text == '-') {
        sign = -1;
        ++text;
    } else if (*text == '+') {
        ++text;
    }
    int value = 0;
    while (*text >= '0' && *text <= '9') {
        if (value < 100000000) value = value * 10 + (*text - '0');
        ++text;
    }
    return value * sign;
}

static int scale_from_name(const char *name) {
    if (!name) return 4;
    for (int i = 0; i < (int)(sizeof(SCALE_NAMES) / sizeof(SCALE_NAMES[0])); ++i) {
        if (!strcmp(name, SCALE_NAMES[i])) return i;
    }
    return clampi(parse_int_value(name), 0, (int)(sizeof(SCALE_NAMES) / sizeof(SCALE_NAMES[0])) - 1);
}

static void requantize(sts_lite_t *s) {
    for (int ch = 0; ch < STS_CHANNELS; ++ch) {
        sts_channel_t *c = &s->channel[ch];
        c->input_pitch = quantize_pitch(s, c->input_pitch);
    }
}

static sts_sample_t *slot_sample(sts_lite_t *s, int ch, int slot_number) {
    return atomic_load_explicit(&s->slots[ch][slot_number].sample, memory_order_seq_cst);
}

/* Hand the sounding voice to the fade slot, so the new state can start from
 * frame zero (or silence) without a step in the waveform. */
static void begin_fade(sts_channel_t *c) {
    if (!c->playback.active) return;
    c->fade = c->playback;
    c->fade_slot = c->selected_sample;
    c->fade_left = STS_DECLICK_FRAMES;
}

/* Caller holds render_readers. */
static void trigger_step(sts_lite_t *s, int step) {
    s->clock_step = step;
    for (int ch = 0; ch < STS_CHANNELS; ++ch) {
        sts_channel_t *c = &s->channel[ch];
        begin_fade(c);
        c->selected_sample = c->pending_sample;
        c->current_step = step;
        sts_playback_engine_reset(&c->playback);
        c->active = 0;
        if (s->pitch_mode && c->touched && c->touch_lease > 0)
            c->sequence[step] = c->input_pitch;
        if (!c->gates[step]) continue;
        int pitch = c->sequence[step];
        sts_sample_t *sample = slot_sample(s, ch, c->selected_sample);
        if (sample) {
            sts_playback_engine_trigger(&c->playback, pitch, sample->sample_rate,
                                        s->output_rate, 1);
            c->active = c->playback.active;
        }
    }
}

/* One frame of one voice, ramped down over its last STS_DECLICK_FRAMES output
 * frames so a sample that ends off zero does not click. */
static float voice_next(sts_playback_engine_t *v, const sts_sample_t *sample) {
    if (!v->active) return 0.0f;
    if (!sample || v->position >= (double)sample->frames) {
        v->active = 0;
        return 0.0f;
    }
    double remaining = ((double)sample->frames - v->position) / v->rate;
    float value = sts_playback_engine_next(v, sample->data, sample->frames);
    if (remaining < (double)STS_DECLICK_FRAMES)
        value *= (float)(remaining / (double)STS_DECLICK_FRAMES);
    return value;
}

/* Caller holds render_readers. */
static int16_t render_channel(sts_lite_t *s, int ch) {
    sts_channel_t *c = &s->channel[ch];
    float value = 0.0f;
    if (c->active) {
        value = voice_next(&c->playback, slot_sample(s, ch, c->selected_sample));
        c->active = c->playback.active;
    }
    if (c->fade_left > 0) {
        float gain = (float)c->fade_left / (float)STS_DECLICK_FRAMES;
        value += gain * voice_next(&c->fade, slot_sample(s, ch, c->fade_slot));
        if (--c->fade_left == 0 || !c->fade.active) {
            c->fade_left = 0;
            c->fade.active = 0;
        }
    }
    if (!c->active && !c->fade_left && value == 0.0f) return 0;
    if (value > 1.0f) value = 1.0f;
    if (value < -1.0f) value = -1.0f;
    value *= (float)c->volume / 100.0f;
    return (int16_t)lrintf(value * 32767.0f);
}

static void init_defaults(sts_lite_t *s) {
    s->base_pitch = -24;
    s->pitch_range = 60;
    s->root_note = 0;
    s->scale = 4;
    s->pitch_mode = 0;
    s->steps = 8;
    s->rng = 0x5a17c0deu;
    s->clock_step = -1;
    for (int ch = 0; ch < STS_CHANNELS; ++ch) {
        sts_channel_t *c = &s->channel[ch];
        c->selected_sample = 0;
        c->pending_sample = 0;
        c->input_pitch = -24;
        c->current_step = 0;
        c->volume = 100;
        c->random_source = -1;
        for (int i = 0; i < STS_STEPS; ++i) {
            c->sequence[i] = -24;
            c->gates[i] = 1;
        }
    }
}

static void *create_instance(const char *module_dir, const char *json_defaults) {
    (void)json_defaults;
    sts_lite_t *s = calloc(1, sizeof(*s));
    if (!s) return NULL;
    s->host = g_host;
    s->output_rate = g_host && g_host->sample_rate > 0 ? g_host->sample_rate : MOVE_SAMPLE_RATE;
    snprintf(s->module_dir, sizeof(s->module_dir), "%s", module_dir ? module_dir : ".");
    init_defaults(s);
    if (sem_init(&s->loader_sem, 0, 0) == 0) {
        atomic_store_explicit(&s->loader_running, 1, memory_order_release);
        if (pthread_create(&s->loader_thread, NULL, loader_main, s) == 0) {
            s->loader_started = 1;
        } else {
            atomic_store_explicit(&s->loader_running, 0, memory_order_release);
            sem_destroy(&s->loader_sem);
        }
    }
    if (s->loader_started) {
        char path[STS_PATH_MAX + 64];
        for (int ch = 0; ch < STS_CHANNELS; ++ch) {
            for (int n = 0; n < STS_SLOTS; ++n) {
                snprintf(path, sizeof(path), "%s/%s/%02d.wav",
                         STS_USER_SAMPLE_ROOT, ch == 0 ? "left" : "right", n + 1);
                request_load(s, ch, n, path, 1);
            }
        }
    }
    return s;
}

static void destroy_instance(void *instance) {
    sts_lite_t *s = instance;
    if (!s) return;
    if (s->loader_started) {
        atomic_store_explicit(&s->loader_running, 0, memory_order_release);
        sem_post(&s->loader_sem);
        pthread_join(s->loader_thread, NULL);
        sem_destroy(&s->loader_sem);
    }
    for (int ch = 0; ch < STS_CHANNELS; ++ch) {
        for (int n = 0; n < STS_SLOTS; ++n) {
            free_sample(atomic_load_explicit(&s->slots[ch][n].sample, memory_order_relaxed));
        }
    }
    free(s);
}

static void on_midi(void *instance, const uint8_t *msg, int len, int source) {
    (void)instance; (void)msg; (void)len; (void)source;
}

static int indexed_key(const char *key, const char *prefix, const char *suffix, int *index) {
    size_t prefix_len = strlen(prefix);
    if (strncmp(key, prefix, prefix_len)) return 0;
    const char *p = key + prefix_len;
    if (*p < '0' || *p > '9') return 0;
    int value = 0;
    do {
        value = value * 10 + (*p - '0');
        ++p;
    } while (*p >= '0' && *p <= '9');
    if (strcmp(p, suffix)) return 0;
    *index = value;
    return 1;
}

static void set_path_param(sts_lite_t *s, const char *key, const char *value) {
    int slot = -1;
    int ch = -1;
    if (indexed_key(key, "left_sample_", "_path", &slot)) ch = 0;
    else if (indexed_key(key, "right_sample_", "_path", &slot)) ch = 1;
    if (ch >= 0 && slot >= 1 && slot <= STS_SLOTS)
        request_load(s, ch, slot - 1, value, 0);
}

static int json_number(const char *json, const char *key, int *out) {
    char needle[40];
    snprintf(needle, sizeof(needle), "\"%s\":", key);
    const char *p = json ? strstr(json, needle) : NULL;
    if (!p) return 0;
    p += strlen(needle);
    while (*p == ' ' || *p == '\t') ++p;
    int sign = 1;
    if (*p == '-') {
        sign = -1;
        ++p;
    }
    if (*p < '0' || *p > '9') return 0;
    int value = 0;
    do {
        if (value < 100000000) value = value * 10 + (*p - '0');
        ++p;
    } while (*p >= '0' && *p <= '9');
    *out = value * sign;
    return 1;
}

#include "performance.h"

static void set_param(void *instance, const char *key, const char *value) {
    sts_lite_t *s = instance;
    if (!s || !key) return;
    int v = parse_int_value(value);
    if (performance_set(s, key, value, v)) return;
    if (!strcmp(key, "state")) {
        int restored;
        s->pitch_mode = 0;
        s->channel[0].touched = s->channel[1].touched = 0;
        if (json_number(value, "steps", &restored)) s->steps = restored == 16 ? 16 : 8;
        if (json_number(value, "edit", &restored)) s->edit_channel = clampi(restored, 0, 1);
        if (json_number(value, "base", &restored)) s->base_pitch = clampi(restored, -48, 0);
        /* Old presets may carry a narrower range; this performance surface
         * always exposes the full five-octave width. */
        s->pitch_range = 60;
        if (json_number(value, "root", &restored)) s->root_note = floor_mod(restored, 12);
        if (json_number(value, "scale", &restored)) s->scale = clampi(restored, 0, 8);
        if (json_number(value, "ls", &restored)) s->channel[0].pending_sample = clampi(restored, 0, 9);
        if (json_number(value, "rs", &restored)) s->channel[1].pending_sample = clampi(restored, 0, 9);
        if (json_number(value, "lv", &restored)) s->channel[0].volume = clampi(restored, 0, 100);
        if (json_number(value, "rv", &restored)) s->channel[1].volume = clampi(restored, 0, 100);
        for (int i = 0; i < STS_STEPS; ++i) {
            char state_key[16];
            snprintf(state_key, sizeof(state_key), "l%d", i);
            if (json_number(value, state_key, &restored))
                s->channel[0].sequence[i] = clampi(restored, -60, 60);
            snprintf(state_key, sizeof(state_key), "r%d", i);
            if (json_number(value, state_key, &restored))
                s->channel[1].sequence[i] = clampi(restored, -60, 60);
            snprintf(state_key, sizeof(state_key), "lg%d", i);
            if (json_number(value, state_key, &restored)) s->channel[0].gates[i] = !!restored;
            snprintf(state_key, sizeof(state_key), "rg%d", i);
            if (json_number(value, state_key, &restored)) s->channel[1].gates[i] = !!restored;
        }
        requantize(s);
        s->channel[0].input_pitch = s->channel[0].sequence[s->channel[0].current_step];
        s->channel[1].input_pitch = s->channel[1].sequence[s->channel[1].current_step];
    } else if (!strcmp(key, "base_pitch")) {
        s->base_pitch = clampi(v, -48, 0);
        requantize(s);
    } else if (!strcmp(key, "pitch_range")) {
        s->pitch_range = 60;
        requantize(s);
    } else if (!strcmp(key, "root_note")) {
        s->root_note = floor_mod(v, 12);
        requantize(s);
    } else if (!strcmp(key, "scale")) {
        s->scale = scale_from_name(value);
        requantize(s);
    } else if (!strcmp(key, "pitch_mode")) {
        s->pitch_mode = v ? 1 : 0;
        if (!s->pitch_mode) {
            s->channel[0].touched = 0;
            s->channel[1].touched = 0;
        }
    } else if (!strcmp(key, "left_touch") || !strcmp(key, "left_pitch_touch")) {
        s->channel[0].touched = v ? 1 : 0;
        s->channel[0].touch_lease = v ? s->output_rate / 2 : 0;
    } else if (!strcmp(key, "right_touch") || !strcmp(key, "right_pitch_touch")) {
        s->channel[1].touched = v ? 1 : 0;
        s->channel[1].touch_lease = v ? s->output_rate / 2 : 0;
    } else if (!strcmp(key, "left_sample")) {
        s->channel[0].pending_sample = clampi(v, 0, STS_SLOTS - 1);
    } else if (!strcmp(key, "right_sample")) {
        s->channel[1].pending_sample = clampi(v, 0, STS_SLOTS - 1);
    } else if (!strcmp(key, "left_volume")) {
        s->channel[0].volume = clampi(v, 0, 100);
    } else if (!strcmp(key, "right_volume")) {
        s->channel[1].volume = clampi(v, 0, 100);
    } else if (!strcmp(key, "left_pitch_delta")) {
        s->channel[0].input_pitch = move_scale_steps(s, s->channel[0].input_pitch, v);
        if (s->pitch_mode && s->channel[0].touched && s->clock_step >= 0)
            s->channel[0].sequence[s->channel[0].current_step] = s->channel[0].input_pitch;
    } else if (!strcmp(key, "right_pitch_delta")) {
        s->channel[1].input_pitch = move_scale_steps(s, s->channel[1].input_pitch, v);
        if (s->pitch_mode && s->channel[1].touched && s->clock_step >= 0)
            s->channel[1].sequence[s->channel[1].current_step] = s->channel[1].input_pitch;
    } else if (!strcmp(key, "left_pitch")) {
        s->channel[0].input_pitch = quantize_pitch(s, v);
        if (s->pitch_mode && s->channel[0].touched && s->clock_step >= 0)
            s->channel[0].sequence[s->channel[0].current_step] = s->channel[0].input_pitch;
    } else if (!strcmp(key, "right_pitch")) {
        s->channel[1].input_pitch = quantize_pitch(s, v);
        if (s->pitch_mode && s->channel[1].touched && s->clock_step >= 0)
            s->channel[1].sequence[s->channel[1].current_step] = s->channel[1].input_pitch;
    } else {
        int step = -1;
        if (indexed_key(key, "left_step_", "", &step) && step >= 1 && step <= STS_STEPS)
            s->channel[0].sequence[step - 1] = quantize_pitch(s, v);
        else if (indexed_key(key, "right_step_", "", &step) && step >= 1 && step <= STS_STEPS)
            s->channel[1].sequence[step - 1] = quantize_pitch(s, v);
        else set_path_param(s, key, value ? value : "");
    }
}

static int get_param(void *instance, const char *key, char *buf, int len) {
    sts_lite_t *s = instance;
    if (!s || !key || !buf || len <= 0) return -1;
    int performance_result = performance_get(s, key, buf, len);
    if (performance_result != -2) return performance_result;
    if (!strcmp(key, "preset_list")) {
        buf[0] = '\0';
        return 0;
    }
    /* chain_params, ui_hierarchy, state and scale are answered by
     * performance_get above. */
    if (!strcmp(key, "base_pitch")) return snprintf(buf, len, "%d", s->base_pitch);
    if (!strcmp(key, "pitch_range")) return snprintf(buf, len, "%d", s->pitch_range);
    if (!strcmp(key, "root_note")) return snprintf(buf, len, "%d", s->root_note);
    if (!strcmp(key, "pitch_mode")) return snprintf(buf, len, "%d", s->pitch_mode);
    if (!strcmp(key, "left_sample")) return snprintf(buf, len, "%d", s->channel[0].pending_sample);
    if (!strcmp(key, "right_sample")) return snprintf(buf, len, "%d", s->channel[1].pending_sample);
    if (!strcmp(key, "left_volume")) return snprintf(buf, len, "%d", s->channel[0].volume);
    if (!strcmp(key, "right_volume")) return snprintf(buf, len, "%d", s->channel[1].volume);
    if (!strcmp(key, "left_pitch")) return snprintf(buf, len, "%d", s->channel[0].input_pitch);
    if (!strcmp(key, "right_pitch")) return snprintf(buf, len, "%d", s->channel[1].input_pitch);
    int step = -1;
    if (indexed_key(key, "left_step_", "", &step) && step >= 1 && step <= STS_STEPS)
        return snprintf(buf, len, "%d", s->channel[0].sequence[step - 1]);
    if (indexed_key(key, "right_step_", "", &step) && step >= 1 && step <= STS_STEPS)
        return snprintf(buf, len, "%d", s->channel[1].sequence[step - 1]);
    return -1;
}

static int get_error(void *instance, char *buf, int len) {
    (void)instance;
    if (buf && len > 0) buf[0] = '\0';
    return 0;
}

static void expire_touch(sts_lite_t *s, int frames) {
    for (int ch = 0; ch < STS_CHANNELS; ++ch) {
        sts_channel_t *c = &s->channel[ch];
        c->touch_lease = c->touch_lease > frames ? c->touch_lease - frames : 0;
        if (!c->touch_lease) c->touched = 0;
    }
}

/* Caller holds render_readers. */
static void render_frames(sts_lite_t *s, int16_t *out, int frames) {
    double bpm = STS_DEFAULT_BPM;
    if (s->host && s->host->get_bpm) {
        double candidate = s->host->get_bpm();
        if (candidate >= 20.0 && candidate <= 400.0) bpm = candidate;
    }
    double beat = -1.0;
    if (s->host && s->host->get_beat_position) beat = s->host->get_beat_position();
    int host_clock = isfinite(beat) && beat >= 0.0 && beat < 1e8;
    if (s->host && s->host->get_beat_position && !host_clock) {
        /* Transport stopped: cut both voices through the declick ramp, and
         * let a stale touch lease run out rather than wait for the next
         * Play to spend it on recording. */
        for (int ch = 0; ch < STS_CHANNELS; ++ch) {
            sts_channel_t *c = &s->channel[ch];
            begin_fade(c);
            c->active = 0;
            c->playback.active = 0;
        }
        s->clock_step = -1;
        expire_touch(s, frames);
        if (!s->channel[0].fade_left && !s->channel[1].fade_left) {
            memset(out, 0, (size_t)frames * 2u * sizeof(*out));
            return;
        }
        for (int i = 0; i < frames; ++i) {
            out[i * 2] = render_channel(s, 0);
            out[i * 2 + 1] = render_channel(s, 1);
        }
        return;
    }
    if (!host_clock) beat = s->free_beat;
    double beat_per_frame = bpm / (60.0 * (double)s->output_rate);
    for (int i = 0; i < frames; ++i) {
        int absolute_step = (int)floor(beat * 4.0);
        /* Within a block the beat is extrapolated from get_bpm; the host's
         * next reading can land a hair behind it. A one-step regression is
         * that jitter, not a relocation, and must not retrigger the step that
         * already played. A larger jump back (restart, loop) is real. */
        if (host_clock && s->clock_step >= 0 && absolute_step == s->last_abs_step - 1)
            absolute_step = s->last_abs_step;
        if (s->clock_step < 0 || absolute_step != s->last_abs_step) {
            s->last_abs_step = absolute_step;
            trigger_step(s, floor_mod(absolute_step, s->steps));
        }
        for (int ch = 0; ch < STS_CHANNELS; ++ch) {
            sts_channel_t *c = &s->channel[ch];
            if (c->touch_lease > 0) --c->touch_lease;
            else c->touched = 0;
            if (s->pitch_mode && c->touched) c->sequence[c->current_step] = c->input_pitch;
        }
        out[i * 2] = render_channel(s, 0);
        out[i * 2 + 1] = render_channel(s, 1);
        beat += beat_per_frame;
    }
    if (!host_clock) s->free_beat = beat;
}

static void render_block(void *instance, int16_t *out, int frames) {
    sts_lite_t *s = instance;
    if (!out || frames <= 0) return;
    if (!s) {
        memset(out, 0, (size_t)frames * 2u * sizeof(*out));
        return;
    }
    atomic_fetch_add_explicit(&s->render_readers, 1, memory_order_seq_cst);
    render_frames(s, out, frames);
    atomic_fetch_sub_explicit(&s->render_readers, 1, memory_order_release);
}

static plugin_api_v2_t g_api = {
    .api_version = MOVE_PLUGIN_API_VERSION_2,
    .create_instance = create_instance,
    .destroy_instance = destroy_instance,
    .on_midi = on_midi,
    .set_param = set_param,
    .get_param = get_param,
    .get_error = get_error,
    .render_block = render_block,
};

plugin_api_v2_t *move_plugin_init_v2(const host_api_v1_t *host) {
    g_host = host;
    return &g_api;
}
