/////////////////////////////////////////////////////////////////////////////
//
//  Copyright (c) 1999 - 2002 Microsoft Corporation
//
//  Module Name:
//      DDxDDv.cpp
//
//  Description:
//      Implementation of custom dialog data exchange/dialog data validation
//      routines.
//
//  Author:
//      David Potter (DavidP)   March 24, 1999
//
//  Maintained by:
//      George Potts (GPotts)   April 19, 2002
//
//  Revision History:
//
//  Notes:
//      The IDS_REQUIRED_FIELD_EMPTY string resource must be defined in
//      the resource file.
//
/////////////////////////////////////////////////////////////////////////////

#include "stdafx.h"
#include "DDxDDv.h"

#include "resource.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif


/////////////////////////////////////////////////////////////////////////////
//++
//
//  DDX_Number
//
//  Description:
//      Do data exchange between the dialog and the class.
//
//  Arguments:
//      pDX         [IN OUT] Data exchange object 
//      nIDC        [IN] Control ID.
//      dwValue     [IN OUT] Value to set or get.
//      dwMin       [IN] Minimum value.
//      dwMax       [IN] Maximum value.
//      bSigned     [IN] TRUE = value is signed, FALSE = value is unsigned
//
//  Return Value:
//      None.
//
//--
/////////////////////////////////////////////////////////////////////////////
void AFXAPI DDX_Number(
    IN OUT CDataExchange *  pDX,
    IN int                  nIDC,
    IN OUT DWORD &          rdwValue,
    IN DWORD                dwMin,
    IN DWORD                dwMax,
    IN BOOL                 bSigned
    )
{
    HWND    hwndCtrl;
    DWORD   dwValue;
    HRESULT hr = S_OK;

    ASSERT( pDX != NULL );
#ifdef _DEBUG
    if ( bSigned )
    {
        ASSERT( static_cast< LONG >( dwMin ) < static_cast< LONG >( dwMax ) );
    }
    else
    {
        ASSERT( dwMin < dwMax );
    }
#endif // _DEBUG

    AFX_MANAGE_STATE( AfxGetStaticModuleState() );

    // Get the control window handle.
    hwndCtrl = pDX->PrepareEditCtrl( nIDC );

    if ( pDX->m_bSaveAndValidate )
    {
        BOOL    bTranslated;

        // Get the number from the control.
        dwValue = GetDlgItemInt( pDX->m_pDlgWnd->m_hWnd, nIDC, &bTranslated, bSigned );

        // If the retrival failed, it is a signed number, and the minimum
        // value is the smallest negative value possible, check the string itself.
        if ( ! bTranslated && bSigned && (dwMin == 0x80000000) )
        {
            UINT    cch;
            TCHAR   szNumber[ 20 ];

            // See if it is the smallest negative number.
            cch = GetDlgItemText( pDX->m_pDlgWnd->m_hWnd, nIDC, szNumber, RTL_NUMBER_OF( szNumber ) );
            if ( (cch != 0) && (ClRtlStrNICmp( szNumber, _T("-2147483648"), RTL_NUMBER_OF( szNumber ) ) == 0) )
            {
                dwValue = 0x80000000;
                bTranslated = TRUE;
            } // if:  text retrieved successfully and is highest negative number
        } // if:  error translating number and getting signed number

        // If the retrieval failed or the specified number is
        // out of range, display an error.
        if (    ! bTranslated
            ||  (bSigned
                && (    (static_cast< LONG >( dwValue ) < static_cast< LONG >( dwMin ))
                    ||  (static_cast< LONG >( dwValue ) > static_cast< LONG >( dwMax ))
                    )
                )
            ||  (!  bSigned
                &&  (   (dwValue < dwMin)
                    ||  (dwValue > dwMax)
                    )
                )
            )
        {
            TCHAR szMin[ 32 ];
            TCHAR szMax[ 32 ];
            CString strPrompt;

            if ( bSigned )
            {
                hr = StringCchPrintf( szMin, RTL_NUMBER_OF( szMin ), _T("%d%"), dwMin );
                hr = StringCchPrintf( szMax, RTL_NUMBER_OF( szMax ), _T("%d%"), dwMax );
            } // if:  signed number
            else
            {
                hr = StringCchPrintf( szMin, RTL_NUMBER_OF( szMin ), _T("%u%"), dwMin );
                hr = StringCchPrintf( szMax, RTL_NUMBER_OF( szMax ), _T("%u%"), dwMax );
            } // else:  unsigned number
            AfxFormatString2( strPrompt, AFX_IDP_PARSE_INT_RANGE, szMin, szMax );
            AfxMessageBox( strPrompt, MB_ICONEXCLAMATION, AFX_IDP_PARSE_INT_RANGE );
            strPrompt.Empty(); // exception prep
            pDX->Fail();
        } // if:  invalid string
        else
        {
            rdwValue = dwValue;
        } // if:  number is in range
    } // if:  saving data
    else
    {
        CString     strMinValue;
        CString     strMaxValue;
        UINT        cchMax;

        // Set the maximum number of characters that can be entered.
        if ( bSigned )
        {
            strMinValue.Format( _T("%d"), dwMin );
            strMaxValue.Format( _T("%d"), dwMax );
        } // if:  signed value
        else
        {
            strMinValue.Format( _T("%u"), dwMin );
            strMaxValue.Format( _T("%u"), dwMax );
        } // else:  unsigned value
        cchMax = max( strMinValue.GetLength(), strMaxValue.GetLength() );
        SendMessage( hwndCtrl, EM_LIMITTEXT, cchMax, 0 );

        // Set the value into the control.
        if ( bSigned )
        {
            LONG lValue = static_cast< LONG >( rdwValue );
            DDX_Text( pDX, nIDC, lValue );
        } // if:  signed value
        else
            DDX_Text( pDX, nIDC, rdwValue );
    } // else:  setting data onto the dialog

} //*** DDX_Number()

/////////////////////////////////////////////////////////////////////////////
//++
//
//  DDV_RequiredText
//
//  Description:
//      Validate that the dialog string is present.
//
//  Arguments:
//      pDX         [IN OUT] Data exchange object 
//      nIDC        [IN] Control ID.
//      nIDCLabel   [IN] Label control ID.
//      rstrValue   [IN] Value to set or get.
//
//  Return Value:
//      None.
//
//--
/////////////////////////////////////////////////////////////////////////////
void AFXAPI DDV_RequiredText(
    IN OUT CDataExchange *  pDX,
    IN int                  nIDC,
    IN int                  nIDCLabel,
    IN const CString &      rstrValue
    )
{
    ASSERT( pDX != NULL );

    AFX_MANAGE_STATE( AfxGetStaticModuleState() );

    if ( pDX->m_bSaveAndValidate )
    {
        if ( rstrValue.GetLength() == 0 )
        {
            HWND        hwndLabel;
            TCHAR       szLabel[ 1024 ];
            CString     strPrompt;

            // Get the label window handle
            hwndLabel = pDX->PrepareEditCtrl( nIDCLabel );

            // Get the text of the label.
            GetWindowText( hwndLabel, szLabel, RTL_NUMBER_OF( szLabel ) );

            // Remove ampersands (&) and colons (:).
            CleanupLabel( szLabel );

            // Format and display a message.
            strPrompt.FormatMessage( IDS_REQUIRED_FIELD_EMPTY, szLabel );
            AfxMessageBox( strPrompt, MB_ICONEXCLAMATION );

            // Do this so that the control receives focus.
            (void) pDX->PrepareEditCtrl( nIDC );

            // Fail the call.
            strPrompt.Empty();  // exception prep
            pDX->Fail();
        } // if:  field not specified
    } // if:  saving data

} //*** DDV_RequiredText()

/////////////////////////////////////////////////////////////////////////////
//++
//
//  CleanupLabel
//
//  Description:
//      Prepare a label read from a dialog to be used as a string in a
//      message by removing ampersands (&) and colons (:).
//
//  Arguments:
//      pszLabel    [IN OUT] Label to be cleaned up.
//
//  Return Value:
//      None.
//
//--
/////////////////////////////////////////////////////////////////////////////
void CleanupLabel( LPTSTR pszLabel )
{
    LPTSTR  pIn, pOut;
    LANGID  langid;
    WORD    primarylangid;
    BOOL    bFELanguage;

    // Get the language ID.
    langid = GetUserDefaultLangID();
    primarylangid = static_cast< WORD >( PRIMARYLANGID( langid ) );
    bFELanguage = ((primarylangid == LANG_JAPANESE)
                || (primarylangid == LANG_CHINESE)
                || (primarylangid == LANG_KOREAN) );

    //
    // Copy the name sans '&' and ':' chars
    //

    pIn = pOut = pszLabel;
    do
    {
        //
        // Strip FE accelerators with parentheses. e.g. "foo(&F)" -> "foo"
        //
        if (    bFELanguage
            &&  (pIn[ 0 ] == _T('('))
            &&  (pIn[ 1 ] == _T('&'))
            &&  (pIn[ 2 ] != _T('\0'))
            &&  (pIn[ 3 ] == _T(')')) )
        {
            pIn += 3;
        } // if:  Far East language with accelerator
        else if ( (*pIn != _T('&')) && (*pIn != _T(':')) )
        {
            *pOut++ = *pIn;
        } // else if:  accelerator found
    } while ( *pIn++ != _T('\0') );

} //*** CleanupLabel()
