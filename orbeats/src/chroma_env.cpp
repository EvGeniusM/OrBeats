// Chroma stage 5 for CUSA12878 2.04: the loaded environment hierarchy, read
// through public Unity methods (by name via the IL2CPP API, every managed
// exception caught).
//  * 5.0 dump (flag file chroma-env-dump): one text file per level, scene unchanged;
//  * 5a apply: GameObject.SetActive and Transform local scale / position /
//    euler angles for the entries of the level's sidecar, once per level;
//  * 5b copies: Object.Internal_CloneSingleWithParent for "duplicate" entries,
//    lights of the copy registered with the original's LightWithIdManager.
// Ids follow Chroma's GameObjectInfo; known deviation: a root's index is
// Transform.GetSiblingIndex (0 for scene roots on 2.04, HW8), not the
// position in Scene.GetRootGameObjects. Stock environments have one root.
// NOT hardware-verified until docs/map-hardware-test-matrix.md says so.
#include <fcntl.h>
#include <math.h>
#include <orbis/libkernel.h>
#include "additive_song_packs.h"
#include "additive_probe_level.h"
#include "additive_palette.h"
#include "additive_chroma_env.h"

namespace BsdAdditive {
namespace {
struct Methods {
    const void *transform, *scene, *active, *parent, *child_count, *child, *sibling, *game_object, *name,
               *scene_name, *root_count, *find_all, *components;
    const void *set_active, *set_scale, *set_local_position, *set_position, *set_local_euler, *set_euler;   // apply only
    const void *clone, *active_in_hierarchy, *destroy, *register_light;                                     // apply only (5b)
};
struct Mark { void* object; unsigned flags; };
// Shell sort by address and binary search: no libc qsort/bsearch imports.
static void sort_marks(Mark* marks, size_t count) {
    static const size_t gaps[] = {1750, 701, 301, 132, 57, 23, 10, 4, 1};
    for (size_t g = 0; g < sizeof(gaps) / sizeof(gaps[0]); ++g)
        for (size_t gap = gaps[g], i = gap; i < count; ++i) {
            Mark item = marks[i]; size_t j = i;
            for (; j >= gap && (uintptr_t)marks[j - gap].object > (uintptr_t)item.object; j -= gap) marks[j] = marks[j - gap];
            marks[j] = item;
        }
}
static const Mark* find_mark(const Mark* marks, size_t count, void* object) {
    size_t lo = 0, hi = count;
    while (lo < hi) {
        size_t mid = lo + (hi - lo) / 2;
        if ((uintptr_t)marks[mid].object < (uintptr_t)object) lo = mid + 1; else hi = mid;
    }
    return lo < count && marks[lo].object == object ? &marks[lo] : NULL;
}
struct Root { void* transform; int32_t handle, sibling; char scene[64]; };
static const size_t kMaxRoots = 4096, kMaxMarks = 8192;

struct Walk {
    const Api& a; const PaletteApis& c; void (*log)(const char*);
    Methods m; bool failed; const char* what;
    Mark* marks; size_t mark_count;
    ChromaEnvLines lines;
    void* mono_type;   // typeof(MonoBehaviour); the dump lists each object's classes when set
    void* light_manager;   // FieldInfo LightWithIdMonoBehaviour._lightManager (0x28), apply only
    bool with_components;
    Walk(const Api& api, const PaletteApis& classes, void (*l)(const char*))
        : a(api), c(classes), log(l), failed(false), what(""), marks(NULL), mark_count(0), mono_type(NULL), light_manager(NULL), with_components(false) { memset(&m, 0, sizeof(m)); }
    ~Walk() { free(marks); }

    void* call(const void* method, void* object, void** args, const char* label) {
        if (failed) return NULL;
        void* exception = NULL;
        void* result = a.runtime_invoke(method, object, args, &exception);
        if (exception) {   // "<method> (<exception class>)" for the log
            static char described[160];
            void* klass = a.object_get_class(exception); const char* kind = klass ? a.class_get_name(klass) : NULL;
            snprintf(described, sizeof(described), "%s (%s)", label, kind ? kind : "?");
            failed = true; what = described; return NULL;
        }
        return result;
    }
    // Boxed value types keep their payload after the 16-byte object header.
    template <class T> bool value(void* boxed, T& out) {
        if (!boxed) { failed = true; what = "null boxed value"; return false; }
        memcpy(&out, (char*)boxed + 0x10, sizeof(T)); return true;
    }
    bool text(void* string, char* out, size_t size) {
        if (!string) { out[0] = 0; return false; }
        chroma_env_ascii(a.string_chars(string), a.string_length(string), out, size); return true;
    }
    const void* method(void* klass, const char* name, int args) {
        const void* found = klass ? a.class_get_method_from_name(klass, name, args) : NULL;
        if (!found && !failed) { failed = true; what = name; }
        return found;
    }
    void* type_of(void* klass) {
        const void* type = klass ? c.class_get_type(klass) : NULL;
        return type ? c.type_get_object(type) : NULL;
    }
    // Every object of `klass`, assets and prefabs included (like Chroma).
    void* find_all(void* klass) {
        void* type = type_of(klass);
        if (!type) { failed = true; what = "type object"; return NULL; }
        void* args[1] = {type};
        return call(m.find_all, NULL, args, "Resources.FindObjectsOfTypeAll");
    }
    // MonoBehaviours of a GameObject (GetComponentsInChildren(type, true) when recursive): Component[].
    void* behaviours(void* game_object, bool recursive) {
        if (!mono_type || !game_object) { failed = true; what = "typeof(MonoBehaviour)"; return NULL; }
        bool typed = false, inactive = true, reverse = false;
        void* args[6] = {mono_type, &typed, &recursive, &inactive, &reverse, NULL};
        return call(m.components, game_object, args, "GameObject.GetComponentsInternal");
    }
    const char* class_of(void* object) {
        void* klass = object ? a.object_get_class(object) : NULL;
        const char* name = klass ? a.class_get_name(klass) : NULL;
        return name ? name : "?";
    }
    unsigned flags_of(void* game_object) {
        if (!mark_count) return 0;
        const Mark* hit = find_mark(marks, mark_count, game_object);
        return hit ? hit->flags : 0;
    }
    void mark(void* klass, unsigned flag) {
        void* found = klass ? find_all(klass) : NULL;
        if (!found) { failed = false; return; }   // optional: no flags of this kind
        uintptr_t n = a.array_length(found);
        for (uintptr_t i = 0; i < n && !failed; ++i) {
            void* component = *(void**)((char*)found + 0x20 + i * 8);
            void* owner = component ? call(m.game_object, component, NULL, "Component.get_gameObject") : NULL;
            if (!owner) continue;
            if (mark_count == kMaxMarks) return;
            if (!marks) { marks = (Mark*)malloc(kMaxMarks * sizeof(Mark)); if (!marks) return; }
            marks[mark_count].object = owner; marks[mark_count++].flags = flag;
        }
    }
    void merge_marks() {
        if (mark_count < 2) return;
        sort_marks(marks, mark_count);
        size_t out = 0;
        for (size_t i = 0; i < mark_count; ++i) {
            if (out && marks[out - 1].object == marks[i].object) marks[out - 1].flags |= marks[i].flags;
            else marks[out++] = marks[i];
        }
        mark_count = out;
    }
    // Writes this object, then its children (sibling index = GetChild index).
    void walk(void* transform, char* path, size_t length, size_t depth) {
        if (failed || lines.full) return;
        void* owner = call(m.game_object, transform, NULL, "Component.get_gameObject");
        bool active = false; value(call(m.active, owner, NULL, "GameObject.get_activeSelf"), active);
        if (failed) return;
        unsigned flags = flags_of(owner);
        static char classes[256];   // dump v2: the object's MonoBehaviour classes
        classes[0] = 0;
        if (with_components) {
            void* list = behaviours(owner, false);
            if (failed) return;
            uintptr_t n = list ? a.array_length(list) : 0;
            for (uintptr_t i = 0; i < n; ++i)
                if (!chroma_env_list_add(classes, sizeof(classes), class_of(*(void**)((char*)list + 0x20 + i * 8)))) { chroma_env_list_add(classes, sizeof(classes), "..."); break; }
        }
        static char line[kEnvMaxPath + 300];   // static: walk() recurses on the game's main thread
        snprintf(line, sizeof(line), "%s\t%d\t%s%s%s%s%s%s", path, active ? 1 : 0, flags & kEnvRing ? "R" : "", flags & kEnvBox ? "B" : "",
                 flags & kEnvLight ? "L" : "", flags ? "" : "-", with_components ? "\t" : "", with_components ? (classes[0] ? classes : "-") : "");
        lines.add(line, transform, owner, flags);
        int32_t count = 0; value(call(m.child_count, transform, NULL, "Transform.get_childCount"), count);
        if (failed || count <= 0) return;
        if (depth >= kEnvMaxDepth || (size_t)count > kEnvMaxChildren) { lines.add("# depth or child limit reached"); return; }
        for (int32_t i = 0; i < count && !failed && !lines.full; ++i) {
            void* args[1] = {&i};
            void* child = call(m.child, transform, args, "Transform.GetChild");
            if (!child) return;
            static char name[160]; text(call(m.name, child, NULL, "Object.get_name"), name, sizeof(name));
            size_t grown = chroma_env_push(path, kEnvMaxPath, length, i, name);
            if (!grown) { lines.add("# path too long"); continue; }
            walk(child, path, grown, depth + 1);
            path[length] = 0;
        }
    }
};
static void write_all(int fd, const char* text) { size_t n = strlen(text); while (n) { int64_t w = sceKernelWrite(fd, text, n); if (w <= 0) return; text += w; n -= (size_t)w; } }

// The whole hierarchy into w.lines (unsorted). False with the reason logged.
struct Collected { char scenes[256]; size_t roots, outside; bool roots_ok; };
static bool collect(Walk& w, Collected& out, bool apply) {
    const Api& a = w.a; const PaletteApis& c = w.c; void (*log)(const char*) = w.log;
    memset(&out, 0, sizeof(out));
    void* go_class = palette_class(a, c, "UnityEngine", "GameObject");
    void* tr_class = palette_class(a, c, "UnityEngine", "Transform");
    void* scene_class = palette_class(a, c, "UnityEngine.SceneManagement", "Scene");
    void* resources = palette_class(a, c, "UnityEngine", "Resources");
    Methods& m = w.m;
    m.transform = w.method(go_class, "get_transform", 0); m.scene = w.method(go_class, "get_scene", 0);
    m.active = w.method(go_class, "get_activeSelf", 0); m.parent = w.method(tr_class, "get_parent", 0);
    m.child_count = w.method(tr_class, "get_childCount", 0); m.child = w.method(tr_class, "GetChild", 1);
    m.sibling = w.method(tr_class, "GetSiblingIndex", 0); m.game_object = w.method(tr_class, "get_gameObject", 0);
    m.name = w.method(tr_class, "get_name", 0); m.scene_name = w.method(scene_class, "GetNameInternal", 1);
    m.root_count = w.method(scene_class, "GetRootCountInternal", 1); m.find_all = w.method(resources, "FindObjectsOfTypeAll", 1);
    // Optional (dump classes column, 5b copies): a missing one only disables that part.
    m.components = go_class ? a.class_get_method_from_name(go_class, "GetComponentsInternal", 6) : NULL;
    w.mono_type = m.components ? w.type_of(palette_class(a, c, "UnityEngine", "MonoBehaviour")) : NULL;
    if (!w.mono_type) w.with_components = false;
    if (apply) {
        void* object_class = palette_class(a, c, "UnityEngine", "Object");
        void* light_class = palette_class(a, c, "", "LightWithIdMonoBehaviour");
        m.clone = object_class ? a.class_get_method_from_name(object_class, "Internal_CloneSingleWithParent", 3) : NULL;
        m.destroy = object_class ? a.class_get_method_from_name(object_class, "Destroy", 1) : NULL;
        m.active_in_hierarchy = go_class ? a.class_get_method_from_name(go_class, "get_activeInHierarchy", 0) : NULL;
        m.register_light = light_class ? a.class_get_method_from_name(light_class, "RegisterLight", 0) : NULL;
        w.light_manager = light_class ? field(a, light_class, "_lightManager", 0x28) : NULL;
        m.set_active = w.method(go_class, "SetActive", 1); m.set_scale = w.method(tr_class, "set_localScale", 1);
        m.set_local_position = w.method(tr_class, "set_localPosition", 1); m.set_position = w.method(tr_class, "set_position", 1);
        m.set_local_euler = w.method(tr_class, "set_localEulerAngles", 1); m.set_euler = w.method(tr_class, "set_eulerAngles", 1);
    }
    char line[kEnvMaxPath + 160];
    if (w.failed) { snprintf(line, sizeof(line), "[CHROMA ENV] method %s not found; environment unchanged", w.what); log(line); return false; }

    // Flags: rings, parametric boxes, lights (each optional).
    w.mark(palette_class(a, c, "", "TrackLaneRing"), kEnvRing);
    w.mark(palette_class(a, c, "", "ParametricBoxController"), kEnvBox);
    w.mark(palette_class(a, c, "", "LightWithIdMonoBehaviour"), kEnvLight);
    w.mark(palette_class(a, c, "", "LightWithIds"), kEnvLight);
    w.merge_marks();

    // Roots of the environment scenes.
    void* all = w.find_all(go_class);
    if (!all) { snprintf(line, sizeof(line), "[CHROMA ENV] %s failed; environment unchanged", w.what); log(line); return false; }
    Root* roots = (Root*)malloc(kMaxRoots * sizeof(Root)); size_t root_count = 0;
    if (!roots) return false;
    uintptr_t total = a.array_length(all);
    for (uintptr_t i = 0; i < total && !w.failed && root_count < kMaxRoots; ++i) {
        void* go = *(void**)((char*)all + 0x20 + i * 8);
        if (!go) continue;
        void* tr = w.call(m.transform, go, NULL, "GameObject.get_transform");
        if (!tr || w.call(m.parent, tr, NULL, "Transform.get_parent") || w.failed) continue;
        int32_t handle = 0; if (!w.value(w.call(m.scene, go, NULL, "GameObject.get_scene"), handle)) break;
        if (!handle) continue;   // prefab or asset: no scene
        void* args[1] = {&handle};
        char scene[64];
        if (!w.text(w.call(m.scene_name, NULL, args, "Scene.GetNameInternal"), scene, sizeof(scene)) || !chroma_env_scene_wanted(scene)) continue;
        Root& r = roots[root_count++]; r.transform = tr; r.handle = handle; memcpy(r.scene, scene, sizeof(scene));
        w.value(w.call(m.sibling, tr, NULL, "Transform.GetSiblingIndex"), r.sibling);
    }
    if (w.failed || !root_count) {
        snprintf(line, sizeof(line), "[CHROMA ENV] %s; environment unchanged (objects %lu)", w.failed ? w.what : "no environment scene loaded", (unsigned long)total);
        log(line); free(roots); return false;
    }
    // Self-check: per scene, root sibling indices must be 0..count-1 like GetRootGameObjects.
    char* scenes = out.scenes; bool roots_ok = true;
    for (size_t i = 0; i < root_count; ++i) {
        bool first = true; size_t in_scene = 0;
        for (size_t j = 0; j < root_count; ++j) if (roots[j].handle == roots[i].handle) {
            if (j < i) first = false;
            ++in_scene;
            if (j != i && roots[j].sibling == roots[i].sibling) roots_ok = false;
        }
        if (roots[i].sibling < 0 || (size_t)roots[i].sibling >= in_scene) roots_ok = false;
        if (!first) continue;
        int32_t handle = roots[i].handle, expected = -1; void* args[1] = {&handle};
        w.value(w.call(m.root_count, NULL, args, "Scene.GetRootCountInternal"), expected);
        if (expected != (int32_t)in_scene) roots_ok = false;
        size_t used = strlen(scenes);
        if (used + strlen(roots[i].scene) + 2 < sizeof(out.scenes)) snprintf(scenes + used, sizeof(out.scenes) - used, "%s%s", used ? "+" : "", roots[i].scene);
    }
    char* path = (char*)malloc(kEnvMaxPath);
    for (size_t i = 0; path && i < root_count && !w.failed; ++i) {
        char name[160]; w.text(w.call(m.name, roots[i].transform, NULL, "Object.get_name"), name, sizeof(name));
        size_t length = strlen(roots[i].scene); memcpy(path, roots[i].scene, length + 1);
        length = chroma_env_push(path, kEnvMaxPath, length, roots[i].sibling, name);
        if (length) w.walk(roots[i].transform, path, length, 0);
    }
    // Rings that live outside the environment scenes (Chroma adds those too).
    size_t outside = 0;
    for (size_t k = 0; path && k < w.mark_count && !w.failed; ++k) {
        if (!(w.marks[k].flags & kEnvRing)) continue;
        void* go = w.marks[k].object;
        int32_t handle = 0; if (!w.value(w.call(m.scene, go, NULL, "GameObject.get_scene"), handle)) break;
        if (!handle) continue;
        void* args[1] = {&handle}; char scene[64];
        if (!w.text(w.call(m.scene_name, NULL, args, "Scene.GetNameInternal"), scene, sizeof(scene)) || !scene[0] || chroma_env_scene_wanted(scene)) continue;
        // Build the id from the root down: collect the chain first.
        void* chain[kEnvMaxDepth]; size_t depth = 0;
        void* tr = w.call(m.transform, go, NULL, "GameObject.get_transform");
        while (tr && depth < kEnvMaxDepth && !w.failed) { chain[depth++] = tr; tr = w.call(m.parent, tr, NULL, "Transform.get_parent"); }
        size_t length = strlen(scene); memcpy(path, scene, length + 1);
        for (size_t d = depth; d-- > 0 && length && !w.failed;) {
            int32_t sibling = -1; w.value(w.call(m.sibling, chain[d], NULL, "Transform.GetSiblingIndex"), sibling);
            char name[160]; w.text(w.call(m.name, chain[d], NULL, "Object.get_name"), name, sizeof(name));
            length = chroma_env_push(path, kEnvMaxPath, length, sibling, name);
        }
        if (length && depth) { w.walk(chain[0], path, length, 0); ++outside; }
    }
    free(path); free(roots);
    if (w.failed) { snprintf(line, sizeof(line), "[CHROMA ENV] %s threw; environment unchanged", w.what); log(line); return false; }
    out.roots = root_count; out.outside = outside; out.roots_ok = roots_ok;
    return true;
}
static bool lookup_ready(const ChromaEnvHost& host) {
    if (!host.api || !host.classes || !host.log) return false;
    const PaletteApis& c = *host.classes;
    if (!c.domain_get || !c.domain_get_assemblies || !c.assembly_get_image || !c.class_from_name || !c.class_get_type || !c.type_get_object) {
        host.log("[CHROMA ENV] class lookup APIs not ready; environment unchanged"); return false;
    }
    return true;
}
} // namespace

size_t chroma_env_dump(const ChromaEnvHost& host, const char* folder) {
    if (!folder || !lookup_ready(host)) return 0;
    Walk w(*host.api, *host.classes, host.log); Collected got;
    w.with_components = true;
    if (!collect(w, got, false)) return 0;
    w.lines.sort();
    const char* scenes = got.scenes; const size_t root_count = got.roots, outside = got.outside; const bool roots_ok = got.roots_ok;
    char line[kEnvMaxPath + 160];

    sceKernelMkdir(folder, 0777);
    char file[256]; snprintf(file, sizeof(file), "%s%s.txt", folder, scenes);
    int fd = sceKernelOpen(file, O_WRONLY | O_CREAT | O_TRUNC, 0666);
    if (fd < 0) { snprintf(line, sizeof(line), "[CHROMA ENV] cannot write %s", file); host.log(line); return 0; }
    snprintf(line, sizeof(line), "# OrBeats environment dump v2: scenes=%s objects=%lu roots=%lu root-order=%s outside-rings=%lu%s\n"
             "# columns: Chroma FullID, activeSelf, flags (R ring, B parametric box, L light with id), MonoBehaviour classes\n", scenes,
             (unsigned long)w.lines.count, (unsigned long)root_count, roots_ok ? "ok" : "MISMATCH", (unsigned long)outside, w.lines.full ? " TRUNCATED" : "");
    write_all(fd, line);
    for (size_t i = 0; i < w.lines.count; ++i) { write_all(fd, w.lines.items[i]); write_all(fd, "\n"); }
    sceKernelClose(fd);
    sceKernelChmod(file, 0644);   // the open mode is not applied on the console (HW8: files came out as ----------)
    snprintf(line, sizeof(line), "[CHROMA ENV] dumped %lu objects of %s (roots %lu, root order %s, rings outside %lu) to %s", (unsigned long)w.lines.count,
             scenes, (unsigned long)root_count, roots_ok ? "ok" : "MISMATCH", (unsigned long)outside, file);
    host.log(line);
    return w.lines.count;
}
namespace {
static ChromaRegex g_entry_regex, g_background_regex;
struct ApplyStats { unsigned entries, applied, not_found, objects, active, scale, position, rotation, skipped, background,
                    copies, copied, lights, refused, refusal_lines; };
// One entry on one object; returns false when a managed call threw.
static bool apply_one(Walk& w, const ChromaEnvEntry& e, const ChromaEnvLines::Ref& ref, ApplyStats& s) {
    const Methods& m = w.m;
    if (!ref.object || !ref.transform) return true;   // a "#" note line of the dump
    const uint8_t fields = chroma_env_allowed(e.fields, ref.flags);
    if (fields != e.fields) ++s.skipped;
    if (fields & kEnvHasActive) {
        bool value = (fields & kEnvActive) != 0; void* args[1] = {&value};
        w.call(m.set_active, ref.object, args, "GameObject.SetActive"); ++s.active;
    }
    // Chroma TransformData.Apply: scale, then local position or position, then local rotation or rotation.
    float vector[3]; void* args[1] = {vector};
    if (fields & kEnvScale) { memcpy(vector, e.scale, sizeof(vector)); w.call(m.set_scale, ref.transform, args, "Transform.set_localScale"); ++s.scale; }
    if (fields & kEnvLocalPosition) { memcpy(vector, e.local_position, sizeof(vector)); w.call(m.set_local_position, ref.transform, args, "Transform.set_localPosition"); ++s.position; }
    else if (fields & kEnvPosition) { memcpy(vector, e.position, sizeof(vector)); w.call(m.set_position, ref.transform, args, "Transform.set_position"); ++s.position; }
    if (fields & kEnvLocalRotation) { memcpy(vector, e.local_rotation, sizeof(vector)); w.call(m.set_local_euler, ref.transform, args, "Transform.set_localEulerAngles"); ++s.rotation; }
    else if (fields & kEnvRotation) { memcpy(vector, e.rotation, sizeof(vector)); w.call(m.set_euler, ref.transform, args, "Transform.set_eulerAngles"); ++s.rotation; }
    return !w.failed;
}
// -- 5b copies --
static void refuse_copy(Walk& w, ApplyStats& s, const char* id, const char* why) {
    ++s.refused;
    if (s.refusal_lines >= 8) return;   // the summary carries the count
    ++s.refusal_lines;
    char line[kEnvMaxPath + 160]; snprintf(line, sizeof(line), "[CHROMA ENV] duplicate refused (%s): %.*s", why, (int)chroma_env_id_length(id), id);
    w.log(line);
}
static void* component_at(void* array, uintptr_t i) { return *(void**)((char*)array + 0x20 + i * 8); }
// Object.Destroy on a half-made copy, even after a managed call threw (the failure stays recorded).
static void discard(Walk& w, void* clone) {
    if (!clone || !w.m.destroy) return;
    const bool failed = w.failed; const char* what = w.what; w.failed = false;
    void* args[1] = {clone}; w.call(w.m.destroy, NULL, args, "Object.Destroy");
    w.failed = failed || w.failed; if (failed) w.what = what;
}
// Copies one found object `e.duplicate` times next to itself (Chroma DuplicateInitializer for
// the lights subset), adds the copies' ids to w.lines and applies the entry to each copy.
static void duplicate_object(Walk& w, const ChromaEnvEntry& e, size_t line_index, ApplyStats& s) {
    const Api& a = w.a; const Methods& m = w.m;
    const ChromaEnvLines::Ref ref = w.lines.refs[line_index];   // by value: adding lines reallocates
    const char* id = w.lines.items[line_index];                 // the string itself never moves
    if (!m.clone || !m.components || !w.mono_type || !m.destroy) { refuse_copy(w, s, id, "Unity methods missing"); return; }
    void* parent = w.call(m.parent, ref.transform, NULL, "Transform.get_parent");
    if (w.failed) return;
    size_t parent_line = w.lines.count;
    for (size_t i = 0; parent && i < w.lines.count; ++i) if (w.lines.refs[i].transform == parent) { parent_line = i; break; }
    if (!parent || parent_line == w.lines.count) { refuse_copy(w, s, id, parent ? "parent outside the environment" : "scene root"); return; }
    // Every MonoBehaviour of the subtree must be safe to copy (checked before any Awake runs).
    void* originals = w.behaviours(ref.object, true);
    if (w.failed) return;
    const uintptr_t n = originals ? a.array_length(originals) : 0;
    bool lights = false;
    for (uintptr_t i = 0; i < n; ++i) {
        void* component = component_at(originals, i);
        ChromaCloneKind kind = component ? chroma_env_clone_kind(w.class_of(component)) : kCloneRefused;
        if (kind == kCloneRefused) { refuse_copy(w, s, id, component ? w.class_of(component) : "missing script"); return; }
        if (kind == kCloneLightWithId) lights = true;
    }
    if (lights && (!w.light_manager || !m.register_light || !m.active_in_hierarchy)) { refuse_copy(w, s, id, "light registration unavailable"); return; }
    static char path[kEnvMaxPath];
    const char* parent_id = w.lines.items[parent_line];
    const size_t parent_length = chroma_env_id_length(parent_id);
    for (uint8_t copy = 0; copy < e.duplicate && !w.failed; ++copy) {
        if (s.copies >= kEnvMaxClones) { refuse_copy(w, s, id, "512 copies per level"); return; }
        bool world = true; void* args[3] = {ref.object, parent, &world};
        void* clone = w.call(m.clone, NULL, args, "Object.Internal_CloneSingleWithParent");
        if (!clone) { if (!w.failed) refuse_copy(w, s, id, "clone failed"); return; }
        void* copies = w.behaviours(clone, true);
        if (w.failed || (copies ? a.array_length(copies) : 0) != n) {
            discard(w, clone);
            if (!w.failed) refuse_copy(w, s, id, "copy has other components");
            return;
        }
        // Injected light manager: il2cpp_field_set_value takes the object itself for a reference
        // field (0.8090 passed &manager, stored a stack address and crashed in RegisterLight).
        bool wired = true;
        for (uintptr_t i = 0; i < n && wired; ++i) {
            void* original = component_at(originals, i); void* light = component_at(copies, i);
            if (!light || chroma_env_clone_kind(w.class_of(light)) != kCloneLightWithId) continue;
            void* manager = NULL; a.field_get_value(original, w.light_manager, &manager);
            if (!manager) continue;   // the original has no manager either
            a.field_set_value(light, w.light_manager, manager);
            void* check = NULL; a.field_get_value(light, w.light_manager, &check);
            wired = check == manager;
        }
        if (!wired) {
            for (uintptr_t i = 0; i < n; ++i) {   // never leave a copy with a wrong manager
                void* light = component_at(copies, i);
                if (light && chroma_env_clone_kind(w.class_of(light)) == kCloneLightWithId) a.field_set_value(light, w.light_manager, NULL);
            }
            discard(w, clone); refuse_copy(w, s, id, "light manager not written"); return;
        }
        for (uintptr_t i = 0; i < n && !w.failed; ++i) {
            void* light = component_at(copies, i);
            if (!light || chroma_env_clone_kind(w.class_of(light)) != kCloneLightWithId) continue;
            void* manager = NULL; a.field_get_value(light, w.light_manager, &manager);
            if (!manager) continue;
            void* owner = w.call(m.game_object, light, NULL, "Component.get_gameObject");
            bool on = false; w.value(w.call(m.active_in_hierarchy, owner, NULL, "GameObject.get_activeInHierarchy"), on);
            if (on && !w.failed) { w.call(m.register_light, light, NULL, "LightWithIdMonoBehaviour.RegisterLight"); if (!w.failed) ++s.lights; }   // else OnEnable registers it
        }
        // Id: parent id + ".[last child]<name>(Clone)"; the copy and its children join the list.
        void* clone_transform = w.call(m.transform, clone, NULL, "GameObject.get_transform");
        int32_t children = 0; w.value(w.call(m.child_count, parent, NULL, "Transform.get_childCount"), children);
        char name[160]; w.text(w.call(m.name, clone, NULL, "Object.get_name"), name, sizeof(name));
        if (w.failed || !clone_transform) { discard(w, clone); return; }
        memcpy(path, parent_id, parent_length); path[parent_length] = 0;
        const size_t length = chroma_env_push(path, kEnvMaxPath, parent_length, children - 1, name);
        if (length) w.walk(clone_transform, path, length, 0);
        if (w.failed) return;
        ++s.copies;
        ChromaEnvLines::Ref root = {clone_transform, clone, 0u};   // only lights are allowed: no ring/box flags
        apply_one(w, e, root, s);
    }
    ++s.copied;
}
} // namespace

bool chroma_env_apply(const ChromaEnvHost& host, const ChromaChart& chart) {
    if (!lookup_ready(host)) return false;
    Walk w(*host.api, *host.classes, host.log); Collected got;
    if (!collect(w, got, true)) return false;
    ApplyStats s; memset(&s, 0, sizeof(s));
    char line[kEnvMaxPath + 160];
    // GradientBackground: off for every Chroma level (Chroma EnvironmentEnhancementManager.DelayedStart).
    if (!g_background_regex.size) chroma_regex_compile(kEnvGradientBackground, strlen(kEnvGradientBackground), g_background_regex);
    for (size_t i = 0; g_background_regex.size && i < w.lines.count && !w.failed; ++i) {
        const char* id = w.lines.items[i]; size_t len = chroma_env_id_length(id);
        if (id[0] == '#' || !w.lines.refs[i].object || !chroma_regex_search(g_background_regex, id, len)) continue;
        bool off = false; void* args[1] = {&off};
        w.call(w.m.set_active, w.lines.refs[i].object, args, "GameObject.SetActive"); ++s.background;
    }
    // The map's own environment only (Chroma's EnvironmentOverrideChecker).
    bool loaded = false;
    if (chart.env_count) {
        const char* p = got.scenes; size_t n = strlen(chart.env_name);
        while (*p && !loaded) { const char* q = strchr(p, '+'); size_t k = q ? (size_t)(q - p) : strlen(p); loaded = k == n && !memcmp(p, chart.env_name, n); p += k + (q ? 1 : 0); }
        if (!loaded) {
            snprintf(line, sizeof(line), "[CHROMA ENV] map environment %s is not loaded (%s; player override?): %u entries not applied",
                     chart.env_name, got.scenes, chart.env_count);
            host.log(line);
        }
    }
    for (uint32_t k = 0; loaded && k < chart.env_count && !w.failed; ++k) {
        const ChromaEnvEntry& e = chart.env[k];
        const char* pattern = chart.env_ids + e.id_at;
        if (e.method == kEnvRegex && !chroma_regex_compile(pattern, e.id_len, g_entry_regex)) { ++s.not_found; continue; }   // decoder checked it
        ++s.entries; unsigned hits = 0;
        const size_t lines = w.lines.count;   // copies made by this entry are not its own targets (Chroma looks up first)
        if (e.fields & kEnvDuplicate) {
            unsigned found = 0;
            for (size_t i = 0; i < lines; ++i) {
                const char* id = w.lines.items[i];
                if (id[0] != '#' && chroma_env_match(e.method, pattern, e.id_len, &g_entry_regex, id, chroma_env_id_length(id))) ++found;
            }
            if (found > kEnvMaxDuplicateFound) {
                snprintf(line, sizeof(line), "[CHROMA ENV] duplicate refused: %u objects found (Chroma stops above %u): %.*s", found, kEnvMaxDuplicateFound,
                         (int)e.id_len, pattern);
                host.log(line); ++s.refused; continue;
            }
        }
        for (size_t i = 0; i < lines && !w.failed; ++i) {
            const char* id = w.lines.items[i];
            if (id[0] == '#' || !chroma_env_match(e.method, pattern, e.id_len, &g_entry_regex, id, chroma_env_id_length(id))) continue;
            ++hits;
            if (e.fields & kEnvDuplicate) duplicate_object(w, e, i, s);
            else apply_one(w, e, w.lines.refs[i], s);
        }
        s.objects += hits;
        if (hits) ++s.applied; else ++s.not_found;
    }
    if (w.failed) {
        snprintf(line, sizeof(line), "[CHROMA ENV] %s threw after %u entries; the rest of the environment unchanged", w.what, s.entries);
        host.log(line); return false;
    }
    snprintf(line, sizeof(line), "[CHROMA ENV] %s: %u of %u entries applied to %u objects (active %u, scale %u, position %u, rotation %u); "
             "found nothing %u; ring/box parts skipped %u; GradientBackground off %u; duplicated %u copies of %u objects (lights registered %u), "
             "duplicate refused %u", got.scenes, s.applied, chart.env_count, s.objects, s.active, s.scale, s.position, s.rotation, s.not_found,
             s.skipped, s.background, s.copies, s.copied, s.lights, s.refused);
    host.log(line);
    return true;
}
} // namespace BsdAdditive
