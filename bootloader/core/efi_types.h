#ifndef VQEAF_EFI_TYPES_H
#define VQEAF_EFI_TYPES_H
#include <stdint.h>
#include <stddef.h>
typedef uint64_t EFI_STATUS;
typedef void *EFI_HANDLE;
typedef uint16_t CHAR16;
typedef uint64_t UINTN;
typedef uint32_t UINT32;
typedef uint16_t UINT16;
typedef uint64_t UINT64;
typedef uint8_t UINT8;
typedef struct { uint8_t b[16]; } EFI_GUID;
typedef struct { uint64_t Signature; uint32_t Revision,HeaderSize,CRC32,Reserved; } EFI_TABLE_HEADER;
#define EFI_SUCCESS 0
#define EFI_ERROR(s) (((s)&0x8000000000000000ULL)!=0)
#define EFI_LOAD_ERROR 0x8000000000000001ULL
#define EFI_BUFFER_TOO_SMALL 0x8000000000000005ULL
#define EFI_FILE_MODE_READ 1ULL
#define EFI_LOADER_DATA 2
#define EFIAPI __attribute__((ms_abi))
#endif
