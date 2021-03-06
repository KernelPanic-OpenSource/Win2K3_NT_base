//////////////////////////////////////////////////////////////////////////////
//
//  Copyright (c) 2000 Microsoft Corporation
//
//  Module Name:
//      CTaskCleanInstall.h
//
//  Description:
//      This file contains the declaration of the class CTaskCleanInstall.
//      which encapsulates a clean installation of cluster binaries.
//
//  Implementation Files:
//      CTaskCleanInstall.cpp
//
//  Maintained By:
//      Vij Vasu (Vvasu) 03-MAR-2000
//
//////////////////////////////////////////////////////////////////////////////

#pragma once

//////////////////////////////////////////////////////////////////////////////
// Include Files
//////////////////////////////////////////////////////////////////////////////

// For the base class
#include "CClusOCMTask.h"


//////////////////////////////////////////////////////////////////////////////
// Forward Declarations
//////////////////////////////////////////////////////////////////////////////
class CClusOCMApp;


//////////////////////////////////////////////////////////////////////////////
//++
//
//  class CTaskCleanInstall
//
//  Description:
//      This class encapsulates a clean installation of cluster binaries.
//
//--
//////////////////////////////////////////////////////////////////////////////
class CTaskCleanInstall : public CClusOCMTask
{
public:
    //////////////////////////////////////////////////////////////////////////
    // Public constructors and destructors
    //////////////////////////////////////////////////////////////////////////

    // Constructor.
    CTaskCleanInstall( const CClusOCMApp & rAppIn );

    // Destructor
    virtual ~CTaskCleanInstall( void );


    //////////////////////////////////////////////////////////////////////////
    // Message handlers
    //////////////////////////////////////////////////////////////////////////

    // Handler for the OC_QUEUE_FILE_OPS message.
    virtual DWORD
        DwOcQueueFileOps( HSPFILEQ hSetupFileQueueIn );

    // Handler for the OC_COMPLETE_INSTALLATION message.
    virtual DWORD
        DwOcCompleteInstallation( void );

    // Handler for the OC_CLEANUP message.
    virtual DWORD
        DwOcCleanup( void );


private:
    //////////////////////////////////////////////////////////////////////////
    // Private types
    //////////////////////////////////////////////////////////////////////////
    typedef CClusOCMTask BaseClass;

}; //*** class CTaskCleanInstall
