#define _GNU_SOURCE
#include <assert.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "../dsp/sts_lite.c"

static void attach(sts_lite_t *s, int ch, int slot, sts_sample_t *sample) {
    atomic_store_explicit(&s->slots[ch][slot].sample, sample, memory_order_release);
}

static double stopped_beat_position(void) {
    return -1.0;
}

static float test_bpm(void) {
    return 120.0f;
}

static double g_beat;
static double settable_beat(void) {
    return g_beat;
}

/* A PCM16 WAV whose data chunk CLAIMS `claimed` bytes but carries `count`
 * samples of `value`. */
static void write_wav(const char *path, uint32_t rate, uint32_t claimed,
                      uint32_t count, int16_t value) {
    FILE *f = fopen(path, "wb");
    assert(f);
    uint8_t h[44] = {
        'R','I','F','F', 0,0,0,0, 'W','A','V','E',
        'f','m','t',' ', 16,0,0,0, 1,0, 1,0,
        0,0,0,0, 0,0,0,0, 2,0, 16,0,
        'd','a','t','a', 0,0,0,0
    };
    for (int i = 0; i < 4; ++i) {
        h[24 + i] = (uint8_t)(rate >> (8 * i));
        h[28 + i] = (uint8_t)((rate * 2u) >> (8 * i));
        h[40 + i] = (uint8_t)(claimed >> (8 * i));
    }
    assert(fwrite(h, 1, sizeof(h), f) == sizeof(h));
    for (uint32_t i = 0; i < count; ++i) assert(fwrite(&value, 2, 1, f) == 1);
    fclose(f);
}

int main(void) {
    sts_lite_t s;
    memset(&s, 0, sizeof(s));
    s.output_rate = 44100;
    init_defaults(&s);

    float left_data[] = {0.25f, 0.5f, 0.75f, 1.0f};
    float right_data[] = {-0.5f, -0.25f, 0.0f, 0.25f};
    float alternate_data[] = {0.9f, 0.8f, 0.7f, 0.6f};
    sts_sample_t left = {left_data, 4, 44100};
    sts_sample_t right = {right_data, 4, 44100};
    sts_sample_t alternate = {alternate_data, 4, 44100};
    attach(&s, 0, 0, &left);
    attach(&s, 1, 0, &right);
    attach(&s, 0, 1, &alternate);

    /* Loader: ordinary PCM16 WAV is decoded and normalized off the RT path. */
    char wav_path[] = "/tmp/sts-lite-test-XXXXXX";
    int wav_fd = mkstemp(wav_path);
    assert(wav_fd >= 0);
    const uint8_t wav[] = {
        'R','I','F','F', 40,0,0,0, 'W','A','V','E',
        'f','m','t',' ', 16,0,0,0, 1,0, 1,0,
        0x44,0xac,0,0, 0x88,0x58,0x01,0, 2,0, 16,0,
        'd','a','t','a', 4,0,0,0, 0,0, 0xff,0x7f
    };
    assert(write(wav_fd, wav, sizeof(wav)) == (ssize_t)sizeof(wav));
    close(wav_fd);
    sts_sample_t *decoded = load_wav(wav_path, NULL);
    unlink(wav_path);
    assert(decoded && decoded->frames == 1 && decoded->sample_rate == 44100);
    assert(fabsf(decoded->data[0] - 0.98f) < 0.0001f);
    free_sample(decoded);

    /* Loader limits: five seconds, truncated files, unpatched headers, a
     * cancelled load and an implausible rate. */
    char limit_path[] = "/tmp/sts-lite-limit-XXXXXX";
    int limit_fd = mkstemp(limit_path);
    assert(limit_fd >= 0);
    close(limit_fd);
    write_wav(limit_path, 1000, 6000u * 2u, 6000, 1000);
    decoded = load_wav(limit_path, NULL);
    assert(decoded && decoded->frames == 5000);   /* cut to the first 5 s */
    free_sample(decoded);
    write_wav(limit_path, 1000, 100u * 2u, 7, 1000);
    decoded = load_wav(limit_path, NULL);
    assert(decoded && decoded->frames == 7);      /* keeps what is there */
    free_sample(decoded);
    write_wav(limit_path, 1000, 0xffffffffu, 9, 1000);
    decoded = load_wav(limit_path, NULL);
    assert(decoded && decoded->frames == 9);      /* runs to end of file */
    free_sample(decoded);
    atomic_int stopped_loader = 0;
    assert(load_wav(limit_path, &stopped_loader) == NULL);
    write_wav(limit_path, 500, 10u * 2u, 10, 1000);
    assert(load_wav(limit_path, NULL) == NULL);   /* below STS_MIN_SOURCE_RATE */
    unlink(limit_path);

    float quiet[] = {0.0f, 0.00001f, 0.25f, -0.5f};
    uint32_t quiet_frames = 4;
    prepare_sample(quiet, &quiet_frames);
    assert(quiet_frames == 2);
    assert(fabsf(quiet[0] - 0.49f) < 0.0001f);
    assert(fabsf(quiet[1] + 0.98f) < 0.0001f);

    float silent[] = {0.0f, 0.0f, 0.0f};
    uint32_t silent_frames = 3;
    prepare_sample(silent, &silent_frames);
    assert(silent_frames == 3 && silent[0] == 0.0f && silent[2] == 0.0f);

    float noise_only[] = {0.0f, 0.00005f};
    uint32_t noise_frames = 2;
    prepare_sample(noise_only, &noise_frames);
    assert(noise_frames == 2 && noise_only[1] == 0.00005f);

    /* Phase 1: every step is a hard frame-zero retrigger. */
    s.channel[0].playback.position = 3.0;
    trigger_step(&s, 0);
    assert(s.channel[0].playback.position == 0.0);
    assert(s.channel[0].active == 1);
    s.channel[0].playback.position = 2.0;
    trigger_step(&s, 1);
    assert(s.channel[0].playback.position == 0.0);

    /* Phase 2: true varispeed ratios include source-rate correction. */
    assert(fabs(sts_playback_rate(-24, 44100, 44100) - 0.25) < 1e-12);
    assert(fabs(sts_playback_rate(-48, 44100, 44100) - 0.0625) < 1e-12);
    assert(fabs(sts_playback_rate(0, 22050, 44100) - 0.5) < 1e-12);

    /* Phase 3: a sample selection is pending until the next trigger. */
    s.channel[0].selected_sample = 0;
    s.channel[0].pending_sample = 1;
    assert(s.channel[0].selected_sample == 0);
    trigger_step(&s, 2);
    assert(s.channel[0].selected_sample == 1);

    /* Phase 4: C Phrygian quantizes D down to Db on an equal-distance tie. */
    assert(s.scale == 4 && s.root_note == 0);
    assert(s.pitch_mode == 0);
    assert(quantize_pitch(&s, -22) == -23);
    assert(move_scale_steps(&s, -24, 1) == -23);
    assert(move_scale_steps(&s, -23, -1) == -24);

    /* Phase 5: touch writes only the step currently crossing the playhead. */
    s.channel[0].pending_sample = 0;
    s.channel[1].pending_sample = 0;
    s.pitch_mode = 1;
    s.channel[0].touched = 1;
    s.channel[0].touch_lease = s.output_rate / 2;
    s.channel[0].input_pitch = -12;
    s.free_beat = 0.75; /* step 4, zero based index 3 */
    s.clock_step = -1;
    int16_t out[2] = {0, 0};
    render_block(&s, out, 1);
    assert(s.channel[0].sequence[3] == -12);
    assert(s.channel[0].sequence[2] == -24);

    /* Chain macro touch convention arms the corresponding pitch writer. */
    set_param(&s, "left_pitch_touch", "0");
    assert(s.channel[0].touched == 0);
    set_param(&s, "left_pitch_touch", "1");
    assert(s.channel[0].touched == 1);

    /* Untouched values must not record; the dedicated UI supplies touches. */
    set_param(&s, "left_pitch_touch", "0");
    s.channel[0].current_step = 5;
    set_param(&s, "left_pitch", "-12");
    assert(s.channel[0].sequence[5] == -24);
    s.channel[1].current_step = 6;
    set_param(&s, "right_pitch", "-36");
    assert(s.channel[1].sequence[6] == -24);

    set_param(&s, "length", "1");
    assert(s.steps == 16);
    set_param(&s, "gate_toggle", "15");
    trigger_step(&s, 15);
    assert(!s.channel[0].active && s.channel[1].active);
    set_param(&s, "edit_channel", "1");
    assert(!s.channel[0].touched && !s.channel[1].touched);
    set_param(&s, "touch", "pitch,1");
    set_param(&s, "pitch", "0");
    assert(s.channel[1].input_pitch == -53);
    assert(s.channel[1].sequence[15] == -53);
    set_param(&s, "write", "0");
    set_param(&s, "pitch", "1");
    assert(s.channel[1].input_pitch == -52);
    assert(s.channel[1].sequence[15] == -53);
    set_param(&s, "randomize", "1");
    for (int i=0;i<16;++i) assert(pitch_allowed(&s,s.channel[1].sequence[i]));
    assert(s.channel[0].sequence[15] == -24);
    int saved_note = s.channel[1].sequence[0];
    set_param(&s, "root", "2");
    assert(s.channel[1].sequence[0] == saved_note);
    set_param(&s, "touch", "pitch,0");
    set_param(&s, "edit_channel", "0");

    /* A present host clock returning -1 means transport stopped, not free-run. */
    host_api_v1_t stopped_host;
    memset(&stopped_host, 0, sizeof(stopped_host));
    stopped_host.get_beat_position = stopped_beat_position;
    stopped_host.get_bpm = test_bpm;
    s.host = &stopped_host;
    s.channel[0].active = 1;
    s.channel[1].active = 1;
    s.clock_step = 3;
    out[0] = 123;
    out[1] = -123;
    render_block(&s, out, 1);
    assert(s.channel[0].active == 0 && s.channel[1].active == 0);
    assert(s.clock_step == -1);
    /* A stop ramps out instead of cutting, then is silent. */
    for (int i = 0; i < STS_DECLICK_FRAMES; ++i) render_block(&s, out, 1);
    assert(s.channel[0].fade_left == 0 && s.channel[1].fade_left == 0);
    out[0] = 123;
    out[1] = -123;
    render_block(&s, out, 1);
    assert(out[0] == 0 && out[1] == 0);
    s.host = NULL;

    /* Phase 6: voices have independent positions and output channels. */
    trigger_step(&s, 4);
    out[0] = out[1] = 0;
    render_block(&s, out, 1);
    assert(out[0] > 0);
    assert(out[1] < 0);
    assert(s.channel[0].playback.position > 0.0);
    assert(s.channel[1].playback.position > 0.0);

    /* Legacy volume parameters remain loadable, despite no longer having knobs. */
    set_param(&s, "left_volume", "50");
    set_param(&s, "right_volume", "25");
    assert(s.channel[0].volume == 50 && s.channel[1].volume == 25);

    /* Declick: a retrigger hands the sounding voice to the fade slot, so the
     * old voice ramps out under the new one instead of jumping. */
    float long_data[512];
    for (int i = 0; i < 512; ++i) long_data[i] = 0.5f;
    sts_sample_t long_sample = {long_data, 512, 44100};
    attach(&s, 0, 2, &long_sample);
    s.channel[0].pending_sample = 2;
    int saved_gates[STS_STEPS];
    memcpy(saved_gates, s.channel[0].gates, sizeof(saved_gates));
    for (int i = 0; i < 16; ++i) s.channel[0].gates[i] = 1;
    trigger_step(&s, 0);
    for (int i = 0; i < 40; ++i) render_channel(&s, 0);
    assert(s.channel[0].playback.active);
    trigger_step(&s, 1);
    assert(s.channel[0].fade_left == STS_DECLICK_FRAMES);
    assert(s.channel[0].fade.position > 0.0);
    /* A gate cut fades too, rather than stopping dead. */
    s.channel[0].gates[2] = 0;
    for (int i = 0; i < 40; ++i) render_channel(&s, 0);
    trigger_step(&s, 2);
    assert(!s.channel[0].active && s.channel[0].fade_left == STS_DECLICK_FRAMES);
    int16_t first = render_channel(&s, 0);
    assert(first > 0);
    for (int i = 0; i < STS_DECLICK_FRAMES; ++i) render_channel(&s, 0);
    assert(render_channel(&s, 0) == 0);
    s.channel[0].gates[2] = 1;
    /* A sample end ramps to zero: the last frames are scaled down. */
    sts_playback_engine_t tail = {.position = 511.0, .rate = 1.0, .active = 1};
    float last = voice_next(&tail, &long_sample);
    assert(last > 0.0f && last < 0.05f);

    /* Clock jitter: a host reading one step BEHIND the last trigger must not
     * retrigger; a real jump back (restart) must. */
    host_api_v1_t jitter_host;
    memset(&jitter_host, 0, sizeof(jitter_host));
    jitter_host.get_beat_position = settable_beat;
    jitter_host.get_bpm = test_bpm;
    s.host = &jitter_host;
    s.steps = 16;
    s.clock_step = -1;
    g_beat = 0.26;   /* step 1 */
    render_block(&s, out, 1);
    assert(s.clock_step == 1);
    double played = s.channel[0].playback.position;
    g_beat = 0.2499; /* a hair behind: step 0 */
    render_block(&s, out, 1);
    assert(s.clock_step == 1);
    assert(s.channel[0].playback.position > played);
    g_beat = 1.3;    /* step 5 */
    render_block(&s, out, 1);
    assert(s.clock_step == 5);
    g_beat = 0.0;    /* restart: more than one step back is real */
    render_block(&s, out, 1);
    assert(s.clock_step == 0);
    s.host = NULL;
    memcpy(s.channel[0].gates, saved_gates, sizeof(saved_gates));
    s.channel[0].pending_sample = 0;
    trigger_step(&s, 0);
    for (int i = 0; i < 2 * STS_DECLICK_FRAMES; ++i) {
        render_channel(&s, 0);
        render_channel(&s, 1);
    }

    /* Phase 7: compact state preserves setup and both pitch sequences. */
    char state[2048];
    assert(get_param(&s, "state", state, sizeof(state)) > 0);
    sts_lite_t restored;
    memset(&restored, 0, sizeof(restored));
    restored.output_rate = 44100;
    init_defaults(&restored);
    set_param(&restored, "state", state);
    assert(restored.base_pitch == s.base_pitch);
    assert(restored.pitch_range == s.pitch_range);
    assert(restored.channel[0].sequence[3] == -12);
    assert(restored.channel[0].pending_sample == s.channel[0].pending_sample);
    assert(restored.channel[0].volume == 50);
    assert(restored.channel[1].volume == 25);
    assert(restored.steps == 16);
    assert(restored.channel[0].gates[15] == 0);
    assert(restored.channel[1].sequence[15] == s.channel[1].sequence[15]);
    set_param(&restored, "pitch_range", "12");
    assert(restored.pitch_range == 60);
    set_param(&restored, "state", "{\"range\":12}");
    assert(restored.pitch_range == 60);
    /* A corrupted state cannot store a pitch outside the playable range. */
    set_param(&restored, "state", "{\"l0\":1000,\"r3\":-1000}");
    assert(restored.channel[0].sequence[0] == 60);
    assert(restored.channel[1].sequence[3] == -60);

    /* The display sees the step's playback pitch, separately from K8 input. */
    sts_lite_t visual;
    memset(&visual, 0, sizeof(visual));
    visual.output_rate = 44100;
    init_defaults(&visual);
    char pitch_value[32], perf[128];
    visual.clock_step = 0;
    visual.channel[0].sequence[0] = -12;
    assert(get_param(&visual, "eff_pitch", pitch_value, sizeof(pitch_value)) > 0);
    assert(atoi(pitch_value) == pitch_index(&visual, -12));
    assert(get_param(&visual, "performance_status", perf, sizeof(perf)) > 0);
    assert(strstr(perf, ",1,0,-12") != NULL);
    visual.channel[0].current_step = 1;
    visual.channel[0].sequence[1] = -36;
    assert(get_param(&visual, "eff_pitch", pitch_value, sizeof(pitch_value)) > 0);
    assert(atoi(pitch_value) == pitch_index(&visual, -36));
    visual.channel[0].touched = 1;
    assert(get_param(&visual, "eff_pitch", pitch_value, sizeof(pitch_value)) > 0);
    assert(atoi(pitch_value) == pitch_index(&visual, -24));
    visual.channel[0].touched = 0;
    visual.edit_channel = 1;
    visual.channel[1].sequence[0] = -47;
    assert(get_param(&visual, "eff_pitch", pitch_value, sizeof(pitch_value)) > 0);
    assert(atoi(pitch_value) == pitch_index(&visual, -47));

    /* Shadow UI must bypass the generic editor and load ui_chain.js. */
    char ui_contract[1024];
    memset(ui_contract, 0x7f, sizeof(ui_contract));
    assert(get_param(&s, "ui_hierarchy", ui_contract, sizeof(ui_contract)) == 0);
    assert(ui_contract[0] == '\0');
    assert(get_param(&s, "grid_hierarchy", ui_contract, sizeof(ui_contract)) > 0);
    assert(strstr(ui_contract, "STS Lite") != NULL);
    assert(strstr(ui_contract, "\"knobs\":[\"volume\",\"pitch\"]") != NULL);
    assert(strstr(ui_contract, "\"knobs\":[\"base_pitch\",\"root\"") != NULL);
    assert(strstr(ui_contract, "\"label\":\"Scale\"") != NULL);
    assert(get_param(&s, "chain_params", ui_contract, sizeof(ui_contract)) > 0);
    assert(strstr(ui_contract, "\"key\":\"sample\"") == NULL);
    assert(strstr(ui_contract, "\"key\":\"pitch_range\"") == NULL);
    assert(strstr(ui_contract, "A Volume") == NULL);
    assert(strstr(ui_contract, "B Volume") == NULL);
    assert(strstr(ui_contract, "\"name\":\"Record\"") == NULL);

    puts("sts-lite: phases 1-7 passed");
    return 0;
}
