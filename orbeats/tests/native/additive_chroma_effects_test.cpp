// Stage 4a/4b planning functions (rings, lasers) under ASAN/UBSan. The expected
// values follow Chroma's handlers as described in the stage-4 spec, section 4.
#include "additive_chroma_effects.h"
#include <assert.h>
#include <stdio.h>
using namespace BsdAdditive;
static ChromaEvent record(unsigned flags) {
    ChromaEvent e; memset(&e, 0, sizeof(e)); e.flags = (uint16_t)flags; e.step_mult = e.prop_mult = e.speed_mult = 1.0f; return e;
}
static bool near(float a, float b) { return a - b < 1e-4f && b - a < 1e-4f; }
static void rings() {
    // Game: dest 30, it chose -rotation (rotRight) with rotation 90, step 5.
    const ChromaRingIn game = {30.0f, -60.0f, 5.0f, 90.0f, 1, 1.0f, false};
    ChromaEvent e = record(0);
    assert(!chroma_has_ring_rotation(e) && !chroma_has_ring_rotation(record(kChromaColor | kChromaLock)));
    e.step_mult = 2.0f; assert(chroma_has_ring_rotation(e));
    // Only a multiplier: the game's own direction, step and values, scaled.
    ChromaRingOut out = chroma_ring_rotation(e, game);
    assert(near(out.angle, -60.0f) && near(out.step, 10.0f) && out.prop == 1 && near(out.flexy, 1.0f));
    // Every value from the map; direction 1 = right = minus.
    e = record(kChromaDirection | kChromaRotation | kChromaStep | kChromaProp | kChromaSpeed);
    e.direction = 1; e.rotation = 45; e.step = 12; e.prop = 2.4f; e.speed = 3; e.prop_mult = 2; e.speed_mult = 0.5f;
    out = chroma_ring_rotation(e, game);
    assert(near(out.angle, 30.0f - 45.0f) && near(out.step, 12.0f) && out.prop == 5 && near(out.flexy, 1.5f));
    e.direction = 0; out = chroma_ring_rotation(e, game); assert(near(out.angle, 75.0f));
    // Unset direction keeps the game's random choice, also with a different rotation.
    e = record(kChromaRotation); e.rotation = 10;
    assert(near(chroma_ring_rotation(e, game).angle, 20.0f));
    ChromaRingIn left = game; left.game_angle = 120.0f; assert(near(chroma_ring_rotation(e, left).angle, 40.0f));
    ChromaRingIn negative = game; negative.field_rotation = -90.0f; negative.game_angle = 120.0f;   // +(-90) chosen = rotRight
    assert(near(chroma_ring_rotation(e, negative).angle, 20.0f));
    // counterSpin flips small rings only.
    e = record(kChromaCounterSpin | kChromaDirection); e.direction = 1;
    assert(near(chroma_ring_rotation(e, game).angle, 120.0f));
    ChromaRingIn big = game; big.big = true; assert(near(chroma_ring_rotation(e, big).angle, -60.0f));
    // reset: the spawner's rotation, step 0, propagation 50, flexy 50; map values ignored.
    e = record(kChromaReset | kChromaRotation | kChromaStep); e.rotation = 1; e.step = 99;
    out = chroma_ring_rotation(e, game);
    assert(near(out.angle, -60.0f) && out.step == 0.0f && out.prop == 50 && out.flexy == 50.0f);
    // Propagation is an int in the game: rounded, at least 1, bounded.
    assert(chroma_ring_prop(0.0f) == 1 && chroma_ring_prop(-5.0f) == 1 && chroma_ring_prop(0.4f) == 1 && chroma_ring_prop(1.5f) == 2);
    assert(chroma_ring_prop(NAN) == 1 && chroma_ring_prop(1e9f) == 1000 && chroma_ring_prop(INFINITY) == 1000);
    // Names: case-insensitive equality, "Big" is case-sensitive like Chroma's Contains.
    assert(chroma_name_equals("bigtracklanerings", 17, "BigTrackLaneRings") && !chroma_name_equals("bigtracklanerings", 17, "BigTrackLaneRings2"));
    assert(!chroma_name_equals("bigtracklanerings", 17, "BigTrack") && !chroma_name_equals("x", 1, NULL) && chroma_name_equals("", 0, ""));
    assert(chroma_name_has_big("BigTrackLaneRings") && !chroma_name_has_big("bigrings") && !chroma_name_has_big(NULL));
}
static void pair() {
    // No record values: original with the game's direction, speed = value*20*direction.
    ChromaEvent e = record(kChromaSpeed); e.speed = 2.5f;
    ChromaPairPlan p = chroma_pair_plan(e, 3, true, -1.0f);
    assert(p.call_original && p.direction == -1.0f && p.set_speed && near(p.speed, -50.0f) && !p.set_enabled);
    e = record(kChromaDirection); e.direction = 0;
    p = chroma_pair_plan(e, 3, true, 1.0f); assert(p.call_original && p.direction == -1.0f && near(p.speed, -60.0f));
    p = chroma_pair_plan(e, 3, false, -1.0f); assert(p.direction == 1.0f && near(p.speed, 60.0f));
    e.direction = 1; p = chroma_pair_plan(e, 3, true, -1.0f); assert(p.direction == 1.0f);
    p = chroma_pair_plan(e, 3, false, 1.0f); assert(p.direction == -1.0f);
    // lockPosition: never the original (it would reset the position).
    e = record(kChromaLock);
    p = chroma_pair_plan(e, 5, false, 1.0f); assert(!p.call_original && p.set_enabled && p.enabled && near(p.speed, 100.0f));
    p = chroma_pair_plan(e, 0, false, 1.0f); assert(!p.call_original && p.set_enabled && !p.enabled && !p.set_speed);
    // value 0 without lock: the game's reset; negative: nothing at all.
    p = chroma_pair_plan(record(kChromaSpeed), 0, true, 1.0f); assert(p.call_original && !p.set_speed && !p.set_enabled);
    p = chroma_pair_plan(record(kChromaSpeed), -1, true, 1.0f); assert(!p.call_original && !p.set_speed && !p.set_enabled);
}
static void single() {
    ChromaEvent e = record(kChromaSpeed); e.speed = 1.5f;
    ChromaSinglePlan s = chroma_single_plan(e, 4, true, false);
    assert(s.call_original && s.set_speed && near(s.speed, 30.0f) && s.keep_game_sign && s.spinning_after);
    e = record(kChromaDirection); e.direction = 0;
    s = chroma_single_plan(e, 2, true, false); assert(near(s.speed, -40.0f) && !s.keep_game_sign);
    s = chroma_single_plan(e, 2, false, false); assert(near(s.speed, 40.0f));
    // lockPosition on a spinning laser: speed only, no reset.
    e = record(kChromaLock); s = chroma_single_plan(e, 3, true, true);
    assert(!s.call_original && near(s.speed, 60.0f) && s.keep_game_sign && s.spinning_after);
    // lockPosition on a laser the game switched off: the game's handler (documented deviation).
    s = chroma_single_plan(e, 3, true, false); assert(s.call_original && s.spinning_after);
    // value 0: lock freezes at speed 0 and stays "enabled"; no lock is the game's reset.
    s = chroma_single_plan(e, 0, true, true); assert(!s.call_original && s.set_speed && s.speed == 0.0f && s.spinning_after);
    s = chroma_single_plan(record(kChromaSpeed), 0, true, true); assert(s.call_original && !s.set_speed && !s.spinning_after);
    s = chroma_single_plan(record(kChromaSpeed), -2, true, true); assert(s.call_original && !s.set_speed && s.spinning_after);
}
static void gradient() {
    ChromaEvent e = record(kChromaGradient | kChromaColor);
    const float from[4] = {1, 0, 0, 1}, to[4] = {0, 1, 0, 0.5f};
    memcpy(e.gradient_from, from, 16); memcpy(e.gradient_to, to, 16); e.gradient_seconds = 1.5f; e.easing = 6;
    // On, flash and fade: the tween runs the gradient from the event time.
    static const int32_t values[] = {1, 2, 3, 5, 7, 4, -1};
    for (int32_t value : values) {
        ChromaGradientPlan g = chroma_gradient_plan(e, value, 12.25f);
        assert(g.apply && g.start == 12.25f && g.duration == 1.5f && g.ease == 6 && !memcmp(g.from, from, 16) && !memcmp(g.to, to, 16));
    }
    // Off keeps the game's behaviour; no gradient flag, nothing to do.
    assert(!chroma_gradient_plan(e, 0, 1.0f).apply);
    assert(!chroma_gradient_plan(record(kChromaColor), 5, 1.0f).apply);
}
int main() {
    rings(); pair(); single(); gradient();
    puts("chroma effects ok");
    return 0;
}
