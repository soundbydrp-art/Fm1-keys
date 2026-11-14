# Frodhes's Rhodes engine

These files are copied **unchanged** from [Frodhes](https://github.com/leonardoMirabal/frodhes) by Leonardo Mirabal,
commit `8cf3ea312cad94064362b6ae9b772aa34e0cac9c` (12 Nov 2026, version 0.9.11), licence **GPL-3.0-only** (the
repository's `LICENSE`):

| File | From | What |
|---|---|---|
| `rhodes.c` | `firmware/src/` | the physically modelled Rhodes voice |
| `fxp.h` | `firmware/src/` | its fixed-point helpers |
| `rhodes_tables.h`, `rhodes_phim.h`, `pulse_pz.h`, `rh_tone_g.h`, `rh_drive_mk.h`, `rh_top.h` | `firmware/gen/` | the per-key physics, pickup, hammer pulses and level tables its host tools generate |
| `fx_tables.h` | `firmware/gen/` | used here only for the voicing knobs' laws (`KN_P64`, `KN_P32`, the noise levels) |

FM-1 KEYS includes `rhodes.c` in its unity build with `DINGHY_HOST` defined (Frodhes's plain build: no section
placement, no register hint, the same integers), posts notes with `rh_note_on` / `rh_note_off`, maps its knobs to
`rh_tone_off`, `rh_bark_s/g`, `rh_bell`, `rh_k_decay`, `rh_k_rel`, `rh_type`, and mixes `rh_render`'s block
(`app.c`: `keys_apply`, `app_render`). To update: copy the same files again from a newer Frodhes commit and note it here.
