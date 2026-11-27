#!/usr/bin/env python3
"""QEAF Android/Linux driver intake and port planner.

This tool never pretends that Linux/Android kernel C is directly executable by
QEAF. It inventories source, detects OS-bound APIs, preserves source unchanged,
and emits a QEAF-native port plan plus an explicit implementation skeleton.
"""
from __future__ import annotations
import argparse, hashlib, json, re, shutil
from pathlib import Path
from datetime import datetime, timezone

PATTERNS = {
 'android_userspace': [r'\b(property_get|__system_property_get|ALOG[VDIWEF]|android/log\.h|hw_get_module|hw_get_module_by_class|HMI|HAL_MODULE_INFO_SYM)\b', r'\b(android::|sp<|wp<|RefBase|Looper|AServiceManager|IServiceManager)'],
 'binder_hidl': [r'\b(Binder|binder|hwbinder|vndbinder|HIDL|AIDL|IServiceManager|BnHw|BpHw)\b'],
 'android_power': [r'\b(wake_lock|wake_unlock|wakeup_source|android_vh_|__pm_stay_awake|__pm_relax)\b'],
 'android_memory': [r'\b(ion_alloc|ion_import|ion_share|ion_client_create|ION_IOC|dma_buf|dma_heap|gralloc)\b'],
 'linux_kernel': [r'#\s*include\s*[<"]linux/', r'\b(platform_driver|module_init|module_exit|devm_kzalloc|devm_ioremap_resource|request_irq|dma_alloc_coherent|of_device_id|struct device)\b'],
 'qualcomm_ipc': [r'\b(qmi_handle|qmi_send|qrtr|rpmsg|glink|smem|smd|pil_|subsystem_restart|rproc_)\b'],
 'qualcomm_security': [r'\b(qseecom|scm_call|tz_app|hyp_assign|secure_monitor)\b'],
 'firmware_loading': [r'\b(request_firmware|firmware_request|firmware_class|MODULE_FIRMWARE)\b'],
 'android_logging': [r'\b(pr_err|pr_warn|pr_info|pr_debug|dev_err|dev_warn|dev_info|dev_dbg|trace_printk)\s*\('],
 'kernel_config': [r'\b(CONFIG_[A-Z0-9_]+)\b'],
}
RISK = {
 'android_userspace': ('BLOCKED','Userspace Android HAL/framework APIs require a QEAF service implementation.'),
 'binder_hidl': ('BLOCKED','Binder/HIDL/AIDL transport is not part of native QEAF; replace with QEAF IPC/service contracts.'),
 'android_power': ('REWRITE','Map wake locks to QEAF power-management leases with suspend/resume semantics.'),
 'android_memory': ('REWRITE','Map ION/dma-buf to QEAF DMA/IOMMU allocator; preserve ownership and cache rules.'),
 'linux_kernel': ('REWRITE','Linux driver model APIs require a QEAF bus/device/IRQ/DMA adapter or hardware-layer rewrite.'),
 'qualcomm_ipc': ('REVIEW','Validate exact QRTR/QMI/GLINK/SMEM wire protocol and lifecycle against target firmware.'),
 'qualcomm_security': ('BLOCKED','Secure-world calls require platform-specific ABI, signing and threat review.'),
 'firmware_loading': ('REWRITE','Use QEAF firmware catalog/loader with hash, board-ID and calibration checks.'),
 'android_logging': ('REWRITE','Replace logging calls with QEAF structured logging after choosing the QEAF logger ABI.'),
 'kernel_config': ('REVIEW','Translate Kconfig dependencies into QEAF build features and hardware capabilities.'),
}
EXTS={'.c','.h','.cc','.cpp','.S','.s','.dts','.dtsi','.mk','.txt'}

def sha256(p):
 h=hashlib.sha256()
 with p.open('rb') as f:
  for b in iter(lambda:f.read(1024*1024),b''): h.update(b)
 return h.hexdigest()

def main():
 ap=argparse.ArgumentParser(description=__doc__)
 ap.add_argument('source',type=Path,help='Android/Linux driver source directory or file')
 ap.add_argument('--name',default=None,help='QEAF driver name')
 ap.add_argument('--out',type=Path,default=Path('build/qeaf-driver-convert'))
 ap.add_argument('--max-files',type=int,default=10000)
 args=ap.parse_args(); src=args.source.resolve()
 if not src.exists(): ap.error(f'source does not exist: {src}')
 files=[src] if src.is_file() else sorted(p for p in src.rglob('*') if p.is_file() and p.suffix.lower() in EXTS)
 files=files[:args.max_files]
 name=args.name or (src.stem if src.is_file() else src.name)
 safe=re.sub(r'[^A-Za-z0-9_.-]+','_',name).strip('._') or 'imported_driver'
 out=(args.out/safe).resolve(); ref=out/'reference_source'; ref.mkdir(parents=True,exist_ok=True)
 findings={k:[] for k in PATTERNS}; file_records=[]; symbols=set()
 for p in files:
  try: data=p.read_bytes(); text=data.decode('utf-8',errors='replace')
  except OSError: continue
  rel=str(p.relative_to(src.parent if src.is_file() else src)).replace('\\','/')
  hits=[]
  for category, pats in PATTERNS.items():
   for pat in pats:
    for m in re.finditer(pat,text,re.I|re.M):
     line=text.count('\n',0,m.start())+1; token=m.group(0)[:100]
     findings[category].append({'file':rel,'line':line,'symbol':token}); hits.append(category)
  file_records.append({'path':rel,'bytes':len(data),'sha256':hashlib.sha256(data).hexdigest(),'categories':sorted(set(hits))})
  dest=ref/rel; dest.parent.mkdir(parents=True,exist_ok=True); shutil.copy2(p,dest)
 active={k:v for k,v in findings.items() if v}
 statuses={k:RISK[k][0] for k in active}
 blocked=sorted(k for k,v in statuses.items() if v=='BLOCKED')
 rewrites=sorted(k for k,v in statuses.items() if v=='REWRITE')
 overall='BLOCKED' if blocked else ('REQUIRES_PORT' if rewrites else 'REVIEW_REQUIRED')
 report={'schema':'qeaf-driver-port-plan/v1','created_utc':datetime.now(timezone.utc).isoformat(),'driver':safe,'source':str(src),'file_count':len(file_records),'files':file_records,'findings':active,'category_status':statuses,'overall_status':overall,'automatic_transformations':0,'source_modified':False,'target':'QEAF native driver ABI','notes':['No source code was rewritten automatically.','Linux kernel modules (.ko) are not source and cannot be converted by text rewriting.','Firmware blobs are copied only as references; compatibility, signature, board ID and calibration must be verified separately.']}
 (out/'port-plan.json').write_text(json.dumps(report,indent=2),encoding='utf-8')
 lines=[f'# QEAF port plan: {safe}','',f'- Source: `{src}`',f'- Files inventoried: **{len(file_records)}**',f'- Result: **{overall}**','- Automatic source transformations: **0** (intentionally safe)','', '## Required work']
 for cat in active:
  status,why=RISK[cat]; lines += [f'### {cat} — {status}',why]
  for hit in findings[cat][:30]: lines.append(f"- `{hit['file']}:{hit['line']}` — `{hit['symbol']}`")
 if not active: lines += ['No known Android/Linux API markers found. This is not proof of QEAF compatibility; review hardware, ABI and concurrency assumptions.']
 lines += ['', '## Conversion boundary','The converter preserves imported files under `reference_source/`. It does not rename Linux APIs, fabricate QEAF implementations, or claim a driver is converted. A port is accepted only after a QEAF implementation exists and host/AArch64/device tests pass.','']
 (out/'PORTING.md').write_text('\n'.join(lines),encoding='utf-8')
 (out/'qeaf_driver_skeleton.c').write_text(f'''/* SPDX-License-Identifier: MIT */\n/* Generated port skeleton for {safe}.\n * Implement only after reviewing PORTING.md and port-plan.json.\n * This file intentionally contains no fake hardware operations.\n */\n#include <stdint.h>\n\ntypedef struct {{ uint32_t abi; void *context; }} qeaf_driver_context_t;\n\nint qeaf_{re.sub(r"[^a-z0-9_]","_",safe.lower())}_probe(qeaf_driver_context_t *ctx) {{\n    if (!ctx || ctx->abi == 0) return -1;\n    /* TODO: bind verified QEAF bus, MMIO, IRQ, DMA and firmware services. */\n    return -2; /* -2: not implemented; never report a false successful probe. */\n}}\n\nvoid qeaf_{re.sub(r"[^a-z0-9_]","_",safe.lower())}_remove(qeaf_driver_context_t *ctx) {{\n    (void)ctx; /* TODO: quiesce hardware before releasing DMA/resources. */\n}}\n''',encoding='utf-8')
 print(json.dumps({'driver':safe,'source':str(src),'files':len(file_records),'status':overall,'blocked_categories':blocked,'rewrite_categories':rewrites,'output':str(out),'source_modified':False},indent=2))
 return 0
if __name__=='__main__': raise SystemExit(main())
