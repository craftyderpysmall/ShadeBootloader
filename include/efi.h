/*
 * Minimal, self-contained UEFI definitions for Shade.
 * Only what the bootloader uses is spelled out; unused services are
 * declared as opaque pointers so table layouts stay spec-exact.
 */
#ifndef SHADE_EFI_H
#define SHADE_EFI_H

#include <stddef.h>
#include <stdint.h>

#define EFIAPI __attribute__((ms_abi))

typedef uint8_t   UINT8;
typedef uint16_t  UINT16;
typedef uint32_t  UINT32;
typedef uint64_t  UINT64;
typedef int8_t    INT8;
typedef int16_t   INT16;
typedef int32_t   INT32;
typedef int64_t   INT64;
typedef uint64_t  UINTN;
typedef int64_t   INTN;
typedef uint8_t   BOOLEAN;
typedef uint16_t  CHAR16;
typedef void      VOID;
typedef UINTN     EFI_STATUS;
typedef void     *EFI_HANDLE;
typedef void     *EFI_EVENT;
typedef UINT64    EFI_PHYSICAL_ADDRESS;
typedef UINTN     EFI_TPL;

#define TRUE  1
#define FALSE 0

typedef struct {
    UINT32 Data1;
    UINT16 Data2;
    UINT16 Data3;
    UINT8  Data4[8];
} EFI_GUID;

#define EFI_ERR(x)              (0x8000000000000000ULL | (x))
#define EFI_SUCCESS             0
#define EFI_LOAD_ERROR          EFI_ERR(1)
#define EFI_INVALID_PARAMETER   EFI_ERR(2)
#define EFI_UNSUPPORTED         EFI_ERR(3)
#define EFI_BUFFER_TOO_SMALL    EFI_ERR(5)
#define EFI_NOT_READY           EFI_ERR(6)
#define EFI_DEVICE_ERROR        EFI_ERR(7)
#define EFI_OUT_OF_RESOURCES    EFI_ERR(9)
#define EFI_NOT_FOUND           EFI_ERR(14)
#define EFI_ACCESS_DENIED       EFI_ERR(15)
#define EFI_ABORTED             EFI_ERR(21)
#define EFI_SECURITY_VIOLATION  EFI_ERR(26)
#define EFI_ERROR(s)            (((INTN)(s)) < 0)

typedef struct {
    UINT64 Signature;
    UINT32 Revision;
    UINT32 HeaderSize;
    UINT32 CRC32;
    UINT32 Reserved;
} EFI_TABLE_HEADER;

typedef struct {
    UINT16 Year;
    UINT8  Month, Day, Hour, Minute, Second, Pad1;
    UINT32 Nanosecond;
    INT16  TimeZone;
    UINT8  Daylight, Pad2;
} EFI_TIME;

/* ---- Memory ---------------------------------------------------------- */

typedef enum {
    EfiReservedMemoryType, EfiLoaderCode, EfiLoaderData, EfiBootServicesCode,
    EfiBootServicesData, EfiRuntimeServicesCode, EfiRuntimeServicesData,
    EfiConventionalMemory,
} EFI_MEMORY_TYPE;

/* ---- Device paths ---------------------------------------------------- */

typedef struct {
    UINT8 Type;
    UINT8 SubType;
    UINT8 Length[2];
} EFI_DEVICE_PATH;

#define DP_TYPE_HARDWARE   0x01
#define DP_TYPE_ACPI       0x02
#define DP_TYPE_MESSAGING  0x03
#define DP_TYPE_MEDIA      0x04
#define DP_TYPE_BBS        0x05
#define DP_TYPE_END        0x7f
#define DP_SUB_END_ENTIRE  0xff

#define DP_SUB_MSG_USB     0x05
#define DP_SUB_MSG_MAC     0x0b
#define DP_SUB_MSG_IPV4    0x0c
#define DP_SUB_MSG_IPV6    0x0d
#define DP_SUB_MSG_URI     0x18
#define DP_SUB_MEDIA_HD    0x01
#define DP_SUB_MEDIA_CDROM 0x02
#define DP_SUB_MEDIA_FILE  0x04
#define DP_SUB_MEDIA_FVFILE 0x06
#define DP_SUB_MEDIA_FV    0x07

#pragma pack(push, 1)
typedef struct {
    EFI_DEVICE_PATH Header;
    UINT32 PartitionNumber;
    UINT64 PartitionStart;
    UINT64 PartitionSize;
    UINT8  Signature[16];
    UINT8  MBRType;
    UINT8  SignatureType;   /* 1 = MBR, 2 = GUID */
} HARDDRIVE_DEVICE_PATH;

typedef struct {
    EFI_DEVICE_PATH Header;
    CHAR16 PathName[1];
} FILEPATH_DEVICE_PATH;
#pragma pack(pop)

/* ---- Console input --------------------------------------------------- */

typedef struct {
    UINT16 ScanCode;
    CHAR16 UnicodeChar;
} EFI_INPUT_KEY;

#define SCAN_NULL      0x00
#define SCAN_UP        0x01
#define SCAN_DOWN      0x02
#define SCAN_RIGHT     0x03
#define SCAN_LEFT      0x04
#define SCAN_HOME      0x05
#define SCAN_END       0x06
#define SCAN_DELETE    0x08
#define SCAN_F1        0x0b
#define SCAN_F2        0x0c
#define SCAN_F3        0x0d
#define SCAN_F10       0x14
#define SCAN_ESC       0x17

typedef struct EFI_SIMPLE_TEXT_INPUT_PROTOCOL {
    EFI_STATUS (EFIAPI *Reset)(struct EFI_SIMPLE_TEXT_INPUT_PROTOCOL *, BOOLEAN);
    EFI_STATUS (EFIAPI *ReadKeyStroke)(struct EFI_SIMPLE_TEXT_INPUT_PROTOCOL *, EFI_INPUT_KEY *);
    EFI_EVENT WaitForKey;
} EFI_SIMPLE_TEXT_INPUT_PROTOCOL;

#define EFI_SHIFT_STATE_VALID 0x80000000
#define EFI_RIGHT_SHIFT_PRESSED 0x00000001
#define EFI_LEFT_SHIFT_PRESSED  0x00000002

typedef struct {
    UINT32 KeyShiftState;
    UINT8  KeyToggleState;
} EFI_KEY_STATE;

typedef struct {
    EFI_INPUT_KEY Key;
    EFI_KEY_STATE KeyState;
} EFI_KEY_DATA;

typedef struct EFI_SIMPLE_TEXT_INPUT_EX_PROTOCOL {
    EFI_STATUS (EFIAPI *Reset)(struct EFI_SIMPLE_TEXT_INPUT_EX_PROTOCOL *, BOOLEAN);
    EFI_STATUS (EFIAPI *ReadKeyStrokeEx)(struct EFI_SIMPLE_TEXT_INPUT_EX_PROTOCOL *, EFI_KEY_DATA *);
    EFI_EVENT WaitForKeyEx;
    void *SetState;
    void *RegisterKeyNotify;
    void *UnregisterKeyNotify;
} EFI_SIMPLE_TEXT_INPUT_EX_PROTOCOL;

/* ---- Console output -------------------------------------------------- */

typedef struct {
    INT32 MaxMode, Mode, Attribute, CursorColumn, CursorRow;
    BOOLEAN CursorVisible;
} SIMPLE_TEXT_OUTPUT_MODE;

typedef struct EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL {
    EFI_STATUS (EFIAPI *Reset)(struct EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL *, BOOLEAN);
    EFI_STATUS (EFIAPI *OutputString)(struct EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL *, CHAR16 *);
    void *TestString;
    void *QueryMode;
    void *SetMode;
    EFI_STATUS (EFIAPI *SetAttribute)(struct EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL *, UINTN);
    EFI_STATUS (EFIAPI *ClearScreen)(struct EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL *);
    EFI_STATUS (EFIAPI *SetCursorPosition)(struct EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL *, UINTN, UINTN);
    EFI_STATUS (EFIAPI *EnableCursor)(struct EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL *, BOOLEAN);
    SIMPLE_TEXT_OUTPUT_MODE *Mode;
} EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL;

/* ---- Graphics output ------------------------------------------------- */

typedef enum {
    PixelRedGreenBlueReserved8BitPerColor,
    PixelBlueGreenRedReserved8BitPerColor,
    PixelBitMask,
    PixelBltOnly,
} EFI_GRAPHICS_PIXEL_FORMAT;

typedef struct {
    UINT32 RedMask, GreenMask, BlueMask, ReservedMask;
} EFI_PIXEL_BITMASK;

typedef struct {
    UINT32 Version;
    UINT32 HorizontalResolution;
    UINT32 VerticalResolution;
    EFI_GRAPHICS_PIXEL_FORMAT PixelFormat;
    EFI_PIXEL_BITMASK PixelInformation;
    UINT32 PixelsPerScanLine;
} EFI_GRAPHICS_OUTPUT_MODE_INFORMATION;

typedef struct {
    UINT32 MaxMode;
    UINT32 Mode;
    EFI_GRAPHICS_OUTPUT_MODE_INFORMATION *Info;
    UINTN SizeOfInfo;
    EFI_PHYSICAL_ADDRESS FrameBufferBase;
    UINTN FrameBufferSize;
} EFI_GRAPHICS_OUTPUT_PROTOCOL_MODE;

typedef enum {
    EfiBltVideoFill,
    EfiBltVideoToBltBuffer,
    EfiBltBufferToVideo,
    EfiBltVideoToVideo,
} EFI_GRAPHICS_OUTPUT_BLT_OPERATION;

typedef struct EFI_GRAPHICS_OUTPUT_PROTOCOL {
    EFI_STATUS (EFIAPI *QueryMode)(struct EFI_GRAPHICS_OUTPUT_PROTOCOL *, UINT32, UINTN *,
                                   EFI_GRAPHICS_OUTPUT_MODE_INFORMATION **);
    EFI_STATUS (EFIAPI *SetMode)(struct EFI_GRAPHICS_OUTPUT_PROTOCOL *, UINT32);
    EFI_STATUS (EFIAPI *Blt)(struct EFI_GRAPHICS_OUTPUT_PROTOCOL *, UINT32 *BltBuffer,
                             EFI_GRAPHICS_OUTPUT_BLT_OPERATION, UINTN SrcX, UINTN SrcY,
                             UINTN DstX, UINTN DstY, UINTN Width, UINTN Height, UINTN Delta);
    EFI_GRAPHICS_OUTPUT_PROTOCOL_MODE *Mode;
} EFI_GRAPHICS_OUTPUT_PROTOCOL;

/* ---- Pointers -------------------------------------------------------- */

typedef struct {
    INT32 RelativeMovementX, RelativeMovementY, RelativeMovementZ;
    BOOLEAN LeftButton, RightButton;
} EFI_SIMPLE_POINTER_STATE;

typedef struct {
    UINT64 ResolutionX, ResolutionY, ResolutionZ;
    BOOLEAN LeftButton, RightButton;
} EFI_SIMPLE_POINTER_MODE;

typedef struct EFI_SIMPLE_POINTER_PROTOCOL {
    EFI_STATUS (EFIAPI *Reset)(struct EFI_SIMPLE_POINTER_PROTOCOL *, BOOLEAN);
    EFI_STATUS (EFIAPI *GetState)(struct EFI_SIMPLE_POINTER_PROTOCOL *, EFI_SIMPLE_POINTER_STATE *);
    EFI_EVENT WaitForInput;
    EFI_SIMPLE_POINTER_MODE *Mode;
} EFI_SIMPLE_POINTER_PROTOCOL;

typedef struct {
    UINT64 AbsoluteMinX, AbsoluteMinY, AbsoluteMinZ;
    UINT64 AbsoluteMaxX, AbsoluteMaxY, AbsoluteMaxZ;
    UINT32 Attributes;
} EFI_ABSOLUTE_POINTER_MODE;

typedef struct {
    UINT64 CurrentX, CurrentY, CurrentZ;
    UINT32 ActiveButtons;
} EFI_ABSOLUTE_POINTER_STATE;

typedef struct EFI_ABSOLUTE_POINTER_PROTOCOL {
    EFI_STATUS (EFIAPI *Reset)(struct EFI_ABSOLUTE_POINTER_PROTOCOL *, BOOLEAN);
    EFI_STATUS (EFIAPI *GetState)(struct EFI_ABSOLUTE_POINTER_PROTOCOL *, EFI_ABSOLUTE_POINTER_STATE *);
    EFI_EVENT WaitForInput;
    EFI_ABSOLUTE_POINTER_MODE *Mode;
} EFI_ABSOLUTE_POINTER_PROTOCOL;

/* ---- Files ----------------------------------------------------------- */

#define EFI_FILE_MODE_READ   0x0000000000000001ULL
#define EFI_FILE_MODE_WRITE  0x0000000000000002ULL
#define EFI_FILE_MODE_CREATE 0x8000000000000000ULL
#define EFI_FILE_DIRECTORY   0x10

typedef struct EFI_FILE_PROTOCOL {
    UINT64 Revision;
    EFI_STATUS (EFIAPI *Open)(struct EFI_FILE_PROTOCOL *, struct EFI_FILE_PROTOCOL **, CHAR16 *, UINT64, UINT64);
    EFI_STATUS (EFIAPI *Close)(struct EFI_FILE_PROTOCOL *);
    EFI_STATUS (EFIAPI *Delete)(struct EFI_FILE_PROTOCOL *);
    EFI_STATUS (EFIAPI *Read)(struct EFI_FILE_PROTOCOL *, UINTN *, VOID *);
    EFI_STATUS (EFIAPI *Write)(struct EFI_FILE_PROTOCOL *, UINTN *, VOID *);
    EFI_STATUS (EFIAPI *GetPosition)(struct EFI_FILE_PROTOCOL *, UINT64 *);
    EFI_STATUS (EFIAPI *SetPosition)(struct EFI_FILE_PROTOCOL *, UINT64);
    EFI_STATUS (EFIAPI *GetInfo)(struct EFI_FILE_PROTOCOL *, EFI_GUID *, UINTN *, VOID *);
    EFI_STATUS (EFIAPI *SetInfo)(struct EFI_FILE_PROTOCOL *, EFI_GUID *, UINTN, VOID *);
    EFI_STATUS (EFIAPI *Flush)(struct EFI_FILE_PROTOCOL *);
} EFI_FILE_PROTOCOL;

typedef struct {
    UINT64 Size;
    UINT64 FileSize;
    UINT64 PhysicalSize;
    EFI_TIME CreateTime;
    EFI_TIME LastAccessTime;
    EFI_TIME ModificationTime;
    UINT64 Attribute;
    CHAR16 FileName[1];
} EFI_FILE_INFO;

typedef struct {
    UINT64 Size;
    BOOLEAN ReadOnly;
    UINT64 VolumeSize;
    UINT64 FreeSpace;
    UINT32 BlockSize;
    CHAR16 VolumeLabel[1];
} EFI_FILE_SYSTEM_INFO;

typedef struct EFI_SIMPLE_FILE_SYSTEM_PROTOCOL {
    UINT64 Revision;
    EFI_STATUS (EFIAPI *OpenVolume)(struct EFI_SIMPLE_FILE_SYSTEM_PROTOCOL *, EFI_FILE_PROTOCOL **);
} EFI_SIMPLE_FILE_SYSTEM_PROTOCOL;

/* ---- Block I/O ------------------------------------------------------- */

typedef struct {
    UINT32 MediaId;
    BOOLEAN RemovableMedia;
    BOOLEAN MediaPresent;
    BOOLEAN LogicalPartition;
    BOOLEAN ReadOnly;
    BOOLEAN WriteCaching;
    UINT32 BlockSize;
    UINT32 IoAlign;
    UINT64 LastBlock;
} EFI_BLOCK_IO_MEDIA;

typedef struct {
    UINT64 Revision;
    EFI_BLOCK_IO_MEDIA *Media;
    void *Reset, *ReadBlocks, *WriteBlocks, *FlushBlocks;
} EFI_BLOCK_IO_PROTOCOL;

/* ---- USB I/O --------------------------------------------------------- */

#pragma pack(push, 1)
typedef struct {
    UINT8  RequestType;
    UINT8  Request;
    UINT16 Value;
    UINT16 Index;
    UINT16 Length;
} EFI_USB_DEVICE_REQUEST;

typedef struct {
    UINT8 Length, DescriptorType, InterfaceNumber, AlternateSetting, NumEndpoints;
    UINT8 InterfaceClass, InterfaceSubClass, InterfaceProtocol, Interface;
} EFI_USB_INTERFACE_DESCRIPTOR;

typedef struct {
    UINT8  Length, DescriptorType, EndpointAddress, Attributes;
    UINT16 MaxPacketSize;
    UINT8  Interval;
} EFI_USB_ENDPOINT_DESCRIPTOR;
#pragma pack(pop)

typedef enum { EfiUsbDataIn, EfiUsbDataOut, EfiUsbNoData } EFI_USB_DATA_DIRECTION;

typedef EFI_STATUS (EFIAPI *EFI_ASYNC_USB_TRANSFER_CALLBACK)(VOID *Data, UINTN DataLength,
                                                             VOID *Context, UINT32 Status);

typedef struct EFI_USB_IO_PROTOCOL {
    EFI_STATUS (EFIAPI *UsbControlTransfer)(struct EFI_USB_IO_PROTOCOL *, EFI_USB_DEVICE_REQUEST *,
                                            EFI_USB_DATA_DIRECTION, UINT32 TimeoutMs, VOID *Data,
                                            UINTN DataLength, UINT32 *Status);
    void *UsbBulkTransfer;
    EFI_STATUS (EFIAPI *UsbAsyncInterruptTransfer)(struct EFI_USB_IO_PROTOCOL *, UINT8 Endpoint,
                                                   BOOLEAN IsNewTransfer, UINTN PollingIntervalMs,
                                                   UINTN DataLength, EFI_ASYNC_USB_TRANSFER_CALLBACK,
                                                   VOID *Context);
    void *UsbSyncInterruptTransfer;
    void *UsbIsochronousTransfer, *UsbAsyncIsochronousTransfer;
    void *UsbGetDeviceDescriptor, *UsbGetConfigDescriptor;
    EFI_STATUS (EFIAPI *UsbGetInterfaceDescriptor)(struct EFI_USB_IO_PROTOCOL *, EFI_USB_INTERFACE_DESCRIPTOR *);
    EFI_STATUS (EFIAPI *UsbGetEndpointDescriptor)(struct EFI_USB_IO_PROTOCOL *, UINT8 Index,
                                                  EFI_USB_ENDPOINT_DESCRIPTOR *);
    void *UsbGetStringDescriptor, *UsbGetSupportedLanguages, *UsbPortReset;
} EFI_USB_IO_PROTOCOL;

/* ---- Loaded image ---------------------------------------------------- */

typedef struct {
    UINT32 Revision;
    EFI_HANDLE ParentHandle;
    struct EFI_SYSTEM_TABLE *SystemTable;
    EFI_HANDLE DeviceHandle;
    EFI_DEVICE_PATH *FilePath;
    VOID *Reserved;
    UINT32 LoadOptionsSize;
    VOID *LoadOptions;
    VOID *ImageBase;
    UINT64 ImageSize;
    EFI_MEMORY_TYPE ImageCodeType;
    EFI_MEMORY_TYPE ImageDataType;
    void *Unload;
} EFI_LOADED_IMAGE_PROTOCOL;

/* ---- Services -------------------------------------------------------- */

typedef enum { AllHandles, ByRegisterNotify, ByProtocol } EFI_LOCATE_SEARCH_TYPE;
typedef enum { TimerCancel, TimerPeriodic, TimerRelative } EFI_TIMER_DELAY;
typedef enum { EfiResetCold, EfiResetWarm, EfiResetShutdown, EfiResetPlatformSpecific } EFI_RESET_TYPE;

#define EVT_TIMER 0x80000000

#define TPL_APPLICATION 4
#define TPL_CALLBACK    8
#define TPL_NOTIFY      16

#define EFI_VARIABLE_NON_VOLATILE       0x1
#define EFI_VARIABLE_BOOTSERVICE_ACCESS 0x2
#define EFI_VARIABLE_RUNTIME_ACCESS     0x4

#define EFI_OS_INDICATIONS_BOOT_TO_FW_UI 0x1ULL

#define LOAD_OPTION_ACTIVE 0x1
#define LOAD_OPTION_HIDDEN 0x8

typedef struct {
    EFI_TABLE_HEADER Hdr;
    EFI_TPL (EFIAPI *RaiseTPL)(EFI_TPL);
    void (EFIAPI *RestoreTPL)(EFI_TPL);
    void *AllocatePages, *FreePages, *GetMemoryMap;
    EFI_STATUS (EFIAPI *AllocatePool)(EFI_MEMORY_TYPE, UINTN, VOID **);
    EFI_STATUS (EFIAPI *FreePool)(VOID *);
    EFI_STATUS (EFIAPI *CreateEvent)(UINT32, EFI_TPL, void *, VOID *, EFI_EVENT *);
    EFI_STATUS (EFIAPI *SetTimer)(EFI_EVENT, EFI_TIMER_DELAY, UINT64);
    EFI_STATUS (EFIAPI *WaitForEvent)(UINTN, EFI_EVENT *, UINTN *);
    EFI_STATUS (EFIAPI *SignalEvent)(EFI_EVENT);
    EFI_STATUS (EFIAPI *CloseEvent)(EFI_EVENT);
    EFI_STATUS (EFIAPI *CheckEvent)(EFI_EVENT);
    void *InstallProtocolInterface, *ReinstallProtocolInterface, *UninstallProtocolInterface;
    EFI_STATUS (EFIAPI *HandleProtocol)(EFI_HANDLE, EFI_GUID *, VOID **);
    void *Reserved;
    void *RegisterProtocolNotify, *LocateHandle;
    EFI_STATUS (EFIAPI *LocateDevicePath)(EFI_GUID *, EFI_DEVICE_PATH **, EFI_HANDLE *);
    void *InstallConfigurationTable;
    EFI_STATUS (EFIAPI *LoadImage)(BOOLEAN, EFI_HANDLE, EFI_DEVICE_PATH *, VOID *, UINTN, EFI_HANDLE *);
    EFI_STATUS (EFIAPI *StartImage)(EFI_HANDLE, UINTN *, CHAR16 **);
    void *Exit;
    EFI_STATUS (EFIAPI *UnloadImage)(EFI_HANDLE);
    void *ExitBootServices;
    void *GetNextMonotonicCount;
    EFI_STATUS (EFIAPI *Stall)(UINTN);
    EFI_STATUS (EFIAPI *SetWatchdogTimer)(UINTN, UINT64, UINTN, CHAR16 *);
    EFI_STATUS (EFIAPI *ConnectController)(EFI_HANDLE, EFI_HANDLE *, EFI_DEVICE_PATH *, BOOLEAN);
    void *DisconnectController;
    void *OpenProtocol, *CloseProtocol, *OpenProtocolInformation;
    void *ProtocolsPerHandle;
    EFI_STATUS (EFIAPI *LocateHandleBuffer)(EFI_LOCATE_SEARCH_TYPE, EFI_GUID *, VOID *, UINTN *, EFI_HANDLE **);
    EFI_STATUS (EFIAPI *LocateProtocol)(EFI_GUID *, VOID *, VOID **);
    void *InstallMultipleProtocolInterfaces, *UninstallMultipleProtocolInterfaces;
    void *CalculateCrc32;
    void *CopyMem, *SetMem;
    void *CreateEventEx;
} EFI_BOOT_SERVICES;

typedef struct {
    EFI_TABLE_HEADER Hdr;
    EFI_STATUS (EFIAPI *GetTime)(EFI_TIME *, VOID *);
    void *SetTime, *GetWakeupTime, *SetWakeupTime;
    void *SetVirtualAddressMap, *ConvertPointer;
    EFI_STATUS (EFIAPI *GetVariable)(CHAR16 *, EFI_GUID *, UINT32 *, UINTN *, VOID *);
    EFI_STATUS (EFIAPI *GetNextVariableName)(UINTN *, CHAR16 *, EFI_GUID *);
    EFI_STATUS (EFIAPI *SetVariable)(CHAR16 *, EFI_GUID *, UINT32, UINTN, VOID *);
    void *GetNextHighMonotonicCount;
    void (EFIAPI *ResetSystem)(EFI_RESET_TYPE, EFI_STATUS, UINTN, VOID *);
    void *UpdateCapsule, *QueryCapsuleCapabilities, *QueryVariableInfo;
} EFI_RUNTIME_SERVICES;

typedef struct EFI_SYSTEM_TABLE {
    EFI_TABLE_HEADER Hdr;
    CHAR16 *FirmwareVendor;
    UINT32 FirmwareRevision;
    EFI_HANDLE ConsoleInHandle;
    EFI_SIMPLE_TEXT_INPUT_PROTOCOL *ConIn;
    EFI_HANDLE ConsoleOutHandle;
    EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL *ConOut;
    EFI_HANDLE StandardErrorHandle;
    EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL *StdErr;
    EFI_RUNTIME_SERVICES *RuntimeServices;
    EFI_BOOT_SERVICES *BootServices;
    UINTN NumberOfTableEntries;
    VOID *ConfigurationTable;
} EFI_SYSTEM_TABLE;

/* ---- GUIDs ----------------------------------------------------------- */

#define EFI_GRAPHICS_OUTPUT_PROTOCOL_GUID \
    { 0x9042a9de, 0x23dc, 0x4a38, { 0x96, 0xfb, 0x7a, 0xde, 0xd0, 0x80, 0x51, 0x6a } }
#define EFI_SIMPLE_TEXT_INPUT_EX_PROTOCOL_GUID \
    { 0xdd9e7534, 0x7762, 0x4698, { 0x8c, 0x14, 0xf5, 0x85, 0x17, 0xa6, 0x25, 0xaa } }
#define EFI_SIMPLE_POINTER_PROTOCOL_GUID \
    { 0x31878c87, 0x0b75, 0x11d5, { 0x9a, 0x4f, 0x00, 0x90, 0x27, 0x3f, 0xc1, 0x4d } }
#define EFI_ABSOLUTE_POINTER_PROTOCOL_GUID \
    { 0x8d59d32b, 0xc655, 0x4ae9, { 0x9b, 0x15, 0xf2, 0x59, 0x04, 0x99, 0x2a, 0x43 } }
#define EFI_LOADED_IMAGE_PROTOCOL_GUID \
    { 0x5b1b31a1, 0x9562, 0x11d2, { 0x8e, 0x3f, 0x00, 0xa0, 0xc9, 0x69, 0x72, 0x3b } }
#define EFI_SIMPLE_FILE_SYSTEM_PROTOCOL_GUID \
    { 0x964e5b22, 0x6459, 0x11d2, { 0x8e, 0x39, 0x00, 0xa0, 0xc9, 0x69, 0x72, 0x3b } }
#define EFI_FILE_INFO_GUID \
    { 0x09576e92, 0x6d3f, 0x11d2, { 0x8e, 0x39, 0x00, 0xa0, 0xc9, 0x69, 0x72, 0x3b } }
#define EFI_FILE_SYSTEM_INFO_GUID \
    { 0x09576e93, 0x6d3f, 0x11d2, { 0x8e, 0x39, 0x00, 0xa0, 0xc9, 0x69, 0x72, 0x3b } }
#define EFI_DEVICE_PATH_PROTOCOL_GUID \
    { 0x09576e91, 0x6d3f, 0x11d2, { 0x8e, 0x39, 0x00, 0xa0, 0xc9, 0x69, 0x72, 0x3b } }
#define EFI_BLOCK_IO_PROTOCOL_GUID \
    { 0x964e5b21, 0x6459, 0x11d2, { 0x8e, 0x39, 0x00, 0xa0, 0xc9, 0x69, 0x72, 0x3b } }
#define EFI_USB_IO_PROTOCOL_GUID \
    { 0x2b2f68d6, 0x0cd2, 0x44cf, { 0x8e, 0x8b, 0xbb, 0xa2, 0x0b, 0x1b, 0x5b, 0x75 } }
#define EFI_SIMPLE_TEXT_INPUT_PROTOCOL_GUID \
    { 0x387477c1, 0x69c7, 0x11d2, { 0x8e, 0x39, 0x00, 0xa0, 0xc9, 0x69, 0x72, 0x3b } }
#define EFI_GLOBAL_VARIABLE_GUID \
    { 0x8be4df61, 0x93ca, 0x11d2, { 0xaa, 0x0d, 0x00, 0xe0, 0x98, 0x03, 0x2b, 0x8c } }

#endif
