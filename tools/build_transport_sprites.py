#!/usr/bin/env python3
"""Build compact, pinned historic art packs for the optional Transporter.
Source: PokeAPI/sprites, same source as tools/fetch_sprites.py.
Stdlib-only PNG conversion for non-interlaced images at <= 8-bit samples.
"""
import concurrent.futures
import pathlib
import struct
import sys
import time
import urllib.request
import zlib

PIN = "c0542f7c51215d9672439012838c99f520596cbd"
BASE = "https://raw.githubusercontent.com/PokeAPI/sprites/" + PIN + "/sprites/pokemon/versions/"
KINDS = {
    "transport-gb": ("generation-i/red-blue/transparent", 151),
    "transport-gbc": ("generation-ii/crystal/transparent", 251),
    "transport-gbc-shiny": ("generation-ii/crystal/transparent/shiny", 251),
    "transport-gba": ("generation-iii/emerald", 251),
    "transport-gba-shiny": ("generation-iii/emerald/shiny", 251),
}

def paeth(a,b,c):
    p=a+b-c
    pa,pb,pc=abs(p-a),abs(p-b),abs(p-c)
    return a if pa<=pb and pa<=pc else b if pb<=pc else c

def convert(png):
    if png[:8]!=b"\x89PNG\r\n\x1a\n": raise ValueError("PNG magic")
    at=8; palette=b""; transparency=b""; data=bytearray()
    while at+12<=len(png):
        n=struct.unpack_from(">I",png,at)[0]; tag=png[at+4:at+8]; block=png[at+8:at+8+n]; at+=12+n
        if tag==b"IHDR":
            w,h,depth,typ,comp,filt,interlace=struct.unpack(">IIBBBBB",block)
        elif tag==b"PLTE": palette=block
        elif tag==b"tRNS": transparency=block
        elif tag==b"IDAT": data+=block
        elif tag==b"IEND": break
    channels={0:1,2:3,3:1,4:2,6:4}[typ]
    if not (1<=w<=128 and 1<=h<=128) or interlace or comp or filt or depth not in (1,2,4,8):
        raise ValueError("unsupported PNG geometry")
    if depth!=8 and typ not in (0,3): raise ValueError("unsupported PNG sample depth")
    stride=(w*channels*depth+7)//8; step=max(1,(channels*depth+7)//8)
    packed=zlib.decompress(data)
    if len(packed)!=h*(stride+1): raise ValueError("invalid PNG data")
    prev=bytearray(stride); pixels=[]; maxv=(1<<depth)-1
    for y in range(h):
        mode=packed[y*(stride+1)]; row=bytearray(packed[y*(stride+1)+1:(y+1)*(stride+1)])
        for i in range(stride):
            a=row[i-step] if i>=step else 0
            b=prev[i]; c=prev[i-step] if i>=step else 0
            if mode==1: row[i]=(row[i]+a)&255
            elif mode==2: row[i]=(row[i]+b)&255
            elif mode==3: row[i]=(row[i]+((a+b)//2))&255
            elif mode==4: row[i]=(row[i]+paeth(a,b,c))&255
            elif mode!=0: raise ValueError("PNG filter")
        for x in range(w):
            if typ in (0,3):
                v=(row[(x*depth)//8]>>(8-depth-(x*depth)%8))&maxv
                if typ==3:
                    r,g,b=palette[v*3:v*3+3]; alpha=transparency[v] if v<len(transparency) else 255
                else:
                    r=g=b=v*255//maxv
                    alpha=0 if len(transparency)==2 and v==struct.unpack(">H",transparency)[0] else 255
            elif typ==2:
                r,g,b=row[x*3:x*3+3]; alpha=0 if len(transparency)==6 and (r,g,b)==struct.unpack(">HHH",transparency) else 255
            elif typ==4: r=g=b=row[x*2]; alpha=row[x*2+1]
            else: r,g,b,alpha=row[x*4:x*4+4]
            pixels.append((0x8000|(r>>3)|((g>>3)<<5)|((b>>3)<<10)) if alpha>=128 else 0)
        prev=row
    xs=[i%w for i,p in enumerate(pixels) if p]; ys=[i//w for i,p in enumerate(pixels) if p]
    if not xs: raise ValueError("empty sprite")
    x0,x1,y0,y1=min(xs),max(xs)+1,min(ys),max(ys)+1
    cropped=[pixels[y*w+x] for y in range(y0,y1) for x in range(x0,x1)]
    return b"PKS1"+struct.pack("<HH",x1-x0,y1-y0)+struct.pack("<"+"H"*len(cropped),*cropped)

def get(job):
    kind, folder, species=job
    url=BASE+folder+"/"+str(species)+".png"
    last=None
    for attempt in range(3):
        try:
            with urllib.request.urlopen(url,timeout=20) as response: png=response.read(200000)
            return kind,species,convert(png),None
        except Exception as e:
            last=e;time.sleep(attempt+1)
    return kind,species,None,str(last)

def main():
    out=pathlib.Path(sys.argv[1]);out.mkdir(parents=True,exist_ok=True)
    jobs=[(kind,folder,i) for kind,(folder,count) in KINDS.items() for i in range(1,count+1)]
    packs={k:{} for k in KINDS}; failures=[]
    with concurrent.futures.ThreadPoolExecutor(max_workers=20) as pool:
        for kind,i,blob,error in pool.map(get,jobs):
            if error: failures.append((kind,i,error))
            else: packs[kind][i]=blob
    for kind,(_,count) in KINDS.items():
        offset=8+4*(count+2); offsets=[]; contents=bytearray()
        for i in range(count+1):
            offsets.append(offset+len(contents)); contents+=packs[kind].get(i,b"")
        offsets.append(offset+len(contents))
        (out/(kind+".pak")).write_bytes(b"PKSP"+struct.pack("<HH",count,0)+struct.pack("<"+"I"*len(offsets),*offsets)+contents)
        print(kind,len(packs[kind]),"/",count,flush=True)
    if failures:
        for f in failures[:20]: print("sprite fallback",f,flush=True)
        print("Missing art entries use existing base sprites:",len(failures),flush=True)
        if any(not packs[k] for k in KINDS): raise SystemExit("historical sprite pack missing entirely")
if __name__=="__main__": main()
