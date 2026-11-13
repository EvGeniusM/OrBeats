// Chroma stage 5: environment hierarchy (5.0 dump, 5a active + transform, 5b copies).
// Pure helpers live here so they can be tested on the host; the IL2CPP walk is
// src/chroma_env.cpp. Ids follow Chroma's GameObjectInfo.FullID:
// "Scene.[i]Root.[j]Child", i = sibling index.
// See docs/plans/2026-11-12-chroma-stage5-environment-spec.md.
#pragma once
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include "additive_chroma.h"

namespace BsdAdditive {
struct Api;
struct PaletteApis;
static const size_t kEnvMaxObjects = 20000;   // a stock environment has a few thousand
static const size_t kEnvMaxDepth = 48;
static const size_t kEnvMaxPath = 1024;
static const size_t kEnvMaxChildren = 4096;   // per transform
// Flags per object: the object itself carries the component.
enum { kEnvRing = 1, kEnvBox = 2, kEnvLight = 4 };

// Chroma's scene rule: name contains "Environment" and not "Menu".
inline bool chroma_env_scene_wanted(const char* scene) {
    return scene && strstr(scene, "Environment") && !strstr(scene, "Menu");
}
// UTF-16 to printable ASCII; anything else becomes '?'. Always terminates.
inline size_t chroma_env_ascii(const uint16_t* chars, int32_t count, char* out, size_t size) {
    if (!out || !size) return 0;
    size_t n = 0;
    for (int32_t i = 0; chars && i < count && n + 1 < size; ++i) out[n++] = (chars[i] >= 32 && chars[i] <= 126) ? (char)chars[i] : '?';
    out[n] = 0; return n;
}
// Appends ".[index]name" (or "[index]name" when the path is empty) at `length`.
// Returns the new length, or 0 when it would not fit (path left unchanged).
inline size_t chroma_env_push(char* path, size_t capacity, size_t length, int32_t index, const char* name) {
    if (!path || !name || length >= capacity) return 0;
    int n = snprintf(path + length, capacity - length, "%s[%d]%s", length ? "." : "", (int)index, name);
    if (n < 0 || (size_t)n >= capacity - length) { path[length] = 0; return 0; }
    return length + (size_t)n;
}
// Length of the id column of a dump line (up to the first tab).
inline size_t chroma_env_id_length(const char* line) { size_t n = 0; while (line[n] && line[n] != '\t') ++n; return n; }

// Chroma's LookupID on one id. `re` must be the compiled pattern for kEnvRegex.
inline bool chroma_env_match(uint8_t method, const char* pattern, size_t plen, const ChromaRegex* re, const char* id, size_t len) {
    switch (method) {
    case kEnvRegex: return re && chroma_regex_search(*re, id, len);
    case kEnvExact: return len == plen && !memcmp(id, pattern, plen);
    case kEnvStartsWith: return len >= plen && !memcmp(id, pattern, plen);
    case kEnvEndsWith: return len >= plen && !memcmp(id + len - plen, pattern, plen);
    case kEnvContains:
        if (!plen) return true;
        for (size_t i = 0; i + plen <= len; ++i) if (id[i] == pattern[0] && !memcmp(id + i, pattern, plen)) return true;
        return false;
    default: return false;
    }
}
// Chroma always switches off GameObject.Find("/Environment/GradientBackground"):
// a scene root "Environment" with a direct child "GradientBackground".
static const char kEnvGradientBackground[] = "^[^.]+\\.\\[\\d+\\]Environment\\.\\[\\d+\\]GradientBackground$";

// What a transform change may touch on an object the game itself re-places.
// Rings rewrite their local position and rotation every frame (LateUpdateRing);
// parametric boxes their local scale and position (Refresh). 5a skips those
// parts (Chroma moves them through patches, stage 5b) and counts the skip.
inline uint8_t chroma_env_allowed(uint8_t fields, unsigned object_flags) {
    uint8_t out = fields;
    if (object_flags & kEnvRing) out &= (uint8_t)~(kEnvPosition | kEnvLocalPosition | kEnvRotation | kEnvLocalRotation);
    if (object_flags & kEnvBox) out &= (uint8_t)~(kEnvScale | kEnvPosition | kEnvLocalPosition);
    return out;
}

// Stage 5b copies (Chroma "duplicate"). Chroma stops above 100 found objects;
// the plugin skips that one entry. 512 copies per level at most.
static const unsigned kEnvMaxDuplicateFound = 100, kEnvMaxClones = 512;
enum ChromaCloneKind { kCloneRefused = 0, kClonePlain = 1, kCloneLightWithId = 2 };
// MonoBehaviour classes a copy may carry. Instantiate copies serialized fields
// only; a component with other [Inject] fields would run with nulls (2.04 dump,
// scratchpad/inject_check.py). LightWithIdMonoBehaviour's injected _lightManager
// is filled from the original by the plugin (kCloneLightWithId).
inline ChromaCloneKind chroma_env_clone_kind(const char* class_name) {
    // Parametric*: the NeonTube children BakedBloom / BoxLight (0.8089 dump v2, FitBeat and BigMirror).
    static const char* const plain[] = {"TubeBloomPrePassLight", "MaterialPropertyBlockColorSetter", "MaterialPropertyBlockController",
                                        "Parametric3SliceSpriteController", "ParametricBoxController"};
    static const char* const lights[] = {"TubeBloomPrePassLightWithId", "MaterialLightWithId", "InstancedMaterialLightWithId",
                                         "SpriteLightWithId", "UnityLightWithId", "BloomPrePassBackgroundLightWithId"};
    if (!class_name) return kCloneRefused;
    for (size_t i = 0; i < sizeof(plain) / sizeof(plain[0]); ++i) if (!strcmp(class_name, plain[i])) return kClonePlain;
    for (size_t i = 0; i < sizeof(lights) / sizeof(lights[0]); ++i) if (!strcmp(class_name, lights[i])) return kCloneLightWithId;
    return kCloneRefused;
}
// Appends "name" to a comma list ("A,B"); false (list unchanged) when it would not fit.
inline bool chroma_env_list_add(char* list, size_t capacity, const char* name) {
    if (!list || !name || !capacity) return false;
    size_t used = strlen(list), n = strlen(name), extra = used ? 1 : 0;
    if (used + extra + n + 1 > capacity) return false;
    if (extra) list[used] = ',';
    memcpy(list + used + extra, name, n + 1); return true;
}

// Growable list of dump lines with the objects behind them; sorted by id for the dump.
struct ChromaEnvLines {
    struct Ref { void* transform; void* object; unsigned flags; };
    char** items; Ref* refs; size_t count, capacity; bool full;
    ChromaEnvLines() : items(NULL), refs(NULL), count(0), capacity(0), full(false) {}
    ~ChromaEnvLines() { clear(); }
    void clear() { for (size_t i = 0; i < count; ++i) free(items[i]); free(items); free(refs); items = NULL; refs = NULL; count = capacity = 0; full = false; }
    bool add(const char* text, void* transform = NULL, void* object = NULL, unsigned flags = 0) {
        if (!text || count >= kEnvMaxObjects) { full = true; return false; }
        if (count == capacity) {
            size_t grown = capacity ? capacity * 2 : 256;
            char** bigger = (char**)realloc(items, grown * sizeof(char*));
            if (!bigger) { full = true; return false; }
            items = bigger;
            Ref* more = (Ref*)realloc(refs, grown * sizeof(Ref));
            if (!more) { full = true; return false; }
            refs = more; capacity = grown;
        }
        size_t n = strlen(text); char* copy = (char*)malloc(n + 1);
        if (!copy) { full = true; return false; }
        memcpy(copy, text, n + 1); items[count] = copy;
        refs[count].transform = transform; refs[count].object = object; refs[count].flags = flags; ++count; return true;
    }
    // Ordinal by the first column only (a tab ends the id), so a parent sorts before its children.
    static int compare(const char* a, const char* b) {
        const unsigned char* x = (const unsigned char*)a; const unsigned char* y = (const unsigned char*)b;
        for (;; ++x, ++y) {
            unsigned cx = *x == '\t' ? 0 : *x, cy = *y == '\t' ? 0 : *y;
            if (cx != cy) return cx < cy ? -1 : 1;
            if (!cx) return 0;
        }
    }
    // Shell sort (Ciura gaps): no libc qsort import in the plugin.
    void sort() {
        static const size_t gaps[] = {1750, 701, 301, 132, 57, 23, 10, 4, 1};
        for (size_t g = 0; g < sizeof(gaps) / sizeof(gaps[0]); ++g)
            for (size_t gap = gaps[g], i = gap; i < count; ++i) {
                char* item = items[i]; Ref ref = refs[i]; size_t j = i;
                for (; j >= gap && compare(items[j - gap], item) > 0; j -= gap) { items[j] = items[j - gap]; refs[j] = refs[j - gap]; }
                items[j] = item; refs[j] = ref;
            }
    }
private:
    ChromaEnvLines(const ChromaEnvLines&);
    ChromaEnvLines& operator=(const ChromaEnvLines&);
};

struct ChromaEnvHost {
    const Api* api;
    const PaletteApis* classes;   // filled by main.cpp; may still be empty, then nothing runs
    void (*log)(const char*);
};
// Walks the loaded environment scenes and writes `<folder><scene>.txt`.
// Returns the number of objects written (0 on any failure; reason in the log).
size_t chroma_env_dump(const ChromaEnvHost& host, const char* folder);
// Stage 5a/5b: switches off GradientBackground (every Chroma level, like Chroma)
// and, when `chart.env_name` is a loaded environment scene, applies the chart's
// entries in file order; a "duplicate" entry copies each found object and
// changes only the copies, whose ids later entries can find. Logs one summary
// line. False when nothing could run.
bool chroma_env_apply(const ChromaEnvHost& host, const ChromaChart& chart);
} // namespace BsdAdditive
