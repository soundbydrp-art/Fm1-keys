# FM-1 KEYS — handoff

Paste this into a new session to resume. Last updated 2026-11-14 (0.1-dev, emulator only; KEYS is now Frodhes's Rhodes engine, licence GPL-3.0-only).

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
- KEYS = Frodhes's Rhodes engine (Leonardo Mirabal, GPL-3.0-only), vendored unchanged in `app/frodhes/` (see its
  README: commit 8cf3ea3, v0.9.11). Andy chose this on 2026-11-14 after hearing my own tine model as distorted and
  Frodhes as beautiful; the whole firmware is therefore GPL-3.0-only (LICENSE at the root; donationware is fine).
  `app.c` includes `frodhes/rhodes.c` with DINGHY_HOST defined (plain build). Notes: `do_note_on` posts `rh_note_on`
  when the model is a piano TYPE; `do_note_off` always posts `rh_note_off`. `keys_apply()` (every frame) maps the knobs
  (0..100 here → 0..127 there, 50 = its middle) to rh_tone_off / rh_bark_s,g / rh_bell / rh_k_decay / rh_k_rel /
  rh_type, NOISE fixed at its default 48, SPREAD 56, then `rh_cc_poll()`. `app_render` calls `rh_render(keys_mono, n)`
  (Q15, n = 128) and mixes mid ± side at KEYS_G = 2.1/32768 (hardest note ≈ 0.45 in the mix). This app's tremolo, amp,
  tone and reverb follow. ALGO = model: STAGE 77, SUITCASE, EARLY FELT, BELL DYNO, STRING, TUBE, MODAL. Measured: in
  tune within 4 cents C2–C6, real velocity dynamics (v70 0.03 → v127 0.12 before gain). The tine display reads
  Frodhes's `rv[]` voices (lvl / lvl0, held).
  Not used from Frodhes: its effects, HAMMER/NOISE/SPREAD knobs, RESPONSE, bars (vibes/marimba), two-core split.
  Device risks to check on hardware: Frodhes places its hot loops and tables in RAM/pool sections and uses core 1 for
  effects; here it runs in the plain build from flash on one core, with ~25 KB of its state in .bss.
- PHYS: STRING = one-loop waveguide (one-pole LP for damping, first-order allpass for stiffness, tuned by the loop
  filters' phase delay); bowed string = STK-style two delay lines either side of the bow with the bow table (in tune
  to ±3 cents). TUBE = inverting one-loop bore with a reed table (blow) or noise. MODAL = 12 modes, material table
  wood/glass/metal/skin with log-interpolated ratios anchored to the heard mode; strike = half-sine force; bow on a
  body = a locked drive at the note's mode (a model of the result, not the friction itself — a real friction loop
  drifted 50–100 cents) with the driven mode damped while bowed; noise/blow = filtered noise.
- PHYS realism pass (2026-11-14, after Andy said the other patches stood out next to Frodhes's piano; sources:
  Jaffe & Smith's Karplus-Strong extensions, Smith's PASP, STK, marimba bar tuning 1:4:10):
  - every key is a fixed hair out of tune (±2 cents), brighter/duller (±6 %), longer/shorter (±8 %); velocity sets
    the exciter's brightness (`v->exb`) and shortens the mallet's contact; mallet contact noise on strikes.
  - plucked/struck strings: loss filter is now the two-point average (1−S)+S·z⁻¹ (S from BRIGHT) so harmonics ring
    on instead of dying in 0.2 s; a second polarisation (`wgn` line, 0.07 % sharp, quieter, 2.4× the ring) gives the
    fast-then-slow decay and slow beating; ring time shortens up the keyboard.
  - bodies after the PHYS mix (`body_set`/`body_tick`): plucked string = soundbox resonances 104/208/415 Hz; bowed =
    violin 285/460/2600 Hz with a 170 Hz low cut; tube = bell radiation (highs out). Bow and reed get a delayed 5.3 Hz
    vibrato and noise riding the bow speed / breath pressure.
  - modal: wood is a tuned marimba bar (1 : 3.98 : 9.92, upper modes heavily damped, fundamental boosted by its
    tube); near metal the top three modes are detuned twins of the lowest three (the bell's warble); a struck skin's
    pitch falls after the blow (`glide`, per block).
  - voice steal fade 5.7 ms; output limit is linear to 0.7 then a soft shoulder (`soft_limit`), not tanh on everything.
  - Host render now ~2 % of real time, emulator ~5 % (strings run two lines).
- Gain staging (fixed 2026-11-14 — Andy heard most patches as distorted): the loudest single note of each path peaks
  near 0.3 in the mix (`v->g` in voice_start; host-only `expect bus` reports the mix peak ×1000 and resets it), chords
  of 4 peak 0.26–0.83, the output soft limit is tanh(1.3x). The amp (DRIVE) is unity when soft: tanh(xg)·(1/g + ½(1−1/g)).
  (My own tine model, now removed:) the pickup used to fold the wave (the tine swung past the flux slope's peak) and ignored velocity; now the swing is
  O·(0.15 + 0.55·BARK)·velocity around O = 0.3 + 0.5·TONE, always on the near side of the pole, and the output scales
  with velocity: soft notes are clean (3rd harmonic −59 dB at C4), BARK adds a lopsided 2nd harmonic (−25 dB).
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
1. Andy's ears on the tour after the PHYS realism pass (below): which patches still stand out next to the piano.
   Ideas not yet done: 2× oversampling of the reed and bow tables, a jet (flute) exciter for the tube, sympathetic
   strings, a body for the struck materials (drum shell), Frodhes's vibes/marimba (`bars.c`, GPL, could be vendored).
2. BASS engine for hip-hop: 808 (sine + pitch drop + drive), Reese (detuned saws), FM bass, pluck bass; glide/mono;
   "print mode": evenly spaced one-shots up a scale to sample straight into the MPC 1000 (it samples at 40 kHz,
   ~26 s per sample in stock memory, so short notes with gaps for chopping).
3. Chord progressions (a row of degrees stepped per key or by PLAY), inversions/augmentation by hand, pads (slow
   attack on all engines).
4. Device CPU: measure on hardware (the unity build is -Os; if tight, move the DSP to an `o2` unit or drop NMODE to 8).
5. Release setup: release.yml, `install` and a signed package; check `id_prefix` "FM-1_58" is unused on The Firmware
   Hub. Licence is settled: GPL-3.0-only.

## Andy's preferences that apply
Check work before handing over (one iteration); keep this file current; donationware likely (compatible with the GPL). I'm "Wren" to him.
