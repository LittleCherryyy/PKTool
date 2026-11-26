#!/usr/bin/env python3
"""Apply one audited compatibility fix to the disposable pinned PCCS checkout.
Language IDs skip 6 (Spanish=7); FR/DE and IT/ES share separate GB tables.
Original index incorrectly addresses Korean/Japanese for Spanish and swaps IT/DE.
The conversion algorithms, PID search and legality checks are unchanged.
"""
import pathlib
import sys
p=pathlib.Path(sys.argv[1])/"source/pokemon_data.cpp"
s=p.read_text()
old="""const u8 lang_index =
        localization_charset_indices[(gen - 1) * 7 + (lang - 1)];"""
new="""const u8 lang_index =
        lang == JAPANESE || lang == KOREAN ? 0 :
        gen == 3 || lang == ENGLISH ? 1 :
        lang == FRENCH || lang == GERMAN ? 2 : 3;"""
if s.count(old)!=1: raise SystemExit("Pinned PCCS charset indexing changed; refusing patch")
p.write_text(s.replace(old,new))
print("PCCS charset mapping fixed for sparse language IDs")
