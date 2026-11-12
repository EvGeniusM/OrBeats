// Chroma levels 1-3 (colours; rings and lasers; light gradients) for CUSA12878 2.04.
// See docs: native-map-expansion.md, docs/plans/*chroma-stage4*.
//
// Rules this file keeps:
//  * every patched function is fingerprinted (FNV over its body) first; the
//    stage-4 effect hooks are optional: a mismatch there skips only them;
//  * no managed allocation and no managed calls except one: the name of a ring
//    spawner object is read once (UnityEngine.Object.get_name through
//    il2cpp_runtime_invoke, exceptions caught) for nameFilter/counterSpin;
//  * a light gradient calls one game function the game itself calls for every
//    transition event (TweeningManager.AddTweenToDataStructures, fingerprinted;
//    it may grow the manager's own lists, as it does for the game);
//  * otherwise only verified field reads and writes of plain numbers;
//  * without an active sidecar every hook goes straight to the game's code;
//  * a layout surprise at run time disables Chroma for the rest of the process.
// NOT hardware-verified until docs/map-hardware-test-matrix.md says so.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <orbis/libkernel.h>
#include <GoldHEN/Common.h>
#include "additive_chroma_runtime.h"
#include "additive_chroma_session.h"
#include "additive_chroma_trampoline.h"
#include "additive_chroma_effects.h"

namespace BsdAdditive {
namespace {
struct Color { float r, g, b, a; };   // returned in xmm0:xmm1, like UnityEngine.Color
struct BeatmapKey { void* characteristic; int32_t difficulty; uint32_t padding; void* level_id; };
struct CodeSpec { uint32_t rva, bytes; uint64_t hash; };
enum Hook { kLoad, kLoadAsync, kLight, kFade, kNormal, kHighlight, kNote, kSlider, kWall, kRegister,
            kRingRotation, kRingStep, kPairHandle, kPairUpdate, kSingle, kHookCount };
static const int kRequiredHooks = kRingRotation;   // the rest is optional (stage 4)
// FNV-1a 64 of the function bodies in the retail 2.04 image.
static const CodeSpec kCode[kHookCount] = {
    {0x1bf0b90, 0x570, UINT64_C(0xfc5cdabf7beff05a)},  // GameplayCoreSceneSetupData.LoadTransformedBeatmapData
    {0x1bf1150, 0x130, UINT64_C(0x5473b8467ee3f28a)},  // ...LoadTransformedBeatmapDataAsync
    {0x1c8c1e0, 0x3f0, UINT64_C(0xe724710be6b9c87c)},  // LightSwitchEventEffect.HandleColorChangeBeatmapEvent
    {0x1c8c5f0, 0x2a6, UINT64_C(0x47bd09ec3a713384)},  // ...CheckNextEventForFade
    {0x1c8c8a0, 0x0c0, UINT64_C(0xd377e123160cbf60)},  // ...GetNormalColor
    {0x1c8c960, 0x0c0, UINT64_C(0x61ce253cbbb9cbe0)},  // ...GetHighlightColor
    {0x1b677a0, 0x4b0, UINT64_C(0xdf606b4d2cc31803)},  // ColorNoteVisuals.HandleNoteControllerDidInit
    {0x1c53690, 0x900, UINT64_C(0x408f8971e76512ee)},  // SliderController.Init
    {0x1b75460, 0x5f0, UINT64_C(0x4e60231853be8869)},  // ObstacleController.Init
    {0x19a6d40, 0x100, UINT64_C(0xff7ddce918928b39)},  // LightWithIdManager.RegisterLight (diagnostic only)
    {0x1ca0390, 0x1d0, UINT64_C(0x1487c672282491e1)},  // TrackLaneRingsRotationEffectSpawner.HandleBeatmapEvent
    {0x1c9f9a0, 0x060, UINT64_C(0xf1a9cd326e31706f)},  // TrackLaneRingsPositionStepEffectSpawner.HandleBeatmapEvent
    {0x1c89dc0, 0x5f0, UINT64_C(0xa790f40f3c8092ed)},  // LightPairRotationEventEffect.HandleBeatmapEvent
    {0x1c8a3b0, 0x340, UINT64_C(0x9755db07d838cbf2)},  // LightPairRotationEventEffect.UpdateRotationData
    {0x1c8b9e0, 0x1f0, UINT64_C(0xfa4cfa4b8d0cb36b)},  // LightRotationEventEffect.HandleBeatmapEvent
};
// Called (never patched) for light gradients.
static const CodeSpec kAddTween = {0x2814200, 0x1b0, UINT64_C(0xd6cb1c5d610caa0a)};  // TweeningManager.AddTweenToDataStructures
static const char kFolder[] = "/data/GoldHEN/AFR/CUSA12878/additive/";
static const char kDiagnosticFlag[] = "/data/GoldHEN/AFR/CUSA12878/additive/chroma-diagnostic";
static const char* const kModes[5] = {"Standard", "OneSaber", "NoArrows", "90Degree", "360Degree"};
// ColorScheme: _saberAColor 0x34, _saberBColor 0x44, _obstaclesColor 0x54.
static const size_t kSaberColors = 0x34, kObstacleColor = 0x54;

static Detour g_detours[kHookCount];
static bool g_hooked[kHookCount];
static const Api* g_api = NULL;
static void (*g_log)(const char*) = NULL;
static ChromaSession g_session;
static bool g_installed = false, g_broken = false, g_diagnostic = false;
static unsigned char* g_page = NULL;            // relocated prefixes: +0 normal, +64 highlight, +128 async load
static const size_t kPageBytes = 4096;
static int g_diag_events = 0, g_diag_lights = 0;
// Classes whose field offsets were confirmed through the IL2CPP API.
static void *g_setup_class, *g_characteristic_class, *g_event_class, *g_manager_class, *g_scheme_class,
            *g_note_data_class, *g_slider_data_class, *g_obstacle_data_class, *g_light_manager_class,
            *g_ring_spawner_class, *g_ring_effect_class, *g_ring_manager_class, *g_ring_class, *g_ring_rotation_class,
            *g_ring_step_class, *g_pair_class, *g_rotation_data_class, *g_single_class;
static bool g_effects = false;   // stage-4 hooks armed
static bool g_gradients = false;   // stage 4c: AddTweenToDataStructures fingerprint matched
static uintptr_t g_base = 0;

static void say(const char* text) { if (g_log) g_log(text); }
static void broken(const char* what) {
    if (g_broken) return;
    g_broken = true; g_session.end(free);
    char line[160]; snprintf(line, sizeof(line), "[CHROMA] disabled for this run: unexpected %s", what); say(line);
}
struct FieldSpec { const char* name; size_t offset; };
// True when `klass` is exactly `name` and all fields sit where 2.04 has them.
// A matching name with different offsets is a layout surprise.
static bool confirm(void*& cache, void* klass, const char* name, const FieldSpec* fields, size_t count) {
    if (!klass || g_broken) return false;
    if (cache) return cache == klass;
    if (!named(*g_api, klass, name)) return false;
    for (size_t i = 0; i < count; ++i) if (!field(*g_api, klass, fields[i].name, fields[i].offset)) { broken(name); return false; }
    cache = klass; return true;
}
// Small per-class cache for "does this object keep `name` at `offset`?" (the
// owner may be a subclass, so the name is looked up through its parents).
struct OwnerCache { void* klass[8]; bool ok[8]; size_t count; };
static bool owner_has(OwnerCache& cache, void* object, const char* name, size_t offset) {
    if (!object || g_broken) return false;
    void* klass = g_api->object_get_class(object);
    if (!klass) return false;
    for (size_t i = 0; i < cache.count; ++i) if (cache.klass[i] == klass) return cache.ok[i];
    bool ok = field(*g_api, klass, name, offset) != NULL;
    if (cache.count < 8) { cache.klass[cache.count] = klass; cache.ok[cache.count++] = ok; }
    return ok;
}
template <class T> static T peek(void* object, size_t offset) { T value; memcpy(&value, (char*)object + offset, sizeof(T)); return value; }
template <class T> static void poke(void* object, size_t offset, T value) { memcpy((char*)object + offset, &value, sizeof(T)); }

// ── level start ─────────────────────────────────────────────────────────────
static bool ascii(void* text, char* out, size_t size) {
    if (!text) return false;
    int32_t count = g_api->string_length(text); const uint16_t* chars = g_api->string_chars(text);
    if (count <= 0 || (size_t)count >= size || !chars) return false;
    for (int32_t i = 0; i < count; ++i) { if (chars[i] < 32 || chars[i] > 126) return false; out[i] = (char)chars[i]; }
    out[count] = 0; return true;
}
static unsigned char* read_file(const char* path, size_t& size) {
    size = 0; int fd = sceKernelOpen(path, O_RDONLY, 0);
    if (fd < 0) return NULL;
    size_t capacity = 65536; unsigned char* bytes = (unsigned char*)malloc(capacity); int64_t n = 0;
    while (bytes && (n = sceKernelRead(fd, bytes + size, capacity - size)) > 0) {
        size += (size_t)n;
        if (size == capacity) {
            if (capacity > kChromaMaxBytes) { n = -1; break; }   // larger than any valid sidecar
            capacity *= 2; unsigned char* grown = (unsigned char*)realloc(bytes, capacity);
            if (!grown) { n = -1; break; }
            bytes = grown;
        }
    }
    sceKernelClose(fd);
    if (!bytes || n < 0) { free(bytes); size = 0; return NULL; }
    return bytes;
}
static void reset_effect_state();
// Per-level evidence for hardware runs: how often each stage-4 hook changed the
// game's values, and how often a record could not be applied (layout fallback).
struct EffectStats { unsigned ring, ring_filtered, ring_fallback, step, step_fallback, pair, pair_fallback, single, single_fallback,
                     gradient, gradient_off, gradient_fallback; bool first[5]; };
static EffectStats g_stats; static char g_level[40];
static void report_effects() {
    const EffectStats& t = g_stats;
    if (!g_level[0] || !(t.ring | t.ring_filtered | t.ring_fallback | t.step | t.step_fallback | t.pair | t.pair_fallback | t.single | t.single_fallback |
                         t.gradient | t.gradient_off | t.gradient_fallback)) return;
    char line[320];
    snprintf(line, sizeof(line), "[CHROMA] level %s effects applied: ring rotation %u (name-filtered %u), ring zoom %u, paired lasers %u, single lasers %u, "
             "gradients %u (on off events %u); not applied (layout fallback): rotation %u, zoom %u, paired %u, single %u, gradient %u", g_level, t.ring,
             t.ring_filtered, t.step, t.pair, t.single, t.gradient, t.gradient_off, t.ring_fallback, t.step_fallback, t.pair_fallback, t.single_fallback,
             t.gradient_fallback);
    say(line);
}
static void first_use(int kind, const char* text) {
    if (g_stats.first[kind]) return;
    g_stats.first[kind] = true;
    char line[224]; snprintf(line, sizeof(line), "[CHROMA] first %s", text); say(line);
}
static void start_level(void* setup) {
    report_effects(); memset(&g_stats, 0, sizeof(g_stats)); g_level[0] = 0;
    g_session.end(free); g_diag_events = 0; g_diag_lights = 0; reset_effect_state();
    if (g_broken || !setup) return;
    static const FieldSpec setup_fields[] = {{"beatmapKey", 0x10}};
    if (!confirm(g_setup_class, g_api->object_get_class(setup), "GameplayCoreSceneSetupData", setup_fields, 1)) return;
    BeatmapKey key = peek<BeatmapKey>(setup, 0x10);
    char id[40];
    if (!key.characteristic || key.difficulty < 0 || key.difficulty > 4 || !ascii(key.level_id, id, sizeof(id)) ||
        strlen(id) != 37 || strncmp(id, "bsd_additive_", 13)) return;   // not one of our levels
    static const FieldSpec characteristic_fields[] = {{"_serializedName", 0x30}};
    if (!confirm(g_characteristic_class, g_api->object_get_class(key.characteristic), "BeatmapCharacteristicSO", characteristic_fields, 1)) return;
    void* name = peek<void*>(key.characteristic, 0x30); int mode = -1;
    for (int i = 0; i < 5; ++i) if (ascii_equals(*g_api, name, kModes[i])) mode = i;
    char line[224];
    if (g_diagnostic) { snprintf(line, sizeof(line), "[CHROMA DIAG] level id=%s mode=%d difficulty=%d", id, mode, key.difficulty); say(line); return; }
    if (mode < 0) return;
    char path[160]; snprintf(path, sizeof(path), "%s%s-chroma.bin", kFolder, id);
    size_t size = 0; unsigned char* bytes = read_file(path, size);
    if (!bytes) return;   // no sidecar: an ordinary map
    ChromaChart chart; ChromaDecode result = chroma_decode(bytes, size, id, (uint8_t)mode, (uint8_t)key.difficulty, chart, malloc, free);
    free(bytes);
    if (result == ChromaInvalid) { snprintf(line, sizeof(line), "[CHROMA] id=%s sidecar refused (size=%lu); level runs without Chroma", id, (unsigned long)size); say(line); return; }
    if (result == ChromaNoChart) return;
    g_session.begin(chart); memcpy(g_level, id, sizeof(g_level) - 1); g_level[sizeof(g_level) - 1] = 0;
    snprintf(line, sizeof(line), "[CHROMA] id=%s mode=%s difficulty=%d events=%u notes=%u walls=%u", id, kModes[mode], key.difficulty,
             chart.event_count, chart.note_count, chart.wall_count);
    say(line);
}
static void load_hook(void* self, const void* method) {
    start_level(self);
    Detour_Stub(&g_detours[kLoad], void (*)(void*, const void*), self, method);
}
static void* load_async_hook(void* self, const void* method) {
    start_level(self);
    return ((void* (*)(void*, const void*))(g_page + 128))(self, method);
}

// ── lights ──────────────────────────────────────────────────────────────────
static bool basic_event(void* event) {
    static const FieldSpec fields[] = {{"<time>k__BackingField", 0x10}, {"<nextSameTypeEventData>k__BackingField", 0x28},
        {"<sameTypeIndex>k__BackingField", 0x30}, {"basicBeatmapEventType", 0x34}, {"value", 0x38}, {"floatValue", 0x3c}};
    return event && confirm(g_event_class, g_api->object_get_class(event), "BasicBeatmapEventData", fields, 6);
}
static const float* event_color(void* event) {
    if (!g_session.active || !basic_event(event)) return NULL;
    int32_t type = peek<int32_t>(event, 0x34);
    if (type < 0 || type > 255) return NULL;
    return g_session.event_color((uint8_t)type, peek<float>(event, 0x10), peek<int32_t>(event, 0x38));
}
static void apply_gradient(void* self, void* event);
static void light_hook(void* self, void* event, const void* method) {
    {
        ChromaLightScope scope(g_session, event_color(event), NULL, 0);
        Detour_Stub(&g_detours[kLight], void (*)(void*, void*, const void*), self, event, method);
    }
    if (g_gradients && g_session.active) apply_gradient(self, event);
}
// The game asks GetNormalColor for the NEXT same-type event twice, then for the
// current event (verified by this function's fingerprint).
static void fade_hook(void* self, void* event, const void* method) {
    const float* current = event_color(event);
    const float* next = g_session.active && basic_event(event) ? event_color(peek<void*>(event, 0x28)) : NULL;
    ChromaLightScope scope(g_session, current, next, 2);
    Detour_Stub(&g_detours[kFade], void (*)(void*, void*, const void*), self, event, method);
}
typedef Color (*Getter)(void*, int32_t, bool, const void*);
static Color normal_hook(void* self, int32_t value, bool boost, const void* method) {
    if (const float* c = g_session.pick()) { Color color = {c[0], c[1], c[2], c[3]}; return color; }
    return ((Getter)g_page)(self, value, boost, method);
}
static Color highlight_hook(void* self, int32_t value, bool boost, const void* method) {
    if (const float* c = g_session.pick()) { Color color = {c[0], c[1], c[2], c[3]}; return color; }
    return ((Getter)(g_page + 64))(self, value, boost, method);
}

// ── notes, arcs, walls ──────────────────────────────────────────────────────
// ColorScheme reached through the owner's injected ColorManager, or NULL.
static char* scheme_of(void* owner, OwnerCache& cache, size_t manager_offset) {
    if (!owner_has(cache, owner, "_colorManager", manager_offset)) return NULL;
    void* manager = peek<void*>(owner, manager_offset);
    static const FieldSpec manager_fields[] = {{"_colorScheme", 0x10}};
    if (!manager || !confirm(g_manager_class, g_api->object_get_class(manager), "ColorManager", manager_fields, 1)) return NULL;
    void* scheme = peek<void*>(manager, 0x10);
    static const FieldSpec scheme_fields[] = {{"_saberAColor", 0x34}, {"_saberBColor", 0x44}, {"_obstaclesColor", 0x54}};
    if (!scheme || !confirm(g_scheme_class, g_api->object_get_class(scheme), "ColorScheme", scheme_fields, 3)) return NULL;
    return (char*)scheme;
}
// Overwrites `count` consecutive Color fields for the lifetime of the object.
struct SchemeOverride {
    char* slot; float saved[8]; size_t bytes;
    SchemeOverride(char* scheme, size_t offset, size_t count, const float* rgba) : slot(scheme ? scheme + offset : NULL), bytes(count * 16) {
        if (!slot) return;
        memcpy(saved, slot, bytes);
        for (size_t i = 0; i < count; ++i) memcpy(slot + i * 16, rgba, 16);
    }
    ~SchemeOverride() { if (slot) memcpy(slot, saved, bytes); }
private:
    SchemeOverride(const SchemeOverride&);
    SchemeOverride& operator=(const SchemeOverride&);
};
static OwnerCache g_visuals_cache, g_controller_cache, g_slider_cache, g_wall_cache;
static void note_hook(void* self, void* controller, const void* method) {
    const ChromaNote* record = NULL;
    if (g_session.active && g_session.chart.note_count && owner_has(g_controller_cache, controller, "_noteData", 0x78)) {
        void* data = peek<void*>(controller, 0x78);
        static const FieldSpec fields[] = {{"<time>k__BackingField", 0x10}, {"<colorType>k__BackingField", 0x30},
            {"<lineIndex>k__BackingField", 0x40}, {"<noteLineLayer>k__BackingField", 0x44}};
        if (data && confirm(g_note_data_class, g_api->object_get_class(data), "NoteData", fields, 4))
            record = chroma_claim_note(g_session.chart, peek<float>(data, 0x10), peek<int32_t>(data, 0x40), peek<int32_t>(data, 0x44), peek<int32_t>(data, 0x30), 0);
    }
    // Both saber colours: whichever the game picks for this note is ours.
    SchemeOverride colour(record ? scheme_of(self, g_visuals_cache, 0x48) : NULL, kSaberColors, 2, record ? record->rgba : NULL);
    Detour_Stub(&g_detours[kNote], void (*)(void*, void*, const void*), self, controller, method);
}
static void slider_hook(void* self, int32_t length_type, void* data, const void* spawn, float scale, float random, const void* method) {
    const ChromaNote* record = NULL;
    static const FieldSpec fields[] = {{"<time>k__BackingField", 0x10}, {"<colorType>k__BackingField", 0x28},
        {"<headLineIndex>k__BackingField", 0x38}, {"<headLineLayer>k__BackingField", 0x3c}};
    if (g_session.active && g_session.chart.note_count && data && confirm(g_slider_data_class, g_api->object_get_class(data), "SliderData", fields, 4))
        record = chroma_claim_note(g_session.chart, peek<float>(data, 0x10), peek<int32_t>(data, 0x38), peek<int32_t>(data, 0x3c), peek<int32_t>(data, 0x28), 1);
    SchemeOverride colour(record ? scheme_of(self, g_slider_cache, 0x58) : NULL, kSaberColors, 2, record ? record->rgba : NULL);
    Detour_Stub(&g_detours[kSlider], void (*)(void*, int32_t, void*, const void*, float, float, const void*), self, length_type, data, spawn, scale, random, method);
}
static void wall_hook(void* self, void* data, const void* spawn, const void* method) {
    const ChromaWall* record = NULL;
    static const FieldSpec fields[] = {{"<time>k__BackingField", 0x10}, {"<lineIndex>k__BackingField", 0x2c}, {"<lineLayer>k__BackingField", 0x30},
        {"<duration>k__BackingField", 0x34}, {"<width>k__BackingField", 0x38}, {"<height>k__BackingField", 0x3c}};
    if (g_session.active && g_session.chart.wall_count && data && confirm(g_obstacle_data_class, g_api->object_get_class(data), "ObstacleData", fields, 6))
        record = chroma_claim_wall(g_session.chart, peek<float>(data, 0x10), peek<int32_t>(data, 0x2c), peek<int32_t>(data, 0x30),
                                  peek<int32_t>(data, 0x38), peek<int32_t>(data, 0x3c), peek<float>(data, 0x34));
    SchemeOverride colour(record ? scheme_of(self, g_wall_cache, 0x60) : NULL, kObstacleColor, 1, record ? record->rgba : NULL);
    Detour_Stub(&g_detours[kWall], void (*)(void*, void*, const void*, const void*), self, data, spawn, method);
}


// ── rings and lasers (stage 4a/4b) ──────────────────────────────────────────
// Managed array: items at 0x20. List<T>: _items 0x10, _size 0x18.
static OwnerCache g_list_cache;
static bool list_ok(void* list) {
    return list && owner_has(g_list_cache, list, "_items", 0x10) && owner_has(g_list_cache, list, "_size", 0x18);
}
// Any record for this live event (colour or not), or NULL.
static const ChromaEvent* effect_record(void* event) {
    if (!g_session.active || !basic_event(event)) return NULL;
    int32_t type = peek<int32_t>(event, 0x34);
    if (type < 0 || type > 255) return NULL;
    return chroma_find_event_at(g_session.chart, (uint8_t)type, peek<float>(event, 0x10), peek<int32_t>(event, 0x38));
}
// Ring spawner names, read once per object for nameFilter/counterSpin.
struct NameEntry { void* object; bool ok; char name[48]; };
static NameEntry g_names[16]; static size_t g_name_count = 0;
static const char* object_name(void* object) {
    for (size_t i = 0; i < g_name_count; ++i) if (g_names[i].object == object) return g_names[i].ok ? g_names[i].name : NULL;
    if (g_name_count == 16) return NULL;
    NameEntry& entry = g_names[g_name_count++]; entry.object = object; entry.ok = false;
    void* klass = g_api->object_get_class(object);
    const void* method = klass ? g_api->class_get_method_from_name(klass, "get_name", 0) : NULL;   // searches base classes
    void* exception = NULL;
    void* text = method ? g_api->runtime_invoke(method, object, NULL, &exception) : NULL;
    entry.ok = !exception && ascii(text, entry.name, sizeof(entry.name));
    if (!entry.ok) say("[CHROMA] ring object name unreadable; nameFilter/counterSpin ignored for it");
    return entry.ok ? entry.name : NULL;
}
struct SpinEntry { void* effect; bool spinning; };
static SpinEntry g_spins[32]; static size_t g_spin_count = 0;
static SpinEntry* spin_of(void* effect) {
    for (size_t i = 0; i < g_spin_count; ++i) if (g_spins[i].effect == effect) return &g_spins[i];
    if (g_spin_count == 32) return NULL;
    g_spins[g_spin_count].effect = effect; g_spins[g_spin_count].spinning = false;   // Start() leaves it switched off
    return &g_spins[g_spin_count++];
}
static void reset_effect_state() { g_name_count = 0; g_spin_count = 0; }

static void ring_rotation_hook(void* self, void* event, const void* method) {
    typedef void (*Fn)(void*, void*, const void*);
    const ChromaEvent* rec = effect_record(event);
    static const FieldSpec spawner_fields[] = {{"_trackLaneRingsRotationEffect", 0x20}, {"_rotation", 0x2c}, {"_rotationStep", 0x30},
        {"_rotationStepType", 0x34}, {"_rotationPropagationSpeed", 0x38}, {"_rotationFlexySpeed", 0x3c}};
    if (!rec || !chroma_has_ring_rotation(*rec)) { Detour_Stub(&g_detours[kRingRotation], Fn, self, event, method); return; }
    if (!confirm(g_ring_spawner_class, g_api->object_get_class(self), "TrackLaneRingsRotationEffectSpawner", spawner_fields, 6)) {
        ++g_stats.ring_fallback; Detour_Stub(&g_detours[kRingRotation], Fn, self, event, method); return;
    }
    const char* name = (rec->flags & (kChromaName | kChromaCounterSpin)) ? object_name(self) : NULL;
    if ((rec->flags & kChromaName) && name && !chroma_name_equals(g_session.chart.names + rec->name_at, rec->name_len, name))
        { ++g_stats.ring_filtered; return; }   // Chroma: a filtered-out spawner ignores the event
    void* effect = peek<void*>(self, 0x20);
    static const FieldSpec effect_fields[] = {{"_trackLaneRingsManager", 0x20}, {"_activeRingRotationEffects", 0x38}};
    static const FieldSpec manager_fields[] = {{"_rings", 0x40}};
    static const FieldSpec ring_fields[] = {{"_destRotZ", 0x28}};
    void* manager = effect && confirm(g_ring_effect_class, g_api->object_get_class(effect), "TrackLaneRingsRotationEffect", effect_fields, 2) ? peek<void*>(effect, 0x20) : NULL;
    void* rings = manager && confirm(g_ring_manager_class, g_api->object_get_class(manager), "TrackLaneRingsManager", manager_fields, 1) ? peek<void*>(manager, 0x40) : NULL;
    void* ring = rings && g_api->array_length(rings) > 0 ? peek<void*>(rings, 0x20) : NULL;
    void* list = effect ? peek<void*>(effect, 0x38) : NULL;
    if (!ring || !confirm(g_ring_class, g_api->object_get_class(ring), "TrackLaneRing", ring_fields, 1) || !list_ok(list)) {
        ++g_stats.ring_fallback; Detour_Stub(&g_detours[kRingRotation], Fn, self, event, method); return;
    }
    const float dest = peek<float>(ring, 0x28); const int32_t before = peek<int32_t>(list, 0x18);
    Detour_Stub(&g_detours[kRingRotation], Fn, self, event, method);
    void* items = peek<void*>(list, 0x10); const int32_t after = peek<int32_t>(list, 0x18);
    if (after != before + 1 || !items || (uintptr_t)after > g_api->array_length(items)) { ++g_stats.ring_fallback; return; }
    void* added = peek<void*>(items, 0x20 + (size_t)(after - 1) * 8);
    static const FieldSpec rotation_fields[] = {{"rotationAngle", 0x10}, {"rotationStep", 0x14}, {"rotationFlexySpeed", 0x18}, {"rotationPropagationSpeed", 0x1c}};
    if (!added || !confirm(g_ring_rotation_class, g_api->object_get_class(added), "RingRotationEffect", rotation_fields, 4)) { ++g_stats.ring_fallback; return; }
    ChromaRingIn in = {dest, peek<float>(added, 0x10), peek<float>(added, 0x14), peek<float>(self, 0x2c), peek<int32_t>(self, 0x38),
                       peek<float>(self, 0x3c), name ? chroma_name_has_big(name) : true};   // unreadable name: no counter-spin flip
    ChromaRingOut out = chroma_ring_rotation(*rec, in);
    poke<float>(added, 0x10, out.angle); poke<float>(added, 0x14, out.step); poke<float>(added, 0x18, out.flexy); poke<int32_t>(added, 0x1c, out.prop);
    ++g_stats.ring;
    char text[192]; snprintf(text, sizeof(text), "ring rotation: game angle %.1f step %.1f -> angle %.1f step %.1f prop %d flexy %.2f",
                             (double)in.game_angle, (double)in.game_step, (double)out.angle, (double)out.step, out.prop, (double)out.flexy);
    first_use(0, text);
}
static void ring_step_hook(void* self, void* event, const void* method) {
    typedef void (*Fn)(void*, void*, const void*);
    const ChromaEvent* rec = effect_record(event);
    static const FieldSpec fields[] = {{"_minPositionStep", 0x2c}, {"_maxPositionStep", 0x30}, {"_moveSpeed", 0x34}};
    if (!rec || !(rec->flags & (kChromaStep | kChromaSpeed))) { Detour_Stub(&g_detours[kRingStep], Fn, self, event, method); return; }
    if (!confirm(g_ring_step_class, g_api->object_get_class(self), "TrackLaneRingsPositionStepEffectSpawner", fields, 3)) {
        ++g_stats.step_fallback; Detour_Stub(&g_detours[kRingStep], Fn, self, event, method); return;
    }
    float saved[3]; memcpy(saved, (char*)self + 0x2c, sizeof(saved));
    if (rec->flags & kChromaStep) { poke<float>(self, 0x2c, rec->step); poke<float>(self, 0x30, rec->step); }
    if (rec->flags & kChromaSpeed) poke<float>(self, 0x34, rec->speed);
    Detour_Stub(&g_detours[kRingStep], Fn, self, event, method);
    memcpy((char*)self + 0x2c, saved, sizeof(saved));
    ++g_stats.step;
    char text[160]; snprintf(text, sizeof(text), "ring zoom: game step %.2f/%.2f speed %.2f -> step %.2f speed %.2f", (double)saved[0], (double)saved[1],
                             (double)saved[2], (double)((rec->flags & kChromaStep) ? rec->step : saved[0]), (double)((rec->flags & kChromaSpeed) ? rec->speed : saved[2]));
    first_use(1, text);
}
// Paired lasers: HandleBeatmapEvent says which event is running; its single
// UpdateRotationData call site (both sides) applies the record.
struct PairContext { const ChromaEvent* rec; bool left; };
static PairContext g_pair = {NULL, false};
static void pair_handle_hook(void* self, void* event, const void* method) {
    PairContext saved = g_pair; g_pair.rec = NULL;
    static const FieldSpec fields[] = {{"_eventL", 0x20}, {"_eventR", 0x24}};
    const ChromaEvent* rec = effect_record(event);
    const bool pair_ok = rec && (rec->flags & kChromaLaserFlags) && confirm(g_pair_class, g_api->object_get_class(self), "LightPairRotationEventEffect", fields, 2);
    if (rec && (rec->flags & kChromaLaserFlags) && !pair_ok) ++g_stats.pair_fallback;
    if (pair_ok) {
        const int32_t type = peek<int32_t>(event, 0x34);
        if (type == peek<int32_t>(self, 0x20) || type == peek<int32_t>(self, 0x24)) { g_pair.rec = rec; g_pair.left = type == peek<int32_t>(self, 0x20); }
    }
    Detour_Stub(&g_detours[kPairHandle], void (*)(void*, void*, const void*), self, event, method);
    g_pair = saved;
}
static void pair_update_hook(void* self, int32_t value, void* data, float offset, float direction, const void* method) {
    typedef void (*Fn)(void*, int32_t, void*, float, float, const void*);
    static const FieldSpec fields[] = {{"enabled", 0x10}, {"rotationSpeed", 0x14}};
    if (!g_pair.rec) { Detour_Stub(&g_detours[kPairUpdate], Fn, self, value, data, offset, direction, method); return; }
    if (!data || !confirm(g_rotation_data_class, g_api->object_get_class(data), "RotationData", fields, 2)) {
        ++g_stats.pair_fallback; Detour_Stub(&g_detours[kPairUpdate], Fn, self, value, data, offset, direction, method); return;
    }
    ChromaPairPlan plan = chroma_pair_plan(*g_pair.rec, value, g_pair.left, direction);
    if (plan.call_original) Detour_Stub(&g_detours[kPairUpdate], Fn, self, value, data, offset, plan.direction, method);
    if (plan.set_enabled) poke<uint8_t>(data, 0x10, plan.enabled ? 1 : 0);
    if (plan.set_speed) poke<float>(data, 0x14, plan.speed);
    ++g_stats.pair;
    char text[160]; snprintf(text, sizeof(text), "paired laser: value %d %s, game direction %.0f -> direction %.0f speed %.2f original %s",
                             value, g_pair.left ? "left" : "right", (double)direction, (double)plan.direction, (double)plan.speed, plan.call_original ? "yes" : "no");
    first_use(2, text);
}
static void single_hook(void* self, void* event, const void* method) {
    typedef void (*Fn)(void*, void*, const void*);
    static const FieldSpec fields[] = {{"_event", 0x20}, {"_rotationSpeed", 0x68}};
    if (!g_session.active || !basic_event(event)) { Detour_Stub(&g_detours[kSingle], Fn, self, event, method); return; }
    if (!confirm(g_single_class, g_api->object_get_class(self), "LightRotationEventEffect", fields, 2)) {
        const ChromaEvent* lost = effect_record(event);
        if (lost && (lost->flags & kChromaLaserFlags)) ++g_stats.single_fallback;
        Detour_Stub(&g_detours[kSingle], Fn, self, event, method); return;
    }
    const int32_t value = peek<int32_t>(event, 0x38);
    SpinEntry* spin = spin_of(self);
    const ChromaEvent* rec = effect_record(event);
    if (rec && (rec->flags & kChromaLaserFlags) && !spin) ++g_stats.single_fallback;   // more than 32 single lasers
    if (!rec || !(rec->flags & kChromaLaserFlags) || !spin) {
        Detour_Stub(&g_detours[kSingle], Fn, self, event, method);
        if (spin && value >= 0) spin->spinning = value > 0;   // the game's own on/off
        return;
    }
    ChromaSinglePlan plan = chroma_single_plan(*rec, value, peek<int32_t>(self, 0x20) == 12, spin->spinning);
    if (plan.call_original) Detour_Stub(&g_detours[kSingle], Fn, self, event, method);
    if (plan.set_speed) {
        float speed = plan.speed;
        if (plan.keep_game_sign && peek<float>(self, 0x68) < 0.0f) speed = -speed;
        poke<float>(self, 0x68, speed);
    }
    spin->spinning = plan.spinning_after;
    ++g_stats.single;
    char text[160]; snprintf(text, sizeof(text), "single laser: value %d -> speed %.2f original %s", value, (double)peek<float>(self, 0x68),
                             plan.call_original ? "yes" : "no");
    first_use(3, text);
}

// ── light gradients (stage 4c) ──────────────────────────────────────────────
// ColorTween (namespace Tweening) with its Tween / Tween<Color> fields. A
// different layout switches gradients off for the run; colours keep working.
static void* g_tween_class = NULL; static void* g_tween_manager_class = NULL; static void* g_switch_class = NULL;
static bool g_tween_bad = false;
static bool tweening_class(void* klass, const char* name) {
    const char* n = klass ? g_api->class_get_name(klass) : NULL; const char* ns = klass ? g_api->class_get_namespace(klass) : NULL;
    return n && ns && !strcmp(n, name) && !strcmp(ns, "Tweening");
}
static bool tween_ok(void* tween, void* manager) {
    if (!tween || !manager || g_tween_bad) return false;
    void* klass = g_api->object_get_class(tween); void* manager_class = g_api->object_get_class(manager);
    if (g_tween_class) return klass == g_tween_class && manager_class == g_tween_manager_class;
    static const FieldSpec fields[] = {{"_startTime", 0x2c}, {"_duration", 0x30}, {"_loop", 0x34}, {"_isStarted", 0x3c},
        {"_isKilled", 0x3d}, {"_easeType", 0x40}, {"fromValue", 0x48}, {"toValue", 0x58}};
    bool ok = tweening_class(klass, "ColorTween") && tweening_class(manager_class, "SongTimeTweeningManager");
    for (size_t i = 0; ok && i < sizeof(fields) / sizeof(fields[0]); ++i) ok = field(*g_api, klass, fields[i].name, fields[i].offset) != NULL;
    if (!ok) { g_tween_bad = true; say("[CHROMA] ColorTween layout differs from 2.04; light gradients off for this run, colours unchanged"); return false; }
    g_tween_class = klass; g_tween_manager_class = manager_class; return true;
}
static void apply_gradient(void* self, void* event) {
    const ChromaEvent* rec = effect_record(event);
    if (!rec || !(rec->flags & kChromaGradient)) return;
    ChromaGradientPlan plan = chroma_gradient_plan(*rec, peek<int32_t>(event, 0x38), peek<float>(event, 0x10));
    if (!plan.apply) { ++g_stats.gradient_off; return; }
    static const FieldSpec fields[] = {{"_tweeningManager", 0x80}, {"_colorTween", 0x90}};
    void* tween = NULL; void* manager = NULL;
    if (confirm(g_switch_class, g_api->object_get_class(self), "LightSwitchEventEffect", fields, 2)) { tween = peek<void*>(self, 0x90); manager = peek<void*>(self, 0x80); }
    if (!tween_ok(tween, manager)) { ++g_stats.gradient_fallback; return; }
    // Exactly the fields CheckNextEventForFade writes for a vanilla transition.
    memcpy((char*)tween + 0x48, plan.from, 16); memcpy((char*)tween + 0x58, plan.to, 16);
    poke<float>(tween, 0x2c, plan.start); poke<float>(tween, 0x30, plan.duration); poke<uint8_t>(tween, 0x34, 0);
    poke<int32_t>(tween, 0x40, plan.ease); poke<uint16_t>(tween, 0x3c, 0);   // _isStarted = _isKilled = false
    typedef bool (*AddTween)(void*, void*, void*, bool, const void*);
    ((AddTween)(g_base + kAddTween.rva))(manager, tween, self, false, NULL);
    ++g_stats.gradient;
    char text[200]; snprintf(text, sizeof(text), "light gradient: value %d, %.2f s, ease %d, from %.2f %.2f %.2f to %.2f %.2f %.2f", peek<int32_t>(event, 0x38),
                             (double)plan.duration, plan.ease, (double)plan.from[0], (double)plan.from[1], (double)plan.from[2], (double)plan.to[0],
                             (double)plan.to[1], (double)plan.to[2]);
    first_use(4, text);
}

// ── stage 3.0 diagnostic (log only; never changes a colour) ─────────────────
static void diagnostic_light_hook(void* self, void* event, const void* method) {
    if (g_diag_events < 200 && basic_event(event)) {
        char line[192]; ++g_diag_events;
        snprintf(line, sizeof(line), "[CHROMA DIAG] event type=%d index=%d time=%.4f value=%d float=%.3f", peek<int32_t>(event, 0x34),
                 peek<int32_t>(event, 0x30), (double)peek<float>(event, 0x10), peek<int32_t>(event, 0x38), (double)peek<float>(event, 0x3c));
        say(line);
    }
    Detour_Stub(&g_detours[kLight], void (*)(void*, void*, const void*), self, event, method);
}
// Managed array: max_length 0x18, items 0x20. List<T>: _items 0x10, _size 0x18.
static void diagnostic_register_hook(void* self, void* light, const void* method) {
    Detour_Stub(&g_detours[kRegister], void (*)(void*, void*, const void*), self, light, method);
    static const FieldSpec fields[] = {{"_lights", 0x28}};
    if (g_diag_lights >= 3000 || !self || !light || !confirm(g_light_manager_class, g_api->object_get_class(self), "LightWithIdManager", fields, 1)) return;
    void* groups = peek<void*>(self, 0x28); int group = -1, position = -1;
    uintptr_t count = groups ? g_api->array_length(groups) : 0;
    for (uintptr_t i = 0; i < count && i < 1024 && group < 0; ++i) {
        void* list = peek<void*>(groups, 0x20 + i * 8); if (!list) continue;
        void* items = peek<void*>(list, 0x10); int32_t size = peek<int32_t>(list, 0x18);
        if (!items || size <= 0 || (uintptr_t)size > g_api->array_length(items)) continue;
        if (peek<void*>(items, 0x20 + (size_t)(size - 1) * 8) == light) { group = (int)i; position = size - 1; }
    }
    void* klass = g_api->object_get_class(light); const char* name = klass ? g_api->class_get_name(klass) : NULL;
    char line[192]; ++g_diag_lights;
    snprintf(line, sizeof(line), "[CHROMA DIAG] light group=%d position=%d class=%s", group, position, name ? name : "?");
    say(line);
}

// ── install ─────────────────────────────────────────────────────────────────
static bool code_matches(uintptr_t base, const CodeSpec& spec) {
    const uintptr_t address = base + spec.rva; void *start = NULL, *end = NULL; int32_t prot = 0;
    if (base > UINTPTR_MAX - spec.rva || sceKernelQueryMemoryProtection((void*)address, &start, &end, &prot) < 0) return false;
    if ((prot & 5) != 5 || address < (uintptr_t)start || address > (uintptr_t)end || spec.bytes > (uintptr_t)end - address) return false;
    return chroma_fnv((const unsigned char*)address, spec.bytes) == spec.hash;
}
static bool hook(Hook which, uintptr_t base, void* replacement) {
    Detour_Construct(&g_detours[which], DetourMode_x64);
    if (!Detour_DetourFunction(&g_detours[which], base + kCode[which].rva, replacement)) return false;
    g_hooked[which] = true; return true;
}
static void unhook_all() {
    for (int i = kHookCount - 1; i >= 0; --i) if (g_hooked[i]) { Detour_RestoreFunction(&g_detours[i]); g_hooked[i] = false; }
}
} // namespace

bool chroma_install(const ChromaHost& host) {
    if (g_installed) return true;
    if (!host.base || !host.api || !host.log || !complete(*host.api)) return false;
    g_api = host.api; g_log = host.log; g_base = host.base;
    for (int i = 0; i < kRequiredHooks; ++i) if (!code_matches(host.base, kCode[i])) {
        char line[128]; snprintf(line, sizeof(line), "[CHROMA] refused: code fingerprint %d differs from 2.04 or is already patched; nothing changed", i);
        say(line); return false;
    }
    int flag = sceKernelOpen(kDiagnosticFlag, O_RDONLY, 0);
    g_diagnostic = flag >= 0; if (flag >= 0) sceKernelClose(flag);
    void* page = NULL;
    if (sceKernelMmap(NULL, kPageBytes, 7, 0x1002, -1, 0, &page) < 0 || !page) { say("[CHROMA] refused: trampoline page allocation failed; nothing changed"); return false; }
    g_page = (unsigned char*)page; memset(g_page, 0xcc, kPageBytes);
    const uintptr_t normal = host.base + kCode[kNormal].rva, highlight = host.base + kCode[kHighlight].rva, async = host.base + kCode[kLoadAsync].rva;
    bool ok = chroma_getter_trampoline((const unsigned char*)normal, normal, g_page, 64) &&
              chroma_getter_trampoline((const unsigned char*)highlight, highlight, g_page + 64, 64) &&
              chroma_load_trampoline((const unsigned char*)async, async, g_page + 128, 64);
    if (!ok) say("[CHROMA] refused: entry relocation rejected; nothing changed");
    // The SDK stub of a relocated entry is never executed; ours is used instead.
    ok = ok && hook(kLoad, host.base, (void*)load_hook) && hook(kLoadAsync, host.base, (void*)load_async_hook);
    if (g_diagnostic) ok = ok && hook(kLight, host.base, (void*)diagnostic_light_hook) && hook(kRegister, host.base, (void*)diagnostic_register_hook);
    else ok = ok && hook(kNormal, host.base, (void*)normal_hook) && hook(kHighlight, host.base, (void*)highlight_hook) &&
              hook(kFade, host.base, (void*)fade_hook) && hook(kLight, host.base, (void*)light_hook) &&
              hook(kNote, host.base, (void*)note_hook) && hook(kSlider, host.base, (void*)slider_hook) && hook(kWall, host.base, (void*)wall_hook);
    if (!ok) {
        unhook_all(); sceKernelMunmap(g_page, kPageBytes); g_page = NULL;
        say("[CHROMA] install failed; every Chroma hook restored"); return false;
    }
    // Stage 4 (rings, lasers): all five or none, and never in diagnostic mode.
    bool effects = !g_diagnostic;
    for (int i = kRequiredHooks; i < kHookCount && effects; ++i) if (!code_matches(host.base, kCode[i])) {
        char line[128]; snprintf(line, sizeof(line), "[CHROMA] ring/laser fingerprint %d differs; colours only", i); say(line); effects = false;
    }
    if (effects) {
        effects = hook(kRingRotation, host.base, (void*)ring_rotation_hook) && hook(kRingStep, host.base, (void*)ring_step_hook) &&
                  hook(kPairHandle, host.base, (void*)pair_handle_hook) && hook(kPairUpdate, host.base, (void*)pair_update_hook) &&
                  hook(kSingle, host.base, (void*)single_hook);
        if (!effects) {
            for (int i = kHookCount - 1; i >= kRequiredHooks; --i) if (g_hooked[i]) { Detour_RestoreFunction(&g_detours[i]); g_hooked[i] = false; }
            say("[CHROMA] ring/laser hooks failed and were restored; colours only");
        }
    }
    g_effects = effects;
    g_gradients = !g_diagnostic && code_matches(host.base, kAddTween);
    if (!g_diagnostic && !g_gradients) say("[CHROMA] tween fingerprint differs; light gradients off");
    g_installed = true;
    say(g_diagnostic ? "[CHROMA DIAG] diagnostic mode: logging light registration and events; colours unchanged"
                     : g_effects ? (g_gradients ? "[CHROMA] hooks armed: colours, rings, lasers, light gradients; gradients NOT hardware verification"
                                                : "[CHROMA] hooks armed: colours, rings, lasers; ring/laser parameters NOT hardware verification")
                                 : "[CHROMA] level-1 hooks armed (event, note, arc and wall colours); NOT hardware verification");
    return true;
}
void chroma_uninstall() {
    if (!g_installed) return;
    report_effects();
    unhook_all(); g_session.end(free); g_installed = false; g_gradients = false;
    // The page stays mapped: a game thread may still be inside a relocated entry.
}
} // namespace BsdAdditive
