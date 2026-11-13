"""Chroma level 6 (stage 3b): per-light lightID -> light indices through Chroma's table of the scene."""
import pytest

import additive_chroma as chroma


def sec(beat):
    return beat * 0.5


def ev2(beat, kind, value, **custom):
    out = {"_time": beat, "_type": kind, "_value": value}
    if custom:
        out["_customData"] = {"_" + k: v for k, v in custom.items()}
    return out


def ev3(beat, kind, value, **custom):
    out = {"b": beat, "et": kind, "i": value}
    if custom:
        out["customData"] = custom
    return out


def v2(*events):
    return {"_version": "2.2.0", "_events": list(events)}


def v3(*events):
    return {"version": "3.3.0", "basicBeatmapEvents": list(events)}


def test_tables_hold_the_chroma_data_for_stock_scenes():
    default = chroma.light_table("DefaultEnvironment")
    assert default[1][1] == 9 and default[1][10] == 15 and default[2][1] == 6
    assert chroma.light_table("NoSuchEnvironment") == {} and chroma.light_table(None) == {}


def test_lights_are_written_only_at_level_six_with_a_scene():
    chart = v2(ev2(1, 0, 1, lightID=[1, 2], color=[1, 0, 0]))
    for level, scene in ((5, "DefaultEnvironment"), (6, None)):
        assert chroma.extract(chart, sec, level, scene)["events"][0]["lights"] == []
    record = chroma.extract(chart, sec, 6, "DefaultEnvironment")["events"][0]
    assert record["lights"] == [9, 10] and record["rgba"] == [1.0, 0.0, 0.0, 1.0]


def test_ids_without_a_table_entry_are_the_index_and_repeats_go():
    chart = v3(ev3(1, 1, 1, lightID=[3, 999, 3]))
    assert chroma.extract(chart, sec, 6, "DefaultEnvironment")["events"][0]["lights"] == [8, 999]
    assert chroma.extract(chart, sec, 6, "UnknownEnvironment")["events"][0]["lights"] == [3, 999]
    assert chroma.extract(v3(ev3(1, 1, 1, lightID=7)), sec, 6, "UnknownEnvironment")["events"][0]["lights"] == [7]


def test_every_event_of_a_light_type_gets_a_record_in_file_order():
    chart = v3(ev3(1, 1, 1), ev3(1, 1, 1, lightID=[1]), ev3(2, 1, 0), ev3(1, 2, 1))
    events = chroma.extract(chart, sec, 6, "UnknownEnvironment")["events"]
    assert [(e["type"], e["index"], e["time"], e["lights"], e["flags"]) for e in events] == [
        (1, 1, 0.5, [], 0), (1, 2, 0.5, [1], 0), (1, 3, 1.0, [], 0)]   # type 2 has no lightID: no plain records
    data = chroma.encode("bsd_additive_0123456789abcdef01234567", [dict(mode=0, difficulty=4, notes=[], walls=[], events=events)])
    assert data[:8] == b"BSDACHR2"   # colourless records need the flags field


def test_other_light_types_keep_colouring_the_whole_group():
    chart = v2(ev2(1, 6, 1, lightID=[1], color=[0, 1, 0]), ev2(1, 10, 1, lightID=2, color=[0, 0, 1]))
    events = chroma.extract(chart, sec, 6, "BillieEnvironment")["events"]
    assert [e["lights"] for e in events] == [[], []]


def test_disagreeing_transition_keeps_the_whole_group():
    # Light 1 fades into the transition at beat 3 (Chroma), but the game's next event is beat 2 (light 2).
    red = [1, 0, 0]
    chart = v2(ev2(1, 0, 1, lightID=1, color=red), ev2(2, 0, 1, lightID=2, color=red), ev2(3, 0, 4, lightID=1, color=red))
    assert [e["lights"] for e in chroma.extract(chart, sec, 6, "UnknownEnvironment")["events"]] == [[], [], []]
    agree = v2(ev2(1, 0, 1, lightID=1), ev2(2, 0, 4, lightID=1), ev2(3, 0, 1, lightID=2))
    assert [e["lights"] for e in chroma.extract(agree, sec, 6, "UnknownEnvironment")["events"]] == [[1], [1], [2]]


@pytest.mark.parametrize("rows,expected", [
    ([(1, None), (4, None)], True),
    ([(1, [1]), (4, [1])], True),
    ([(1, [1]), (1, [2]), (4, [1])], False),
    ([(1, None), (4, [1])], False),   # the global event refreshes light 1 too: Chroma fades, and unnamed lights do not
    ([(1, [1]), (0, None), (4, [1])], False),  # after the global off, lights no event names do not fade (Chroma)
    ([(1, [1]), (0, [1]), (4, [1])], True),
    ([(1, [1]), (2, [2])], True),
])
def test_fades_agree(rows, expected):
    assert chroma.fades_agree(rows) is expected


def test_bad_light_ids_are_refused():
    for bad in ([1.5], ["1"], [-1], [70000], True):
        with pytest.raises(ValueError):
            chroma.extract(v2(ev2(1, 0, 1, lightID=bad)), sec, 6, "DefaultEnvironment")
