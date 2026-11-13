"""Chroma sidecar publication, undo and removal on a fake console (no fixtures, no network)."""
import contextlib
from ftplib import error_perm
from pathlib import Path
import sys

import pytest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
sys.path.insert(0, str(ROOT / "webapp"))
import additive_chroma as chroma
import additive_companion as app
import library_manager as manager
from chroma_reference import LEVEL_ID, reference_sidecar

PATH = app.BASE + "/" + LEVEL_ID + "-chroma.bin"


class FTP:
    def __init__(self, files=None):
        self.files, self.deletes, self.renames = dict(files or {}), [], []

    def nlst(self, path):
        if path == "/mnt/sandbox":
            return []
        return [p for p in self.files if p.startswith(path + "/")]

    def retrbinary(self, cmd, callback):
        path = cmd[5:]
        if path not in self.files:
            raise error_perm("550 missing")
        callback(self.files[path])

    def storbinary(self, cmd, stream):
        self.files[cmd[5:]] = stream.read()

    def rename(self, a, b):
        assert b not in self.files
        self.renames.append((a, b))
        self.files[b] = self.files.pop(a)

    def delete(self, path):
        if path not in self.files:
            raise error_perm("550 missing")
        self.deletes.append(path)
        del self.files[path]


def test_publish_stages_verifies_and_renames():
    f = FTP()
    data = reference_sidecar()
    assert chroma.publish(f, app, LEVEL_ID, data, "run") is True
    assert f.files == {PATH: data}
    assert f.renames == [(PATH + ".stage-run", PATH)]


def test_publish_is_idempotent_and_never_overwrites_other_bytes():
    data = reference_sidecar()
    f = FTP({PATH: data})
    assert chroma.publish(f, app, LEVEL_ID, data, "run") is False
    assert not f.renames
    other = FTP({PATH: b"something else"})
    with pytest.raises(ValueError, match="conflicting"):
        chroma.publish(other, app, LEVEL_ID, data, "run")
    assert other.files == {PATH: b"something else"}


def test_publish_refuses_when_title_is_running():
    f = FTP()
    f.nlst = lambda path: ["/mnt/sandbox/CUSA12878_000"] if path == "/mnt/sandbox" else []
    with pytest.raises(ValueError, match="Fully close"):
        chroma.publish(f, app, LEVEL_ID, reference_sidecar(), "run")
    assert not f.files


def test_discard_removes_only_the_exact_bytes_it_created():
    data = reference_sidecar()
    f = FTP({PATH: data})
    chroma.discard(f, app, LEVEL_ID, data)
    assert PATH not in f.files
    foreign = FTP({PATH: b"foreign"})
    chroma.discard(foreign, app, LEVEL_ID, data)
    assert foreign.files == {PATH: b"foreign"}
    chroma.discard(FTP(), app, LEVEL_ID, data)  # missing file is not an error


def test_remove_deletes_the_sidecar_with_the_map(monkeypatch):
    other = "bsd_additive_" + "1" * 24
    songs = [{"id": LEVEL_ID}, {"id": other}]
    files = {}
    for song in songs:
        for path in manager.paths(song):
            files[path] = b"x"
        files[app.BASE + "/" + song["id"] + "-chroma.bin"] = b"chroma " + song["id"].encode()
    f = FTP(files)
    f.rmd = lambda path: None
    monkeypatch.setattr(manager, "connection", lambda config, write=False: contextlib.nullcontext(f))
    monkeypatch.setattr(manager.core, "load_library", lambda ftp: (list(songs), b"old"))
    monkeypatch.setattr(manager.core, "commit_library", lambda *a: None)
    monkeypatch.setattr(manager.core, "closed", lambda ftp: None)
    result = manager.remove({}, LEVEL_ID)
    assert result["ok"] and not result["cleanup_pending"]
    assert PATH not in f.files
    assert app.BASE + "/" + other + "-chroma.bin" in f.files


def test_remove_without_a_sidecar_is_unchanged(monkeypatch):
    songs = [{"id": LEVEL_ID}, {"id": "bsd_additive_" + "1" * 24}]
    f = FTP({p: b"x" for p in manager.paths(songs[0])})
    f.rmd = lambda path: None
    monkeypatch.setattr(manager, "connection", lambda config, write=False: contextlib.nullcontext(f))
    monkeypatch.setattr(manager.core, "load_library", lambda ftp: (list(songs), b"old"))
    monkeypatch.setattr(manager.core, "commit_library", lambda *a: None)
    monkeypatch.setattr(manager.core, "closed", lambda ftp: None)
    assert not manager.remove({}, LEVEL_ID)["cleanup_pending"]


def install_body():
    return (ROOT / "tools/additive_companion.py").read_text(encoding="utf8").split("def install(", 1)[1]


def test_install_wiring_publishes_before_library_and_undoes_on_failure():
    body = install_body()
    # Chroma extraction is requested only for a plugin that declared the marker.
    assert 'cap.get("chroma") in (1, 2, 3, 4, 5, 6)' in body and '{"chroma": cap["chroma"]}' in body
    assert body.count("additive_chroma.publish(") == 2
    new_song = body.index("chroma_created = additive_chroma.publish(")
    assert body.index("score_source.publish(ftp", body.index("metadata = source_metadata")) < new_song
    assert new_song < body.index("commit_library(ftp, candidate")
    failure = body.index("additive_chroma.discard(")
    assert body.index("commit_library(ftp, candidate") < failure < body.index("raise", failure)
    assert "if chroma_created:" in body


def test_plugin_deployment_records_the_marker_in_the_capability_file():
    text = (ROOT / "webapp/library_manager.py").read_text(encoding="utf8")
    assert '"chroma": 6 if b"ORB_CHROMA_V6" in canonical else 5 if b"ORB_CHROMA_V5" in canonical else 4 if b"ORB_CHROMA_V4" in canonical else 3 if b"ORB_CHROMA_V3" in canonical else 2 if b"ORB_CHROMA_V2" in canonical else 1 if b"ORB_CHROMA_V1" in canonical else 0' in text


def test_desktop_bundle_ships_the_module():
    root = ROOT.parent / "desktop"
    resources = (root / "backend/resources.py").read_text(encoding="utf8")
    for name in ("additive_chroma.py", "additive_chroma_regex.py", "chroma_light_id_tables.json"):
        assert f'"tools/{name}"' in resources
    freeze = (root / "freeze_backend.py").read_text(encoding="utf8")
    assert '"additive_chroma"' in freeze and '"additive_chroma_regex"' in freeze


def test_search_label_lifts_chroma_only_for_a_capable_plugin():
    import json
    import shutil
    import subprocess

    node = shutil.which("node")
    if not node:
        pytest.skip("Node required for executable UI checks")
    source = (ROOT / "webapp/static/companion.js").read_text(encoding="utf-8")
    body = source[source.index("function compatibility(map)"):source.index("function compatibilityBadge(")]
    program = r'''
const vm=require('vm');
for (const capable of [false, true]) {
  const ctx=vm.createContext({});
  vm.runInContext("let chromaCapable=" + capable + ";" + JSON.parse(process.argv[1]), ctx);
  const out=vm.runInContext(`
const chart={characteristic:'Standard',difficulty:'Expert',ne:false,me:false,chroma:false,cinema:false};
const map=(diffs)=>({versions:[{diffs}]});
JSON.stringify([compatibility(map([{...chart,chroma:true}])).status,
                compatibility(map([{...chart,chroma:true,ne:true}])).status,
                compatibility(map([{...chart,chroma:true,vivify:true}])).status,
                compatibility(map([chart])).status])`, ctx);
  console.log(capable + " " + out);
}
'''
    result = subprocess.run([node, "-e", program, json.dumps(body)], capture_output=True, check=True, encoding="utf-8")
    lines = result.stdout.split()
    # Without ORB_CHROMA_V1 the flag still refuses; with it the archive decides ("unknown").
    # Noodle and Vivify keep refusing either way; a vanilla chart is unaffected.
    assert lines == ['false', '["unsupported","unsupported","unsupported","likely"]',
                     'true', '["unknown","unsupported","unsupported","likely"]']
