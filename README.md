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

## Requirements

- SKSE, Skyrim SE 1.5.97 / AE 1.6.x
- OStim Standalone 7.5 or newer
- Keep OStim's **"Disable Scaling"** on (this mod is for keeping the height difference).

## Settings

`SKSE/Plugins/OStimAnchorAlign.ini` is re-read on every scene change, so you can edit it in game.
The log is `Documents/My Games/Skyrim Special Edition/SKSE/OStimAnchorAlign.log`.

## Limits

- It only translates the whole body. Arm or leg length differences (hand positions) aren't changed.
- With several conflicting rules the result is a weighted compromise.
- If you already hand-aligned a scene for a given height combination in OStim's alignment menu,
  the corrections add up. Reset that alignment, or lower `fStrength`.

## License

GPL-3.0-or-later. Contains OStim Standalone's plugin interface headers (GPL-3.0, VersuchDrei and contributors).
