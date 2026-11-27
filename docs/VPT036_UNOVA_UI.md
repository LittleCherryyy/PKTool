# VPT036 — Unova Pokedex UI (DSi native-scale visual pass)

Status: isolated theme candidate on `dev/vpt036-unova-ui`. The known VPT035 private branch is unchanged.

## Target and limits

- Both screens are 256 x 192 pixels, the existing PKTool Canvas API outputs RGB555 (15-bit color).
- Flat, high-contrast Unova/Pokedex-inspired charcoal, crimson selection, cyan outlines; simple grid and pixel primitives. **No** additional full-screen bitmap textures, shaders, font downloads, alpha/blur effects or animated sprite cache required.
- Reuse the existing DS font, Canvas, button/touch events, main-screen framebuffer and established 128 KiB sprite cache. No new heap-intensive visual subsystem.
- Headers and hint bars now use the same palette across editor, Bank, save browser and Transporter.
- Existing UI functions remain functional; mockups are visual references, **not a promise of new features**. No invented battery percentage, free-SD meter, 'Import/Export' menu or separate audio setting.
- Main menu entry order: Open save, Backups, Bank, Settings; when the optional transporter is enabled, the full-width bottom tile opens Poke Transporter. Hardware Y shortcut remains. D-pad now follows the actual 2x2 + 1 grid naturally.
- Settings exposes the existing order/new level/language/animation/About controls unchanged. The save browser shows existing saves and backups; no new file format or export features.
- On the completed transfer screen, A (or touching Continue) archives and returns to Home; START/B/X still do not archive it. Existing two-START precommit safety is unchanged.
- Sprite resolution, conversion, legality policy, transaction ordering, journal format, recovery, Bank and save persistence are not touched by this theme.

## Private DSi candidate (not public)

The helper accepts the themed branch while preserving the old VPT035 branch. It refuses to overwrite an existing file or run in CI.

In devkitPro MSYS2:

```bash
cd /g/GITHUB/PKTOOLTRANSPORTER/PKTool
git fetch origin
git switch --track origin/dev/vpt036-unova-ui
export DEVKITPRO=/c/devkitPro
export DEVKITARM="$DEVKITPRO/devkitARM"
export PATH="$DEVKITARM/bin:$DEVKITPRO/tools/bin:$PATH"
export VPT_SPRITES_FETCHER=curl
bash experiments/pccs/build_private_candidate.sh \
  /g/GITHUB/PKTOOLTRANSPORTER/VPT036-Unova.nds
```

If that branch is already checked out, use `git pull --ff-only` instead of `git switch --track`. Run the private binary from TWiLight Menu++ in DSi mode with backups of both saves.

## Verification gates

1. Normal host tests (Canvas, touch zones, existing settings navigation, parser) and normal DS/ARM9 compile.
2. Experimental PCCS host UI integration with updated five-tile main navigation; strict PKHeX tests.
3. Experimental linked ARM9 build, which is intentionally **not uploaded** by CI.
4. DSi physical test: open Save, Backups, Bank, Settings and Transporter by touch and D-pad; verify red cursor and readable labels on both LCDs; transfer using disposable saves and verify no journal/save behavior change; test Continue via A.

External PCCS licensing is still unresolved. Do **not** distribute a PCCS-linked executable as a public release.
