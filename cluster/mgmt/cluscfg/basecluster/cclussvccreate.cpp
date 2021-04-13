//////////////////////////////////////////////////////////////////////////////
//
//  Copyright (c) 1999-2002 Microsoft Corporation
//
//  Module Name:
//      CClusSvcCreate.cpp
//
//  Description:
//      Contains the definition of the CClusSvcCreate class.
//
//  Maintained By:
//      David Potter    (DavidP)    14-JUN-2001
//      Vij Vasu        (Vvasu)     08-MAR-2000
//
//////////////////////////////////////////////////////////////////////////////


//////////////////////////////////////////////////////////////////////////////
// Include Files
//////////////////////////////////////////////////////////////////////////////

// The precompiled header.
#include "Pch.h"

// The header for this file
#include "CClusSvcCreate.h"

// For the CBaseClusterAddNode class.
#include "CBaseClusterAddNode.h"


//////////////////////////////////////////////////////////////////////////////
//++
//
//  CClusSvcCreate::CClusSvcCreate
//
//  Description:
//      Constructor of the CClusSvcCreate class
//
//  Arguments:
//      pbcanParentActionIn
//          Pointer to the base cluster action of which this action is a part.
//
//  Return Value:
//      None. 
//
//  Exceptions Thrown:
//      Any exceptions thrown by underlying functions
//
    //--
//////////////////////////////////////////////////////////////////////////////
CClusSvcCreate::CClusSvcCreate(
      CBaseClusterAddNode *     pbcanParentActionIn
    )
    : BaseClass( pbcanParentActionIn )
{

    TraceFunc( "" );

    SetRollbackPossible( true );

    TraceFuncExit();

} //*** CClusSvcCreate::CClusSvcCreate


//////////////////////////////////////////////////////////////////////////////
//++
//
//  CClusSvcCreate::~CClusSvcCreate
//
//  Description:
//      Destructor of the CClusSvcCreate class.
//
//  Arguments:
//      None.
//
//  Return Value:
//      None. 
//
//  Exceptions Thrown:
//      Any exceptions thrown by underlying functions
//
//--
//////////////////////////////////////////////////////////////////////////////
CClusSvcCreate::~CClusSvcCreate( void )
{
    TraceFunc( "" );
    TraceFuncExit();

} //*** CClusSvcCreate::~CClusSvcCreate


//////////////////////////////////////////////////////////////////////////////
//++
//
//  CClusSvcCreate::Commit
//
//  Description:
//      Create and start the service.
//
//  Arguments:
//      None.
//
//  Return Value:
//      None. 
//
//  Exceptions Thrown:
//      CAssert
//          If the base parent of this action is not CBaseClusterAddNode.
//
//      Any that are thrown by the contained actions.
//
//--
//////////////////////////////////////////////////////////////////////////////
void
CClusSvcCreate::Commit( void )
{
    TraceFunc( "" );

    // Get the parent action pointer.
    HRESULT                 hr = S_OK;
    CBaseClusterAddNode *   pcanClusterAddNode = dynamic_cast< CBaseClusterAddNode *>( PbcaGetParent() );
    CBString                bstrPassword;

    // If the parent action of this action is not CBaseClusterForm
    if ( pcanClusterAddNode == NULL )
    {
        THROW_ASSERT( E_POINTER, "The parent action of this action is not CBaseClusterAddNode." );
    } // an invalid pointer was passed in.

    hr = THR( pcanClusterAddNode->GetServiceAccountCredentials().GetPassword( &bstrPassword ) );
    TraceMemoryAddBSTR( static_cast< BSTR >( bstrPassword ) );
    if ( FAILED( hr ) )
    {
        THROW_EXCEPTION( hr );
    }
    
    // Call the base class commit method.
    BaseClass::Commit();

    try
    {
        CStr strAccountUserPrincipalName( pcanClusterAddNode->StrGetServiceAccountUPN() );
        
        // Create the service.
        ConfigureService(
              strAccountUserPrincipalName.PszData()
            , bstrPassword
            , pcanClusterAddNode->PszGetNodeIdString()
            , pcanClusterAddNode->FIsVersionCheckingDisabled()
            , pcanClusterAddNode->DwGetClusterIPAddress()
            );

    } // try:
    catch( ... )
    {
        // If we are here, then something went wrong with the create.

        LogMsg( "[BC] Caught exception during commit." );

        //
        // Cleanup anything that the failed create might have done.
        // Catch any exceptions thrown during Cleanup to make sure that there 
        // is no collided unwind.
        //
        try
        {
            CleanupService();

        }
        catch( ... )
        {
            //
            // The rollback of the committed action has failed.
            // There is nothing that we can do.
            // We certainly cannot rethrow this exception, since
            // the exception that caused the rollback is more important.
            //

            TW32( ERROR_CLUSCFG_ROLLBACK_FAILED );

            LogMsg( "[BC] THIS COMPUTER MAY BE IN AN INVALID STATE. Caught an exception during cleanup." );

        } // catch: all

        // Rethrow the exception thrown by commit.
        throw;

    } // catch: all

    // If we are here, then everything went well.
    SetCommitCompleted( true );

    TraceFuncExit();

} //*** CClusSvcCreate::Commit


//////////////////////////////////////////////////////////////////////////////
//++
//
//  CClusSvcCreate::Rollback
//
//  Description:
//      Cleanup the service.
//
//  Arguments:
//      None.
//
//  Return Value:
//      None. 
//
//  Exceptions Thrown:
//      Any that are thrown by the underlying functions.
//
//--
//////////////////////////////////////////////////////////////////////////////
void
CClusSvcCreate::Rollback( void )
{
    TraceFunc( "" );

    // Call the base class rollback method. 
    BaseClass::Rollback();

    // Cleanup the service.
    CleanupService();

    SetCommitCompleted( false );

    TraceFuncExit();

} //*** CClusSvcCreate::Rollback
