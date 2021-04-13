#include "agp.h"

#define PCI_ADDRESS_MEMORY_ADDRESS_MASK_64 0xFFFFFFFFFFFFFFF0UI64

//
// Define the location of the GART aperture control registers
//

//
// The GART registers on the 440 live in the host-PCI bridge.
// This is unfortunate, since the AGP driver attaches to the PCI-PCI (AGP)
// bridge. So we have to get to the host-PCI bridge config space
// and this is only possible because we KNOW this is bus 0, slot 0.
//

//
// TBD: The GART registers on the 460 live in the GXB.  Though we are trying to use
// the code from 440 as much as possible. The HalGet calls may not be needed if
// all the GART registers live on the PCI-PCI bridge (GXB).  DOUBLE CHECK THIS 
// WITH THE EDS, VERIFY IT WITH JOHN VERT - NAGA G
//

// Compared to 440 there exists no equivalents for PAC configuration, 
// AGP Control & ATTBASE registers in 460GX. 

#define EXTRACT_LSBYTE(x)       x = (x & 0xFF)  // Sunil

#define ONE_KB                  1024
#define ONE_MB                  (ONE_KB * ONE_KB)
#define AP_256MB                (256 * ONE_MB)
#define AP_1GB                  (ONE_MB * ONE_KB)
#define AP_32GB                 (32 * AP_1GB)

#define ABOVE_TOM(x)             ( (x) & (0x08) )

#define AGP_460GX_IDENTIFIER	0x84E28086	// Device ID & Vendor ID 
											// for the 460GX SAC


 
#define APBASE_OFFSET  0x10		// Aperture Base Address:APBASE & BAPBASE are used to
#define BAPBASE_OFFSET 0x98		// store the base address of the Graphics Aperture(GA).
								// Only one of APBASE or BAPBASE is visible at a time.
								// APBASE is visible when AGPSIZ[3]=0 && AGPSIZ[2:0]!=0. 
								// BAPBASE is visible when AGPSIZ[3]=1 & AGPSIZ[2:0]!=0.
								// BAPBASE is used when the GA is mapped above 4GB and
								// APBASE when the GA is mapped below 4GB


#define APSIZE_OFFSET  0xA2     // Aperture Size Register - AGPSIZ    


      
#define ATTBASE 0xFE200000		// Aperture Translation Table Base - It is a 
								// 2MB region hard coded in 460GX to 0xFE200000h


#define AGPSTATUS_OFFSET  0xE4	// AGP Status Register - The CAP_PTR is
								// at 0xE0h in 460GX and the AGP Status
								// register is at CAP_PTR+4.


#define AGPCMD_OFFSET	  0xE8	// AGP Command Register - CAP_PTR + 8



//
// 82460GX specific definitions to get to the PCI configuration space of the
// SAC, GXB et al.
//
#define AGP460_SAC_BUS_ID		0	 // The bus number where the SAC resides in 82460GX

//
// The following two definitions should be interpreted as of type PCI_SLOT_NUMBER
// which combines the device & function number for a particular PCI device.  It is 
// a ULONG value which should be deciphered as follows:
// [xxxxxxxx xxxxxxxx xxxxxxxx YYYZZZZZ]
// where x = Reserved, Y = Function Number, Z = Device Number
//

#define AGP460_SAC_CBN_SLOT_ID 0x10 // The Chipset Bus Number resides at Bus 0,
							     // Device 10h & Function 0.

#define AGP460_GXB_SLOT_ID		0x34 // The GXB would be accessed at Bus CBN, Device 14h
								 // function BFN.  BFN is 1 by default.

#define AGP460_PAGE_SIZE_4KB	(4 * ONE_KB)
//
// Handy macros to read & write in the PCI Configuration space
//

//
// Read460CBN reads the CBN - Chipset Bus Number from the 82460GX SAC.
// CBN is a BYTE located at Bus 0, Device 10h, Function 0, Offset 40
// in the SAC Configuration space.  The CBN can be read once and reused
// subsequently.
// 

void Read460CBN(PVOID  CBN);


void Read460Config(ULONG  _CBN_,PVOID  _buf_,ULONG _offset_,ULONG _size_);

void Write460Config(ULONG _CBN_,PVOID  _buf_,ULONG _offset_,ULONG _size_);

//
// Conversions from AGPSIZ[2:0] encoding to Aperture Size in MB/GB
//
//  AGPSIZE[2:0]   Aperture Size
//          000         0MB (power on default; no GART SRAM present)
//			001       256MB 
//			010         1GB
//          100        32GB  (only with 4MB pages)
//
#define AP_SIZE_0MB     0x00
#define AP_SIZE_256MB   0x01
#define AP_SIZE_1GB     0x02
#define AP_SIZE_32GB    0x04


#define AP_SIZE_COUNT_4KB	2 //Only apertures of 256M & 1G are possible with 4KB pages
#define AP_SIZE_COUNT_4MB   3 //Apertures of 256M, 1G & 32G are possible with 4MB pages

#define AP_MIN_SIZE		    AP_256MB	// 0 is not counted as a possible 
									    // aperture size
#define AP_MAX_SIZE_4KB	    AP_1GB      // 1 GB is the maximum with 4KB pages
#define AP_MAX_SIZE_4MB     AP_1GB      // 32GB is the maximum with 4MB pages

#define PAGESIZE_460GX_CHIPSET  (4 * ONE_KB)

#define GART_PAGESHIFT_460GX	12


//
// Define the 82460GX GART table entry.  4KB pages are assumed.  To support 4MB pages new 
// structures have to be defined.
//
typedef struct _GART_ENTRY_HW {
    ULONG Page	     :  24;
	ULONG Valid		 :   1;
	ULONG Coherency  :   1;
	ULONG Parity     :   1;  // Parity bit is generated by Hardware. Software should
							 // mask it out and treat it as a reserved.
    ULONG Reserved   :   5;
} GART_ENTRY_HW, *PGART_ENTRY_HW;


//
// GART Entry states are defined so that all software-only states
// have the Valid bit clear.
//
#define GART_ENTRY_VALID        1		    //  Bit 24 is the valid bit in 460GX GART
#define GART_ENTRY_FREE         0           //  000

#define GART_ENTRY_WC           2           //  010
#define GART_ENTRY_UC           4           //  100
#define GART_ENTRY_WB           6           //  110

#define GART_ENTRY_RESERVED_WC  GART_ENTRY_WC
#define GART_ENTRY_RESERVED_UC  GART_ENTRY_UC
#define GART_ENTRY_RESERVED_WB  GART_ENTRY_WB



//
// Unlike 440, 82460GX GART driver doesn't have direct equivalent for GART PTE software
// states like GART_ENTRY_VALID_WC, GART_ENTRY_VALID_UC etc.  This is because of
// the organization of the GART PTEs - the valid bit is disjoint from any reserved bits
// and therefore have to be manipulated separately. - Naga G
//

typedef struct _GART_ENTRY_SW {
    ULONG Reserved0 : 24;
	ULONG Valid     :  1;
    ULONG Reserved1 :  2;
	ULONG State     :  3;
	ULONG Reserved2 :  2;
} GART_ENTRY_SW, *PGART_ENTRY_SW;

typedef struct _GART_PTE {
    union {
        GART_ENTRY_HW Hard;
        ULONG      AsUlong;
        GART_ENTRY_SW Soft;
    };
} GART_PTE, *PGART_PTE;

//
// Define the 460-specific extension
//
typedef struct _AGP460_EXTENSION {
    BOOLEAN             GlobalEnable;				// Software only bit.  The GART will be 
													// initialized to a known invalid state (0s)
													// during initialization. Other than that
													// no hardware control is available in 460GX
													// to enable/disable GART accesses. Thus, this
													// is not of much use !
    PHYSICAL_ADDRESS    ApertureStart; 
    ULONG               ApertureLength;				// Aperture Length in Bytes
	ULONG               ChipsetPageSize;			// Can be 4KB or 4MB.
    PGART_PTE           Gart;
    ULONG               GartLength;					 // Maximum is 2MB
    PHYSICAL_ADDRESS    GartPhysical;				 // Physical address where GART starts
	BOOLEAN             bSupportMultipleAGPDevices;  // For future use.
	BOOLEAN             bSupportsCacheCoherency;     // For future use. 
    ULONGLONG SpecialTarget;
} AGP460_EXTENSION, *PAGP460_EXTENSION;

NTSTATUS Agp460FlushPages(
    IN PAGP460_EXTENSION AgpContext,
    IN PMDL Mdl
    );
