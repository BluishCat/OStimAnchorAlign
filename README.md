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
  moved, so it doesn't fight OStim's position lock. Offsets written by other mods (NiOverride heels,
  OStim's heel removal) are kept and stacked with it.
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

- SKSE, Skyrim SE 1.5.97 / AE 1.6.x
- OStim Standalone 7.5 or newer
- Keep OStim's **"Disable Scaling"** on (this mod is for keeping the height difference).

## Settings

**In-game menu (optional):** with [SKSE Menu Framework](https://www.nexusmods.com/skyrimspecialedition/mods/120352) installed, open its Mod Control Panel → *OStim Anchor Align*. Every option can be changed live, the pause hotkey can be bound by pressing it, and the menu language can be English or Japanese. Changes are saved to the INI.

**Pause hotkey:** default **Left Shift + Pause** pauses/resumes all corrections. While paused (or when there is nothing to correct) the skeleton is left completely untouched, e.g. for posing with other tools.


`SKSE/Plugins/OStimAnchorAlign.ini` is re-read on every scene change, so you can edit it in game.
The log is `Documents/My Games/Skyrim Special Edition/SKSE/OStimAnchorAlign.log`.

## Compatibility

- **PPA - Procedural Penis Animations**: tested and recommended. Anchor Align brings the hips together, and PPA does the final penis bending.
- **Fabulous Poser** and other live skeleton editors: press the pause hotkey while posing.

## Limits

- Arms that can't reach stop fully extended. Fingers keep their animated shape.
- With several conflicting rules the result is a weighted compromise.
- If you already hand-aligned a scene for a given height combination in OStim's alignment menu,
  the corrections add up. Reset that alignment, or lower `fStrength`.

## License

GPL-3.0-or-later. Contains OStim Standalone's plugin interface headers (GPL-3.0, VersuchDrei and contributors) and the SKSE Menu Framework header (MIT). See THIRD_PARTY_NOTICES.txt.
