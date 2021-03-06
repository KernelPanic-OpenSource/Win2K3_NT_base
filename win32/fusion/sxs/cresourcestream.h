#if !defined(_FUSION_SXS_CRESOURCESTREAM_H_INCLUDED_)
#define _FUSION_SXS_CRESOURCESTREAM_H_INCLUDED_

/*++

Copyright (c) Microsoft Corporation

Module Name:

    filestream.cpp

Abstract:

    Minimal implementation of IStream over a Windows PE/COFF resource.

Author:

    Jay Krell (a-JayK, JayKrell) May 2000

Revision History:

--*/
#pragma once
#include "cmemorystream.h"
#include "fusionhandle.h"
#include "sxsp.h"

class CResourceStream :  public CMemoryStream
{
    typedef CMemoryStream Base;
public:
    CResourceStream() { }

    // NOTE the order of type/name is 1) as you might expect 2) consistent with
    // FindResourceEx, 3) INconsistent with FindResource
    // RT_* are actually of type PCWSTR
    BOOL Initialize(PCWSTR file, PCWSTR type, PCWSTR name, WORD language = MAKELANGID(LANG_NEUTRAL, SUBLANG_NEUTRAL));
    BOOL Initialize(PCWSTR file, PCWSTR type);
    /*
    feel free to add more overloads that take, say
        HMODULE, HRSRC, HGLOBAL
    */

    // Override so that we can get times from the open file...
    HRESULT __stdcall Stat(STATSTG *pstatstg, DWORD grfStatFlag);

    virtual ~CResourceStream() { }

private: // intentionally not implemented
    CResourceStream(const CResourceStream&);
    void operator=(const CResourceStream&);

    BOOL InitializeAlreadyOpen(
        PCWSTR type,
        PCWSTR name,
        LANGID   language = MAKELANGID(LANG_NEUTRAL, SUBLANG_NEUTRAL)
        );

    CDynamicLinkLibrary m_dll;
    CStringBuffer m_buffFilePath;
};

#endif // !defined(_FUSION_SXS_CRESOURCESTREAM_H_INCLUDED_)
