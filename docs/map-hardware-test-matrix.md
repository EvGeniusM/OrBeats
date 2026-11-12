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
| Chroma 3.0 diagnostic (`chroma-diagnostic` flag) | User ran 0.8080 on PS4 2026-11-12: fingerprints matched, no crash over three full songs ($100 Bills, Heavy Is the Crown 40804, SUPADOPA 4033c). Log: strobe filter kept 200 of 785 events and renumbered them; all live events exist in the chart by type and time. 188 of 788 light registrations not yet found in the manager list (group -1) | Light-ID order for 3b needs a second look at registration timing |
| Chroma light-event colours | User-observed on 0.8082 (2026-11-12): authored background colours visible on 4033c and 40804 (multicolour flashes); events without a Chroma colour keep the environment colours | Fades between coloured and plain events; comparison with "All effects"; exact PC parity not checked |
| Chroma note/arc/wall colours | User-observed on 0.8083 (2026-11-12): 55a43 rainbow arc fans show one Chroma colour per arc (same head, file order) and the two side walls (x = -1/4) show the Chroma light green over the map palette | A map with Chroma-coloured notes (cubes); chain links and bombs keep game colours |
| Chroma after a plain/OST map | Not verified on PS4. Session reset unit test | No colour leaks into the next level |
| Chroma rings and lasers (level 2, 0.8084–0.8085) | User ran 35766 Shivers on 0.8084 (2026-11-12): all five ring/laser fingerprints matched, session loaded 98 ring events, no layout surprise or crash; user saw "rings actively spinning and twisting". On 0.8085 (2026-11-12) the per-level counter confirmed it: `ring rotation 196` applied (98 events × two ring spawners), 0 name-filtered, 0 layout fallbacks; first record turned the game step 2.1 into the map's −60 with prop 1 and flexy. Control run of the vanilla Battle!! (Torna) on the same build: no sidecar, no Chroma session, no crash. Colours on 4033c and 55a43 still loaded after the update. Lasers: user played 1a4af "Can We Be Free" (DragonsEnvironment, Expert, 3749 Chroma events) on 0.8085 to the end without a crash; the log shows the single-laser hook setting speed 200 from the map, the paired-laser hook turning the game's random direction into the map's direction 1 at speed 200, and ring rotation on Dragons (angle −1665 → −1485). The per-level total was not flushed because the game was closed right after the song | Per-level laser totals (play a second song after 1a4af); visual comparison with a PC video; ring zoom (`step`/`speed` on type 9); `nameFilter`/`counterSpin` on hardware |
| Stock regressions | Repository preservation and palette no-stock-write tests | One stock song before/after a custom song; controls/UI/audio/colours unchanged |

Known unimplemented cases remain explicit: runtime mods/custom Unity environments,
independent per-difficulty environments, conflicting palettes across same-rank
characteristics, unsupported future V4 fields/waypoints and incompatible per-chart
BPM timelines. Finite capacity/audio/manifest/object bounds still apply. Native
API fixtures are not PS4 playback proof. No additional song is recommended solely
from BeatSaver labels; acquire/validate its actual files first.
