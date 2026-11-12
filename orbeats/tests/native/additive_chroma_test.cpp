// Production Chroma sidecar decoder under ASAN/UBSan. No game, no console.
// With argv[1] it also checks the reference file written by the Python encoder.
#include "additive_chroma.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string>
#include <vector>
using namespace BsdAdditive;
typedef std::vector<unsigned char> Bytes;
static const char kId[] = "bsd_additive_0123456789abcdef01234567";
static int live = 0, fail_at = -1, calls = 0;
static void* counted(size_t n) { if (calls++ == fail_at) return NULL; ++live; void* p = malloc(n); if (p) memset(p, 0xab, n); return p; }   // dirty, like reused heap
static void released(void* p) { if (p) { --live; free(p); } }

static void u8(Bytes& b, unsigned v) { b.push_back((unsigned char)v); }
static void u16(Bytes& b, unsigned v) { u8(b, v & 255); u8(b, v >> 8); }
static void u32(Bytes& b, uint32_t v) { for (int i = 0; i < 4; ++i) u8(b, (v >> (8 * i)) & 255); }
static void real(Bytes& b, float f) { uint32_t v; memcpy(&v, &f, 4); u32(b, v); }
static void color(Bytes& b, float r, float g, float bl, float a) { real(b, r); real(b, g); real(b, bl); real(b, a); }
static void event(Bytes& b, unsigned type, uint32_t index, float time, int32_t value, float r, const std::vector<unsigned>& lights = std::vector<unsigned>()) {
    u8(b, type); u32(b, index); real(b, time); u32(b, (uint32_t)value); color(b, r, 0.5f, 0.25f, 1); u16(b, (unsigned)lights.size());
    for (size_t i = 0; i < lights.size(); ++i) u16(b, lights[i]);
}
static void note(Bytes& b, float time, unsigned x, unsigned y, unsigned type, unsigned kind, float r) { real(b, time); u8(b, x); u8(b, y); u8(b, type); u8(b, kind); color(b, r, 0, 0, 1); }
static void wall(Bytes& b, float time, unsigned x, unsigned y, unsigned w, unsigned h, float duration, float r) { real(b, time); u8(b, x); u8(b, y); u8(b, w); u8(b, h); real(b, duration); color(b, r, 0, 0, 1); }
static Bytes seal(const Bytes& payload, char version = '1') {
    Bytes out; const char magic[] = "BSDACHR1"; out.assign(magic, magic + 8); out[7] = (unsigned char)version;
    u32(out, (uint32_t)payload.size()); uint64_t d = chroma_fnv(payload.data(), payload.size());
    u32(out, (uint32_t)d); u32(out, (uint32_t)(d >> 32)); out.insert(out.end(), payload.begin(), payload.end()); return out;
}
static void header(Bytes& p, uint32_t charts, const char* id = kId) { u32(p, (uint32_t)strlen(id)); p.insert(p.end(), id, id + strlen(id)); u32(p, charts); }
// Two charts: Standard/Expert with data, OneSaber/Easy empty.
static Bytes good() {
    Bytes p; header(p, 2);
    u8(p, 0); u8(p, 3);
    u32(p, 4);
    std::vector<unsigned> ids; ids.push_back(3); ids.push_back(500);
    event(p, 1, 1, 1.0f, 1, 1.0f); event(p, 1, 2, 1.0f, 5, 2.0f, ids); event(p, 1, 7, 9.0f, 0, 3.0f); event(p, 4, 1, 2.5f, 3, 16.0f);
    u32(p, 3); note(p, 1.0f, 1, 0, 0, 0, 0.1f); note(p, 1.0f, 2, 0, 1, 0, 0.2f); note(p, 3.0f, 2, 1, 1, 1, 0.3f);
    u32(p, 2); wall(p, 4.0f, 0, 0, 1, 5, 2.0f, 0.4f); wall(p, 5.0f, 0xff, 0, 1, 1, 2.0f, 0.6f);   // x = -1: decorative wall left of the lanes
    u8(p, 1); u8(p, 0); u32(p, 0); u32(p, 0); u32(p, 0);
    return p;
}
static ChromaDecode decode(const Bytes& file, ChromaChart& chart, uint8_t mode = 0, uint8_t difficulty = 3, const char* id = kId) {
    return chroma_decode(file.data(), file.size(), id, mode, difficulty, chart, counted, released);
}
static bool zeroed(const ChromaChart& c) { ChromaChart z; memset(&z, 0, sizeof(z)); return !memcmp(&c, &z, sizeof(z)); }
static void refused(const Bytes& payload, const char* why) {
    ChromaChart chart; int before = live;
    if (decode(seal(payload), chart) != ChromaInvalid) { fprintf(stderr, "accepted: %s\n", why); abort(); }
    assert(zeroed(chart) && live == before);
}
// Offsets inside good(): id text 4+37, charts 4, mode+difficulty 2, event count 4.
static const size_t kFirstEvent = 4 + 37 + 4 + 2 + 4;

static void accepts() {
    ChromaChart chart; Bytes file = seal(good());
    assert(decode(file, chart) == ChromaLoaded);
    assert(chart.event_count == 4 && chart.note_count == 3 && chart.wall_count == 2 && chart.light_id_count == 2);
    const ChromaEvent* e = chroma_find_event(chart, 1, 2);
    assert(e && e->value == 5 && e->time == 1.0f && e->rgba[0] == 2.0f && e->rgba[1] == 0.5f && e->rgba[3] == 1.0f);
    assert(e->lights == 2 && chart.light_ids[e->lights_at] == 3 && chart.light_ids[e->lights_at + 1] == 500);
    assert(chroma_find_event(chart, 1, 1)->lights == 0 && chroma_find_event(chart, 4, 1)->rgba[0] == 16.0f && chroma_find_event(chart, 1, 7)->value == 0);
    assert(!chroma_find_event(chart, 1, 3) && !chroma_find_event(chart, 0, 1) && !chroma_find_event(chart, 5, 1) && !chroma_find_event(chart, 4, 2));
    assert(chroma_event_matches(*e, 1.0049f, 5) && chroma_event_matches(*e, 0.9951f, 5));
    assert(!chroma_event_matches(*e, 1.0051f, 5) && !chroma_event_matches(*e, 0.9949f, 5) && !chroma_event_matches(*e, 1.0f, 4) && !chroma_event_matches(*e, NAN, 5));
    // Two notes share a time: position/colour pick the right one.
    assert(chroma_find_note(chart, 1.0f, 1, 0, 0, 0)->rgba[0] == 0.1f && chroma_find_note(chart, 1.004f, 2, 0, 1, 0)->rgba[0] == 0.2f);
    assert(chroma_find_note(chart, 3.0f, 2, 1, 1, 1)->rgba[0] == 0.3f);
    assert(!chroma_find_note(chart, 3.0f, 2, 1, 1, 0) && !chroma_find_note(chart, 1.0f, 1, 0, 1, 0) && !chroma_find_note(chart, 1.006f, 1, 0, 0, 0));
    assert(!chroma_find_note(chart, 1.0f, -1, 0, 0, 0) && !chroma_find_note(chart, NAN, 1, 0, 0, 0) && !chroma_find_note(chart, 99.0f, 1, 0, 0, 0));
    assert(chroma_find_wall(chart, 4.0f, 0, 0, 1, 5, 2.0f)->rgba[0] == 0.4f);
    assert(chroma_find_wall(chart, 5.0f, -1, 0, 1, 1, 2.0f)->rgba[0] == 0.6f && !chroma_find_wall(chart, 5.0f, 255, 0, 1, 1, 2.0f));
    assert(!chroma_find_wall(chart, 4.0f, 0, 0, 1, 5, 2.1f) && !chroma_find_wall(chart, 4.0f, 1, 0, 1, 5, 2.0f) && !chroma_find_wall(chart, NAN, 0, 0, 1, 5, 2.0f));
    chroma_free(chart, released); assert(zeroed(chart) && live == 0);
    // Present but empty chart is a loaded, empty session; an absent one is NoChart.
    assert(decode(file, chart, 1, 0) == ChromaLoaded && !chart.event_count && !chroma_find_event(chart, 1, 1) && !chroma_find_note(chart, 1, 1, 0, 0, 0));
    chroma_free(chart, released); assert(live == 0);
    assert(decode(file, chart, 0, 4) == ChromaNoChart && zeroed(chart) && live == 0);
    assert(decode(file, chart, 2, 3) == ChromaNoChart && zeroed(chart));
}
static void refuses() {
    ChromaChart chart; const Bytes payload = good(); Bytes file = seal(payload);
    assert(chroma_decode(NULL, file.size(), kId, 0, 3, chart, counted, released) == ChromaInvalid);
    assert(chroma_decode(file.data(), file.size(), NULL, 0, 3, chart, counted, released) == ChromaInvalid);
    assert(chroma_decode(file.data(), file.size(), kId, 0, 3, chart, NULL, released) == ChromaInvalid);
    assert(chroma_decode(file.data(), file.size(), kId, 0, 3, chart, counted, NULL) == ChromaInvalid);
    assert(decode(file, chart, 0, 3, "bsd_additive_0123456789abcdef01234568") == ChromaInvalid && zeroed(chart));
    assert(decode(file, chart, 0, 3, "bsd_additive_0123456789abcdef0123456") == ChromaInvalid);
    Bytes bad = file; bad[0] ^= 1; assert(decode(bad, chart) == ChromaInvalid);                  // magic
    bad = file; bad[8] ^= 1; assert(decode(bad, chart) == ChromaInvalid);                       // length
    bad = file; bad[12] ^= 1; assert(decode(bad, chart) == ChromaInvalid);                      // digest
    bad = file; bad.back() ^= 1; assert(decode(bad, chart) == ChromaInvalid);                   // payload vs digest
    bad = file; bad.push_back(0); assert(decode(bad, chart) == ChromaInvalid);                  // trailing byte, stale length
    // Every truncation, including ones cut exactly at a record boundary.
    for (size_t n = 0; n < file.size(); ++n) { Bytes cut(file.begin(), file.begin() + n); assert(decode(cut, chart) == ChromaInvalid && zeroed(chart)); }
    for (size_t n = 0; n < payload.size(); ++n) refused(Bytes(payload.begin(), payload.begin() + n), "resealed truncation");
    Bytes p = payload; u8(p, 0); refused(p, "sealed trailing byte");
    p.clear(); header(p, 0); refused(p, "zero charts");
    p.clear(); header(p, 26); for (int i = 0; i < 26; ++i) { u8(p, i % 5); u8(p, (i / 5) % 5); u32(p, 0); u32(p, 0); u32(p, 0); } refused(p, "26 charts");
    p.clear(); header(p, 2); for (int i = 0; i < 2; ++i) { u8(p, 0); u8(p, 3); u32(p, 0); u32(p, 0); u32(p, 0); } refused(p, "duplicate chart");
    p.clear(); header(p, 1); u8(p, 5); u8(p, 0); u32(p, 0); u32(p, 0); u32(p, 0); refused(p, "mode 5");
    p.clear(); header(p, 1); u8(p, 0); u8(p, 5); u32(p, 0); u32(p, 0); u32(p, 0); refused(p, "difficulty 5");
    p.clear(); header(p, 1); u8(p, 0); u8(p, 3); u32(p, 200001); refused(p, "event count");
    p.clear(); header(p, 1); u8(p, 0); u8(p, 3); u32(p, 0); u32(p, 200001); refused(p, "note count");
    p.clear(); header(p, 1); u8(p, 0); u8(p, 3); u32(p, 0); u32(p, 0); u32(p, 0xffffffffu); refused(p, "wall count");
    struct Case { const char* why; void (*write)(Bytes&); uint32_t count; };
    static const Case events[] = {
        {"index 0", [](Bytes& b) { event(b, 1, 0, 1, 1, 1); }, 1},
        {"index repeat", [](Bytes& b) { event(b, 1, 2, 1, 1, 1); event(b, 1, 2, 1, 1, 1); }, 2},
        {"index back", [](Bytes& b) { event(b, 1, 2, 1, 1, 1); event(b, 1, 1, 1, 1, 1); }, 2},
        {"type back", [](Bytes& b) { event(b, 2, 1, 1, 1, 1); event(b, 1, 1, 1, 1, 1); }, 2},
        {"time back", [](Bytes& b) { event(b, 1, 1, 2, 1, 1); event(b, 1, 2, 1, 1, 1); }, 2},
        {"negative time", [](Bytes& b) { event(b, 1, 1, -0.5f, 1, 1); }, 1},
        {"nan time", [](Bytes& b) { event(b, 1, 1, NAN, 1, 1); }, 1},
        {"inf time", [](Bytes& b) { event(b, 1, 1, INFINITY, 1, 1); }, 1},
        {"late time", [](Bytes& b) { event(b, 1, 1, 3600.5f, 1, 1); }, 1},
        {"colour 16.5", [](Bytes& b) { event(b, 1, 1, 1, 1, 16.5f); }, 1},
        {"colour negative", [](Bytes& b) { event(b, 1, 1, 1, 1, -0.01f); }, 1},
        {"colour nan", [](Bytes& b) { event(b, 1, 1, 1, 1, NAN); }, 1},
        {"too many lights", [](Bytes& b) { event(b, 1, 1, 1, 1, 1, std::vector<unsigned>(1025, 1)); }, 1},
    };
    for (size_t i = 0; i < sizeof(events) / sizeof(events[0]); ++i) {
        Bytes body; events[i].write(body); p.clear(); header(p, 1); u8(p, 0); u8(p, 3);
        u32(p, events[i].count); p.insert(p.end(), body.begin(), body.end()); u32(p, 0); u32(p, 0);
        refused(p, events[i].why);
    }
    static const Case notes[] = {
        {"note colour type", [](Bytes& b) { u32(b, 1); note(b, 1, 0, 0, 2, 0, 1); }, 0},
        {"note kind", [](Bytes& b) { u32(b, 1); note(b, 1, 0, 0, 0, 3, 1); }, 0},
        {"note time back", [](Bytes& b) { u32(b, 2); note(b, 2, 0, 0, 0, 0, 1); note(b, 1, 0, 0, 0, 0, 1); }, 0},
        {"note colour", [](Bytes& b) { u32(b, 1); note(b, 1, 0, 0, 0, 0, 17); }, 0},
        {"note nan", [](Bytes& b) { u32(b, 1); note(b, NAN, 0, 0, 0, 0, 1); }, 0},
    };
    for (size_t i = 0; i < sizeof(notes) / sizeof(notes[0]); ++i) { p.clear(); header(p, 1); u8(p, 0); u8(p, 3); u32(p, 0); notes[i].write(p); u32(p, 0); refused(p, notes[i].why); }
    static const Case walls[] = {
        {"wall time back", [](Bytes& b) { u32(b, 2); wall(b, 2, 0, 0, 1, 1, 1, 1); wall(b, 1, 0, 0, 1, 1, 1, 1); }, 0},
        {"wall duration nan", [](Bytes& b) { u32(b, 1); wall(b, 1, 0, 0, 1, 1, NAN, 1); }, 0},
        {"wall colour", [](Bytes& b) { u32(b, 1); wall(b, 1, 0, 0, 1, 1, 1, -1); }, 0},
    };
    for (size_t i = 0; i < sizeof(walls) / sizeof(walls[0]); ++i) { p.clear(); header(p, 1); u8(p, 0); u8(p, 3); u32(p, 0); u32(p, 0); walls[i].write(p); refused(p, walls[i].why); }
    // The unselected chart is validated too.
    p.clear(); header(p, 2); u8(p, 0); u8(p, 3); u32(p, 0); u32(p, 0); u32(p, 0); u8(p, 1); u8(p, 0); u32(p, 1); event(p, 1, 0, 1, 1, 1); u32(p, 0); u32(p, 0);
    refused(p, "invalid other chart");
    // A flipped byte inside a record with a recomputed digest must not crash.
    for (size_t i = kFirstEvent; i < payload.size(); ++i) { p = payload; p[i] ^= 0x80; ChromaChart c; ChromaDecode r = decode(seal(p), c); if (r == ChromaLoaded) chroma_free(c, released); assert(live == 0); }
    // Every allocation failure releases what was already taken.
    for (int k = 0; k < 5; ++k) { calls = 0; fail_at = k; assert(decode(file, chart) == ChromaInvalid && zeroed(chart) && live == 0); }
    fail_at = -1;
    std::vector<unsigned char> huge(kChromaMaxBytes + 1, 0); memcpy(huge.data(), "BSDACHR1", 8);
    assert(chroma_decode(huge.data(), huge.size(), kId, 0, 3, chart, counted, released) == ChromaInvalid);
}

// ── v2: ring and laser parameters ───────────────────────────────────────────
struct E2 { unsigned type; uint32_t index; float time; int32_t value; unsigned flags; float r; int dir;
            float speed, step, prop, rot, sm, pm, spm; std::string name; float gf[4], gt[4], gs; unsigned ease; };
static E2 e2(unsigned type, uint32_t index, float time, int32_t value, unsigned flags) {
    E2 e = {type, index, time, value, flags, (flags & kChromaColor) ? 0.5f : 0.0f, 0, 0, 0, 0, 0, 1, 1, 1, "", {1, 0, 0, 1}, {0, 0, 1, 1}, 0.25f, 5};
    return e;
}
static void event2(Bytes& b, const E2& e) {
    u8(b, e.type); u32(b, e.index); real(b, e.time); u32(b, (uint32_t)e.value); u16(b, e.flags);
    if (e.flags & kChromaColor) color(b, e.r, 0.5f, 0.25f, 1); else color(b, e.r, 0, 0, 0);   // must be zeros without the flag
    u8(b, (unsigned)(e.dir & 255)); real(b, e.speed); real(b, e.step); real(b, e.prop); real(b, e.rot); real(b, e.sm); real(b, e.pm); real(b, e.spm);
    u8(b, (unsigned)e.name.size()); b.insert(b.end(), e.name.begin(), e.name.end());
    if (e.flags & kChromaGradient) { color(b, e.gf[0], e.gf[1], e.gf[2], e.gf[3]); color(b, e.gt[0], e.gt[1], e.gt[2], e.gt[3]); real(b, e.gs); u8(b, e.ease); }
    u16(b, 0);
}
static Bytes chart2(const std::vector<E2>& events) {
    Bytes p; header(p, 1); u8(p, 0); u8(p, 3); u32(p, (uint32_t)events.size());
    for (size_t i = 0; i < events.size(); ++i) event2(p, events[i]);
    u32(p, 0); u32(p, 0); return p;
}
static void v2() {
    std::vector<E2> ok;
    E2 ring = e2(8, 1, 1.0f, 0, kChromaDirection | kChromaStep | kChromaProp | kChromaSpeed | kChromaRotation | kChromaName | kChromaCounterSpin);
    ring.dir = 1; ring.step = 15; ring.prop = 2.5f; ring.speed = -3; ring.rot = 90; ring.sm = 2; ring.name = "bigtracklanerings";
    E2 zoom = e2(9, 1, 1.0f, 0, kChromaStep | kChromaSpeed); zoom.step = 0.5f; zoom.speed = 3;
    E2 laser = e2(12, 1, 2.0f, 4, kChromaColor | kChromaLock | kChromaSpeed | kChromaDirection); laser.speed = 7.5f; laser.dir = 0;
    E2 reset = e2(8, 2, 3.0f, 0, kChromaReset); reset.pm = 0; reset.spm = 100000;
    E2 gradient = e2(1, 1, 0.5f, 5, kChromaGradient);
    ok.push_back(gradient); ok.push_back(ring); ok.push_back(reset); ok.push_back(zoom); ok.push_back(laser);
    ChromaChart c; assert(decode(seal(chart2(ok), '2'), c) == ChromaLoaded && c.event_count == 5 && c.name_bytes == 17);
    const ChromaEvent* g = chroma_find_event(c, 1, 1);
    assert(g && g->flags == kChromaGradient && g->gradient_seconds == 0.25f && g->easing == 5 && g->gradient_from[0] == 1 && g->gradient_to[2] == 1 && g->rgba[3] == 0);
    assert(chroma_find_event(c, 8, 1)->gradient_seconds == 0 && chroma_find_event(c, 8, 1)->easing == 0);
    const ChromaEvent* r = chroma_find_event(c, 8, 1);
    assert(r && r->flags == ring.flags && r->direction == 1 && r->step == 15 && r->prop == 2.5f && r->speed == -3 && r->rotation == 90);
    assert(r->step_mult == 2 && r->prop_mult == 1 && r->speed_mult == 1 && r->name_len == 17 && !memcmp(c.names + r->name_at, "bigtracklanerings", 17));
    assert(r->rgba[0] == 0 && r->rgba[3] == 0);
    r = chroma_find_event(c, 8, 2); assert(r && r->flags == kChromaReset && r->prop_mult == 0 && r->speed_mult == 100000 && !r->name_len);
    r = chroma_find_event(c, 12, 1); assert(r && (r->flags & kChromaColor) && r->rgba[0] == 0.5f && r->speed == 7.5f && r->direction == 0);
    chroma_free(c, released); assert(live == 0);
    // A v1 file decodes as colour-only records with neutral multipliers.
    assert(decode(seal(good()), c) == ChromaLoaded && c.events[0].flags == kChromaColor && c.events[0].step_mult == 1 && !c.name_bytes);
    chroma_free(c, released);
    struct Bad { const char* why; void (*spoil)(E2&); };
    static const Bad bad[] = {
        {"unknown flag", [](E2& e) { e.flags |= 2048; }},
        {"gradient zero duration", [](E2& e) { e.flags |= kChromaGradient; e.gs = 0; }},
        {"gradient negative duration", [](E2& e) { e.flags |= kChromaGradient; e.gs = -1; }},
        {"gradient too long", [](E2& e) { e.flags |= kChromaGradient; e.gs = 60.5f; }},
        {"gradient nan", [](E2& e) { e.flags |= kChromaGradient; e.gs = NAN; }},
        {"gradient ease 0", [](E2& e) { e.flags |= kChromaGradient; e.ease = 0; }},
        {"gradient ease 32", [](E2& e) { e.flags |= kChromaGradient; e.ease = 32; }},
        {"gradient colour range", [](E2& e) { e.flags |= kChromaGradient; e.gt[1] = 17; }},
        {"gradient colour negative", [](E2& e) { e.flags |= kChromaGradient; e.gf[0] = -0.5f; }},
        {"direction 2", [](E2& e) { e.flags |= kChromaDirection; e.dir = 2; }},
        {"direction without flag", [](E2& e) { e.dir = 1; }},
        {"speed without flag", [](E2& e) { e.speed = 1; }},
        {"speed range", [](E2& e) { e.flags |= kChromaSpeed; e.speed = 100001; }},
        {"step nan", [](E2& e) { e.flags |= kChromaStep; e.step = NAN; }},
        {"prop range", [](E2& e) { e.flags |= kChromaProp; e.prop = -100001; }},
        {"rotation range", [](E2& e) { e.flags |= kChromaRotation; e.rot = 3601; }},
        {"multiplier negative", [](E2& e) { e.sm = -1; }},
        {"multiplier range", [](E2& e) { e.spm = 100001; }},
        {"multiplier inf", [](E2& e) { e.pm = INFINITY; }},
        {"name without flag", [](E2& e) { e.name = "rings"; }},
        {"flag without name", [](E2& e) { e.flags |= kChromaName; }},
        {"name upper case", [](E2& e) { e.flags |= kChromaName; e.name = "Rings"; }},
        {"name control byte", [](E2& e) { e.flags |= kChromaName; e.name = std::string("ri\x01gs"); }},
        {"name too long", [](E2& e) { e.flags |= kChromaName; e.name = std::string(33, 'a'); }},
        {"colour data without flag", [](E2& e) { e.r = 0.5f; }},
    };
    for (size_t i = 0; i < sizeof(bad) / sizeof(bad[0]); ++i) {
        E2 e = e2(8, 1, 1.0f, 0, kChromaStep); e.step = 1; bad[i].spoil(e);
        std::vector<E2> one(1, e); Bytes payload = chart2(one);
        ChromaChart chart; int before = live;
        if (decode(seal(payload, '2'), chart) != ChromaInvalid) { fprintf(stderr, "v2 accepted: %s\n", bad[i].why); abort(); }
        assert(zeroed(chart) && live == before);
    }
    // Truncated at every length, one flipped bit everywhere, every allocation failure.
    Bytes file = seal(chart2(ok), '2');
    for (size_t n = 0; n < file.size(); ++n) { ChromaChart t; assert(chroma_decode(file.data(), n, kId, 0, 3, t, counted, released) == ChromaInvalid && zeroed(t)); }
    Bytes payload = chart2(ok);
    for (size_t i = kFirstEvent; i < payload.size(); ++i) { Bytes q = payload; q[i] ^= 0x80; ChromaChart t; if (decode(seal(q, '2'), t) == ChromaLoaded) chroma_free(t, released); assert(live == 0); }
    for (int k = 0; k < 5; ++k) { calls = 0; fail_at = k; ChromaChart t; assert(decode(file, t) == ChromaInvalid && zeroed(t) && live == 0); }
    fail_at = -1;
    Bytes v3 = file; v3[7] = '3'; assert(decode(v3, c) == ChromaInvalid);
}
static bool rgba(const float* c, float r, float g, float b, float a) { return c[0] == r && c[1] == g && c[2] == b && c[3] == a; }
static void reference(const char* path) {
    FILE* f = fopen(path, "rb"); assert(f); Bytes file(1 << 16); size_t n = fread(file.data(), 1, file.size(), f); fclose(f); assert(n && n < file.size()); file.resize(n);
    ChromaChart c; assert(decode(file, c) == ChromaLoaded && c.event_count == 3 && c.note_count == 2 && c.wall_count == 1 && c.light_id_count == 0);
    const ChromaEvent* e = chroma_find_event(c, 1, 1); assert(e && chroma_event_matches(*e, 1.0f, 1) && rgba(e->rgba, 1, 0, 0, 1) && !e->lights);
    e = chroma_find_event(c, 1, 2); assert(e && chroma_event_matches(*e, 1.0f, 5) && rgba(e->rgba, 0, 1, 0, 0.5f));
    e = chroma_find_event(c, 4, 1); assert(e && chroma_event_matches(*e, 2.5f, 3) && rgba(e->rgba, 2, 2, 2, 1));
    // The plugin's live lookup (type + time, value as tie-break) on the host-written file.
    assert(chroma_find_event_at(c, 1, 1.0f, 5) == chroma_find_event(c, 1, 2) && chroma_find_event_at(c, 1, 1.004f, 1) == chroma_find_event(c, 1, 1));
    assert(chroma_find_event_at(c, 1, 1.0f, 7) == chroma_find_event(c, 1, 1) && chroma_find_event_at(c, 4, 2.5f, 0) == chroma_find_event(c, 4, 1));
    assert(!chroma_find_event_at(c, 1, 2.5f, 3) && !chroma_find_event_at(c, 4, 1.0f, 1) && !chroma_find_event_at(c, 0, 1.0f, 1));
    // Decoded records start unclaimed even in reused, dirty allocations.
    assert(!c.notes[0].used && !c.notes[1].used && !c.walls[0].used);
    assert(chroma_claim_note(c, 3.0f, 2, 1, 1, 1) == chroma_find_note(c, 3.0f, 2, 1, 1, 1) && c.notes[1].used);
    const ChromaNote* v = chroma_find_note(c, 1.0f, 1, 0, 0, 0); assert(v && rgba(v->rgba, 0, 0, 1, 1));
    v = chroma_find_note(c, 3.0f, 2, 1, 1, 1); assert(v && rgba(v->rgba, 1, 1, 0, 1));
    const ChromaWall* w = chroma_find_wall(c, 4.0f, 0, 0, 1, 5, 2.0f); assert(w && rgba(w->rgba, 1, 0, 1, 1));
    chroma_free(c, released); assert(live == 0);
    assert(decode(file, c, 0, 4) == ChromaNoChart);
}
// The level-2 reference written by tests/chroma_reference.py::reference_sidecar_v2.
static void reference_v2(const char* path) {
    FILE* f = fopen(path, "rb"); assert(f); Bytes file(1 << 16); size_t n = fread(file.data(), 1, file.size(), f); fclose(f); assert(n && n < file.size()); file.resize(n);
    ChromaChart c; assert(decode(file, c) == ChromaLoaded && c.event_count == 4 && c.name_bytes == 17);
    const ChromaEvent* e = chroma_find_event_at(c, 1, 0.5f, 5);
    assert(e && e->flags == kChromaGradient && e->gradient_seconds == 0.75f && e->easing == 6 && rgba(e->gradient_from, 1, 0, 0, 1) && rgba(e->gradient_to, 0, 0, 1, 0.5f));
    e = chroma_find_event(c, 8, 1);
    assert(e && e->flags == (kChromaDirection | kChromaStep | kChromaName) && e->direction == 1 && e->step == 12.5f && e->step_mult == 2.0f);
    assert(e->prop_mult == 1.0f && e->speed_mult == 1.0f && e->name_len == 17 && !memcmp(c.names + e->name_at, "bigtracklanerings", 17));
    e = chroma_find_event(c, 9, 1); assert(e && e->flags == (kChromaStep | kChromaSpeed) && e->step == 0.5f && e->speed == 3.0f);
    e = chroma_find_event_at(c, 12, 2.0f, 4);
    assert(e && e->flags == (kChromaColor | kChromaLock | kChromaSpeed | kChromaDirection) && e->speed == 7.25f && e->direction == 0 && rgba(e->rgba, 1, 0, 0, 1));
    chroma_free(c, released);
}
int main(int argc, char** argv) {
    accepts(); refuses(); v2(); assert(live == 0);
    if (argc > 1) reference(argv[1]);
    if (argc > 2) reference_v2(argv[2]);
    puts("chroma sidecar decoder ok");
    return 0;
}
