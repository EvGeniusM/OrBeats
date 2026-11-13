// Execute the production relocated getter prefix from a distant page and
// compare with a direct call. Synthetic code with the same instruction forms
// as the fingerprint-verified game getters; no shipped private game bytes.
#include "additive_chroma_trampoline.h"
#include <assert.h>
#include <limits.h>
#include <stdio.h>
#include <sys/mman.h>
#include <unistd.h>
using namespace BsdAdditive;
// JIT buffers have no compiler UBSan type descriptor before their entry.
__attribute__((no_sanitize("function"))) static int call_native(void* code, int value) { return ((int (*)(void*, int))code)(NULL, value); }
static void load_prefix() {
    // prefix; mov rax,r15; add rsp,0x58; pop rbx; pop r14; pop r15; pop rbp; ret  -> returns the qword in the slot
    unsigned char prefix[20] = {0x55, 0x48, 0x89, 0xe5, 0x41, 0x57, 0x41, 0x56, 0x53, 0x48, 0x83, 0xec, 0x58, 0x4c, 0x8b, 0x3d, 0, 0, 0, 0};
    int32_t displacement = 128 - 20; memcpy(prefix + 16, &displacement, 4);
    unsigned char out[kChromaLoadTrampoline] = {};
    assert(!chroma_load_trampoline(NULL, 4096, out, sizeof(out)) && !chroma_load_trampoline(prefix, 4096, NULL, sizeof(out)));
    assert(!chroma_load_trampoline(prefix, 4096, out, sizeof(out) - 1) && !chroma_load_trampoline(prefix, UINTPTR_MAX - 16, out, sizeof(out)));
    for (size_t i = 0; i < 16; ++i) { unsigned char bad[20]; memcpy(bad, prefix, 20); bad[i] ^= 0x01; assert(!chroma_load_trampoline(bad, 4096, out, sizeof(out))); }
    unsigned char bad[20]; memcpy(bad, prefix, 20); int32_t low = INT32_MIN; memcpy(bad + 16, &low, 4);
    assert(!chroma_load_trampoline(bad, 4096, out, sizeof(out)));
    size_t page = (size_t)sysconf(_SC_PAGESIZE);
    unsigned char* source = (unsigned char*)mmap(NULL, page, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    unsigned char* stub = (unsigned char*)mmap(NULL, page, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    assert(source != MAP_FAILED && stub != MAP_FAILED);
    memset(source, 0xcc, page); memset(stub, 0xcc, page); memcpy(source, prefix, 20);
    const unsigned char body[] = {0x4c, 0x89, 0xf8, 0x48, 0x83, 0xc4, 0x58, 0x5b, 0x41, 0x5e, 0x41, 0x5f, 0x5d, 0xc3};
    memcpy(source + 20, body, sizeof(body));
    const uint64_t marker = UINT64_C(0x1122334455667788); memcpy(source + 128, &marker, 8);
    assert(chroma_load_trampoline(source, (uintptr_t)source, stub, page));
    assert(!mprotect(source, page, PROT_READ | PROT_EXEC) && !mprotect(stub, page, PROT_READ | PROT_EXEC));
    typedef uint64_t (*Entry)();
    struct Call { __attribute__((no_sanitize("function"))) static uint64_t run(void* code) { return ((Entry)code)(); } };
    assert(Call::run(source) == marker && Call::run(stub) == marker);
    assert(!mprotect(source, page, PROT_READ | PROT_WRITE)); memset(source, 0xcc, 14);
    assert(!mprotect(source, page, PROT_READ | PROT_EXEC));
    assert(Call::run(stub) == marker);   // callee-saved registers and the stack survived, too
    munmap(source, page); munmap(stub, page);
}
static void set_color_prefix() {
    // prefix; setne al; movzx eax,al; add rsp,0x28; pop rbx; pop r14; pop r15; pop rbp; ret  -> 1 when the flag byte is set
    unsigned char prefix[20] = {0x55, 0x48, 0x89, 0xe5, 0x41, 0x57, 0x41, 0x56, 0x53, 0x48, 0x83, 0xec, 0x28, 0x80, 0x3d, 0, 0, 0, 0, 0};
    int32_t displacement = 128 - 20; memcpy(prefix + 15, &displacement, 4);
    unsigned char out[kChromaSetColorTrampoline] = {};
    assert(!chroma_set_color_trampoline(NULL, 4096, out, sizeof(out)) && !chroma_set_color_trampoline(prefix, 4096, NULL, sizeof(out)));
    assert(!chroma_set_color_trampoline(prefix, 4096, out, sizeof(out) - 1) && !chroma_set_color_trampoline(prefix, UINTPTR_MAX - 16, out, sizeof(out)));
    for (size_t i = 0; i < 20; ++i) {   // every fixed byte is part of the accepted pattern
        if (i >= 15 && i < 19) continue;
        unsigned char bad[20]; memcpy(bad, prefix, 20); bad[i] ^= 0x01; assert(!chroma_set_color_trampoline(bad, 4096, out, sizeof(out)));
    }
    unsigned char bad[20]; memcpy(bad, prefix, 20); int32_t low = INT32_MIN; memcpy(bad + 15, &low, 4);
    assert(!chroma_set_color_trampoline(bad, 4096, out, sizeof(out)));
    size_t page = (size_t)sysconf(_SC_PAGESIZE);
    unsigned char* source = (unsigned char*)mmap(NULL, page, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    unsigned char* stub = (unsigned char*)mmap(NULL, page, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    assert(source != MAP_FAILED && stub != MAP_FAILED);
    memset(source, 0xcc, page); memset(stub, 0xcc, page); memcpy(source, prefix, 20);
    const unsigned char body[] = {0x0f, 0x95, 0xc0, 0x0f, 0xb6, 0xc0, 0x48, 0x83, 0xc4, 0x28, 0x5b, 0x41, 0x5e, 0x41, 0x5f, 0x5d, 0xc3};
    memcpy(source + 20, body, sizeof(body));
    assert(chroma_set_color_trampoline(source, (uintptr_t)source, stub, page));
    typedef int (*Entry)();
    struct Call { __attribute__((no_sanitize("function"))) static int run(void* code) { return ((Entry)code)(); } };
    for (int value = 0; value < 2; ++value) {
        assert(!mprotect(source, page, PROT_READ | PROT_WRITE)); source[128] = (unsigned char)value;
        assert(!mprotect(source, page, PROT_READ | PROT_EXEC) && !mprotect(stub, page, PROT_READ | PROT_EXEC));
        assert(Call::run(source) == value && Call::run(stub) == value);
    }
    assert(!mprotect(source, page, PROT_READ | PROT_WRITE)); memset(source, 0xcc, 14);   // the installed detour
    assert(!mprotect(source, page, PROT_READ | PROT_EXEC));
    assert(Call::run(stub) == 1);
    munmap(source, page); munmap(stub, page);
}
int main() {
    load_prefix();
    set_color_prefix();
    // Function body: [0..17) prefix, [17..21) mov eax,[rcx+rax*4]; ret, [34..40) the JA target: mov eax,-1; ret.
    // Table of eight ints follows at +64.
    unsigned char prefix[17] = {0x83, 0xc6, 0xfb, 0x83, 0xfe, 0x07, 0x77, 0x1a, 0x89, 0xf0, 0x48, 0x8d, 0x0d, 0, 0, 0, 0};
    int32_t displacement = 64 - 17; memcpy(prefix + 13, &displacement, 4);
    unsigned char out[kChromaGetterTrampoline] = {};
    assert(!chroma_getter_trampoline(NULL, 4096, out, sizeof(out)));
    assert(!chroma_getter_trampoline(prefix, 4096, NULL, sizeof(out)));
    assert(!chroma_getter_trampoline(prefix, 4096, out, sizeof(out) - 1));
    assert(!chroma_getter_trampoline(prefix, UINTPTR_MAX - 16, out, sizeof(out)));
    for (size_t i = 0; i < 13; ++i) {   // every fixed byte is part of the accepted pattern
        if (i == 7) continue;
        unsigned char bad[17]; memcpy(bad, prefix, 17); bad[i] ^= 0x01;
        assert(!chroma_getter_trampoline(bad, 4096, out, sizeof(out)));
    }
    unsigned char bad[17]; memcpy(bad, prefix, 17);
    bad[7] = 0x08; assert(!chroma_getter_trampoline(bad, 4096, out, sizeof(out)));   // target inside the patched prefix
    bad[7] = 0x80; assert(!chroma_getter_trampoline(bad, 4096, out, sizeof(out)));   // backward short jump
    memcpy(bad, prefix, 17); int32_t low = INT32_MIN; memcpy(bad + 13, &low, 4);
    assert(!chroma_getter_trampoline(bad, 4096, out, sizeof(out)));                  // table before address zero
    assert(chroma_getter_trampoline(prefix, 4096, out, sizeof(out)));

    size_t page = (size_t)sysconf(_SC_PAGESIZE);
    unsigned char* source = (unsigned char*)mmap(NULL, page, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    unsigned char* stub = (unsigned char*)mmap(NULL, page, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    assert(source != MAP_FAILED && stub != MAP_FAILED);
    memset(source, 0xcc, page); memset(stub, 0xcc, page);
    memcpy(source, prefix, 17);
    const unsigned char body[] = {0x8b, 0x04, 0x81, 0xc3};                 // mov eax,[rcx+rax*4]; ret
    const unsigned char above[] = {0xb8, 0xff, 0xff, 0xff, 0xff, 0xc3};    // mov eax,-1; ret
    memcpy(source + 17, body, sizeof(body)); memcpy(source + 8 + 0x1a, above, sizeof(above));
    const int32_t table[8] = {11, 12, 13, 14, 25, 26, 27, 28}; memcpy(source + 64, table, sizeof(table));
    assert(chroma_getter_trampoline(source, (uintptr_t)source, stub, page));
    assert(!mprotect(source, page, PROT_READ | PROT_EXEC) && !mprotect(stub, page, PROT_READ | PROT_EXEC));
    static const int values[] = {-1, 0, 4, 5, 6, 8, 9, 12, 13, 100, INT_MIN, INT_MAX};
    for (size_t i = 0; i < sizeof(values) / sizeof(values[0]); ++i) {
        int direct = call_native(source, values[i]), relocated = call_native(stub, values[i]);
        int expected = values[i] >= 5 && values[i] <= 12 ? table[values[i] - 5] : -1;
        assert(direct == expected && relocated == expected);
    }
    // Simulate the installed detour: clobber the first 14 bytes of the original.
    // The relocated copy must not depend on them any more.
    assert(!mprotect(source, page, PROT_READ | PROT_WRITE)); memset(source, 0xcc, 14);
    assert(!mprotect(source, page, PROT_READ | PROT_EXEC));
    assert(call_native(stub, 5) == 11 && call_native(stub, 12) == 28 && call_native(stub, 13) == -1 && call_native(stub, 0) == -1);
    munmap(source, page); munmap(stub, page);
    puts("chroma getter trampoline ok");
    return 0;
}
