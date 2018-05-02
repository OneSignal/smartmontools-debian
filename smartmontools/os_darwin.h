/*
 * os_generic.h
 *
 * Home page of code is: http://www.smartmontools.org
 *
 * Copyright (C) 2004-8 Geoff Keating <geoffk@geoffk.org>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2, or (at your option)
 * any later version.
 *
 * You should have received a copy of the GNU General Public License
 * (for example COPYING); if not, write to the Free Software Foundation,
 * Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 *
 * This code was originally developed as a Senior Thesis by Michael Cornwell
 * at the Concurrent Systems Laboratory (now part of the Storage Systems
 * Research Center), Jack Baskin School of Engineering, University of
 * California, Santa Cruz. http://ssrc.soe.ucsc.edu/
 *
 */

#ifndef OS_DARWIN_H_
#define OS_DARWIN_H_

#define OS_DARWIN_H_CVSID "$Id$\n"

#define kIOATABlockStorageDeviceClass   "IOATABlockStorageDevice"

// Isn't in 10.3.9?

#ifndef kIOPropertySMARTCapableKey
#define kIOPropertySMARTCapableKey	"SMART Capable"
#endif

// NVMe definitions, non documented, experimental
#define kIOPropertyNVMeSMARTCapableKey	"NVMe SMART Capable"

// Constant to init driver
#define kIONVMeSMARTUserClientTypeID		CFUUIDGetConstantUUIDWithBytes(NULL,	  \
										0xAA, 0x0F, 0xA6, 0xF9, 0xC2, 0xD6, 0x45, 0x7F, 0xB1, 0x0B, \
                    0x59, 0xA1, 0x32, 0x53, 0x29, 0x2F)

// Constant to use plugin interface
#define kIONVMeSMARTInterfaceID		CFUUIDGetConstantUUIDWithBytes(NULL,				  \
                    0xcc, 0xd1, 0xdb, 0x19, 0xfd, 0x9a, 0x4d, 0xaf, 0xbf, 0x95, \
                    0x12, 0x45, 0x4b, 0x23, 0xa, 0xb6)

// interface structure, obtained using lldb, could be incomplete or wrong
typedef struct IONVMeSMARTInterface
{
        IUNKNOWN_C_GUTS;

        UInt16 version;
        UInt16 revision;

				// NVMe smart data, returns nvme_smart_log structure
        IOReturn ( *SMARTReadData )( void *  interface,
                                     struct nvme_smart_log * NVMeSMARTData );

				// NVMe IdentifyData, returns nvme_id_ctrl per namespace
        IOReturn ( *GetIdentifyData )( void *  interface,
                                      struct nvme_id_ctrl * NVMeIdentifyControllerStruct,
                                      unsigned int ns );

				// Always getting kIOReturnDeviceError
        IOReturn ( *GetFieldCounters )( void *   interface,
                                        char * FieldCounters );
				// Returns 0
        IOReturn ( *ScheduleBGRefresh )( void *   interface);

				// Always returns kIOReturnDeviceError, probably expects pointer to some
				// structure as an argument
        IOReturn ( *GetLogPage )( void *  interface, void * data, unsigned int, unsigned int);


				/* GetSystemCounters Looks like a table with an attributes. Sample result:

				0x101022200: 0x01 0x00 0x08 0x00 0x00 0x00 0x00 0x00
				0x101022208: 0x00 0x00 0x00 0x00 0x02 0x00 0x08 0x00
				0x101022210: 0x00 0x00 0x00 0x00 0x00 0x00 0x00 0x00
				0x101022218: 0x03 0x00 0x08 0x00 0xf1 0x74 0x26 0x01
				0x101022220: 0x00 0x00 0x00 0x00 0x04 0x00 0x08 0x00
				0x101022228: 0x0a 0x91 0xb1 0x00 0x00 0x00 0x00 0x00
				0x101022230: 0x05 0x00 0x08 0x00 0x24 0x9f 0xfe 0x02
				0x101022238: 0x00 0x00 0x00 0x00 0x06 0x00 0x08 0x00
				0x101022240: 0x9b 0x42 0x38 0x02 0x00 0x00 0x00 0x00
				0x101022248: 0x07 0x00 0x08 0x00 0xdd 0x08 0x00 0x00
				0x101022250: 0x00 0x00 0x00 0x00 0x08 0x00 0x08 0x00
				0x101022258: 0x07 0x00 0x00 0x00 0x00 0x00 0x00 0x00
				0x101022260: 0x09 0x00 0x08 0x00 0x00 0x00 0x00 0x00
				0x101022268: 0x00 0x00 0x00 0x00 0x0a 0x00 0x04 0x00
				.........
				0x101022488: 0x74 0x00 0x08 0x00 0x00 0x00 0x00 0x00
				0x101022490: 0x00 0x00 0x00 0x00 0x75 0x00 0x40 0x02
				0x101022498: 0x00 0x00 0x00 0x00 0x00 0x00 0x00 0x00
				*/
        IOReturn ( *GetSystemCounters )( void *  interface, char *, unsigned int *);


				/* GetAlgorithmCounters returns mostly 0
				0x102004000: 0x00 0x00 0x00 0x00 0x00 0x00 0x00 0x00
				0x102004008: 0x00 0x00 0x00 0x00 0x00 0x00 0x00 0x00
				0x102004010: 0x00 0x00 0x00 0x00 0x00 0x00 0x00 0x00
				0x102004018: 0x00 0x00 0x00 0x00 0x00 0x00 0x00 0x00
				0x102004020: 0x00 0x00 0x00 0x00 0x00 0x00 0x00 0x00
				0x102004028: 0x00 0x00 0x00 0x00 0x00 0x00 0x00 0x00
				0x102004038: 0x00 0x00 0x00 0x00 0x00 0x00 0x00 0x00
				0x102004040: 0x00 0x00 0x00 0x00 0x00 0x00 0x00 0x00
				0x102004048: 0x00 0x00 0x00 0x00 0x00 0x00 0x00 0x00
				0x102004050: 0x00 0x00 0x00 0x00 0x80 0x00 0x00 0x00
				0x102004058: 0x80 0x00 0x00 0x00 0x00 0x00 0x00 0x00
				0x102004060: 0x00 0x00 0x00 0x00 0x00 0x00 0x00 0x00
				0x102004068: 0x00 0x00 0x00 0x00 0x00 0x00 0x00 0x00
				0x102004070: 0x00 0x00 0x00 0x00 0x00 0x00 0x00 0x00
				0x102004078: 0x00 0x00 0x00 0x00 0x00 0x00 0x00 0x00
				0x102004080: 0x00 0x00 0x00 0x00 0x00 0x00 0x00 0x00
				0x102004088: 0x00 0x00 0x00 0x00 0x00 0x00 0x00 0x00
				0x102004090: 0x00 0x01 0x00 0x00 0x00 0x00 0x00 0x00
				0x102004098: 0x00 0x00 0x00 0x00 0x00 0x00 0x00 0x00

				*/
        IOReturn ( *GetAlgorithmCounters )( void *  interface, char *, unsigned int *);
} IONVMeSMARTInterface;


#endif /* OS_DARWIN_H_ */