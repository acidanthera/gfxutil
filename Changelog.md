gfxutil changelog
=================
#### 1.80b
- Replaced efidevp.h, efidevp.c according to the following changes:
- Added edk2 for efi stuff. It is used for all device path conversions to and from text.
  Therefore all node types can be converted, not just PCI and ACPI.
- Added edk2misc.c and edk2misc.c for compatibility with edk2.
- There is now only one function for iterating over the ioregistry.
  It outputs all devices (-p or -t) or devices with a certain name (-f).
- Added flags for device path conversions:
  -l Display Only (display only paths cannot be reversed back to binary).
  -m Don't Allow Shortcuts (shortcuts make vendor nodes more readable).
- For string conversion, assume strings don't have trailing null. This seems to work for
  MacPro3,1 and Macmini8,1 device properties.
- Fix searching for end of device path bytes so that the search does not exceed the
  end of the device properties. Also, the search now skips data inside a node.
- The call to isHexString was not checking the last character of the string (for
  converting hex string paths to bytes).
- Device paths can now be UTF16 in the plist. Property names still need to be ASCII.
- Paths work for both ACPI and PCI devices.
- Results of -p, -t and -f return PCI bus:device.function numbers, PCI vendor and device
  ID, and full ioregistry path (Device plane) with the EFI device path.
- ioregistry is searched in the same order shown by ioreg or IORegistryExplorer.app (with
  -t or -f).
- Require -p or -t to output all devices.
- Added the ability to convert a single efi device path from bin, hex, or nvram format to
  string or from string to bin.
- The command by itself will convert the path from stdin (stdin is required for bin).
- Help text uses spaces instead of tabs.
- Return 1 for error. 0 for no error.
- Fixed indent of verbose output to work more like ioreg output but also trims trailing
  spaces.
- Added code to verify the lengths of each node in a binary efi path.
- Added the ability to override or wrap the conversion routines of edk2.

#### 1.79b
- Fix unsupported registry entry class type error
- Run without command line args to get devicepaths for all PCI devices
- Add HDD device path output

#### 1.78b
- Unified release archive names

#### 1.77b
- Fix string saving cutting down 2 bytes
- Fix string detection to match IORegistryExplorer behaviour

#### 1.76b
- x86_64 support
- Minor bugfixes
