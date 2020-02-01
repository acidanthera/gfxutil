/*
 *  main.c
 *  gfxutil
 *
 *  Created by mcmatrix on 07.01.08.
 *  Copyright 2008 mcmatrix. All rights reserved.
 *
 */
 
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <CoreFoundation/CoreFoundation.h>            // (CFDictionary, ...)
#include <IOKit/IOCFSerialize.h>                      // (IOCFSerialize, ...)
#include <IOKit/IOKitLib.h>                           // (IOMasterPort, ...)
#include "edk2misc.h"
#include "efidevp.h"
#include "utils.h"
#include "main.h"

/*
 * Get the number of bytes necessary to represent a Unicode string.
 */
static int unilen(const char *str, int len)
{
	unsigned long ch;
	int posn = 0;
	int nbytes = 2;
	while(posn < len)
	{
		ch = UTF8ReadChar(str, len, &posn);
		if(ch < (unsigned long)0x10000)
		{
			nbytes += 2;
		}
		else if(ch < (((unsigned long)1) << 20))
		{
			nbytes += 4;
		}
	}
	return nbytes;
}

/*
 * Write a length-specified unicode string to a binary output stream.
 */
unsigned char *str2uni(const char *str, int len)
{
	int posn = 0;
	char buf[4];
	unsigned char *bout, *binaryout;
	int templen;
	int blen,i;

	blen = unilen(str, len);

	bout = (unsigned char *)calloc(blen, sizeof(unsigned char));
	binaryout = bout;
	if (!bout) 
	{
		fprintf(stderr, "str2unicode: out of memory\n");
		return NULL;
	}
	
	/* Write out the contents of the string */
	while(posn < len)
	{
		templen = UTF16WriteCharAsBytes(buf, UTF8ReadChar(str, len, &posn));
		for(i = 0; i< templen; i++)
		{
			*bout++ = buf[i];
		}
	}
	
	return binaryout;
}

static int readbin(unsigned char **data, unsigned int *size, unsigned char **dat, unsigned int len)
{
	unsigned char *d = *data;
	unsigned int s = *size;
	
	if( s != 0 )
	{			
		*dat = (unsigned char *)calloc(len, sizeof(unsigned char));
		if (!dat) 
		{
			fprintf(stderr, "read_binary: out of memory\n");
			return 0;
		}
		
		if(((unsigned int)(len)) <= s)
		{
			memcpy((*dat),d,len);
			*data = d + len;
			*size = s - len;
			return 1;
		}
	}
	fprintf(stderr, "read_binary: invalid binary data\n");
	return 0;
}

/*
 * Returns zero if the data is badly formatted.
 */
static int uni2str(unsigned char *d, unsigned int length, char **str, unsigned int *len)
{
	unsigned unich;
	
	if(length != 0)
	{
		/* Allocate space for the converted string */
		// Two unicode characters make up 1 buffer byte. Round up
		if((*str = (char *)calloc(length*2 + 1, sizeof(char))) == 0)
		{
			fprintf(stderr, "unicode2str: out of memory\n");
			return 0;
		}

		/* Convert the string from Unicode into UTF-8 */
		*len = 0;
		while(length >= 2)
		{
			unich = READ_UINT16(d);
			d += 2;
			if(unich < 0x80)
			{
				(*str)[*len] = (char)unich;
				++(*len);
			}
			else if(unich < (1 << 11))
			{
				(*str)[*len] = (char)(0xC0 | (unich >> 6));
				++(*len);
				(*str)[*len] = (char)(0x80 | (unich & 0x3F));
				++(*len);
			}
			else
			{
				(*str)[*len] = (char)(0xE0 | (unich >> 12));
				++(*len);
				(*str)[*len] = (char)(0x80 | ((unich >> 6) & 0x3F));
				++(*len);
				(*str)[*len] = (char)(0x80 | (unich & 0x3F));
				++(*len);
			}
			length -= 2;
		}
		(*str)[*len] = '\0';
		return 1;
	}
	fprintf(stderr, "unicode2str: invalid binary unicode data\n");
	return 0;
}

unsigned char _nibbleValue(CHAR16 hexchar)
{
	unsigned char val;
	
	if(hexchar >= '0' && hexchar <= '9')
		val = hexchar - '0';
	else if(hexchar >= 'A' && hexchar <= 'F')
		val = hexchar - 'A' + 10;
	else if(hexchar >= 'a' && hexchar <= 'f')
		val = hexchar - 'a' + 10;
	else
		val = 0xff;
	return(val);
	
}

int isHexString16(CHAR16 * buffer, unsigned int Size)
{
	int HexCnt;

	// Find out how many hex characters the string has.
	for (HexCnt = 0; IS_HEX( buffer[HexCnt] ); HexCnt++);
	
	if( (HexCnt == Size) && (HexCnt != 0) ) return 1;					
	
	return 0;	
}

const unsigned char _HexTabLC[16]= "0123456789abcdef";

/* binary to HEX conversion function -- for Unicode support */
char *bin2hex(const unsigned char *data, unsigned long size)
{
	long i;
	unsigned char *pin = (unsigned char *)data;
	char *pout, *p;
	unsigned char c;
	// one binary byte make up 2 hex characters
	pout = p = (char *)calloc((size * 2) + 1, sizeof(char)); 
	if (!p) 
	{
		fprintf(stderr, "bin2hex: out of memory\n");
		return 0;
	}
	
	for(i=0; i < size; i++) 
	{
		c = *pin++;
		*p++ = _HexTabLC[c >> 4];
		*p++ = _HexTabLC[c & 0xf];
	}
	*p = '\0';
	return(pout);
}

/* After the call, length will contain the byte length of binary data. */
unsigned char *hex2bin(const char *data, unsigned long *size)
{
	long bcount = 0;
	char *pin = (char *)data;
	unsigned char *pout, *p;
	unsigned char ch;
	int HighNibble = 1, HexCnt = 0;

	// Find out how many hex characters the string has.
	for (HexCnt = 0; IS_HEX(data[HexCnt]); HexCnt++);
	if (HexCnt == 0) 
	{
		*size = 0;
		return NULL;
	}	
	// two hex characters make up 1 binary byte. Round up.
	pout = p = (unsigned char *)calloc((HexCnt + 1) / 2, sizeof(unsigned char)); 
	if (!p) 
	{
		fprintf(stderr, "hex2bin: out of memory\n");
		return NULL;
	}
	
	// read until string end
	for( ;*pin != '\0'; pin++)
	{
		ch = _nibbleValue(*pin);
		if(ch > 15)
			continue;		// not a HEX character

		if(HighNibble) 
		{
			*p = ch << 4;
			HighNibble = 0;
		}
		else 
		{
			*p++ |= ch;
			HighNibble = 1;
			bcount++;
		}
	}
	
	*size = bcount;	/* byte count */
	return(pout);
}

/* After the call, length will contain the byte length of binary data. */
unsigned char *hex2bin16(CHAR16 *data, unsigned long *size)
{
	long bcount = 0;
	CHAR16 *pin = data;
	unsigned char *pout, *p;
	unsigned char ch;
	int HighNibble = 1, HexCnt = 0;

	// Find out how many hex characters the string has.
	for (HexCnt = 0; IS_HEX(data[HexCnt]); HexCnt++);
	if (HexCnt == 0)
	{
		*size = 0;
		return NULL;
	}	
	// two hex characters make up 1 binary byte. Round up.
	pout = p = (unsigned char *)calloc((HexCnt + 1) / 2, sizeof(unsigned char)); 
	if (!p)
	{
		fprintf(stderr, "hex2bin16: out of memory\n");
		return NULL;
	}
	
	// read until string end
	for( ;*pin != '\0'; pin++)
	{
		ch = _nibbleValue(*pin);
		if(ch > 15)
			continue;		// not a HEX character

		if(HighNibble)
		{
			*p = ch << 4;
			HighNibble = 0;
		}
		else 
		{
			*p++ |= ch;
			HighNibble = 1;
			bcount++;
		}
	}
	
	*size = bcount;	/* byte count */
	return(pout);
}

int is_string(void * buffer, int size)
{
	int i;
	
	for(i=0;i < size; i++)
	{
		if(!IS_ALPHANUMMARK( ((unsigned char *)buffer)[i]) ) return 0;
	}
	return size > 0; // && ((unsigned char *)buffer)[size-1] == '\0';
}

void dump_buffer(void * buffer, int size)
{
	int i;
	
	printf("Buffer size: %d, data = <",size);
	for(i=0;i < size; ++i)
	{
		printf("%c",((char *)buffer)[i]);
	}
	printf(">\n");
}

// this writes gfx data to binary file
unsigned char *gfx2bin(GFX_HEADER *gfx)
{
	GFX_BLOCKHEADER *gfx_blockheader_tmp;	
	GFX_ENTRY *gfx_entry_tmp;
	unsigned char *buffer, *buffer_head;
	
	if(gfx->filesize > 0)
	{
		buffer_head = buffer = (unsigned char *)calloc(gfx->filesize, sizeof(unsigned char));
		if (!buffer) 
		{
			fprintf(stderr, "gfx2bin: out of memory\n");
			return NULL;
		}	
		
		WRITE_UINT32(buffer, gfx->filesize);
		buffer +=4;
		WRITE_UINT32(buffer, gfx->var1);	
		buffer +=4;
		WRITE_UINT32(buffer, gfx->countofblocks);
		buffer +=4;
				
		gfx_blockheader_tmp = gfx->blocks;
		while(gfx_blockheader_tmp)
		{		
			WRITE_UINT32(buffer, gfx_blockheader_tmp->blocksize);
			buffer +=4;
			WRITE_UINT32(buffer, gfx_blockheader_tmp->records);
			buffer +=4;
			//write header
			memcpy(buffer,gfx_blockheader_tmp->devpath,gfx_blockheader_tmp->devpath_len);
			buffer += gfx_blockheader_tmp->devpath_len;
			gfx_entry_tmp = gfx_blockheader_tmp->entries;
			while(gfx_entry_tmp)
			{
				WRITE_UINT32(buffer, gfx_entry_tmp->bkey_len + 4); // 4bytes - include length record too
				buffer +=4;
				memcpy(buffer, gfx_entry_tmp->bkey, gfx_entry_tmp->bkey_len);
				buffer += gfx_entry_tmp->bkey_len;
				WRITE_UINT32(buffer, gfx_entry_tmp->val_len +4); // 4bytes - include length record too
				buffer +=4;
				memcpy(buffer, gfx_entry_tmp->val, gfx_entry_tmp->val_len);
				buffer += gfx_entry_tmp->val_len;
				gfx_entry_tmp = gfx_entry_tmp->next;
			}
			gfx_blockheader_tmp = gfx_blockheader_tmp->next;
		}	
	
		return buffer_head;
	}
	fprintf(stderr, "gfx2bin: invalid binary data\n");
	return NULL;					
}

static void free_gfx_blockheader_list(GFX_BLOCKHEADER *head, GFX_BLOCKHEADER *end)
{
	GFX_BLOCKHEADER *tmp;

	do {
		if (head) {
			tmp = head;
			head = head->next;
			free(tmp);
		}
	} while (head != end);
}

static void free_gfx_entry_list(GFX_ENTRY *head, GFX_ENTRY *end)
{
	GFX_ENTRY *tmp;

	do {
		if (head) {
			tmp = head;
			head = head->next;
			free(tmp);
		}
	} while (head != end);
}

// this reads gfx binary info and parses it
GFX_HEADER *parse_binary(unsigned char * bp, unsigned char * bpend, SETTINGS *settings)
{
	GFX_HEADER *gfx_header = NULL;
	// head points to the first node in list, end points to the last node in list
	GFX_BLOCKHEADER *gfx_blockheader = NULL;
	GFX_BLOCKHEADER *gfx_blockheader_head = NULL;
	GFX_BLOCKHEADER *gfx_blockheader_end = NULL;
	GFX_ENTRY *gfx_entry = NULL;
	GFX_ENTRY *gfx_entry_head = NULL;
	GFX_ENTRY *gfx_entry_end = NULL;
	unsigned char *data = NULL, *bin = NULL, *tmp = NULL, *dpathtmp = NULL;
	char * str = NULL;
	unsigned int str_len, data_len, size, length;	
	int i,j;

	//read header data	
	gfx_header = (GFX_HEADER *)calloc(1, sizeof(GFX_HEADER));	
	if(!gfx_header)
	{
		fprintf(stderr, "parse_binary: out of memory\n");
		return NULL;	
	}

	gfx_header->filesize = READ_UINT32(bp);
	bp+=4;
	
	gfx_header->var1 = READ_UINT32(bp);
	bp+=4;
	
	gfx_header->countofblocks = READ_UINT32(bp);		
	bp+=4;

	//read blocks
	gfx_blockheader_head = NULL;
	gfx_blockheader_end = NULL;
	for(i=0;i<gfx_header->countofblocks;i++)
	{
		//create new block
		gfx_blockheader = (GFX_BLOCKHEADER *)calloc(1, sizeof(GFX_BLOCKHEADER));
		if(!gfx_blockheader)
		{
			fprintf(stderr, "parse_binary: out of memory\n");
			goto error;
		}
		//read block data
		gfx_blockheader->blocksize = READ_UINT32(bp);
		bp+=4;
	
		gfx_blockheader->records = READ_UINT32(bp);
		bp+=4;
		
		size = gfx_blockheader->blocksize;
		
		Boolean foundend = false;
		// read device path data until devpath end node 0x0004FF7F
		for (tmp = bp; tmp+4 <= bpend && !foundend; tmp+=READ_UINT16(tmp+2))
		{
			if( READ_UINT32(tmp) == 0x0004ff7f || READ_UINT32(tmp) == 0x0004ffff )
				foundend = true;
		}

		if (!foundend)
		{
			// BugBug: Code to catch bogus device path
			fprintf(stderr, "parse_binary: Cannot find device path end! Probably a bogus device path.\n");
			goto error;
		}
		
		// read device path data
		gfx_blockheader->devpath_len = (unsigned int)abs((int)(tmp-bp));
		assert(readbin(&bp, &size, &dpathtmp,gfx_blockheader->devpath_len));
		gfx_blockheader->devpath = (EFI_DEVICE_PATH_PROTOCOL *)dpathtmp;
		
		gfx_entry_head = NULL;
		gfx_entry_end = NULL;
		for(j=1;j <= gfx_blockheader->records;j++)
		{
			length = READ_UINT32(bp);
			length -= 4; bp += 4; size -=4;	
			if(readbin(&bp, &size, &bin, length))
			{
				if(!uni2str(bin, length, &str, &str_len))
				{
					goto error;
				}
			}
			else
			{
				goto error;
			}
			
			data_len = READ_UINT32(bp);
			data_len -= 4; bp += 4; size -=4;
			if(!readbin(&bp, &size, &data, data_len))
			{
				goto error;
			}
			
			gfx_entry = (GFX_ENTRY *)calloc(1, sizeof(GFX_ENTRY));			
			if(!gfx_entry)
			{
				fprintf(stderr, "parse_binary: out of memory\n");
				goto error;
			}
			//read entries
			gfx_entry->bkey = bin;
			gfx_entry->bkey_len = length;				
			gfx_entry->key = str;
			gfx_entry->key_len = str_len;
			gfx_entry->val_type = DATA_BINARY; // set default data type
			gfx_entry->val = data;
			gfx_entry->val_len = data_len;
			
			if(settings->detect_numbers)	// detect numbers
			{			
				switch(data_len)
				{
					case sizeof(UINT8): // int8
						gfx_entry->val_type = DATA_INT8;
					break;
					case sizeof(UINT16): //int16
						gfx_entry->val_type = DATA_INT16;
					break;
					case sizeof(UINT32): //int32
						gfx_entry->val_type = DATA_INT32;
					break;
					default:
						gfx_entry->val_type = DATA_BINARY;
					break;
				}
			}
			
			// detect strings
			if(settings->detect_strings && is_string(data, data_len) && gfx_entry->val_type == DATA_BINARY)
			{
				gfx_entry->val_type = DATA_STRING;
			}
			
			if(!gfx_entry_head)							// if there are no nodes in list then
				gfx_entry_head = gfx_entry;				// set head to this new node			
			if(gfx_entry_end)
				gfx_entry_end->next = gfx_entry;		// link in new node to the end of the list
			gfx_entry->next = NULL;						// set next field to signify the end of list
			gfx_entry_end = gfx_entry;					// adjust end to point to the last node
		}
				
		gfx_blockheader->entries = gfx_entry_head;
		
		if(!gfx_blockheader_head)						// if there are no nodes in list then
			gfx_blockheader_head = gfx_blockheader;		// set head to this new node		
		if(gfx_blockheader_end)
			gfx_blockheader_end->next = gfx_blockheader;// link in new node to the end of the list
		gfx_blockheader->next = NULL;					// set next field to signify the end of list
		gfx_blockheader_end = gfx_blockheader;			// adjust end to point to the last node
	}
	
	gfx_header->blocks = gfx_blockheader_head;

	return (gfx_header);
error:
	free(str);
	free(data);
	free(bin);
	free_gfx_blockheader_list(gfx_blockheader_head, gfx_blockheader_end);
	free_gfx_entry_list(gfx_entry_head, gfx_entry_end);
	free(gfx_blockheader);
	free(gfx_header);
	return NULL;
}

CFDictionaryRef CreateGFXDictionary(GFX_HEADER * gfx)
{
	CFMutableDictionaryRef dict, items;
	CFDataRef data = NULL;
	//CFNumberRef number = NULL;
	CFStringRef string = NULL;
	CFStringRef key = NULL; 
	GFX_BLOCKHEADER *gfx_blockheader_tmp;	
	GFX_ENTRY *gfx_entry_tmp;	
	uint64_t bigint;
	char hexstr[32];
	CHAR16 *dpath;
	
	// Create dictionary that will hold gfx data
	dict = CFDictionaryCreateMutable(kCFAllocatorDefault, 0 ,&kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);

	gfx_blockheader_tmp = gfx->blocks;
	while(gfx_blockheader_tmp)
	{
		items = CFDictionaryCreateMutable(kCFAllocatorDefault, 0 ,&kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
		gfx_entry_tmp = gfx_blockheader_tmp->entries;
		while(gfx_entry_tmp)
		{
			key = CFStringCreateWithCString(kCFAllocatorDefault, gfx_entry_tmp->key, kCFStringEncodingUTF8);
			switch(gfx_entry_tmp->val_type)
			{
				case DATA_STRING:
					string = CFStringCreateWithBytes(kCFAllocatorDefault,gfx_entry_tmp->val, gfx_entry_tmp->val_len, kCFStringEncodingASCII, false);
					CFDictionarySetValue(items, key, string);
					CFRelease(string);
					CFRelease(key);											
				break;
				case DATA_INT8:
					bigint = READ_UINT8(gfx_entry_tmp->val);
					sprintf(hexstr,"0x%02llx",bigint);
					string = CFStringCreateWithCString(kCFAllocatorDefault,hexstr, kCFStringEncodingASCII);
					CFDictionarySetValue(items, key, string);
					CFRelease(string);
					CFRelease(key);													
				break;
				case DATA_INT16:
					bigint = READ_UINT16(gfx_entry_tmp->val);
					sprintf(hexstr,"0x%04llx",bigint);
					string = CFStringCreateWithCString(kCFAllocatorDefault,hexstr, kCFStringEncodingASCII);
					CFDictionarySetValue(items, key, string);
					CFRelease(string);
					CFRelease(key);										
				break;
				case DATA_INT32:
					bigint = READ_UINT32(gfx_entry_tmp->val);
					sprintf(hexstr,"0x%08llx",bigint);
					string = CFStringCreateWithCString(kCFAllocatorDefault,hexstr, kCFStringEncodingASCII);
					CFDictionarySetValue(items, key, string);
					CFRelease(string);
					CFRelease(key);										
				break;
				default:				
				case DATA_BINARY:
					data = CFDataCreate(kCFAllocatorDefault,gfx_entry_tmp->val, gfx_entry_tmp->val_len);
					CFDictionarySetValue(items, key, data);
					CFRelease(data);
					CFRelease(key);					
				break;			
			}
			gfx_entry_tmp = gfx_entry_tmp->next;
		}

		VerifyDevicePathNodeSizes(gfx_blockheader_tmp->devpath);
		dpath = PatchedConvertDevicePathToText (gfx_blockheader_tmp->devpath, 1, 1);
		if(dpath != NULL)
		{
			key = CFStringCreateWithCharacters(kCFAllocatorDefault, dpath, StrLen(dpath));
		}
		else
		{
			printf("CreateGFXDictionary: error converting device path to text shorthand notation\n");
			return NULL;			
		}
	
		CFDictionarySetValue(dict, key, items);
		
		free(dpath);
		CFRelease(key);
		CFRelease(items);							
		gfx_blockheader_tmp = gfx_blockheader_tmp->next;
	}

	return dict;
}

GFX_HEADER *CreateGFXFromPlist(CFPropertyListRef plist)
{
	int num_blocks, num_rec;
	int block_size, gfx_size;
	int i, IsHex;
	unsigned int HexBytes = 0;
	unsigned long len;
	char *HexStr = NULL;
	unsigned char *bytes = NULL;
	CHAR16* bytes16 = NULL;
	unsigned char *data = NULL;
	CFStringRef *block_keys = NULL;
	CFTypeRef *block_vals = NULL;
	CFStringRef *dict_keys = NULL;
	CFDictionaryRef *dict_vals = NULL;
	CFMutableDictionaryRef this_block;
	GFX_HEADER *gfx_header;
	// head points to the first node in list, end points to the last node in list
	GFX_BLOCKHEADER *gfx_blockheader = (GFX_BLOCKHEADER *) NULL; 
	GFX_BLOCKHEADER *gfx_blockheader_head = (GFX_BLOCKHEADER *) NULL;
	GFX_BLOCKHEADER *gfx_blockheader_end = (GFX_BLOCKHEADER *) NULL;
	GFX_ENTRY *gfx_entry = (GFX_ENTRY *) NULL; 
	GFX_ENTRY *gfx_entry_head = (GFX_ENTRY *) NULL;
	GFX_ENTRY *gfx_entry_end = (GFX_ENTRY *) NULL;
	CFIndex ret, count;
	CFIndex needed;	
	uint64_t bigint;
	
	num_blocks = (int)CFDictionaryGetCount(plist);
	if(!num_blocks)
	{
		printf("CreateGFXFromPlist: no dictionaries found in property list\n");
		return NULL;	
	} 
	
	dict_keys = CFAllocatorAllocate(NULL, num_blocks * sizeof(CFStringRef), 0);
	dict_vals = CFAllocatorAllocate(NULL, num_blocks * sizeof(CFDictionaryRef), 0);
	
	if(!dict_keys || !dict_vals)
	{
		fprintf(stderr, "CreateGFXFromPlist: out of memory\n");
		return NULL;	
	}
	//create header data	
	gfx_header = (GFX_HEADER *)calloc(1,sizeof(GFX_HEADER));	
	gfx_header->filesize = 0; // we dont know yet
	gfx_header->var1 = 0x1;	
	gfx_header->countofblocks = num_blocks;
	
	CFDictionaryGetKeysAndValues(plist, (const void **)dict_keys, (const void **)dict_vals);
	
	gfx_size = 12; // set first 12 bytes
	gfx_blockheader_head = NULL;	
	gfx_blockheader_end = NULL;
	
	
	for(i=0; i<num_blocks; i++)
	{
		this_block = (CFMutableDictionaryRef) CFDictionaryGetValue(plist,dict_keys[i]);		
		num_rec = (int)CFDictionaryGetCount(this_block);
		
		if(!num_rec)
		{
			printf("CreateGFXFromPlist: empty dictionary block found in property list\n");
			goto error;
		}

		block_size=0;
		block_size+=4; // size record itself
		block_size+=4; // entries count record		

		block_keys = CFAllocatorAllocate(NULL, num_rec * sizeof(CFStringRef), 0);
		block_vals = CFAllocatorAllocate(NULL, num_rec * sizeof(CFTypeRef), 0);
		
		gfx_blockheader = (GFX_BLOCKHEADER *)calloc(1,sizeof(GFX_BLOCKHEADER));
		gfx_blockheader->blocksize = 0; // dont know yet
		gfx_blockheader->records = num_rec;
		
		count = CFStringGetLength(dict_keys[i]) + 1;
		bytes16 = calloc(count, sizeof(CHAR16));
		if (!bytes16)
		{
			fprintf(stderr, "CreateGFXFromPlist: out of memory\n");
			goto error;
		}
		ret = CFStringGetBytes(dict_keys[i], CFRangeMake(0, count-1), kCFStringEncodingUTF16, 0, false, (void*) bytes16, count * sizeof(CHAR16), &needed);
		if(ret != count-1) // not utf16 string
		{
			fprintf(stderr, "CreateGFXFromPlist: string conversion error occured, not UTF16 string!\n");
			goto error;
		}
		// add at end string terminator
		bytes16[ret] = '\0';
		// is hex or text notation
		if(isHexString16(bytes16,(unsigned int)ret))
		{
			// hexadecimal devicepath
			gfx_blockheader->devpath = (EFI_DEVICE_PATH_PROTOCOL *)hex2bin16(bytes16, &len);
		}
		else
		{
			// try convert from text notation
			gfx_blockheader->devpath = ConvertTextToDevicePath (bytes16);
		}
		free(bytes16);
		bytes16 = NULL;

		if(gfx_blockheader->devpath == NULL)
		{
			fprintf(stderr, "CreateGFXFromPlist: device path conversion error occured, not correct syntax!\n");
			goto error;
		}

		gfx_blockheader->devpath_len = (unsigned int)UefiDevicePathLibGetDevicePathSize (gfx_blockheader->devpath);
		block_size+= gfx_blockheader->devpath_len; // header bytes count				
		
		CFDictionaryGetKeysAndValues(this_block, (const void **)block_keys, (const void **)block_vals);
		gfx_entry_head = NULL;
		gfx_entry_end = NULL;
		while(--num_rec >= 0)
		{
			gfx_entry = (GFX_ENTRY *)calloc(1,sizeof(GFX_ENTRY));
			count = CFStringGetLength(block_keys[num_rec]) + 1;
				
			bytes = (unsigned char *)calloc(count, sizeof(unsigned char));	
			if (!bytes) 
			{
				fprintf(stderr, "CreateGFXFromPlist: out of memory\n");
				goto error;
			}
			
			ret = CFStringGetBytes(block_keys[num_rec], CFRangeMake(0, count-1), kCFStringEncodingASCII, 0, false, bytes, count, &needed);
			if(ret != count-1) // not ascii string
			{
				fprintf(stderr, "CreateGFXFromPlist: string conversion error occured, not ascii string!\n");
				goto error;
			}
			// add at end string terminator
			bytes[ret] = '\0';
			
			gfx_entry->key = (char *)bytes;
			gfx_entry->key_len = (unsigned int)ret;
			
			gfx_entry->bkey = str2uni((char *)bytes, (int)ret);
			gfx_entry->bkey_len = unilen((char *)bytes, (int)ret);
			
			block_size+=4; // key len
			block_size+=gfx_entry->bkey_len;			
			
			if(CFGetTypeID(block_vals[num_rec]) == CFStringGetTypeID())
			{	
				count = CFStringGetLength(block_vals[num_rec]) + 1;
				
				bytes = (unsigned char *)calloc(count, sizeof(unsigned char));				
				if (!bytes) 
				{
					fprintf(stderr, "CreateGFXFromPlist: out of memory\n");
					goto error;
				}
				
				ret = CFStringGetBytes(block_vals[num_rec], CFRangeMake(0, count-1), kCFStringEncodingASCII, 0, false, bytes, count, &needed);
				if(ret != count-1) // not ascii string
				{
					fprintf(stderr, "CreateGFXFromPlist: string conversion error occured, not ascii string!\n");
					goto error;
				}
				// add at end string terminator
				bytes[ret] = '\0';
				
				// if is 0xXX or 0xXXXX or 0xXXXXXXXX hex string
				HexStr = TrimHexStr ((char *)bytes, &IsHex);
				if(IsHex)
				{
					bigint = Xtoi (HexStr, &HexBytes);
					switch(HexBytes)
					{
						case 2:
							needed = sizeof(unsigned char);
							data = (unsigned char *)calloc(needed, sizeof(unsigned char));
							if (!data) 
							{
								fprintf(stderr, "CreateGFXFromPlist: out of memory\n");
								return NULL;
							}
				
							WRITE_UINT8(data, bigint);
							gfx_entry->val = data;
							gfx_entry->val_len = (unsigned int)needed;
							gfx_entry->val_type = DATA_INT8;							
						break;
						case 4:
							needed = sizeof(unsigned short);
							data = (unsigned char *)calloc(needed, sizeof(unsigned char));
							if (!data) 
							{
								fprintf(stderr, "CreateGFXFromPlist: out of memory\n");
								return NULL;
							}
				
							WRITE_UINT16(data, bigint);
							gfx_entry->val = data;
							gfx_entry->val_len = (unsigned int)needed;
							gfx_entry->val_type = DATA_INT16;						
						break;
						case 8:
							needed = sizeof(unsigned int);
							data = (unsigned char *)calloc(needed, sizeof(unsigned char));
							if (!data) 
							{
								fprintf(stderr, "CreateGFXFromPlist: out of memory\n");
								return NULL;
							}
				
							WRITE_UINT32(data, bigint);
							gfx_entry->val = data;
							gfx_entry->val_len = (unsigned int)needed;
							gfx_entry->val_type = DATA_INT32;												
						break;
						default:
							fprintf(stderr, "CreateGFXFromPlist: incompatible hex string size, (only 0xXX or 0xXXXX or 0xXXXXXXXX hex strings accepted)\n");
							return NULL;						
						break;						
					}					
				}
				else
				{
					gfx_entry->val = bytes;
					// strings in device properties don't end with null so ignore the fact that strings in IORegExplorer do end with \0.
					gfx_entry->val_len = (unsigned int)(ret);
					gfx_entry->val_type = DATA_STRING;
				}			
			}
			else if(CFGetTypeID(block_vals[num_rec]) == CFNumberGetTypeID())
			{
				needed = sizeof(unsigned int);
				bytes = (unsigned char *)calloc(needed, sizeof(unsigned char));
				if (!bytes) 
				{
					fprintf(stderr, "CreateGFXFromPlist: out of memory\n");
					goto error;
				}
				
				CFNumberGetValue(block_vals[num_rec], kCFNumberSInt64Type, &bigint);
				WRITE_UINT32(bytes, bigint);
				gfx_entry->val = bytes;
				gfx_entry->val_len = (unsigned int)needed;
				gfx_entry->val_type = DATA_INT32; //only known number type from plist										
			}
			else if(CFGetTypeID(block_vals[num_rec]) == CFBooleanGetTypeID())
			{
				needed = sizeof(unsigned char);
				bytes = (unsigned char *)calloc(needed, sizeof(unsigned char));
				if (!bytes) 
				{
					fprintf(stderr, "CreateGFXFromPlist: out of memory\n");
					goto error;
				}
				bigint = CFBooleanGetValue(block_vals[num_rec]);
				WRITE_UINT8(bytes, bigint);
				gfx_entry->val = bytes;
				gfx_entry->val_len = (unsigned int)needed;
				gfx_entry->val_type = DATA_INT8;									
			}			
			else // data type
			{
				needed = CFDataGetLength(block_vals[num_rec]);
				bytes = (unsigned char *)calloc(needed, sizeof(unsigned char));
				if (!bytes) 
				{
					fprintf(stderr, "CreateGFXFromPlist: out of memory\n");
					goto error;
				}
				CFDataGetBytes(block_vals[num_rec], CFRangeMake(0,needed), bytes);
				gfx_entry->val = bytes;
				gfx_entry->val_len = (unsigned int)needed;
				gfx_entry->val_type = DATA_BINARY;						
			}
						
			block_size+=4; // value len
			block_size+=gfx_entry->val_len;		

			if(!gfx_entry_head)							// if there are no nodes in list then
				gfx_entry_head = gfx_entry;				// set head to this new node			
			if(gfx_entry_end)
				gfx_entry_end->next = gfx_entry;		// link in new node to the end of the list
			gfx_entry->next = NULL;						// set next field to signify the end of list
			gfx_entry_end = gfx_entry;					// adjust end to point to the last node
		}
		gfx_size+=block_size;
		gfx_blockheader->blocksize = block_size;
		gfx_blockheader->entries = gfx_entry_head;

		if(!gfx_blockheader_head)						// if there are no nodes in list then
			gfx_blockheader_head = gfx_blockheader;		// set head to this new node		
		if(gfx_blockheader_end)
			gfx_blockheader_end->next = gfx_blockheader;// link in new node to the end of the list
		gfx_blockheader->next = NULL;					// set next field to signify the end of list
		gfx_blockheader_end = gfx_blockheader;			// adjust end to point to the last node
		
		CFAllocatorDeallocate(NULL, block_keys);
		CFAllocatorDeallocate(NULL, block_vals);
	}
	gfx_header->filesize = gfx_size;
	gfx_header->blocks = gfx_blockheader_head;

	CFAllocatorDeallocate(NULL, dict_keys);
	CFAllocatorDeallocate(NULL, dict_vals);
	return gfx_header;
	
error:
	free(bytes16);
	free(bytes);
	free_gfx_entry_list(gfx_entry_head, gfx_entry_end);
	free(gfx_entry);
	free(gfx_header);
	free_gfx_blockheader_list(gfx_blockheader_head, gfx_blockheader_end);
	free(gfx_blockheader);
	return NULL;
}

int WritePropertyList(CFPropertyListRef propertyList, CFURLRef fileURL)
{
	CFWriteStreamRef stream;
	CFIndex ret = -1;
	
	if(propertyList && fileURL)
	{
		stream = CFWriteStreamCreateWithFile(kCFAllocatorDefault, fileURL);
	
		if (stream)
		{
			if(CFWriteStreamOpen(stream))
			{
				ret = CFPropertyListWrite(propertyList, stream, kCFPropertyListXMLFormat_v1_0, 0, NULL);
				CFWriteStreamClose(stream);
			}

			CFRelease(stream);
		}
	}	
	return (int)ret;
}

CFPropertyListRef ReadPropertyList(CFURLRef fileURL)
{
	CFReadStreamRef stream;
	CFPropertyListRef plist = NULL;
	CFErrorRef error = NULL;
	CFPropertyListFormat format;
		
	stream = CFReadStreamCreateWithFile(kCFAllocatorDefault, fileURL);
	
	if(CFReadStreamOpen(stream))
	{
		plist = CFPropertyListCreateWithStream(kCFAllocatorDefault, stream, 0, kCFPropertyListImmutable, &format, &error); // 0 streamlength, read to EOF
		CFReadStreamClose(stream);
		CFRelease(stream);	
	}
	else
	{
		if (stream) CFRelease(stream);
	}

	if(error)
	{
		CFRelease(error);
		if (plist) CFRelease(plist);
		return NULL;
	}

	if(plist == NULL) return NULL;
	
	if(CFDictionaryGetTypeID() != CFGetTypeID(plist))
	{
		CFRelease(plist);
		return NULL;
	}
	if(!CFPropertyListIsValid(plist, kCFPropertyListXMLFormat_v1_0))
	{
		CFRelease(plist);
		return NULL;
	}
		
	return plist;
}

CFURLRef URLCreate(const char *path)
{
	CFStringRef cfpath = CFStringCreateWithCStringNoCopy(kCFAllocatorDefault, path, kCFStringEncodingASCII, kCFAllocatorNull);
	if (cfpath)
	{
		CFURLRef url = CFURLCreateWithFileSystemPath(kCFAllocatorDefault, cfpath, kCFURLPOSIXPathStyle, false);
		CFRelease(cfpath);
		return url;
	}

	return NULL;
}

static void usage()
{
	fprintf(stdout, "\n");
	fprintf(stdout, "GFX conversion utility version: %s. Copyright (c) 2007 McMatrix\n",VERSION);
	fprintf(stdout, "This program comes with ABSOLUTELY NO WARRANTY. This is free software!\n");
	fprintf(stdout, "\n");
	fprintf(stdout, "gfxutil: [command_flags] [command_option] [other_options] [[infile] outfile | efidevicepath]\n");
	fprintf(stdout, "\n");
	fprintf(stdout, "Command flags are:\n");
	fprintf(stdout, "-v              verbose mode\n");
	fprintf(stdout, "-s              automatically detect string format from binary data\n");
	fprintf(stdout, "-n              automatically detect numeric format from binary data\n");
	fprintf(stdout, "-l              shorter text representation of the display node is used, where applicable\n");
	fprintf(stdout, "                (shorter text representations are not reversable)\n");
	fprintf(stdout, "-m              shortcut forms of text representation for a device node should not be used\n");
	fprintf(stdout, "                (shortcut forms are used be some vendor messages)\n");
	fprintf(stdout, "\n");
	fprintf(stdout, "Command options are:\n");
	fprintf(stdout, "-c, [pipe]      translate a device path to and from text\n");
	fprintf(stdout, "-p, default     list all devicepaths for PCI devices in IODeviceTree plane\n");
	fprintf(stdout, "-t              list all devicepaths for PCI and ACPI devices in IODeviceTree plane in tree order\n");
	fprintf(stdout, "-f name         finds object devicepath with the given name from IODeviceTree plane\n");
	fprintf(stdout, "-i fmt          infile type, fmt is one of (default is hex): xml bin hex\n");
	fprintf(stdout, "-o fmt          outfile type, fmt is one of (default is xml): xml bin hex\n");
	fprintf(stdout, "-d path prop    output the efi device path, path is the ioreg path, prop is the property\n");
	fprintf(stdout, "                (Eg. gfxutil -d IODeviceTree:/chosen boot-device-path)\n");
	fprintf(stdout, "\n");
	fprintf(stdout, "Other command options are:\n");
	fprintf(stdout, "-h              print this summary\n");
	fprintf(stdout, "-a              show version\n");
	fprintf(stdout, "\n");
}

int translate_properties (char * argv[], SETTINGS *settings);

int handle_pipe(SETTINGS *settings)
{
	unsigned long argtlenmax = 10000;
	char* argt = malloc(argtlenmax);
	if (!argt)
		return 1;
	argt[0] = '\0';
	unsigned long argtlen = 0;
	for (;;)
	{
		if (argtlen == argtlenmax - 1)
		{
			argtlenmax += 10000;
			argt = realloc(argt, argtlenmax);
			if (!argt)
				return 1;
		}
		unsigned long readlen = fread(argt + argtlen, 1, argtlenmax - argtlen - 1, stdin);
		if (!readlen)
			break;
		argtlen += readlen;
	}
	argt[argtlen] = '\0';
	return parse_generic_option(argt, argtlen, settings);
}

int parse_args(int argc, char * argv[], SETTINGS *settings)
{
	int c,i;
	int iflag = 0;
	int oflag = 0;
	extern char *optarg;
	extern int optind, optopt;
	
	// set default values
	settings->ifile_type = FILE_HEX;
	settings->ofile_type = FILE_XML;
	settings->verbose = 0;
	settings->detect_strings = 0;
	settings->detect_numbers = 0;
	settings->display_only = 0;
	settings->allow_shortcuts = 1;
	settings->search = NULL;
	settings->matched = false;
	settings->plane = kIODeviceTreePlane;
	
	while((c = getopt(argc, argv, "vsnlmahdcptf:i:o:") ) != -1)
	{
		switch(c)
		{
			case 'f':
				settings->search = optarg;
				settings->matched = false;
				OutputPCIDevicePathsByTree(settings);
				if (!settings->matched)
				{
					printf("DevicePath not found!\n");
					return 1;
				}
				return 0;
			case 'd':
				if (optind + 2 != argc) {
					usage();
					return 1;
				}
				
				io_registry_entry_t device = IORegistryEntryFromPath(kIOMasterPortDefault, argv[optind++]);
				
				if (device != MACH_PORT_NULL)
				{
					char buffer[4096] = {0};
					uint32_t size = sizeof(buffer);
					
					kern_return_t kr = IORegistryEntryGetProperty(device, argv[optind], buffer, &size);
					IOObjectRelease(device);

					if (kr == KERN_SUCCESS)
					{
						if (PrintDevicePathUtilToText(buffer, size, settings)) {
							printf("Error attempting to convert device property to text!\n");
							return 1;
						}
					}
					else
					{
						printf("DevicePath not found!\n");
						return 1;
					}
				}
				else
				{
					printf("DevicePath not found!\n");
					return 1;
				}
				return 0;
			case 'v':
				settings->verbose = 1;
				break;
			case 's':
				settings->detect_strings = 1;
				break;
			case 'n':
				settings->detect_numbers = 1;
				break;
			case 'a':
				printf("%s Version: %s by McMatrix\n",argv[0],VERSION);
				return 0;
			case 'h':
				usage();
				return 0;
			case 'i':
				iflag++;
				if(iflag > 1)
				{
					fprintf(stderr,"-i option given twice\n");
					return 1;
				}
				// I only speak lower case.
				for(i = 0; i < strlen(optarg); i++)
				{
					optarg[i] = tolower(optarg[i]);
				}
				
				if(!strcmp(optarg,"hex"))
				{
					settings->ifile_type = FILE_HEX;				
				}
				else if(!strcmp(optarg,"bin"))
				{
					settings->ifile_type = FILE_BIN;
				}
				else if(!strcmp(optarg,"xml"))
				{
					settings->ifile_type = FILE_XML;				
				}
				else
				{
					fprintf(stderr,"Unknown infile format option: %s\n",optarg);
					return 1;
				}
				break;
			case 'o':
				oflag++;
				if(oflag > 1)
				{
					fprintf(stderr,"-o option given twice\n");
					return 1;
				}			
				// I only speak lower case.
				for(i = 0; i < strlen(optarg) ; i++)
				{
					optarg[i] = tolower(optarg[i]);
				}
				
				if(!strcmp(optarg,"hex"))
				{
					settings->ofile_type = FILE_HEX;				
				}
				else if(!strcmp(optarg,"bin"))
				{		
					settings->ofile_type = FILE_BIN;			
				}
				else if(!strcmp(optarg,"xml"))
				{
					settings->ofile_type = FILE_XML;				
				}
				else
				{
					fprintf(stderr,"Unknown outfile format option: %s\n",optarg);
					return 1;
				}
				break;
			case '?':
				usage();
				return 1;
			case 'l':
				settings->display_only = 1;
				break;
			case 'm':
				settings->allow_shortcuts = 0;
				break;
			case 'c':
				return handle_pipe(settings);
			case 'p':
				return OutputPCIDevicePaths(settings);
			case 't':
				return OutputPCIDevicePathsByTree(settings);

		}
	}
	
	if (optind + 2 == argc)
	{
		strncpy(settings->ifile, argv[optind++], MAX_FILENAME);
		strncpy(settings->ofile, argv[optind++], MAX_FILENAME);
		return translate_properties(argv, settings);
	}
	else if (optind + 1 == argc)
	{
		return parse_generic_option(argv[optind], strlen(argv[optind]), settings);
	}
	else if (optind == argc)
	{
		if (isatty(fileno(stdin)))
		{
			return OutputPCIDevicePaths(settings);
		}
		return handle_pipe(settings);
	}
	else
	{
		usage();
		return 1;
	}
	
	return 0;
}

/* Returns the size of file in bytes */
long getFileSize(const char *file)
{
	long filesize = 0;
	struct stat filestat;

	if(stat(file, &filestat))
	{
		filesize = 0;		/* stat failed -- no such file */
	}
	else
	{
		if( !(filestat.st_mode & S_IFREG) )	filesize = 0;	/* not a regular file */
		else if( !(filestat.st_mode & S_IREAD) ) filesize = 0;	/* not readable */
		else filesize = (long)filestat.st_size;
	}
	
	return(filesize);
}

static void indent(Boolean isNode, UInt32 depth, UInt64 stackOfBits, Boolean newline)
{
	// stackOfBits representation, given current zero-based depth is n:
	//   bit n+1             = does depth n have children?       1=yes, 0=no
	//   bit [n, .. i .., 0] = does depth i have more siblings?  1=yes, 0=no

	char istr[129];
	unsigned int index = 0;
	unsigned int bar = 0;
	unsigned int len = 0;
	
	for (index = 0; index < depth + 2 - isNode * 2; index++) {
		if (stackOfBits & (1 << index)) {
			istr[len++] = '|';
			bar = len;
		} else {
			istr[len++] = ' ';
		}
		istr[len++] = ' ';
	}
	istr[newline ? bar : len] = '\0';
	
	printf("%s", istr);
	if (isNode)
		printf("+-o ");
}

static void setbits(UInt32 depth, UInt64 *stackOfBits, Boolean hasSiblings, Boolean hasChildren)
{
	// Save has-more-siblings state into stackOfBits for this depth.

	if (hasSiblings)
		*stackOfBits |= (1 << depth);
	else
		*stackOfBits &= ~(1 << depth);

	// Save has-children state into stackOfBits for this depth.

	if (hasChildren)
		*stackOfBits |= (2 << depth);
	else
		*stackOfBits &= ~(2 << depth);

	*stackOfBits &= ((1 << (depth + 2)) - 1);
}

void print_gfx(GFX_HEADER * gfx)
{
	GFX_BLOCKHEADER *gfx_blockheader_tmp;
	GFX_ENTRY *gfx_entry_tmp;
	char *devpath_text = NULL;
	CHAR16 *devpath_text16 = NULL;
	UINT8 int8;
	UINT16 int16;
	UINT32 int32;
	UInt64 stackOfBits;
	int depth;
	int count;
	int i;
	
	count = gfx->countofblocks;

	stackOfBits = 0;
	depth = 0;
	setbits(depth, &stackOfBits, 0, count > 0);

	indent(true, depth, stackOfBits, false);
	printf("device-properties <size=%d, children=%d>\n",gfx->filesize, count);
	indent(false, depth, stackOfBits, true);
	printf("\n");

	gfx_blockheader_tmp = gfx->blocks;
	
	i = 0;
	while(gfx_blockheader_tmp)
	{
		depth++;
		setbits(depth, &stackOfBits, i < count - 1, 0);
				
		indent(true, depth, stackOfBits, false);

		VerifyDevicePathNodeSizes(gfx_blockheader_tmp->devpath);
		devpath_text16 = PatchedConvertDevicePathToText(gfx_blockheader_tmp->devpath, 1, 1);
		if(devpath_text16 != NULL) {
			devpath_text = (char *)calloc(StrLen(devpath_text16) + 1, sizeof(char));
			if (devpath_text) {
				UnicodeStrToAsciiStr(devpath_text16, devpath_text);
				printf("%s <size=%d, records=%d>\n",(devpath_text != NULL)?devpath_text:"???", gfx_blockheader_tmp->blocksize, gfx_blockheader_tmp->records);
				free(devpath_text);
				devpath_text = NULL;
			}
			free(devpath_text16);
			devpath_text16 = NULL;
		}
		
		indent(false, depth, stackOfBits, false);
		printf("{\n");
		gfx_entry_tmp = gfx_blockheader_tmp->entries;
		while(gfx_entry_tmp)
		{
				indent(false, depth, stackOfBits, false);
				switch(gfx_entry_tmp->val_type)
				{
					case DATA_STRING:
						printf("  \"%s\"='%s'\n",gfx_entry_tmp->key, gfx_entry_tmp->val);
					break;
					case DATA_INT8:
						int8 = READ_UINT8(gfx_entry_tmp->val);
						printf("  \"%s\"=0x%02x\n",gfx_entry_tmp->key,int8);
					break;
					case DATA_INT16:
						int16 = READ_UINT16(gfx_entry_tmp->val);
						printf("  \"%s\"=0x%04x\n",gfx_entry_tmp->key,int16);
					break;
					case DATA_INT32:
						int32 = READ_UINT32(gfx_entry_tmp->val);
						printf("  \"%s\"=0x%08x\n",gfx_entry_tmp->key,int32);
					break;
					default:
					case DATA_BINARY:
						printf("  \"%s\"=<%s>\n",gfx_entry_tmp->key, bin2hex(gfx_entry_tmp->val,gfx_entry_tmp->val_len));
					break;
				}
			gfx_entry_tmp = gfx_entry_tmp->next;
		}
		indent(false, depth, stackOfBits, false);
		printf("}\n");
		indent(false, depth, stackOfBits, true);
		printf("\n");
		depth--;
		setbits(depth, &stackOfBits, 0, count > 0);
		gfx_blockheader_tmp = gfx_blockheader_tmp->next;
		i++;
	}
}

char *file_get_contents(FILE *fp)
{
	char *buf = NULL;
	char line[BUFSIZ];
	
	buf = (char *)malloc(BUFSIZ);
	if(!buf)
	{
		fprintf(stderr, "file_get_contents: out of memory\n");
		return NULL;
	}
	buf[0]='\0';
	while(fgets(line, BUFSIZ, fp) != NULL)
	{
		char *nbuf = realloc(buf,strlen(buf) + strlen(line) + 1);
		if(!nbuf)
		{
			fprintf(stderr, "file_get_contents: out of memory\n");
			free(buf);
			return NULL;
		}

		buf = nbuf;

		strcat(buf,line);
	}
	return buf;
}

int translate_properties (char * argv[], SETTINGS *settings)
{
	unsigned char *binbuf, *bp, *bin;
	char *textbuf = NULL, *hex;
	unsigned int gfx_size;
	GFX_HEADER * gfx;
	FILE * fp, *out;
	unsigned long filesize, len = 0;
	CFPropertyListRef plist;
	CFURLRef fileURL;
	
	// read input file
	switch(settings->ifile_type)
	{
		case FILE_HEX:
			
			// open ifile in text mode
			fp = fopen(settings->ifile,"r");
			if(fp == NULL)
			{
				fprintf(stderr,"%s: input file '%s' cannot be open for reading hex data\n", argv[0], settings->ifile);
				return(1);
			}
			
			textbuf = file_get_contents(fp);
			if(!textbuf)
			{
				fprintf(stderr, "%s: out of memory\n", argv[0] );
				fclose(fp);
				return(1);
			}
			fclose(fp);
			
			// convert from hex to bin
			bin = hex2bin(textbuf, &len);
			
			if(!bin)
			{
				fprintf(stderr, "%s: cannot convert from hex to bin, invalid hex inputfile '%s'!\n",argv[0],settings->ifile);
				return(1);
			}
			
			// check if we can read filesize from binary and that if this value equals real data size
			gfx_size = READ_UINT32(bin);

			if( (gfx_size == len) && (gfx_size != 0) )
			{
				// inputfile is gfx binary
				gfx = parse_binary(bin, bin + gfx_size, settings);
			}
			else
			{
				fprintf(stderr, "%s: invalid hex inputfile (filesize dont match or zero) '%s'!\n",argv[0],settings->ifile);
				return(1);
			}

			if(!gfx)
			{
				fprintf(stderr, "%s: cannot parse gfx data from hex input file '%s'!\n",argv[0],settings->ifile);
				return(1);
			}
			
			free(textbuf);
			free(bin);
		break;
		case FILE_BIN:
			filesize = getFileSize(settings->ifile);

			if(filesize <= 0)
			{
				fprintf(stderr,"%s: invalid or empty binary input file '%s'\n", argv[0],settings->ifile);
				return(1);
			}
	
			fp = fopen(settings->ifile,"rb");
			if(fp == NULL)
			{
				fprintf(stderr,"%s: input file '%s' cannot be open for reading binary data\n",argv[0],settings->ifile);
				return(1);
			}
	
			binbuf = (unsigned char *)calloc(filesize+2, sizeof(unsigned char));

			if(!binbuf)
			{
				fprintf(stderr, "%s: out of memory\n", argv[0]);
				fclose(fp);
				return(1);
			}
	
			fread(binbuf,filesize,1,fp);
			fclose(fp);
	
			bp = binbuf;
		
			// check if we can read filesize from binary and that if this value equals real data size
			gfx_size = READ_UINT32(bp);
			
			if( (gfx_size == filesize) && (gfx_size != 0))
			{
				// inputfile is gfx binary
				gfx = parse_binary(bp, bp + gfx_size, settings);
			}
			else
			{
				fprintf(stderr, "%s: invalid binary inputfile (filesize dont match or zero) '%s'!\n",argv[0],settings->ifile);
				return(1);
			}

			if(!gfx)
			{
				fprintf(stderr, "%s: cannot parse gfx from binary input file '%s'!\n",argv[0],settings->ifile);
				return(1);
			}
			free(binbuf);
		break;
		case FILE_XML:
			fileURL = URLCreate(settings->ifile);
			plist = ReadPropertyList(fileURL);
			
			if(!plist)
			{
				fprintf(stderr, "%s: invalid property list xml inputfile '%s'!\n",argv[0],settings->ifile);
				if (fileURL) CFRelease(fileURL);
				return(1);
			}
			
			gfx = CreateGFXFromPlist(plist);
			if(!gfx)
			{
				fprintf(stderr, "%s: cannot create gfx data from property list xml inputfile '%s'!\n",argv[0],settings->ifile);
				if (fileURL) CFRelease(fileURL);
				return(1);
			}

			if(fileURL) CFRelease(fileURL);

			CFRelease(plist);
		break;
		default:
			fprintf(stderr, "%s: unknown input file type\n", argv[0]);
			fclose(fp);
			return(1);
		break;
	}
	
	if(settings->verbose) print_gfx(gfx);
	
	// write output file
	switch(settings->ofile_type)
	{
		case FILE_HEX:
			out = fopen(settings->ofile,"w");
			if(out == NULL)
			{
				fprintf(stderr,"%s: file '%s' cannot be open for writing hex data\n",argv[0],settings->ofile);
				return(1);
			}
			
			bin = gfx2bin(gfx);
			if(!bin)
			{
				fprintf(stderr,"%s: file '%s' cannot write gfx data buffer for hex file\n",argv[0],settings->ofile);
				return(1);
			}
			
			hex = bin2hex(bin, gfx->filesize);
			
			if(!hex)
			{
				fprintf(stderr,"%s: cannot create hex data from binary gfx\n",argv[0]);
				return(1);
			}
			
			fputs(hex, out);
			fclose(out);
			free(bin);
			free(hex);
		break;
		case FILE_BIN:
			out = fopen(settings->ofile,"wb");
			if(out == NULL)
			{
				fprintf(stderr,"%s: file '%s' cannot be open for writing binary data\n",argv[0],settings->ofile);
				return(1);
			}

			bin = gfx2bin(gfx);
			if(!bin)
			{
				fprintf(stderr,"%s: file '%s' cannot write gfx data buffer for binary file\n",argv[0],settings->ofile);
				return(1);
			}
			
			fwrite(bin, 1,gfx->filesize, out);
			fclose(out);
			free(bin);
		break;
		case FILE_XML:
			plist = CreateGFXDictionary(gfx);
			if(!plist)
			{
				fprintf(stderr, "%s: cannot build property list from gfx data!\n",argv[0]);
				return(1);
			}
	
			fileURL = URLCreate(settings->ofile);
	
			if(!WritePropertyList(plist,fileURL))
			{
				fprintf(stderr, "%s: file '%s' cannot be open for writing property list data\n",argv[0], settings->ofile);
				if (fileURL) CFRelease(fileURL);
				return(1);
			}
			if (fileURL) CFRelease(fileURL);
			CFRelease(plist);
		break;
	}
	
	free(gfx);
	return 0;
}


int main (int argc, char * argv[])
{
	UefiBootServicesTableLibConstructor();
	SETTINGS settings;
	exit(parse_args(argc,argv, &settings));
}
