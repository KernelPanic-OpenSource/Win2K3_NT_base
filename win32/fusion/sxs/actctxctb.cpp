/*++

Copyright (c) Microsoft Corporation

Module Name:

    actctxctb.cpp

Abstract:

    Code to manage the list of activation context contributors in sxs.dll.

Author:

    Michael J. Grier (MGrier) 23-Feb-2000

Revision History:

--*/

#include "stdinc.h"

CCriticalSectionNoConstructor g_ActCtxCtbListCritSec;
PACTCTXCTB g_ActCtxCtbListHead;
ULONG g_ActCtxCtbListCount;

BOOL
SxspAddBuiltinActCtxContributor(
    ACTCTXCTB_CALLBACK_FUNCTION CallbackFunction,
    const GUID *ExtensionGuid,
    ULONG SectionId,
    ULONG Format,
    PCWSTR ContributorName
    )
{
    CSmartPtr<ACTCTXCTB> Contrib;
    BOOL fSuccess = FALSE;
    FN_TRACE_WIN32(fSuccess);

    PARAMETER_CHECK(
        (Format == ACTIVATION_CONTEXT_SECTION_FORMAT_STRING_TABLE) ||
        (Format == ACTIVATION_CONTEXT_SECTION_FORMAT_GUID_TABLE) ||
        (Format == 0));
    PARAMETER_CHECK(CallbackFunction != NULL);
    PARAMETER_CHECK(ContributorName != NULL);

    IFW32FALSE_EXIT(Contrib.Win32Allocate(__FILE__, __LINE__));

    Contrib->m_BuiltinContributor = true;
    Contrib->m_CallbackFunction = CallbackFunction;

    if (ExtensionGuid != NULL)
    {
        Contrib->m_ExtensionGuid = *ExtensionGuid;
        Contrib->m_IsExtendedSection = ((Contrib->m_ExtensionGuid != GUID_NULL) != FALSE);
    }
    else
    {
        Contrib->m_ExtensionGuid = GUID_NULL;
        Contrib->m_IsExtendedSection = false;
    }

    IFW32FALSE_EXIT(Contrib->m_ContributorNameBuffer.Win32Assign(ContributorName, ::wcslen(ContributorName)));

    Contrib->m_SectionId = SectionId;
    Contrib->m_Format = Format;
    Contrib->m_RefCount = 1;
    {
        CSxsLockCriticalSection lock(g_ActCtxCtbListCritSec);
        IFW32FALSE_EXIT(lock.Lock());
        Contrib->m_Next = g_ActCtxCtbListHead;
        g_ActCtxCtbListHead = Contrib.Detach();
        g_ActCtxCtbListCount++;
    }

    fSuccess = TRUE;
Exit:
    return fSuccess;
}

BOOL
SxspInitActCtxContributors(
    )
{
    FN_PROLOG_WIN32

    IFW32FALSE_EXIT(g_ActCtxCtbListCritSec.Initialize());

    ASSERT(g_ActCtxCtbListHead == NULL);
    ASSERT(g_ActCtxCtbListCount == 0);

    IFW32FALSE_EXIT(::SxspAddBuiltinActCtxContributor(
                &SxspAssemblyMetadataContributorCallback,
                NULL,
                ACTIVATION_CONTEXT_SECTION_ASSEMBLY_INFORMATION,
                ACTIVATION_CONTEXT_SECTION_FORMAT_STRING_TABLE,
                L"Builtin Assembly Metadata Contributor"));

    IFW32FALSE_EXIT(::SxspAddBuiltinActCtxContributor(
                &SxspDllRedirectionContributorCallback,
                NULL,
                ACTIVATION_CONTEXT_SECTION_DLL_REDIRECTION,
                ACTIVATION_CONTEXT_SECTION_FORMAT_STRING_TABLE,
                L"Builtin DLL Redirection contributor"));

    IFW32FALSE_EXIT(::SxspAddBuiltinActCtxContributor(
                &SxspWindowClassRedirectionContributorCallback,
                NULL,
                ACTIVATION_CONTEXT_SECTION_WINDOW_CLASS_REDIRECTION,
                ACTIVATION_CONTEXT_SECTION_FORMAT_STRING_TABLE,
                L"Builtin Window Class Redirection contributor"));

    IFW32FALSE_EXIT(::SxspAddBuiltinActCtxContributor(
                &SxspComClassRedirectionContributorCallback,
                NULL,
                ACTIVATION_CONTEXT_SECTION_COM_SERVER_REDIRECTION,
                ACTIVATION_CONTEXT_SECTION_FORMAT_GUID_TABLE,
                L"Builtin COM Server Redirection contributor"));

    IFW32FALSE_EXIT(::SxspAddBuiltinActCtxContributor(
                &SxspComProgIdRedirectionContributorCallback,
                NULL,
                ACTIVATION_CONTEXT_SECTION_COM_PROGID_REDIRECTION,
                ACTIVATION_CONTEXT_SECTION_FORMAT_STRING_TABLE,
                L"Builtin COM ProgId redirection contributor"));

    IFW32FALSE_EXIT(::SxspAddBuiltinActCtxContributor(
                &SxspComTypeLibRedirectionContributorCallback,
                NULL,
                ACTIVATION_CONTEXT_SECTION_COM_TYPE_LIBRARY_REDIRECTION,
                ACTIVATION_CONTEXT_SECTION_FORMAT_GUID_TABLE,
                L"Builtin COM Type Library redirection contributor"));

    IFW32FALSE_EXIT(::SxspAddBuiltinActCtxContributor(
                &SxspComInterfaceRedirectionContributorCallback,
                NULL,
                ACTIVATION_CONTEXT_SECTION_COM_INTERFACE_REDIRECTION,
                ACTIVATION_CONTEXT_SECTION_FORMAT_GUID_TABLE,
                L"Builtin COM interface redirection contributor"));

    IFW32FALSE_EXIT(::SxspAddBuiltinActCtxContributor(
                &SxspClrInteropContributorCallback,
                NULL,
                ACTIVATION_CONTEXT_SECTION_CLR_SURROGATES,
                ACTIVATION_CONTEXT_SECTION_FORMAT_GUID_TABLE,
                L"Builtin NDP surrogate data contributor"));

    FN_EPILOG
}

VOID
SxspUninitActCtxContributors(
    VOID
    )
{
    FN_TRACE();
    PACTCTXCTB pCtb;

    g_ActCtxCtbListCritSec.Destruct();

    pCtb = g_ActCtxCtbListHead;

    while (pCtb != NULL)
    {
        PACTCTXCTB pNext = pCtb->m_Next;
        pCtb->Release();
        pCtb = pNext;
    }
}

