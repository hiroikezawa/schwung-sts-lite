# STS Lite

**English** | [日本語](README.ja.md)

A sound generator for [Schwung](https://github.com/charlesvestal/schwung) on Ableton Move.
It is a pared-down clone of the Eurorack module
**[Stereo Triggered Sampler (STS)](https://github.com/4ms/STS)**. The playback is based on
the STS firmware (MIT License, Dan Green and Hugo Paris). It is unofficial and not
affiliated with the original STS's authors or maker.

Four independent samplers, **A**, **B**, **C** and **D**, restart their samples from the
top on every 1/16 note in time with Move's transport (retrigger).

- **Varispeed**: pitch changes with playback speed, like tape or vinyl, with no time
  stretching. Lower is slower and deeper, higher is faster and higher.
- **Per-step pitch**: A–D each hold pitches for 16 steps, which you can write in real time
  by turning knob 8 (motion recording).
- **Output**: A is panned 30 % left, B 30 % right, C 50 % left and D 50 % right (as a share
  of the way from centre to edge), with a pan law where each voice's left and right sum to
  1. When all four play loudly at once the total can exceed full scale; the output simply
  caps it (the numbers never wrap around into loud noise). Set the levels with knobs 1–4.

A–D each keep their own sample, pitch pattern, step on/off and level, and voices you are not
editing on screen keep playing.

## Differences from the original STS

**Kept from the original STS**

- Independent playback channels (the STS has two; STS Lite raises it to four, A–D)
- Choosing the sample to play from a bank of 10 samples
- A trigger restarts the sample from the top
- Varispeed pitch, changing with playback speed

**Left out**

- Recording (the STS records straight to microSD)
- Start (playback start position), Length (how much to play) and reverse playback
- CV inputs (1V/oct pitch, sample select and so on) and the End Out trigger
- Multiple banks: the STS can switch between up to 600 samples (10 samples × 60 banks);
  STS Lite has one fixed bank of 10 samples each for A–D
- Stereo playback: samples are mixed to mono and output with A panned 30 % left, B 30 %
  right, C 50 % left and D 50 % right
- Long files: only the first 5 seconds of each file are loaded
- Pitch range: where the STS covers −10 to +4.3 octaves, STS Lite covers 5 octaves around
  BASE

**Added in STS Lite**

- Instead of external triggers, an 8/16-step sequencer synced to Move's tempo plays them
- Per-step pitch and hit on/off
- Pitch entry that follows a scale (ROOT / SCALE)
- Motion recording that writes step pitches from knob movement
- RANDOM, which generates pitch phrases

## Installation

### From a release (recommended)

1. Download the latest `sts-lite-module.tar.gz` from [Releases](../../releases).
2. Upload it in Schwung Manager (`http://<Move's IP>:7700`) under Modules → "Install custom".
   With this repository on hand,
   `scripts/install.py http://<Move's IP>:7700 --package sts-lite-module.tar.gz` does the same.
3. **Power-cycle Move once.**

### Building from source

You need an aarch64 cross compiler (`gcc-aarch64-linux-gnu`) and Python 3.

```bash
scripts/build.sh arm64                               # creates dist/sts-lite-module.tar.gz
python3 scripts/install.py http://<Move's IP>:7700   # upload through Schwung Manager
```

> **Always power-cycle Move after an update.** A DSP (dsp.so) that has been loaded is not
> replaced by reloading the slot.

## Getting started

1. Put samples in place (see "Samples"). No samples are included.
2. Load STS Lite as a slot's synth and open that synth: you get its own screen, and the
   32 pads turn into STS Lite's control surface.
3. Press Play on Move and A–D start playing on every 1/16 note. **Nothing plays while Move
   is stopped.**

Leaving the screen (Back, Menu, holding a track) keeps it playing, and the pads and their
colours go back to Move. STS Lite does not respond to MIDI notes; only its built-in steps
make sound.

## Screen

The top line shows the current state.

```
1/2 A01 P-24 3/8 ARM          Main
```

| Shown | Meaning |
| --- | --- |
| `1/2` | Current page / number of pages. The page name (`Main` / `Scale`) also appears at the right |
| `A01` | The voice being edited (A–D) and the sample number (01–10) |
| `P-24` | The pitch playing now (semitones; 0 is the sample's own speed) |
| `3/8` | Playing step / number of steps. `STOP` while stopped |
| `ARM` / `REC` | Recording armed / writing while knob 8 is touched (nothing shown with recording off) |

While choosing a sample (below) it changes to `SAMPLE A 01/10`.

## Pads

```
row 1  [ -- ][ -- ][ -- ][ -- ][ -- ][ -- ][RANDOM][REC]
row 2  [SAMPLE][ -- ][ -- ][ -- ][ A  ][ B  ][ C  ][ D  ]
row 3  with 16 steps: steps 9-16
row 4  steps 1-8
```

| Pad | Action |
| --- | --- |
| REC (top right) | Motion recording on/off. Bright red = on, dark red = off. Starts off |
| RANDOM (left of REC) | Makes a new pitch phrase for the voice being edited. Does not change step on/off |
| A / B / C / D (right four of row 2) | Choose the voice to edit. The chosen voice lights brightly. All of them keep playing when you switch |
| SAMPLE (left end of row 2) | While held, you choose samples: pads 1–8 of row 4 pick samples 1–8, the left two of row 3 samples 9 and 10. The chosen number lights brightly |
| Steps (rows 4 and 3) | Each press toggles that step's hit on/off. The bright light is the play position. On an off step that voice does not sound |

- A sample change takes effect from the start of the next step.
- With 8 steps the row 3 pads are not used (except samples 9 and 10 while SAMPLE is held).

## Knobs

| Knob | Action |
| --- | --- |
| 1–4 | Levels of A, B, C and D (0–100 %). All four can be turned at any time, whichever voice is being edited |
| 5–7 | Unused |
| 8 | Pitch of the voice being edited. One click = one scale note |

Knob 8 steps only through notes in the current scale. It shows the note name and the number
of semitones (e.g. `Db -23st`). Even turned quickly, it moves exactly as far as you turn it.

### Page 2 (Scale)

Turn the jog wheel to go to page 2 (the top left shows `2/2`, the top right `Scale`). Turn
back for the performance screen (`1/2`, `Main`).

| Knob | Action |
| --- | --- |
| 1–4 | Levels of A, B, C and D (same as page 1) |
| 5 BASE | The pitch centre. −48 / −36 / −24 / −12 / 0 semitones (starts at −24) |
| 6 ROOT | Root of the scale (C–B, starts at C) |
| 7 SCALE | chromatic / major / minor / dorian / phrygian / lydian / mixolydian / locrian / pentatonic (starts at phrygian) |
| 8 STEPS | 8 / 16 steps (starts at 8; shared by A–D) |

Knob 8's range is 30 semitones either side of BASE (5 octaves), never beyond −60 to +60
semitones. With the initial setting (BASE −24), for example, it is −54 to +6 semitones.
−12 semitones is half speed (an octave down), +12 semitones double speed (an octave up).

The scale applies **only at entry**. Changing ROOT or SCALE later does not rewrite pitches
already recorded.

## Motion recording

Writes per-step pitches from knob 8's movement.

1. Turn REC on (bright red; `ARM` on screen).
2. Play Move.
3. It records **only while you touch knob 8** (`REC` on screen). Each step the play position
   passes gets the pitch of that moment. Turning while touching lays the turned pitches out
   across the steps.
4. Letting go stops recording.

- Recorded pitches play on their step every loop.
- Nothing is recorded while not playing, because there is no play position.
- The REC state is not saved. Reopening the set always starts with it off.
- While the pattern has movement, knob 8's dial shows the pitch playing on the current step
  as a moving value, separate from the knob's position (the pitch you enter). While you
  touch knob 8 it is also shown at the top of the screen as `knob value>playing valuest`.

## RANDOM

Pressing RANDOM (the pad left of the top-right one) replaces **the pitch pattern of the
voice being edited (one of A–D)** with a new phrase.

### What changes and what does not

- Only the edited voice's pitches change, for the current number of steps (8 or 16). With
  8 steps, steps 9–16 are not changed.
- Step on/off, samples, levels and the other voices do not change.
- There is no undo. If you do not like it, press again for a different phrase.
- Even if knob 8 is touched during motion recording, writing stops at the moment you press.

### How the phrase is made

1. **Pick a source phrase**: one of six built-in phrases (8 notes each) is chosen. If it is
   the same one chosen last time for that voice, it is re-picked from the other five, so the
   same source phrase never comes twice in a row.
2. **Shift half the notes**: for each step, with a 50 % chance, one of −12 / −7 / −5 / −3 /
   −2 / −1 / +1 / +2 / +3 / +5 / +7 / +12 semitones is added at random (octave, fifth,
   fourth, third, second, semitone, up or down).
3. **Fit the range**: notes outside a range from a fifth below (−7 semitones) to 17
   semitones above the edited voice's knob 8 note are moved up or down by octaves into it.
   So turning knob 8 before pressing changes the phrase's height. When knob 8 is near the
   ends of BASE's range (30 semitones either side), the whole range is shifted to stay
   within BASE's range.
4. **Fit the scale**: notes not in the current ROOT / SCALE are moved to the nearest scale
   note within the range (the lower one on a tie).

With 16 steps, the 8-note source phrase is repeated twice and steps 2–4 are applied to each
step separately. So the first and second halves are similar but usually a little different.

### Source phrases

In semitones (0 = the sample's own speed) they are as below. What actually plays is these
with steps 2–4 applied, so it is not these values as such.

| # | Steps 1–8 |
| --- | --- |
| 1 | −7, +5, −7, −9, +34, +46, −16, −9 |
| 2 | 0, −9, +7, +34, +48, +12, +10, −9 |
| 3 | −2, +1, +7, +12, +15, +46, +51, −17 |
| 4 | −2, +1, −7, +10, +48, +43, −17, −14 |
| 5 | +44, −4, +19, −11, −7, +7, +8, +34 |
| 6 | +44, +43, +17, +29, +19, +51, −16, −24 |

### About the random numbers

The random numbers start from the same seed every time the module is loaded, and are shared
by A–D. So if the RANDOM presses since loading (which voice, how many times) and knob 8 /
ROOT / SCALE / BASE / number of steps are the same, you get the same sequence of phrases
every time. The random state is not saved in sets or presets.

## Samples

Put WAVs in the following places. You can upload them from Schwung Manager's file screen.
They survive module updates.

```
/data/UserData/UserLibrary/Samples/STS Lite/left/01.wav to 10.wav    (for A)
/data/UserData/UserLibrary/Samples/STS Lite/right/01.wav to 10.wav   (for B)
/data/UserData/UserLibrary/Samples/STS Lite/ch3/01.wav to 10.wav     (for C)
/data/UserData/UserLibrary/Samples/STS Lite/ch4/01.wav to 10.wav     (for D)
```

- Format: PCM 16 / 24 / 32-bit or 32-bit float, mono or stereo (stereo is mixed to mono),
  1–384 kHz.
- Each file is loaded **up to its first 5 seconds**; the rest is ignored. One step uses
  about 0.7 s even at the highest pitch (+30 semitones) at 120 BPM, so normal use is not
  affected.
- On load, leading silence is trimmed (playback starts where the sound first appears) and
  peaks are evened out (−0.2 dBFS). Near-silent files are left as they are, not raised. The
  WAVs on disk are not changed.
- Loading happens when the module starts. Restart Move after putting samples in place.

## How it sounds the way it does

- At the start of each step the sample restarts from the top at that step's pitch. If the
  sample is shorter than the step, the sound stops there.
- Varispeed playback uses 4-point Hermite interpolation. Differences in the samples' sample
  rates are corrected automatically.
- Retriggers, off steps, the end of a sample and stops get a fade of about 1.5 ms so they do
  not click.
- STS Lite runs on its own tempo and receives no MIDI, so it declares
  `capabilities.requires_continuous_processing` to stop Schwung parking it when silent
  (Schwung 1.7.3 or later). Without this, a slot parked while stopped can miss the first few
  steps after Play.

## What is saved

Sets and presets save, for each of A–D, the pitch and on/off of 16 steps, the sample number
and the level, plus BASE / ROOT / SCALE / STEPS and the voice being edited (A–D). Saved data
from the old 8-step version and the two-voice (A and B only) version loads as is; C and D
then start in their initial state (sample 1, level 100 %, all steps on, pitch −24).

## Development

```
scripts/test.sh            # native build + engine tests (ASan/UBSan) + UI tests + ARM64 build and ABI check
scripts/build.sh arm64     # build for Move: dist/sts-lite-module.tar.gz
scripts/install.py http://<Move's IP>:7700   # install through Schwung Manager
```

The UI tests draw the screen with the same Schwung page controller as Move, so they need a
Schwung checkout. The default is `../schwung` next to this repository; `SCHWUNG_ROOT` sets
it too. On WSL, if there is no node, the Windows `node.exe` is used.

| File | Contents |
| --- | --- |
| `dsp/sts_lite.c` | The plugin API v2 entry points, WAV loading (worker thread), following the step clock, playback and fades |
| `dsp/performance.h` | Parameters for the screen, RANDOM, state save / restore |
| `dsp/sts_playback_engine.{h,c}` | Playback engine (varispeed and Hermite interpolation) |
| `ui_core_v0302.mjs` | Screen and pad handling (uses Schwung's page controller) |
| `ui_chain.js` | Connects it to Schwung's screen |

The audio path does no memory allocation or locking. Samples are loaded only by a worker
thread lowered to normal priority and moved to cores 0–2, and removing the module stops it
at once, even mid-load. The number in `ui_core_v0302.mjs` is there to avoid QuickJS's cache
and is only raised when the contents change.

## Licence

STS Lite is under the MIT License (`LICENSE`). The licences of the works it is based on are
collected in `THIRD_PARTY_LICENSES.md`.

- **The STS firmware**: MIT License, Copyright (c) 2017 Dan Green and Hugo Paris. The playback
  (varispeed with 4-point Hermite interpolation, and mono mixing by averaging left and right)
  is based on it. Only the software is used; the STS hardware design (CC BY-NC-SA 4.0) and
  the STS sample sounds are not included.
- **The Schwung header**: `third_party/schwung/host/plugin_api_v1.h` is the Schwung plugin API
  header, MIT License, Copyright (c) Charles Vestal (`third_party/schwung/LICENSE`).
