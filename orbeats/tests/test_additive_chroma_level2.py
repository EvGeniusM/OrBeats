"""Chroma level 2 (rings, lasers): whitelist by plugin level, extraction and the BSDACHR2 layout."""
import struct

import pytest

import additive_chroma as chroma
from chroma_reference import LEVEL_ID, reference_sidecar_v2


def sec(beat):
    return beat * 0.5  # 120 BPM


def v3(*events):
    return {"version": "3.3.0", "basicBeatmapEvents": list(events)}


def v2(*events):
    return {"_version": "2.6.0", "_events": list(events)}


def chart_item(events):
    return dict(mode=0, difficulty=3, events=events, notes=[], walls=[])


def test_level_two_keys_need_the_level_two_plugin():
    legacy = v2({"_time": 1, "_type": 8, "_value": 0, "_customData": {"_direction": 1, "_step": 5, "_counterSpin": True}})
    assert chroma.unsupported_keys(legacy) == ["event._counterSpin", "event._direction", "event._step"]
    assert chroma.unsupported_keys(legacy, 2) == []
    modern = v3({"b": 1, "et": 12, "i": 3, "customData": {"lockRotation": True, "speed": 2, "direction": 0}})
    assert chroma.unsupported_keys(modern) == ["event.direction", "event.lockRotation", "event.speed"]
    assert chroma.unsupported_keys(modern, 2) == []


def test_level_two_still_refuses_gradients_environment_and_v2_only_keys_in_v3():
    legacy = v2({"_time": 1, "_type": 1, "_value": 1, "_customData": {"_lightGradient": {}, "_easing": "easeLinear", "_propID": 1}})
    assert chroma.unsupported_keys(legacy, 2) == ["event._easing", "event._lightGradient", "event._propID"]
    modern = v3({"b": 1, "et": 8, "i": 0, "customData": {"counterSpin": True, "reset": True, "easing": "easeInQuad", "lerpType": "HSV"}})
    assert chroma.unsupported_keys(modern, 2) == ["event.counterSpin", "event.easing", "event.lerpType", "event.reset"]
    top = dict(modern, customData={"environment": []})
    assert "chart.environment" in chroma.unsupported_keys(top, 2)


def test_extract_v2_ring_and_laser_parameters():
    chart = v2(
        {"_time": 2, "_type": 8, "_value": 0, "_customData": {
            "_direction": 1, "_rotation": 45, "_step": 12.5, "_prop": 2, "_speed": 3, "_stepMult": 2, "_propMult": 0.5,
            "_speedMult": 4, "_counterSpin": True, "_reset": False, "_nameFilter": "BigTrackLaneRings"}},
        {"_time": 4, "_type": 12, "_value": 3, "_customData": {"_lockPosition": True, "_preciseSpeed": 7.25, "_direction": 0,
                                                              "_color": [1, 0, 0]}},
        {"_time": 4, "_type": 9, "_value": 0, "_customData": {"_step": 0.5, "_speed": -1}},
        {"_time": 5, "_type": 13, "_value": 1, "_customData": {"_speed": 1, "_preciseSpeed": 9}},   # _speed wins
        {"_time": 6, "_type": 8, "_value": 0, "_customData": {"_reset": True}},
        {"_time": 7, "_type": 8, "_value": 0},                                                   # nothing: not written
    )
    events = {(e["type"], e["index"]): e for e in chroma.extract(chart, sec, 2)["events"]}
    assert sorted(events) == [(8, 1), (8, 2), (9, 1), (12, 1), (13, 1)]
    ring = events[(8, 1)]
    assert ring["flags"] == (chroma.FLAG_DIRECTION | chroma.FLAG_ROTATION | chroma.FLAG_STEP | chroma.FLAG_PROP | chroma.FLAG_SPEED
                             | chroma.FLAG_COUNTER_SPIN | chroma.FLAG_NAME)
    assert (ring["direction"], ring["rotation"], ring["step"], ring["prop"], ring["speed"]) == (1, 45.0, 12.5, 2.0, 3.0)
    assert (ring["step_mult"], ring["prop_mult"], ring["speed_mult"]) == (2.0, 0.5, 4.0)
    assert ring["name"] == "bigtracklanerings" and ring["rgba"] == [0.0, 0.0, 0.0, 0.0] and ring["time"] == 1.0
    laser = events[(12, 1)]
    assert laser["flags"] == chroma.FLAG_COLOR | chroma.FLAG_LOCK | chroma.FLAG_SPEED | chroma.FLAG_DIRECTION
    assert laser["speed"] == 7.25 and laser["direction"] == 0 and laser["rgba"] == [1.0, 0.0, 0.0, 1.0]
    assert events[(9, 1)]["step"] == 0.5 and events[(9, 1)]["speed"] == -1.0
    assert events[(13, 1)]["speed"] == 1.0
    assert events[(8, 2)]["flags"] == chroma.FLAG_RESET


def test_extract_v3_lock_rotation_and_name_filter():
    chart = v3({"b": 2, "et": 13, "i": 1, "customData": {"lockRotation": True, "speed": 4}},
               {"b": 2, "et": 8, "i": 0, "customData": {"rotation": -30, "nameFilter": "SmallTrackLaneRings", "prop": 1}},
               {"b": 3, "et": 12, "i": 1, "customData": {"lockRotation": False}})   # false is the default: not written
    events = chroma.extract(chart, sec, 2)["events"]
    assert [(e["type"], e["flags"]) for e in events] == [
        (8, chroma.FLAG_ROTATION | chroma.FLAG_PROP | chroma.FLAG_NAME), (13, chroma.FLAG_LOCK | chroma.FLAG_SPEED)]
    assert events[0]["name"] == "smalltracklanerings" and events[0]["rotation"] == -30.0


def test_level_one_extraction_ignores_parameters_entirely():
    chart = v2({"_time": 2, "_type": 8, "_value": 0, "_customData": {"_step": 2, "_color": [0, 1, 0]}})
    events = chroma.extract(chart, sec)["events"]
    assert len(events) == 1 and "flags" not in events[0] and "step" not in events[0]


@pytest.mark.parametrize("custom", [
    {"_direction": 2}, {"_direction": True}, {"_direction": 1.0}, {"_speed": "3"}, {"_speed": 100001}, {"_step": float("nan")},
    {"_rotation": 3601}, {"_prop": -100001}, {"_stepMult": -1}, {"_speedMult": 100001}, {"_lockPosition": "yes"}, {"_reset": 1},
    {"_counterSpin": 0}, {"_nameFilter": ""}, {"_nameFilter": "x" * 33}, {"_nameFilter": "ringé"}, {"_nameFilter": 5},
])
def test_invalid_parameter_values_refuse(custom):
    with pytest.raises(ValueError):
        chroma.extract(v2({"_time": 1, "_type": 8, "_value": 0, "_customData": custom}), sec, 2)


def test_encode_picks_v1_for_colours_and_v2_for_parameters():
    colour = dict(type=1, index=1, time=1.0, value=1, rgba=[1.0, 0.0, 0.0, 1.0], lights=[])
    assert chroma.encode(LEVEL_ID, [chart_item([colour])])[:8] == b"BSDACHR1"
    level2_colour_only = dict(colour, flags=chroma.FLAG_COLOR, direction=0, speed=0.0, step=0.0, prop=0.0, rotation=0.0,
                              step_mult=1.0, prop_mult=1.0, speed_mult=1.0, name="")
    assert chroma.encode(LEVEL_ID, [chart_item([level2_colour_only])]) == chroma.encode(LEVEL_ID, [chart_item([colour])])
    ring = dict(level2_colour_only, type=8, flags=chroma.FLAG_STEP | chroma.FLAG_NAME, step=2.0, name="rings", rgba=[0.0] * 4)
    data = chroma.encode(LEVEL_ID, [chart_item([ring])])
    assert data[:8] == b"BSDACHR2"
    record = data[20 + 4 + len(LEVEL_ID) + 4 + 2 + 4:]
    expected = struct.pack("<BIfiH", 8, 1, 1.0, 1, chroma.FLAG_STEP | chroma.FLAG_NAME) + struct.pack("<4f", 0, 0, 0, 0)
    expected += struct.pack("<B4f3f", 0, 0.0, 2.0, 0.0, 0.0, 1.0, 1.0, 1.0) + b"\x05rings" + struct.pack("<H", 0)
    assert record[:len(expected)] == expected
    multiplier_only = dict(level2_colour_only, step_mult=2.0)
    assert chroma.encode(LEVEL_ID, [chart_item([multiplier_only])])[:8] == b"BSDACHR2"


@pytest.mark.parametrize("bad", [
    dict(flags=chroma.FLAG_STEP, step=1.0, speed=1.0),           # value without its flag
    dict(flags=chroma.FLAG_DIRECTION, direction=2),
    dict(flags=0, direction=1),
    dict(flags=chroma.FLAG_NAME, name="Rings"),                    # must already be lower case
    dict(flags=chroma.FLAG_NAME, name=""),
    dict(flags=0, name="rings"),
    dict(flags=2048),
    dict(flags=chroma.FLAG_ROTATION, rotation=4000.0),
    dict(flags=chroma.FLAG_STEP, step=1.0, prop_mult=-1.0),
])
def test_encode_refuses_inconsistent_v2_records(bad):
    event = dict(type=8, index=1, time=1.0, value=0, rgba=[0.0] * 4, lights=[], direction=0, speed=0.0, step=0.0,
                 prop=0.0, rotation=0.0, step_mult=1.0, prop_mult=1.0, speed_mult=1.0, name="")
    event.update(bad)
    with pytest.raises(ValueError):
        chroma.encode(LEVEL_ID, [chart_item([event])])


def test_v2_reference_is_deterministic_and_checksummed():
    data = reference_sidecar_v2()
    assert data[:8] == b"BSDACHR2" and data == reference_sidecar_v2()
    length, digest = struct.unpack("<IQ", data[8:20])
    assert length == len(data) - 20 and digest == chroma.checksum(data[20:])
