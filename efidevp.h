#ifndef _DEVPATH_H
#define _DEVPATH_H

#include "main.h"

//========================================================================================
// Convert bin, hex, or nvram path to or from text.

int PrintDevicePathUtilToText(void* bytepath, unsigned long bytepathlen, SETTINGS *settings);
int OutputDevicePathUtilFromText(void* asciitextpath, unsigned long asciitextpathlen, SETTINGS *settings);
int parse_generic_option(char *arg, unsigned long arglen, SETTINGS *settings);

//========================================================================================
// Iterate ioregistry

int OutputPCIDevicePaths(SETTINGS *settings);
int OutputPCIDevicePathsByTree(SETTINGS *settings);

//========================================================================================
#endif
