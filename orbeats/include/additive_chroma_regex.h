// Chroma stage 5a: the ECMAScript regex subset Chroma maps use for environment
// lookups (`lookupMethod: "Regex"`; .NET IsMatch = match anywhere). Pure, no
// allocation, bounded: compiled to a small backtracking program and run with a
// step budget. The host validator (tools/additive_chroma_regex.py) accepts
// exactly this grammar, so the plugin never sees a pattern it cannot run.
//
// Grammar:  alt  := seq ('|' seq)*            seq := (atom quant?)*
//   atom  := '(' ['?:'] alt ')' | '[' ['^'] item+ ']' | '.' | '^' | '$' | '\' esc | literal
//   quant := '*' | '+' | '?' | '{n}' | '{n,}' | '{n,m}'   (n <= m <= 16; no lazy or stacked quantifiers)
//   esc   := d D w W s S | any printable non-alphanumeric ASCII character (itself)
//   literal: printable ASCII except  \ ^ $ . | ? * + ( ) [ ] { }
//   item  := char | char '-' char | '\' esc   (a '-' first or last is literal; no '[' inside)
// Refused: a quantifier on an anchor or on anything that can match the empty
// string (it would loop), empty alternatives inside quantified groups included.
#pragma once
#include <stddef.h>
#include <stdint.h>
#include <string.h>
namespace BsdAdditive {
static const size_t kRegexMaxPattern = 512, kRegexMaxProgram = 2048, kRegexMaxSets = 32, kRegexMaxDepth = 16;
static const int kRegexMaxRepeat = 16;
static const uint32_t kRegexMaxSteps = 200000;   // per subject; over budget counts as "no match"

enum RegexOp { kReChar, kReAny, kReSet, kReBol, kReEol, kReSplit, kReJmp, kReMatch };
struct RegexInst { uint8_t op, c; uint16_t x, y; };   // SPLIT: try x, then y; JMP: x
struct ChromaRegex {
    RegexInst prog[kRegexMaxProgram]; uint16_t size;
    uint8_t sets[kRegexMaxSets][32]; uint8_t set_count;
};

namespace regex_detail {
inline bool is_digit(unsigned c) { return c >= '0' && c <= '9'; }
inline bool is_word(unsigned c) { return is_digit(c) || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_'; }
inline bool is_space(unsigned c) { return c == ' ' || (c >= 9 && c <= 13); }
inline bool printable(unsigned c) { return c >= 0x20 && c <= 0x7e; }
inline bool alnum(unsigned c) { return is_word(c) && c != '_'; }
inline bool class_escape(char e) { return e == 'd' || e == 'D' || e == 'w' || e == 'W' || e == 's' || e == 'S'; }
inline void put(uint8_t* set, unsigned c) { set[c >> 3] |= (uint8_t)(1u << (c & 7)); }
inline void put_class(uint8_t* set, char kind) {
    for (unsigned c = 0; c < 256; ++c) {
        bool in = kind == 'd' || kind == 'D' ? is_digit(c) : kind == 'w' || kind == 'W' ? is_word(c) : is_space(c);
        if (kind >= 'A' && kind <= 'Z') in = !in;
        if (in) put(set, c);
    }
}
inline bool in_set(const uint8_t* set, unsigned c) { return (set[c >> 3] >> (c & 7)) & 1; }

struct Parser {
    const char* p; const char* end; ChromaRegex& re; size_t depth;
    bool emit(uint8_t op, uint8_t c = 0, uint16_t x = 0, uint16_t y = 0) {
        if (re.size >= kRegexMaxProgram) return false;
        RegexInst& i = re.prog[re.size++]; i.op = op; i.c = c; i.x = x; i.y = y; return true;
    }
    static bool jumps(const RegexInst& i) { return i.op == kReSplit || i.op == kReJmp; }
    // Appends a copy of [from, to); jump targets inside the fragment move with it.
    bool copy(uint16_t from, uint16_t to) {
        const uint16_t shift = (uint16_t)(re.size - from);
        for (uint16_t k = from; k < to; ++k) {
            RegexInst i = re.prog[k];
            if (jumps(i)) { i.x = (uint16_t)(i.x + shift); i.y = (uint16_t)(i.y + shift); }
            if (!emit(i.op, i.c, i.x, i.y)) return false;
        }
        return true;
    }
    // Inserts one jump instruction at `at` (the start of the fragment being
    // wrapped). Code before `at` that targets `at` now reaches the new
    // instruction; code from `at` on (the fragment, which moves) keeps its targets.
    bool insert(uint16_t at, uint8_t op, uint16_t x, uint16_t y) {
        if (re.size >= kRegexMaxProgram) return false;
        for (uint16_t k = 0; k < re.size; ++k) if (jumps(re.prog[k])) {
            const uint16_t moved = k < at ? (uint16_t)(at + 1) : at;
            if (re.prog[k].x >= moved) ++re.prog[k].x;
            if (re.prog[k].y >= moved) ++re.prog[k].y;
        }
        memmove(&re.prog[at + 1], &re.prog[at], (size_t)(re.size - at) * sizeof(RegexInst));
        ++re.size; RegexInst& i = re.prog[at]; i.op = op; i.c = 0; i.x = x; i.y = y; return true;
    }
    uint8_t* new_set() {
        if (re.set_count >= kRegexMaxSets) return NULL;
        uint8_t* set = re.sets[re.set_count]; memset(set, 0, 32); return set;
    }
    bool class_item(uint8_t* set) {
        if (*p == '\\') {
            if (++p >= end) return false;
            char e = *p++;
            if (class_escape(e)) { put_class(set, e); return p >= end || *p != '-' || (p + 1 < end && p[1] == ']'); }
            if (!printable((unsigned char)e) || alnum((unsigned char)e)) return false;
            put(set, (unsigned char)e); return true;
        }
        unsigned lo = (unsigned char)*p++;
        if (!printable(lo) || lo == '[') return false;
        if (p + 1 < end && *p == '-' && p[1] != ']') {
            unsigned hi = (unsigned char)p[1];
            if (hi == '\\' || hi == '[' || !printable(hi) || hi < lo) return false;
            p += 2; for (unsigned c = lo; c <= hi; ++c) put(set, c);
            return true;
        }
        put(set, lo); return true;
    }
    // One atom; `empty` says whether it can match the empty string.
    bool atom(bool& empty) {
        const char c = *p; empty = false;
        if (c == '(') {
            ++p;
            if (end - p >= 2 && p[0] == '?' && p[1] == ':') p += 2;
            else if (p < end && *p == '?') return false;   // lookarounds, named groups
            if (++depth > kRegexMaxDepth || !alternation(empty) || p >= end || *p != ')') return false;
            --depth; ++p; return true;
        }
        if (c == '[') {
            ++p; uint8_t* set = new_set(); if (!set) return false;
            const bool negate = p < end && *p == '^'; if (negate) ++p;
            if (p >= end || *p == ']') return false;   // [] and [^]
            while (p < end && *p != ']') if (!class_item(set)) return false;
            if (p >= end) return false;
            ++p;
            if (negate) for (int k = 0; k < 32; ++k) set[k] = (uint8_t)~set[k];
            return emit(kReSet, re.set_count++);
        }
        if (c == '.') { ++p; return emit(kReAny); }
        if (c == '^' || c == '$') { ++p; empty = true; return emit(c == '^' ? kReBol : kReEol); }
        if (c == '\\') {
            if (++p >= end) return false;
            const char e = *p++;
            if (class_escape(e)) { uint8_t* set = new_set(); if (!set) return false; put_class(set, e); return emit(kReSet, re.set_count++); }
            if (!printable((unsigned char)e) || alnum((unsigned char)e)) return false;
            return emit(kReChar, (uint8_t)e);
        }
        if (!printable((unsigned char)c) || strchr("\\^$.|?*+()[]{}", c)) return false;
        ++p; return emit(kReChar, (uint8_t)c);
    }
    bool number(int& out) {
        if (p >= end || !is_digit((unsigned char)*p)) return false;
        out = 0;
        while (p < end && is_digit((unsigned char)*p)) { out = out * 10 + (*p++ - '0'); if (out > kRegexMaxRepeat) return false; }
        return true;
    }
    // Applies the quantifier at p to the fragment [start, re.size); `optional`: it may repeat zero times.
    bool quantifier(uint16_t start, bool& optional) {
        int lo = 0, hi = 0; bool unbounded = false;
        const char q = *p++;
        if (q == '*') unbounded = true;
        else if (q == '+') { lo = 1; unbounded = true; }
        else if (q == '?') hi = 1;
        else {   // '{'
            if (!number(lo)) return false;
            hi = lo;
            if (p < end && *p == ',') { ++p; if (p < end && *p == '}') unbounded = true; else if (!number(hi)) return false; }
            if (p >= end || *p != '}' || (!unbounded && hi < lo)) return false;
            ++p;
        }
        if (p < end && (*p == '?' || *p == '*' || *p == '+' || *p == '{')) return false;   // lazy or stacked
        optional = lo == 0;
        const uint16_t frag_end = re.size;
        if (lo == 0 && !unbounded && hi == 0) { re.size = start; return true; }   // e{0}
        if (lo == 0) {
            // The original copy becomes optional: SPLIT body, out.
            if (!insert(start, kReSplit, (uint16_t)(start + 1), 0)) return false;
            const uint16_t body = (uint16_t)(start + 1), body_end = re.size;
            if (unbounded) {   // e*: SPLIT body, out; body; JMP split
                if (!emit(kReJmp, 0, start)) return false;
                re.prog[start].y = re.size; return true;
            }
            uint16_t splits[kRegexMaxRepeat]; int n = 0; splits[n++] = start;
            for (int k = 1; k < hi; ++k) {
                splits[n++] = re.size;
                if (!emit(kReSplit, 0, (uint16_t)(re.size + 1)) || !copy(body, body_end)) return false;
            }
            for (int k = 0; k < n; ++k) re.prog[splits[k]].y = re.size;
            return true;
        }
        for (int k = 1; k < lo; ++k) if (!copy(start, frag_end)) return false;
        const uint16_t length = (uint16_t)(frag_end - start);
        if (unbounded) {   // the last mandatory copy loops: SPLIT back to it, or on
            const uint16_t last = (uint16_t)(re.size - length);
            return emit(kReSplit, 0, last, (uint16_t)(re.size + 1));
        }
        uint16_t splits[kRegexMaxRepeat]; int n = 0;
        for (int k = lo; k < hi; ++k) {
            splits[n++] = re.size;
            if (!emit(kReSplit, 0, (uint16_t)(re.size + 1)) || !copy(start, frag_end)) return false;
        }
        for (int k = 0; k < n; ++k) re.prog[splits[k]].y = re.size;
        return true;
    }
    bool sequence(bool& empty) {
        empty = true;
        while (p < end && *p != '|' && *p != ')') {
            const uint16_t start = re.size; bool atom_empty = false;
            if (!atom(atom_empty)) return false;
            if (p < end && (*p == '*' || *p == '+' || *p == '?' || *p == '{')) {
                if (atom_empty) return false;   // quantified anchor or empty-matching group
                bool optional = false;
                if (!quantifier(start, optional)) return false;
                if (optional) atom_empty = true;
            }
            empty = empty && atom_empty;
        }
        return true;
    }
    bool alternation(bool& empty) {
        const uint16_t start = re.size;
        if (!sequence(empty)) return false;
        uint16_t jumps_out[64]; int count = 0;
        while (p < end && *p == '|') {
            ++p;
            // SPLIT left, right; left; JMP out; right
            if (count >= 64 || !insert(start, kReSplit, (uint16_t)(start + 1), 0)) return false;
            for (int k = 0; k < count; ++k) ++jumps_out[k];
            jumps_out[count++] = re.size;
            if (!emit(kReJmp)) return false;
            re.prog[start].y = re.size;
            bool right_empty = false;
            if (!sequence(right_empty)) return false;
            empty = empty || right_empty;
        }
        for (int k = 0; k < count; ++k) re.prog[jumps_out[k]].x = re.size;
        return true;
    }
};
} // namespace regex_detail

// False for anything outside the subset or the bounds.
inline bool chroma_regex_compile(const char* pattern, size_t length, ChromaRegex& re) {
    re.size = 0; re.set_count = 0;
    if (!pattern || !length || length > kRegexMaxPattern) return false;
    regex_detail::Parser parser = {pattern, pattern + length, re, 0};
    bool empty = false;
    if (!parser.alternation(empty) || parser.p != parser.end || !parser.emit(kReMatch)) { re.size = 0; return false; }
    return true;
}
// .NET Regex.IsMatch: true when the pattern matches anywhere in `text`. Over the
// step budget the answer is false and *steps (optional) exceeds kRegexMaxSteps.
inline bool chroma_regex_search(const ChromaRegex& re, const char* text, size_t length, uint32_t* steps = NULL) {
    struct Frame { uint16_t pc, sp; };
    Frame stack[256]; uint32_t work = 0; bool found = false;
    if (steps) *steps = 0;
    if (!re.size || !text || length > 0xFFFF) return false;
    for (size_t start = 0; start <= length && !found && work <= kRegexMaxSteps; ++start) {
        size_t top = 0; stack[top].pc = 0; stack[top].sp = (uint16_t)start; ++top;
        while (top && !found && work <= kRegexMaxSteps) {
            Frame f = stack[--top];
            while (++work <= kRegexMaxSteps) {
                const RegexInst& i = re.prog[f.pc];
                if (i.op == kReMatch) { found = true; break; }
                if (i.op == kReJmp) { f.pc = i.x; continue; }
                if (i.op == kReSplit) {
                    if (top == sizeof(stack) / sizeof(stack[0])) { work = kRegexMaxSteps + 1; break; }
                    stack[top].pc = i.y; stack[top].sp = f.sp; ++top; f.pc = i.x; continue;
                }
                if (i.op == kReBol || i.op == kReEol) {
                    if (i.op == kReBol ? f.sp != 0 : f.sp != length) break;
                    ++f.pc; continue;
                }
                if (f.sp >= length) break;
                const unsigned ch = (unsigned char)text[f.sp];
                if (i.op == kReChar ? ch != i.c : i.op == kReAny ? ch == '\n' : !regex_detail::in_set(re.sets[i.c], ch)) break;
                ++f.pc; ++f.sp;
            }
        }
    }
    if (steps) *steps = work;
    return found && work <= kRegexMaxSteps;
}
} // namespace BsdAdditive
