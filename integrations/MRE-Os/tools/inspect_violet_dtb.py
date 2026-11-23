import struct
from pathlib import Path
ROOT=Path(r"D:\os-system\MRE-Os")
MAGIC=b"\xd0\x0d\xfe\xed"
def be32(b,o): return struct.unpack_from(">I",b,o)[0]
def cstr(v): return v.split(b"\0",1)[0].decode("utf-8","replace")
def parse_fdt(buf):
    total,os,ot=struct.unpack_from(">III",buf,4)
    ro=be32(buf,16);ver,last=struct.unpack_from(">II",buf,20)
    szs,szst=struct.unpack_from(">II",buf,32)
    if total<40 or total>len(buf) or ver<17 or last>ver: raise ValueError("invalid FDT header")
    ss=os;se=ss+szst;st=ot;ste=st+szs
    if se>total or ste>total or ro<40 or ro>=ss or (ro&7): raise ValueError("invalid FDT offsets")
    reservations=[];rp=ro
    while rp+16<=ss:
        address,size=struct.unpack_from(">QQ",buf,rp);rp+=16
        if address==0 and size==0:break
        reservations.append((hex(address),hex(size)))
    else:raise ValueError("unterminated reserve map")
    strings=buf[st:ste];roots=[];stack=[];p=ss
    while p<se:
        tok=be32(buf,p);p+=4
        if tok==1:
            end=buf.find(b"\0",p,se)
            if end<0:raise ValueError("bad node name")
            name=buf[p:end].decode("utf-8","replace");p=(end+4)&~3
            n={"name":name,"props":{},"children":[],"parent":stack[-1] if stack else None}
            (stack[-1]["children"] if stack else roots).append(n);stack.append(n)
        elif tok==2:
            if not stack:raise ValueError("unbalanced node")
            stack.pop()
        elif tok==3:
            ln,no=struct.unpack_from(">II",buf,p);p+=8
            if p+ln>se or no>=len(strings):raise ValueError("bad property")
            e=strings.find(b"\0",no)
            if e<0:raise ValueError("bad property name")
            stack[-1]["props"][strings[no:e].decode("utf-8","replace")]=buf[p:p+ln]
            p=(p+ln+3)&~3
        elif tok==4:pass
        elif tok==9:break
        else:raise ValueError(f"bad token {tok} at {p-4-ss}")
    if stack:raise ValueError("unterminated tree")
    return roots,total,reservations
def strings(n,k):
    v=n["props"].get(k,b"")
    return ", ".join(x.decode("utf-8","replace") for x in v.rstrip(b"\0").split(b"\0"))
def cells(b,n):
    x=0
    for i in range(n):x=(x<<32)|be32(b,i*4)
    return x
def reg(n):
    raw=n["props"].get("reg")
    if raw is None or n["parent"] is None:return []
    pp=n["parent"]["props"];ac=be32(pp["#address-cells"],0) if "#address-cells" in pp else 2
    sc=be32(pp["#size-cells"],0) if "#size-cells" in pp else 1
    stride=(ac+sc)*4
    if not stride or len(raw)%stride:return ["bad reg"]
    return [(hex(cells(raw[i:i+ac*4],ac)),hex(cells(raw[i+ac*4:i+stride],sc))) for i in range(0,len(raw),stride)]
def walk(nodes,path=""):
    for n in nodes:
        p=path+"/"+n["name"];yield p,n;yield from walk(n["children"],p)
def inspect(path,offset=None,label=""):
    data=Path(path).read_bytes()
    if offset is None:
        offsets=[];i=0
        while True:
            i=data.find(MAGIC,i)
            if i<0:break
            offsets.append(i);i+=1
    else:offsets=[offset]
    print(f"\n=== {label} ({len(data)} bytes) ===")
    for off in offsets:
        try:
            blob=data[off:];roots,total,reservations=parse_fdt(blob)
            out=ROOT/"build"/"analysis"/(label.lower().replace(" ","-")+".dtb")
            out.parent.mkdir(parents=True,exist_ok=True);out.write_bytes(blob[:total])
            print(f"valid FDT offset={off}, size={total}, extracted={out}")
            print("root compatible:",strings(roots[0],"compatible"))
            print("memory reservation map:",reservations)
            for pathn,n in walk(roots):
                comp=strings(n,"compatible");dtype=strings(n,"device_type")
                if pathn.endswith("/chosen") or dtype=="memory" or "reserved-memory" in pathn or any(x in comp for x in ("arm,gic","msm-geni","arm,armv8-timer")):
                    print("NODE",pathn)
                    if comp:print(" compatible:",comp)
                    if dtype:print(" device_type:",dtype)
                    rr=reg(n)
                    if rr:print(" reg:",rr)
                    for key in ("interrupts","interrupts-extended","#interrupt-cells","stdout-path","bootargs","status","phandle"):
                        if key in n["props"]:
                            v=n["props"][key]
                            if key.startswith("interrupts") and len(v)%4==0:print(" ",key,[hex(be32(v,i)) for i in range(0,len(v),4)])
                            elif key in ("#interrupt-cells","phandle"):print(" ",key,be32(v,0))
                            else:print(" ",key,cstr(v))
            return
        except Exception as e:print("candidate",off,"rejected:",e)
pre=ROOT/"third_party"/"reference"/"device_xiaomi_violet_redminote7pro"/"prebuilt"
inspect(pre/"Image.gz-dtb",label="violet-image-reference")
inspect(pre/"dtbo.img",offset=64,label="violet-dtbo-reference")
