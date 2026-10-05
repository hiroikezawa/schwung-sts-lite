#include "sts_playback_engine.h"

#include <math.h>

double sts_playback_rate(double semitones, double source_rate, double output_rate) {
    double sr_ratio = output_rate > 0.0 ? source_rate / output_rate : 1.0;
    return sr_ratio * pow(2.0, semitones / 12.0);
}

float sts_hermite4(const float *data, uint32_t frames, double position) {
    if (!data || frames == 0) return 0.0f;
    if (position <= 0.0) return data[0];
    uint32_t i = (uint32_t)position;
    if (i >= frames) return 0.0f;
    double t = position - (double)i;
    uint32_t im1 = i > 0 ? i - 1 : 0;
    uint32_t ip1 = i + 1 < frames ? i + 1 : frames - 1;
    uint32_t ip2 = i + 2 < frames ? i + 2 : frames - 1;
    double xm1 = data[im1], x0 = data[i], x1 = data[ip1], x2 = data[ip2];
    double c0 = x0;
    double c1 = 0.5 * (x1 - xm1);
    double c2 = xm1 - 2.5 * x0 + 2.0 * x1 - 0.5 * x2;
    double c3 = 0.5 * (x2 - xm1) + 1.5 * (x0 - x1);
    return (float)(((c3 * t + c2) * t + c1) * t + c0);
}

void sts_playback_engine_reset(sts_playback_engine_t *engine) {
    if (!engine) return;
    engine->position = 0.0;
    engine->rate = 1.0;
    engine->active = 0;
}

void sts_playback_engine_trigger(sts_playback_engine_t *engine, double semitones,
                                 double source_rate, double output_rate,
                                 int sample_available) {
    if (!engine) return;
    engine->position = 0.0;
    engine->rate = sts_playback_rate(semitones, source_rate, output_rate);
    engine->active = sample_available && source_rate > 0.0 && engine->rate > 0.0;
}

float sts_playback_engine_next(sts_playback_engine_t *engine,
                               const float *data, uint32_t frames) {
    if (!engine || !engine->active) return 0.0f;
    if (!data || engine->position >= (double)frames) {
        engine->active = 0;
        return 0.0f;
    }
    float value = sts_hermite4(data, frames, engine->position);
    engine->position += engine->rate;
    if (engine->position >= (double)frames) engine->active = 0;
    return value;
}
