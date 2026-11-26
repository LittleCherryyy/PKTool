#!/usr/bin/env python3
"""Fix the pinned PCCS Gen I/II -> Gen III LEGAL/FAITHFUL EV loop.
The original OR clause makes statExp==255 impossible to terminate.
Remove only that clause; ceil(sqrt(statExp)), EV rounding and caps are unchanged.
"""
import pathlib
import sys
p = pathlib.Path(sys.argv[1]) / "source/GBPokemon.cpp"
s = p.read_text()
old = "while((sqrt * sqrt) < statExp || statExp == 255)"
new = "while((sqrt * sqrt) < statExp)"
if s.count(old) != 1:
    raise SystemExit("Pinned PCCS Stat Exp loop changed; refusing patch")
p.write_text(s.replace(old, new))
print("PCCS Stat Exp=255 non-terminating loop fixed")
