# STS Lite 0.2.11
Two independent mono varispeed samplers, A (left output) and B (right output).
Both keep their own sample selection, 16 pitch values and trigger gates.
The second-row rightmost pad switches the editing target; both continue playing.
Uses the shared STS playback engine (Hermite interpolation, no time stretching).

## Performance UI
Open STS Lite's component editor in Schwung. It uses the shared knob-page UI.
While this screen is visible all 32 pads are blocked from native Move playback.
Leaving restores native pad ownership and LEDs.

| Control | Action |
| --- | --- |
| Top-right pad (99) | Motion recording on/off: bright red on, dim red off |
| Adjacent pad (98) | New pitch phrase for the selected sound |
| Second row from top, rightmost (91) | Switch edit sound A/B |
| Second row from top, leftmost (84) + bottom pads | Hold 84 and choose sample 1-8 on pads 68-75, or 9-10 on pads 76-77 |
| Bottom row (68-75) | Toggle trigger steps 1-8 |
| Next row up (76-83) | Toggle trigger steps 9-16 in 16-step mode |
| K1 | Selected sound's volume (0-100%) |
| K2-K7 | Unassigned |
| K8 | Selected sound's pitch, one scale degree per encoder detent |
| Jog page 2 | Base pitch, root, scale, 8/16 steps |

The K2-K7 gaps are applied only while handling input and drawing the page.
They are not stored in the shared page controller, whose contract refresh
expects every stored page key to name a parameter.
K8 preserves multi-detent relative CC values during fast turns; the shared
generic input path reduces those values to a single direction step.
K8 release flushes the controller's pending final pitch before closing the DSP
touch gate, so a last-second turn is recorded into the current step.

Recording starts disarmed. It writes pitch only when armed, K8 is touched,
and transport is running. Holding K8 writes its current pitch across steps.
Touch refresh expires after half a second if the UI disappears.
Sample/volume are live controls, not motion lanes.
The K8 widget shows the current step's recorded pitch as a moving live value,
while its base pointer remains at the pitch selected by the encoder. The
header also shows the currently playing semitone and selected sample number.
The randomizer uses MANIAC's six Phrase Gen source phrases and 50% mutation,
folded into the configured pitch range and scale. It leaves trigger gates intact.

Quantization is at INPUT: K8 indexes a table of valid notes. Playback uses the
saved semitone values directly. Changing the scale does not rewrite a recorded
phrase. An OFF trigger cuts that voice for that step. Tempo is shared, 1/16 per
step; transport stop silences both voices. Every cut -- a retrigger, an OFF
trigger, a sample's end and a transport stop -- ramps out over 64 frames
(~1.5 ms) instead of jumping, so steps do not click.
The module declares requires_continuous_processing: it keeps its own time and
receives no MIDI, so the slot must not be idle-parked (a parked slot renders
one block in 172, which made the first steps after Play late).
Pitch range is fixed at the maximum five octaves (60 semitones), including
when loading older presets. State v3 stores both full 16-step pitch/gate
patterns, volumes, sample choices, scale/base, length and edit target.
Old eight-step states remain readable.
Armed/touched states are transient and never restored armed.

## Samples
Persistent paths (unchanged):
- /data/UserData/UserLibrary/Samples/STS Lite/left/01.wav through 10.wav
- /data/UserData/UserLibrary/Samples/STS Lite/right/01.wav through 10.wav

These paths are outside the module installation. Module-local samples/left and
samples/right remain fallbacks. PCM16/24/32 and float32 WAV, mono or stereo
(downmixed during preload), 1-384 kHz. Only the first five seconds of a file
are loaded; the rest is ignored. A data chunk that claims more than the file
holds keeps what is actually there.
Samples are loaded on a worker; switching chooses a preloaded slot at retrigger.
Removing the module abandons a load in progress between 4096-frame chunks.
On load, the mono signal is trimmed to its first audible frame (a -80 dBFS
floor and -60 dB relative-to-peak threshold), then peak-normalized to 0.98.
Files below the silence floor are left silent rather than amplifying noise.
The WAV files on disk are not changed; a module reload reprocesses them.

## Build, test, install
```
scripts/test.sh                       # engine (ASan/UBSan), UI, Move ABI check
scripts/build.sh arm64                # dist/sts-lite-module.tar.gz
scripts/install.py http://move.local:7700   # upload via Schwung Manager
```
Restart Move after installing. The UI test runs the real Schwung page
controller and framebuffer, so it needs a Schwung checkout: a sibling
`../schwung`, or `SCHWUNG_ROOT`. The cross build needs `aarch64-linux-gnu-gcc`.
The core keeps its 0210 file name: it is unchanged in 0.2.11.

## Layout
- `dsp/` -- the module (`sts_lite.c`, `performance.h`) and the shared
  playback engine (`sts_playback_engine.{h,c}`).
- `vst/sts-varispeed/` -- a VST3 one-shot sampler built on the same engine.
- `third_party/schwung/` -- Schwung's plugin API header (MIT).
