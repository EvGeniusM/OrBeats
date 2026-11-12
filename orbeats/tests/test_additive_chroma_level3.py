"""Chroma level 3 (V2 light gradients): whitelist by plugin level, extraction and the BSDACHR2 gradient tail."""
import struct

import pytest

import additive_chroma as chroma
from chroma_reference import LEVEL_ID


def sec(beat):
    return beat * 0.5  # 120 BPM


def v2(*events):
    return {"_version": "2.6.0", "_events": list(events)}


def gradient_event(**gradient):
    body = {"_duration": 1, "_startColor": [1, 0, 0], "_endColor": [0, 0, 1, 0.5]}
    body.update(gradient)
    return {"_time": 4, "_type": 1, "_value": 5, "_customData": {"_lightGradient": body}}


def test_gradients_need_the_level_three_plugin():
    chart = v2(gradient_event())
    assert chroma.unsupported_keys(chart) == ["event._lightGradient"]
    assert chroma.unsupported_keys(chart, 2) == ["event._lightGradient"]
    assert chroma.unsupported_keys(chart, 3) == []
    # Still refused at level 3: top-level easing/lerpType, propID, V3 transition easing.
    other = v2({"_time": 1, "_type": 1, "_value": 1, "_customData": {"_easing": "easeLinear", "_lerpType": "HSV", "_propID": 1}})
    assert chroma.unsupported_keys(other, 3) == ["event._easing", "event._lerpType", "event._propID"]
    modern = {"version": "3.3.0", "basicBeatmapEvents": [{"b": 1, "et": 1, "i": 4, "customData": {"easing": "easeInQuad"}}]}
    assert chroma.unsupported_keys(modern, 3) == ["event.easing"]


def test_extract_gradient_in_seconds_with_game_easing():
    chart = v2(gradient_event(), gradient_event(_easing="easeOutQuad", _duration=3), dict(gradient_event(), _time=8, _customData={
        "_color": [0, 1, 0], "_lightGradient": {"_duration": 0.5, "_startColor": [1, 1, 1], "_endColor": [0, 0, 0], "_easing": "easeInOutBounce"}}))
    events = chroma.extract(chart, sec, 3)["events"]
    assert [e["index"] for e in events] == [1, 2, 3]
    first, second, third = events
    assert first["flags"] == chroma.FLAG_GRADIENT and first["rgba"] == [0.0] * 4 and first["time"] == 2.0
    assert first["gradient"] == dict(start=[1.0, 0.0, 0.0, 1.0], end=[0.0, 0.0, 1.0, 0.5], duration=0.5, easing=1)
    assert second["gradient"]["easing"] == 6 and second["gradient"]["duration"] == 1.5
    assert third["flags"] == chroma.FLAG_GRADIENT | chroma.FLAG_COLOR and third["gradient"]["easing"] == 31
    # Level 2 never reads the gradient (its plugin would refuse the whole sidecar).
    assert "gradient" not in chroma.extract(v2(dict(gradient_event(), _customData={"_lightGradient": {}, "_color": [1, 0, 0]})), sec, 2)["events"][0]


def test_game_easing_table_matches_the_2_04_enum():
    assert chroma.EASINGS["easeLinear"] == 1 and chroma.EASINGS["easeInSine"] == 2 and chroma.EASINGS["easeInOutSine"] == 4
    assert chroma.EASINGS["easeInQuad"] == 5 and chroma.EASINGS["easeInExpo"] == 17 and chroma.EASINGS["easeOutBack"] == 24
    assert chroma.EASINGS["easeInOutBounce"] == 31 and "easeStep" not in chroma.EASINGS and len(chroma.EASINGS) == 31


@pytest.mark.parametrize("change", [
    dict(_easing="easeStep"), dict(_easing="linear"), dict(_duration=0), dict(_duration=-1), dict(_duration=121),
    dict(_duration="1"), dict(_startColor=[1, 0]), dict(_endColor=[0, 0, 17]), dict(_extra=1),
])
def test_invalid_gradients_refuse(change):
    with pytest.raises(ValueError):
        chroma.extract(v2(gradient_event(**change)), sec, 3)


@pytest.mark.parametrize("body", [[], "x", {"_duration": 1, "_startColor": [1, 0, 0]}])
def test_malformed_gradient_objects_refuse(body):
    with pytest.raises(ValueError):
        chroma.extract(v2({"_time": 1, "_type": 1, "_value": 1, "_customData": {"_lightGradient": body}}), sec, 3)


def test_encode_writes_the_gradient_tail_after_the_name():
    event = chroma.extract(v2(gradient_event(_easing="easeInQuad")), sec, 3)["events"][0]
    data = chroma.encode(LEVEL_ID, [dict(mode=0, difficulty=3, events=[event], notes=[], walls=[])])
    assert data[:8] == b"BSDACHR2"
    record = data[20 + 4 + len(LEVEL_ID) + 4 + 2 + 4:]
    expected = struct.pack("<BIfiH", 1, 1, 2.0, 5, chroma.FLAG_GRADIENT) + struct.pack("<4f", 0, 0, 0, 0)
    expected += struct.pack("<B4f3f", 0, 0, 0, 0, 0, 1, 1, 1) + b"\x00"
    expected += struct.pack("<4f4ffB", 1, 0, 0, 1, 0, 0, 1, 0.5, 0.5, 5) + struct.pack("<H", 0)
    assert record[:len(expected)] == expected


@pytest.mark.parametrize("bad", [
    dict(gradient=None),                                                                         # flag without data
    dict(flags=0),                                                                               # data without flag
    dict(gradient=dict(start=[1.0, 0, 0, 1], end=[0, 0, 1.0, 1], duration=0.0, easing=1)),
    dict(gradient=dict(start=[1.0, 0, 0, 1], end=[0, 0, 1.0, 1], duration=61.0, easing=1)),
    dict(gradient=dict(start=[1.0, 0, 0, 1], end=[0, 0, 1.0, 1], duration=1.0, easing=32)),
    dict(gradient=dict(start=[1.0, 0, 0, 1], end=[0, 0, 1.0, 1], duration=1.0, easing=0)),
    dict(gradient=dict(start=[1.0, 0, 0], end=[0, 0, 1.0, 1], duration=1.0, easing=1)),
])
def test_encode_refuses_inconsistent_gradients(bad):
    event = chroma.extract(v2(gradient_event()), sec, 3)["events"][0]
    event.update(bad)
    with pytest.raises(ValueError):
        chroma.encode(LEVEL_ID, [dict(mode=0, difficulty=3, events=[event], notes=[], walls=[])])
