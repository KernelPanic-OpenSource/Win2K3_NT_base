/////////////////////////////////////////////////////////////////////////////
//
//  Copyright (c) 1997-2002 Microsoft Corporation
//
//  Module Name:
//      DlgHelp.h
//
//  Abstract:
//      Definition of the CDialogHelp class.
//
//  Implementation File:
//      DlgHelp.cpp
//
//  Author:
//      David Potter (davidp)   February 6, 1997
//
//  Revision History:
//
//  Notes:
//
/////////////////////////////////////////////////////////////////////////////

#ifndef __DLGHELP_H__
#define __DLGHELP_H__

/////////////////////////////////////////////////////////////////////////////
// Forward Class Declarations
/////////////////////////////////////////////////////////////////////////////

class CDialogHelp;

/////////////////////////////////////////////////////////////////////////////
// External Class Declarations
/////////////////////////////////////////////////////////////////////////////

/////////////////////////////////////////////////////////////////////////////
// Include Files
/////////////////////////////////////////////////////////////////////////////

/////////////////////////////////////////////////////////////////////////////
// Type Definitions
/////////////////////////////////////////////////////////////////////////////

struct CMapCtrlToHelpID
{
    DWORD   m_nCtrlID;
    DWORD   m_nHelpCtrlID;
};

/////////////////////////////////////////////////////////////////////////////
// CDialogHelp dialog
/////////////////////////////////////////////////////////////////////////////

class CDialogHelp : public CObject
{
    DECLARE_DYNAMIC( CDialogHelp )

// Construction
public:
    CDialogHelp( void ) { CommonConstruct(); }
    CDialogHelp( const DWORD * pdwHelpMap, DWORD dwMask );

    void CommonConstruct(void);

// Attributes
protected:
    const CMapCtrlToHelpID *    m_pmap;
    DWORD                       m_dwMask;
    DWORD                       m_nHelpID;

public:
    const CMapCtrlToHelpID *    Pmap( void ) const      { return m_pmap; }
    DWORD                       DwMask( void ) const    { return m_dwMask; }
    DWORD                       NHelpID( void ) const   { return m_nHelpID; }

    DWORD                       NHelpFromCtrlID( IN DWORD nCtrlID ) const;
    void                        SetMap( IN const DWORD * pdwHelpMap )
    {
        ASSERT( pdwHelpMap != NULL );
        m_pmap = (const CMapCtrlToHelpID *) pdwHelpMap;
    } //*** SetMap()

// Operations
public:
    void        SetHelpMask( IN DWORD dwMask )  { ASSERT( dwMask != 0 ); m_dwMask = dwMask; }

    void        OnContextMenu( CWnd * pWnd, CPoint point );
    BOOL        OnHelpInfo( HELPINFO * pHelpInfo );
    LRESULT     OnCommandHelp( WPARAM wParam, LPARAM lParam );

// Overrides

// Implementation

};  //*** class CDialogHelp

/////////////////////////////////////////////////////////////////////////////

#endif // __DLGHELP_H__
