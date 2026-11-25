# PKTool PCCS — VPT001

Independent PKTool fork, not part of Pokewalker Virtual or Virtual Slot-2.
Initial target: original English Pokemon Crystal to original English Pokemon Ruby, on Nintendo DSi with internal SD. Keep all original PKTool functionality.

## Status
Experimental read-only adapter. Not linked into the normal application; no save mutations, bank changes, transfer UI or working NDS conversion build yet. Existing generation restrictions remain unchanged. The adapter is isolated outside the normal build source glob until its external dependency and tests are ready.

## Upstream credits and pinned dependencies
- PKTool by pioacosta and contributors, GPLv3: https://github.com/pioacosta/PKTool/tree/dbb6db01486287ebde05aa1ead803aa9279f5a87
- Poke Transporter GB by Striaton Lab Team and contributors: https://github.com/Striaton-Lab-Team/Poke_Transporter_GB/tree/ed29300e5b88666e9c3112a387f4ca36be149743
- PCCS submodule pinned by that Transporter: https://github.com/Striaton-Lab-Team/Pokemon-Community-Conversion-Standard/tree/9cd6bc9bd1250013ceb1b1a70d96569985a87543

The PCCS tree at this revision has no separate explicit license file. The Transporter repository describes its code as MIT, but the distinct PCCS library's redistribution terms must be confirmed before distributing the library or linked binaries. No PCCS source, binaries, game ROMs or saves are included. New adapter code is GPL-3.0-or-later.

## Conversion policy
Use PCCS LEGAL only. Reject eggs, held items, unsupported languages, invalid inputs and mythical/event remapping until tested. Reject uncertain source or destination legality findings; no auto-legalizer fallback. Preserve species, shiny status, gender, form, nickname and original trainer identity where compatible. A raw PK2 alone cannot establish the source save edition. Game checks belong to the caller. No claim of official transfer or guaranteed Pokemon HOME compatibility.

## Next gates
1. Run unmodified PKTool host tests and DS baseline. Confirm PCCS licensing. Compile PCCS in ARM9 non-GBA mode, with its generated data tables and bounded memory/runtime.
2. Compile the preview against pinned APIs and compare against real PCCS vectors. Verify all 24 PK3 permutations, checksum, identity and legality, fail closed. Test source game identity, invalid entities and RNG timing.
3. Add an explicit conversion preview for Crystal to Ruby, without changing existing Time Capsule or Gen III–V routes.
4. Stage and validate both saves. Journal a recoverable destination-first transaction, verify destination persistence before removing the exact source entity, verify source persistence, test interruption and storage failure. Preserve backups and provenance. No normal cloning or silent loss.
5. Hardware test with disposable saves before expanding language, game and mythical support.

## Independent preflight test
Run from repository root:
```sh
c++ -std=c++17 -Wall -Wextra -Werror experiments/pccs/test_preflight.cpp -o /tmp/vpt001-preflight
/tmp/vpt001-preflight
```
Only the preflight test is standalone. The converter itself has not yet been compiled against PCCS or tested for conversion correctness.

## VPT002 host conversion probe
The independent VPT002 workflow fetches the exact PCCS pin into the ephemeral runner, builds its host library and PKTool's portable core, then executes three synthetic English Crystal starter fixtures through the actual LEGAL API and strict adapter. It checks encrypted PK3 serialization, decoded checksum and identity, independent legality checks, rejection of eggs, and source immutability. Any refusal is a test failure, not a reason to relax legality. The job uploads no PCCS code or linked binaries. This is a test candidate until the job passes; no hardware or save transfer claims.

### First probe finding
[Run 45594895514](https://github.com/LittleCherryyy/PKTool/actions/runs/45594895514) built both libraries and linked the adapter; all three converted fixtures failed the PK3 checksum/identity gate. PCCS infers encryption from checksum mismatch. Fresh conversion output requires explicit checksum initialization, as in the upstream convertPKs example. The follow-up initializes this checksum before asking for canonical plaintext and checks both upstream serialization paths against PKTool. Results still require CI verification.
