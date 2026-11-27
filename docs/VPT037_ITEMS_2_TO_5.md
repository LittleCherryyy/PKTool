# VPT037 — integrated UI polish (items 2–5)

Branch: `dev/vpt037-items-2-5`, based on `dev/vpt037-no-startup-probe`. The approved VPT035 and the VPT036 Unova theme remain unchanged.

## Implemented

1. Startup diagnostic removed (inherited from VPT037 item 1; it is silent unless built with `VPT_BOOT_DIAGNOSTIC`).
2. User-facing menu and Settings text in **English**. Native 256x192 RGB555 Unova/Pokédex styling maintained.
3. Home: no `POKEDEX / UNOVA` subtitle, no build ID beneath `Pokemon Save Manager`, no duplicate `Y Transporter` hint. The legacy Y shortcut still works, but selection is via the large menu tile. `HOME` is a neutral title. Version is available under Settings > About.
   - GB/GBC Japanese cartridge save layout can be identified from its actual data. Non-Japanese Gen I/II saves generally do not encode a definitive USA-versus-Europe country; English is therefore displayed as `English` unless the source filename explicitly contains `(USA)`, `(US)`, `(Europe)`, or `(EU)`. Likewise French/German/Italian/Spanish are inferred from explicit filename tags, and left/right may correct ambiguous input. No invented ROM nationality.
4. Source selection, with original game-edition sprites retained: dark Pokédex panels, source language, shiny indicator, source box/slot, six visible touch rows with red cursor, cyan borders, green selected marks, Y for whole box, X for toggle, L/R for source box navigation.
5. Destination: dark-themed six-column box grid, occupied Pokémon species icons, green empty markers, red cursor and cyan valid starts; Up/Down moves a row, Left/Right moves to the next valid slot in the same box, L/R switches to another box with eligible starting positions. Invalid/occupied slots are not selectable. A accepts, B returns. Existing explicit selection and journal/backup protections remain.

## Scope

The source-to-destination PCCS conversion, legality rules, transfer journal, real-write ordering, bank logic and link animation were **not changed**. Repair for Transfer and writing concurrently with animation are independent upcoming items 6 and 7; preload is item 8. This is a UI/navigation batch only.

## Private test on real DSi

With devkitPro MSYS2 and the local repository already cloned:

```sh
cd /g/GITHUB/PKTOOLTRANSPORTER/PKTool
git fetch origin
git switch dev/vpt037-items-2-5 || git switch --track origin/dev/vpt037-items-2-5
git pull --ff-only
export DEVKITPRO=/c/devkitPro
export DEVKITARM="$DEVKITPRO/devkitARM"
export PATH="$DEVKITARM/bin:$DEVKITPRO/tools/bin:$PATH"
export VPT_SPRITES_FETCHER=curl
bash experiments/pccs/build_private_candidate.sh \
  /g/GITHUB/PKTOOLTRANSPORTER/VPT037-UI-Polish.nds
```

If this output filename already exists, choose a new one: the builder intentionally refuses overwrite. Wait for `PASS: VPT037 English home, private identifier, silent boot` and `Build verified`; then copy only the new output to the DSi internal SD and open with TWiLight Menu++ in DSi mode. Version `VPT037-UI` appears under About, not Home.

### Tests

- Silent startup: no interactive debug.
- Home: English status, `MAIN MENU`, `Pokemon Save Manager`, no visible version, no Y hint.
- Source: game sprites, source language, X marked/removed, Y whole box, touch rows, L/R jumps boxes.
- Destination: 6-column grid; Up/Down ±6 row; Left/Right valid slot; L/R other box; touch cursor, unavailable slots blocked.
- Save backups and transfer (on disposable saves only), journal/restart recovery, A Continue on complete.
- Normal DS and host CI plus PCCS host and ARM9 linked checks.

Do not distribute a PCCS-linked binary publicly while its license conditions are unresolved.
