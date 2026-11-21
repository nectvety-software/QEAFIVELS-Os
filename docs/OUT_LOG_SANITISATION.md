# Sanitised captured logs in out/

Date: 2026-11-20

The captured kernel-log copies under `out/` contained a vendor brand token
inside raw text and binary DTB/cmdline fragments. This project now uses
neutral identifiers. The copies below were patched in place with the
same-length replacement token `qeafvnd` (lower case), `Qeafvnd` (title case)
and `QEAFVND` (upper case). File sizes are unchanged.

These are derived, sanitised evidence copies, not byte-exact captures. The
SHA-256 values below are the original pre-sanitisation hashes so the change
can be audited without restoring the removed token.
Boot/backup images are deliberately excluded: their raw format token is
part of the image contract and must not be altered. The text-log copies
listed here remain sanitised.


| File (relative to repository) | Original SHA-256 |
| --- | --- |
| `out/boot-vqeaf-violet-ref-20261114-1325.img` | `4de5924f8024b971d9a69f230bbb9f401c61da1b407391db24718e56ee4229bf` |
| `out/p77-backup-20261114-061514.img` | `5dd899f8013d1df696c6862dc543c95a973585f92b05c2c990ed684832f76637` |
| `out/report-abi-v2-boot/HOMOLOG.TXT` | `5c4c6c9e4fb376076f065b9ed84f79d4cf5d6c56159d04173411d9ef131ae8e3` |
| `out/report-appbar-boot/HOMOLOG.TXT` | `a48a2cc2dc34378b14d4097f04f21f7166b1ecad93073af65e7095e97efa7c75` |
| `out/report-boot-latest/HOMOLOG.TXT` | `1294f75c31d70f151888496095346275c678d34d7ec8c24ec77ee0a370edf671` |
| `out/report-brand-boot/HOMOLOG.TXT` | `d4928e2414a4d9850b26d10beb73e88923277a8f7691621d48670d3739dbe81d` |
| `out/report-browser-boot/HOMOLOG.TXT` | `807f8d5cbddd2a8242ca8618aae5a8b762a7e50a0304d60c8cee9304e75fac62` |
| `out/report-flash-20261114/after-reboot/HOMOLOG.TXT` | `4471cb5b47e2e5ed4df80915e69c13cc0f7e41296ec2de8a9440b9c651b84fab` |
| `out/report-gboard-boot/HOMOLOG.TXT` | `9028465caac2b01b65fb4f86f39a39684e11c5878815ac87ee68b2815024f7b8` |
| `out/report-gesture-boot/HOMOLOG.TXT` | `ac9828fa41eee88b2a35a165aea6681c03656934e6d640d148f85b0943a3b796` |
| `out/report-kb-boot/HOMOLOG.TXT` | `51ba7b1b1cf514edc9d683e5a2d3b69d9d6d0b5214c2b15797815819509ec51f` |
| `out/report-kbclose-boot/HOMOLOG.TXT` | `49c28f290396235cb4cdea9e3baf5e842c99adb7a27cb68d43461eaa419c93e2` |
| `out/report-kbtheme-boot/HOMOLOG.TXT` | `fb6c7a0778ac80b92697d83f91c8158f42dde606ad96ba678261274ef4cc0f92` |
| `out/report-lightbt-boot/HOMOLOG.TXT` | `319cc096fa59d3cafa57796ff61155a8f972ec11b41f4e57c157808fc4983594` |
| `out/report-media-boot/HOMOLOG.TXT` | `96791a8e54a112e99e46fe60e76f5fb8ba31fbbaa5389caca0d2fbd92fbb68d8` |
| `out/report-multitask-boot/HOMOLOG.TXT` | `3ed5c9594acf5e6346de33fff9d26f3de1998ce7684d4934d10c4c541122c2a0` |
| `out/report-notes-boot/HOMOLOG.TXT` | `0a121483b01efeaf0d7af143d4ff810af64fa32723c48c7bb525da6b889f197d` |
| `out/report-power-boot/HOMOLOG.TXT` | `43b177d90926e9a0dbcd8a97cfca21e81dc83e9d55cbe527ece85a0f9682b8be` |
| `out/report-qeappfix-boot/HOMOLOG.TXT` | `d6c3fa8cdd08e652ba5ab134f7f946ceb1a5c19dcb98b8a9121a8c42077a1285` |
| `out/report-recents-boot/HOMOLOG.TXT` | `21eb17852f5df59634f24ec0f56ff5bd68f77127c4a94e9d4c3e9056c9db6530` |
| `out/report-search-boot/HOMOLOG.TXT` | `201bfe4da5950490c671ac1fe169d54a1e23e83a1042ec8d1272c7757850400d` |
| `out/report-sleep-boot/HOMOLOG.TXT` | `d293229bdceb5ae32b80ed9ced8f436cced6d469e4fe0d126fe50bd680f33460` |
| `out/report-splash-boot/HOMOLOG.TXT` | `f091b32ca33c894dd201581c3af5d7a66a5b9b165ac7f32e176bf95d05c6eb05` |
| `out/report-timeout-boot/HOMOLOG.TXT` | `d47cdbf4a2ede13b8d82d00a33326a277ef46084b57457540c6fae5e2ef8410c` |
| `out/report-volrepeat-boot/HOMOLOG.TXT` | `70cd11daa9a719f4719288c39c8b934037116d2dc3ccd0b8ac65fcfdb4984b7b` |
| `out/report-volume-boot/HOMOLOG.TXT` | `77c3f4cde1a70a16c56b24c1cb960a3815b3713c3a65536e8aa357730bd4097a` |
