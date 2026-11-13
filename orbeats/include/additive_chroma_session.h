// Per-level Chroma state shared by the gameplay hooks. Game main thread only.
// Pure logic: the hooks in main.cpp read game fields and call into this.
#pragma once
#include "additive_chroma.h"
namespace BsdAdditive {
// Which colour the hooked LightSwitchEventEffect colour getters must return.
// CheckNextEventForFade asks for the NEXT same-type event twice, then for the
// current one; next_calls counts the calls that still belong to `next`.
// A NULL colour means "run the game's own getter".
struct ChromaLightContext { const float* current; const float* next; int next_calls; };
struct ChromaSession {
    ChromaChart chart; bool active; ChromaLightContext light;
    void begin(const ChromaChart& loaded) { chart = loaded; active = true; memset(&light, 0, sizeof(light)); }
    void end(void (*release)(void*)) { chroma_free(chart, release); active = false; memset(&light, 0, sizeof(light)); }
    // Colour for a live basic event, or NULL (game colour). Keyed by type and
    // time, not sameTypeIndex: the game's effect filters drop and rewrite events
    // and renumber the rest, so an event the filter removed simply never asks.
    const float* event_color(uint8_t type, float time, int32_t value) {
        if (!active) return NULL;
        const ChromaEvent* e = chroma_find_event_at(chart, type, time, value);
        return e && (e->flags & kChromaColor) ? e->rgba : NULL;   // a v2 record may carry ring/laser data only
    }
    // Stage 3b: the record of a live light event, claimed (`take`) or only looked at.
    ChromaEvent* record(uint8_t type, float time, int32_t value, bool take) {
        return active ? chroma_claim_event(chart, type, time, value, take) : NULL;
    }
    static const float* color_of(const ChromaEvent* e) { return e && (e->flags & kChromaColor) ? e->rgba : NULL; }
    const float* pick() {
        if (light.next_calls > 0) { --light.next_calls; return light.next; }
        return light.current;
    }
};
// Replaces the getter context for the duration of one original game call.
struct ChromaLightScope {
    ChromaSession& session; ChromaLightContext saved;
    ChromaLightScope(ChromaSession& s, const float* current, const float* next, int next_calls) : session(s), saved(s.light) {
        s.light.current = current; s.light.next = next; s.light.next_calls = next_calls;
    }
    ~ChromaLightScope() { session.light = saved; }
private:
    ChromaLightScope(const ChromaLightScope&);
    ChromaLightScope& operator=(const ChromaLightScope&);
};
} // namespace
