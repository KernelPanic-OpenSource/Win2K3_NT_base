/*++

Copyright (c) Microsoft Corporation

Module Name:

    rtlimagentheader.c

Abstract:

    The module contains RtlImageNtHeader and RtlImageNtHeaderEx.

Author:

    Jay Krell (JayKrell) February 2002

Environment:

    user mode
    kernel mode
    boot loader
    statically linked to imagehlp.dll (actually RtlpImageNtHeader)
    statically linked to unicows.lib  (actually RtlpImageNtHeader)

Revision History:

--*/

#include "ntrtlp.h"

#if DBG
int
RtlImageNtHeaderEx_ExceptionFilter(
    BOOLEAN RangeCheck,
    ULONG ExceptionCode
    )
{
    ASSERT(!RangeCheck || ExceptionCode == STATUS_IN_PAGE_ERROR);
    return EXCEPTION_EXECUTE_HANDLER;
}
#else
#define RtlImageNtHeaderEx_ExceptionFilter(RangeCheck, ExceptionCode) EXCEPTION_EXECUTE_HANDLER
#endif

NTSTATUS
NTAPI
RtlImageNtHeaderEx(
    ULONG Flags,
    PVOID Base,
    ULONG64 Size,
    OUT PIMAGE_NT_HEADERS * OutHeaders
    )

/*++

Routine Description:

    This function returns the address of the NT Header.

    This function is a bit complicated.
    It is this way because RtlImageNtHeader that it replaces was hard to understand,
      and this function retains compatibility with RtlImageNtHeader.

    RtlImageNtHeader was #ifed such as to act different in each of the three
        boot loader, kernel, usermode flavors.

    boot loader -- no exception handling
    usermode -- limit msdos header to 256meg, catch any exception accessing the msdos-header
                or the pe header
    kernel -- don't cross user/kernel boundary, don't catch the exceptions,
                no 256meg limit

Arguments:

    Flags - RTL_IMAGE_NT_HEADER_EX_FLAG_NO_RANGE_CHECK -- don't be so picky
                about the image, for compatibility with RtlImageNtHeader
    Base - Supplies the base of the image.
    Size - The size of the view, usually larger than the size of the file on disk.
            This is available from NtMapViewOfSection but not from MapViewOfFile.
    OutHeaders -

Return Value:

    STATUS_SUCCESS -- everything ok
    STATUS_INVALID_IMAGE_FORMAT -- bad filesize or signature value
    STATUS_INVALID_PARAMETER -- bad parameters

--*/

{
    PIMAGE_NT_HEADERS NtHeaders = 0;
    ULONG e_lfanew = 0;
    BOOLEAN RangeCheck = 0;
    NTSTATUS Status = 0;
    const ULONG ValidFlags = 
        RTL_IMAGE_NT_HEADER_EX_FLAG_NO_RANGE_CHECK;

    if (OutHeaders != NULL) {
        *OutHeaders = NULL;
    }
    if (OutHeaders == NULL) {
        Status = STATUS_INVALID_PARAMETER;
        goto Exit;
    }
    if ((Flags & ~ValidFlags) != 0) {
        Status = STATUS_INVALID_PARAMETER;
        goto Exit;
    }
    if (Base == NULL || Base == (PVOID)(LONG_PTR)-1) {
        Status = STATUS_INVALID_PARAMETER;
        goto Exit;
    }

    RangeCheck = ((Flags & RTL_IMAGE_NT_HEADER_EX_FLAG_NO_RANGE_CHECK) == 0);
    if (RangeCheck) {
        if (Size < sizeof(IMAGE_DOS_HEADER)) {
            Status = STATUS_INVALID_IMAGE_FORMAT;
            goto Exit;
        }
    }
#if !defined (BLDR_KERNEL_RUNTIME) && !defined(NTOS_KERNEL_RUNTIME)
    //
    // In usermode, catch any exceptions taken while accessing e_magic, e_lfanew or Signature;
    // This should only be needed in the no_range_check case now.
    //
    __try {
#define EXIT __leave
#else
    //
    // Exception handling is not available in the boot loader, and exceptions
    // were not historically caught here in kernel mode. Drivers are considered
    // trusted, so we can't get an exception here due to a bad file, but we
    // could take an inpage error.
    //
#define EXIT goto Exit
#endif
    if (((PIMAGE_DOS_HEADER)Base)->e_magic != IMAGE_DOS_SIGNATURE) {
        Status = STATUS_INVALID_IMAGE_FORMAT;
        EXIT;
    }
    e_lfanew = ((PIMAGE_DOS_HEADER)Base)->e_lfanew;
    if (RangeCheck) {
        if (e_lfanew >= Size
#define SIZEOF_PE_SIGNATURE 4
            || e_lfanew >= (MAXULONG - SIZEOF_PE_SIGNATURE - sizeof(IMAGE_FILE_HEADER))
            || (e_lfanew + SIZEOF_PE_SIGNATURE + sizeof(IMAGE_FILE_HEADER)) >= Size
            ) {
            Status = STATUS_INVALID_IMAGE_FORMAT;
            EXIT;
        }
    }
#if !defined (BLDR_KERNEL_RUNTIME) && !defined(NTOS_KERNEL_RUNTIME)
    //
    // In usermode, limit msdos header to 256meg.
    //
    if (e_lfanew >= RTLP_IMAGE_MAX_DOS_HEADER) {
        Status = STATUS_INVALID_IMAGE_FORMAT;
        EXIT;
    }
#endif
    NtHeaders = (PIMAGE_NT_HEADERS)((PCHAR)Base + e_lfanew);
#if defined(NTOS_KERNEL_RUNTIME)
    //
    // In kernelmode, do not cross from usermode address to kernelmode address.
    //
    if (Base < MM_HIGHEST_USER_ADDRESS) {
        if ((PVOID)NtHeaders >= MM_HIGHEST_USER_ADDRESS) {
            Status = STATUS_INVALID_IMAGE_FORMAT;
            EXIT;
        }
        //
        // Note that this check is slightly overeager since IMAGE_NT_HEADERS has
        // a builtin array of data_directories that may be larger than the image
        // actually has. A better check would be to add FileHeader.SizeOfOptionalHeader,
        // after ensuring that the FileHeader does not cross the u/k boundary.
        //
        if ((PVOID)((PCHAR)NtHeaders + sizeof (IMAGE_NT_HEADERS)) >= MM_HIGHEST_USER_ADDRESS) {
            Status = STATUS_INVALID_IMAGE_FORMAT;
            EXIT;
        }
    }
#endif
    if (NtHeaders->Signature != IMAGE_NT_SIGNATURE) {
        Status = STATUS_INVALID_IMAGE_FORMAT;
        EXIT;
    }
    Status = STATUS_SUCCESS;
#if !defined (BLDR_KERNEL_RUNTIME) && !defined(NTOS_KERNEL_RUNTIME)
    } __except(RtlImageNtHeaderEx_ExceptionFilter(RangeCheck, GetExceptionCode())) {
        //
        // In usermode, catch any exceptions taken while accessing e_magic, e_lfanew or Signature;
        // This should only be needed in the no_range_check case now, the exception filter
        //   asserts to that affect.
        //
        // Propagating STATUS_IN_PAGE_ERROR if that's what the exception
        // was may be better, but it may be incompatible.
        //
        // Letting STATUS_IN_PAGE_ERROR go as an exception may also be better.
        //
        Status = STATUS_INVALID_IMAGE_FORMAT;
    }
#endif
Exit:
    if (NT_SUCCESS(Status)) {
        *OutHeaders = NtHeaders;
    }
    return Status;
}
#undef EXIT

PIMAGE_NT_HEADERS
NTAPI
RtlImageNtHeader(
    PVOID Base
    )
{
    PIMAGE_NT_HEADERS NtHeaders = NULL;
    (VOID)RtlImageNtHeaderEx(RTL_IMAGE_NT_HEADER_EX_FLAG_NO_RANGE_CHECK, Base, 0, &NtHeaders);
    return NtHeaders;
}
