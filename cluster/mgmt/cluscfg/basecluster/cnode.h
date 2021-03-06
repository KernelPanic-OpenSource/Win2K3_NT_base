//////////////////////////////////////////////////////////////////////////////
//
//  Copyright (c) 2000 Microsoft Corporation
//
//  Module Name:
//      CNode.h
//
//  Description:
//      Header file for CNode class.
//      The CNode class is a base class for action classes that perform
//      configuration tasks related to the node being configured.
//
//  Implementation Files:
//      CNode.cpp
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

// For the CAction base class
#include "CAction.h"

// For the SmartSz typedef
#include "CommonDefs.h"


//////////////////////////////////////////////////////////////////////////
// Forward declaration
//////////////////////////////////////////////////////////////////////////

class CBaseClusterAction;
class CStr;


//////////////////////////////////////////////////////////////////////////////
//++
//
//  class CNode
//
//  Description:
//      The CNode class is a base class for action classes that perform
//      configuration tasks related to the node being configured.
//
//--
//////////////////////////////////////////////////////////////////////////////
class CNode : public CAction
{
protected:
    //////////////////////////////////////////////////////////////////////////
    // Protected constructors and destructors
    //////////////////////////////////////////////////////////////////////////

    // Constructor.
    CNode( CBaseClusterAction * pbcaParentActionIn );

    // Default destructor.
    ~CNode();


    //////////////////////////////////////////////////////////////////////////
    // Protected methods
    //////////////////////////////////////////////////////////////////////////

    // Configure this node.
    void
        Configure( const CStr & rcstrClusterNameIn );

    // Clean up the changes made to this node when it joined a cluster.
    void
        Cleanup();


    //////////////////////////////////////////////////////////////////////////
    // Protected accessors
    //////////////////////////////////////////////////////////////////////////

    // Get the parent action
    CBaseClusterAction *
        PbcaGetParent() throw()
    {
        return m_pbcaParentAction;
    }


private:
    //////////////////////////////////////////////////////////////////////////
    // Private member functions
    //////////////////////////////////////////////////////////////////////////

    // Copy constructor
    CNode( const CNode & );

    // Assignment operator
    const CNode & operator =( const CNode & );


    //////////////////////////////////////////////////////////////////////////
    // Private data
    //////////////////////////////////////////////////////////////////////////

    // Pointer to the base cluster action of which this action is a part.
    CBaseClusterAction *    m_pbcaParentAction;

    // Did we change the cluster adminstrator connections list?
    bool                    m_fChangedConnectionsList;

    // The cluster administrator connections list before we changed it.
    SmartSz                 m_sszOldConnectionsList;

}; //*** class CNode
