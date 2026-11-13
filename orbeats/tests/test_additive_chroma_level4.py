"""Chroma level 4 (stage 5a): environment entries -> BSDACHR3 block, whitelist by plugin level."""
import struct

import pytest

import additive_chroma as chroma
from chroma_reference import LEVEL_ID, reference_sidecar_v3


def sec(beat):
    return beat * 0.5


def v2(*entries, **extra):
    return {"_version": "2.6.0", "_events": [], "_customData": {"_environment": list(entries), **extra}}


def v3(*entries):
    return {"version": "3.3.0", "basicBeatmapEvents": [], "customData": {"environment": list(entries)}}


def test_environment_needs_the_level_four_plugin():
    chart = v3({"id": "Smoke", "lookupMethod": "Contains", "active": False})
    assert chroma.unsupported_keys(chart, 3) == ["chart.environment"]
    assert chroma.unsupported_keys(chart, 4) == []
    assert "environment" not in chroma.extract(chart, sec, 3)


@pytest.mark.parametrize("entry,key", [
    ({"id": "a", "lookupMethod": "Contains", "duplicate": 1}, "environment.duplicate"),
    ({"geometry": {"type": "Cube"}}, "environment.geometry"),
    ({"id": "a", "lookupMethod": "Contains", "track": "t"}, "environment.track"),
    ({"id": "a", "lookupMethod": "Contains", "components": {"ILightWithId": {"lightID": 3}}}, "environment.components.ILightWithId"),
    ({"id": "a", "lookupMethod": "Contains", "components": {"TubeBloomPrePassLight": {"colorAlphaMultiplier": 2}}},
     "environment.components.TubeBloomPrePassLight"),
    ({"id": "a", "lookupMethod": "Contains", "components": []}, "environment.components"),
])
def test_level_four_still_refuses_copies_geometry_animation_and_components(entry, key):
    assert chroma.unsupported_keys(v3(entry), 4) == [key]
    with pytest.raises(ValueError):
        chroma.extract(v3(entry), sec, 4)


def test_v2_refusals_include_light_ids_and_duplicates():
    chart = v2({"_id": "a", "_lookupMethod": "Regex", "_duplicate": 1, "_lightID": 100})
    assert chroma.unsupported_keys(chart, 4) == ["environment._duplicate", "environment._lightID"]
    assert chroma.unsupported_keys(v2({"_id": "a", "_lookupMethod": "Regex"}, _environmentRemoval=["x"]), 4) == ["chart._environmentRemoval"]


def test_extract_v3_in_file_order_and_drops_no_op_light_components():
    chart = v3({"id": "Spectrograms", "lookupMethod": "Contains", "active": False, "components": {"ILightWithId": {"lightID": None}}},
               {"id": "Logo", "lookupMethod": "Contains", "components": {"ILightWithId": {"type": None, "lightID": None}}},
               {"id": r"\[\d+\]Ring$", "lookupMethod": "Regex", "scale": [1, 2, 3], "localRotation": [0, 90, 0],
                "position": [1, 2, 3], "localPosition": [4, 5, 6], "rotation": [7, 8, 9]})
    assert chroma.unsupported_keys(chart, 4) == []
    entries = chroma.extract(chart, sec, 4)["environment"]
    assert entries == [
        dict(method=2, id="Spectrograms", active=False),
        dict(method=0, id=r"\[\d+\]Ring$", scale=[1.0, 2.0, 3.0], position=[1.0, 2.0, 3.0], localPosition=[4.0, 5.0, 6.0],
             rotation=[7.0, 8.0, 9.0], localRotation=[0.0, 90.0, 0.0]),
    ]


def test_v2_positions_are_lane_widths_rotations_and_scale_are_not():
    entries = chroma.environment(v2({"_id": "Tube", "_lookupMethod": "EndsWith", "_position": [-5, 7, 15], "_localPosition": [1, 0, 0],
                                     "_scale": [1.5, 200, 1.5], "_rotation": [0, 0, 90], "_active": True}))
    (e,) = entries
    assert e["method"] == 4 and e["active"] is True and e["scale"] == [1.5, 200.0, 1.5] and e["rotation"] == [0.0, 0.0, 90.0]
    assert e["position"] == pytest.approx([-3.0, 4.2, 9.0]) and e["localPosition"] == pytest.approx([0.6, 0.0, 0.0])


@pytest.mark.parametrize("entry", [
    {"id": "(a*)*", "lookupMethod": "Regex", "active": False},          # outside the plugin's regex subset
    {"id": r"(?=x)", "lookupMethod": "Regex", "active": False},
    {"id": "a", "lookupMethod": "Fuzzy", "active": False},
    {"id": "", "lookupMethod": "Exact", "active": False},
    {"id": "é", "lookupMethod": "Exact", "active": False},
    {"id": "a" * 513, "lookupMethod": "Exact", "active": False},
    {"id": "a", "lookupMethod": "Exact", "active": 0},
    {"id": "a", "lookupMethod": "Exact", "scale": [1, 2]},
    {"id": "a", "lookupMethod": "Exact", "position": [1, 2, float("nan")]},
    {"id": "a", "lookupMethod": "Exact", "position": [1, 2, 200000]},
    {"lookupMethod": "Exact", "active": False},
    "not an object",
])
def test_invalid_entries_refuse(entry):
    with pytest.raises(ValueError):
        chroma.extract(v3(entry), sec, 4)


def test_list_shape_and_size_bounds():
    with pytest.raises(ValueError):
        chroma.environment({"version": "3.3.0", "customData": {"environment": {"id": "a"}}})
    many = [{"id": "a", "lookupMethod": "Exact", "active": False}] * (chroma.MAX_ENV_ENTRIES + 1)
    with pytest.raises(ValueError):
        chroma.environment(v3(*many))
    assert chroma.environment(v3({"id": "a", "lookupMethod": "Exact"})) == []   # nothing to change


def _payload(data):
    return data[20 + 4 + len(LEVEL_ID) + 4:]


def test_encode_writes_bsdachr3_with_the_block_after_the_walls():
    entries = chroma.environment(v3({"id": "Smoke", "lookupMethod": "Contains", "active": False},
                                    {"id": "Ring", "lookupMethod": "EndsWith", "scale": [2, 2, 2], "localRotation": [0, 0, 45]}))
    data = chroma.encode(LEVEL_ID, [dict(mode=0, difficulty=3, events=[], notes=[], walls=[],
                                         environment=dict(name="KDAEnvironment", entries=entries))])
    assert data[:8] == b"BSDACHR3"
    expected = bytes([0, 3]) + struct.pack("<III", 0, 0, 0) + struct.pack("<B", 14) + b"KDAEnvironment" + struct.pack("<I", 2)
    expected += struct.pack("<BH", 2, 5) + b"Smoke" + struct.pack("<B", chroma.ENV_HAS_ACTIVE)
    expected += struct.pack("<BH", 4, 4) + b"Ring" + struct.pack("<B", 4 | 64) + struct.pack("<3f3f", 2, 2, 2, 0, 0, 45)
    assert _payload(data) == expected


def test_v3_file_carries_an_empty_block_for_charts_without_environment():
    data = reference_sidecar_v3()
    assert data[:8] == b"BSDACHR3" and b"FitBeatEnvironment" in data
    easy_end = 2 + 4 + (1 + 4 + 4 + 4 + 2 + 16 + 1 + 16 + 12 + 1 + 2) + 4 + 4
    assert _payload(data)[easy_end:easy_end + 5] == struct.pack("<BI", 0, 0)


@pytest.mark.parametrize("block", [
    dict(name="", entries=[dict(method=2, id="a", active=False)]),
    dict(name="KDA\x01", entries=[dict(method=2, id="a", active=False)]),
    dict(name="KDAEnvironment", entries=[]),
    dict(name="KDAEnvironment", entries=[dict(method=5, id="a", active=False)]),
    dict(name="KDAEnvironment", entries=[dict(method=0, id="(a*)*", active=False)]),
    dict(name="KDAEnvironment", entries=[dict(method=2, id="a")]),
    dict(name="KDAEnvironment", entries=[dict(method=2, id="a", active=False, duplicate=0)]),   # copies: v4 only, 1..100
    dict(name="KDAEnvironment", entries=[dict(method=2, id="a", scale=[1, 2])]),
])
def test_encode_refuses_invalid_blocks(block):
    with pytest.raises(ValueError):
        chroma.encode(LEVEL_ID, [dict(mode=0, difficulty=3, events=[], notes=[], walls=[], environment=block)])


def test_environment_without_v3_is_never_written():
    with pytest.raises(ValueError):
        chroma._chart(dict(events=[], notes=[], walls=[], environment=dict(name="E", entries=[dict(method=2, id="a", active=False)])), 2)
