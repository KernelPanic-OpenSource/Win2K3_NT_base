//////////////////////////////////////////////////////////////////////////////
//
//  Copyright (c) 2000-2002 Microsoft Corporation
//
//  Module Name:
//      Pch.h
//
//  Description:
//      Precompiled header file.
//
//  Maintained By:
//      Galen Barbee (GalenB) 24-SEP-2001
//
//////////////////////////////////////////////////////////////////////////////

#pragma once

//////////////////////////////////////////////////////////////////////////////
//  Constant Definitions
//////////////////////////////////////////////////////////////////////////////

#define UNICODE
#define _UNICODE

//////////////////////////////////////////////////////////////////////////////
//  Include Files
//////////////////////////////////////////////////////////////////////////////

#include <Windows.h>
#include <ObjBase.h>
#include <ComCat.h>

#include <clusapi.h>

#include <Log.h>

//
// Interface definitions for IClusCfgStartupListener
//

#include <ClusCfgServer.h>

//
// Categories and ClusCfg GUIDS needed by the main program
//

#include <ClusCfgGuids.h>
#include <IISClusCfgGuids.h>

#include <StrSafe.h>

extern  LONG        g_cObjects;
extern  HINSTANCE   g_hInstance;