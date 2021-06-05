//
//  edk2misc.c
//  gfxutil
//
//  Created by joevt on 2019-12-14.
//

#include "edk2misc.h"
#include "UefiDevicePathLib.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/errno.h>
#include <sys/mman.h>

//========================================================================================

EFI_SYSTEM_TABLE ST;
EFI_BOOT_SERVICES BS;
EFI_SYSTEM_TABLE *gST = NULL;
EFI_BOOT_SERVICES *gBS = NULL;

#define EFI_UART_DEVICE_PATH_GUID \
{ \
	0x37499a9d, 0x542f, 0x4c89, {0xa0, 0x26, 0x35, 0xda, 0x14, 0x20, 0x94, 0xe4 } \
}


EFI_GUID gEfiDebugPortProtocolGuid = EFI_DEBUGPORT_PROTOCOL_GUID;
EFI_GUID gEfiPcAnsiGuid = EFI_PC_ANSI_GUID;
EFI_GUID gEfiPersistentVirtualCdGuid = EFI_PERSISTENT_VIRTUAL_CD_GUID;
EFI_GUID gEfiPersistentVirtualDiskGuid = EFI_PERSISTENT_VIRTUAL_DISK_GUID;
EFI_GUID gEfiSasDevicePathGuid = EFI_SAS_DEVICE_PATH_GUID;
EFI_GUID gEfiUartDevicePathGuid = EFI_UART_DEVICE_PATH_GUID;
EFI_GUID gEfiVirtualCdGuid = EFI_VIRTUAL_CD_GUID;
EFI_GUID gEfiVirtualDiskGuid = EFI_VIRTUAL_DISK_GUID;
EFI_GUID gEfiVT100Guid = EFI_VT_100_GUID;
EFI_GUID gEfiVT100PlusGuid = EFI_VT_100_PLUS_GUID;
EFI_GUID gEfiVTUTF8Guid = EFI_VT_UTF8_GUID;

EFI_GUID gEfiDevicePathProtocolGuid = EFI_DEVICE_PATH_PROTOCOL_GUID;


VOID *
InternalAllocateCopyPool (
	UINTN AllocationSize,
	CONST VOID *Buffer
	)
{
	VOID  *Memory;

	ASSERT (Buffer != NULL);

	Memory = malloc (AllocationSize);
	if (Memory != NULL) {
		Memory = memcpy (Memory, Buffer, AllocationSize);
	}
	return Memory;
}


VOID *
AllocateCopyPool (
	UINTN AllocationSize,
	CONST VOID *Buffer
	)
{
	return InternalAllocateCopyPool (AllocationSize, Buffer);
}


EFI_STATUS
EFIAPI
DontHandleProtocol (
	IN EFI_HANDLE UserHandle,
	IN EFI_GUID *Protocol,
	OUT VOID **Interface
	)
{
	return EFI_UNSUPPORTED;
}


VOID
PrintMem (
	CONST VOID *Buffer,
	UINTN Count
	)
{
	CONST UINT8 *Bytes;
	UINTN Idx;

	Bytes = Buffer;
	for (Idx = 0; Idx < Count; Idx++) {
		printf("%02x", Bytes[Idx]);
	}
}

CHAR16 *
EFIAPI
UefiDevicePathLibCatPrint (
	IN OUT POOL_PRINT *Str,
	IN CHAR16 *Fmt,
	...
);

CHAR16 *
UefiDevicePathLibStrDuplicate (
	IN CONST CHAR16 *Src
	);

CHAR16 *
GetNextParamStr (
	IN OUT CHAR16 **List
	);

VOID
Strtoi64 (
	IN CHAR16 *Str,
	OUT UINT64 *Data
	);

UINTN
Strtoi (
	IN CHAR16 *Str
	);

typedef struct {
	EFI_DEVICE_PATH_PROTOCOL Header;
	UINT8 SasAddress[4]; // reduced from 8 to 4 bytes
	UINT8 Lun[4]; // reduced from 8 to 4 bytes
	UINT16 DeviceTopology;
	UINT16 RelativeTargetPort;
} PATCHED_SASEX_DEVICE_PATH;


VOID
OldDevPathToTextSasEx (
	IN OUT POOL_PRINT *Str,
	IN VOID *DevPath,
	IN BOOLEAN DisplayOnly,
	IN BOOLEAN AllowShortcuts
	);

VOID
DevPathToTextSasEx (
	IN OUT POOL_PRINT *Str,
	IN VOID *DevPath,
	IN BOOLEAN DisplayOnly,
	IN BOOLEAN AllowShortcuts
	)
{
	if (DevicePathNodeLength(DevPath) == sizeof(SASEX_DEVICE_PATH)) {
		OldDevPathToTextSasEx(Str, DevPath, DisplayOnly, AllowShortcuts);
		return;
	}
	
	PATCHED_SASEX_DEVICE_PATH *SasEx;
	UINTN Index;
	
	SasEx = DevPath;
	UefiDevicePathLibCatPrint (Str, L"SasEx(0x");
	
	for (Index = 0; Index < sizeof (SasEx->SasAddress) / sizeof (SasEx->SasAddress[0]); Index++) {
		UefiDevicePathLibCatPrint (Str, L"%02x", SasEx->SasAddress[Index]);
	}
	UefiDevicePathLibCatPrint (Str, L",0x");
	for (Index = 0; Index < sizeof (SasEx->Lun) / sizeof (SasEx->Lun[0]); Index++) {
		UefiDevicePathLibCatPrint (Str, L"%02x", SasEx->Lun[Index]);
	}
	UefiDevicePathLibCatPrint (Str, L",0x%x,", SasEx->RelativeTargetPort);

	if (((SasEx->DeviceTopology & 0x0f) == 0) && ((SasEx->DeviceTopology & BIT7) == 0)) {
		UefiDevicePathLibCatPrint (Str, L"NoTopology,0,0,0");
	} else if (((SasEx->DeviceTopology & 0x0f) <= 2) && ((SasEx->DeviceTopology & BIT7) == 0)) {
		UefiDevicePathLibCatPrint (
			Str,
			L"%s,%s,%s,",
			((SasEx->DeviceTopology & BIT4) != 0) ? L"SATA" : L"SAS",
			((SasEx->DeviceTopology & BIT5) != 0) ? L"External" : L"Internal",
			((SasEx->DeviceTopology & BIT6) != 0) ? L"Expanded" : L"Direct"
		);
		if ((SasEx->DeviceTopology & 0x0f) == 1) {
			UefiDevicePathLibCatPrint (Str, L"0");
		} else {
			//
			// Value 0x0 thru 0xFF -> Drive 1 thru Drive 256
			//
			UefiDevicePathLibCatPrint (Str, L"0x%x", ((SasEx->DeviceTopology >> 8) & 0xff) + 1);
		}
	} else {
		UefiDevicePathLibCatPrint (Str, L"0x%x,0,0,0", SasEx->DeviceTopology);
	}

	UefiDevicePathLibCatPrint (Str, L")");
	return ;
}

EFI_DEVICE_PATH_PROTOCOL *
OldDevPathFromTextSasEx (
	IN CHAR16 *TextDeviceNode
	);

EFI_DEVICE_PATH_PROTOCOL *
DevPathFromTextSasEx (
	IN CHAR16 *TextDeviceNode
	)
{
	CHAR16 *AddressStr;
	CHAR16 *LunStr;
	SASEX_DEVICE_PATH *SasEx;
	BOOLEAN is64;

	CHAR16 *duppath = UefiDevicePathLibStrDuplicate (TextDeviceNode);
	CHAR16 *path = duppath;
	AddressStr = GetNextParamStr (&path);
	LunStr = GetNextParamStr (&path);

	SasEx = (SASEX_DEVICE_PATH *) OldDevPathFromTextSasEx (TextDeviceNode);
	
	is64 = (StrLen(AddressStr) > 10 || StrLen(LunStr) > 10);
	FreePool(duppath);
	
	if (is64) {
		return (EFI_DEVICE_PATH_PROTOCOL *) SasEx;
	}
	
	PATCHED_SASEX_DEVICE_PATH * PatchedSasEx = (PATCHED_SASEX_DEVICE_PATH *) CreateDeviceNode (
		MESSAGING_DEVICE_PATH,
		MSG_SASEX_DP,
		(UINT16) sizeof (PATCHED_SASEX_DEVICE_PATH)
	);

	PatchedSasEx->SasAddress[0] = SasEx->SasAddress[4];
	PatchedSasEx->SasAddress[1] = SasEx->SasAddress[5];
	PatchedSasEx->SasAddress[2] = SasEx->SasAddress[6];
	PatchedSasEx->SasAddress[3] = SasEx->SasAddress[7];
	PatchedSasEx->Lun[0] = SasEx->Lun[4];
	PatchedSasEx->Lun[1] = SasEx->Lun[5];
	PatchedSasEx->Lun[2] = SasEx->Lun[6];
	PatchedSasEx->Lun[3] = SasEx->Lun[7];
	PatchedSasEx->DeviceTopology = SasEx->DeviceTopology;
	PatchedSasEx->RelativeTargetPort = SasEx->RelativeTargetPort;
	
	FreePool(SasEx);

	return (EFI_DEVICE_PATH_PROTOCOL *) PatchedSasEx;
}


VOID
DevPathToTextNodeGeneric (
	IN OUT POOL_PRINT *Str,
	IN VOID *DevPath,
	IN BOOLEAN DisplayOnly,
	IN BOOLEAN AllowShortcuts
);

VOID
DevPathToTextEndInstance (
	IN OUT POOL_PRINT *Str,
	IN VOID *DevPath,
	IN BOOLEAN DisplayOnly,
	IN BOOLEAN AllowShortcuts
);


#define psize(x) sizeof(((EFI_DEV_PATH *)0)->x)

typedef struct {
	UINT8 Type;
	UINT8 SubType;
	int SizeMin;
	int SizeMax; // 0 = same as min, 1 = infinite
	int SizeInc;
} DEVICE_NODE_TO_SIZE;


const DEVICE_NODE_TO_SIZE NodeSize[] = {
	{HARDWARE_DEVICE_PATH  , HW_PCI_DP                        , psize(Pci)                       , 0, 0 },
	{HARDWARE_DEVICE_PATH  , HW_PCCARD_DP                     , psize(PcCard)                    , 0, 0 },
	{HARDWARE_DEVICE_PATH  , HW_MEMMAP_DP                     , psize(MemMap)                    , 0, 0 },
	{HARDWARE_DEVICE_PATH  , HW_VENDOR_DP                     , psize(Vendor)                    , 1, 1 },
	{HARDWARE_DEVICE_PATH  , HW_CONTROLLER_DP                 , psize(Controller)                , 0, 0 },
	{HARDWARE_DEVICE_PATH  , HW_BMC_DP                        , psize(Bmc)                       , 0, 0 },
	{ACPI_DEVICE_PATH      , ACPI_DP                          , psize(Acpi)                      , 0, 0 },
	{ACPI_DEVICE_PATH      , ACPI_EXTENDED_DP                 , psize(ExtendedAcpi)              , 0, 0 },
	{ACPI_DEVICE_PATH      , ACPI_ADR_DP                      , psize(AcpiAdr)                   , 1, psize(AcpiAdr.ADR) },
	{MESSAGING_DEVICE_PATH , MSG_ATAPI_DP                     , psize(Atapi)                     , 0, 0 },
	{MESSAGING_DEVICE_PATH , MSG_SCSI_DP                      , psize(Scsi)                      , 0, 0 },
	{MESSAGING_DEVICE_PATH , MSG_FIBRECHANNEL_DP              , psize(FibreChannel)              , 0, 0 },
	{MESSAGING_DEVICE_PATH , MSG_FIBRECHANNELEX_DP            , psize(FibreChannelEx)            , 0, 0 },
	{MESSAGING_DEVICE_PATH , MSG_SASEX_DP                     , sizeof(PATCHED_SASEX_DEVICE_PATH), psize(SasEx), psize(SasEx) - sizeof(PATCHED_SASEX_DEVICE_PATH) }, // Patched to support both sizes
	{MESSAGING_DEVICE_PATH , MSG_NVME_NAMESPACE_DP            , psize(NvmeNamespace)             , 0, 0 },
	{MESSAGING_DEVICE_PATH , MSG_UFS_DP                       , psize(Ufs)                       , 0, 0 },
	{MESSAGING_DEVICE_PATH , MSG_SD_DP                        , psize(Sd)                        , 0, 0 },
	{MESSAGING_DEVICE_PATH , MSG_EMMC_DP                      , psize(Emmc)                      , 0, 0 },
	{MESSAGING_DEVICE_PATH , MSG_1394_DP                      , psize(F1394)                     , 0, 0 },
	{MESSAGING_DEVICE_PATH , MSG_USB_DP                       , psize(Usb)                       , 0, 0 },
	{MESSAGING_DEVICE_PATH , MSG_USB_WWID_DP                  , psize(UsbWwid) + sizeof(CHAR16)  , 1, sizeof(CHAR16) }, // bug exists with zero length string, so add CHAR(16) for minimum
	{MESSAGING_DEVICE_PATH , MSG_DEVICE_LOGICAL_UNIT_DP       , psize(LogicUnit)                 , 0, 0 },
	{MESSAGING_DEVICE_PATH , MSG_USB_CLASS_DP                 , psize(UsbClass)                  , 0, 0 },
	{MESSAGING_DEVICE_PATH , MSG_SATA_DP                      , psize(Sata)                      , 0, 0 },
	{MESSAGING_DEVICE_PATH , MSG_I2O_DP                       , psize(I2O)                       , 0, 0 },
	{MESSAGING_DEVICE_PATH , MSG_MAC_ADDR_DP                  , psize(MacAddr)                   , 0, 0 },
	{MESSAGING_DEVICE_PATH , MSG_IPv4_DP                      , psize(Ipv4) - psize(Ipv4.GatewayIpAddress) - psize(Ipv4.SubnetMask  ), psize(Ipv4), psize(Ipv4.GatewayIpAddress) + psize(Ipv4.SubnetMask  ) },
	{MESSAGING_DEVICE_PATH , MSG_IPv6_DP                      , psize(Ipv6) - psize(Ipv6.GatewayIpAddress) - psize(Ipv6.PrefixLength), psize(Ipv6), psize(Ipv6.GatewayIpAddress) + psize(Ipv6.PrefixLength) },
	{MESSAGING_DEVICE_PATH , MSG_INFINIBAND_DP                , psize(InfiniBand)                , 0, 0 },
	{MESSAGING_DEVICE_PATH , MSG_UART_DP                      , psize(Uart)                      , 0, 0 },
	{MESSAGING_DEVICE_PATH , MSG_VENDOR_DP                    , psize(Vendor)                    , 1, 1 },
	{MESSAGING_DEVICE_PATH , MSG_ISCSI_DP                     , psize(Iscsi)                     , 0, 0 },
	{MESSAGING_DEVICE_PATH , MSG_VLAN_DP                      , psize(Vlan)                      , 0, 0 },
	{MESSAGING_DEVICE_PATH , MSG_DNS_DP                       , psize(Dns)                       , 1, psize(Dns.DnsServerIp[0]) },
	{MESSAGING_DEVICE_PATH , MSG_URI_DP                       , psize(Uri)                       , 1, psize(Uri.Uri[0]) },
	{MESSAGING_DEVICE_PATH , MSG_BLUETOOTH_DP                 , psize(Bluetooth)                 , 0, 0 },
	{MESSAGING_DEVICE_PATH , MSG_WIFI_DP                      , psize(WiFi)                      , 0, 0 },
	{MESSAGING_DEVICE_PATH , MSG_BLUETOOTH_LE_DP              , sizeof(BLUETOOTH_LE_DEVICE_PATH) , 0, 0 },
	{MEDIA_DEVICE_PATH     , MEDIA_HARDDRIVE_DP               , psize(HardDrive)                 , 0, 0 },
	{MEDIA_DEVICE_PATH     , MEDIA_CDROM_DP                   , psize(CD)                        , 0, 0 },
	{MEDIA_DEVICE_PATH     , MEDIA_VENDOR_DP                  , psize(Vendor)                    , 1, 1 },
	{MEDIA_DEVICE_PATH     , MEDIA_PROTOCOL_DP                , psize(MediaProtocol)             , 0, 0 },
	{MEDIA_DEVICE_PATH     , MEDIA_FILEPATH_DP                , psize(FilePath)                  , 1, psize(FilePath.PathName[0]) }, // should check for terminating null
	{MEDIA_DEVICE_PATH     , MEDIA_PIWG_FW_VOL_DP             , psize(FirmwareVolume)            , 0, 0 },
	{MEDIA_DEVICE_PATH     , MEDIA_PIWG_FW_FILE_DP            , psize(FirmwareFile)              , 0, 0 },
	{MEDIA_DEVICE_PATH     , MEDIA_RELATIVE_OFFSET_RANGE_DP   , psize(Offset)                    , 0, 0 },
	{MEDIA_DEVICE_PATH     , MEDIA_RAM_DISK_DP                , psize(RamDisk)                   , 0, 0 },
	{BBS_DEVICE_PATH       , BBS_BBS_DP                       , psize(Bbs)                       , 0, 0 },
	{END_DEVICE_PATH_TYPE  , END_INSTANCE_DEVICE_PATH_SUBTYPE , psize(DevPath)                   , 0, 0 },
	{END_DEVICE_PATH_TYPE  , END_ENTIRE_DEVICE_PATH_SUBTYPE   , psize(DevPath)                   , 0, 0 },
	{0                     , 0                                , psize(DevPath)                   , 1, 1 },
};

void VerifyDevicePathNodeSizes(VOID * DevicePath) {
	EFI_DEVICE_PATH_PROTOCOL *Node;
	UINTN Index;
	BOOLEAN found;

	if (DevicePath == NULL) {
		return;
	}

	//
	// Process each device path node
	//
	Node = (EFI_DEVICE_PATH_PROTOCOL *) DevicePath;
	while (1) {
		//
		// Find the handler to dump this device path node
		// If not found, use a generic function
		//

		found = FALSE;
		for (Index = 0; NodeSize[Index].Type != 0; Index += 1) {
			if (
				DevicePathType (Node) == NodeSize[Index].Type &&
				DevicePathSubType (Node) == NodeSize[Index].SubType
			) {
				found = TRUE;
				break;
			}
		}
		if (!found) {
			fprintf(stderr, "Node at offset %ld has unknown type 0x%02x or sub type 0x%02x.\n", (void*)Node-(void*)DevicePath, DevicePathType (Node), DevicePathSubType (Node));
		}
		if (
			(DevicePathNodeLength (Node) < NodeSize[Index].SizeMin)
			||
			(NodeSize[Index].SizeMax == 0 && DevicePathNodeLength (Node) > NodeSize[Index].SizeMin)
		) {
			fprintf(stderr, "Node at offset %ld has length %llu which is not as expected %d.\n", (void*)Node-(void*)DevicePath, DevicePathNodeLength (Node), NodeSize[Index].SizeMin);
		}
		else if (NodeSize[Index].SizeMax > 3 && DevicePathNodeLength (Node) > NodeSize[Index].SizeMax) {
			fprintf(stderr, "Node at offset %ld has length %llu which is greater than expected %d.\n", (void*)Node-(void*)DevicePath, DevicePathNodeLength (Node), NodeSize[Index].SizeMax);
		}
		else if (NodeSize[Index].SizeInc > 1 && ((DevicePathNodeLength (Node) - NodeSize[Index].SizeMin) % NodeSize[Index].SizeInc) != 0 ) {
			fprintf(stderr, "Node at offset %ld has length %llu which is not equal to %d + n * %d.\n", (void*)Node-(void*)DevicePath, DevicePathNodeLength (Node), NodeSize[Index].SizeMin, NodeSize[Index].SizeInc);
		}
		
		if (IsDevicePathEnd (Node)) {
			break;
		}
		Node = NextDevicePathNode (Node);
	}
}


EFI_STATUS
EFIAPI
UefiBootServicesTableLibConstructor ()
{
	ZeroMem(&BS, sizeof(&BS));
	BS.HandleProtocol = &DontHandleProtocol;
	
	ZeroMem(&ST, sizeof(&ST));
	ST.BootServices = &BS;

	//
	// Cache pointer to the EFI System Table
	//
	gST = &ST;

	//
	// Cache pointer to the EFI Boot Services Table
	//
	gBS = gST->BootServices;
	
	return EFI_SUCCESS;
}
