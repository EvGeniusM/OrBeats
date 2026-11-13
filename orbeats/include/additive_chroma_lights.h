// Stage 3b (Chroma lightID): per-light state of one light group. Pure logic,
// no game types: the hooks in src/chroma.cpp read the game and call into this.
// Spec: docs/plans/2026-11-12-chroma-stage3b-lightid-spec.md, section 4.
//
// A light either follows the game's group tween (kLightFollow) or keeps its own
// copy of the group's look from the moment an event stopped selecting it
// (kLightConst / kLightTween). Invariant: a following light shows the last event
// that selected it, exactly like Chroma's per-light tweens.
#pragma once
#include <stddef.h>
#include <stdint.h>
#include <string.h>
namespace BsdAdditive {
static const uint16_t kLightGroupMax = 1024;   // lights per group (Chroma's lightID lists are smaller)
enum LightMode { kLightFollow = 0, kLightConst = 1, kLightTween = 2 };
// The fields Tween<Color>.Update reads (2.04).
struct LightTween { float from[4], to[4]; float start, delay, duration; int32_t ease; };
struct LightState { uint8_t mode; float color[4]; LightTween tween; };   // color: kLightConst only
// The group's look right before an event: a running tween, or a constant colour.
struct GroupLook { bool running; float color[4]; LightTween tween; };
struct LightGroup {
    uintptr_t manager; int32_t id;      // key: LightWithIdManager and lightsID
    uintptr_t* keys; LightState* state; // capacity kLightGroupMax each, owned by the caller
    uint16_t count, own, running;       // recorded lights; lights not following; of them with a tween
    uint32_t full;                      // registrations refused because the group was full
};
inline void light_group_init(LightGroup& g, uintptr_t manager, int32_t id, uintptr_t* keys, LightState* state) {
    g.manager = manager; g.id = id; g.keys = keys; g.state = state; g.count = g.own = g.running = 0; g.full = 0;
}
inline int light_find(const LightGroup& g, uintptr_t key) {
    for (uint16_t i = 0; i < g.count; ++i) if (g.keys[i] == key) return i;
    return -1;
}
// Chroma's list: first registration order, never removed, never twice.
inline bool light_record(LightGroup& g, uintptr_t key) {
    if (!key || light_find(g, key) >= 0) return true;
    if (g.count >= kLightGroupMax) { ++g.full; return false; }
    g.keys[g.count] = key; memset(&g.state[g.count], 0, sizeof(LightState)); ++g.count;
    return true;
}
inline void light_recount(LightGroup& g) {
    g.own = g.running = 0;
    for (uint16_t i = 0; i < g.count; ++i) { if (g.state[i].mode != kLightFollow) ++g.own; if (g.state[i].mode == kLightTween) ++g.running; }
}
// An event with lights: followers it does not select take `look`; the selected ones follow.
// Returns how many indices fell outside the group (Chroma skips them too).
inline uint32_t light_select(LightGroup& g, const uint16_t* ids, uint16_t n, const GroupLook& look) {
    uint8_t chosen[kLightGroupMax]; memset(chosen, 0, sizeof(chosen));
    uint32_t outside = 0;
    for (uint16_t k = 0; k < n; ++k) { if (ids[k] < g.count) chosen[ids[k]] = 1; else ++outside; }
    for (uint16_t i = 0; i < g.count; ++i) {
        LightState& s = g.state[i];
        if (chosen[i]) { s.mode = kLightFollow; continue; }
        if (s.mode != kLightFollow) continue;
        if (look.running) { s.mode = kLightTween; s.tween = look.tween; }
        else { s.mode = kLightConst; memcpy(s.color, look.color, sizeof(s.color)); }
    }
    light_recount(g);
    return outside;
}
inline void light_all_follow(LightGroup& g) {
    for (uint16_t i = 0; i < g.count; ++i) g.state[i].mode = kLightFollow;
    g.own = g.running = 0;
}
// Tween<Color>.Update (2.04): elapsed clamped to duration + delay + 0.001, then
// progress = max(0, elapsed - delay) / duration (1 for a zero duration).
inline float light_progress(const LightTween& t, float now) {
    float elapsed = now - t.start, cap = t.duration + t.delay + 0.001f;
    if (elapsed > cap) elapsed = cap;
    if (!(t.duration > 0.0f)) return 1.0f;
    float p = elapsed - t.delay; if (p < 0.0f) p = 0.0f;
    return p / t.duration;
}
inline bool light_tween_done(const LightTween& t, float now) { return now - t.start >= t.duration + t.delay; }
typedef float (*LightEase)(float, int32_t, const void*);   // Interpolation.Interpolate(t, easeType)
// ColorTween.GetValue: from + (to - from) * ease(progress).
inline void light_tween_color(const LightTween& t, float now, LightEase ease, float out[4]) {
    const float e = ease(light_progress(t, now), t.ease, NULL);
    for (int k = 0; k < 4; ++k) out[k] = t.from[k] + (t.to[k] - t.from[k]) * e;
}
// Colour of an own light at `now`; a finished tween becomes constant (one last colour).
inline void light_own_color(LightState& s, float now, LightEase ease, float out[4]) {
    if (s.mode == kLightTween) {
        light_tween_color(s.tween, now, ease, out);
        if (light_tween_done(s.tween, now)) { s.mode = kLightConst; memcpy(s.color, out, sizeof(s.color)); }
        return;
    }
    memcpy(out, s.color, sizeof(s.color));
}
} // namespace
