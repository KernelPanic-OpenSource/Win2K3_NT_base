/*++ BUILD Version: 0001    // Increment this if a change has global effects

Copyright (c) 1995 Microsoft Corporation

Module Name:

    perfutil.h  

Abstract:

    This file supports routines used to parse and
    create Performance Monitor Data Structures.
    It actually supports Performance Object types with
    multiple instances

Author:

    Bob Watson  28-Jul-1995

Revision History:


--*/
#ifndef _PERFUTIL_H_
#define _PERFUTIL_H_

#include <windows.h>
#include <winperf.h>

#define MAX_INSTANCE_NAME   32
#ifdef _WIN64
#define SMALL_BUFFER_SIZE   ((DWORD)8192)
#else
#define SMALL_BUFFER_SIZE   ((DWORD)4096)
#endif
#define MEDIUM_BUFFER_SIZE  ((DWORD)(4096*8))
#define LARGE_BUFFER_SIZE   ((DWORD)(4096*16))
#define INCREMENT_BUFFER_SIZE ((DWORD)(4096*2))

#define MAX_VALUE_NAME_LENGTH 256*sizeof(WCHAR)
#define MAX_VALUE_DATA_LENGTH 256*sizeof(WCHAR)
//
//  Until USER supports Unicode, we have to work in ASCII:
//

#define DEFAULT_NT_CODE_PAGE 437
#define UNICODE_CODE_PAGE      0

// enable this define to log process heap data to the event log
#ifdef PROBE_HEAP_USAGE
#undef PROBE_HEAP_USAGE
#endif

//
//  Utility macro.  This is used to reserve a DWORD multiple of
//  bytes for Unicode strings embedded in the definitional data,
//  viz., object instance names.
//
//  Assumes x is DWORD, and returns a DWORD
//
#define DWORD_MULTIPLE(x) (((ULONG)(x) + ((sizeof(DWORD))-1)) & ~((ULONG)(sizeof(DWORD))-1))
#define QWORD_MULTIPLE(x) (((ULONG)(x) + ((sizeof(ULONG64))-1)) & ~((ULONG)(sizeof(ULONG64))-1))
#define PAGESIZE_MULTIPLE(x) \
     (((ULONG)(x) + ((SMALL_BUFFER_SIZE)-1)) & ~((ULONG)(SMALL_BUFFER_SIZE)-1))

//
//  Returns a PVOID
//
#define ALIGN_ON_DWORD(x) \
     ((VOID *)(((ULONG_PTR)(x) + ((sizeof(DWORD))-1)) & ~((ULONG_PTR)(sizeof(DWORD))-1)))
#define ALIGN_ON_QWORD(x) \
     ((VOID *)(((ULONG_PTR)(x) + ((sizeof(ULONG64))-1)) & ~((ULONG_PTR)(sizeof(ULONG64))-1)))

extern const    WCHAR  GLOBAL_STRING[];      // Global command (get all local ctrs)
extern const    WCHAR  FOREIGN_STRING[];           // get data from foreign computers
extern const    WCHAR  COSTLY_STRING[];      
extern const    WCHAR  NULL_STRING[];

extern const    WCHAR  szTotalValue[];
extern const    WCHAR  szDefaultTotalString[];
#define DEFAULT_TOTAL_STRING_LEN    14

extern DWORD  MESSAGE_LEVEL;

#define QUERY_GLOBAL    1
#define QUERY_ITEMS     2
#define QUERY_FOREIGN   3
#define QUERY_COSTLY    4

// function prototypes for data collection routines
typedef DWORD (APIENTRY PM_LOCAL_COLLECT_PROC) (LPVOID *, LPDWORD, LPDWORD);

typedef struct _POS_FUNCTION_INFO {
    DWORD   dwObjectId;
    DWORD   dwCollectFunctionBit;
    DWORD   dwDataFunctionBit;
    PM_LOCAL_COLLECT_PROC *pCollectFunction;
} POS_FUNCTION_INFO, * PPOS_FUNCTION_INFO;

//
// The definition of the only routine of perfutil.c, It builds part of a 
// performance data instance (PERF_INSTANCE_DEFINITION) as described in 
// winperf.h
//

HANDLE MonOpenEventLog (IN LPWSTR);
VOID MonCloseEventLog ();
DWORD GetQueryType (IN LPWSTR);
BOOL IsNumberInUnicodeList (DWORD, LPWSTR);

BOOL
MonBuildInstanceDefinition(
    PERF_INSTANCE_DEFINITION *pBuffer,
    PVOID *pBufferNext,
    DWORD ParentObjectTitleIndex,
    DWORD ParentObjectInstance,
    DWORD UniqueID,
    LPWSTR Name
    );

LONG
GetPerflibKeyValue (
    LPCWSTR szItem,
    DWORD   dwRegType,
    DWORD   dwMaxSize,      // ... of pReturnBuffer in bytes
    LPVOID  pReturnBuffer,
    DWORD   dwDefaultSize,  // ... of pDefault in bytes
    LPVOID  pDefault
);

//
//  Memory Probe macro
//
#ifdef PROBE_HEAP_USAGE

typedef struct _LOCAL_HEAP_INFO_BLOCK {
    DWORD   AllocatedEntries;
    DWORD   AllocatedBytes;
    DWORD   FreeEntries;
    DWORD   FreeBytes;
} LOCAL_HEAP_INFO, *PLOCAL_HEAP_INFO;

#define HEAP_PROBE()    { \
    DWORD   dwHeapStatus[5]; \
    NTSTATUS CallStatus; \
    dwHeapStatus[4] = __LINE__; \
    if (!(CallStatus = memprobe (dwHeapStatus, 16L, NULL))) { \
        REPORT_INFORMATION_DATA (TCP_HEAP_STATUS, LOG_DEBUG,    \
            &dwHeapStatus, sizeof(dwHeapStatus));  \
    } else {  \
        REPORT_ERROR_DATA (TCP_HEAP_STATUS_ERROR, LOG_DEBUG, \
            &CallStatus, sizeof (DWORD)); \
    } \
}

#else

#define HEAP_PROBE()    ;

#endif

#ifdef DBG
#define PERF_HEAP_FLAGS    HEAP_ZERO_MEMORY | HEAP_GENERATE_EXCEPTIONS
#else
#define PERF_HEAP_FLAGS    HEAP_ZERO_MEMORY
#endif

#ifndef PERF_HEAP
#define PERF_HEAP RtlProcessHeap()
#endif

#define ALLOCMEM(size)     HeapAlloc (PERF_HEAP, PERF_HEAP_FLAGS, size)
#define REALLOCMEM(pointer, newsize) \
                  HeapReAlloc (PERF_HEAP, 0, pointer, newsize)
#define FREEMEM(pointer)   HeapFree (PERF_HEAP, 0, pointer)

#endif  //_PERFUTIL_H_
