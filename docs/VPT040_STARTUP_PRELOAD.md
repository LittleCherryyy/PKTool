# VPT040 — Fast startup, bounded preload and consistent Unova menus

Based on the DSi-tested VPT038.1 destination-repair branch
`dev/vpt038-destination-repair`. Isolated work branch:
`dev/vpt040-startup-cache-and-ui`.

## Observed problems and verified code paths

1. The DSi shows a **white screen before PKTool becomes visible**. The TWiLight/nds-bootstrap launcher may read the full .nds and its NitroFS data before `main()`; code inside `main()` cannot draw during that interval. The previous build bundled ~8.6 MB of base art plus historical sprite packs, so the startup download/loading cost is material. We now **losslessly compact** old base and historical PKSP sprite packs into compact `PKS2` RLE packets when profitable. The renderer can read both old `PKS1` and `PKS2`. Pack indexes and all RGB555 pixels are preserved. This reduces the file transferred/loaded by the launcher, and never requires shipping external art, but the actual white-screen duration remains device- and SD-card-dependent.
2. FAT/NitroFS mounts are synchronous. The screen now shows an actual VBlank-committed **dark PKTool splash** before either mount, so the application part of startup no longer looks like a frozen white screen.
3. SD discovery used to begin **on first menu entry** (`startTransportScan`). Instead, an incremental scan is **scheduled at construction** and stepped only while Home is idle. The Home is rendered **before any scan step**, so the app becomes interactive promptly. The source and destination browsers share the same scanned catalog and never restart the scan merely because the user entered a menu.
4. After discovery, an image cache reads up to **384 KiB of validated-size GB/GBC/GBA saves** from the SD, maximum **8 files**, prioritizing GB/GBC first (editor browser ordering unchanged). This is only a bounded **read-only session hint**: any file whose size or timestamp changes falls back to disk. Journal commits always perform full rechecks and exact backup/verification; all snapshots are discarded before reviewed transfers and save writes. Gen IV/V raw files and all transactional journal files are **not cached**.
5. After the save cache, a few important sprite packs are prewarmed on Home **one per idle frame**, including game-cover art and the first known exact classic-edition source Pokémon. The sprite cache still enforces the original **128 KiB decoded limit**. DSi screens keep those warmed sprites while browsing, and release them before a transaction.
6. The **initial classic source box selection** was doing one box per frame and showing a transitional screen. Those GB/GBC saves are 32–64 KiB and already in memory, so classic **source box decoding is now performed at once**, transitioning directly to the new Unova Pokémon picker. Destination Gen III boxes remain incremental to avoid a heavy allocation/frame stall. Any remaining loading panels, edition-choice menus and source selection menus use the proper Unova palette and cards — no legacy generic list.
7. Home labels progress as `INDEXING SD / SAVES`, `CACHING n/N`, `PREPARING SPRITES`, then `N SAVES READY`. Entering a menu before READY may still require a save read (data not yet preloaded), because responsiveness is prioritized over holding the user on a blank boot screen.

## Safety and explicit boundaries

- Preloading does **not** load the entirety of every 512 KiB save or all Pokémon sprites into RAM. That would exhaust ARM9 heap, especially on regular DS mode. It does **not** eliminate mandatory full reads for review, commit, backup, and recovery.
- Source/destination journal order, PCCS conversion rules, no-guess identity policy, opt-in repairs and item return are unchanged from VPT038.1.
- A game cartridge or SD may be changed externally; metadata rechecks are necessary, and restart the homebrew to discover newly added files. Do not hot-remove the SD card.
- We cannot guarantee the duration of the **pre-main launcher** white screen on every version of TWiLight/nds-bootstrap. Real DSi timing requires an on-device test; the .nds compression report measures bytes, not seconds.

## DSi private test

Use **devkitPro MSYS2**; from the previously cloned repository:

```bash
cd /g/GITHUB/PKTOOLTRANSPORTER/PKTool
git fetch origin refs/heads/dev/vpt040-startup-cache-and-ui:refs/remotes/origin/dev/vpt040-startup-cache-and-ui
git switch dev/vpt040-startup-cache-and-ui 2>/dev/null || git switch -c dev/vpt040-startup-cache-and-ui
test "$(git branch --show-current)" = "dev/vpt040-startup-cache-and-ui" || { echo "Wrong branch"; exit 1; }
git pull --ff-only origin dev/vpt040-startup-cache-and-ui
export DEVKITPRO=/c/devkitPro
export DEVKITARM="$DEVKITPRO/devkitARM"
export PATH="$DEVKITARM/bin:$DEVKITPRO/tools/bin:$PATH"
export VPT_SPRITES_FETCHER=curl
bash experiments/pccs/build_private_candidate.sh \
  /g/GITHUB/PKTOOLTRANSPORTER/VPT040-Preload-DSi.nds
```

The verified link must contain `VPT040-Preload`, `INDEXING SD / SAVES`,
`POKEDEX / LOADING`, `MAIN MENU`, plus ARM9 journal/transport symbols.
The build refuses to overwrite an existing output, and the ROM will be
**PKTool V040** in TWiLight, **VPT040-Preload** in Settings > About.

On DSi, verify the following with copied/disposable Silver and Emerald saves:

- The launcher white delay, the first dark PKTool splash, and how long the
  `N SAVES READY` status takes. Compare against the working VPT038.1 ROM.
- Do not press a button until the status reads SAVES READY on the first run.
  Enter Transporter and open the first source Pokémon box: it should show
  the Unova Pokémon list *directly*, no old list/menu and no source spinner.
- Switch to Emerald destination; the destination loader, if needed, is
  themed, and the six-column free-slot grid remains.
- Back to Home and enter the regular save browser. The catalog should appear
  without rediscovering the SD card. Compare load behavior against VPT038.1.
- Use only disposable saves to verify Repair for Transfer / normal transfers,
  including backup creation and reboot/recovery. The exact live write checks
  remain mandatory.

Host regressions exercise startup discovery, the 384 KiB maximum, prewarming,
instant source-box navigation and the old transfer journal/recovery tests.
Python and C++ tests assert every compressed image expands exactly with
correct RGB555 values and malformed images are rejected.
