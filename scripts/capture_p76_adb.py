import subprocess, hashlib, json, os, sys, time
from pathlib import Path
from datetime import datetime, timezone

SERIAL="ca1af25d"
SIZE=64*1024*1024
CHUNK=16*1024*1024
SECTORS=32768
ROOT=Path(r"D:\QEAFIVELS-Os\build\p76")
OUT=ROOT/("backup-"+datetime.now().strftime("%Y%m%d-%H%M%S"))
ADB=["adb","-s",SERIAL]

def adb_text(*args):
    p=subprocess.run(ADB+list(args),stdout=subprocess.PIPE,stderr=subprocess.PIPE,text=True,timeout=30)
    if p.returncode: raise RuntimeError(p.stderr.strip())
    return p.stdout.strip()

def digest(path):
    h=hashlib.sha256()
    with path.open("rb") as f:
        for b in iter(lambda:f.read(1024*1024),b""): h.update(b)
    return h.hexdigest()

def identity():
    u=adb_text("shell","cat","/sys/class/block/mmcblk0p76/uevent")
    sectors=adb_text("shell","cat","/sys/class/block/mmcblk0p76/size")
    target=adb_text("shell","readlink","-f","/dev/block/bootdevice/by-name/boot")
    serial=adb_text("get-serialno")
    fields=dict(x.split("=",1) for x in u.splitlines() if "=" in x)
    expected={"DEVNAME":"mmcblk0p76","DEVTYPE":"partition","PARTN":"76","PARTNAME":"boot"}
    for k,v in expected.items():
        if fields.get(k)!=v: raise RuntimeError(f"identity mismatch {k}: {fields.get(k)}")
    if sectors!="131072" or target!="/dev/block/mmcblk0p76" or serial!=SERIAL:
        raise RuntimeError(f"geometry/serial mismatch sectors={sectors} target={target} serial={serial}")
    return {"serial":serial,"uevent":fields,"sectors":int(sectors),"resolved_boot":target}

def capture(folder):
    folder.mkdir(parents=True,exist_ok=False)
    rows=[]
    for i in range(4):
        skip=i*SECTORS
        # head limits stdout to the exact raw payload; toybox dd status text is excluded.
        remote=f"dd if=/dev/block/mmcblk0p76 bs=512 skip={skip} count={SECTORS} 2>/dev/null | head -c {CHUNK}"
        cmd=ADB+["exec-out","sh","-c",remote]
        dest=folder/f"P760000{i+1}.BIN"
        t=time.time()
        with dest.open("xb") as f:
            p=subprocess.Popen(cmd,stdout=subprocess.PIPE,stderr=subprocess.PIPE)
            while True:
                b=p.stdout.read(1024*1024)
                if not b: break
                f.write(b)
            err=p.stderr.read().decode("utf-8","replace")
            rc=p.wait(timeout=120)
            f.flush(); os.fsync(f.fileno())
        n=dest.stat().st_size
        if rc or n!=CHUNK: raise RuntimeError(f"chunk {i+1}: rc={rc} size={n} stderr={err}")
        h=digest(dest)
        rows.append({"file":dest.name,"offset_bytes":i*CHUNK,"size_bytes":n,"sha256":h})
        print(f"{folder.name}/{dest.name} {n} bytes SHA256 {h} ({time.time()-t:.1f}s)",flush=True)
    return rows

def main():
    ROOT.mkdir(parents=True,exist_ok=True)
    if OUT.exists(): raise RuntimeError("refusing existing output")
    before=identity()
    print("p76 identity PASS",json.dumps(before),flush=True)
    r1=capture(OUT/"pass1")
    middle=identity()
    r2=capture(OUT/"pass2")
    after=identity()
    if before!=middle or middle!=after: raise RuntimeError("p76 identity changed during capture")
    for a,b in zip(r1,r2):
        if a["sha256"]!=b["sha256"]: raise RuntimeError(f"two-pass mismatch: {a['file']}")
    final=OUT/"p76-backup.img"
    h1=hashlib.sha256()
    with final.open("xb") as dst:
        for row in r1:
            with (OUT/"pass1"/row["file"]).open("rb") as src:
                while True:
                    b=src.read(1024*1024)
                    if not b: break
                    dst.write(b); h1.update(b)
        dst.flush(); os.fsync(dst.fileno())
    h2=hashlib.sha256()
    for row in r2:
        with (OUT/"pass2"/row["file"]).open("rb") as src:
            for b in iter(lambda:src.read(1024*1024),b""): h2.update(b)
    if final.stat().st_size!=SIZE or h1.hexdigest()!=h2.hexdigest():
        raise RuntimeError("assembled size/hash verification failed")
    actual=digest(final)
    if actual!=h1.hexdigest(): raise RuntimeError("final artifact readback hash mismatch")
    manifest={
      "schema_version":1,
      "device":{"product":"violet","serial":SERIAL},
      "partition":{"name":"boot","index":76,"path":"/dev/block/mmcblk0p76","size_bytes":SIZE,"sectors":131072},
      "acquisition":{"method":"ADB root adb exec-out dd; source read-only","chunk_size_bytes":CHUNK,"chunk_count":4,"independent_reads":2,"created_at_utc":datetime.now(timezone.utc).isoformat()},
      "identity_before":before,"identity_after":after,
      "verification":{"four_chunks_exact_16MiB":True,"chunk_sha256_match_two_passes":True,"whole_image_sha256_match_two_passes":True,"final_file_rehashed":True},
      "pass1_chunks":r1,"pass2_chunks":r2,
      "backup":{"file":final.name,"size_bytes":SIZE,"sha256":actual}
    }
    mp=Path(str(final)+".manifest.json")
    mp.write_text(json.dumps(manifest,indent=2,sort_keys=True)+"\n",encoding="utf-8")
    print("BACKUP COMPLETE",json.dumps({"backup":str(final),"manifest":str(mp),"size_bytes":SIZE,"sha256":actual},indent=2),flush=True)

if __name__=="__main__":
    try: main()
    except Exception as e:
        print("BACKUP FAILED:",repr(e),file=sys.stderr,flush=True)
        raise
