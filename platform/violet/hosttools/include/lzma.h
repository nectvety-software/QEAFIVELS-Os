/* Empty stand-in: we deliberately do not link liblzma. The only use in
 * pd-mapper/json.c is the include itself plus the lzma_decomp() call, which
 * our stub refuses so compressed JSON files are skipped rather than faked. */
#ifndef QEAF_HOSTTOOLS_LZMA_H
#define QEAF_HOSTTOOLS_LZMA_H
#endif
