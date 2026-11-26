# VPT028 checkpoint — 2026-11-26

Stable hardware baseline: VPT027, df666c98db33febded737d2ff3b53296c75f6935.
Candidate branch: dev/vpt028-bank-box-history. User owns USA game dumps only; other region tests must use synthetic fixtures, not require ROM acquisition.

- Whole-box selection (Y), up to 30; result and progress pagination.
- Existing canonical PKTool Bank destination via SELECT; stored entities are converted PK3; native PK1/PK2 Bank-source conversion and initial Bank creation/migration are not implemented by Transporter. Original Bank withdrawal path remains available.
- History from existing exact-image archives, read only, sorted by archive filename rather than time.
- Host integration, whole-box Bank transfer, recovery, normal CI and ARM9 build-only passed. Physical retest pending.
- Independent PKHeX.Core audit pinned bfe89c74a3f5265805e364b79bc04ad38ebbc5ea: 36 synthetic cases, 24 valid, 6 invalid, 6 conversion refusals, 0 analysis errors.
- Japanese Gen I/II fixtures: final terminator missing (6 cases). French and German Gen I accented name fixtures: protected identity refused (6 cases); French ÉRIC/ÉCLAIR -> ïRIC/ïCLAIR, German BÄR -> B R. English, Italian, Spanish fixtures both gens; French/German Gen II fixtures passed. Not evidence of comprehensive language compatibility. Do not weaken protected identity or claim official provenance.
- Next: isolate exact Japanese terminator field and correct Gen I charset mapping, repeat independent audit; later cover more regional layouts/encounters and localized destination data. No changes to legal policy until independently reviewed.
- No VPT028 executable delivered in this turn. Build-only CI does not expose linked binary. Retain VPT027 for hardware use.

Host audit: https://github.com/LittleCherryyy/PKTool/actions/runs/45751101761
ARM9: https://github.com/LittleCherryyy/PKTool/actions/runs/45751176963
