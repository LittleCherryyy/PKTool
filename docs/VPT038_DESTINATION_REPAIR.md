# VPT038.1 — Destination legality repair and meaningful diagnostics

**Baseline:** VPT038 `dev/vpt038-repair-for-transfer` (source repair, consent UI, reversible journal).
**Isolated candidate:** `dev/vpt038-destination-repair`.

## Reason for this update

A real DSi screenshot showed **DON** (a Gen II Silver source being transferred to Emerald) with:

- `BLOCKED`
- `Destination legality cannot be confirmed`
- `Hatched from an egg` on the details line
- `0 ready / 1 blocked`

In `checkGen3`, **`Hatched from an egg` is a Valid informational finding**, not the cause of rejection. The VPT038 implementation only offered repair for `Source legality cannot be confirmed`, so no Y action was available.

Without DON's raw save, the *specific* invalid condition (for example an incompatible Gen III move or a provenance/encounter rule) cannot be determined from this screenshot alone.

## New behavior

- Invalid destination findings are displayed **first and marked `ERROR:`**; warnings and ordinary information follow. On a blocked transfer, the top panel shows the first actual reason, not the misleading informational egg note.
- In Transfer Review, **LEFT / RIGHT** cycles the detailed findings for the selected Pokémon.
- When an independent candidate is legal under **both Gen III legality and full original-to-target identity rules**, the same **Y = Repair for Transfer** control appears.
- The repair preview lists any proposed changes, including a Gen III met-data adjustment, PID re-roll preserving traits, Poké Ball normalization, PP normalization or removal of moves that the Pokémon cannot legally know in Gen III. Move removal is offered only when it strictly reduces the number of Invalid findings and leaves another move.
- A approves; B cancels. Approval restarts the **entire reviewed batch**, checks the exact original source and destination files and requires the existing two-step journal/commit flow. No source Pokémon is rewritten by the proposal. Successful transfers continue to remove the source Pokémon and retain the original bytes in journal sidecars.
- If the proposal cannot preserve all protected identity traits or cannot pass the real PCCS output's verification, **no repair option is shown**. An honest blocker is safer than injecting an arbitrary "legalized" Pokémon.

This is an unofficial Gen II → III conversion, not a legitimate Nintendo transfer. A locally passing repair cannot establish historical authenticity, and independent pinned PKHeX checks are an additional requirement.

## Validation

The tests assert that a clean PCCS output has no repair offer; an invalid Gen III ball can be fixed without changing identity, and an incompatible Pokémon move can only be removed with full disclosure. A Pokémon whose nickname does not match the source may not be repaired. The two destination candidates are added to the PKHeX audit alongside the original 36 supported conversion cases and the VPT038 Gen II EXP repair: **39 independently validated PK3 samples**.

The DS, PCCS staged transfer, FAT journal/recovery and ARM9 workflows run on this branch before a hardware test.

## Build this ROM, not VPT038

In the same devkitPro MSYS2 terminal used for VPT037/VPT038:

```bash
cd /g/GITHUB/PKTOOLTRANSPORTER/PKTool
git fetch origin refs/heads/dev/vpt038-destination-repair:refs/remotes/origin/dev/vpt038-destination-repair
git switch dev/vpt038-destination-repair 2>/dev/null || git switch -c dev/vpt038-destination-repair
test "$(git branch --show-current)" = "dev/vpt038-destination-repair" || { echo "WRONG BRANCH"; exit 1; }
git pull --ff-only origin dev/vpt038-destination-repair
export DEVKITPRO=/c/devkitPro
export DEVKITARM="$DEVKITPRO/devkitARM"
export PATH="$DEVKITARM/bin:$DEVKITPRO/tools/bin:$PATH"
export VPT_SPRITES_FETCHER=curl
bash experiments/pccs/build_private_candidate.sh \
  /g/GITHUB/PKTOOLTRANSPORTER/VPT038-DestinationRepair.nds
```

A successfully verified build shows `PASS: VPT038 destination repair diagnostics present, silent boot` and `Build verified`; the ROM banner is **PKTool V38D**, and Settings/About shows **VPT038-DestRepair**.

Test **only with copies** of Silver and Emerald saves. Do not overwrite the working VPT038. If DON still shows BLOCKED and there is no Y repair offer, use LEFT/RIGHT to inspect all ERRORS and record those actual messages; the source and destination backups remain untouched.
