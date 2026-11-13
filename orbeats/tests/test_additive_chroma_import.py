"""Level-1 Chroma through the real V3 importer: extraction, whitelist, requirements."""
import copy
import importlib.util
import json
import shutil
import wave
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location("v3_import_chroma", ROOT / "development/scripts/import_v3_additive.py")
imp = importlib.util.module_from_spec(spec)
spec.loader.exec_module(imp)


def ffmpeg():
    found = shutil.which("ffmpeg")
    if found:
        return found
    try:
        import imageio_ffmpeg
    except ImportError:
        pytest.skip("ffmpeg required")
    return imageio_ffmpeg.get_ffmpeg_exe()


def v3_chart(**extra):
    chart = {
        "version": "3.3.0",
        "colorNotes": [
            {"b": 2, "x": 1, "y": 0, "a": 0, "c": 1, "d": 1, "customData": {"color": [0.1, 0.2, 0.3]}},
            {"b": 4, "x": 2, "y": 1, "a": 0, "c": 0, "d": 1},
        ],
        "bombNotes": [],
        "obstacles": [{"b": 1, "x": 0, "y": 0, "d": 2, "w": 1, "h": 5, "customData": {"color": [1, 0, 1, 0.5]}}],
        "basicBeatmapEvents": [
            {"b": 3, "et": 1, "i": 5, "f": 1},
            {"b": 3, "et": 1, "i": 1, "f": 1, "customData": {"color": [2, 0, 0], "lightID": [1, 2]}},
            {"b": 1, "et": 4, "i": 3, "f": 1, "customData": {"color": [0, 1, 0, 1]}},
        ],
        "basicEventTypesWithKeywords": {"d": []},
        "useNormalEventsAsCompatibleEvents": False,
    }
    chart.update(extra)
    return chart


def v2_chart():
    return {
        "_version": "2.2.0",
        "_notes": [
            {"_time": 2, "_lineIndex": 1, "_lineLayer": 0, "_type": 1, "_cutDirection": 1, "_customData": {"_color": [0.1, 0.2, 0.3]}},
            {"_time": 4, "_lineIndex": 2, "_lineLayer": 1, "_type": 0, "_cutDirection": 1},
        ],
        "_obstacles": [
            {"_time": 1, "_lineIndex": 0, "_type": 1, "_duration": 2, "_width": 1, "_customData": {"_color": [1, 0, 1]}}
        ],
        "_events": [
            {"_time": 3, "_type": 1, "_value": 1, "_customData": {"_color": [2, 0, 0], "_lightID": 7}},
            {"_time": 2, "_type": 1, "_value": 5},
        ],
    }


def make_source(tmp_path, charts, requirements=None, bpm=120, environment=None):
    source = tmp_path / "source"
    source.mkdir(parents=True)
    with wave.open(str(source / "audio.wav"), "wb") as w:
        w.setnchannels(2)
        w.setsampwidth(2)
        w.setframerate(44100)
        w.writeframes(bytes([0, 2, 0, 2]) * 44100 * 4)
    diffs = []
    for index, (name, chart) in enumerate(charts.items()):
        (source / (name + ".dat")).write_text(json.dumps(chart))
        entry = {"_difficulty": name, "_difficultyRank": 7 + 2 * index, "_beatmapFilename": name + ".dat",
                 "_noteJumpMovementSpeed": 12, "_noteJumpStartBeatOffset": 0}
        if requirements is not None:
            entry["_customData"] = {"_requirements": requirements}
        diffs.append(entry)
    info = {"_version": "2.1.0", "_songName": "Chroma fixture", "_songAuthorName": "Fixture", "_beatsPerMinute": bpm,
            "_songFilename": "audio.wav", "_songTimeOffset": 0,
            "_difficultyBeatmapSets": [{"_beatmapCharacteristicName": "Standard", "_difficultyBeatmaps": diffs}]}
    if environment:
        info["_environmentName"] = environment
    (source / "Info.dat").write_text(json.dumps(info))
    return source


def run(tmp_path, charts, chroma=True, requirements=None, bpm=120, environment=None):
    source = make_source(tmp_path, charts, requirements, bpm, environment)
    out = tmp_path / "normalized"
    return imp.import_song(source, out, ffmpeg=ffmpeg(), chroma=chroma), out


def test_v3_colors_become_records_and_leave_the_bundle_untouched(tmp_path):
    report, out = run(tmp_path, {"Expert": v3_chart()})
    (chart,) = report["chroma_charts"]
    assert (chart["mode"], chart["difficulty"]) == (0, 3)
    # 120 BPM: one beat is half a second. Events: type 1 sorted stably, indices over ALL type-1 events.
    assert chart["events"] == [
        dict(type=1, index=2, time=1.5, value=1, rgba=[2.0, 0.0, 0.0, 1.0], lights=[]),
        dict(type=4, index=1, time=0.5, value=3, rgba=[0.0, 1.0, 0.0, 1.0], lights=[]),
    ]
    assert chart["notes"] == [dict(time=1.0, x=1, y=0, color_type=1, kind=0, rgba=[0.1, 0.2, 0.3, 1.0])]
    assert chart["walls"] == [dict(time=0.5, x=0, y=0, w=1, h=5, duration=1.0, rgba=[1.0, 0.0, 1.0, 0.5])]
    assert "chroma_charts" not in json.loads((out / "import-report.json").read_text())
    text = json.dumps(imp.prepare.load_song(out)["maps"])
    assert "customData" not in text and "lightID" not in text


def test_bpm_changes_use_the_authored_tempo_map(tmp_path):
    chart = v3_chart(bpmEvents=[{"b": 0, "m": 240}, {"b": 1, "m": 120}])
    report, out = run(tmp_path, {"Expert": chart})
    # beat 3 = 1 beat at 240 (0.25 s) + 2 beats at 120 (1.0 s)
    event = next(e for e in report["chroma_charts"][0]["events"] if e["type"] == 1)
    assert event["time"] == pytest.approx(1.25)
    stored = imp.prepare.load_song(out)["maps"]["Expert"]["basicBeatmapEvents"]
    # The bundle keeps the same instant as a baked beat at the base BPM.
    assert min(e["b"] for e in stored if e["et"] == 1) * 60 / 120 == pytest.approx(1.25)


def test_v2_chart_records_use_the_converted_geometry(tmp_path):
    report, out = run(tmp_path, {"Expert": v2_chart()})
    (chart,) = report["chroma_charts"]
    assert [(e["type"], e["index"], e["time"], e["value"]) for e in chart["events"]] == [(1, 2, 1.5, 1)]
    assert chart["notes"][0]["time"] == 1.0 and chart["notes"][0]["color_type"] == 1
    assert chart["walls"][0]["y"] == 2 and chart["walls"][0]["h"] == 3 and chart["walls"][0]["duration"] == 1.0
    assert "_customData" not in json.dumps(imp.prepare.load_song(out)["maps"])


def test_charts_without_chroma_data_report_nothing(tmp_path):
    plain = v3_chart()
    for key in ("colorNotes", "obstacles", "basicBeatmapEvents"):
        for entry in plain[key]:
            entry.pop("customData", None)
    report, _ = run(tmp_path, {"Expert": plain})
    assert "chroma_charts" not in report


def test_unsupported_key_is_refused_with_its_name(tmp_path):
    chart = v3_chart()
    chart["basicBeatmapEvents"][0]["customData"] = {"lightGradient": {}, "color": [1, 1, 1]}
    with pytest.raises(ValueError, match="event.lightGradient"):
        run(tmp_path, {"Expert": chart}, requirements=["Chroma"])


@pytest.mark.parametrize("top", ["environment", "customEvents", "pointDefinitions", "materials"])
def test_top_level_chroma_keys_are_still_refused(tmp_path, top):
    chart = v3_chart(customData={top: []})
    with pytest.raises(ValueError):
        run(tmp_path, {"Expert": chart}, requirements=["Chroma"])


def test_without_plugin_marker_colors_are_refused_as_before(tmp_path):
    with pytest.raises(ValueError):
        run(tmp_path, {"Expert": v3_chart()}, chroma=False)


def test_chroma_requirement_needs_the_marker_and_clean_charts(tmp_path):
    report, _ = run(tmp_path, {"Expert": v3_chart()}, requirements=["Chroma"])
    assert report["chroma_charts"]
    with pytest.raises(ValueError, match="requirements"):
        run(tmp_path / "off", {"Expert": v3_chart()}, chroma=False, requirements=["Chroma"])


def test_other_requirements_stay_refused(tmp_path):
    for value in (["Chroma", "Noodle Extensions"], ["Noodle Extensions"], ["Mapping Extensions"]):
        with pytest.raises(ValueError, match="requirements"):
            run(tmp_path / str(len(value)) / value[-1], {"Expert": v3_chart()}, requirements=value)


def test_requirement_is_refused_when_any_installed_chart_uses_other_keys(tmp_path):
    good, bad = v3_chart(), v3_chart()
    bad["colorNotes"][0]["customData"] = {"color": [1, 1, 1], "position": [0, 0]}
    with pytest.raises(ValueError):
        run(tmp_path, {"Hard": good, "Expert": bad}, requirements=["Chroma"])


def test_import_does_not_mutate_the_source(tmp_path):
    chart = v3_chart()
    before = copy.deepcopy(chart)
    source = make_source(tmp_path, {"Expert": chart})
    snapshot = {p.name: p.read_bytes() for p in source.iterdir()}
    imp.import_song(source, tmp_path / "normalized", ffmpeg=ffmpeg(), chroma=True)
    assert {p.name: p.read_bytes() for p in source.iterdir()} == snapshot
    assert chart == before


def test_v2_gradient_is_imported_only_for_the_level_three_plugin(tmp_path):
    chart = v2_chart()
    chart["_events"].append({"_time": 6, "_type": 2, "_value": 5, "_customData": {"_lightGradient": {
        "_duration": 2, "_startColor": [1, 0, 0], "_endColor": [0, 0, 1], "_easing": "easeOutQuad"}}})
    with pytest.raises(ValueError, match="event._lightGradient"):
        run(tmp_path / "level2", {"Expert": chart}, chroma=2, requirements=["Chroma"])
    report, out = run(tmp_path / "level3", {"Expert": chart}, chroma=3, requirements=["Chroma"])
    gradient = next(e for e in report["chroma_charts"][0]["events"] if e["type"] == 2)
    assert gradient["time"] == 3.0 and gradient["gradient"] == dict(start=[1.0, 0.0, 0.0, 1.0], end=[0.0, 0.0, 1.0, 1.0], duration=1.0, easing=6)
    assert "_lightGradient" not in json.dumps(imp.prepare.load_song(out)["maps"])


def test_environment_is_imported_only_for_the_level_four_plugin(tmp_path):
    entries = [{"id": "Tentacle", "lookupMethod": "Contains", "active": False},
               {"id": r"\[\d+\]Smoke$", "lookupMethod": "Regex", "scale": [2, 2, 2]}]
    chart = v3_chart(customData={"environment": entries})
    with pytest.raises(ValueError, match="chart.environment"):
        run(tmp_path / "level3", {"Expert": chart}, chroma=3, requirements=["Chroma"], environment="KDAEnvironment")
    report, out = run(tmp_path / "level4", {"Expert": chart}, chroma=4, requirements=["Chroma"], environment="KDAEnvironment")
    (record,) = report["chroma_charts"]
    assert record["environment"] == dict(name="KDAEnvironment", entries=[
        dict(method=2, id="Tentacle", active=False), dict(method=0, id=r"\[\d+\]Smoke$", scale=[2.0, 2.0, 2.0])])
    song = imp.prepare.load_song(out)
    assert "Tentacle" not in json.dumps(song["maps"]) and json.loads((out / "Info.dat").read_text())["_environmentName"] == "KDAEnvironment"
    # The sidecar for this chart is BSDACHR3.
    import additive_chroma
    assert additive_chroma.encode("bsd_additive_0123456789abcdef01234567", report["chroma_charts"])[:8] == b"BSDACHR3"


def test_environment_copies_are_imported_only_for_the_level_five_plugin(tmp_path):
    entries = [{"id": r"\[19\]FrontLights\.\[0\]NeonTube$", "lookupMethod": "Regex", "duplicate": 10},
               {"id": "NeonTube(Clone)", "lookupMethod": "EndsWith", "scale": [2, 1, 2]}]
    chart = v3_chart(customData={"environment": entries})
    with pytest.raises(ValueError, match="environment.duplicate"):
        run(tmp_path / "level4", {"Expert": chart}, chroma=4, requirements=["Chroma"], environment="BigMirrorEnvironment")
    report, _ = run(tmp_path / "level5", {"Expert": chart}, chroma=5, requirements=["Chroma"], environment="BigMirrorEnvironment")
    (record,) = report["chroma_charts"]
    assert record["environment"]["entries"][0] == dict(method=0, id=r"\[19\]FrontLights\.\[0\]NeonTube$", duplicate=10)
    import additive_chroma
    assert additive_chroma.encode("bsd_additive_0123456789abcdef01234567", report["chroma_charts"])[:8] == b"BSDACHR4"


def test_environment_changes_need_a_stock_environment(tmp_path):
    chart = v3_chart(customData={"environment": [{"id": "Smoke", "lookupMethod": "Contains", "active": False}]})
    with pytest.raises(ValueError, match="stock environment"):
        run(tmp_path / "none", {"Expert": chart}, chroma=4, requirements=["Chroma"])
    with pytest.raises(ValueError, match="stock environment"):
        run(tmp_path / "custom", {"Expert": chart}, chroma=4, requirements=["Chroma"], environment="MyCustomEnvironment")


def test_info_indices_copied_into_a_chart_are_dropped(tmp_path):
    # BeatSaver 3dbbe: editors copy _beatmapColorSchemeIdx/_environmentNameIdx into the chart file.
    chart = v3_chart(_beatmapColorSchemeIdx=0, _environmentNameIdx=0)
    report, out = run(tmp_path / "ok", {"Expert": chart}, requirements=["Chroma"])
    assert "_environmentNameIdx" not in json.dumps(imp.prepare.load_song(out)["maps"])
    with pytest.raises(ValueError, match="copied Info index"):
        run(tmp_path / "bad", {"Expert": v3_chart(_environmentNameIdx="0")}, requirements=["Chroma"])


def test_lights_only_chart_is_skipped_before_any_conversion(tmp_path):
    lights = {"version": "3.3.0", "basicBeatmapEvents": [{"b": 1, "et": 1, "i": 1, "f": 1, "customData": {"color": [1, 0, 0]}}]}
    report, _ = run(tmp_path, {"Easy": lights, "Expert": v3_chart()}, requirements=["Chroma"])
    assert report["skipped_difficulties"] == ["Easy (lighting-only; no playable objects)"]
    assert [c["difficulty"] for c in report["chroma_charts"]] == [3]
