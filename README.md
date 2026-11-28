# OStimAnchorAlign

An SKSE add-on for **OStim Standalone** (7.5+) that corrects scene alignment when the actors
are taller or shorter than the animation's reference height, **without** rescaling them.

What gets aligned depends on what is happening in the current stage, taken from OStim's
action metadata:

| Action | Kept together |
|---|---|
| vaginal / anal sex | penis ↔ vagina / pelvis |
| kissing | face ↔ face |
| blowjob, cunnilingus, rimjob | face ↔ groin |
| boobjob, buttjob, thighjob, handjob, footjob | the body part ↔ penis |
| … | see `SKSE/Plugins/OStimAnchorAlign/anchors.json` |

## How it works

- For each rule, it measures where the two anchor bones are, then works out where they would be
  if both actors had height 1.0 (the height animators build for). The difference comes only from
  the height mismatch, so the animator's intended poses, distances and gaps are kept.
- If a stage has several actions (3P, kiss + sex …), one offset per actor is solved by weighted
  least squares.
- The offset goes onto the skeleton's `NPC` node as a visual translation. The actor reference isn't
  moved, so it doesn't fight OStim's position lock. Offsets written by other mods (e.g. NiOverride
  heels) are kept, and the correction is added on top.
- High-heel lift counts as part of the height difference (`bHeelAware`). Many standing scenes make
  OStim remove the heel lift, which sinks the shoes into the floor; while an actor stands on its
  feet, the lift is given back (`bRestoreHeels`).
- The default (`iMode=1`) samples the correction over the first 1.5 s of every stage and then holds
  it. `iMode=0` follows the correction every frame.
- Actors standing, kneeling or lying on the floor are never moved vertically. When a taller standing
  actor has to come down, the knees bend while the feet stay planted (leg IK, `[LegIK]`).
- In lift/carry scenes, only the lifted actor is moved vertically.
- A kneeling actor (incl. sitting on heels) that is too low raises the hips by straightening the thighs;
  knees and feet stay planted. Actors lying on the floor are never moved vertically.
- **Arm IK** (`[ArmIK]`): touching hands (groping breasts/butt, stroking the head, holding, fingering,
  handjob, ...) are bent so the palm reaches the partner's body part the way it would at reference
  height. The hand keeps its animated angle; the elbow swivels and the upper body leans a little to
  keep the wrist natural. Hand rules are in `anchors.json` (`handTargets`, `handActions`).

## Requirements

- Skyrim SE 1.5.97 / AE 1.6.x
- SKSE64
- Address Library for SKSE Plugins
- OStim Standalone 7.5 or newer
- XPMSE or a compatible skeleton (standard bone names)
- Optional: SKSE Menu Framework, for the in-game settings page
- Optional, for creature scenes: OCreatures, with creature skeletons that have genital bones (e.g. BakaFactory Animated Beasts Cocks / MNC)
- Keep OStim's **"Disable Scaling"** on (this mod is for keeping the height difference).

## Settings

**In-game menu (optional):** with [SKSE Menu Framework](https://www.nexusmods.com/skyrimspecialedition/mods/120352) installed, open its Mod Control Panel → *OStim Anchor Align*. Every option can be changed live, the pause hotkey can be bound by pressing it, and the menu language can be English or Japanese. Changes are saved to the INI.

**Pause hotkey:** default **Left Shift + Pause** pauses/resumes all corrections. While paused (or when there is nothing to correct) the skeleton is left completely untouched, e.g. for posing with other tools.

**Hands on breasts:** `fBreastUp` / `fBreastForward` under `[ArmIK]` move the hands up/down and forward/back (default 3 up). Raise them for bodies with bigger breasts than the animation expects.

**Creatures:** with `[Creatures] bEnabled = 1` (default) scenes with creatures are aligned too. The creature is never moved or bent; only the human actors adapt to it (body, legs, hands). A creature counts as the size its animations were made for (`iHeightMode = 0`); `iHeightMode = 1` uses its measured skeleton scale instead. Creature bone names can be extended in `anchors.json` (`creatureBones`, `creatureHandTargets`).

**Test scene:** the menu's *Test scene* section spawns a peaceful creature in front of you and starts a scene with it (or with the actor selected in the console), and removes it afterwards. Useful for checking creature scenes without finding a partner.

`SKSE/Plugins/OStimAnchorAlign.ini` is re-read on every scene change, so you can edit it in game.
The log is `Documents/My Games/Skyrim Special Edition/SKSE/OStimAnchorAlign.log`.

## Troubleshooting

- When reporting a problem, include the log and the scene/stage name. Set `bVerboseLog = 1` under `[Debug]` for a detailed log.
- Since 0.5.0 the per-frame work runs in the game's main loop. If that causes problems on your game version, `iUpdateHook = 0` under `[Debug]` switches back to the old method (0.4.0 and earlier). The plugin also falls back to it on its own if the main-loop hook can't be installed.

## Compatibility

- **PPA - Procedural Penis Animations**: tested and recommended. Anchor Align brings the hips together, and PPA does the final penis bending.
- **GT-Soft Body / CBPC / SMP physics**: tested. Physics follows the corrected pose.
- **Fabulous Poser** and other skeleton editors: since 0.5.0 everything runs on the main thread in a fixed order, which should stop jitter and snapping back (untested with Fabulous Poser). The pause hotkey turns all corrections off.
- **OStim Alignment Heel Fix**: not needed since 0.5.0. Running both would probably correct heels twice; if you keep it, set `bHeelAware = 0` and `bRestoreHeels = 0`.
- **SexLab**: not supported. The mod is built on OStim's per-stage action data, which SexLab doesn't have.
- Tested on AE 1.6.1170, XPMSE, OStim Standalone 7.5.1b. Not tested: SE 1.5.97, Skyrim 1.7.x, skeletons other than XPMSE. Creature scenes were tested with wolf, horse and Falmer (OCreatures 1.4, BakaFactory skeletons); others are untested. VR is not supported.

## Limits

- Arms that can't reach stop fully extended. Fingers keep their animated shape.
- With several conflicting rules the result is a weighted compromise.
- If you already hand-aligned a scene for a given height combination in OStim's alignment menu,
  the corrections add up. Reset that alignment, or lower `fStrength`.
- Height is taken from the skeleton's overall scale. Proportion changes (e.g. longer legs at the same
  overall scale via Mu Skeleton Editor) aren't counted as a height difference, so some offset may remain.

## Changelog

- **0.6.0**: creature scene support (the creature stays put, humans adapt; built-in bone names for vanilla creature skeletons and BakaFactory / MNC genital bones); test-scene tools in the menu
- **0.5.0**: all corrections run on the main thread (fixes jitter with CBPC/SMP physics and other skeleton editors); high-heel lift counts as part of the height difference; heels no longer sink into the floor in standing scenes; adjustable hand position on breasts
- **0.4.0**: optional in-game settings page (SKSE Menu Framework, English/Japanese); pause hotkey; the skeleton is left untouched when there is nothing to correct
- **0.3.0**: arm IK, hip raise for kneeling actors, lying-pose detection
- **0.2.0**: first release

## License

GPL-3.0-or-later. Contains OStim Standalone's plugin interface headers (GPL-3.0, VersuchDrei and contributors) and the SKSE Menu Framework header (MIT). See THIRD_PARTY_NOTICES.txt.
