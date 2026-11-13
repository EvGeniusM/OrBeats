"""Run the production Chroma sidecar/session/trampoline headers under ASAN/UBSan.

No game, console or network. The decoder is also fed the reference sidecar
written by the Python encoder, so both sides of the format are checked against
each other rather than against their own assumptions.
"""

import platform
import shutil
import subprocess
import sys
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parents[2]
NATIVE = ROOT / "orbeats/tests/native"
FLAGS = ["-g", "-O1", "-std=c++11", "-Wall", "-Wextra", "-Werror", "-fsanitize=address,undefined",
         "-fno-omit-frame-pointer", "-I" + str(ROOT / "orbeats/include")]


def build(tmp_path, name):
    cxx = shutil.which("clang++")
    if not cxx:
        pytest.skip("Native sanitizers need clang++")
    out = tmp_path / name
    subprocess.run([cxx, *FLAGS, str(NATIVE / f"additive_chroma{name}_test.cpp"), "-o", str(out)], check=True)
    return out


def test_sidecar_decoder_accepts_reference_and_refuses_damage(tmp_path):
    binary = build(tmp_path, "")
    assert b"decoder ok" in subprocess.run([str(binary)], check=True, capture_output=True).stdout
    sys.path[:0] = [str(ROOT / "orbeats/tools"), str(ROOT / "orbeats/tests")]
    module = pytest.importorskip("chroma_reference")
    path, path_v2, path_v3 = tmp_path / "reference-chroma.bin", tmp_path / "reference-chroma-v2.bin", tmp_path / "reference-chroma-v3.bin"
    path_v4 = tmp_path / "reference-chroma-v4.bin"
    path.write_bytes(module.reference_sidecar())
    path_v2.write_bytes(module.reference_sidecar_v2())
    path_v3.write_bytes(module.reference_sidecar_v3())
    path_v4.write_bytes(module.reference_sidecar_v4())
    assert b"decoder ok" in subprocess.run([str(binary), str(path), str(path_v2), str(path_v3), str(path_v4)], check=True, capture_output=True).stdout


def test_light_context_order_and_time_keyed_event_lookup(tmp_path):
    binary = build(tmp_path, "_session")
    assert b"session ok" in subprocess.run([str(binary)], check=True, capture_output=True).stdout


def test_relocated_getter_prefix_executes_like_the_original(tmp_path):
    if platform.machine().lower() not in {"x86_64", "amd64"} or sys.platform == "win32":
        pytest.skip("executes x86-64 System V code")
    binary = build(tmp_path, "_trampoline")
    assert b"trampoline ok" in subprocess.run([str(binary)], check=True, capture_output=True).stdout


def test_ring_and_laser_plans_follow_chroma(tmp_path):
    binary = build(tmp_path, "_effects")
    assert b"effects ok" in subprocess.run([str(binary)], check=True, capture_output=True).stdout


def test_per_light_state_follows_chroma_and_the_game_tween(tmp_path):
    binary = build(tmp_path, "_lights")
    assert b"chroma lights ok" in subprocess.run([str(binary)], check=True, capture_output=True).stdout


def test_environment_regex_engine_and_dump_helpers(tmp_path):
    for name, marker in (("_regex", b"regex ok"), ("_env", b"env ok")):
        binary = build(tmp_path, name)
        assert marker in subprocess.run([str(binary)], check=True, capture_output=True).stdout
