//////////////////////////////////////////////////////////////////////////////
//
//  Copyright (c) 2000-2001 Microsoft Corporation
//
//  Module Name:
//      IPAddressPage.h
//
//  Maintained By:
//      David Potter    (DavidP)    14-MAR-2001
//      Geoffrey Pease  (GPease)    12-MAY-2000
//
//////////////////////////////////////////////////////////////////////////////

#pragma once

class CIPAddressPage
    : public INotifyUI
{

private: // data
    HWND                m_hwnd;             // Our HWND
    CClusCfgWizard *    m_pccw;             // Wizard
    ULONG *             m_pulIPAddress;     // External storage of IPAddress
    ULONG *             m_pulIPSubnet;      // Subnet mask
    BSTR *              m_pbstrNetworkName; // Network name for address
    OBJECTCOOKIE        m_cookieCompletion; // Completion cookie
    HANDLE              m_event;            // Event when verify IP address task is completed

    //  IUnknown
    LONG                m_cRef;             // Reference count

private: // methods
    LRESULT OnInitDialog( void );
    LRESULT OnNotify( WPARAM idCtrlIn, LPNMHDR pnmhdrIn );
    LRESULT OnNotifyQueryCancel( void );
    LRESULT OnNotifySetActive( void );
    LRESULT OnNotifyWizNext( void );
    LRESULT OnCommand( UINT idNotificationIn, UINT idControlIn, HWND hwndSenderIn );

    HRESULT HrUpdateWizardButtons( void );

    HRESULT 
        HrFindNetworkForIPAddress(
            IClusCfgNetworkInfo **  ppccniOut
            );
    HRESULT
        HrMatchNetwork(
            IClusCfgNetworkInfo *   pccniIn,
            BSTR                    bstrNetworkNameIn
            );

public: // methods
    CIPAddressPage(
          CClusCfgWizard *      pccwIn
        , ECreateAddMode        ecamCreateAddModeIn
        , ULONG *               pulIPAddressInout
        , ULONG *               pulIPSubnetInout
        , BSTR *                pbstrNetworkNameInout
        );
    virtual ~CIPAddressPage( void );

    static INT_PTR CALLBACK
        S_DlgProc( HWND hDlg, UINT Msg, WPARAM wParam, LPARAM lParam );

    //  IUnknown
    STDMETHOD( QueryInterface )( REFIID riidIn, LPVOID * ppvOut );
    STDMETHOD_( ULONG, AddRef )( void );
    STDMETHOD_( ULONG, Release )( void );

    //  INotifyUI
    STDMETHOD( ObjectChanged )( OBJECTCOOKIE cookieIn);

}; //*** class CIPAddressPage
