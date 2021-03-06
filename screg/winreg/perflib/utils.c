/*++ BUILD Version: 0001    // Increment this if a change has global effects

Copyright (c) 1994-1997   Microsoft Corporation

Module Name:

    utils.c

Abstract:

        Utility functions used by the performance library functions

Author:

    Russ Blake  11/15/91

Revision History:


--*/
#define UNICODE
//
//  Include files
//
#pragma warning(disable:4306)
#include <nt.h>
#include <ntrtl.h>
#include <nturtl.h>
#include <windows.h>
#include <winperf.h>
#include <prflbmsg.h>
#include <regrpc.h>
#include "ntconreg.h"
#include "perflib.h"
#pragma warning(default:4306)

// test for delimiter, end of line and non-digit characters
// used by IsNumberInUnicodeList routine
//
#define DIGIT       1
#define DELIMITER   2
#define INVALID     3

#define EvalThisChar(c,d) ( \
     (c == d) ? DELIMITER : \
     (c == 0) ? DELIMITER : \
     (c < '0') ? INVALID : \
     (c > '9') ? INVALID : \
     DIGIT)

#define MAX_KEYWORD_LEN   (sizeof (ADDHELP_STRING) / sizeof(WCHAR))
const   WCHAR GLOBAL_STRING[]     = L"GLOBAL";
const   WCHAR FOREIGN_STRING[]    = L"FOREIGN";
const   WCHAR COSTLY_STRING[]     = L"COSTLY";
const   WCHAR COUNTER_STRING[]    = L"COUNTER";
const   WCHAR HELP_STRING[]       = L"EXPLAIN";
const   WCHAR HELP_STRING2[]      = L"HELP";
const   WCHAR ADDCOUNTER_STRING[] = L"ADDCOUNTER";
const   WCHAR ADDHELP_STRING[]    = L"ADDEXPLAIN";
const   WCHAR ONLY_STRING[]       = L"ONLY";
const   WCHAR DisablePerformanceCounters[] = L"Disable Performance Counters";

// minimum length to hold a value name understood by Perflib

const   DWORD VALUE_NAME_LENGTH = ((sizeof(COSTLY_STRING) * 2) + sizeof(UNICODE_NULL));

#define PL_TIMER_START_EVENT    0
#define PL_TIMER_EXIT_EVENT     1
#define PL_TIMER_NUM_OBJECTS    2

static HANDLE   hTimerHandles[PL_TIMER_NUM_OBJECTS] = {NULL,NULL};

static  HANDLE  hTimerDataMutex = NULL;
static  HANDLE  hPerflibTimingThread   = NULL;
static  LPOPEN_PROC_WAIT_INFO   pTimerItemListHead = NULL;
#define PERFLIB_TIMER_INTERVAL  200     // 200 ms Timer
#define PERFLIB_TIMEOUT_COUNT    64

extern HANDLE hEventLog;

#ifdef DBG
#include <stdio.h> // for _vsnprintf
#define DEBUG_BUFFER_LENGTH MAX_PATH*2

ULONG PerfLibDebug = 0;
UCHAR PerfLibDebugBuffer[DEBUG_BUFFER_LENGTH];
#endif

RTL_CRITICAL_SECTION PerfpCritSect;


//
//  Perflib functions:
//
NTSTATUS
GetPerflibKeyValue (
    IN      LPCWSTR szItem,
    IN      DWORD   dwRegType,
    IN      DWORD   dwMaxSize,      // ... of pReturnBuffer in bytes
    OUT     LPVOID  pReturnBuffer,
    IN      DWORD   dwDefaultSize,  // ... of pDefault in bytes
    IN      LPVOID  pDefault,
    IN OUT  PHKEY   pKey
)
/*++

    read and return the current value of the specified value
    under the Perflib registry key. If unable to read the value
    return the default value from the argument list.

    the value is returned in the pReturnBuffer.

    NOTE: if pKey is NULL, this routine will open and close a local key
          if pKey is not NULL, this routine will return the handle to the
          Perflib regkey and the caller is responsible for closing it.

--*/
{

    HKEY                    hPerflibKey;
    OBJECT_ATTRIBUTES       Obja;
    NTSTATUS                Status;
    UNICODE_STRING          PerflibSubKeyString;
    UNICODE_STRING          ValueNameString;
    LONG                    lReturn = STATUS_SUCCESS;
    PKEY_VALUE_PARTIAL_INFORMATION  pValueInformation;
    ULONG                   ValueBufferLength;
    ULONG                   ResultLength;
    BOOL                    bUseDefault = TRUE;

    Status = STATUS_SUCCESS;
    hPerflibKey = NULL;
    if (pKey != NULL) {
        hPerflibKey = *pKey;
    }
    if (hPerflibKey == NULL) {
        // initialize UNICODE_STRING structures used in this function

        RtlInitUnicodeString (
            &PerflibSubKeyString,
            (LPCWSTR)L"\\Registry\\Machine\\SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\Perflib");

        RtlInitUnicodeString (
            &ValueNameString,
            (LPWSTR)szItem);

        //
        // Initialize the OBJECT_ATTRIBUTES structure and open the key.
        //
        InitializeObjectAttributes(
                &Obja,
                &PerflibSubKeyString,
                OBJ_CASE_INSENSITIVE,
                NULL,
                NULL
                );

        Status = NtOpenKey(
                    &hPerflibKey,
                    KEY_READ,
                    &Obja
                    );
    }

    if (NT_SUCCESS( Status )) {
        // read value of desired entry

        ValueBufferLength = ResultLength = 1024;
        pValueInformation = ALLOCMEM(ResultLength);

        if (pValueInformation != NULL) {
            while ( (Status = NtQueryValueKey(hPerflibKey,
                                            &ValueNameString,
                                            KeyValuePartialInformation,
                                            pValueInformation,
                                            ValueBufferLength,
                                            &ResultLength))
                    == STATUS_BUFFER_OVERFLOW ) {

                FREEMEM(pValueInformation);
                pValueInformation = ALLOCMEM(ResultLength);
                if ( pValueInformation == NULL) {
                    ValueBufferLength = 0;
                    Status = STATUS_NO_MEMORY;
                    break;
                } else {
                    ValueBufferLength = ResultLength;
                }
            }

            if (NT_SUCCESS(Status)) {
                // check to see if it's the desired type
                if (pValueInformation->Type == dwRegType) {
                    // see if it will fit
                    if (pValueInformation->DataLength <= dwMaxSize) {
                        memcpy (pReturnBuffer, &pValueInformation->Data[0],
                            pValueInformation->DataLength);
                        bUseDefault = FALSE;
                        lReturn = STATUS_SUCCESS;
                    }
                }
            } else {
                // return the default value
                lReturn = Status;
            }
            // release temp buffer
            if (pValueInformation) {
                FREEMEM (pValueInformation);
            }
        } else {
            // unable to allocate memory for this operation so
            // just return the default value
        }
        if (pKey == NULL) {
            // close the local registry key
            NtClose(hPerflibKey);
        }
        else {
            *pKey = hPerflibKey;
        }
    } else {
        // return default value
    }

    if (bUseDefault) {
        memcpy (pReturnBuffer, pDefault, dwDefaultSize);
        lReturn = STATUS_SUCCESS;
    }

    return lReturn;
}

BOOL
MatchString (
    IN LPCWSTR lpValueArg,
    IN LPCWSTR lpNameArg
)
/*++

MatchString

    return TRUE if lpName is in lpValue.  Otherwise return FALSE

Arguments

    IN lpValue
        string passed to PerfRegQuery Value for processing

    IN lpName
        string for one of the keyword names

Return TRUE | FALSE

--*/
{
    BOOL    bFound      = TRUE; // assume found until contradicted
    LPWSTR  lpValue     = (LPWSTR)lpValueArg;
    LPWSTR  lpName      = (LPWSTR)lpNameArg;

    // check to the length of the shortest string

    while ((*lpValue != 0) && (*lpName != 0)) {
        if (*lpValue++ != *lpName++) {
            bFound = FALSE; // no match
            break;          // bail out now
        }
    }

    return (bFound);
}

DWORD
GetQueryType (
    IN LPWSTR lpValue
)
/*++

GetQueryType

    returns the type of query described in the lpValue string so that
    the appropriate processing method may be used

Arguments

    IN lpValue
        string passed to PerfRegQuery Value for processing

Return Value

    QUERY_GLOBAL
        if lpValue == 0 (null pointer)
           lpValue == pointer to Null string
           lpValue == pointer to "Global" string

    QUERY_FOREIGN
        if lpValue == pointer to "Foriegn" string

    QUERY_COSTLY
        if lpValue == pointer to "Costly" string

    QUERY_COUNTER
        if lpValue == pointer to "Counter" string

    QUERY_HELP
        if lpValue == pointer to "Explain" string

    QUERY_ADDCOUNTER
        if lpValue == pointer to "Addcounter" string

    QUERY_ADDHELP
        if lpValue == pointer to "Addexplain" string

    otherwise:

    QUERY_ITEMS

--*/
{
    WCHAR   LocalBuff[MAX_KEYWORD_LEN+1];
    WORD    i;

    if (lpValue == 0 || *lpValue == 0)
        return QUERY_GLOBAL;

    // convert the input string to Upper case before matching
    for (i=0; i < MAX_KEYWORD_LEN; i++) {
        if (*lpValue == TEXT(' ') || *lpValue == TEXT('\0')) {
            break;
        }
        LocalBuff[i] = *lpValue ;
        if (*lpValue >= TEXT('a') && *lpValue <= TEXT('z')) {
            LocalBuff[i]  = LocalBuff[i] - TEXT('a') + TEXT('A');
        }
        lpValue++ ;
    }
    LocalBuff[i] = TEXT('\0');

    // check for "Global" request
    if (MatchString (LocalBuff, GLOBAL_STRING))
        return QUERY_GLOBAL ;

    // check for "Foreign" request
    if (MatchString (LocalBuff, FOREIGN_STRING))
        return QUERY_FOREIGN ;

    // check for "Costly" request
    if (MatchString (LocalBuff, COSTLY_STRING))
        return QUERY_COSTLY;

    // check for "Counter" request
    if (MatchString (LocalBuff, COUNTER_STRING))
        return QUERY_COUNTER;

    // check for "Help" request
    if (MatchString (LocalBuff, HELP_STRING))
        return QUERY_HELP;

    if (MatchString (LocalBuff, HELP_STRING2))
        return QUERY_HELP;

    // check for "AddCounter" request
    if (MatchString (LocalBuff, ADDCOUNTER_STRING))
        return QUERY_ADDCOUNTER;

    // check for "AddHelp" request
    if (MatchString (LocalBuff, ADDHELP_STRING))
        return QUERY_ADDHELP;

    // None of the above, then it must be an item list
    return QUERY_ITEMS;

}

DWORD
GetNextNumberFromList (
    IN LPWSTR   szStartChar,
    IN LPWSTR   *szNextChar
)
/*++

 Reads a character string from the szStartChar to the next
 delimiting space character or the end of the string and returns
 the value of the decimal number found. If no valid number is found
 then 0 is returned. The pointer to the next character in the
 string is returned in the szNextChar parameter. If the character
 referenced by this pointer is 0, then the end of the string has
 been reached.

--*/
{
    DWORD   dwThisNumber    = 0;
    WCHAR   *pwcThisChar    = szStartChar;
    WCHAR   wcDelimiter     = L' ';
    BOOL    bValidNumber    = FALSE;

    if (szStartChar != 0) {
        do {
            switch (EvalThisChar (*pwcThisChar, wcDelimiter)) {
                case DIGIT:
                    // if this is the first digit after a delimiter, then
                    // set flags to start computing the new number
                    bValidNumber = TRUE;
                    dwThisNumber *= 10;
                    dwThisNumber += (*pwcThisChar - (WCHAR)'0');
                    break;

                case DELIMITER:
                    // a delimter is either the delimiter character or the
                    // end of the string ('\0') if when the delimiter has been
                    // reached a valid number was found, then return it
                    //
                    if (bValidNumber || (*pwcThisChar == 0)) {
                        *szNextChar = pwcThisChar;
                        return dwThisNumber;
                    } else {
                        // continue until a non-delimiter char or the
                        // end of the file is found
                    }
                    break;

                case INVALID:
                    // if an invalid character was encountered, ignore all
                    // characters up to the next delimiter and then start fresh.
                    // the invalid number is not compared.
                    bValidNumber = FALSE;
                    break;

                default:
                    break;

            }
            pwcThisChar++;
        } while (pwcThisChar != NULL);    // always TRUE - avoid W4 warning
        return 0;
    } else {
        *szNextChar = szStartChar;
        return 0;
    }
}

BOOL
IsNumberInUnicodeList (
    IN DWORD   dwNumber,
    IN LPWSTR  lpwszUnicodeList
)
/*++

IsNumberInUnicodeList

Arguments:

    IN dwNumber
        DWORD number to find in list

    IN lpwszUnicodeList
        Null terminated, Space delimited list of decimal numbers

Return Value:

    TRUE:
            dwNumber was found in the list of unicode number strings

    FALSE:
            dwNumber was not found in the list.

--*/
{
    DWORD   dwThisNumber;
    WCHAR   *pwcThisChar;

    if (lpwszUnicodeList == 0) return FALSE;    // null pointer, # not founde

    pwcThisChar = lpwszUnicodeList;
    dwThisNumber = 0;

    while (*pwcThisChar != 0) {
        dwThisNumber = GetNextNumberFromList (
            pwcThisChar, &pwcThisChar);
        if (dwNumber == dwThisNumber) return TRUE;
    }
    // if here, then the number wasn't found
    return FALSE;

}   // IsNumberInUnicodeList

BOOL
MonBuildPerfDataBlock(
    PERF_DATA_BLOCK *pBuffer,
    PVOID *pBufferNext,
    DWORD NumObjectTypes,
    DWORD DefaultObject
)
/*++

    MonBuildPerfDataBlock -     build the PERF_DATA_BLOCK structure

        Inputs:

            pBuffer         -   where the data block should be placed

            pBufferNext     -   where pointer to next byte of data block
                                is to begin; DWORD aligned

            NumObjectTypes  -   number of types of objects being reported

            DefaultObject   -   object to display by default when
                                this system is selected; this is the
                                object type title index
--*/

{
    // Initialize Signature and version ID for this data structure

    pBuffer->Signature[0] = L'P';
    pBuffer->Signature[1] = L'E';
    pBuffer->Signature[2] = L'R';
    pBuffer->Signature[3] = L'F';

    pBuffer->LittleEndian = TRUE;

    pBuffer->Version = PERF_DATA_VERSION;
    pBuffer->Revision = PERF_DATA_REVISION;

    //
    //  The next field will be filled in at the end when the length
    //  of the return data is known
    //

    pBuffer->TotalByteLength = 0;

    pBuffer->NumObjectTypes = NumObjectTypes;
    pBuffer->DefaultObject = DefaultObject;
    GetSystemTime(&pBuffer->SystemTime);
    NtQueryPerformanceCounter(&pBuffer->PerfTime,&pBuffer->PerfFreq);
    GetSystemTimeAsFileTime ((FILETIME *)&pBuffer->PerfTime100nSec.QuadPart);

    if ( ComputerNameLength ) {

        //  There is a Computer name: i.e., the network is installed

        pBuffer->SystemNameLength = ComputerNameLength;
        pBuffer->SystemNameOffset = sizeof(PERF_DATA_BLOCK);
        RtlMoveMemory(&pBuffer[1],
               pComputerName,
               ComputerNameLength);
        *pBufferNext = (PVOID) ((PCHAR) &pBuffer[1] +
                                QWORD_MULTIPLE(ComputerNameLength));
        pBuffer->HeaderLength = (DWORD)((PCHAR) *pBufferNext - (PCHAR) pBuffer);
    } else {

        // Member of Computers Anonymous

        pBuffer->SystemNameLength = 0;
        pBuffer->SystemNameOffset = 0;
        *pBufferNext = &pBuffer[1];
        pBuffer->HeaderLength = sizeof(PERF_DATA_BLOCK);
    }

    return 0;
}

//
// Timer functions
//
DWORD
PerflibTimerFunction (
    LPDWORD dwArg
)
/*++

 PerflibTimerFunction

    Timing thread used to write an event log message if the timer expires.

    This thread runs until the Exit event is set or the wait for the
    Exit event times out.

    While the start event is set, then the timer checks the current events
    to be timed and reports on any that have expired. It then sleeps for
    the duration of the timing interval after which it checks the status
    of the start & exit events to begin the next cycle.

    The timing events are added and deleted from the list only by the
    StartPerflibFunctionTimer and KillPerflibFunctionTimer functions.

 Arguments

    dwArg -- Not Used

--*/
{
    NTSTATUS                NtStatus = STATUS_SUCCESS;
    BOOL                    bKeepTiming = TRUE;
    LPOPEN_PROC_WAIT_INFO   pLocalInfo;
    LPWSTR                  szMessageArray[2];
    LARGE_INTEGER           liWaitTime;

    UNREFERENCED_PARAMETER (dwArg);

    DebugPrint ((5, "\nPERFLIB: Entering Timing Thread: PID: %d, TID: %d", 
        GetCurrentProcessId(), GetCurrentThreadId()));

    TRACE((WINPERF_DBG_TRACE_INFO),
          (& PerflibGuid,
           __LINE__,
           PERF_TIMERFUNCTION,
           0,
           STATUS_SUCCESS,
           NULL));

    while (bKeepTiming) {
        liWaitTime.QuadPart =
            MakeTimeOutValue((PERFLIB_TIMING_THREAD_TIMEOUT));
        // wait for either the start or exit event flags to be set
        NtStatus = NtWaitForMultipleObjects (
                PL_TIMER_NUM_OBJECTS,
                &hTimerHandles[0],
                WaitAny,          //wait for either one to be set
                FALSE,            // not alertable
                &liWaitTime);

        if ((NtStatus != STATUS_TIMEOUT) &&
            (NtStatus <= STATUS_WAIT_3)) {
            if ((NtStatus - STATUS_WAIT_0) == PL_TIMER_EXIT_EVENT ) {
              DebugPrint ((5, "\nPERFLIB: Timing Thread received Exit Event (1): PID: %d, TID: %d", 
                    GetCurrentProcessId(), GetCurrentThreadId()));

                // then that's all
                bKeepTiming = FALSE;
                NtStatus = STATUS_SUCCESS;
                break;
            } else if ((NtStatus - STATUS_WAIT_0) == PL_TIMER_START_EVENT) {
                DebugPrint ((5, "\nPERFLIB: Timing Thread received Start Event: PID: %d, TID: %d", 
                    GetCurrentProcessId(), GetCurrentThreadId()));
                // then the timer is running so wait the interval period
                // wait on exit event here to prevent hanging
                liWaitTime.QuadPart =
                    MakeTimeOutValue((PERFLIB_TIMER_INTERVAL));
                NtStatus = NtWaitForSingleObject (
                        hTimerHandles[PL_TIMER_EXIT_EVENT],
                        FALSE,
                        &liWaitTime);

                if (NtStatus == STATUS_TIMEOUT) {
                    // then the wait time expired without being told
                    // to terminate the thread so
                    // now evaluate the list of timed events
                    // lock the data mutex
                    DWORD dwTimeOut = 0;

                    DebugPrint ((5, "\nPERFLIB: Timing Thread Evaluating Entries: PID: %d, TID: %d", 
                        GetCurrentProcessId(), GetCurrentThreadId()));

                    liWaitTime.QuadPart =
                        MakeTimeOutValue((PERFLIB_TIMER_INTERVAL * 2));

                    NtStatus = STATUS_TIMEOUT;
                    while (   NtStatus == STATUS_TIMEOUT
                           && dwTimeOut < PERFLIB_TIMEOUT_COUNT) {
                        NtStatus = NtWaitForSingleObject (
                                hTimerDataMutex,
                                FALSE,
                                & liWaitTime);
                        if (NtStatus == STATUS_TIMEOUT) {
                            dwTimeOut ++;
                            DebugPrint((5, "\nPERFLIB:NtWaitForSingleObject(TimerDataMutex,%d) time out for the %dth time. PID: %d, TID: %d",
                                    liWaitTime, dwTimeOut,
                                    GetCurrentProcessId(),
                                    GetCurrentThreadId()));
                            TRACE((WINPERF_DBG_TRACE_WARNING),
                                  (& PerflibGuid,
                                   __LINE__,
                                   PERF_TIMERFUNCTION,
                                   0,
                                   STATUS_TIMEOUT,
                                   & dwTimeOut, sizeof(dwTimeOut),
                                   NULL));
                        }
                    }

                    if (NtStatus != STATUS_WAIT_0) {
                        // cannot grab hTimerDataMutex, there is no guarantee
                        // that this is the exclusive one to work on
                        // pTimerItemListHead list, so just bail out.
                        //
                        bKeepTiming = FALSE;
                        NtStatus    = STATUS_SUCCESS;
                        TRACE((WINPERF_DBG_TRACE_WARNING),
                              (& PerflibGuid,
                               __LINE__,
                               PERF_TIMERFUNCTION,
                               0,
                               NtStatus,
                               NULL));
                        break;
                    }
                    else {
                        for (pLocalInfo = pTimerItemListHead;
                            pLocalInfo != NULL;
                            pLocalInfo = pLocalInfo->pNext) {

                              DebugPrint ((5, "\nPERFLIB: Timing Thread Entry %X. count %d: PID: %d, TID: %d", 
                              pLocalInfo, pLocalInfo->dwWaitTime,
                              GetCurrentProcessId(), GetCurrentThreadId()));

                            if (pLocalInfo->dwWaitTime > 0) {
                                if (pLocalInfo->dwWaitTime == 1) {
                                    if (THROTTLE_PERFLIB(pLocalInfo->dwEventMsg)) {
                                        // then this is the last interval so log error
                                        // if this DLL hasn't already been disabled

                                        szMessageArray[0] = pLocalInfo->szServiceName;
                                        szMessageArray[1] = pLocalInfo->szLibraryName;

                                        ReportEvent (hEventLog,
                                            EVENTLOG_ERROR_TYPE,  // error type
                                            0,                    // category (not used)
                                            (DWORD)pLocalInfo->dwEventMsg, // event,
                                            NULL,                 // SID (not used),
                                            2,                    // number of strings
                                            0,                    // sizeof raw data
                                            szMessageArray,       // message text array
                                            NULL);                // raw data
                                    }

                                    if (pLocalInfo->pData != NULL) {
                                        if (lPerflibConfigFlags & PLCF_ENABLE_TIMEOUT_DISABLE) {
                                            if (!(((PEXT_OBJECT)pLocalInfo->pData)->dwFlags & PERF_EO_DISABLED)) {
                                                // then pData is an extensible counter data block
                                                // disable the ext. counter
                                                DisablePerfLibrary((PEXT_OBJECT) pLocalInfo->pData,
                                                                   PERFLIB_DISABLE_ALL);
                                            } // end if not already disabled
                                        } // end if disable DLL on Timeouts is enabled
                                    } // data is NULL so skip
                                } 
                                pLocalInfo->dwWaitTime--;
                            }
                        }
                        ReleaseMutex (hTimerDataMutex);
                    }
                } else {
                  DebugPrint ((5, "\nPERFLIB: Timing Thread received Exit Event (2): PID: %d, TID: %d", 
                     GetCurrentProcessId(), GetCurrentThreadId()));

                    // we've been told to exit so
                    NtStatus = STATUS_SUCCESS;
                    bKeepTiming = FALSE;
                    break;
                }
            } else {
                // some unexpected error was returned
                assert (FALSE);
            }
        } else {
            DebugPrint ((5, "\nPERFLIB: Timing Thread Timed out: PID: %d, TID: %d", 
                GetCurrentProcessId(), GetCurrentThreadId()));
            // the wait timed out so it's time to go
            NtStatus = STATUS_SUCCESS;
            bKeepTiming = FALSE;
            break;
        }
    }

    DebugPrint ((5, "\nPERFLIB: Leaving Timing Thread: PID: %d, TID: %d", 
        GetCurrentProcessId(), GetCurrentThreadId()));

    return PerfpDosError(NtStatus);
}

HANDLE
StartPerflibFunctionTimer (
    IN  LPOPEN_PROC_WAIT_INFO pInfo
)
/*++

    Starts a timing event by adding it to the list of timing events.
    If the timer thread is not running, then the is started as well.

    If this is the first event in the list then the Start Event is
    set indicating that the timing thread can begin processing timing
    event(s).

--*/
{
    LONG    Status = ERROR_SUCCESS;
    LPOPEN_PROC_WAIT_INFO   pLocalInfo = NULL;
    DWORD   dwLibNameLen = 0;
    DWORD   dwBufferLength = sizeof (OPEN_PROC_WAIT_INFO);
    LARGE_INTEGER   liWaitTime;
    HANDLE  hReturn = NULL;
    HANDLE  hDataMutex;

    if (pInfo == NULL) {
        // no required argument
        Status = ERROR_INVALID_PARAMETER;
    } else {
        // check on or create sync objects

        // allocate timing events for the timing thread
        if (hTimerHandles[PL_TIMER_START_EVENT] == NULL) {
            // create the event as NOT signaled since we're not ready to start
            hTimerHandles[PL_TIMER_START_EVENT] = CreateEvent (NULL, TRUE, FALSE, NULL);
            if (hTimerHandles[PL_TIMER_START_EVENT] == NULL) {
                Status = GetLastError();
            }
        }

        if (hTimerHandles[PL_TIMER_EXIT_EVENT] == NULL) {
            hTimerHandles[PL_TIMER_EXIT_EVENT] = CreateEvent (NULL, TRUE, FALSE, NULL);
            if (hTimerHandles[PL_TIMER_EXIT_EVENT] == NULL) {
            Status = GetLastError();
            }
        }

        // create data sync mutex if it hasn't already been created
        if (hTimerDataMutex  == NULL) {
            hDataMutex = CreateMutex(NULL, FALSE, NULL);
            if (hDataMutex == NULL) {
                Status = GetLastError();
            }
            else {
                if (InterlockedCompareExchangePointer(& hTimerDataMutex,
                        hDataMutex,
                        NULL) != NULL) {
                    CloseHandle(hDataMutex);
                    hDataMutex = NULL;
                }
                else {
                    hTimerDataMutex = hDataMutex;
                }
            }
        }
    }

    if (Status == ERROR_SUCCESS) {
        // continue creating timer entry
        if (hPerflibTimingThread != NULL) {
    	    // see if the handle is valid (i.e the thread is alive)
            Status = WaitForSingleObject (hPerflibTimingThread, 0);
    	    if (Status == WAIT_OBJECT_0) {
                // the thread has terminated so close the handle
                CloseHandle (hPerflibTimingThread);
    	        hPerflibTimingThread = NULL;
    	        Status = ERROR_SUCCESS;
    	    } else if (Status == WAIT_TIMEOUT) {
		// the thread is still running so continue
		Status = ERROR_SUCCESS;
    	    } else {
		// some other, probably serious, error
		// so pass it on through
	    }
        } else {
	        // the thread has never been created yet so continue
        }

        if (hPerflibTimingThread == NULL) {
            // create the timing thread

            assert (pTimerItemListHead == NULL);    // there should be no entries, yet

            // everything is ready for the timer thread

            hPerflibTimingThread = CreateThread (
                NULL, 0,
                (LPTHREAD_START_ROUTINE)PerflibTimerFunction,
                NULL, 0, NULL);

            assert (hPerflibTimingThread != NULL);
            if (hPerflibTimingThread == NULL) {
                Status = GetLastError();
            }
        }

        if (Status == ERROR_SUCCESS) {

            // compute the length of the required buffer;

            dwLibNameLen = (lstrlenW (pInfo->szLibraryName) + 1) * sizeof(WCHAR);
            dwBufferLength += dwLibNameLen;
            dwBufferLength += (lstrlenW (pInfo->szServiceName) + 1) * sizeof(WCHAR);
            dwBufferLength = QWORD_MULTIPLE (dwBufferLength);

            pLocalInfo = ALLOCMEM (dwBufferLength);
            if (pLocalInfo == NULL)
                Status = ERROR_OUTOFMEMORY;
        }
        if ((Status == ERROR_SUCCESS) && (pLocalInfo != NULL)) {

            // copy the arg buffer to the local list

            pLocalInfo->szLibraryName = (LPWSTR)&pLocalInfo[1];
            lstrcpyW (pLocalInfo->szLibraryName, pInfo->szLibraryName);
            pLocalInfo->szServiceName = (LPWSTR)
                ((LPBYTE)pLocalInfo->szLibraryName + dwLibNameLen);
            lstrcpyW (pLocalInfo->szServiceName, pInfo->szServiceName);
            // convert wait time in milliseconds to the number of "loops"
            pLocalInfo->dwWaitTime = pInfo->dwWaitTime / PERFLIB_TIMER_INTERVAL;
            if (pLocalInfo->dwWaitTime  == 0) pLocalInfo->dwWaitTime =1; // have at least 1 loop
            pLocalInfo->dwEventMsg = pInfo->dwEventMsg;
            pLocalInfo->pData = pInfo->pData;

            // wait for access to the data
            if (hTimerDataMutex != NULL) {
                NTSTATUS NtStatus;
                liWaitTime.QuadPart =
                    MakeTimeOutValue((PERFLIB_TIMER_INTERVAL * 2));

                NtStatus = NtWaitForSingleObject (
                    hTimerDataMutex,
                    FALSE,
                    &liWaitTime);
                Status = PerfpDosError(NtStatus);
            } else {
                Status = ERROR_NOT_READY;
            }

            if (Status == WAIT_OBJECT_0) {
                DebugPrint ((5, "\nPERFLIB: Timing Thread Adding Entry: %X (%d) PID: %d, TID: %d", 
                    pLocalInfo, pLocalInfo->dwWaitTime,
                    GetCurrentProcessId(), GetCurrentThreadId()));

                // we have access to the data so add this item to the front of the list
                pLocalInfo->pNext = pTimerItemListHead;
                pTimerItemListHead = pLocalInfo;
                ReleaseMutex (hTimerDataMutex);

                if (pLocalInfo->pNext == NULL) {
                    // then the list was empty before this call so start the timer
                    // going
                    SetEvent (hTimerHandles[PL_TIMER_START_EVENT]);
                }

                hReturn = (HANDLE)pLocalInfo;
            } else {
                SetLastError (Status);
            }
        } else {
            // unable to create thread
            SetLastError (Status);
        }
    } else {
        // unable to start timer
        SetLastError (Status);
    }

    return hReturn;
}

DWORD
KillPerflibFunctionTimer (
    IN  HANDLE  hPerflibTimer
)
/*++

    Terminates a timing event by removing it from the list. When the last
    item is removed from the list the Start event is reset so the timing
    thread will wait for either the next start event, exit event or it's
    timeout to expire.

--*/
{
    NTSTATUS Status;
    LPOPEN_PROC_WAIT_INFO   pArg = (LPOPEN_PROC_WAIT_INFO)hPerflibTimer;
    LPOPEN_PROC_WAIT_INFO   pLocalInfo;
    BOOL                    bFound = FALSE;
    LARGE_INTEGER           liWaitTime;
    DWORD   dwReturn = ERROR_SUCCESS;

    if (hTimerDataMutex == NULL) {
        dwReturn = ERROR_NOT_READY;
    } else if (pArg == NULL) {
        dwReturn = ERROR_INVALID_HANDLE;
    } else {
	// so far so good
        // wait for access to the data
        liWaitTime.QuadPart =
            MakeTimeOutValue((PERFLIB_TIMER_INTERVAL * 2));
        Status = NtWaitForSingleObject (
            hTimerDataMutex,
            FALSE,
            &liWaitTime);

        if (Status == STATUS_WAIT_0) {
            // we have access to the list so walk down the list and remove the
            // specified item
            // see if it's the first one in the list

            DebugPrint ((5, "\nPERFLIB: Timing Thread Removing Entry: %X (%d) PID: %d, TID: %d", 
                pArg, pArg->dwWaitTime,
                GetCurrentProcessId(), GetCurrentThreadId()));

            if (pArg == pTimerItemListHead) {
                // then remove it
                pTimerItemListHead = pArg->pNext;
                bFound = TRUE;
            } else {
                for (pLocalInfo = pTimerItemListHead;
                    pLocalInfo != NULL;
                    pLocalInfo = pLocalInfo->pNext) {
                    if (pLocalInfo->pNext == pArg) {
                        pLocalInfo->pNext = pArg->pNext;
                        bFound = TRUE;
                        break;
                    }
                }
            }
            assert (bFound);

            if (bFound) {
                // it's out of the list so release the lock
                ReleaseMutex (hTimerDataMutex);

                if (pTimerItemListHead == NULL) {
                    // then the list is empty now so stop timing
                    // going
                    ResetEvent (hTimerHandles[PL_TIMER_START_EVENT]);
                }

                // free memory

                FREEMEM (pArg);
                dwReturn = ERROR_SUCCESS;
            } else {
                dwReturn = ERROR_NOT_FOUND;
            }
        } else {
            dwReturn = ERROR_TIMEOUT;
        }
    }

    return dwReturn;
}

DWORD
DestroyPerflibFunctionTimer (
)
/*++

    Terminates the timing thread and cancels any current timer events.
    NOTE: This routine can be called even if timer thread is not started!

--*/
{
    NTSTATUS    Status   = STATUS_WAIT_0;
    LPOPEN_PROC_WAIT_INFO   pThisItem;
    LPOPEN_PROC_WAIT_INFO   pNextItem;
    LARGE_INTEGER           liWaitTime;
    HANDLE hTemp;

    if (hTimerDataMutex != NULL) {
        DWORD  dwTimeOut = 0;
        LONG   dwStatus  = ERROR_SUCCESS;

        // wait for data mutex
        liWaitTime.QuadPart =
            MakeTimeOutValue((PERFLIB_TIMER_INTERVAL * 5));

        Status = STATUS_TIMEOUT;
        while (Status == STATUS_TIMEOUT && dwTimeOut < PERFLIB_TIMEOUT_COUNT) {
            Status = NtWaitForSingleObject (
                    hTimerDataMutex,
                    FALSE,
                    & liWaitTime);
            if (Status == STATUS_TIMEOUT) {
                if (hPerflibTimingThread != NULL) {
                    // see if the handle is valid (i.e the thread is alive)
                    dwStatus = WaitForSingleObject(hPerflibTimingThread,
                                                   liWaitTime.LowPart);
                    if (dwStatus == WAIT_OBJECT_0) {
                        // the thread has terminated so close the handle
                        Status = STATUS_WAIT_0;
                    }
                }
            }
            if (Status == STATUS_TIMEOUT) {
                dwTimeOut ++;
                DebugPrint((5, "\nPERFLIB:NtWaitForSingleObject(TimerDataMutex,%d) time out for the %dth time in DestroyPErflibFunctionTimer(). PID: %d, TID: %d",
                        liWaitTime, dwTimeOut,
                        GetCurrentProcessId(),
                        GetCurrentThreadId()));
                TRACE((WINPERF_DBG_TRACE_WARNING),
                      (& PerflibGuid,
                       __LINE__,
                       PERF_DESTROYFUNCTIONTIMER,
                       0,
                       STATUS_TIMEOUT,
                       & dwTimeOut, sizeof(dwTimeOut),
                       NULL));
            }
        }

        assert (Status != STATUS_TIMEOUT);
    }

    // free all entries in the list

    if (Status == STATUS_WAIT_0) {
        for (pNextItem = pTimerItemListHead;
            pNextItem != NULL;) {
            pThisItem = pNextItem;
            pNextItem = pThisItem->pNext;
            FREEMEM (pThisItem);
        }
    }
    else {
        TRACE((WINPERF_DBG_TRACE_WARNING),
              (& PerflibGuid,
               __LINE__,
               PERF_DESTROYFUNCTIONTIMER,
               0,
               Status,
               NULL));
    }
    // all items have been freed so clear header
    pTimerItemListHead = NULL;

    // set exit event
    if (hTimerHandles[PL_TIMER_EXIT_EVENT] != NULL) {
        SetEvent (hTimerHandles[PL_TIMER_EXIT_EVENT]);
    }

    if (hPerflibTimingThread != NULL) {
        // wait for thread to terminate
        liWaitTime.QuadPart =
            MakeTimeOutValue((PERFLIB_TIMER_INTERVAL * 5));

        Status = NtWaitForSingleObject (
            hPerflibTimingThread,
            FALSE,
            &liWaitTime);

        assert (Status != STATUS_TIMEOUT);

        hTemp = hPerflibTimingThread;
	    hPerflibTimingThread = NULL;
    	CloseHandle (hTemp);
    }

    if (hTimerDataMutex != NULL) {
        hTemp = hTimerDataMutex;
        hTimerDataMutex = NULL;
        // close handles and leave
    	ReleaseMutex (hTemp);
        CloseHandle (hTemp);
    }

    if (hTimerHandles[PL_TIMER_START_EVENT] != NULL) {
        CloseHandle (hTimerHandles[PL_TIMER_START_EVENT]);
        hTimerHandles[PL_TIMER_START_EVENT] = NULL;
    }

    if (hTimerHandles[PL_TIMER_EXIT_EVENT] != NULL) {
        CloseHandle (hTimerHandles[PL_TIMER_EXIT_EVENT]);
        hTimerHandles[PL_TIMER_EXIT_EVENT] = NULL;
    }

    return ERROR_SUCCESS;
}

LONG
PrivateRegQueryValueExT (
    HKEY    hKey,
    LPVOID  lpValueName,
    LPDWORD lpReserved,
    LPDWORD lpType,
    LPBYTE  lpData,
    LPDWORD lpcbData,
    BOOL    bUnicode
)
/*
    wrapper function to allow RegQueryValues while inside a RegQueryValue

*/
{
    LONG    ReturnStatus;
    NTSTATUS    ntStatus = STATUS_SUCCESS;
	BOOL	bStatus;

    UNICODE_STRING      usLocal = {0,0,NULL};
    PSTR                AnsiValueBuffer;
    ULONG               AnsiValueLength;
    PWSTR               UnicodeValueBuffer;
    ULONG               UnicodeValueLength;
    ULONG               Index;

    PKEY_VALUE_PARTIAL_INFORMATION  pValueInformation;
    LONG                    ValueBufferLength;
    ULONG                   ResultLength;


    UNREFERENCED_PARAMETER (lpReserved);

    if (hKey == NULL || hKey == INVALID_HANDLE_VALUE) return ERROR_INVALID_HANDLE;

    if (bUnicode) {
        bStatus = RtlCreateUnicodeString (&usLocal, (LPCWSTR)lpValueName);
    } else {
        bStatus = RtlCreateUnicodeStringFromAsciiz (&usLocal, (LPCSTR)lpValueName);
    }

    if (bStatus) {

        ValueBufferLength =
		ResultLength =
			sizeof(KEY_VALUE_PARTIAL_INFORMATION) + *lpcbData;
        pValueInformation = ALLOCMEM(ResultLength);

        if (pValueInformation != NULL) {
            ntStatus = NtQueryValueKey(
                hKey,
                &usLocal,
                KeyValuePartialInformation,
                pValueInformation,
                ValueBufferLength,
                &ResultLength);

            if ((NT_SUCCESS(ntStatus) || ntStatus == STATUS_BUFFER_OVERFLOW)) {
                // return data
                if (ARGUMENT_PRESENT(lpType)) {
                    *lpType = pValueInformation->Type;
                }

                if (ARGUMENT_PRESENT(lpcbData)) {
                    *lpcbData = pValueInformation->DataLength;
                }

                if (NT_SUCCESS(ntStatus)) {
                    if (ARGUMENT_PRESENT(lpData)) {
                        if (!bUnicode &&
                            (pValueInformation->Type == REG_SZ ||
                            pValueInformation->Type == REG_EXPAND_SZ ||
                            pValueInformation->Type == REG_MULTI_SZ)
                        ) {
                            // then convert the unicode return to an
                            // ANSI string before returning
                            // the local wide buffer used

                            UnicodeValueLength  = ResultLength;
                            UnicodeValueBuffer  = (LPWSTR)&pValueInformation->Data[0];

                            AnsiValueBuffer = (LPSTR)lpData;
                            AnsiValueLength = ARGUMENT_PRESENT( lpcbData )?
                                                     *lpcbData : 0;
                            Index = 0;
                            ntStatus = RtlUnicodeToMultiByteN(
                                AnsiValueBuffer,
                                AnsiValueLength,
                                &Index,
                                UnicodeValueBuffer,
                                UnicodeValueLength);

                            if (NT_SUCCESS( ntStatus ) &&
                                (ARGUMENT_PRESENT( lpcbData ))) {
                                *lpcbData = Index;
                            }
                        } else {
                            if (pValueInformation->DataLength <= *lpcbData) {
                                // copy the buffer to the user's buffer
                                memcpy (lpData, &pValueInformation->Data[0],
                                    pValueInformation->DataLength);
                                ntStatus = STATUS_SUCCESS;
                             } else {
                                 ntStatus = STATUS_BUFFER_OVERFLOW;
                             }
                             *lpcbData = pValueInformation->DataLength;
                        }
                    }
                }
            }

            if (pValueInformation != NULL) {
                // release temp buffer
                FREEMEM (pValueInformation);
            }
        } else {
            // unable to allocate memory for this operation so
            ntStatus = STATUS_NO_MEMORY;
        }

        RtlFreeUnicodeString (&usLocal);
    } else {
		// this is a guess at the most likely cause for the string
		// creation to fail.
		ntStatus = STATUS_NO_MEMORY;
	}

    ReturnStatus = PerfpDosError(ntStatus);

    return ReturnStatus;
}

LONG
GetPerfDllFileInfo (
    LPCWSTR             szFileName,
    PDLL_VALIDATION_DATA  pDllData
)
{
    WCHAR   szFullPath[MAX_PATH*2];
    DWORD   dwStatus = ERROR_FILE_NOT_FOUND;
    DWORD   dwRetValue;
    HANDLE  hFile;
    BOOL    bStatus;
    LARGE_INTEGER   liSize;

    szFullPath[0] = UNICODE_NULL;
    dwRetValue = SearchPathW (
        NULL,
        szFileName,
        NULL,
        sizeof(szFullPath) / sizeof(szFullPath[0]),
        szFullPath,
        NULL);

    if (dwRetValue > 0) {
        //then the file was found so open it.
        hFile = CreateFileW (
            szFullPath,
            GENERIC_READ,
            FILE_SHARE_READ,
            NULL, 
            OPEN_EXISTING,
            FILE_ATTRIBUTE_NORMAL,
            NULL);

        if (hFile != INVALID_HANDLE_VALUE) {
            // get file creation date/time
            bStatus = GetFileTime (
                hFile,
                &pDllData->CreationDate,
                NULL, NULL);
            if (bStatus)  {
                // get file size
                liSize.LowPart  =  GetFileSize (
                    hFile, (PULONG)&liSize.HighPart);
                if (liSize.LowPart != 0xFFFFFFFF) {
                    pDllData->FileSize = liSize.QuadPart;
                    dwStatus = ERROR_SUCCESS;
                } else {
                    dwStatus = GetLastError();
                }
            } else {
                dwStatus = GetLastError();
            } 

            CloseHandle (hFile);
        } else {
            dwStatus = GetLastError();
        }
    } else {
        dwStatus = GetLastError();
    }

    return dwStatus;
}

DWORD
DisablePerfLibrary (
    PEXT_OBJECT  pObj,
    DWORD        dwValue
)
{
    // continue only if the "Disable" feature is enabled and
    // if this library hasn't already been disabled.
    if ((!(lPerflibConfigFlags & PLCF_NO_DISABLE_DLLS)) &&
        (!(pObj->dwFlags & PERF_EO_DISABLED))) {

        // set the disabled bit in the info
        pObj->dwFlags |= PERF_EO_DISABLED;
        return DisableLibrary(pObj->hPerfKey, pObj->szServiceName, dwValue);
    }
    return ERROR_SUCCESS;
}


DWORD
DisableLibrary(
    IN HKEY hPerfKey,
    IN LPWSTR szServiceName,
    IN DWORD  dwValue
)
{
    //
    // This routine will disable regardless of settings
    //
    DWORD   dwLocalValue, dwSize;
    DWORD   dwFnStatus = ERROR_SUCCESS;
    WORD    wStringIndex = 0;
    LPWSTR  szMessageArray[2];

    dwLocalValue = dwValue;
    if (dwLocalValue != 1 && dwLocalValue != 2 && dwLocalValue != 4) dwLocalValue = 1;
    // disable perf library entry in the service key
    dwSize = sizeof(dwLocalValue);
    dwFnStatus = RegSetValueExW (
            hPerfKey,
            DisablePerformanceCounters,
            0L,
            REG_DWORD,
            (LPBYTE) & dwLocalValue,
            dwSize);
        // report error

    if (dwFnStatus == ERROR_SUCCESS) {
        // system disabled
        szMessageArray[wStringIndex++] =
            szServiceName;

        ReportEvent (hEventLog,
            EVENTLOG_ERROR_TYPE,        // error type
            0,                          // category (not used)
            (DWORD)PERFLIB_LIBRARY_DISABLED,              // event,
            NULL,                       // SID (not used),
            wStringIndex,               // number of strings
            0,                          // sizeof raw data
            szMessageArray,             // message text array
            NULL);                      // raw data
    } else {
        // local disable only
        szMessageArray[wStringIndex++] =
            szServiceName;

        ReportEvent (hEventLog,
            EVENTLOG_ERROR_TYPE,        // error type
            0,                          // category (not used)
            (DWORD)PERFLIB_LIBRARY_TEMP_DISABLED,              // event,
            NULL,                       // SID (not used),
            wStringIndex,               // number of strings
            0,                          // sizeof raw data
            szMessageArray,             // message text array
            NULL);                      // raw data
    }
    return ERROR_SUCCESS;
}

/*
DWORD
PerfCheckRegistry(
    IN HKEY hPerfKey,
    IN LPCWSTR szServiceName
    )
{
    DWORD dwType = 0;
    DWORD dwSize = sizeof(DWORD);
    DWORD dwData = 0;
    DWORD status;
    WORD  wStringIndex;
    LPWSTR szMessageArray[2];

    status = PrivateRegQueryValueExA(
                hPerfKey,
                FirstCounter,
                NULL,
                &dwType,
                (LPBYTE)&dwData,
                &dwSize);

    if ((status != ERROR_SUCCESS) || (dwType != REG_DWORD) ||
        (dwData < LAST_BASE_INDEX)) {
        wStringIndex = 0;
        szMessageArray[wStringIndex++] = (LPWSTR) FirstCounter;
        szMessageArray[wStringIndex++] = (LPWSTR) szServiceName;
        ReportEvent(hEventLog,
            EVENTLOG_ERROR_TYPE,
            0,
            (DWORD)PERFLIB_REGVALUE_NOT_FOUND,
            NULL,
            wStringIndex,
            0,
            szMessageArray,
            NULL);
        return FALSE;
    }

    status = PrivateRegQueryValueExA(
                hPerfKey,
                LastCounter,
                NULL,
                &dwType,
                (LPBYTE)&dwData,
                &dwSize);

    if ((status != ERROR_SUCCESS) || (dwType != REG_DWORD) ||
        (dwData <= LAST_BASE_INDEX)) {
        wStringIndex = 0;
        szMessageArray[wStringIndex++] = (LPWSTR) LastCounter;
        szMessageArray[wStringIndex++] = (LPWSTR) szServiceName;
        ReportEvent(hEventLog,
            EVENTLOG_ERROR_TYPE,
            0,
            (DWORD)PERFLIB_REGVALUE_NOT_FOUND,
            NULL,
            wStringIndex,
            0,
            szMessageArray,
            NULL);
        return FALSE;
    }

    status = PrivateRegQueryValueExA(
                hPerfKey,
                FirstHelp,
                NULL,
                &dwType,
                (LPBYTE)&dwData,
                &dwSize);

    if ((status != ERROR_SUCCESS) || (dwType != REG_DWORD) ||
        (dwData < LAST_BASE_INDEX)) {
        wStringIndex = 0;
        szMessageArray[wStringIndex++] = (LPWSTR) FirstHelp;
        szMessageArray[wStringIndex++] = (LPWSTR) szServiceName;
        ReportEvent(hEventLog,
            EVENTLOG_ERROR_TYPE,
            0,
            (DWORD)PERFLIB_REGVALUE_NOT_FOUND,
            NULL,
            wStringIndex,
            0,
            szMessageArray,
            NULL);
        return FALSE;
    }

    status = PrivateRegQueryValueExA(
                hPerfKey,
                LastHelp,
                NULL,
                &dwType,
                (LPBYTE)&dwData,
                &dwSize);

    if ((status != ERROR_SUCCESS) || (dwType != REG_DWORD) ||
        (dwData <= LAST_BASE_INDEX)) {
        wStringIndex = 0;
        szMessageArray[wStringIndex++] = (LPWSTR) LastHelp;
        szMessageArray[wStringIndex++] = (LPWSTR) szServiceName;
        ReportEvent(hEventLog,
            EVENTLOG_ERROR_TYPE,
            0,
            (DWORD)PERFLIB_REGVALUE_NOT_FOUND,
            NULL,
            wStringIndex,
            0,
            szMessageArray,
            NULL);
        return FALSE;
    }

    return TRUE;
}
*/

DWORD
PerfpDosError(
    IN NTSTATUS Status
    )
// Need to convert NtStatus that we generate to DosError
{
    if (Status == STATUS_SUCCESS)
        return ERROR_SUCCESS;
    if (Status == STATUS_BUFFER_OVERFLOW)
        return ERROR_MORE_DATA;
    if (Status == STATUS_TIMEOUT)
        return WAIT_TIMEOUT;
    if (Status == STATUS_WAIT_63)
        return (DWORD) ERROR_SUCCESS;
    if ((Status >= STATUS_ABANDONED) && (Status <= STATUS_ABANDONED_WAIT_63))
        return (DWORD) ERROR_CANCELLED;
    return RtlNtStatusToDosError(Status);
}

PERROR_LOG
PerfpFindError(
    IN ULONG  ErrorNumber,
    IN PERROR_LOG ErrorLog
    )
{
    PLIST_ENTRY entry, head;
    PERROR_LOG pError;

    DebugPrint((3, "PERFLIB:FindError Entering critsec for %d\n", ErrorNumber));
    RtlEnterCriticalSection(&PerfpCritSect);
    head = (PLIST_ENTRY) ErrorLog;
    entry = head->Flink;
    while (entry != head) {
        pError = (PERROR_LOG) entry;
        DebugPrint((4, "PERFLIB:FindError Comparing entry %X/%X %d\n",
                    entry, ErrorLog, pError->ErrorNumber));
        if (pError->ErrorNumber == ErrorNumber)
            break;
        entry = entry->Flink;
    }
    if (entry == head) {
        pError = ALLOCMEM(sizeof(ERROR_LOG));
        if (pError == NULL) {
        DebugPrint((3, "PERFLIB:FindError Leaving critsec1\n"));
            RtlLeaveCriticalSection(&PerfpCritSect);
            return NULL;
        }
        pError->ErrorNumber = ErrorNumber;
        pError->ErrorCount = 0;
        pError->LastTime = 0;
        entry = (PLIST_ENTRY) pError;
        DebugPrint((3, "PERFLIB:FindError Added entry %X to %X\n", entry, ErrorLog));
        InsertHeadList(head, entry);
    }
    else {
        RemoveEntryList(entry);
        InsertHeadList(head, entry);
        pError = (PERROR_LOG) entry;
        DebugPrint((3, "PERFLIB:FindError Found entry %X in %X\n", entry, ErrorLog));
    }
    DebugPrint((3, "PERFLIB:FindError Leaving critsec\n"));
    RtlLeaveCriticalSection(&PerfpCritSect);
    return (PERROR_LOG) entry;
}

ULONG
PerfpCheckErrorTime(
    IN PERROR_LOG   ErrorLog,
    IN LONG64       TimeLimit,
    IN HKEY         hKey
    )
{
    WCHAR wstr[32];
    LONG64 timeStamp;
    DWORD status, dwType, dwSize;
    ULONG bUpdate = FALSE;
    UNICODE_STRING uString;

    GetSystemTimeAsFileTime((PFILETIME) &timeStamp);
    DebugPrint((3, "PERFLIB: CheckErrorTime Error %d time %I64d last %I64d limit %I64d\n",
             ErrorLog->ErrorNumber, timeStamp, ErrorLog->LastTime, TimeLimit));

    if ((timeStamp - ErrorLog->LastTime) < TimeLimit)
        return FALSE;

    wstr[0] = UNICODE_NULL;
    if (hKey != NULL) {
        uString.Buffer = &wstr[0];
        uString.Length = 0;
        uString.MaximumLength = sizeof(wstr);

        RtlIntegerToUnicodeString(ErrorLog->ErrorNumber, 10, &uString);
        DebugPrint((3, "Err %d string %ws\n", ErrorLog->ErrorNumber, uString.Buffer));
        if (ErrorLog->LastTime == 0) {
            dwType = REG_DWORD;
            dwSize = sizeof(timeStamp);
            status = PrivateRegQueryValueExW(
                        hKey,
                        wstr, // Need to be string
                        NULL,
                        &dwType,
                        (LPBYTE) &ErrorLog->LastTime,
                        &dwSize
                        );
            if ((status == ERROR_SUCCESS) &&
                (dwType == REG_QWORD)) {
                if ((timeStamp - ErrorLog->LastTime) >= TimeLimit) {
                    ErrorLog->LastTime = 0;
                }
            }
        }
        if (ErrorLog->LastTime == 0) {
            bUpdate = TRUE;
            ErrorLog->LastTime = timeStamp;
            dwSize = sizeof(timeStamp);
            status = RegSetValueExW(
                        hKey,
                        wstr,
                        0L,
                        REG_QWORD,
                        (LPBYTE) &timeStamp,
                        dwSize);
        }
    }
    if ((timeStamp - ErrorLog->LastTime) >= TimeLimit) {
        // Get here if we no write access to regkey
        ErrorLog->LastTime = timeStamp;
        bUpdate = TRUE;
    }
    return bUpdate;
}

VOID
PerfpDeleteErrorLogs(
    IN PERROR_LOG ErrorLog
    )
{
    PLIST_ENTRY entry, head, pError;

    head = (PLIST_ENTRY) ErrorLog;
    entry = head->Flink;
    DebugPrint((3, "PERFLIB:DeleteErrorLogs Entering Critsec %x\n", ErrorLog));
    RtlEnterCriticalSection(&PerfpCritSect);
    while (entry != head) {
        pError = entry;
        entry = entry->Flink;
        DebugPrint((3, "PERFLIB: Deleting error log entry %d/%x from %x\n",
                 ((PERROR_LOG) pError)->ErrorNumber, pError, ErrorLog));
        FREEMEM(pError);        // because we put ListEntry first
    }
    InitializeListHead(head);
    RtlLeaveCriticalSection(&PerfpCritSect);
    DebugPrint((3, "PERFLIB:DeleteErrorLogs Leaving Critsec\n"));
}

ULONG
PerfpThrottleError(
    IN DWORD ErrorNumber,
    IN HKEY hKey,
    IN PERROR_LOG ErrorLog
    )
{
    PERROR_LOG pError;
    ULONG bReportError, status;
    LONG64 TimeLimit = 3600 * 24;
    LONG64 SavedTime = 0;

    if (lEventLogLevel > LOG_USER) { // only throttle for <= LOG_USER
        return TRUE;
    }
    if (lEventLogLevel < LOG_USER) { // don't report error
        return FALSE;
    }
    ErrorNumber = ErrorNumber & 0x00FFFFFF;

    pError = PerfpFindError(ErrorNumber, ErrorLog);

    if (pError == NULL)
        return FALSE;       // don't report error if no more resources
    TimeLimit *= 10000000;

    SavedTime = pError->LastTime;
    bReportError = PerfpCheckErrorTime(pError, TimeLimit, hKey);
    DebugPrint((3, "PERFLIB:PerfpThrottleError ReportError %d hKey %X\n",
                    bReportError, hKey));

    if ((hKey == NULL) && (bReportError)) {
        status = RegOpenKeyExW(
                   HKEY_LOCAL_MACHINE,
                   HKLMPerflibKey,
                   0L,
                   KEY_READ | KEY_WRITE,
                   &hKey);
        if (status != ERROR_SUCCESS) {  // try read access anyway
            status = RegOpenKeyExW(
                        HKEY_LOCAL_MACHINE,
                        HKLMPerflibKey,
                        0L,
                        KEY_READ,
                        &hKey);
        }
        DebugPrint((3, "PERFLIB:PerfpThrottleError Perflib open status %d\n", status));
        if (status == ERROR_SUCCESS) {
            //
            // if this is the first time we see this error, reset the time
            // and see if a previous time was saved in registry
            //
            pError->LastTime = SavedTime;
            bReportError = PerfpCheckErrorTime(pError, TimeLimit, hKey);
            DebugPrint((3, "PERFLIB:PerfpThrottleError ReportError2 %d\n", bReportError));
            RegCloseKey(hKey);
        }
    }
    return bReportError;
}

#ifdef DBG
VOID
PerfpDebug(
    ULONG DebugPrintLevel,
    PCCHAR DebugMessage,
    ...
    )

/*++

Routine Description:

    Debug print for all Perflib

Arguments:

    Debug print level between 0 and 3, with 3 being the most verbose.

Return Value:

    None

--*/

{
    va_list ap;

    if ((DebugPrintLevel <= (PerfLibDebug & 0x0000ffff)) ||
        ((1 << (DebugPrintLevel + 15)) & PerfLibDebug)) {
        DbgPrint("%d:Perflib:", GetCurrentThreadId());
    }
    else
        return;

    va_start(ap, DebugMessage);


    if ((DebugPrintLevel <= (PerfLibDebug & 0x0000ffff)) ||
        ((1 << (DebugPrintLevel + 15)) & PerfLibDebug)) {

        _vsnprintf(
            (LPSTR)PerfLibDebugBuffer, DEBUG_BUFFER_LENGTH, DebugMessage, ap);

        DbgPrint((LPSTR)PerfLibDebugBuffer);
    }

    va_end(ap);

}
#endif // DBG
