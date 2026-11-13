"""Chroma environment regex subset: host check, and the native engine against Python `re`."""
import random
import re
import shutil
import subprocess
from pathlib import Path

import pytest

import additive_chroma_regex as rx

ROOT = Path(__file__).resolve().parents[1]
# Patterns of the kind real Chroma maps use (BeatSaver 1dcf4, 2c327, 30097, 3dbbe...).
MAP_PATTERNS = [
    r"Environment\.\[[0-9]+\]Spectrograms\.\[0\]Spectrogram1$", r"\[[0-9]+\]FrontLights\.\[1\]NeonTube \(1\)$",
    r"PanelLightTrackLaneRing\(Clone\)$", r"Sun\.\[\d*\]NeonTube \(2\)$", r"\[19\]FrontLights\.\[2\]NeonTube\(Clone\)$",
    r"BottomPairLasers \(4\)\.\[\d*\]PillarL$", r"LightRailingSegment\.\[\d*\]NeonTubeDirectionalL$", r"GlowLineR$",
    r"^.*\[\d*\]Construction(?:L|R)?$", r"(Left|Right)Laser", r"\[\d+\]LightLinesTrackLaneRing\(Clone\)$",
]
IDS = [
    "FitBeatEnvironment.[0]Environment.[10]Spectrograms.[0]Spectrogram1", "FitBeatEnvironment.[0]Environment.[17]FrontLights.[1]NeonTube (1)",
    "FitBeatEnvironment.[0]Environment.[17]FrontLights.[1]NeonTube (1).[1]BoxLight", "GameCore.[0]PanelLightTrackLaneRing(Clone)",
    "X.[0]Environment.[3]Sun.[12]NeonTube (2)", "X.[0]Environment.[19]FrontLights.[2]NeonTube(Clone)",
    "X.[0]Environment.[5]BottomPairLasers (4).[0]PillarL", "X.[0]Environment.[9]LeftLaser", "X.[0]Environment.[2]Construction",
    "X.[0]Environment.[4]GlowLineR", "X.[0]Environment.[4]GlowLineRx", "", "a", "aaaa", "ab-c 1_2", "[0]", "(Clone)",
]


def test_map_patterns_are_inside_the_subset():
    for pattern in MAP_PATTERNS:
        assert rx.check(pattern) > 1


@pytest.mark.parametrize("pattern", [
    "", "(", ")", "[", "[]", "[^]", "a{17}", "a{3,2}", "a*?", "a**", "(?=a)", "(?<n>a)", r"\b", r"\1", r"\n", "(a*)*",
    "(?:)+", "^*", "$?", "(a|)*", "{", "}", "]", "a{", "a{2", "[z-a]", r"[\d-z]", "\x01", "é", "a" * 513,
    "((((((((((((((((((a))))))))))))))))))", "(?:(?:(?:abcdefgh){16}){16})", 5, None,
])
def test_patterns_outside_the_subset_refuse(pattern):
    with pytest.raises(ValueError):
        rx.check(pattern)


def _random_pattern(rand):
    atoms = ["a", "b", "\\.", "\\(", "\\)", "\\[", "\\]", ".", "\\d", "\\w", "[0-9]", "[^a]", "[a-c-]", "[\\d.]", " ", "_"]
    def seq(depth):
        out = []
        for _ in range(rand.randint(0, 4)):
            roll = rand.random()
            if roll < 0.12 and depth < 3:
                body = seq(depth + 1) + ("|" + seq(depth + 1) if rand.random() < 0.4 else "")
                out.append(("(?:" if rand.random() < 0.5 else "(") + body + ")")
            elif roll < 0.18:
                out.append(rand.choice(["^", "$"]))
            else:
                out.append(rand.choice(atoms))
            if rand.random() < 0.3:
                out[-1] += rand.choice(["*", "+", "?", "{2}", "{0,2}", "{1,}", "{0}"])
        return "".join(out)
    return seq(0) + ("|" + seq(0) if rand.random() < 0.2 else "")


def test_native_engine_matches_python(tmp_path):
    compiler = shutil.which("clang++")
    if not compiler:
        pytest.skip("clang++ required for native sanitizer checks")
    binary = tmp_path / "regex-test"
    subprocess.run([compiler, "-std=c++17", "-Wall", "-Wextra", "-fsanitize=address,undefined", "-fno-sanitize-recover=all", "-g",
                    "-I" + str(ROOT / "include"), str(ROOT / "tests/native/additive_chroma_regex_test.cpp"), "-o", str(binary)],
                   check=True, capture_output=True, text=True)
    assert "chroma regex ok" in subprocess.run([str(binary)], check=True, capture_output=True, text=True).stdout
    rand = random.Random(20261112)
    patterns = MAP_PATTERNS + [_random_pattern(rand) for _ in range(1500)]
    subjects = IDS + ["".join(rand.choice("ab.()[]0123 _x") for _ in range(rand.randint(0, 12))) for _ in range(40)]
    lines = []
    for pattern in patterns:
        lines.append("P\t" + pattern)
        lines.extend("S\t" + s for s in subjects)
    batch = tmp_path / "batch.txt"
    batch.write_text("\n".join(lines) + "\n", encoding="ascii")
    out = subprocess.run([str(binary), str(batch)], check=True, capture_output=True, text=True).stdout.split("\n")
    k, accepted = 0, 0
    for pattern in patterns:
        size = int(out[k].split()[1]); k += 1
        try:
            expected = rx.check(pattern)
        except ValueError:
            expected = -1
        assert size == expected, pattern
        compiled = re.compile(pattern) if size > 0 else None
        accepted += size > 0
        for s in subjects:
            got = out[k].split(); k += 1
            if compiled is not None and "budget" not in got:
                assert got[1] == ("1" if compiled.search(s) else "0"), (pattern, s)
    assert accepted > 500
