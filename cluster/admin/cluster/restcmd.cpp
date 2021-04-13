/////////////////////////////////////////////////////////////////////////////
//
//  Copyright (c) 1996-2002 Microsoft Corporation
//
//  Module Name:
//      restcmd.cpp
//
//  Description:
//      Resource Type Commands.
//      Implements commands which may be performed on resource types
//
//  Author:
//      Charles Stacy Harris III (stacyh)     20-March-1997
//      Michael Burton (t-mburt)              04-Aug-1997
//
//  Maintained By:
//      George Potts (GPotts)                 11-Apr-2002
//
//  Revision History:
//      April 10, 2002              Updated for the security push.
//
//  Notes:
//
/////////////////////////////////////////////////////////////////////////////
#include "precomp.h"

#include "clusudef.h"
#include <cluswrap.h>
#include <ResTypeUtils.h>
#include "restcmd.h"

#include "cmdline.h"


/////////////////////////////////////////////////////////////////////////////
//++
//
//  CResTypeCmd::CResTypeCmd
//
//  Routine Description:
//      Constructor
//      Initializes all the DWORD params used by CGenericModuleCmd to
//      provide generic functionality.
//
//  Arguments:
//      IN  LPCWSTR lpszClusterName
//          Cluster name. If NULL, opens default cluster.
//
//      IN  CCommandLine & cmdLine
//          CommandLine Object passed from DispatchCommand
//
//  Member variables used / set:
//      All.
//
//  Return Value:
//      None.
//
//--
/////////////////////////////////////////////////////////////////////////////
CResTypeCmd::CResTypeCmd( const CString & strClusterName, CCommandLine & cmdLine ) :
    CGenericModuleCmd( cmdLine )
{
    m_hCluster = 0;
    m_strClusterName = strClusterName;
    m_strDisplayName.Empty();

    m_dwMsgStatusList          = MSG_RESTYPE_STATUS_LIST;
    m_dwMsgStatusListAll       = MSG_RESTYPE_STATUS_LIST_ALL;
    m_dwMsgStatusHeader        = MSG_RESTYPE_STATUS_HEADER;
    m_dwMsgPrivateListAll      = MSG_PRIVATE_LISTING_RESTYPE_ALL;
    m_dwMsgPropertyListAll     = MSG_PROPERTY_LISTING_RESTYPE_ALL;
    m_dwMsgPropertyHeaderAll   = MSG_PROPERTY_HEADER_RESTYPE_ALL;
    m_dwCtlGetPrivProperties   = CLUSCTL_RESOURCE_TYPE_GET_PRIVATE_PROPERTIES;
    m_dwCtlGetCommProperties   = CLUSCTL_RESOURCE_TYPE_GET_COMMON_PROPERTIES;
    m_dwCtlGetROPrivProperties = CLUSCTL_RESOURCE_TYPE_GET_RO_PRIVATE_PROPERTIES;
    m_dwCtlGetROCommProperties = CLUSCTL_RESOURCE_TYPE_GET_RO_COMMON_PROPERTIES;
    m_dwCtlSetPrivProperties   = CLUSCTL_RESOURCE_TYPE_SET_PRIVATE_PROPERTIES;
    m_dwCtlSetCommProperties   = CLUSCTL_RESOURCE_TYPE_SET_COMMON_PROPERTIES;
    m_dwClusterEnumModule      = CLUSTER_ENUM_RESTYPE;
    m_pfnOpenClusterModule     = (HCLUSMODULE(*)(HCLUSTER,LPCWSTR)) NULL;
    m_pfnCloseClusterModule    = (BOOL(*)(HCLUSMODULE))  NULL;
    m_pfnClusterModuleControl  = (DWORD(*)(HCLUSMODULE,HNODE,DWORD,LPVOID,DWORD,LPVOID,DWORD,LPDWORD)) ClusterResourceTypeControl;

} //*** CResTypeCmd::CResTypeCmd()


/////////////////////////////////////////////////////////////////////////////
//++
//
//  CResTypeCmd::~CResTypeCmd
//
//  Routine Description:
//      Destructor
//
//  Arguments:
//      None.
//
//  Member variables used / set:
//      All.
//
//  Return Value:
//      None.
//
//--
/////////////////////////////////////////////////////////////////////////////
CResTypeCmd::~CResTypeCmd( void )
{
    CloseCluster();

} //*** CResTypeCmd::~CResTypeCmd()


/////////////////////////////////////////////////////////////////////////////
//++
//
//  CResTypeCmd::OpenModule
//
//  Routine Description:
//      This function does not really open a resource type since resource types
//      don't have handles. It actaully just converts the "display name" of a resource
//      to a type name.
//
//  Arguments:
//      None.
//
//  Member variables used / set:
//      m_strModuleName             The name of the module
//      m_hModule                   The handle to the module
//
//  Return Value:
//      ERROR_SUCCESS               on success
//      ERROR_INVALID_DATA          if no display name was specified
//      Win32 Error code            on failure
//
//--
/////////////////////////////////////////////////////////////////////////////
DWORD CResTypeCmd::OpenModule( void )
{
    DWORD _sc = ERROR_SUCCESS;

    // The command line uses the display name of the resource
    if ( m_strDisplayName.IsEmpty() == FALSE )
    {
        LPWSTR _pszTypeName = NULL;

        _sc = ScResDisplayNameToTypeName( m_hCluster, m_strDisplayName, &_pszTypeName );

        if ( _sc == ERROR_SUCCESS )
        {
            m_strModuleName = _pszTypeName;
            LocalFree( _pszTypeName );
        } // if: name converted successfully

    } // if: display name specified
    else
    {
        _sc = ERROR_INVALID_DATA;
    } // else: no display name specified

    return _sc;

} //*** CResTypeCmd::OpenModule()


/////////////////////////////////////////////////////////////////////////////
//++
//
//  CResTypeCmd::Execute
//
//  Routine Description:
//      Gets the next command line parameter and calls the appropriate
//      handler.  If the command is not recognized, calls Execute of
//      parent classes (first CRenamableModuleCmd, then CRsourceUmbrellaCmd)
//
//  Arguments:
//      None.
//
//  Member variables used / set:
//      None
//
//  Return Value:
//      ERROR_SUCCESS               on success
//      Win32 Error code            on failure
//
//--
/////////////////////////////////////////////////////////////////////////////
DWORD CResTypeCmd::Execute( void )
{
    m_theCommandLine.ParseStageTwo();

    const vector<CCmdLineOption> & optionList = m_theCommandLine.GetOptions();

    vector<CCmdLineOption>::const_iterator curOption = optionList.begin();
    vector<CCmdLineOption>::const_iterator lastOption = optionList.end();

    CSyntaxException se( SeeHelpStringID() );

    // No options specified. Execute the default command.
    if ( optionList.empty() )
        return ListResTypes( NULL );

    DWORD dwReturnValue = ERROR_SUCCESS;

        // Process one option after another.
    while ( ( curOption != lastOption ) && ( dwReturnValue == ERROR_SUCCESS ) )
    {
        // Look up the command
        switch( curOption->GetType() )
        {
            case optHelp:
            {
                // If help is one of the options, process no more options.
                dwReturnValue = PrintHelp();
                break;
            }

            case optDefault:
            {
                const vector<CCmdLineParameter> & paramList = curOption->GetParameters();

                //  Check number of parameters.
                if ( paramList.size() == 0 )
                {
                    se.LoadMessage( IDS_MISSING_PARAMETERS );
                    throw se;
                }
                else if ( paramList.size() > 1 )
                {
                    se.LoadMessage( MSG_EXTRA_PARAMETERS_ERROR_NO_NAME );
                    throw se;
                }
                else // just one parameter present
                {
                    const CCmdLineParameter & param = paramList[0];

                    //  Check parameter type.
                    if ( param.GetType() != paramUnknown )
                    {
                        se.LoadMessage( MSG_INVALID_PARAMETER, param.GetName() );
                        throw se;
                    }

                        // This parameter takes no values.
                    if ( param.GetValues().size() != 0 )
                    {
                        se.LoadMessage( MSG_PARAM_NO_VALUES, param.GetName() );
                        throw se;
                    }

                    m_strDisplayName = param.GetName();

                    // No more options are provided, just show status.
                    // For example: cluster myCluster restype myResourceType
                    if ( ( curOption + 1 ) == lastOption )
                    {
                        dwReturnValue = ListResTypes( NULL );
                    }

                } // else: this option has the right number of parameters

                break;

            } // case optDefault

            case optList:
            {
                dwReturnValue = ListResTypes( curOption );
                break;
            }

            case optCreate:
            {
                dwReturnValue = Create( *curOption );
                break;
            }

            case optDelete:
            {
                dwReturnValue = Delete( *curOption );
                break;
            }

            case optListOwners:
            {
                dwReturnValue = ShowPossibleOwners( *curOption );
                break;
            }

            // ResType does not support the /status option. So, don't pass it
            // on to the base class (which tries to handle /status).
            case optStatus:
            {
                se.LoadMessage( IDS_INVALID_OPTION, curOption->GetName() );
                throw se;
            }

            default:
            {
                dwReturnValue = CGenericModuleCmd::Execute( *curOption );
            }

        } // switch: based on the type of option

        PrintMessage( MSG_OPTION_FOOTER, curOption->GetName() );
        ++curOption;
    } // for each option in the list

    return dwReturnValue;

} //*** CResTypeCmd::Execute()


/////////////////////////////////////////////////////////////////////////////
//++
//
//  CResTypeCmd::PrintHelp
//
//  Routine Description:
//      Prints help for Resource Types
//
//  Arguments:
//      None.
//
//  Member variables used / set:
//      None.
//
//  Return Value:
//      ERROR_SUCCESS               on success
//      Win32 Error code            on failure
//
//--
/////////////////////////////////////////////////////////////////////////////
DWORD CResTypeCmd::PrintHelp( void )
{
    return PrintMessage( MSG_HELP_RESTYPE );

} //*** CResTypeCmd::PrintHelp()


/////////////////////////////////////////////////////////////////////////////
//++
//
//  CResTypeCmd::SeeHelpStringID
//
//  Routine Description:
//      Provides the message ID of the string that shows what command line to
//      use to get help for this kind of command.
//
//  Arguments:
//      None.
//
//  Member variables used / set:
//      None.
//
//  Return Value:
//      The command-specific message ID.
//
//--
/////////////////////////////////////////////////////////////////////////////
DWORD CResTypeCmd::SeeHelpStringID() const
{
    return MSG_SEE_RESTYPE_HELP;
}


/////////////////////////////////////////////////////////////////////////////
//++
//
//  CResTypeCmd::Create
//
//  Routine Description:
//      Create a resource type.  Reads the command line to get
//      additional options
//
//  Arguments:
//      IN  const CCmdLineOption & thisOption
//          Contains the type, values and arguments of this option.
//
//  Exceptions:
//      CSyntaxException
//          Thrown for incorrect command line syntax.
//
//  Member variables used / set:
//      m_hCluster                  Cluster Handle
//      m_hModule                   Resource Type Handle
//      m_strDisplayName            Display name of resource type
//
//  Return Value:
//      ERROR_SUCCESS               on success
//      Win32 Error code            on failure
//
//--
/////////////////////////////////////////////////////////////////////////////
DWORD CResTypeCmd::Create( const CCmdLineOption & thisOption )
    throw( CSyntaxException )
{
    CSyntaxException se( SeeHelpStringID() );

    // This option takes no values.
    if ( thisOption.GetValues().size() != 0 )
    {
        se.LoadMessage( MSG_OPTION_NO_VALUES, thisOption.GetName() );
        throw se;
    }

    CString strDLLName;
    CString strTypeName;
    DWORD dwLooksAlivePollInterval = CLUSTER_RESTYPE_DEFAULT_LOOKS_ALIVE;
    DWORD dwIsAlivePollInterval = CLUSTER_RESTYPE_DEFAULT_IS_ALIVE;

    const vector<CCmdLineParameter> & paramList = thisOption.GetParameters();
    vector<CCmdLineParameter>::const_iterator curParam = paramList.begin();
    vector<CCmdLineParameter>::const_iterator last = paramList.end();
    BOOL bDLLNameFound = FALSE, bTypeFound = FALSE,
         bIsAliveFound = FALSE, bLooksAliveFound = FALSE;

    while( curParam != last )
    {
        const vector<CString> & valueList = curParam->GetValues();

        switch( curParam->GetType() )
        {
            case paramDLLName:
                // Each of the parameters must have exactly one value.
                if ( valueList.size() != 1 )
                {
                    se.LoadMessage( MSG_PARAM_ONLY_ONE_VALUE, curParam->GetName() );
                    throw se;
                }

                if ( bDLLNameFound != FALSE )
                {
                    se.LoadMessage( MSG_PARAM_REPEATS, curParam->GetName() );
                    throw se;
                }

                strDLLName = valueList[0];
                bDLLNameFound = TRUE;
                break;

            case paramResType:
                // Each of the parameters must have exactly one value.
                if ( valueList.size() != 1 )
                {
                    se.LoadMessage( MSG_PARAM_ONLY_ONE_VALUE, curParam->GetName() );
                    throw se;
                }

                if ( bTypeFound != FALSE )
                {
                    se.LoadMessage( MSG_PARAM_REPEATS, curParam->GetName() );
                    throw se;
                }

                strTypeName = valueList[0];
                bTypeFound = TRUE;
                break;

            case paramLooksAlive:
                // Each of the parameters must have exactly one value.
                if ( valueList.size() != 1 )
                {
                    se.LoadMessage( MSG_PARAM_ONLY_ONE_VALUE, curParam->GetName() );
                    throw se;
                }

                if ( bLooksAliveFound != FALSE )
                {
                    se.LoadMessage( MSG_PARAM_REPEATS, curParam->GetName() );
                    throw se;
                }

                dwLooksAlivePollInterval = _wtol( valueList[0] );
                bLooksAliveFound = TRUE;
                break;

            case paramIsAlive:
                // Each of the parameters must have exactly one value.
                if ( valueList.size() != 1 )
                {
                    se.LoadMessage( MSG_PARAM_ONLY_ONE_VALUE, curParam->GetName() );
                    throw se;
                }

                if ( bIsAliveFound != FALSE )
                {
                    se.LoadMessage( MSG_PARAM_REPEATS, curParam->GetName() );
                    throw se;
                }

                dwIsAlivePollInterval = _wtoi( valueList[0] );
                bIsAliveFound = TRUE;
                break;

            default:
            {
                se.LoadMessage( MSG_INVALID_PARAMETER, curParam->GetName() );
                throw se;
            }
        }

        ++curParam;
    }


    // Check for missing parameters.
    if ( strDLLName.IsEmpty() )
    {
        se.LoadMessage( MSG_MISSING_DLLNAME );
        throw se;
    }

    if ( strTypeName.IsEmpty() )
        strTypeName = m_strDisplayName;

    // Execute command
    DWORD dwError = OpenCluster();
    if ( dwError != ERROR_SUCCESS )
        return dwError;

    dwError = CreateClusterResourceType(
        m_hCluster,
        strTypeName,
        m_strDisplayName,
        strDLLName,
        dwLooksAlivePollInterval,
        dwIsAlivePollInterval );

    if ( dwError == ERROR_SUCCESS )
        PrintMessage( MSG_RESTCMD_CREATE, m_strDisplayName );

    return dwError;

} //*** CResTypeCmd::Create()


/////////////////////////////////////////////////////////////////////////////
//++
//
//  CResTypeCmd::Delete
//
//  Routine Description:
//      Delete a resource type.  Accepts an optional /TYPE parameter
//      to denote that the specified name is a resource type name
//      and not a display name
//
//  Arguments:
//      IN  const CCmdLineOption & thisOption
//          Contains the type, values and arguments of this option.
//
//  Exceptions:
//      CSyntaxException
//          Thrown for incorrect command line syntax.
//
//  Member variables used / set:
//      m_hCluster                  Cluster Handle
//      m_hModule                   Resource Type Handle
//      m_strDisplayName            Display name of resource type
//      m_strModuleName             Name of resource type
//
//  Return Value:
//      ERROR_SUCCESS               on success
//      Win32 Error code            on failure
//
//--
/////////////////////////////////////////////////////////////////////////////
DWORD CResTypeCmd::Delete( const CCmdLineOption & thisOption )
    throw( CSyntaxException )
{
    CSyntaxException se( SeeHelpStringID() );

    // This option takes no values.
    if ( thisOption.GetValues().size() != 0 )
    {
        se.LoadMessage( MSG_OPTION_NO_VALUES, thisOption.GetName() );
        throw se;
    }

    DWORD dwError = OpenCluster();
    if ( dwError != ERROR_SUCCESS )
        return dwError;

    CString strResTypeName;

    const vector<CCmdLineParameter> & paramList = thisOption.GetParameters();
    vector<CCmdLineParameter>::const_iterator curParam = paramList.begin();
    vector<CCmdLineParameter>::const_iterator last = paramList.end();
    BOOL bTypeFound = FALSE;

    while( curParam != last )
    {
        const vector<CString> & valueList = curParam->GetValues();

        switch( curParam->GetType() )
        {
            case paramResType:
            {
                // Each of the parameters must have exactly one value.
                if ( valueList.size() != 0 )
                {
                    se.LoadMessage( MSG_PARAM_NO_VALUES, curParam->GetName() );
                    throw se;
                }

                if ( bTypeFound != FALSE )
                {
                    se.LoadMessage( MSG_PARAM_REPEATS, curParam->GetName() );
                    throw se;
                }

                strResTypeName = m_strDisplayName;
                bTypeFound = TRUE;
                break;
            }

            default:
            {
                se.LoadMessage( MSG_INVALID_PARAMETER, curParam->GetName() );
                throw se;
            }

        } // switch: based on the type of the parameter

        ++curParam;
    }


    if ( strResTypeName.IsEmpty() != FALSE )
    {
        dwError = OpenModule();
        if ( dwError != ERROR_SUCCESS )
            return dwError;
        strResTypeName = m_strModuleName;
    }

    dwError = DeleteClusterResourceType( m_hCluster, strResTypeName );

    if ( dwError == ERROR_SUCCESS )
        return PrintMessage( MSG_RESTCMD_DELETE, strResTypeName );

    return dwError;

} //*** CResTypeCmd:Delete()


/////////////////////////////////////////////////////////////////////////////
//++
//
//  CResTypeCmd::ShowPossibleOwners
//
//  Routine Description:
//      Display the nodes which can own a resource type.
//
//  Arguments:
//      IN  const CCmdLineOption & thisOption
//          Contains the type, values and arguments of this option.
//
//  Exceptions:
//      CSyntaxException
//          Thrown for incorrect command line syntax.
//
//  Member variables used / set:
//      m_hCluster                  Cluster Handle
//      m_hModule                   Resource Type Handle
//      m_strDisplayName            Display name of resource type
//      m_strModuleName             Name of resource type
//
//  Return Value:
//      ERROR_SUCCESS               on success
//      Win32 Error code            on failure
//
//--
/////////////////////////////////////////////////////////////////////////////
DWORD CResTypeCmd::ShowPossibleOwners( const CCmdLineOption & thisOption )
    throw( CSyntaxException )
{
    CSyntaxException se( SeeHelpStringID() );
    DWORD       sc = ERROR_SUCCESS;
    HCLUSENUM   hClusEnum = NULL;
    CString     strResTypeName;
    BOOL        bTypeFound = FALSE;
    DWORD       dwType;
    LPWSTR      pszNameBuffer;
    DWORD       dwNameBufferSize = 256; // some arbitrary starting buffer size
    DWORD       dwRequiredSize = dwNameBufferSize;

    // This option takes no values.
    if ( thisOption.GetValues().size() != 0 )
    {
        se.LoadMessage( MSG_OPTION_NO_VALUES, thisOption.GetName() );
        throw se;
    }

    sc = OpenCluster();
    if ( sc != ERROR_SUCCESS )
    {
        goto Cleanup;
    }

    const vector<CCmdLineParameter> & paramList = thisOption.GetParameters();
    vector<CCmdLineParameter>::const_iterator curParam = paramList.begin();
    vector<CCmdLineParameter>::const_iterator last = paramList.end();

    while( curParam != last )
    {
        const vector<CString> & valueList = curParam->GetValues();

        switch( curParam->GetType() )
        {
            case paramResType:
            {
                // This parameter does not take a value.
                if ( valueList.size() != 0 )
                {
                    se.LoadMessage( MSG_PARAM_NO_VALUES, curParam->GetName() );
                    throw se;
                }

                if ( bTypeFound != FALSE )
                {
                    se.LoadMessage( MSG_PARAM_REPEATS, curParam->GetName() );
                    throw se;
                }

                strResTypeName = m_strDisplayName;
                bTypeFound = TRUE;
                break;
            }

            default:
            {
                se.LoadMessage( MSG_INVALID_PARAMETER, curParam->GetName() );
                throw se;
            }

        } // switch: based on the type of the parameter

        ++curParam;
    } // while: curParam != last

    // The /type switch has not been specified and a display name has been given.
    if ( ( bTypeFound == FALSE ) && ( m_strDisplayName.IsEmpty() == FALSE ) )
    {
        sc = OpenModule();
        if ( sc != ERROR_SUCCESS )
        {
            goto Cleanup;
        }
        strResTypeName = m_strModuleName;
    }

    if ( strResTypeName.IsEmpty() != FALSE )
    {
        // No type name is given. Show possible owners of all resource types.

        // If the type name is not specified, no other parameters are allowed.
        if ( thisOption.GetParameters().size() != 0 )
        {
            se.LoadMessage( MSG_OPTION_NO_PARAMETERS, thisOption.GetName() );
            throw se;
        }

        // Open an enumeration of the resource types.
        hClusEnum = ClusterOpenEnum( m_hCluster, CLUSTER_ENUM_RESTYPE );
        if ( hClusEnum == NULL )
        {
            sc = GetLastError();
            goto Cleanup;
        }

        // Allocate a buffer for holding the name of the resource types.
        pszNameBuffer = (LPWSTR) LocalAlloc( LMEM_FIXED, dwNameBufferSize * sizeof( *pszNameBuffer ) );
        if ( pszNameBuffer == NULL )
        {
            ClusterCloseEnum( hClusEnum );
            sc = GetLastError();
            goto Cleanup;
        }

        PrintMessage( MSG_RESTYPE_POSSIBLE_OWNERS_LIST_ALL );
        PrintMessage( MSG_HEADER_RESTYPE_POSSIBLE_OWNERS );

        DWORD dwIndex = 0;
        do
        {
            dwRequiredSize = dwNameBufferSize;
            sc = ClusterEnum( hClusEnum, dwIndex, &dwType,
                                   pszNameBuffer, &dwRequiredSize );

            // Buffer space is insufficient. Allocate some more.
            if ( sc == ERROR_MORE_DATA )
            {
                // Make space for the NULL character.
                ++dwRequiredSize;

                LPWSTR pszNewMemory = (LPWSTR) LocalReAlloc( pszNameBuffer,
                                                             dwRequiredSize * sizeof( *pszNameBuffer ),
                                                             LMEM_FIXED );
                if ( pszNewMemory == NULL )
                {
                    sc = GetLastError();
                    break;
                }

                pszNameBuffer = pszNewMemory;
                dwNameBufferSize = dwRequiredSize;

                sc = ClusterEnum( hClusEnum, dwIndex, &dwType,
                                       pszNameBuffer, &dwRequiredSize );

            } // if: more buffer space is needed
            else
            {
                // We are finished with the enumeration.
                if ( sc == ERROR_NO_MORE_ITEMS )
                {
                    sc = ERROR_SUCCESS;
                    break;
                }

                // Something went wrong. Don't proceed.
                if ( sc != ERROR_SUCCESS )
                {
                    break;
                }

                sc = ResTypePossibleOwners( pszNameBuffer );

            } // else: buffer space was sufficient.

            ++dwIndex;
        }
        while ( sc == ERROR_SUCCESS );

        LocalFree( pszNameBuffer );
        ClusterCloseEnum( hClusEnum );

    } // if: no resource type has been specified.
    else
    {
        // Type name found. Show possible owner owners for this resource type only.

        PrintMessage( MSG_RESTYPE_POSSIBLE_OWNERS_LIST, strResTypeName );
        PrintMessage( MSG_HEADER_RESTYPE_POSSIBLE_OWNERS );
        sc = ResTypePossibleOwners( strResTypeName );

    } // else: resource type name has been specified.

Cleanup:

    return sc;

} //*** CResTypeCmd::ShowPossibleOwners()


/////////////////////////////////////////////////////////////////////////////
//++
//
//  CResTypeCmd::ResTypePossibleOwners
//
//  Routine Description:
//      Display the nodes which can own this particular resource type.
//
//  Arguments:
//      IN  const CString & strResTypeName
//          The possible owners of this resource type are displayed.
//
//  Return Value:
//      ERROR_SUCCESS               on success
//      Win32 Error code            on failure
//
//--
/////////////////////////////////////////////////////////////////////////////
DWORD CResTypeCmd::ResTypePossibleOwners( const CString & strResTypeName )
{
    HRESTYPEENUM    hResTypeEnum = NULL;
    DWORD           sc = ERROR_SUCCESS;
    DWORD           dwType;
    LPWSTR          pszNameBuffer = NULL;
    DWORD           dwNameBufferSize = 256; // some arbitrary starting buffer size
    DWORD           dwRequiredSize = dwNameBufferSize;


    hResTypeEnum = ClusterResourceTypeOpenEnum( m_hCluster,
                                                strResTypeName,
                                                CLUSTER_RESOURCE_TYPE_ENUM_NODES );

    // Could not open the resource type enumeration
    if ( hResTypeEnum == NULL )
    {
        sc = GetLastError();
        goto Cleanup;
    }

    // Allocate a buffer for holding the name of the possible owner node.
    pszNameBuffer = (LPWSTR) LocalAlloc( LMEM_FIXED, dwNameBufferSize * sizeof( *pszNameBuffer ) );
    if ( pszNameBuffer == NULL )
    {
        ClusterResourceTypeCloseEnum( hResTypeEnum );
        sc = GetLastError();
        goto Cleanup;
    }

    DWORD dwIndex = 0;
    for ( ;; )
    {
        dwRequiredSize = dwNameBufferSize;
        sc = ClusterResourceTypeEnum( hResTypeEnum, dwIndex, &dwType,
                                           pszNameBuffer, &dwRequiredSize );

        // Buffer space is insufficient. Allocate some more.
        if ( sc == ERROR_MORE_DATA )
        {
            // Make space for the NULL character.
            ++dwRequiredSize;

            LPWSTR pszNewMemory = (LPWSTR) LocalReAlloc( pszNameBuffer,
                                                         dwRequiredSize * sizeof( *pszNameBuffer ),
                                                         LMEM_FIXED );
            if ( pszNewMemory == NULL )
            {
                sc = GetLastError();
                break;
            }

            pszNameBuffer = pszNewMemory;
            dwNameBufferSize = dwRequiredSize;

            sc = ClusterResourceTypeEnum( hResTypeEnum, dwIndex, &dwType,
                                               pszNameBuffer, &dwRequiredSize );

        } // if: more buffer space is needed
        else
        {
            // We are finished with the enumeration.
            if ( sc == ERROR_NO_MORE_ITEMS )
            {
                sc = ERROR_SUCCESS;
                break;
            }

            // Something went wrong. Don't proceed.
            if ( sc != ERROR_SUCCESS )
            {
                break;
            }

            PrintMessage( MSG_RESTYPE_POSSIBLE_OWNERS, strResTypeName, pszNameBuffer );

        } // else: buffer space was sufficient.

        ++dwIndex;
    } // for: ever

Cleanup:

    LocalFree( pszNameBuffer );

    if ( hResTypeEnum != NULL )
    {
        ClusterResourceTypeCloseEnum( hResTypeEnum );
    }

    return sc;

} // CResTypeCmd::ResTypePossibleOwners(

/////////////////////////////////////////////////////////////////////////////
//++
//
//  CResTypeCmd::DoProperties
//
//  Routine Description:
//      Dispatches the property command to either Get, Set, or All properties
//
//  Arguments:
//      IN  const CCmdLineOption & thisOption
//          Contains the type, values and arguments of this option.
//
//      IN  PropertyType ePropertyType
//          The type of property, PRIVATE or COMMON
//
//  Exceptions:
//      CSyntaxException
//          Thrown for incorrect command line syntax.
//
//  Member variables used / set:
//      m_hCluster                  SET (by OpenCluster)
//      m_hModule                   SET (by OpenModule)
//      m_strDisplayName            Name of module.  If non-NULL, prints
//                                  out properties for the specified module.
//                                  Otherwise, prints props for all modules.
//
//  Return Value:
//      ERROR_SUCCESS               on success
//      Win32 Error code            on failure
//
//--
/////////////////////////////////////////////////////////////////////////////
DWORD CResTypeCmd::DoProperties( const CCmdLineOption & thisOption,
                                 PropertyType ePropType )
    throw( CSyntaxException )
{
    DWORD dwError;

    if ( m_strDisplayName.IsEmpty() != FALSE )
        return AllProperties( thisOption, ePropType );

    dwError = OpenCluster();
    if ( dwError != ERROR_SUCCESS )
        return dwError;

    dwError = OpenModule();
    if ( dwError != ERROR_SUCCESS )
        return dwError;

    const vector<CCmdLineParameter> & paramList = thisOption.GetParameters();

    // If there are no property-value pairs on the command line,
    // then we print the properties otherwise we set them.
    if( paramList.size() == 0 )
    {
        PrintMessage( ePropType==PRIVATE ? MSG_PRIVATE_LISTING : MSG_PROPERTY_LISTING,
            (LPCWSTR) m_strModuleName );
        PrintMessage( m_dwMsgPropertyHeaderAll );
        return GetProperties( thisOption, ePropType, m_strModuleName);
    }
    else
        return SetProperties( thisOption, ePropType );

} //*** CResTypeCmd::DoProperties()


/////////////////////////////////////////////////////////////////////////////
//++
//
//  CResTypeCmd::GetProperties
//
//  Routine Description:
//      Prints out properties for the specified module
//      Needs to take into account the fact that it doesn't actually
//      open a handle to a resource type, so this function overrides the
//      default in CGenericModuleCmd
//
//  Arguments:
//      IN  const vector<CCmdLineParameter> & paramList
//          Contains the list of property-value pairs to be set
//
//      IN  PropertyType ePropertyType
//          The type of property, PRIVATE or COMMON
//
//      IN  LPCWSTR lpszResTypeName
//          Name of the module
//
//  Member variables used / set:
//      m_hModule                   Module handle
//      m_strModuleName             Name of resource type
//
//
//  Return Value:
//      ERROR_SUCCESS               on success
//      Win32 Error code            on failure
//
//--
/////////////////////////////////////////////////////////////////////////////
DWORD CResTypeCmd::GetProperties( const CCmdLineOption & thisOption,
                                  PropertyType ePropType, LPCWSTR lpszResTypeParam )
{
    assert( m_hCluster != NULL );

    DWORD dwError = ERROR_SUCCESS;
    LPCWSTR lpszResTypeName;

    // If no lpszResTypeName specified, use current resource type,
    if ( ! lpszResTypeParam )
    {
        lpszResTypeName = m_strModuleName;
    }
    else
    {
        lpszResTypeName = lpszResTypeParam;
    }


    // Use the proplist helper class.
    CClusPropList PropList;


    // Get R/O properties
    DWORD dwControlCode = ePropType==PRIVATE ? CLUSCTL_RESOURCE_TYPE_GET_RO_PRIVATE_PROPERTIES
                             : CLUSCTL_RESOURCE_TYPE_GET_RO_COMMON_PROPERTIES;

    dwError = PropList.ScGetResourceTypeProperties(
        m_hCluster,
        lpszResTypeName,
        dwControlCode
        );

    if ( dwError != ERROR_SUCCESS )
        return dwError;

    dwError = PrintProperties( PropList, thisOption.GetValues(), READONLY, lpszResTypeParam );
    if ( dwError != ERROR_SUCCESS )
        return dwError;

    // Get R/W properties
    dwControlCode = ePropType==PRIVATE ? CLUSCTL_RESOURCE_TYPE_GET_PRIVATE_PROPERTIES
                               : CLUSCTL_RESOURCE_TYPE_GET_COMMON_PROPERTIES;

    dwError = PropList.ScGetResourceTypeProperties(
        m_hCluster,
        lpszResTypeName,
        dwControlCode
        );

    if ( dwError != ERROR_SUCCESS )
        return dwError;

    dwError = PrintProperties( PropList, thisOption.GetValues(), READWRITE, lpszResTypeParam );

    return dwError;

} //*** CResTypeCmd::GetProperties()


/////////////////////////////////////////////////////////////////////////////
//++
//
//  CResTypeCmd::SetProperties
//
//  Routine Description:
//      Set the properties for the specified module
//      Needs to take into account the fact that it doesn't actually
//      open a handle to a resource type, so this function overrides the
//      default in CGenericModuleCmd
//
//  Arguments:
//      IN  const CCmdLineOption & thisOption
//          Contains the type, values and arguments of this option.
//
//      IN  PropertyType ePropertyType
//          The type of property, PRIVATE or COMMON
//
//  Member variables used / set:
//      m_hModule                   Module handle
//      m_strModuleName             Name of resource type
//
//
//  Return Value:
//      ERROR_SUCCESS               on success
//      Win32 Error code            on failure
//
//--
/////////////////////////////////////////////////////////////////////////////
DWORD CResTypeCmd::SetProperties( const CCmdLineOption & thisOption,
                                  PropertyType ePropType )
    throw( CSyntaxException )
{
    assert( m_hCluster != NULL );

    DWORD dwError = ERROR_SUCCESS;
    DWORD dwControlCode;
    DWORD dwBytesReturned = 0;
    CSyntaxException se( SeeHelpStringID() );

    // Use the proplist helper class.
    CClusPropList CurrentProps;
    CClusPropList NewProps;

    LPCWSTR lpszResTypeName = m_strModuleName;

    // First get the existing properties...
    dwControlCode = ePropType==PRIVATE ? CLUSCTL_RESOURCE_TYPE_GET_PRIVATE_PROPERTIES
                                       : CLUSCTL_RESOURCE_TYPE_GET_COMMON_PROPERTIES;

    dwError = CurrentProps.ScGetResourceTypeProperties(
        m_hCluster,
        lpszResTypeName,
        dwControlCode
        );

    if ( dwError != ERROR_SUCCESS )
        return dwError;


    // If values have been specified with this option, then it means that we want
    // to set these properties to their default values. So, there has to be
    // exactly one parameter and it has to be /USEDEFAULT.
    if ( thisOption.GetValues().size() != 0 )
    {
        const vector<CCmdLineParameter> & paramList = thisOption.GetParameters();

        if ( paramList.size() != 1 )
        {
            se.LoadMessage( MSG_EXTRA_PARAMETERS_ERROR_WITH_NAME, thisOption.GetName() );
            throw se;
        }

        if ( paramList[0].GetType() != paramUseDefault )
        {

            se.LoadMessage( MSG_INVALID_PARAMETER, paramList[0].GetName() );
            throw se;
        }

        // This parameter does not take any values.
        if ( paramList[0].GetValues().size() != 0 )
        {
            se.LoadMessage( MSG_PARAM_NO_VALUES, paramList[0].GetName() );
            throw se;
        }

        dwError = ConstructPropListWithDefaultValues( CurrentProps, NewProps, thisOption.GetValues() );
        if( dwError != ERROR_SUCCESS )
            return dwError;

    } // if: values have been specified with this option.
    else
    {
        dwError = ConstructPropertyList( CurrentProps, NewProps, thisOption.GetParameters(), FALSE, SeeHelpStringID() );
        if ( dwError != ERROR_SUCCESS )
            return dwError;
    }


    // Call the set function...
    dwControlCode = ePropType==PRIVATE ? CLUSCTL_RESOURCE_TYPE_SET_PRIVATE_PROPERTIES
                             : CLUSCTL_RESOURCE_TYPE_SET_COMMON_PROPERTIES;

    dwBytesReturned = 0;
    dwError = ClusterResourceTypeControl(
        m_hCluster,
        lpszResTypeName,
        NULL, // hNode
        dwControlCode,
        NewProps.Plist(),
        (DWORD) NewProps.CbBufferSize(),
        0,
        0,
        &dwBytesReturned );

    return dwError;

} //*** CResTypeCmd::SetProperties()


/////////////////////////////////////////////////////////////////////////////
//++
//
//  CResTypeCmd::ListResTypes
//
//  Routine Description:
//      Prints out all the available resource types.  Akin to Status
//      for most other modules
//
//  Arguments:
//      IN  const CCmdLineOption & thisOption
//          Contains the type, values and arguments of this option.
//
//  Exceptions:
//      CSyntaxException
//          Thrown for incorrect command line syntax.
//
//  Member variables used / set:
//      m_hCluster                  SET (by OpenCluster)
//      m_strDisplayName            Name of module.  If non-NULL, prints
//                                  out info for the specified module.
//                                  Otherwise, prints props for all modules.
//
//
//  Return Value:
//      ERROR_SUCCESS               on success
//      Win32 Error code            on failure
//
//--
/////////////////////////////////////////////////////////////////////////////
DWORD CResTypeCmd::ListResTypes( const CCmdLineOption * pOption )
    throw( CSyntaxException )
{
    CSyntaxException se( SeeHelpStringID() );
    DWORD           sc      = ERROR_SUCCESS;
    HCLUSENUM       hEnum   = NULL;
    DWORD           dwIndex = 0;
    DWORD           dwType  = 0;
    LPWSTR          pszName = NULL;

    // pOption will be NULL if this function has been called as the
    // default action.
    if ( pOption != NULL )
    {
        // This option takes no values.
        if ( pOption->GetValues().size() != 0 )
        {
            se.LoadMessage( MSG_OPTION_NO_VALUES, pOption->GetName() );
            throw se;
        } // if:

        // This option takes no parameters.
        if ( pOption->GetParameters().size() != 0 )
        {
            se.LoadMessage( MSG_OPTION_NO_PARAMETERS, pOption->GetName() );
            throw se;
        } // if:
    } // if:

    sc = OpenCluster();
    if ( sc != ERROR_SUCCESS )
    {
        goto Cleanup;
    } // if:

    if ( m_strDisplayName.IsEmpty() == FALSE )
    {
        PrintMessage( MSG_RESTYPE_STATUS_LIST, m_strDisplayName );
        PrintMessage( MSG_RESTYPE_STATUS_HEADER );
        sc = PrintResTypeInfo( m_strDisplayName );
        goto Cleanup;
    } // if:

    hEnum = ClusterOpenEnum( m_hCluster, CLUSTER_ENUM_RESTYPE );
    if( hEnum == NULL )
    {
        sc = GetLastError();
        goto Cleanup;
    } // if:

    PrintMessage( MSG_RESTYPE_STATUS_LIST_ALL );
    PrintMessage( MSG_RESTYPE_STATUS_HEADER );

    for ( dwIndex = 0; sc == ERROR_SUCCESS; dwIndex++ )
    {
        sc = WrapClusterEnum( hEnum, dwIndex, &dwType, &pszName );
        if ( sc == ERROR_SUCCESS )
        {
            PrintResTypeInfo( pszName ); // option.svValue == nodename
        } // if:

        // LocalFree on NULL is ok.
        LocalFree( pszName );
    } // for:

    if( sc == ERROR_NO_MORE_ITEMS )
    {
        sc = ERROR_SUCCESS;
    } // if:

    ClusterCloseEnum( hEnum );

Cleanup:

    return sc;

} //*** CResTypeCmd::ListResTypes()


/////////////////////////////////////////////////////////////////////////////
//++
//
//  CResTypeCmd::PrintResTypeInfo
//
//  Routine Description:
//      Prints out info for the specified resource type
//
//  Arguments:
//      pszResTypeName              Name of the resource type
//
//  Member variables used / set:
//      m_hCluster                  Cluster handle
//
//
//  Return Value:
//      ERROR_SUCCESS               on success
//      Win32 Error code            on failure
//
//--
/////////////////////////////////////////////////////////////////////////////
DWORD CResTypeCmd::PrintResTypeInfo( LPCWSTR pszResTypeName )
{
    DWORD   _sc             = ERROR_SUCCESS;
    LPWSTR  _pszDisplayName = NULL;

    _sc = ScResTypeNameToDisplayName( m_hCluster, pszResTypeName, &_pszDisplayName );
    if ( _sc == ERROR_SUCCESS )
    {
        PrintMessage( MSG_RESTYPE_STATUS, _pszDisplayName, pszResTypeName );
    } // if:  resource type name information retrieved successfully
    else
    {
        PrintMessage( MSG_RESTYPE_STATUS_ERROR, pszResTypeName );
    }

    LocalFree( _pszDisplayName );

    return _sc;

} //*** CResTypeCmd::PrintResTypeInfo()

