// Per-level Chroma light context: the order in which the hooked colour getters
// receive "next" and "current" colours, and the time-keyed event lookup.
#include "additive_chroma_session.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
using namespace BsdAdditive;
static int live = 0;
static void released(void* p) { if (p) { --live; free(p); } }
static ChromaChart chart() {
    ChromaChart c; memset(&c, 0, sizeof(c)); live += 4;
    c.events = (ChromaEvent*)calloc(3, sizeof(ChromaEvent)); c.notes = (ChromaNote*)calloc(1, sizeof(ChromaNote));
    c.walls = (ChromaWall*)calloc(1, sizeof(ChromaWall)); c.light_ids = (uint16_t*)calloc(1, sizeof(uint16_t));
    c.event_count = 3;   // sorted by (type, index); time non-decreasing within a type
    c.events[0].type = 1; c.events[0].index = 1; c.events[0].time = 1.0f; c.events[0].value = 1; c.events[0].rgba[0] = 0.25f;
    c.events[1].type = 1; c.events[1].index = 2; c.events[1].time = 2.0f; c.events[1].value = 5; c.events[1].rgba[0] = 0.75f;
    c.events[2].type = 1; c.events[2].index = 3; c.events[2].time = 2.0f; c.events[2].value = 1; c.events[2].rgba[0] = 0.5f;
    for (int i = 0; i < 3; ++i) c.events[i].flags = kChromaColor;
    return c;
}
int main() {
    static ChromaSession s; // zero-initialised like the plugin's global
    const float a[4] = {1, 0, 0, 1}, b[4] = {0, 1, 0, 1};
    // No session and no scope: every getter call falls through to the game.
    assert(!s.active && !s.pick() && !s.event_color(1, 1.0f, 1));
    s.begin(chart());
    assert(s.active && !s.pick());
    {   // HandleColorChangeBeatmapEvent: every call is the current event.
        ChromaLightScope outer(s, a, NULL, 0);
        assert(s.pick() == a && s.pick() == a);
        {   // Nested CheckNextEventForFade: next twice, then current.
            ChromaLightScope inner(s, a, b, 2);
            assert(s.pick() == b && s.pick() == b && s.pick() == a && s.pick() == a && s.pick() == a);
        }
        assert(s.pick() == a); // outer context restored, including its counter
        {   // Early return in the game: fewer calls than expected must not leak.
            ChromaLightScope inner(s, a, b, 2);
            assert(s.pick() == b);
        }
        assert(s.light.next_calls == 0 && s.pick() == a);
    }
    assert(!s.pick() && !s.light.current && !s.light.next);
    {   // Current has a colour, next does not: game colour for next, ours for current.
        ChromaLightScope scope(s, a, NULL, 2);
        assert(!s.pick() && !s.pick() && s.pick() == a && s.pick() == a);
    }
    {   // Next has a colour, current does not.
        ChromaLightScope scope(s, NULL, b, 2);
        assert(s.pick() == b && s.pick() == b && !s.pick() && !s.pick());
    }
    // Found by type and time (+-5 ms); the sameTypeIndex the game assigns is not used,
    // because its effect filters drop events and renumber the rest (HW0).
    const float* c = s.event_color(1, 1.004f, 1); assert(c && c[0] == 0.25f);
    assert(!s.event_color(1, 1.006f, 1) && !s.event_color(1, 1.5f, 1) && !s.event_color(4, 1.0f, 1) && !s.event_color(0, 2.0f, 5));
    // Two records at one time: the live value picks; a filter-rewritten value keeps the earliest colour.
    c = s.event_color(1, 2.0f, 5); assert(c && c[0] == 0.75f);
    c = s.event_color(1, 2.003f, 1); assert(c && c[0] == 0.5f);
    c = s.event_color(1, 1.998f, 3); assert(c && c[0] == 0.75f);
    c = s.event_color(1, 1.0f, 3); assert(c && c[0] == 0.25f);
    float nan = 0.0f; nan = nan / nan; assert(!s.event_color(1, nan, 1));
    // A miss never switches anything off: later events still colour.
    assert(s.event_color(1, 1.0f, 1));
    s.end(released);
    assert(live == 0 && !s.active && !s.chart.events && !s.pick() && !s.event_color(1, 1.0f, 1));
    // The next level starts clean.
    s.begin(chart());
    assert(s.event_color(1, 1.0f, 1) && !s.light.current);
    {   // Stage 3b: equal-time events are paired with records in order (one lamp each).
        ChromaEvent* e = s.chart.events;
        assert(s.record(1, 2.0f, 1, false) == &e[2] && s.record(1, 2.0f, 1, false) == &e[2] && !e[2].used);   // looking claims nothing
        assert(s.record(1, 2.0f, 1, true) == &e[2] && e[2].used);
        assert(s.record(1, 2.002f, 1, true) == &e[1] && e[1].used);   // no free record of that value: the first free one
        assert(s.record(1, 2.0f, 1, true) == &e[2] && s.record(1, 2.0f, 5, false) == &e[1]);   // all claimed: plain lookup
        assert(s.record(1, 1.0f, 3, true) == &e[0] && !s.record(1, 3.0f, 1, true) && !s.record(2, 2.0f, 1, true));
        assert(ChromaSession::color_of(&e[0]) == e[0].rgba);
        e[0].flags = 0; assert(!ChromaSession::color_of(&e[0]) && !ChromaSession::color_of(NULL)); e[0].flags = kChromaColor;
    }
    {   // Seven-arc rainbow shape (HW1, BeatSaver 55a43): same head, one colour each, file order.
        ChromaNote arcs[4]; memset(arcs, 0, sizeof(arcs));
        for (int i = 0; i < 3; ++i) { arcs[i].time = 5.0f; arcs[i].x = 2; arcs[i].kind = 1; arcs[i].rgba[0] = 0.1f * (float)(i + 1); }
        arcs[3].time = 5.0f; arcs[3].x = 3; arcs[3].kind = 1; arcs[3].rgba[0] = 0.9f;
        ChromaWall walls[2]; memset(walls, 0, sizeof(walls));
        for (int i = 0; i < 2; ++i) { walls[i].time = 6.0f; walls[i].x = -1; walls[i].w = 1; walls[i].h = 1; walls[i].duration = 2.0f; walls[i].rgba[1] = 0.5f * (float)(i + 1); }
        ChromaChart c; memset(&c, 0, sizeof(c)); c.notes = arcs; c.note_count = 4; c.walls = walls; c.wall_count = 2;
        assert(chroma_claim_note(c, 5.0f, 3, 0, 0, 1)->rgba[0] == 0.9f);     // a different head is independent
        assert(chroma_claim_note(c, 5.001f, 2, 0, 0, 1)->rgba[0] == 0.1f && chroma_claim_note(c, 4.999f, 2, 0, 0, 1)->rgba[0] == 0.2f);
        assert(chroma_claim_note(c, 5.0f, 2, 0, 0, 1)->rgba[0] == 0.3f);
        assert(chroma_claim_note(c, 5.0f, 2, 0, 0, 1)->rgba[0] == 0.1f);     // all claimed: the first repeats
        assert(!chroma_claim_note(c, 5.0f, 2, 0, 1, 1) && !chroma_claim_note(c, 5.0f, 2, 0, 0, 0) && !chroma_claim_note(c, 5.01f, 2, 0, 0, 1));
        assert(chroma_claim_wall(c, 6.0f, -1, 0, 1, 1, 2.0f)->rgba[1] == 0.5f && chroma_claim_wall(c, 6.0f, -1, 0, 1, 1, 2.0f)->rgba[1] == 1.0f);
        assert(!chroma_claim_wall(c, 6.0f, 4, 0, 1, 1, 2.0f) && !chroma_claim_wall(c, 6.0f, -1, 0, 1, 1, 2.5f));
    }
    s.end(released); s.end(released); // idempotent
    assert(live == 0);
    puts("chroma session ok");
    return 0;
}
