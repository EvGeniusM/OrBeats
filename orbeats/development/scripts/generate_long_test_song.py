#!/usr/bin/env python3
"""Generate a synthetic long-audio test song folder; never connects to a console.

Development-only input for the long-audio hardware experiment
(docs/plans/2026-11-12-modded-maps-and-long-audio.md, section 6). Everything is
synthesized: low-volume noise, a pad and a beat click, plus a deterministic
chart. No third-party music or charts. Keep the output out of version control.

The folder is an ordinary InfoV2 + V3 source for
``full_custom_song_pipeline.py --additive-install --song-dir <folder>``.
Songs longer than the host default need the developer override printed by this
script (ORBEATS_DEV_MAX_AUDIO_SECONDS); the companion UI never sets it.

Profiles:
  sparse  a note every four beats and a few light events (baseline).
  dense   roughly 3,000 notes, 300 arcs and 40,000 basic light events per 13
          minutes. It deliberately uses basic events on DefaultEnvironment and
          does NOT reproduce event-box-group lighting, whose group IDs are
          environment-specific.
"""

from __future__ import annotations

import argparse
import json
import math
import sys
import wave
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools"))
from additive_limits import DEFAULT_AUDIO_SECONDS, ENV, NATIVE_AUDIO_SECONDS  # noqa: E402

BPM = 120
NJS = 12
PROFILES = ("sparse", "dense")
RATES = (44100, 48000)
CODECS = ("vorbis", "pcm")
LIGHT_VALUES = (1, 5, 2, 6, 3, 7, 0)


def check(minutes, profile, rate, codec):
    if type(minutes) is not int or not 1 <= minutes * 60 <= NATIVE_AUDIO_SECONDS:
        raise ValueError(f"minutes must be a whole number from 1 to {NATIVE_AUDIO_SECONDS // 60}")
    if profile not in PROFILES:
        raise ValueError("profile must be one of: " + ", ".join(PROFILES))
    if rate not in RATES or codec not in CODECS:
        raise ValueError("rate must be 44100 or 48000 and codec vorbis or pcm")
    if codec == "pcm" and rate != 44100:
        raise ValueError("the PCM16 path stores 44100 Hz only; use vorbis to test 48000 Hz")


def build_chart(minutes, profile):
    """Deterministic vanilla V3 chart: only fields the strict importer accepts."""
    check(minutes, profile, 44100, "pcm")
    beats = minutes * BPM
    first, last = 8, beats - 8
    notes, arcs, walls, lights = [], [], [], []
    if profile == "dense":
        for half in range(first * 2, last * 2):
            right, hit = half % 2, half // 2
            notes.append({"b": half / 2, "x": 2 if right else 1, "y": 0, "c": right, "d": hit % 2, "a": 0})
        for beat in range(first, last - 1, 5):
            arcs.append({"b": beat, "c": 0, "x": 1, "y": 0, "d": 1, "mu": 1,
                         "tb": beat + 1, "tx": 1, "ty": 2, "tc": 0, "tmu": 1, "m": 0})
        for beat in range(first + 16, last - 4, 32):
            walls.append({"b": beat, "x": 0, "y": 0, "d": 2, "w": 1, "h": 5})
        count = round(40500 * minutes / 13)
    else:
        for index, beat in enumerate(range(first, last, 4)):
            notes.append({"b": beat, "x": 2 if index % 2 else 1, "y": 0, "c": index % 2, "d": 1, "a": 0})
        for beat in range(first + 16, last - 4, 64):
            walls.append({"b": beat, "x": 0, "y": 0, "d": 2, "w": 1, "h": 5})
        count = min(300, beats // 4)
    span = last - first
    for index in range(count):
        lights.append({"b": round(first + span * index / count, 4), "et": index % 5,
                       "i": LIGHT_VALUES[index % len(LIGHT_VALUES)], "f": 1})
    return {"version": "3.3.0", "colorNotes": notes, "sliders": arcs, "obstacles": walls,
            "basicBeatmapEvents": lights}


def title(minutes, profile, rate, codec):
    return f"OrBeats Long Test {minutes} min {profile} {codec} {rate // 1000}k"


def info_for(minutes, profile, rate, codec):
    check(minutes, profile, rate, codec)
    return {
        "_version": "2.1.0", "_songName": title(minutes, profile, rate, codec), "_songSubName": "",
        "_songAuthorName": "OrBeats generated test", "_levelAuthorName": "OrBeats",
        "_beatsPerMinute": BPM, "_songTimeOffset": 0, "_previewStartTime": 10, "_previewDuration": 10,
        "_songFilename": "song.ogg" if codec == "vorbis" else "song.wav", "_coverImageFilename": "cover.png",
        "_environmentName": "DefaultEnvironment",
        "_difficultyBeatmapSets": [{"_beatmapCharacteristicName": "Standard", "_difficultyBeatmaps": [
            {"_difficulty": "Expert", "_difficultyRank": 7, "_beatmapFilename": "ExpertStandard.dat",
             "_noteJumpMovementSpeed": NJS, "_noteJumpStartBeatOffset": 0}]}],
    }


def summary(minutes, profile, rate, codec):
    check(minutes, profile, rate, codec)
    seconds = minutes * 60
    over = seconds > DEFAULT_AUDIO_SECONDS
    return {"title": title(minutes, profile, rate, codec), "seconds": seconds, "needs_override": over,
            "install_environment": {ENV: str(seconds)} if over else {}}


def write_audio(path, seconds, rate, codec):
    """Non-silent stereo: quiet noise, a slow pad and a click on every beat."""
    import numpy as np

    rng = np.random.default_rng(20261112)
    beat = int(rate * 60 / BPM)
    click = np.sin(2 * math.pi * 880 * np.arange(int(rate * 0.03)) / rate) * np.linspace(0.08, 0, int(rate * 0.03))

    def block(second):
        t = (np.arange(rate) + second * rate) / rate
        mono = 0.03 * np.sin(2 * math.pi * 220 * t) * (0.6 + 0.4 * np.sin(2 * math.pi * 0.25 * t))
        for start in range(0, rate, beat):
            end = min(rate, start + len(click))
            mono[start:end] += click[: end - start]
        data = np.stack([mono, mono], axis=1) + rng.standard_normal((rate, 2)) * 0.01
        return np.clip(data, -1, 1)

    if codec == "pcm":
        with wave.open(str(path), "wb") as w:
            w.setnchannels(2)
            w.setsampwidth(2)
            w.setframerate(rate)
            for second in range(seconds):
                w.writeframes((block(second) * 32767).astype("<i2").tobytes())
        return
    import soundfile

    with soundfile.SoundFile(str(path), "w", samplerate=rate, channels=2, format="OGG", subtype="VORBIS") as f:
        for second in range(seconds):
            f.write(block(second))


def generate(output, minutes, profile, rate, codec):
    result = summary(minutes, profile, rate, codec)
    output = Path(output).resolve()
    if output.exists():
        raise ValueError("output already exists: " + str(output))
    info = info_for(minutes, profile, rate, codec)
    chart = build_chart(minutes, profile)
    from PIL import Image

    output.mkdir(parents=True)
    write_audio(output / info["_songFilename"], result["seconds"], rate, codec)
    Image.new("RGB", (256, 256), (28, 62, 94)).save(output / "cover.png")
    (output / "ExpertStandard.dat").write_text(json.dumps(chart), encoding="utf-8")
    (output / "Info.dat").write_text(json.dumps(info, indent=2) + "\n", encoding="utf-8")
    return dict(result, folder=str(output), notes=len(chart["colorNotes"]), arcs=len(chart["sliders"]),
                light_events=len(chart["basicBeatmapEvents"]))


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--output", type=Path, required=True, help="new folder to create")
    parser.add_argument("--minutes", type=int, required=True)
    parser.add_argument("--profile", choices=PROFILES, default="dense")
    parser.add_argument("--rate", type=int, choices=RATES, default=44100)
    parser.add_argument("--codec", choices=CODECS, default="vorbis")
    args = parser.parse_args()
    try:
        result = generate(args.output, args.minutes, args.profile, args.rate, args.codec)
    except ValueError as exc:
        parser.error(str(exc))
    print(json.dumps(result, indent=2))
    if result["needs_override"]:
        print(f"\nLonger than the {DEFAULT_AUDIO_SECONDS} s host default. Install only for the hardware "
              f"experiment, with Beat Saber closed, after setting {ENV}={result['seconds']} for that one command.")


if __name__ == "__main__":
    main()
