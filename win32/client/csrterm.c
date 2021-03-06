/*++

Copyright (c) 1998  Microsoft Corporation

Module Name:

    csrterm.c

Abstract:

    This module implements functions that are used by the Terminal server support functions
    to communicate with csrss.

Author:

    Michael Zoran (mzoran) 21-Jun-1998

Revision History:

--*/

#include "basedll.h"

NTSTATUS
CsrBasepSetTermsrvAppInstallMode(
    BOOL bState
    )
{

#if defined(BUILD_WOW6432)
    return NtWow64CsrBasepSetTermsrvAppInstallMode(bState);
#else

    BASE_API_MSG m;
    PBASE_SET_TERMSRVAPPINSTALLMODE c = &m.u.SetTermsrvAppInstallMode;

    c->bState = bState;
    return  CsrClientCallServer((PCSR_API_MSG)&m, NULL,
                                 CSR_MAKE_API_NUMBER(BASESRV_SERVERDLL_INDEX,
                                                     BasepSetTermsrvAppInstallMode),
                                 sizeof( *c ));
#endif

}


NTSTATUS
CsrBasepSetClientTimeZoneInformation(
    IN PBASE_SET_TERMSRVCLIENTTIMEZONE c
    )
{

#if defined(BUILD_WOW6432)
    return NtWow64CsrBasepSetClientTimeZoneInformation(c);
#else

    BASE_API_MSG m;
    
    RtlCopyMemory(&m.u.SetTermsrvClientTimeZone, c, sizeof(*c));
    
    return  CsrClientCallServer((PCSR_API_MSG)&m, NULL,
                                 CSR_MAKE_API_NUMBER(BASESRV_SERVERDLL_INDEX,
                                                     BasepSetTermsrvClientTimeZone),
                                 sizeof( *c ));
#endif

}