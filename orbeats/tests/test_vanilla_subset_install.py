"""Opt-in vanilla-subset wiring: download, installer hand-off, CLI, queue. No console/network."""
import hashlib
import io
import json
import sys
import zipfile
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
sys.path.insert(0, str(ROOT))
import additive_companion as app


def pack(tmp_path, name="source"):
    src = tmp_path / name
    src.mkdir()
    for file in ("song.egg", "cover.png", "EasyStandard.dat", "EasyLawless.dat"):
        (src / file).write_bytes(file.encode())
    info = {"_version": "2.1.0", "_songFilename": "song.egg", "_coverImageFilename": "cover.png",
            "_difficultyBeatmapSets": [
                {"_beatmapCharacteristicName": "Standard", "_difficultyBeatmaps": [
                    {"_difficulty": "Easy", "_beatmapFilename": "EasyStandard.dat",
                     "_customData": {"_requirements": ["Vivify"]}}]},
                {"_beatmapCharacteristicName": "Lawless", "_difficultyBeatmaps": [
                    {"_difficulty": "Easy", "_beatmapFilename": "EasyLawless.dat"}]}]}
    (src / "Info.dat").write_text(json.dumps(info))
    return src, info


def fake_beatsaver(monkeypatch, info, files, diffs):
    import urllib.request

    raw = json.dumps(info).encode()
    digest = hashlib.sha1(raw + b"".join(files[n] for n in ("EasyStandard.dat", "EasyLawless.dat"))).hexdigest()
    out = io.BytesIO()
    with zipfile.ZipFile(out, "w") as z:
        z.writestr("Info.dat", raw)
        for name, data in files.items():
            z.writestr(name, data)

    class Response(io.BytesIO):
        def __init__(self, data, url):
            super().__init__(data)
            self.url = url

        def geturl(self):
            return self.url

    def urlopen(req, timeout):
        if "/maps/id/" in req.full_url:
            meta = {"versions": [{"hash": digest, "downloadURL": "https://r2cdn.beatsaver.com/f.zip", "diffs": diffs}]}
            return Response(json.dumps(meta).encode(), req.full_url)
        return Response(out.getvalue(), req.full_url)

    monkeypatch.setattr(urllib.request, "urlopen", urlopen)


def test_download_defers_mod_flags_to_the_subset_only_when_opted_in(monkeypatch, tmp_path):
    src, info = pack(tmp_path)
    files = {n: (src / n).read_bytes() for n in ("song.egg", "cover.png", "EasyStandard.dat", "EasyLawless.dat")}
    diffs = [{"characteristic": "Standard", "difficulty": "Easy", "chroma": True, "vivify": True},
             {"characteristic": "Lawless", "difficulty": "Easy", "chroma": False}]
    fake_beatsaver(monkeypatch, info, files, diffs)
    with pytest.raises(ValueError, match="Chroma in Easy"):
        app.download("475ef", tmp_path / "strict")
    assert not (tmp_path / "strict").exists()
    folder = app.download("475ef", tmp_path / "subset", vanilla_subset=True)
    # The original content hash is still verified on the complete pack.
    assert (folder / "EasyStandard.dat").is_file() and (folder / "beatsaver-metadata.json").is_file()


def test_selection_records_the_report_and_leaves_vanilla_packs_untouched(tmp_path, capsys):
    src, _ = pack(tmp_path)
    (src / "beatsaver-metadata.json").write_text('{"id": "475ef"}')
    chosen, report = app.select_vanilla_subset(src, tmp_path / "subset")
    assert chosen == (tmp_path / "subset").resolve() and report["changed"] and not report["score_identity"]
    stored = json.loads((chosen / "beatsaver-metadata.json").read_text())
    assert stored["id"] == "475ef"
    assert stored["orbeats_vanilla_subset"] == {
        "kept": ["Standard/Easy"], "dropped": ["Standard/Easy: requires Vivify"], "renamed": ["Lawless -> Standard"],
        "stripped": []}
    assert json.loads((src / "beatsaver-metadata.json").read_text()) == {"id": "475ef"}  # original untouched
    out = capsys.readouterr().out
    assert "Not installed: Standard/Easy: requires Vivify" in out and "Lawless -> Standard" in out
    assert "leaderboard" in out

    plain = tmp_path / "plain"
    plain.mkdir()
    (plain / "song.egg").write_bytes(b"a")
    (plain / "Easy.dat").write_bytes(b"c")
    (plain / "Info.dat").write_text(json.dumps({"_version": "2.1.0", "_songFilename": "song.egg",
        "_difficultyBeatmapSets": [{"_beatmapCharacteristicName": "Standard", "_difficultyBeatmaps": [
            {"_difficulty": "Easy", "_beatmapFilename": "Easy.dat"}]}]}))
    chosen, report = app.select_vanilla_subset(plain, tmp_path / "subset2")
    assert chosen == plain and report["changed"] is False


def test_installer_hashes_the_original_pack_and_skips_identity_for_renamed_charts():
    text = (ROOT / "tools/additive_companion.py").read_text(encoding="utf-8")
    body = text[text.index("def install("):]
    assert "def install(config, map_id=\"\", song_dir=\"\", report_dir=None, vanilla_subset=False):" in text
    # Subset selection happens on the verified original, before any format conversion.
    assert body.index("select_vanilla_subset(") < body.index('helper("import_v4_additive.py").normalize(')
    assert "score_source.original_hash(source if identity_source is None else identity_source," in body
    assert body.count("score_source.publish(") == 2 and body.count("if publish_identity:") == 2
    for call in body.split("score_source.publish(")[:-1]:
        assert call.rstrip().endswith("if publish_identity:")
    assert 'download(map_id, tmp / "download", vanilla_subset, chroma=cap.get("chroma") in (1, 2, 3))' in body


def test_cli_and_companion_pass_the_flag_only_when_requested(tmp_path, monkeypatch):
    import full_custom_song_pipeline as pipeline

    from webapp.adapters import paths
    from webapp.adapters.deploy import DeployOptions

    base = ["--additive-install", "--download-beat-saver-song", "475ef", "--config", str(paths.CONFIG_PATH)]
    assert DeployOptions(additive=True, map_id="475ef").build_argv() == base
    assert DeployOptions(additive=True, map_id="475ef", vanilla_subset=True).build_argv() == base + ["--vanilla-subset"]
    with pytest.raises(ValueError, match="additive"):
        DeployOptions(map_id="475ef", target="x", vanilla_subset=True).build_argv()

    cfg = tmp_path / "config.json"
    cfg.write_text(json.dumps({"ps4": {"ip": "test"}}))
    seen = []
    monkeypatch.setattr(app, "install", lambda config, **kw: seen.append(kw) or {"title": "t", "count": 1})
    for extra in ([], ["--vanilla-subset"]):
        monkeypatch.setattr(sys, "argv", ["pipeline", "--additive-install", "--song-dir", "fixture",
                                          "--config", str(cfg), *extra])
        try:
            pipeline.main()
        except SystemExit as exc:
            assert exc.code in (0, None)
    assert [kw.get("vanilla_subset", False) for kw in seen] == [False, True]
    monkeypatch.setattr(sys, "argv", ["pipeline", "--vanilla-subset", "--song-dir", "fixture", "--config", str(cfg)])
    with pytest.raises(SystemExit) as exc:
        pipeline.main()
    assert exc.value.code == 2  # never silently applies to the legacy replacement flow


def test_queue_keeps_the_choice_per_map_and_restores_it(monkeypatch):
    from webapp.adapters import install_queue

    started = []

    class Job:
        id, exit_code, cancelled, lines = 1, 0, False, []

    class Runner:
        def start(self, argv, label):
            started.append(argv)
            return Job()

    queue = install_queue.InstallQueue(Runner())
    assert queue.add("475ef", "Calamity", vanilla_subset=True)["vanilla_subset"] is True
    assert "vanilla_subset" not in queue.add("4ebb2", "Plain")
    for bad in ("true", 1, None):
        with pytest.raises(ValueError, match="vanilla_subset"):
            queue.add("abc", "x", vanilla_subset=bad)
    snapshot = queue.status()
    restored = install_queue.InstallQueue(Runner())
    restored.restore(snapshot)
    assert [e.get("vanilla_subset", False) for e in restored.status()["pending"]] == [True, False]
    queue.start()
    queue._thread and queue._thread.join(5)
    assert ["--vanilla-subset" in argv for argv in started] == [True, False]
