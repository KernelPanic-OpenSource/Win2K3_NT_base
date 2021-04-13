//////////////////////////////////////////////////////////////////////////////
//
//  Copyright (c) 1999-2002 Microsoft Corporation
//
//  Module Name:
//      ClusterGroup.cpp
//
//  Description:    
//      Implementation of CClusterGroup class 
//
//  Maintained by:
//      Ozan Ozhan  (OzanO)     26-NOV-2002
//      Henry Wang  (HenryWa)   24-AUG-1999
//
//////////////////////////////////////////////////////////////////////////////

#include "Pch.h"
#include "ClusterGroup.h"
#include "ClusterGroup.tmh"

//****************************************************************************
//
//  CClusterGroup
//
//****************************************************************************

//////////////////////////////////////////////////////////////////////////////
//++
//
//  CClusterGroup::CClusterGroup
//
//  Description:
//      Constructor.
//
//  Arguments:
//      pwszNameIn      -- Class name
//      pNamespaceIn    -- Namespace
//
//  Return Values:
//      None.
//
//--
//////////////////////////////////////////////////////////////////////////////
CClusterGroup::CClusterGroup(
    LPCWSTR         pwszNameIn,
    CWbemServices * pNamespaceIn
    )
    : CProvBase( pwszNameIn, pNamespaceIn )
{
} //*** CClusterGroup::CClusterGroup()

//////////////////////////////////////////////////////////////////////////////
//++
//
//  static
//  CClusterGroup::S_CreateThis
//
//  Description:
//      Create a CClusterGroup object.
//
//  Arguments:
//      pwszNameIn      -- Class name
//      pNamespaceIn    -- Namespace
//      dwEnumTypeIn    -- Type id
//
//  Return Values:
//      pointer to the CProvBase
//
//--
//////////////////////////////////////////////////////////////////////////////
CProvBase *
CClusterGroup::S_CreateThis(
    LPCWSTR         pwszNameIn,
    CWbemServices * pNamespaceIn,
    DWORD           dwEnumTypeIn
    )
{
    TracePrint(("CClusterGroup::S_CreatThis for Name = %ws, EnumType %u\n", pwszNameIn, dwEnumTypeIn )); 
    return new CClusterGroup( pwszNameIn, pNamespaceIn );

} //*** CClusterGroup::S_CreateThis()

//////////////////////////////////////////////////////////////////////////////
//++
//
//  CClusterGroup::GetPropMap
//
//  Description:
//      Retrieve the property maping table of the cluster group.
//
//  Arguments:
//      None.
//
//  Return Values:
//      Reference to the array of property maping table.
//
//--
//////////////////////////////////////////////////////////////////////////////
const SPropMapEntryArray *
CClusterGroup::RgGetPropMap( void )
{

    static SPropMapEntry s_rgpm[] =
    {
        {
            NULL,
            CLUSREG_NAME_GRP_LOADBAL_STATE,
            DWORD_TYPE,
            READWRITE
        }
    };

    static SPropMapEntryArray   s_pamea(
                sizeof( s_rgpm ) / sizeof( SPropMapEntry ),
                s_rgpm
                );

    return &s_pamea;

} //*** CClusterGroup::GetPropMap()

//////////////////////////////////////////////////////////////////////////////
//++
//
//  CClusterGroup::EnumInstance
//
//  Description:
//      Enum cluster instance.
//
//  Arguments:
//      lFlagsIn    -- WMI flag
//      pCtxIn      -- WMI context
//      pHandlerIn  -- WMI sink pointer
//
//  Return Values:
//      WBEM_S_NO_ERROR
//
//--
//////////////////////////////////////////////////////////////////////////////
SCODE
CClusterGroup::EnumInstance(
    long                 lFlagsIn,
    IWbemContext *       pCtxIn,
    IWbemObjectSink *    pHandlerIn
    )
{
    SAFECLUSTER     shCluster;
    SAFEGROUP       shGroup;
    LPCWSTR         pwszGroup;

    TracePrint(( "CClusterGroup::EnumInstance, pHandlerIn = %p\n", pHandlerIn ));
    shCluster = OpenCluster( NULL );
    CClusterEnum cluEnum( shCluster, CLUSTER_ENUM_GROUP );

    while ( ( pwszGroup = cluEnum.GetNext() ) != NULL )
    {
        shGroup = OpenClusterGroup( shCluster, pwszGroup );

        ClusterToWMI( shGroup, pHandlerIn );

    } // while: more groups

    return WBEM_S_NO_ERROR;

} //*** CClusterGroup::EnumInstance()

//////////////////////////////////////////////////////////////////////////////
//++
//
//  CClusterGroup::ClusterToWMI
//
//  Description:
//      Translate a cluster group object to WMI object.
//
//  Arguments:
//      hGroupIn    -- Handle to group
//      pHandlerIn  -- Handler
//
//  Return Values:
//      None.
//
//--
//////////////////////////////////////////////////////////////////////////////
void
CClusterGroup::ClusterToWMI(
    HGROUP              hGroupIn,
    IWbemObjectSink *   pHandlerIn
    )
{
    static SGetControl  s_rgControl[] =
    {
        { CLUSCTL_GROUP_GET_RO_COMMON_PROPERTIES,   FALSE },
        { CLUSCTL_GROUP_GET_COMMON_PROPERTIES,      FALSE },
        { CLUSCTL_GROUP_GET_RO_PRIVATE_PROPERTIES,  TRUE },
        { CLUSCTL_GROUP_GET_PRIVATE_PROPERTIES,     TRUE }
    };
    static UINT         s_cControl = sizeof( s_rgControl ) / sizeof( SGetControl );

    CError              er;
    UINT                idx;
    CWbemClassObject    wco;

    m_pClass->SpawnInstance( 0, &wco );
    for ( idx = 0 ; idx < s_cControl ; idx++ )
    {
        CClusPropList pl;
        er = pl.ScGetGroupProperties(
                    hGroupIn,
                    s_rgControl[ idx ].dwControl,
                    NULL,
                    0
                    );

        CClusterApi::GetObjectProperties(
            RgGetPropMap(),
            pl,
            wco,
            s_rgControl[ idx ].fPrivate
            );

    } // for: each common property type

    //
    // flags and characteristics
    //
    {
        DWORD   cbReturned;
        DWORD   dwOut;
        DWORD   dwState;

        er = ClusterGroupControl( 
            hGroupIn,
            NULL,
            CLUSCTL_GROUP_GET_CHARACTERISTICS,
            NULL,
            0,
            &dwOut,
            sizeof( DWORD ),
            &cbReturned
            );
        wco.SetProperty(
            dwOut,
            PVD_PROP_CHARACTERISTIC
            );

        dwState = GetClusterGroupState(
                            hGroupIn,
                            NULL,
                            NULL
                            );
        wco.SetProperty( dwState, PVD_PROP_GROUP_STATE );
    }

    pHandlerIn->Indicate( 1, &wco );
    return;

} //*** CClusterGroup::ClusterToWMI()

//////////////////////////////////////////////////////////////////////////////
//++
//
//  CClusterGroup::GetObject
//
//  Description:
//      Retrieve cluster group object based given object path.
//
//  Arguments:
//      rObjPathIn  -- Object path to cluster object
//      lFlagsIn    -- WMI flag
//      pCtxIn      -- WMI context
//      pHandlerIn  -- WMI sink pointer
//
//  Return Values:
//      WBEM_S_NO_ERROR
//
//--
//////////////////////////////////////////////////////////////////////////////
SCODE
CClusterGroup::GetObject(
    CObjPath &           rObjPathIn,
    long                 lFlagsIn,
    IWbemContext *       pCtxIn,
    IWbemObjectSink *    pHandlerIn
    )
{
    SAFECLUSTER     shCluster;
    SAFEGROUP       shGroup;
    
    shCluster = OpenCluster( NULL );
    shGroup = OpenClusterGroup(
        shCluster,
        rObjPathIn.GetStringValueForProperty( PVD_PROP_NAME )
        );

    ClusterToWMI( shGroup, pHandlerIn );

    return WBEM_S_NO_ERROR;
        
} //*** CClusterGroup::GetObject()

//////////////////////////////////////////////////////////////////////////////
//++
//
//  CClusterGroup::ExecuteMethod
//
//  Description:
//      Execute methods defined in the mof for cluster node.
//
//  Arguments:
//      rObjPathIn          -- Object path to cluster object
//      pwszMethodNameIn    -- Name of the method to be invoked
//      lFlagIn             -- WMI flag
//      pParamsIn           -- Input parameters for the method
//      pHandlerIn          -- WMI sink pointer
//
//  Return Values:
//      WBEM_S_NO_ERROR
//
//--
//////////////////////////////////////////////////////////////////////////////
SCODE
CClusterGroup::ExecuteMethod(
    CObjPath &           rObjPathIn,
    WCHAR *              pwszMethodNameIn,
    long                 lFlagIn,
    IWbemClassObject *   pParamsIn,
    IWbemObjectSink *    pHandlerIn
    )
{
    SAFECLUSTER         shCluster;
    SAFEGROUP           shGroup;
    CWbemClassObject    InArgs( pParamsIn );
    CError              er;
    
    shCluster = OpenCluster( NULL );
    //
    // static method
    //
    if ( ClRtlStrICmp( pwszMethodNameIn, PVD_MTH_GROUP_CREATEGROUP ) == 0 )
    {
        
        _bstr_t bstrNewGroup;
        InArgs.GetProperty( bstrNewGroup, PVD_MTH_GROUP_PARM_GROUPNAME );
        shGroup = CreateClusterGroup( shCluster, bstrNewGroup );

        er = HrWrapOnlineClusterGroup( shCluster, shGroup );
    } // if: CREATEGROUP
    else
    {
        shGroup = OpenClusterGroup(
            shCluster,
            rObjPathIn.GetStringValueForProperty( PVD_PROP_GROUP_NAME )
            );
    
        if ( ClRtlStrICmp( pwszMethodNameIn, PVD_MTH_GROUP_DELETEGROUP ) == 0 )
        {
            er = DeleteClusterGroup( shGroup );
        }
        else if ( ClRtlStrICmp( pwszMethodNameIn, PVD_MTH_GROUP_TAKEOFFLINE ) == 0 )
        {
            DWORD dwTimeOut = 0;
            InArgs.GetProperty( &dwTimeOut, PVD_MTH_GROUP_PARM_TIMEOUT );
            er = HrWrapOfflineClusterGroup( shCluster, shGroup, dwTimeOut );
        } // if: TAKEOFFLINE
        else if ( ClRtlStrICmp( pwszMethodNameIn, PVD_MTH_GROUP_BRINGONLINE ) == 0 )
        {
            DWORD dwTimeOut = 0;
            InArgs.GetProperty( &dwTimeOut, PVD_MTH_GROUP_PARM_TIMEOUT );
            er = HrWrapOnlineClusterGroup( shCluster, shGroup, NULL, dwTimeOut );
        } // else if: BRINGONLINE
        else if ( ClRtlStrICmp( pwszMethodNameIn, PVD_MTH_GROUP_MOVETONEWNODE ) == 0 )
        {
            _bstr_t     bstrNewNode;
            SAFENODE    shNode;
            DWORD       dwTimeOut = 0;

            InArgs.GetProperty( &dwTimeOut, PVD_MTH_GROUP_PARM_TIMEOUT );
            InArgs.GetProperty( bstrNewNode, PVD_MTH_GROUP_PARM_NODENAME );
            shNode = OpenClusterNode( shCluster, bstrNewNode );
            er = HrWrapMoveClusterGroup( shCluster, shGroup, shNode, dwTimeOut );
        } // else if: MOVETONEWNODE
        else if ( ClRtlStrICmp( pwszMethodNameIn, PVD_MTH_GROUP_RENAME ) == 0 )
        {
            _bstr_t     bstrNewName;
            InArgs.GetProperty( bstrNewName, PVD_MTH_GROUP_PARM_NEWNAME );
            er = SetClusterGroupName( shGroup, bstrNewName );
        } // else if: RENAME
        else
        {
            er = static_cast< HRESULT >( WBEM_E_INVALID_PARAMETER );
        }
    } // else: not create new group
    
    return WBEM_S_NO_ERROR;

} //*** CClusterGroup::ExecuteMethod()

//////////////////////////////////////////////////////////////////////////////
//++
//
//  CClusterGroup::PutInstance
//
//  Description:
//      Save this instance.
//
//  Arguments:
//      rInstToPutIn    -- WMI object to be saved
//      lFlagIn         -- WMI flag
//      pCtxIn          -- WMI context
//      pHandlerIn      -- WMI sink pointer
//
//  Return Values:
//      WBEM_S_NO_ERROR
//
//--
//////////////////////////////////////////////////////////////////////////////
SCODE
CClusterGroup::PutInstance(
    CWbemClassObject &   rInstToPutIn,
    long                 lFlagIn,
    IWbemContext *       pCtxIn,
    IWbemObjectSink *    pHandlerIn
    )
{
    static SGetSetControl   s_rgControl[] =
    {
        { 
            CLUSCTL_GROUP_GET_COMMON_PROPERTIES,
            CLUSCTL_GROUP_SET_COMMON_PROPERTIES,
            FALSE
        },
        {
            CLUSCTL_GROUP_GET_PRIVATE_PROPERTIES,
            CLUSCTL_GROUP_SET_PRIVATE_PROPERTIES,
            TRUE
        }
    };
    static DWORD    s_cControl = sizeof( s_rgControl ) / sizeof( SGetSetControl );

    _bstr_t         bstrName;
    SAFECLUSTER     shCluster;
    SAFEGROUP       shGroup;
    CError          er;
    UINT    idx;

    rInstToPutIn.GetProperty( bstrName, PVD_PROP_NAME );

    shCluster = OpenCluster( NULL );
    shGroup = OpenClusterGroup( shCluster, bstrName );

    for ( idx = 0 ; idx < s_cControl ; idx++ )
    {
        CClusPropList   plOld;
        CClusPropList   plNew;
        
        er = plOld.ScGetGroupProperties(
                shGroup,
                s_rgControl[ idx ].dwGetControl,
                NULL,
                NULL,
                0
                );

        CClusterApi::SetObjectProperties(
            NULL,
            plNew,
            plOld,
            rInstToPutIn,
            s_rgControl[ idx ].fPrivate
            );

        if ( plNew.Cprops() > 0 )
        {
            er = ClusterGroupControl( 
                shGroup,
                NULL,
                s_rgControl[ idx ].dwSetControl,
                plNew.PbPropList(),
                static_cast< DWORD >( plNew.CbPropList() ),
                NULL,
                0,
                NULL
                );
        }

    } // for: each control code

    return WBEM_S_NO_ERROR;

} //*** CClusterGroup::PutInstance()

//////////////////////////////////////////////////////////////////////////////
//++
//
//  CClusterGroup::DeleteInstance
//
//  Description:
//      Delete the object specified in rObjPathIn.
//
//  Arguments:
//      rObjPathIn      -- ObjPath for the instance to be deleted
//      lFlagIn         -- WMI flag
//      pCtxIn          -- WMI context
//      pHandlerIn      -- WMI sink pointer
//
//  Return Values:
//      WBEM_S_NO_ERROR
//
//--
//////////////////////////////////////////////////////////////////////////////
SCODE
CClusterGroup::DeleteInstance(
    CObjPath &           rObjPathIn,
    long                 lFlagIn,
    IWbemContext *       pCtxIn,
    IWbemObjectSink *    pHandlerIn
    )
{
    SAFECLUSTER     shCluster;
    SAFEGROUP       shGroup;
    CError          er;

    shCluster = OpenCluster( NULL );
    shGroup = OpenClusterGroup(
                    shCluster,
                    rObjPathIn.GetStringValueForProperty( PVD_PROP_NAME )
                    );

    er = DeleteClusterGroup( shGroup );

    return WBEM_S_NO_ERROR;

} //*** CClusterGroup::DeleteInstance()

//****************************************************************************
//
//  CClusterGroupRes
//
//****************************************************************************

//////////////////////////////////////////////////////////////////////////////
//++
//
//  CClusterGroupRes::CClusterGroupRes
//
//  Description:
//      Constructor.
//
//  Arguments:
//      pwszNameIn      -- Class name
//      pNamespaceIn    -- Namespace
//      dwEnumTypeIn    -- Type id
//
//  Return Values:
//      None.
//
//--
//////////////////////////////////////////////////////////////////////////////
CClusterGroupRes::CClusterGroupRes(
    LPCWSTR         pwszNameIn,
    CWbemServices * pNamespaceIn,
    DWORD           dwEnumTypeIn
    )
    : CClusterObjAssoc( pwszNameIn, pNamespaceIn, dwEnumTypeIn )
{

} //*** CClusterGroupRes::CClusterGroupRes()

//////////////////////////////////////////////////////////////////////////////
//++
//
//  static
//  CClusterGroupRes::S_CreateThis
//
//  Description:
//      Create a cluster node object
//
//  Arguments:
//      pwszNameIn      -- Class name
//      pNamespaceIn    -- Namespace
//      dwEnumTypeIn    -- Type id
//
//  Return Values:
//      pointer to the CProvBase
//
//--
//////////////////////////////////////////////////////////////////////////////
CProvBase *
CClusterGroupRes::S_CreateThis(
    LPCWSTR         pwszNameIn,
    CWbemServices * pNamespaceIn,
    DWORD           dwEnumTypeIn
    )
{
    return new CClusterGroupRes(
                    pwszNameIn,
                    pNamespaceIn,
                    dwEnumTypeIn
                    );

} //*** CClusterGroupRes::S_CreateThis()


//////////////////////////////////////////////////////////////////////////////
//++
//
//  CClusterGroupRes::EnumInstance
//
//  Description:
//      Retrieve the property maping table of the cluster node active resource.
//
//  Arguments:
//      lFlagsIn        -- 
//      pCtxIn          -- 
//      pHandlerIn      -- 
//
//  Return Values:
//      SCODE
//
//--
//////////////////////////////////////////////////////////////////////////////
SCODE
CClusterGroupRes::EnumInstance(
    long                 lFlagsIn,
    IWbemContext *       pCtxIn,
    IWbemObjectSink *    pHandlerIn
    )
{
    SAFECLUSTER         shCluster;
    SAFEGROUP           shGroup;
    SAFERESOURCE        shResource;
    LPCWSTR             pwszName = NULL;
    DWORD               cchGroupName = MAX_PATH;
    CWstrBuf            wsbGroupName;
    DWORD               cch;
    CError              er;
    DWORD               dwError;
    CWbemClassObject    wcoGroup;
    CWbemClassObject    wcoPart;
    _bstr_t             bstrGroup;
    _bstr_t             bstrPart;
 
    wsbGroupName.SetSize( cchGroupName );
    shCluster = OpenCluster( NULL );
    CClusterEnum clusEnum( shCluster, m_dwEnumType );


    m_wcoGroup.SpawnInstance( 0, &wcoGroup );
    m_wcoPart.SpawnInstance( 0, &wcoPart );


    while ( ( pwszName = clusEnum.GetNext() ) != NULL )
    {
        CWbemClassObject    wco;
        DWORD               dwState;

        cch = cchGroupName;
        wcoPart.SetProperty( pwszName, PVD_PROP_RES_NAME );
        wcoPart.GetProperty( bstrPart, PVD_WBEM_RELPATH );

        shResource = OpenClusterResource( shCluster, pwszName );

        dwState = GetClusterResourceState(
                        shResource,
                        NULL,
                        0,
                        wsbGroupName,
                        &cch
                        );
        if ( dwState == ClusterResourceStateUnknown )
        {
            dwError = GetLastError();
            if ( dwError == ERROR_MORE_DATA )
            {
                cchGroupName = ++cch;
                wsbGroupName.SetSize( cch );
                dwState = GetClusterResourceState(
                                shResource,
                                NULL,
                                0,
                                wsbGroupName,
                                &cch
                                );
            } // if:  more data
            else
            {
                er = dwError;
            }
        } // if: state unknown

        wcoGroup.SetProperty( wsbGroupName, CLUSREG_NAME_GRP_NAME );
        wcoGroup.GetProperty( bstrGroup,    PVD_WBEM_RELPATH );

        m_pClass->SpawnInstance( 0, &wco );
        wco.SetProperty( (LPWSTR) bstrGroup, PVD_PROP_GROUPCOMPONENT );
        wco.SetProperty( (LPWSTR) bstrPart,  PVD_PROP_PARTCOMPONENT );
        pHandlerIn->Indicate( 1, &wco );

    } // while: more resources

    return WBEM_S_NO_ERROR;

} //*** CClusterGroupRes::EnumInstance()
