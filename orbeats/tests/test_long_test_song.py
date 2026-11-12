"""Synthetic long-audio test songs: generated content only, no console, no third-party music."""
import importlib.util
import json
import sys
import wave
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
import additive_limits as limits
import additive_v3_native as v3native

spec = importlib.util.spec_from_file_location("long_song", ROOT / "development/scripts/generate_long_test_song.py")
gen = importlib.util.module_from_spec(spec)
spec.loader.exec_module(gen)


def audio(seconds, rate=44100):
    count = rate * seconds
    return {"version": "4.0.0", "songSampleCount": count, "songFrequency": rate,
            "bpmData": [{"si": 0, "ei": count, "sb": 0, "eb": seconds * gen.BPM / 60}]}


def test_dense_chart_matches_the_target_load_and_passes_the_strict_native_schema(monkeypatch):
    monkeypatch.setenv(limits.ENV, "780")
    chart = gen.build_chart(13, "dense")
    assert chart == gen.build_chart(13, "dense")  # deterministic
    assert len(chart["colorNotes"]) >= 3000 and len(chart["sliders"]) >= 300
    assert len(chart["basicBeatmapEvents"]) >= 40000 and chart["obstacles"]
    assert set(chart) == {"version", "colorNotes", "sliders", "obstacles", "basicBeatmapEvents"}
    end = 13 * 60 * gen.BPM / 60
    assert all(0 < n["b"] < end for n in chart["colorNotes"])
    out, light = v3native.convert(chart, audio(780), gen.NJS)
    assert len(out["arcs"]) == len(chart["sliders"]) and len(light["basicEvents"]) == len(chart["basicBeatmapEvents"])


def test_sparse_chart_is_small_and_valid():
    chart = gen.build_chart(8, "sparse")
    assert 100 <= len(chart["colorNotes"]) <= 400 and len(chart["basicBeatmapEvents"]) <= 400
    v3native.convert(chart, audio(480), gen.NJS)


def test_generated_folder_is_a_complete_importable_source(tmp_path):
    out = tmp_path / "song"
    summary = gen.generate(out, minutes=1, profile="sparse", rate=44100, codec="pcm")
    info = json.loads((out / "Info.dat").read_text(encoding="utf-8"))
    assert info["_version"] == "2.1.0" and info["_songFilename"] == "song.wav" and info["_beatsPerMinute"] == gen.BPM
    assert info["_environmentName"] == "DefaultEnvironment" and "1 min" in info["_songName"]
    sets = info["_difficultyBeatmapSets"]
    assert [s["_beatmapCharacteristicName"] for s in sets] == ["Standard"]
    chart = json.loads((out / sets[0]["_difficultyBeatmaps"][0]["_beatmapFilename"]).read_text(encoding="utf-8"))
    assert chart == gen.build_chart(1, "sparse")
    with wave.open(str(out / "song.wav")) as w:
        assert (w.getnchannels(), w.getsampwidth(), w.getframerate(), w.getnframes()) == (2, 2, 44100, 60 * 44100)
        frames = w.readframes(44100)
    assert any(frames) and (out / "cover.png").read_bytes().startswith(b"\x89PNG")
    assert summary["seconds"] == 60 and summary["needs_override"] is False
    assert sorted(p.name for p in out.iterdir()) == ["ExpertStandard.dat", "Info.dat", "cover.png", "song.wav"]


def test_vorbis_variant_keeps_exact_length_and_rate(tmp_path):
    soundfile = pytest.importorskip("soundfile")
    out = tmp_path / "song"
    summary = gen.generate(out, minutes=1, profile="sparse", rate=48000, codec="vorbis")
    data = soundfile.info(str(out / "song.ogg"))
    assert (data.format, data.subtype, data.samplerate, data.channels, data.frames) == ("OGG", "VORBIS", 48000, 2, 60 * 48000)
    assert json.loads((out / "Info.dat").read_text(encoding="utf-8"))["_songFilename"] == "song.ogg"
    assert summary["install_environment"] == {}


def test_variants_differ_and_bad_requests_refuse(tmp_path):
    a = gen.info_for(13, "dense", 44100, "vorbis")
    b = gen.info_for(13, "dense", 44100, "pcm")
    assert a["_songName"] != b["_songName"]  # distinct level IDs per variant
    assert gen.summary(13, "dense", 44100, "vorbis")["install_environment"] == {limits.ENV: "780"}
    (tmp_path / "exists").mkdir()
    for kwargs in (dict(minutes=0), dict(minutes=31), dict(minutes=1.5), dict(profile="huge"), dict(rate=22050),
                   dict(codec="mp3"), dict(rate=48000, codec="pcm")):
        with pytest.raises(ValueError):
            gen.generate(tmp_path / "bad", **{"minutes": 1, "profile": "sparse", "rate": 44100, "codec": "pcm", **kwargs})
    assert not (tmp_path / "bad").exists()
    with pytest.raises(ValueError, match="exists"):
        gen.generate(tmp_path / "exists", minutes=1, profile="sparse", rate=44100, codec="pcm")
