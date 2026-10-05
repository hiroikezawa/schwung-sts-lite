# STS Varispeed VST3

A deliberately small, monophonic one-shot sampler for Windows 11 and Ableton
Live. It uses the same playback-position, sample-rate correction, four-point
Hermite interpolation, boundary handling, and hard retrigger code as Schwung's
STS Lite module.

## DSP provenance

The original implementation was audited in
`dsp/sts_lite.c`. Its audio path stores a
mono float buffer plus integer source rate, advances a double-precision
fractional position, calculates true-varispeed rate at trigger time, performs
four-point Hermite interpolation, clamps edge taps to the first/last frame,
and stops when the position reaches the frame count. It has no additional
filter or anti-alias stage.

Those sound-defining operations now live in
`dsp/sts_playback_engine.{h,c}` (repository root) and are called by both STS Lite
and this plug-in. WAV parsing remains platform-specific: the Move module uses
its compact C decoder and the VST3 uses JUCE for file/Unicode integration, then
converts 16/24/32-bit mono or stereo WAV to the same mono float layout.

## Behavior

- One WAV file, downmixed to mono exactly as STS Lite does, then sent to both outputs.
- Every MIDI Note On restarts frame zero; Note Off and velocity are ignored.
- Rate is `source sample rate / host sample rate * 2^(semitones / 12)`.
- Root Note defaults to C3 (MIDI 60), Pitch Offset is -48 to +24 semitones.
- Gain is effectively silent at -60 dB and ranges through +12 dB.
- The absolute sample path, parameters, and missing-sample state are saved in
  the plug-in state. Load or Reload can relocate a missing file.

There is intentionally no loop, warp, time stretch, granular processing,
filter, envelope, sequencer, sample start/end edit, or multisampling.

## Windows build

Install Visual Studio 2022 with **Desktop development with C++**, Git, and
CMake 3.22 or newer. From a Developer PowerShell:

```powershell
cmake -S vst/sts-varispeed -B build/sts-varispeed -G "Visual Studio 17 2022" -A x64
cmake --build build/sts-varispeed --config Release --target STSVarispeed_VST3
ctest --test-dir build/sts-varispeed -C Release --output-on-failure
```

The bundle is written below
`build/sts-varispeed/STSVarispeed_artefacts/Release/VST3/`. Copy it to a VST3
folder scanned by Live, normally `%CommonProgramFiles%\VST3`.

To use an existing JUCE checkout and avoid FetchContent, append
`-DJUCE_DIR=C:\path\to\JUCE`. The dependency is pinned to JUCE 9.0.1 when
fetched automatically.

## DSP-only verification

The exact shared playback core can be built on any C++17 host without JUCE:

```sh
cmake -S vst/sts-varispeed -B build/sts-core -DSTS_BUILD_PLUGIN=OFF
cmake --build build/sts-core
ctest --test-dir build/sts-core --output-on-failure
```

## License

The source is MIT (repository `LICENSE`); the playback engine is based on the
STS firmware (MIT, Dan Green and Hugo Paris). A built VST3 links JUCE, which
is AGPLv3 or commercially licensed, so distributing the binary must follow
one of those. See the repository's `THIRD_PARTY_LICENSES.md`.
