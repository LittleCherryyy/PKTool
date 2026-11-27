# VPT037 / Step 1 — Remove the interactive startup probe

Branch: `dev/vpt037-no-startup-probe`. Based on the proven VPT036 Unova UI.

## Change

The experimental ARM9 private builder previously inserted `vptArm9Probe()` **before** `main()` initialized display hardware. Its old implementation called `consoleDemoInit()`, exercised a three-species PCCS test and waited for A. This caused the diagnostic screen observed at the start of every private candidate.

Now `vptArm9Probe()` remains a silent link-anchor: it retains PCCS journal functions in the linked ELF, but the console output and interactive loop are wrapped in `#if defined(VPT_BOOT_DIAGNOSTIC)`. No production/private build defines that macro. The private build rejects an ELF containing the old `VPT006 ARM9 read-only probe` label, verifies the themed UI marker, and shows `VPT037-NoProbe` as its version to distinguish it from VPT036.

All transactional, PCCS, journal and save code is unchanged. The original short `Mounting SD card...` splash is retained for now; startup preload/initial scanning belongs to a later step.

## Build in devkitPro MSYS2

```sh
cd /g/GITHUB/PKTOOLTRANSPORTER/PKTool
git fetch origin
git switch --track origin/dev/vpt037-no-startup-probe
export DEVKITPRO=/c/devkitPro
export DEVKITARM="$DEVKITPRO/devkitARM"
export PATH="$DEVKITARM/bin:$DEVKITPRO/tools/bin:$PATH"
export VPT_SPRITES_FETCHER=curl
bash experiments/pccs/build_private_candidate.sh \
  /g/GITHUB/PKTOOLTRANSPORTER/VPT037-NoProbe.nds
```

If you already have the branch, use `git switch dev/vpt037-no-startup-probe` and `git pull --ff-only`. The build refuses to overwrite any existing output file. Use another output filename for a subsequent build.

## Real DSi checks

1. Open `VPT037-NoProbe.nds` (internal SD, TWiLight Menu++ DSi mode).
2. Confirm there is no "VPT006 ARM9 read-only probe" page or "A: continue" prompt, and no need to press A before the PKTool home screen.
3. Confirm the VPT037 version ID is visible on Home and the five Unova-themed entries are accessible.
4. Confirm Poke Transporter opens and a read-only source preview is possible. Use disposable saves if proceeding to a transfer.
5. Confirm that pending journal recovery still takes precedence over opening saves; only complete it with explicit user input.

Private PCCS-linked builds are not uploaded to CI or public releases while licensing remains unresolved.
