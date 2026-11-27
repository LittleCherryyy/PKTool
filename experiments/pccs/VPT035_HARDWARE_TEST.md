# VPT035 — DSi private hardware test candidate

This is a **development test only**, not a public PKTool release.
The pinned PCCS subproject currently has no standalone redistribution license
identified. Do not upload/share the PCCS-linked binary or any included PCCS
objects. The independently owned Poke Transporter GB MIT license must not be
assumed to license the separate PCCS library. The build helper keeps the binary
on the hardware owner's computer; it does not publish it through GitHub Actions.

## Build on a computer with a working devkitPro ARM9 toolchain

Requirements: bash, git, tar, make, Python 3, devkitARM, libnds, libfat and
libfilesystem, plus internet access to the pinned PCCS and sprite sources.
The experimental ARM9 CI uses the `devkitpro/devkitarm:latest` container as
its reproducible build environment. On Windows, use a compatible
Linux/devkitPro environment (for example WSL or a container), *not* an
unverified independent ARM toolchain without libnds.

From a clean checkout of this repository:

```sh
git fetch origin
git switch dev/vpt035-private-test-build
export DEVKITARM=/opt/devkitpro/devkitARM
bash experiments/pccs/build_private_candidate.sh ./VPT035-PKTool.nds
```

The private wrapper creates a disposable tracked-source snapshot, applies the
same pinned PCCS charset/Stat Exp fixes as CI, builds the complete transporter
for ARM9, checks that `App::commitTransporter` is actually linked, and prints
the file size and SHA-256. It refuses to run in CI and refuses to overwrite an
existing candidate. The user's working checkout stays unmodified; its local
uncommitted edits are not included in the binary. A plain `make` builds
upstream PKTool and **does not include this experimental transporter**.

## Windows/MSYS2: restart stalled sprite download safely

If the first private build displays PCCS `libpccs.a` sizes and then appears
idle in `python3`, it is building thousands of historical art images. VPT035
now prints progress every 100 images and saves every converted sprite outside
the disposable checkout in `~/.cache/pktool-vpt-art`. A new run can reuse
already downloaded images. Pinned missing HTTP 404 sprites are remembered too.

In devkitPro MSYS2, after stopping the **old** running build with Ctrl+C:

```sh
cd /g/GITHUB/PKTOOLTRANSPORTER/PKTool
git pull --ff-only origin dev/vpt035-private-test-build
export DEVKITPRO=/c/devkitPro
export DEVKITARM="$DEVKITPRO/devkitARM"
export PATH="$DEVKITARM/bin:$DEVKITPRO/tools/bin:$PATH"
# Use system curl if Python urllib hangs on this Windows network.
export VPT_SPRITES_FETCHER=curl
bash experiments/pccs/build_private_candidate.sh \
  /g/GITHUB/PKTOOLTRANSPORTER/VPT035-PKTool.nds
```

This restarts the temporary PCCS build, but future art downloads are cached.
The first line `Historic art: ...` indicates sprite generation has started.
If network access drops, the generator prints failures and uses the built-in
sprite fallback for missing entries. A completely empty edition pack still
fails the build rather than silently claiming to contain its artwork.

## Prepare the Nintendo DSi

1. Store an external copy of **both** original `.sav` files on the PC.
   First hardware trial: a disposable **English Crystal** save containing one
   boxed non-egg Pokemon and a disposable **English Ruby** save with a free PC
   slot. Reproduce success with these before testing a batch of 30.
2. Place the two saves on the **DSi internal SD** in folders scanned by PKTool.
   Keep each game's save format intact (Gen II commonly 32 KiB, Gen III 128 KiB).
   Do not assume a flashcart's microSD is identical to internal `sd:/`.
3. Copy the private `VPT035-PKTool.nds` to the DSi internal SD. Launch it via
   TWiLight Menu++ in DSi mode, where `isDSiMode()` and internal SD are
   available. DS-mode flashcarts cannot use the experimental internal-SD route.
4. Open **Poke Transporter** from PKTool Home (Y). Select the GB/GBC source,
   confirm its visual edition when prompted, choose the boxed Pokemon, then
   choose the Gen III save and an empty destination slot. Confirm its edition
   when prompted; game identity affects artwork, not PCCS policy.
5. Inspect the PCCS legality/identity review. Only proceed if every selected
   entry is READY. Press START twice. The Link animation is read-only: the
   source sprite crosses the first half, flashes at the transformer, and the
   GBA sprite crosses the second half. A/START skips the remainder.
6. A successful transfer enters the recovery/completion screen. Archive the
   completed journal with the screen's indicated A/START action **before**
   reopening either save in its game. Then verify the Pokemon is absent from
   the Crystal PC and present in the Ruby PC.

## Stop conditions

If the DSi hangs, reports a conflict, shows a missing SD path, or enters an
incomplete/pending journal state, **do not open either save in a game** and
**do not delete** `sd:/PKTOOL/transporter/active.vpt`, `.new`, sidecars or
backups. Preserve the entire `PKTOOL/transporter` directory and both saves.
The recovery UI can recheck or resume exact recorded transactions; guessing,
deleting files or blindly repeating a commit may cause data loss.

Report the source/destination editions, species/quantity, whether artwork
changed at the transformer, any visible error, and whether the final pair of
saves validates in-game. No ROMs or copyrighted assets need to be uploaded.

## Evidence and limits

VPT031 and VPT032/033 were validated in host CI, a disposable ARM9 link and
a strict PKHeX audit. VPT034 adds a full host UI pixel-level regression with
synthetic Crystal/Ruby art. None of this proves the complete private VPT035
binary has run on a real DSi, confirms actual DSi frame pacing or validates
sudden power-loss behavior on physical FAT media. Those are the reason for
this isolated, disposable-save hardware candidate.
