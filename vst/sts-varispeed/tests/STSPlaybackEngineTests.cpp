#include "sts_playback_engine.h"

#include <cstdlib>
#include <cmath>

static void require(bool condition) {
    if (!condition) std::abort();
}

int main() {
    require(std::abs(sts_playback_rate(0.0, 48000.0, 48000.0) - 1.0) < 1.0e-12);
    require(std::abs(sts_playback_rate(12.0, 48000.0, 48000.0) - 2.0) < 1.0e-12);
    require(std::abs(sts_playback_rate(-12.0, 48000.0, 48000.0) - 0.5) < 1.0e-12);
    require(std::abs(sts_playback_rate(0.0, 48000.0, 44100.0)
                    - (48000.0 / 44100.0)) < 1.0e-12);

    const float sample[] = {0.0f, 1.0f, 0.0f, -1.0f, 0.0f};
    sts_playback_engine_t engine {};
    sts_playback_engine_trigger(&engine, 0.0, 44100.0, 44100.0, 1);
    require(sts_playback_engine_next(&engine, sample, 5) == 0.0f);
    require(sts_playback_engine_next(&engine, sample, 5) == 1.0f);

    sts_playback_engine_trigger(&engine, -12.0, 44100.0, 44100.0, 1);
    require(engine.position == 0.0);
    require(sts_playback_engine_next(&engine, sample, 5) == 0.0f);
    const float half = sts_playback_engine_next(&engine, sample, 5);
    require(std::abs(half - 0.5625f) < 1.0e-6f);

    sts_playback_engine_trigger(&engine, 0.0, 44100.0, 44100.0, 0);
    require(!engine.active);
    require(sts_playback_engine_next(&engine, nullptr, 0) == 0.0f);
}
