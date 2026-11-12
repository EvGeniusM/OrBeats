// Chroma level-1 runtime (CUSA12878 2.04): per-event light colours and
// per-object note/arc/wall colours from an "<id>-chroma.bin" sidecar.
// Implemented in src/chroma.cpp; main.cpp only supplies its verified module
// base, resolved IL2CPP API and logger.
#pragma once
#include "additive_song_packs.h"
namespace BsdAdditive {
struct ChromaHost {
    uintptr_t base;                 // Il2CppUserAssemblies.prx image base
    const Api* api;                 // must outlive the plugin
    void (*log)(const char*);
};
// Verifies every code fingerprint before changing anything. Returns false and
// leaves the game untouched on any mismatch. Safe to call once.
bool chroma_install(const ChromaHost& host);
void chroma_uninstall();
} // namespace
