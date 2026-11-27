#!/usr/bin/env bash
# VPT035/VPT036/VPT037: private build for the hardware owner, never CI/public.
# This script redistributes no PCCS files and does not modify the Git checkout.
set -euo pipefail

if [ "$#" -ne 1 ]; then
    echo "Usage: bash experiments/pccs/build_private_candidate.sh <output.nds>" >&2
    exit 2
fi
if [ "${CI:-}" = true ]; then
    echo "REFUSED: VPT035 PCCS-linked binary must not be produced in CI." >&2
    exit 2
fi
: "${DEVKITARM:?Set DEVKITARM to a devkitPro devkitARM installation.}"
for program in git tar make python3; do
    if ! command -v "$program" >/dev/null 2>&1; then
        echo "Missing tool: $program" >&2; exit 2
    fi
done
if [ ! -x "$DEVKITARM/bin/arm-none-eabi-gcc" ]; then
    echo "Missing devkitARM ARM compiler at $DEVKITARM/bin/arm-none-eabi-gcc" >&2
    exit 2
fi
branch="$(git symbolic-ref --quiet --short HEAD || true)"
if [ "$branch" != "dev/vpt035-private-test-build" ] && [ "$branch" != "dev/vpt036-unova-ui" ] &&
   [ "$branch" != "dev/vpt037-no-startup-probe" ] &&
   [ "$branch" != "dev/vpt037-items-2-5" ]; then
    echo "Checkout a private VPT035/VPT036/VPT037 test branch first (current: $branch)." >&2
    exit 2
fi
commit="$(git rev-parse --verify HEAD)"
# Use the caller's current directory when given a relative output path.
output="$(python3 - "$1" <<'PY'
import pathlib,sys
p=pathlib.Path(sys.argv[1]).expanduser().resolve()
if p.suffix.lower()!=".nds": raise SystemExit("Output must end in .nds")
print(p)
PY
)"
# Keep filename and checked-out source branch consistent. A filename is
# not a version selector: the private build always archives the CURRENT HEAD.
case "$(basename "$output" | tr '[:upper:]' '[:lower:]')" in
  *vpt037*|*v037*)
    if [ "$branch" != "dev/vpt037-items-2-5" ]; then
        echo "ERROR: VPT037 filename requested, but current branch is $branch." >&2
        echo "Switch to dev/vpt037-items-2-5 before compiling." >&2
        exit 2
    fi
    ;;
esac
if [ -e "$output" ]; then
    echo "Refusing to overwrite existing $output" >&2; exit 2
fi
mkdir -p "$(dirname "$output")"
work="$(mktemp -d)"
partial="$output.partial.$$"
trap 'rm -rf "$work"; rm -f "$partial"' EXIT

echo "Private PKTool Transporter build ($branch) from commit: $commit"
echo "IMPORTANT: only this branch and commit are packaged; filename does not choose a version."
echo "Changes in your working tree are NOT included in the test binary."
# Source-only snapshot; no downloaded PCCS subtree is copied into this repo.
git archive --format=tar "$commit" | tar -xf - -C "$work"
(
    cd "$work"
    CI= VPT_KEEP_LOCAL=1 bash experiments/pccs/build_arm9_probe.sh
    test -s PKTool.nds
    # A working ARM9 probe is required, not a base PKTool-only build.
    "$DEVKITARM/bin/arm-none-eabi-nm" -C PKTool.elf > "$work/candidate-symbols"
    grep -Fq 'App::commitTransporter(' "$work/candidate-symbols"
    # A different filename alone does not establish the themed build.
    # Refuse to produce a misleading VPT036 binary if this compiled ELF
    # does not contain the new UI and its explicit on-screen version ID.
    if [ "$branch" = "dev/vpt037-items-2-5" ]; then
        if grep -aFq 'VPT036-Unova' "$work/PKTool.elf" || 
           grep -aFq 'MENU PRINCIPAL' "$work/PKTool.elf" ||
           grep -aFq 'Gerenciador Pokemon' "$work/PKTool.elf"; then
            echo "ERROR: stale VPT036 UI strings detected in VPT037 executable." >&2
            exit 1
        fi
        if ! grep -aFq 'VPT037-UI' "$work/PKTool.elf" ||
           ! grep -aFq 'Pokemon Save Manager' "$work/PKTool.elf" ||
           ! grep -aFq 'MAIN MENU' "$work/PKTool.elf"; then
            echo "ERROR: English VPT037 interface or version missing from linked executable." >&2
            exit 1
        fi
        if grep -aFq 'POKEDEX  /  UNOVA' "$work/PKTool.elf" ||
           grep -aFq 'VPT006 ARM9 read-only probe' "$work/PKTool.elf"; then
            echo "ERROR: obsolete home header or diagnostic remains in VPT037." >&2
            exit 1
        fi
        echo "PASS: VPT037 English home, private identifier, silent boot"
    fi
    if [ "$branch" = "dev/vpt037-no-startup-probe" ]; then
        if ! grep -aFq 'VPT037-NoProbe' "$work/PKTool.elf"; then
            echo "ERROR: VPT037 identity missing from linked ARM9 executable." >&2
            exit 1
        fi
        if ! grep -aFq 'POKEDEX  /  UNOVA' "$work/PKTool.elf"; then
            echo "ERROR: Unova UI missing from VPT037 private build." >&2
            exit 1
        fi
        if grep -aFq 'VPT006 ARM9 read-only probe' "$work/PKTool.elf"; then
            echo "ERROR: startup diagnostic is still present." >&2
            exit 1
        fi
        echo "PASS: VPT037 silent startup, Unova UI and Transporter confirmed"
    fi
    if [ "$branch" = "dev/vpt036-unova-ui" ]; then
        if ! grep -aFq 'VPT036-Unova' "$work/PKTool.elf"; then
            echo "ERROR: VPT036 version marker missing from linked ARM9 executable." >&2
            exit 1
        fi
        if ! grep -aFq 'POKEDEX  /  UNOVA' "$work/PKTool.elf"; then
            echo "ERROR: Unova main-menu UI marker missing from linked ARM9 executable." >&2
            exit 1
        fi
        echo "PASS: VPT036-Unova UI and version confirmed in linked ARM9 executable"
    fi
)
cp "$work/PKTool.nds" "$partial"
test -s "$partial"
mv "$partial" "$output"
echo "Build verified: $output"
python3 - "$output" <<'PY'
import hashlib,pathlib,sys
p=pathlib.Path(sys.argv[1])
print("Size:",p.stat().st_size,"bytes")
print("SHA-256:",hashlib.sha256(p.read_bytes()).hexdigest())
PY
echo "Keep the output private until PCCS licensing is clarified."
