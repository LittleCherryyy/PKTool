#!/usr/bin/env bash
# Build-only experiment. Run in a disposable checkout; never upload linked files.
set -euo pipefail
: "${DEVKITARM:?devkitARM is required}"
work="$(mktemp -d)"
trap 'rm -rf "$work"; if [ "${VPT_KEEP_LOCAL:-0}" != 1 ] || [ "${CI:-}" = true ]; then rm -f PKTool.nds PKTool.elf; fi' EXIT
if [ "${VPT_KEEP_LOCAL:-0}" = 1 ] && [ "${CI:-}" = true ]; then
    echo "Local output is disabled in CI"; exit 1
fi
pin=9cd6bc9bd1250013ceb1b1a70d96569985a87543
git clone --no-checkout https://github.com/Striaton-Lab-Team/Pokemon-Community-Conversion-Standard.git "$work/pccs"
git -C "$work/pccs" checkout --detach "$pin"
test "$(git -C "$work/pccs" rev-parse HEAD)" = "$pin"
python3 experiments/pccs/fix_upstream_charset.py "$work/pccs"
python3 experiments/pccs/fix_upstream_statexp.py "$work/pccs"
# Build uncompressed, host-generated tables; do not use PCCS's ARMv4T/GBA
# compressed-table path on ARM9. Apply the documented charset and Stat Exp fixes.
make -C "$work/pccs" -j2 CXX=c++ CC=cc AR=ar
mkdir -p "$work/arm9/lib" "$work/arm9/include" "$work/obj"
cp "$work/pccs/include/"*.h "$work/arm9/include/"
arm="$DEVKITARM/bin/arm-none-eabi"
flags=(-march=armv5te -mtune=arm946e-s -O2 -ffunction-sections -fdata-sections)
for bin in "$work/pccs/data/"*.bin; do
    name="$(basename "$bin")"
    hdr="${name//./_}.h"
    "$work/pccs/tools/bin2s/bin2s" -a 4 -H "$work/arm9/include/$hdr" "$bin" > "$work/obj/$name.s"
    "$arm-gcc" "${flags[@]}" -c "$work/obj/$name.s" -o "$work/obj/$name.o"
done
for cpp in "$work/pccs/source/"*.cpp; do
    "$arm-g++" "${flags[@]}" -std=gnu++17 -fno-rtti -fno-exceptions \
        -I"$work/arm9/include" -c "$cpp" -o "$work/obj/$(basename "$cpp" .cpp).o"
done
"$arm-ar" rcs "$work/arm9/lib/libpccs.a" "$work/obj/"*.o
"$arm-size" "$work/arm9/lib/libpccs.a"
# Keep the normal UI linked. Call only the silent link-anchor at startup;
# the interactive diagnostic is compiled only with VPT_BOOT_DIAGNOSTIC.
# Changes below apply only to this disposable CI checkout, not the source tree.
python3 - "$work/arm9" <<'PY'
import pathlib, sys
root = pathlib.Path(sys.argv[1])
p = pathlib.Path("Makefile")
s = p.read_text()
# Distinct icon/banner title in TWiLight Menu++ to prevent mistaken launch
# of an older VPT036 ROM with the same generic "PKTool" display name.
banner = "GAME_TITLE     := PKTool"
assert s.count(banner) == 1
s = s.replace(banner, "GAME_TITLE     := PKTool V038")
subtitle = "GAME_SUBTITLE1 := Pokemon save editor"
assert s.count(subtitle) == 1
s = s.replace(subtitle, "GAME_SUBTITLE1 := Repair for Transfer")
a = "LIBDIRS := $(LIBNDS) $(PORTLIBS)"
b = "LIBS := -lfilesystem -lfat -lnds9"
assert s.count(a) == s.count(b) == 1
p.write_text(s.replace(a, a + " " + str(root)).replace(b, "LIBS := -lpccs -lfilesystem -lfat -lnds9").replace("-DARM9", "-DARM9 -DVPT_TRANSPORTER_PREVIEW -DVPT_TRANSPORTER_WRITE"))
p = pathlib.Path("ds/main.cpp")
s = p.read_text()
a = "int main() {"
assert s.count(a) == 1
p.write_text(s.replace(a, "void vptArm9Probe();\n\nint main() {\n    vptArm9Probe();").replace("config.storageReady = fatInitDefault();", "config.storageReady = fatInitDefault();\n    config.transporterPreview = isDSiMode();\n    if (config.transporterPreview) {\n        config.scanRoot = \"sd:/\";\n        config.dataDir = \"sd:/PKTOOL\";\n    }\n    config.version = \"VPT038-Repair\";"))
pathlib.Path("ds/vpt_arm9_bridge.cpp").write_text('#include "../experiments/pccs/pccs_bridge.cpp"\n')
pathlib.Path("ds/vpt_arm9_stage.cpp").write_text('#include "../experiments/pccs/stage_transfer.cpp"\n')
pathlib.Path("ds/vpt_arm9_journal.cpp").write_text('#include "../experiments/pccs/transfer_journal.cpp"\n')
pathlib.Path("ds/vpt_arm9_probe.cpp").write_text(pathlib.Path("experiments/pccs/arm9_probe.cpp").read_text())
PY
# Pinned historical art packs, streamed from the same PokeAPI sprite source
# as the base editor; no persistent extra runtime memory for the whole pack.
python3 tools/build_transport_sprites.py assets/nitrofs/sprites
make -j2
"$arm-size" PKTool.elf
"$arm-nm" -C PKTool.elf > "$work/symbols"
grep -F 'previewCrystalToRuby(' "$work/symbols"
grep -F 'GBPokemon::convertToGen3(' "$work/symbols"
grep -F 'pccs_bridge::prepareJournal(' "$work/symbols"
grep -F 'pccs_bridge::resumeJournal(' "$work/symbols"
grep -F 'App::drawTransporter(' "$work/symbols"
grep -F 'ReviewedTransfer::create(' "$work/symbols"
grep -F 'inspectActiveJournal(' "$work/symbols"
grep -F 'App::drawTransferRecovery(' "$work/symbols"
grep -F 'archiveActiveJournal(' "$work/symbols"
grep -F 'App::commitTransporter(' "$work/symbols"
# The old startup diagnostic must not be present in the real linked image.
# This still retains journal/PCCS link anchors and all existing gates above.
if grep -aFq 'VPT006 ARM9 read-only probe' PKTool.elf; then
    echo "ERROR: interactive ARM9 startup probe linked into private candidate" >&2
    exit 1
fi
# Neither the linked NDS nor the static library is published by this workflow.
echo "PASS: linked ARM9 Transporter, silent startup (no diagnostic screen)"
