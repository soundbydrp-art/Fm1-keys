/* FM-1 KEYS. Copyright (C) 2026 Andy. All rights reserved.
 * Two physical models and a chord engine for the FM-1:
 *  - KEYS: a tine electric piano. A felt hammer (a half-sine force pulse; harder hits are shorter, so brighter) rings
 *    four modes: the tine's fundamental, the tonebar coupled to it (a hair off, so they beat), and the tine's two
 *    cantilever overtones (x6.27, x17.55: the bell of the attack). The tine's motion passes an electromagnetic pickup,
 *    modelled as the flux's slope at the tine's place times its speed: small swings come out clean, big ones (hard
 *    playing, a narrow gap) bend into harmonics, and where the tine sits against the pole (TONE) sets warm or nasal.
 *  - PHYS: an exciter (pluck, strike, bow, blow, noise) into a resonator: a waveguide string (loop filter for damping,
 *    an allpass for stiffness, tuned exactly), a reed tube (clarinet-like bore), or a modal body of 12 modes whose
 *    ratios and damping morph from wood to glass to metal to skin.
 *  - CHORDS: one key plays the diatonic chord on its scale degree, 3 to 6 notes, close, open, voice-led, or voice-led
 *    over a bass note, strummed if asked.
 * Pages: SOUND, TONE, CHORD, FX (SELECT turns; HOME, EDIT, ARP, FX jump). ALGO picks the engine, PRESET the patch,
 * K1..K4 the four controls drawn at the bottom. PLAY auditions the patch with a progression. MIDI in plays it.
 * Part of fm1-base's unity build (gfx.c, announce.c and lib/touch in scope). */
#include "fastmath.h"

#define NV 8
#define NMODE 12
#define FS 44100.0f
#define PI_F 3.14159265f
#define TWO_PI 6.28318531f
#define DLMAX 2048u
#define SES_MAGIC 0x3159454Bu            /* "KEY1" */

enum { ENG_KEYS, ENG_PHYS };
enum { RES_STRING, RES_TUBE, RES_MODAL };
enum { EXC_PLUCK, EXC_STRIKE, EXC_BOW, EXC_BLOW, EXC_NOISE };
static const char *const EXC_NAME[5] = {"PLUCK", "STRIKE", "BOW", "BLOW", "NOISE"};
static const char *const RES_NAME[3] = {"STRING", "TUBE", "MODAL"};

/* ============================================================================= the sound's settings === */
typedef struct {
    uint8_t eng, res, exc;
    uint8_t k_tone, k_bark, k_bell, k_decay, k_trem, k_rate, k_drive, k_rel;   /* KEYS */
    uint8_t mat, bright, decay, pos, press, atk, rel;                           /* PHYS */
    uint8_t ch_on, ch_key, ch_scale, ch_notes, ch_voice;                        /* CHORD */
    uint8_t rev, room, strum, tone;                                             /* FX */
} params_t;

typedef struct { uint32_t magic; params_t p; uint8_t patch; int8_t oct; uint8_t rsv[2]; } session_t;
static session_t ses;
static uint8_t ses_dirty;
#define P (ses.p)

/*                      eng res exc  tone bark bell dec trem rate drv rel  mat brt dec pos prs atk rel  ch key sc  n  v  rev room str tone */
static const params_t FACTORY[] = {
    /* SUITCASE    */ {0, 0, 0,  70, 30, 35, 55, 45, 35, 15, 35,  30, 50, 50, 25, 50, 10, 30,  0, 0, 0, 4, 2,  25, 50, 0, 60},
    /* STAGE BARK  */ {0, 0, 0,  35, 80, 45, 50, 0, 30, 45, 30,   30, 50, 50, 25, 50, 10, 30,  0, 0, 0, 4, 2,  15, 40, 0, 75},
    /* BELL TINES  */ {0, 0, 0,  55, 25, 85, 70, 25, 50, 10, 45,  30, 50, 50, 25, 50, 10, 30,  1, 0, 0, 5, 2,  40, 60, 20, 80},
    /* NEO SOUL    */ {0, 0, 0,  62, 50, 40, 60, 30, 45, 25, 35,  30, 50, 50, 25, 50, 10, 30,  1, 2, 2, 4, 3,  30, 55, 12, 65},
    /* NYLON       */ {1, 0, 0,  50, 40, 40, 50, 0, 40, 0, 30,    15, 45, 45, 25, 0, 0, 30,    0, 0, 0, 4, 2,  20, 45, 0, 70},
    /* KOTO        */ {1, 0, 0,  50, 40, 40, 50, 0, 40, 0, 30,    45, 80, 55, 12, 0, 0, 25,    0, 9, 6, 3, 1,  25, 50, 0, 85},
    /* HARP        */ {1, 0, 0,  50, 40, 40, 50, 0, 40, 0, 30,    25, 55, 80, 40, 0, 0, 60,    1, 0, 0, 4, 1,  45, 70, 45, 75},
    /* BOWED       */ {1, 0, 2,  50, 40, 40, 50, 0, 40, 0, 30,    30, 55, 70, 15, 60, 35, 45,  0, 0, 1, 3, 2,  40, 65, 0, 70},
    /* REED        */ {1, 1, 3,  50, 40, 40, 50, 0, 40, 0, 30,    50, 55, 60, 30, 65, 20, 25,  0, 0, 0, 4, 2,  25, 50, 0, 75},
    /* AIR TUBE    */ {1, 1, 4,  50, 40, 40, 50, 0, 40, 0, 30,    30, 70, 70, 30, 50, 45, 40,  0, 0, 0, 4, 2,  45, 65, 0, 80},
    /* MARIMBA     */ {1, 2, 1,  50, 40, 40, 50, 0, 40, 0, 30,    6, 50, 40, 30, 0, 0, 30,     0, 0, 0, 4, 2,  20, 45, 0, 80},
    /* GLASS BOW   */ {1, 2, 2,  50, 40, 40, 50, 0, 40, 0, 30,    33, 60, 75, 35, 55, 60, 60,  1, 0, 4, 3, 2,  55, 75, 0, 85},
    /* TEMPLE BELL */ {1, 2, 1,  50, 40, 40, 50, 0, 40, 0, 30,    67, 65, 85, 35, 0, 0, 50,    0, 0, 0, 4, 2,  50, 80, 0, 85},
    /* HAND DRUM   */ {1, 2, 1,  50, 40, 40, 50, 0, 40, 0, 30,    95, 40, 30, 20, 0, 0, 20,    0, 0, 0, 4, 2,  15, 35, 0, 75},
    /* BREATH PAD  */ {1, 2, 4,  50, 40, 40, 50, 0, 40, 0, 30,    40, 45, 80, 30, 50, 80, 75,  1, 0, 1, 4, 2,  70, 85, 30, 70},
};
static const char *const PATCH_NAME[] = {"SUITCASE", "STAGE BARK", "BELL TINES", "NEO SOUL", "NYLON", "KOTO", "HARP",
                                         "BOWED", "REED", "AIR TUBE", "MARIMBA", "GLASS BOW", "TEMPLE BELL", "HAND DRUM",
                                         "BREATH PAD"};
#define NPATCH ((int)(sizeof FACTORY / sizeof FACTORY[0]))

static int params_valid(const params_t *q)
{
    const uint8_t *b = (const uint8_t *)q;
    uint32_t i;
    if (q->eng > 1 || q->res > 2 || q->exc > 4 || q->ch_on > 1 || q->ch_key > 11 || q->ch_scale > 6 ||
        q->ch_notes < 3 || q->ch_notes > 6 || q->ch_voice > 3)
        return 0;
    for (i = 3; i < sizeof *q; i++)
        if (b[i] > 100) return 0;
    return 1;
}

/* ======================================================================== shared with the render === */
/* events from the main loop: bits 0..6 note, 7 off, 8..14 velocity, 16..31 delay in samples (strum) */
#define EVQ 64u
static volatile uint32_t evq[EVQ], ev_w, ev_r;
static void ev_push(uint32_t e) { uint32_t w = ev_w; if (w - ev_r < EVQ) { evq[w % EVQ] = e; ev_w = w + 1u; } }
static void note_on_ev(uint32_t n, uint32_t vel, uint32_t delay) { ev_push((n & 127u) | (vel & 127u) << 8 | (delay & 0xFFFFu) << 16); }
static void note_off_ev(uint32_t n, uint32_t delay) { ev_push((n & 127u) | 128u | (delay & 0xFFFFu) << 16); }

typedef struct {
    uint8_t on, note, gate, kill, eng, res, exc;
    uint32_t age, t;
    float vel, f0;
    /* modes: z = z * c + x (complex), displacement = Im z */
    float zr[NMODE], zi[NMODE], cr[NMODE], ci[NMODE], w[NMODE], wt[NMODE], t60[NMODE];
    uint8_t nm;
    /* KEYS */
    float A, O, pnorm, vnorm, xprev, tc, pamp, thump;
    /* waveguide */
    float dlen, lp, lpc, apc, apx, apy, loopg, nlen, bph, bc, bt60;
    uint8_t pm;
    uint32_t dw;
    /* exciters */
    float env, ex_lp, ex_len, cnorm;
    uint32_t rng;
    float lvl, hp_x, hp_y, g;
    uint8_t pend_note, pend_vel;
} voice_t;
static voice_t vc[NV];
static PLAT_POOL float wg[NV][DLMAX];   /* the waveguides' delay lines */
static PLAT_POOL float wgn[NV][DLMAX / 2u];   /* a bowed string's second: bow to nut */
static uint32_t age_ctr;
static volatile float out_peak;
static volatile uint32_t voices_on;

/* pending (strummed) events */
typedef struct { uint32_t e; int32_t left; } pend_t;
static pend_t pend[48];
static uint32_t npend;

/* ============================================================================ materials (modal) === */
static const float MAT_R[4][NMODE] = {
    {1.0f, 2.756f, 3.99f, 5.404f, 8.933f, 10.65f, 13.34f, 15.6f, 18.64f, 21.0f, 24.8f, 28.1f},          /* wood bar */
    {1.0f, 2.32f, 4.25f, 6.63f, 9.38f, 12.5f, 16.0f, 19.9f, 24.1f, 28.7f, 33.6f, 38.9f},               /* glass */
    {0.5f, 1.0f, 1.183f, 1.506f, 2.0f, 2.514f, 2.662f, 3.011f, 4.166f, 5.433f, 6.796f, 8.215f},        /* bell metal */
    {1.0f, 1.594f, 2.136f, 2.296f, 2.653f, 2.918f, 3.156f, 3.501f, 3.6f, 3.652f, 4.06f, 4.154f},        /* membrane */
};
static const float MAT_D[4][NMODE] = {
    {1.0f, 0.45f, 0.35f, 0.25f, 0.15f, 0.1f, 0.08f, 0.06f, 0.05f, 0.04f, 0.03f, 0.03f},
    {1.0f, 0.9f, 0.8f, 0.7f, 0.6f, 0.5f, 0.42f, 0.35f, 0.3f, 0.25f, 0.2f, 0.18f},
    {1.3f, 1.0f, 0.9f, 0.8f, 0.75f, 0.6f, 0.55f, 0.5f, 0.4f, 0.3f, 0.25f, 0.2f},
    {1.0f, 0.6f, 0.5f, 0.45f, 0.4f, 0.35f, 0.3f, 0.28f, 0.25f, 0.22f, 0.2f, 0.18f},
};
static const float MAT_T[4] = {0.45f, 1.6f, 2.2f, 0.35f};   /* each material's ring time, x DECAY */
static const uint8_t MAT_PITCH[4] = {0, 0, 1, 0};           /* the mode heard as the note (a bell's prime, above its hum) */

static void material(float m, float *ratio, float *damp, float *tscale)   /* m 0..1: wood, glass, metal, skin */
{
    float x = m * 3.0f;
    int a = (int)x;
    float f;
    uint32_t i;
    if (a > 2) a = 2;
    f = x - (float)a;
    {
        uint32_t pi_ = f < 0.5f ? MAT_PITCH[a] : MAT_PITCH[a + 1];
        float anchor = fm_expf(fm_logf(MAT_R[a][pi_]) * (1.0f - f) + fm_logf(MAT_R[a + 1][pi_]) * f);
        for (i = 0; i < NMODE; i++) {   /* the morph keeps the note in tune: the heard mode stays at 1 */
            ratio[i] = fm_expf(fm_logf(MAT_R[a][i]) * (1.0f - f) + fm_logf(MAT_R[a + 1][i]) * f) / anchor;
            damp[i] = MAT_D[a][i] * (1.0f - f) + MAT_D[a + 1][i] * f;
        }
    }
    *tscale = MAT_T[a] * (1.0f - f) + MAT_T[a + 1] * f;
}

/* ================================================================================== voices === */
static float mtof(float n) { return 440.0f * fm_exp2f((n - 69.0f) / 12.0f); }
static float rnd(uint32_t *r) { *r ^= *r << 13; *r ^= *r >> 17; *r ^= *r << 5; return (float)(int32_t)*r * (1.0f / 2147483648.0f); }
static float t60_r(float t60) { return fm_expf(-6.9078f / (fm_maxf(t60, 0.002f) * FS)); }

static void mode_set(voice_t *v, uint32_t m, float hz, float t60, float wt)
{
    float w = TWO_PI * hz / FS, r;
    if (hz <= 0.0f || w > 0.9f * PI_F) wt = 0.0f, w = 0.5f;   /* above ~20 kHz: silent */
    r = t60_r(t60);
    v->w[m] = w; v->t60[m] = t60; v->wt[m] = wt;
    v->cr[m] = r * fm_cosf(w);
    v->ci[m] = r * fm_sinf(w);
}

static void mode_damp(voice_t *v, uint32_t m, float t60)  /* a new ring time, same pitch (the damper) */
{
    float r = t60_r(fm_minf(t60, v->t60[m]));
    v->cr[m] = r * fm_cosf(v->w[m]);
    v->ci[m] = r * fm_sinf(v->w[m]);
}

static float at2(float y, float x)       /* atan2, ~1e-5 rad: note-on only */
{
    float ax = fm_fabsf(x), ay = fm_fabsf(y), a, s, r;
    if (ax < 1e-20f && ay < 1e-20f) return 0.0f;
    a = ax > ay ? ay / ax : ax / ay;
    s = a * a;
    r = ((((-0.0040540580f * s + 0.0218612288f) * s - 0.0559098861f) * s + 0.0964200441f) * s - 0.1390853351f) * s;
    r = (((r + 0.1994653599f) * s - 0.3332985605f) * s + 1.0f) * a;
    if (ay > ax) r = 1.57079637f - r;
    if (x < 0.0f) r = PI_F - r;
    return y < 0.0f ? -r : r;
}

/* the phase delay of the string's loop filters at f, in samples: subtracted from the delay so the note is in tune */
static float loop_delay(float c, float a, float f)
{
    float w = TWO_PI * f / FS, cw = fm_cosf(w), sw = fm_sinf(w);
    /* one-pole low-pass H = c / (1 - (1-c) z^-1) */
    float re = 1.0f - (1.0f - c) * cw, im = (1.0f - c) * sw;
    float ph_lp = -at2(im, re);
    /* first-order allpass H = (a + z^-1) / (1 + a z^-1) */
    float ph_ap = at2(-sw, a + cw) - at2(-a * sw, 1.0f + a * cw);
    return -(ph_lp + ph_ap) / w;
}

static void voice_start(voice_t *v, uint32_t note, float vel, int retrig)
{
    float f0 = mtof((float)note), n = (float)note;
    uint32_t m;
    v->note = (uint8_t)note; v->vel = vel; v->f0 = f0; v->gate = 1; v->t = 0;
    v->eng = P.eng; v->res = P.res; v->exc = P.exc;
    v->age = ++age_ctr;
    if (!retrig) {
        for (m = 0; m < NMODE; m++) v->zr[m] = v->zi[m] = 0.0f;
        v->xprev = 0.0f; v->lp = v->apx = v->apy = 0.0f; v->dw = 0; v->hp_x = v->hp_y = 0.0f;
        v->env = 0.0f; v->ex_lp = 0.0f; v->lvl = 0.0f;
        v->rng = 0x9E3779B9u ^ (note * 2654435761u) ^ age_ctr;
        for (m = 0; m < DLMAX; m++) wg[v - vc][m] = 0.0f;
        for (m = 0; m < DLMAX / 2u; m++) wgn[v - vc][m] = 0.0f;
    }
    if (v->eng == ENG_KEYS) {
        /* ring times by register: low tines sing for seconds, high ones are short */
        float dk = 0.35f + (float)P.k_decay / 100.0f * 2.2f;
        float tf = fm_clampf(9.0f * fm_exp2f(-(n - 33.0f) / 20.0f), 0.7f, 14.0f) * dk;
        float bell = (float)P.k_bell / 100.0f;
        float r2 = 6.27f - (n - 60.0f) * 0.012f;                 /* the overtone ratio drifts with tine length */
        float det = 1.0f + 0.0011f + 0.0006f * fm_sinf(n * 1.7f);   /* the tonebar a hair off: a slow beat */
        v->nm = 4;
        mode_set(v, 0, f0, tf, 1.0f);
        mode_set(v, 1, f0 * det, tf * 1.3f, 0.55f);
        mode_set(v, 2, f0 * r2, 0.12f * tf + 0.15f, 0.25f + bell * 1.1f);
        mode_set(v, 3, f0 * 17.55f, 0.05f * tf + 0.04f, (0.15f + bell * 0.9f) * 0.6f);
        /* the hammer: harder and with more BELL a shorter contact, so more of the upper modes */
        v->tc = (0.6f + (1.0f - bell) * 2.6f) * (1.0f - 0.45f * vel) * 0.001f * FS;
        v->tc = fm_clampf(v->tc, 4.0f, 0.45f * FS / f0);   /* never longer than half a period: that cancels the note */
        v->pamp = PI_F / (2.0f * v->tc);
        /* the pickup: the swing against the gap grows with velocity and BARK; TONE sets where the tine sits */
        /* (the tine's swing, in units of the gap, grows with velocity; it stays on the near side of the pole, so a
         * hard note bends the wave lopsided, the bark, but never folds it) */
        v->O = 0.3f + (float)P.k_tone / 100.0f * 0.5f;
        v->A = v->O * (0.15f + (float)P.k_bark / 100.0f * 0.55f) * vel;
        v->pnorm = (v->O * v->O + 1.0f) * fm_sqrtf(v->O * v->O + 1.0f) / v->O;
        v->vnorm = FS / (TWO_PI * f0);
        v->thump = vel * 0.05f;
    } else {
        float bright = (float)P.bright / 100.0f, dec = (float)P.decay / 100.0f, mat = (float)P.mat / 100.0f;
        if (v->res == RES_MODAL) {
            float ratio[NMODE], damp[NMODE], ts, pos = 0.08f + (float)P.pos / 100.0f * 0.42f, sum = 0.0f;
            float t = (0.08f + dec * dec * 6.0f), hz;
            material(mat, ratio, damp, &ts);
            t *= ts * fm_clampf(fm_exp2f(-(n - 60.0f) / 30.0f), 0.4f, 2.0f);
            v->nm = NMODE;
            for (m = 0; m < NMODE; m++) {
                float wt = fm_fabsf(fm_sinf(PI_F * (float)(m + 1u) * pos)) / fm_sqrtf((float)(m + 1u)) * (0.4f + 0.6f * bright * (m > 2u ? 1.0f : 1.6f));
                hz = f0 * ratio[m];
                mode_set(v, m, hz, t * damp[m] * fm_clampf(fm_powf(200.0f / hz, 0.25f), 0.3f, 1.5f), wt);
                sum += v->wt[m];
            }
            for (m = 0; m < NMODE; m++) v->wt[m] /= (sum > 0.0f ? sum : 1.0f) * 0.55f;
            v->pm = 0;                   /* the mode heard as the note, for the bow */
            for (m = 1; m < NMODE; m++) if (fm_fabsf(ratio[m] - 1.0f) < fm_fabsf(ratio[v->pm] - 1.0f)) v->pm = (uint8_t)m;
            v->bt60 = v->t60[v->pm];
            if (v->exc == EXC_BOW) {     /* the bow's hair damps the mode it drives: it speaks in a fraction of a second */
                float tb = fm_minf(v->bt60, 0.35f);
                mode_set(v, v->pm, f0 * ratio[v->pm], tb, v->wt[v->pm]);
            }
            v->bc = 1.0f - t60_r(v->t60[v->pm]);
            v->bph = 0.0f;
            v->cnorm = fm_sqrtf(1.0f - t60_r(t) * t60_r(t));
        } else {
            /* waveguide: the loop's damping filter by BRIGHT, stiffness by MATERIAL (string) */
            float t60 = 0.15f + dec * dec * 12.0f, len;
            v->lpc = 0.12f + bright * 0.85f;
            v->apc = v->res == RES_STRING ? -mat * 0.62f : 0.0f;
            if (v->res == RES_STRING && v->exc == EXC_BOW) {   /* two lines either side of the bow (as STK's Bowed) */
                float beta = 0.06f + (float)P.pos / 100.0f * 0.4f;
                v->lpc = 0.2f + bright * 0.6f;
                len = FS / f0 - (loop_delay(v->lpc, 0.0f, f0) - 1.0f);
                v->nlen = fm_clampf(len * beta, 2.0f, (float)(DLMAX / 2u) - 4.0f);
                len -= v->nlen;
                v->loopg = 0.94f + dec * 0.055f;
            } else if (v->res == RES_STRING) {
                len = FS / f0 - loop_delay(v->lpc, v->apc, f0);
                v->loopg = fm_expf(-6.9078f / (t60 * f0));
            } else {                     /* the bore: an inverting open end, so half the period */
                len = FS / (2.0f * f0) - (loop_delay(v->lpc, 0.0f, f0) - 1.0f);   /* (a = 0: no allpass, its sample out) */
                v->loopg = fm_minf(0.995f, fm_expf(-6.9078f / (t60 * f0 * 2.0f)) + 0.02f);
            }
            v->dlen = fm_clampf(len, 4.0f, (float)DLMAX - 4.0f);
            v->nm = 0;
        }
        /* exciters */
        v->tc = (0.25f + (1.0f - bright) * 3.5f) * 0.001f * FS;
        v->tc = fm_clampf(v->tc, 3.0f, 0.5f * FS / f0);
        v->pamp = PI_F / (2.0f * v->tc);
        v->ex_len = v->res == RES_MODAL ? 0.004f * FS : v->dlen;
    }
    /* each path's level, measured (expect bus): the loudest single note peaks near 0.3 in the mix, so a six-note
     * chord still has headroom before the amp and the output's soft limit */
    if (v->eng == ENG_KEYS) v->g = 0.85f;
    else if (v->res == RES_MODAL) v->g = v->exc <= EXC_STRIKE ? 1.0f : v->exc == EXC_BOW ? 1.25f : 1.3f;
    else if (v->res == RES_TUBE) v->g = v->exc == EXC_BLOW ? 0.46f : 0.24f;
    else v->g = v->exc == EXC_BOW ? 0.12f : v->exc <= EXC_STRIKE ? 0.75f : 0.5f;
    v->on = 1;
    v->kill = 0;
}

static voice_t *voice_for(uint32_t note)
{
    uint32_t i;
    voice_t *best = 0;
    for (i = 0; i < NV; i++) if (vc[i].on && vc[i].note == note && !vc[i].kill) return &vc[i];   /* the same note: struck again */
    for (i = 0; i < NV; i++) if (!vc[i].on) return &vc[i];
    for (i = 0; i < NV; i++)             /* the quietest released voice, else the oldest */
        if (!vc[i].gate && !vc[i].kill && (!best || vc[i].lvl < best->lvl)) best = &vc[i];
    if (!best)
        for (i = 0; i < NV; i++) if (!vc[i].kill && (!best || vc[i].age < best->age)) best = &vc[i];
    return best;
}

static void do_note_on(uint32_t note, uint32_t vel7)
{
    voice_t *v = voice_for(note);
    float vel = (float)(vel7 ? vel7 : 1u) / 127.0f;
    if (!v) return;
    if (v->on && v->note == note) { voice_start(v, note, vel, 1); return; }
    if (v->on) { v->kill = 1; v->pend_note = (uint8_t)note; v->pend_vel = (uint8_t)vel7; return; }   /* fades first */
    voice_start(v, note, vel, 0);
}

static void do_note_off(uint32_t note)
{
    uint32_t i, m;
    for (i = 0; i < NV; i++) {
        voice_t *v = &vc[i];
        if (!v->on || v->note != note || !v->gate) continue;
        v->gate = 0;
        if (v->eng == ENG_KEYS) {        /* the damper: the fundamental and tonebar first, the overtones faster */
            float rt = 0.06f + (float)P.k_rel / 100.0f * 1.4f;
            for (m = 0; m < v->nm; m++) mode_damp(v, m, m < 2u ? rt : rt * 0.4f);
        } else if (v->res == RES_MODAL && v->exc == EXC_BOW) {   /* the bow lifts: the body rings on */
            mode_set(v, v->pm, v->w[v->pm] * FS / TWO_PI, v->bt60, v->wt[v->pm]);
        } else if (v->res == RES_MODAL && v->exc <= EXC_STRIKE) {
            float rt = 0.08f + (float)P.rel / 100.0f * 2.5f;
            for (m = 0; m < v->nm; m++) mode_damp(v, m, rt);
        } else if (v->res != RES_MODAL && v->exc <= EXC_STRIKE) {
            float rt = 0.08f + (float)P.rel / 100.0f * 2.5f;
            v->loopg = fm_minf(v->loopg, fm_expf(-6.9078f / (rt * v->f0)));
        }
    }
}

/* ---- one voice, one sample ---- */
static float voice_tick(voice_t *v)
{
    float out = 0.0f, e = 0.0f, nz;
    uint32_t m, t = v->t++;
    if (v->eng == ENG_KEYS) {
        float p = t < (uint32_t)v->tc ? fm_sinf(PI_F * (float)t / v->tc) * v->pamp : 0.0f, X = 0.0f, V, q, r2;
        for (m = 0; m < 4u; m++) {
            float zr = v->zr[m], zi = v->zi[m];
            v->zr[m] = zr * v->cr[m] - zi * v->ci[m] + p * v->wt[m];
            v->zi[m] = zr * v->ci[m] + zi * v->cr[m];
        }
        X = v->zi[0] + v->zi[1] * 0.35f + v->zi[2] * 0.06f + v->zi[3] * 0.012f;
        V = (X - v->xprev) * v->vnorm;
        v->xprev = X;
        /* the pickup: d(flux)/dt = flux'(x) * dx/dt, flux' ~ (x + o) / ((x + o)^2 + 1)^1.5, the gap as the unit */
        q = X * v->A + v->O;
        r2 = q * q + 1.0f;
        out = V * v->vel * q / (r2 * fm_sqrtf(r2)) * v->pnorm;   /* louder with velocity, clean when soft */
        out *= 0.55f;
        if (t < 900u) {                  /* the hammer's knock */
            nz = rnd(&v->rng);
            v->ex_lp += (nz - v->ex_lp) * 0.08f;
            out += v->ex_lp * v->thump * (1.0f - (float)t / 900.0f);
        }
        return out;
    }
    /* ---- PHYS: the exciter ---- */
    nz = rnd(&v->rng);
    {
        float bright = (float)P.bright / 100.0f, press = (float)P.press / 100.0f;
        float a_rate = 1.0f / ((0.003f + (float)P.atk * (float)P.atk / 10000.0f * 3.0f) * FS);
        float r_rate = 1.0f / ((0.01f + (float)P.rel * (float)P.rel / 10000.0f * 4.0f) * FS);
        switch (v->exc) {
        case EXC_PLUCK:                  /* a burst shaped by where it is plucked, noise mixed in for the nail */
            if ((float)t < v->ex_len) {
                float x = (float)t / v->ex_len, pos = 0.08f + (float)P.pos / 100.0f * 0.42f;
                float tri = x < pos ? x / pos : (1.0f - x) / (1.0f - pos);
                v->ex_lp += ((tri * 0.8f + nz * (0.15f + bright * 0.5f)) - v->ex_lp) * (0.1f + bright * 0.85f);
                e = v->ex_lp * v->vel * (v->res == RES_MODAL ? 0.6f : 0.9f);
            }
            break;
        case EXC_STRIKE:                 /* a mallet: a half-sine of force, shorter when BRIGHT, struck twice for position */
            if ((float)t < v->tc) e = fm_sinf(PI_F * (float)t / v->tc) * v->vel;
            if (v->res != RES_MODAL) {
                float d = (float)t - v->dlen * (0.08f + (float)P.pos / 100.0f * 0.42f);
                if (d >= 0.0f && d < v->tc) e -= fm_sinf(PI_F * d / v->tc) * v->vel;
                e *= 0.5f;
            } else e *= v->pamp * 2.5f;     /* the force sums to VEL x 2.5: the blow's momentum */
            break;
        default:                         /* bow, blow, noise: held, with an envelope */
            if (v->gate) v->env += (1.0f - v->env) * a_rate * 4.0f;
            else v->env -= v->env * r_rate * 4.0f;
            if (v->exc == EXC_NOISE || (v->exc == EXC_BLOW && v->res != RES_TUBE)) {
                v->ex_lp += (nz - v->ex_lp) * (0.05f + bright * 0.9f);
                e = v->ex_lp * v->env * (0.2f + press * 0.8f) * v->vel * (v->exc == EXC_BOW ? 1.6f : 1.0f);
            }
            break;
        }
    }
    /* ---- the resonator ---- */
    if (v->res == RES_MODAL) {
        float in = (v->exc >= EXC_BLOW) ? e * v->cnorm * 6.0f : e;
        if (v->exc == EXC_BOW) {         /* a bow on a bar or glass: the stick-slip's pulse at the note (a model of its
                                          * result: bowed modes lock to the note's mode), its hair's noise on top */
            float press = (float)P.press / 100.0f;
            v->bph += v->w[v->pm];
            if (v->bph > TWO_PI) v->bph -= TWO_PI;
            v->ex_lp += (nz - v->ex_lp) * 0.3f;
            in = (fm_sinf(v->bph) + 0.35f * fm_sinf(2.0f * v->bph) * press + v->ex_lp * (0.15f + 0.3f * press)) *
                 v->env * v->vel * v->bc * 1.6f;
        }
        for (m = 0; m < v->nm; m++) {
            float zr = v->zr[m], zi = v->zi[m];
            v->zr[m] = zr * v->cr[m] - zi * v->ci[m] + in * v->wt[m];
            v->zi[m] = zr * v->ci[m] + zi * v->cr[m];
            out += v->zi[m] * v->wt[m];
        }
        {                                /* (a bow or breath pushes the body: no DC out) */
            float hy = out - v->hp_x + 0.995f * v->hp_y;
            v->hp_x = out; v->hp_y = hy; out = hy;
        }
        out *= 0.9f;
    } else {
        float *dl = wg[v - vc], rp = (float)v->dw - v->dlen, y, f, s, ap;
        uint32_t i0, i1;
        if (v->res == RES_STRING && v->exc == EXC_BOW) {
            float *dn = wgn[v - vc], rn, br, nr, sv, dv, tb, nv, press = (float)P.press / 100.0f;
            float vb = v->env * (0.03f + 0.2f * v->vel) * (0.7f + press * 0.6f);
            if (rp < 0.0f) rp += (float)DLMAX;
            i0 = (uint32_t)rp; f = rp - (float)i0; i1 = (i0 + 1u) & (DLMAX - 1u);
            y = dl[i0] + (dl[i1] - dl[i0]) * f;                  /* at the bridge */
            rp = (float)v->dw - v->nlen;
            if (rp < 0.0f) rp += (float)(DLMAX / 2u);
            i0 = (uint32_t)rp & (DLMAX / 2u - 1u); f = rp - (float)(uint32_t)rp; i1 = (i0 + 1u) & (DLMAX / 2u - 1u);
            rn = dn[i0] + (dn[i1] - dn[i0]) * f;                 /* at the nut */
            v->lp += v->lpc * (y - v->lp);
            br = -v->lp * v->loopg;
            nr = -rn;
            sv = br + nr;
            dv = vb - sv;
            tb = fm_fabsf(dv * (5.0f - 4.0f * press)) + 0.75f;
            tb = 1.0f / (tb * tb * tb * tb);
            if (tb > 1.0f) tb = 1.0f;
            nv = dv * tb;
            dn[v->dw & (DLMAX / 2u - 1u)] = fm_flush(fm_clampf(br + nv, -4.0f, 4.0f));
            dl[v->dw] = fm_flush(fm_clampf(nr + nv, -4.0f, 4.0f));
            v->dw = (v->dw + 1u) & (DLMAX - 1u);
            {
                float hy = y - v->hp_x + 0.995f * v->hp_y;
                v->hp_x = y; v->hp_y = hy;
                return hy * 6.0f;
            }
        }
        if (rp < 0.0f) rp += (float)DLMAX;
        i0 = (uint32_t)rp; f = rp - (float)i0; i1 = (i0 + 1u) & (DLMAX - 1u);
        y = dl[i0] + (dl[i1] - dl[i0]) * f;
        v->lp += v->lpc * (y - v->lp);
        if (v->res == RES_STRING) {
            ap = v->apc * (v->lp - v->apy) + v->apx;     /* stiffness: dispersion */
            v->apx = v->lp; v->apy = ap;
            s = ap * v->loopg;
            s += e;
            out = y + e * 0.25f;
        } else {
            float refl = -v->lp * v->loopg;
            if (v->exc == EXC_BLOW) {    /* a reed: breath against the bore's return */
                float press = (float)P.press / 100.0f, mat = (float)P.mat / 100.0f;
                float pb = v->env * (0.45f + press * 0.55f) * (0.6f + v->vel * 0.4f) + nz * 0.025f * v->env;
                float pd = refl - pb, reed = 0.7f - (0.2f + mat * 0.25f) * pd;
                reed = fm_clampf(reed, -1.0f, 1.0f);
                s = pb + pd * reed;
            } else s = refl + e;
            out = y;
        }
        dl[v->dw] = fm_flush(fm_clampf(s, -4.0f, 4.0f));
        v->dw = (v->dw + 1u) & (DLMAX - 1u);
        /* remove DC (the reed's breath) */
        {
            float hy = out - v->hp_x + 0.995f * v->hp_y;
            v->hp_x = out; v->hp_y = hy; out = hy;
        }
        out *= v->res == RES_TUBE ? 0.9f : 1.1f;
    }
    return out;
}

/* ===================================================================================== effects === */
#define RVN 4
static const uint16_t RV_LEN[RVN] = {1557, 1617, 1491, 1422};
static PLAT_POOL float rv_buf[RVN][2600];
static uint32_t rv_w[RVN];
static float rv_lp[RVN], trem_ph, tone_l, tone_r, drv_hp;

static void effects(float *l, float *r)
{
    float x = *l, y = *r, send, a[RVN], s, mix = (float)P.rev / 100.0f;
    float room = 0.6f + (float)P.room / 100.0f * 1.0f, fb = 0.55f + (float)P.room / 100.0f * 0.33f;
    uint32_t i;
    if (P.eng == ENG_KEYS) {
        if (P.k_drive) {                 /* the amp */
            float g = 1.0f + (float)P.k_drive / 100.0f * 6.0f, mk = 1.0f / g + 0.5f * (1.0f - 1.0f / g);
            x = fm_tanhf(x * g) * mk;    /* unity when soft: it only bites on hard notes and big chords */
            y = fm_tanhf(y * g) * mk;
        }
        if (P.k_trem) {                  /* stereo tremolo: the sound swings side to side */
            float d = (float)P.k_trem / 100.0f, sn;
            trem_ph += (1.0f + (float)P.k_rate / 100.0f * 8.0f) / FS;
            if (trem_ph >= 1.0f) trem_ph -= 1.0f;
            sn = fm_sinf(TWO_PI * trem_ph);
            x *= 1.0f - d * (0.5f + 0.5f * sn);
            y *= 1.0f - d * (0.5f - 0.5f * sn);
        }
    }
    {                                    /* tone: a gentle low-pass, open at 100 */
        float c = 0.06f + (float)P.tone / 100.0f * 0.94f;
        tone_l += (x - tone_l) * c; tone_r += (y - tone_r) * c;
        x = tone_l; y = tone_r;
    }
    if (mix > 0.0f) {                    /* four delay lines, a Householder matrix, damped */
        send = (x + y) * 0.5f;
        for (i = 0; i < RVN; i++) {
            uint32_t len = (uint32_t)((float)RV_LEN[i] * room), rp = (rv_w[i] + 2600u - len) % 2600u;
            a[i] = rv_buf[i][rp];
        }
        s = (a[0] + a[1] + a[2] + a[3]) * 0.5f;
        for (i = 0; i < RVN; i++) {
            float v = (a[i] - s) * fb + send * 0.35f;
            rv_lp[i] += (v - rv_lp[i]) * 0.45f;
            rv_buf[i][rv_w[i]] = fm_flush(rv_lp[i]);
            rv_w[i] = (rv_w[i] + 1u) % 2600u;
        }
        x = x * (1.0f - mix * 0.5f) + (a[0] + a[2]) * mix * 0.7f;
        y = y * (1.0f - mix * 0.5f) + (a[1] + a[3]) * mix * 0.7f;
    }
    (void)drv_hp;
    *l = x; *r = y;
}

/* ======================================================================================= render === */
static volatile uint8_t lvl_hist[232];
static volatile uint32_t lvl_w;

#ifdef PLAT_HOST
static float dbg_bus;
#endif
void app_render(int32_t *lr, uint32_t n, uint32_t master_q12)
{
    float gain = (float)master_q12 / 4096.0f * 8300000.0f, pk = 0.0f;
    uint32_t i, k, on = 0;
    while (ev_r != ev_w) {               /* new events, some to wait (strum) */
        uint32_t e = evq[ev_r % EVQ];
        ev_r++;
        if ((e >> 16) == 0u) {
            if (e & 128u) do_note_off(e & 127u); else do_note_on(e & 127u, (e >> 8) & 127u);
        } else if (npend < 48u) { pend[npend].e = e & 0xFFFFu; pend[npend].left = (int32_t)(e >> 16); npend++; }
    }
    for (k = 0; k < npend;) {
        pend[k].left -= (int32_t)n;
        if (pend[k].left <= 0) {
            uint32_t e = pend[k].e;
            if (e & 128u) do_note_off(e & 127u); else do_note_on(e & 127u, (e >> 8) & 127u);
            pend[k] = pend[--npend];
        } else k++;
    }
    for (i = 0; i < n; i++) {
        float l = 0.0f, r = 0.0f;
        for (k = 0; k < NV; k++) {
            voice_t *v = &vc[k];
            float s;
            if (!v->on) continue;
            s = voice_tick(v) * v->g;
            if (v->kill) {               /* stolen: a 2 ms fade, then the waiting note */
                s *= 1.0f - (float)(v->kill) / 90.0f;
                if (++v->kill >= 90u) { voice_start(v, v->pend_note, (float)(v->pend_vel ? v->pend_vel : 1u) / 127.0f, 0); }
            }
            /* a little stereo: lower notes left of centre, higher right */
            {
                float pan = fm_clampf(((float)v->note - 60.0f) / 48.0f, -0.6f, 0.6f);
                l += s * (1.0f - pan * 0.5f);
                r += s * (1.0f + pan * 0.5f);
            }
            {
                float a = fm_fabsf(s);
                v->lvl += (a - v->lvl) * (a > v->lvl ? 0.01f : 0.0005f);
            }
        }
        l *= 0.5f; r *= 0.5f;
#ifdef PLAT_HOST
        if (fm_fabsf(l) > dbg_bus) dbg_bus = fm_fabsf(l);
#endif
        effects(&l, &r);
        if (fm_fabsf(l) > pk) pk = fm_fabsf(l);
        if (fm_fabsf(r) > pk) pk = fm_fabsf(r);
        lr[2 * i] = (int32_t)(fm_tanhf(l * 1.3f) * gain);
        lr[2 * i + 1] = (int32_t)(fm_tanhf(r * 1.3f) * gain);
    }
    for (k = 0; k < NV; k++) {           /* finished voices go quiet */
        voice_t *v = &vc[k];
        if (!v->on) continue;
        if (v->lvl < 0.00004f && v->t > 4410u && (!v->gate || (v->eng == ENG_KEYS || v->exc <= EXC_STRIKE))) {
            uint32_t m, still = 0;
            for (m = 0; m < v->nm; m++) if (fm_fabsf(v->zi[m]) + fm_fabsf(v->zr[m]) > 0.0002f) still = 1;
            if (!still) v->on = 0;
        }
        if (v->on) on |= 1u << k;
    }
    voices_on = on;
    out_peak = pk;
    lvl_hist[lvl_w] = (uint8_t)(fm_minf(pk, 1.0f) * 255.0f);
    lvl_w = (lvl_w + 1u) % 232u;
}

void app_panic(void)
{
    uint32_t i;
    for (i = 0; i < NV; i++) vc[i].on = 0;
    npend = 0;
}

/* ===================================================================================== chords === */
static void text_cat(char *d, const char *s) { while (*d) d++; while (*s) *d++ = *s++; *d = 0; }
static const uint8_t SCALE[7][7] = {
    {0, 2, 4, 5, 7, 9, 11}, {0, 2, 3, 5, 7, 8, 10}, {0, 2, 3, 5, 7, 9, 10}, {0, 1, 3, 5, 7, 8, 10},
    {0, 2, 4, 6, 7, 9, 11}, {0, 2, 4, 5, 7, 9, 10}, {0, 2, 3, 5, 7, 8, 11},
};
static const char *const SCALE_NAME[7] = {"MAJOR", "MINOR", "DORIAN", "PHRYGIAN", "LYDIAN", "MIXOLYDIAN", "HARM MINOR"};
static const char *const VOICE_NAME[4] = {"CLOSE", "OPEN", "LEAD", "LEAD+BASS"};
static const char *const NOTE_NAME[12] = {"C", "C#", "D", "Eb", "E", "F", "F#", "G", "Ab", "A", "Bb", "B"};
static const char *const ROMAN[7] = {"I", "II", "III", "IV", "V", "VI", "VII"};

static int8_t prev_ch[8];
static uint32_t prev_n;
static struct { char name[16]; char roman[10]; int8_t notes[8]; uint32_t n; int8_t root; } chord;

static void chord_name(int root_pc, const int *iv, uint32_t n, char *out)   /* intervals above the root, mod 12 */
{
    uint32_t i, has[12] = {0};
    for (i = 0; i < n; i++) has[((iv[i] % 12) + 12) % 12] = 1;
    out[0] = 0;
    text_cat(out, NOTE_NAME[root_pc]);
    if (has[3] && has[6] && !has[7]) {
        text_cat(out, has[10] ? "MIN7B5" : has[9] ? "DIM7" : "DIM");   /* (the screen's large font has capitals only) */
    } else if (has[4] && has[8] && !has[7]) text_cat(out, "AUG");
    else {
        int minor = has[3] && !has[4];
        if (minor) text_cat(out, "MIN");
        if (has[2] && n >= 5) text_cat(out, has[11] ? "MAJ9" : has[10] ? "9" : "ADD9");
        else if (has[11]) text_cat(out, "MAJ7");
        else if (has[10]) text_cat(out, "7");
        if (has[5] && n >= 6) text_cat(out, "11");
    }
}


/* the chord for a played note: the scale degree it falls on (snapped down), stacked in thirds, voiced */
static uint32_t chord_build(int note, int *out)
{
    const uint8_t *sc = SCALE[P.ch_scale];
    int rel = ((note - (int)P.ch_key) % 12 + 12) % 12, d = 0, i, k = P.ch_notes, tonic, best_cost = 1 << 30;
    int base[8] = {0}, cand[8] = {0}, iv[8] = {0};
    for (i = 0; i < 7; i++) if (sc[i] <= rel) d = i;
    tonic = note - rel;                  /* the key's tonic at or below the note */
    for (i = 0; i < k; i++) {
        int deg = d + 2 * i;
        base[i] = tonic + sc[deg % 7] + 12 * (deg / 7);
        iv[i] = base[i] - base[0];
    }
    chord.root = (int8_t)base[0];
    chord_name(base[0] % 12, iv, (uint32_t)k, chord.name);
    {                                    /* the numeral: upper case major, lower case minor or diminished */
        uint32_t j;
        int minor = ((base[1] - base[0]) % 12) == 3;
        chord.roman[0] = 0;
        text_cat(chord.roman, ROMAN[d]);
        if (minor) for (j = 0; chord.roman[j]; j++) chord.roman[j] = (char)(chord.roman[j] - 'A' + 'a');
    }
    if (P.ch_voice == 1) {               /* open: drop the second from the top an octave (or lift the third) */
        if (k >= 4) base[k - 2] -= 12; else base[1] += 12;
    }
    if (P.ch_voice >= 2 && prev_n) {     /* voice-led: the inversion and octave nearest the last chord */
        int inv, oct, j, pc = 0;
        for (j = 0; j < (int)prev_n && j < k; j++) pc += prev_ch[j];
        for (inv = 0; inv < k; inv++)
            for (oct = -2; oct <= 1; oct++) {
                int cost = 0, tmp[8], a, b;
                for (j = 0; j < k; j++) tmp[j] = base[(j + inv) % k] + 12 * ((j + inv) / k) + 12 * oct;
                for (a = 1; a < k; a++) for (b = a; b > 0 && tmp[b] < tmp[b - 1]; b--) { int t = tmp[b]; tmp[b] = tmp[b - 1]; tmp[b - 1] = t; }
                for (j = 0; j < k; j++) cost += abs(tmp[j] - prev_ch[j < (int)prev_n ? j : (int)prev_n - 1]);
                if (tmp[0] < 40 || tmp[k - 1] > 92) cost += 100;
                if (cost < best_cost) { best_cost = cost; for (j = 0; j < k; j++) cand[j] = tmp[j]; }
            }
        for (i = 0; i < k; i++) base[i] = cand[i];
        (void)pc;
    }
    for (i = 0; i < k; i++) out[i] = base[i];
    if (P.ch_voice >= 2) { prev_n = (uint32_t)k; for (i = 0; i < k; i++) prev_ch[i] = (int8_t)base[i]; }
    if (P.ch_voice == 3) {               /* a bass note under the voicing */
        int b = chord.root;
        while (b > base[0] - 12) b -= 12;
        while (b < 33) b += 12;
        out[k++] = b;
    }
    chord.n = (uint32_t)k;
    for (i = 0; i < k; i++) chord.notes[i] = (int8_t)out[i];
    return (uint32_t)k;
}

/* who holds which notes: a key or a MIDI note (its number) owns the notes it started */
static int8_t own[128][8];
static uint8_t own_n[128], refc[128], sus_held[128], sustain;

static void play_src(uint32_t src, uint32_t vel)
{
    int notes[8], i;
    uint32_t k;
    if (own_n[src]) return;
    if (P.ch_on) k = chord_build((int)src, notes);
    else { notes[0] = (int)src; k = 1; chord.n = 1; chord.notes[0] = (int8_t)src; }
    for (i = 0; i < (int)k; i++) {
        int nn_ = notes[i];
        uint32_t delay = P.ch_on ? (uint32_t)i * (uint32_t)P.strum * 60u : 0u;   /* up to 6000 samples between notes */
        if (nn_ < 0 || nn_ > 127) continue;
        own[src][own_n[src]++] = (int8_t)nn_;
        refc[nn_]++;
        sus_held[nn_] = 0;
        note_on_ev((uint32_t)nn_, vel - (uint32_t)i * 2u, delay);
    }
}

static void stop_src(uint32_t src)
{
    uint32_t i;
    for (i = 0; i < own_n[src]; i++) {
        int nn_ = own[src][i];
        if (refc[nn_]) refc[nn_]--;
        if (!refc[nn_]) { if (sustain) sus_held[nn_] = 1; else note_off_ev((uint32_t)nn_, 0); }
    }
    own_n[src] = 0;
}

static void sustain_set(uint8_t on)
{
    uint32_t n;
    sustain = on;
    if (!on) for (n = 0; n < 128u; n++) if (sus_held[n]) { sus_held[n] = 0; if (!refc[n]) note_off_ev(n, 0); }
}

/* ======================================================================================= the UI === */
enum { P_SOUND, P_TONE, P_CHORD, P_FX, NPAGE };
static const char *const PAGE_NAME[NPAGE] = {"SOUND", "TONE", "CHORD", "FX"};
static struct {
    uint8_t page, demo, redraw_all;
    uint32_t keys, btn, msg_until, touched_at[4], demo_t, demo_step;
    char msg[2][24];
    float tine_ph[NV];
} ui;

static void int_text(int32_t v, char *b)
{
    char t[12];
    int n = 0;
    uint32_t u = (uint32_t)(v < 0 ? -v : v);
    do t[n++] = (char)('0' + u % 10u); while ((u /= 10u) && n < 11);
    if (v < 0) *b++ = '-';
    while (n) *b++ = t[--n];
    *b = 0;
}
static void note_text(int n, char *b) { char t[6]; b[0] = 0; text_cat(b, NOTE_NAME[n % 12]); int_text(n / 12 - 1, t); text_cat(b, t); }

void app_say(const char *a, const char *b)
{
    uint32_t i;
    for (i = 0; i < 23u && a && a[i]; i++) ui.msg[0][i] = a[i];
    ui.msg[0][i] = 0;
    for (i = 0; i < 23u && b && b[i]; i++) ui.msg[1][i] = b[i];
    ui.msg[1][i] = 0;
    ui.msg_until = plat_ms() + 1300u;
    if (ann_on()) {
        char t[48] = "";
        text_cat(t, ui.msg[0]);
        if (ui.msg[1][0]) { text_cat(t, " "); text_cat(t, ui.msg[1]); }
        ann_say(t, 0, ANN_NOW);
    }
}

static uint16_t mix565(uint16_t a, uint16_t b, uint32_t t)   /* t 0..256 */
{
    uint32_t ar = a >> 11, ag = (a >> 5) & 63u, ab = a & 31u, br = b >> 11, bg = (b >> 5) & 63u, bb = b & 31u;
    return (uint16_t)(((ar + (br - ar) * t / 256u) << 11) | ((ag + (bg - ag) * t / 256u) << 5) | (ab + (bb - ab) * t / 256u));
}
static uint16_t dim565(uint16_t c, uint32_t num, uint32_t den)
{
    uint32_t r = (c >> 11) & 31u, g = (c >> 5) & 63u, b = c & 31u;
    if (num > den) num = den;
    return (uint16_t)(((r * num / den) << 11) | ((g * num / den) << 5) | (b * num / den));
}

static uint16_t accent(void)             /* the engine's colour; a modal body takes its material's */
{
    static const uint16_t MATC[4] = {RGB(214, 150, 92), RGB(150, 230, 255), RGB(255, 206, 96), RGB(255, 140, 160)};
    if (P.eng == ENG_KEYS) return RGB(255, 178, 92);
    if (P.res == RES_STRING) return RGB(255, 220, 130);
    if (P.res == RES_TUBE) return RGB(96, 224, 208);
    {
        float x = (float)P.mat / 100.0f * 3.0f;
        int a = (int)x;
        if (a > 2) a = 2;
        return mix565(MATC[a], MATC[a + 1], (uint32_t)((x - (float)a) * 256.0f));
    }
}

/* ---- the four controls of each page ---- */
typedef struct { const char *label; char val[12]; int32_t fill; } ctl_t;
static uint8_t *ctl_ptr(uint32_t k, int32_t *lo, int32_t *hi, int32_t *step)
{
    *lo = 0; *hi = 100; *step = 2;
    switch (ui.page) {
    case P_SOUND:
        if (P.eng == ENG_KEYS) { uint8_t *a[4] = {&P.k_tone, &P.k_bark, &P.k_bell, &P.k_decay}; return a[k]; }
        if (k == 0) { *hi = 4; *step = 1; return &P.exc; }
        { uint8_t *a[4] = {0, &P.mat, &P.bright, &P.decay}; return a[k]; }
    case P_TONE:
        if (P.eng == ENG_KEYS) { uint8_t *a[4] = {&P.k_trem, &P.k_rate, &P.k_drive, &P.k_rel}; return a[k]; }
        { uint8_t *a[4] = {&P.pos, &P.press, &P.atk, &P.rel}; return a[k]; }
    case P_CHORD:
        if (k == 0) { *hi = 11; *step = 1; return &P.ch_key; }
        if (k == 1) { *hi = 6; *step = 1; return &P.ch_scale; }
        if (k == 2) { *lo = 3; *hi = 6; *step = 1; return &P.ch_notes; }
        *hi = 3; *step = 1; return &P.ch_voice;
    default: { uint8_t *a[4] = {&P.rev, &P.room, &P.strum, &P.tone}; return a[k]; }
    }
}

static void controls(ctl_t c[4])
{
    static const char *const L_KS[4] = {"TONE", "BARK", "BELL", "DECAY"}, *const L_KT[4] = {"TREMOLO", "RATE", "DRIVE", "DAMPER"};
    static const char *const L_PS[4] = {"EXCITE", "MATERIAL", "BRIGHT", "DECAY"}, *const L_PT[4] = {"POSITION", "PRESSURE", "ATTACK", "RELEASE"};
    static const char *const L_CH[4] = {"KEY", "SCALE", "NOTES", "VOICING"}, *const L_FX[4] = {"REVERB", "ROOM", "STRUM", "TONE"};
    uint32_t k;
    for (k = 0; k < 4u; k++) {
        int32_t lo, hi, st;
        uint8_t *p = ctl_ptr(k, &lo, &hi, &st);
        c[k].val[0] = 0;
        c[k].label = ui.page == P_SOUND ? (P.eng == ENG_KEYS ? L_KS[k] : L_PS[k]) : ui.page == P_TONE ? (P.eng == ENG_KEYS ? L_KT[k] : L_PT[k]) : ui.page == P_CHORD ? L_CH[k] : L_FX[k];
        c[k].fill = (int32_t)(*p - lo) * 100 / (hi - lo);
        if (ui.page == P_SOUND && P.eng == ENG_PHYS && k == 0) text_cat(c[k].val, EXC_NAME[P.exc]);
        else if (ui.page == P_SOUND && P.eng == ENG_PHYS && k == 1 && P.res == RES_MODAL) {
            static const char *const MN[4] = {"WOOD", "GLASS", "METAL", "SKIN"};
            int a = P.mat * 3 / 100 + (P.mat * 3 % 100 >= 50 ? 1 : 0);
            text_cat(c[k].val, MN[a > 3 ? 3 : a]);
        } else if (ui.page == P_CHORD && k == 0) text_cat(c[k].val, NOTE_NAME[P.ch_key]);
        else if (ui.page == P_CHORD && k == 1) { const char *s = SCALE_NAME[P.ch_scale]; char t[8]; uint32_t j; for (j = 0; j < 6u && s[j]; j++) t[j] = s[j]; t[j] = 0; text_cat(c[k].val, t); }
        else if (ui.page == P_CHORD && k == 2) { int_text(P.ch_notes, c[k].val); text_cat(c[k].val, " NOTES"); }
        else if (ui.page == P_CHORD && k == 3) text_cat(c[k].val, P.ch_voice == 3 ? "LEAD+B" : VOICE_NAME[P.ch_voice]);
        else if (ui.page == P_FX && k == 2) { int_text((int32_t)P.strum * 60 * 1000 / 44100, c[k].val); text_cat(c[k].val, "ms"); }
        else int_text(*p, c[k].val);
    }
}

static void turn(uint32_t k, int32_t e)
{
    int32_t lo, hi, st;
    uint8_t *p = ctl_ptr(k, &lo, &hi, &st);
    int32_t step = st == 1 ? e : e * st * ((e > 1 || e < -1) ? 2 : 1);
    *p = (uint8_t)clamp((int32_t)*p + step, lo, hi);
    ui.touched_at[k] = plat_ms();
    ses_dirty = 1;
    if (ui.page == P_CHORD) prev_n = 0;
}

static void load_patch(int32_t i)
{
    i = (i % NPATCH + NPATCH) % NPATCH;
    ses.patch = (uint8_t)i;
    P = FACTORY[i];
    prev_n = 0;
    ses_dirty = 1;
    app_say(PATCH_NAME[i], "");
}

static void page_to(uint32_t p)
{
    if (p == ui.page) return;
    ui.page = (uint8_t)p;
    ui.redraw_all = 1;
    app_say(PAGE_NAME[p], "");
}

static void engine_step(int32_t e)       /* ALGO: KEYS, STRING, TUBE, MODAL */
{
    int32_t cur = P.eng == ENG_KEYS ? 0 : 1 + P.res;
    cur = clamp(cur + (e > 0 ? 1 : -1), 0, 3);
    if (cur == 0) P.eng = ENG_KEYS;
    else { P.eng = ENG_PHYS; P.res = (uint8_t)(cur - 1); if (P.res == RES_TUBE && P.exc == EXC_STRIKE) P.exc = EXC_BLOW; }
    ses_dirty = 1;
    app_say(cur == 0 ? "KEYS" : RES_NAME[cur - 1], cur == 0 ? "TINE PIANO" : "PHYSICAL");
}

/* the audition: four chords around the key, two beats each at 84 BPM, played through the chord engine */
static void demo_tick(uint32_t now)
{
    static const int8_t DEG[4] = {0, 5, 1, 4};
    static uint32_t src = 0;
    uint32_t step_ms = 1430u;
    if (!ui.demo) return;
    if (now - ui.demo_t < step_ms && ui.demo_step) return;
    if (src) { stop_src(src); src = 0; }
    {
        const uint8_t *sc = SCALE[P.ch_scale];
        int d = DEG[ui.demo_step % 4u];
        uint8_t was = P.ch_on;
        src = (uint32_t)(60 + P.ch_key + sc[d] - (P.ch_key > 5 ? 12 : 0));
        P.ch_on = 1;
        play_src(src, 100);
        P.ch_on = was;
    }
    ui.demo_t = now;
    ui.demo_step++;
}
static void demo_stop(void) { uint32_t s; for (s = 0; s < 128u; s++) if (own_n[s]) stop_src(s); ui.demo = 0; ui.demo_step = 0; }

static void input(void)
{
    uint32_t keys, btn = plat_buttons(), k, pkt;
    int32_t e;
    touch_poll();
    keys = touch_keys();
    for (k = 0; k < NKEYS; k++) {
        uint32_t bit = 1u << k, src = (uint32_t)clamp(53 + (int32_t)k + 12 * ses.oct, 0, 127);
        if ((keys & bit) && !(ui.keys & bit)) play_src(src, touch_vel(k));
        else if (!(keys & bit) && (ui.keys & bit)) stop_src(src);
    }
    while (plat_midi_in(&pkt, 0)) {      /* MIDI in, any channel: notes, the sustain pedal, all notes off */
        uint32_t st = (pkt >> 8) & 0xF0u, n = (pkt >> 16) & 0x7Fu, v = (pkt >> 24) & 0x7Fu;
        if (st == 0x90u && v) play_src(n, v);
        else if (st == 0x80u || (st == 0x90u && !v)) stop_src(n);
        else if (st == 0xB0u && n == 64u) sustain_set(v >= 64u);
        else if (st == 0xB0u && (n == 123u || n == 120u)) { uint32_t s; for (s = 0; s < 128u; s++) if (own_n[s]) stop_src(s); sustain_set(0); }
    }
    for (k = 0; k < NB; k++) {
        uint32_t bit = 1u << k;
        if (!(btn & bit) || (ui.btn & bit)) continue;
        switch (k) {
        case B_HOME: page_to(P_SOUND); break;
        case B_EDIT: page_to(P_TONE); break;
        case B_FX: page_to(P_FX); break;
        case B_ARP:
            if (ui.page != P_CHORD) page_to(P_CHORD);
            else { P.ch_on = !P.ch_on; prev_n = 0; ses_dirty = 1; app_say("CHORDS", P.ch_on ? "ON" : "OFF"); }
            break;
        case B_PLAY:
            if (ui.demo) demo_stop(); else { ui.demo = 1; ui.demo_step = 0; prev_n = 0; }
            app_say(ui.demo ? "AUDITION" : "STOP", "");
            break;
        case B_OCTDN: case B_OCTUP:
            ses.oct = (int8_t)clamp(ses.oct + (k == B_OCTUP ? 1 : -1), -3, 3);
            { char t[4]; int_text(ses.oct, t); app_say("OCTAVE", t); }
            break;
        case B_SAVE:
            if (plat_store_save(OBJ_SESSION, &ses, sizeof ses) == 0) { ses_dirty = 0; app_say("SAVED", ""); }
            else app_say("SAVE", "FAILED");
            break;
        default: break;
        }
    }
    if ((e = plat_enc(EN_SELECT)) != 0) page_to((uint32_t)((ui.page + NPAGE + (e > 0 ? 1 : -1)) % NPAGE));
    if ((e = plat_enc(EN_ALGO)) != 0) engine_step(e);
    if ((e = plat_enc(EN_PRESET)) != 0) load_patch((int32_t)ses.patch + (e > 0 ? 1 : -1));
    for (k = 0; k < 4u; k++)
        if ((e = plat_enc((int)(EN_K1 + k))) != 0) turn(k, e);
    ui.keys = keys;
    ui.btn = btn;
}

void app_input_only(void) { input(); }

/* ---------------------------------------------------------------------------------- drawing --- */
#define MAIN_Y 34
#define MAIN_H 144
#define FOOT_Y 180

static void draw_header(void)
{
    uint32_t i, now = plat_ms(), on = voices_on;
    uint16_t ac = accent();
    cv_begin(240, MAIN_Y, C_BLACK);
    if (now < ui.msg_until) {
        int32_t x = cv_text(8, 4, &FONT_B, ui.msg[0], C_WHITE);
        cv_text(x + 8, 4, &FONT_B, ui.msg[1], ac);
    } else {
        int32_t x = cv_text(8, 4, &FONT_B, PATCH_NAME[ses.patch], C_WHITE);
        if (ses_dirty) cv_rect(x + 4, 10, 4, 4, ac);
        if (P.ch_on && chord.n > 1) cv_text(150, 6, &FONT_B, chord.name, ac);
    }
    for (i = 0; i < NV; i++) {           /* the voices: lit while they sound */
        uint32_t lvl = (uint32_t)(vc[i].lvl * 2000.0f);
        cv_rect(196 + (int32_t)(i % 4u) * 10, 4 + (int32_t)(i / 4u) * 9, 6, 6, (on >> i & 1u) ? dim565(ac, 2u + (lvl > 6u ? 6u : lvl), 8) : C_LINE);
    }
    for (i = 0; i < NPAGE; i++)
        cv_rect(8 + (int32_t)i * 57, 27, 52, 3, i == ui.page ? ac : C_LINE);
    cv_blit(0, 0);
}

/* KEYS: the harp seen from above: a tine per voice, swinging by how loud it rings, its pickup glowing */
static void draw_tines(uint16_t ac)
{
    uint32_t i, s;
    for (i = 0; i < NV; i++) {
        voice_t *v = &vc[i];
        int32_t cx = 16 + (int32_t)i * 29, base = 128, on = v->on && v->eng == ENG_KEYS;
        float note = on ? (float)v->note : 48.0f + (float)i * 4.0f, amp, d;
        int32_t L = (int32_t)fm_clampf(112.0f - (note - 36.0f) * 1.1f, 36.0f, 110.0f), px = 0, py = base;
        uint16_t col;
        amp = on ? fm_minf(13.0f, fm_sqrtf(v->lvl) * 40.0f) : 0.0f;
        ui.tine_ph[i] += 1.9f + note * 0.013f;
        d = amp * fm_sinf(ui.tine_ph[i]);
        if (amp > 1.0f) {                                          /* the blur of its swing either side */
            int32_t gx = cx, gy = base, ax = cx, ay = base;
            uint16_t gc = dim565(ac, 1, 4);
            for (s = 1; s <= 12u; s++) {
                float h = (float)s / 12.0f;
                int32_t y = base - (int32_t)((float)L * h), x0 = cx - (int32_t)(amp * h * h), x1 = cx + (int32_t)(amp * h * h);
                cv_line(gx, gy, x0, y, gc);
                cv_line(ax, ay, x1, y, gc);
                gx = x0; gy = y; ax = x1; ay = y;
            }
        }
        col = on ? mix565(C_GRAY, ac, (uint32_t)fm_minf(256.0f, amp * 40.0f)) : C_LINE;
        cv_rect(cx - 6, base, 13, 6, C_DIM);                       /* the clamp */
        cv_rect(cx + 5, base - L * 6 / 10, 3, L * 6 / 10, on ? dim565(ac, 2, 5) : C_LINE);   /* the tonebar */
        for (s = 0; s <= 12u; s++) {                               /* the tine bends as a cantilever */
            float h = (float)s / 12.0f;
            int32_t x = cx + (int32_t)(d * h * h), y = base - (int32_t)((float)L * h);
            if (s) cv_line(px, py, x, y, col);
            if (s && on) cv_line(px + 1, py, x + 1, y, col);
            px = x; py = y;
        }
        {                                                          /* the pickup, lit by the signal */
            uint32_t glow = on ? (uint32_t)fm_minf(8.0f, v->lvl * 300.0f) : 0u;
            cv_rect(cx - 5, base - L - 12, 11, 7, glow ? mix565(C_DIM, ac, 64u + glow * 24u) : C_DIM);
            if (glow > 2u) { cv_rect(cx - 1, base - L - 5, 2, 4, dim565(ac, glow, 8)); }
        }
        if (on) { char t[6]; note_text(v->note, t); cv_text(cx - 8, 136, &FONT_XS, t, v->gate ? C_WHITE : C_GRAY); }
    }
}

/* STRING / TUBE: the newest voices' delay lines drawn as they are, a string or a bore each */
static void draw_waveguides(uint16_t ac)
{
    uint32_t order[NV], n = 0, i, j;
    for (i = 0; i < NV; i++) if (vc[i].on && vc[i].eng == ENG_PHYS && vc[i].res != RES_MODAL) order[n++] = i;
    for (i = 1; i < n; i++) for (j = i; j > 0 && vc[order[j]].age > vc[order[j - 1]].age; j--) { uint32_t t = order[j]; order[j] = order[j - 1]; order[j - 1] = t; }
    if (n > 4u) n = 4u;
    for (i = 0; i < 4u; i++) {
        int32_t y = 22 + (int32_t)i * 32, x0 = 14, x1 = 226, px = x0, py = y, s;
        voice_t *v = i < n ? &vc[order[i]] : 0;
        uint16_t col = v ? mix565(C_GRAY, ac, (uint32_t)fm_minf(256.0f, v->lvl * 6000.0f)) : C_LINE;
        int tube = v ? v->res == RES_TUBE : P.res == RES_TUBE;
        if (tube) { cv_rect(x0, y - 12, x1 - x0, 1, C_DIM); cv_rect(x0, y + 12, x1 - x0, 1, C_DIM); cv_rect(x0 - 6, y - 6, 6, 13, C_DIM); }
        else { cv_rect(x0 - 3, y - 7, 3, 15, C_DIM); cv_rect(x1, y - 7, 3, 15, C_DIM); }
        if (!v) { cv_rect(x0, y, x1 - x0, 1, col); continue; }
        {
            const float *dl = wg[order[i]];
            float len = v->dlen, mx = 0.0001f;
            for (s = 0; s < 72; s++) { float rp = (float)v->dw - len + len * (float)s / 72.0f; uint32_t ix; if (rp < 0.0f) rp += (float)DLMAX; ix = (uint32_t)rp & (DLMAX - 1u); mx = fm_maxf(mx, fm_fabsf(dl[ix])); }
            mx = fm_maxf(mx, 0.05f);
            for (s = 0; s <= 72; s++) {
                float rp = (float)v->dw - len + len * (float)s / 72.0f;
                uint32_t ix;
                int32_t x = x0 + (x1 - x0) * s / 72, yy;
                if (rp < 0.0f) rp += (float)DLMAX;
                ix = (uint32_t)rp & (DLMAX - 1u);
                yy = y - (int32_t)(dl[ix] / mx * (tube ? 9.0f : 11.0f) * fm_minf(1.0f, v->lvl * 400.0f));
                if (s) cv_line(px, py, x, yy, col);
                px = x; py = yy;
            }
        }
        {                                                          /* where it is excited */
            int32_t ex = x0 + (int32_t)((float)(x1 - x0) * (0.08f + (float)P.pos / 100.0f * 0.42f));
            if (tube) ex = x0 - 3;
            if (v->exc == EXC_BOW) cv_rect(ex - 1, y - 13, 3, 27, dim565(ac, 3, 4));
            else if (v->exc == EXC_STRIKE) cv_rect(ex - 3, y - 16, 7, 5, C_WHITE);
            else if (v->exc == EXC_PLUCK) cv_rect(ex - 1, y - 15, 3, 4, C_WHITE);
            else { uint32_t k; for (k = 0; k < 6u; k++) cv_rect(ex + (int32_t)((v->t / 300u + k * 7u) % 30u) - 4, y - 4 + (int32_t)(k * 3u % 9u), 2, 2, dim565(ac, (uint32_t)(v->env * 6.0f) + 1u, 7)); }
        }
        { char t[6]; note_text(v->note, t); cv_text(x1 - 22, y - 26, &FONT_XS, t, C_GRAY); }
    }
}

/* MODAL: the body (a ring that wobbles with its modes; a bar for wood) and its modes as a spectrum */
static void draw_modes(uint16_t ac)
{
    voice_t *v = 0;
    uint32_t i, m;
    float amp[NMODE] = {0}, ratio[NMODE], damp[NMODE], ts, mx = 0.0001f;
    material((float)P.mat / 100.0f, ratio, damp, &ts);
    for (i = 0; i < NV; i++) if (vc[i].on && vc[i].res == RES_MODAL && vc[i].eng == ENG_PHYS && (!v || vc[i].age > v->age)) v = &vc[i];
    for (i = 0; i < NV; i++) {           /* every sounding voice's modes, summed by mode */
        voice_t *u = &vc[i];
        if (!u->on || u->res != RES_MODAL || u->eng != ENG_PHYS) continue;
        for (m = 0; m < NMODE; m++) amp[m] += fm_sqrtf(u->zr[m] * u->zr[m] + u->zi[m] * u->zi[m]) * u->wt[m];
    }
    for (m = 0; m < NMODE; m++) mx = fm_maxf(mx, amp[m]);
    {
        int32_t cx = 62, cy = 70, R = 42, s, px = 0, py = 0;
        static float ph;
        ph += 0.25f;
        if (P.mat < 17) {                /* wood: a bar bending in its first two shapes */
            for (s = 0; s <= 40; s++) {
                float x = (float)s / 40.0f, b = amp[0] / mx * fm_sinf(PI_F * x) * fm_sinf(ph) * 9.0f + amp[1] / mx * fm_sinf(TWO_PI * x) * fm_sinf(ph * 2.7f) * 5.0f;
                int32_t X = cx - 50 + s * 100 / 40, Y = cy + (int32_t)(v ? b : 0.0f);
                cv_rect(X, Y - 6, 3, 13, mix565(C_DIM, ac, v ? 200u : 60u));
            }
        } else {
            for (s = 0; s <= 64; s++) {
                float th = TWO_PI * (float)s / 64.0f, r = (float)R;
                if (v) for (m = 0; m < 6u; m++) r += amp[m] / mx * 6.0f * fm_sinf((float)(m + 2u) * th + ph * (1.0f + (float)m * 0.7f));
                {
                    int32_t x = cx + (int32_t)(r * fm_cosf(th)), y = cy + (int32_t)(r * fm_sinf(th));
                    if (s) { cv_line(px, py, x, y, v ? ac : C_DIM); cv_line(px, py + 1, x, y + 1, v ? dim565(ac, 1, 2) : C_LINE); }
                    px = x; py = y;
                }
            }
        }
    }
    for (m = 0; m < NMODE; m++) {        /* the spectrum: where each mode sits, how loud it rings */
        float lr = fm_logf(ratio[m] / 0.5f) / fm_logf(80.0f);
        int32_t x = 124 + (int32_t)(lr * 104.0f), h = (int32_t)(amp[m] / mx * 100.0f);
        cv_rect(x, 118 - 100, 1, 100, C_LINE);
        if (v) cv_rect(x - 1, 118 - h, 3, h + 1, mix565(dim565(ac, 1, 2), ac, (uint32_t)(h * 256 / 101)));
    }
    cv_text(124, 126, &FONT_XS, "MODES", C_GRAY);
}

static void draw_chord(uint16_t ac)
{
    char t[24];
    int32_t lo = 48, i, w;
    if (chord.n) { lo = chord.notes[0]; for (i = 1; i < (int32_t)chord.n; i++) if (chord.notes[i] < lo) lo = chord.notes[i]; lo = lo / 12 * 12; }
    if (P.ch_on && chord.n > 1) {
        /* the large font is capitals only: a flat's "b" in the smaller one */
        char root[2] = {chord.name[0], 0};
        const char *rest = chord.name + 1;
        int flat = chord.name[1] == 'b';
        if (flat) rest++;
        w = text_w(&FONT_M, root) + (flat ? text_w(&FONT_B, "b") : 0) + text_w(&FONT_M, rest);
        {
            int32_t x = cv_text(120 - w / 2, 6, &FONT_M, root, C_WHITE);
            if (flat) x = cv_text(x, 4, &FONT_B, "b", C_WHITE);
            cv_text(x, 6, &FONT_M, rest, C_WHITE);
        }
        t[0] = 0; text_cat(t, chord.roman); text_cat(t, "  IN "); text_cat(t, NOTE_NAME[P.ch_key]); text_cat(t, " "); text_cat(t, SCALE_NAME[P.ch_scale]);
        w = text_w(&FONT_XS, t);
        cv_text(120 - w / 2, 40, &FONT_XS, t, ac);
    } else {
        const char *s = P.ch_on ? "PLAY A KEY" : "CHORDS OFF: ARP TURNS THEM ON";
        w = text_w(&FONT_XS, s);
        cv_text(120 - w / 2, 22, &FONT_XS, s, C_GRAY);
    }
    {                                    /* two octaves of keys from the chord's lowest C; its notes lit */
        static const int8_t WHITE[7] = {0, 2, 4, 5, 7, 9, 11};
        int32_t k, x0 = 8, kw = 16, y0 = 62, kh = 70;
        for (k = 0; k < 14; k++) {
            int32_t n = lo + WHITE[k % 7] + 12 * (k / 7), lit = 0, j;
            for (j = 0; j < (int32_t)chord.n; j++) if (chord.notes[j] == n) lit = 1 + (n % 12 == chord.root % 12);
            cv_rect(x0 + k * kw, y0, kw - 2, kh, lit ? (lit == 2 ? C_WHITE : ac) : C_GRAY);
        }
        for (k = 0; k < 14; k++) {
            int32_t pc = WHITE[k % 7], n, lit = 0, j;
            if (pc == 4 || pc == 11) continue;
            n = lo + pc + 1 + 12 * (k / 7);
            for (j = 0; j < (int32_t)chord.n; j++) if (chord.notes[j] == n) lit = 1 + (n % 12 == chord.root % 12);
            cv_rect(x0 + k * kw + kw - 6, y0, 9, kh * 6 / 10, lit ? (lit == 2 ? C_WHITE : ac) : C_BLACK);
        }
    }
}

static void draw_fx(uint16_t ac)
{
    uint32_t i, mx = 24;
    for (i = 0; i < 232u; i++) if (lvl_hist[i] > mx) mx = lvl_hist[i];
    for (i = 0; i < 232u; i++) {
        uint32_t v = lvl_hist[(lvl_w + i) % 232u];
        int32_t hh = (int32_t)(v * 50u / mx);
        cv_rect(4 + (int32_t)i, 60 - hh, 1, hh * 2 + 1, i > 220u ? C_WHITE : dim565(ac, 2u + i * 3u / 232u, 5));
    }
    {                                    /* the reverb's tail, as it is set */
        int32_t x;
        float t = 0.3f + (float)P.room / 100.0f * 2.5f;
        for (x = 0; x < 232; x += 2) {
            float a = fm_expf(-(float)x / 232.0f * 4.0f / t) * (float)P.rev / 100.0f;
            int32_t h = (int32_t)(a * 22.0f);
            cv_rect(4 + x, 138 - h, 1, h + 1, C_DIM);
        }
        cv_text(4, 116, &FONT_XS, "REVERB TAIL", C_GRAY);
    }
}

static void draw_main(void)
{
    uint16_t ac = accent();
    cv_begin(240, MAIN_H, C_BLACK);
    if (ui.page == P_CHORD) draw_chord(ac);
    else if (ui.page == P_FX) draw_fx(ac);
    else if (P.eng == ENG_KEYS) draw_tines(ac);
    else if (P.res == RES_MODAL) draw_modes(ac);
    else draw_waveguides(ac);
    cv_blit(0, MAIN_Y);
}

static void draw_footer(void)
{
    ctl_t c[4];
    uint32_t i, now = plat_ms();
    uint16_t ac = accent();
    controls(c);
    cv_begin(240, 240 - FOOT_Y, C_BLACK);
    cv_rect(0, 0, 240, 1, C_LINE);
    for (i = 0; i < 4u; i++) {
        int32_t x = 2 + (int32_t)i * 60;
        int lit = ui.touched_at[i] && now - ui.touched_at[i] < 600u;
        cv_text(x + 2, 6, &FONT_XS, c[i].label, lit ? C_WHITE : C_GRAY);
        cv_text(x + 2, 24, &FONT_B, c[i].val, lit ? C_WHITE : C_HI);
        cv_rect(x + 2, 48, 54, 3, C_LINE);
        cv_rect(x + 2, 48, c[i].fill * 54 / 100, 3, ac);
    }
    cv_blit(0, FOOT_Y);
}

static void lights(void)
{
    uint32_t lit = ui.keys, dim = 0, b = 0, bd = 0, k;
    for (k = 0; k < NV; k++) {           /* keys light while their notes sound */
        int32_t key = (int32_t)vc[k].note - 53 - 12 * ses.oct;
        if (vc[k].on && key >= 0 && key < (int32_t)NKEYS) { if (vc[k].gate) lit |= 1u << key; else dim |= 1u << key; }
    }
    {
        static const uint8_t PB[NPAGE] = {B_HOME, B_EDIT, B_ARP, B_FX};
        b |= 1u << PB[ui.page];
        for (k = 0; k < NPAGE; k++) bd |= 1u << PB[k];
    }
    if (P.ch_on) b |= 1u << B_ARP;
    if (ui.demo) b |= 1u << B_PLAY;
    if (ses_dirty) b |= 1u << B_SAVE;
    plat_lights(b | ui.btn, lit, bd, dim, 1);
}

void app_init(void)
{
    if (plat_store_load(OBJ_SESSION, &ses, sizeof ses) != (int)sizeof ses || ses.magic != SES_MAGIC || !params_valid(&ses.p) || ses.patch >= NPATCH) {
        ses.magic = SES_MAGIC;
        ses.patch = 0;
        ses.oct = 0;
        P = FACTORY[0];
    }
    if (ses.oct < -3 || ses.oct > 3) ses.oct = 0;
}

void app_ui_init(void)
{
    touch_on(1);
    lcd_fill(0, 0, 240, 240, C_BLACK);
    ui.redraw_all = 1;
}

void app_frame(void)
{
    static uint32_t last_hdr, last_ft;
    uint32_t now = plat_ms(), h = 2166136261u, i, j;
    ctl_t c[4];
    input();
    demo_tick(now);
    if (ann_on() && ann_where()) ann_say(PAGE_NAME[ui.page], 0, ANN_NOW);
    ann_tick(now);
    /* the header redraws for the voice lights (every frame they move) or a message */
    draw_header();
    (void)last_hdr;
    controls(c);
    for (i = 0; i < 4u; i++) for (j = 0; c[i].val[j]; j++) h = (h ^ (uint8_t)c[i].val[j]) * 16777619u;
    for (i = 0; i < 4u; i++) h = (h ^ (ui.touched_at[i] && now - ui.touched_at[i] < 600u)) * 16777619u;
    h ^= ui.page * 7919u + (uint32_t)P.eng * 131u + P.res * 31u + P.mat;
    if (h != last_ft || ui.redraw_all) { last_ft = h; draw_footer(); }
    draw_main();
    ui.redraw_all = 0;
    lights();
}

#ifdef PLAT_HOST
int app_expect(const char *what, int *got)
{
    if (!strcmp(what, "page")) *got = ui.page;
    else if (!strcmp(what, "eng")) *got = P.eng;
    else if (!strcmp(what, "res")) *got = P.res;
    else if (!strcmp(what, "patch")) *got = ses.patch;
    else if (!strcmp(what, "chord")) *got = P.ch_on;
    else if (!strcmp(what, "chordn")) *got = (int)chord.n;
    else if (!strcmp(what, "root")) *got = chord.root;
    else if (!strcmp(what, "voices")) { int n = 0, i; for (i = 0; i < NV; i++) n += vc[i].on; *got = n; }
    else if (!strcmp(what, "peak")) *got = (int)(out_peak * 1000.0f);
    else if (!strcmp(what, "loud")) *got = out_peak > 0.005f;
    else if (!strcmp(what, "bus")) { *got = (int)(dbg_bus * 1000.0f); dbg_bus = 0.0f; }   /* the loudest mix before FX, x1000 */
    else if (!strcmp(what, "dirty")) *got = ses_dirty;
    else if (!strcmp(what, "oct")) *got = ses.oct;
    else return 0;
    return 1;
}
#endif
