//////////////////////////////////////////////////////////////////////////////
//
//  Copyright (c) 1999-2003 Microsoft Corporation
//
//  Module Name:
//      Pch.h
//
//  Description:
//      Precompiled header file.
//
//  Maintained By:
//      Ozan Ozhan      (OzanO)     08-JAN-2003
//      Geoffrey Pease  (GPease)    15-OCT-1999
//
//////////////////////////////////////////////////////////////////////////////

//////////////////////////////////////////////////////////////////////////////
//  Constant Definitions
//////////////////////////////////////////////////////////////////////////////
#define UNICODE

#if DBG==1 || defined( _DEBUG )
#define DEBUG
//
//  Define this to change Interface Tracking
//
//#define NO_TRACE_INTERFACES
//
//  Define this to pull in the SysAllocXXX functions. Requires linking to 
//  OLEAUT32.DLL
//
#define USES_SYSALLOCSTRING
#endif // DBG==1 || _DEBUG

//////////////////////////////////////////////////////////////////////////////
//  Forward Class Declarations
//////////////////////////////////////////////////////////////////////////////

//////////////////////////////////////////////////////////////////////////////
//  External Class Declarations
//////////////////////////////////////////////////////////////////////////////

//////////////////////////////////////////////////////////////////////////////
//  Include Files
//////////////////////////////////////////////////////////////////////////////
#include <windows.h>
#include <objbase.h>
#include <ocidl.h>
#include <dispex.h>
#include <shlwapi.h>
#include <resapi.h>
#include <activscp.h>
#include <activdbg.h>
#include <nserror.h>

// Safe string functions
#include <strsafe.h>
#include <Pragmas.h>
#include <CITracker.h>
#include <Debug.h>
#include <Log.h>
#include <Common.h>
#include <clusudef.h>
#include <clusrtl.h>

#include "clstrcmp.h"
#include "genscript.h"
#define cchGUID_STRING_SIZE (sizeof("{12345678-1234-1234-1234-123456789012}"))

extern "C" {
extern PLOG_EVENT_ROUTINE ClusResLogEvent;
}

//////////////////////////////////////////////////////////////////////////////
//  Type Definitions
//////////////////////////////////////////////////////////////////////////////

//
// Generic script resource type private properties.
//
typedef struct GENSCRIPT_PROPS
{
    LPWSTR           pszScriptFilePath;
} GENSCRIPT_PROPS, * PGENSCRIPT_PROPS;

