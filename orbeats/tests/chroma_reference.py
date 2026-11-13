"""Reference Chroma sidecar shared by the Python and native format tests."""
import os
import sys

sys.path.insert(0, os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "tools")))

import additive_chroma  # noqa: E402

LEVEL_ID = "bsd_additive_0123456789abcdef01234567"


def reference_sidecar() -> bytes:
    """One Standard (0) / Expert (3) chart: 3 events, 2 notes, 1 wall."""
    chart = {
        "mode": 0,
        "difficulty": 3,
        "events": [
            dict(type=1, index=1, time=1.0, value=1, rgba=[1.0, 0.0, 0.0, 1.0], lights=[]),
            dict(type=1, index=2, time=1.0, value=5, rgba=[0.0, 1.0, 0.0, 0.5], lights=[]),
            dict(type=4, index=1, time=2.5, value=3, rgba=[2.0, 2.0, 2.0, 1.0], lights=[]),
        ],
        "notes": [
            dict(time=1.0, x=1, y=0, color_type=0, kind=0, rgba=[0.0, 0.0, 1.0, 1.0]),
            dict(time=3.0, x=2, y=1, color_type=1, kind=1, rgba=[1.0, 1.0, 0.0, 1.0]),
        ],
        "walls": [dict(time=4.0, x=0, y=0, w=1, h=5, duration=2.0, rgba=[1.0, 0.0, 1.0, 1.0])],
    }
    return additive_chroma.encode(LEVEL_ID, [chart])


def reference_sidecar_v2() -> bytes:
    """Standard/Expert with level-2/3 records, read back by the native decoder test:
    light gradient (1,1) at 0.5 s, value 5;
    ring rotation (8,1) right, step 12.5, name "bigtracklanerings", step x2;
    ring zoom (9,1) step 0.5 speed 3; laser (12,1) coloured, locked, speed 7.25, direction 0."""
    def event(type, index, time, value, flags, rgba=None, **fields):
        base = dict(type=type, index=index, time=time, value=value, rgba=rgba or [0.0] * 4, lights=[], flags=flags,
                    direction=0, speed=0.0, step=0.0, prop=0.0, rotation=0.0, step_mult=1.0, prop_mult=1.0,
                    speed_mult=1.0, name="")
        base.update(fields)
        return base
    c = additive_chroma
    chart = dict(mode=0, difficulty=3, notes=[], walls=[], events=[
        # Level 3: light gradient red -> half-transparent blue, 0.75 s, OutQuad (6).
        event(1, 1, 0.5, 5, c.FLAG_GRADIENT, gradient=dict(start=[1.0, 0.0, 0.0, 1.0], end=[0.0, 0.0, 1.0, 0.5], duration=0.75, easing=6)),
        event(8, 1, 1.0, 0, c.FLAG_DIRECTION | c.FLAG_STEP | c.FLAG_NAME, direction=1, step=12.5, step_mult=2.0,
              name="bigtracklanerings"),
        event(9, 1, 1.0, 0, c.FLAG_STEP | c.FLAG_SPEED, step=0.5, speed=3.0),
        event(12, 1, 2.0, 4, c.FLAG_COLOR | c.FLAG_LOCK | c.FLAG_SPEED | c.FLAG_DIRECTION, rgba=[1.0, 0.0, 0.0, 1.0],
              speed=7.25, direction=0),
    ])
    return additive_chroma.encode(LEVEL_ID, [chart])


def reference_sidecar_v3() -> bytes:
    """Level 4: Standard/Expert keeps one coloured event (4,1) and changes FitBeatEnvironment:
    Regex "Spectrograms\\.\\[0\\]Spectrogram1$" off; Contains "NeonTube" scale 1.5, V2 position
    (0, 7, 0) lanes = (0, 4.2, 0) units, rotation z 90. Standard/Easy has no environment block."""
    c = additive_chroma
    entries = c.environment({"_version": "2.6.0", "_customData": {"_environment": [
        {"_id": r"Spectrograms\.\[0\]Spectrogram1$", "_lookupMethod": "Regex", "_active": False},
        {"_id": "NeonTube", "_lookupMethod": "Contains", "_scale": [1.5, 1.5, 1.5], "_position": [0, 7, 0], "_rotation": [0, 0, 90]},
    ]}})
    expert = dict(mode=0, difficulty=3, notes=[], walls=[], environment=dict(name="FitBeatEnvironment", entries=entries),
                  events=[dict(type=4, index=1, time=1.0, value=1, rgba=[0.0, 1.0, 0.0, 1.0], lights=[], flags=c.FLAG_COLOR)])
    easy = dict(mode=0, difficulty=0, notes=[], walls=[], events=[
        dict(type=1, index=1, time=1.0, value=1, rgba=[1.0, 0.0, 0.0, 1.0], lights=[], flags=c.FLAG_COLOR)])
    return additive_chroma.encode(LEVEL_ID, [easy, expert])


def reference_sidecar_v4() -> bytes:
    """Level 5: BigMirrorEnvironment, Standard/ExpertPlus. Entry 1 copies "[19]FrontLights.[0]NeonTube$"
    ten times (Regex, duplicate 10, nothing else); entry 2 is Exact "x" with duplicate 2,
    scale (2, 1, 2) and V2 position (0, 10, 0) lanes = (0, 6, 0) units."""
    c = additive_chroma
    entries = c.environment({"_version": "2.2.0", "_customData": {"_environment": [
        {"_id": r"\[19\]FrontLights\.\[0\]NeonTube$", "_lookupMethod": "Regex", "_duplicate": 10},
        {"_id": "x", "_lookupMethod": "Exact", "_duplicate": 2, "_scale": [2, 1, 2], "_position": [0, 10, 0]},
    ]}}, level=5)
    chart = dict(mode=0, difficulty=4, notes=[], walls=[], events=[], environment=dict(name="BigMirrorEnvironment", entries=entries))
    return additive_chroma.encode(LEVEL_ID, [chart])
