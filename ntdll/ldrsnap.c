/*++

Copyright (c) Microsoft Corporation

Module Name:

    ldrsnap.c

Abstract:

    This module implements the guts of the Ldr Dll Snap Routine.
    This code is executed only in user mode; the kernel mode
    loader is implemented as part of the memory manager kernel
    component.

Author:

    Mike O'Leary (mikeol) 23-Mar-1990

Revision History:

    Michael Grier (mgrier) 5/4/2000

        Isolate static (import) library loads when activation contexts
        are used to redirect so that a dynamically loaded library
        does not bind to whatever component dll may already be
        loaded for the process.  When redirection is in effect,
        the full path names of the loads must match, not just the
        base names of the dlls.

        Also clean up path allocation policy so that we should be
        clean for 64k paths in the loader.

--*/

#define LDRDBG 0

#pragma warning(disable:4214)   // bit field types other than int
#pragma warning(disable:4201)   // nameless struct/union
#pragma warning(disable:4221)   // use automatic variable for initialization
#pragma warning(disable:4204)   // non-constant aggregate initializer
#pragma warning(disable:4115)   // named type definition in parentheses
#pragma warning(disable:4127)   // condition expression is constant

#include "ntos.h"
#include <nt.h>
#include <ntrtl.h>
#include <nturtl.h>
#include <heap.h>
#include <winsnmp.h>
#include <winsafer.h>
#include "ldrp.h"
#include "ntdllp.h"
#include <sxstypes.h>
#include <ntrtlpath.h>

#define DLL_EXTENSION L".DLL"
#define DLL_REDIRECTION_LOCAL_SUFFIX L".Local"
UNICODE_STRING DefaultExtension = RTL_CONSTANT_STRING(L".DLL");
UNICODE_STRING User32String = RTL_CONSTANT_STRING(L"user32.dll");
UNICODE_STRING Kernel32String = RTL_CONSTANT_STRING(L"kernel32.dll");

#if DBG // DBG
LARGE_INTEGER MapBeginTime, MapEndTime, MapElapsedTime;
#endif // DBG

PCUNICODE_STRING LdrpTopLevelDllBeingLoaded;
BOOLEAN LdrpShowInitRoutines = FALSE;

#if defined(_WIN64)
extern ULONG UseWOW64;
#endif


#if defined (_X86_)
extern PVOID LdrpLockPrefixTable;
extern PVOID __safe_se_handler_table[]; /* base of safe handler entry table */
extern BYTE  __safe_se_handler_count;   /* absolute symbol whose address is
                                           the count of table entries */
//
// Specify address of kernel32 lock prefixes
//
IMAGE_LOAD_CONFIG_DIRECTORY _load_config_used = {
    sizeof(_load_config_used),     // size
    0,                             // Reserved
    0,                             // Reserved
    0,                             // Reserved
    0,                             // GlobalFlagsClear
    0,                             // GlobalFlagsSet
    0,                             // CriticalSectionTimeout (milliseconds)
    0,                             // DeCommitFreeBlockThreshold
    0,                             // DeCommitTotalFreeThreshold
    (ULONG_PTR) &LdrpLockPrefixTable,  // LockPrefixTable
    0, 0, 0, 0, 0, 0, 0,            // Reserved
    0,                             // & security_cookie
    (ULONG_PTR)__safe_se_handler_table,
    (ULONG_PTR)&__safe_se_handler_count
};


VOID
LdrpValidateImageForMp (
    IN PLDR_DATA_TABLE_ENTRY LdrDataTableEntry
    )
{
    PIMAGE_LOAD_CONFIG_DIRECTORY ImageConfigData;
    ULONG i;
    PUCHAR *pb;
    ULONG ErrorParameters;
    ULONG ErrorResponse;

    //
    // If we are on an MP system and the DLL has image config info,
    // check to see if it has a lock prefix table and make sure the
    // locks have not been converted to NOPs.
    //

    ImageConfigData = RtlImageDirectoryEntryToData (LdrDataTableEntry->DllBase,
                                                    TRUE,
                                                    IMAGE_DIRECTORY_ENTRY_LOAD_CONFIG,
                                                    &i);

    if (ImageConfigData != NULL &&
        (i >= RTL_SIZEOF_THROUGH_FIELD(IMAGE_LOAD_CONFIG_DIRECTORY, LockPrefixTable)) &&
        ImageConfigData->LockPrefixTable) {

        pb = (PUCHAR *)ImageConfigData->LockPrefixTable;

        while (*pb) {

            if (**pb == (UCHAR)0x90) {

                if (LdrpNumberOfProcessors > 1) {

                    //
                    // Hard error time. One of the known DLLs is corrupt !
                    //

                    ErrorParameters = (ULONG)&LdrDataTableEntry->BaseDllName;

                    NtRaiseHardError (STATUS_IMAGE_MP_UP_MISMATCH,
                                      1,
                                      1,
                                      &ErrorParameters,
                                      OptionOk,
                                      &ErrorResponse);

                    if (LdrpInLdrInit) {
                        LdrpFatalHardErrorCount += 1;
                    }
                }
            }
            pb += 1;
        }
    }
}
#endif

NTSTATUS
LdrpWalkImportDescriptor (
    IN PCWSTR DllPath OPTIONAL,
    IN PLDR_DATA_TABLE_ENTRY LdrDataTableEntry
    );


NTSTATUS
LdrpLoadImportModule (
    IN PCWSTR DllPath OPTIONAL,
    IN PCSTR ImportName,
    OUT PLDR_DATA_TABLE_ENTRY *DataTableEntry,
    OUT PBOOLEAN AlreadyLoaded
    )
{
    NTSTATUS st;
    ANSI_STRING AnsiString;
    PUNICODE_STRING ImportDescriptorName_U;
    WCHAR StaticRedirectedDllNameBuffer[DOS_MAX_PATH_LENGTH];
    UNICODE_STRING StaticRedirectedDllName;
    UNICODE_STRING DynamicRedirectedDllName;
    BOOLEAN Redirected;
    
    Redirected = FALSE;
    DynamicRedirectedDllName.Buffer = NULL;

    ImportDescriptorName_U = &NtCurrentTeb()->StaticUnicodeString;

    RtlInitAnsiString (&AnsiString, ImportName);

    st = RtlAnsiStringToUnicodeString (ImportDescriptorName_U,
                                       &AnsiString,
                                       FALSE);
    if (!NT_SUCCESS(st)) {
        goto Exit;
    }

    //
    // If the module name has no '.' in the name then it can't have
    // an extension.  Add .dll in this case as 9x does this and some
    // apps rely on it.
    //

    if (strchr (ImportName, '.') == NULL) {
        RtlAppendUnicodeToString (ImportDescriptorName_U, L".dll");
    }

    RtlInitUnicodeString (&DynamicRedirectedDllName, NULL);

    StaticRedirectedDllName.Length = 0;
    StaticRedirectedDllName.MaximumLength = sizeof(StaticRedirectedDllNameBuffer);
    StaticRedirectedDllName.Buffer = StaticRedirectedDllNameBuffer;

    st = RtlDosApplyFileIsolationRedirection_Ustr(
                RTL_DOS_APPLY_FILE_REDIRECTION_USTR_FLAG_RESPECT_DOT_LOCAL,
                ImportDescriptorName_U,
                &DefaultExtension,
                &StaticRedirectedDllName,
                &DynamicRedirectedDllName,
                &ImportDescriptorName_U,
                NULL,
                NULL,
                NULL);

    if (NT_SUCCESS(st)){
        Redirected = TRUE;
    } else if (st != STATUS_SXS_KEY_NOT_FOUND) {
        if (ShowSnaps) {
            DbgPrint("LDR: %s - RtlDosApplyFileIsolationRedirection_Ustr failed with status %x\n", __FUNCTION__, st);
        }

        goto Exit;
    }

    st = STATUS_SUCCESS;

    //
    // Check the LdrTable to see if the DLL has already been mapped
    // into this image. If not, map it.
    //

    if (LdrpCheckForLoadedDll (DllPath,
                               ImportDescriptorName_U,
                               TRUE,
                               Redirected,
                               DataTableEntry)) {
        *AlreadyLoaded = TRUE;
    } else {
        *AlreadyLoaded = FALSE;

        st = LdrpMapDll (DllPath,
                         ImportDescriptorName_U->Buffer,
                         NULL,       // MOOCOW
                         TRUE,
                         Redirected,
                         DataTableEntry);

        if (!NT_SUCCESS(st)) {
            if (ShowSnaps) {
                DbgPrint("LDR: %s - LdrpMapDll(%p, %ls, NULL, TRUE, %d, %p) failed with status %x\n", __FUNCTION__, DllPath, ImportDescriptorName_U->Buffer, Redirected, DataTableEntry, st);
            }

            goto Exit;
        }
        
        //
        // Register dll with the stack tracing module.
        // This is used for getting reliable stack traces on X86.
        //

#if defined(_X86_)
        RtlpStkMarkDllRange (*DataTableEntry);
#endif

        st = LdrpWalkImportDescriptor (DllPath, *DataTableEntry);

        if (!NT_SUCCESS(st)) {

            if (ShowSnaps) {
                DbgPrint("LDR: %s - LdrpWalkImportDescriptor [dll %ls]  failed with status %x\n", __FUNCTION__, ImportDescriptorName_U->Buffer, st);
            }

            InsertTailList(&PebLdr.InInitializationOrderModuleList,
                           &(*DataTableEntry)->InInitializationOrderLinks);
        }
    }

Exit:
    if (DynamicRedirectedDllName.Buffer != NULL) {
        RtlFreeUnicodeString (&DynamicRedirectedDllName);
    }

    return st;
}


NTSTATUS
LdrpHandleOneNewFormatImportDescriptor (
    IN PCWSTR DllPath OPTIONAL,
    IN PLDR_DATA_TABLE_ENTRY LdrDataTableEntry,
    PCIMAGE_BOUND_IMPORT_DESCRIPTOR *NewImportDescriptorInOut,
    PCSZ NewImportStringBase
    )
{
    NTSTATUS st;
    PCIMAGE_BOUND_IMPORT_DESCRIPTOR NewImportDescriptor = *NewImportDescriptorInOut;
    PCIMAGE_BOUND_FORWARDER_REF NewImportForwarder;
    PCSZ ImportName;
    PCSZ NewImportName;
    PCSZ NewFwdImportName;
    BOOLEAN AlreadyLoaded = FALSE;
    BOOLEAN StaleBinding = FALSE;
    PLDR_DATA_TABLE_ENTRY DataTableEntry, FwdDataTableEntry;
    ULONG i;
    PCIMAGE_IMPORT_DESCRIPTOR ImportDescriptor;
    ULONG ImportSize;

    NewImportName = NewImportStringBase + NewImportDescriptor->OffsetModuleName;

    if (ShowSnaps) {
        DbgPrint("LDR: %wZ bound to %s\n", &LdrDataTableEntry->BaseDllName, NewImportName);
    }

    st = LdrpLoadImportModule (DllPath,
                               NewImportName,
                               &DataTableEntry,
                               &AlreadyLoaded);

    if (!NT_SUCCESS(st)) {
        if (ShowSnaps)
            DbgPrint("LDR: %wZ failed to load import module %s; status = %x\n", &LdrDataTableEntry->BaseDllName, NewImportName, st);

        goto Exit;
    }

    //
    // Add to initialization list.
    //

    if (!AlreadyLoaded) {
        InsertTailList (&PebLdr.InInitializationOrderModuleList,
                        &DataTableEntry->InInitializationOrderLinks);
    }

    if ((NewImportDescriptor->TimeDateStamp != DataTableEntry->TimeDateStamp) ||
        (DataTableEntry->Flags & LDRP_IMAGE_NOT_AT_BASE)) {

        if (ShowSnaps) {
            DbgPrint("LDR: %wZ has stale binding to %s\n", &LdrDataTableEntry->BaseDllName, NewImportName);
        }

        StaleBinding = TRUE;
    } else {
#if DBG
        LdrpSnapBypass += 1;
#endif
        if (ShowSnaps) {
            DbgPrint("LDR: %wZ has correct binding to %s\n", &LdrDataTableEntry->BaseDllName, NewImportName);
        }

        StaleBinding = FALSE;
    }

    NewImportForwarder = (PCIMAGE_BOUND_FORWARDER_REF) (NewImportDescriptor + 1);
    for (i=0; i<NewImportDescriptor->NumberOfModuleForwarderRefs; i++) {
        NewFwdImportName = NewImportStringBase + NewImportForwarder->OffsetModuleName;
        if (ShowSnaps) {
            DbgPrint("LDR: %wZ bound to %s via forwarder(s) from %wZ\n",
                &LdrDataTableEntry->BaseDllName,
                NewFwdImportName,
                &DataTableEntry->BaseDllName);
        }

        st = LdrpLoadImportModule (DllPath,
                                   NewFwdImportName,
                                   &FwdDataTableEntry,
                                   &AlreadyLoaded);

        if ( NT_SUCCESS(st) ) {
            if (!AlreadyLoaded) {
                InsertTailList (&PebLdr.InInitializationOrderModuleList,
                                &FwdDataTableEntry->InInitializationOrderLinks);
            }
        }

        if ( (!NT_SUCCESS(st)) ||
             (NewImportForwarder->TimeDateStamp != FwdDataTableEntry->TimeDateStamp) ||
             (FwdDataTableEntry->Flags & LDRP_IMAGE_NOT_AT_BASE)) {

            if (ShowSnaps) {
                DbgPrint("LDR: %wZ has stale binding to %s\n", &LdrDataTableEntry->BaseDllName, NewFwdImportName);
            }

            StaleBinding = TRUE;
        } else {
#if DBG
            LdrpSnapBypass += 1;
#endif
            if (ShowSnaps) {
                DbgPrint("LDR: %wZ has correct binding to %s\n",
                        &LdrDataTableEntry->BaseDllName,
                        NewFwdImportName);
            }
        }

        NewImportForwarder += 1;
    }

    NewImportDescriptor = (PCIMAGE_BOUND_IMPORT_DESCRIPTOR) NewImportForwarder;

    if (StaleBinding) {
#if DBG
        LdrpNormalSnap += 1;
#endif
        //
        // Find the unbound import descriptor that matches this bound
        // import descriptor
        //

        ImportDescriptor = (PCIMAGE_IMPORT_DESCRIPTOR)RtlImageDirectoryEntryToData(
                            LdrDataTableEntry->DllBase,
                            TRUE,
                            IMAGE_DIRECTORY_ENTRY_IMPORT,
                            &ImportSize);

        ImportName = NULL;
        while (ImportDescriptor->Name != 0) {
            ImportName = (PCSZ)((ULONG_PTR)LdrDataTableEntry->DllBase + ImportDescriptor->Name);
            if (_stricmp(ImportName, NewImportName) == 0) {
                break;
            }

            ImportDescriptor += 1;
        }

        if (ImportDescriptor->Name == 0) {
            if (ShowSnaps) {
                DbgPrint("LDR: LdrpWalkImportTable - failing with STATUS_OBJECT_NAME_INVALID due to no import descriptor name\n");
            }

            st = STATUS_OBJECT_NAME_INVALID;
            goto Exit;
        }

        if (ShowSnaps) {
            DbgPrint("LDR: Stale Bind %s from %wZ\n", ImportName, &LdrDataTableEntry->BaseDllName);
        }

        st = LdrpSnapIAT (DataTableEntry,
                          LdrDataTableEntry,
                          ImportDescriptor,
                          FALSE);

        if (!NT_SUCCESS(st)) {
            if (ShowSnaps) {
                DbgPrint("LDR: LdrpWalkImportTable - LdrpSnapIAT failed with status %x\n", st);
            }
            
            goto Exit;
        }
    }

    st = STATUS_SUCCESS;

Exit:
    *NewImportDescriptorInOut = NewImportDescriptor;
    return st;
}


NTSTATUS
LdrpHandleNewFormatImportDescriptors (
    IN PCWSTR DllPath OPTIONAL,
    IN PLDR_DATA_TABLE_ENTRY LdrDataTableEntry,
    PCIMAGE_BOUND_IMPORT_DESCRIPTOR NewImportDescriptor
    )
{
    NTSTATUS st;
    PCSZ NewImportStringBase;

    NewImportStringBase = (PCSZ) NewImportDescriptor;

    while (NewImportDescriptor->OffsetModuleName) {

        st = LdrpHandleOneNewFormatImportDescriptor (DllPath,
                                                     LdrDataTableEntry,
                                                     &NewImportDescriptor,
                                                     NewImportStringBase);

        if (!NT_SUCCESS(st)) {
            return st;
        }
    }

    return STATUS_SUCCESS;
}


NTSTATUS
LdrpHandleOneOldFormatImportDescriptor (
    IN PCWSTR DllPath OPTIONAL,
    IN PLDR_DATA_TABLE_ENTRY LdrDataTableEntry,
    PCIMAGE_IMPORT_DESCRIPTOR *ImportDescriptorInOut
    )
{
    NTSTATUS st;
    PCIMAGE_IMPORT_DESCRIPTOR ImportDescriptor = *ImportDescriptorInOut;
    PIMAGE_THUNK_DATA FirstThunk = NULL;
    PCSTR ImportName = NULL;
    PLDR_DATA_TABLE_ENTRY DataTableEntry = NULL;
    BOOLEAN AlreadyLoaded = FALSE;
    BOOLEAN SnapForwardersOnly = FALSE;

    ImportName = (PCSZ)((ULONG_PTR)LdrDataTableEntry->DllBase + ImportDescriptor->Name);

    //
    // check for import that has no references
    //

    FirstThunk = (PIMAGE_THUNK_DATA) ((ULONG_PTR)LdrDataTableEntry->DllBase + ImportDescriptor->FirstThunk);

    if (FirstThunk->u1.Function != 0) {

        if (ShowSnaps) {
            DbgPrint("LDR: %s used by %wZ\n", ImportName, &LdrDataTableEntry->BaseDllName);
        }

        st = LdrpLoadImportModule (DllPath,
                                   ImportName,
                                   &DataTableEntry,
                                   &AlreadyLoaded);
        if (!NT_SUCCESS(st)) {
            if (ShowSnaps)
                DbgPrint("LDR: LdrpWalkImportTable - LdrpLoadImportModule failed on import %s with status %x\n", ImportName, st);

            goto Exit;
        }

        if (ShowSnaps) {
            DbgPrint("LDR: Snapping imports for %wZ from %s\n", &LdrDataTableEntry->BaseDllName, ImportName);
        }

        //
        // If the image has been bound and the import date stamp
        // matches the date time stamp in the export modules header,
        // and the image was mapped at it's prefered base address,
        // then we are done.
        //

        SnapForwardersOnly = FALSE;

#if DBG
        LdrpNormalSnap++;
#endif
        //
        // Add to initialization list.
        //

        if (!AlreadyLoaded) {
            InsertTailList (&PebLdr.InInitializationOrderModuleList,
                            &DataTableEntry->InInitializationOrderLinks);
        }

        st = LdrpSnapIAT (DataTableEntry,
                          LdrDataTableEntry,
                          ImportDescriptor,
                          SnapForwardersOnly);

        if (!NT_SUCCESS(st)) {
            if (ShowSnaps) {
                DbgPrint("LDR: LdrpWalkImportTable - LdrpSnapIAT #2 failed with status %x\n", st);
            }

            goto Exit;
        }
    }

    ImportDescriptor += 1;

    st = STATUS_SUCCESS;
Exit:
    *ImportDescriptorInOut = ImportDescriptor;
    return st;
}


NTSTATUS
LdrpHandleOldFormatImportDescriptors(
    IN PCWSTR DllPath OPTIONAL,
    IN PLDR_DATA_TABLE_ENTRY LdrDataTableEntry,
    IN PCIMAGE_IMPORT_DESCRIPTOR ImportDescriptor
    )
{
    NTSTATUS st = STATUS_INTERNAL_ERROR;

    //
    // For each DLL used by this DLL, load the dll. Then snap
    // the IAT, and call the DLL's init routine.
    //

    while (ImportDescriptor->Name && ImportDescriptor->FirstThunk) {
        st = LdrpHandleOneOldFormatImportDescriptor(DllPath, LdrDataTableEntry, &ImportDescriptor);
        if (!NT_SUCCESS(st))
            goto Exit;
    }

    st = STATUS_SUCCESS;
Exit:
    return st;
}


NTSTATUS
LdrpMungHeapImportsForTagging (
    IN PLDR_DATA_TABLE_ENTRY LdrDataTableEntry
    )
{
    NTSTATUS st = STATUS_INTERNAL_ERROR;
    PVOID IATBase;
    SIZE_T BigIATSize;
    ULONG  LittleIATSize;
    PVOID *ProcAddresses;
    ULONG NumberOfProcAddresses;
    ULONG OldProtect;
    USHORT TagIndex;

    //
    // Determine the location and size of the IAT.  If found, scan the
    // IAT address to see if any are pointing to RtlAllocateHeap.  If so
    // replace when with a pointer to a unique thunk function that will
    // replace the tag with a unique tag for this image.
    //

    IATBase = RtlImageDirectoryEntryToData (LdrDataTableEntry->DllBase,
                                            TRUE,
                                            IMAGE_DIRECTORY_ENTRY_IAT,
                                            &LittleIATSize);

    if (IATBase == NULL) {
        return STATUS_SUCCESS;
    }

    BigIATSize = LittleIATSize;

    st = NtProtectVirtualMemory (NtCurrentProcess(),
                                 &IATBase,
                                 &BigIATSize,
                                 PAGE_READWRITE,
                                 &OldProtect);

    if (!NT_SUCCESS(st)) {
        DbgPrint( "LDR: Unable to unprotect IAT to enable tagging by DLL.\n");
        return STATUS_SUCCESS;
    }

    ProcAddresses = (PVOID *)IATBase;
    NumberOfProcAddresses = (ULONG)(BigIATSize / sizeof(PVOID));

    while (NumberOfProcAddresses--) {
        if (*ProcAddresses == RtlAllocateHeap) {
            *ProcAddresses = LdrpDefineDllTag(LdrDataTableEntry->BaseDllName.Buffer, &TagIndex);
            if (*ProcAddresses == NULL) {
                *ProcAddresses = (PVOID) (ULONG_PTR) RtlAllocateHeap;
            }
        }

        ProcAddresses += 1;
    }

    NtProtectVirtualMemory (NtCurrentProcess(),
                            &IATBase,
                            &BigIATSize,
                            OldProtect,
                            &OldProtect);

    return STATUS_SUCCESS;
}


NTSTATUS
LdrpWalkImportDescriptor (
    IN PCWSTR DllPath OPTIONAL,
    IN PLDR_DATA_TABLE_ENTRY LdrDataTableEntry
    )

/*++

Routine Description:

    This is a recursive routine which walks the Import Descriptor
    Table and loads each DLL that is referenced.

Arguments:

    DllPath - Supplies an optional search path to be used to locate
        the DLL.

    LdrDataTableEntry - Supplies the address of the data table entry
        to initialize.

Return Value:

    Status value.

--*/

{
    ULONG ImportSize, NewImportSize;
    PCIMAGE_IMPORT_DESCRIPTOR ImportDescriptor = NULL;
    PCIMAGE_BOUND_IMPORT_DESCRIPTOR NewImportDescriptor = NULL;
    NTSTATUS st = STATUS_SUCCESS;
    PPEB Peb = NtCurrentPeb();
    RTL_CALLER_ALLOCATED_ACTIVATION_CONTEXT_STACK_FRAME ActivationFrame = { sizeof(ActivationFrame), RTL_CALLER_ALLOCATED_ACTIVATION_CONTEXT_STACK_FRAME_FORMAT_WHISTLER };
    CONST PLDR_DATA_TABLE_ENTRY Entry = LdrDataTableEntry;

    if (LdrpManifestProberRoutine != NULL) {

        PVOID SavedEntry = PebLdr.EntryInProgress;

        __try {

            //
            // don't check .exes that have id 1 manifest, id 1 in an .exe makes Peb->ActivationContextData not NULL
            //
            if (Peb->ActivationContextData == NULL || LdrDataTableEntry != LdrpImageEntry) {
                CONST PVOID ViewBase = LdrDataTableEntry->DllBase;
                PVOID ResourceViewBase = ViewBase;
                NTSTATUS stTemp;
                PCWSTR DllName;
#if defined(_WIN64)
                SIZE_T ReturnLength;
                MEMORY_BASIC_INFORMATION MemoryInformation;
                PIMAGE_NT_HEADERS NtHeaders = RtlImageNtHeader(ViewBase);

                if (NtHeaders->OptionalHeader.SectionAlignment < NativePageSize) {
                    stTemp = NtQueryVirtualMemory (NtCurrentProcess(),
                                                   ViewBase,
                                                   MemoryBasicInformation,
                                                   &MemoryInformation,
                                                   sizeof MemoryInformation,
                                                      &ReturnLength);

                    if ((! NT_SUCCESS(stTemp)) ||
                        ((MemoryInformation.Protect != PAGE_READONLY) &&
                        (MemoryInformation.Protect != PAGE_EXECUTE_READ))) {

                        ResourceViewBase = LDR_VIEW_TO_DATAFILE(ViewBase);
                    }
                }
#endif
                DllName = Entry->FullDllName.Buffer;
                //
                // RtlCreateUserProcess() causes this.
                //

                if (LdrDataTableEntry == LdrpImageEntry &&
                    DllName[0] == L'\\' &&
                    DllName[1] == L'?' &&
                    DllName[2] == L'?' &&
                    DllName[3] == L'\\' &&
                    DllName[4] != UNICODE_NULL &&
                    DllName[5] == ':' &&
                    DllName[6] == L'\\'
                    ) {
                    DllName += 4;
                }

                PebLdr.EntryInProgress = Entry;

                stTemp = (*LdrpManifestProberRoutine)(ResourceViewBase, DllName, &Entry->EntryPointActivationContext);
                if (!NT_SUCCESS(stTemp)) {
                    if ((stTemp != STATUS_NO_SUCH_FILE) &&
                        (stTemp != STATUS_RESOURCE_DATA_NOT_FOUND) &&
                        (stTemp != STATUS_RESOURCE_TYPE_NOT_FOUND) &&
                        (stTemp != STATUS_RESOURCE_LANG_NOT_FOUND) &&
                        (stTemp != STATUS_RESOURCE_NAME_NOT_FOUND)) {
                        DbgPrintEx(
                            DPFLTR_SXS_ID,
                            DPFLTR_ERROR_LEVEL,
                            "LDR: LdrpWalkImportDescriptor() failed to probe %wZ for its manifest, ntstatus 0x%08lx\n", &LdrDataTableEntry->FullDllName, stTemp);
                        st = stTemp;
                        __leave;
                    }
                }
            }
        } __finally {
            PebLdr.EntryInProgress = SavedEntry;
        }
    }

    if (!NT_SUCCESS(st)) {
        goto Exit;
    }

    // If we didn't start a private activation context for the DLL, let's use the currently/previously active one.
    if (Entry->EntryPointActivationContext == NULL) {
        st = RtlGetActiveActivationContext((PACTIVATION_CONTEXT *) &LdrDataTableEntry->EntryPointActivationContext);
        if (!NT_SUCCESS(st)) {
#if DBG
            DbgPrintEx(
                DPFLTR_SXS_ID,
                DPFLTR_ERROR_LEVEL,
                "LDR: RtlGetActiveActivationContext() failed; ntstatus = 0x%08lx\n", st);
#endif
            goto Exit;
        }
    }

    RtlActivateActivationContextUnsafeFast(&ActivationFrame, LdrDataTableEntry->EntryPointActivationContext);

    __try {
        //
        // See if there is a bound import table.  If so, walk that to
        // verify if the binding is good.  If so, then succeed with
        // having touched the .idata section, as all the information
        // in the bound imports table is stored in the header.  If any
        // are stale, then fall out into the unbound case.
        //

        //
        // NOTICE-2000/09/30-JayKrell
        // Don't allow binding to redirected .dlls, because the Bind machinery
        // is too weak. It breaks when different files with the same leaf name
        // are built at the same time. This has been seen to happen,
        // with comctl32.dll and comctlv6.dll.
        //
        if ((LdrDataTableEntry->Flags & LDRP_REDIRECTED) == 0) {
            NewImportDescriptor = (PCIMAGE_BOUND_IMPORT_DESCRIPTOR)RtlImageDirectoryEntryToData(
                                   LdrDataTableEntry->DllBase,
                                   TRUE,
                                   IMAGE_DIRECTORY_ENTRY_BOUND_IMPORT,
                                   &NewImportSize
                                   );
        }

        ImportDescriptor = (PCIMAGE_IMPORT_DESCRIPTOR)RtlImageDirectoryEntryToData(
                            LdrDataTableEntry->DllBase,
                            TRUE,
                            IMAGE_DIRECTORY_ENTRY_IMPORT,
                            &ImportSize
                            );


        if (NewImportDescriptor != NULL) {
            st = LdrpHandleNewFormatImportDescriptors(DllPath, LdrDataTableEntry, NewImportDescriptor);
            if (!NT_SUCCESS(st)) {
                __leave;
            }
        } else if (ImportDescriptor != NULL) {
            st = LdrpHandleOldFormatImportDescriptors(DllPath, LdrDataTableEntry, ImportDescriptor);
            if (!NT_SUCCESS(st)) {
                __leave;
            }
        }

        if (Peb->NtGlobalFlag & FLG_HEAP_ENABLE_TAG_BY_DLL) {
            st = LdrpMungHeapImportsForTagging(LdrDataTableEntry);
            if (!NT_SUCCESS(st)) {
                __leave;
            }
        }

        //
        // Notify page heap per dll part of verifier that a dll got loaded.
        // It is important to call this before the main verifier hook so that
        // heap related imports are redirected before any redirection from
        // verifier providers. In time all this logic should move into
        // verifier.dll.
        //

        if (Peb->NtGlobalFlag & FLG_HEAP_PAGE_ALLOCS) {
            st = AVrfPageHeapDllNotification (LdrDataTableEntry);

            if (!NT_SUCCESS(st)) {
                __leave;
            }
        }

        //
        // Notify verifier that a dll got loaded.
        //

        if (Peb->NtGlobalFlag & FLG_APPLICATION_VERIFIER) {
            st = AVrfDllLoadNotification (LdrDataTableEntry);

            if (!NT_SUCCESS(st)) {
                __leave;
            }
        }
        st = STATUS_SUCCESS;
    } __finally {
        RtlDeactivateActivationContextUnsafeFast(&ActivationFrame);
    }

Exit:
    return st;
}


ULONG
LdrpClearLoadInProgress (
    VOID
    )
{
    PLIST_ENTRY Head, Next;
    PLDR_DATA_TABLE_ENTRY LdrDataTableEntry;
    ULONG i;

    i = 0;
    Head = &PebLdr.InInitializationOrderModuleList;
    Next = Head->Flink;

    while (Next != Head) {

        LdrDataTableEntry = CONTAINING_RECORD (Next,
                                               LDR_DATA_TABLE_ENTRY,
                                               InInitializationOrderLinks);

        LdrDataTableEntry->Flags &= ~LDRP_LOAD_IN_PROGRESS;

        //
        // Return the number of entries that have not been processed, but
        // have init routines.
        //

        if (!(LdrDataTableEntry->Flags & LDRP_ENTRY_PROCESSED) && LdrDataTableEntry->EntryPoint) {
            i += 1;
        }

        Next = Next->Flink;
    }
    return i;
}


NTSTATUS
LdrpRunInitializeRoutines (
    IN PCONTEXT Context OPTIONAL
    )
{
    PPEB Peb;
    PLIST_ENTRY Head, Next;
    PLDR_DATA_TABLE_ENTRY LdrDataTableEntry;
    PLDR_DATA_TABLE_ENTRY *LdrDataTableBase;
    PDLL_INIT_ROUTINE InitRoutine;
    BOOLEAN InitStatus;
    ULONG NumberOfRoutines;
    ULONG i;
    NTSTATUS Status;
    ULONG BreakOnDllLoad;
    PLDR_DATA_TABLE_ENTRY StackLdrDataTable[16];
    PTEB OldTopLevelDllBeingLoadedTeb;

    LdrpEnsureLoaderLockIsHeld();

    //
    // Run the Init routines
    // Capture the entries that have init routines
    //

    NumberOfRoutines = LdrpClearLoadInProgress();

    if (NumberOfRoutines != 0) {
        if (NumberOfRoutines <= RTL_NUMBER_OF(StackLdrDataTable)) {
            LdrDataTableBase = StackLdrDataTable;
        }
        else {
            LdrDataTableBase = RtlAllocateHeap(RtlProcessHeap(), MAKE_TAG(TEMP_TAG), NumberOfRoutines * sizeof(PLDR_DATA_TABLE_ENTRY));
            if (LdrDataTableBase == NULL) {
                DbgPrintEx(
                    DPFLTR_LDR_ID,
                    LDR_ERROR_DPFLTR,
                    "LDR: %s - failed to allocate dynamic array of %u DLL initializers to run\n",
                    __FUNCTION__,
                    NumberOfRoutines);

                return STATUS_NO_MEMORY;
            }
        }
    } else {
        LdrDataTableBase = NULL;
    }

    Peb = NtCurrentPeb ();

    Head = &PebLdr.InInitializationOrderModuleList;
    Next = Head->Flink;

    if (ShowSnaps || LdrpShowInitRoutines) {
        DbgPrint("[%x,%x] LDR: Real INIT LIST for process %wZ pid %u 0x%x\n",
            HandleToULong(NtCurrentTeb()->ClientId.UniqueProcess),
            HandleToULong(NtCurrentTeb()->ClientId.UniqueThread),
            &Peb->ProcessParameters->ImagePathName,
            HandleToULong(NtCurrentTeb()->ClientId.UniqueProcess),
            HandleToULong(NtCurrentTeb()->ClientId.UniqueProcess)
            );
    }

    i = 0;
    while ( Next != Head ) {
        LdrDataTableEntry = CONTAINING_RECORD(Next, LDR_DATA_TABLE_ENTRY, InInitializationOrderLinks);

        if (LdrDataTableBase && !(LdrDataTableEntry->Flags & LDRP_ENTRY_PROCESSED) && LdrDataTableEntry->EntryPoint) {
            ASSERT(i < NumberOfRoutines);
            LdrDataTableBase[i] = LdrDataTableEntry;

            if (ShowSnaps || LdrpShowInitRoutines) {
                DbgPrint("[%x,%x]    %wZ init routine %p\n",
                    HandleToULong(NtCurrentTeb()->ClientId.UniqueProcess),
                    HandleToULong(NtCurrentTeb()->ClientId.UniqueThread),
                    &LdrDataTableEntry->FullDllName,
                    LdrDataTableEntry->EntryPoint);
            }

            i++;
        }
        LdrDataTableEntry->Flags |= LDRP_ENTRY_PROCESSED;

        Next = Next->Flink;
    }

    ASSERT(i == NumberOfRoutines);

    if (LdrDataTableBase == NULL) {
        return STATUS_SUCCESS;
    }

    i = 0;

    OldTopLevelDllBeingLoadedTeb = LdrpTopLevelDllBeingLoadedTeb;
    LdrpTopLevelDllBeingLoadedTeb = NtCurrentTeb();

    //
    // If we are in LdrpInitializeProcess then call into
    // kernel32.dll's "post import" function so Terminal Server can do
    // various patching of import address tables.
    //
    if (Context != NULL &&
        Kernel32ProcessInitPostImportFunction != NULL) {

        PKERNEL32_PROCESS_INIT_POST_IMPORT_FUNCTION LocalFunction;

        LocalFunction = Kernel32ProcessInitPostImportFunction;
        Kernel32ProcessInitPostImportFunction = NULL;

        if (LocalFunction != NULL) {
            Status = (*LocalFunction)();

            if (!NT_SUCCESS(Status)) {
                DbgPrintEx(
                    DPFLTR_LDR_ID,
                    LDR_ERROR_DPFLTR,
                    "LDR: %s - Failed running kernel32 post-import function; status 0x%08lx\n",
                    __FUNCTION__,
                    Status);

                return Status;
            }
        }
    }

    Status = STATUS_SUCCESS;
    try {
        while ( i < NumberOfRoutines ) {
            LdrDataTableEntry = LdrDataTableBase[i];
            i += 1;
            InitRoutine = (PDLL_INIT_ROUTINE)(ULONG_PTR)LdrDataTableEntry->EntryPoint;

            //
            // Walk through the entire list looking for un-processed
            // entries. For each entry, set the processed flag
            // and optionally call it's init routine
            //

            BreakOnDllLoad = 0;
#if DBG
            if (TRUE)
#else
            if (Peb->BeingDebugged || Peb->ReadImageFileExecOptions)
#endif
            {
                Status = LdrQueryImageFileExecutionOptions( &LdrDataTableEntry->BaseDllName,
                                                            L"BreakOnDllLoad",
                                                            REG_DWORD,
                                                            &BreakOnDllLoad,
                                                            sizeof( BreakOnDllLoad ),
                                                            NULL
                                                          );
                if (!NT_SUCCESS( Status )) {
                    BreakOnDllLoad = 0;
                    Status = STATUS_SUCCESS;
                }
            }

            if (BreakOnDllLoad) {
                if (ShowSnaps) {
                    DbgPrint( "LDR: %wZ loaded.", &LdrDataTableEntry->BaseDllName );
                    DbgPrint( " - About to call init routine at %p\n", InitRoutine );
                }
                DbgBreakPoint();

            } else if (ShowSnaps) {
                if ( InitRoutine ) {
                    DbgPrint( "[%x,%x] LDR: %wZ loaded",
                        HandleToULong(NtCurrentTeb()->ClientId.UniqueProcess),
                        HandleToULong(NtCurrentTeb()->ClientId.UniqueThread),
                        &LdrDataTableEntry->BaseDllName);

                    DbgPrint(" - Calling init routine at %p\n", InitRoutine);
                }
            }

            if ( InitRoutine ) {
                PLDR_DATA_TABLE_ENTRY SavedInitializer = LdrpCurrentDllInitializer;
                LdrpCurrentDllInitializer = LdrDataTableEntry;

                InitStatus = FALSE;

                __try {
                    LDRP_ACTIVATE_ACTIVATION_CONTEXT(LdrDataTableEntry);
                    //
                    // If the DLL has TLS data, then call the optional initializers
                    //
                    if ((LdrDataTableEntry->TlsIndex != 0) && (Context != NULL))
                        LdrpCallTlsInitializers(LdrDataTableEntry->DllBase,DLL_PROCESS_ATTACH);

                    if (LdrpShowInitRoutines) {
                        DbgPrint("[%x,%x] LDR: calling init routine %p for DLL_PROCESS_ATTACH\n",
                            HandleToULong(NtCurrentTeb()->ClientId.UniqueProcess),
                            HandleToULong(NtCurrentTeb()->ClientId.UniqueThread),
                            InitRoutine);
                    }

                    InitStatus = LdrpCallInitRoutine(InitRoutine,
                                                     LdrDataTableEntry->DllBase,
                                                     DLL_PROCESS_ATTACH,
                                                     Context);
                    LDRP_DEACTIVATE_ACTIVATION_CONTEXT();
                } __finally {
                    LdrpCurrentDllInitializer = SavedInitializer;
                }

                LdrDataTableEntry->Flags |= LDRP_PROCESS_ATTACH_CALLED;

                if (!InitStatus) {
                    DbgPrintEx(
                        DPFLTR_LDR_ID,
                        LDR_ERROR_DPFLTR,
                        "[%x,%x] LDR: DLL_PROCESS_ATTACH for dll \"%wZ\" (InitRoutine: %p) failed\n",
                        HandleToULong(NtCurrentTeb()->ClientId.UniqueProcess),
                        HandleToULong(NtCurrentTeb()->ClientId.UniqueThread),
                        &LdrDataTableEntry->FullDllName,
                        InitRoutine);

                    Status = STATUS_DLL_INIT_FAILED;
                    __leave;
                }
            }
        }

        //
        // If the image has tls than call its initializers
        //

        if (LdrpImageHasTls && (Context != NULL))
        {
            LDRP_ACTIVATE_ACTIVATION_CONTEXT (LdrpImageEntry);

            LdrpCallTlsInitializers (Peb->ImageBaseAddress,DLL_PROCESS_ATTACH);

            LDRP_DEACTIVATE_ACTIVATION_CONTEXT ();
        }
    } finally {
        LdrpTopLevelDllBeingLoadedTeb = OldTopLevelDllBeingLoadedTeb;

        if ((LdrDataTableBase != NULL) &&
            (LdrDataTableBase != StackLdrDataTable)) {

            RtlFreeHeap(RtlProcessHeap(),0,LdrDataTableBase);
        }
    }

    return Status;
}

NTSTATUS
LdrpResolveFullName(
    IN PCUNICODE_STRING FileName,
    IN OUT PUNICODE_STRING StaticString,
    IN OUT PUNICODE_STRING DynamicString,
    OUT PUNICODE_STRING *StringUsed
    )
{
    BOOLEAN NameInvalid;
    RTL_PATH_TYPE InputPathType;
    NTSTATUS Status = STATUS_SUCCESS;
    DWORD Length;

    if (ShowSnaps) {
        DbgPrintEx(DPFLTR_LDR_ID,
                   LDR_ERROR_DPFLTR,
                   "LDR: %s - Expanding full name of %wZ\n",
                   __FUNCTION__,
                   FileName);
    }

    RtlAcquirePebLock();

    // Try with static buffer first
    Length = RtlGetFullPathName_Ustr(FileName,
                                     StaticString->MaximumLength,
                                     StaticString->Buffer,
                                     NULL,
                                     &NameInvalid,
                                     &InputPathType);

    if (!Length || UNICODE_STRING_MAX_BYTES < Length) {
        Status = STATUS_DLL_NOT_FOUND;
    } else if (Length < StaticString->MaximumLength) {
        *StringUsed = StaticString;
        StaticString->Length = (USHORT) Length;
    } else {

        // Didn't work -- try dynamic buffer.  Subtract off a char
        // because LdrpAllocateUnicodeString takes the length of the
        // string without the trailing NULL, and
        // RtlGetFullPathName_Ustr includes the trailing NULL.

        ASSERT(Length >= sizeof(WCHAR));
        Status = LdrpAllocateUnicodeString(DynamicString, (USHORT)Length - sizeof(WCHAR));
        if (NT_SUCCESS(Status)) {
            Length = RtlGetFullPathName_Ustr(FileName,
                                             DynamicString->MaximumLength,
                                             DynamicString->Buffer,
                                             NULL,
                                             &NameInvalid,
                                             &InputPathType);
            if (!Length || DynamicString->MaximumLength <= Length) {
                LdrpFreeUnicodeString(DynamicString);
                Status = STATUS_DLL_NOT_FOUND;
            } else {
                *StringUsed = DynamicString;
                DynamicString->Length = (USHORT) Length;
            }
        }
    }

    RtlReleasePebLock();

    if (ShowSnaps) {
        if (NT_SUCCESS(Status)) {
            DbgPrintEx(DPFLTR_LDR_ID,
                       LDR_ERROR_DPFLTR,
                       "LDR: %s - Expanded to %wZ\n",
                       __FUNCTION__,
                       *StringUsed);
        } else {
            DbgPrintEx(DPFLTR_LDR_ID,
                       LDR_ERROR_DPFLTR,
                       "LDR: %s - Failed to expand %wZ; 0x%08x\n",
                       __FUNCTION__,
                       FileName,
                       Status);
        }
    }

    if (! NT_SUCCESS(Status)) {
        *StringUsed = NULL;
    }

    return Status;
}

NTSTATUS
LdrpSearchPath(
    IN PCWSTR lpPath,
    IN PCWSTR lpFileName,
    IN OUT PUNICODE_STRING StaticString,
    IN OUT PUNICODE_STRING DynamicString,
    OUT PUNICODE_STRING *StringUsed
    )
{
    PCWSTR EltStart, EltEnd, NamePtr;
    PWSTR Buffer = NULL, BufEnd;
    ULONG BufferCchLen;
    ULONG FileCchLen;
    NTSTATUS Status;
    UNICODE_STRING TestName;
    BOOLEAN FoundInPath = FALSE;
    BOOLEAN FoundEnd = FALSE;

    if (! ARGUMENT_PRESENT(lpPath)) {
        lpPath = LdrpDefaultPath.Buffer;
    }

    if (ShowSnaps) {
        DbgPrintEx(DPFLTR_LDR_ID,
                   LDR_ERROR_DPFLTR,
                   "LDR: %s - Looking for %ws in %ws\n",
                   __FUNCTION__,
                   lpFileName,
                   lpPath);
    }

    if (RtlDetermineDosPathNameType_U(lpFileName) != RtlPathTypeRelative) {

        Status = RtlInitUnicodeStringEx(&TestName, lpFileName);
        if (! NT_SUCCESS(Status)) {
            goto cleanup;
        }

        if (! RtlDoesFileExists_UstrEx(&TestName, TRUE)) {
            Status = STATUS_DLL_NOT_FOUND;
            goto cleanup;
        }

        Status = LdrpResolveFullName(&TestName,
                                     StaticString,
                                     DynamicString,
                                     StringUsed);
        goto cleanup;
    }

    BufferCchLen = 0;

    // For each ';' or NULL-terminated element, find the length; save
    // the max length found.
    EltStart = EltEnd = lpPath;
    for (;;) {
        if (! *EltEnd || *EltEnd == L';') {
            if (BufferCchLen < ((ULONG)(EltEnd - EltStart))) {
                BufferCchLen = ((ULONG)(EltEnd - EltStart));
            }
            EltStart = EltEnd + 1;
        }

        if (! *EltEnd) {
            break;
        }

        EltEnd++;
    }

    // Add in the length of the file name, a char for a '\', and a
    // char for the trailing NULL.
    FileCchLen = (LONG) wcslen(lpFileName);
    BufferCchLen += FileCchLen + 2;

    if (UNICODE_STRING_MAX_CHARS < BufferCchLen) {
        Status = STATUS_NAME_TOO_LONG;
        goto cleanup;
    }

    // Allocate the buffer
    Buffer = RtlAllocateHeap(RtlProcessHeap(),
                             0,
                             BufferCchLen * sizeof(WCHAR));
    if (! Buffer) {
        Status = STATUS_NO_MEMORY;
        goto cleanup;
    }

    RtlInitEmptyUnicodeString(&TestName,
                              Buffer,
                              BufferCchLen * sizeof(WCHAR));

    // For each ';' or NULL-terminated path element, copy it into our
    // buffer, and see if it exists.
    EltStart = EltEnd = lpPath;
    BufEnd = Buffer;

    Status = STATUS_NOT_FOUND;

    while (!FoundEnd && !FoundInPath) {
        if (! *EltEnd || *EltEnd == L';') {
            if (EltEnd != EltStart) { // ignore empty elements
                ASSERT(BufEnd > Buffer);
                if (BufEnd[-1] != L'\\') {
                    *BufEnd++ = L'\\';
                }
                NamePtr = lpFileName;

                while (*NamePtr) {
                    *BufEnd++ = *NamePtr++;
                }
                *BufEnd = UNICODE_NULL;

                if (ShowSnaps) {
                    DbgPrintEx(DPFLTR_LDR_ID,
                               LDR_ERROR_DPFLTR,
                               "LDR: %s - Looking for %ws\n",
                               __FUNCTION__,
                               Buffer);
                }
                TestName.Length = (USHORT)((BufEnd - Buffer) * sizeof(WCHAR));
                ASSERT(TestName.Length < TestName.MaximumLength);

                if (RtlDoesFileExists_UstrEx(&TestName, FALSE)) {
                    TestName.MaximumLength = (USHORT)((BufEnd - Buffer + 1) * sizeof(WCHAR));
                    TestName.Buffer = RtlReAllocateHeap(RtlProcessHeap(),
                                                        0,
                                                        Buffer,
                                                        TestName.MaximumLength);
                    if (! TestName.Buffer) {
                        TestName.Buffer = Buffer;
                    } else {
                        Buffer = TestName.Buffer;
                    }
                    ASSERT(TestName.Buffer);
                    FoundInPath = TRUE;
                    break;
                }
                BufEnd = Buffer;
            }
            EltStart = EltEnd + 1;
        } else {
            *BufEnd++ = *EltEnd;
        }

        if (! *EltEnd) {
            FoundEnd = TRUE;
        }

        EltEnd++;
    }

    if (FoundInPath) {
        Status = LdrpResolveFullName(&TestName,
                                     StaticString,
                                     DynamicString,
                                     StringUsed);
    }

cleanup:
    if (Buffer) {
        RtlFreeHeap(RtlProcessHeap(), 0, Buffer);
    }

    if (! NT_SUCCESS(Status)) {
        *StringUsed = NULL;
    }

    if (ShowSnaps) {
        if (NT_SUCCESS(Status)) {
            DbgPrintEx(DPFLTR_LDR_ID,
                       LDR_ERROR_DPFLTR,
                       "LDR: %s - Returning %Z\n",
                       __FUNCTION__,
                       *StringUsed);
        } else {
            DbgPrintEx(DPFLTR_LDR_ID,
                       LDR_ERROR_DPFLTR,
                       "LDR: %s - Unable to locate %ws in %ws: 0x%08x\n",
                       __FUNCTION__,
                       lpFileName,
                       lpPath,
                       Status);
        }
    }

    return Status;
}

BOOLEAN
LdrpCheckForLoadedDll (
    IN PCWSTR DllPath OPTIONAL,
    IN PCUNICODE_STRING DllName,
    IN BOOLEAN StaticLink,
    IN BOOLEAN Redirected,
    OUT PLDR_DATA_TABLE_ENTRY *LdrDataTableEntry
    )

/*++

Routine Description:

    This function scans the loader data table looking to see if
    the specified DLL has already been mapped into the image. If
    the dll has been loaded, the address of its data table entry
    is returned.

Arguments:

    DllPath - Supplies an optional search path used to locate the DLL.

    DllName - Supplies the name to search for.

    StaticLink - TRUE if performing a static link.

    LdrDataTableEntry - Returns the address of the loader data table
        entry that describes the first dll section that implements the
        dll.

Return Value:

    TRUE- The dll is already loaded.  The address of the data table
        entries that implement the dll, and the number of data table
        entries are returned.

    FALSE - The dll is not already mapped.

--*/

{
    HANDLE CurrentProcess;
    BOOLEAN Result = FALSE;
    PLDR_DATA_TABLE_ENTRY Entry;
    PLIST_ENTRY Head, Next;
    PUNICODE_STRING FullDllName;
    BOOLEAN HardCodedPath;
    PWCH p;
    ULONG i;
    WCHAR FullDllNameStaticBuffer[40]; // Arbitrary short length so most
                                       // d:\windows\system32\foobar.dll loads
                                       // don't need to search the search path twice
    UNICODE_STRING FullDllNameStaticString;
    UNICODE_STRING FullDllNameDynamicString;
    NTSTATUS Status;

    RtlInitEmptyUnicodeString(&FullDllNameStaticString,
                              FullDllNameStaticBuffer,
                              sizeof(FullDllNameStaticBuffer));

    RtlInitUnicodeString(&FullDllNameDynamicString, NULL);

    FullDllName = NULL;

    if (!DllName->Buffer || !DllName->Buffer[0]) {
        return FALSE;
    }

    Status = STATUS_SUCCESS;

    //
    // for static links, just go to the hash table
    //

staticlink:

    if (StaticLink) {

        //
        // If this is a redirected static load, the dll name is a
        // fully qualified path.  The hash table is maintained based on
        // the first character of the base dll name, so find the base dll name.
        //

        if (Redirected) {

            PWSTR LastChar;

            LastChar = DllName->Buffer + (DllName->Length / sizeof(WCHAR)) - (DllName->Length == 0 ? 0 : 1);

            while (LastChar != DllName->Buffer) {
                const WCHAR wch = *LastChar;

                if ((wch == L'\\') || (wch == L'/'))
                    break;

                LastChar -= 1;
            }

            //
            // This assert ignores the "possibility" that the first and
            // only slash is the first character, but that's
            // an error, too.  The redirection should be a complete DOS path.
            //

            ASSERTMSG(
                "Redirected DLL name does not have full path; either caller lied or redirection info is in error",
                LastChar != DllName->Buffer);

            if (LastChar == DllName->Buffer) {
                if (ShowSnaps) {
                    DbgPrint("LDR: Failing LdrpCheckForLoadedDll because redirected DLL name %wZ does not include a slash\n", DllName);
                }

                Result = FALSE;
                goto alldone;
            }

            LastChar += 1;

            i = LDRP_COMPUTE_HASH_INDEX(*LastChar);
        } else {
            i = LDRP_COMPUTE_HASH_INDEX(DllName->Buffer[0]);
        }

        Head = &LdrpHashTable[i];
        Next = Head->Flink;
        while ( Next != Head ) {
            Entry = CONTAINING_RECORD(Next, LDR_DATA_TABLE_ENTRY, HashLinks);
#if DBG
            LdrpCompareCount += 1;
#endif            
            //
            // Redirected static loads never match unredirected entries
            // and vice versa.
            //

            if (Redirected) {
                if (((Entry->Flags & LDRP_REDIRECTED) != 0) &&
                    RtlEqualUnicodeString(DllName, &Entry->FullDllName, TRUE)) {
                    *LdrDataTableEntry = Entry;
                    Result = TRUE;
                    goto alldone;
                }
            } else {
                // Not redirected...
                if (((Entry->Flags & LDRP_REDIRECTED) == 0) &&
                    RtlEqualUnicodeString(DllName, &Entry->BaseDllName, TRUE)) {
                    *LdrDataTableEntry = Entry;
                    Result = TRUE;
                    goto alldone;
                }
            }

            Next = Next->Flink;
        }
        Result = FALSE;
        goto alldone;
    }

    //
    // If the DLL name contained a hard coded path
    // (dynamic link only), then the fully qualified
    // name needs to be compared to make sure we
    // have the correct DLL.
    //

    p = DllName->Buffer;
    HardCodedPath = FALSE;

    if (Redirected) {
        HardCodedPath = TRUE;
        FullDllNameStaticString.Length = DllName->Length;
        FullDllNameStaticString.MaximumLength = DllName->MaximumLength;
        FullDllNameStaticString.Buffer = DllName->Buffer;
        FullDllName = &FullDllNameStaticString;
    } else {
        while (*p) {
            const WCHAR wch = *p++;
            if (wch == (WCHAR)'\\' || wch == (WCHAR)'/' ) {

                HardCodedPath = TRUE;

                //
                // We have a hard coded path, so we have to search path
                // for the DLL. We need the full DLL name.
                //

                Status = LdrpSearchPath(DllPath,
                                        DllName->Buffer,
                                        &FullDllNameStaticString,
                                        &FullDllNameDynamicString,
                                        &FullDllName);

                if (! NT_SUCCESS(Status)) {
                    if (ShowSnaps) {
                        DbgPrint("LDR: LdrpCheckForLoadedDll - Unable To Locate "
                                 "%ws: 0x%08x\n",
                                 DllName->Buffer,
                                 Status);
                    }

                    Result = FALSE;
                    goto alldone;
                }

                break;
            }
        }
    }

    //
    // If this is a dynamic load lib, and there is not a hard
    // coded path, then go to the static lib hash table for resolution
    //

    if ( !HardCodedPath ) {

        //
        // If we're redirecting this DLL, don't check if there's
        // another DLL by the same name already loaded.
        //

        if (NT_SUCCESS(RtlFindActivationContextSectionString(0, NULL, ACTIVATION_CONTEXT_SECTION_DLL_REDIRECTION, DllName, NULL))) {
            Result = FALSE;
            goto alldone;
        }

        StaticLink = TRUE;

        goto staticlink;
    }

    Result = FALSE;
    Head = &PebLdr.InLoadOrderModuleList;
    Next = Head->Flink;

    while (Next != Head) {
        Entry = CONTAINING_RECORD(Next, LDR_DATA_TABLE_ENTRY, InLoadOrderLinks);
        Next = Next->Flink;

        //
        // When we unload, the memory order links flink field is nulled.
        // this is used to skip the entry pending list removal.
        //

        if (!Entry->InMemoryOrderLinks.Flink) {
            continue;
        }

        //
        // Since this is a full string comparison, we don't worry
        // about redirection - we don't want to load the dll from
        // a particular path more than once just because one of them
        // explicitly (and erroneously) specified the magic side-by-side
        // location of the DLL and the other loaded it via side-by-side
        // isolation automagically.
        //

        if (RtlEqualUnicodeString (FullDllName,
                                   &Entry->FullDllName,
                                   TRUE)) {

            Result = TRUE;
            *LdrDataTableEntry = Entry;
            break;
        }
    }

    if ( !Result ) {

        //
        // No names matched. This might be a long short name mismatch or
        // any kind of alias pathname. Deal with this by opening and mapping
        // full dll name and then repeat the scan this time checking for
        // timedatestamp matches
        //

        HANDLE File;
        HANDLE Section;
        NTSTATUS st;
        OBJECT_ATTRIBUTES ObjectAttributes;
        IO_STATUS_BLOCK IoStatus;
        PVOID ViewBase;
        SIZE_T ViewSize;
        PIMAGE_NT_HEADERS NtHeadersSrc,NtHeadersE;
        UNICODE_STRING NtFileName;

        if (!RtlDosPathNameToNtPathName_U (FullDllName->Buffer,
                                           &NtFileName,
                                           NULL,
                                           NULL)) {
            goto alldone;
        }

        InitializeObjectAttributes (&ObjectAttributes,
                                    &NtFileName,
                                    OBJ_CASE_INSENSITIVE,
                                    NULL,
                                    NULL);

        st = NtOpenFile (&File,
                         SYNCHRONIZE | FILE_EXECUTE,
                         &ObjectAttributes,
                         &IoStatus,
                         FILE_SHARE_READ | FILE_SHARE_DELETE,
                         FILE_NON_DIRECTORY_FILE | FILE_SYNCHRONOUS_IO_NONALERT);
        RtlFreeHeap (RtlProcessHeap(), 0, NtFileName.Buffer);

        if (!NT_SUCCESS(st)) {
            goto alldone;
        }

        st = NtCreateSection(
                &Section,
                SECTION_MAP_READ | SECTION_MAP_EXECUTE | SECTION_MAP_WRITE,
                NULL,
                NULL,
                PAGE_EXECUTE,
                SEC_COMMIT,
                File);

        NtClose (File);

        if (!NT_SUCCESS(st)) {
            goto alldone;
        }

        ViewBase = NULL;
        ViewSize = 0;

        CurrentProcess = NtCurrentProcess();

        st = NtMapViewOfSection (Section,
                                 CurrentProcess,
                                 (PVOID *)&ViewBase,
                                 0L,
                                 0L,
                                 NULL,
                                 &ViewSize,
                                 ViewShare,
                                 0L,
                                 PAGE_EXECUTE);

        NtClose(Section);

        if (!NT_SUCCESS(st)) {
            goto alldone;
        }

        //
        // The section is mapped. Now find the headers
        //
        
        st = RtlImageNtHeaderEx(0, ViewBase, ViewSize, &NtHeadersSrc);
        if (!NT_SUCCESS(st) || !NtHeadersSrc) {
            NtUnmapViewOfSection(CurrentProcess,ViewBase);
            goto alldone;
        }

        Head = &PebLdr.InLoadOrderModuleList;
        Next = Head->Flink;

        while ( Next != Head ) {
            Entry = CONTAINING_RECORD(Next, LDR_DATA_TABLE_ENTRY, InLoadOrderLinks);
            Next = Next->Flink;

            //
            // When we unload, the memory order links flink field is nulled.
            // this is used to skip the entry pending list removal.
            //

            if ( !Entry->InMemoryOrderLinks.Flink ) {
                continue;
            }

            try {
                if (Entry->TimeDateStamp == NtHeadersSrc->FileHeader.TimeDateStamp &&
                    Entry->SizeOfImage == NtHeadersSrc->OptionalHeader.SizeOfImage ) {

                    //
                    // There is a very good chance we have an image match.
                    // Check the entire file header and optional header. If
                    // they match, declare this a match.
                    //

                    NtHeadersE = RtlImageNtHeader(Entry->DllBase);

                    if ( RtlCompareMemory(NtHeadersE,NtHeadersSrc,sizeof(*NtHeadersE)) == sizeof(*NtHeadersE) ) {

                        //
                        // Now that it looks like we have a match, compare
                        // volume serial numbers and file indexes.
                        //

                        st = NtAreMappedFilesTheSame(Entry->DllBase,ViewBase);

                        if ( !NT_SUCCESS(st) ) {
                            continue;
                        }
                        else {
                            Result = TRUE;
                            *LdrDataTableEntry = Entry;
                            break;
                        }
                    }
                }
            }
            except (LdrpGenericExceptionFilter(GetExceptionInformation(), __FUNCTION__)) {
                DbgPrintEx(
                    DPFLTR_LDR_ID,
                    LDR_ERROR_DPFLTR,
                    "LDR: %s - Caught exception %08lx\n",
                    __FUNCTION__,
                    GetExceptionCode());

                break;
            }
        }
        NtUnmapViewOfSection(CurrentProcess,ViewBase);
    }

alldone:
    LdrpFreeUnicodeString(&FullDllNameDynamicString);

    return Result;
}


BOOLEAN
LdrpCheckForLoadedDllHandle (
    IN PVOID DllHandle,
    OUT PLDR_DATA_TABLE_ENTRY *LdrDataTableEntry
    )

/*++

Routine Description:

    This function scans the loader data table looking to see if
    the specified DLL has already been mapped into the image address
    space. If the dll has been loaded, the address of its data table
    entry that describes the dll is returned.

Arguments:

    DllHandle - Supplies the DllHandle of the DLL being searched for.

    LdrDataTableEntry - Returns the address of the loader data table
        entry that describes the dll.

Return Value:

    TRUE- The dll is loaded.  The address of the data table entry is
        returned.

    FALSE - The dll is not loaded.

--*/

{
    PLDR_DATA_TABLE_ENTRY Entry;
    PLIST_ENTRY Head,Next;

    if ( LdrpLoadedDllHandleCache &&
        (PVOID) LdrpLoadedDllHandleCache->DllBase == DllHandle ) {
        *LdrDataTableEntry = LdrpLoadedDllHandleCache;
        return TRUE;
    }

    Head = &PebLdr.InLoadOrderModuleList;
    Next = Head->Flink;

    while (Next != Head) {

        Entry = CONTAINING_RECORD(Next, LDR_DATA_TABLE_ENTRY, InLoadOrderLinks);
        Next = Next->Flink;

        //
        // when we unload, the memory order links flink field is nulled.
        // this is used to skip the entry pending list removal.
        //

        if ( !Entry->InMemoryOrderLinks.Flink ) {
            continue;
        }

        if (DllHandle == (PVOID)Entry->DllBase ){
            LdrpLoadedDllHandleCache = Entry;
            *LdrDataTableEntry = Entry;
            return TRUE;
        }
    }
    return FALSE;
}


NTSTATUS
LdrpCheckCorImage (
    IN PIMAGE_COR20_HEADER Cor20Header,
    IN PCUNICODE_STRING FullDllName,
    IN OUT PVOID *ViewBase,
    OUT PBOOLEAN Cor20ILOnly
    )

{
    PIMAGE_NT_HEADERS NtHeaders;
    NTSTATUS NtStatus = STATUS_SUCCESS;
    PVOID OriginalViewBase = *ViewBase;

    
    if (Cor20Header) {

        //
        // The image is COM+ so notify the runtime that the image was loaded
        // and allow it to verify the image for correctness.
        //

        NtStatus = LdrpCorValidateImage(ViewBase, FullDllName->Buffer);
        if (!NT_SUCCESS (NtStatus)) {
            
            //
            // Image is bad, or mscoree failed, etc.
            //

            *ViewBase = OriginalViewBase;
            goto return_result;
        }
        
        //
        // Indicates it's an ILONLY image if the flag is set in the header.
        //

        if ((Cor20Header->Flags & COMIMAGE_FLAGS_ILONLY) == COMIMAGE_FLAGS_ILONLY) {
            *Cor20ILOnly = TRUE;
        }

        if (*ViewBase != OriginalViewBase) {
            
            //
            // Mscoree has substituted a new image at a new base in place
            // of the original image.  Unmap the original image and use
            // the new image from now on.
            //

            NtUnmapViewOfSection(NtCurrentProcess(), OriginalViewBase);
            NtHeaders = RtlImageNtHeader(*ViewBase);
            
            if (!NtHeaders) {
                NtStatus = STATUS_INVALID_IMAGE_FORMAT;
                goto return_result;
            }
        }
    }

return_result:

    return NtStatus;
}


NTSTATUS
LdrpMapDll (
    IN PCWSTR DllPath OPTIONAL,
    IN PCWSTR DllName,
    IN PULONG DllCharacteristics OPTIONAL,
    IN BOOLEAN StaticLink,
    IN BOOLEAN Redirected,
    OUT PLDR_DATA_TABLE_ENTRY *LdrDataTableEntry
    )

/*++

Routine Description:

    This routine maps the DLL into the users address space.

Arguments:

    DllPath - Supplies an optional search path to be used to locate the DLL.

    DllName - Supplies the name of the DLL to load.

    StaticLink - TRUE if this DLL has a static link to it.

    LdrDataTableEntry - Supplies the address of the data table entry.

Return Value:

    Status value.

--*/

{
    NTSTATUS st = STATUS_INTERNAL_ERROR;
    PVOID ViewBase = NULL;
    const PTEB Teb = NtCurrentTeb();
    SIZE_T ViewSize;
    HANDLE Section, DllFile;
    UNICODE_STRING FullDllName, BaseDllName;
    UNICODE_STRING NtFileName;
    PLDR_DATA_TABLE_ENTRY Entry;
    PIMAGE_NT_HEADERS NtHeaders;
    PVOID ArbitraryUserPointer;
    BOOLEAN KnownDll;
    PCUNICODE_STRING CollidingDll = NULL;
    const static UNICODE_STRING DynamicallyAllocatedMemoryString = RTL_CONSTANT_STRING(L"Dynamically Allocated Memory");
    PUCHAR ImageBase, ImageBounds, ScanBase, ScanTop;
    PLDR_DATA_TABLE_ENTRY ScanEntry;
    PLIST_ENTRY ScanHead,ScanNext;
    BOOLEAN CollidingDllFound;
    NTSTATUS ErrorStatus;
    ULONG_PTR ErrorParameters[2];
    ULONG ErrorResponse;
    IMAGE_COR20_HEADER *Cor20Header;
    ULONG Cor20HeaderSize;
    BOOLEAN Cor20ILOnly = FALSE;
    PVOID OriginalViewBase = NULL;
    PWSTR AppCompatDllName = NULL;

    RtlZeroMemory (&BaseDllName, sizeof (UNICODE_STRING));
    FullDllName.Buffer = NULL;

    //
    // Get section handle of DLL being snapped
    //

#if LDRDBG
    if (ShowSnaps) {
        DbgPrint("LDR: LdrpMapDll: Image Name %ws, Search Path %ws\n",
                DllName,
                ARGUMENT_PRESENT(DllPath) ? DllPath : L""
                );
    }
#endif

    Entry = NULL;
    KnownDll = FALSE;
    Section = NULL;

    LdrpEnsureLoaderLockIsHeld();

    // No capturing etc. of the globals since we "know" that the loader lock is taken to synchronize access.
    if (LdrpAppCompatDllRedirectionCallbackFunction != NULL) {
        st = (*LdrpAppCompatDllRedirectionCallbackFunction)(
                0,              // Flags - reserved for the future
                DllName,
                DllPath,
                DllCharacteristics,
                LdrpAppCompatDllRedirectionCallbackData,
                &AppCompatDllName);
        if (!NT_SUCCESS(st)) {
            DbgPrintEx(
                DPFLTR_LDR_ID,
                LDR_ERROR_DPFLTR,
                "LDR: %s - call back to app compat redirection function @ %p (cb data: %p) failed with status %x\n",
                __FUNCTION__,
                LdrpAppCompatDllRedirectionCallbackFunction,
                LdrpAppCompatDllRedirectionCallbackData,
                st);

            goto Exit;
        }

        if (AppCompatDllName != NULL) {
            Redirected = TRUE;
            DllName = AppCompatDllName;
        }
    }

    if ((LdrpKnownDllObjectDirectory != NULL) && !Redirected) {
        PCWCH p = DllName;
        WCHAR wch;

        //
        // Skip the KnownDll check if this is an explicit path.
        //

        while ((wch = *p) != L'\0') {
            p++;
            if (RTL_IS_PATH_SEPARATOR(wch))
                break;
        }

        // If we hit the end of the string, there must have not been a path separator.
        if (wch == L'\0') {
            st = LdrpCheckForKnownDll(DllName, &FullDllName, &BaseDllName, &Section);
            if ((!NT_SUCCESS(st)) && (st != STATUS_DLL_NOT_FOUND)) {
                DbgPrintEx(
                    DPFLTR_LDR_ID,
                    LDR_ERROR_DPFLTR,
                    "LDR: %s - call to LdrpCheckForKnownDll(\"%ws\", ...) failed with status %x\n",
                    __FUNCTION__,
                    DllName,
                    st);

                goto Exit;
            }
        }
    }

    if (Section == NULL) {
        st = LdrpResolveDllName(DllPath, DllName, Redirected, &FullDllName, &BaseDllName, &DllFile);
        //
        // NOTICE-2002/03/06-ELi
        // assuming DllFile is also NULL when returning from LdrpResolveDllName
        // not a handle leak in the error paths below
        //
        if (!NT_SUCCESS(st)) {
            if (st == STATUS_DLL_NOT_FOUND) {
                if (StaticLink) {
                    UNICODE_STRING ErrorDllName, ErrorDllPath;
                    PUNICODE_STRING ErrorStrings[2] = { &ErrorDllName, &ErrorDllPath };
                    ULONG xErrorResponse;

                    RtlInitUnicodeString(&ErrorDllName,DllName);
                    RtlInitUnicodeString(&ErrorDllPath,ARGUMENT_PRESENT(DllPath) ? DllPath : LdrpDefaultPath.Buffer);

                    NtRaiseHardError(
                        STATUS_DLL_NOT_FOUND,
                        2,              // Number of error strings
                        0x00000003,
                        (PULONG_PTR)ErrorStrings,
                        OptionOk,
                        &xErrorResponse);

                    if (LdrpInLdrInit)
                        LdrpFatalHardErrorCount++;
                }
            } else {
                DbgPrintEx(
                    DPFLTR_LDR_ID,
                    LDR_ERROR_DPFLTR,
                    "LDR: %s - call to LdrpResolveDllName on dll \"%ws\" failed with status %x\n",
                    __FUNCTION__,
                    DllName,
                    st);
            }



            goto Exit;
        }

        if (ShowSnaps) {
            PCSZ type;
            PCSZ type2;
            type = StaticLink ? "STATIC" : "DYNAMIC";
            type2 = Redirected ? "REDIRECTED" : "NON_REDIRECTED";

            DbgPrint(
                "LDR: Loading (%s, %s) %wZ\n",
                type,
                type2,
                &FullDllName);
        }

        if (!RtlDosPathNameToNtPathName_U(
                FullDllName.Buffer,
                &NtFileName,
                NULL,
                NULL)) {
            st = STATUS_OBJECT_PATH_SYNTAX_BAD;
            DbgPrintEx(
                DPFLTR_LDR_ID,
                LDR_ERROR_DPFLTR,
                "LDR: %s - call to RtlDosPathNameToNtPathName_U on path \"%wZ\" failed; returning status %x\n",
                __FUNCTION__,
                &FullDllName,
                st);
            goto Exit;
        }

        st = LdrpCreateDllSection(&NtFileName,
                                  DllFile,
                                  DllCharacteristics,
                                  &Section);

        if (!NT_SUCCESS(st)) {
            DbgPrintEx(
                DPFLTR_LDR_ID,
                LDR_ERROR_DPFLTR,
                "LDR: %s - LdrpCreateDllSection (%wZ) failed with status %x\n",
                __FUNCTION__,
                &NtFileName,
                st);

            LdrpFreeUnicodeString(&FullDllName);
            // We do not free BaseDllName since it's just a substring of FullDllName.

            RtlFreeHeap(RtlProcessHeap(), 0, NtFileName.Buffer);
            goto Exit;
        }
        RtlFreeHeap(RtlProcessHeap(), 0, NtFileName.Buffer);
#if DBG
        LdrpSectionCreates++;
#endif
    } else {
        KnownDll = TRUE;
    }

    ViewBase = NULL;
    ViewSize = 0;

#if DBG
    LdrpSectionMaps++;
    if (LdrpDisplayLoadTime) {
        NtQueryPerformanceCounter(&MapBeginTime, NULL);
    }
#endif

    //
    // arrange for debugger to pick up the image name
    //

    ArbitraryUserPointer = Teb->NtTib.ArbitraryUserPointer;
    Teb->NtTib.ArbitraryUserPointer = (PVOID)FullDllName.Buffer;
    st = NtMapViewOfSection(
            Section,
            NtCurrentProcess(),
            (PVOID *)&ViewBase,
            0L,
            0L,
            NULL,
            &ViewSize,
            ViewShare,
            0L,
            PAGE_READWRITE
            );
    Teb->NtTib.ArbitraryUserPointer = ArbitraryUserPointer;

    if (!NT_SUCCESS(st)) {
        DbgPrintEx(
            DPFLTR_LDR_ID,
            LDR_ERROR_DPFLTR,
            "LDR: %s - failed to map view of section; status = %x\n",
            __FUNCTION__,
            st);

        goto Exit;
    }

    NtHeaders = RtlImageNtHeader(ViewBase);
    if ( !NtHeaders ) {
        NtUnmapViewOfSection(NtCurrentProcess(),ViewBase);
        st = STATUS_INVALID_IMAGE_FORMAT;
        DbgPrintEx(
            DPFLTR_LDR_ID,
            LDR_ERROR_DPFLTR,
            "LDR: %s - unable to map ViewBase (%p) to image headers; failing with status %x\n",
            __FUNCTION__,
            ViewBase,
            st);
        goto Exit;
        }

#if _WIN64
    if (st != STATUS_IMAGE_NOT_AT_BASE &&
        (NtCurrentPeb()->NtGlobalFlag & FLG_LDR_TOP_DOWN) &&
        !(NtHeaders->FileHeader.Characteristics & IMAGE_FILE_RELOCS_STRIPPED)) {

        // The image was loaded at its preferred base and has relocs.  Map
        // it again using the default ViewBase.  This will collide with the
        // initial mapping, and force the mm to choose a new base address.
        // On Win64, the mm will do this top-down, forcing the DLL to
        // be mapped above 4gb if possible, to catch pointer truncations.
        PCUNICODE_STRING SystemDll;
        PVOID AlternateViewBase;
        ULONG_PTR AlternateViewSize;
        NTSTATUS AlternateSt;
        BOOLEAN LoadTopDown;

        LoadTopDown = TRUE;
        SystemDll = &User32String;
        if (RtlEqualUnicodeString(&BaseDllName, &User32String, TRUE)) {
            LoadTopDown = FALSE;
        } else {
            SystemDll = &Kernel32String;
            if (RtlEqualUnicodeString(&BaseDllName, &Kernel32String, TRUE)) {
                LoadTopDown = FALSE;
            }
        }
        if (LoadTopDown) {
            //
            // Map the image again.  It will collide with itself, and
            // the 64-bit mm will find a new base address for it,
            // working top-down
            //
            AlternateViewBase = NULL;
            AlternateViewSize = 0;
            ArbitraryUserPointer = Teb->NtTib.ArbitraryUserPointer;
            Teb->NtTib.ArbitraryUserPointer = (PVOID)FullDllName.Buffer;
            AlternateSt = NtMapViewOfSection(
                    Section,
                    NtCurrentProcess(),
                    (PVOID *)&AlternateViewBase,
                    0L,
                    0L,
                    NULL,
                    &AlternateViewSize,
                    ViewShare,
                    0L,
                    PAGE_READWRITE
                    );
            Teb->NtTib.ArbitraryUserPointer = ArbitraryUserPointer;
            if (NT_SUCCESS(AlternateSt)) {
                //
                // Success.  Unmap the original image from the low
                // part of the address space and keep the new mapping
                // which was allocated top-down.
                //
                NtUnmapViewOfSection(NtCurrentProcess(), ViewBase);
                ViewSize = AlternateViewSize;
                ViewBase = AlternateViewBase;
                NtHeaders = RtlImageNtHeader(ViewBase);
                st = AlternateSt;
                if ( !NtHeaders ) {
                    NtUnmapViewOfSection(NtCurrentProcess(),AlternateViewBase);
                    st = STATUS_INVALID_IMAGE_FORMAT;
                    goto Exit;
                }
            }
        }
    }
#endif

#if defined (BUILD_WOW6432)
    if (NtHeaders->OptionalHeader.SectionAlignment < NativePageSize) {

        NTSTATUS stTemp;
        SIZE_T ReturnLength;
        MEMORY_BASIC_INFORMATION MemoryInformation;

        stTemp = NtQueryVirtualMemory (NtCurrentProcess(),
                                       NtHeaders,
                                       MemoryBasicInformation,
                                       &MemoryInformation,
                                       sizeof MemoryInformation,
                                       &ReturnLength);

        if (! NT_SUCCESS(stTemp)) {

            st = stTemp;
            DbgPrintEx(
                DPFLTR_LDR_ID,
                LDR_ERROR_DPFLTR,
                "LDR: %s - Call to NtQueryVirtualMemory (%ls) failed with status 0x%08lx\n",
                __FUNCTION__,
                FullDllName.Buffer,
                st);

            NtUnmapViewOfSection(NtCurrentProcess(), ViewBase);
            ViewBase = NULL;
            goto Exit;
        }

        if ((MemoryInformation.Protect != PAGE_READONLY) &&
            (MemoryInformation.Protect != PAGE_EXECUTE_READ)) {

            stTemp = LdrpWx86FormatVirtualImage (&FullDllName,
                                                 (PIMAGE_NT_HEADERS32)NtHeaders,
                                                 ViewBase);

             if (!NT_SUCCESS(stTemp)) {
                 st = stTemp;
                 DbgPrintEx(
                     DPFLTR_LDR_ID,
                     LDR_ERROR_DPFLTR,
                     "LDR: %s - Call to LdrpWx86FormatVirtualImage(%ls) failed with status 0x%08lx\n",
                     __FUNCTION__,
                     FullDllName.Buffer,
                     st);

                 NtUnmapViewOfSection(NtCurrentProcess(), ViewBase);
                 ViewBase = NULL;
                 goto Exit;
             }
        }
    }
#endif

    Cor20Header = RtlImageDirectoryEntryToData(ViewBase,
                                               TRUE,
                                               IMAGE_DIRECTORY_ENTRY_COM_DESCRIPTOR,
                                               &Cor20HeaderSize);
    OriginalViewBase = ViewBase;

    //
    // if this is an IL_ONLY image, then validate the image now
    //

    if ((Cor20Header != NULL) && 
        ((Cor20Header->Flags & COMIMAGE_FLAGS_ILONLY) != 0)) {
        
        //
        // NOTICE-2001/05/21-MGrier
        // This is wacky but code later on depends on the fact that st *was* STATUS_IMAGE_MACHINE_TYPE_MISMATCH
        // and got overwritten with STATUS_SUCCESS.  This in effect means that COR images can never have
        // relocation information.  XP Bug #400007.
        //
        st = LdrpCheckCorImage (Cor20Header,
                                &FullDllName,
                                &ViewBase,
                                &Cor20ILOnly);

        if (!NT_SUCCESS(st))
            goto Exit;
    }


#if DBG
    if (LdrpDisplayLoadTime) {
        NtQueryPerformanceCounter(&MapEndTime, NULL);
        MapElapsedTime.QuadPart = MapEndTime.QuadPart - MapBeginTime.QuadPart;
        DbgPrint("Map View of Section Time %ld %ws\n", MapElapsedTime.LowPart, DllName);
    }
#endif

    //
    // Allocate a data table entry.
    //

    Entry = LdrpAllocateDataTableEntry(ViewBase);

    if (!Entry) {
        DbgPrintEx(
            DPFLTR_LDR_ID,
            LDR_ERROR_DPFLTR,
            "LDR: %s - failed to allocate new data table entry for %p\n",
            __FUNCTION__,
            ViewBase);

        st = STATUS_NO_MEMORY;
        goto Exit;
    }

    Entry->Flags = 0;

    if (StaticLink)
        Entry->Flags |= LDRP_STATIC_LINK;

    if (Redirected)
        Entry->Flags |= LDRP_REDIRECTED;

    Entry->LoadCount = 0;

    Entry->FullDllName = FullDllName;
    FullDllName.Length = 0;
    FullDllName.MaximumLength = 0;
    FullDllName.Buffer = NULL;

    Entry->BaseDllName = BaseDllName;
    BaseDllName.Length = 0;
    BaseDllName.MaximumLength = 0;
    BaseDllName.Buffer = NULL;

    Entry->EntryPoint = LdrpFetchAddressOfEntryPoint(Entry->DllBase);

#if LDRDBG
    if (ShowSnaps)
        DbgPrint(
            "LDR: LdrpMapDll: Full Name %wZ, Base Name %wZ\n",
            &Entry->FullDllName,
            &Entry->BaseDllName);
#endif

    LdrpInsertMemoryTableEntry(Entry);

    LdrpSendDllNotifications (Entry,
                              LDR_DLL_NOTIFICATION_REASON_LOADED,
                              (st == STATUS_IMAGE_NOT_AT_BASE) ? LDR_DLL_LOADED_FLAG_RELOCATED : 0);

    if ( st == STATUS_IMAGE_MACHINE_TYPE_MISMATCH ) {

        PIMAGE_NT_HEADERS ImageHeader = RtlImageNtHeader( NtCurrentPeb()->ImageBaseAddress );

        //
        // apps compiled for NT 3.x and below can load cross architecture
        // images
        //

        ErrorStatus = STATUS_SUCCESS;
        ErrorResponse = ResponseCancel;

        if ( ImageHeader->OptionalHeader.MajorSubsystemVersion <= 3 ) {

            Entry->EntryPoint = 0;

            //
            // Hard Error Time
            //

            //
            // Its error time...
            //

            ErrorParameters[0] = (ULONG_PTR)&FullDllName;

            ErrorStatus = NtRaiseHardError(
                            STATUS_IMAGE_MACHINE_TYPE_MISMATCH,
                            1,
                            1,
                            ErrorParameters,
                            OptionOkCancel,
                            &ErrorResponse
                            );
            }
        if ( NT_SUCCESS(ErrorStatus) && ErrorResponse == ResponseCancel ) {


#if defined(_AMD64_) || defined(_IA64_)


            RtlRemoveInvertedFunctionTable(&LdrpInvertedFunctionTable,
                                           Entry->DllBase);

#endif

            RemoveEntryList(&Entry->InLoadOrderLinks);
            RemoveEntryList(&Entry->InMemoryOrderLinks);
            RemoveEntryList(&Entry->HashLinks);
            LdrpDeallocateDataTableEntry(Entry);

            if ( ImageHeader->OptionalHeader.MajorSubsystemVersion <= 3 ) {
                if ( LdrpInLdrInit ) {
                    LdrpFatalHardErrorCount++;
                    }
                }
            st = STATUS_INVALID_IMAGE_FORMAT;
            goto Exit;
            }
        }
    else {
        if (NtHeaders->FileHeader.Characteristics & IMAGE_FILE_DLL) {
            Entry->Flags |= LDRP_IMAGE_DLL;
            }

        if (!(Entry->Flags & LDRP_IMAGE_DLL)) {
            Entry->EntryPoint = 0;
            }
        }

    *LdrDataTableEntry = Entry;

    if (st == STATUS_IMAGE_NOT_AT_BASE) {

        Entry->Flags |= LDRP_IMAGE_NOT_AT_BASE;

        //
        // now find the colliding dll. If we can not find a dll,
        // then the colliding dll must be dynamic memory
        //

        ImageBase = (PUCHAR)NtHeaders->OptionalHeader.ImageBase;
        ImageBounds = ImageBase + ViewSize;

        CollidingDllFound = FALSE;

        ScanHead = &PebLdr.InLoadOrderModuleList;
        ScanNext = ScanHead->Flink;

        while ( ScanNext != ScanHead ) {
            ScanEntry = CONTAINING_RECORD(ScanNext, LDR_DATA_TABLE_ENTRY, InLoadOrderLinks);
            ScanNext = ScanNext->Flink;

            ScanBase = (PUCHAR)ScanEntry->DllBase;
            ScanTop = ScanBase + ScanEntry->SizeOfImage;

            //
            // when we unload, the memory order links flink field is nulled.
            // this is used to skip the entry pending list removal.
            //

            if ( !ScanEntry->InMemoryOrderLinks.Flink ) {
                continue;
                }

            //
            // See if the base address of the scan image is within the relocating dll
            // or if the top address of the scan image is within the relocating dll
            //

            if ( (ImageBase >= ScanBase && ImageBase <= ScanTop)

                 ||

                 (ImageBounds >= ScanBase && ImageBounds <= ScanTop)

                 ||

                 (ScanBase >= ImageBase && ScanBase <= ImageBounds)

                 ){

                CollidingDllFound = TRUE;
                CollidingDll = &ScanEntry->FullDllName;
                break;
                }
            }

        if ( !CollidingDllFound ) {
            CollidingDll = &DynamicallyAllocatedMemoryString;
            }

#if DBG
        if ( BeginTime.LowPart || BeginTime.HighPart ) {
            DbgPrint(
                "\nLDR: %s Relocating Image Name %ws\n",
                __FUNCTION__,
                DllName
                );
        }
        LdrpSectionRelocates++;
#endif

        if (Entry->Flags & LDRP_IMAGE_DLL) {

            BOOLEAN AllowRelocation;
            PCUNICODE_STRING SystemDll;

            if (!(NtHeaders->FileHeader.Characteristics & IMAGE_FILE_RELOCS_STRIPPED)) {
                PVOID pBaseRelocs;
                ULONG BaseRelocCountBytes = 0;

                //
                // If the image doesn't have the reloc stripped bit set and there's no
                // relocs in the data directory, allow this through.  This is probably
                // a pure forwarder dll or data w/o relocs.
                //

                pBaseRelocs = RtlImageDirectoryEntryToData(
                        ViewBase, TRUE, IMAGE_DIRECTORY_ENTRY_BASERELOC, &BaseRelocCountBytes);

                if (!pBaseRelocs && !BaseRelocCountBytes)
                    goto NoRelocNeeded;
            }

            //
            // decide whether or not to allow the relocation
            // certain system dll's like user32 and kernel32 are not relocatable
            // since addresses within these dll's are not always stored per process
            // do not allow these dll's to be relocated
            //

            AllowRelocation = TRUE;
            SystemDll = &User32String;
            if ( RtlEqualUnicodeString(&Entry->BaseDllName, SystemDll, TRUE)) {
                AllowRelocation = FALSE;
            } else {
                SystemDll = &Kernel32String;
                if (RtlEqualUnicodeString(&Entry->BaseDllName, SystemDll, TRUE))
                    AllowRelocation = FALSE;
            }

            if ( !AllowRelocation && KnownDll ) {

                //
                // totally disallow the relocation since this is a knowndll
                // that matches our system binaries and is being relocated
                //

                //
                // Hard Error Time
                //

                ErrorParameters[0] = (ULONG_PTR)SystemDll;
                ErrorParameters[1] = (ULONG_PTR)CollidingDll;

                NtRaiseHardError(
                    STATUS_ILLEGAL_DLL_RELOCATION,
                    2,
                    3,
                    ErrorParameters,
                    OptionOk,
                    &ErrorResponse);

                if ( LdrpInLdrInit ) {
                    LdrpFatalHardErrorCount++;
                }

                st = STATUS_CONFLICTING_ADDRESSES;
                goto skipreloc;
            }

            st = LdrpSetProtection (ViewBase, FALSE);
            if (NT_SUCCESS(st)) {
                __try {
                    st = LdrRelocateImage(ViewBase,
                                "LDR",
                                STATUS_SUCCESS,
                                STATUS_CONFLICTING_ADDRESSES,
                                STATUS_INVALID_IMAGE_FORMAT);
                } __except (LdrpGenericExceptionFilter(GetExceptionInformation(), __FUNCTION__)) {
                    st = GetExceptionCode();
                }

                if (NT_SUCCESS(st)) {
                    //
                    // If we did relocations, then map the section again.
                    // this will force the debug event
                    //

                    //
                    // arrange for debugger to pick up the image name
                    //

                    ArbitraryUserPointer = Teb->NtTib.ArbitraryUserPointer;
                    Teb->NtTib.ArbitraryUserPointer = (PVOID)FullDllName.Buffer;

                    st = NtMapViewOfSection(
                        Section,
                        NtCurrentProcess(),
                        (PVOID *)&ViewBase,
                        0L,
                        0L,
                        NULL,
                        &ViewSize,
                        ViewShare,
                        0L,
                        PAGE_READWRITE);

                    Teb->NtTib.ArbitraryUserPointer = ArbitraryUserPointer;

                    if ((st != STATUS_CONFLICTING_ADDRESSES) && !NT_SUCCESS(st)) {
                        DbgPrintEx(
                            DPFLTR_LDR_ID,
                            LDR_ERROR_DPFLTR,
                            "[%x,%x] LDR: Failed to map view of section; ntstatus = %x\n",
                            HandleToULong(NtCurrentTeb()->ClientId.UniqueProcess),
                            HandleToULong(NtCurrentTeb()->ClientId.UniqueThread),
                            st);

                        goto Exit;
                    }

                    st = LdrpSetProtection (ViewBase, TRUE);
                }
            }
skipreloc:
            //
            // if the set protection failed, or if the relocation failed, then
            // remove the partially loaded dll from the lists and clear entry
            // that it has been freed.
            //

            if ( !NT_SUCCESS(st) ) {

#if defined(_AMD64_) || defined(_IA64_)


                RtlRemoveInvertedFunctionTable(&LdrpInvertedFunctionTable,
                                               Entry->DllBase);

#endif

                RemoveEntryList(&Entry->InLoadOrderLinks);
                RemoveEntryList(&Entry->InMemoryOrderLinks);
                RemoveEntryList(&Entry->HashLinks);
                if (ShowSnaps) {
                    DbgPrint("LDR: Fixups unsuccessfully re-applied @ %p\n",
                           ViewBase);
                }
                goto Exit;
            }

            if (ShowSnaps) {                
                DbgPrint("LDR: Fixups successfully re-applied @ %p\n",
                       ViewBase);               
            }
        } else {
NoRelocNeeded:

            st = STATUS_SUCCESS;

            //
            // arrange for debugger to pick up the image name
            //

            ArbitraryUserPointer = Teb->NtTib.ArbitraryUserPointer;
            Teb->NtTib.ArbitraryUserPointer = (PVOID)FullDllName.Buffer;

            st = NtMapViewOfSection(
                Section,
                NtCurrentProcess(),
                (PVOID *)&ViewBase,
                0L,
                0L,
                NULL,
                &ViewSize,
                ViewShare,
                0L,
                PAGE_READWRITE
                );
            Teb->NtTib.ArbitraryUserPointer = ArbitraryUserPointer;

            //
            // NOTICE-2001/04/09-MGrier
            // If the thing was relocated, we get back the failure status STATUS_CONFLICTING_ADDRESSES
            // but of all the strange things, the relocations aren't done.  I have questions in to folks
            // asking about this behavior but who knows how many legacy apps depend on dlls that statically
            // link to EXEs which from time to time are not loaded at their default addresses.
            //
            if ((st != STATUS_CONFLICTING_ADDRESSES) && !NT_SUCCESS(st))
                DbgPrintEx(
                    DPFLTR_LDR_ID,
                    LDR_ERROR_DPFLTR,
                    "[%x,%x] LDR: %s - NtMapViewOfSection on no reloc needed dll failed with status %x\n",
                    HandleToULong(Teb->ClientId.UniqueProcess),
                    HandleToULong(Teb->ClientId.UniqueThread),
                    __FUNCTION__,
                    st);
            else
                st = STATUS_SUCCESS;

            if (ShowSnaps)
                DbgPrint("LDR: Fixups won't be re-applied to non-Dll @ %p\n", ViewBase);
        }
    }

    //
    // if this is NOT an IL_ONLY image, then validate the image now after applying the
    // fixups
    //

    if ((Cor20Header != NULL) && 
        ((Cor20Header->Flags & COMIMAGE_FLAGS_ILONLY) == 0)) {
        
        st = LdrpCheckCorImage (Cor20Header,
                                &Entry->FullDllName,
                                &ViewBase,
                                &Cor20ILOnly);
        if (!NT_SUCCESS (st)) {
            goto Exit;
        }
    }

    if (Cor20ILOnly) {
        Entry->Flags |= LDRP_COR_IMAGE;
    }

    if (ViewBase != OriginalViewBase) {
        Entry->Flags |= LDRP_COR_OWNS_UNMAP;
    }

#if defined(_X86_)
    if ( LdrpNumberOfProcessors > 1 && (Entry->Flags & LDRP_IMAGE_DLL) ) {
        LdrpValidateImageForMp(Entry);
        }
#endif

    ViewBase = NULL;

Exit:
    if (ViewBase != NULL) {

        if (Cor20ILOnly) {
            LdrpCorUnloadImage(ViewBase);
        }

        if (ViewBase == OriginalViewBase) {
            NtUnmapViewOfSection(NtCurrentProcess(),ViewBase);
        }
    }

    if (Section != NULL) {
        NtClose(Section);
    }

    if (AppCompatDllName != NULL) {
        (*RtlFreeStringRoutine)(AppCompatDllName);
    }

    if (FullDllName.Buffer != NULL) {
        LdrpFreeUnicodeString(&FullDllName);
    }

#if DBG
    if (!NT_SUCCESS(st) && (ShowSnaps || st != STATUS_DLL_NOT_FOUND))
        DbgPrint("LDR: %s(%ws) failing 0x%lx\n", __FUNCTION__, DllName, st);
#endif

    return st;
}

//#define SAFER_DEBUGGING
//#define SAFER_ERRORS_ARE_FATAL


NTSTATUS
LdrpCodeAuthzCheckDllAllowed(
    IN PCUNICODE_STRING  FileName,
    IN HANDLE           FileImageHandle
    )
/*++

Routine Description:

    This routine dynamically loads ADVAPI32.DLL and obtains entry points
    to the WinSafer sandboxing APIs, so that the trustworthiness of the
    requested library can be determined.  Libraries that are equally
    or greater "trusted" than the Access Token of the process loading
    the library are allowed to be loaded.  Libraries that are less
    trusted than the process will be denied.

    Care must be taken to ensure that this function is kept threadsafe
    without requiring the use of critical sections.  In particular,
    the usage of the variable "AdvApi32ModuleHandleMaster" needs to be
    accessed only through a copy, since it may be changed unexpected by
    another thread.

Arguments:

    FileName - the fully qualified NT filename of the library being loaded.
        The filename will be used to perform path validation checks.

    FileImageHandle - the opened file handle of the library being loaded.
        This handle will be used to read the contents of the library to
        perform size and hash validation checks by WinSafer.

Return Value:

    STATUS_SUCCESS - the library is of equal or greater trustworthiness
        than that process it is being loaded into, and should be allowed.

    STATUS_NOT_FOUND - the library does not have a trust level configured
        and no default rule in in effect (treat same as STATUS_SUCCESS).

    STATUS_ACCESS_DENIED - the library is less trustworthy than the
        process and the load should be denied.

    Other non-success - an error occurred trying to load/determine the
        trust of the library, so the load should be denied.
        (including STATUS_ENTRY_POINT_NOT_FOUND)

--*/
{


#define SAFER_USER_KEY_NAME L"\\Software\\Policies\\Microsoft\\Windows\\Safer\\CodeIdentifiers"

    typedef BOOL (WINAPI *ComputeAccessTokenFromCodeAuthzLevelT) (
        IN SAFER_LEVEL_HANDLE      LevelObject,
        IN HANDLE           InAccessToken         OPTIONAL,
        OUT PHANDLE         OutAccessToken,
        IN DWORD            dwFlags,
        IN LPVOID           lpReserved
        );

    typedef BOOL (WINAPI *IdentifyCodeAuthzLevelWT) (
        IN DWORD                dwCheckFlags,
        IN PSAFER_CODE_PROPERTIES    CodeProperties,
        OUT SAFER_LEVEL_HANDLE        *pLevelObject,
        IN LPVOID               lpReserved
        );

    typedef BOOL (WINAPI *CloseCodeAuthzLevelT) (
        IN SAFER_LEVEL_HANDLE      hLevelObject);

    NTSTATUS Status;
    SAFER_LEVEL_HANDLE hAuthzLevel = NULL;
    SAFER_CODE_PROPERTIES codeproperties;
    DWORD dwCompareResult = 0;
    HANDLE hProcessToken= NULL;
    HANDLE TempAdvApi32Handle = NULL;

    const static SID_IDENTIFIER_AUTHORITY NtAuthority =
            SECURITY_NT_AUTHORITY;
    const static UNICODE_STRING UnicodeSafeBootKeyName =
        RTL_CONSTANT_STRING(L"\\Registry\\MACHINE\\System\\CurrentControlSet\\Control\\SafeBoot\\Option");
    const static UNICODE_STRING UnicodeSafeBootValueName =
        RTL_CONSTANT_STRING(L"OptionValue");
    const static OBJECT_ATTRIBUTES ObjectAttributesSafeBoot =
        RTL_CONSTANT_OBJECT_ATTRIBUTES(&UnicodeSafeBootKeyName, OBJ_CASE_INSENSITIVE);
    const static UNICODE_STRING UnicodeKeyName =
        RTL_CONSTANT_STRING(L"\\Registry\\Machine\\Software\\Policies\\Microsoft\\Windows\\Safer\\CodeIdentifiers");
    const static UNICODE_STRING UnicodeTransparentValueName =
        RTL_CONSTANT_STRING(L"TransparentEnabled");
    const static OBJECT_ATTRIBUTES ObjectAttributesCodeIdentifiers =
        RTL_CONSTANT_OBJECT_ATTRIBUTES(&UnicodeKeyName, OBJ_CASE_INSENSITIVE);
    const static UNICODE_STRING ModuleNameAdvapi =
        RTL_CONSTANT_STRING(L"ADVAPI32.DLL");
    const static ANSI_STRING ProcedureNameIdentify =
        RTL_CONSTANT_STRING("SaferIdentifyLevel");
    const static ANSI_STRING ProcedureNameCompute =
        RTL_CONSTANT_STRING("SaferComputeTokenFromLevel");
    const static ANSI_STRING ProcedureNameClose =
        RTL_CONSTANT_STRING("SaferCloseLevel");

    static volatile HANDLE AdvApi32ModuleHandleMaster = (HANDLE) (ULONG_PTR) -1;
    static IdentifyCodeAuthzLevelWT lpfnIdentifyCodeAuthzLevelW;
    static ComputeAccessTokenFromCodeAuthzLevelT
            lpfnComputeAccessTokenFromCodeAuthzLevel;
    static CloseCodeAuthzLevelT lpfnCloseCodeAuthzLevel;


    PIMAGE_NT_HEADERS NtHeader;

    NtHeader = RtlImageNtHeader(NtCurrentPeb()->ImageBaseAddress);

    // Check for NULL header.

    if (!NtHeader) {
        return STATUS_SUCCESS;
    }

    // Continue only if this is a windows subsystem app. We run into all sorts
    // of problems because kernel32 might not initialize for others.

    if (!((NtHeader->OptionalHeader.Subsystem == IMAGE_SUBSYSTEM_WINDOWS_GUI) ||
        (NtHeader->OptionalHeader.Subsystem == IMAGE_SUBSYSTEM_WINDOWS_CUI))) {
        return STATUS_SUCCESS;
    }

    //
    // If either of these two cases are true, then we should bail out
    // as quickly as possible because we know that WinSafer evaluations
    // should definitely not occur for this process anymore.
    //
    TempAdvApi32Handle = AdvApi32ModuleHandleMaster;
    if (TempAdvApi32Handle == NULL) {
        // We tried to load ADVAPI32.DLL once before, but failed.
        Status = STATUS_ACCESS_DENIED;
        goto ExitHandler;
    } else if (TempAdvApi32Handle == LongToHandle(-2)) {
        // Indicates that DLL checking should never be done for this process.
        Status = STATUS_SUCCESS;
        goto ExitHandler;
    }


    //
    // Open a handle to the current process's access token.
    // We care only about the process token, and not the
    // thread impersonation token.
    //
    Status = NtOpenProcessToken(
                    NtCurrentProcess(),
                    TOKEN_QUERY,
                    &hProcessToken);
    if (!NT_SUCCESS(Status)) {
#ifdef SAFER_ERRORS_ARE_FATAL
        AdvApi32ModuleHandleMaster = NULL;
        Status = STATUS_ACCESS_DENIED;
#else
        AdvApi32ModuleHandleMaster = LongToHandle(-2);
        Status = STATUS_SUCCESS;
#endif
        goto ExitHandler;
    }


    //
    // If this is our first time through here, then we need to
    // load ADVAPI32.DLL and get pointers to our functions.
    //
    if (TempAdvApi32Handle == LongToHandle(-1))
    {
        static LONG LoadInProgress = 0;


        //
        // We need to prevent multiple threads from simultaneously
        // getting stuck and trying to load advapi at the same time.
        //
        if (InterlockedCompareExchange(&LoadInProgress, 1, 0) != 0) {
            Status = STATUS_SUCCESS;
            goto ExitHandler2;
        }

        //
        // Check if this process's access token is running as
        // the Local SYSTEM, LOCAL SERVICE or NETWORK SERVICE account, 
        // and disable enforcement if so.
        //
        {
            BYTE tokenuserbuff[sizeof(TOKEN_USER) + 128];
            PTOKEN_USER ptokenuser = (PTOKEN_USER) tokenuserbuff;
            BYTE localsystembuff[128];
            PSID LocalSystemSid = (PSID) localsystembuff;
            ULONG ulReturnLength;


            Status = NtQueryInformationToken(
                            hProcessToken, TokenUser,
                            tokenuserbuff, sizeof(tokenuserbuff),
                            &ulReturnLength);
            if (NT_SUCCESS(Status)) {
                Status = RtlInitializeSid(
                            LocalSystemSid,
                            (PSID_IDENTIFIER_AUTHORITY) &NtAuthority, 1);
                ASSERTMSG("InitializeSid should not fail.", NT_SUCCESS(Status));
                *RtlSubAuthoritySid(LocalSystemSid, 0) = SECURITY_LOCAL_SYSTEM_RID;

                if (RtlEqualSid(ptokenuser->User.Sid, LocalSystemSid)) {
                    goto FailSuccessfully;
                }
                *RtlSubAuthoritySid(LocalSystemSid, 0) = SECURITY_LOCAL_SERVICE_RID;

                if (RtlEqualSid(ptokenuser->User.Sid, LocalSystemSid)) {
                    goto FailSuccessfully;
                }
                *RtlSubAuthoritySid(LocalSystemSid, 0) = SECURITY_NETWORK_SERVICE_RID;

                if (RtlEqualSid(ptokenuser->User.Sid, LocalSystemSid)) {
                    goto FailSuccessfully;
                }
            }
        }


        //
        // If we are booting in safe mode and the user is a member of
        // the local Administrators group, then disable enforcement.
        // Notice that Windows itself does not perform any implicit
        // restriction of only allowing Administrators to log in during
        // Safe mode boot, so we must perform the test ourself.
        //
        {
            HANDLE hKeySafeBoot;
            BYTE QueryBuffer[sizeof(KEY_VALUE_PARTIAL_INFORMATION) + 64];
            PKEY_VALUE_PARTIAL_INFORMATION pKeyValueInfo =
                (PKEY_VALUE_PARTIAL_INFORMATION) QueryBuffer;
            DWORD dwActualSize;
            BOOLEAN bSafeModeBoot = FALSE;

            // We open the key for SET access (in addition to QUERY)
            // because only Administrators should be able to modify values
            // under this key.  This allows us to combine our test of
            // being an Administrator and having booted in Safe mode.
            Status = NtOpenKey(&hKeySafeBoot, KEY_QUERY_VALUE | KEY_SET_VALUE,
                               (POBJECT_ATTRIBUTES) &ObjectAttributesSafeBoot);
            if (NT_SUCCESS(Status)) {
                Status = NtQueryValueKey(
                            hKeySafeBoot,
                            (PUNICODE_STRING) &UnicodeSafeBootValueName,
                            KeyValuePartialInformation,
                            pKeyValueInfo,
                            sizeof(QueryBuffer),
                            &dwActualSize);
                NtClose(hKeySafeBoot);
                if (NT_SUCCESS(Status)) {
                    if (pKeyValueInfo->Type == REG_DWORD &&
                        pKeyValueInfo->DataLength == sizeof(DWORD) &&
                        *((PDWORD) pKeyValueInfo->Data) > 0) {
                        bSafeModeBoot = TRUE;
                    }
                }
            }

            if (bSafeModeBoot) {
FailSuccessfully:
                AdvApi32ModuleHandleMaster = LongToHandle(-2);
                Status = STATUS_SUCCESS;
                goto ExitHandler2;
            }
        }



        //
        // Allow a way for policy to enable whether transparent
        // enforcement should be enabled or not (default to disable).
        // Note that the following values have meanings:
        //      0 = Transparent WinSafer enforcement disabled.
        //      1 = means enable transparent EXE enforcement
        //     >1 = means enable transparent EXE and DLL enforcement.
        //
        {
            //
            // FUTURE-2001/01/09-kedard
            // see BUG 240635: change to use existence of policy instead.
            //
            HANDLE hKeyEnabled;
            BYTE QueryBuffer[sizeof(KEY_VALUE_PARTIAL_INFORMATION) + 64];
            PKEY_VALUE_PARTIAL_INFORMATION pKeyValueInfo =
                (PKEY_VALUE_PARTIAL_INFORMATION) QueryBuffer;
            DWORD dwActualSize;
            BOOLEAN bPolicyEnabled = FALSE;

            Status = NtOpenKey(&hKeyEnabled, KEY_QUERY_VALUE,
                               (POBJECT_ATTRIBUTES) &ObjectAttributesCodeIdentifiers);
            if (NT_SUCCESS(Status)) {
                Status = NtQueryValueKey(
                            hKeyEnabled,
                            (PUNICODE_STRING) &UnicodeTransparentValueName,
                            KeyValuePartialInformation,
                            pKeyValueInfo, sizeof(QueryBuffer), &dwActualSize);
                NtClose(hKeyEnabled);
                if (NT_SUCCESS(Status)) {
                    if (pKeyValueInfo->Type == REG_DWORD &&
                        pKeyValueInfo->DataLength == sizeof(DWORD) &&
                        *((PDWORD) pKeyValueInfo->Data) > 1) {
                        bPolicyEnabled = TRUE;
                    }
                }
            }


            //
            // There was no machine policy. Check if user policy is enabled.
            //

            if (!bPolicyEnabled) {
                UNICODE_STRING CurrentUserKeyPath;
                UNICODE_STRING SubKeyNameUser;
                OBJECT_ATTRIBUTES ObjectAttributesUser;
                ULONG SubKeyLength;

                //
                // Get the prefix for the user key.
                //

                Status = RtlFormatCurrentUserKeyPath( &CurrentUserKeyPath );

                if (NT_SUCCESS( Status ) ) {

                    SubKeyNameUser.Length = 0;
                    SubKeyLength = CurrentUserKeyPath.Length + sizeof(WCHAR) +
                                                   sizeof(SAFER_USER_KEY_NAME);

                    if (SubKeyLength > UNICODE_STRING_MAX_BYTES) {
                        Status = STATUS_NAME_TOO_LONG;
                        goto UserKeyCleanup;
                    }

                    SubKeyNameUser.MaximumLength = (USHORT)SubKeyLength;

                    //
                    // Allocate memory big enough to hold the unicode string.
                    //

                    SubKeyNameUser.Buffer = RtlAllocateHeap( 
                                                RtlProcessHeap(),
                                                MAKE_TAG( TEMP_TAG ),
                                                SubKeyNameUser.MaximumLength);

                    if (SubKeyNameUser.Buffer != NULL) {

                        //
                        // Copy the prefix into the string.
                        // This is of the type Registry\S-1-5-21-xxx-xxx-xxx-xxx.
                        //

                        Status = RtlAppendUnicodeStringToString(
                                    &SubKeyNameUser, 
                                    &CurrentUserKeyPath );

                        if (NT_SUCCESS( Status ) ) {

                            //
                            // Append the Safer suffix.
                            //

                            Status = RtlAppendUnicodeToString( 
                                         &SubKeyNameUser,
                                         SAFER_USER_KEY_NAME );

                            if (NT_SUCCESS( Status ) ) {

                                InitializeObjectAttributes(
                                    &ObjectAttributesUser,
                                    &SubKeyNameUser,
                                    OBJ_CASE_INSENSITIVE,
                                    NULL,
                                    NULL
                                );

                                Status = NtOpenKey( &hKeyEnabled,KEY_QUERY_VALUE,
                                             (POBJECT_ATTRIBUTES) &ObjectAttributesUser);

                                if (NT_SUCCESS(Status)) {
                                    Status = NtQueryValueKey(
                                                hKeyEnabled,
                                                (PUNICODE_STRING) &UnicodeTransparentValueName,
                                                KeyValuePartialInformation,
                                                pKeyValueInfo, sizeof(QueryBuffer), &dwActualSize);
                                    NtClose (hKeyEnabled);
                                    if (NT_SUCCESS(Status)) {
                                        if (pKeyValueInfo->Type == REG_DWORD &&
                                            pKeyValueInfo->DataLength == sizeof(DWORD) &&
                                            *((PDWORD) pKeyValueInfo->Data) > 1) {
                                            bPolicyEnabled = TRUE;
                                        }
                                    }
                                }
                            }

                        }
                        RtlFreeHeap(RtlProcessHeap(), 0, SubKeyNameUser.Buffer);
                    }

UserKeyCleanup:
                    RtlFreeUnicodeString( &CurrentUserKeyPath );
                }
            }



            if (!bPolicyEnabled) {
                goto FailSuccessfully;
            }
        }


        //
        // Finally load the library.  We'll pass a special flag in
        // DllCharacteristics to eliminate WinSafer checking on advapi
        // itself, but that (currently) doesn't affect dependent DLLs
        // so we still depend on the above LoadInProgress flag to
        // prevent unintended recursion.
        //
        {
            //
            // NTRAID#NTBUG9-241835-2000/11/27-johnla
            // the WinSafer supression doesn't affect dependencies.
            //
            ULONG DllCharacteristics = IMAGE_FILE_SYSTEM;
            Status = LdrLoadDll(UNICODE_NULL,
                                &DllCharacteristics,  // prevents recursion too
                                &ModuleNameAdvapi,
                                &TempAdvApi32Handle);
            if (!NT_SUCCESS(Status)) {
                #if DBG
                DbgPrint("LDR: AuthzCheck: load failure on advapi (Status=%d) inside %d for %wZ\n",
                         Status, HandleToUlong(NtCurrentTeb()->ClientId.UniqueProcess), FileName);
                #endif
                AdvApi32ModuleHandleMaster = NULL;
                Status = STATUS_ENTRYPOINT_NOT_FOUND;
                goto ExitHandler2;
            }
        }



        //
        // Get function pointers to the APIs that we'll need.  If we fail
        // to get pointers for any of them, then just unload advapi and
        // ignore all future attempts to load it within this process.
        //
        Status = LdrpGetProcedureAddress(
                TempAdvApi32Handle,
                (PANSI_STRING) &ProcedureNameIdentify,
                0,
                (PVOID*)&lpfnIdentifyCodeAuthzLevelW, 
                FALSE);

        if (!NT_SUCCESS(Status) || !lpfnIdentifyCodeAuthzLevelW) {
            #if DBG
            DbgPrint("LDR: AuthzCheck: advapi getprocaddress identify (Status=%X) inside %d for %wZ\n",
                     Status, HandleToUlong(NtCurrentTeb()->ClientId.UniqueProcess), FileName);
            #endif
            LdrUnloadDll(TempAdvApi32Handle);
            AdvApi32ModuleHandleMaster = NULL;
            Status = STATUS_ENTRYPOINT_NOT_FOUND;
            goto ExitHandler2;
        }

        Status = LdrpGetProcedureAddress(
                TempAdvApi32Handle,
                (PANSI_STRING) &ProcedureNameCompute,
                0,
                (PVOID*)&lpfnComputeAccessTokenFromCodeAuthzLevel,
                FALSE);

        if (!NT_SUCCESS(Status) ||
            !lpfnComputeAccessTokenFromCodeAuthzLevel) {
            #if DBG
            DbgPrint("LDR: AuthzCheck: advapi getprocaddress compute (Status=%X) inside %d for %wZ\n",
                     Status, HandleToUlong(NtCurrentTeb()->ClientId.UniqueProcess), FileName);
            #endif
            LdrUnloadDll(TempAdvApi32Handle);
            AdvApi32ModuleHandleMaster = NULL;
            Status = STATUS_ENTRYPOINT_NOT_FOUND;
            goto ExitHandler2;
        }

        Status = LdrpGetProcedureAddress(
                TempAdvApi32Handle,
                (PANSI_STRING) &ProcedureNameClose,
                0,
                (PVOID*)&lpfnCloseCodeAuthzLevel,
                FALSE);

        if (!NT_SUCCESS(Status) || !lpfnCloseCodeAuthzLevel) {
            #if DBG
            DbgPrint("LDR: AuthzCheck: advapi getprocaddress close (Status=%X) inside %d for %wZ\n",
                     Status, HandleToUlong(NtCurrentTeb()->ClientId.UniqueProcess), FileName);
            #endif
            LdrUnloadDll(TempAdvApi32Handle);
            AdvApi32ModuleHandleMaster = NULL;
            Status = STATUS_ENTRYPOINT_NOT_FOUND;
            goto ExitHandler2;
        }
        AdvApi32ModuleHandleMaster = TempAdvApi32Handle;
    }


    //
    // Prepare the code properties struct.
    //
    RtlZeroMemory(&codeproperties, sizeof(codeproperties));
    codeproperties.cbSize = sizeof(codeproperties);
    codeproperties.dwCheckFlags =
            (SAFER_CRITERIA_IMAGEPATH | SAFER_CRITERIA_IMAGEHASH |
             SAFER_CRITERIA_IMAGEPATH_NT | SAFER_CRITERIA_NOSIGNEDHASH);
    ASSERTMSG("FileName not null terminated",
              FileName->Buffer[FileName->Length / sizeof(WCHAR)] == UNICODE_NULL);
    codeproperties.ImagePath = FileName->Buffer;
    codeproperties.hImageFileHandle = FileImageHandle;


    //
    // Ask the system to find the Authorization Level that classifies it.
    //
    ASSERT(lpfnIdentifyCodeAuthzLevelW != NULL);
    if (lpfnIdentifyCodeAuthzLevelW(
            1,                      // 1 structure
            &codeproperties,        // details to identify
            &hAuthzLevel,           // Safer level
            NULL))                  // reserved.
    {
        //
        // We found an Authorization Level applicable to this application.
        // See if this Level represents something less trusted than us.
        //

        ASSERT(lpfnComputeAccessTokenFromCodeAuthzLevel != NULL);
        if (!lpfnComputeAccessTokenFromCodeAuthzLevel(
                hAuthzLevel,                // Safer Level
                hProcessToken,              // source token.
                NULL,                       // output token not used for compare.
                SAFER_TOKEN_COMPARE_ONLY,    // we want to compare
                &dwCompareResult))          // reserved
        {
            // failed to compare, for some reason.
            #if DBG
            DbgPrint("LDR: AuthzCheck: compute failed in %d for %wZ\n",
                     HandleToUlong(NtCurrentTeb()->ClientId.UniqueProcess), FileName);
            #endif
            Status = STATUS_ACCESS_DISABLED_BY_POLICY_DEFAULT;
        } else if (dwCompareResult == -1) {
            // less privileged, deny access.
            #ifdef SAFER_DEBUGGING
            DbgPrint("LDR: AuthzCheck: compute access denied in %d for %wZ\n",
                     HandleToUlong(NtCurrentTeb()->ClientId.UniqueProcess), FileName);
            #endif
            Status = STATUS_ACCESS_DISABLED_BY_POLICY_DEFAULT;
        } else {
            // greater or equally privileged, allow access to load.
            #ifdef SAFER_DEBUGGING
            DbgPrint("LDR: AuthzCheck: compute access ok in %d for %wZ\n",
                     HandleToUlong(NtCurrentTeb()->ClientId.UniqueProcess), FileName);
            #endif
            Status = STATUS_SUCCESS;
        }

        ASSERT(lpfnCloseCodeAuthzLevel != NULL);
        lpfnCloseCodeAuthzLevel(hAuthzLevel);

    } else {
        // No authorization level found for this DLL, and the
        // policy does not have a Default Level in effect.
        Status = STATUS_NOT_FOUND;
    }

ExitHandler2:
    NtClose(hProcessToken);

ExitHandler:
    return Status;
}




NTSTATUS
LdrpCreateDllSection (
    IN PCUNICODE_STRING NtFullDllName,
    IN HANDLE DllFile,
    IN PULONG DllCharacteristics OPTIONAL,
    OUT PHANDLE SectionHandle
    )
{
    HANDLE File;
    NTSTATUS st;
    OBJECT_ATTRIBUTES ObjectAttributes;
    IO_STATUS_BLOCK IoStatus;
    SECTION_IMAGE_INFORMATION ImageInformation;

    //
    // NOTICE-2002/03/10-ELi
    // If DllFile is specified, then it will be closed before returning from
    // the function
    //

    if (!DllFile) {

        //
        // Since ntsd does not search paths well, we can't use
        // relative object names.
        //

        InitializeObjectAttributes (&ObjectAttributes,
                                    (PUNICODE_STRING)NtFullDllName,
                                    OBJ_CASE_INSENSITIVE,
                                    NULL,
                                    NULL);

        //
        // Open for FILE_GENERIC_READ as well. This is needed in case Safer hash
        // policy exists for DLLs. If we do not have READ access we will continue
        // with lesses access. Safer code, if it needs to be executed will fail
        // later on.
        //

        st = NtOpenFile (&File,
                         SYNCHRONIZE | FILE_EXECUTE | FILE_READ_DATA,
                         &ObjectAttributes,
                         &IoStatus,
                         FILE_SHARE_READ | FILE_SHARE_DELETE,
                         FILE_NON_DIRECTORY_FILE | FILE_SYNCHRONOUS_IO_NONALERT);

        if (!NT_SUCCESS(st)) {

            st = NtOpenFile (&File,
                             SYNCHRONIZE | FILE_EXECUTE,
                             &ObjectAttributes,
                             &IoStatus,
                             FILE_SHARE_READ | FILE_SHARE_DELETE,
                             FILE_NON_DIRECTORY_FILE | FILE_SYNCHRONOUS_IO_NONALERT);

        }

        if (!NT_SUCCESS(st)) {

            if (ShowSnaps) {
                DbgPrint ("LDR: %s - NtOpenFile failed; status = %x\n", __FUNCTION__, st);
            }

            //
            // Swizzle the return value. If we got STATUS_OBJECT_NAME_NOT_FOUND,
            // it should really be STATUS_DLL_NOT_FOUND.
            //
            if (st == STATUS_OBJECT_NAME_NOT_FOUND) {
                if (ShowSnaps) {
                    DbgPrint("LDR: %s - Turning NtOpenFile's %x into %x\n", __FUNCTION__, st, STATUS_DLL_NOT_FOUND);
                }
                st = STATUS_DLL_NOT_FOUND;
            }
            
            *SectionHandle = NULL;
            return st;
        }
    }
    else {
        File = DllFile;
    }

    //
    // Create a memory section of the library file's contents.
    //

    st = NtCreateSection (SectionHandle,
                          SECTION_MAP_READ | SECTION_MAP_EXECUTE | SECTION_MAP_WRITE | SECTION_QUERY,
                          NULL,
                          NULL,
                          PAGE_EXECUTE,
                          SEC_IMAGE,
                          File);

    if (NT_SUCCESS(st)) {

        if ((ARGUMENT_PRESENT(DllCharacteristics)) &&
            (*DllCharacteristics & IMAGE_FILE_SYSTEM)) {
#if DBG
            DbgPrint("LDR: WinSafer AuthzCheck on %wZ skipped by request\n",
                     &NtFullDllName);
#endif
        }
        else {

#if defined(_WIN64)

            //
            // WOW64 processes should not load 64-bit dlls (advapi32.dll)
            // but the DLLs will get SAFERized when the 32-bit load kicks in.
            //

            if (UseWOW64 == FALSE) {
#endif

                //
                // Ask the WinSafer code authorization sandboxing
                // infrastructure if the library load should be permitted.
                //
                // The Winsafer check is here since the
                // IMAGE_LOADER_FLAGS_COMPLUS information from the image
                // will be made available shortly.
                //
                // Query the section to determine whether or not this is a
                // .NET image.  On failure to query, the error will be returned.
                //

                st = NtQuerySection (*SectionHandle,
                                     SectionImageInformation,
                                     &ImageInformation,
                                     sizeof (ImageInformation),
                                     NULL);

                if (!NT_SUCCESS (st)) {
                    NtClose (*SectionHandle);
                    *SectionHandle = NULL;
                    NtClose (File);
                    return st;
                }

                if ((ImageInformation.LoaderFlags & IMAGE_LOADER_FLAGS_COMPLUS) == 0) {

                    st = LdrpCodeAuthzCheckDllAllowed (NtFullDllName, File);

                    if ((!NT_SUCCESS(st)) && (st != STATUS_NOT_FOUND)) {
#if !DBG
                        if (ShowSnaps)
#endif
                        {
                            DbgPrint("LDR: Loading of (%wZ) blocked by WinSafer\n",
                                     &NtFullDllName);
                        }

                        NtClose (*SectionHandle);
                        *SectionHandle = NULL;
                        NtClose (File);
                        return st;
                    }
                    st = STATUS_SUCCESS;
                }
#if defined(_WIN64)        
            }
#endif
        }
    }
    else {

        //
        // Hard error time.
        //

        ULONG_PTR ErrorParameters[1];
        ULONG ErrorResponse;

        *SectionHandle = NULL;
        ErrorParameters[0] = (ULONG_PTR)NtFullDllName;

        NtRaiseHardError (STATUS_INVALID_IMAGE_FORMAT,
                          1,
                          1,
                          ErrorParameters,
                          OptionOk,
                          &ErrorResponse);

        if (LdrpInLdrInit)  {
            LdrpFatalHardErrorCount += 1;
        }

#if DBG
        if (st != STATUS_INVALID_IMAGE_NE_FORMAT &&
            st != STATUS_INVALID_IMAGE_LE_FORMAT &&
            st != STATUS_INVALID_IMAGE_WIN_16    &&
            st != STATUS_INVALID_IMAGE_WIN_32    &&
            st != STATUS_INVALID_IMAGE_WIN_64    &&
            LdrpShouldDbgPrintStatus(st)) {

            DbgPrint("LDR: " __FUNCTION__ " - NtCreateSection %wZ failed. Status == 0x%08lx\n",
                     NtFullDllName,
                     st
                    );
        }
#endif
    }

    NtClose (File);
    
    return st;
}


NTSTATUS
LdrpSnapIAT (
    IN PLDR_DATA_TABLE_ENTRY LdrDataTableEntry_Export,
    IN PLDR_DATA_TABLE_ENTRY LdrDataTableEntry_Import,
    IN PCIMAGE_IMPORT_DESCRIPTOR ImportDescriptor,
    IN BOOLEAN SnapForwardersOnly
    )

/*++

Routine Description:

    This function snaps the Import Address Table for this
    Import Descriptor.

Arguments:

    LdrDataTableEntry_Export - Information about the image to import from.

    LdrDataTableEntry_Import - Information about the image to import to.

    ImportDescriptor - Contains a pointer to the IAT to snap.

    SnapForwardersOnly - TRUE if just snapping forwarders only.

Return Value:

    Status value

--*/

{
    NTSTATUS st;
    ULONG ExportSize;
    PCIMAGE_EXPORT_DIRECTORY ExportDirectory;
    PIMAGE_THUNK_DATA Thunk, OriginalThunk;
    PCSZ ImportName;
    ULONG ForwarderChain;
    PIMAGE_NT_HEADERS NtHeaders;
    PIMAGE_SECTION_HEADER NtSection;
    ULONG i, Rva;
    PVOID IATBase;
    SIZE_T IATSize;
    ULONG LittleIATSize;
    ULONG OldProtect;

    ExportDirectory = (PCIMAGE_EXPORT_DIRECTORY)RtlImageDirectoryEntryToData(
                       LdrDataTableEntry_Export->DllBase,
                       TRUE,
                       IMAGE_DIRECTORY_ENTRY_EXPORT,
                       &ExportSize
                       );

    if (!ExportDirectory) {
        KdPrint(("LDR: %wZ doesn't contain an EXPORT table\n", &LdrDataTableEntry_Export->BaseDllName));
        return STATUS_INVALID_IMAGE_FORMAT;
    }

    //
    // Determine the location and size of the IAT.  If the linker did
    // not tell use explicitly, then use the location and size of the
    // image section that contains the import table.
    //

    IATBase = RtlImageDirectoryEntryToData( LdrDataTableEntry_Import->DllBase,
                                            TRUE,
                                            IMAGE_DIRECTORY_ENTRY_IAT,
                                            &LittleIATSize
                                          );
    if (IATBase == NULL) {
        NtHeaders = RtlImageNtHeader( LdrDataTableEntry_Import->DllBase );
        if (! NtHeaders) {
            return STATUS_INVALID_IMAGE_FORMAT;
        }
        NtSection = IMAGE_FIRST_SECTION( NtHeaders );
        Rva = NtHeaders->OptionalHeader.DataDirectory[ IMAGE_DIRECTORY_ENTRY_IMPORT ].VirtualAddress;
        if (Rva != 0) {
            for (i=0; i<NtHeaders->FileHeader.NumberOfSections; i++) {
                if (Rva >= NtSection->VirtualAddress &&
                    Rva < (NtSection->VirtualAddress + NtSection->SizeOfRawData)
                   ) {
                    IATBase = (PVOID)
                        ((ULONG_PTR)(LdrDataTableEntry_Import->DllBase) + NtSection->VirtualAddress);

                    LittleIATSize = NtSection->Misc.VirtualSize;
                    if (LittleIATSize == 0) {
                        LittleIATSize = NtSection->SizeOfRawData;
                    }
                    break;
                }

                ++NtSection;
            }
        }

        if (IATBase == NULL) {
            KdPrint(( "LDR: Unable to unprotect IAT for %wZ (Image Base %p)\n",
                      &LdrDataTableEntry_Import->BaseDllName,
                      LdrDataTableEntry_Import->DllBase
                   ));
            return STATUS_INVALID_IMAGE_FORMAT;
        }
    }
    IATSize = LittleIATSize;

    st = NtProtectVirtualMemory( NtCurrentProcess(),
                                 &IATBase,
                                 &IATSize,
                                 PAGE_READWRITE,
                                 &OldProtect
                               );
    if (!NT_SUCCESS(st)) {
        KdPrint(( "LDR: Unable to unprotect IAT for %wZ (Status %x)\n",
                  &LdrDataTableEntry_Import->BaseDllName,
                  st
               ));
        return st;
    }

    //
    // If just snapping forwarded entries, walk that list
    //
    if (SnapForwardersOnly) {
        ImportName = (PCSZ)((ULONG_PTR)LdrDataTableEntry_Import->DllBase + ImportDescriptor->Name);
        ForwarderChain = ImportDescriptor->ForwarderChain;
        while (ForwarderChain != -1) {
            OriginalThunk = (PIMAGE_THUNK_DATA)((ULONG_PTR)LdrDataTableEntry_Import->DllBase +
                            ImportDescriptor->OriginalFirstThunk +
                            (ForwarderChain * sizeof(IMAGE_THUNK_DATA)));
            Thunk = (PIMAGE_THUNK_DATA)((ULONG_PTR)LdrDataTableEntry_Import->DllBase +
                            ImportDescriptor->FirstThunk +
                            (ForwarderChain * sizeof(IMAGE_THUNK_DATA)));
            ForwarderChain = (ULONG) Thunk->u1.Ordinal;
            try {
                st = LdrpSnapThunk(LdrDataTableEntry_Export->DllBase,
                        LdrDataTableEntry_Import->DllBase,
                        OriginalThunk,
                        Thunk,
                        ExportDirectory,
                        ExportSize,
                        TRUE,
                        ImportName
                        );
                Thunk++;
            }
            except (LdrpGenericExceptionFilter(GetExceptionInformation(), __FUNCTION__)) {
                st = GetExceptionCode();
                DbgPrintEx(
                    DPFLTR_LDR_ID,
                    LDR_ERROR_DPFLTR,
                    "LDR: %s - caught exception %08lx snapping thunks (#1)\n",
                    __FUNCTION__,
                    st);
            }
            if (!NT_SUCCESS(st) ) {
                break;
            }
        }
    }
    else if (ImportDescriptor->FirstThunk) {

        //
        // Otherwise, walk through the IAT and snap all the thunks.
        //

        Thunk = (PIMAGE_THUNK_DATA)((ULONG_PTR)LdrDataTableEntry_Import->DllBase + ImportDescriptor->FirstThunk);

        NtHeaders = RtlImageNtHeader( LdrDataTableEntry_Import->DllBase );

        //
        // If the OriginalFirstThunk field does not point inside the image, then ignore
        // it.  This is will detect bogus Borland Linker 2.25 images that did not fill
        // this field in.
        //

        if (ImportDescriptor->Characteristics < NtHeaders->OptionalHeader.SizeOfHeaders ||
            ImportDescriptor->Characteristics >= NtHeaders->OptionalHeader.SizeOfImage
           ) {
            OriginalThunk = Thunk;
        } else {
            OriginalThunk = (PIMAGE_THUNK_DATA)((ULONG_PTR)LdrDataTableEntry_Import->DllBase +
                            ImportDescriptor->OriginalFirstThunk);
        }
        ImportName = (PCSZ)((ULONG_PTR)LdrDataTableEntry_Import->DllBase + ImportDescriptor->Name);
        while (OriginalThunk->u1.AddressOfData) {
            try {
                st = LdrpSnapThunk(LdrDataTableEntry_Export->DllBase,
                        LdrDataTableEntry_Import->DllBase,
                        OriginalThunk,
                        Thunk,
                        ExportDirectory,
                        ExportSize,
                        TRUE,
                        ImportName
                        );
                OriginalThunk += 1;
                Thunk += 1;
            }
            except (LdrpGenericExceptionFilter(GetExceptionInformation(), __FUNCTION__)) {
                st = GetExceptionCode();
                DbgPrintEx(
                    DPFLTR_LDR_ID,
                    LDR_ERROR_DPFLTR,
                    "LDR: %s - caught exception %08lx snapping thunks (#2)\n",
                    __FUNCTION__,
                    st);
            }

            if (!NT_SUCCESS(st) ) {
                break;
            }
        }
    }

    //
    // Restore protection for IAT and flush instruction cache.
    //

    NtProtectVirtualMemory (NtCurrentProcess(),
                            &IATBase,
                            &IATSize,
                            OldProtect,
                            &OldProtect);

    NtFlushInstructionCache (NtCurrentProcess(), IATBase, LittleIATSize);

    return st;
}


NTSTATUS
LdrpSnapThunk (
    IN PVOID DllBase,
    IN PVOID ImageBase,
    IN PIMAGE_THUNK_DATA OriginalThunk,
    IN OUT PIMAGE_THUNK_DATA Thunk,
    IN PCIMAGE_EXPORT_DIRECTORY ExportDirectory,
    IN ULONG ExportSize,
    IN BOOLEAN StaticSnap,
    IN PCSZ DllName
    )

/*++

Routine Description:

    This function snaps a thunk using the specified Export Section data.
    If the section data does not support the thunk, then the thunk is
    partially snapped (Dll field is still non-null, but snap address is
    set).

Arguments:

    DllBase - Base of Dll.

    ImageBase - Base of image that contains the thunks to snap.

    Thunk - On input, supplies the thunk to snap.  When successfully
        snapped, the function field is set to point to the address in
        the DLL, and the DLL field is set to NULL.

    ExportDirectory - Supplies the Export Section data from a DLL.

    StaticSnap - If TRUE, then loader is attempting a static snap,
                 and any ordinal/name lookup failure will be reported.

Return Value:

    STATUS_SUCCESS or STATUS_PROCEDURE_NOT_FOUND

--*/

{
    BOOLEAN Ordinal;
    USHORT OrdinalNumber;
    ULONG OriginalOrdinalNumber;
    PCIMAGE_IMPORT_BY_NAME AddressOfData;
    PULONG NameTableBase;
    PUSHORT NameOrdinalTableBase;
    PULONG Addr;
    USHORT HintIndex;
    NTSTATUS st;
    PCSZ ImportString;

    //
    // Determine if snap is by name, or by ordinal.
    //

    Ordinal = (BOOLEAN)IMAGE_SNAP_BY_ORDINAL(OriginalThunk->u1.Ordinal);

    if (Ordinal) {
        OriginalOrdinalNumber = (ULONG)IMAGE_ORDINAL(OriginalThunk->u1.Ordinal);
        OrdinalNumber = (USHORT)(OriginalOrdinalNumber - ExportDirectory->Base);
        ImportString = NULL;
    } else {

        //
        // NOTICE-2000/08/27-DavePr
        // This should never happen, because we will only be called if either
        // Ordinal is set or ImageBase is not NULL.  But to satisfy prefix...
        //

        if (ImageBase == NULL) {
#if LDRDBG
            DbgPrint("LDR: ImageBase=NULL and !Ordinal\n");
#endif
            return STATUS_PROCEDURE_NOT_FOUND;
        }

        OriginalOrdinalNumber = 0;

         //
         // Change AddressOfData from an RVA to a VA.
         //

         AddressOfData = (PCIMAGE_IMPORT_BY_NAME)((ULONG_PTR)ImageBase + ((ULONG_PTR)OriginalThunk->u1.AddressOfData & 0xffffffff));
         ImportString = (PCSZ)AddressOfData->Name;

         //
         // Lookup Name in NameTable
         //

         NameTableBase = (PULONG)((ULONG_PTR)DllBase + (ULONG)ExportDirectory->AddressOfNames);
         NameOrdinalTableBase = (PUSHORT)((ULONG_PTR)DllBase + (ULONG)ExportDirectory->AddressOfNameOrdinals);

         //
         // Before dropping into binary search, see if
         // the hint index results in a successful
         // match. If the hint index is zero, then
         // drop into binary search.
         //

        HintIndex = AddressOfData->Hint;
        if ((ULONG)HintIndex < ExportDirectory->NumberOfNames &&
            !strcmp(ImportString, (PSZ)((ULONG_PTR)DllBase + NameTableBase[HintIndex]))) {
            OrdinalNumber = NameOrdinalTableBase[HintIndex];
#if LDRDBG
            if (ShowSnaps) {
                DbgPrint("LDR: Snapping %s\n", ImportString);
            }
#endif
        } else {
#if LDRDBG
             if (HintIndex) {
                 DbgPrint("LDR: Warning HintIndex Failure. Name %s (%lx) Hint 0x%lx\n",
                     ImportString,
                     (ULONG)ImportString,
                     (ULONG)HintIndex);
             }
#endif
             OrdinalNumber = LdrpNameToOrdinal(
                               ImportString,
                               ExportDirectory->NumberOfNames,
                               DllBase,
                               NameTableBase,
                               NameOrdinalTableBase);
        }
    }

    //
    // If OrdinalNumber is not within the Export Address Table,
    // then DLL does not implement function. Snap to LDRP_BAD_DLL.
    //

    if ((ULONG)OrdinalNumber >= ExportDirectory->NumberOfFunctions) {
baddllref:
#if DBG
        if (StaticSnap) {
            if (Ordinal) {
                DbgPrint("LDR: Can't locate ordinal 0x%lx\n", OriginalOrdinalNumber);
            }
            else {
                DbgPrint("LDR: Can't locate %s\n", ImportString);
            }
        }
#endif
        if ( StaticSnap ) {
            //
            // Hard Error Time
            //

            ULONG_PTR ErrorParameters[3];
            UNICODE_STRING ErrorDllName, ErrorEntryPointName;
            ANSI_STRING AnsiScratch;
            ULONG ParameterStringMask;
            ULONG ErrorResponse;
            NTSTATUS Status;

            RtlInitAnsiString(&AnsiScratch,DllName ? DllName : "Unknown");
            Status = RtlAnsiStringToUnicodeString(&ErrorDllName,&AnsiScratch,TRUE);
            if (NT_SUCCESS (Status)) {

                ErrorParameters[1] = (ULONG_PTR)&ErrorDllName;
                ParameterStringMask = 2;

                if ( Ordinal ) {
                    ErrorParameters[0] = OriginalOrdinalNumber;
                } else {
                    RtlInitAnsiString (&AnsiScratch, ImportString);
                    Status = RtlAnsiStringToUnicodeString(&ErrorEntryPointName,&AnsiScratch,TRUE);
                    if (NT_SUCCESS (Status)) {

                        ErrorParameters[0] = (ULONG_PTR)&ErrorEntryPointName;
                        ParameterStringMask = 3;
                    }
                }

                if (NT_SUCCESS (Status)) {
                    NtRaiseHardError(
                      Ordinal ? STATUS_ORDINAL_NOT_FOUND : STATUS_ENTRYPOINT_NOT_FOUND,
                      2,
                      ParameterStringMask,
                      ErrorParameters,
                      OptionOk,
                      &ErrorResponse
                      );

                    if (LdrpInLdrInit) {
                        LdrpFatalHardErrorCount += 1;
                    }
                    if (!Ordinal) {
                        RtlFreeUnicodeString (&ErrorEntryPointName);
                        RtlRaiseStatus (STATUS_ENTRYPOINT_NOT_FOUND);
                    }
                }

                RtlFreeUnicodeString(&ErrorDllName);

            }

            RtlRaiseStatus(STATUS_ORDINAL_NOT_FOUND);
        }

        Thunk->u1.Function = (ULONG_PTR)LDRP_BAD_DLL;
        st = Ordinal ? STATUS_ORDINAL_NOT_FOUND : STATUS_ENTRYPOINT_NOT_FOUND;

    } else {
        Addr = (PULONG)((ULONG_PTR)DllBase + (ULONG)ExportDirectory->AddressOfFunctions);
        Thunk->u1.Function = ((ULONG_PTR)DllBase + Addr[OrdinalNumber]);
        if (Thunk->u1.Function > (ULONG_PTR)ExportDirectory &&
            Thunk->u1.Function < ((ULONG_PTR)ExportDirectory + ExportSize)) {

            UNICODE_STRING UnicodeString;
            ANSI_STRING ForwardDllName;
            PVOID ForwardDllHandle;
            PANSI_STRING ForwardProcName;
            ULONG ForwardProcOrdinal;

            ImportString = (PCSZ)Thunk->u1.Function;
            ForwardDllName.Buffer = (PSZ)ImportString;
            // We should handle the case where strchr returns NULL or >32k.

            ForwardDllName.Length = (USHORT)(strchr(ImportString, '.') - ImportString);
            ForwardDllName.MaximumLength = ForwardDllName.Length;

            //
            // Most forwarders seem to point to NTDLL and since we know
            // that every process has ntdll already loaded and pinned
            // let's optimize away all the calls to load it.
            //

            if (ASCII_STRING_IS_NTDLL(&ForwardDllName)) {
                ForwardDllHandle = LdrpNtDllDataTableEntry->DllBase;
                st = STATUS_SUCCESS;
            } else {
                ForwardDllHandle = NULL;
                st = RtlAnsiStringToUnicodeString(&UnicodeString, &ForwardDllName, TRUE);

                if (NT_SUCCESS(st)) {
                    UNICODE_STRING AnotherUnicodeString = {0, 0, NULL};
                    PUNICODE_STRING UnicodeStringToUse = &UnicodeString;
                    ULONG LdrpLoadDllFlags = 0;

                    st = RtlDosApplyFileIsolationRedirection_Ustr(
                            RTL_DOS_APPLY_FILE_REDIRECTION_USTR_FLAG_RESPECT_DOT_LOCAL,
                            &UnicodeString,
                            &DefaultExtension,
                            NULL,
                            &AnotherUnicodeString,
                            &UnicodeStringToUse,
                            NULL,
                            NULL,
                            NULL);

                    if (NT_SUCCESS(st)) {
                        LdrpLoadDllFlags |= LDRP_LOAD_DLL_FLAG_DLL_IS_REDIRECTED;
                    }

                    if (st == STATUS_SXS_KEY_NOT_FOUND) {
                        st = STATUS_SUCCESS;
                    }

                    if (NT_SUCCESS(st)) {
                        st = LdrpLoadDll(LdrpLoadDllFlags, NULL, NULL, UnicodeStringToUse, &ForwardDllHandle, FALSE);
                    }

                    if (AnotherUnicodeString.Buffer != NULL) {
                        RtlFreeUnicodeString(&AnotherUnicodeString);
                    }
                    RtlFreeUnicodeString(&UnicodeString);
                }
            }

            if (!NT_SUCCESS(st)) {
                goto baddllref;
            }

            RtlInitAnsiString (&ForwardDllName,
                               ImportString + ForwardDllName.Length + 1);

            if (ForwardDllName.Length > 1 &&
                *ForwardDllName.Buffer == '#') {

                ForwardProcName = NULL;

                st = RtlCharToInteger (ForwardDllName.Buffer+1,
                                       0,
                                       &ForwardProcOrdinal);

                if (!NT_SUCCESS(st)) {
                    goto baddllref;
                }
            }
            else {
                ForwardProcName = &ForwardDllName;

                //
                // Following line is not needed since this is a by name lookup,
                // but keep it in so the code can compile W4.
                //

                ForwardProcOrdinal = 0;
            }

            st = LdrpGetProcedureAddress (ForwardDllHandle,
                                          ForwardProcName,
                                          ForwardProcOrdinal,
                                          (PVOID*)&Thunk->u1.Function,
                                          FALSE);

            if (!NT_SUCCESS(st)) {
                goto baddllref;
            }
        }
        else {
            if ( !Addr[OrdinalNumber] ) {
                goto baddllref;
            }
        }
        st = STATUS_SUCCESS;
    }

    return st;
}


USHORT
LdrpNameToOrdinal (
    IN PCSZ Name,
    IN ULONG NumberOfNames,
    IN PVOID DllBase,
    IN PULONG NameTableBase,
    IN PUSHORT NameOrdinalTableBase
    )
{
    LONG High;
    LONG Low;
    LONG Middle;
    LONG Result;

    //
    // Lookup the import name in the name table using a binary search.
    //

    Low = 0;
    Middle = 0;
    High = NumberOfNames - 1;
    while (High >= Low) {

        //
        // Compute the next probe index and compare the import name
        // with the export name entry.
        //

        Middle = (Low + High) >> 1;
        Result = strcmp(Name, (PCHAR)((ULONG_PTR)DllBase + NameTableBase[Middle]));

        if (Result < 0) {
            High = Middle - 1;

        } else if (Result > 0) {
            Low = Middle + 1;

        } else {
            break;
        }
    }

    //
    // If the high index is less than the low index, then a matching
    // table entry was not found. Otherwise, get the ordinal number
    // from the ordinal table.
    //

    if (High < Low) {
        return (USHORT)-1;
    } else {
        return NameOrdinalTableBase[Middle];
    }

}


VOID
LdrpUpdateLoadCount2 (
    IN PLDR_DATA_TABLE_ENTRY LdrDataTableEntry,
    IN ULONG UpdateCountHow
    )
{
    WCHAR PreAllocatedStringBuffer[DOS_MAX_PATH_LENGTH];
    UNICODE_STRING PreAllocatedString = {0, sizeof(PreAllocatedStringBuffer), PreAllocatedStringBuffer};

    LdrpUpdateLoadCount3(LdrDataTableEntry, UpdateCountHow, &PreAllocatedString);
}


VOID
LdrpUpdateLoadCount3(
    IN PLDR_DATA_TABLE_ENTRY LdrDataTableEntry,
    IN ULONG UpdateCountHow,
    IN OUT PUNICODE_STRING PreAllocatedRedirectionBuffer OPTIONAL
    )
/*++

Routine Description:

    This function dereferences a loaded DLL adjusting its reference
    count.  It then dereferences each dll referenced by this dll.

Arguments:

    LdrDataTableEntry - Supplies the address of the DLL to dereference

    UpdateCountHow - 
        LDRP_UPDATE_LOAD_COUNT_INCREMENT add one
        LDRP_UPDATE_LOAD_COUNT_DECREMENT subtract one
        LDRP_UPDATE_LOAD_COUNT_PIN       set to 0xffff

    PreAllocatedRedirectionBuffer - optional pointer to a caller-allocated
        (usually on the stack) fixed-sized buffer to use for redirection
        to avoid having a large buffer on our stack used during recursion.

Return Value:

    None.

--*/

{
    PCIMAGE_IMPORT_DESCRIPTOR ImportDescriptor;
    PCIMAGE_BOUND_IMPORT_DESCRIPTOR NewImportDescriptor;
    PCIMAGE_BOUND_FORWARDER_REF NewImportForwarder;
    PCSZ ImportName, NewImportStringBase;
    ULONG i, ImportSize, NewImportSize;
    ANSI_STRING AnsiString;
    PUNICODE_STRING ImportDescriptorName_U;
    PUNICODE_STRING ImportDescriptorNameToUse; // overrides ImportDescriptorName_U when redirection is turned on for a DLL
    PLDR_DATA_TABLE_ENTRY Entry;
    NTSTATUS st;
    PIMAGE_THUNK_DATA FirstThunk;
    UNICODE_STRING DynamicRedirectionString;
    RTL_CALLER_ALLOCATED_ACTIVATION_CONTEXT_STACK_FRAME ActivationFrame = { sizeof(ActivationFrame), RTL_CALLER_ALLOCATED_ACTIVATION_CONTEXT_STACK_FRAME_FORMAT_WHISTLER };

    RtlActivateActivationContextUnsafeFast(&ActivationFrame, LdrDataTableEntry->EntryPointActivationContext);
    __try {
        switch (UpdateCountHow
            ) {
            case LDRP_UPDATE_LOAD_COUNT_PIN:
            case LDRP_UPDATE_LOAD_COUNT_INCREMENT:
                if (LdrDataTableEntry->Flags & LDRP_LOAD_IN_PROGRESS) {
                    __leave;
                } else {
                    LdrDataTableEntry->Flags |= LDRP_LOAD_IN_PROGRESS;
                }
                break;
            case LDRP_UPDATE_LOAD_COUNT_DECREMENT:
                if (LdrDataTableEntry->Flags & LDRP_UNLOAD_IN_PROGRESS) {
                    __leave;
                } else {
                    LdrDataTableEntry->Flags |= LDRP_UNLOAD_IN_PROGRESS;
                }
                break;
        }

        //
        // For each DLL used by this DLL, reference or dereference the DLL.
        //

        if (LdrDataTableEntry->Flags & LDRP_COR_IMAGE) {
            //
            // The image is COR.  Ignore its import table and make it import
            // mscoree only.
            //
            const PCUNICODE_STRING xImportName = &MscoreeDllString;

            if (LdrpCheckForLoadedDll( NULL,
                                       xImportName,
                                       TRUE,
                                       FALSE,
                                       &Entry
                                     )
               ) {
                if ( Entry->LoadCount != 0xffff ) {
                    PCSTR SnapString = NULL;
                    switch (UpdateCountHow) {
                    case LDRP_UPDATE_LOAD_COUNT_PIN:
                        Entry->LoadCount = 0xffff;
                        SnapString = "Pin";
                        break;
                    case LDRP_UPDATE_LOAD_COUNT_INCREMENT:
                        Entry->LoadCount++;
                        SnapString = "Refcount";
                        break;
                    case LDRP_UPDATE_LOAD_COUNT_DECREMENT:
                        Entry->LoadCount--;
                        SnapString = "Derefcount";
                        break;
                    }
                    if (ShowSnaps) {
                        DbgPrint("LDR: %s %wZ (%lx)\n",
                                SnapString,
                                xImportName,
                                (ULONG)Entry->LoadCount
                                );
                    }
                }
                LdrpUpdateLoadCount3(Entry, UpdateCountHow, PreAllocatedRedirectionBuffer);
            }
            __leave;
        }

        ImportDescriptorName_U = &NtCurrentTeb()->StaticUnicodeString;

        //
        // See if there is a bound import table.  If so, walk that to
        // determine DLL names to reference or dereference.  Avoids touching
        // the .idata section
        //
        NewImportDescriptor = (PCIMAGE_BOUND_IMPORT_DESCRIPTOR)RtlImageDirectoryEntryToData(
                               LdrDataTableEntry->DllBase,
                               TRUE,
                               IMAGE_DIRECTORY_ENTRY_BOUND_IMPORT,
                               &NewImportSize
                               );
        if (NewImportDescriptor) {
            switch (UpdateCountHow) {
                case LDRP_UPDATE_LOAD_COUNT_INCREMENT:
                case LDRP_UPDATE_LOAD_COUNT_PIN:
                    LdrDataTableEntry->Flags |= LDRP_LOAD_IN_PROGRESS;
                    break;
                case LDRP_UPDATE_LOAD_COUNT_DECREMENT:
                    LdrDataTableEntry->Flags |= LDRP_UNLOAD_IN_PROGRESS;
                    break;
            }
            NewImportStringBase = (LPSTR)NewImportDescriptor;
            while (NewImportDescriptor->OffsetModuleName) {
                ImportName = NewImportStringBase +
                             NewImportDescriptor->OffsetModuleName;
                RtlInitAnsiString(&AnsiString, ImportName);
                st = RtlAnsiStringToUnicodeString(ImportDescriptorName_U, &AnsiString, FALSE);
                if ( NT_SUCCESS(st) ) {
                    BOOLEAN Redirected = FALSE;

                    ImportDescriptorNameToUse = ImportDescriptorName_U;

                    RtlInitEmptyUnicodeString(&DynamicRedirectionString, NULL, 0);

                    st = RtlDosApplyFileIsolationRedirection_Ustr(
                            RTL_DOS_APPLY_FILE_REDIRECTION_USTR_FLAG_RESPECT_DOT_LOCAL,
                            ImportDescriptorName_U,
                            &DefaultExtension,
                            PreAllocatedRedirectionBuffer,
                            &DynamicRedirectionString,
                            &ImportDescriptorNameToUse,
                            NULL,
                            NULL,
                            NULL);
                    if (NT_SUCCESS(st)) {
                        Redirected = TRUE;
                    } else if (st == STATUS_SXS_KEY_NOT_FOUND) {
                        st = STATUS_SUCCESS;
                    }

                    if (NT_SUCCESS(st)) {
                        if (LdrpCheckForLoadedDll( NULL,
                                                   ImportDescriptorNameToUse,
                                                   TRUE,
                                                   Redirected,
                                                   &Entry
                                                 )
                           ) {
                            if ( Entry->LoadCount != 0xffff ) {
                                PCSTR SnapString = NULL;
                                switch (UpdateCountHow) {
                                case LDRP_UPDATE_LOAD_COUNT_PIN:
                                    Entry->LoadCount = 0xffff;
                                    SnapString = "Pin";
                                    break;
                                case LDRP_UPDATE_LOAD_COUNT_INCREMENT:
                                    Entry->LoadCount++;
                                    SnapString = "Refcount";
                                    break;
                                case LDRP_UPDATE_LOAD_COUNT_DECREMENT:
                                    Entry->LoadCount--;
                                    SnapString = "Derefcount";
                                    break;
                                }
                                if (ShowSnaps) {
                                    DbgPrint("LDR: %s %wZ (%lx)\n",
                                            SnapString,
                                            ImportDescriptorNameToUse,
                                            (ULONG)Entry->LoadCount
                                            );
                                }
                            }
                            LdrpUpdateLoadCount3(Entry, UpdateCountHow, PreAllocatedRedirectionBuffer);
                        }

                        if (DynamicRedirectionString.Buffer != NULL)
                            RtlFreeUnicodeString(&DynamicRedirectionString);
                    }
                }

                NewImportForwarder = (PCIMAGE_BOUND_FORWARDER_REF)(NewImportDescriptor+1);
                for (i=0; i<NewImportDescriptor->NumberOfModuleForwarderRefs; i++) {
                    ImportName = NewImportStringBase +
                                 NewImportForwarder->OffsetModuleName;

                    RtlInitAnsiString(&AnsiString, ImportName);
                    st = RtlAnsiStringToUnicodeString(ImportDescriptorName_U, &AnsiString, FALSE);
                    if ( NT_SUCCESS(st) ) {
                        BOOLEAN Redirected = FALSE;

                        ImportDescriptorNameToUse = ImportDescriptorName_U;
                        RtlInitEmptyUnicodeString(&DynamicRedirectionString, NULL, 0);

                        st = RtlDosApplyFileIsolationRedirection_Ustr(
                                RTL_DOS_APPLY_FILE_REDIRECTION_USTR_FLAG_RESPECT_DOT_LOCAL,
                                ImportDescriptorName_U,
                                &DefaultExtension,
                                PreAllocatedRedirectionBuffer,
                                &DynamicRedirectionString,
                                &ImportDescriptorNameToUse,
                                NULL,
                                NULL,
                                NULL);
                        if (NT_SUCCESS(st)) {
                            Redirected = TRUE;
                        } else if (st == STATUS_SXS_KEY_NOT_FOUND) {
                            st = STATUS_SUCCESS;
                        }

                        if (NT_SUCCESS(st)) {
                            if (LdrpCheckForLoadedDll( NULL,
                                                       ImportDescriptorNameToUse,
                                                       TRUE,
                                                       Redirected,
                                                       &Entry
                                                     )
                               ) {
                                if ( Entry->LoadCount != 0xffff ) {
                                    PCSTR SnapString = NULL;
                                    switch (UpdateCountHow) {
                                    case LDRP_UPDATE_LOAD_COUNT_PIN:
                                        Entry->LoadCount = 0xffff;
                                        SnapString = "Pin";
                                        break;
                                    case LDRP_UPDATE_LOAD_COUNT_INCREMENT:
                                        Entry->LoadCount++;
                                        SnapString = "Refcount";
                                        break;
                                    case LDRP_UPDATE_LOAD_COUNT_DECREMENT:
                                        Entry->LoadCount--;
                                        SnapString = "Derefcount";
                                        break;
                                    }
                                    if (ShowSnaps) {
                                        DbgPrint("LDR: %s %wZ (%lx)\n",
                                                SnapString,
                                                ImportDescriptorNameToUse,
                                                (ULONG)Entry->LoadCount
                                                );
                                    }
                                }
                                LdrpUpdateLoadCount3(Entry, UpdateCountHow, PreAllocatedRedirectionBuffer);
                            }

                            if (DynamicRedirectionString.Buffer != NULL)
                                RtlFreeUnicodeString(&DynamicRedirectionString);
                        }
                    }

                    NewImportForwarder += 1;
                }

                NewImportDescriptor = (PCIMAGE_BOUND_IMPORT_DESCRIPTOR)NewImportForwarder;
            }

            __leave;
        }

        ImportDescriptor = (PCIMAGE_IMPORT_DESCRIPTOR)RtlImageDirectoryEntryToData(
                            LdrDataTableEntry->DllBase,
                            TRUE,
                            IMAGE_DIRECTORY_ENTRY_IMPORT,
                            &ImportSize
                            );
        if (ImportDescriptor) {

            while (ImportDescriptor->Name && ImportDescriptor->FirstThunk) {

                //
                // Match code in walk that skips references like this. IE3 had
                // some dll's with these bogus links to url.dll. On load, the url.dll
                // ref was skipped. On unload, it was not skipped because
                // this code was missing.
                //
                // Since the skip logic is only in the old style import
                // descriptor path, it is only duplicated here.
                //
                // check for import that has no references
                //
                FirstThunk = (PIMAGE_THUNK_DATA)((ULONG_PTR)LdrDataTableEntry->DllBase + ImportDescriptor->FirstThunk);
                if ( !FirstThunk->u1.Function ) {
                    goto skipskippedimport;
                    }

                ImportName = (PCSZ)((ULONG_PTR)LdrDataTableEntry->DllBase + ImportDescriptor->Name);

                RtlInitAnsiString(&AnsiString, ImportName);
                st = RtlAnsiStringToUnicodeString(ImportDescriptorName_U, &AnsiString, FALSE);
                if ( NT_SUCCESS(st) ) {
                    BOOLEAN Redirected = FALSE;                    

                    ImportDescriptorNameToUse = ImportDescriptorName_U;
                    RtlInitEmptyUnicodeString(&DynamicRedirectionString, NULL, 0);

                    st = RtlDosApplyFileIsolationRedirection_Ustr(
                            RTL_DOS_APPLY_FILE_REDIRECTION_USTR_FLAG_RESPECT_DOT_LOCAL,
                            ImportDescriptorName_U,
                            &DefaultExtension,
                            PreAllocatedRedirectionBuffer,
                            &DynamicRedirectionString,
                            &ImportDescriptorNameToUse,
                            NULL,
                            NULL,
                            NULL);
                    if (NT_SUCCESS(st)) {
                        Redirected = TRUE;
                    } else if (st == STATUS_SXS_KEY_NOT_FOUND) {
                        st = STATUS_SUCCESS;
                    }

                    if (NT_SUCCESS(st)) {
                        if (LdrpCheckForLoadedDll( NULL,
                                                   ImportDescriptorNameToUse,
                                                   TRUE,
                                                   Redirected,
                                                   &Entry
                                                 )
                           ) {
                            if ( Entry->LoadCount != 0xffff ) {
                                PCSTR SnapString = NULL;
                                switch (UpdateCountHow) {
                                case LDRP_UPDATE_LOAD_COUNT_PIN:
                                    Entry->LoadCount = 0xffff;
                                    SnapString = "Pin";
                                    break;
                                case LDRP_UPDATE_LOAD_COUNT_INCREMENT:
                                    Entry->LoadCount++;
                                    SnapString = "Refcount";
                                    break;
                                case LDRP_UPDATE_LOAD_COUNT_DECREMENT:
                                    Entry->LoadCount--;
                                    SnapString = "Derefcount";
                                    break;
                                }
                                if (ShowSnaps) {
                                    DbgPrint("LDR: %s %wZ (%lx)\n",
                                            SnapString,
                                            ImportDescriptorNameToUse,
                                            (ULONG)Entry->LoadCount
                                            );
                                }
                            }
                            LdrpUpdateLoadCount3(Entry, UpdateCountHow, PreAllocatedRedirectionBuffer);
                        }

                        if (DynamicRedirectionString.Buffer != NULL) {
                            RtlFreeUnicodeString(&DynamicRedirectionString);
                        }
                    }
                }
    skipskippedimport:
                ++ImportDescriptor;
            }
        }
    } __finally {
        RtlDeactivateActivationContextUnsafeFast(&ActivationFrame);
    }
}


VOID
LdrpInsertMemoryTableEntry (
    IN PLDR_DATA_TABLE_ENTRY LdrDataTableEntry
    )

/*++

Routine Description:

    This function inserts a loader data table entry into the
    list of loaded modules for this process. The insertion is
    done in "image memory base order".

Arguments:

    LdrDataTableEntry - Supplies the address of the loader data table
        entry to insert in the list of loaded modules for this process.

Return Value:

    None.

--*/
{
    PPEB_LDR_DATA Ldr;
    ULONG i;

    Ldr = &PebLdr;

    i = LDRP_COMPUTE_HASH_INDEX(LdrDataTableEntry->BaseDllName.Buffer[0]);
    InsertTailList(&LdrpHashTable[i],&LdrDataTableEntry->HashLinks);
    InsertTailList(&Ldr->InLoadOrderModuleList, &LdrDataTableEntry->InLoadOrderLinks);

#if defined(_AMD64_) || defined(_IA64_)

    RtlInsertInvertedFunctionTable(&LdrpInvertedFunctionTable,
                                   LdrDataTableEntry->DllBase,
                                   LdrDataTableEntry->SizeOfImage);

#endif

    InsertTailList(&Ldr->InMemoryOrderModuleList, &LdrDataTableEntry->InMemoryOrderLinks);
}


NTSTATUS
LdrpResolveDllName (
    IN PCWSTR DllPath OPTIONAL,
    IN PCWSTR DllName,
    IN BOOLEAN Redirected,
    OUT PUNICODE_STRING FullDllNameOut,
    OUT PUNICODE_STRING BaseDllNameOut,
    OUT PHANDLE DllFile
    )

/*++

Routine Description:

    This function computes the DLL pathname and base dll name (the
    unqualified, extensionless portion of the file name) for the specified
    DLL.

Arguments:

    DllPath - Supplies the DLL search path.

    DllName - Supplies the name of the DLL.

    FullDllName - Returns the fully qualified pathname of the
        DLL. The Buffer field of this string is dynamically
        allocated from the loader heap.

    BaseDLLName - Returns the base dll name of the dll.  The base name
        is the file name portion of the dll path without the trailing
        extension. The Buffer field of this string points into the
        FullDllName since the one is a substring of the other.

    DllFile - Returns an open handle to the DLL file. This parameter may
        still be NULL even upon success.

Return Value:

    TRUE - The operation was successful. A DLL file was found, and the
        FullDllName->Buffer & BaseDllName->Buffer field points to the
        base of process heap allocated memory.

    FALSE - The DLL could not be found.

--*/

{
    NTSTATUS st = STATUS_INTERNAL_ERROR;
    UNICODE_STRING DllNameString = { 0, 0, NULL };
    PUNICODE_STRING FullDllName;
    USHORT PrefixLength = 0;
    PCWSTR EffectiveDllPath = (DllPath != NULL) ? DllPath : LdrpDefaultPath.Buffer;
    WCHAR FullDllNameStaticBuffer[40]; // Arbitrary short length so most
                                       // d:\windows\system32\foobar.dll loads
                                       // don't need to search the search path twice
    UNICODE_STRING FullDllNameStaticString;
    UNICODE_STRING FullDllNameDynamicString;

    RtlInitEmptyUnicodeString(&FullDllNameStaticString,
                              FullDllNameStaticBuffer,
                              sizeof(FullDllNameStaticBuffer));

    RtlInitUnicodeString(&FullDllNameDynamicString, NULL);

    FullDllName = NULL;

    if (DllFile != NULL) {
        *DllFile = NULL;
    }

    if (FullDllNameOut != NULL) {
        FullDllNameOut->Length = 0;
        FullDllNameOut->MaximumLength = 0;
        FullDllNameOut->Buffer = NULL;
    }

    if (BaseDllNameOut != NULL) {
        BaseDllNameOut->Length = 0;
        BaseDllNameOut->MaximumLength = 0;
        BaseDllNameOut->Buffer = NULL;
    }

    if ((DllFile == NULL) ||
        (FullDllNameOut == NULL) ||
        (BaseDllNameOut == NULL)) {
        return STATUS_INVALID_PARAMETER;
    }

    RtlInitUnicodeString(&DllNameString, DllName);

    //
    // Look for ".local" redirection for this DLL.
    //

    st = LdrpResolveDllNameForAppPrivateRedirection (&DllNameString,
                                                     &FullDllNameDynamicString);

    if (!NT_SUCCESS(st)) {
        DbgPrintEx(
            DPFLTR_LDR_ID,
            LDR_ERROR_DPFLTR,
            "LDR: %s failed calling LdrpResolveDllNameForAppPrivateRediretion with status %lx\n",
            __FUNCTION__,
            st);
        return st;
    }

    //
    // .local always wins, so only search other solutions if that wasn't
    // the answer.
    //

    if (FullDllNameDynamicString.Length != 0) {

        *FullDllNameOut = FullDllNameDynamicString;

    } else {

        if (Redirected) {

            //
            // If the path was redirected, we assume that DllNameString is
            // an absolute path to the DLL so there's nothing more to do.
            //

            st = LdrpCopyUnicodeString (FullDllNameOut, &DllNameString);

            if (!NT_SUCCESS(st)) {
                DbgPrintEx(
                    DPFLTR_LDR_ID,
                    LDR_ERROR_DPFLTR,
                    "LDR: %s failed call to LdrpCopyUnicodeString() in redirected case; status = %lx\n",
                    __FUNCTION__,
                    st);
                return st;
            }
        } else {

            //
            // Not redirected; search the search path.
            //

            if (! NT_SUCCESS( LdrpSearchPath (EffectiveDllPath,
                                              DllName,
                                              &FullDllNameStaticString,
                                              &FullDllNameDynamicString,
                                              &FullDllName))) {
                return STATUS_DLL_NOT_FOUND;
            }

            if (FullDllName == &FullDllNameStaticString) {
                st = LdrpCopyUnicodeString(FullDllNameOut,
                                           FullDllName);
                if (!NT_SUCCESS(st)) {
                    DbgPrintEx(
                        DPFLTR_LDR_ID,
                        LDR_ERROR_DPFLTR,
                        "LDR: %s failed call to LdrpCopyUnicodeString() in redirected case; status = %lx\n",
                        __FUNCTION__,
                        st);
                    return st;
                }
            } else {
                *FullDllNameOut = *FullDllName;
            }
        }
    }

    //
    // Compute Length of base dll name
    //

    st = RtlFindCharInUnicodeString(
            RTL_FIND_CHAR_IN_UNICODE_STRING_START_AT_END,
            FullDllNameOut,
            &RtlDosPathSeperatorsString,
            &PrefixLength);

    if (st == STATUS_NOT_FOUND) {

        *BaseDllNameOut = *FullDllNameOut;

    } else if (!NT_SUCCESS(st)) {

        DbgPrintEx(
            DPFLTR_LDR_ID,
            LDR_ERROR_DPFLTR,
            "LDR: %s - failing because RtlFindCharInUnicodeString failed with status %x\n",
            __FUNCTION__,
            st);

        LdrpFreeUnicodeString (FullDllNameOut);
        RtlInitEmptyUnicodeString (FullDllNameOut, NULL, 0);
        return st;

    } else {

        //
        // The prefixlength is the number of bytes prior to the
        // path separator.  We also want to skip the path separator.
        //

        PrefixLength += sizeof(WCHAR);

        BaseDllNameOut->Length = FullDllNameOut->Length - PrefixLength;
        BaseDllNameOut->MaximumLength = FullDllNameOut->MaximumLength - PrefixLength;
        BaseDllNameOut->Buffer = (PWSTR) (((ULONG_PTR) FullDllNameOut->Buffer) + PrefixLength);
    }

    return STATUS_SUCCESS;
}


PVOID
LdrpFetchAddressOfEntryPoint (
    IN PVOID Base
    )

/*++

Routine Description:

    This function returns the address of the initialization routine.

Arguments:

    Base - Base of image.

Return Value:

    Status value

--*/

{
    PIMAGE_NT_HEADERS NtHeaders;
    ULONG_PTR ep;

    NtHeaders = RtlImageNtHeader(Base);
    if (NtHeaders == NULL) {
        return NULL;
    }

    ep = NtHeaders->OptionalHeader.AddressOfEntryPoint;
    if (ep != 0) {
        ep += (ULONG_PTR) Base;
    }

    return (PVOID) ep;
}


NTSTATUS
LdrpCheckForKnownDll (
    IN PCWSTR DllName,
    OUT PUNICODE_STRING FullDllNameOut,
    OUT PUNICODE_STRING BaseDllNameOut,
    OUT HANDLE *SectionOut
    )

/*++

Routine Description:

    This function checks to see if the specified DLL is a known DLL.
    It assumes it is only called for static DLL's, and when
    the known DLL directory structure has been set up.

Arguments:

    DllName - Supplies the name of the DLL.

    FullDllName - Returns the fully qualified pathname of the
        DLL. The Buffer field of this string is dynamically
        allocated from the processes heap.

    BaseDLLName - Returns the base dll name of the dll.  The base name
        is the file name portion of the dll path without the trailing
        extension. The Buffer field of this string is dynamically
        allocated from the processes heap.

    SectionOut - Returns an open handle to the section associated with
        the DLL

Return Value:

    Appropriate NTSTATUS code

--*/

{
    UNICODE_STRING Unicode;
    HANDLE Section = NULL;
    NTSTATUS Status;
    OBJECT_ATTRIBUTES Obja;
    ULONG FullLength = 0;
    UNICODE_STRING FullDllName = { 0, 0, NULL };
    PPEB Peb;

    if (SectionOut != NULL) {
        *SectionOut = NULL;
    }

    if (FullDllNameOut != NULL) {
        FullDllNameOut->Length = 0;
        FullDllNameOut->MaximumLength = 0;
        FullDllNameOut->Buffer = NULL;
    }

    if (BaseDllNameOut != NULL) {
        BaseDllNameOut->Length = 0;
        BaseDllNameOut->MaximumLength = 0;
        BaseDllNameOut->Buffer = NULL;
    }

    if ((SectionOut == NULL) ||
        (FullDllNameOut == NULL) ||
        (BaseDllNameOut == NULL)) {
        Status = STATUS_INVALID_PARAMETER;
        goto Exit;
    }

    LdrpEnsureLoaderLockIsHeld ();

    //
    // calculate base name
    //

    RtlInitUnicodeString(&Unicode, DllName);

    Peb = NtCurrentPeb ();

    //check DLL_REDIRECTION by .local file
    if ((Peb->ProcessParameters != NULL) &&
        (Peb->ProcessParameters->Flags & RTL_USER_PROC_DLL_REDIRECTION_LOCAL) &&
        (Unicode.Length != 0)) { // dll redirection with .local

        UNICODE_STRING NewDllNameUnderImageDir, NewDllNameUnderLocalDir;
        static WCHAR DllNameUnderImageDirBuffer[DOS_MAX_PATH_LENGTH];
        static WCHAR DllNameUnderLocalDirBuffer[DOS_MAX_PATH_LENGTH];
        BOOLEAN fIsKnownDll = TRUE;  // not known yet,

        NewDllNameUnderImageDir.Buffer = DllNameUnderImageDirBuffer;
        NewDllNameUnderImageDir.Length = 0 ;
        NewDllNameUnderImageDir.MaximumLength = sizeof(DllNameUnderImageDirBuffer) ;

        NewDllNameUnderLocalDir.Buffer = DllNameUnderLocalDirBuffer;
        NewDllNameUnderLocalDir.Length = 0 ;
        NewDllNameUnderLocalDir.MaximumLength = sizeof(DllNameUnderLocalDirBuffer) ;

        Status = RtlComputePrivatizedDllName_U(&Unicode, &NewDllNameUnderImageDir, &NewDllNameUnderLocalDir) ;
        if(!NT_SUCCESS(Status)) {
            goto Exit;
        }

        if ((RtlDoesFileExists_U(NewDllNameUnderLocalDir.Buffer)) ||
            (RtlDoesFileExists_U(NewDllNameUnderImageDir.Buffer))) {
            fIsKnownDll = FALSE;
        }

        //cleanup
        if (NewDllNameUnderLocalDir.Buffer != DllNameUnderLocalDirBuffer) {
            (*RtlFreeStringRoutine)(NewDllNameUnderLocalDir.Buffer);
        }

        if (NewDllNameUnderImageDir.Buffer != DllNameUnderImageDirBuffer) {
            (*RtlFreeStringRoutine)(NewDllNameUnderImageDir.Buffer);
        }

        if (!fIsKnownDll) { // must not be a known dll
            Status = STATUS_SUCCESS;
            goto Exit;
        }
    }

    // If the DLL is being redirected via Fusion/Side-by-Side support, don't bother with the
    // KnownDLL mechanism.
    Status = RtlFindActivationContextSectionString(
            0,              // flags - none for now
            NULL,           // default section group
            ACTIVATION_CONTEXT_SECTION_DLL_REDIRECTION,
            &Unicode,       // string to look for
            NULL);          // we don't want any data back, just look for existence

    if ((Status != STATUS_SXS_SECTION_NOT_FOUND) &&
        (Status != STATUS_SXS_KEY_NOT_FOUND))
    {
        if (NT_SUCCESS(Status)) {
            Status = STATUS_SUCCESS;
        }

        goto Exit;
    }

    //
    // now compute the full name for the dll
    //

    FullLength = LdrpKnownDllPath.Length +  // path prefix e.g. c:\windows\system32
                 RtlCanonicalDosPathSeperatorString.Length +
                 Unicode.Length; // base

    if (FullLength > UNICODE_STRING_MAX_BYTES) {
        Status = STATUS_NAME_TOO_LONG;
        goto Exit;
    }

    Status = LdrpAllocateUnicodeString(&FullDllName, (USHORT) FullLength);
    if (!NT_SUCCESS(Status)) {
        goto Exit;
    }

    RtlAppendUnicodeStringToString(&FullDllName, &LdrpKnownDllPath);
    RtlAppendUnicodeStringToString(&FullDllName, &RtlCanonicalDosPathSeperatorString);
    RtlAppendUnicodeStringToString(&FullDllName, &Unicode);

    ASSERT(FullDllName.Length == FullLength);

    //
    // open the section object
    //

    InitializeObjectAttributes (&Obja,
                                &Unicode,
                                OBJ_CASE_INSENSITIVE,
                                LdrpKnownDllObjectDirectory,
                                NULL);

    Status = NtOpenSection (&Section,
                            SECTION_MAP_READ | SECTION_MAP_EXECUTE | SECTION_MAP_WRITE,
                            &Obja);

    if (!NT_SUCCESS(Status)) {
        // STATUS_OBJECT_NAME_NOT_FOUND is the expected reason to fail, so it's OK
        if (Status == STATUS_OBJECT_NAME_NOT_FOUND) {
            Status = STATUS_SUCCESS;
        }

        goto Exit;
    }

#if DBG
    LdrpSectionOpens += 1;
#endif // DBG

    BaseDllNameOut->Length = Unicode.Length;
    BaseDllNameOut->MaximumLength = Unicode.Length + sizeof(WCHAR);
    BaseDllNameOut->Buffer = (PWSTR) (((ULONG_PTR) FullDllName.Buffer) + (FullDllName.Length - Unicode.Length));

    *FullDllNameOut = FullDllName;
    FullDllName.Length = 0;
    FullDllName.MaximumLength = 0;
    FullDllName.Buffer = NULL;

    *SectionOut = Section;
    Section = NULL;

    Status = STATUS_SUCCESS;

Exit:
    if (Section != NULL) {
        NtClose(Section);
    }

    if (FullDllName.Buffer != NULL) {
        LdrpFreeUnicodeString(&FullDllName);
    }

    return Status;
}


NTSTATUS
LdrpSetProtection (
    IN PVOID Base,
    IN BOOLEAN Reset
    )

/*++

Routine Description:

    This function loops thru the images sections/objects, setting
    all sections/objects marked r/o to r/w. It also resets the
    original section/object protections.

Arguments:

    Base - Base of image.

    Reset - If TRUE, reset section/object protection to original
            protection described by the section/object headers.
            If FALSE, then set all sections/objects to r/w.

Return Value:

    SUCCESS or reason NtProtectVirtualMemory failed.

--*/

{
    HANDLE CurrentProcessHandle;
    SIZE_T RegionSize;
    ULONG NewProtect, OldProtect;
    PVOID VirtualAddress;
    ULONG i;
    PIMAGE_NT_HEADERS NtHeaders;
    PCIMAGE_SECTION_HEADER SectionHeader;
    NTSTATUS st;

    CurrentProcessHandle = NtCurrentProcess();

    NtHeaders = RtlImageNtHeader(Base);

    if (! NtHeaders) {
        return STATUS_INVALID_IMAGE_FORMAT;
    }

#if defined(BUILD_WOW6432)

    if (NtHeaders->OptionalHeader.SectionAlignment < NativePageSize) {
        
        SIZE_T ReturnLength;
        MEMORY_BASIC_INFORMATION MemoryInformation;

        st = NtQueryVirtualMemory (CurrentProcessHandle,
                                   Base,
                                   MemoryBasicInformation,
                                   &MemoryInformation,
                                   sizeof MemoryInformation,
                                   &ReturnLength);

        if ((NT_SUCCESS(st)) &&
            ((MemoryInformation.Protect == PAGE_READONLY) ||
            (MemoryInformation.Protect == PAGE_EXECUTE_READ))) {

            //
            // March on and make sections writable as this image is natively
            // formatted with proper protections.
            //

            NOTHING;
        }
        else {

            //
            // Reset protection on the image pages if this is a Wow64 image.
            //

            return LdrpWx86ProtectImagePages (Base, Reset);
        }
    }
#endif

    SectionHeader = (PCIMAGE_SECTION_HEADER)((ULONG_PTR)NtHeaders + sizeof(ULONG) +
                        sizeof(IMAGE_FILE_HEADER) +
                        NtHeaders->FileHeader.SizeOfOptionalHeader
                        );

    for (i=0; i<NtHeaders->FileHeader.NumberOfSections; i++) {

        if (!(SectionHeader->Characteristics & IMAGE_SCN_MEM_WRITE) &&
            (SectionHeader->SizeOfRawData)) {

            //
            // Object isn't writable and has a non-zero on disk size, change it.
            //
            if (Reset) {
                if (SectionHeader->Characteristics & IMAGE_SCN_MEM_EXECUTE) {
                    NewProtect = PAGE_EXECUTE;
                } else {
                    NewProtect = PAGE_READONLY;
                }
                NewProtect |= (SectionHeader->Characteristics & IMAGE_SCN_MEM_NOT_CACHED) ? PAGE_NOCACHE : 0;
            } else {
                NewProtect = PAGE_READWRITE;
            }

            VirtualAddress = (PVOID)((ULONG_PTR)Base + SectionHeader->VirtualAddress);
            RegionSize = SectionHeader->SizeOfRawData;

            if (RegionSize != 0) {
                st = NtProtectVirtualMemory (CurrentProcessHandle,
                                             &VirtualAddress,
                                             &RegionSize,
                                             NewProtect, 
                                             &OldProtect);

                if (!NT_SUCCESS(st)) {
                    return st;
                }
            }

        }
        ++SectionHeader;
    }

    if (Reset) {
        NtFlushInstructionCache(CurrentProcessHandle, NULL, 0);
    }
    return STATUS_SUCCESS;
}


NTSTATUS
LdrpResolveDllNameForAppPrivateRedirection(
    PCUNICODE_STRING DllNameString,
    PUNICODE_STRING FullDllName
    )
/*++

Routine Description:

    This function takes a DLL name that's to be loaded, and if there was
    a .local file in the app dir to cause redirection, returns the full
    path to the file.

Arguments:

    DllNameString - Name of the DLL under consideration.  May be a full or
        partially qualified path.

    FullDllName - output string.  Must be deallocated using
        LdrpFreeUnicodeString().  If no redirection was present, the
        length will be left zero.

Return Value:

    NTSTATUS indicating the success/failure of this function.

--*/

{
    PPEB Peb;
    NTSTATUS st = STATUS_INTERNAL_ERROR;

    UNICODE_STRING FullDllNameUnderImageDir;
    UNICODE_STRING FullDllNameUnderLocalDir;

    // These two are static to relieve some stack size issues; this function is only called with the
    // loader lock taken so access is properly synchronized.
    static WCHAR DllNameUnderImageDirBuffer[DOS_MAX_PATH_LENGTH];
    static WCHAR DllNameUnderLocalDirBuffer[DOS_MAX_PATH_LENGTH];

    // Initialize these so that cleanup at the Exit: label an always just check whether
    // they're not null and don't point to the static buffers and then free them.

    FullDllNameUnderImageDir.Buffer = DllNameUnderImageDirBuffer;
    FullDllNameUnderImageDir.Length = 0 ;
    FullDllNameUnderImageDir.MaximumLength = sizeof(DllNameUnderImageDirBuffer);

    FullDllNameUnderLocalDir.Buffer = DllNameUnderLocalDirBuffer;
    FullDllNameUnderLocalDir.Length = 0 ;
    FullDllNameUnderLocalDir.MaximumLength = sizeof(DllNameUnderLocalDirBuffer);

    if (FullDllName != NULL) {
        FullDllName->Length = 0;
        FullDllName->MaximumLength = 0;
        FullDllName->Buffer = NULL;
    }

    if ((FullDllName == NULL) ||
        (DllNameString == NULL)) {
        st = STATUS_INVALID_PARAMETER;
        goto Exit;
    }

    st = RtlValidateUnicodeString(0, DllNameString);
    if (!NT_SUCCESS(st)) {
        goto Exit;
    }

    Peb = NtCurrentPeb ();

    if ((Peb->ProcessParameters != NULL) &&
        (Peb->ProcessParameters->Flags & RTL_USER_PROC_DLL_REDIRECTION_LOCAL) &&
        (DllNameString->Length != 0)) { // dll redirection with .local

        st = RtlComputePrivatizedDllName_U(DllNameString, &FullDllNameUnderImageDir, &FullDllNameUnderLocalDir);

        if(!NT_SUCCESS(st)) {
            DbgPrintEx(
                DPFLTR_LDR_ID,
                DPFLTR_ERROR_LEVEL,
                "LDR: %s call to RtlComputePrivatizedDllName_U() failed with status %lx\n",
                __FUNCTION__,
                st);
            goto Exit;
        }

        if (RtlDoesFileExists_U(FullDllNameUnderLocalDir.Buffer)) {// there is a local dll, use it
            st = LdrpCopyUnicodeString(FullDllName, &FullDllNameUnderLocalDir);
            if (!NT_SUCCESS(st)) {
                if (ShowSnaps)
                    DbgPrintEx(
                        DPFLTR_LDR_ID,
                        DPFLTR_ERROR_LEVEL,
                        "LDR: %s calling LdrpCopyUnicodeString() failed; exiting with status %lx\n",
                        __FUNCTION__,
                        st);

                goto Exit;
            }
        } else if (RtlDoesFileExists_U(FullDllNameUnderImageDir.Buffer)) { // there is a local dll, use it
            st = LdrpCopyUnicodeString(FullDllName, &FullDllNameUnderImageDir);
            if (!NT_SUCCESS(st)) {
                if (ShowSnaps)
                    DbgPrintEx(
                        DPFLTR_LDR_ID,
                        DPFLTR_ERROR_LEVEL,
                        "LDR: %s calling LdrpCopyUnicodeString() failed; exiting with status %lx\n",
                        __FUNCTION__,
                        st);

                goto Exit;
            }
        }
    }

    st = STATUS_SUCCESS;
Exit:
    if ((FullDllNameUnderImageDir.Buffer != NULL) &&
        (FullDllNameUnderImageDir.Buffer != DllNameUnderImageDirBuffer))
        (*RtlFreeStringRoutine)(FullDllNameUnderImageDir.Buffer);

    if ((FullDllNameUnderLocalDir.Buffer != NULL) &&
        (FullDllNameUnderLocalDir.Buffer != DllNameUnderLocalDirBuffer))
        (*RtlFreeStringRoutine)(FullDllNameUnderLocalDir.Buffer);

    return st;
}
