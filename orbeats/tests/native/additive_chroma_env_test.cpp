// Stage 5.0 environment-dump helpers under ASAN/UBSan.
#include "additive_chroma_env.h"
#include <assert.h>
using namespace BsdAdditive;
int main() {
    // Chroma's scene rule.
    assert(chroma_env_scene_wanted("BillieEnvironment") && chroma_env_scene_wanted("Environment"));
    assert(!chroma_env_scene_wanted("MainMenu") && !chroma_env_scene_wanted("MenuEnvironment") && !chroma_env_scene_wanted("GameCore"));
    assert(!chroma_env_scene_wanted(NULL) && !chroma_env_scene_wanted(""));
    // UTF-16 names: printable ASCII kept, the rest '?', always terminated and bounded.
    const uint16_t name[] = {'N', 'e', 'o', 'n', ' ', '(', '1', ')', 0x00e9, 9};
    char out[16]; assert(chroma_env_ascii(name, 10, out, sizeof(out)) == 10 && !strcmp(out, "Neon (1)??"));
    char small[4]; assert(chroma_env_ascii(name, 10, small, sizeof(small)) == 3 && !strcmp(small, "Neo"));
    assert(chroma_env_ascii(NULL, 5, out, sizeof(out)) == 0 && out[0] == 0);
    // Path segments: "Scene.[i]Name", no separator on an empty path, refusal leaves the path intact.
    char path[40]; strcpy(path, "BTSEnvironment");
    size_t n = chroma_env_push(path, sizeof(path), strlen(path), 0, "Environment");
    assert(n == strlen("BTSEnvironment.[0]Environment") && !strcmp(path, "BTSEnvironment.[0]Environment"));
    assert(chroma_env_push(path, sizeof(path), n, 19, "FrontLights-too-long-for-the-buffer") == 0 && !strcmp(path, "BTSEnvironment.[0]Environment"));
    char empty[16] = ""; assert(chroma_env_push(empty, sizeof(empty), 0, 3, "Root") == 7 && !strcmp(empty, "[3]Root"));
    assert(chroma_env_push(empty, sizeof(empty), 16, 0, "x") == 0 && chroma_env_push(NULL, 4, 0, 0, "x") == 0);
    // Lines sort by the id column only: a parent before its children, other columns ignored.
    ChromaEnvLines lines;
    assert(lines.add("B.[1]b\t1\t-") && lines.add("B.[0]a.[0]x\t1\tL") && lines.add("A\t1\t-") && lines.add("B.[0]a\t0\tR") && !lines.add(NULL));
    lines.full = false; lines.sort();
    assert(!strcmp(lines.items[0], "A\t1\t-") && !strcmp(lines.items[1], "B.[0]a\t0\tR") && !strcmp(lines.items[2], "B.[0]a.[0]x\t1\tL"));
    assert(!strcmp(lines.items[3], "B.[1]b\t1\t-") && lines.count == 4);
    // Shell sort on a large shuffled set (gaps above the count included).
    ChromaEnvLines big; char text[32]; unsigned seed = 12345;
    for (int i = 0; i < 6000; ++i) { seed = seed * 1103515245u + 12345u; snprintf(text, sizeof(text), "E.[%u]n\t%d", (seed >> 8) % 3000, i & 1); assert(big.add(text)); }
    big.sort();
    for (size_t i = 1; i < big.count; ++i) assert(ChromaEnvLines::compare(big.items[i - 1], big.items[i]) <= 0);
    // Refs travel with their lines through the sort.
    ChromaEnvLines tagged; int a = 1, b = 2;
    assert(tagged.add("Z\t1\t-", &a, &b, kEnvRing) && tagged.add("A\t1\t-", &b, &a, kEnvBox));
    tagged.sort(); assert(tagged.refs[0].transform == &b && tagged.refs[0].flags == kEnvBox && tagged.refs[1].object == &b && tagged.refs[1].flags == kEnvRing);
    assert(chroma_env_id_length("A.[0]b\t1\t-") == 6 && chroma_env_id_length("plain") == 5);
    // Chroma's five lookup methods.
    const char* id = "KDAEnvironment.[0]Environment.[3]TentacleLeft"; size_t len = strlen(id);
    assert(chroma_env_match(kEnvContains, "Tentacle", 8, NULL, id, len) && !chroma_env_match(kEnvContains, "tentacle", 8, NULL, id, len));
    assert(chroma_env_match(kEnvExact, id, len, NULL, id, len) && !chroma_env_match(kEnvExact, id, len - 1, NULL, id, len));
    assert(chroma_env_match(kEnvStartsWith, "KDAEnv", 6, NULL, id, len) && !chroma_env_match(kEnvStartsWith, "Environment", 11, NULL, id, len));
    assert(chroma_env_match(kEnvEndsWith, "Left", 4, NULL, id, len) && !chroma_env_match(kEnvEndsWith, "Lef", 3, NULL, id, len));
    assert(!chroma_env_match(kEnvContains, "TentacleLeftX", 13, NULL, "Tentacle", 8) && !chroma_env_match(kEnvEndsWith, "xx", 2, NULL, "x", 1));
    static ChromaRegex re; const char pattern[] = "\\[\\d+\\]Tentacle(Left|Right)$";
    assert(chroma_regex_compile(pattern, strlen(pattern), re) && chroma_env_match(kEnvRegex, pattern, strlen(pattern), &re, id, len));
    assert(!chroma_env_match(kEnvRegex, pattern, strlen(pattern), NULL, id, len) && !chroma_env_match(9, "x", 1, NULL, "x", 1));
    // GradientBackground: a direct child of a scene root named Environment only.
    assert(chroma_regex_compile(kEnvGradientBackground, strlen(kEnvGradientBackground), re));
    const char* on = "MonstercatEnvironment.[0]Environment.[48]GradientBackground";
    const char* deeper = "X.[0]Environment.[1]Other.[2]GradientBackground"; const char* other_root = "X.[0]Stage.[3]GradientBackground";
    assert(chroma_regex_search(re, on, strlen(on)) && !chroma_regex_search(re, deeper, strlen(deeper)) && !chroma_regex_search(re, other_root, strlen(other_root)));
    // Rings and parametric boxes keep the parts the game rewrites every frame.
    const uint8_t all = kEnvHasActive | kEnvScale | kEnvPosition | kEnvLocalPosition | kEnvRotation | kEnvLocalRotation;
    assert(chroma_env_allowed(all, 0) == all && chroma_env_allowed(all, kEnvLight) == all);
    assert(chroma_env_allowed(all, kEnvRing) == (kEnvHasActive | kEnvScale));
    assert(chroma_env_allowed(all, kEnvBox) == (kEnvHasActive | kEnvRotation | kEnvLocalRotation));
    assert(chroma_env_allowed(kEnvHasActive | kEnvActive, kEnvRing | kEnvBox) == (kEnvHasActive | kEnvActive));
    // The bound is enforced.
    ChromaEnvLines many;
    for (size_t i = 0; i < kEnvMaxObjects; ++i) assert(many.add("x"));
    assert(!many.add("x") && many.full && many.count == kEnvMaxObjects);
    // 5b: which MonoBehaviours a copy may carry (exact class names, no prefixes).
    assert(chroma_env_clone_kind("TubeBloomPrePassLight") == kClonePlain && chroma_env_clone_kind("MaterialPropertyBlockController") == kClonePlain);
    assert(chroma_env_clone_kind("Parametric3SliceSpriteController") == kClonePlain && chroma_env_clone_kind("ParametricBoxController") == kClonePlain);
    assert(chroma_env_clone_kind("GraphicSettingsConditionalActivator") == kCloneRefused);   // injected SettingsManager
    assert(chroma_env_clone_kind("TubeBloomPrePassLightWithId") == kCloneLightWithId && chroma_env_clone_kind("SpriteLightWithId") == kCloneLightWithId);
    assert(chroma_env_clone_kind("InstancedMaterialLightWithId") == kCloneLightWithId && chroma_env_clone_kind("BloomPrePassBackgroundLightWithId") == kCloneLightWithId);
    assert(chroma_env_clone_kind("Spectrogram") == kCloneRefused && chroma_env_clone_kind("LightRotationEventEffect") == kCloneRefused);
    assert(chroma_env_clone_kind("TrackLaneRing") == kCloneRefused && chroma_env_clone_kind("TubeBloomPrePassLightWithIdX") == kCloneRefused);
    assert(chroma_env_clone_kind("") == kCloneRefused && chroma_env_clone_kind(NULL) == kCloneRefused);
    // Comma lists for the dump's classes column: bounded, unchanged when full.
    char list[16] = "";
    assert(chroma_env_list_add(list, sizeof(list), "Mirror") && !strcmp(list, "Mirror"));
    assert(chroma_env_list_add(list, sizeof(list), "TubeBl") && !strcmp(list, "Mirror,TubeBl"));
    assert(!chroma_env_list_add(list, sizeof(list), "ab") && !strcmp(list, "Mirror,TubeBl"));   // 13 + 1 + 2 + 1 > 16
    assert(chroma_env_list_add(list, sizeof(list), "a") && !strcmp(list, "Mirror,TubeBl,a"));
    assert(!chroma_env_list_add(NULL, 4, "a") && !chroma_env_list_add(list, 0, "a") && !chroma_env_list_add(list, sizeof(list), NULL));
    puts("chroma env ok");
    return 0;
}
