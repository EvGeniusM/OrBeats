// Stage 5a regex subset under ASAN/UBSan.
// With a file argument it runs a differential batch: lines "P\t<pattern>" and
// "S\t<subject>"; prints "C <size|-1>" per pattern and "M <0|1>" per subject
// (tests/test_additive_chroma_regex.py compares that with Python `re`).
#include "additive_chroma_regex.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
using namespace BsdAdditive;
static ChromaRegex g_re;
static bool compiles(const char* p) { return chroma_regex_compile(p, strlen(p), g_re); }
static bool hit(const char* p, const char* s) { assert(compiles(p)); return chroma_regex_search(g_re, s, strlen(s)); }
static int batch(const char* path) {
    FILE* f = fopen(path, "rb"); if (!f) return 2;
    static char line[4096]; bool have = false;
    while (fgets(line, sizeof(line), f)) {
        size_t n = strlen(line); while (n && (line[n - 1] == '\n' || line[n - 1] == '\r')) line[--n] = 0;
        if (n < 2 || line[1] != '\t') continue;
        if (line[0] == 'P') { have = chroma_regex_compile(line + 2, n - 2, g_re); printf("C %d\n", have ? (int)g_re.size : -1); }
        else if (line[0] == 'S') {
            uint32_t steps = 0; bool m = have && chroma_regex_search(g_re, line + 2, n - 2, &steps);
            printf("M %d%s\n", m ? 1 : 0, steps > kRegexMaxSteps ? " budget" : "");
        }
    }
    fclose(f); return 0;
}
int main(int argc, char** argv) {
    if (argc > 1) return batch(argv[1]);
    // Real map patterns against Chroma ids.
    assert(hit("\\[[0-9]+\\]FrontLights\\.\\[0\\]NeonTube$", "X.[0]Environment.[17]FrontLights.[0]NeonTube"));
    assert(!hit("\\[[0-9]+\\]FrontLights\\.\\[0\\]NeonTube$", "X.[0]Environment.[17]FrontLights.[0]NeonTube.[1]BoxLight"));
    assert(hit("Sun\\.\\[\\d*\\]NeonTube \\(2\\)$", "B.[0]Environment.[3]Sun.[12]NeonTube (2)"));
    assert(hit("PanelLightTrackLaneRing\\(Clone\\)$", "GameCore.[0]PanelLightTrackLaneRing(Clone)"));
    assert(hit("^Big|Small$", "x.[0]Small") && hit("^Big|Small$", "BigOne") && !hit("^Big|Small$", "x.Big"));
    // Search anywhere, anchors, classes, repeats.
    assert(hit("b", "abc") && !hit("^b", "abc") && hit("c$", "abc") && !hit("b$", "abc"));
    assert(hit("a[^0-9]c", "a-c") && !hit("a[^0-9]c", "a5c") && hit("[a-c-]x", "-x") && hit("[-a]", "-"));
    assert(hit("^a{2,3}$", "aa") && hit("^a{2,3}$", "aaa") && !hit("^a{2,3}$", "aaaa") && !hit("^a{2,3}$", "a"));
    assert(hit("^(?:ab){2,}$", "ababab") && !hit("^(?:ab){2,}$", "ab") && hit("^x(ab)?y$", "xy") && hit("^x(ab)?y$", "xaby"));
    assert(hit("^\\w+\\s\\d$", "Neon_1 5") && !hit("^\\W", "a") && hit("^x|y*z$", "z") && hit("^(a|b)*c$", "ababc") && !hit("^(a|b)*c$", "abd"));
    assert(hit("^q|(?:a*)b$", "b") && hit("^(?:x|a*b)$", "aaab") && hit("^(?:x|a*b)$", "x"));   // fragment starting at an insert point
    assert(hit("^a{0,3}b$", "aab") && !hit("^a{0,3}b$", "aaaab") && hit("^a{0}b$", "b"));
    // Refused: outside the subset or would loop.
    const char* refused[] = {"", "(", ")", "[", "[]", "[^]", "a{17}", "a{3,2}", "a*?", "a**", "(?=a)", "(?<n>a)", "\\b", "\\1", "\\n",
                             "(a*)*", "(?:)+", "^*", "$?", "(a|)*", "{", "}", "]", "a{", "a{2", "[z-a]", "[\\d-z]", "\x01", "\xc3\xa9"};
    for (size_t i = 0; i < sizeof(refused) / sizeof(refused[0]); ++i) { if (compiles(refused[i])) { printf("accepted %s\n", refused[i]); return 1; } }
    // Bounds: nesting and program size.
    char deep[64] = ""; for (int i = 0; i < 17; ++i) strcat(deep, "("); strcat(deep, "a"); for (int i = 0; i < 17; ++i) strcat(deep, ")");
    assert(!compiles(deep));
    assert(!compiles("(?:(?:(?:abcdefgh){16}){16})"));
    // The step budget turns a pathological search into "no match", never a hang.
    assert(compiles("^(?:a|a)(?:a|a)(?:a|a)(?:a|a)(?:a|a)(?:a|a)(?:a|a)(?:a|a)(?:a|a)(?:a|a)(?:a|a)(?:a|a)(?:a|a)(?:a|a)(?:a|a)(?:a|a)(?:a|a)(?:a|a)b"));
    uint32_t steps = 0; char text[64]; memset(text, 'a', 40); text[40] = 0;
    assert(!chroma_regex_search(g_re, text, 40, &steps) && steps > kRegexMaxSteps);
    puts("chroma regex ok");
    return 0;
}
