# PS4 map-feature test matrix

User confirms0.8077 restores Custom Songs and Here Come the Cats plays correctly.
This closes the registration regression; it does not validate every feature below.
Do not silently replace or remove existing entries to create a test fixture.

| Case | Existing evidence | Next hardware observation |
| --- | --- | --- |
| Pack registration |0.8076 failed; all27entries retained.0.8077 user-confirmed pack recovery with typed-array guard | Recovery confirmed; exact counts not separately reported by user |
| Here Come the Cats555fe | Remote bundle retains787notes,18V4arcs,2675basic events and6authored colours; user now reports correct playback on0.8077 | Playback confirmed; exact palette-reference parity, player overrides and Practice remain unconfirmed |
| Looping the Rooms4f7e2 | User confirmed correct gameplay and visible background lights;780colour groups,714rotation groups,8boosts retained offline | Regression after recovery; mid-song Practice restart; distinguish visible lighting from exact authored parity |
| Bloom | User reported requested Bloom/Looping test worked; inspected Bloom candidates have no arcs/chains/NJS | Basic V4-source/audio regression only; never count it as arcs/chains/dynamic-NJS proof |
| V3/V4 chains | Schema, reference, count and reopened-bundle fixtures pass | Need a verified real source with chains; slice/tail timing, scoring and Practice checks |
| Dynamic NJS | Native loader/schema and local timing fixtures | Need a verified real V4 source with NJS events; timing/distance feel and Practice seek |
| Translation/FX/variable BPM event boxes | Preservation and exact schema/seconds fixtures | Need representative real-map/reference comparisons for these distinct effects |
| OneSaber/NoArrows/mixed modes | Genuine characteristic resolution and atomic refusal tested with managed fixtures | Mode selector, actual chart selection, counters and gameplay |
|90/360rotation | Native V3 preservation and bounded V2 enum conversion fixtures | Headset/play-area alignment, obstacle orientation, same-beat early/late ordering; user controls safe play space |
| Offsets/LUFS/separate preview | Wire/bundle/resampling and no-stock-write fixtures | Preview versus song audio, timing and loudness, player overrides, Practice seek |
| Vanilla part of a mod pack (475ef, Lawless as Standard) | Host only: subset selection and strict import of five charts pass with the developer length override | Install, mode/difficulty list, playback; no ScoreSaber panel data expected |
| Overbright light brightness (>100, up to452.5 in 475ef) | Schema bound raised to1000; host fixtures | Visual result: bloom/white-out versus PC reference |
| One-link chains (`sc`=1) | Passed through unchanged; host fixtures | No crash on spawn; appearance and scoring of the head note |
| Audio longer than600s | Host limit is one constant; native reader accepts1800s/200MB; generator for8–15min songs | Staged experiment in `docs/plans/2026-11-12-modded-maps-and-long-audio.md` section6: load, pause, Practice seek, full play, second start |
| Stock regressions | Repository preservation and palette no-stock-write tests | One stock song before/after a custom song; controls/UI/audio/colours unchanged |

Known unimplemented cases remain explicit: runtime mods/custom Unity environments,
independent per-difficulty environments, conflicting palettes across same-rank
characteristics, unsupported future V4 fields/waypoints and incompatible per-chart
BPM timelines. Finite capacity/audio/manifest/object bounds still apply. Native
API fixtures are not PS4 playback proof. No additional song is recommended solely
from BeatSaver labels; acquire/validate its actual files first.
