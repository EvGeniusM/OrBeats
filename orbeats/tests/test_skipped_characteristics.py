"""Lightshow/Legacy/Lawless charts are left out and reported; the rest of the pack installs.

Synthetic packs only; no network, game inputs or console writes.
"""

import io
import json
import shutil
import zipfile

import additive_companion as app
import pytest
from additive_modes import SKIPPED, skipped

v3 = app.helper("import_v3_additive.py")
v4 = app.helper("import_v4_additive.py")
prepare = app.helper("prepare_additive_bundle.py")
NOTE = {"_time": 1, "_lineIndex": 0, "_lineLayer": 1, "_type": 1, "_cutDirection": 2}
# Data no importer accepts: a left-out chart must not be read as gameplay at all.
NOODLE = {**NOTE, "_customData": {"_position": [9, 9]}}


def v2_pack(tmp_path, extra_sets, standard=True):
    folder = tmp_path / "source"
    prepare.generate_probe(folder)
    info = json.loads((folder / "Info.dat").read_bytes())
    sets = info["_difficultyBeatmapSets"] if standard else []
    for mode, difficulty, custom in extra_sets:
        name = difficulty + mode + ".dat"
        (folder / name).write_text(json.dumps({"_version": "2.0.0", "_notes": [NOODLE], "_obstacles": [], "_events": []}))
        entry = {"_difficulty": difficulty, "_difficultyRank": 7, "_beatmapFilename": name,
                 "_noteJumpMovementSpeed": 16, "_noteJumpStartBeatOffset": 0}
        if custom:
            entry["_customData"] = custom
        sets.append({"_beatmapCharacteristicName": mode, "_difficultyBeatmaps": [entry]})
    info["_difficultyBeatmapSets"] = sets
    (folder / "Info.dat").write_text(json.dumps(info))
    return folder


def test_report_lines_name_every_left_out_characteristic():
    assert set(SKIPPED) == {"Lightshow", "Legacy", "Lawless"}
    assert skipped("Lawless", "Expert") == "Lawless/Expert (Lawless set, not supported yet)"
    assert skipped("Standard", "Expert") is None


def test_v2_pack_installs_standard_and_reports_left_out_sets(tmp_path):
    if not shutil.which("ffmpeg"):
        pytest.skip("ffmpeg required")
    folder = v2_pack(tmp_path, [
        ("Lightshow", "ExpertPlus", None),
        ("Legacy", "Expert", {"_requirements": ["Noodle Extensions"]}),
        ("Lawless", "ExpertPlus", {"_requirements": ["Mapping Extensions"]}),
    ])
    report = v3.import_song(folder, tmp_path / "out", chroma=6)
    assert report["skipped_difficulties"] == [
        "Lightshow/ExpertPlus (light show without notes)",
        "Legacy/Expert (older version of the map)",
        "Lawless/ExpertPlus (Lawless set, not supported yet)",
    ]
    info = json.loads((tmp_path / "out" / "Info.dat").read_bytes())
    assert [s["_beatmapCharacteristicName"] for s in info["_difficultyBeatmapSets"]] == ["Standard"]


def test_pack_with_only_left_out_sets_is_refused_and_names_them(tmp_path):
    folder = v2_pack(tmp_path, [("Lightshow", "Easy", None), ("Legacy", "Hard", None)], standard=False)
    with pytest.raises(ValueError, match=r"Left out: Lightshow/Easy .*; Legacy/Hard"):
        v3.import_song(folder, tmp_path / "out")


def test_other_unknown_characteristics_still_refuse(tmp_path):
    folder = v2_pack(tmp_path, [("Rhythm", "Easy", None)])
    with pytest.raises(ValueError, match="characteristic"):
        v3.import_song(folder, tmp_path / "out")


@pytest.mark.parametrize("bad", [["x"] * 26, [1], ["x" * 121]])
def test_forged_left_out_report_is_refused(tmp_path, bad):
    folder = v2_pack(tmp_path, [])
    info = json.loads((folder / "Info.dat").read_bytes())
    info["_orbeatsSkipped"] = bad
    (folder / "Info.dat").write_text(json.dumps(info))
    with pytest.raises(ValueError, match="left-out chart report"):
        v3.import_song(folder, tmp_path / "out")


def test_v4_normalize_drops_left_out_charts_before_any_check(tmp_path):
    from test_additive_v4_import import source

    root, info = source(tmp_path)
    for mode in ("Lightshow", "Lawless"):
        # Missing chart file and mod data: reading it at all would fail.
        info["difficultyBeatmaps"].append({**info["difficultyBeatmaps"][0], "characteristic": mode,
                                           "beatmapDataFilename": "missing.dat"})
    (root / "Info.dat").write_text(json.dumps(info))
    normalized = v4.normalize(root, tmp_path / "v4", prepare.local_file)
    assert normalized["_orbeatsSkipped"] == [
        "Lightshow/ExpertPlus (light show without notes)",
        "Lawless/ExpertPlus (Lawless set, not supported yet)",
    ]
    assert [g["_beatmapCharacteristicName"] for g in normalized["_difficultyBeatmapSets"]] == ["Standard"]
    info["difficultyBeatmaps"] = info["difficultyBeatmaps"][1:]
    (root / "Info.dat").write_text(json.dumps(info))
    with pytest.raises(ValueError, match="No playable charts; left out: Lightshow"):
        v4.normalize(root, tmp_path / "v4-only", prepare.local_file)


def test_download_ignores_mod_flags_of_left_out_charts_only(tmp_path, monkeypatch):
    meta = {"versions": [{"diffs": [{"characteristic": "Standard", "difficulty": "Expert", "ne": False},
                                    {"characteristic": "Lawless", "difficulty": "Expert", "ne": True},
                                    {"characteristic": "Lightshow", "difficulty": "Expert", "me": True}],
                          "hash": "0" * 40, "downloadURL": "https://r2cdn.beatsaver.com/x.zip"}]}
    archive = io.BytesIO()
    with zipfile.ZipFile(archive, "w") as z:
        z.writestr("Info.dat", "{}")

    class Response(io.BytesIO):
        def geturl(self):
            return "https://r2cdn.beatsaver.com/x.zip"

    monkeypatch.setattr(app.urllib.request, "urlopen", lambda request, **kw: Response(
        json.dumps(meta).encode() if "/maps/id/" in request.full_url else archive.getvalue()))
    # Past the mod-flag check: the next failure is the archive content, not the Lawless/Lightshow flags.
    with pytest.raises(ValueError) as caught:
        app.download("1a2b", tmp_path / "ok")
    assert "unsupported extensions" not in str(caught.value)
    meta["versions"][0]["diffs"][0]["ne"] = True
    with pytest.raises(ValueError, match="Noodle Extensions in Expert"):
        app.download("1a2b", tmp_path / "blocked")
