# TYLER — VALHANNA VH03: "Jour Hors du Temps"

**Mode:** MODE_TYLER, local lobby play order (VH01 -> E03 -> FB01 -> VH02 -> VH03 -> VH04 -> CONSTRUCT)
**Source script:** `TYLER/episodes/vh03_valhanna_day_out_of_time.md` (TYLER Build 0141)
**Level:** `assets/tyler_levels/tyler_vh03_day_out_of_time.json` (same room as VH02, renamed `TYLER_VH03_DAY_OUT_OF_TIME`; geometry unchanged)
**Coordinator:** `packages/simulation/tyler_vh03_coldopen.{h,c}` (mirrors VH02/E03/FB01)
**VO source of truth:** `TYLER/tts/scenes/vh03.tsv` (`render_scenes.py` drift-checks every subtitle below)

## Actor mapping

VH03 has no Tyler or Hana. The two scripted slots are the crew: `TYLER_ACTOR_TYLER` = CAM (voice
`fr_FR-siwis`), `TYLER_ACTOR_HANA` = THE SOUND GUY (voice `fr_FR-tom`). The PA's arithmetic and the
Producer's radio line are spoken in the sound guy's slot (one body, one voice -- a named stopgap). The
Duck's bedroom is a subtitle-only beat. The slots render with the level's two existing story-ally kits.

## Beat table

| Beat | Actor | Marker | Hold (ms) | Subtitle (French + [EN:] gloss, as committed) |
|---|---|---|---|---|
| 0 | NONE | (0,0,0) | 3000 | (Le Canard compte treize pages. Ce n'est pas comme ca que le mois passe.) [EN: (The Duck counts thirteen pages. That's not how the month goes.)] |
| 1 | BOTH | (0,1,9) | 5000 | L'horloge a perdu la date. / Elle ne l'a pas perdue. Il n'y en a pas. [EN: The clock lost the date. / It didn't lose it. There isn't one.] |
| 2 | HANA | (3,1,-3) | 5500 | Douze mois. Trente et un, vingt-huit, trente et un... trois cent soixante-cinq. [EN: Twelve months. Thirty-one, twenty-eight, thirty-one... three hundred sixty-five.] |
| 3 | HANA | (3,1,-3) | 5000 | Treize fois vingt-huit. Trois cent soixante-quatre. Il en reste un. [EN: Thirteen of twenty-eight. Three hundred sixty-four. There's one left over.] |
| 4 | BOTH | (3,1,-3) | 4000 | Et les annees bissextiles ? / Alors il y en a deux. [EN: And in a leap year? / Then there are two.] |
| 5 | HANA | (4,1,-4.5) | 6500 | Pas de repos dans une production. Continuez a tourner. / Il n'a rien prevu pour aujourd'hui. [EN: Producer (radio): No rest in a production. Keep rolling. / He has no plan for today. (DAY 365 --)] |
| 6 | BOTH | (0,1,2) | 4500 | Je capte vingt-huit virgule sept. / C'est presque vingt-huit. [EN: I'm getting twenty-eight point seven. / That's almost twenty-eight.] |
| 7 | TYLER | (4,1,-2.6) | 3000 *(fires exit)* | Aujourd'hui n'est pas un jour de la semaine. RENCONTRE : [non consignee] [EN: TODAY IS NOT A DAY OF THE WEEK. ENCOUNTER: [UNLOGGED]] |

## Skins

VH03 Cam=WANDERER, sound guy=VIKING (the same bodies as VH02: it is the same crew on the day with no date).

## Known gaps (named)

- **VO is build-time only.** `TYLER/tts/render_scenes.py` (Piper stopgap) generates it in CI; a local
  build without it is silent. Subtitles are French + `[EN:]` and committed. Holds above are the
  authored minimum; `on_tyler_voice_stretched_hold_ms` stretches a beat to fit its generated clip.
- **Lobby-local only.** Like E03/FB01/VH02, not wired into `apps/server` / `--tyler`
  (no PACKET_TYLER_BEAT).
- **The Duck's bedroom is not a level.** Beat 0 is a subtitle over the Iceland room; the wall calendar
  (thirteen pages, twenty-eight squares) exists only in the text.
- **Exit is the shared button mechanism**: the last beat "presses" the exit button, so the closing
  card cuts to the next scene rather than to an on-screen reveal.
- **Headless-verified only**: `make test-tyler-vo` runs the scene to its exit and checks that every
  beat is French with an `[EN:]` gloss and fits the subtitle buffer. The lobby binary was NOT built
  or run (no SDL2 headers in this sandbox; `make lobby` also needs `../EDITOR.GAME/gen`).
