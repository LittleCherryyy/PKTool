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
# Build uncompressed, host-generated tables; do not use PCCS's ARMv4T/GBA
# compressed-table path on ARM9. Do not alter the conversion implementation.
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
# Keep the normal UI linked; insert a read-only diagnostic at startup.
# Changes below apply only to this disposable CI checkout, not the source tree.
python3 - "$work/arm9" <<'PY'
import pathlib, sys
root = pathlib.Path(sys.argv[1])
p = pathlib.Path("Makefile")
s = p.read_text()
a = "LIBDIRS := $(LIBNDS) $(PORTLIBS)"
b = "LIBS := -lfilesystem -lfat -lnds9"
assert s.count(a) == s.count(b) == 1
p.write_text(s.replace(a, a + " " + str(root)).replace(b, "LIBS := -lpccs -lfilesystem -lfat -lnds9").replace("-DARM9", "-DARM9 -DVPT_TRANSPORTER_PREVIEW -DVPT_TRANSPORTER_WRITE"))
p = pathlib.Path("ds/main.cpp")
s = p.read_text()
a = "int main() {"
assert s.count(a) == 1
p.write_text(s.replace(a, "void vptArm9Probe();\n\nint main() {\n    vptArm9Probe();").replace("config.storageReady = fatInitDefault();", "config.storageReady = fatInitDefault();\n    config.transporterPreview = isDSiMode();\n    if (config.transporterPreview) {\n        config.scanRoot = \"sd:/\";\n        config.dataDir = \"sd:/PKTOOL\";\n    }\n    config.version = \"VPT017-candidate\";"))
pathlib.Path("ds/vpt_arm9_bridge.cpp").write_text('#include "../experiments/pccs/pccs_bridge.cpp"\n')
pathlib.Path("ds/vpt_arm9_stage.cpp").write_text('#include "../experiments/pccs/stage_transfer.cpp"\n')
pathlib.Path("ds/vpt_arm9_journal.cpp").write_text('#include "../experiments/pccs/transfer_journal.cpp"\n')
pathlib.Path("ds/vpt_arm9_probe.cpp").write_text(pathlib.Path("experiments/pccs/arm9_probe.cpp").read_text())
PY
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
# Neither the linked NDS nor the static library is published by this workflow.
echo "PASS: linked ARM9 probe (build only; no hardware execution)"
