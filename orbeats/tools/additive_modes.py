"""Stable internal keys retain Standard compatibility and explicit native modes."""

MODES = ("Standard", "OneSaber", "NoArrows", "90Degree", "360Degree")
DIFFS = ("Easy", "Normal", "Hard", "Expert", "ExpertPlus")
# SongCore characteristics the game has no mode for. Their charts are left out
# (reported, never silently), the rest of the pack is installed. Lawless is
# playable vanilla content and is a deferred feature (its own library entry),
# not a permanent decision; the opt-in vanilla subset still renames it.
SKIPPED = {
    "Lightshow": "light show without notes",
    "Legacy": "older version of the map",
    "Lawless": "Lawless set, not supported yet",
}


def skipped(mode, difficulty):
    """Report line for a left-out chart, or None when the characteristic is installed."""
    reason = SKIPPED.get(mode)
    return None if reason is None else f"{mode}/{difficulty} ({reason})"


def key(mode, difficulty):
    if mode not in MODES or difficulty not in DIFFS:
        raise ValueError("unsupported vanilla characteristic/difficulty")
    return difficulty if mode == "Standard" else mode + "/" + difficulty


def parts(value):
    items = value.split("/")
    mode, difficulty = ("Standard", items[0]) if len(items) == 1 else items
    key(mode, difficulty)
    return mode, difficulty


def order(value):
    mode, difficulty = parts(value)
    return MODES.index(mode), DIFFS.index(difficulty)
