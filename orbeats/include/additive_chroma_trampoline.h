// Relocated entry for LightSwitchEventEffect.GetNormalColor/GetHighlightColor
// (CUSA12878 2.04). Their first 17 bytes hold a short JA and a RIP-relative LEA
// that the SDK's copied Detour stub cannot relocate, so the original is reached
// through this explicitly rebuilt prefix instead. Accepted prefix only:
//   83 C6 FB        add esi,-5
//   83 FE 07        cmp esi,7
//   77 xx           ja  +xx
//   89 F0           mov eax,esi
//   48 8D 0D d32    lea rcx,[rip+d32]
#pragma once
#include <stddef.h>
#include <stdint.h>
#include <string.h>
namespace BsdAdditive {
static const size_t kChromaGetterPrefix = 17, kChromaGetterTrampoline = 48;
inline bool chroma_getter_trampoline(const unsigned char* code, uintptr_t address, unsigned char* out, size_t capacity) {
    static const unsigned char head[] = {0x83, 0xc6, 0xfb, 0x83, 0xfe, 0x07, 0x77};
    static const unsigned char tail[] = {0x89, 0xf0, 0x48, 0x8d, 0x0d};
    if (!code || !out || capacity < kChromaGetterTrampoline || address > UINTPTR_MAX - 0x1000 ||
        memcmp(code, head, sizeof(head)) || memcmp(code + 8, tail, sizeof(tail)) || code[7] < 0x09 || code[7] > 0x7f) return false;
    int32_t displacement = 0; memcpy(&displacement, code + 13, 4);
    const uintptr_t next = address + kChromaGetterPrefix, above = address + 8 + code[7];
    uintptr_t table;
    if (displacement >= 0) { if (next > UINTPTR_MAX - (uintptr_t)displacement) return false; table = next + (uintptr_t)displacement; }
    else { uintptr_t distance = (uintptr_t)(-(int64_t)displacement); if (next < distance) return false; table = next - distance; }
    static const unsigned char jump[] = {0xff, 0x25, 0, 0, 0, 0}; // jmp [rip+0]; absolute target follows
    unsigned char* p = out;
    memcpy(p, code, 6); p += 6;                 // add esi,-5 ; cmp esi,7
    *p++ = 0x76; *p++ = 0x0e;                   // jbe over the 14-byte absolute jump
    memcpy(p, jump, 6); p += 6; memcpy(p, &above, 8); p += 8;   // ja target, any distance
    *p++ = 0x89; *p++ = 0xf0;                   // mov eax,esi
    *p++ = 0x48; *p++ = 0xb9; memcpy(p, &table, 8); p += 8;     // movabs rcx,table (was RIP-relative)
    memcpy(p, jump, 6); p += 6; memcpy(p, &next, 8); p += 8;    // continue after the prefix
    return (size_t)(p - out) == kChromaGetterTrampoline;
}
// Relocated entry for GameplayCoreSceneSetupData.LoadTransformedBeatmapDataAsync.
// Its copied prefix would include a RIP-relative MOV. Accepted prefix only:
//   55 | 48 89 E5 | 41 57 | 41 56 | 53 | 48 83 EC 58 | 4C 8B 3D d32   (20 bytes)
static const size_t kChromaLoadPrefix = 20, kChromaLoadTrampoline = 40;
inline bool chroma_load_trampoline(const unsigned char* code, uintptr_t address, unsigned char* out, size_t capacity) {
    static const unsigned char head[] = {0x55, 0x48, 0x89, 0xe5, 0x41, 0x57, 0x41, 0x56, 0x53, 0x48, 0x83, 0xec, 0x58, 0x4c, 0x8b, 0x3d};
    if (!code || !out || capacity < kChromaLoadTrampoline || address > UINTPTR_MAX - 0x1000 || memcmp(code, head, sizeof(head))) return false;
    int32_t displacement = 0; memcpy(&displacement, code + 16, 4);
    const uintptr_t next = address + kChromaLoadPrefix;
    uintptr_t slot;
    if (displacement >= 0) { if (next > UINTPTR_MAX - (uintptr_t)displacement) return false; slot = next + (uintptr_t)displacement; }
    else { uintptr_t distance = (uintptr_t)(-(int64_t)displacement); if (next < distance) return false; slot = next - distance; }
    unsigned char* p = out;
    memcpy(p, code, 13); p += 13;                               // pushes and sub rsp,0x58
    *p++ = 0x49; *p++ = 0xbf; memcpy(p, &slot, 8); p += 8;      // movabs r15,slot
    *p++ = 0x4d; *p++ = 0x8b; *p++ = 0x3f;                      // mov r15,[r15] (was RIP-relative)
    *p++ = 0xff; *p++ = 0x25; memset(p, 0, 4); p += 4; memcpy(p, &next, 8); p += 8;
    return (size_t)(p - out) == kChromaLoadTrampoline;
}
// Relocated entry for LightWithIdManager.SetColorForId (stage 3b). The SDK copies
// at least 14 bytes, which here ends inside a RIP-relative CMP. Accepted prefix only:
//   55 | 48 89 E5 | 41 57 | 41 56 | 53 | 48 83 EC 28 | 80 3D d32 00   (20 bytes)
// RAX is free at the entry (no argument lives there and the body writes it before
// any read), so the CMP becomes movabs rax,flag; cmp byte [rax],0 with the same flags.
static const size_t kChromaSetColorPrefix = 20, kChromaSetColorTrampoline = 40;
inline bool chroma_set_color_trampoline(const unsigned char* code, uintptr_t address, unsigned char* out, size_t capacity) {
    static const unsigned char head[] = {0x55, 0x48, 0x89, 0xe5, 0x41, 0x57, 0x41, 0x56, 0x53, 0x48, 0x83, 0xec, 0x28, 0x80, 0x3d};
    if (!code || !out || capacity < kChromaSetColorTrampoline || address > UINTPTR_MAX - 0x1000 || memcmp(code, head, sizeof(head)) || code[19] != 0)
        return false;
    int32_t displacement = 0; memcpy(&displacement, code + 15, 4);
    const uintptr_t next = address + kChromaSetColorPrefix;
    uintptr_t flag;
    if (displacement >= 0) { if (next > UINTPTR_MAX - (uintptr_t)displacement) return false; flag = next + (uintptr_t)displacement; }
    else { uintptr_t distance = (uintptr_t)(-(int64_t)displacement); if (next < distance) return false; flag = next - distance; }
    unsigned char* p = out;
    memcpy(p, code, 13); p += 13;                               // pushes and sub rsp,0x28
    *p++ = 0x48; *p++ = 0xb8; memcpy(p, &flag, 8); p += 8;      // movabs rax,flag
    *p++ = 0x80; *p++ = 0x38; *p++ = 0x00;                      // cmp byte [rax],0 (was RIP-relative)
    *p++ = 0xff; *p++ = 0x25; memset(p, 0, 4); p += 4; memcpy(p, &next, 8); p += 8;
    return (size_t)(p - out) == kChromaSetColorTrampoline;
}
} // namespace
