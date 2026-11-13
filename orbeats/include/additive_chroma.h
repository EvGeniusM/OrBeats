// Bounded Chroma sidecar ("BSDACHR1" colours, "BSDACHR2" colours + ring and
// laser parameters + light gradients, "BSDACHR3" = BSDACHR2 + a static
// environment block per chart, "BSDACHR4" = BSDACHR3 + environment copies):
// explicit little-endian decode, no struct mapping,
// whole-file validation before any chart is exposed. No SDK/game dependency so
// the exact production decoder runs under host sanitizers.
#pragma once
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include "additive_chroma_regex.h"
namespace BsdAdditive {
// 16 MiB: 34aa3 KDA is 9.7 MiB (four identical 40k-event light shows); the plugin reads it at exact size.
static const size_t kChromaMaxBytes = 16u << 20;
static const uint32_t kChromaMaxItems = 200000, kChromaMaxCharts = 25, kChromaMaxLights = 1024;
static const float kChromaTimeTolerance = 0.005f, kChromaMaxTime = 3600.0f, kChromaMaxColor = 16.0f;
// Mappers use 10000 for "instant" and multipliers up to 1e5 (BeatSaver 1dcf4, 1b6c2).
static const float kChromaMaxEffect = 100000.0f, kChromaMaxRotation = 3600.0f, kChromaMaxMult = 100000.0f;
static const uint8_t kChromaMaxName = 32;
// Light gradient (stage 4c): seconds, and the game's EaseType range Linear..InOutBounce.
static const float kChromaMaxGradient = 60.0f;
static const uint8_t kChromaEaseFirst = 1, kChromaEaseLast = 31;
// Which optional event parameters a record carries (v2; a v1 record is colour only).
enum ChromaFlag {
    kChromaColor = 1, kChromaDirection = 2, kChromaLock = 4, kChromaSpeed = 8, kChromaStep = 16, kChromaProp = 32,
    kChromaRotation = 64, kChromaReset = 128, kChromaCounterSpin = 256, kChromaName = 512, kChromaGradient = 1024,
    kChromaFlagMask = 2047
};

struct ChromaEvent {
    uint32_t index; float time; int32_t value; float rgba[4]; uint32_t lights_at; uint16_t lights; uint8_t type;
    uint16_t flags; int8_t direction; uint8_t name_len; uint32_t name_at;    // name: lower-case ASCII in ChromaChart::names
    float speed, step, prop, rotation, step_mult, prop_mult, speed_mult;  // unset values are 0; multipliers default to 1
    float gradient_from[4], gradient_to[4], gradient_seconds; uint8_t easing;   // kChromaGradient only; else 0
    uint8_t used;   // claimed by a live light event this level (stage 3b); the decoder leaves 0
};
// Stage 5a environment entry; vectors are Unity units/degrees (V2 positions already scaled).
static const uint32_t kChromaMaxEnvEntries = 2048; static const uint16_t kChromaMaxEnvId = 512; static const uint8_t kChromaMaxEnvName = 63;
static const float kChromaMaxEnvValue = 100000.0f;
enum ChromaEnvMethod { kEnvRegex, kEnvExact, kEnvContains, kEnvStartsWith, kEnvEndsWith, kEnvMethodCount };
enum ChromaEnvField { kEnvHasActive = 1, kEnvActive = 2, kEnvScale = 4, kEnvPosition = 8, kEnvLocalPosition = 16, kEnvRotation = 32,
                      kEnvLocalRotation = 64, kEnvDuplicate = 128, kEnvFieldMask = 127 };
// v4: kEnvDuplicate is followed by the copy count (Chroma "duplicate"), 1..kChromaMaxDuplicate.
static const uint8_t kChromaMaxDuplicate = 100;
struct ChromaEnvEntry { uint8_t method, fields, duplicate; uint16_t id_len; uint32_t id_at; float scale[3], position[3], local_position[3], rotation[3], local_rotation[3]; };
struct ChromaNote  { float time; float rgba[4]; uint8_t x, y, color_type, kind; uint8_t used; };   // used: claimed by a live spawn this level
struct ChromaWall  { float time, duration; float rgba[4]; int8_t x, y; uint8_t w, h; uint8_t used; };   // x/y signed: decorative walls sit outside the lanes (x = -1, 4)
struct ChromaChart {
    ChromaEvent* events; ChromaNote* notes; ChromaWall* walls; uint16_t* light_ids; char* names;
    uint32_t event_count, note_count, wall_count, light_id_count, name_bytes;
    ChromaEnvEntry* env; char* env_ids; uint32_t env_count, env_id_bytes;   // ids: ChromaEnvEntry::id_at/id_len, not terminated
    char env_name[kChromaMaxEnvName + 1];                                    // the map's environment scene; "" without a block
};
enum { kChromaCounts = 7 };   // events, notes, walls, light ids, name bytes, env entries, env id bytes
enum ChromaDecode { ChromaLoaded, ChromaNoChart, ChromaInvalid };

struct ChromaReader {
    const unsigned char* p; size_t n; bool ok;
    uint8_t u8() { if (n < 1) { ok = false; return 0; } --n; return *p++; }
    uint16_t u16() { if (n < 2) { ok = false; return 0; } uint16_t v = (uint16_t)(p[0] | (p[1] << 8)); p += 2; n -= 2; return v; }
    uint32_t u32() { if (n < 4) { ok = false; return 0; } uint32_t v = 0; for (int i = 0; i < 4; ++i) v |= uint32_t(p[i]) << (8 * i); p += 4; n -= 4; return v; }
    uint64_t u64() { uint64_t lo = u32(), hi = u32(); return lo | (hi << 32); }
    float real() { uint32_t v = u32(); float f; memcpy(&f, &v, 4); return f; }
    // Finite and inside [low, high]; NaN fails every comparison.
    float real(float low, float high) { float f = real(); if (!(f >= low && f <= high)) ok = false; return f; }
    void color(float* out) { for (int i = 0; i < 4; ++i) out[i] = real(0.0f, kChromaMaxColor); }
};
inline uint64_t chroma_fnv(const unsigned char* p, size_t n) {
    uint64_t h = UINT64_C(14695981039346656037);
    while (n--) h = (h ^ *p++) * UINT64_C(1099511628211);
    return h;
}
// Validates one chart. With fill != NULL also copies into its preallocated arrays.
// A v2 numeric parameter: inside [low, high] when its flag is set, exactly 0 otherwise.
inline float chroma_param(ChromaReader& r, uint16_t flags, uint16_t flag, float low, float high) {
    float f = r.real(); if (!((flags & flag) ? (f >= low && f <= high) : f == 0.0f)) r.ok = false; return f;
}
inline bool chroma_printable(const unsigned char* p, size_t n) { for (size_t i = 0; i < n; ++i) if (p[i] < 0x20 || p[i] > 0x7e) return false; return true; }
// BSDACHR3 environment block. Every Regex id must compile in the plugin's subset.
inline bool chroma_walk_env(ChromaReader& r, ChromaChart* fill, uint32_t& entries, uint32_t& id_total, int version) {
    static ChromaRegex scratch;   // decoder runs on one thread (level load); 13 KiB kept off the stack
    uint8_t name_len = r.u8(); if (!r.ok || name_len > kChromaMaxEnvName || name_len > r.n || !chroma_printable(r.p, name_len)) return false;
    if (fill) { memcpy(fill->env_name, r.p, name_len); fill->env_name[name_len] = 0; }
    r.p += name_len; r.n -= name_len;
    entries = r.u32(); id_total = 0;
    if (!r.ok || entries > kChromaMaxEnvEntries || (entries != 0) != (name_len != 0)) return false;
    for (uint32_t i = 0; i < entries; ++i) {
        ChromaEnvEntry e; memset(&e, 0, sizeof(e));
        e.method = r.u8(); e.id_len = r.u16(); e.id_at = id_total;
        if (!r.ok || e.method >= kEnvMethodCount || !e.id_len || e.id_len > kChromaMaxEnvId || e.id_len > r.n || !chroma_printable(r.p, e.id_len)) return false;
        if (e.method == kEnvRegex && !chroma_regex_compile((const char*)r.p, e.id_len, scratch)) return false;
        if (fill) memcpy(fill->env_ids + id_total, r.p, e.id_len);
        r.p += e.id_len; r.n -= e.id_len; id_total += e.id_len;
        e.fields = r.u8();
        const uint8_t mask = version >= 4 ? (uint8_t)(kEnvFieldMask | kEnvDuplicate) : (uint8_t)kEnvFieldMask;
        if (!r.ok || !e.fields || (e.fields & ~mask) || ((e.fields & kEnvActive) && !(e.fields & kEnvHasActive))) return false;
        if (e.fields & kEnvDuplicate) { e.duplicate = r.u8(); if (!r.ok || !e.duplicate || e.duplicate > kChromaMaxDuplicate) return false; }
        float* vectors[5] = {e.scale, e.position, e.local_position, e.rotation, e.local_rotation};
        for (int v = 0; v < 5; ++v) if (e.fields & (kEnvScale << v)) for (int k = 0; k < 3; ++k) vectors[v][k] = r.real(-kChromaMaxEnvValue, kChromaMaxEnvValue);
        if (!r.ok) return false;
        if (fill) fill->env[i] = e;
    }
    return true;
}
inline bool chroma_walk_chart(ChromaReader& r, ChromaChart* fill, uint32_t counts[kChromaCounts], int version) {
    uint32_t events = r.u32(); if (!r.ok || events > kChromaMaxItems) return false;
    uint32_t light_total = 0, name_total = 0; uint8_t last_type = 0; uint32_t last_index = 0; float last_time = 0;
    for (uint32_t i = 0; i < events; ++i) {
        ChromaEvent e; memset(&e, 0, sizeof(e)); e.type = r.u8(); e.index = r.u32(); e.time = r.real(0.0f, kChromaMaxTime); e.value = (int32_t)r.u32();
        e.flags = version == 1 ? (uint16_t)kChromaColor : r.u16();
        if (e.flags & ~kChromaFlagMask) return false;
        if (version == 1 || (e.flags & kChromaColor)) r.color(e.rgba);
        else for (int k = 0; k < 4; ++k) if (r.real() != 0.0f) r.ok = false;   // no colour: zeros
        e.step_mult = e.prop_mult = e.speed_mult = 1.0f;
        if (version >= 2) {
            e.direction = (int8_t)r.u8();
            if ((e.flags & kChromaDirection) ? (e.direction != 0 && e.direction != 1) : e.direction != 0) return false;
            e.speed = chroma_param(r, e.flags, kChromaSpeed, -kChromaMaxEffect, kChromaMaxEffect);
            e.step = chroma_param(r, e.flags, kChromaStep, -kChromaMaxEffect, kChromaMaxEffect);
            e.prop = chroma_param(r, e.flags, kChromaProp, -kChromaMaxEffect, kChromaMaxEffect);
            e.rotation = chroma_param(r, e.flags, kChromaRotation, -kChromaMaxRotation, kChromaMaxRotation);
            e.step_mult = r.real(0.0f, kChromaMaxMult); e.prop_mult = r.real(0.0f, kChromaMaxMult); e.speed_mult = r.real(0.0f, kChromaMaxMult);
            e.name_len = r.u8(); e.name_at = name_total;
            if (!r.ok || e.name_len > kChromaMaxName || (e.name_len != 0) != ((e.flags & kChromaName) != 0) || e.name_len > r.n) return false;
            for (uint8_t k = 0; k < e.name_len; ++k) {
                unsigned char c = r.p[k];
                if (c < 0x20 || c > 0x7e || (c >= 'A' && c <= 'Z')) return false;   // printable, already lower-case
                if (fill) fill->names[name_total + k] = (char)c;
            }
            r.p += e.name_len; r.n -= e.name_len; name_total += e.name_len;
            if (e.flags & kChromaGradient) {
                r.color(e.gradient_from); r.color(e.gradient_to);
                e.gradient_seconds = r.real(); e.easing = r.u8();
                if (!(e.gradient_seconds > 0.0f && e.gradient_seconds <= kChromaMaxGradient) || e.easing < kChromaEaseFirst || e.easing > kChromaEaseLast) return false;
            }
        }
        e.lights = r.u16(); e.lights_at = light_total;
        if (!r.ok || !e.index || e.lights > kChromaMaxLights) return false;
        // Strict (type, index) order; time may repeat but never go back within a type.
        if (i && (e.type < last_type || (e.type == last_type && (e.index <= last_index || e.time < last_time)))) return false;
        last_type = e.type; last_index = e.index; last_time = e.time;
        for (uint16_t k = 0; k < e.lights; ++k) {
            uint16_t id = r.u16(); if (!r.ok) return false;
            if (fill) fill->light_ids[light_total + k] = id;
        }
        light_total += e.lights;
        if (fill) fill->events[i] = e;
    }
    uint32_t notes = r.u32(); if (!r.ok || notes > kChromaMaxItems) return false;
    last_time = 0;
    for (uint32_t i = 0; i < notes; ++i) {
        ChromaNote v; v.used = 0; v.time = r.real(0.0f, kChromaMaxTime); v.x = r.u8(); v.y = r.u8(); v.color_type = r.u8(); v.kind = r.u8(); r.color(v.rgba);
        if (!r.ok || v.color_type > 1 || v.kind > 2 || v.time < last_time) return false;
        last_time = v.time; if (fill) fill->notes[i] = v;
    }
    uint32_t walls = r.u32(); if (!r.ok || walls > kChromaMaxItems) return false;
    last_time = 0;
    for (uint32_t i = 0; i < walls; ++i) {
        ChromaWall v; v.used = 0; v.time = r.real(0.0f, kChromaMaxTime); v.x = (int8_t)r.u8(); v.y = (int8_t)r.u8(); v.w = r.u8(); v.h = r.u8();
        v.duration = r.real(-kChromaMaxTime, kChromaMaxTime); r.color(v.rgba);
        if (!r.ok || v.time < last_time) return false;
        last_time = v.time; if (fill) fill->walls[i] = v;
    }
    counts[0] = events; counts[1] = notes; counts[2] = walls; counts[3] = light_total; counts[4] = name_total; counts[5] = counts[6] = 0;
    if (version >= 3 && !chroma_walk_env(r, fill, counts[5], counts[6], version)) return false;
    return true;
}
inline void chroma_free(ChromaChart& chart, void (*release)(void*)) {
    if (release) { release(chart.events); release(chart.notes); release(chart.walls); release(chart.light_ids); release(chart.names); release(chart.env); release(chart.env_ids); }
    memset(&chart, 0, sizeof(chart));
}
// Validates the WHOLE file, then copies only the requested chart. `out` is
// zeroed unless the result is ChromaLoaded.
inline ChromaDecode chroma_decode(const unsigned char* bytes, size_t size, const char* level_id, uint8_t mode, uint8_t difficulty,
                                  ChromaChart& out, void* (*allocate)(size_t), void (*release)(void*)) {
    memset(&out, 0, sizeof(out));
    if (!bytes || !level_id || !allocate || !release || size < 20 || size > kChromaMaxBytes || memcmp(bytes, "BSDACHR", 7) ||
        bytes[7] < '1' || bytes[7] > '4') return ChromaInvalid;
    const int version = bytes[7] - '0';
    ChromaReader r = {bytes + 8, size - 8, true};
    uint32_t length = r.u32(); uint64_t digest = r.u64();
    if (length != r.n || chroma_fnv(r.p, r.n) != digest) return ChromaInvalid;
    uint32_t id_size = r.u32(); size_t expected = strlen(level_id);
    if (!r.ok || !id_size || id_size != expected || id_size > r.n || memcmp(r.p, level_id, id_size)) return ChromaInvalid;
    r.p += id_size; r.n -= id_size;
    uint32_t charts = r.u32(); if (!r.ok || !charts || charts > kChromaMaxCharts) return ChromaInvalid;
    bool seen[5][5]; memset(seen, 0, sizeof(seen));
    ChromaReader chosen = {NULL, 0, false}; uint32_t chosen_counts[kChromaCounts] = {0, 0, 0, 0, 0, 0, 0};
    for (uint32_t c = 0; c < charts; ++c) {
        uint8_t m = r.u8(), d = r.u8();
        if (!r.ok || m > 4 || d > 4 || seen[m][d]) return ChromaInvalid;
        seen[m][d] = true;
        ChromaReader start = r; uint32_t counts[kChromaCounts];
        if (!chroma_walk_chart(r, NULL, counts, version)) return ChromaInvalid;
        if (m == mode && d == difficulty) { chosen = start; memcpy(chosen_counts, counts, sizeof(counts)); }
    }
    if (!r.ok || r.n) return ChromaInvalid;
    if (!chosen.ok) return ChromaNoChart;
    // +1 keeps zero-count arrays valid without relying on malloc(0).
    out.events = (ChromaEvent*)allocate(sizeof(ChromaEvent) * ((size_t)chosen_counts[0] + 1));
    out.notes = (ChromaNote*)allocate(sizeof(ChromaNote) * ((size_t)chosen_counts[1] + 1));
    out.walls = (ChromaWall*)allocate(sizeof(ChromaWall) * ((size_t)chosen_counts[2] + 1));
    out.light_ids = (uint16_t*)allocate(sizeof(uint16_t) * ((size_t)chosen_counts[3] + 1));
    out.names = (char*)allocate((size_t)chosen_counts[4] + 1);
    out.env = (ChromaEnvEntry*)allocate(sizeof(ChromaEnvEntry) * ((size_t)chosen_counts[5] + 1));
    out.env_ids = (char*)allocate((size_t)chosen_counts[6] + 1);
    uint32_t counts[kChromaCounts];
    if (!out.events || !out.notes || !out.walls || !out.light_ids || !out.names || !out.env || !out.env_ids ||
        !chroma_walk_chart(chosen, &out, counts, version)) {
        chroma_free(out, release); return ChromaInvalid;
    }
    out.event_count = counts[0]; out.note_count = counts[1]; out.wall_count = counts[2]; out.light_id_count = counts[3]; out.name_bytes = counts[4];
    out.env_count = counts[5]; out.env_id_bytes = counts[6];
    return ChromaLoaded;
}
inline bool chroma_near(float a, float b) { float d = a - b; return d >= -kChromaTimeTolerance && d <= kChromaTimeTolerance; }
inline const ChromaEvent* chroma_find_event(const ChromaChart& chart, uint8_t type, uint32_t index) {
    size_t low = 0, high = chart.event_count;
    while (low < high) {
        size_t mid = low + (high - low) / 2; const ChromaEvent& e = chart.events[mid];
        if (e.type < type || (e.type == type && e.index < index)) low = mid + 1; else high = mid;
    }
    return low < chart.event_count && chart.events[low].type == type && chart.events[low].index == index ? &chart.events[low] : NULL;
}
inline bool chroma_event_matches(const ChromaEvent& e, float time, int32_t value) { return e.value == value && chroma_near(e.time, time); }
// Live lookup used by the plugin. The game renumbers sameTypeIndex after its
// own event filters (hardware HW0, 2026-11-12: the strobe filter kept 200 of
// 785 events and rewrote some values), so events are found by type and time.
// Among same-type records within the tolerance the one with the live value
// wins, otherwise the earliest; a filter-rewritten event keeps its colour.
// Records are sorted by (type, index) and time never decreases within a type
// (the decoder enforces both), so the type range is time-sorted.
inline const ChromaEvent* chroma_find_event_at(const ChromaChart& chart, uint8_t type, float time, int32_t value) {
    if (!(time == time)) return NULL;
    const float floor = time - kChromaTimeTolerance;
    size_t low = 0, high = chart.event_count;
    while (low < high) {
        size_t mid = low + (high - low) / 2; const ChromaEvent& e = chart.events[mid];
        if (e.type < type || (e.type == type && e.time < floor)) low = mid + 1; else high = mid;
    }
    const ChromaEvent* first = NULL;
    for (size_t i = low; i < chart.event_count && chart.events[i].type == type && chart.events[i].time <= time + kChromaTimeTolerance; ++i) {
        if (chart.events[i].value == value) return &chart.events[i];
        if (!first) first = &chart.events[i];
    }
    return first;
}
// Stage 3b: equal-time light events of one type (one per lamp in lightID maps)
// are paired with records in order. The first record of the time window that is
// not claimed yet, preferring the same value; when every record of the window is
// claimed (a replayed event), the plain lookup above. `take` marks it claimed.
inline ChromaEvent* chroma_claim_event(ChromaChart& chart, uint8_t type, float time, int32_t value, bool take) {
    const ChromaEvent* found = chroma_find_event_at(chart, type, time, value);
    if (!found) return NULL;
    size_t i = 0;   // window start: the first record of this type in the window
    { size_t low = 0, high = chart.event_count; const float floor = time - kChromaTimeTolerance;
      while (low < high) { size_t mid = low + (high - low) / 2; const ChromaEvent& e = chart.events[mid];
          if (e.type < type || (e.type == type && e.time < floor)) low = mid + 1; else high = mid; }
      i = low; }
    ChromaEvent* same = NULL; ChromaEvent* other = NULL;
    for (; i < chart.event_count && chart.events[i].type == type && chart.events[i].time <= time + kChromaTimeTolerance; ++i) {
        ChromaEvent& e = chart.events[i];
        if (e.used) continue;
        if (e.value == value) { same = &e; break; }
        if (!other) other = &e;
    }
    ChromaEvent* pick = same ? same : other ? other : &chart.events[found - chart.events];
    if (take) pick->used = 1;
    return pick;
}
// First index whose time is >= time - tolerance (arrays are time-sorted).
template <class T> inline size_t chroma_lower_time(const T* items, size_t count, float time) {
    size_t low = 0, high = count; const float floor = time - kChromaTimeTolerance;
    while (low < high) { size_t mid = low + (high - low) / 2; if (items[mid].time < floor) low = mid + 1; else high = mid; }
    return low;
}
inline const ChromaNote* chroma_find_note(const ChromaChart& chart, float time, int x, int y, int color_type, int kind) {
    if (!(time == time)) return NULL;
    for (size_t i = chroma_lower_time(chart.notes, chart.note_count, time); i < chart.note_count && chart.notes[i].time <= time + kChromaTimeTolerance; ++i) {
        const ChromaNote& v = chart.notes[i];
        if (v.x == x && v.y == y && v.color_type == color_type && v.kind == kind) return &v;
    }
    return NULL;
}
inline const ChromaWall* chroma_find_wall(const ChromaChart& chart, float time, int x, int y, int w, int h, float duration) {
    if (!(time == time)) return NULL;
    for (size_t i = chroma_lower_time(chart.walls, chart.wall_count, time); i < chart.wall_count && chart.walls[i].time <= time + kChromaTimeTolerance; ++i) {
        const ChromaWall& v = chart.walls[i];
        if (v.x == x && v.y == y && v.w == w && v.h == h && chroma_near(v.duration, duration)) return &v;
    }
    return NULL;
}
// Live lookups used by the plugin. Several objects may share every key the game
// exposes (HW1, BeatSaver 55a43: seven arcs with one head, differing only in
// curvature, each with its own colour), so every spawn claims the next unused
// matching record in file order; the game spawns equal-time objects in that
// order. Once all are claimed (no new level was loaded) the first one repeats.
inline ChromaNote* chroma_claim_note(ChromaChart& chart, float time, int x, int y, int color_type, int kind) {
    if (!(time == time)) return NULL;
    ChromaNote* first = NULL;
    for (size_t i = chroma_lower_time(chart.notes, chart.note_count, time); i < chart.note_count && chart.notes[i].time <= time + kChromaTimeTolerance; ++i) {
        ChromaNote& v = chart.notes[i];
        if (v.x != x || v.y != y || v.color_type != color_type || v.kind != kind) continue;
        if (!v.used) { v.used = 1; return &v; }
        if (!first) first = &v;
    }
    return first;
}
inline ChromaWall* chroma_claim_wall(ChromaChart& chart, float time, int x, int y, int w, int h, float duration) {
    if (!(time == time)) return NULL;
    ChromaWall* first = NULL;
    for (size_t i = chroma_lower_time(chart.walls, chart.wall_count, time); i < chart.wall_count && chart.walls[i].time <= time + kChromaTimeTolerance; ++i) {
        ChromaWall& v = chart.walls[i];
        if (v.x != x || v.y != y || v.w != w || v.h != h || !chroma_near(v.duration, duration)) continue;
        if (!v.used) { v.used = 1; return &v; }
        if (!first) first = &v;
    }
    return first;
}
} // namespace
