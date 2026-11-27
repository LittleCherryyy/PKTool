# VPT038 — Repair for Transfer (experimental, opt-in)

Base: `dev/vpt037-items-2-5`, which was tested successfully on the user's DSi.
New isolated branch: `dev/vpt038-repair-for-transfer`.

## User flow

In **Transfer Review**, select a **BLOCKED** Pokémon using the D-pad. When the source's **Gen I/II legality is the cause** and an actual corrected candidate has passed every check, the review shows `Y: Repair for Transfer available`.

- Press **Y** to inspect the original error and a scrollable list of proposed changes.
- **B** cancels with no changes.
- **A** approves this specific Pokémon's repair. The app runs a **fresh complete review** of both exact save images and all selected Pokémon. If another Pokémon remains blocked, the user may inspect/repair it separately, or select an approved subset with X. Nothing is written yet.
- Only after the reviewed batch is completely accepted does the old **START, START, link animation, journal/commit, Transfer Complete** flow become available. Transfer always removes the source Pokémon on success; this action does **not** rewrite or silently legalize the source save before transfer.

All messages in the UI are English, retaining the VPT037 dark Unova/Pokédex theme.

## Scope and safety

The first VPT038 build focuses on **limited, verifiable source Gen I/II legality normalization**. It uses PKTool's existing encounter-based `legality::legalizeGb` proposal as a candidate, **not** as an unconditional authorization.

The offer is emitted **only** if all of these hold:

1. A normal PCCS review explicitly fails with `Source legality cannot be confirmed`.
2. The proposed changes are nonempty, and the legalizer reports no unresolved issues.
3. Source legality after re-encoding the candidate is **Valid**, not Warning or Invalid.
4. Species, form, nickname, original trainer name/ID/gender, shiny, Pokémon gender, level, originating game, language, generation and egg status are byte-decoding equivalent. Repairs requiring changes to any of these are **not** offered.
5. The **complete real PCCS LEGAL Gen I/II → Gen III conversion**, Pokémon identity checks, PK3 structural/checksum verification and Gen III legality all pass on the corrected temporary entity.
6. A and B are separate explicit choices; Y alone does not authorize a repair.
7. On A, `ReviewedBatch` re-reads exact current source and target files and independently recomputes the candidate before a reviewed journal may exist. Approvals are indexed to the exact selected source/destination pair list; changing the batch clears them.

Eggs, unsupported languages, mythical/event conversions with unverified provenance, and corrupted or unavailable save files remain blocked. Destination-full, Bag overflow, unsupported mail/key item, and write/recovery conflicts are **not** repairable by this tool.

**Definition:** `Repair for Transfer` means validated compatibility with this conversion path, **not proof of an originally legitimate Pokémon**. Transfer is unofficial and not a Nintendo Poké Transporter service.

All original pre-transfer save bytes remain in the existing journal and source/destination backups. The correction is applied **only to the Pokémon representation exported to the destination**, while the source file follows the normal exact removal/compaction; no intermediate write or silent source normalization.

No changes to item-return semantics, transaction order, journal format, recovery rules or the existing animated presentation. Concurrent commit during animation is reserved for VPT039.

## Regression checks

- Host stage test: intentional Gen II level/EXP mismatch rejects normal preview, proposes a compatible fix, refuses invalid or duplicate repair indices, then produces a checked destination and exact source removal **only with explicit approval**.
- UI integration test: Y opens preview on eligible BLOCKED entry; B leaves saves and journal unchanged; a later Y/A starts a fresh checked batch; double START plus animation/commit produce a Complete journal; archiving clears it, and original pre-transfer bytes remain available in the backup.
- Other VPT002, VPT006 ARM9 and base CI checks remain enabled, including synthetic PKHeX audit and crash/recovery tests.

## Private build — devkitPro MSYS2

```bash
cd /g/GITHUB/PKTOOLTRANSPORTER/PKTool
git fetch origin refs/heads/dev/vpt038-repair-for-transfer:refs/remotes/origin/dev/vpt038-repair-for-transfer
git switch dev/vpt038-repair-for-transfer 2>/dev/null || git switch -c dev/vpt038-repair-for-transfer --track origin/dev/vpt038-repair-for-transfer
git pull --ff-only origin dev/vpt038-repair-for-transfer
test "$(git branch --show-current)" = "dev/vpt038-repair-for-transfer" || { echo "Wrong branch"; exit 1; }
export DEVKITPRO=/c/devkitPro
export DEVKITARM="$DEVKITPRO/devkitARM"
export PATH="$DEVKITARM/bin:$DEVKITPRO/tools/bin:$PATH"
export VPT_SPRITES_FETCHER=curl
bash experiments/pccs/build_private_candidate.sh /g/GITHUB/PKTOOLTRANSPORTER/VPT038-Repair-DSi.nds
```

The private builder should show `PASS: VPT038 Repair for Transfer present in ARM9, English UI, silent boot` and `Build verified`. It refuses to overwrite an existing output file and refuses a VPT038 filename on an older branch. The ROM identifies as **PKTool V038** on the DSi, and **VPT038-Repair** in Settings/About.

**Always test with disposable copies of saves, not irreplaceable originals.** The private NDS is not published in CI or released publicly while PCCS licensing remains unresolved.
