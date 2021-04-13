// FileSpy.h : main header file for the FILESPY application
//

#if !defined(AFX_FILESPY_H__177A9CDF_B3B4_41D6_B48C_79D0F309D152__INCLUDED_)
#define AFX_FILESPY_H__177A9CDF_B3B4_41D6_B48C_79D0F309D152__INCLUDED_

#if _MSC_VER > 1000
#pragma once
#endif // _MSC_VER > 1000

#ifndef __AFXWIN_H__
	#error include 'stdafx.h' before including this file for PCH
#endif

#include "resource.h"       // main symbols

/////////////////////////////////////////////////////////////////////////////
// CFileSpyApp:
// See FileSpy.cpp for the implementation of this class
//

class CFileSpyApp : public CWinApp
{
public:
	CFileSpyApp();

// Overrides
	// ClassWizard generated virtual function overrides
	//{{AFX_VIRTUAL(CFileSpyApp)
	public:
	virtual BOOL InitInstance();
	//}}AFX_VIRTUAL

// Implementation
	//{{AFX_MSG(CFileSpyApp)
	afx_msg void OnAppAbout();
	//}}AFX_MSG
	DECLARE_MESSAGE_MAP()
};


/////////////////////////////////////////////////////////////////////////////

//{{AFX_INSERT_LOCATION}}
// Microsoft Visual C++ will insert additional declarations immediately before the previous line.

#endif // !defined(AFX_FILESPY_H__177A9CDF_B3B4_41D6_B48C_79D0F309D152__INCLUDED_)
