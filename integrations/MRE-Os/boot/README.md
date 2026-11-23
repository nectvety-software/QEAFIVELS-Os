# MRE Boot

The target is a native MRE boot manager and an independent AArch64 runtime. Qualcomm PBL is mask ROM and cannot be replaced. Any attempt to replace a later boot stage or write a boot-related partition requires the exact Violet GPT, bootloader/AVB policy, matching stock recovery images and a separately approved write plan.

The current project intentionally performs read-only device inspection only. It has no partition writer, no flash/erase script and no bootloader replacement binary. Do not infer partition names or addresses from generic SM6150/IDP sources.

See [BOOT_CHAIN_PLAN.md](BOOT_CHAIN_PLAN.md) for required gates and current blockers.
