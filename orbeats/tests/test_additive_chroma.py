"""Chroma level-1 sidecar: extraction, whitelist and byte-exact encoding."""
import struct

import pytest

import additive_chroma as chroma
from chroma_reference import LEVEL_ID, reference_sidecar


def sec(beat):
    return beat * 0.5  # 120 BPM


def v3(**lists):
    return {"version": "3.3.0", **lists}


def v2(**lists):
    return {"_version": "2.6.0", **lists}


def test_extract_v3_events_numbered_per_type_after_stable_sort():
    chart = v3(basicBeatmapEvents=[
        {"b": 4, "et": 1, "i": 1, "customData": {"color": [1, 0, 0]}},
        {"b": 2, "et": 1, "i": 5},
        {"b": 2, "et": 1, "i": 6, "customData": {"color": [0, 1, 0, 0.5]}},
        {"b": 2, "et": 0, "i": 3, "customData": {"color": [0, 0, 1], "lightID": [1, 2]}},
        {"b": 1, "et": 4, "i": 2, "customData": {"lightID": 3}},
    ])
    out = chroma.extract(chart, sec)["events"]
    assert [(e["type"], e["index"], e["time"], e["value"]) for e in out] == [
        (0, 1, 1.0, 3), (1, 2, 1.0, 6), (1, 3, 2.0, 1)]
    assert out[0]["rgba"] == [0.0, 0.0, 1.0, 1.0] and out[1]["rgba"] == [0.0, 1.0, 0.0, 0.5]
    assert all(e["lights"] == [] for e in out)


def test_extract_equal_time_keeps_file_order():
    chart = v3(basicBeatmapEvents=[
        {"b": 1, "et": 2, "i": 1}, {"b": 1, "et": 2, "i": 2, "customData": {"color": [1, 1, 1]}}])
    assert chroma.extract(chart, sec)["events"][0]["index"] == 2


def test_extract_v2_both_key_spellings_and_rotation_events_ignored():
    chart = v2(
        _events=[
            {"_time": 3, "_type": 2, "_value": 7, "_customData": {"_color": [1, 0, 0, 1]}},
            {"_time": 1, "_type": 14, "_value": 2},
            {"_time": 2, "_type": 2, "_value": 1},
        ],
        _notes=[
            {"_time": 2, "_lineIndex": 1, "_lineLayer": 0, "_type": 1, "_cutDirection": 1, "_customData": {"_color": [0, 0, 1]}},
            {"_time": 1, "_lineIndex": 0, "_lineLayer": 0, "_type": 3, "_cutDirection": 1},
        ],
        _obstacles=[
            {"_time": 4, "_lineIndex": 2, "_type": 1, "_duration": 2, "_width": 1, "_customData": {"_color": [1, 0, 1]}},
        ],
    )
    out = chroma.extract(chart, sec)
    assert [(e["type"], e["index"], e["time"], e["value"]) for e in out["events"]] == [(2, 2, 1.5, 7)]
    assert out["notes"] == [dict(time=1.0, x=1, y=0, color_type=1, kind=0, rgba=[0.0, 0.0, 1.0, 1.0])]
    assert out["walls"] == [dict(time=2.0, x=2, y=2, w=1, h=3, duration=1.0, rgba=[1.0, 0.0, 1.0, 1.0])]


def test_extract_v3_notes_arcs_chains_walls_skip_bombs():
    chart = v3(
        colorNotes=[{"b": 4, "x": 1, "y": 0, "c": 1, "customData": {"color": [1, 2, 3]}}, {"b": 6, "x": 0, "y": 0, "c": 0}],
        bombNotes=[{"b": 1, "x": 0, "y": 0}],
        sliders=[{"b": 2, "x": 3, "y": 2, "c": 0, "customData": {"color": [0.5, 0.5, 0.5, 0.25]}}],
        burstSliders=[{"b": 3, "x": 2, "y": 1, "c": 1, "customData": {"color": [1, 1, 1]}}],
        obstacles=[{"b": 1, "x": 0, "y": 0, "d": 4, "w": 1, "h": 5, "customData": {"color": [0, 0, 0]}}],
    )
    out = chroma.extract(chart, sec)
    assert [(n["kind"], n["time"], n["color_type"]) for n in out["notes"]] == [(1, 1.0, 0), (2, 1.5, 1), (0, 2.0, 1)]
    assert out["walls"][0]["duration"] == 2.0 and out["walls"][0]["h"] == 5


@pytest.mark.parametrize(
    "bad", [[1, 2], [1, 2, 3, 4, 5], [1, 2, "x"], [1, 2, float("nan")], [1, 2, 17], [1, 2, -0.1], "red", [True, 0, 0]]
)
def test_extract_rejects_bad_colors(bad):
    chart = v3(colorNotes=[{"b": 1, "customData": {"color": bad}}])
    with pytest.raises(ValueError):
        chroma.extract(chart, sec)


def test_extract_rejects_bad_light_id():
    chart = v3(basicBeatmapEvents=[{"b": 1, "et": 1, "customData": {"lightID": [70000]}}])
    with pytest.raises(ValueError):
        chroma.extract(chart, sec)


def test_unsupported_keys_reports_everything_outside_whitelist():
    chart = v3(
        basicBeatmapEvents=[
            {"b": 1, "et": 1, "customData": {"color": [1, 1, 1], "lightID": 1, "lightGradient": {}, "propID": 1}}
        ],
        colorNotes=[{"b": 1, "customData": {"color": [1, 1, 1], "position": [0, 0]}}],
        bombNotes=[{"b": 1, "customData": {"color": [1, 1, 1]}}],
        customData={"environment": [], "customEvents": [], "pointDefinitions": [], "materials": {}, "bookmarks": []},
    )
    assert chroma.unsupported_keys(chart) == sorted([
        "bomb.color", "chart.customEvents", "chart.environment", "chart.materials", "chart.pointDefinitions",
        "event.lightGradient", "event.propID", "note.position"])


def test_unsupported_keys_v2_names():
    chart = v2(
        _events=[
            {"_time": 1, "_type": 1, "_value": 1,
             "_customData": {"_lightGradient": {}, "_color": [1, 1, 1], "_lightID": 2}}
        ],
        _notes=[{"_time": 1, "_type": 0, "_customData": {"_position": [1, 1], "_color": [1, 1, 1]}}],
        _obstacles=[{"_time": 1, "_customData": {"_color": [1, 1, 1], "_scale": [1, 1]}}],
        _customData={"_environment": [], "_customEvents": [], "_bookmarks": [], "_requirements": []},
    )
    assert chroma.unsupported_keys(chart) == sorted([
        "chart._customEvents", "chart._environment", "event._lightGradient", "note._position", "wall._scale"])


def test_unsupported_keys_clean_chart_and_plain_chart():
    assert chroma.unsupported_keys(v3(colorNotes=[{"b": 1}])) == []
    ok = v3(obstacles=[{"b": 1, "customData": {"color": [1, 1, 1]}}], customData={"lighter": "x", "time": 3})
    assert chroma.unsupported_keys(ok) == []


def chart_item(**extra):
    item = dict(mode=0, difficulty=3, events=[], notes=[], walls=[])
    item.update(extra)
    return item


def test_encode_reference_exact_bytes():
    data = reference_sidecar()
    body = data[20:]
    assert data[:8] == b"BSDACHR1"
    length, digest = struct.unpack_from("<IQ", data, 8)
    assert length == len(body) == len(data) - 20
    value = 14695981039346656037
    for byte in body:
        value = ((value ^ byte) * 1099511628211) & (2**64 - 1)
    assert digest == value
    expected = struct.pack("<I", 37) + LEVEL_ID.encode() + struct.pack("<I", 1) + bytes([0, 3])
    expected += struct.pack("<I", 3)
    for kind, index, time, val, rgba in [
        (1, 1, 1.0, 1, (1, 0, 0, 1)), (1, 2, 1.0, 5, (0, 1, 0, 0.5)), (4, 1, 2.5, 3, (2, 2, 2, 1))
    ]:
        expected += struct.pack("<BIfi4fH", kind, index, time, val, *rgba, 0)
    expected += struct.pack("<I", 2)
    expected += struct.pack("<f4B4f", 1.0, 1, 0, 0, 0, 0, 0, 1, 1)
    expected += struct.pack("<f4B4f", 3.0, 2, 1, 1, 1, 1, 1, 0, 1)
    expected += struct.pack("<I", 1) + struct.pack("<f4Bf4f", 4.0, 0, 0, 1, 5, 2.0, 1, 0, 1, 1)
    assert body == expected
    assert reference_sidecar() == data


def test_encode_lights_roundtrip_and_limits():
    ev = dict(type=1, index=1, time=0.0, value=0, rgba=[0, 0, 0, 1], lights=[5, 6])
    data = chroma.encode(LEVEL_ID, [chart_item(events=[ev])])
    assert struct.pack("<3H", 2, 5, 6) in data
    with pytest.raises(ValueError):
        chroma.encode(LEVEL_ID, [chart_item(events=[dict(ev, lights=[0] * 1025)])])


@pytest.mark.parametrize("level", ["", "bsd_additive_XYZ", "bsd_additive_0123456789abcdef0123456", None, "x" * 37])
def test_encode_rejects_bad_level_id(level):
    with pytest.raises(ValueError):
        chroma.encode(level, [chart_item()])


def test_encode_chart_count_and_duplicates():
    with pytest.raises(ValueError):
        chroma.encode(LEVEL_ID, [])
    with pytest.raises(ValueError):
        chroma.encode(LEVEL_ID, [chart_item(), chart_item()])
    many = [chart_item(mode=m, difficulty=d) for m in range(5) for d in range(5)]
    assert len(chroma.encode(LEVEL_ID, many)) > 20
    with pytest.raises(ValueError):
        chroma.encode(LEVEL_ID, many + [chart_item(mode=5)])
    for bad in (dict(mode=5), dict(difficulty=5), dict(mode=-1), dict(difficulty=1.5)):
        with pytest.raises(ValueError):
            chroma.encode(LEVEL_ID, [chart_item(**bad)])


def test_encode_record_limits():
    note = dict(time=1.0, x=0, y=0, color_type=0, kind=0, rgba=[1, 1, 1, 1])
    with pytest.raises(ValueError):
        chroma.encode(LEVEL_ID, [chart_item(notes=[note] * 200001)])
    with pytest.raises(ValueError):  # 8 MiB file cap
        chroma.encode(LEVEL_ID, [chart_item(mode=m, difficulty=d, notes=[note] * 200000) for m in range(2) for d in range(5)])
    bad_notes = [
        dict(note, time=-1.0), dict(note, time=3601.0), dict(note, kind=3), dict(note, color_type=2),
        dict(note, x=256), dict(note, rgba=[1, 1, 1, 17]), dict(note, rgba=[1, 1, 1]), dict(note, time=float("nan")),
    ]
    for bad in bad_notes:
        with pytest.raises(ValueError):
            chroma.encode(LEVEL_ID, [chart_item(notes=[bad])])
    with pytest.raises(ValueError):
        chroma.encode(LEVEL_ID, [chart_item(notes=[dict(note, time=2.0), note])])


def test_encode_event_rules():
    ev = dict(type=1, index=1, time=1.0, value=1, rgba=[1, 1, 1, 1], lights=[])
    chroma.encode(LEVEL_ID, [chart_item(events=[ev, dict(ev, index=2, time=1.0)])])
    for bad in (dict(ev, index=0), dict(ev, type=256), dict(ev, value=2**31), dict(ev, time=-0.5)):
        with pytest.raises(ValueError):
            chroma.encode(LEVEL_ID, [chart_item(events=[bad])])
    with pytest.raises(ValueError):  # index not increasing
        chroma.encode(LEVEL_ID, [chart_item(events=[dict(ev, index=2), ev])])
    with pytest.raises(ValueError):  # time going back inside one type
        chroma.encode(LEVEL_ID, [chart_item(events=[dict(ev, time=2.0), dict(ev, index=2, time=1.0)])])


def test_encode_wall_rules():
    wall = dict(time=1.0, x=0, y=0, w=1, h=5, duration=-1.0, rgba=[1, 1, 1, 1])
    chroma.encode(LEVEL_ID, [chart_item(walls=[wall])])
    with pytest.raises(ValueError):
        chroma.encode(LEVEL_ID, [chart_item(walls=[dict(wall, duration=3601.0)])])
    with pytest.raises(ValueError):
        chroma.encode(LEVEL_ID, [chart_item(walls=[dict(wall, w=-1)])])
    # Decorative walls outside the lanes (seen in BeatSaver 55a43): x/y are signed bytes.
    data = chroma.encode(LEVEL_ID, [chart_item(walls=[dict(wall, x=-1), dict(wall, time=2.0, x=4, y=-2)])])
    assert bytes([0xFF, 0x00, 0x01, 0x05]) in data and bytes([0x04, 0xFE, 0x01, 0x05]) in data
    for bad in (dict(wall, x=-129), dict(wall, x=128), dict(wall, y=1.0)):
        with pytest.raises(ValueError):
            chroma.encode(LEVEL_ID, [chart_item(walls=[bad])])


def test_strip_removes_object_custom_data_only():
    chart = v3(
        colorNotes=[{"b": 1, "customData": {"color": [1, 1, 1]}}],
        basicBeatmapEvents=[{"b": 1, "et": 1, "customData": {"lightID": 1}}],
        customData={"lighter": {}},
    )
    out = chroma.strip(chart)
    assert out["colorNotes"] == [{"b": 1}] and out["basicBeatmapEvents"] == [{"b": 1, "et": 1}]
    assert out["customData"] == {"lighter": {}} and "customData" in chart["colorNotes"][0]
    legacy = chroma.strip(v2(_notes=[{"_time": 1, "_customData": {"_color": [1, 1, 1]}}]))
    assert legacy["_notes"] == [{"_time": 1}]
