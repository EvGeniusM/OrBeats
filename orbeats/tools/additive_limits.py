"""One audio-length limit for every additive host validator.

The native library reader (include/additive_runtime_library.h) accepts
descriptors up to NATIVE_AUDIO_SECONDS and NATIVE_BUNDLE_BYTES and rejects the
WHOLE library when one entry is outside them. The host default is lower: it is
the longest length exercised on hardware so far (user-confirmed 2026-11-12,
plugin 0.8079: a 780 s dense synthetic Vorbis song and the 735 s map 475ef).
The 780 s PCM16 fallback and 48 kHz variants were not played. Longer audio is
unproven on PS4 memory, so it is reachable only through an explicit developer
environment variable used by test-song tooling, never through the companion
UI or CLI. Raise the default only with new hardware evidence.
"""

from __future__ import annotations

import os
import re

DEFAULT_AUDIO_SECONDS = 780
NATIVE_AUDIO_SECONDS = 1800
NATIVE_BUNDLE_BYTES = 200000000
ENV = "ORBEATS_DEV_MAX_AUDIO_SECONDS"


def max_audio_seconds() -> int:
    raw = os.environ.get(ENV, "")
    if raw == "":
        return DEFAULT_AUDIO_SECONDS
    if not re.fullmatch(r"[0-9]{1,4}", raw) or not DEFAULT_AUDIO_SECONDS <= int(raw) <= NATIVE_AUDIO_SECONDS:
        raise ValueError(f"{ENV} must be a whole number from {DEFAULT_AUDIO_SECONDS} to {NATIVE_AUDIO_SECONDS}")
    return int(raw)
