"""Chroma colors (level 1), ring/laser parameters (level 2) and V2 light
gradients (level 3) -> bounded sidecar.

<id>-chroma.bin is "BSDACHR1" when only colours are present and "BSDACHR2" when
ring or laser parameters or gradients are. Level 2 keys are accepted only for a
plugin that declared ORB_CHROMA_V2 (`level=2`), `_lightGradient` only for
ORB_CHROMA_V3 (`level=3`). Anything else a mapper stored in customData
stays unsupported and is reported, never silently dropped.
Binary layout: docs/plans/2026-11-12-chroma-stage3-native-colors-spec.md, section 4;
the production reader is include/additive_chroma.h. All numbers little-endian.

Event numbering mirrors the game: per event type, 1-based, after a stable sort
by time of the events the importer really writes. Equal-time order is unproven
on hardware (spec 3.3), so it is the file order.
"""

import math
import re
import struct

MAGIC = b"BSDACHR1"
MAGIC_V2 = b"BSDACHR2"
MAX_BYTES = 8 << 20
LIMIT = 200_000
MAX_CHARTS = 25
MAX_LIGHTS = 1024
MAX_TIME = 3600.0
MAX_COLOR = 16.0
EVENT_KEYS = frozenset({"color", "_color"})  # lightID/_lightID: accepted, not written before stage 3b
LIGHT_KEYS = frozenset({"lightID", "_lightID"})
# Level 2 (stage 4a/4b): ring rotation/zoom and laser parameters. Chroma reads
# them on every event; only the ring (8, 9) and laser (12, 13) effects use them.
EFFECT_KEYS_V2 = frozenset({"_direction", "_lockPosition", "_speed", "_preciseSpeed", "_step", "_prop", "_rotation",
                            "_stepMult", "_propMult", "_speedMult", "_reset", "_counterSpin", "_nameFilter"})
EFFECT_KEYS_V3 = frozenset({"direction", "lockRotation", "speed", "step", "prop", "rotation", "nameFilter"})
# Mappers use 10000 for "instant" and multipliers up to 1e5 (BeatSaver 1dcf4, 1b6c2).
MAX_EFFECT = 100000.0
MAX_ROTATION = 3600.0
MAX_MULT = 100000.0
MAX_NAME = 32
FLAG_COLOR, FLAG_DIRECTION, FLAG_LOCK, FLAG_SPEED, FLAG_STEP, FLAG_PROP = 1, 2, 4, 8, 16, 32
FLAG_ROTATION, FLAG_RESET, FLAG_COUNTER_SPIN, FLAG_NAME = 64, 128, 256, 512
FLAG_GRADIENT = 1024
FLAG_MASK = 2047
# Level 3 (stage 4c): V2 light gradients, played by the game's own ColorTween.
GRADIENT_KEYS_V2 = frozenset({"_lightGradient"})
GRADIENT_FIELDS = frozenset({"_duration", "_startColor", "_endColor", "_easing"})
MAX_GRADIENT = 60.0  # seconds
# Chroma easing name -> the game's EaseType value (2.04 dump). easeStep has no
# game equivalent and is refused.
_EASE_BASES = ("Sine", "Quad", "Cubic", "Quart", "Quint", "Expo", "Circ", "Back", "Elastic", "Bounce")
EASINGS = {"easeLinear": 1}
for _n, _base in enumerate(_EASE_BASES):
    for _k, _kind in enumerate(("In", "Out", "InOut")):
        EASINGS["ease" + _kind + _base] = 2 + 3 * _n + _k
OBJECT_KEYS = frozenset({"color", "_color"})
# Top-level chart customData keys the importers already accept as cosmetic.
COSMETIC_V3 = frozenset({"lighter", "time", "bookmarks", "bookmarksUseOfficialBpmEvents"})
COSMETIC_V2 = frozenset(
    {"_bookmarks", "_bookmarksUseOfficialBpmEvents", "_atlasBeatsMap", "_time", "_BPMChanges", "_bpmChanges"}
)
V3_LISTS = (
    ("colorNotes", "note"),
    ("bombNotes", "bomb"),
    ("obstacles", "wall"),
    ("sliders", "arc"),
    ("burstSliders", "chain"),
    ("basicBeatmapEvents", "event"),
)
V2_LISTS = (("_notes", "note"), ("_obstacles", "wall"), ("_events", "event"))
_ID = re.compile(r"bsd_additive_[0-9a-f]{24}")


def is_v2(chart):
    return "_version" in chart and "version" not in chart


def _number(value, what):
    if type(value) not in (int, float) or not math.isfinite(value):
        raise ValueError("invalid " + what)
    return value


def color(value):
    """3 or 4 finite numbers in 0..16; alpha defaults to 1."""
    if not isinstance(value, list) or len(value) not in (3, 4):
        raise ValueError("invalid Chroma color")
    out = [float(_number(v, "Chroma color component")) for v in value]
    if len(out) == 3:
        out.append(1.0)
    if any(not 0.0 <= v <= MAX_COLOR for v in out):
        raise ValueError("Chroma color component outside 0..16")
    return out


def _custom(entry, legacy):
    custom = entry.get("_customData" if legacy else "customData")
    if custom is None:
        return {}
    if not isinstance(custom, dict):
        raise ValueError("customData must be an object")
    return custom


def _light_ids(value):
    ids = value if isinstance(value, list) else [value]
    if len(ids) > MAX_LIGHTS or any(type(i) is not int or not 0 <= i <= 0xFFFF for i in ids):
        raise ValueError("invalid Chroma lightID")
    return ids


def unsupported_keys(chart, level=1):
    """Sorted customData keys outside the whitelist of `level`, prefixed by place."""
    legacy = is_v2(chart)
    effect_keys = (EFFECT_KEYS_V2 if legacy else EFFECT_KEYS_V3) if level >= 2 else frozenset()
    if legacy and level >= 3:
        effect_keys = effect_keys | GRADIENT_KEYS_V2
    found = set()
    lists = V2_LISTS if legacy else V3_LISTS
    known = {name for name, _ in lists}
    for name, group in lists:
        for entry in chart.get(name) or []:
            if not isinstance(entry, dict):
                continue
            custom = _custom(entry, legacy)
            place, allowed = group, EVENT_KEYS | LIGHT_KEYS | effect_keys if group == "event" else OBJECT_KEYS
            if group == "bomb" or (legacy and group == "note" and entry.get("_type") == 3):
                place, allowed = "bomb", frozenset()
            elif legacy and group == "event" and entry.get("_type") in (14, 15):
                place, allowed = "rotation", frozenset()
            found.update(place + "." + k for k in custom if k not in allowed)
    cosmetic = COSMETIC_V2 if legacy else COSMETIC_V3
    top = _custom(chart, legacy)
    for key, value in top.items():
        if key in cosmetic:
            continue
        if key in ("_requirements", "_suggestions") and value == []:
            continue
        found.add("chart." + key)
    if not legacy:
        # Any other object list (event boxes, waypoints...) may not carry customData.
        for name, value in chart.items():
            if name in known or not isinstance(value, list):
                continue
            for entry in value:
                if isinstance(entry, dict) and entry.get("customData"):
                    found.update(name + "." + k for k in entry["customData"])
    return sorted(found)


def strip(chart):
    """Copy of the chart without per-object customData; call only when unsupported_keys is empty."""
    legacy = is_v2(chart)
    custom_key = "_customData" if legacy else "customData"
    out = dict(chart)
    for name, _ in V2_LISTS if legacy else V3_LISTS:
        if isinstance(chart.get(name), list):
            out[name] = [
                {k: v for k, v in e.items() if k != custom_key} if isinstance(e, dict) else e for e in chart[name]
            ]
    return out


def _flag(custom, key):
    value = custom.get(key)
    if value is None:
        return False
    if type(value) is not bool:
        raise ValueError("invalid Chroma " + key)
    return value


def _effect_number(custom, keys, low, high, what):
    for key in keys:
        if key in custom and custom[key] is not None:
            value = _number(custom[key], "Chroma " + what)
            if not low <= value <= high:
                raise ValueError("Chroma " + what + " outside " + str(low) + ".." + str(high))
            return float(value)
    return None


def _effects(custom, legacy):
    """Level-2 parameters of one event: (flags, fields) with Chroma's defaults."""
    k = (lambda v2, v3: v2 if legacy else v3)
    flags, out = 0, dict(direction=0, speed=0.0, step=0.0, prop=0.0, rotation=0.0,
                         step_mult=1.0, prop_mult=1.0, speed_mult=1.0, name="")
    direction = custom.get(k("_direction", "direction"))
    if direction is not None:
        if type(direction) is not int or direction not in (0, 1):
            raise ValueError("Chroma direction must be 0 or 1")
        flags |= FLAG_DIRECTION
        out["direction"] = direction
    if _flag(custom, k("_lockPosition", "lockRotation")):
        flags |= FLAG_LOCK
    for flag, name, keys, low, high in (
        (FLAG_SPEED, "speed", k(("_speed", "_preciseSpeed"), ("speed",)), -MAX_EFFECT, MAX_EFFECT),
        (FLAG_STEP, "step", (k("_step", "step"),), -MAX_EFFECT, MAX_EFFECT),
        (FLAG_PROP, "prop", (k("_prop", "prop"),), -MAX_EFFECT, MAX_EFFECT),
        (FLAG_ROTATION, "rotation", (k("_rotation", "rotation"),), -MAX_ROTATION, MAX_ROTATION),
    ):
        value = _effect_number(custom, keys, low, high, name)
        if value is not None:
            flags |= flag
            out[name] = value
    if legacy:
        for name, key in (("step_mult", "_stepMult"), ("prop_mult", "_propMult"), ("speed_mult", "_speedMult")):
            value = _effect_number(custom, (key,), 0.0, MAX_MULT, name)
            if value is not None:
                out[name] = value
        if _flag(custom, "_reset"):
            flags |= FLAG_RESET
        if _flag(custom, "_counterSpin"):
            flags |= FLAG_COUNTER_SPIN
    name = custom.get(k("_nameFilter", "nameFilter"))
    if name is not None:
        if not isinstance(name, str) or not 0 < len(name) <= MAX_NAME or any(not " " <= ch <= "~" for ch in name):
            raise ValueError("invalid Chroma nameFilter")
        flags |= FLAG_NAME
        out["name"] = name.lower()
    return flags, out


def _gradient(value, beat, seconds):
    """V2 `_lightGradient` -> dict(start, end, duration seconds, easing) or ValueError."""
    if not isinstance(value, dict) or set(value) - GRADIENT_FIELDS or not {"_duration", "_startColor", "_endColor"} <= set(value):
        raise ValueError("invalid Chroma _lightGradient")
    beats = _number(value["_duration"], "Chroma gradient duration")
    duration = float(seconds(_number(beat, "event beat") + beats)) - float(seconds(beat))
    if not 0.0 < duration <= MAX_GRADIENT:
        raise ValueError("Chroma gradient duration outside 0..60 s")
    easing = value.get("_easing", "easeLinear")
    if easing not in EASINGS:
        raise ValueError("unsupported Chroma gradient easing " + str(easing)[:40])
    return dict(start=color(value["_startColor"]), end=color(value["_endColor"]), duration=duration, easing=EASINGS[easing])


def _neutral(fields):
    return (fields["step_mult"], fields["prop_mult"], fields["speed_mult"]) == (1.0, 1.0, 1.0)


def _events(chart, legacy, seconds, level=1):
    if legacy:
        rows = [
            (e.get("_time", 0), e.get("_type"), e.get("_value", 0), _custom(e, True), "_color", "_lightID")
            for e in chart.get("_events") or []
            if e.get("_type") not in (14, 15)  # become rotationEvents, never light events
        ]
    else:
        rows = [
            (e.get("b", 0), e.get("et", 0), e.get("i", 0), _custom(e, False), "color", "lightID")
            for e in chart.get("basicBeatmapEvents") or []
        ]
    order = sorted(range(len(rows)), key=lambda i: _number(rows[i][0], "event beat"))  # stable
    counters, out = {}, []
    for i in order:
        beat, kind, value, custom, color_key, light_key = rows[i]
        if type(kind) is not int or type(value) is not int:
            raise ValueError("invalid event type or value")
        counters[kind] = counters.get(kind, 0) + 1
        if light_key in custom:
            _light_ids(custom[light_key])  # validated, intentionally not written
        flags, fields = _effects(custom, legacy) if level >= 2 else (0, None)
        if color_key in custom:
            flags |= FLAG_COLOR
        gradient = None
        if legacy and level >= 3 and custom.get("_lightGradient") is not None:
            gradient = _gradient(custom["_lightGradient"], beat, seconds)
            flags |= FLAG_GRADIENT
        if flags or (fields and not _neutral(fields)):
            record = dict(
                type=kind,
                index=counters[kind],
                time=float(seconds(beat)),
                value=value,
                rgba=color(custom[color_key]) if color_key in custom else [0.0, 0.0, 0.0, 0.0],
                lights=[],
            )
            if level >= 2:
                record.update(flags=flags, **fields)
            if gradient:
                record["gradient"] = gradient
            out.append(record)
    out.sort(key=lambda e: (e["type"], e["index"]))
    return out


def extract(chart, seconds, level=1):
    """Chroma records of the chart the importer writes; seconds(beat) -> float."""
    legacy = is_v2(chart)
    notes, walls = [], []

    def note(entry, x, y, c, beat, kind, key):
        custom = _custom(entry, legacy)
        if key in custom:
            notes.append(
                dict(time=float(seconds(beat)), x=x, y=y, color_type=c, kind=kind, rgba=color(custom[key]))
            )

    if legacy:
        for n in chart.get("_notes") or []:
            if n.get("_type") in (0, 1):
                note(n, n.get("_lineIndex", 0), n.get("_lineLayer", 0), n["_type"], n.get("_time", 0), 0, "_color")
        wall_rows = [
            (w, w.get("_time", 0), w.get("_lineIndex", 0), 2 if w.get("_type") == 1 else 0,
             w.get("_width", 0), 3 if w.get("_type") == 1 else 5, w.get("_duration", 0), "_color")
            for w in chart.get("_obstacles") or []
        ]
    else:
        for key, kind in (("colorNotes", 0), ("sliders", 1), ("burstSliders", 2)):
            for n in chart.get(key) or []:
                note(n, n.get("x", 0), n.get("y", 0), n.get("c", 0), n.get("b", 0), kind, "color")
        wall_rows = [
            (w, w.get("b", 0), w.get("x", 0), w.get("y", 0), w.get("w", 0), w.get("h", 0), w.get("d", 0), "color")
            for w in chart.get("obstacles") or []
        ]
    for entry, beat, x, y, w, h, duration, key in wall_rows:
        custom = _custom(entry, legacy)
        if key in custom:
            start = float(seconds(beat))
            end = float(seconds(_number(beat, "wall beat") + _number(duration, "wall duration")))
            walls.append(dict(time=start, x=x, y=y, w=w, h=h, duration=end - start, rgba=color(custom[key])))
    notes.sort(key=lambda v: v["time"])
    walls.sort(key=lambda v: v["time"])
    return {"events": _events(chart, legacy, seconds, level), "notes": notes, "walls": walls}


def _u8(value, what):
    if type(value) is not int or not 0 <= value <= 255:
        raise ValueError("invalid " + what)
    return value


def _i8(value, what):
    if type(value) is not int or not -128 <= value <= 127:
        raise ValueError("invalid " + what)
    return value


def _real(value, low, high, what):
    if type(value) not in (int, float) or not math.isfinite(value) or not low <= value <= high:
        raise ValueError("invalid " + what)
    return struct.pack("<f", value)


def _rgba(value):
    if not isinstance(value, (list, tuple)) or len(value) != 4:
        raise ValueError("invalid Chroma color")
    return b"".join(_real(v, 0.0, MAX_COLOR, "Chroma color component") for v in value)


def checksum(payload):
    value = 14695981039346656037
    for byte in payload:
        value = ((value ^ byte) * 1099511628211) & ((1 << 64) - 1)
    return value


def _v2_needed(charts):
    """True when any event carries ring/laser data (otherwise BSDACHR1 is written)."""
    return any(e.get("flags", FLAG_COLOR) != FLAG_COLOR or not _neutral(dict(
        step_mult=e.get("step_mult", 1.0), prop_mult=e.get("prop_mult", 1.0), speed_mult=e.get("speed_mult", 1.0)))
        for c in charts for e in c["events"])


def _event_v2(e):
    flags = e.get("flags", FLAG_COLOR)
    if type(flags) is not int or flags & ~FLAG_MASK:
        raise ValueError("invalid Chroma event flags")
    out = struct.pack("<H", flags)
    out += _rgba(e["rgba"] if flags & FLAG_COLOR else [0.0, 0.0, 0.0, 0.0])
    direction = e.get("direction", 0)
    if direction not in (0, 1) or type(direction) is not int or (not flags & FLAG_DIRECTION and direction):
        raise ValueError("invalid Chroma event direction")
    out += struct.pack("<B", direction)
    for flag, key, limit in ((FLAG_SPEED, "speed", MAX_EFFECT), (FLAG_STEP, "step", MAX_EFFECT),
                             (FLAG_PROP, "prop", MAX_EFFECT), (FLAG_ROTATION, "rotation", MAX_ROTATION)):
        value = e.get(key, 0.0)
        if not flags & flag and value != 0:
            raise ValueError("Chroma " + key + " without its flag")
        out += _real(value, -limit, limit, "Chroma " + key)
    for key in ("step_mult", "prop_mult", "speed_mult"):
        out += _real(e.get(key, 1.0), 0.0, MAX_MULT, "Chroma " + key)
    name = e.get("name", "").encode("ascii")
    if len(name) > MAX_NAME or bool(name) != bool(flags & FLAG_NAME) or any(not 0x20 <= c <= 0x7e or 0x41 <= c <= 0x5a for c in name):
        raise ValueError("invalid Chroma nameFilter")
    out += struct.pack("<B", len(name)) + name
    gradient = e.get("gradient")
    if bool(gradient) != bool(flags & FLAG_GRADIENT):
        raise ValueError("Chroma gradient without its flag")
    if gradient:
        easing = gradient["easing"]
        if type(easing) is not int or not 1 <= easing <= 31:
            raise ValueError("invalid Chroma gradient easing")
        duration = gradient["duration"]
        if type(duration) not in (int, float) or not 0.0 < duration <= MAX_GRADIENT:
            raise ValueError("invalid Chroma gradient duration")
        out += _rgba(gradient["start"]) + _rgba(gradient["end"]) + struct.pack("<f", duration) + struct.pack("<B", easing)
    return out


def _chart(item, version=1):
    out = bytearray()
    events, notes, walls = item["events"], item["notes"], item["walls"]
    for rows in (events, notes, walls):
        if len(rows) > LIMIT:
            raise ValueError("too many Chroma records")
    out += struct.pack("<I", len(events))
    last = None
    for e in events:
        kind, index = _u8(e["type"], "event type"), e["index"]
        if type(index) is not int or not 1 <= index <= 0xFFFFFFFF:
            raise ValueError("invalid event index")
        if type(e["value"]) is not int or not -(2**31) <= e["value"] < 2**31:
            raise ValueError("invalid event value")
        time = _real(e["time"], 0.0, MAX_TIME, "event time")
        lights = e.get("lights", [])
        if len(lights) > MAX_LIGHTS or any(type(i) is not int or not 0 <= i <= 0xFFFF for i in lights):
            raise ValueError("invalid event lights")
        if last is not None and (
            kind < last[0] or (kind == last[0] and (index <= last[1] or e["time"] < last[2]))
        ):
            raise ValueError("events are not sorted by type, index and time")
        last = (kind, index, e["time"])
        out += struct.pack("<BI", kind, index) + time + struct.pack("<i", e["value"])
        out += _event_v2(e) if version == 2 else _rgba(e["rgba"])
        out += struct.pack("<H", len(lights)) + b"".join(struct.pack("<H", i) for i in lights)
    out += struct.pack("<I", len(notes))
    previous = 0.0
    for n in notes:
        time = _real(n["time"], 0.0, MAX_TIME, "note time")
        if n["time"] < previous:
            raise ValueError("notes are not sorted by time")
        previous = n["time"]
        if n["color_type"] not in (0, 1) or type(n["color_type"]) is not int or n["kind"] not in (0, 1, 2):
            raise ValueError("invalid note color type or kind")
        out += time + bytes([_u8(n["x"], "note x"), _u8(n["y"], "note y"), n["color_type"], n["kind"]]) + _rgba(n["rgba"])
    out += struct.pack("<I", len(walls))
    previous = 0.0
    for w in walls:
        time = _real(w["time"], 0.0, MAX_TIME, "wall time")
        if w["time"] < previous:
            raise ValueError("walls are not sorted by time")
        previous = w["time"]
        # x/y are signed bytes: decorative walls outside the lanes (x = -1, 4) are legal.
        out += time + struct.pack("<bbBB", _i8(w["x"], "wall x"), _i8(w["y"], "wall y"), _u8(w["w"], "wall w"), _u8(w["h"], "wall h"))
        out += _real(w["duration"], -MAX_TIME, MAX_TIME, "wall duration") + _rgba(w["rgba"])
    return bytes(out)


def encode(level_id, charts):
    """Deterministic file bytes; every format limit raises ValueError."""
    if not isinstance(level_id, str) or not _ID.fullmatch(level_id):
        raise ValueError("invalid Chroma level ID")
    if not isinstance(charts, list) or not 1 <= len(charts) <= MAX_CHARTS:
        raise ValueError("Chroma sidecar needs 1..25 charts")
    version = 2 if _v2_needed(charts) else 1
    seen, body = set(), bytearray()
    ident = level_id.encode("ascii")
    body += struct.pack("<I", len(ident)) + ident + struct.pack("<I", len(charts))
    for item in charts:
        mode, difficulty = item["mode"], item["difficulty"]
        if type(mode) is not int or not 0 <= mode <= 4 or type(difficulty) is not int or not 0 <= difficulty <= 4:
            raise ValueError("invalid Chroma chart mode or difficulty")
        if (mode, difficulty) in seen:
            raise ValueError("duplicate Chroma chart")
        seen.add((mode, difficulty))
        body += bytes([mode, difficulty]) + _chart(item, version)
    data = (MAGIC_V2 if version == 2 else MAGIC) + struct.pack("<IQ", len(body), checksum(body)) + bytes(body)
    if len(data) > MAX_BYTES:
        raise ValueError("Chroma sidecar exceeds 8 MiB")
    return data


def sidecar_path(api, level_id):
    return api.BASE + "/" + level_id + "-chroma.bin"


def publish(ftp, api, level_id, content, run):
    """Closed-title stage/readback/rename, like score_source.publish.

    Returns True when this call created the file (the caller may then undo it),
    False when identical bytes were already there. Never overwrites other bytes.
    """
    path = sidecar_path(api, level_id)
    api.closed(ftp)
    existing = api.read(ftp, path, MAX_BYTES + 1, missing=True)
    if existing is not None:
        if existing != content:
            raise ValueError("conflicting Chroma sidecar; refusing overwrite")
        return False
    staged = api.stage(ftp, path, content, run)
    api.closed(ftp)
    if api.read(ftp, path, MAX_BYTES + 1, missing=True) is not None:
        raise ValueError("Chroma sidecar appeared concurrently")
    ftp.rename(staged, path)
    if api.read(ftp, path, MAX_BYTES + 1) != content:
        raise ValueError("Chroma sidecar final readback mismatch")
    return True


def discard(ftp, api, level_id, content):
    """Best-effort undo of a sidecar this run created; deletes only the exact bytes."""
    path = sidecar_path(api, level_id)
    try:
        api.closed(ftp)
        if api.read(ftp, path, MAX_BYTES + 1, missing=True) == content:
            ftp.delete(path)
    except Exception:
        pass  # Preserve the original failure; an orphan sidecar is inert without a library entry.
