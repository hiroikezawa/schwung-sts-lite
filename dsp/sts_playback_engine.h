#ifndef SCHWUNG_STS_PLAYBACK_ENGINE_H
#define SCHWUNG_STS_PLAYBACK_ENGINE_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    double position;
    double rate;
    int active;
} sts_playback_engine_t;

double sts_playback_rate(double semitones, double source_rate, double output_rate);
float sts_hermite4(const float *data, uint32_t frames, double position);
void sts_playback_engine_reset(sts_playback_engine_t *engine);
void sts_playback_engine_trigger(sts_playback_engine_t *engine, double semitones,
                                 double source_rate, double output_rate,
                                 int sample_available);
float sts_playback_engine_next(sts_playback_engine_t *engine,
                               const float *data, uint32_t frames);

#ifdef __cplusplus
}
#endif

#endif
