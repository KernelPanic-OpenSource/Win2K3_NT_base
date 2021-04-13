/*++

Copyright (c) 1996  Microsoft Corporation

Module Name:

    perfsys.c

Abstract:

    This file implements an Performance Object that presents
    System Performance Object information

Created:

    Bob Watson  22-Oct-1996

Revision History


--*/

//
//  Include Files
//

#include <nt.h>
#include <ntrtl.h>
#include <nturtl.h>
#include <windows.h>
#include <winperf.h>
#include <ntprfctr.h>
#define PERF_HEAP hLibHeap
#include <perfutil.h>
#include "perfos.h"
#include "perfosmc.h"
#include "datasys.h"

typedef struct _PERFSYS_THREAD_DATA_BLOCK {
    DWORD   dwProcessCount;
    DWORD   dwNullProcessCount;
    DWORD   dwThreadCount;
    DWORD   dwReadyThreads;     // (1) this is the same as the queue length
    DWORD   dwTerminatedThreads;    // (4)
    DWORD   dwWaitingThreads;       // (5)
    DWORD   dwTransitionThreads;    // (6)
} PERFSYS_THREAD_DATA_BLOCK, * PPERFSYS_THREAD_DATA_BLOCK;

ULONG ProcessBufSize = LARGE_BUFFER_SIZE;
ULONG dwSysOpenCount = 0;
UCHAR *pProcessBuffer = NULL;

DWORD APIENTRY
OpenSystemObject (
    LPWSTR lpDeviceNames
    )
{
    UNREFERENCED_PARAMETER(lpDeviceNames);

    dwSysOpenCount++;
    return ERROR_SUCCESS;
}


DWORD
GetSystemThreadInfo (
    PPERFSYS_THREAD_DATA_BLOCK pTDB
)
{
    NTSTATUS    status;
    PSYSTEM_THREAD_INFORMATION ThreadInfo;
    PSYSTEM_PROCESS_INFORMATION ProcessInfo;

    ULONG ProcessNumber;
    ULONG NumThreadInstances;
    ULONG ThreadNumber;
    ULONG ProcessBufferOffset;
    BOOLEAN NullProcess;

    DWORD dwReturnedBufferSize = 0;

#ifdef DBG
    DWORD trialcount = 0;

    STARTTIMING;
#endif

    // reset the caller's buffer
    memset (pTDB, 0, sizeof (PERFSYS_THREAD_DATA_BLOCK));

    if (pProcessBuffer == NULL) {
        ProcessBufSize = LARGE_BUFFER_SIZE;
        pProcessBuffer = ALLOCMEM (ProcessBufSize);
#ifdef DBG
        trialcount = 1;
#endif
    }

    if (pProcessBuffer == NULL) {
        status = STATUS_NO_MEMORY;
    } else {
        while( (status = NtQuerySystemInformation(
                            SystemProcessInformation,
                            pProcessBuffer,
                            ProcessBufSize,
                            &dwReturnedBufferSize)) ==
                                STATUS_INFO_LENGTH_MISMATCH ) {
            if (ProcessBufSize < dwReturnedBufferSize) {
                ProcessBufSize = dwReturnedBufferSize;
            }
            ProcessBufSize = PAGESIZE_MULTIPLE(ProcessBufSize + SMALL_BUFFER_SIZE);
#ifdef DBG
            trialcount++;
#endif
            FREEMEM(pProcessBuffer);
            pProcessBuffer = ALLOCMEM(ProcessBufSize);
            if (pProcessBuffer == NULL) {
                status = STATUS_NO_MEMORY;
                break;
            }
        }
    }
#ifdef DBG
    ENDTIMING (("PERFSYS: %d takes %I64u ms size=%d,%d trials=%d\n", __LINE__, diff,
            dwReturnedBufferSize, ProcessBufSize, trialcount));
#endif

    if ( NT_SUCCESS(status) ) {
        // walk processes and threads to count 'ready' threads
        ProcessNumber = 0;
        NumThreadInstances = 0;

        ProcessInfo = (PSYSTEM_PROCESS_INFORMATION)pProcessBuffer;
        ProcessBufferOffset = 0;

        while ( ProcessInfo != NULL ) {
            if ( ProcessInfo->ImageName.Buffer != NULL ||
                ProcessInfo->NumberOfThreads > 0 ) {
                NullProcess = FALSE;
                pTDB->dwProcessCount++;
            } else {
                NullProcess = TRUE;
                pTDB->dwNullProcessCount++;
            }

            ThreadNumber = 0;       //  Thread number of this process

            ThreadInfo = (PSYSTEM_THREAD_INFORMATION)(ProcessInfo + 1);

            while ( !NullProcess &&
                    ThreadNumber < ProcessInfo->NumberOfThreads ) {

                //
                //  Format and collect Thread data
                //
                pTDB->dwThreadCount++;

                // update thread state counters
                if (ThreadInfo->ThreadState == 1) {
                    // then it's READY
                    pTDB->dwReadyThreads++;
                } else if (ThreadInfo->ThreadState == 4) {
                    // then it's TERMINATED
                    pTDB->dwTerminatedThreads++;
                } else if (ThreadInfo->ThreadState == 5) {
                    // then it's WAITING
                    pTDB->dwWaitingThreads++;
                } else if (ThreadInfo->ThreadState == 6) {
                    // then it's in TRANSITION
                    pTDB->dwTransitionThreads++;
                }

                ThreadNumber++;
                ThreadInfo++;
            }

            if (ProcessInfo->NextEntryOffset == 0) {
                // that was the last process
                break;
            }

            ProcessBufferOffset += ProcessInfo->NextEntryOffset;
            ProcessInfo = (PSYSTEM_PROCESS_INFORMATION)
                            &pProcessBuffer[ProcessBufferOffset];

            if ( !NullProcess ) {
                ProcessNumber++;
            }
        }

    } else if (hEventLog != NULL) {
        ReportEvent (hEventLog,
            EVENTLOG_WARNING_TYPE,
            0,
            PERFOS_UNABLE_QUERY_PROCESS_INFO,
            NULL,
            0,
            sizeof(DWORD),
            NULL,
            (LPVOID)&status);
    }

#ifdef DBG
    ENDTIMING (("PERFSYS: %d takes %I64u ms total\n", __LINE__, diff));
#endif
    return ERROR_SUCCESS;

}

DWORD APIENTRY
CollectSystemObjectData (
    IN OUT  LPVOID  *lppData,
    IN OUT  LPDWORD lpcbTotalBytes,
    IN OUT  LPDWORD lpNumObjectTypes
)
/*++

Routine Description:

    This routine will return the data for the System object

Arguments:

    QuerySystemData -    Get data about system

   IN OUT   LPVOID   *lppData
         IN: pointer to the address of the buffer to receive the completed
            PerfDataBlock and subordinate structures. This routine will
            append its data to the buffer starting at the point referenced
            by *lppData.
         OUT: points to the first byte after the data structure added by this
            routine. This routine updated the value at lppdata after appending
            its data.

   IN OUT   LPDWORD  lpcbTotalBytes
         IN: the address of the DWORD that tells the size in bytes of the
            buffer referenced by the lppData argument
         OUT: the number of bytes added by this routine is writted to the
            DWORD pointed to by this argument

   IN OUT   LPDWORD  NumObjectTypes
         IN: the address of the DWORD to receive the number of objects added
            by this routine
         OUT: the number of objects added by this routine is writted to the
            DWORD pointed to by this argument

         Returns:

             0 if successful, else Win 32 error code of failure

--*/
{
    DWORD   TotalLen;            //  Length of the total return block

    NTSTATUS    ntStatus;

    PSYSTEM_DATA_DEFINITION     pSystemDataDefinition;
    PSYSTEM_COUNTER_DATA        pSCD;

    SYSTEM_EXCEPTION_INFORMATION    ExceptionInfo;
    SYSTEM_REGISTRY_QUOTA_INFORMATION   RegistryInfo;
    SYSTEM_TIMEOFDAY_INFORMATION    SysTimeInfo;
    PERFSYS_THREAD_DATA_BLOCK       TDB;

    DWORD   dwReturnedBufferSize;

    //  Check for sufficient space for system data
    //

#ifdef DBG
    STARTTIMING;
#endif
    pSystemDataDefinition = (SYSTEM_DATA_DEFINITION *) *lppData;

    TotalLen = sizeof(SYSTEM_DATA_DEFINITION) +
            sizeof(SYSTEM_COUNTER_DATA);

    TotalLen = QWORD_MULTIPLE (TotalLen);

    if ( *lpcbTotalBytes < TotalLen ) {
        *lpcbTotalBytes = (DWORD) 0;
        *lpNumObjectTypes = (DWORD) 0;
        return ERROR_MORE_DATA;
    }

    //
    //  Define system data block
    //

    memcpy (pSystemDataDefinition,
        &SystemDataDefinition,
        sizeof(SYSTEM_DATA_DEFINITION));

    //
    //  Format and collect system data
    //

    // get the exception data

    ntStatus = NtQuerySystemInformation(
        SystemExceptionInformation,
        &ExceptionInfo,
        sizeof(ExceptionInfo),
        NULL
    );

    if (!NT_SUCCESS(ntStatus)) {
        // unable to collect the data from the system so
        // clear the return data structure to prevent bogus data from
        // being returned
        if (hEventLog != NULL) {
            ReportEvent (hEventLog,
                EVENTLOG_WARNING_TYPE,
                0,
                PERFOS_UNABLE_QUERY_EXCEPTION_INFO,
                NULL,
                0,
                sizeof(DWORD),
                NULL,
                (LPVOID)&ntStatus);
        }
        memset (&ExceptionInfo, 0, sizeof(ExceptionInfo));
    }

    // collect registry quota info

    memset (&RegistryInfo, 0, sizeof (SYSTEM_REGISTRY_QUOTA_INFORMATION));
    ntStatus = NtQuerySystemInformation (
        SystemRegistryQuotaInformation,
        (PVOID)&RegistryInfo,
        sizeof(RegistryInfo),
        NULL);

    if (ntStatus != STATUS_SUCCESS) {
        if (hEventLog != NULL) {
            ReportEvent (hEventLog,
                EVENTLOG_WARNING_TYPE,
                0,
                PERFOS_UNABLE_QUERY_REGISTRY_QUOTA_INFO,
                NULL,
                0,
                sizeof(DWORD),
                NULL,
                (LPVOID)&ntStatus);
        }
        // clear the data fields
        memset (&RegistryInfo, 0, sizeof (SYSTEM_REGISTRY_QUOTA_INFORMATION));
    }

    ntStatus = NtQuerySystemInformation(
        SystemTimeOfDayInformation,
        &SysTimeInfo,
        sizeof(SysTimeInfo),
        &dwReturnedBufferSize
        );

    if (!NT_SUCCESS(ntStatus)) {
        if (hEventLog != NULL) {
            ReportEvent (hEventLog,
                EVENTLOG_WARNING_TYPE,
                0,
                PERFOS_UNABLE_QUERY_SYSTEM_TIME_INFO,
                NULL,
                0,
                sizeof(DWORD),
                NULL,
                (LPVOID)&ntStatus);
        }
        memset (&SysTimeInfo, 0, sizeof(SysTimeInfo));
    }

    // get thread info
    ntStatus = GetSystemThreadInfo (&TDB);
    if (!NT_SUCCESS(ntStatus)) {
        memset (&TDB, 0, sizeof(TDB));
    }

	// update the object perf time (freq is constant)
    pSystemDataDefinition->SystemObjectType.PerfTime = SysTimeInfo.CurrentTime;

    pSCD = (PSYSTEM_COUNTER_DATA)&pSystemDataDefinition[1];

    pSCD->CounterBlock.ByteLength = QWORD_MULTIPLE(sizeof(SYSTEM_COUNTER_DATA));

    pSCD->ReadOperations    = SysPerfInfo.IoReadOperationCount;
    pSCD->WriteOperations   = SysPerfInfo.IoWriteOperationCount;
    pSCD->OtherIOOperations = SysPerfInfo.IoOtherOperationCount;

    pSCD->ReadBytes         = SysPerfInfo.IoReadTransferCount.QuadPart;
    pSCD->WriteBytes        = SysPerfInfo.IoWriteTransferCount.QuadPart;
    pSCD->OtherIOBytes      = SysPerfInfo.IoOtherTransferCount.QuadPart;

    pSCD->ContextSwitches   = SysPerfInfo.ContextSwitches;
    pSCD->SystemCalls       = SysPerfInfo.SystemCalls;

    pSCD->TotalReadWrites   = SysPerfInfo.IoReadOperationCount +
                                SysPerfInfo.IoWriteOperationCount;

    pSCD->SystemElapsedTime = SysTimeInfo.BootTime.QuadPart - SysTimeInfo.BootTimeBias;

    // leave room for the ProcessorQueueLength data
    pSCD->ProcessorQueueLength  = TDB.dwReadyThreads;
    pSCD->ProcessCount          = TDB.dwProcessCount;
    pSCD->ThreadCount           = TDB.dwThreadCount;

    pSCD->AlignmentFixups       = ExceptionInfo.AlignmentFixupCount ;
    pSCD->ExceptionDispatches   = ExceptionInfo.ExceptionDispatchCount ;
    pSCD->FloatingPointEmulations = ExceptionInfo.FloatingEmulationCount ;

    pSCD->RegistryQuotaUsed     = RegistryInfo.RegistryQuotaUsed;
    pSCD->RegistryQuotaAllowed  = RegistryInfo.RegistryQuotaAllowed;

    *lpcbTotalBytes =
        pSystemDataDefinition->SystemObjectType.TotalByteLength =
            (DWORD) QWORD_MULTIPLE(((LPBYTE) (& pSCD[1])) - (LPBYTE) pSystemDataDefinition);
    * lppData = (LPVOID) (((LPBYTE) pSystemDataDefinition) + * lpcbTotalBytes);

    *lpNumObjectTypes = 1;

#ifdef DBG
    ENDTIMING (("PERFSYS: %d takes %I64u ms total\n", __LINE__, diff));
#endif
    return ERROR_SUCCESS;
}

DWORD APIENTRY
CloseSystemObject (
)
/*++

Routine Description:

    This routine closes the open handles to the Signal Gen counters.

Arguments:

    None.


Return Value:

    ERROR_SUCCESS

--*/

{
    UCHAR *pBuffer;

    if (dwSysOpenCount > 0) {
        if (!(--dwSysOpenCount)) { // when this is the last thread...
            // close stuff here
            if ((hLibHeap != NULL) && (pProcessBuffer != NULL)) {
                pBuffer = pProcessBuffer;
                pProcessBuffer = NULL;
                FREEMEM (pBuffer);
                ProcessBufSize = 0;
            }
        }
    }

    return ERROR_SUCCESS;

}
