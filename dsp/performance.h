/* UI-facing selected-channel controls. Pitch is a scale-degree index: no
 * chromatic dead detents, and playback never re-quantizes stored notes. */
static int pitch_notes(const sts_lite_t *s, int *notes) {
    int count = 0;
    for (int n = pitch_min(s); n <= pitch_max(s); ++n)
        if (pitch_allowed(s, n)) notes[count++] = n;
    return count;
}

static int pitch_index(const sts_lite_t *s, int semitones) {
    int notes[121], count = pitch_notes(s, notes);
    int target = quantize_pitch(s, semitones);
    for (int i = 0; i < count; ++i)
        if (notes[i] == target) return i;
    return 0;
}

static int pitch_has_motion(const sts_lite_t *s, const sts_channel_t *c) {
    for (int i = 0; i < s->steps; ++i)
        if (c->sequence[i] != c->input_pitch) return 1;
    return 0;
}

static uint32_t phrase_random(sts_lite_t *s) {
    uint32_t x = s->rng;
    x ^= x << 13; x ^= x >> 17; x ^= x << 5;
    return s->rng = x;
}

static void randomize_pitch(sts_lite_t *s) {
    /* Same six Phrase Gen source phrases and 50% mutation as MANIAC.
     * Fold into this sampler's configured range before choosing a degree. */
    static const int source[6][8] = {
        {53,65,53,51,94,106,44,51},{60,51,67,94,108,72,70,51},
        {58,61,67,72,75,106,111,43},{58,61,53,70,108,103,43,46},
        {104,56,79,49,53,67,68,94},{104,103,77,89,79,111,44,36}
    };
    static const int mutations[] = {-12,-7,-5,-3,-2,-1,1,2,3,5,7,12};
    int src = phrase_random(s) % 6;
    sts_channel_t *c = &s->channel[s->edit_channel];
    if (src == c->random_source) src = (src + 1 + (int)(phrase_random(s) % 5)) % 6;
    c->random_source = src;
    for (int i = 0; i < s->steps; ++i) {
        int n = source[src][i & 7] - 60;
        if ((phrase_random(s) & 255) < 128)
            n += mutations[phrase_random(s) % 12];
        while (n > pitch_max(s)) n -= 12;
        while (n < pitch_min(s)) n += 12;
        c->sequence[i] = quantize_pitch(s, n);
    }
}

static int performance_set(sts_lite_t *s, const char *key, const char *value, int v) {
    sts_channel_t *c = &s->channel[s->edit_channel];
    if (!strcmp(key, "edit_channel")) {
        s->channel[0].touched = s->channel[1].touched = 0;
        s->edit_channel = clampi(v, 0, 1);
    } else if (!strcmp(key, "write")) {
        s->pitch_mode = !!v;
    } else if (!strcmp(key, "length")) {
        s->steps = (v == 16 || v == 1) ? 16 : 8;
        s->clock_step = -1;
        for (int ch = 0; ch < 2; ++ch) s->channel[ch].current_step %= s->steps;
    } else if (!strcmp(key, "root")) {
        s->root_note = floor_mod(v, 12); requantize(s);
    } else if (!strcmp(key, "touch")) {
        c->touched = value && !strcmp(value, "pitch,1");
        c->touch_lease = c->touched ? s->output_rate / 2 : 0;
    } else if (!strcmp(key, "sample")) {
        c->pending_sample = clampi(v, 0, 9);
    } else if (!strcmp(key, "volume")) {
        c->volume = clampi(v, 0, 100);
    } else if (!strcmp(key, "pitch")) {
        int notes[121], count = pitch_notes(s, notes);
        if (count) c->input_pitch = notes[clampi(v, 0, count - 1)];
        if (s->pitch_mode && c->touched && s->clock_step >= 0)
            c->sequence[c->current_step] = c->input_pitch;
    } else if (!strcmp(key, "gate_toggle")) {
        if (v >= 0 && v < s->steps) c->gates[v] ^= 1;
    } else if (!strcmp(key, "randomize")) {
        if (v) randomize_pitch(s);
    } else return 0;
    return 1;
}

static int performance_get(sts_lite_t *s, const char *key, char *buf, int len) {
    sts_channel_t *c = &s->channel[s->edit_channel];
    if (!strcmp(key, "edit_channel")) return snprintf(buf,len,"%d",s->edit_channel);
    if (!strcmp(key, "write")) return snprintf(buf,len,"%d",s->pitch_mode);
    if (!strcmp(key, "length")) return snprintf(buf,len,"%d",s->steps == 16);
    if (!strcmp(key, "root")) return snprintf(buf,len,"%d",s->root_note);
    if (!strcmp(key, "scale")) return snprintf(buf,len,"%d",s->scale);
    if (!strcmp(key, "sample")) return snprintf(buf,len,"%d",c->pending_sample);
    if (!strcmp(key, "volume")) return snprintf(buf,len,"%d",c->volume);
    if (!strcmp(key, "pitch")) return snprintf(buf,len,"%d",pitch_index(s,c->input_pitch));
    if (!strcmp(key, "eff_pitch")) {
        int note = s->clock_step >= 0 && !c->touched
            ? c->sequence[c->current_step] : c->input_pitch;
        return snprintf(buf,len,"%d",pitch_index(s,note));
    }
    if (!strcmp(key, "performance_status")) {
        char gates[17];
        for (int i=0;i<16;++i) gates[i] = c->gates[i] ? '1' : '0';
        gates[16]=0;
        int playing_note = s->clock_step >= 0 && !c->touched
            ? c->sequence[c->current_step] : c->input_pitch;
        return snprintf(buf,len,"%d,%d,%d,%d,%d,%d,%d,%d,%d,%s,%d,%d,%d",
            s->edit_channel,s->pitch_mode,s->steps,s->clock_step,c->input_pitch,
            s->root_note,s->scale,s->base_pitch,s->pitch_range,gates,
            pitch_has_motion(s,c),c->pending_sample,playing_note);
    }
    /* An empty ui_hierarchy tells Shadow UI to load ui_chain.js. Returning
       the grid hierarchy here would route the module into the generic
       hierarchy editor, so the dedicated display/pad takeover never ran. */
    if (!strcmp(key, "ui_hierarchy")) {
        if (len) buf[0] = '\0';
        return 0;
    }
    if (!strcmp(key, "grid_hierarchy"))
        return snprintf(buf,len,
            "{\"modes\":null,\"levels\":{\"root\":{\"label\":\"STS Lite\","
            "\"knobs\":[\"volume\",\"pitch\"],"
            "\"params\":[\"volume\",\"pitch\","
            "{\"level\":\"scale\",\"label\":\"Scale\"}]},"
            "\"scale\":{\"label\":\"Scale\",\"knobs\":[\"base_pitch\",\"root\",\"scale\",\"length\"],"
            "\"params\":[\"base_pitch\",\"root\",\"scale\",\"length\"]}}}");
    if (!strcmp(key, "chain_params")) {
        int notes[121], count = pitch_notes(s,notes);
        return snprintf(buf,len,
            "[{\"key\":\"volume\",\"name\":\"Volume\",\"type\":\"int\",\"min\":0,\"max\":100},"
            "{\"key\":\"base_pitch\",\"name\":\"Base\",\"type\":\"int\",\"min\":-48,\"max\":0,\"step\":12},"
            "{\"key\":\"pitch\",\"name\":\"Pitch\",\"type\":\"int\",\"min\":0,\"max\":%d,\"step\":1},"
            "{\"key\":\"root\",\"name\":\"Root\",\"type\":\"enum\",\"options\":[\"C\",\"Db\",\"D\",\"Eb\",\"E\",\"F\",\"Gb\",\"G\",\"Ab\",\"A\",\"Bb\",\"B\"]},"
            "{\"key\":\"scale\",\"name\":\"Scale\",\"type\":\"enum\",\"options\":[\"chromatic\",\"major\",\"minor\",\"dorian\",\"phrygian\",\"lydian\",\"mixolydian\",\"locrian\",\"pentatonic\"]},"
            "{\"key\":\"length\",\"name\":\"Steps\",\"type\":\"enum\",\"options\":[\"8\",\"16\"]}]",count-1);
    }
    if (!strcmp(key, "state")) {
        char state[2048];
        int n=snprintf(state,sizeof(state),
            "{\"version\":3,\"base\":%d,\"range\":%d,\"root\":%d,\"scale\":%d,"
            "\"ls\":%d,\"rs\":%d,\"lv\":%d,\"rv\":%d,\"steps\":%d,\"edit\":%d",
            s->base_pitch,s->pitch_range,s->root_note,s->scale,
            s->channel[0].pending_sample,s->channel[1].pending_sample,
            s->channel[0].volume,s->channel[1].volume,s->steps,s->edit_channel);
        for(int ch=0;ch<2;++ch) for(int i=0;i<16;++i)
            n+=snprintf(state+n,sizeof(state)-(size_t)n,",\"%c%d\":%d,\"%cg%d\":%d",
                ch?'r':'l',i,s->channel[ch].sequence[i],ch?'r':'l',i,s->channel[ch].gates[i]);
        snprintf(state+n,sizeof(state)-(size_t)n,"}");
        if ((int)strlen(state)>=len) return -1;
        return snprintf(buf,len,"%s",state);
    }
    return -2;
}
