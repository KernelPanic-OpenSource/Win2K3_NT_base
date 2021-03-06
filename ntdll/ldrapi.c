/*++

Copyright (c) 1989  Microsoft Corporation

Module Name:

    ldrapi.c

Abstract:

    This module implements the Ldr APIs that can be linked with
    an application to perform loader services. All of the APIs in
    this component are implemented in a DLL. They are not part of the
    DLL snap procedure.

Author:

    Mike O'Leary (mikeol) 23-Mar-1990

Revision History:

--*/

#include "ldrp.h"
#include "ntos.h"
#include "nt.h"
#include "ntrtl.h"
#include "nturtl.h"
#include "objidl.h"
#include <windows.h>
#include <apcompat.h>
#include <shimhapi.h>

#if defined(_WIN64)
#include <wow64t.h>
#endif // defined(_WIN64)

#define ULONG_PTR_IZE(_x) ((ULONG_PTR) (_x))
#define ULONG_PTR_IZE_SHIFT_AND_MASK(_x, _shift, _mask) ((ULONG_PTR) ((ULONG_PTR_IZE((_x)) & (_mask)) << (_shift)))

#define CHAR_BITS 8

#define LOADER_LOCK_COOKIE_TYPE_BIT_LENGTH (4)
#define LOADER_LOCK_COOKIE_TYPE_BIT_OFFSET ((CHAR_BITS * sizeof(PVOID)) - LOADER_LOCK_COOKIE_TYPE_BIT_LENGTH)
#define LOADER_LOCK_COOKIE_TYPE_BIT_MASK ((1 << LOADER_LOCK_COOKIE_TYPE_BIT_LENGTH) - 1)

#define LOADER_LOCK_COOKIE_TID_BIT_LENGTH (12)
#define LOADER_LOCK_COOKIE_TID_BIT_OFFSET (LOADER_LOCK_COOKIE_TYPE_BIT_OFFSET - LOADER_LOCK_COOKIE_TID_BIT_LENGTH)
#define LOADER_LOCK_COOKIE_TID_BIT_MASK ((1 << LOADER_LOCK_COOKIE_TID_BIT_LENGTH) - 1)

#define LOADER_LOCK_COOKIE_CODE_BIT_LENGTH (16)
#define LOADER_LOCK_COOKIE_CODE_BIT_OFFSET (0)
#define LOADER_LOCK_COOKIE_CODE_BIT_MASK ((1 << LOADER_LOCK_COOKIE_CODE_BIT_LENGTH) - 1)

#define MAKE_LOADER_LOCK_COOKIE(_type, _code) \
    ((ULONG_PTR) (ULONG_PTR_IZE_SHIFT_AND_MASK((_type), LOADER_LOCK_COOKIE_TYPE_BIT_OFFSET, LOADER_LOCK_COOKIE_TYPE_BIT_MASK) | \
                  ULONG_PTR_IZE_SHIFT_AND_MASK((HandleToUlong((NtCurrentTeb())->ClientId.UniqueThread)), LOADER_LOCK_COOKIE_TID_BIT_OFFSET, LOADER_LOCK_COOKIE_TID_BIT_MASK) | \
                  ULONG_PTR_IZE_SHIFT_AND_MASK((_code), LOADER_LOCK_COOKIE_CODE_BIT_OFFSET, LOADER_LOCK_COOKIE_CODE_BIT_MASK)))

#define EXTRACT_LOADER_LOCK_COOKIE_FIELD(_cookie, _shift, _mask) ((((ULONG_PTR) (_cookie)) >> (_shift)) & (_mask))
#define EXTRACT_LOADER_LOCK_COOKIE_TYPE(_cookie) EXTRACT_LOADER_LOCK_COOKIE_FIELD((_cookie), LOADER_LOCK_COOKIE_TYPE_BIT_OFFSET, LOADER_LOCK_COOKIE_TYPE_BIT_MASK)
#define EXTRACT_LOADER_LOCK_COOKIE_TID(_cookie) EXTRACT_LOADER_LOCK_COOKIE_FIELD((_cookie), LOADER_LOCK_COOKIE_TID_BIT_OFFSET, LOADER_LOCK_COOKIE_TID_BIT_MASK)

#define LOADER_LOCK_COOKIE_TYPE_NORMAL (0)

LONG LdrpLoaderLockAcquisitionCount;

// Note the case inconsistency is due to preserving case from earlier versions.
WCHAR DllExtension[] = L".dll";
UNICODE_STRING LdrApiDefaultExtension = RTL_CONSTANT_STRING(L".DLL");

PLDR_MANIFEST_PROBER_ROUTINE LdrpManifestProberRoutine = NULL;

extern PFNSE_DLLLOADED         g_pfnSE_DllLoaded;
extern PFNSE_DLLUNLOADED       g_pfnSE_DllUnloaded;

PLDR_APP_COMPAT_DLL_REDIRECTION_CALLBACK_FUNCTION LdrpAppCompatDllRedirectionCallbackFunction = NULL;
PVOID LdrpAppCompatDllRedirectionCallbackData = NULL;
BOOLEAN LdrpShowRecursiveDllLoads;
BOOLEAN LdrpBreakOnRecursiveDllLoads;
PLDR_DATA_TABLE_ENTRY LdrpCurrentDllInitializer;

VOID
RtlpDphDisableFaultInjection (
    );

VOID
RtlpDphEnableFaultInjection (
    );

ULONG
LdrpClearLoadInProgress(
    VOID
    );


NTSTATUS
LdrLoadDll (
    IN PCWSTR DllPath OPTIONAL,
    IN PULONG DllCharacteristics OPTIONAL,
    IN PCUNICODE_STRING DllName,
    OUT PVOID *DllHandle
    )

/*++

Routine Description:

    This function loads a DLL into the calling process address space.

Arguments:

    DllPath - Supplies the search path to be used to locate the DLL.

    DllCharacteristics - Supplies an optional DLL characteristics flag,
        that if specified is used to match against the dll being loaded.

    DllName - Supplies the name of the DLL to load.

    DllHandle - Returns a handle to the loaded DLL.

Return Value:

    NTSTATUS.

--*/
{
    NTSTATUS Status;
    WCHAR StaticRedirectedDllNameBuffer[DOS_MAX_PATH_LENGTH];
    UNICODE_STRING StaticRedirectedDllName;
    UNICODE_STRING DynamicRedirectedDllName = {0};
    ULONG LoadDllFlags = 0;
    PCUNICODE_STRING OldTopLevelDllBeingLoaded = NULL;
    PVOID LockCookie = NULL;
    PTEB Teb;

    //
    // We need to disable page heap fault injection while loader is active.
    // This is important so that we avoid lots of hits(failures) in this
    // area. The Disable/Enable function have basically zero impact on
    // performance because they just increment/decrement a lock variable
    // that is checked when an actual allocation is performed (page heap
    // needs to be enabled for that).
    //

    RtlpDphDisableFaultInjection ();

    StaticRedirectedDllName.Length = 0;
    StaticRedirectedDllName.MaximumLength = sizeof(StaticRedirectedDllNameBuffer);
    StaticRedirectedDllName.Buffer = StaticRedirectedDllNameBuffer;

    Status = RtlDosApplyFileIsolationRedirection_Ustr(
                RTL_DOS_APPLY_FILE_REDIRECTION_USTR_FLAG_RESPECT_DOT_LOCAL,
                DllName,                    // dll name to look up
                &LdrApiDefaultExtension,
                &StaticRedirectedDllName,
                &DynamicRedirectedDllName,
                (PUNICODE_STRING*)&DllName, // Result is either StaticRedirectedDllName or DynamicRedirectedDllName
                NULL,
                NULL,                       // not interested in where the filename starts
                NULL);                      // not interested in bytes required if we only had a static string
    if (NT_SUCCESS(Status)) {
        LoadDllFlags |= LDRP_LOAD_DLL_FLAG_DLL_IS_REDIRECTED;
    } else if (Status != STATUS_SXS_KEY_NOT_FOUND) {
#if DBG
        DbgPrint("%s(%wZ): RtlDosApplyFileIsolationRedirection_Ustr() failed with status %08lx\n", __FUNCTION__, DllName, Status);
#endif // DBG
        goto Exit;
    }

    LdrLockLoaderLock(LDR_LOCK_LOADER_LOCK_FLAG_RAISE_ON_ERRORS, NULL, &LockCookie);
    OldTopLevelDllBeingLoaded = LdrpTopLevelDllBeingLoaded;

    if (OldTopLevelDllBeingLoaded) {
        if (ShowSnaps || LdrpShowRecursiveDllLoads || LdrpBreakOnRecursiveDllLoads) {
            Teb = NtCurrentTeb();

            DbgPrint(
                "[%lx,%lx] LDR: Recursive DLL load\n",
                HandleToULong(Teb->ClientId.UniqueProcess),
                HandleToULong(Teb->ClientId.UniqueThread));

            DbgPrint(
                "[%lx,%lx]   Previous DLL being loaded: \"%wZ\"\n",
                HandleToULong(Teb->ClientId.UniqueProcess),
                HandleToULong(Teb->ClientId.UniqueThread),
                OldTopLevelDllBeingLoaded);

            DbgPrint(
                "[%lx,%lx]   DLL being requested: \"%wZ\"\n",
                HandleToULong(Teb->ClientId.UniqueProcess),
                HandleToULong(Teb->ClientId.UniqueThread),
                DllName);

            if (LdrpCurrentDllInitializer != NULL) {
                DbgPrint(
                    "[%lx,%lx]   DLL whose initializer was currently running: \"%wZ\"\n",
                    HandleToULong(Teb->ClientId.UniqueProcess),
                    HandleToULong(Teb->ClientId.UniqueThread),
                    &LdrpCurrentDllInitializer->FullDllName);
            } else {
                DbgPrint(
                    "[%lx,%lx]   No DLL initializer was running\n",
                    HandleToULong(Teb->ClientId.UniqueProcess),
                    HandleToULong(Teb->ClientId.UniqueThread));
            }
        }
    }

    LdrpTopLevelDllBeingLoaded = DllName;

    __try {

        Status = LdrpLoadDll (LoadDllFlags,
                              DllPath,
                              DllCharacteristics,
                              DllName,
                              DllHandle,
                              TRUE);

        if (!NT_SUCCESS(Status)) {
            if ((Status != STATUS_NO_SUCH_FILE) &&
                (Status != STATUS_DLL_NOT_FOUND) &&
                (Status != STATUS_OBJECT_NAME_NOT_FOUND)) {

                // Dll initialization failure is common enough that we won't want to print unless snaps are turned on.
                if (ShowSnaps || (Status != STATUS_DLL_INIT_FAILED)) {
                    DbgPrintEx(
                        DPFLTR_LDR_ID,
                        LDR_ERROR_DPFLTR,
                        "LDR: %s - failing because LdrpLoadDll(%wZ) returned status %x\n",
                        __FUNCTION__,
                        DllName,
                        Status);
                }
            }

            __leave;
        }
        Status = STATUS_SUCCESS;
    } __finally {
        LdrpTopLevelDllBeingLoaded = OldTopLevelDllBeingLoaded;
        LdrUnlockLoaderLock(LDR_UNLOCK_LOADER_LOCK_FLAG_RAISE_ON_ERRORS, LockCookie);
    }

Exit:
    if (DynamicRedirectedDllName.Buffer != NULL) {
        RtlFreeUnicodeString(&DynamicRedirectedDllName);
    }

    //
    // Reenable page heap fault injection.
    //

    RtlpDphEnableFaultInjection ();

    return Status;
}


NTSTATUS
NTAPI
LdrpLoadDll (
    IN ULONG Flags OPTIONAL,
    IN PCWSTR DllPath OPTIONAL,
    IN PULONG DllCharacteristics OPTIONAL,
    IN PCUNICODE_STRING DllName,
    OUT PVOID *DllHandle,
    IN BOOLEAN RunInitRoutines
    )
{
    NTSTATUS st;
    PLDR_DATA_TABLE_ENTRY LdrDataTableEntry;
    PWSTR ActualDllName;
    PWCH p, pp;
    UNICODE_STRING ActualDllNameStr;
    WCHAR FreeBuffer[LDR_MAX_PATH + 1];
    BOOLEAN Redirected;
    ULONG DllNameLength;
    const InLdrInit = LdrpInLdrInit;

    if (Flags & LDRP_LOAD_DLL_FLAG_DLL_IS_REDIRECTED) {
        Redirected = TRUE;
    }
    else {
        Redirected = FALSE;
    }

    st = STATUS_SUCCESS;

    p = DllName->Buffer;
    pp = NULL;

    while (*p) {

        switch (*p++) {
        case L'.':
            //
            // pp will point to first character after the last '.', if
            // it occurs after the last '\'.
            //

            pp = p;
            break;
            
        case L'\\':

            pp = NULL;
            break;

        default:
            NOTHING;
        }
    }

    if (DllName->Length >= sizeof(FreeBuffer)) {
        return STATUS_NAME_TOO_LONG;
    }

    ActualDllName = FreeBuffer;

    RtlCopyMemory (ActualDllName, DllName->Buffer, DllName->Length);

    if (!pp || *pp == (WCHAR)'\\') {

        //
        // No extension found (just ..\)
        //

        DllNameLength = DllName->Length + sizeof(DllExtension) - sizeof(WCHAR);
        if ((DllNameLength + sizeof(WCHAR)) >= sizeof(FreeBuffer)) {
            DbgPrintEx(
                DPFLTR_LDR_ID,
                LDR_ERROR_DPFLTR,
                "LDR: %s - Dll name missing extension; with extension added the length is too long\n"
                "   DllName: (@ %p) \"%wZ\"\n"
                "   DllName->Length: %u\n",
                __FUNCTION__,
                DllName, DllName,
                DllName->Length);

            return STATUS_NAME_TOO_LONG;
        }

        RtlCopyMemory ((PCHAR)ActualDllName+DllName->Length, DllExtension, sizeof(DllExtension));
        ActualDllNameStr.Length = (USHORT)(DllNameLength);
    } else {
        ActualDllName[DllName->Length >> 1] = UNICODE_NULL;
        ActualDllNameStr.Length = DllName->Length;
    }

    ActualDllNameStr.MaximumLength = sizeof(FreeBuffer);
    ActualDllNameStr.Buffer = ActualDllName;
    LdrDataTableEntry = NULL;

    //
    // Except during process initialization, grab loader lock and
    // Snap all links to the specified DLL.
    //

    if (!InLdrInit) {
        RtlEnterCriticalSection (&LdrpLoaderLock);
    }

    try {

        if (ShowSnaps) {
            DbgPrint("LDR: LdrLoadDll, loading %ws from %ws\n",
                ActualDllName,
                ARGUMENT_PRESENT(DllPath) ? DllPath : L""
                );
        }

        if (!LdrpCheckForLoadedDll( DllPath,
                                    &ActualDllNameStr,
                                    FALSE,
                                    Redirected,
                                    &LdrDataTableEntry)) {

            st = LdrpMapDll(DllPath,
                            ActualDllName,
                            DllCharacteristics,
                            FALSE,
                            Redirected,
                            &LdrDataTableEntry);

            if (!NT_SUCCESS(st)) {
                leave;
            }

#if defined(_X86_)

            //
            // Register dll with the stack tracing module.
            // This is used for getting reliable stack traces on X86.
            //

            RtlpStkMarkDllRange (LdrDataTableEntry);
#endif

            if (ARGUMENT_PRESENT( DllCharacteristics ) &&
                *DllCharacteristics & IMAGE_FILE_EXECUTABLE_IMAGE) {

                LdrDataTableEntry->EntryPoint = 0;
                LdrDataTableEntry->Flags &= ~LDRP_IMAGE_DLL;
            }

            //
            // walk the import descriptor table of the dll
            //

            if (LdrDataTableEntry->Flags & LDRP_IMAGE_DLL) {

                try {
                    
                    //
                    // if the image is COR-ILONLY, then don't walk the import descriptor 
                    // as it is assumed that it only imports %windir%\system32\mscoree.dll, otherwise
                    // walk the import descriptor table of the dll.
                    //

                    if ((LdrDataTableEntry->Flags & LDRP_COR_IMAGE) == 0) {
                        st = LdrpWalkImportDescriptor(
                                  DllPath,
                                  LdrDataTableEntry
                                  );
                    }
                } __except(LdrpGenericExceptionFilter(GetExceptionInformation(), __FUNCTION__)) {
                    st = GetExceptionCode();
                    DbgPrintEx(
                        DPFLTR_LDR_ID,
                        LDR_ERROR_DPFLTR,
                        "LDR: %s - Exception %x thrown by LdrpWalkImportDescriptor\n",
                        __FUNCTION__,
                        st);
                }

                if ( LdrDataTableEntry->LoadCount != 0xffff ) {
                    LdrDataTableEntry->LoadCount += 1;
                }

                LdrpReferenceLoadedDll (LdrDataTableEntry);

                if (!NT_SUCCESS(st)) {
                    LdrDataTableEntry->EntryPoint = NULL;
                    InsertTailList(
                        &PebLdr.InInitializationOrderModuleList,
                        &LdrDataTableEntry->InInitializationOrderLinks);

                    LdrpClearLoadInProgress();

                    if (ShowSnaps) {
                        DbgPrint("LDR: Unloading %wZ due to error %x walking import descriptors\n", DllName, st);
                    }

                    LdrUnloadDll((PVOID)LdrDataTableEntry->DllBase);
                    leave;
                }
            }
            else {
                if ( LdrDataTableEntry->LoadCount != 0xffff ) {
                    LdrDataTableEntry->LoadCount += 1;
                }
            }

            //
            // Add init routine to list
            //

            InsertTailList(&PebLdr.InInitializationOrderModuleList,
                           &LdrDataTableEntry->InInitializationOrderLinks);


            //
            // If the loader data base is not fully setup, this load was because
            // of a forwarder in the static load set. Can't run init routines
            // yet because the load counts are NOT set
            //

            if ( RunInitRoutines && LdrpLdrDatabaseIsSetup ) {

                //
                // Shim engine callback. This is the chance to patch
                // dynamically loaded modules.
                //

                if (g_pfnSE_DllLoaded != NULL) {
                    (*g_pfnSE_DllLoaded)(LdrDataTableEntry);
                }

                try {

                    st = LdrpRunInitializeRoutines (NULL);

                    if ( !NT_SUCCESS(st) ) {
                        if (ShowSnaps) {
                            DbgPrint("LDR: Unloading %wZ because either its init routine or one of its static imports failed; status = 0x%08lx", DllName, st);
                        }

                        LdrUnloadDll((PVOID)LdrDataTableEntry->DllBase);
                    }
                }
                __except (LdrpGenericExceptionFilter(GetExceptionInformation(), __FUNCTION__)) {
                    st = GetExceptionCode();

                    DbgPrintEx(
                        DPFLTR_LDR_ID,
                        LDR_ERROR_DPFLTR,
                        "LDR: %s - Exception %08lx thrown running initialization routines for %wZ\n",
                        __FUNCTION__,
                        st,
                        &LdrDataTableEntry->FullDllName);

                    LdrUnloadDll((PVOID)LdrDataTableEntry->DllBase);

                    leave;
                }
            }
            else {
                st = STATUS_SUCCESS;
            }
        }
        else {

            //
            // Count it and everything that it imports.
            //

            if ( LdrDataTableEntry->Flags & LDRP_IMAGE_DLL &&
                 LdrDataTableEntry->LoadCount != 0xffff  ) {

                LdrDataTableEntry->LoadCount += 1;

                LdrpReferenceLoadedDll(LdrDataTableEntry);

                //
                // Now clear the Load in progress bits
                //

                LdrpClearLoadInProgress();
            }
            else {
                if ( LdrDataTableEntry->LoadCount != 0xffff ) {
                    LdrDataTableEntry->LoadCount += 1;
                }
            }
        }
    }
    __finally {
        if (!InLdrInit) {
            RtlLeaveCriticalSection(&LdrpLoaderLock);
        }
    }

    if (NT_SUCCESS(st)) {
        *DllHandle = (PVOID)LdrDataTableEntry->DllBase;
    }
    else {
        *DllHandle = NULL;
    }

    return st;
}


NTSTATUS
LdrGetDllHandle(
    IN PCWSTR DllPath OPTIONAL,
    IN PULONG DllCharacteristics OPTIONAL,
    IN PCUNICODE_STRING DllName,
    OUT PVOID *DllHandle
    )
{
    //
    // Preserve the old behavior.
    //

    return LdrGetDllHandleEx (LDR_GET_DLL_HANDLE_EX_UNCHANGED_REFCOUNT,
                              DllPath,
                              DllCharacteristics,
                              DllName,
                              DllHandle);
}


NTSTATUS
LdrGetDllHandleEx(
    IN ULONG Flags,
    IN PCWSTR DllPath OPTIONAL,
    IN PULONG DllCharacteristics OPTIONAL,
    IN PCUNICODE_STRING ConstDllName,
    OUT PVOID *DllHandle OPTIONAL
    )

/*++

Routine Description:

    This function locates the specified DLL and returns its handle.

Arguments:

    Flags - various bits to affect the behavior

        default: the returned handle is addrefed

        LDR_GET_DLL_HANDLE_EX_PIN - the dll will not be unloaded until
                the process exits

        LDR_GET_DLL_HANDLE_EX_UNCHANGED_REFCOUNT - the dll's reference
                count is not changed

    DllPath - Supplies the search path to be used to locate the DLL.

    DllCharacteristics - Supplies an optional DLL characteristics flag,
        that if specified is used to match against the dll being loaded.
        The currently supported flags are:

            IMAGE_FILE_EXECUTABLE_IMAGE - indicates that imported dll
                referenced by the DLL being loaded should not be followed.
                This corresponds to DONT_RESOLVE_DLL_REFERENCES

            IMAGE_FILE_SYSTEM - indicates that the DLL is a known trusted
                system component and that WinSafer sandbox checking
                should not be performed on the DLL before loading it.

    DllName - Supplies the name of the DLL to load.

    DllHandle - Returns a handle to the loaded DLL.

Return Value:

    NTSTATUS.

--*/

{
    NTSTATUS st = STATUS_ACCESS_VIOLATION;
    PLDR_DATA_TABLE_ENTRY LdrDataTableEntry = NULL;
    PWCH p, pp, pEnd;
    UNICODE_STRING ActualDllNameStr = {0, 0, NULL};
    UNICODE_STRING DynamicRedirectedDllName = {0, 0, NULL};
    BOOLEAN Redirected = FALSE;
    BOOLEAN HoldingLoaderLock = FALSE;
    const BOOLEAN InLdrInit = LdrpInLdrInit;
    PVOID LockCookie = NULL;
    const ULONG ValidFlags = LDR_GET_DLL_HANDLE_EX_PIN | LDR_GET_DLL_HANDLE_EX_UNCHANGED_REFCOUNT;    
    UNICODE_STRING xDllName;
    const PUNICODE_STRING DllName = &xDllName;

    UNREFERENCED_PARAMETER (DllCharacteristics);

    xDllName = *ConstDllName;

    __try {

        if (DllHandle != NULL) {
            *DllHandle = NULL;
        }

        if (Flags & ~ValidFlags) {
            st = STATUS_INVALID_PARAMETER;
            goto Exit;
        }

        //
        // DllHandle is optional if you are pinning the .dll, otherwise it is mandatory.
        //
        if ((DllHandle == NULL) &&
            (Flags & LDR_GET_DLL_HANDLE_EX_PIN) == 0) {

            st = STATUS_INVALID_PARAMETER;
            goto Exit;
        }

        if ((Flags & LDR_GET_DLL_HANDLE_EX_PIN) &&
            (Flags & LDR_GET_DLL_HANDLE_EX_UNCHANGED_REFCOUNT)) {

            st = STATUS_INVALID_PARAMETER;
            goto Exit;
        }

        //
        // Grab Ldr lock
        //

        if (!InLdrInit) {
            st = LdrLockLoaderLock(0, NULL, &LockCookie);
            if (!NT_SUCCESS(st)) {
                goto Exit;
            }
            HoldingLoaderLock = TRUE;
        }

        st = RtlDosApplyFileIsolationRedirection_Ustr(
                    RTL_DOS_APPLY_FILE_REDIRECTION_USTR_FLAG_RESPECT_DOT_LOCAL,
                    DllName,
                    &LdrApiDefaultExtension,
                    NULL,
                    &DynamicRedirectedDllName,
                    (PUNICODE_STRING*)&DllName,
                    NULL,
                    NULL,
                    NULL);
        if (NT_SUCCESS(st)) {
            Redirected = TRUE;
        } else if (st != STATUS_SXS_KEY_NOT_FOUND) {
            // Something unusual and bad happened.
            __leave;
        }

        st = STATUS_DLL_NOT_FOUND;

        if ( LdrpGetModuleHandleCache ) {
            if (Redirected) {
                if (((LdrpGetModuleHandleCache->Flags & LDRP_REDIRECTED) != 0) &&
                    RtlEqualUnicodeString(DllName, &LdrpGetModuleHandleCache->FullDllName, TRUE)) {

                    LdrDataTableEntry = LdrpGetModuleHandleCache;
                    st = STATUS_SUCCESS;
                    goto Exit;
                }
            } else {
                // Not redirected...
                if (((LdrpGetModuleHandleCache->Flags & LDRP_REDIRECTED) == 0) &&
                    RtlEqualUnicodeString(DllName, &LdrpGetModuleHandleCache->BaseDllName, TRUE)) {

                    LdrDataTableEntry = LdrpGetModuleHandleCache;
                    st = STATUS_SUCCESS;
                    goto Exit;
                }
            }
        }

        p = DllName->Buffer;
        pEnd = p + (DllName->Length / sizeof(WCHAR));

        pp = NULL;

        while (p != pEnd) {
            switch (*p++) {
            case L'.':
                //
                // pp will point to the first character after the last 
                // '.', if it occurs after the last '\'.
                //

                pp = p;
                break;

            case L'\\':

                pp = NULL;
                break;

            default:
                NOTHING;
            }
        }

        if ((pp == NULL) || (*pp == L'\\') || (*pp == L'/')) {

            //
            // The max length here must include the null-termination, but the length itself
            // should not.  NB that sizeof(DllExtension) will include the size for the
            // terminating UNICODE_NULL
            //
            ActualDllNameStr.MaximumLength = DllName->Length + sizeof(DllExtension);
            ActualDllNameStr.Length = ActualDllNameStr.MaximumLength - sizeof(WCHAR);

            ActualDllNameStr.Buffer = RtlAllocateHeap(RtlProcessHeap(), 0, ActualDllNameStr.MaximumLength);
            if (ActualDllNameStr.Buffer == NULL) {
                st = STATUS_NO_MEMORY;
                goto Exit;
            }

            //
            // Copy the name and the default extension onto the string  This magically null-terminates,
            // as DllExtension includes the unicode null character.
            //
            RtlCopyMemory(ActualDllNameStr.Buffer, DllName->Buffer, DllName->Length);
            RtlCopyMemory(((PCHAR)ActualDllNameStr.Buffer) + DllName->Length, DllExtension, sizeof(DllExtension));

        } else {

            //
            // Trim the trailing dot
            //
            if ((DllName->Length != 0) && (DllName->Buffer[(DllName->Length / sizeof(WCHAR)) - 1] == L'.')) {
                DllName->Length -= sizeof(WCHAR);
            }

            //
            // Size the buffer, allocate - set the max length to include the NULL character
            //
            ActualDllNameStr.MaximumLength = DllName->Length + sizeof(WCHAR);
            ActualDllNameStr.Length = DllName->Length;
            ActualDllNameStr.Buffer = RtlAllocateHeap(RtlProcessHeap(), 0, ActualDllNameStr.MaximumLength);
            if (ActualDllNameStr.Buffer == NULL) {
                st = STATUS_NO_MEMORY;
                goto Exit;
            }


            //
            // Copy data into it
            //
            RtlCopyMemory(ActualDllNameStr.Buffer, DllName->Buffer, DllName->Length);  

            //
            // And null-terminate by hand
            //
            ActualDllNameStr.Buffer[ActualDllNameStr.Length / sizeof(WCHAR)] = UNICODE_NULL;

        }

        
        //
        // Check the LdrTable to see if Dll has already been loaded
        // into this image.
        //
        if (ShowSnaps) {
            DbgPrint(
                "LDR: LdrGetDllHandle, searching for %wZ from %ws\n",
                &ActualDllNameStr,
                ARGUMENT_PRESENT(DllPath) ? (DllPath == (PWSTR)1 ? L"" : DllPath) : L""
                );
        }

        //
        // sort of a hack, but done to speed up GetModuleHandle. kernel32
        // now does a two pass call here to avoid computing
        // process dll path
        //

        if (LdrpCheckForLoadedDll(DllPath,
                                  &ActualDllNameStr,
                                  (BOOLEAN)(DllPath == (PWSTR)1 ? TRUE : FALSE),
                                  Redirected,
                                  &LdrDataTableEntry)) {
            LdrpGetModuleHandleCache = LdrDataTableEntry;
            st = STATUS_SUCCESS;
            goto Exit;
        }
        LdrDataTableEntry = NULL;
        RTL_SOFT_ASSERT(st == STATUS_DLL_NOT_FOUND);
Exit:
        ASSERT((LdrDataTableEntry != NULL) == NT_SUCCESS(st));

        if (LdrDataTableEntry != NULL && NT_SUCCESS(st)) {

            //
            // It's standard gross procedure to put the check for 0xffff,
            // and the updates of the root LoadCount outside the
            // call to LdrpUpdateLoadCount..
            //

            if (LdrDataTableEntry->LoadCount != 0xffff) {

                if ((Flags & LDR_GET_DLL_HANDLE_EX_UNCHANGED_REFCOUNT) != 0) {
                    // nothing
                }
                else {
                    if (Flags & LDR_GET_DLL_HANDLE_EX_PIN) {
                        LdrDataTableEntry->LoadCount = 0xffff;
                        LdrpPinLoadedDll(LdrDataTableEntry);
                    }
                    else {
                        LdrDataTableEntry->LoadCount++;
                        LdrpReferenceLoadedDll(LdrDataTableEntry);
                    }
                    LdrpClearLoadInProgress();
                }
            }
            if (DllHandle != NULL) {
                *DllHandle = (PVOID)LdrDataTableEntry->DllBase;
            }
        }
    } __finally {
        if (DynamicRedirectedDllName.Buffer != NULL) {
            RtlFreeUnicodeString(&DynamicRedirectedDllName);
        }

        if (ActualDllNameStr.Buffer != NULL) {
            RtlFreeHeap(RtlProcessHeap(), 0, (PVOID)ActualDllNameStr.Buffer);
            ActualDllNameStr.Buffer = NULL;
        }

        if (HoldingLoaderLock) {
            LdrUnlockLoaderLock(LDR_UNLOCK_LOADER_LOCK_FLAG_RAISE_ON_ERRORS, LockCookie);
            HoldingLoaderLock = FALSE;
        }
    }
    return st;
}


NTSTATUS
LdrDisableThreadCalloutsForDll (
    IN PVOID DllHandle
    )

/*++

Routine Description:

    This function disables thread attach and detach notification
    for the specified DLL.

Arguments:

    DllHandle - Supplies a handle to the DLL to disable.

Return Value:

    NTSTATUS.

--*/

{
    NTSTATUS st = STATUS_SUCCESS;
    PLDR_DATA_TABLE_ENTRY LdrDataTableEntry = NULL;
    const BOOLEAN InLdrInit = LdrpInLdrInit;
    BOOL HoldingLoaderLock = FALSE;
    PVOID LockCookie = NULL;

    if ( LdrpShutdownInProgress ) {
        return STATUS_SUCCESS;
        }

    try {

        if ( InLdrInit == FALSE ) {
            st = LdrLockLoaderLock(0, NULL, &LockCookie);
            if (!NT_SUCCESS(st))
                goto Exit;
            HoldingLoaderLock = TRUE;
            }

        if (LdrpCheckForLoadedDllHandle(DllHandle, &LdrDataTableEntry)) {
            if ( LdrDataTableEntry->TlsIndex ) {
                st = STATUS_DLL_NOT_FOUND;
                }
            else {
                LdrDataTableEntry->Flags |= LDRP_DONT_CALL_FOR_THREADS;
                }
            }
Exit:
        ;
    }
    finally {
        if (HoldingLoaderLock) {
            LdrUnlockLoaderLock(LDR_UNLOCK_LOADER_LOCK_FLAG_RAISE_ON_ERRORS, LockCookie);
            HoldingLoaderLock = FALSE;
            }
        }
    return st;
}

ULONG LdrpUnloadIndex = 0;
RTL_UNLOAD_EVENT_TRACE RtlpUnloadEventTrace[RTL_UNLOAD_EVENT_TRACE_NUMBER];


NTSYSAPI
PRTL_UNLOAD_EVENT_TRACE
NTAPI
RtlGetUnloadEventTrace (
    VOID
    )
{
    return RtlpUnloadEventTrace;
}


VOID
LdrpRecordUnloadEvent (
    IN PLDR_DATA_TABLE_ENTRY LdrDataTableEntry
    )
/*++

Routine Description:

    This function records in a ring buffer the last few dll unloads

Arguments:

    LdrDataTableEntry - The ldr entry for this dll

Return Value:

    None.

--*/
{
    ULONG Seq, i, Len;
    PVOID BaseAddress;
    PIMAGE_NT_HEADERS NtHeaders;

    Seq = LdrpUnloadIndex++;
    i = Seq % RTL_UNLOAD_EVENT_TRACE_NUMBER;

    BaseAddress = LdrDataTableEntry->DllBase;
    RtlpUnloadEventTrace[i].Sequence    = Seq;
    RtlpUnloadEventTrace[i].BaseAddress = BaseAddress;
    RtlpUnloadEventTrace[i].SizeOfImage = LdrDataTableEntry->SizeOfImage;

    Len = LdrDataTableEntry->BaseDllName.Length;
    if (Len > sizeof (RtlpUnloadEventTrace[i].ImageName)) {
        Len = sizeof (RtlpUnloadEventTrace[i].ImageName);
    }
    RtlCopyMemory (RtlpUnloadEventTrace[i].ImageName,
                   LdrDataTableEntry->BaseDllName.Buffer,
                   Len);
    if (Len < sizeof (RtlpUnloadEventTrace[i].ImageName)) {
        RtlpUnloadEventTrace[i].ImageName[Len/sizeof (WCHAR)] = L'\0';
    }

    NtHeaders = RtlImageNtHeader (BaseAddress);
    if (NtHeaders != NULL) {
        RtlpUnloadEventTrace[i].TimeDateStamp = NtHeaders->FileHeader.TimeDateStamp;
        RtlpUnloadEventTrace[i].CheckSum      = NtHeaders->OptionalHeader.CheckSum;
    } else {
        RtlpUnloadEventTrace[i].TimeDateStamp = 0;
        RtlpUnloadEventTrace[i].CheckSum      = 0;
    }
}


NTSTATUS
LdrUnloadDll (
    IN PVOID DllHandle
    )

/*++

Routine Description:

    This function unloads the DLL from the specified process

Arguments:

    DllHandle - Supplies a handle to the DLL to unload.

Return Value:

    NTSTATUS.

--*/

{
    NTSTATUS st;
    PPEB Peb;
    PLDR_DATA_TABLE_ENTRY LdrDataTableEntry;
    PLDR_DATA_TABLE_ENTRY Entry;
    PDLL_INIT_ROUTINE InitRoutine;
    LIST_ENTRY LocalUnloadHead;
    PLIST_ENTRY Next;
    ULONG Cor20HeaderSize;
    PIMAGE_COR20_HEADER *Cor20Header;
    PRTL_PATCH_HEADER RundownPatchList = NULL;

    Peb = NtCurrentPeb();
    st = STATUS_SUCCESS;

    //
    // Grab Peb lock and decrement reference count of all affected DLLs
    //

    if (!LdrpInLdrInit) {
        RtlEnterCriticalSection(&LdrpLoaderLock);
    }

    try {

        LdrpActiveUnloadCount += 1;

        if (LdrpShutdownInProgress) {
            goto leave_finally;
        }

        if (!LdrpCheckForLoadedDllHandle(DllHandle, &LdrDataTableEntry)) {
            st = STATUS_DLL_NOT_FOUND;
            goto leave_finally;
        }

        //
        // Now that we have the data table entry, unload it
        //

        if (LdrDataTableEntry->LoadCount != 0xffff) {
            LdrDataTableEntry->LoadCount -= 1;
            if (LdrDataTableEntry->Flags & LDRP_IMAGE_DLL) {
                RTL_CALLER_ALLOCATED_ACTIVATION_CONTEXT_STACK_FRAME Frame = { sizeof(Frame), RTL_CALLER_ALLOCATED_ACTIVATION_CONTEXT_STACK_FRAME_FORMAT_WHISTLER };

                RtlActivateActivationContextUnsafeFast(&Frame, LdrDataTableEntry->EntryPointActivationContext);
                __try {
                    LdrpDereferenceLoadedDll(LdrDataTableEntry);
                } __finally {
                    RtlDeactivateActivationContextUnsafeFast(&Frame);
                }
            }
        } else {

            //
            // if the load count is 0xffff, then we do not need to recurse
            // through this DLL's import table.
            //
            // Additionally, we don't have to scan more LoadCount == 0
            // modules since nothing could have happened as a result of a free on this
            // DLL.

            goto leave_finally;
        }

        //
        // Now process init routines and then in a second pass, unload
        // DLLs
        //

        if (ShowSnaps) {
            DbgPrint("LDR: UNINIT LIST\n");
        }

        if (LdrpActiveUnloadCount == 1) {
            InitializeListHead(&LdrpUnloadHead);
        }

        //
        // Go in reverse order initialization order and build
        // the unload list
        //

        Next = PebLdr.InInitializationOrderModuleList.Blink;
        while ( Next != &PebLdr.InInitializationOrderModuleList) {
            LdrDataTableEntry
                = (PLDR_DATA_TABLE_ENTRY)
                  (CONTAINING_RECORD(Next,LDR_DATA_TABLE_ENTRY,InInitializationOrderLinks));

            Next = Next->Blink;
            LdrDataTableEntry->Flags &= ~LDRP_UNLOAD_IN_PROGRESS;

            if (LdrDataTableEntry->LoadCount == 0) {

                if (ShowSnaps) {
                      DbgPrint("          (%d) [%ws] %ws (%lx) deinit %lx\n",
                              LdrpActiveUnloadCount,
                              LdrDataTableEntry->BaseDllName.Buffer,
                              LdrDataTableEntry->FullDllName.Buffer,
                              (ULONG)LdrDataTableEntry->LoadCount,
                              LdrDataTableEntry->EntryPoint
                              );
                }

                Entry = LdrDataTableEntry;

                //
                // Shim engine callback. Remove it from the shim list of hooked modules
                //

                if (g_pfnSE_DllUnloaded != NULL) {
                    (*g_pfnSE_DllUnloaded)(Entry);
                }

                RemoveEntryList(&Entry->InInitializationOrderLinks);
                RemoveEntryList(&Entry->InMemoryOrderLinks);
                RemoveEntryList(&Entry->HashLinks);

                if ( LdrpActiveUnloadCount > 1 ) {
                    LdrpLoadedDllHandleCache = NULL;
                    Entry->InMemoryOrderLinks.Flink = NULL;
                }
                InsertTailList(&LdrpUnloadHead,&Entry->HashLinks);
            }
        }
        //
        // End of new code
        //

        //
        // We only do init routine call's and module free's at the top level,
        // so if the active count is > 1, just return
        //

        if (LdrpActiveUnloadCount > 1 ) {
            goto leave_finally;
        }

        //
        // Now that the unload list is built, walk through the unload
        // list in order and call the init routine. The dll must remain
        // on the InLoadOrderLinks so that the pctoheader stuff will
        // still work
        //

        InitializeListHead(&LocalUnloadHead);
        Entry = NULL;
        Next = LdrpUnloadHead.Flink;
        while ( Next != &LdrpUnloadHead ) {
top:
            if ( Entry ) {

#if defined(_AMD64_) || defined(_IA64_)


                RtlRemoveInvertedFunctionTable(&LdrpInvertedFunctionTable,
                                               Entry->DllBase);

#endif

                RemoveEntryList(&(Entry->InLoadOrderLinks));
                Entry = NULL;
                Next = LdrpUnloadHead.Flink;
                if (Next == &LdrpUnloadHead ) {
                    goto bottom;
                }
            }
            LdrDataTableEntry
                = (PLDR_DATA_TABLE_ENTRY)
                  (CONTAINING_RECORD(Next,LDR_DATA_TABLE_ENTRY,HashLinks));

            LdrpRecordUnloadEvent (LdrDataTableEntry);

            //
            // Remove dll from the global unload list and place
            // on the local unload list. This is because the global list
            // can change during the callout to the init routine
            //

            Entry = LdrDataTableEntry;
            LdrpLoadedDllHandleCache = NULL;
            Entry->InMemoryOrderLinks.Flink = NULL;

            RemoveEntryList(&Entry->HashLinks);
            InsertTailList(&LocalUnloadHead,&Entry->HashLinks);

            //
            // If the function has an init routine, call it.
            //

            InitRoutine = (PDLL_INIT_ROUTINE)(ULONG_PTR)LdrDataTableEntry->EntryPoint;

            if (InitRoutine && (LdrDataTableEntry->Flags & LDRP_PROCESS_ATTACH_CALLED) ) {
                try {
                    if (ShowSnaps) {
                        DbgPrint("LDR: Calling deinit %lx\n",InitRoutine);
                    }

                    LDRP_ACTIVATE_ACTIVATION_CONTEXT(LdrDataTableEntry);

                    LdrpCallInitRoutine(InitRoutine,
                                        LdrDataTableEntry->DllBase,
                                        DLL_PROCESS_DETACH,
                                        NULL);

                    LDRP_DEACTIVATE_ACTIVATION_CONTEXT();

#if defined(_AMD64_) || defined(_IA64_)


                    RtlRemoveInvertedFunctionTable(&LdrpInvertedFunctionTable,
                                                   Entry->DllBase);

#endif

                    RemoveEntryList(&Entry->InLoadOrderLinks);
                    Entry = NULL;
                    Next = LdrpUnloadHead.Flink;
                }
                except(LdrpGenericExceptionFilter(GetExceptionInformation(), __FUNCTION__)){
                    DbgPrintEx(
                        DPFLTR_LDR_ID,
                        LDR_ERROR_DPFLTR,
                        "LDR: %s - exception %08lx caught while sending DLL_PROCESS_DETACH\n",
                        __FUNCTION__,
                        GetExceptionCode());

                    DbgPrintEx(
                        DPFLTR_LDR_ID,
                        LDR_ERROR_DPFLTR,
                        "   Dll Name: %wZ\n",
                        &LdrDataTableEntry->FullDllName);

                    DbgPrintEx(
                        DPFLTR_LDR_ID,
                        LDR_ERROR_DPFLTR,
                        "   InitRoutine: %p\n",
                        InitRoutine);

                    goto top;
                }
            } else {

#if defined(_AMD64_) || defined(_IA64_)


                RtlRemoveInvertedFunctionTable(&LdrpInvertedFunctionTable,
                                               Entry->DllBase);

#endif

                RemoveEntryList(&(Entry->InLoadOrderLinks));
                Entry = NULL;
                Next = LdrpUnloadHead.Flink;
            }
        }
bottom:

        //
        // Now, go through the modules and unmap them
        //

        Next = LocalUnloadHead.Flink;
        while ( Next != &LocalUnloadHead ) {
            LdrDataTableEntry
                = (PLDR_DATA_TABLE_ENTRY)
                  (CONTAINING_RECORD(Next,LDR_DATA_TABLE_ENTRY,HashLinks));

            Next = Next->Flink;
            Entry = LdrDataTableEntry;

            //
            // Notify verifier that a dll will be unloaded.
            //
            // Now that we called the all the init routines with `detach' 
            // there is no excuse if we find a live CS in that region.
            //
            // Note: gdi32.dll's critical sections are deleted only on
            // user32.dll'd DllMain( DLL_PROCESS_DETACH ) so we cannot 
            // do this check for leaked critical sections prior to this point.
            //

            if (Peb->NtGlobalFlag & FLG_APPLICATION_VERIFIER) {
                AVrfDllUnloadNotification (LdrDataTableEntry);
            }

            //
            // Unmap this DLL.
            //

            if (ShowSnaps) {
                  DbgPrint("LDR: Unmapping [%ws]\n",
                          LdrDataTableEntry->BaseDllName.Buffer
                          );
            }

            Cor20Header =  RtlImageDirectoryEntryToData(Entry->DllBase,
                                                        TRUE,
                                                        IMAGE_DIRECTORY_ENTRY_COM_DESCRIPTOR,
                                                        &Cor20HeaderSize);
            if (Cor20Header != NULL) {
                LdrpCorUnloadImage(Entry->DllBase);
            }
            if (!(Entry->Flags & LDRP_COR_OWNS_UNMAP)) {
                st = NtUnmapViewOfSection(NtCurrentProcess(),Entry->DllBase);
                ASSERT(NT_SUCCESS(st));
            }

            LdrUnloadAlternateResourceModule(Entry->DllBase);

            LdrpSendDllNotifications (Entry,
                                      LDR_DLL_NOTIFICATION_REASON_UNLOADED,
                                      (LdrpShutdownInProgress ? LDR_DLL_UNLOADED_FLAG_PROCESS_TERMINATION : 0));

            //
            //  See if we have hotpatch information and push each hotpatch block
            //  to the rundown list
            //

            while (Entry->PatchInformation) {

                PRTL_PATCH_HEADER PatchHead = Entry->PatchInformation;
                Entry->PatchInformation = PatchHead->NextPatch;

                PatchHead->NextPatch = RundownPatchList;
                RundownPatchList = PatchHead;
            }

            LdrpFinalizeAndDeallocateDataTableEntry(Entry);

            if ( Entry == LdrpGetModuleHandleCache ) {
                LdrpGetModuleHandleCache = NULL;
            }
        }

leave_finally:;
    }
    finally {
        LdrpActiveUnloadCount -= 1;
        if (!LdrpInLdrInit) {
            RtlLeaveCriticalSection(&LdrpLoaderLock);
        }
    }

    if ( RundownPatchList ) {

        LdrpRundownHotpatchList( RundownPatchList );
    }

    return st;
}


NTSTATUS
LdrGetProcedureAddress (
    IN PVOID DllHandle,
    IN CONST ANSI_STRING* ProcedureName OPTIONAL,
    IN ULONG ProcedureNumber OPTIONAL,
    OUT PVOID *ProcedureAddress
    )
{
    return LdrpGetProcedureAddress(DllHandle,ProcedureName,ProcedureNumber,ProcedureAddress,TRUE);
}


NTSTATUS
LdrpGetProcedureAddress (
    IN PVOID DllHandle,
    IN CONST ANSI_STRING* ProcedureName OPTIONAL,
    IN ULONG ProcedureNumber OPTIONAL,
    OUT PVOID *ProcedureAddress,
    IN BOOLEAN RunInitRoutines
    )

/*++

Routine Description:

    This function locates the address of the specified procedure in the
    specified DLL and returns its address.

Arguments:

    DllHandle - Supplies a handle to the DLL that the address is being
        looked up in.

    ProcedureName - Supplies that address of a string that contains the
        name of the procedure to lookup in the DLL.  If this argument is
        not specified, then the ProcedureNumber is used.

    ProcedureNumber - Supplies the procedure number to lookup.  If
        ProcedureName is specified, then this argument is ignored.
        Otherwise, it specifies the procedure ordinal number to locate
        in the DLL.

    ProcedureAddress - Returns the address of the procedure found in
        the DLL.

Return Value:

    NTSTATUS.

--*/

{
    NTSTATUS st;
    UCHAR FunctionNameBuffer[64];
    ULONG cb, ExportSize;
    PLDR_DATA_TABLE_ENTRY LdrDataTableEntry;
    IMAGE_THUNK_DATA Thunk;
    PVOID ImageBase;
    PIMAGE_IMPORT_BY_NAME FunctionName;
    PCIMAGE_EXPORT_DIRECTORY ExportDirectory;
    PLIST_ENTRY Next;

    if (ShowSnaps) {
        DbgPrint("LDR: LdrGetProcedureAddress by ");
    }

    RtlZeroMemory( &Thunk, sizeof( Thunk ) );

    FunctionName = NULL;
    if ( ARGUMENT_PRESENT(ProcedureName) ) {

        if (ShowSnaps) {
            DbgPrint("NAME - %s\n", ProcedureName->Buffer);
        }

        cb = ProcedureName->Length + FIELD_OFFSET(IMAGE_IMPORT_BY_NAME, Name) +
                sizeof( UCHAR );
        if (cb > MAXUSHORT) {
            return STATUS_NAME_TOO_LONG;
        }

        if (cb > sizeof( FunctionNameBuffer )) {
            FunctionName = (PIMAGE_IMPORT_BY_NAME)RtlAllocateHeap(
                                                        RtlProcessHeap(),
                                                        MAKE_TAG( TEMP_TAG ),
                                                        cb
                                                        );
            if ( !FunctionName ) {
                return STATUS_INVALID_PARAMETER;
                }
        } else {
            FunctionName = (PIMAGE_IMPORT_BY_NAME) FunctionNameBuffer;
        }

        FunctionName->Hint = 0;

        cb = ProcedureName->Length;

        RtlCopyMemory (FunctionName->Name, ProcedureName->Buffer, cb);

        FunctionName->Name[cb] = '\0';

        //
        // Make sure we don't pass in address with high bit set so we
        // can still use it as ordinal flag
        //

        ImageBase = FunctionName;
        Thunk.u1.AddressOfData = 0;

    } else {
        ImageBase = NULL;
        if (ShowSnaps) {
            DbgPrint("ORDINAL - %lx\n", ProcedureNumber);
        }

        if (ProcedureNumber) {
            Thunk.u1.Ordinal = ProcedureNumber | IMAGE_ORDINAL_FLAG;
        } else {
            return STATUS_INVALID_PARAMETER;
        }
    }

    st = STATUS_ACCESS_VIOLATION;

    if (!LdrpInLdrInit) {
        RtlEnterCriticalSection (&LdrpLoaderLock);
    }

    try {

        if (!LdrpCheckForLoadedDllHandle (DllHandle, &LdrDataTableEntry)) {
            st = STATUS_DLL_NOT_FOUND;
            leave;
        }

        ExportDirectory = (PCIMAGE_EXPORT_DIRECTORY)RtlImageDirectoryEntryToData(
                           LdrDataTableEntry->DllBase,
                           TRUE,
                           IMAGE_DIRECTORY_ENTRY_EXPORT,
                           &ExportSize);

        if (!ExportDirectory) {
            st = STATUS_PROCEDURE_NOT_FOUND;
            leave;
        }

        st = LdrpSnapThunk(LdrDataTableEntry->DllBase,
                           ImageBase,
                           &Thunk,
                           &Thunk,
                           ExportDirectory,
                           ExportSize,
                           FALSE,
                           NULL);

        if (NT_SUCCESS(st) && RunInitRoutines) {

            PLDR_DATA_TABLE_ENTRY LdrInitEntry;

            //
            // Look at last entry in init order list. If entry processed
            // flag is not set, then a forwarded dll was loaded during the
            // getprocaddr call and we need to run init routines
            //

            Next = PebLdr.InInitializationOrderModuleList.Blink;

            LdrInitEntry = CONTAINING_RECORD(Next,
                                             LDR_DATA_TABLE_ENTRY,
                                             InInitializationOrderLinks);

            if ( !(LdrInitEntry->Flags & LDRP_ENTRY_PROCESSED) ) {
                
                //
                // Shim engine callback. This is the chance to patch
                // dynamically loaded modules.
                //

                try {
                    st = LdrpRunInitializeRoutines(NULL);
                }
                except(LdrpGenericExceptionFilter(GetExceptionInformation(), __FUNCTION__)) {
                    st = GetExceptionCode();

                    DbgPrintEx(
                        DPFLTR_LDR_ID,
                        LDR_ERROR_DPFLTR,
                        "LDR: %s - Exception %x thrown by LdrpRunInitializeRoutines\n",
                        __FUNCTION__,
                        st);
                }
            }
        }

        if ( NT_SUCCESS(st) ) {
            *ProcedureAddress = (PVOID)Thunk.u1.Function;
        }
    } finally {
        if ( FunctionName && (FunctionName != (PIMAGE_IMPORT_BY_NAME) FunctionNameBuffer) ) {
            RtlFreeHeap(RtlProcessHeap(),0,FunctionName);
        }

        if (!LdrpInLdrInit) {
            RtlLeaveCriticalSection(&LdrpLoaderLock);
        }
    }
    return st;
}


NTSTATUS
NTAPI
LdrVerifyImageMatchesChecksum (
    IN HANDLE ImageFileHandle,
    IN PLDR_IMPORT_MODULE_CALLBACK ImportCallbackRoutine OPTIONAL,
    IN PVOID ImportCallbackParameter,
    OUT PUSHORT ImageCharacteristics OPTIONAL
    )
{
    NTSTATUS Status;
    HANDLE Section;
    PVOID ViewBase;
    SIZE_T ViewSize;
    IO_STATUS_BLOCK IoStatusBlock;
    FILE_STANDARD_INFORMATION StandardInfo;
    PIMAGE_SECTION_HEADER LastRvaSection;
    BOOLEAN b = FALSE;
    BOOLEAN JustDoSideEffects;

    //
    // stevewo added all sorts of side effects to this API. We want to stop
    // doing checksums for known dll's, but really want the sideeffects
    // (ImageCharacteristics write, and Import descriptor walk).
    //

    if ( (UINT_PTR) ImageFileHandle & 1 ) {
        JustDoSideEffects = TRUE;
    }
    else {
        JustDoSideEffects = FALSE;
    }

    Status = NtCreateSection (&Section,
                              SECTION_MAP_EXECUTE,
                              NULL,
                              NULL,
                              PAGE_EXECUTE,
                              SEC_COMMIT,
                              ImageFileHandle);

    if (!NT_SUCCESS(Status)) {
        return Status;
    }

    ViewBase = NULL;
    ViewSize = 0;

    Status = NtMapViewOfSection (Section,
                                 NtCurrentProcess(),
                                 (PVOID *)&ViewBase,
                                 0L,
                                 0L,
                                 NULL,
                                 &ViewSize,
                                 ViewShare,
                                 0L,
                                 PAGE_EXECUTE);

    if ( !NT_SUCCESS(Status) ) {
        NtClose(Section);
        return Status;
    }

    //
    // now the image is mapped as a data file... Calculate it's size and then
    // check it's checksum
    //

    Status = NtQueryInformationFile(
                ImageFileHandle,
                &IoStatusBlock,
                &StandardInfo,
                sizeof(StandardInfo),
                FileStandardInformation
                );

    if ( !NT_SUCCESS(Status) ) {
        NtUnmapViewOfSection(NtCurrentProcess(),ViewBase);
        NtClose(Section);
        return Status;
        }

    try {
        if ( JustDoSideEffects ) {
            b = TRUE;
            }
        else {
            b = LdrVerifyMappedImageMatchesChecksum(ViewBase,StandardInfo.EndOfFile.LowPart);
            }
        if (b && ARGUMENT_PRESENT( (ULONG_PTR)ImportCallbackRoutine )) {
            PIMAGE_NT_HEADERS NtHeaders;
            PCIMAGE_IMPORT_DESCRIPTOR ImportDescriptor;
            ULONG ImportSize;
            PCHAR ImportName;

            //
            // Caller wants to enumerate the import descriptors while we have
            // the image mapped.  Call back to their routine for each module
            // name in the import descriptor table.
            //
            LastRvaSection = NULL;
            NtHeaders = RtlImageNtHeader( ViewBase );
            if (! NtHeaders) {
                b = FALSE;
                leave;
            }
            if (ARGUMENT_PRESENT( ImageCharacteristics )) {
                *ImageCharacteristics = NtHeaders->FileHeader.Characteristics;
                }

            ImportDescriptor = (PCIMAGE_IMPORT_DESCRIPTOR)
                RtlImageDirectoryEntryToData( ViewBase,
                                              FALSE,
                                              IMAGE_DIRECTORY_ENTRY_IMPORT,
                                              &ImportSize
                                            );
            if (ImportDescriptor != NULL) {
                while (ImportDescriptor->Name) {
                    ImportName = (PSZ)RtlImageRvaToVa( NtHeaders,
                                                       ViewBase,
                                                       ImportDescriptor->Name,
                                                       &LastRvaSection
                                                     );
                    (*ImportCallbackRoutine)( ImportCallbackParameter, ImportName );
                    ImportDescriptor += 1;
                    }
                }
            }
        }
    except (LdrpGenericExceptionFilter(GetExceptionInformation(), __FUNCTION__)) {
        DbgPrintEx(
            DPFLTR_LDR_ID,
            LDR_ERROR_DPFLTR,
            "LDR: %s - caught exception %08lx while checking image checksums\n",
            __FUNCTION__,
            GetExceptionCode());

        NtUnmapViewOfSection(NtCurrentProcess(),ViewBase);
        NtClose(Section);
        return STATUS_IMAGE_CHECKSUM_MISMATCH;
        }
    NtUnmapViewOfSection(NtCurrentProcess(),ViewBase);
    NtClose(Section);
    if ( !b ) {
        Status = STATUS_IMAGE_CHECKSUM_MISMATCH;
        }
    return Status;
}


NTSTATUS 
LdrReadMemory(
    IN HANDLE Process OPTIONAL,
    IN PVOID BaseAddress,
    IN OUT PVOID Buffer,
    IN SIZE_T Size)
{
    NTSTATUS Status = STATUS_SUCCESS;

    if (ARGUMENT_PRESENT( Process )) {
        SIZE_T nRead;
        Status = NtReadVirtualMemory(Process, BaseAddress, Buffer, Size, &nRead);

        if (NT_SUCCESS( Status ) && (Size != nRead)) {
            Status = STATUS_UNSUCCESSFUL;
        }
    }
    else {
        __try {
            RtlCopyMemory(Buffer, BaseAddress, Size);
        }
        __except(LdrpGenericExceptionFilter(GetExceptionInformation(), __FUNCTION__)) {
            DbgPrintEx(
                DPFLTR_LDR_ID,
                LDR_ERROR_DPFLTR,
                "LDR: %s - exception %08lx caught while copying %u bytes from %p to %p\n",
                __FUNCTION__,
                GetExceptionCode(),
                BaseAddress,
                Buffer);

            if (NT_SUCCESS(Status = GetExceptionCode())) {
                Status = STATUS_UNSUCCESSFUL;
            }
        }
    }
    return Status;
}


NTSTATUS 
LdrGetModuleName(
    IN HANDLE Process OPTIONAL,
    IN PCUNICODE_STRING LdrFullDllName,
    IN OUT PRTL_PROCESS_MODULE_INFORMATION ModuleInfo,
    IN BOOL Wow64Redirect)
{
    NTSTATUS Status;
    UNICODE_STRING FullDllName;
    ANSI_STRING AnsiString;
    PCHAR s;
    WCHAR Buffer[ LDR_NUMBER_OF(ModuleInfo->FullPathName) + 1];
    USHORT Length = (USHORT)min(LdrFullDllName->Length, 
                                sizeof(Buffer) - sizeof(Buffer[0]));

    Status = LdrReadMemory(Process, 
                           LdrFullDllName->Buffer, 
                           Buffer,
                           Length);

    if (!NT_SUCCESS( Status )) {
        return Status;
    }

    Buffer[LDR_NUMBER_OF(Buffer) - 1] = UNICODE_NULL;  // Ensure NULL termination
    
#if defined(_WIN64)
    if (Wow64Redirect) {
    
        C_ASSERT( WOW64_SYSTEM_DIRECTORY_U_SIZE == 
                  (sizeof(L"system32") - sizeof(WCHAR)));
                  
        // including preceding '\\' if exists
        SIZE_T System32Offset = wcslen(USER_SHARED_DATA->NtSystemRoot);
        ASSERT(System32Offset != 0);
        
        if (USER_SHARED_DATA->NtSystemRoot[System32Offset - 1] == L'\\') {
            --System32Offset;
        }

        if (!_wcsnicmp(Buffer, USER_SHARED_DATA->NtSystemRoot, System32Offset) &&
            !_wcsnicmp(Buffer + System32Offset, 
                       L"\\system32", 
                       WOW64_SYSTEM_DIRECTORY_U_SIZE / sizeof(WCHAR) + 1)) {
                       
            RtlCopyMemory(Buffer + System32Offset + 1,
                          WOW64_SYSTEM_DIRECTORY_U,
                          WOW64_SYSTEM_DIRECTORY_U_SIZE);
        }
    }
#else
    UNREFERENCED_PARAMETER (Wow64Redirect);
#endif // defined(_WIN64)
    
    FullDllName.Buffer = Buffer;
    FullDllName.Length = FullDllName.MaximumLength = Length;

    AnsiString.Buffer = (PCHAR)ModuleInfo->FullPathName;
    AnsiString.Length = 0;
    AnsiString.MaximumLength = sizeof( ModuleInfo->FullPathName );

    Status = RtlUnicodeStringToAnsiString(&AnsiString,
                                          &FullDllName,
                                          FALSE);
    if (!NT_SUCCESS (Status)) {
        return Status;
    }

    s = AnsiString.Buffer + AnsiString.Length;
    while (s > AnsiString.Buffer && *--s) {
        if (*s == (UCHAR)OBJ_NAME_PATH_SEPARATOR) {
            s++;
            break;
        }
    }

    ModuleInfo->OffsetToFileName = (USHORT)(s - AnsiString.Buffer);
    return STATUS_SUCCESS;
}


NTSTATUS
LdrQueryProcessPeb (
    IN HANDLE Process OPTIONAL,
    IN OUT PPEB* Peb)
{
    NTSTATUS Status;
    PROCESS_BASIC_INFORMATION BasicInfo;

    if (ARGUMENT_PRESENT (Process)) {

        Status = NtQueryInformationProcess (Process, 
                                            ProcessBasicInformation,
                                            &BasicInfo,
                                            sizeof(BasicInfo),
                                            NULL);

        if (NT_SUCCESS (Status)) {
            *Peb = BasicInfo.PebBaseAddress;
        }
    }
    else {
        *Peb = NtCurrentPeb ();
        Status = STATUS_SUCCESS;
    }

    return Status;
}


NTSTATUS
LdrQueryInLoadOrderModuleList (
    IN HANDLE Process OPTIONAL,
    IN OUT PLIST_ENTRY* Head,
    IN OUT PLIST_ENTRY* InInitOrderHead OPTIONAL
    )
{
    PPEB_LDR_DATA Ldr;

    UNREFERENCED_PARAMETER (Process);

    Ldr = &PebLdr;

    *Head = &Ldr->InLoadOrderModuleList;

    if (ARGUMENT_PRESENT (InInitOrderHead)) {
        *InInitOrderHead = &Ldr->InInitializationOrderModuleList;
    }

    return STATUS_SUCCESS;
}


NTSTATUS 
LdrQueryNextListEntry (
    IN HANDLE Process OPTIONAL,
    IN PLIST_ENTRY Head,
    IN OUT PLIST_ENTRY* Tail
    )
{
    return LdrReadMemory (Process, &Head->Flink, Tail, sizeof(*Tail));
}


NTSTATUS
LdrQueryModuleInfoFromLdrEntry (
    IN HANDLE Process OPTIONAL,
    IN PRTL_PROCESS_MODULES ModuleInformation, 
    IN OUT PRTL_PROCESS_MODULE_INFORMATION ModuleInfo,
    IN PLIST_ENTRY LdrEntry,
    IN PLIST_ENTRY InitOrderList)
{
    NTSTATUS Status;
    PLDR_DATA_TABLE_ENTRY LdrDataTableEntryPtr;
    LDR_DATA_TABLE_ENTRY LdrDataTableEntry;

    UNREFERENCED_PARAMETER (ModuleInformation);

    LdrDataTableEntryPtr = CONTAINING_RECORD(LdrEntry, 
                                             LDR_DATA_TABLE_ENTRY, 
                                             InLoadOrderLinks);

    Status = LdrReadMemory(Process, 
                           LdrEntry, 
                           &LdrDataTableEntry, 
                           sizeof(LdrDataTableEntry));

    if (!NT_SUCCESS (Status)) {
        return Status;
    }

    ModuleInfo->ImageBase = LdrDataTableEntry.DllBase;
    ModuleInfo->ImageSize = LdrDataTableEntry.SizeOfImage;
    ModuleInfo->Flags     = LdrDataTableEntry.Flags;
    ModuleInfo->LoadCount = LdrDataTableEntry.LoadCount;
    
    if (!ARGUMENT_PRESENT( Process )) {
        UINT LoopDetectorCount = 10240;  // 10K modules max
        PLIST_ENTRY Next1 = InitOrderList->Flink;

        while ( Next1 != InitOrderList ) {
            PLDR_DATA_TABLE_ENTRY Entry1 = 
                CONTAINING_RECORD(Next1,
                                  LDR_DATA_TABLE_ENTRY,
                                  InInitializationOrderLinks);

            ModuleInfo->InitOrderIndex++;

            if ((LdrDataTableEntryPtr == Entry1) ||
                (!LoopDetectorCount--)) 
            {
                break;
            }

            Next1 = Next1->Flink;
        } 
    }

    Status = LdrGetModuleName(Process, 
                              &LdrDataTableEntry.FullDllName, 
                              ModuleInfo, 
                              FALSE);

    return Status;
}


PRTL_CRITICAL_SECTION
LdrQueryModuleInfoLocalLoaderLock (
    VOID
    )
{
    PRTL_CRITICAL_SECTION LoaderLock = NULL;

    if (!LdrpInLdrInit) {
        LoaderLock = &LdrpLoaderLock;

        if (LoaderLock != NULL) {
            RtlEnterCriticalSection (LoaderLock);
        }
    }

    return LoaderLock;
}


VOID
LdrQueryModuleInfoLocalLoaderUnlock (
    IN PRTL_CRITICAL_SECTION LoaderLock
    )
{
    if (LoaderLock) {
        RtlLeaveCriticalSection(LoaderLock);
    }
}

#if defined(_WIN64)

NTSTATUS
LdrQueryProcessPeb32(
    IN HANDLE Process OPTIONAL,
    IN OUT PPEB32* Peb
    )
{
    NTSTATUS Status;
    HANDLE TargetProcess;

    if (ARGUMENT_PRESENT (Process)) {
        TargetProcess = Process;
    }
    else {
        TargetProcess = NtCurrentProcess ();
    }

    Status = NtQueryInformationProcess (TargetProcess,
                                        ProcessWow64Information,
                                        Peb,
                                        sizeof(*Peb),
                                        NULL);
    return Status;
}


NTSTATUS
LdrQueryInLoadOrderModuleList32(
    IN HANDLE Process OPTIONAL,
    IN OUT PLIST_ENTRY32 *Head,
    IN OUT PLIST_ENTRY32 *InInitOrderHead OPTIONAL
    )
{
    NTSTATUS Status;
    PPEB32 Peb;
    PPEB_LDR_DATA32 Ldr;
    ULONG32 Ptr32;

    Status = LdrQueryProcessPeb32 (Process, &Peb);

    if (!NT_SUCCESS (Status)) {
        return Status;
    }

    if (!Peb) {

        //
        // The process isn't a WOW process.
        //

        *Head = NULL;
        return STATUS_SUCCESS;
    }

    //
    // Ldr = Peb->Ldr
    //

    Status = LdrReadMemory (Process, &Peb->Ldr, &Ptr32, sizeof(Ptr32));

    if (!NT_SUCCESS (Status)) {
        return Status;
    }

    Ldr = (PPEB_LDR_DATA32)(ULONG_PTR) Ptr32;

    if (!Ldr) {
        *Head = NULL;
        return STATUS_SUCCESS;
    }

    *Head = &Ldr->InLoadOrderModuleList;

    if (ARGUMENT_PRESENT (InInitOrderHead)) {
        *InInitOrderHead = &Ldr->InInitializationOrderModuleList;
    }

    return Status;
}


NTSTATUS 
LdrQueryNextListEntry32 (
    IN HANDLE Process OPTIONAL,
    IN PLIST_ENTRY32 Head,
    IN OUT PLIST_ENTRY32 *Tail
    )
{
    NTSTATUS Status;
    ULONG32 Ptr32;

    Status = LdrReadMemory (Process, &Head->Flink, &Ptr32, sizeof(Ptr32));

    *Tail = (PLIST_ENTRY32)(ULONG_PTR)Ptr32;

    return Status;
}


NTSTATUS
LdrQueryModuleInfoFromLdrEntry32 (
    IN HANDLE Process OPTIONAL,
    IN PRTL_PROCESS_MODULES ModuleInformation, 
    IN OUT PRTL_PROCESS_MODULE_INFORMATION ModuleInfo,
    IN PLIST_ENTRY32 LdrEntry,
    IN PLIST_ENTRY32 InitOrderList
    )
{
    NTSTATUS Status;
    PLDR_DATA_TABLE_ENTRY32 LdrDataTableEntryPtr;
    LDR_DATA_TABLE_ENTRY32 LdrDataTableEntry;
    UNICODE_STRING FullDllName;

    UNREFERENCED_PARAMETER (ModuleInformation);

    LdrDataTableEntryPtr = CONTAINING_RECORD(LdrEntry,
                                             LDR_DATA_TABLE_ENTRY32,
                                             InLoadOrderLinks);

    Status = LdrReadMemory (Process, 
                            LdrEntry, 
                            &LdrDataTableEntry, 
                            sizeof(LdrDataTableEntry));

    if (!NT_SUCCESS (Status)) {
        return Status;
    }

    ModuleInfo->ImageBase = (PVOID)(ULONG_PTR) LdrDataTableEntry.DllBase;
    ModuleInfo->ImageSize = LdrDataTableEntry.SizeOfImage;
    ModuleInfo->Flags     = LdrDataTableEntry.Flags;
    ModuleInfo->LoadCount = LdrDataTableEntry.LoadCount;

    if (!ARGUMENT_PRESENT( Process )) {

        UINT LoopDetectorCount = 500;

        PLIST_ENTRY32 Next1 = (PLIST_ENTRY32)(ULONG_PTR)
            (InitOrderList->Flink);

        while (Next1 != InitOrderList) {
            PLDR_DATA_TABLE_ENTRY32 Entry1 = 
                CONTAINING_RECORD(Next1,
                                  LDR_DATA_TABLE_ENTRY32,
                                  InInitializationOrderLinks);

            ModuleInfo->InitOrderIndex++;

            if ((LdrDataTableEntryPtr == Entry1) ||
                (!LoopDetectorCount--)) 
            {
                break;
            }

            Next1 = (PLIST_ENTRY32)(ULONG_PTR)(Next1->Flink);
        }
    }

    FullDllName.Buffer = (PWSTR)(ULONG_PTR)LdrDataTableEntry.FullDllName.Buffer;
    FullDllName.Length = LdrDataTableEntry.FullDllName.Length;
    FullDllName.MaximumLength = LdrDataTableEntry.FullDllName.MaximumLength;

    Status = LdrGetModuleName(Process, &FullDllName, ModuleInfo, TRUE);

    return Status;
}


PRTL_CRITICAL_SECTION32
LdrQueryModuleInfoLocalLoaderLock32 (
    VOID
    )
{
    return NULL;
}


VOID
LdrQueryModuleInfoLocalLoaderUnlock32 (
    IN PRTL_CRITICAL_SECTION32 LoaderLock)
{
    UNREFERENCED_PARAMETER (LoaderLock);
}

#endif // defined(_WIN64)

typedef 
NTSTATUS
(*PLDR_QUERY_IN_LOAD_ORDER_MODULE_LIST)(
    IN HANDLE Process OPTIONAL,
    IN OUT PLIST_ENTRY* Head,
    IN OUT PLIST_ENTRY* InInitOrderHead OPTIONAL);

typedef NTSTATUS 
(*PLDR_QUERY_NEXT_LIST_ENTRY)(
    IN HANDLE Process OPTIONAL,
    IN PLIST_ENTRY Head,
    IN OUT PLIST_ENTRY* Tail);

typedef 
NTSTATUS
(*PLDR_QUERY_MODULE_INFO_FROM_LDR_ENTRY)(
    IN HANDLE Process OPTIONAL,
    IN PRTL_PROCESS_MODULES ModuleInformation, 
    IN OUT PRTL_PROCESS_MODULE_INFORMATION ModuleInfo,
    IN PLIST_ENTRY LdrEntry,
    IN PLIST_ENTRY InitOrderList);

typedef
PRTL_CRITICAL_SECTION
(*PLDR_QUERY_MODULE_INFO_LOCAL_LOADER_LOCK)(VOID);

typedef
VOID
(*PLDR_QUERY_MODULE_INFO_LOCAL_LOADER_UNLOCK)(PRTL_CRITICAL_SECTION);

static struct {
    PLDR_QUERY_IN_LOAD_ORDER_MODULE_LIST LdrQueryInLoadOrderModuleList;
    PLDR_QUERY_NEXT_LIST_ENTRY LdrQueryNextListEntry;
    PLDR_QUERY_MODULE_INFO_FROM_LDR_ENTRY LdrQueryModuleInfoFromLdrEntry;
    PLDR_QUERY_MODULE_INFO_LOCAL_LOADER_LOCK LdrQueryModuleInfoLocalLoaderLock;
    PLDR_QUERY_MODULE_INFO_LOCAL_LOADER_UNLOCK LdrQueryModuleInfoLocalLoaderUnlock;
} LdrQueryMethods[] = {
    { 
        LdrQueryInLoadOrderModuleList, 
        LdrQueryNextListEntry, 
        LdrQueryModuleInfoFromLdrEntry,
        LdrQueryModuleInfoLocalLoaderLock,
        LdrQueryModuleInfoLocalLoaderUnlock
    }
#if defined(_WIN64)
    ,
    { 
        (PLDR_QUERY_IN_LOAD_ORDER_MODULE_LIST)LdrQueryInLoadOrderModuleList32,
        (PLDR_QUERY_NEXT_LIST_ENTRY)LdrQueryNextListEntry32,
        (PLDR_QUERY_MODULE_INFO_FROM_LDR_ENTRY)LdrQueryModuleInfoFromLdrEntry32,
        (PLDR_QUERY_MODULE_INFO_LOCAL_LOADER_LOCK)LdrQueryModuleInfoLocalLoaderLock32,
        (PLDR_QUERY_MODULE_INFO_LOCAL_LOADER_UNLOCK)LdrQueryModuleInfoLocalLoaderUnlock32
    }
#endif defined(_WIN64)
}; 


NTSTATUS
LdrQueryProcessModuleInformationEx(
    IN HANDLE Process OPTIONAL,
    IN ULONG_PTR Flags OPTIONAL,
    OUT PRTL_PROCESS_MODULES ModuleInformation,
    IN ULONG ModuleInformationLength,
    OUT PULONG ReturnLength OPTIONAL)
{
    NTSTATUS Status = STATUS_UNSUCCESSFUL;
    PRTL_CRITICAL_SECTION LoaderLock = NULL;
    SIZE_T mid;

    ULONG RequiredLength = FIELD_OFFSET( RTL_PROCESS_MODULES, Modules );

    PLIST_ENTRY List;
    PLIST_ENTRY InInitOrderList;

    PRTL_PROCESS_MODULE_INFORMATION ModuleInfo;

    if (ModuleInformationLength < RequiredLength) {
        Status = STATUS_INFO_LENGTH_MISMATCH;
        ModuleInfo = NULL;
    }
    else {
        ModuleInformation->NumberOfModules = 0;
        ModuleInfo = &ModuleInformation->Modules[ 0 ];
        Status = STATUS_SUCCESS;
    }

    for (mid = 0;
         mid < (ARGUMENT_PRESENT( Flags ) ? LDR_NUMBER_OF(LdrQueryMethods) : 1);
         ++mid)
    {
        NTSTATUS Status1;
        PLIST_ENTRY Entry;

        __try {
            UINT LoopDetectorCount = 10240; // allow not more than 10K modules

            if ( !ARGUMENT_PRESENT( Process )) {
                LoaderLock = LdrQueryMethods[mid].LdrQueryModuleInfoLocalLoaderLock();
            } 

            Status1 = LdrQueryMethods[mid].LdrQueryInLoadOrderModuleList(Process, &List, &InInitOrderList);

            if (!NT_SUCCESS( Status1 )) {
                Status = Status1;
                __leave;
            }

            if (!List) {
                __leave;
            }

            Status1 = LdrQueryMethods[mid].LdrQueryNextListEntry(Process, 
                                                                 List, 
                                                                 &Entry);
            if (!NT_SUCCESS( Status1 )) {
                Status = Status1;
                __leave;
            }

            while (Entry != List) {
                if (!LoopDetectorCount--) {
                    Status = STATUS_FAIL_CHECK;
                    __leave;
                }

                RequiredLength += sizeof( RTL_PROCESS_MODULE_INFORMATION );

                if (ModuleInformationLength < RequiredLength) {
                    Status = STATUS_INFO_LENGTH_MISMATCH;
                }
                else {
                    Status1 = LdrQueryMethods[mid].LdrQueryModuleInfoFromLdrEntry(Process, 
                                                                                  ModuleInformation, 
                                                                                  ModuleInfo, 
                                                                                  Entry, InInitOrderList);

                    if (!NT_SUCCESS( Status1 )) {
                        Status = Status1;
                        __leave;
                    }

                    ModuleInfo++;
                }

                //
                // NOTICE-2002/03/15-ELi
                // This chould be non-NULL and not a valid access
                // should check ModuleInfo or ModuleInformationLength instead
                // Assuming ModuleInfo is not NULL when the code can safely
                // reference ModuleInformation->NumberOfModules
                //
                if ((ModuleInfo != NULL) && (ModuleInformation != NULL)) {
                    ModuleInformation->NumberOfModules++;
                }

                Status1 = LdrQueryMethods[mid].LdrQueryNextListEntry(Process, 
                                                                     Entry, 
                                                                     &Entry);

                if (!NT_SUCCESS( Status1 )) {
                    Status = Status1;
                    __leave;
                }

            } // while
        }
        __finally {
            if (LoaderLock) {
                LdrQueryMethods[mid].LdrQueryModuleInfoLocalLoaderUnlock(LoaderLock);
            }

            if (ARGUMENT_PRESENT( ReturnLength )) {
                *ReturnLength = RequiredLength;
            }
        }
    } // for

    return Status;
}


NTSTATUS
LdrQueryProcessModuleInformation(
    OUT PRTL_PROCESS_MODULES ModuleInformation,
    IN ULONG ModuleInformationLength,
    OUT PULONG ReturnLength OPTIONAL)
{
    return LdrQueryProcessModuleInformationEx(NULL,
                                              0,
                                              ModuleInformation,
                                              ModuleInformationLength,
                                              ReturnLength);
}


NTSTATUS
NTAPI
LdrRegisterDllNotification (
    ULONG Flags,
    PLDR_DLL_NOTIFICATION_FUNCTION NotificationFunction,
    PVOID Context,
    PVOID *Cookie
    )
{
    NTSTATUS Status = STATUS_SUCCESS;
    PLDRP_DLL_NOTIFICATION_BLOCK NotificationBlock = NULL;
    BOOLEAN HoldingLoaderLock = FALSE;
    const BOOLEAN InLdrInit = LdrpInLdrInit;
    PVOID LockCookie = NULL;

    __try {
        if (Cookie != NULL) {
            *Cookie = NULL;
        }

        if ((Flags != 0) ||
            (Cookie == NULL) ||
            (NotificationFunction == NULL)) {
            Status = STATUS_INVALID_PARAMETER;
            goto Exit;
        }

        NotificationBlock = (PLDRP_DLL_NOTIFICATION_BLOCK)
                RtlAllocateHeap(RtlProcessHeap(), 0, sizeof(LDRP_DLL_NOTIFICATION_BLOCK));
        if (NotificationBlock == NULL) {
            Status = STATUS_NO_MEMORY;
            goto Exit;
        }

        NotificationBlock->NotificationFunction = NotificationFunction;
        NotificationBlock->Context = Context;

        if (!InLdrInit) {
            __try {
                Status = LdrLockLoaderLock(0, NULL, &LockCookie);
                if (!NT_SUCCESS(Status)) {
                    goto Exit;
                }
                HoldingLoaderLock = TRUE;
            } __except (LdrpGenericExceptionFilter(GetExceptionInformation(), __FUNCTION__)) {
                Status = GetExceptionCode();
                goto Exit;
            }
        }

        InsertTailList(&LdrpDllNotificationList, &NotificationBlock->Links);

        *Cookie = (PVOID) NotificationBlock;
        NotificationBlock = NULL;

        Status = STATUS_SUCCESS;
Exit:
        ;
    } __finally {
        if (HoldingLoaderLock) {
            LdrUnlockLoaderLock(LDR_UNLOCK_LOADER_LOCK_FLAG_RAISE_ON_ERRORS, LockCookie);
            HoldingLoaderLock = FALSE;
        }
        if (NotificationBlock != NULL) {
            RtlFreeHeap(RtlProcessHeap(), 0, NotificationBlock);
        }
    }
    return Status;
}


NTSTATUS
NTAPI
LdrUnregisterDllNotification (
    PVOID Cookie
    )
{
    PLDRP_DLL_NOTIFICATION_BLOCK NotificationBlock;
    NTSTATUS Status;
    BOOLEAN HoldingLoaderLock;
    BOOLEAN InLdrInit;
    PVOID LockCookie;

    if (Cookie == NULL) {
        return STATUS_INVALID_PARAMETER;
    }

    Status = STATUS_SUCCESS;
    HoldingLoaderLock = FALSE;
    InLdrInit = LdrpInLdrInit;
    LockCookie = NULL;

    __try {
        if (!InLdrInit) {
            __try {
                Status = LdrLockLoaderLock (0, NULL, &LockCookie);
                if (!NT_SUCCESS(Status)) {
                    goto Exit;
                }
                HoldingLoaderLock = TRUE;
            } __except (LdrpGenericExceptionFilter(GetExceptionInformation(), __FUNCTION__)) {
                Status = GetExceptionCode();
                goto Exit;
            }
        }

        NotificationBlock = CONTAINING_RECORD(LdrpDllNotificationList.Flink, LDRP_DLL_NOTIFICATION_BLOCK, Links);

        while (&NotificationBlock->Links != &LdrpDllNotificationList) {
            if (NotificationBlock == Cookie)
                break;
            NotificationBlock = CONTAINING_RECORD(NotificationBlock->Links.Flink, LDRP_DLL_NOTIFICATION_BLOCK, Links);
        }

        if (&NotificationBlock->Links != &LdrpDllNotificationList) {
            RemoveEntryList(&NotificationBlock->Links);
            RtlFreeHeap(RtlProcessHeap(), 0, NotificationBlock);
            Status = STATUS_SUCCESS;
        } else {
            Status = STATUS_NOT_FOUND;
        }
Exit:
        ;
    } __finally {
        if (HoldingLoaderLock) {
            LdrUnlockLoaderLock(LDR_UNLOCK_LOADER_LOCK_FLAG_RAISE_ON_ERRORS, LockCookie);
            HoldingLoaderLock = FALSE;
        }
    }
    return Status;
}


VOID
LdrpSendDllNotifications (
    IN PLDR_DATA_TABLE_ENTRY Entry,
    IN ULONG NotificationType,
    IN ULONG Flags
    )
{
    PLIST_ENTRY Next;
    LDR_DLL_NOTIFICATION_DATA Data;

    Data.Loaded.Flags = Flags;
    Data.Loaded.FullDllName = &Entry->FullDllName;
    Data.Loaded.BaseDllName = &Entry->BaseDllName;
    Data.Loaded.DllBase = Entry->DllBase;
    Data.Loaded.SizeOfImage = Entry->SizeOfImage;

    Next = LdrpDllNotificationList.Flink;

    while (Next != &LdrpDllNotificationList) {
        PLDRP_DLL_NOTIFICATION_BLOCK Block = CONTAINING_RECORD(Next, LDRP_DLL_NOTIFICATION_BLOCK, Links);
        __try {
            (*Block->NotificationFunction)(NotificationType, &Data, Block->Context);
        } __except (LdrpGenericExceptionFilter(GetExceptionInformation(), __FUNCTION__)) {
            // just go on to the next one...
        }
        Next = Next->Flink;
    }
}


BOOLEAN
NTAPI
RtlDllShutdownInProgress (
    VOID
    )
/*++

Routine Description:

    This routine returns the status of DLL shutdown.

Arguments:

    None

Return Value:

    BOOLEAN - TRUE: Shutdown is in progress, FALSE: Shutdown is not currently in progress.

--*/
{
    if (LdrpShutdownInProgress) {
        return TRUE;
    } else {
        return FALSE;
    }
}


NTSTATUS
NTAPI
LdrLockLoaderLock (
    ULONG Flags,
    PULONG Disposition,
    PVOID *Cookie
    )
{
    NTSTATUS Status;
    BOOLEAN InLdrInit;

    InLdrInit = LdrpInLdrInit;

    if (Disposition != NULL) {
        *Disposition = LDR_LOCK_LOADER_LOCK_DISPOSITION_INVALID;
    }

    if (Cookie != NULL) {
        *Cookie = NULL;
    }

    if ((Flags & ~(LDR_LOCK_LOADER_LOCK_FLAG_TRY_ONLY | LDR_LOCK_LOADER_LOCK_FLAG_RAISE_ON_ERRORS)) != 0) {

        if (Flags & LDR_LOCK_LOADER_LOCK_FLAG_RAISE_ON_ERRORS) {
            RtlRaiseStatus(STATUS_INVALID_PARAMETER_1);
        }

        Status = STATUS_INVALID_PARAMETER_1;
        goto Exit;
    }

    if (Cookie == NULL) {
        if (Flags & LDR_LOCK_LOADER_LOCK_FLAG_RAISE_ON_ERRORS) {
            RtlRaiseStatus (STATUS_INVALID_PARAMETER_3);
        }

        Status = STATUS_INVALID_PARAMETER_3;
        goto Exit;
    }

    //
    // If you hit this assertion failure, you specified that you only wanted to
    // try acquiring the lock, but you forgot to specify a Disposition out where
    // this function could indicate whether the lock was actually acquired.
    //

    ASSERT((Disposition != NULL) || !(Flags & LDR_LOCK_LOADER_LOCK_FLAG_TRY_ONLY));

    if ((Flags & LDR_LOCK_LOADER_LOCK_FLAG_TRY_ONLY) &&
        (Disposition == NULL)) {

        if (Flags & LDR_LOCK_LOADER_LOCK_FLAG_RAISE_ON_ERRORS) {
            RtlRaiseStatus(STATUS_INVALID_PARAMETER_2);
        }

        Status = STATUS_INVALID_PARAMETER_2;
        goto Exit;
    }

    if (InLdrInit) {
        Status = STATUS_SUCCESS;
        goto Exit;
    }

    if (Flags & LDR_LOCK_LOADER_LOCK_FLAG_RAISE_ON_ERRORS) {
        if (Flags & LDR_LOCK_LOADER_LOCK_FLAG_TRY_ONLY) {
            if (RtlTryEnterCriticalSection(&LdrpLoaderLock)) {
                *Cookie = (PVOID) MAKE_LOADER_LOCK_COOKIE(LOADER_LOCK_COOKIE_TYPE_NORMAL, InterlockedIncrement(&LdrpLoaderLockAcquisitionCount));
                *Disposition = LDR_LOCK_LOADER_LOCK_DISPOSITION_LOCK_ACQUIRED;
            } else {
                *Disposition = LDR_LOCK_LOADER_LOCK_DISPOSITION_LOCK_NOT_ACQUIRED;
            }
        } else {
            RtlEnterCriticalSection(&LdrpLoaderLock);
            if (Disposition != NULL) {
                *Disposition = LDR_LOCK_LOADER_LOCK_DISPOSITION_LOCK_ACQUIRED;
            }
            *Cookie = (PVOID) MAKE_LOADER_LOCK_COOKIE(LOADER_LOCK_COOKIE_TYPE_NORMAL, InterlockedIncrement(&LdrpLoaderLockAcquisitionCount));
        }
    } else {
        __try {
            if (Flags & LDR_LOCK_LOADER_LOCK_FLAG_TRY_ONLY) {
                if (RtlTryEnterCriticalSection(&LdrpLoaderLock)) {
                    *Disposition = LDR_LOCK_LOADER_LOCK_DISPOSITION_LOCK_ACQUIRED;
                    *Cookie = (PVOID) MAKE_LOADER_LOCK_COOKIE(LOADER_LOCK_COOKIE_TYPE_NORMAL, InterlockedIncrement(&LdrpLoaderLockAcquisitionCount));
                } else {
                    *Disposition = LDR_LOCK_LOADER_LOCK_DISPOSITION_LOCK_NOT_ACQUIRED;
                }
            } else {
                RtlEnterCriticalSection(&LdrpLoaderLock);
                if (Disposition != NULL) {
                    *Disposition = LDR_LOCK_LOADER_LOCK_DISPOSITION_LOCK_ACQUIRED;
                }
                *Cookie = (PVOID) MAKE_LOADER_LOCK_COOKIE(LOADER_LOCK_COOKIE_TYPE_NORMAL, InterlockedIncrement(&LdrpLoaderLockAcquisitionCount));
            }
        } __except (LdrpGenericExceptionFilter(GetExceptionInformation(), __FUNCTION__)) {
            Status = GetExceptionCode();
            DbgPrintEx(
                DPFLTR_LDR_ID,
                LDR_ERROR_DPFLTR,
                "LDR: %s - Caught exception %08lx\n",
                __FUNCTION__,
                Status);
            goto Exit;
        }
    }

    Status = STATUS_SUCCESS;
Exit:
    return Status;
}


NTSTATUS
NTAPI
LdrUnlockLoaderLock(
    ULONG Flags,
    PVOID CookieIn
    )
{
    NTSTATUS Status;
    const ULONG_PTR Cookie = (ULONG_PTR) CookieIn;

    if ((Flags & ~(LDR_UNLOCK_LOADER_LOCK_FLAG_RAISE_ON_ERRORS)) != 0) {
        if (Flags & LDR_UNLOCK_LOADER_LOCK_FLAG_RAISE_ON_ERRORS)
            RtlRaiseStatus(STATUS_INVALID_PARAMETER_1);

        Status = STATUS_INVALID_PARAMETER_1;
        goto Exit;
    }

    if (CookieIn == NULL) {
        Status = STATUS_SUCCESS;
        goto Exit;
    }

    // A little validation on the cookie...
    if (EXTRACT_LOADER_LOCK_COOKIE_TYPE(Cookie) != LOADER_LOCK_COOKIE_TYPE_NORMAL) {
        if (Flags & LDR_UNLOCK_LOADER_LOCK_FLAG_RAISE_ON_ERRORS)
            RtlRaiseStatus(STATUS_INVALID_PARAMETER_2);

        Status = STATUS_INVALID_PARAMETER_2;
        goto Exit;
    }

    if (EXTRACT_LOADER_LOCK_COOKIE_TID(Cookie) != (HandleToUlong(NtCurrentTeb()->ClientId.UniqueThread) & LOADER_LOCK_COOKIE_TID_BIT_MASK)) {
        if (Flags & LDR_UNLOCK_LOADER_LOCK_FLAG_RAISE_ON_ERRORS)
            RtlRaiseStatus(STATUS_INVALID_PARAMETER_2);

        Status = STATUS_INVALID_PARAMETER_2;
        goto Exit;
    }

    if (Flags & LDR_UNLOCK_LOADER_LOCK_FLAG_RAISE_ON_ERRORS) {
        RtlLeaveCriticalSection(&LdrpLoaderLock);
    } else {
        __try {
            RtlLeaveCriticalSection(&LdrpLoaderLock);
        } __except (LdrpGenericExceptionFilter(GetExceptionInformation(), __FUNCTION__)) {
            Status = GetExceptionCode();
            goto Exit;
        }
    }

    Status = STATUS_SUCCESS;
Exit:
    return Status;
}


NTSTATUS
NTAPI
LdrDoesCurrentThreadOwnLoaderLock(
    BOOLEAN *DoesOwnLock
    )
{
    NTSTATUS Status;
    PTEB Teb;

    if (DoesOwnLock != NULL)
        *DoesOwnLock = FALSE;

    if (DoesOwnLock == NULL) {
        Status = STATUS_INVALID_PARAMETER;
        goto Exit;
    }

    Teb = NtCurrentTeb();

    if (LdrpLoaderLock.OwningThread == Teb->ClientId.UniqueThread)
        *DoesOwnLock = TRUE;

    Status = STATUS_SUCCESS;
Exit:
    return Status;
}


NTSTATUS
NTAPI
LdrEnumerateLoadedModules (
    ULONG Flags,
    PLDR_LOADED_MODULE_ENUMERATION_CALLBACK_FUNCTION CallbackFunction,
    PVOID Context
    )
{
    NTSTATUS Status;
    BOOLEAN LoaderLockLocked = FALSE;
    PLIST_ENTRY LoadOrderListHead = NULL;
    PLIST_ENTRY ListEntry;
    BOOLEAN StopEnumeration = FALSE;
    PVOID   LockCookie = NULL;

    if ((Flags != 0) ||
        (CallbackFunction == NULL)) {
        Status = STATUS_INVALID_PARAMETER;
        goto Exit;
    }

    Status = LdrLockLoaderLock(0, NULL, &LockCookie);
    if (!NT_SUCCESS(Status))
        goto Exit;

    LoaderLockLocked = TRUE;
    LoadOrderListHead = &PebLdr.InLoadOrderModuleList;

    ListEntry = LoadOrderListHead->Flink;

    while (ListEntry != LoadOrderListHead) {
        __try {
            (*CallbackFunction)(
                CONTAINING_RECORD(ListEntry, LDR_DATA_TABLE_ENTRY, InLoadOrderLinks),
                Context,
                &StopEnumeration);
        } __except (LdrpGenericExceptionFilter(GetExceptionInformation(), __FUNCTION__)) {
            Status = GetExceptionCode();
            goto Exit;
        }

        if (StopEnumeration)
            break;

        ListEntry = ListEntry->Flink;
    }

    Status = LdrUnlockLoaderLock(0, LockCookie);
    LoaderLockLocked = FALSE;

    if (!NT_SUCCESS(Status)) {
        goto Exit;
    }

    Status = STATUS_SUCCESS;

Exit:

    if (LoaderLockLocked) {

        NTSTATUS Status2;
                
        Status2 = LdrUnlockLoaderLock(0, LockCookie);

        ASSERT(NT_SUCCESS(Status2));
    }

    return Status;
}


NTSTATUS
NTAPI
LdrAddRefDll(
    ULONG               Flags,
    PVOID               DllHandle
    )
{
    NTSTATUS Status = STATUS_SUCCESS;
    PLDR_DATA_TABLE_ENTRY LdrDataTableEntry = NULL;
    const BOOLEAN InLdrInit = LdrpInLdrInit;
    PVOID LockCookie = NULL;
    BOOLEAN HoldingLoaderLock = FALSE;
    const ULONG ValidFlags = LDR_ADDREF_DLL_PIN;

    __try {

        if (Flags & ~ValidFlags
            ) {
            Status = STATUS_INVALID_PARAMETER;
            goto Exit;
        }
        if (!InLdrInit
            ) {
            Status = LdrLockLoaderLock(0, NULL, &LockCookie);
            if (!NT_SUCCESS(Status))
                goto Exit;
            HoldingLoaderLock = TRUE;
        }
        if (!LdrpCheckForLoadedDllHandle(DllHandle, &LdrDataTableEntry)
            ) {
            Status = STATUS_INVALID_PARAMETER;
            goto Exit;
        }
        if (!RTL_SOFT_VERIFY(LdrDataTableEntry != NULL)
            ) {
            Status = STATUS_INTERNAL_ERROR;
            goto Exit;
        }
        //
        // Gross. Everyone inlines the first part..
        //
        if (LdrDataTableEntry->LoadCount != 0xffff) {
            if (Flags & LDR_ADDREF_DLL_PIN
                ) {
                LdrDataTableEntry->LoadCount = 0xffff;
                LdrpPinLoadedDll(LdrDataTableEntry);
            } else {
                LdrDataTableEntry->LoadCount++;
                LdrpReferenceLoadedDll(LdrDataTableEntry);
            }
            LdrpClearLoadInProgress();
        }
Exit:
        if (LdrpShouldDbgPrintStatus(Status)
            ) {
            DbgPrint("LDR: "__FUNCTION__"(%p) 0x%08lx\n", DllHandle, Status);
        }
    } __finally {
        if (HoldingLoaderLock) {
            LdrUnlockLoaderLock(LDR_UNLOCK_LOADER_LOCK_FLAG_RAISE_ON_ERRORS, LockCookie);
            HoldingLoaderLock = FALSE;
        }
    }
    return Status;
}


VOID
NTAPI
LdrSetDllManifestProber(
    IN PLDR_MANIFEST_PROBER_ROUTINE ManifestProberRoutine
    )
{
    LdrpManifestProberRoutine = ManifestProberRoutine;
}


NTSTATUS
NTAPI
LdrSetAppCompatDllRedirectionCallback(
    IN ULONG Flags,
    IN PLDR_APP_COMPAT_DLL_REDIRECTION_CALLBACK_FUNCTION CallbackFunction,
    IN PVOID CallbackData
    )
/*++

Routine Description:

    This routine allows the application compatibility facility to set a callback
    function that it can use to redirect DLL loads wherever it wants them to go.

Arguments:

    Flags - None defined now; must be zero.

    CallbackFunction - Function pointer to function which is called to resolve
        path names prior to actually loading the DLL.

    CallbackData - PVOID value passed through to the CallbackFunction when it is
        called.

Return Value:

    NTSTATUS indicating the success/failure of the function.

--*/
{
    NTSTATUS st = STATUS_INTERNAL_ERROR;
    PVOID LockCookie = NULL;

    if (Flags != 0) {
        st = STATUS_INVALID_PARAMETER;
        goto Exit;
    }

    LdrLockLoaderLock(LDR_LOCK_LOADER_LOCK_FLAG_RAISE_ON_ERRORS, NULL, &LockCookie);
    __try {
        LdrpAppCompatDllRedirectionCallbackFunction = CallbackFunction;
        LdrpAppCompatDllRedirectionCallbackData = CallbackData;
    } __finally {
        LdrUnlockLoaderLock(LDR_UNLOCK_LOADER_LOCK_FLAG_RAISE_ON_ERRORS, LockCookie);
    }

    st = STATUS_SUCCESS;
Exit:
    return st;
}

PTEB LdrpTopLevelDllBeingLoadedTeb=NULL;


BOOLEAN
RtlIsThreadWithinLoaderCallout (
    VOID
    )
{
    if (LdrpTopLevelDllBeingLoadedTeb == NtCurrentTeb ()) {
        return TRUE;
    } else {
        return FALSE;
    }
}
