//
//  edk2misc.h
//  gfxutil
//
//  Created by joevt on 2019-12-14.
//

#include "UefiDevicePathLib.h"

EFI_STATUS
EFIAPI
UefiBootServicesTableLibConstructor (void);


VOID
PrintMem (
	CONST VOID *Buffer,
	UINTN Count
	);

UINT32 EisaIdFromText ( IN CHAR16 *Text );

void VerifyDevicePathNodeSizes(VOID * DevicePath);
