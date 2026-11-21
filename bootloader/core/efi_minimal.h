#ifndef VQEAF_EFI_MINIMAL_H
#define VQEAF_EFI_MINIMAL_H
#include "efi_types.h"
typedef struct EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL;
typedef EFI_STATUS (EFIAPI *EFI_TEXT_STRING)(EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL*,CHAR16*);
struct EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL { void *Reset; EFI_TEXT_STRING OutputString; void *TestString,*QueryMode,*SetMode,*SetAttribute,*ClearScreen,*SetCursorPosition,*EnableCursor,*Mode; };
typedef void *EFI_EVENT;
typedef struct EFI_SIMPLE_TEXT_INPUT_PROTOCOL EFI_SIMPLE_TEXT_INPUT_PROTOCOL;
typedef struct { UINT16 ScanCode; CHAR16 UnicodeChar; } EFI_INPUT_KEY;
typedef EFI_STATUS (EFIAPI *EFI_INPUT_READ_KEY)(EFI_SIMPLE_TEXT_INPUT_PROTOCOL*,EFI_INPUT_KEY*);
struct EFI_SIMPLE_TEXT_INPUT_PROTOCOL { void *Reset; EFI_INPUT_READ_KEY ReadKey; EFI_EVENT WaitForKey; };
#define EFI_SCAN_UP 0x01
#define EFI_SCAN_DOWN 0x02
#define EFI_SCAN_RIGHT 0x03
#define EFI_SCAN_LEFT 0x04
typedef struct EFI_SYSTEM_TABLE {
 EFI_TABLE_HEADER Hdr; CHAR16 *FirmwareVendor; UINT32 FirmwareRevision;
 EFI_HANDLE ConsoleInHandle; EFI_SIMPLE_TEXT_INPUT_PROTOCOL *ConIn;
 EFI_HANDLE ConsoleOutHandle;
 EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL *ConOut; EFI_HANDLE StandardErrorHandle;
 void *StdErr,*RuntimeServices; void *BootServices;
 UINTN NumberOfTableEntries; void *ConfigurationTable;
} EFI_SYSTEM_TABLE;
typedef struct {
 EFI_TABLE_HEADER Hdr;
 void *RaiseTPL,*RestoreTPL,*AllocatePages,*FreePages,*GetMemoryMap,*AllocatePool,*FreePool;
 void *CreateEvent,*SetTimer,*WaitForEvent,*SignalEvent,*CloseEvent,*CheckEvent;
 void *InstallProtocolInterface,*ReinstallProtocolInterface,*UninstallProtocolInterface,*HandleProtocol;
 void *Reserved,*RegisterProtocolNotify,*LocateHandle,*LocateDevicePath,*InstallConfigurationTable;
 void *LoadImage,*StartImage,*Exit,*UnloadImage,*ExitBootServices,*GetNextMonotonicCount,*Stall,*SetWatchdogTimer;
 void *ConnectController,*DisconnectController,*OpenProtocol,*CloseProtocol,*OpenProtocolInformation,*ProtocolsPerHandle,*LocateHandleBuffer,*LocateProtocol;
 void *InstallMultipleProtocolInterfaces,*UninstallMultipleProtocolInterfaces,*CalculateCrc32,*CopyMem,*SetMem,*CreateEventEx;
} EFI_BOOT_SERVICES;
typedef EFI_STATUS (EFIAPI *EFI_HANDLE_PROTOCOL)(EFI_HANDLE,const EFI_GUID*,void**);
typedef EFI_STATUS (EFIAPI *EFI_LOAD_IMAGE)(UINT8,EFI_HANDLE,void*,void*,UINTN,EFI_HANDLE*);
typedef EFI_STATUS (EFIAPI *EFI_START_IMAGE)(EFI_HANDLE,UINTN*,CHAR16**);
typedef EFI_STATUS (EFIAPI *EFI_ALLOCATE_POOL)(UINT32,UINTN,void**);
typedef struct {
 UINT32 Revision; EFI_HANDLE ParentHandle; EFI_SYSTEM_TABLE *SystemTable; EFI_HANDLE DeviceHandle;
 void *FilePath,*Reserved; UINT32 LoadOptionsSize; void *LoadOptions; void *ImageBase;
 UINT64 ImageSize; UINT32 ImageCodeType,ImageDataType; void *Unload;
} EFI_LOADED_IMAGE_PROTOCOL;
typedef struct EFI_FILE_PROTOCOL EFI_FILE_PROTOCOL;
typedef EFI_STATUS (EFIAPI *EFI_FILE_OPEN)(EFI_FILE_PROTOCOL*,EFI_FILE_PROTOCOL**,CHAR16*,UINT64,UINT64);
typedef EFI_STATUS (EFIAPI *EFI_FILE_CLOSE)(EFI_FILE_PROTOCOL*);
typedef EFI_STATUS (EFIAPI *EFI_FILE_READ)(EFI_FILE_PROTOCOL*,UINTN*,void*);
struct EFI_FILE_PROTOCOL {
 UINT64 Revision; EFI_FILE_OPEN Open; EFI_FILE_CLOSE Close; void *Delete;
 EFI_FILE_READ Read; void *Write,*GetPosition,*SetPosition,*GetInfo,*SetInfo,*Flush;
};
typedef struct { UINT64 Revision; EFI_STATUS (EFIAPI *OpenVolume)(void*,EFI_FILE_PROTOCOL**); } EFI_SIMPLE_FILE_SYSTEM_PROTOCOL;
/* --- UEFI Graphics Output Protocol (UEFI 2.x, EDK2 GraphicsOutput.h) --- */
#define EFI_GOP_PIXEL_RGB 0u  /* bytes R,G,B,reserved */
#define EFI_GOP_PIXEL_BGR 1u  /* bytes B,G,R,reserved */
#define EFI_GOP_PIXEL_BITMASK 2u
#define EFI_GOP_PIXEL_BLT_ONLY 3u
typedef struct {
 UINT32 Version,HorizontalResolution,VerticalResolution,PixelFormat;
 UINT32 RedMask,GreenMask,BlueMask,ReservedMask;
 UINT32 PixelsPerScanLine;
} EFI_GOP_MODE_INFO;
typedef struct {
 UINT32 MaxMode,Mode;
 EFI_GOP_MODE_INFO *Info;
 UINTN SizeOfInfo;
 UINT64 FrameBufferBase;
 UINTN FrameBufferSize;
} EFI_GOP_MODE;
struct EFI_GRAPHICS_OUTPUT_PROTOCOL;
typedef EFI_STATUS (EFIAPI *EFI_GOP_QUERY_MODE)(struct EFI_GRAPHICS_OUTPUT_PROTOCOL*,UINT32,UINTN*,EFI_GOP_MODE_INFO**);
typedef EFI_STATUS (EFIAPI *EFI_GOP_SET_MODE)(struct EFI_GRAPHICS_OUTPUT_PROTOCOL*,UINT32);
typedef struct EFI_GRAPHICS_OUTPUT_PROTOCOL {
 EFI_GOP_QUERY_MODE QueryMode; EFI_GOP_SET_MODE SetMode; void *Blt; EFI_GOP_MODE *Mode;
} EFI_GRAPHICS_OUTPUT_PROTOCOL;
typedef struct EFI_GRAPHICS_OUTPUT_PROTOCOL EFI_GRAPHICS_OUTPUT_PROTOCOL;
typedef EFI_STATUS (EFIAPI *EFI_FREE_POOL)(void*);
typedef EFI_STATUS (EFIAPI *EFI_LOCATE_PROTOCOL)(const EFI_GUID*,void*,void**);
typedef EFI_STATUS (EFIAPI *EFI_STALL)(UINTN);
#define BS(s) ((EFI_BOOT_SERVICES*)((s)->BootServices))
#endif
