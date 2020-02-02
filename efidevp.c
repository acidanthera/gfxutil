#include <stdio.h>
#include <stdlib.h>
#include <CoreFoundation/CoreFoundation.h>
#include <IOKit/IOKitLib.h>

#include "edk2misc.h"
#include "utils.h"
#include "main.h"

//========================================================================================
// Convert bin, hex, or nvram path to or from text.

int hexchar(char c)
{
	return (c - (c < 'A' ? '0' : ((c < 'a' ? 'A' : 'a') - 10)));
}

int hextobyte(char* c)
{
	return (hexchar(*c) << 4) + hexchar(c[1]);
}

int allhex(char *arg, unsigned long len)
{
	for (int i = 0; i < len; i++)
		if (!IS_HEX(arg[i]))
			return 0;
	return 1;
}

int PrintDevicePathUtilToText(void* bytepath, unsigned long bytepathlen, SETTINGS *settings)
{
	int result = 0;
	if (settings->verbose)
	{
		fprintf(stdout, "# Converting %ld bytes to text\n", bytepathlen);
	}

	if (!IsDevicePathValid(bytepath, bytepathlen))
	{
		fprintf(stdout, "# Invalid device path\n");
		return 1;
	}

	VerifyDevicePathNodeSizes(bytepath);
	CHAR16* textpath = PatchedConvertDevicePathToText(bytepath, settings->display_only, settings->allow_shortcuts);
	
	if (textpath)
	{
		UINTN textlen = StrLen(textpath);

		if (settings->verbose)
		{
			fprintf(stdout, "# Text path %llu\n", textlen);
		}

		CHAR8* asciitextpath = AllocatePool(textlen + 1);
		if (asciitextpath)
		{
			UnicodeStrToAsciiStr(textpath, asciitextpath);
			fprintf(stdout, "%s", asciitextpath);
			FreePool(asciitextpath);
		}
		else
		{
			result = 1;
		}
		FreePool(textpath);
	}
	else
	{
		result = 1;
	}
	return result;
}


int OutputDevicePathUtilFromText(void* asciitextpath, unsigned long asciitextpathlen, SETTINGS *settings)
{
	int result = 0;
	if (settings->verbose)
	{
		fprintf(stdout, "# Converting %ld characters to bytes\n", asciitextpathlen);
	}

	assert(strlen(asciitextpath) <= asciitextpathlen);

	CHAR16* textpath = AllocatePool((asciitextpathlen + 1) * sizeof (CHAR16));
	if (textpath)
	{
		AsciiStrToUnicodeStr(asciitextpath, textpath);
		EFI_DEVICE_PATH_PROTOCOL *bytepath = ConvertTextToDevicePath(textpath);
		if (bytepath)
		{
			int bytepathlen = 0;
			for (EFI_DEVICE_PATH_PROTOCOL *node = bytepath;;)
			{
				int nodelen = node->Length[0] | node->Length[1] << 8;
				bytepathlen += nodelen;
				if (node->Type == END_DEVICE_PATH_TYPE && node->SubType == END_ENTIRE_DEVICE_PATH_SUBTYPE) break;
				node = (EFI_DEVICE_PATH_PROTOCOL *)((UINT8*)node + nodelen);
			}
			PrintMem (bytepath, bytepathlen);
		}
		else
		{
			result = 1;
		}
		FreePool(textpath);
	}
	else
	{
		result = 1;
	}

	return result;
}


int parse_generic_option(char *arg, unsigned long arglen, SETTINGS *settings)
{
	if (settings->verbose)
	{
		fprintf(stdout, "# Processing argument of length %lu\n", arglen);
	}

	if (!arg || !arglen)
	{
		return 1;
	}

	int i;

	int bytepathlenmax = 10000;
	int bytepathlen = 0;
	UINT8* bytepath = malloc(bytepathlenmax);
	if (!bytepath) return 1;
	
	if (arg[0] == '%')
	{
		// convert hex "nvram -p" path
		for (i = 0; i < arglen;)
		{
			if (bytepathlen + 1 >= bytepathlenmax)
			{
				bytepathlenmax += 10000;
				bytepath = realloc(bytepath, bytepathlenmax);
				if (!bytepath)
					return 1;
			}
			if (arg[i] == '%' && i+2 < arglen && IS_LOWER_HEX(arg[i+1]) && IS_LOWER_HEX(arg[i+2]))
			{
				i++;
				bytepath[bytepathlen++] = hextobyte(arg + i);
				i += 2;
			}
			else
			{
				bytepath[bytepathlen++] = arg[i++];
			}
		}
		int result = PrintDevicePathUtilToText(bytepath, bytepathlen, settings);
		free(bytepath);
		return result;
	}

	if ((arglen & 1) == 0 && allhex(arg, arglen))
	{
		// convert hex "xxd -p" path
		for (i = 0; i < arglen; i += 2)
		{
			if (bytepathlen + 2 >= bytepathlenmax)
			{
				bytepathlenmax += 10000;
				bytepath = realloc(bytepath, bytepathlenmax);
				if (!bytepath)
					return 1;
			}
			bytepath[bytepathlen++] = hextobyte(arg + i);
		}
		int result = PrintDevicePathUtilToText(bytepath, bytepathlen, settings);
		free(bytepath);
		return result;
	}

	free(bytepath);

	if (!isprint(arg[0]))
	{
		// convert binary path
		return PrintDevicePathUtilToText(arg, arglen, settings);
	}

	return OutputDevicePathUtilFromText(arg, arglen, settings);
}

//========================================================================================
// Iterate ioregistry

void GetPaths(io_service_t device, char *ioregPath, char **efiPath, SETTINGS *settings)
{
	// for an ioregistry device, return an ioregistry path string and an efi device path string

	char temp[4096];
	kern_return_t kr;
	io_service_t leaf_device = device;
	EFI_DEVICE_PATH *DevicePath = NULL;
	if (efiPath) *efiPath = NULL;
	
	while (device) {
		io_iterator_t parentIterator = 0;
		if (IOObjectConformsTo(device, "IOPlatformExpertDevice"))
		{
			sprintf(temp, "%s%s", ioregPath[0] ? "/" : "", ioregPath);
			strcpy(ioregPath, temp);
			kr = KERN_ABORTED; // no more parents
		}
		else
		{
			EFI_DEVICE_PATH *DeviceNode = NULL;
			io_name_t name;
			io_name_t locationInPlane;
			const char *deviceLocation = NULL, *functionLocation = NULL;
			unsigned int deviceInt = 0, functionInt = 0;
			int len;
			name[0] = '\0';
			
			IORegistryEntryGetName(device, name);
			if (IORegistryEntryGetLocationInPlane(device, settings->plane, locationInPlane) == KERN_SUCCESS) {
				len = sprintf(temp, "%s@%s", name, locationInPlane);
				deviceLocation = strtok(locationInPlane, ",");
				functionLocation = strtok(NULL, ",");
				if (deviceLocation != NULL) deviceInt = (unsigned int)strtol(deviceLocation, NULL, 16);
				if (functionLocation != NULL) functionInt = (unsigned int)strtol(functionLocation, NULL, 16);
			}
			else {
				len = sprintf(temp, "%s", name);
			}
			sprintf(temp + len, "%s%s", ioregPath[0] ? "/" : "", ioregPath);
			strcpy(ioregPath, temp);

			if (IOObjectConformsTo(device, "IOPCIDevice"))
			{
				PCI_DEVICE_PATH *Pci = (PCI_DEVICE_PATH *) CreateDeviceNode(HARDWARE_DEVICE_PATH, HW_PCI_DP, sizeof(PCI_DEVICE_PATH));
				Pci->Function = functionInt;
				Pci->Device = deviceInt;
				DeviceNode = (EFI_DEVICE_PATH *) Pci;
			}
			else if (IOObjectConformsTo(device, "IOACPIPlatformDevice"))
			{
				char pnp[20], uid[20];
				CHAR16 pnp16[20];
				uint32_t size;
				uid[0] = '\0';
				pnp[0] = '\0';
				size = sizeof(uid); IORegistryEntryGetProperty(device, "_UID", uid, &size);
				size = sizeof(pnp); if (IORegistryEntryGetProperty(device, "compatible", pnp, &size)) {
					size = sizeof(pnp); IORegistryEntryGetProperty(device, "name", pnp, &size);
				}
				AsciiStrToUnicodeStr(pnp, pnp16);
				ACPI_HID_DEVICE_PATH *Acpi = (ACPI_HID_DEVICE_PATH *) CreateDeviceNode (ACPI_DEVICE_PATH, ACPI_DP, sizeof(ACPI_HID_DEVICE_PATH));
				Acpi->HID = EisaIdFromText(pnp16);
				Acpi->UID = (UINT32)strtoul(uid, NULL, 16);
				DeviceNode = (EFI_DEVICE_PATH *)Acpi;
			}
			if (DeviceNode) {
				EFI_DEVICE_PATH *NodePath = UefiDevicePathLibAppendDevicePathNode(NULL, DeviceNode);
				if (NodePath) {
					EFI_DEVICE_PATH *NewDevicePath = UefiDevicePathLibAppendDevicePath(NodePath, DevicePath);
					if (NewDevicePath) {
						free(DevicePath);
						DevicePath = NewDevicePath;
					}
					free(NodePath);
				}
			}

			kr = IORegistryEntryGetParentIterator(device, settings->plane, &parentIterator);
		} // !IOPlatformExpertDevice
	
		if (device != leaf_device) IOObjectRelease(device);
		if (kr != KERN_SUCCESS)
			break;
		device = IOIteratorNext(parentIterator);
	} // while device
	
	if (DevicePath) {
		char * devpath_text = NULL;
		VerifyDevicePathNodeSizes(DevicePath);
		CHAR16 *devpath_text16 = PatchedConvertDevicePathToText(DevicePath, settings->display_only, settings->allow_shortcuts);
		if(devpath_text16) {
			devpath_text = (char *)calloc(StrLen(devpath_text16) + 1, sizeof(char));
			if (devpath_text) {
				UnicodeStrToAsciiStr(devpath_text16, devpath_text);
				if (efiPath) *efiPath = devpath_text;
			}
			free(devpath_text16);
		}
	}
} // GetPaths

void OutputOneDevice(io_service_t device, SETTINGS *settings)
{
	kern_return_t kr;
	char *devicePath;
	char ioregPath[4096];
	uint32_t size;
	io_struct_inband_t temp;
	uint32_t vendor_id;
	uint32_t device_id;
	Boolean doit = false;

	if (IOObjectConformsTo(device, "IOPCIDevice") || IOObjectConformsTo(device, "IOACPIPlatformDevice"))
	{
		doit = true;
		if (settings->search)
		{
			doit = false;
			io_string_t name;
			unsigned int size = 0;
			io_struct_inband_t prop_name;
			io_struct_inband_t prop_ioname;
			kern_return_t status;

			status = IORegistryEntryGetNameInPlane(device, settings->plane, name);
			assertion(status == KERN_SUCCESS, "can't obtain registry entry name");
			
			size = sizeof(prop_name);
			IORegistryEntryGetProperty(device, "name", prop_name, &size);

			size = sizeof(prop_ioname);
			IORegistryEntryGetProperty(device, "IOName", prop_ioname, &size);
					
			if (!strcasecmp(prop_name, settings->search) || !strcasecmp(prop_ioname, settings->search) || !strcasecmp(name, settings->search))
			{
				settings->matched = true;
				doit = true;
			}

			IOObjectRelease((unsigned int)prop_name);
			IOObjectRelease((unsigned int)prop_ioname);
			IOObjectRelease((unsigned int)name);
		}
		if (doit)
		{
			ioregPath[0] = '\0';

			size = sizeof(temp);
			kr = IORegistryEntryGetProperty(device, "pcidebug", temp, &size);
			if (kr == KERN_SUCCESS)
			{
				unsigned int busInt = 0, deviceInt = 0, functionInt = 0;
				sscanf(temp, "%d:%d:%d", &busInt, &deviceInt, &functionInt);
				printf("%02x:%02x.%01x ", busInt, deviceInt, functionInt);
			} else {
				printf("        ");
			}

			size = sizeof(vendor_id);
			kr = IORegistryEntryGetProperty(device, "vendor-id", (char*)&vendor_id, &size);
			if (kr == KERN_SUCCESS)
			{
				size = sizeof(device_id);
				kr = IORegistryEntryGetProperty(device, "device-id", (char*)&device_id, &size);
			}
			if (kr == KERN_SUCCESS)
				printf("%04x:%04x ", vendor_id, device_id);
			else
				printf("          ");

			GetPaths(device, ioregPath, &devicePath, settings);
			printf("%s = %s\n", ioregPath, devicePath);
			free(devicePath);
		}
	} // if IOPCIDevice, IOACPIPlatformDevice
} // OutputOneDevice

/*
int OutputPCIDevicePaths(SETTINGS *settings)
{
	io_iterator_t iterator;
	kern_return_t kr = IOServiceGetMatchingServices(kIOMasterPortDefault, IOServiceMatching("IOPCIDevice"), &iterator);
	if (kr != KERN_SUCCESS)
		return 1;
	for (io_service_t device; IOIteratorIsValid(iterator) && (device = IOIteratorNext(iterator)); IOObjectRelease(device))
	{
		OutputOneDevice(device, settings);
	}
	IOObjectRelease(iterator);
	return 0;
} // OutputPCIDevicePaths
*/

int OutputPCIDevicePathsByTree1(io_service_t serviceNext, io_iterator_t services, SETTINGS *settings)
{
	io_registry_entry_t service;
	while((service = serviceNext))
	{
		io_iterator_t children;
		io_registry_entry_t	child;
		serviceNext = IOIteratorNext(services);
		IORegistryEntryGetChildIterator(service, settings->plane, &children);
		child = IOIteratorNext(children);
		OutputOneDevice(service, settings);
		OutputPCIDevicePathsByTree1(child, children, settings);
		IOObjectRelease(children);
		IOObjectRelease(service);
	}
	return 0;
} // OutputPCIDevicePathsByTree1

int OutputPCIDevicePathsByTree(SETTINGS *settings)
{
	io_service_t device = IORegistryGetRootEntry(kIOMasterPortDefault);
	return OutputPCIDevicePathsByTree1(device, 0, settings);
} // OutputPCIDevicePathsByTree
