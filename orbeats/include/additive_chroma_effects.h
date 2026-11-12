// Chroma level 2 (stage 4a/4b): what the ring and laser hooks write, as pure
// functions of a sidecar record and the values the game itself chose. The
// rules follow Chroma's own handlers (Aeroluna/Heck: RingRotationChromafier,
// RingStepChromafier, LightPairRotationChromafier, LightRotationChromafier);
// see docs/plans/2026-11-12-chroma-stage4-rings-lasers-spec.md, section 4.
// No SDK/game dependency, so the exact production logic runs under sanitizers.
#pragma once
#include <math.h>
#include "additive_chroma.h"
namespace BsdAdditive {
static const uint16_t kChromaRingRotationFlags = kChromaDirection | kChromaSpeed | kChromaStep | kChromaProp | kChromaRotation |
                                                 kChromaReset | kChromaCounterSpin | kChromaName;
static const uint16_t kChromaLaserFlags = kChromaDirection | kChromaSpeed | kChromaLock;
inline bool chroma_has_ring_rotation(const ChromaEvent& e) {
    return (e.flags & kChromaRingRotationFlags) || e.step_mult != 1.0f || e.prop_mult != 1.0f || e.speed_mult != 1.0f;
}

// ── ring rotation (TrackLaneRingsRotationEffectSpawner, event type 8) ───────
// The game already ran: it picked a random direction and step and appended a
// RingRotationEffect {angle = dest ± rotation, step, propagation, flexy}.
struct ChromaRingIn {
    float dest;            // rings[0]._destRotZ before the game's handler ran
    float game_angle;      // rotationAngle of the effect the game appended
    float game_step;       // rotationStep the game picked (from _rotationStepType)
    float field_rotation;  // spawner _rotation
    int field_prop;        // spawner _rotationPropagationSpeed
    float field_flexy;     // spawner _rotationFlexySpeed
    bool big;              // the spawner's object name contains "Big" (counterSpin)
};
struct ChromaRingOut { float angle, step, flexy; int prop; };
inline int chroma_ring_prop(float prop) {
    // The game's propagation speed is an int (Chroma keeps a float). 0 or less
    // would never finish the effect, so at least 1.
    if (!(prop >= 1.0f)) return 1;
    if (prop > 1000.0f) return 1000;
    return (int)lroundf(prop);
}
inline ChromaRingOut chroma_ring_rotation(const ChromaEvent& e, const ChromaRingIn& in) {
    // Chroma: angle = dest + rotation * (rotRight ? -1 : 1).
    bool right;
    if (e.flags & kChromaDirection) right = e.direction == 1;
    else {
        const float delta = in.game_angle - in.dest;   // the game's own random choice
        right = in.field_rotation >= 0.0f ? delta < 0.0f : delta > 0.0f;
    }
    if ((e.flags & kChromaCounterSpin) && !in.big) right = !right;
    const float sign = right ? -1.0f : 1.0f;
    ChromaRingOut out;
    if (e.flags & kChromaReset) {
        out.angle = in.dest + in.field_rotation * sign; out.step = 0.0f; out.prop = 50; out.flexy = 50.0f;
        return out;
    }
    const float rotation = (e.flags & kChromaRotation) ? e.rotation : in.field_rotation;
    out.angle = in.dest + rotation * sign;
    out.step = ((e.flags & kChromaStep) ? e.step : in.game_step) * e.step_mult;
    out.prop = chroma_ring_prop(((e.flags & kChromaProp) ? e.prop : (float)in.field_prop) * e.prop_mult);
    out.flexy = ((e.flags & kChromaSpeed) ? e.speed : in.field_flexy) * e.speed_mult;
    return out;
}
// Ring name filter: case-insensitive equality (Chroma compares lower-cased names).
inline bool chroma_name_equals(const char* filter, size_t filter_len, const char* name) {
    if (!filter || !name) return false;
    for (size_t i = 0; i < filter_len; ++i) {
        char c = name[i]; if (!c) return false;
        if (c >= 'A' && c <= 'Z') c = (char)(c - 'A' + 'a');
        if (c != filter[i]) return false;
    }
    return name[filter_len] == 0;
}
inline bool chroma_name_has_big(const char* name) { return name && strstr(name, "Big") != NULL; }

// ── laser direction shared by both laser effects ────────────────────────────
// direction 0: left -1, right +1; 1: the opposite. Unset: keep `fallback`.
inline float chroma_laser_direction(const ChromaEvent& e, bool left, float fallback) {
    if (!(e.flags & kChromaDirection)) return fallback;
    if (e.direction == 0) return left ? -1.0f : 1.0f;
    return left ? 1.0f : -1.0f;
}
inline float chroma_laser_speed(const ChromaEvent& e, int32_t value) { return (e.flags & kChromaSpeed) ? e.speed : (float)value; }

// ── paired lasers (LightPairRotationEventEffect.UpdateRotationData) ─────────
struct ChromaPairPlan {
    bool call_original;    // run the game's UpdateRotationData (with `direction`)
    float direction;       // direction argument for the original
    bool set_enabled; bool enabled;
    bool set_speed; float speed;   // RotationData.rotationSpeed, written after the original
};
inline ChromaPairPlan chroma_pair_plan(const ChromaEvent& e, int32_t value, bool left, float game_direction) {
    ChromaPairPlan plan; memset(&plan, 0, sizeof(plan));
    const bool lock = (e.flags & kChromaLock) != 0;
    plan.direction = chroma_laser_direction(e, left, game_direction);
    if (value == 0) {
        if (lock) { plan.set_enabled = true; plan.enabled = false; }   // stop without resetting the position
        else plan.call_original = true;
    } else if (value > 0) {
        plan.set_speed = true; plan.speed = chroma_laser_speed(e, value) * 20.0f * plan.direction;
        if (lock) { plan.set_enabled = true; plan.enabled = true; }    // keep the current position
        else plan.call_original = true;
    }
    // value < 0: Chroma does nothing, and neither does the game.
    return plan;
}

// ── single lasers (LightRotationEventEffect.HandleBeatmapEvent) ─────────────
// enabled/transform are Unity calls the plugin does not make, so lockPosition
// is approximated (spec 4.3): value 0 freezes the laser at speed 0 instead of
// disabling it; a lock on a laser the game had switched off resets its position.
struct ChromaSinglePlan {
    bool call_original;
    bool set_speed; float speed;
    bool keep_game_sign;   // direction unset: multiply by the sign the game picked
    bool spinning_after;
};
inline ChromaSinglePlan chroma_single_plan(const ChromaEvent& e, int32_t value, bool left, bool spinning) {
    ChromaSinglePlan plan; memset(&plan, 0, sizeof(plan));
    const bool lock = (e.flags & kChromaLock) != 0;
    plan.spinning_after = spinning;
    if (value == 0) {
        if (lock) { plan.set_speed = true; plan.speed = 0.0f; }
        else { plan.call_original = true; plan.spinning_after = false; }
        return plan;
    }
    if (value < 0) { plan.call_original = true; return plan; }   // the game's own handling, unchanged
    plan.call_original = !(lock && spinning);
    plan.spinning_after = true;
    plan.set_speed = true;
    plan.keep_game_sign = !(e.flags & kChromaDirection);
    plan.speed = chroma_laser_speed(e, value) * 20.0f * chroma_laser_direction(e, left, 1.0f);
    return plan;
}

// ── light gradient (stage 4c) ───────────────────────────────────────────────
// After the game's own HandleColorChangeBeatmapEvent, the light's ColorTween is
// rewritten to run from -> to over the gradient, starting at the event time;
// the game's SongTimeTweeningManager then samples it every frame. An "off"
// event (0) keeps the game's behaviour: Chroma has nothing to colour there.
// Approximation: on fade and flash events the gradient replaces the fade.
struct ChromaGradientPlan { bool apply; float from[4], to[4]; float start, duration; int32_t ease; };
inline ChromaGradientPlan chroma_gradient_plan(const ChromaEvent& e, int32_t value, float event_time) {
    ChromaGradientPlan plan; memset(&plan, 0, sizeof(plan));
    if (!(e.flags & kChromaGradient) || value == 0) return plan;
    plan.apply = true;
    memcpy(plan.from, e.gradient_from, sizeof(plan.from)); memcpy(plan.to, e.gradient_to, sizeof(plan.to));
    plan.start = event_time; plan.duration = e.gradient_seconds; plan.ease = e.easing;
    return plan;
}
} // namespace BsdAdditive
