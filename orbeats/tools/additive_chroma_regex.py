"""Host check for the Chroma environment regex subset the plugin runs.

Mirrors include/additive_chroma_regex.h exactly: the same grammar, the same
bounds and the same compiled program size. `check(pattern)` raises ValueError
for anything the plugin would refuse; a pattern that passes is also a valid
Python `re` pattern with the same meaning on ASCII object ids.
"""

MAX_PATTERN = 512
MAX_PROGRAM = 2048
MAX_SETS = 32
MAX_DEPTH = 16
MAX_REPEAT = 16
MAX_BRANCHES = 65
_CLASS_ESCAPES = set("dDwWsS")
_SPECIAL = set("\\^$.|?*+()[]{}")


def _printable(c):
    return 0x20 <= ord(c) <= 0x7E


class _Parser:
    def __init__(self, text):
        self.s, self.i, self.sets, self.depth = text, 0, 0, 0

    def fail(self, why):
        raise ValueError(f"unsupported Chroma regex ({why}) at {self.i}: {self.s!r}")

    def peek(self, k=0):
        j = self.i + k
        return self.s[j] if j < len(self.s) else None

    def new_set(self):
        if self.sets >= MAX_SETS:
            self.fail("too many character classes")
        self.sets += 1

    def class_item(self):
        c = self.peek()
        if c == "\\":
            self.i += 1
            e = self.peek()
            if e is None:
                self.fail("dangling escape")
            self.i += 1
            if e in _CLASS_ESCAPES:
                if self.peek() == "-" and self.peek(1) != "]":
                    self.fail("range after a class escape")
                return
            if not _printable(e) or e.isalnum():
                self.fail("escape")
            return
        if not _printable(c) or c == "[":
            self.fail("class character")
        self.i += 1
        if self.peek() == "-" and self.peek(1) is not None and self.peek(1) != "]":
            hi = self.peek(1)
            if hi in ("\\", "[") or not _printable(hi) or hi < c:
                self.fail("class range")
            self.i += 2

    def atom(self):
        """(size, can_be_empty)"""
        c = self.peek()
        if c == "(":
            self.i += 1
            if self.peek() == "?" and self.peek(1) == ":":
                self.i += 2
            elif self.peek() == "?":
                self.fail("lookaround or named group")
            self.depth += 1
            if self.depth > MAX_DEPTH:
                self.fail("nesting")
            size, empty = self.alternation()
            if self.peek() != ")":
                self.fail("unclosed group")
            self.depth -= 1
            self.i += 1
            return size, empty
        if c == "[":
            self.i += 1
            self.new_set()
            if self.peek() == "^":
                self.i += 1
            if self.peek() in (None, "]"):
                self.fail("empty class")
            while self.peek() not in (None, "]"):
                self.class_item()
            if self.peek() is None:
                self.fail("unclosed class")
            self.i += 1
            return 1, False
        if c == ".":
            self.i += 1
            return 1, False
        if c in ("^", "$"):
            self.i += 1
            return 1, True
        if c == "\\":
            self.i += 1
            e = self.peek()
            if e is None:
                self.fail("dangling escape")
            self.i += 1
            if e in _CLASS_ESCAPES:
                self.new_set()
                return 1, False
            if not _printable(e) or e.isalnum():
                self.fail("escape")
            return 1, False
        if not _printable(c) or c in _SPECIAL:
            self.fail("character")
        self.i += 1
        return 1, False

    def number(self):
        if self.peek() is None or not self.peek().isdigit():
            self.fail("repeat count")
        value = 0
        while self.peek() is not None and self.peek().isdigit():
            value = value * 10 + int(self.peek())
            self.i += 1
            if value > MAX_REPEAT:
                self.fail("repeat count over 16")
        return value

    def quantifier(self, size):
        """(new size, optional)"""
        q = self.peek()
        self.i += 1
        lo, hi, unbounded = 0, 0, False
        if q == "*":
            unbounded = True
        elif q == "+":
            lo, unbounded = 1, True
        elif q == "?":
            hi = 1
        else:
            lo = hi = self.number()
            if self.peek() == ",":
                self.i += 1
                if self.peek() == "}":
                    unbounded = True
                else:
                    hi = self.number()
            if self.peek() != "}" or (not unbounded and hi < lo):
                self.fail("repeat")
            self.i += 1
        if self.peek() in ("?", "*", "+", "{"):
            self.fail("lazy or stacked quantifier")
        if lo == 0 and not unbounded and hi == 0:
            return 0, True
        if lo == 0:
            return (size + 2 if unbounded else hi * (size + 1)), True
        if unbounded:
            return lo * size + 1, False
        return lo * size + (hi - lo) * (size + 1), False

    def sequence(self):
        total, empty = 0, True
        while self.peek() not in (None, "|", ")"):
            size, atom_empty = self.atom()
            if self.peek() in ("*", "+", "?", "{"):
                if atom_empty:
                    self.fail("quantified anchor or empty-matching group")
                size, optional = self.quantifier(size)
                atom_empty = atom_empty or optional
            total += size
            empty = empty and atom_empty
        return total, empty

    def alternation(self):
        total, empty = self.sequence()
        branches = 1
        while self.peek() == "|":
            self.i += 1
            branches += 1
            if branches > MAX_BRANCHES:
                self.fail("too many alternatives")
            size, right_empty = self.sequence()
            total += size + 2
            empty = empty or right_empty
        return total, empty


def check(pattern):
    """Program size the plugin will compile; ValueError outside the subset."""
    if not isinstance(pattern, str) or not pattern or len(pattern.encode("utf-8")) > MAX_PATTERN or not pattern.isascii():
        raise ValueError("Chroma regex must be 1..512 ASCII characters")
    parser = _Parser(pattern)
    size, _ = parser.alternation()
    if parser.i != len(pattern):
        parser.fail("unmatched ')'")
    size += 1  # MATCH
    if size > MAX_PROGRAM:
        raise ValueError("Chroma regex program too large")
    return size
