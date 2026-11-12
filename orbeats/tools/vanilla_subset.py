"""Opt-in extraction of the vanilla-playable subset of an InfoV2/InfoV4 mod-assisted pack.

Some maps ship several chart sets: mod-dependent ones (requirements such as
Vivify) plus a "Lawless" set that has identical notes without the mods. The strict
importer refuses the whole pack. This helper writes a *new* source folder that
keeps only difficulties whose declared requirements are empty, so the unchanged
strict importer validates what remains. Nothing is silently stripped from a kept
chart: chart files are copied byte for byte and only descriptive Info metadata is
removed. Every dropped/renamed/stripped item is returned in the report.

Never the default: callers must opt in, and must not publish a leaderboard
identity when ``score_identity`` is False (a renamed Lawless set would otherwise
borrow the mod chart's Standard leaderboard).
"""

from __future__ import annotations

import json
import shutil
from pathlib import Path

from additive_modes import DIFFS, MODES

# Keys that never change gameplay on their own. Requirements and anything else
# unknown are deliberately NOT here and still reach the strict importer.
# InfoV4 custom data has no fixed spelling, so both spellings are recognised.
INFO_COSMETIC = frozenset({"_assetBundle", "assetBundle"})
DIFF_COSMETIC = frozenset(
    {"_information", "_warnings", "_suggestions", "_settings", "information", "warnings", "suggestions", "settings"}
)
REQUIREMENT_KEYS = ("_requirements", "requirements")
METADATA = "beatsaver-metadata.json"


def _requirements(custom) -> list[str] | None:
    """Declared requirements; None when the declaration itself is malformed."""
    if custom is None:
        return []
    if not isinstance(custom, dict):
        return None
    found: list[str] = []
    for key in REQUIREMENT_KEYS:
        value = custom.get(key, [])
        if not isinstance(value, list) or any(not isinstance(item, str) for item in value):
            return None
        found += value
    return found


def _entries(info: dict, v2: bool) -> list[dict]:
    """Flat, ordered view of every difficulty with a handle back to its Info record."""
    if v2:
        return [
            {"mode": group.get("_beatmapCharacteristicName"), "diff": diff.get("_difficulty"), "record": diff,
             "custom": diff.get("_customData"), "files": [diff.get("_beatmapFilename")]}
            for group in info.get("_difficultyBeatmapSets", [])
            for diff in group.get("_difficultyBeatmaps", [])
        ]
    return [
        {"mode": diff.get("characteristic"), "diff": diff.get("difficulty"), "record": diff,
         "custom": diff.get("customData"), "files": [diff.get("beatmapDataFilename"), diff.get("lightshowDataFilename")]}
        for diff in info.get("difficultyBeatmaps", [])
    ]


def apply(source: Path, dest: Path) -> dict:
    """Write the vanilla subset of ``source`` into new folder ``dest``.

    Returns a report with ``kept``, ``dropped``, ``renamed``, ``stripped``,
    ``changed`` (False when the pack was already fully vanilla) and
    ``score_identity`` (False when charts were renamed). Raises ValueError when
    nothing playable remains or the Info format is unsupported.
    """
    source, dest = Path(source).resolve(), Path(dest).resolve()
    if dest.exists():
        raise ValueError("vanilla subset destination must be new")
    infos = [p for p in source.iterdir() if p.name.lower() == "info.dat"]
    if len(infos) != 1:
        raise ValueError("one Info.dat required")
    info = json.loads(infos[0].read_text(encoding="utf-8-sig"))
    if not isinstance(info, dict):
        raise ValueError("invalid Info.dat")
    v2 = str(info.get("_version", "")).startswith("2.")
    if not v2 and info.get("version") not in ("4.0.0", "4.0.1"):
        raise ValueError("Unsupported Info format: " + str(info.get("version", info.get("_version", "missing version"))))

    report: dict = {"dropped": [], "renamed": [], "stripped": []}
    kept = []
    for entry in _entries(info, v2):
        label = f"{entry['mode']}/{entry['diff']}"
        reqs = _requirements(entry["custom"])
        if reqs is None:
            report["dropped"].append(f"{label}: malformed requirements")
        elif reqs:
            report["dropped"].append(f"{label}: requires {', '.join(reqs)}")
        elif entry["mode"] != "Lawless" and entry["mode"] not in MODES:
            report["dropped"].append(f"{label}: unsupported characteristic")
        elif entry["diff"] not in DIFFS:
            report["dropped"].append(f"{label}: unknown difficulty")
        else:
            kept.append(entry)

    # Lawless is the author's mod-free copy; use it as Standard only when no
    # real Standard set survived, never merged over an existing Standard.
    lawless = [e for e in kept if e["mode"] == "Lawless"]
    if lawless and any(e["mode"] == "Standard" for e in kept):
        report["dropped"] += [f"Lawless/{e['diff']}: real Standard set is kept" for e in lawless]
        kept = [e for e in kept if e["mode"] != "Lawless"]
    elif lawless:
        for entry in lawless:
            entry["mode"] = "Standard"
        report["renamed"].append("Lawless -> Standard")
    if not kept:
        raise ValueError("No vanilla-playable difficulties remain after removing mod requirements")

    for entry in kept:
        if isinstance(entry["custom"], dict):
            for key in sorted(DIFF_COSMETIC & entry["custom"].keys()):
                del entry["custom"][key]
                report["stripped"].append(f"{entry['mode']}/{entry['diff']}: {key}")
    custom_key = "_customData" if v2 else "customData"
    if isinstance(info.get(custom_key), dict):
        for key in sorted(INFO_COSMETIC & info[custom_key].keys()):
            del info[custom_key][key]
            report["stripped"].append("Info: " + key)

    cover = info.get("_coverImageFilename" if v2 else "coverImageFilename")
    optional = (cover, METADATA)
    if v2:
        records = {id(e["record"]) for e in kept}
        groups = []
        for group in info.get("_difficultyBeatmapSets", []):
            diffs = [d for d in group.get("_difficultyBeatmaps", []) if id(d) in records]
            if diffs:
                mode = group.get("_beatmapCharacteristicName")
                groups.append(dict(group, _beatmapCharacteristicName="Standard" if mode == "Lawless" else mode,
                                   _difficultyBeatmaps=diffs))
        info["_difficultyBeatmapSets"] = groups
        names = [info.get("_songFilename"), cover]
    else:
        for entry in kept:
            entry["record"]["characteristic"] = entry["mode"]
        info["difficultyBeatmaps"] = [e["record"] for e in kept]
        audio = info.get("audio") if isinstance(info.get("audio"), dict) else {}
        names = [audio.get("songFilename"), audio.get("audioDataFilename"), cover, info.get("songPreviewFilename")]
    names += [name for e in kept for name in e["files"]] + [METADATA]

    dest.mkdir(parents=True)
    for name in sorted({n for n in names if isinstance(n, str) and n}):
        src = (source / name).resolve()
        if not src.is_relative_to(source) or not src.is_file():
            if name in optional:
                continue  # cover is validated by the strict importer if used
            raise ValueError("referenced file missing or escapes source: " + name)
        target = dest / name
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(src, target)
    (dest / "Info.dat").write_text(json.dumps(info), encoding="utf-8")
    report["kept"] = [f"{e['mode']}/{e['diff']}" for e in kept]
    report["changed"] = bool(report["dropped"] or report["renamed"] or report["stripped"])
    report["score_identity"] = not report["renamed"]
    return report
