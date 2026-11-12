// (prototype) The lava floor's flicker in the Volcano's areas 9 and 10 as the Switch shows it, to compare in game.
// F6 switches it on and off (MHGU_LAVA30=1: on from the start).
//
// Material animations are evaluated by 0xd0ea4c(object r0, step s0): for each of the object's 4 slots
// ({u32 flags: bit 15 off, low 16 the animation's index; f32 clock} at +0x50 + 8 x slot) it evaluates the animation's
// keys at the clock, then advances the clock by speed (+0x20) x step and wraps it at the animation's length when it
// loops. The step is in 60 fps frames, so a clock runs at 60 a second at any frame rate (measured 2026-11-12: 59.9 at
// 60 fps, 60.0 at 30 fps, +2.0 per update at 30). An animation's header (r5 there): [[[object +0x74] +0x10]
// + 4 x index + 4]: u32 length, u32 flags (bit 0 loops, bits 2-17 entry count), entry pointers at +0x10; an entry:
// +4 u32 type / key count (0x1511: 21 linear keys), keys of 20 bytes from +0xc ({u32 frame, f32 x4}).
//
// The lava's glow is one such animation, 30 frames long, a brightness that jumps every frame or two (two curves:
// 5, 3, -, 6, 2, ... and 4, 5, -, 3, -, 6, ...; m08a09d / m08a10d materials 2, 3 / 10, 12). At 30 fps the Switch
// evaluates it at every second frame only; at 60 fps the port shows all of them. On, the clock is rounded down to an
// even frame for the evaluation (the Switch's samples, held for two frames) and the fraction put back afterwards, so
// the animation keeps its time. Every other animation is left alone.
#include <math.h>
#include <stdlib.h>

#include "../hle.h"
#include "../overlay.h"

#define FN_MAT_ANIM  0xd0ea4cu  // material animation evaluate + advance (r0: the object, s0: the step)
#define OBJ_SPEED    0x20u
#define OBJ_SLOTS    0x50u      // 4 x {u32 flags, f32 clock}
#define OBJ_ANIMS    0x74u
#define SLOT_OFF     0x8000u

static volatile bool g_on;

// An animation's header is one of the lava's two flicker curves.
static bool is_lava(uint32_t hdr) {
    if (!hdr || LD32(hdr) != 30 || (LD32(hdr + 4) & 0x3fffd) != 0x5) return false;  // 30 frames, loops, one entry
    uint32_t e = LD32(hdr + 0x10);
    if (!e || LD32(e + 4) != 0x1511 || LD32(e + 0xc) != 0 || LD32(e + 0x20) != 1) return false;
    float v0 = LDF(e + 0x10), v1 = LDF(e + 0x24);
    return (v0 == 5.0f && v1 == 3.0f) || (v0 == 4.0f && v1 == 5.0f);
}

static void mat_anim(Cpu *c) {
    uint32_t obj = c->r[0], anims = obj && g_on ? LD32(obj + OBJ_ANIMS) : 0;
    float frac[4];
    uint32_t hdr[4], mask = 0;
    if (anims) {
        uint32_t table = LD32(anims + 0x10);
        for (int i = 0; i < 4 && table; i++) {
            uint32_t flags = LD32(obj + OBJ_SLOTS + 8 * i);
            if (flags & SLOT_OFF) continue;
            hdr[i] = LD32(table + 4 * (flags & 0xffff) + 4);
            if (!is_lava(hdr[i])) continue;
            uint32_t at = obj + OBJ_SLOTS + 8 * i + 4;
            float t = LDF(at), q = 2.0f * floorf(t * 0.5f);
            frac[i] = t - q;
            STF(at, q);
            mask |= 1u << i;
        }
    }
    fnhook_call_original(c, FN_MAT_ANIM);
    for (int i = 0; i < 4; i++) {
        if (!(mask & (1u << i))) continue;
        uint32_t at = obj + OBJ_SLOTS + 8 * i + 4;
        float t = LDF(at) + frac[i], len = (float)LD32(hdr[i]);
        if (t >= len) t -= len;
        STF(at, t);
    }
}

void lavaflicker_toggle(void) {
    g_on = !g_on;
    LOG("[lava] Switch-style lava flicker %s\n", g_on ? "on" : "off");
    overlay_notice("Lava flicker (F6)", g_on ? "Switch style: every second frame" : "Port: every frame");
}

void lavaflicker_init(void) {
    const char *e = getenv("MHGU_LAVA30");
    g_on = e && atoi(e) > 0;
    fnhook_set(FN_MAT_ANIM, hle_register("lavaflicker:mat_anim", mat_anim));
}
