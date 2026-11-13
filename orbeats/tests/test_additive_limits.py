"""One host audio-length limit, bounded by the native library reader. No console access."""
import re
import sys
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
import additive_limits as limits
import additive_v4_native as native

USERS = ("tools/additive_v4_native.py", "tools/vorbis_audio.py", "development/scripts/import_v4_additive.py",
         "development/scripts/prepare_additive_bundle.py")


def audio(seconds, rate=44100):
    count = rate * seconds
    return {"version": "4.0.0", "songSampleCount": count, "songFrequency": rate,
            "bpmData": [{"si": 0, "ei": count, "sb": 0, "eb": seconds * 2}]}


def test_default_stays_at_the_proven_length_and_override_is_bounded(monkeypatch):
    monkeypatch.delenv(limits.ENV, raising=False)
    assert limits.max_audio_seconds() == limits.DEFAULT_AUDIO_SECONDS == 780
    monkeypatch.setenv(limits.ENV, "")
    assert limits.max_audio_seconds() == 780
    for good in ("780", "900", "1800"):
        monkeypatch.setenv(limits.ENV, good)
        assert limits.max_audio_seconds() == int(good)
    for bad in ("600", "779", "1801", "0", "-1", "9e2", "900.0", " 900", "abc", "99999"):
        monkeypatch.setenv(limits.ENV, bad)
        with pytest.raises(ValueError, match=limits.ENV):
            limits.max_audio_seconds()


def test_native_audio_validation_follows_the_single_limit(monkeypatch):
    monkeypatch.delenv(limits.ENV, raising=False)
    native.audio(audio(780))
    with pytest.raises(ValueError, match="sample count"):
        native.audio(audio(781))
    monkeypatch.setenv(limits.ENV, "900")
    native.audio(audio(900))
    with pytest.raises(ValueError, match="sample count"):
        native.audio(audio(901))
    monkeypatch.setenv(limits.ENV, "1800")
    native.audio(audio(1800, 48000))
    with pytest.raises(ValueError, match="sample count"):
        native.audio(audio(1801, 48000))


def test_host_ceiling_matches_the_native_library_reader():
    # A descriptor outside these bounds makes the plugin reject the WHOLE library.
    header = (ROOT / "include/additive_runtime_library.h").read_text(encoding="utf-8")
    assert f"library_float(s.duration,1,{limits.NATIVE_AUDIO_SECONDS})" in header
    assert f"s.bytes>{limits.NATIVE_BUNDLE_BYTES}" in header
    library = (ROOT / "tools/additive_library.py").read_text(encoding="utf-8")
    assert f"integer(s['bytes'],1,{limits.NATIVE_BUNDLE_BYTES})" in library
    assert f"real(s['duration'],1,{limits.NATIVE_AUDIO_SECONDS})" in library
    assert limits.DEFAULT_AUDIO_SECONDS <= limits.NATIVE_AUDIO_SECONDS == 1800


@pytest.mark.parametrize("path", USERS)
def test_no_private_copy_of_the_audio_limit_remains(path):
    text = (ROOT / path).read_text(encoding="utf-8")
    assert "max_audio_seconds()" in text
    # 599/601 catch bounds derived from the limit, e.g. one seek entry per second.
    assert not re.search(r"\b(599|600|601)\b", text), "duplicate audio limit in " + path
