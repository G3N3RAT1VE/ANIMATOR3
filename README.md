# AUTOPILOT - realtime 3-stem retrigger for Ableton (hitech psytrance oriented)

Two VST3 plug-ins built from one codebase (installed to `C:\Program Files\Common Files\VST3`):

| Plug-in | Type | Goes on | Job |
|---|---|---|---|
| **AUTOPILOT Send** | audio effect | each source audio track | streams the track into slot A / B / C |
| **AUTOPILOT** | instrument | one MIDI track | 3 independent loop engines, outputs **Mix + Stem 1 + Stem 2 + Stem 3** |

(Formerly "RiQ Live". Internal plug-in IDs are unchanged, so existing Live sets keep working.)

## Ableton setup

1. Put **AUTOPILOT Send** on each source track and pick its slot **A / B / C**. Any number of tracks may
   share a slot - they are summed exactly, like routing several tracks into one audio track.
   While the transport plays the source is muted there (AUTOPILOT plays it); stopped, it passes through.
2. A MIDI track: load **AUTOPILOT**. Stem A/B/C sources default to Send A/B/C.
3. Separate outputs: 3 audio tracks, *Audio From* -> the AUTOPILOT track -> **Stem 1 / 2 / 3**, monitoring **In**.
   Set the stem's **OUT -> Own** to take it off the Mix output.
4. MIDI into the AUTOPILOT track: keyboard fills on 16 keys from **C1** (*Fill Base Note*), scenes on **C3-G#3**.
5. Everything is an automatable parameter, so Ableton's MIDI map works on any control.

## Screen (top to bottom)

- **Top bar**: preset browser, global KEEP, GRID STYLE (Classic / Hitech), Mix loudness (LUFS-S, peak,
  gain reduction), limiter DRIVE / CEIL / LIMIT, master GAIN.
- **AUTOPILOT deck**: IQ ARRANGE / IQ EVOLVE / IQ REVERSE, LINK A B C, the **phrase map** (plan for the
  whole phrase: brightness = arrange, magenta = ratchet, arrow = reverse, dot = evolve, red cross = drop,
  lime frame = current bar), live meters of what each stem uses right now, and the autopilot settings:
  Phrase 4/8/16, Evolve every 1-16 bars, Drift rate 1/2-16 bars, Smooth, drift targets (RAT / REV / SIL / SLICE),
  Drift, Build, Fill, Ratchet, Evolve %, Rev Depth, Drop.

### How the autopilot works (generative)
- Like Rhythmiq's Beat Assistant, **IQ takes over the stem knobs and moves them over time**, and every step of
  the Arrange / Reverse knobs (17 steps) has **its own pattern** - so a drifting knob walks through different
  patterns, not just more or fewer edits. Evolve (button or IQ Evolve) re-rolls the pattern of the step that is
  playing, and the step remembers it.
- **Drift**: IQ and each linked stem's Arrange / Ratchet / Reverse (optionally Silence and slice size) follow
  smoothed random walks - each stem its own, never repeating. **Rate** sets the bars between new targets,
  **Smooth** glides between them (off = jumps on the grid). The phrase map draws the walk; the IQ knob's magenta
  ring shows where IQ is right now.
- **Build / Fill** raise the centre of the walk across the phrase and in its last bar; **Drop** lets a stem sit out a bar.

- **Master strip**: loop length 1/2/4 bars, fill-key slice size, Freeze Q (Off/Beat/Bar/Loop/1/8/1/16),
  Scene Q (Off/Beat/Bar/2/4/8/16 bars), summed waveform, fill keys.
- **Stem lanes A / B / C**: source, KEEP (per stem, or Global), slice 1/4-1/32, **IN / FROZEN**,
  FREEZE (hold = repeat current slice), AUTO (follow the autopilot), Arrange / Reverse / Silence / Ratchet
  with Evolve, ON / SOLO / OUT, Pan / Gain. The waveform shows the edits: arcs = material moved by
  Arrange, white ticks = ratchet retriggers, arrow = reversed slice, magenta = edited.
- **Scenes**: RESET + 1-8 (SAVE then a scene, or Shift-click, to store).

## Grid

- **KEEP**: Off, Downbeat, 1 & 3, All Beats, 2 & 4, Off-Beats - slices starting there are never edited.
  Per stem it can override the global setting (kick: All Beats, hats: Off-Beats).
- **Hitech style**: groove bars keep the 16th phase inside the beat (the rolling bass stays in phase);
  edits pile up on the last beat, the last two beats in a phrase-end bar; rolls accelerate x2 -> x4.
  Ratchet never uses triplets. **Classic** is the original weighting.

## Presets

Whole plugin state (all parameters, scenes, variations) as files in `Documents\AUTOPILOT\Presets`
(`*.riqpreset`): `<` `>` step, click the name for the list / Save as / Rename / Delete / Open folder,
SAVE stores (same name overwrites), DEL deletes. Scenes are also saved with the Live set.

## Limiter / loudness

True-peak limiter on the Mix **and on each stem output**: 1.5 ms lookahead, 4x inter-sample peak
detection, no overshoot; ceiling in dBTP (default 0.0), Drive up to +18 dB, release 10-500 ms.
Below the ceiling the audio passes bit-exact. Engine latency = one audio block + limiter lookahead,
reported to Ableton (delay compensation lines it up).

## Build

```
cmake -B build -G "Visual Studio 16 2019" -A x64 -DRIQ_BUILD_TESTS=ON
cmake --build build --config Release
build\RiqLiveTest_artefacts\Release\RiqLiveTest.exe
```
Needs JUCE 8 at `C:/JUCE` (override with `-DJUCE_DIR=...`) and CMake >= 3.22.
Genre notes for development: `.claude/skills/hitech-psytrance/SKILL.md`.

## Limits
- Frozen buffers are not saved with the Live set (audio is re-captured after reload); settings, scenes and presets are.
- Up to 16 Sends in total; AUTOPILOT and AUTOPILOT Send must run in the same Live process (normal for Ableton).
- Tempo change while frozen repitches the frozen loop (Hermite-interpolated); while IN the buffer restarts at the new tempo.
