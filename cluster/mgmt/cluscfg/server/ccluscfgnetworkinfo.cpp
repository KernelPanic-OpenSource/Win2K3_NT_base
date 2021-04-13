//////////////////////////////////////////////////////////////////////////////
//
//  Copyright (c) 2000-2001 Microsoft Corporation
//
//  Module Name:
//      CClusCfgNetworkInfo.cpp
//
//  Description:
//      This file contains the definition of the CClusCfgNetworkInfo
//       class.
//
//      The class CClusCfgNetworkInfo represents a cluster manageable
//      network. It implements the IClusCfgNetworkInfo interface.
//
//  Maintained By:
//      Galen Barbee (GalenB) 23-FEB-2000
//
//////////////////////////////////////////////////////////////////////////////


//////////////////////////////////////////////////////////////////////////////
// Include Files
//////////////////////////////////////////////////////////////////////////////
#include "Pch.h"
#include <PropList.h>
#include <ClusRtl.h>
#include "CClusCfgNetworkInfo.h"
#include "CEnumClusCfgIPAddresses.h"


//////////////////////////////////////////////////////////////////////////////
// Constant Definitions
//////////////////////////////////////////////////////////////////////////////

DEFINE_THISCLASS( "CClusCfgNetworkInfo" );


//*************************************************************************//


/////////////////////////////////////////////////////////////////////////////
// CClusCfgNetworkInfo class
/////////////////////////////////////////////////////////////////////////////


//////////////////////////////////////////////////////////////////////////////
//++
//
//  CClusCfgNetworkInfo::S_HrCreateInstance
//
//  Description:
//      Create a CClusCfgNetworkInfo instance.
//
//  Arguments:
//      None.
//
//  Return Values:
//      Pointer to CClusCfgNetworkInfo instance.
//
//--
//////////////////////////////////////////////////////////////////////////////
HRESULT
CClusCfgNetworkInfo::S_HrCreateInstance(
    IUnknown ** ppunkOut
    )
{
    TraceFunc( "" );

    HRESULT                 hr = S_OK;
    CClusCfgNetworkInfo *   pccni = NULL;

    if ( ppunkOut == NULL )
    {
        hr = THR( E_POINTER );
        goto Cleanup;
    } // if:

    pccni = new CClusCfgNetworkInfo();
    if ( pccni == NULL )
    {
        hr = THR( E_OUTOFMEMORY );
        goto Cleanup;
    } // if: error allocating object

    hr = THR( pccni->HrInit() );
    if ( FAILED( hr ) )
    {
        goto Cleanup;
    } // if: HrInit() failed

    hr = THR( pccni->TypeSafeQI( IUnknown, ppunkOut ) );
    if ( FAILED( hr ) )
    {
        goto Cleanup;
    } // if: QI failed

Cleanup:

    if ( FAILED( hr ) )
    {
        LogMsg( L"[SRV] CClusCfgNetworkInfo::S_HrCreateInstance() failed. (hr = %#08x)", hr );
    } // if:

    if ( pccni != NULL )
    {
        pccni->Release();
    } // if:

    HRETURN( hr );

} //*** CClusCfgNetworkInfo::S_HrCreateInstance


//////////////////////////////////////////////////////////////////////////////
//++
//
//  CClusCfgNetworkInfo::S_HrCreateInstance
//
//  Description:
//      Create a CClusCfgNetworkInfo instance.
//
//  Arguments:
//
//
//  Return Values:
//      Pointer to CClusCfgNetworkInfo instance.
//
//--
//////////////////////////////////////////////////////////////////////////////
HRESULT
CClusCfgNetworkInfo::S_HrCreateInstance(
      HNETWORK          hNetworkIn
    , HNETINTERFACE     hNetInterfaceIn
    , IUnknown *        punkCallbackIn
    , LCID              lcidIn
    , IWbemServices *   pIWbemServicesIn
    , IUnknown **       ppunkOut
    )
{
    TraceFunc( "" );
    Assert( hNetworkIn != NULL );
    Assert( ppunkOut != NULL );

    HRESULT                 hr = S_OK;
    CClusCfgNetworkInfo *   pccni = NULL;

    if ( ppunkOut == NULL )
    {
        hr = THR( E_POINTER );
        goto Cleanup;
    } // if:

    LogMsg( L"[SRV] Creating NetworkInfo object from a cluster network." );

    pccni = new CClusCfgNetworkInfo();
    if ( pccni == NULL )
    {
        hr = THR( E_OUTOFMEMORY );
        goto Cleanup;
    } // if: error allocating object

    hr = THR( pccni->Initialize( punkCallbackIn, lcidIn ) );
    if ( FAILED( hr ) )
    {
        goto Cleanup;
    } // if:

    hr = THR( pccni->SetWbemServices( pIWbemServicesIn ) );
    if ( FAILED( hr ) )
    {
        goto Cleanup;
    } // if:

    hr = THR( pccni->HrInit( hNetworkIn, hNetInterfaceIn ) );
    if ( FAILED( hr ) )
    {
        goto Cleanup;
    } // if: HrInit() failed

    hr = THR( pccni->TypeSafeQI( IUnknown, ppunkOut ) );
    if ( FAILED( hr ) )
    {
        goto Cleanup;
    } // if: QI succeeded

Cleanup:

    if ( FAILED( hr ) )
    {
        LogMsg( L"[SRV] CClusCfgNetworkInfo::S_HrCreateInstance( HRESOURCE ) failed. (hr = %#08x)", hr );
    } // if:

    if ( pccni != NULL )
    {
        pccni->Release();
    } // if:

    HRETURN( hr );

} //*** CClusCfgNetworkInfo::S_HrCreateInstance


//////////////////////////////////////////////////////////////////////////////
//++
//
//  CClusCfgNetworkInfo::CClusCfgNetworkInfo
//
//  Description:
//      Constructor of the CClusCfgNetworkInfo class. This initializes
//      the m_cRef variable to 1 instead of 0 to account of possible
//      QueryInterface failure in DllGetClassObject.
//
//  Arguments:
//      None.
//
//  Return Value:
//      None.
//
//  Remarks:
//      None.
//
//--
//////////////////////////////////////////////////////////////////////////////
CClusCfgNetworkInfo::CClusCfgNetworkInfo( void )
    : m_cRef( 1 )
{
    TraceFunc( "" );

    // Increment the count of components in memory so the DLL hosting this
    // object cannot be unloaded.
    InterlockedIncrement( &g_cObjects );

    Assert( m_pIWbemServices == NULL );
    Assert( m_dwFlags  == 0 );
    Assert( m_bstrName == NULL );
    Assert( m_bstrDescription == NULL );
    Assert( m_bstrDeviceID == NULL );
    Assert( m_punkAddresses == NULL );
    Assert( m_bstrConnectionName == NULL );
    Assert( m_picccCallback == NULL );
    Assert( !m_fIsClusterNetwork );

    TraceFuncExit();

} //*** CClusCfgNetworkInfo::CClusCfgNetworkInfo


//////////////////////////////////////////////////////////////////////////////
//++
//
//  CClusCfgNetworkInfo::~CClusCfgNetworkInfo
//
//  Description:
//      Desstructor of the CClusCfgNetworkInfo class.
//
//  Arguments:
//      None.
//
//  Return Value:
//      None.
//
//  Remarks:
//      None.
//
//--
//////////////////////////////////////////////////////////////////////////////
CClusCfgNetworkInfo::~CClusCfgNetworkInfo( void )
{
    TraceFunc( "" );

    if ( m_pIWbemServices != NULL )
    {
        m_pIWbemServices->Release();
    } // if:

    if ( m_punkAddresses != NULL )
    {
        m_punkAddresses->Release();
    } // if:

    if ( m_picccCallback != NULL )
    {
        m_picccCallback->Release();
    } // if:

    TraceSysFreeString( m_bstrName );
    TraceSysFreeString( m_bstrDescription );
    TraceSysFreeString( m_bstrDeviceID );
    TraceSysFreeString( m_bstrConnectionName );

    // There's going to be one less component in memory. Decrement component count.
    InterlockedDecrement( &g_cObjects );

    TraceFuncExit();

} //*** CClusCfgNetworkInfo::~CClusCfgNetworkInfo


//*************************************************************************//


/////////////////////////////////////////////////////////////////////////////
// CClusCfgNetworkInfo -- IUknkown interface.
/////////////////////////////////////////////////////////////////////////////


//////////////////////////////////////////////////////////////////////////////
//++
//
//  CClusCfgNetworkInfo::AddRef
//
//  Description:
//      Increment the reference count of this object by one.
//
//  Arguments:
//      None.
//
//  Return Value:
//      The new reference count.
//
//  Remarks:
//      None.
//
//--
//////////////////////////////////////////////////////////////////////////////
STDMETHODIMP_( ULONG )
CClusCfgNetworkInfo::AddRef( void )
{
    TraceFunc( "[IUnknown]" );

    InterlockedIncrement( & m_cRef );

    CRETURN( m_cRef );

} //*** CClusCfgNetworkInfo::AddRef


//////////////////////////////////////////////////////////////////////////////
//++
//
//  CClusCfgNetworkInfo::Release
//
//  Description:
//      Decrement the reference count of this object by one.
//
//  Arguments:
//      None.
//
//  Return Value:
//      The new reference count.
//
//  Remarks:
//      None.
//
//--
//////////////////////////////////////////////////////////////////////////////
STDMETHODIMP_( ULONG )
CClusCfgNetworkInfo::Release( void )
{
    TraceFunc( "[IUnknown]" );

    LONG    cRef;

    cRef = InterlockedDecrement( &m_cRef );
    if ( cRef == 0 )
    {
        TraceDo( delete this );
    } // if: reference count equal to zero

    CRETURN( cRef );

} //*** CClusCfgNetworkInfo::Release


//////////////////////////////////////////////////////////////////////////////
//++
//
//  CClusCfgNetworkInfo::QueryInterface
//
//  Description:
//      Query this object for the passed in interface.
//
//  Arguments:
//      riidIn
//          Id of interface requested.
//
//      ppvOut
//          Pointer to the requested interface.
//
//  Return Value:
//      S_OK
//          If the interface is available on this object.
//
//      E_NOINTERFACE
//          If the interface is not available.
//
//      E_POINTER
//          ppvOut was NULL.
//
//  Remarks:
//      None.
//
//--
//////////////////////////////////////////////////////////////////////////////
STDMETHODIMP
CClusCfgNetworkInfo::QueryInterface(
      REFIID    riidIn
    , void **   ppvOut
    )
{
    TraceQIFunc( riidIn, ppvOut );

    HRESULT hr = S_OK;

    //
    // Validate arguments.
    //

    Assert( ppvOut != NULL );
    if ( ppvOut == NULL )
    {
        hr = THR( E_POINTER );
        goto Cleanup;
    }

    //
    // Handle known interfaces.
    //

    if ( IsEqualIID( riidIn, IID_IUnknown ) )
    {
         *ppvOut = static_cast< IClusCfgNetworkInfo * >( this );
    } // if: IUnknown
    else if ( IsEqualIID( riidIn, IID_IClusCfgNetworkInfo ) )
    {
        *ppvOut = TraceInterface( __THISCLASS__, IClusCfgNetworkInfo, this, 0 );
    } // else if: IClusCfgNetworkInfo
    else if ( IsEqualIID( riidIn, IID_IClusCfgWbemServices ) )
    {
        *ppvOut = TraceInterface( __THISCLASS__, IClusCfgWbemServices, this, 0 );
    } // else if: IClusCfgWbemServices
    else if ( IsEqualIID( riidIn, IID_IClusCfgSetWbemObject ) )
    {
        *ppvOut = TraceInterface( __THISCLASS__, IClusCfgSetWbemObject, this, 0 );
    } // else if: IClusCfgSetWbemObject
    else if ( IsEqualIID( riidIn, IID_IEnumClusCfgIPAddresses ) )
    {
        *ppvOut = TraceInterface( __THISCLASS__, IEnumClusCfgIPAddresses, this, 0 );
    } // else if: IEnumClusCfgIPAddresses
    else if ( IsEqualIID( riidIn, IID_IClusCfgInitialize ) )
    {
        *ppvOut = TraceInterface( __THISCLASS__, IClusCfgInitialize, this, 0 );
    } // else if: IClusCfgInitialize
    else if ( IsEqualIID( riidIn, IID_IClusCfgClusterNetworkInfo ) )
    {
        *ppvOut = TraceInterface( __THISCLASS__, IClusCfgClusterNetworkInfo, this, 0 );
    } // else if: IClusCfgClusterNetworkInfo
    else
    {
        *ppvOut = NULL;
        hr = E_NOINTERFACE;
    }

    //
    // Add a reference to the interface if successful.
    //

    if ( SUCCEEDED( hr ) )
    {
        ((IUnknown *) *ppvOut)->AddRef();
    } // if: success

Cleanup:

     QIRETURN_IGNORESTDMARSHALLING( hr, riidIn );

} //*** CClusCfgNetworkInfo::QueryInterface


//*************************************************************************//


/////////////////////////////////////////////////////////////////////////////
// CPhysicalDisk -- IClusCfgWbemServices interface.
/////////////////////////////////////////////////////////////////////////////


//////////////////////////////////////////////////////////////////////////////
//++
//
//  CClusCfgNetworkInfo::SetWbemServices
//
//  Description:
//      Set the WBEM services provider.
//
//  Arguments:
//    IN  IWbemServices  pIWbemServicesIn
//
//  Return Value:
//      S_OK
//          Success
//
//      E_POINTER
//          The pIWbemServicesIn param is NULL.
//
//  Remarks:
//      None.
//
//--
//////////////////////////////////////////////////////////////////////////////
STDMETHODIMP
CClusCfgNetworkInfo::SetWbemServices( IWbemServices * pIWbemServicesIn )
{
    TraceFunc( "[IClusCfgWbemServices]" );

    HRESULT hr = S_OK;

    if ( pIWbemServicesIn == NULL )
    {
        hr = THR( E_POINTER );
        STATUS_REPORT_REF( TASKID_Major_Find_Devices, TASKID_Minor_SetWbemServices_Network, IDS_ERROR_NULL_POINTER, IDS_ERROR_NULL_POINTER_REF, hr );
        goto Cleanup;
    } // if:

    m_pIWbemServices = pIWbemServicesIn;
    m_pIWbemServices->AddRef();

Cleanup:

    HRETURN( hr );

} //*** CClusCfgNetworkInfo::SetWbemServices


//*************************************************************************//


/////////////////////////////////////////////////////////////////////////////
// CClusCfgNetworkInfo -- IClusCfgInitialize interface.
/////////////////////////////////////////////////////////////////////////////


//////////////////////////////////////////////////////////////////////////////
//++
//
//  CClusCfgNetworkInfo::Initialize
//
//  Description:
//      Initialize this component.
//
//  Arguments:
//    IN  IUknown * punkCallbackIn
//
//    IN  LCID      lcidIn
//
//  Return Value:
//      S_OK
//          Success
//
//  Remarks:
//      None.
//
//--
//////////////////////////////////////////////////////////////////////////////
STDMETHODIMP
CClusCfgNetworkInfo::Initialize(
    IUnknown *  punkCallbackIn,
    LCID        lcidIn
    )
{
    TraceFunc( "[IClusCfgInitialize]" );
    Assert( m_picccCallback == NULL );

    HRESULT hr = S_OK;

    m_lcid = lcidIn;

    if ( punkCallbackIn == NULL )
    {
        hr = THR( E_POINTER );
        goto Cleanup;
    } // if:

    hr = THR( punkCallbackIn->TypeSafeQI( IClusCfgCallback, &m_picccCallback ) );

Cleanup:

    HRETURN( hr );

} //*** CClusCfgNetworkInfo::Initialize


//*************************************************************************//


/////////////////////////////////////////////////////////////////////////////
// CClusCfgNetworkInfo -- IClusCfgSetWbemObject interface.
/////////////////////////////////////////////////////////////////////////////


//////////////////////////////////////////////////////////////////////////////
//++
//
//  CClusCfgNetworkInfo::SetWbemObject
//
//  Description:
//
//  Arguments:
//
//  Return Value:
//
//  Remarks:
//      None.
//
//--
//////////////////////////////////////////////////////////////////////////////
STDMETHODIMP
CClusCfgNetworkInfo::SetWbemObject(
      IWbemClassObject *    pNetworkAdapterIn
    , bool *                pfRetainObjectOut
    )
{
    TraceFunc( "[IClusCfgSetWbemObject]" );
    Assert( pNetworkAdapterIn != NULL );
    Assert( pfRetainObjectOut != NULL );

    HRESULT hr = S_OK;
    VARIANT var;

    VariantInit( &var );

    hr = THR( HrGetWMIProperty( pNetworkAdapterIn, L"Description", VT_BSTR, &var ) );
    if ( FAILED( hr ) )
    {
        goto Cleanup;
    } // if:

    m_bstrDescription = TraceSysAllocString( var.bstrVal );
    if ( m_bstrDescription == NULL )
    {
        goto OutOfMemory;
    } // if:

    VariantClear( &var );

    hr = THR( HrGetWMIProperty( pNetworkAdapterIn, L"Name", VT_BSTR, &var ) );
    if ( FAILED( hr ) )
    {
        goto Cleanup;
    } // if:

    m_bstrName = TraceSysAllocString( var.bstrVal );
    if ( m_bstrName == NULL )
    {
        goto OutOfMemory;
    } // if:

    VariantClear( &var );

    hr = THR( HrGetWMIProperty( pNetworkAdapterIn, L"DeviceID", VT_BSTR, &var ) );
    if ( FAILED( hr ) )
    {
        goto Cleanup;
    } // if:

    m_bstrDeviceID = TraceSysAllocString( var.bstrVal );
    if ( m_bstrDeviceID == NULL )
    {
        goto OutOfMemory;
    } // if:

    VariantClear( &var );

    hr = THR( HrGetWMIProperty( pNetworkAdapterIn, L"NetConnectionID", VT_BSTR, &var ) );
    if ( FAILED( hr ) )
    {
        goto Cleanup;
    } // if:

    m_bstrConnectionName = TraceSysAllocString( var.bstrVal );
    if ( m_bstrConnectionName == NULL )
    {
        goto OutOfMemory;
    } // if:

    hr = STHR( HrLoadEnum( pNetworkAdapterIn, pfRetainObjectOut ) );
    if ( FAILED( hr ) )
    {
        goto Cleanup;
    } // if:

    goto Cleanup;

OutOfMemory:

    hr = THR( E_OUTOFMEMORY );
    STATUS_REPORT_REF( TASKID_Major_Find_Devices, TASKID_Minor_SetWbemObject_Network, IDS_ERROR_OUTOFMEMORY, IDS_ERROR_OUTOFMEMORY_REF, hr );

Cleanup:

    LOG_STATUS_REPORT_STRING( L"Created network adapter '%1!ws!'", ( m_bstrConnectionName != NULL ) ? m_bstrConnectionName : L"<Unknown>", hr );

    VariantClear( &var );

    HRETURN( hr );

} //*** CClusCfgNetworkInfo::SetWbemObject


//*************************************************************************//


/////////////////////////////////////////////////////////////////////////////
// CClusCfgNetworkInfo -- IClusCfgNetworkInfo interface.
/////////////////////////////////////////////////////////////////////////////


//////////////////////////////////////////////////////////////////////////////
//++
//
//  CClusCfgNetworkInfo::GetUID
//
//  Description:
//
//  Arguments:
//
//  Return Value:
//
//  Remarks:
//      None.
//
//--
//////////////////////////////////////////////////////////////////////////////
STDMETHODIMP
CClusCfgNetworkInfo::GetUID(
    BSTR * pbstrUIDOut
    )
{
    TraceFunc( "[IClusCfgNetworkInfo]" );
    Assert( m_punkAddresses != NULL );

    HRESULT                 hr;
    IClusCfgIPAddressInfo * piccipi = NULL;

    if ( pbstrUIDOut == NULL )
    {
        hr = THR( E_POINTER );
        STATUS_REPORT_REF( TASKID_Major_Find_Devices, TASKID_Minor_GetUID, IDS_ERROR_NULL_POINTER, IDS_ERROR_NULL_POINTER_REF, hr );
        goto Cleanup;
    } // if:

    hr = STHR( GetPrimaryNetworkAddress( &piccipi ) );
    if ( FAILED( hr ) )
    {
        goto Cleanup;
    } // if:

    //
    // When there is not a primary network address, we goto cleanup
    //
    if ( ( piccipi == NULL ) && ( hr == S_FALSE ) )
    {
        goto Cleanup;
    } // if:

    hr = THR( piccipi->GetUID( pbstrUIDOut ) );

Cleanup:

    if ( piccipi != NULL )
    {
        piccipi->Release();
    } // if:

    HRETURN( hr );

} //*** CClusCfgNetworkInfo::GetUID


//////////////////////////////////////////////////////////////////////////////
//++
//
//  CClusCfgNetworkInfo::GetName
//
//  Description:
//
//  Arguments:
//
//  Return Value:
//
//  Remarks:
//      None.
//
//--
//////////////////////////////////////////////////////////////////////////////
STDMETHODIMP
CClusCfgNetworkInfo::GetName( BSTR * pbstrNameOut )
{
    TraceFunc( "[IClusCfgNetworkInfo]" );
    Assert( m_bstrConnectionName != NULL );

    HRESULT hr = S_OK;

    if ( pbstrNameOut == NULL )
    {
        hr = THR( E_POINTER );
        STATUS_REPORT_REF( TASKID_Major_Find_Devices, TASKID_Minor_NetworkInfo_GetName_Pointer, IDS_ERROR_NULL_POINTER, IDS_ERROR_NULL_POINTER_REF, hr );
        goto Cleanup;
    } // if:

    *pbstrNameOut = SysAllocString( m_bstrConnectionName );
    if ( *pbstrNameOut == NULL )
    {
        hr = THR( E_OUTOFMEMORY );
        STATUS_REPORT_REF( TASKID_Major_Find_Devices, TASKID_Minor_GetName_Memory, IDS_ERROR_OUTOFMEMORY, IDS_ERROR_OUTOFMEMORY_REF, hr );
    } // if:

Cleanup:

    HRETURN( hr );

} //*** CClusCfgNetworkInfo::GetName


//////////////////////////////////////////////////////////////////////////////
//++
//
//  CClusCfgNetworkInfo::SetName
//
//  Description:
//
//  Arguments:
//
//  Return Value:
//
//  Remarks:
//      None.
//
//--
//////////////////////////////////////////////////////////////////////////////
STDMETHODIMP
CClusCfgNetworkInfo::SetName( LPCWSTR pcszNameIn )
{
    TraceFunc1( "[IClusCfgNetworkInfo] pcszNameIn = '%ls'", pcszNameIn == NULL ? L"<null>" : pcszNameIn );

    HRESULT hr = S_FALSE;

    HRETURN( hr );

} //*** CClusCfgNetworkInfo::SetName


//////////////////////////////////////////////////////////////////////////////
//++
//
//  CClusCfgNetworkInfo::GetDescription
//
//  Description:
//
//  Arguments:
//
//  Return Value:
//
//  Remarks:
//      None.
//
//--
//////////////////////////////////////////////////////////////////////////////
STDMETHODIMP
CClusCfgNetworkInfo::GetDescription( BSTR * pbstrDescriptionOut )
{
    TraceFunc( "[IClusCfgNetworkInfo]" );
    Assert( m_bstrDescription != NULL );

    HRESULT hr = S_OK;

    if ( pbstrDescriptionOut == NULL )
    {
        hr = THR( E_POINTER );
        STATUS_REPORT_REF( TASKID_Major_Find_Devices, TASKID_Minor_NetworkInfo_GetDescription_Pointer, IDS_ERROR_NULL_POINTER, IDS_ERROR_NULL_POINTER_REF, hr );
        goto Cleanup;
    } // if:

    *pbstrDescriptionOut = SysAllocString( m_bstrDescription );
    if ( *pbstrDescriptionOut == NULL )
    {
        hr = THR( E_OUTOFMEMORY );
        STATUS_REPORT_REF( TASKID_Major_Find_Devices, TASKID_Minor_NetworkInfo_GetDescription_Memory, IDS_ERROR_OUTOFMEMORY, IDS_ERROR_OUTOFMEMORY_REF, hr );
    } // if:

Cleanup:

    HRETURN( hr );

} //*** CClusCfgNetworkInfo::GetDescription


//////////////////////////////////////////////////////////////////////////////
//++
//
//  CClusCfgNetworkInfo::SetDescription
//
//  Description:
//
//  Arguments:
//
//  Return Value:
//
//  Remarks:
//      None.
//
//--
//////////////////////////////////////////////////////////////////////////////
STDMETHODIMP
CClusCfgNetworkInfo::SetDescription( LPCWSTR pcszDescriptionIn )
{
    TraceFunc1( "[IClusCfgNetworkInfo] pcszDescriptionIn = '%ls'", pcszDescriptionIn == NULL ? L"<null>" : pcszDescriptionIn );

    HRESULT hr = S_FALSE;

    HRETURN( hr );

} //*** CClusCfgNetworkInfo::SetDescription


//////////////////////////////////////////////////////////////////////////////
//++
//
//  CClusCfgNetworkInfo::GetPrimaryNetworkAddress
//
//  Description:
//
//  Arguments:
//
//  Return Value:
//
//  Remarks:
//      None.
//
//--
//////////////////////////////////////////////////////////////////////////////
STDMETHODIMP
CClusCfgNetworkInfo::GetPrimaryNetworkAddress(
    IClusCfgIPAddressInfo ** ppIPAddressOut
    )
{
    TraceFunc( "[IClusCfgNetworkInfo]" );
    Assert( m_punkAddresses != NULL );

    HRESULT                     hr;
    ULONG                       cFetched;

    hr = STHR( HrGetPrimaryNetworkAddress( ppIPAddressOut, &cFetched ) );
    if ( FAILED( hr ) )
    {
        goto Cleanup;
    } // if:

    if ( ( hr == S_FALSE ) && ( cFetched == 0 ) )
    {
        STATUS_REPORT_STRING_REF(
              TASKID_Major_Find_Devices
            , TASKID_Minor_Primary_IP_Address
            , IDS_ERROR_PRIMARY_IP_NOT_FOUND
            , m_bstrConnectionName
            , IDS_ERROR_PRIMARY_IP_NOT_FOUND_REF
            , hr
            );
    } // if:

Cleanup:

    HRETURN( hr );

} //*** CClusCfgNetworkInfo::GetPrimaryNetworkAddress


//////////////////////////////////////////////////////////////////////////////
//++
//
//  CClusCfgNetworkInfo::SetPrimaryNetworkAddress
//
//  Description:
//
//  Arguments:
//
//  Return Value:
//
//  Remarks:
//      None.
//
//--
//////////////////////////////////////////////////////////////////////////////
STDMETHODIMP
CClusCfgNetworkInfo::SetPrimaryNetworkAddress(
    IClusCfgIPAddressInfo * pIPAddressIn
    )
{
    TraceFunc( "[IClusCfgNetworkInfo]" );

    HRESULT hr = THR( E_NOTIMPL );

    HRETURN( hr );

} //*** CClusCfgNetworkInfo::SetPrimaryNetworkAddress


//////////////////////////////////////////////////////////////////////////////
//++
//
//  CClusCfgNetworkInfo::IsPublic
//
//  Description:
//
//  Arguments:
//
//  Return Value:
//      S_OK
//          The network is public -- client traffic only.
//
//      S_FALSE
//          The network is not public -- has cluster and client traffic.
//
//      Win32 error as HRESULT when an error occurs.
//
//  Remarks:
//      None.
//
//--
//////////////////////////////////////////////////////////////////////////////
STDMETHODIMP
CClusCfgNetworkInfo::IsPublic( void )
{
    TraceFunc( "[IClusCfgNetworkInfo]" );

    HRESULT hr = S_FALSE;

    if ( m_dwFlags & eIsPublic )
    {
        hr = S_OK;
    } // if:

    HRETURN( hr );

} //*** CClusCfgNetworkInfo::IsPublic


//////////////////////////////////////////////////////////////////////////////
//++
//
//  CClusCfgNetworkInfo::SetPublic
//
//  Description:
//
//  Arguments:
//
//  Return Value:
//
//  Remarks:
//      None.
//
//--
//////////////////////////////////////////////////////////////////////////////
STDMETHODIMP
CClusCfgNetworkInfo::SetPublic( BOOL fIsPublicIn )
{
    TraceFunc( "[IClusCfgNetworkInfo]" );

    HRESULT hr = S_OK;

    if ( fIsPublicIn )
    {
        m_dwFlags |= eIsPublic;
    } // if:
    else
    {
        m_dwFlags &= ~eIsPublic;
    } // else:

    HRETURN( hr );

} //*** CClusCfgNetworkInfo::SetPublic


//////////////////////////////////////////////////////////////////////////////
//++
//
//  CClusCfgNetworkInfo::IsPrivate
//
//  Description:
//
//  Arguments:
//
//  Return Value:
//      S_OK
//          The network is private -- no client traffic.
//
//      S_FALSE
//          The network is not private -- has client traffic.
//
//      Win32 error as HRESULT when an error occurs.
//
//  Remarks:
//      None.
//
//--
//////////////////////////////////////////////////////////////////////////////
STDMETHODIMP
CClusCfgNetworkInfo::IsPrivate( void )
{
    TraceFunc( "[IClusCfgNetworkInfo]" );

    HRESULT hr = S_FALSE;

    if ( m_dwFlags & eIsPrivate )
    {
        hr = S_OK;
    } // if:

    HRETURN( hr );

} //*** CClusCfgNetworkInfo::IsPrivate


//////////////////////////////////////////////////////////////////////////////
//++
//
//  CClusCfgNetworkInfo::SetPrivate
//
//  Description:
//
//  Arguments:
//
//  Return Value:
//
//  Remarks:
//      None.
//
//--
//////////////////////////////////////////////////////////////////////////////
STDMETHODIMP
CClusCfgNetworkInfo::SetPrivate( BOOL fIsPrivateIn )
{
    TraceFunc( "[IClusCfgNetworkInfo]" );

    HRESULT hr = S_OK;

    if ( fIsPrivateIn )
    {
        m_dwFlags |= eIsPrivate;
    } // if:
    else
    {
        m_dwFlags &= ~eIsPrivate;
    } // else:

    HRETURN( hr );

} //*** CClusCfgNetworkInfo::SetPrivate


//*************************************************************************//


/////////////////////////////////////////////////////////////////////////////
// CClusCfgNetworkInfo -- IEnumClusCfgIPAddresses interface.
/////////////////////////////////////////////////////////////////////////////


//////////////////////////////////////////////////////////////////////////////
//++
//
//  CClusCfgNetworkInfo::Next
//
//  Description:
//
//  Arguments:
//
//  Return Value:
//
//  Remarks:
//      None.
//
//--
//////////////////////////////////////////////////////////////////////////////
STDMETHODIMP
CClusCfgNetworkInfo::Next(
    ULONG                       cNumberRequestedIn,
    IClusCfgIPAddressInfo **   rgpIPAddresseInfoOut,
    ULONG *                     pcNumberFetchedOut
    )
{
    TraceFunc( "[IEnumClusCfgIPAddresses]" );
    Assert( m_punkAddresses != NULL );

    HRESULT                     hr;
    IEnumClusCfgIPAddresses *   pccipa;

    hr = THR( m_punkAddresses->TypeSafeQI( IEnumClusCfgIPAddresses, &pccipa ) );
    if ( SUCCEEDED( hr ) )
    {
        hr = STHR( pccipa->Next( cNumberRequestedIn, rgpIPAddresseInfoOut, pcNumberFetchedOut ) );
        pccipa->Release();
    } // if:

    HRETURN( hr );

} //*** CClusCfgNetworkInfo::Next


//////////////////////////////////////////////////////////////////////////////
//++
//
//  CClusCfgNetworkInfo::Skip
//
//  Description:
//
//  Arguments:
//
//  Return Value:
//
//  Remarks:
//      None.
//
//--
//////////////////////////////////////////////////////////////////////////////
STDMETHODIMP
CClusCfgNetworkInfo::Skip( ULONG cNumberToSkipIn )
{
    TraceFunc( "[IEnumClusCfgIPAddresses]" );
    Assert( m_punkAddresses != NULL );

    HRESULT                     hr;
    IEnumClusCfgIPAddresses *   pccipa;

    hr = THR( m_punkAddresses->TypeSafeQI( IEnumClusCfgIPAddresses, &pccipa ) );
    if ( SUCCEEDED( hr ) )
    {
        hr = THR( pccipa->Skip( cNumberToSkipIn ) );
        pccipa->Release();
    } // if:

    HRETURN( hr );

} //*** CClusCfgNetworkInfo::Skip


//////////////////////////////////////////////////////////////////////////////
//++
//
//  CClusCfgNetworkInfo::Reset
//
//  Description:
//
//  Arguments:
//
//  Return Value:
//
//  Remarks:
//      None.
//
//--
//////////////////////////////////////////////////////////////////////////////
STDMETHODIMP
CClusCfgNetworkInfo::Reset( void )
{
    TraceFunc( "[IEnumClusCfgIPAddresses]" );
    Assert( m_punkAddresses != NULL );

    HRESULT                     hr;
    IEnumClusCfgIPAddresses *   pccipa;

    hr = THR( m_punkAddresses->TypeSafeQI( IEnumClusCfgIPAddresses, &pccipa ) );
    if ( SUCCEEDED( hr ) )
    {
        hr = THR( pccipa->Reset() );
        pccipa->Release();
    } // if:

    HRETURN( hr );

} //*** CClusCfgNetworkInfo::Reset


//////////////////////////////////////////////////////////////////////////////
//++
//
//  CClusCfgNetworkInfo::Clone
//
//  Description:
//
//  Arguments:
//
//  Return Value:
//
//  Remarks:
//      None.
//
//--
//////////////////////////////////////////////////////////////////////////////
STDMETHODIMP
CClusCfgNetworkInfo::Clone(
    IEnumClusCfgIPAddresses ** ppEnumClusCfgIPAddressesOut
    )
{
    TraceFunc( "[IEnumClusCfgIPAddresses]" );
    Assert( m_punkAddresses != NULL );

    HRESULT                     hr;
    IEnumClusCfgIPAddresses *   pccipa;

    hr = THR( m_punkAddresses->TypeSafeQI( IEnumClusCfgIPAddresses, &pccipa ) );
    if ( SUCCEEDED( hr ) )
    {
        hr = THR( pccipa->Clone( ppEnumClusCfgIPAddressesOut ) );
        pccipa->Release();
    } // if:

    HRETURN( hr );

} //*** CClusCfgNetworkInfo::Clone


//////////////////////////////////////////////////////////////////////////////
//++
//
//  CClusCfgNetworkInfo::Count
//
//  Description:
//
//  Arguments:
//
//  Return Value:
//
//  Remarks:
//      None.
//
//--
//////////////////////////////////////////////////////////////////////////////
STDMETHODIMP
CClusCfgNetworkInfo::Count( DWORD * pnCountOut )
{
    TraceFunc( "[IEnumClusCfgIPAddresses]" );
    Assert( m_punkAddresses != NULL );

    HRESULT                     hr = S_OK;
    IEnumClusCfgIPAddresses *   pccipa = NULL;

    if ( pnCountOut == NULL )
    {
        hr = THR( E_POINTER );
        goto Cleanup;
    } // if:

    hr = THR( m_punkAddresses->TypeSafeQI( IEnumClusCfgIPAddresses, &pccipa ) );
    if ( FAILED( hr ) )
    {
        goto Cleanup;
    } // if:

    hr = THR( pccipa->Count( pnCountOut ) );

Cleanup:

    if ( pccipa != NULL )
    {
       pccipa->Release();
    }

    HRETURN( hr );

} //** CClusCfgNetworkInfo::Count


//*************************************************************************//


/////////////////////////////////////////////////////////////////////////////
// CClusCfgNetworkInfo class -- IClusCfgClusterNetworkInfo interface.
/////////////////////////////////////////////////////////////////////////////


//////////////////////////////////////////////////////////////////////////////
//++
//
//  CClusCfgNetworkInfo::HrIsClusterNetwork
//
//  Description:
//      Is this network already a cluster network.
//
//  Arguments:
//      None.
//
//  Return Value:
//      S_OK
//          This network is already a cluster network.
//
//      S_FALSE
//          This network is not already a cluster network.
//
//  Remarks:
//      None.
//
//--
//////////////////////////////////////////////////////////////////////////////
HRESULT
CClusCfgNetworkInfo::HrIsClusterNetwork( void )
{
    TraceFunc( "" );

    HRESULT hr = S_FALSE;

    if ( m_fIsClusterNetwork )
    {
        hr = S_OK;
    } // if:

    HRETURN( hr );

} //*** CClusCfgNetworkInfo::HrIsClusterNetwork


//////////////////////////////////////////////////////////////////////////////
//++
//
//  CClusCfgNetworkInfo::HrGetNetUID
//
//  Description:
//
//  Arguments:
//
//  Return Value:
//
//  Remarks:
//      None.
//
//--
//////////////////////////////////////////////////////////////////////////////
HRESULT
CClusCfgNetworkInfo::HrGetNetUID(
      BSTR *        pbstrUIDOut
    , const CLSID * pclsidMajorIdIn
    , LPCWSTR       pwszNetworkNameIn
    )
{
    TraceFunc( "[IClusCfgNetworkInfo]" );
    Assert( m_punkAddresses != NULL );
    Assert( pclsidMajorIdIn != NULL );
    Assert( pwszNetworkNameIn != NULL );

    HRESULT                 hr;
    IClusCfgIPAddressInfo * piccipi = NULL;

    if ( pbstrUIDOut == NULL )
    {
        hr = THR( E_POINTER );
        STATUS_REPORT_REF( TASKID_Major_Find_Devices, TASKID_Minor_GetUID, IDS_ERROR_NULL_POINTER, IDS_ERROR_NULL_POINTER_REF, hr );
        goto Cleanup;
    } // if:

    hr = STHR( HrGetPrimaryNetAddress( &piccipi, pclsidMajorIdIn, pwszNetworkNameIn ) );
    if ( FAILED( hr ) )
    {
        goto Cleanup;
    } // if:

    //
    // When there is not a primary network address, we goto cleanup
    //
    if ( ( piccipi == NULL ) && ( hr == S_FALSE ) )
    {
        goto Cleanup;
    } // if:

    hr = THR( piccipi->GetUID( pbstrUIDOut ) );

Cleanup:

    if ( piccipi != NULL )
    {
        piccipi->Release();
    } // if:

    HRETURN( hr );

} //*** CClusCfgNetworkInfo::HrGetNetUID


//////////////////////////////////////////////////////////////////////////////
//++
//
//  CClusCfgNetworkInfo::HrGetPrimaryNetAddress
//
//  Description:
//
//  Arguments:
//      pwszNetworkNameIn
//          Need to pass this in because m_bstrConnectionName doesn't reflect
//          the correct connection.
//
//  Return Value:
//
//  Remarks:
//      None.
//
//--
//////////////////////////////////////////////////////////////////////////////
HRESULT
CClusCfgNetworkInfo::HrGetPrimaryNetAddress(
      IClusCfgIPAddressInfo ** ppIPAddressOut
    , const CLSID *            pclsidMajorIdIn
    , LPCWSTR                  pwszNetworkNameIn
    )
{
    TraceFunc( "[IClusCfgNetworkInfo]" );
    Assert( m_punkAddresses != NULL );
    Assert( pclsidMajorIdIn != NULL );
    Assert( pwszNetworkNameIn != NULL );

    HRESULT hr;
    ULONG   cFetched;

    hr = STHR( HrGetPrimaryNetworkAddress( ppIPAddressOut, &cFetched ) );
    if ( FAILED( hr ) )
    {
        goto Cleanup;
    } // if:

    if ( ( hr == S_FALSE ) && ( cFetched == 0 ) )
    {
        STATUS_REPORT_STRING(
              TASKID_Major_Find_Devices
            , *pclsidMajorIdIn
            , IDS_INFO_NETWORK_CONNECTION_CONCERN
            , pwszNetworkNameIn
            , hr
            );
        STATUS_REPORT_STRING_REF(
              *pclsidMajorIdIn
            , TASKID_Minor_Primary_IP_Address
            , IDS_ERROR_PRIMARY_IP_NOT_FOUND
            , pwszNetworkNameIn
            , IDS_ERROR_PRIMARY_IP_NOT_FOUND_REF
            , hr
            );
    } // if:

Cleanup:

    HRETURN( hr );

} //*** CClusCfgNetworkInfo::HrGetPrimaryNetAddress


//*************************************************************************//


/////////////////////////////////////////////////////////////////////////////
// CClusCfgNetworkInfo class -- Private Methods.
/////////////////////////////////////////////////////////////////////////////


//////////////////////////////////////////////////////////////////////////////
//++
//
//  CClusCfgNetworkInfo::HrInit
//
//  Description:
//      Initialize this component.
//
//  Arguments:
//      None.
//
//  Return Value:
//
//
//  Remarks:
//      None.
//
//--
//////////////////////////////////////////////////////////////////////////////
HRESULT
CClusCfgNetworkInfo::HrInit( void )
{
    TraceFunc( "" );

    HRESULT hr = S_OK;

    // IUnknown
    Assert( m_cRef == 1 );

    m_dwFlags = ( eIsPrivate | eIsPublic );

    HRETURN( hr );

} //*** CClusCfgNetworkInfo::HrInit


//////////////////////////////////////////////////////////////////////////////
//++
//
//  CClusCfgNetworkInfo::HrInit
//
//  Description:
//      Initialize this component.
//
//  Arguments:
//      hNetworkIn
//      hNetInterfaceIn
//
//  Return Value:
//
//
//  Remarks:
//      None.
//
//--
//////////////////////////////////////////////////////////////////////////////
HRESULT
CClusCfgNetworkInfo::HrInit(
      HNETWORK      hNetworkIn
    , HNETINTERFACE hNetInterfaceIn
    )
{
    TraceFunc( "[HNETWORK]" );

    HRESULT                 hr = S_OK;
    DWORD                   sc;
    CClusPropList           cplNetworkCommon;
    CClusPropList           cplNetworkROCommon;
    CClusPropList           cplNetInterfaceROCommon;
    CLUSPROP_BUFFER_HELPER  cpbh;
    ULONG                   ulIPAddress;
    ULONG                   ulSubnetMask;

    // IUnknown
    Assert( m_cRef == 1 );

    //
    //  Get the network common read only properties
    //
    sc = TW32( cplNetworkROCommon.ScGetNetworkProperties( hNetworkIn, CLUSCTL_NETWORK_GET_RO_COMMON_PROPERTIES ) );
    if ( sc != ERROR_SUCCESS )
    {
        goto MakeHr;
    } // if:

    //
    //  Get the network common properties
    //
    sc = TW32( cplNetworkCommon.ScGetNetworkProperties( hNetworkIn, CLUSCTL_NETWORK_GET_COMMON_PROPERTIES ) );
    if ( sc != ERROR_SUCCESS )
    {
        goto MakeHr;
    } // if:

    //
    //  Get the netinterface common read only properties
    //
    sc = TW32( cplNetInterfaceROCommon.ScGetNetInterfaceProperties( hNetInterfaceIn, CLUSCTL_NETINTERFACE_GET_RO_COMMON_PROPERTIES ) );
    if ( sc != ERROR_SUCCESS )
    {
        goto MakeHr;
    } // if:

    //
    //  Find the RO property "name" and save it.
    //
    sc = TW32( cplNetworkROCommon.ScMoveToPropertyByName( L"Name" ) );
    if ( sc != ERROR_SUCCESS )
    {
        goto MakeHr;
    } // if:

    cpbh = cplNetworkROCommon.CbhCurrentValue();
    Assert( cpbh.pSyntax->dw == CLUSPROP_SYNTAX_LIST_VALUE_SZ );

    //
    //  If the name is empty tell the user and leave.
    //
    if ( ( cpbh.pStringValue->sz != NULL ) && ( cpbh.pStringValue->sz[ 0 ] != L'\0' ) )
    {
        m_bstrConnectionName = TraceSysAllocString( cpbh.pStringValue->sz );
        if ( m_bstrConnectionName == NULL )
        {
            goto OutOfMemory;
        } // if:
    } // if:
    else
    {
        hr = HRESULT_FROM_WIN32( TW32( ERROR_INVALID_DATA ) );
        LOG_STATUS_REPORT_MINOR(
                  TASKID_Minor_HrInit_No_Network_Name
                , L"[SRV] A network name was not found in the common read-only properties."
                , hr
                );
        goto Cleanup;
    } // else:

    //
    //  Find the property "description" and save it.
    //
    sc = TW32( cplNetworkCommon.ScMoveToPropertyByName( L"Description" ) );
    if ( sc != ERROR_SUCCESS )
    {
        goto MakeHr;
    } // if:

    cpbh = cplNetworkCommon.CbhCurrentValue();
    Assert( cpbh.pSyntax->dw == CLUSPROP_SYNTAX_LIST_VALUE_SZ );

    //
    //  If the description is empty then alloc a space and continue.
    //
    if ( ( cpbh.pStringValue->sz != NULL ) && ( cpbh.pStringValue->sz[ 0 ] != L'\0' ) )
    {
        m_bstrDescription = TraceSysAllocString( cpbh.pStringValue->sz );
        if ( m_bstrDescription == NULL )
        {
            goto OutOfMemory;
        } // if:
    } // if:
    else
    {
        m_bstrDescription = TraceSysAllocString( L" " );
        if ( m_bstrDescription == NULL )
        {
            goto OutOfMemory;
        } // if:
    } // else:

    //
    //  Find the property "role".
    //
    sc = TW32( cplNetworkCommon.ScMoveToPropertyByName( L"Role" ) );
    if ( sc != ERROR_SUCCESS )
    {
        goto MakeHr;
    } // if:

    cpbh = cplNetworkCommon.CbhCurrentValue();
    Assert( cpbh.pSyntax->dw == CLUSPROP_SYNTAX_LIST_VALUE_DWORD );

    //
    //  Switch on the role value and set the "private" or "public" bits.
    //
    switch ( cpbh.pDwordValue->dw )
    {
        case ClusterNetworkRoleNone :
            m_dwFlags = 0;
            break;

        case ClusterNetworkRoleInternalUse :
            m_dwFlags = eIsPrivate;
            break;

        case ClusterNetworkRoleClientAccess :
            m_dwFlags = eIsPublic;
            break;

        case ClusterNetworkRoleInternalAndClient :
            m_dwFlags = ( eIsPrivate | eIsPublic );
            break;

        //
        //  Should never get here!
        //
        default:
            hr = HRESULT_FROM_WIN32( TW32( ERROR_INVALID_DATA ) );
            LOG_STATUS_REPORT_STRING_MINOR2(
                      TASKID_Minor_HrInit_Unknown_Network_Role
                    , L"[SRV] The Role property on the network '%1!ws!' is set to %d which is unrecognized."
                    , m_bstrConnectionName
                    , cpbh.pDwordValue->dw
                    , hr
                    );
            goto Cleanup;
    } // switch:

    //
    //  Find the RO property address and save it.
    //
    sc = TW32( cplNetInterfaceROCommon.ScMoveToPropertyByName( L"Address" ) );
    if ( sc != ERROR_SUCCESS )
    {
        goto MakeHr;
    } // if:

    cpbh = cplNetInterfaceROCommon.CbhCurrentValue();
    Assert( cpbh.pSyntax->dw == CLUSPROP_SYNTAX_LIST_VALUE_SZ );

    sc = TW32( ClRtlTcpipStringToAddress( cpbh.pStringValue->sz, &ulIPAddress ) );
    if ( sc != ERROR_SUCCESS )
    {
        goto MakeHr;
    } // if:

    //
    //  Find the RO property AddressMask and save it.
    //
    sc = TW32( cplNetworkROCommon.ScMoveToPropertyByName( L"AddressMask" ) );
    if ( sc != ERROR_SUCCESS )
    {
        goto MakeHr;
    } // if:

    cpbh = cplNetworkROCommon.CbhCurrentValue();
    Assert( cpbh.pSyntax->dw == CLUSPROP_SYNTAX_LIST_VALUE_SZ );

    sc = TW32( ClRtlTcpipStringToAddress( cpbh.pStringValue->sz, &ulSubnetMask ) );
    if ( sc != ERROR_SUCCESS )
    {
        goto MakeHr;
    } // if:

    hr = THR( HrCreateEnumAndAddIPAddress( ulIPAddress, ulSubnetMask ) );
    if ( FAILED( hr ) )
    {
        goto Cleanup;
    } // if:

    m_fIsClusterNetwork = TRUE;

    goto Cleanup;

MakeHr:

    hr = HRESULT_FROM_WIN32( sc );
    STATUS_REPORT_REF( TASKID_Major_Find_Devices, TASKID_Minor_HrInit_Win32Error, IDS_ERROR_WIN32, IDS_ERROR_WIN32_REF, hr );
    goto Cleanup;

OutOfMemory:

    hr = THR( E_OUTOFMEMORY );
    STATUS_REPORT_REF( TASKID_Major_Find_Devices, TASKID_Minor_HrInit_OutOfMemory, IDS_ERROR_OUTOFMEMORY, IDS_ERROR_OUTOFMEMORY_REF, hr );

Cleanup:

    HRETURN( hr );

} //*** CClusCfgNetworkInfo::HrInit( hNetworkIn, hNetInterfaceIn )


//////////////////////////////////////////////////////////////////////////////
//++
//
//  CClusCfgNetworkInfo::HrLoadEnum
//
//  Description:
//      Load the contained enumerator
//
//  Arguments:
//      None.
//
//  Return Value:
//
//
//  Remarks:
//      None.
//
//--
//////////////////////////////////////////////////////////////////////////////
HRESULT
CClusCfgNetworkInfo::HrLoadEnum(
      IWbemClassObject * pNetworkAdapterIn
    , bool *            pfRetainObjectOut
    )
{
    TraceFunc( "" );

    HRESULT                 hr;
    IClusCfgSetWbemObject * piccswo = NULL;

    hr = THR( HrCreateEnum() );
    if ( FAILED( hr ) )
    {
        goto Cleanup;
    } // if:

    hr = THR( m_punkAddresses->TypeSafeQI( IClusCfgSetWbemObject, &piccswo ) );
    if ( FAILED( hr ) )
    {
        goto Cleanup;
    } // if:

    hr = STHR( piccswo->SetWbemObject( pNetworkAdapterIn, pfRetainObjectOut ) );

Cleanup:

    if ( piccswo != NULL )
    {
        piccswo->Release();
    } // if:

    HRETURN( hr );

} //*** CClusCfgNetworkInfo::HrLoadEnum


//////////////////////////////////////////////////////////////////////////////
//++
//
//  CClusCfgNetworkInfo::HrCreateEnum
//
//  Description:
//      Create the contained enumerator
//
//  Arguments:
//      None.
//
//  Return Value:
//
//
//  Remarks:
//      None.
//
//--
//////////////////////////////////////////////////////////////////////////////
HRESULT
CClusCfgNetworkInfo::HrCreateEnum( void )
{
    TraceFunc( "" );
    Assert( m_punkAddresses == NULL );

    HRESULT     hr;
    IUnknown *  punk = NULL;

    hr = THR( CEnumClusCfgIPAddresses::S_HrCreateInstance( &punk ) );
    if ( FAILED( hr ) )
    {
        goto Cleanup;
    } // if:

    punk = TraceInterface( L"CEnumClusCfgIPAddresses", IUnknown, punk, 1 );

    hr = THR( HrSetInitialize( punk, m_picccCallback, m_lcid ) );
    if ( FAILED( hr ))
    {
        goto Cleanup;
    } // if:

    hr = THR( HrSetWbemServices( punk, m_pIWbemServices ) );
    if ( FAILED( hr ) )
    {
        goto Cleanup;
    } // if:

    m_punkAddresses = punk;
    m_punkAddresses->AddRef();

Cleanup:

    if ( punk != NULL )
    {
        punk->Release();
    } // if:

    HRETURN( hr );

} //*** CClusCfgNetworkInfo::HrCreateEnum


//////////////////////////////////////////////////////////////////////////////
//++
//
//  CClusCfgNetworkInfo::HrCreateEnumAndAddIPAddress
//
//  Description:
//      Add an IPAddress to the contained enumerator.
//
//  Arguments:
//      None.
//
//  Return Value:
//
//
//  Remarks:
//      None.
//
//--
//////////////////////////////////////////////////////////////////////////////
HRESULT
CClusCfgNetworkInfo::HrCreateEnumAndAddIPAddress(
      ULONG ulIPAddressIn
    , ULONG ulSubnetMaskIn
    )
{
    TraceFunc( "" );
    Assert( m_punkAddresses == NULL );

    HRESULT     hr;
    IUnknown *  punk = NULL;
    IUnknown *  punkCallback = NULL;

    hr = THR( m_picccCallback->TypeSafeQI( IUnknown, &punkCallback ) );
    if ( FAILED( hr ) )
    {
        goto Cleanup;
    } // if:

    //
    //  Have to pass the Initialize interface arguments since new objects will
    //  be created when this call is made.
    //
    hr = THR( CEnumClusCfgIPAddresses::S_HrCreateInstance( ulIPAddressIn, ulSubnetMaskIn, punkCallback, m_lcid, m_pIWbemServices, &punk ) );
    if ( FAILED( hr ) )
    {
        goto Cleanup;
    } // if:

    punk = TraceInterface( L"CEnumClusCfgIPAddresses", IUnknown, punk, 1 );

    //
    //  This is special -- do not initialize this again.
    //
    //hr = THR( HrSetInitialize( punk, m_picccCallback, m_lcid ) );
    //if ( FAILED( hr ))
    //{
    //    goto Cleanup;
    //} // if:

    //hr = THR( HrSetWbemServices( punk, m_pIWbemServices ) );
    //if ( FAILED( hr ) )
    //{
    //    goto Cleanup;
    //} // if:

    m_punkAddresses = punk;
    m_punkAddresses->AddRef();

Cleanup:

    if ( punkCallback != NULL )
    {
        punkCallback->Release();
    } // if:

    if ( punk != NULL )
    {
        punk->Release();
    } // if:

    HRETURN( hr );

} //*** CClusCfgNetworkInfo::HrCreateEnumAndAddIPAddress



//////////////////////////////////////////////////////////////////////////////
//++
//
//  CClusCfgNetworkInfo::HrGetPrimaryNetworkAddress
//
//  Description:
//
//  Arguments:
//
//  Return Value:
//
//  Remarks:
//      None.
//
//--
//////////////////////////////////////////////////////////////////////////////
HRESULT
CClusCfgNetworkInfo::HrGetPrimaryNetworkAddress(
      IClusCfgIPAddressInfo ** ppIPAddressOut
    , ULONG *                  pcFetched
    )
{
    TraceFunc( "[IClusCfgNetworkInfo]" );
    Assert( m_punkAddresses != NULL );

    HRESULT                     hr;
    IEnumClusCfgIPAddresses *   pieccipa = NULL;

    if ( ppIPAddressOut == NULL )
    {
        hr = THR( E_POINTER );
        STATUS_REPORT_REF( TASKID_Major_Find_Devices, TASKID_Minor_GetPrimaryNetworkAddress, IDS_ERROR_NULL_POINTER, IDS_ERROR_NULL_POINTER_REF, hr );
        goto Cleanup;
    } // if:

    hr = THR( m_punkAddresses->TypeSafeQI( IEnumClusCfgIPAddresses, &pieccipa ) );
    if ( FAILED( hr ) )
    {
        goto Cleanup;
    } // if:

    hr = THR( pieccipa->Reset() );
    if ( FAILED( hr ) )
    {
        goto Cleanup;
    } // if:

    hr = STHR( pieccipa->Next( 1, ppIPAddressOut, pcFetched ) );
    if ( FAILED( hr ) )
    {
        goto Cleanup;
    } // if:

Cleanup:

    if ( pieccipa != NULL )
    {
        pieccipa->Release();
    } // if:

    HRETURN( hr );

} //*** CClusCfgNetworkInfo::HrGetPrimaryNetworkAddress