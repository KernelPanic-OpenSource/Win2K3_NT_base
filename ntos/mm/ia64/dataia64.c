/*++

Copyright (c) 1990  Microsoft Corporation

Module Name:

   dataia64.c

Abstract:

    This module contains the private hardware specific global storage for
    the memory management subsystem.

Author:

    Lou Perazzoli (loup) 22-Jan-1990

Revision History:

    Koichi Yamada (kyamada) 9-Jan-1996 : IA64 version based on i386 version

--*/

#include "mi.h"


//
// A zero Pte.
//

const MMPTE ZeroPte = { 0 };


//
// A kernel zero PTE.
//

const MMPTE ZeroKernelPte = {0x0};


MMPTE ValidKernelPte = { MM_PTE_VALID_MASK |
                         MM_PTE_CACHE |
                         MM_PTE_WRITE_MASK |
                         MM_PTE_EXECUTE_MASK |
                         MM_PTE_ACCESS_MASK |
                         MM_PTE_DIRTY_MASK |
                         MM_PTE_EXC_DEFER};

const MMPTE ValidKernelPteLocal = { MM_PTE_VALID_MASK |
                                    MM_PTE_CACHE |
                                    MM_PTE_WRITE_MASK |
                                    MM_PTE_ACCESS_MASK |
                                    MM_PTE_DIRTY_MASK |
                                    MM_PTE_EXC_DEFER};


const MMPTE ValidUserPte = { MM_PTE_VALID_MASK |
                       MM_PTE_CACHE |
                       MM_PTE_WRITE_MASK |
                       MM_PTE_OWNER_MASK |
                       MM_PTE_ACCESS_MASK |
                       MM_PTE_DIRTY_MASK |
                       MM_PTE_EXC_DEFER};


const MMPTE ValidPtePte = { MM_PTE_VALID_MASK |
                            MM_PTE_CACHE |
                            MM_PTE_WRITE_MASK |
                            MM_PTE_ACCESS_MASK |
                            MM_PTE_DIRTY_MASK  };


const MMPTE ValidPdePde = { MM_PTE_VALID_MASK |
                            MM_PTE_CACHE |
                            MM_PTE_WRITE_MASK |
                            MM_PTE_ACCESS_MASK |
                            MM_PTE_DIRTY_MASK };


MMPTE ValidKernelPde = { MM_PTE_VALID_MASK |
                         MM_PTE_CACHE |
                         MM_PTE_WRITE_MASK |
                         MM_PTE_ACCESS_MASK |
                         MM_PTE_DIRTY_MASK };

const MMPTE ValidKernelPdeLocal = { MM_PTE_VALID_MASK |
                                    MM_PTE_CACHE |
                                    MM_PTE_WRITE_MASK |
                                    MM_PTE_ACCESS_MASK |
                                    MM_PTE_DIRTY_MASK };

MMPTE ValidPpePte = { MM_PTE_VALID_MASK |
                      MM_PTE_CACHE |
                      MM_PTE_WRITE_MASK |
                      MM_PTE_ACCESS_MASK |
                      MM_PTE_DIRTY_MASK };


MMPTE DemandZeroPde = { MM_READWRITE << MM_PROTECT_FIELD_SHIFT };


const MMPTE DemandZeroPte = { MM_READWRITE << MM_PROTECT_FIELD_SHIFT };


const MMPTE TransitionPde = { MM_PTE_TRANSITION_MASK |
                              MM_READWRITE << MM_PROTECT_FIELD_SHIFT };


MMPTE PrototypePte = { MI_PTE_LOOKUP_NEEDED << 32 |
                       MM_PTE_PROTOTYPE_MASK |
                       MM_READWRITE << MM_PROTECT_FIELD_SHIFT };

//
// PTE which generates an access violation when referenced.
//

const MMPTE NoAccessPte = {MM_NOACCESS << MM_PROTECT_FIELD_SHIFT};

//
// Pool start and end.
//

PVOID MmNonPagedPoolStart;

PVOID MmNonPagedPoolEnd = (PVOID)MM_NONPAGED_POOL_END;

PVOID MmPagedPoolStart =  (PVOID)MM_PAGED_POOL_START;

PVOID MmPagedPoolEnd;

//
// Color tables for free and zeroed pages.
//

#if MM_MAXIMUM_NUMBER_OF_COLORS > 1
MMPFNLIST MmFreePagesByPrimaryColor[2][MM_MAXIMUM_NUMBER_OF_COLORS];
#endif

PMMCOLOR_TABLES MmFreePagesByColor[2];

//
// Color tables for modified pages destined for the paging file.
//

MMPFNLIST MmModifiedPageListByColor[MM_MAXIMUM_NUMBER_OF_COLORS] = {
                            0, ModifiedPageList, MM_EMPTY_LIST, MM_EMPTY_LIST};


//
// Count of the number of modified pages destined for the paging file.
//

PFN_NUMBER MmTotalPagesForPagingFile = 0;

//
// Pte reserved for mapping pages for the debugger.
//

PMMPTE MmDebugPte;

//
// 16 PTEs reserved for mapping MDLs (64k max).
//

PMMPTE MmCrashDumpPte;

//
// Maximum size of system cache
//

ULONG MiMaximumSystemCacheSize;

#if defined(_MIALT4K_)

//
// Map a IA32 compatible PTE protection from Pte.Protect field
//

ULONG MmProtectToPteMaskForIA32[32] = {
                       MM_PTE_NOACCESS,
                       MM_PTE_EXECUTE_READ | MM_PTE_CACHE,
                       MM_PTE_EXECUTE_READ | MM_PTE_CACHE,
                       MM_PTE_EXECUTE_READ | MM_PTE_CACHE,
                       MM_PTE_EXECUTE_READWRITE | MM_PTE_CACHE,
                       MM_PTE_EXECUTE_WRITECOPY | MM_PTE_CACHE,
                       MM_PTE_EXECUTE_READWRITE | MM_PTE_CACHE,
                       MM_PTE_EXECUTE_WRITECOPY | MM_PTE_CACHE,
                       MM_PTE_NOACCESS,
                       MM_PTE_NOCACHE | MM_PTE_EXECUTE_READ,
                       MM_PTE_NOCACHE | MM_PTE_EXECUTE_READ,
                       MM_PTE_NOCACHE | MM_PTE_EXECUTE_READ,
                       MM_PTE_NOCACHE | MM_PTE_EXECUTE_READWRITE,
                       MM_PTE_NOCACHE | MM_PTE_EXECUTE_WRITECOPY,
                       MM_PTE_NOCACHE | MM_PTE_EXECUTE_READWRITE,
                       MM_PTE_NOCACHE | MM_PTE_EXECUTE_WRITECOPY,
                       MM_PTE_NOACCESS,
                       MM_PTE_GUARD | MM_PTE_EXECUTE_READ | MM_PTE_CACHE,
                       MM_PTE_GUARD | MM_PTE_EXECUTE_READ | MM_PTE_CACHE,
                       MM_PTE_GUARD | MM_PTE_EXECUTE_READ | MM_PTE_CACHE,
                       MM_PTE_GUARD | MM_PTE_EXECUTE_READWRITE | MM_PTE_CACHE,
                       MM_PTE_GUARD | MM_PTE_EXECUTE_WRITECOPY | MM_PTE_CACHE,
                       MM_PTE_GUARD | MM_PTE_EXECUTE_READWRITE | MM_PTE_CACHE,
                       MM_PTE_GUARD | MM_PTE_EXECUTE_WRITECOPY | MM_PTE_CACHE,
                       MM_PTE_NOACCESS,
                       MM_PTE_NOCACHE | MM_PTE_GUARD | MM_PTE_EXECUTE_READ,
                       MM_PTE_NOCACHE | MM_PTE_GUARD | MM_PTE_EXECUTE_READ,
                       MM_PTE_NOCACHE | MM_PTE_GUARD | MM_PTE_EXECUTE_READ,
                       MM_PTE_NOCACHE | MM_PTE_GUARD | MM_PTE_EXECUTE_READWRITE,
                       MM_PTE_NOCACHE | MM_PTE_GUARD | MM_PTE_EXECUTE_WRITECOPY,
                       MM_PTE_NOCACHE | MM_PTE_GUARD | MM_PTE_EXECUTE_READWRITE,
                       MM_PTE_NOCACHE | MM_PTE_GUARD | MM_PTE_EXECUTE_WRITECOPY
                    };

ULONG MmProtectToPteMaskForSplit[32] = {
                       MM_PTE_NOACCESS,
                       MM_PTE_EXECUTE_READ | MM_PTE_CACHE,
                       MM_PTE_EXECUTE_READ | MM_PTE_CACHE,
                       MM_PTE_EXECUTE_READ | MM_PTE_CACHE,
                       MM_PTE_EXECUTE_READ | MM_PTE_CACHE,
                       MM_PTE_EXECUTE_WRITECOPY | MM_PTE_CACHE,
                       MM_PTE_EXECUTE_READ | MM_PTE_CACHE,
                       MM_PTE_EXECUTE_WRITECOPY | MM_PTE_CACHE,
                       MM_PTE_NOACCESS,
                       MM_PTE_NOCACHE | MM_PTE_EXECUTE_READ,
                       MM_PTE_NOCACHE | MM_PTE_EXECUTE_READ,
                       MM_PTE_NOCACHE | MM_PTE_EXECUTE_READ,
                       MM_PTE_NOCACHE | MM_PTE_EXECUTE_READ,
                       MM_PTE_NOCACHE | MM_PTE_EXECUTE_WRITECOPY,
                       MM_PTE_NOCACHE | MM_PTE_EXECUTE_READWRITE,
                       MM_PTE_NOCACHE | MM_PTE_EXECUTE_WRITECOPY,
                       MM_PTE_NOACCESS,
                       MM_PTE_GUARD | MM_PTE_EXECUTE_READ | MM_PTE_CACHE,
                       MM_PTE_GUARD | MM_PTE_EXECUTE_READ | MM_PTE_CACHE,
                       MM_PTE_GUARD | MM_PTE_EXECUTE_READ | MM_PTE_CACHE,
                       MM_PTE_GUARD | MM_PTE_EXECUTE_READ | MM_PTE_CACHE,
                       MM_PTE_GUARD | MM_PTE_EXECUTE_WRITECOPY | MM_PTE_CACHE,
                       MM_PTE_GUARD | MM_PTE_EXECUTE_READ | MM_PTE_CACHE,
                       MM_PTE_GUARD | MM_PTE_EXECUTE_WRITECOPY | MM_PTE_CACHE,
                       MM_PTE_NOACCESS,
                       MM_PTE_NOCACHE | MM_PTE_GUARD | MM_PTE_EXECUTE_READ,
                       MM_PTE_NOCACHE | MM_PTE_GUARD | MM_PTE_EXECUTE_READ,
                       MM_PTE_NOCACHE | MM_PTE_GUARD | MM_PTE_EXECUTE_READ,
                       MM_PTE_NOCACHE | MM_PTE_GUARD | MM_PTE_EXECUTE_READ,
                       MM_PTE_NOCACHE | MM_PTE_GUARD | MM_PTE_EXECUTE_WRITECOPY,
                       MM_PTE_NOCACHE | MM_PTE_GUARD | MM_PTE_EXECUTE_READ,
                       MM_PTE_NOCACHE | MM_PTE_GUARD | MM_PTE_EXECUTE_WRITECOPY
                    };

#endif


