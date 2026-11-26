# VPT029 checkpoint

Stable hardware baseline remains VPT027 (df666c9). Candidate source 25b41e5b96ecd2d97b43fabc584d39168077b4e6; individual build workflow 4b252ada003c934f60e7a8fe330165cf0bbc9df8 (only workflow differs).
- Bank destination and native PK1/PK2 source to GBA, exact-image journal, per-entity locale, no item discard, no Bank-to-Bank conversion. Requires existing canonical Bank. Whole box up to 30, visual history, VPT027 animation and session cache preserved.
- French/German Gen I names corrected by exact independent codec round-trip. Japanese PK3 OT: FF at index 5, zero at index 6 per pinned PKHeX verifier. No PID/IV or encounter policy change.
- Corrected synthetic locale fixtures to set actual source language and assert original name encoding; CI pipefail ensures PKHeX failure is not masked by tee.
- Strict independent PKHeX regression: 36 valid, 0 invalid, 0 refused, 0 errors. Host integration and ARM9 compilation passed. Not comprehensive region/encounter coverage and not official provenance. DSi physical test pending.
- User owns only USA dumps; no non-USA dumps required.
- Individual candidate SHA256: b8e7d9fff690079820be02082f0e02fc9893b37b87fd6b76368fd598937bb288.
- Host: https://github.com/LittleCherryyy/PKTool/actions/runs/45752330353
- ARM9: https://github.com/LittleCherryyy/PKTool/actions/runs/45752330415
- Individual build: https://github.com/LittleCherryyy/PKTool/actions/runs/45752439774
