# FM-1 KEYS — handoff

Paste this into a new session to resume. Last updated 2026-11-13 (first build, 0.1-dev, emulator only).

## What it is
Andy's own firmware for his M-VAVE FM-1 (newer hardware: UC25HQ80 flash, JEDEC B3 60 14 — current fm1-base accepts
it). Goal, in his words: sick hip-hop bass to sample into an MPC 1000, a really high-quality physically modelled
Rhodes, chords/pads from one trigger with key/scale/inversions, a solid physical-modelling synth (plucks, strings,
wind, materials), and a really clean, animated UI. 0.1 does the Rhodes-style tine piano, the physical-modelling
voice and the chord engine. Bass comes next.

Repo: github.com/soundbydrp-art/fm1-keys (Andy's). Pages (when enabled: Settings > Pages > main, /docs):
soundbydrp-art.github.io/fm1-keys — the emulator; demo audio at /audio/fm1-keys-tour.mp3.

## Layout
- `app/` — the firmware: `app.c` (everything), `app.h`, `fm1.toml`, `tests/basic.txt` (32 checks),
  `tests/screens.txt` (doc shots), `docs/MANUAL.md`, `site/landing.md`, `words.tsv`.
- `base/` — fm1-base submodule (Charles Vestal, MPL-2.0; bsp = felucca-bsp).
- `docs/` — the built browser emulator (copied from `app/build/emu`), screenshots, the audio tour. GitHub Pages.
- Build from the repo root:
  - `python3 base/tools/build.py app --host`, then `"app/build/host/fm-1 keys_host" app/tests/basic.txt app/build/host`
  - `python3 base/tools/build.py app --web` (Ubuntu emcc 3.1.6 needs a shim named `emcc` first on PATH rewriting
    `-sSTACK_SIZE=` to `-sTOTAL_STACK=`), then copy `app/build/emu/*` into `docs/` (drop the empty `o2/`, `web/`).
  - device `.fwsc`: GitHub Actions via `base/template/.github/workflows/release.yml` (zvenson/fm1-build@v1) — not set
    up yet. Check any package with fwsc_ids.py (both flash IDs present) before flashing.

## Design (as built)
- 8 voices, steal = quietest released else oldest, 2 ms fade before the stolen note starts. Same note restrikes.
- KEYS: 4 complex-oscillator modes (tine f0, tonebar f0×1.0011±, cantilever overtones ×6.27 and ×17.55), half-sine
  hammer (contact shorter with BELL and velocity, capped at 0.45 period), pickup = flux slope (x+o)/((x+o)²+1)^1.5 ×
  velocity, normalised to unit small-signal gain; A (swing) from velocity^1.6 and BARK, O (offset) from TONE.
  Damper on note-off (fundamental by DAMPER, overtones faster). Hammer knock = filtered noise burst. FX: tanh drive,
  stereo tremolo.
- PHYS: STRING = one-loop waveguide (one-pole LP for damping, first-order allpass for stiffness, tuned by the loop
  filters' phase delay); bowed string = STK-style two delay lines either side of the bow with the bow table (in tune
  to ±3 cents). TUBE = inverting one-loop bore with a reed table (blow) or noise. MODAL = 12 modes, material table
  wood/glass/metal/skin with log-interpolated ratios anchored to the heard mode; strike = half-sine force; bow on a
  body = a locked drive at the note's mode (a model of the result, not the friction itself — a real friction loop
  drifted 50–100 cents) with the driven mode damped while bowed; noise/blow = filtered noise.
- Per-path gains measured so a velocity-100 note peaks alike (KEYS 5.5, modal strike 5, etc.) — `v->g` in voice_start.
- Measured (tests and renders): KEYS, strings, tube, bowed string within ±5 cents C2–C6; four-note chords peak
  ~0.35–0.48 of full scale at full master; host render 1.2 % of real time; emulator 1–2 % CPU on a desktop.
- Chords: diatonic stack in thirds on the played note's degree (snapped down into the scale), 3–6 notes; CLOSE,
  OPEN (drop-2 / lifted third), LEAD (inversion + octave closest to the last chord, range 40–92), LEAD+BASS. Strum
  0–136 ms per note via delayed events in the render. Each key/MIDI note owns the notes it started (ref counts), so
  overlapping chords release correctly; CC64 sustain, CC120/123 all off.
- UI: pages SOUND/TONE/CHORD/FX; header with patch, chord name, eight voice lights and page bars; animated middle
  (tines with swing blur and glowing pickups; the strings' real delay-line contents; the body's mode wobble and
  spectrum; chord keyboard with numeral; level history and reverb tail); footer cells with fill bars that light when
  turned. FONT_M is capitals only: chord names are written MIN/MAJ7, a flat's "b" drawn in FONT_B.
- Session (store object at 0xFC000): magic KEY1, params, patch, octave; validated on load.

## Not done yet (next steps, in order)
1. Andy's ears on the tour (docs/audio) and the emulator: KEYS character, levels, which patches need work.
2. BASS engine for hip-hop: 808 (sine + pitch drop + drive), Reese (detuned saws), FM bass, pluck bass; glide/mono;
   "print mode": evenly spaced one-shots up a scale to sample straight into the MPC 1000 (it samples at 40 kHz,
   ~26 s per sample in stock memory, so short notes with gaps for chopping).
3. Chord progressions (a row of degrees stepped per key or by PLAY), inversions/augmentation by hand, pads (slow
   attack on all engines).
4. Device CPU: measure on hardware (the unity build is -Os; if tight, move the DSP to an `o2` unit or drop NMODE to 8).
5. Release setup: release.yml, `install` and a signed package; check `id_prefix` "FM-1_58" is unused on The Firmware
   Hub; licence line (donationware) to decide.

## Andy's preferences that apply
Check work before handing over (one iteration); keep this file current; donationware likely, so the licence in
fm1.toml ("All rights reserved") is a placeholder. I'm "Wren" to him.
