---
name: hitech-psytrance
description: Genre traits of hitech psytrance (Glosolalia, Arcek, Flossy Waves, Cyberhen, Will O' Wisp and similar) and how they map onto AUTOPILOT's grid, quantisation and autopilot settings. Use when tuning the Hitech grid style, autopilot behaviour, default presets, or answering how something should sound "hitech".
---

# Hitech psytrance - what matters for AUTOPILOT

Sources: general genre knowledge plus a short web search (October 2026). The user's reference
WAVs were **not** analysed (they cancelled that), so nothing below is measured from their tracks.

## Confirmed facts about the named artists (web search)
- **Arcek** - project of Alejandro Rayas Rivera, releasing since 2006; described as fast BPM,
  powerful basslines, "crazy hi-tech sounds"; listed among representative hi-tech artists
  (with Highko, Psykovsky, Gotalien).
- **Will O' Wisp** - Gaston Mellino, Buenos Aires; called one of the pioneers of South American
  darkpsy; releases on Dark Prisma Records and OSOM Music.
- **Glosolalia** (one "s") - psytrance composer from Buenos Aires, releases on Dark Prisma Records.
- **Flossy Waves, Cyberhen** - nothing reliable found; do not invent facts about them.
- User's reference folder (names only): Kliluk, Yucatec, Qawaq (Kamino Records), Sonic Ripples,
  Spiral, Frenesi Hertz - Kliluk tracks are labelled 200 BPM.

## Genre traits (general knowledge, treat as guidance, not measurement)
- **Tempo**: roughly 180-200+ BPM; 200 BPM is common, some go faster. Strict 4/4.
- **Kick on every beat**, short and tight. The kick is the anchor: it is almost never edited inside
  a groove bar. -> stem holding the kick: `KEEP = All Beats`.
- **Rolling bass**: kick + bass on the remaining three 16ths of each beat (K-B-B-B). The 16th phase
  inside the beat is sacred in groove bars - moving a bass 16th onto the kick position kills the roll.
  -> Hitech grid style only allows edits that keep the in-beat 16th position in groove bars.
- **Off-beat elements**: open hats / percussion on the 8th "and". -> hats stem: `KEEP = Off-Beats`.
  `2 & 4` is for a clap/snare layer if a track has one; `1 & 3` and `Downbeat` stay available.
- **Edits are constant but placed**: micro-edits, glitch stutters, 1/32 and 1/64 rolls, reversed
  swells, short silences. They pile up at the **end of the bar** (last beat, especially its second
  half) and much more at the **end of 4/8/16-bar phrases** (last bar, often last two beats).
  A smaller "call" edit at the end of beat 2 answers the bar-end fill.
- **Rolls accelerate into the downbeat**: x2 then x4 (1/32 -> 1/64 at the slice size used).
  **No triplets** - the user explicitly rejected triplet ratchets; keep subdivisions straight.
- **Phrasing**: elements enter/leave every 8 or 16 bars; scene changes belong on phrase boundaries
  (Scene Q = 8 or 16 bars), never mid-phrase unless it is a deliberate break.
- **Drops/breaks**: whole layers drop out for a bar or more and slam back on bar 1 of a phrase.
  -> autopilot `DROP`, never on phrase bar 1, at least one stem keeps playing.
- **Loudness**: masters are very loud and dense; the user wants every output at a 0 dBTP ceiling
  (true-peak limiter on Mix and on each stem output, Drive to push loudness).

## Mapping to AUTOPILOT settings (starting point)
| Stem content | KEEP | Slice | Arrange | Ratchet | Reverse | AUTO |
|---|---|---|---|---|---|---|
| Kick | All Beats | 1/16 | 0-0.2 | 0 | 0 | off or on |
| Rolling bass | Downbeat | 1/16 | 0.2-0.5 | 0.3-0.6 | 0.1 | on |
| Hats / perc / FX | Off-Beats | 1/32 | 0.3-0.7 | 0.5-0.9 | 0.2-0.4 | on |

- Grid style **Hitech**; Phrase **8 bars** (16 for long builds); Evolve every **4** bars, chance ~0.5.
- Fill 0.7-0.9, Fill Ratchet 0.6-0.8, Motion 0.3-0.5, Drop 0.1-0.25, Rev Depth 0.2-0.3.
- Buffer Freeze Q **Bar** or **Loop**; Scene Q **8 Bars**.
- Autopilot is generative (user rejected canned, repeating fills and fixed A-A-B-A forms): IQ and the
  linked stems' parameters drift on smoothed random walks; every Arrange/Reverse knob step has its own
  pattern, so drifting = new patterns. Starting point: Drift 0.4-0.7, Rate 2-4 bars, Smooth on,
  drift RAT + REV (SIL / SLICE for wilder sections), Build 0.5-0.8, Fill 0.5-0.8.

## Code pointers
- Grid decisions: `Source/StemEngine.cpp` `StemEngine::decide()` (GridStyle::hitech branch:
  roll zone = last beat, or last two beats in a phrase-end bar; groove bars keep the 16th phase).
- Autopilot plan: `Source/Autopilot.h` `planAutopilotBar()` - pure function, also drawn by the
  phrase map in the editor (drift walks via driftNoise, build/fill, drops, evolve).
- Per-step pattern memory: `seedIndex()` / `knobStep()` in PluginProcessor.h.
- Keep options: `Protect` enum (append only - saved sets store indices).
