//////////////////////////////////////////////////////////////////////////////
//
//  Copyright (c) 1999-2002 Microsoft Corporation
//
//  Module Name:
//      Pch.h
//
//  Description:
//      Precompiled header file.
//
//  Maintained By:
//      Geoffrey Pease (GPease) 15-OCT-1999
//
//////////////////////////////////////////////////////////////////////////////

//////////////////////////////////////////////////////////////////////////////
//  Constant Definitions
//////////////////////////////////////////////////////////////////////////////
#define UNICODE
#define SECURITY_WIN32

#if DBG==1 || defined( _DEBUG )
#define DEBUG
//
//  Define this to change Interface Tracking
//
//#define NO_TRACE_INTERFACES
#endif // DBG==1 || _DEBUG

//
//  Define this to pull in the SysAllocXXX functions. Requires linking to
//  OLEAUT32.DLL
//
#define USES_SYSALLOCSTRING

//////////////////////////////////////////////////////////////////////////////
//  Forward Class Declarations
//////////////////////////////////////////////////////////////////////////////

//////////////////////////////////////////////////////////////////////////////
//  External Class Declarations
//////////////////////////////////////////////////////////////////////////////

//////////////////////////////////////////////////////////////////////////////
//  Include Files
//////////////////////////////////////////////////////////////////////////////

#include <Pragmas.h>

#include <windows.h>
#include <shlwapi.h>
#include <objbase.h>
#include <ocidl.h>
#include <olectl.h>
#include <ComCat.h>
#include <ntdsapi.h>
#include <dsgetdc.h>
#include <lm.h>
#include <lmapibuf.h>
#include <windns.h>
#include <clusapi.h>
#include <crt\limits.h>
#include <security.h>
#include <strsafe.h>

#include <clusrtl.h>

#include <Debug.h>
#include <Log.h>
#include <CITracker.h>
#include <Common.h>
#include <CriticalSection.h>
#include <CFactory.h>
#include <Dll.h>
#include <Guids.h>
#include <ObjectCookie.h>
#include <ClusCfgClient.h>
#include "ServiceManager.h"
#include <ClusCfgGuids.h>
#include <ClusCfgInternalGuids.h>
#include <ClusCfgWizard.h>
#include <ClusCfgServer.h>
#include <ClusCfgPrivate.h>
#include <LoadString.h>
#include <NameUtil.h>

#include "MiddleTierGuids.h"
#include "MiddleTierUtils.h"

#include "MiddleTierStrings.h"
#include "CommonStrings.h"
#include <ClusCfgDef.h>

//////////////////////////////////////////////////////////////////////////////
//  Type Definitions
//////////////////////////////////////////////////////////////////////////////

//////////////////////////////////////////////////////////////////////////////
//  Constants
//////////////////////////////////////////////////////////////////////////////
