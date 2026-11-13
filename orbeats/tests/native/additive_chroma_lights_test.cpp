// Stage 3b per-light state: Chroma's list order, follow/own switching, the game's tween formula.
#include "additive_chroma_lights.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
using namespace BsdAdditive;
static float linear(float t, int32_t, const void*) { return t; }
static float squared(float t, int32_t ease, const void*) { return ease == 7 ? t * t : -1.0f; }
static bool near(float a, float b) { return fabsf(a - b) < 1e-5f; }
static uintptr_t keys[kLightGroupMax]; static LightState state[kLightGroupMax];
static GroupLook constant(float r) { GroupLook g; memset(&g, 0, sizeof(g)); g.color[0] = r; g.color[3] = 1; return g; }
int main() {
    LightGroup g; light_group_init(g, 0x1000, 2, keys, state);
    // Registration order, no repeats, never removed; a null key is ignored.
    assert(light_record(g, 0xa0) && light_record(g, 0xb0) && light_record(g, 0xa0) && light_record(g, 0) && light_record(g, 0xc0));
    assert(g.count == 3 && light_find(g, 0xb0) == 1 && light_find(g, 0xc0) == 2 && light_find(g, 0xd0) == -1);
    // Event selecting light 1: lights 0 and 2 keep the old look, light 1 follows.
    const uint16_t one[] = {1, 7};
    assert(light_select(g, one, 2, constant(0.25f)) == 1);   // index 7 is outside the group
    assert(g.state[0].mode == kLightConst && g.state[1].mode == kLightFollow && g.state[2].mode == kLightConst);
    assert(g.own == 2 && g.running == 0 && g.state[0].color[0] == 0.25f);
    // Next event selects light 0: light 1 freezes at the newer look, light 2 keeps its own.
    const uint16_t zero[] = {0};
    light_select(g, zero, 1, constant(0.5f));
    assert(g.state[0].mode == kLightFollow && g.state[1].mode == kLightConst && g.state[1].color[0] == 0.5f && g.state[2].color[0] == 0.25f);
    // A running group tween is copied, not its current colour.
    GroupLook fade; memset(&fade, 0, sizeof(fade)); fade.running = true;
    fade.tween.from[0] = 1; fade.tween.to[0] = 0; fade.tween.start = 10; fade.tween.duration = 2; fade.tween.ease = 7;
    const uint16_t two[] = {2};
    light_select(g, two, 1, fade);
    assert(g.state[0].mode == kLightTween && g.running == 1 && g.own == 2);
    float c[4];
    light_own_color(g.state[0], 11.0f, squared, c); assert(near(c[0], 0.75f) && g.state[0].mode == kLightTween);   // 1 - 0.5^2
    light_own_color(g.state[0], 12.0f, squared, c);   // done: constant from now on
    assert(g.state[0].mode == kLightConst && c[0] < 0.0f + 1e-3f); light_recount(g); assert(g.running == 0);
    // An event without lights: everything follows again.
    light_all_follow(g); assert(g.own == 0 && g.state[1].mode == kLightFollow);
    // Tween<Color>.Update progress: delay, clamp at duration + delay + 0.001, zero duration.
    LightTween t; memset(&t, 0, sizeof(t)); t.start = 1; t.delay = 0.5f; t.duration = 2;
    assert(near(light_progress(t, 1.2f), 0.0f) && near(light_progress(t, 2.5f), 0.5f) && near(light_progress(t, 9.0f), (2.0f + 0.001f) / 2.0f));
    assert(!light_tween_done(t, 3.4f) && light_tween_done(t, 3.5f));
    t.duration = 0; assert(light_progress(t, 0.0f) == 1.0f);
    t.duration = 4; t.from[1] = 0.2f; t.to[1] = 0.6f; light_tween_color(t, 3.0f, linear, c); assert(near(c[1], 0.2f + 0.4f * 0.375f));
    // A full group refuses more lights and counts them.
    LightGroup full; light_group_init(full, 0x2000, 1, keys, state);
    for (uintptr_t k = 1; k <= kLightGroupMax; ++k) assert(light_record(full, k));
    assert(!light_record(full, 0xffff0) && full.full == 1 && full.count == kLightGroupMax && light_record(full, 5));
    puts("chroma lights ok");
    return 0;
}
