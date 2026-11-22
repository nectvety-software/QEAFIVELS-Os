#!/usr/bin/env python3
"""QEAFRESCUE v2 file backup/restore and append-only recovery logs; no block API."""
from __future__ import annotations
import argparse, hashlib, json, os, stat, time, uuid, zipfile
from pathlib import Path, PurePosixPath

FORMAT="QEAFRESCUE-BACKUP"; VERSION=1; MANIFEST="QEAFRESCUE-MANIFEST.json"
MAX_FILES=100000; MAX_TOTAL=2*1024*1024*1024; CHUNK=1024*1024

class DataError(RuntimeError): pass

def sha256_file(path):
    h=hashlib.sha256()
    with Path(path).open("rb") as f:
        for block in iter(lambda:f.read(CHUNK),b""): h.update(block)
    return h.hexdigest()

def safe_relative(name):
    p=PurePosixPath(name)
    if not name or p.is_absolute() or "\\" in name or any(x in ("",".","..") for x in p.parts):
        raise DataError(f"unsafe archive path: {name!r}")
    if ":" in p.parts[0] or name==MANIFEST: raise DataError(f"reserved archive path: {name!r}")
    return p.as_posix()

def collect_files(source):
    source=Path(source)
    if source.is_symlink(): raise DataError("source cannot be a symlink")
    source=source.resolve(strict=True)
    if not source.is_dir(): raise DataError("source must be an ordinary directory")
    result=[]; total=0
    for root,dirs,files in os.walk(source,followlinks=False):
        base=Path(root)
        for d in list(dirs):
            if (base/d).is_symlink(): raise DataError(f"symlink directory rejected: {base/d}")
        for name in files:
            p=base/name
            if p.is_symlink() or not p.is_file(): raise DataError(f"non-regular file rejected: {p}")
            rel=safe_relative(p.relative_to(source).as_posix()); total+=p.stat().st_size
            if len(result)>=MAX_FILES or total>MAX_TOTAL: raise DataError("backup exceeds safety limits")
            result.append((p,rel))
    return sorted(result,key=lambda x:x[1])

def create_backup(source,archive):
    source=Path(source)
    if source.is_symlink(): raise DataError("source cannot be a symlink")
    source=source.resolve(strict=True); archive=Path(archive).resolve()
    if archive==source or source in archive.parents: raise DataError("archive must be outside source")
    if archive.exists(): raise DataError("archive exists; overwrite refused")
    archive.parent.mkdir(parents=True,exist_ok=True); files=collect_files(source)
    entries=[{"path":r,"size":p.stat().st_size,"sha256":sha256_file(p)} for p,r in files]
    manifest={"format":FORMAT,"version":VERSION,"created_unix":int(time.time()),
      "source_label":source.name,"file_count":len(entries),
      "total_bytes":sum(x["size"] for x in entries),"files":entries}
    tmp=archive.with_name(archive.name+"."+uuid.uuid4().hex+".tmp")
    try:
        with zipfile.ZipFile(tmp,"w",zipfile.ZIP_DEFLATED,compresslevel=6,allowZip64=True) as z:
            for p,r in files: z.write(p,r)
            z.writestr(MANIFEST,json.dumps(manifest,ensure_ascii=False,sort_keys=True,indent=2).encode("utf-8"))
        os.replace(tmp,archive)
    finally:
        if tmp.exists(): tmp.unlink()
    return {"archive":str(archive),"file_count":len(entries),"total_bytes":manifest["total_bytes"],"archive_sha256":sha256_file(archive)}

def inspect_backup(archive):
    archive=Path(archive)
    if archive.is_symlink() or not archive.is_file(): raise DataError("archive missing or not regular file")
    try:
        with zipfile.ZipFile(archive,"r") as z:
            infos=z.infolist(); names=[i.filename for i in infos]
            if len(names)!=len(set(names)) or MANIFEST not in names: raise DataError("duplicate entries or manifest missing")
            if len(infos)>MAX_FILES+1: raise DataError("too many archive entries")
            for i in infos:
                if i.filename==MANIFEST: continue
                safe_relative(i.filename)
                mode=(i.external_attr>>16)&0xF000
                if mode not in (0,stat.S_IFREG) or i.is_dir(): raise DataError("non-regular archive entry rejected")
            m=json.loads(z.read(MANIFEST).decode("utf-8"))
            if m.get("format")!=FORMAT or m.get("version")!=VERSION: raise DataError("unsupported manifest")
            entries=m.get("files")
            if not isinstance(entries,list) or len(entries)!=m.get("file_count"): raise DataError("manifest count mismatch")
            expected=set(); total=0
            for item in entries:
                rel=safe_relative(item.get("path",""))
                if rel in expected: raise DataError("duplicate manifest path")
                expected.add(rel); size=item.get("size"); digest=item.get("sha256")
                if not isinstance(size,int) or size<0 or not isinstance(digest,str) or len(digest)!=64: raise DataError("invalid metadata")
                total+=size
                if total>MAX_TOTAL: raise DataError("restore exceeds 2 GiB limit")
                h=hashlib.sha256(); seen=0
                with z.open(rel,"r") as stream:
                    for block in iter(lambda:stream.read(CHUNK),b""):
                        seen+=len(block)
                        if seen>size: raise DataError(f"size overflow: {rel}")
                        h.update(block)
                if seen!=size or h.hexdigest()!=digest: raise DataError(f"hash/size mismatch: {rel}")
            if set(names)-{MANIFEST}!=expected or total!=m.get("total_bytes"): raise DataError("inventory mismatch")
            bad=z.testzip()
            if bad: raise DataError(f"ZIP CRC failure: {bad}")
            return {"archive":str(archive.resolve()),"file_count":len(entries),"total_bytes":total,
                    "archive_sha256":sha256_file(archive),"manifest":m}
    except (zipfile.BadZipFile,UnicodeDecodeError,json.JSONDecodeError,KeyError) as e:
        raise DataError(f"invalid archive: {e}") from e

def restore_backup(archive,destination,execute=False):
    report=inspect_backup(archive); dest=Path(destination).resolve()
    if dest.exists(): raise DataError("destination exists; overwrite/merge refused")
    if not execute: return {"status":"DRY_RUN","destination":str(dest),"file_count":report["file_count"],"total_bytes":report["total_bytes"]}
    dest.parent.mkdir(parents=True,exist_ok=True); stage=dest.with_name(dest.name+"."+uuid.uuid4().hex+".staging")
    try:
        stage.mkdir()
        with zipfile.ZipFile(archive,"r") as z:
            for item in report["manifest"]["files"]:
                rel=safe_relative(item["path"]); target=stage.joinpath(*PurePosixPath(rel).parts)
                target.parent.mkdir(parents=True,exist_ok=True)
                with z.open(rel,"r") as src,target.open("xb") as out:
                    while True:
                        block=src.read(CHUNK)
                        if not block: break
                        out.write(block)
                if target.stat().st_size!=item["size"] or sha256_file(target)!=item["sha256"]:
                    raise DataError(f"post-restore verification failed: {rel}")
        os.rename(stage,dest)
    except Exception:
        if stage.exists():
            import shutil; shutil.rmtree(stage,ignore_errors=True)
        raise
    return {"status":"RESTORED_VERIFIED","destination":str(dest),"file_count":report["file_count"],"total_bytes":report["total_bytes"]}

def append_recovery_event(log_dir,event,status="INFO",detail=""):
    if not event.strip() or len(event)>80 or len(detail)>2000: raise DataError("event/detail length invalid")
    if status not in ("INFO","OK","WARN","ERROR","BLOCKED"): raise DataError("invalid status")
    d=Path(log_dir); d.mkdir(parents=True,exist_ok=True)
    if d.is_symlink(): raise DataError("log directory cannot be symlink")
    session=os.environ.get("QEAFRESCUE_SESSION") or str(uuid.uuid4())
    record={"timestamp_unix_ms":int(time.time()*1000),"session":session,"event":event,"status":status,"detail":detail}
    path=d/"recovery.jsonl"
    with path.open("a",encoding="utf-8",newline="\n") as f:
        f.write(json.dumps(record,ensure_ascii=False,sort_keys=True)+"\n"); f.flush(); os.fsync(f.fileno())
    return {"log":str(path.resolve()),"session":session,"status":status}

def main(argv=None):
    p=argparse.ArgumentParser(description=__doc__); s=p.add_subparsers(dest="cmd",required=True)
    b=s.add_parser("backup"); b.add_argument("--source",type=Path,required=True); b.add_argument("--archive",type=Path,required=True)
    v=s.add_parser("verify"); v.add_argument("--archive",type=Path,required=True)
    r=s.add_parser("restore"); r.add_argument("--archive",type=Path,required=True); r.add_argument("--destination",type=Path,required=True); r.add_argument("--execute",action="store_true")
    l=s.add_parser("log"); l.add_argument("--dir",type=Path,required=True); l.add_argument("--event",required=True); l.add_argument("--status",choices=("INFO","OK","WARN","ERROR","BLOCKED"),default="INFO"); l.add_argument("--detail",default="")
    a=p.parse_args(argv)
    try:
        if a.cmd=="backup": result=create_backup(a.source,a.archive)
        elif a.cmd=="verify":
            result=inspect_backup(a.archive); result.pop("manifest",None)
        elif a.cmd=="restore": result=restore_backup(a.archive,a.destination,a.execute)
        else: result=append_recovery_event(a.dir,a.event,a.status,a.detail)
        if a.cmd!="log":
            append_recovery_event(Path.cwd()/"recovery-logs",a.cmd.upper()+"_COMPLETE","OK",
                                  json.dumps({k:v for k,v in result.items() if k!="manifest"},ensure_ascii=False))
        print(json.dumps(result,ensure_ascii=False,indent=2)); return 0
    except (DataError,OSError) as e:
        if a.cmd!="log":
            try: append_recovery_event(Path.cwd()/"recovery-logs",a.cmd.upper()+"_FAILED","ERROR",str(e)[:2000])
            except OSError: pass
        print(f"QEAFRESCUE DATA ERROR: {e}"); return 2

if __name__=="__main__": raise SystemExit(main())
