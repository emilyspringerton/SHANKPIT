# TYLER — VALHANNA VH04: "Les Chaises Pliantes"

**Mode:** MODE_TYLER, local lobby play order (VH01 -> E03 -> FB01 -> VH02 -> VH03 -> VH04 -> CONSTRUCT)
**Source script:** `TYLER/episodes/vh04_valhanna_les_chaises_pliantes.md` (TYLER Build 0142)
**Level:** `assets/tyler_levels/tyler_vh04_chaises_pliantes.json` (same room as VH02, renamed `TYLER_VH04_CHAISES_PLIANTES`; adds six folding-chair boxes (0.63 high, the seat height Cam measures), a seventh set apart, and one stockpot, all outside the scripted paths)
**Coordinator:** `packages/simulation/tyler_vh04_coldopen.{h,c}` (mirrors VH02/E03/FB01)
**VO source of truth:** `TYLER/tts/scenes/vh04.tsv` (`render_scenes.py` drift-checks every subtitle below)

## Actor mapping

VH04 has no Tyler or Hana. The two scripted slots are CAM (`TYLER_ACTOR_TYLER`, voice
`fr_FR-siwis`) and THE AUDITOR (`TYLER_ACTOR_HANA`, voice `fr_FR-tom`). The PA, the sound guy, the Duck
and the Producer are subtitle-only. The Auditor is unnamed on principle (title only): no line in this
scene gives a name, and none should be added.

## Beat table

| Beat | Actor | Marker | Hold (ms) | Subtitle (French + [EN:] gloss, as committed) |
|---|---|---|---|---|
| 0 | NONE | (0,0,0) | 3000 | (Une chaise pliante au pied du lit. Le Canard : Ce n'est pas mon tour.) [EN: (A folding chair at the foot of the bed. The Duck: It isn't my turn.)] |
| 1 | HANA | (0,1,9) | 6000 | Bonjour ! Ne vous levez pas. / J'apporte toujours une chaise de plus que la liste. [EN: Good morning! Don't get up. / I always bring one more chair than the list.] |
| 2 | BOTH | (0,1,5) | 5500 | Six. Il y en a sept. Il y a toujours quelqu'un qui arrive. / Soixante-trois. [EN: Six. There are seven. Somebody always turns up. / Sixty-three. (Cam, measuring a chair)] |
| 3 | HANA | (3,1,-3) | 6000 | Vingt-huit virgule sept. Pas un bruit. Un reste. Il faut deux corps dans la piece. [EN: Twenty-eight point seven. Not a noise. A remainder. It takes two bodies in the room.] |
| 4 | HANA | (0,1,4) | 4500 | C'est ma recette. Neuf sur dix. Quelqu'un a ajoute le dixieme. [EN: That's my recipe. Nine of ten. Someone added the tenth.] |
| 5 | HANA | (1,1,2) | 2500 | Vous avez une chaise aussi. / (La radio se tait.) [EN: You have a chair too. / (The radio goes quiet.)] |
| 6 | BOTH | (-2,1,1) | 6500 | Je ne copie pas ce que vous avez scelle. / Supprimer, c'est voler. Sceller, c'est choisir. [EN: I don't mirror what you sealed. / To suppress is to steal. To seal is to choose.] |
| 7 | TYLER | (4,1,-2.6) | 3500 *(fires exit)* | Il me la faut jeudi. Alors ce n'est pas du. / RENCONTRE : l'equipe rencontre un inspecteur [EN: I need it Thursday. Then it isn't due. / ENCOUNTER: THE CREW MEETS AN INSPECTOR] |

## Skins

VH04 Cam=WANDERER; the Auditor=BILL (a teal chest panel reads as the badge on his lanyard). The enamel circuit-flower pin has no model.

## Known gaps (named)

- **VO is build-time only.** `TYLER/tts/render_scenes.py` (Piper stopgap) generates it in CI; a local
  build without it is silent. Subtitles are French + `[EN:]` and committed. Holds above are the
  authored minimum; `on_tyler_voice_stretched_hold_ms` stretches a beat to fit its generated clip.
- **Lobby-local only.** Like E03/FB01/VH02, not wired into `apps/server` / `--tyler`
  (no PACKET_TYLER_BEAT).
- **The soup is not a prop.** The stockpot is a metal box; the steam that bends toward the seventh chair
  exists only in the text and the subtitle.
- **Seven chairs, one unoccupied by design.** The seventh chair (0,0.315,-6.8) is deliberately never
  addressed by a beat; the script leaves who it is for unanswered and so does this scene.
- **Exit is the shared button mechanism**: the last beat "presses" the exit button, so the closing
  card cuts to the next scene rather than to an on-screen reveal.
- **Headless-verified only**: `make test-tyler-vo` runs the scene to its exit and checks that every
  beat is French with an `[EN:]` gloss and fits the subtitle buffer. The lobby binary was NOT built
  or run (no SDL2 headers in this sandbox; `make lobby` also needs `../EDITOR.GAME/gen`).
