"""Chroma level 5 (stage 5b): environment copies ("duplicate") -> BSDACHR4, whitelist by plugin level."""
import struct

import pytest

import additive_chroma as chroma
from chroma_reference import LEVEL_ID, reference_sidecar_v4


def sec(beat):
    return beat * 0.5


def v2(*entries):
    return {"_version": "2.2.0", "_events": [], "_customData": {"_environment": list(entries)}}


def v3(*entries):
    return {"version": "3.3.0", "basicBeatmapEvents": [], "customData": {"environment": list(entries)}}


def chart(entries, name="BigMirrorEnvironment"):
    return dict(mode=0, difficulty=4, notes=[], walls=[], events=[], environment=dict(name=name, entries=entries))


def test_duplicate_needs_the_level_five_plugin():
    legacy = v2({"_id": "NeonTube$", "_lookupMethod": "Regex", "_duplicate": 10})
    modern = v3({"id": "NeonTube", "lookupMethod": "Contains", "duplicate": 1, "scale": [1, 2, 1]})
    assert chroma.unsupported_keys(legacy, 4) == ["environment._duplicate"]
    assert chroma.unsupported_keys(modern, 4) == ["environment.duplicate"]
    assert chroma.unsupported_keys(legacy, 5) == [] and chroma.unsupported_keys(modern, 5) == []
    with pytest.raises(ValueError):
        chroma.extract(legacy, sec, 4)
    assert chroma.extract(legacy, sec, 5)["environment"] == [dict(method=0, id="NeonTube$", duplicate=10)]
    assert chroma.extract(modern, sec, 5)["environment"] == [dict(method=2, id="NeonTube", duplicate=1, scale=[1.0, 2.0, 1.0])]


@pytest.mark.parametrize("entry,key", [
    ({"_id": "a", "_lookupMethod": "Regex", "_duplicate": 1, "_lightID": 100}, "environment._lightID"),
    ({"_id": "a", "_lookupMethod": "Regex", "_duplicate": 1, "_track": "t"}, "environment._track"),
    ({"_geometry": {"_type": "Cube"}, "_duplicate": 1}, "environment._geometry"),
])
def test_level_five_still_refuses_light_ids_tracks_and_geometry(entry, key):
    assert chroma.unsupported_keys(v2(entry), 5) == [key]


@pytest.mark.parametrize("count", [0, 101, -1, 1.5, True, "2", None])
def test_copy_count_is_an_integer_from_one_to_a_hundred(count):
    entry = {"_id": "a", "_lookupMethod": "Exact", "_duplicate": count}
    if count is None:  # a null key is "not set": nothing to change, the entry is dropped
        assert chroma.environment(v2(entry), 5) == []
        return
    with pytest.raises(ValueError):
        chroma.environment(v2(entry), 5)


def test_encode_writes_bsdachr4_only_when_an_entry_copies():
    plain = chroma.encode(LEVEL_ID, [chart([dict(method=1, id="x", active=False)], "KDAEnvironment")])
    assert plain[:8] == b"BSDACHR3"
    data = chroma.encode(LEVEL_ID, [chart([dict(method=1, id="x", duplicate=3, scale=[2.0, 1.0, 2.0])])])
    assert data[:8] == b"BSDACHR4"
    entry = struct.pack("<BH", 1, 1) + b"x" + struct.pack("<BB", chroma.ENV_DUPLICATE | 4, 3) + struct.pack("<3f", 2.0, 1.0, 2.0)
    assert data.endswith(entry)


def test_v3_block_refuses_a_copy_and_counts_are_checked_at_encode():
    with pytest.raises(ValueError):
        chroma._environment_block(dict(name="E", entries=[dict(method=1, id="x", duplicate=1)]), 3)
    for bad in (0, 101, 2.0, True):
        with pytest.raises(ValueError):
            chroma.encode(LEVEL_ID, [chart([dict(method=1, id="x", duplicate=bad)])])


def test_reference_sidecar_v4_layout():
    data = reference_sidecar_v4()
    assert data[:8] == b"BSDACHR4"
    neon = rb"\[19\]FrontLights\.\[0\]NeonTube$"
    assert struct.pack("<BH", 0, len(neon)) + neon + struct.pack("<BB", chroma.ENV_DUPLICATE, 10) in data


def test_size_cap_is_sixteen_mib():
    assert chroma.MAX_BYTES == 16 << 20
