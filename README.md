# RiQ Live — realtime Rhythmiq-style retrigger for Ableton

Two VST3 plug-ins built from one codebase (installed to `C:\Program Files\Common Files\VST3`):

| Plug-in | Type | Goes on | Job |
|---|---|---|---|
| **RiQ Live Send** | audio effect | each of your 3 source audio tracks | streams the track into slot A / B / C |
| **RiQ Live** | instrument | one MIDI track | 3 independent loop engines, outputs **Mix + Stem 1 + Stem 2 + Stem 3** |

## Ableton setup

1. Tracks with your 3 sources (e.g. Kick / Snare / Hats, or any live input): put **RiQ Live Send** on each, set slot **A**, **B**, **C**.
   While the transport plays the source is muted there (RiQ Live plays it); stopped, it passes through.
   *THRU WHILE PLAYING* keeps the dry track audible as well.
2. A MIDI track: load **RiQ Live**. Stem 1/2/3 sources default to Send A/B/C.
3. Separate outputs: create 3 audio tracks, set *Audio From* → the RiQ Live track → **Stem 1 / Stem 2 / Stem 3**, monitoring **In**.
   On each stem set **OUT → Own** so it leaves the Mix output (no doubling). **OUT → Mix** keeps it on the main output.
4. MIDI into the RiQ Live track: keyboard fills on 16 keys from **C1** (36, adjustable via *Fill Base Note*), scenes on **C3–G#3** (C3 = Reset, C#3… = 1…8).
5. Any knob/button can be MIDI-mapped with Ableton's own MIDI map (all controls are automatable parameters).

The engine reports one audio block of latency; Ableton's delay compensation lines it up with the grid.
Untouched slices are bit-exact copies of the input (verified by the offline test).

## Per stem (3 independent engines)

- **IN / FROZEN** — IN: the track streams live into a 4-bar buffer; untouched slices play the live signal.
  Click to freeze the buffer (quantised by *Buffer Freeze Q*: Off / Beat / Bar / Loop); everything is then retriggered from the frozen audio.
- **Slice size** 1/4 · 1/8 · 1/16 · 1/32
- **FREEZE** (hold) — repeats the currently playing slice (Rhythmiq's Freeze)
- **ARRANGE** — chance a slice is replaced / repeated (loops the previous slice or jumps to the same position in another beat)
- **REVERSE**, **SILENCE** (drops the lowest-energy slices), **RATCHET** (subdivides a slice into 2 or 4 retriggers, no triplets)
- **EVOLVE** under Arrange and under Reverse — new variation for that knob
- **ON / SOLO / PAN / GAIN / OUT (Mix|Own)**

**Keep strong beats** (Off / Downbeat / 1 & 3 / All beats): slices starting on those beats are never rearranged, reversed or ratcheted —
loops and subdivision happen only on the weak beats. More action toward the end of the bar.

## Beat Assistant (as in Rhythmiq)

- **IQ ARRANGE** takes over the Arrange (and Ratchet) of linked stems — the orange inner ring on the knob shows the live value.
  Touching a linked stem's Arrange knob sets IQ Arrange back to 0.
- **IQ EVOLVE** presses the stem Evolve buttons by itself (they lock while it's on and flash when it fires).
- **IQ REVERSE** reverses slices on alternating bars; touching a linked Reverse knob turns it off.
- **A / B / C** link buttons choose which stems the assistant drives.

### Autopilot
- **Phrase** 4 / 8 / 16 bars — last bar is the fill bar (plus a half-way fill for 8/16); progress shown under the selector
- **Fill** — how hard the fill bars go · **Motion** — bar-to-bar variation · **Fill Ratchet** — subdivision in fills
- **Evolve Every** 1/2/4/8/16 bars + **Evolve Chance** — when IQ Evolve fires
- **Rev Depth** — IQ Reverse amount · **Drop** — chance a linked stem sits out a bar (never on the phrase start, one always keeps playing)

## Master
Loop length 1 / 2 / 4 bars, fill slice size, scene quantisation, master gain, limiter (Mix bus), scenes **RESET + 1–8**
(click = recall, **SAVE** then a slot or Shift-click = store).

## Build

```
cmake -B build -G "Visual Studio 16 2019" -A x64 -DRIQ_BUILD_TESTS=ON
cmake --build build --config Release
build\RiqLiveTest_artefacts\Release\RiqLiveTest.exe
```
Needs JUCE 8 at `C:/JUCE` (override with `-DJUCE_DIR=...`) and CMake ≥ 3.22.
`docs/rhythmiq_manual_excerpt.txt` is the original Rhythmiq manual text this was modelled on.

## Limits
- Frozen buffers are not saved with the Live set (audio is re-captured after reload); scenes and settings are.
- Any number of Sends may share a slot (up to 16 Sends in total); a slot plays the exact sum of its tracks.
  RiQ Live Send and RiQ Live must run in the same Live process (normal for Ableton).
- Tempo change while frozen repitches the frozen loop (Hermite-interpolated); while IN the buffer restarts at the new tempo.
