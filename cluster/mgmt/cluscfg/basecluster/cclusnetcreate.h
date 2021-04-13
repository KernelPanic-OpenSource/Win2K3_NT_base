//////////////////////////////////////////////////////////////////////////////
//
//  Copyright (c) 2000 Microsoft Corporation
//
//  Module Name:
//      CClusNetCreate.h
//
//  Description:
//      Header file for CClusNetCreate class.
//
//      The CClusNetCreate class creates and configures the ClusNet service.
//      This class can be used during both form and join operations.
//
//  Implementation Files:
//      CClusNetCreate.cpp
//
//  Maintained By:
//      Vij Vasu (Vvasu) 03-MAR-2000
//
//////////////////////////////////////////////////////////////////////////////


// Make sure that this file is included only once per compile path.
#pragma once


//////////////////////////////////////////////////////////////////////////
// Include Files
//////////////////////////////////////////////////////////////////////////

// For the CClusNet base class
#include "CClusNet.h"


//////////////////////////////////////////////////////////////////////////
// Forward declaration
//////////////////////////////////////////////////////////////////////////

class CBaseClusterAddNode;


//////////////////////////////////////////////////////////////////////////////
//++
//
//  class CClusNetCreate
//
//  Description:
//      The CClusNetCreate class performs operations that are common to many
//      configuration tasks.
//
//      This class is intended to be used as the base class for other ClusNet
//      related action classes.
//
//--
//////////////////////////////////////////////////////////////////////////////
class CClusNetCreate : public CClusNet
{
public:
    //////////////////////////////////////////////////////////////////////////
    // Public constructors and destructors
    //////////////////////////////////////////////////////////////////////////

    // Constructor.
    CClusNetCreate(
          CBaseClusterAddNode * pbcanParentActionIn
        );

    // Default destructor.
    ~CClusNetCreate();


    //////////////////////////////////////////////////////////////////////////
    // Public methods
    //////////////////////////////////////////////////////////////////////////

    // Create the ClusNet service.
    void Commit();

    // Rollback this creation.
    void Rollback();

    // Returns the number of progress messages that this action will send.
    UINT
        UiGetMaxProgressTicks() const throw()
    {
        // Two notifications are sent:
        // 1. When the service is created.
        // 2. When the service starts.
        return 2;
    }


private:
    //////////////////////////////////////////////////////////////////////////
    // Private types
    //////////////////////////////////////////////////////////////////////////
    typedef CClusNet BaseClass;

}; //*** class CClusNetCreate