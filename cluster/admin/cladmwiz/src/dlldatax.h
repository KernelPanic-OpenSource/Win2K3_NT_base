#if !defined(AFX_DLLDATAX_H__CC7C59FA_64F2_11D1_9AA7_00C04FB93A80__INCLUDED_)
#define AFX_DLLDATAX_H__CC7C59FA_64F2_11D1_9AA7_00C04FB93A80__INCLUDED_

#if _MSC_VER >= 1000
#pragma once
#endif // _MSC_VER >= 1000

#ifdef _MERGE_PROXYSTUB

extern "C" 
{
BOOL WINAPI PrxDllMain(HINSTANCE hInstance, DWORD dwReason, 
	LPVOID lpReserved);
STDAPI PrxDllCanUnloadNow(void);
STDAPI PrxDllGetClassObject(REFCLSID rclsid, REFIID riid, LPVOID* ppv);
STDAPI PrxDllRegisterServer(void);
STDAPI PrxDllUnregisterServer(void);
}

#endif

//{{AFX_INSERT_LOCATION}}
// Microsoft Developer Studio will insert additional declarations immediately before the previous line.

#endif // !defined(AFX_DLLDATAX_H__CC7C59FA_64F2_11D1_9AA7_00C04FB93A80__INCLUDED_)
