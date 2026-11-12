"""Opt-in vanilla subset of mod-assisted InfoV2/InfoV4 packs (no console, no game data)."""
import json
import sys
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
import vanilla_subset as vs


def make(tmp_path, sets, info_custom=None):
    src = tmp_path / "src"
    src.mkdir()
    (src / "song.egg").write_bytes(b"audio")
    (src / "cover.png").write_bytes(b"cover")
    out = []
    for mode, diffs in sets:
        items = []
        for name, custom in diffs:
            filename = f"{name}{mode}.dat"
            (src / filename).write_bytes(f"{mode}{name}".encode())
            item = {"_difficulty": name, "_difficultyRank": 1, "_beatmapFilename": filename}
            if custom is not None:
                item["_customData"] = custom
            items.append(item)
        out.append({"_beatmapCharacteristicName": mode, "_difficultyBeatmaps": items})
    info = {"_version": "2.1.0", "_songName": "t", "_songFilename": "song.egg", "_coverImageFilename": "cover.png",
            "_difficultyBeatmapSets": out}
    if info_custom is not None:
        info["_customData"] = info_custom
    (src / "Info.dat").write_text(json.dumps(info))
    return src


def test_lawless_becomes_standard_when_mod_sets_are_dropped(tmp_path):
    src = make(tmp_path, [
        ("Standard", [("Easy", {"_requirements": ["Vivify"], "_suggestions": ["Chroma"]})]),
        ("Lawless", [("Easy", {"_information": ["x"], "_settings": {"_chroma": {}}, "_difficultyLabel": "Tragedy"})]),
    ], {"_assetBundle": {"a": 1}, "_editors": {}})
    report = vs.apply(src, tmp_path / "out")
    assert report["kept"] == ["Standard/Easy"]
    assert report["renamed"] == ["Lawless -> Standard"]
    assert any("requires Vivify" in x for x in report["dropped"])
    info = json.loads((tmp_path / "out" / "Info.dat").read_text())
    easy = info["_difficultyBeatmapSets"][0]["_difficultyBeatmaps"][0]
    assert easy["_beatmapFilename"] == "EasyLawless.dat"
    assert easy["_customData"] == {"_difficultyLabel": "Tragedy"}
    assert info["_customData"] == {"_editors": {}}
    # Chart bytes are copied untouched; mod-only charts are not carried over.
    assert (tmp_path / "out" / "EasyLawless.dat").read_bytes() == b"LawlessEasy"
    assert not (tmp_path / "out" / "EasyStandard.dat").exists()


def test_real_standard_wins_over_lawless_and_is_not_merged(tmp_path):
    src = make(tmp_path, [
        ("Standard", [("Easy", None)]),
        ("Lawless", [("Easy", None)]),
    ])
    report = vs.apply(src, tmp_path / "out")
    assert report["kept"] == ["Standard/Easy"]
    assert report["renamed"] == []
    # A discarded Lawless set is reported, never dropped silently.
    assert report["dropped"] == ["Lawless/Easy: real Standard set is kept"]
    assert report["score_identity"] is True and report["changed"] is True


def test_unknown_gameplay_metadata_is_not_stripped(tmp_path):
    src = make(tmp_path, [("Standard", [("Easy", {"_customEnvironment": "Evil", "_settings": {}})])])
    vs.apply(src, tmp_path / "out")
    info = json.loads((tmp_path / "out" / "Info.dat").read_text())
    custom = info["_difficultyBeatmapSets"][0]["_difficultyBeatmaps"][0]["_customData"]
    assert custom == {"_customEnvironment": "Evil"}  # still reaches the strict importer


def test_nothing_playable_is_refused(tmp_path):
    src = make(tmp_path, [("Standard", [("Easy", {"_requirements": ["Noodle Extensions"]})])])
    with pytest.raises(ValueError, match="No vanilla-playable"):
        vs.apply(src, tmp_path / "out")
    v4 = tmp_path / "v4"
    v4.mkdir()
    (v4 / "Info.dat").write_text(json.dumps({"version": "4.0.1", "audio": {}, "difficultyBeatmaps": []}))
    with pytest.raises(ValueError, match="No vanilla-playable"):
        vs.apply(v4, tmp_path / "out2")


def test_unsupported_characteristic_is_dropped_not_renamed(tmp_path):
    src = make(tmp_path, [("Standard", [("Easy", None)]), ("Generated360Degree", [("Easy", None)])])
    report = vs.apply(src, tmp_path / "out")
    assert report["kept"] == ["Standard/Easy"]
    assert any("unsupported characteristic" in x for x in report["dropped"])


def test_rename_disables_score_identity_and_metadata_travels_with_subset(tmp_path):
    src = make(tmp_path, [
        ("Standard", [("Easy", {"_requirements": ["Vivify"]})]),
        ("Lawless", [("Easy", None)]),
    ])
    (src / "beatsaver-metadata.json").write_text('{"id": "abc"}')
    report = vs.apply(src, tmp_path / "out")
    # Renamed charts must not borrow the mod chart's Standard leaderboard.
    assert report["score_identity"] is False and report["changed"] is True
    assert json.loads((tmp_path / "out" / "beatsaver-metadata.json").read_text()) == {"id": "abc"}


def test_fully_vanilla_pack_reports_no_change(tmp_path):
    src = make(tmp_path, [("Standard", [("Easy", None), ("Hard", {"_difficultyLabel": "x"})])])
    report = vs.apply(src, tmp_path / "out")
    assert report["changed"] is False and report["score_identity"] is True
    assert report["dropped"] == report["renamed"] == report["stripped"] == []


def make_v4(tmp_path, diffs, info_custom=None, version="4.0.1"):
    src = tmp_path / "src4"
    src.mkdir()
    for name in ("song.ogg", "cover.png", "Audio.dat", "Shared.light.dat"):
        (src / name).write_bytes(name.encode())
    items = []
    for mode, name, custom, light in diffs:
        chart = f"{name}{mode}.beatmap.dat"
        (src / chart).write_bytes(f"{mode}{name}".encode())
        if light != "Shared.light.dat":
            (src / light).write_bytes(light.encode())
        item = {"characteristic": mode, "difficulty": name, "beatmapDataFilename": chart,
                "lightshowDataFilename": light}
        if custom is not None:
            item["customData"] = custom
        items.append(item)
    info = {"version": version, "song": {"title": "t", "author": "a"},
            "audio": {"songFilename": "song.ogg", "audioDataFilename": "Audio.dat", "bpm": 120},
            "coverImageFilename": "cover.png", "difficultyBeatmaps": items}
    if info_custom is not None:
        info["customData"] = info_custom
    (src / "Info.dat").write_text(json.dumps(info))
    return src


def test_v4_subset_reads_both_requirement_spellings_and_keeps_shared_lightshow(tmp_path):
    src = make_v4(tmp_path, [
        ("Standard", "Easy", {"requirements": ["Vivify"]}, "Mod.light.dat"),
        ("Standard", "Hard", {"_requirements": ["Noodle Extensions"]}, "Mod.light.dat"),
        ("Lawless", "Easy", {"information": ["x"], "difficultyLabel": "Tragedy"}, "Shared.light.dat"),
        ("Lawless", "Hard", None, "Shared.light.dat"),
        ("Lightshow", "Expert", None, "Shared.light.dat"),
    ], {"assetBundle": {"a": 1}, "editors": {}})
    report = vs.apply(src, tmp_path / "out")
    assert report["kept"] == ["Standard/Easy", "Standard/Hard"]
    assert report["renamed"] == ["Lawless -> Standard"] and report["score_identity"] is False
    assert report["dropped"] == [
        "Standard/Easy: requires Vivify",
        "Standard/Hard: requires Noodle Extensions",
        "Lightshow/Expert: unsupported characteristic",
    ]
    assert report["stripped"] == ["Standard/Easy: information", "Info: assetBundle"]
    out = tmp_path / "out"
    info = json.loads((out / "Info.dat").read_text())
    assert info["customData"] == {"editors": {}}
    assert [(d["characteristic"], d["difficulty"], d["beatmapDataFilename"]) for d in info["difficultyBeatmaps"]] == [
        ("Standard", "Easy", "EasyLawless.beatmap.dat"), ("Standard", "Hard", "HardLawless.beatmap.dat")]
    assert info["difficultyBeatmaps"][0]["customData"] == {"difficultyLabel": "Tragedy"}
    assert sorted(p.name for p in out.iterdir()) == [
        "Audio.dat", "EasyLawless.beatmap.dat", "HardLawless.beatmap.dat", "Info.dat", "Shared.light.dat",
        "cover.png", "song.ogg"]
    assert (out / "HardLawless.beatmap.dat").read_bytes() == b"LawlessHard"


def test_v4_unknown_metadata_survives_and_bad_inputs_refuse(tmp_path):
    src = make_v4(tmp_path, [("Standard", "Easy", {"customMagic": 1}, "Shared.light.dat")])
    vs.apply(src, tmp_path / "out")
    kept = json.loads((tmp_path / "out" / "Info.dat").read_text())["difficultyBeatmaps"][0]
    assert kept["customData"] == {"customMagic": 1}  # still reaches the strict importer
    (tmp_path / "a").mkdir()
    with pytest.raises(ValueError, match="No vanilla-playable"):
        vs.apply(make_v4(tmp_path / "a", [("Standard", "Easy", {"requirements": ["Chroma"]}, "Shared.light.dat")]),
                 tmp_path / "out2")
    (tmp_path / "b").mkdir()
    with pytest.raises(ValueError, match="Unsupported Info format"):
        vs.apply(make_v4(tmp_path / "b", [("Standard", "Easy", None, "Shared.light.dat")], version="4.9.0"),
                 tmp_path / "out3")
    (tmp_path / "c").mkdir()
    escape = make_v4(tmp_path / "c", [("Standard", "Easy", None, "Shared.light.dat")])
    info = json.loads((escape / "Info.dat").read_text())
    info["difficultyBeatmaps"][0]["beatmapDataFilename"] = "../../src4/Info.dat"
    (escape / "Info.dat").write_text(json.dumps(info))
    with pytest.raises(ValueError, match="missing or escapes"):
        vs.apply(escape, tmp_path / "out4")


@pytest.mark.parametrize("bad", ["Vivify", {"a": 1}, [1], 5])
def test_malformed_requirements_drop_the_difficulty_instead_of_passing(tmp_path, bad):
    src = make(tmp_path, [("Standard", [("Easy", {"_requirements": bad}), ("Hard", None)])])
    report = vs.apply(src, tmp_path / "out")
    assert report["kept"] == ["Standard/Hard"]
    assert any(x.startswith("Standard/Easy: ") for x in report["dropped"])
