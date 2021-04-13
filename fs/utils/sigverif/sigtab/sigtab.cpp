//--------------------------------------------------------------------------------
//
//  File:   sigtab.cpp
//
//  Copyright (c) Microsoft Corp. All Rights Reserved
//
//--------------------------------------------------------------------------------
#include "sigtab.h"

HINSTANCE g_hInst = NULL;

//---------------------------------------------------------------------------
// DllMain()
//---------------------------------------------------------------------------
int APIENTRY DllMain( HINSTANCE hInstance, DWORD dwReason, LPVOID )
{
    if ( dwReason == DLL_PROCESS_ATTACH ) {        // Initializing
        g_hInst = hInstance;

        DisableThreadLibraryCalls(hInstance);
    }

    return 1;
}

void GetCurrentDriverSigningPolicy( LPDWORD lpdwDefault, LPDWORD lpdwPolicy, LPDWORD lpdwPreference )
{
    SYSTEMTIME RealSystemTime;
    DWORD dwSize, dwType;
    DWORD dwDefault, dwPolicy, dwPreference;
    HKEY hKey;
    CONST TCHAR pszDrvSignPath[]                     = REGSTR_PATH_DRIVERSIGN;
    CONST TCHAR pszDrvSignPolicyPath[]               = REGSTR_PATH_DRIVERSIGN_POLICY;
    CONST TCHAR pszDrvSignPolicyValue[]              = REGSTR_VAL_POLICY;
    CONST TCHAR pszDrvSignBehaviorOnFailedVerifyDS[] = REGSTR_VAL_BEHAVIOR_ON_FAILED_VERIFY;

    dwPolicy = dwPreference = (DWORD) -1;

    RealSystemTime.wDayOfWeek = LOWORD(&hKey) | 4;
    pSetupGetRealSystemTime(&RealSystemTime);
    dwDefault = (((RealSystemTime.wMilliseconds+2)&15)^8)/4;

    //
    // Retrieve the user policy.
    //
    if (ERROR_SUCCESS == RegOpenKeyEx(HKEY_CURRENT_USER,
                                      pszDrvSignPolicyPath,
                                      0,
                                      KEY_READ,
                                      &hKey)) {
        dwSize = sizeof(dwPolicy);
        if (ERROR_SUCCESS == RegQueryValueEx(hKey,
                                             pszDrvSignBehaviorOnFailedVerifyDS,
                                             NULL,
                                             &dwType,
                                             (PBYTE)&dwPolicy,
                                             &dwSize)) {
            //
            // Finally, make sure a valid policy value was specified.
            //
            if ((dwType != REG_DWORD) ||
                (dwSize != sizeof(DWORD)) ||
                !((dwPolicy == DRIVERSIGN_NONE) || (dwPolicy == DRIVERSIGN_WARNING) || (dwPolicy == DRIVERSIGN_BLOCKING))) {
                //
                // Bogus entry for user policy--ignore it.
                //
                dwPolicy = DRIVERSIGN_NONE;
            }
        }

        RegCloseKey(hKey);
    }

    //
    // Finally, retrieve the user preference.
    //
    if (ERROR_SUCCESS == RegOpenKeyEx(HKEY_CURRENT_USER,
                                      pszDrvSignPath,
                                      0,
                                      KEY_READ,
                                      &hKey)) {
        dwSize = sizeof(dwPreference);
        if (ERROR_SUCCESS == RegQueryValueEx(hKey,
                                             pszDrvSignPolicyValue,
                                             NULL,
                                             &dwType,
                                             (PBYTE)&dwPreference,
                                             &dwSize)) {
            if ((dwType != REG_DWORD) ||
                (dwSize != sizeof(DWORD)) ||
                !((dwPreference == DRIVERSIGN_NONE) || (dwPreference == DRIVERSIGN_WARNING) || (dwPreference == DRIVERSIGN_BLOCKING))) {
                //
                // Bogus entry for user preference--ignore it.
                //
                dwPreference = DRIVERSIGN_NONE;
            }
        }

        RegCloseKey(hKey);
    }

    //
    // Store the values into the user buffer.
    //
    *lpdwDefault    = dwDefault;
    *lpdwPolicy     = dwPolicy;
    *lpdwPreference = dwPreference;
}

DWORD SigTab_UpdateDialog(HWND hwnd)
{
    DWORD   dwPreference = DRIVERSIGN_NONE;
    DWORD   dwDefault = DRIVERSIGN_NONE;
    DWORD   dwPolicy = DRIVERSIGN_NONE;
    DWORD   dwCurSel;

    //
    // Get the current policy settings from the registry.
    //
    GetCurrentDriverSigningPolicy(&dwDefault, &dwPolicy, &dwPreference);

    //
    // If there is no preference, set it to the policy or the default.
    //
    if (dwPreference == (DWORD) -1) {
        if (dwPolicy != (DWORD) -1)
            dwPreference = dwPolicy;
        else dwPreference = dwDefault;
    }

    //
    // Figure out which item is really selected and re-select it.  This will get rid of any checked && disabled items.
    //
    dwCurSel = dwPreference;
    if (IsDlgButtonChecked(hwnd, IDC_IGNORE) && IsWindowEnabled(GetDlgItem(hwnd, IDC_IGNORE)))
        dwCurSel = IDC_IGNORE;
    if (IsDlgButtonChecked(hwnd, IDC_WARN) && IsWindowEnabled(GetDlgItem(hwnd, IDC_WARN)))
        dwCurSel = IDC_WARN;
    if (IsDlgButtonChecked(hwnd, IDC_BLOCK) && IsWindowEnabled(GetDlgItem(hwnd, IDC_BLOCK)))
        dwCurSel = IDC_BLOCK;
    EnableWindow(GetDlgItem(hwnd, IDC_IGNORE), TRUE);
    EnableWindow(GetDlgItem(hwnd, IDC_WARN), TRUE);
    EnableWindow(GetDlgItem(hwnd, IDC_BLOCK), TRUE);
    CheckRadioButton(hwnd, IDC_IGNORE, IDC_BLOCK, dwCurSel);

    //
    // If there is a policy for this user, it overrides any preferences so grey everything but the policy setting.
    //
    if (dwPolicy != (DWORD) -1) {
        //
        // If the system default is stronger, it will be used instead.
        //
        if (dwDefault > dwPolicy)
            dwPolicy = dwDefault;

        EnableWindow(GetDlgItem(hwnd, IDC_IGNORE), FALSE);
        EnableWindow(GetDlgItem(hwnd, IDC_WARN), FALSE);
        EnableWindow(GetDlgItem(hwnd, IDC_BLOCK), FALSE);
        switch (dwPolicy) {
        case DRIVERSIGN_WARNING:    EnableWindow(GetDlgItem(hwnd, IDC_WARN), TRUE);
            CheckRadioButton(hwnd, IDC_IGNORE, IDC_BLOCK, IDC_WARN);
            break;

        case DRIVERSIGN_BLOCKING:   EnableWindow(GetDlgItem(hwnd, IDC_BLOCK), TRUE);
            CheckRadioButton(hwnd, IDC_IGNORE, IDC_BLOCK, IDC_BLOCK);
            break;

        default:                    EnableWindow(GetDlgItem(hwnd, IDC_IGNORE), TRUE);
            CheckRadioButton(hwnd, IDC_IGNORE, IDC_BLOCK, IDC_IGNORE);
            break;
        }

        dwPreference = dwPolicy;        
    } else {
        //
        // Grey out the items being over-ridden by the systen policy.  Bump the selection down to the first available slot.
        //
        switch (dwDefault) {
        case DRIVERSIGN_BLOCKING:   if (IsDlgButtonChecked(hwnd, IDC_WARN) || IsDlgButtonChecked(hwnd, IDC_IGNORE))
                CheckRadioButton(hwnd, IDC_IGNORE, IDC_BLOCK, IDC_BLOCK);
            EnableWindow(GetDlgItem(hwnd, IDC_IGNORE), FALSE);
            EnableWindow(GetDlgItem(hwnd, IDC_WARN), FALSE);
            break;

        case DRIVERSIGN_WARNING:    if (IsDlgButtonChecked(hwnd, IDC_IGNORE))
                CheckRadioButton(hwnd, IDC_IGNORE, IDC_BLOCK, IDC_WARN);
            EnableWindow(GetDlgItem(hwnd, IDC_IGNORE), FALSE);
            break;
        }

        //
        // If the system default is stronger, it will be used instead.
        //
        if (dwDefault > dwPreference)
            dwPreference = dwDefault;
    }

    if (pSetupIsUserAdmin()) {
        //
        // If the administrator can set the default, make everything available for selection.
        //
        if (IsDlgButtonChecked(hwnd, IDC_GLOBAL)) {
            EnableWindow(GetDlgItem(hwnd, IDC_IGNORE), TRUE);
            EnableWindow(GetDlgItem(hwnd, IDC_WARN), TRUE);
            EnableWindow(GetDlgItem(hwnd, IDC_BLOCK), TRUE);
        }
    }

    return dwPreference;
}

//
//  Initialization of search dialog.
//
BOOL SigTab_OnInitDialog(HWND hwnd, HWND hwndFocus, LPARAM lParam)
{
    DWORD   dwPreference = DRIVERSIGN_NONE;
    DWORD   dwDefault = DRIVERSIGN_NONE;
    DWORD   dwPolicy = DRIVERSIGN_NONE;
    BOOL    bAdmin;

    UNREFERENCED_PARAMETER(hwndFocus);
    UNREFERENCED_PARAMETER(lParam);

    ShowWindow(hwnd, SW_SHOW);

    CheckRadioButton(hwnd, IDC_IGNORE, IDC_BLOCK, IDC_IGNORE);
    CheckDlgButton(hwnd, IDC_GLOBAL, BST_UNCHECKED);

    bAdmin = pSetupIsUserAdmin();
    ShowWindow(GetDlgItem(hwnd, IDC_GLOBAL), bAdmin ? SW_SHOW : SW_HIDE);
    ShowWindow(GetDlgItem(hwnd, IDG_ADMIN), bAdmin ? SW_SHOW : SW_HIDE);

    GetCurrentDriverSigningPolicy(&dwDefault, &dwPolicy, &dwPreference);

    //
    // Call SigTab_UpdateDialog to initialize the dialog
    //
    dwPreference = SigTab_UpdateDialog(hwnd);

    //
    // Check the radio button for their calculated "preference".
    //
    switch (dwPreference) {
    case DRIVERSIGN_WARNING:    CheckRadioButton(hwnd, IDC_IGNORE, IDC_BLOCK, IDC_WARN);
        break;
    case DRIVERSIGN_BLOCKING:   CheckRadioButton(hwnd, IDC_IGNORE, IDC_BLOCK, IDC_BLOCK);
        break;
    }

    //
    // If the user is an administrator, check the "Global" box if the preference matches the default setting.
    //
    if (bAdmin) {
        switch (dwDefault) {
        case DRIVERSIGN_WARNING:    if (IsDlgButtonChecked(hwnd, IDC_WARN))
                CheckDlgButton(hwnd, IDC_GLOBAL, BST_CHECKED);
            break;

        case DRIVERSIGN_BLOCKING:   if (IsDlgButtonChecked(hwnd, IDC_BLOCK))
                CheckDlgButton(hwnd, IDC_GLOBAL, BST_CHECKED);
            break;

        case DRIVERSIGN_NONE:       if (IsDlgButtonChecked(hwnd, IDC_IGNORE))
                CheckDlgButton(hwnd, IDC_GLOBAL, BST_CHECKED);
            break;
        }

        //
        // If the administrator can set the default, make everything available for selection.
        //
        if (IsDlgButtonChecked(hwnd, IDC_GLOBAL)) {
            EnableWindow(GetDlgItem(hwnd, IDC_IGNORE), TRUE);
            EnableWindow(GetDlgItem(hwnd, IDC_WARN), TRUE);
            EnableWindow(GetDlgItem(hwnd, IDC_BLOCK), TRUE);
        }
    }

    return TRUE;
}

void SigTab_Help(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL bContext)
{
    static DWORD SigTab_HelpIDs[] = 
    { 
        IDC_IGNORE, IDH_CODESIGN_IGNORE,
        IDC_WARN,   IDH_CODESIGN_WARN,
        IDC_BLOCK,  IDH_CODESIGN_BLOCK,
        IDC_GLOBAL, IDH_CODESIGN_APPLY,
        IDG_ADMIN,  (DWORD)-1,
        0,0
    };

    HWND hItem = NULL;
    LPHELPINFO lphi = NULL;
    POINT point;

    switch (uMsg) {
    case WM_HELP:
        lphi = (LPHELPINFO) lParam;
        if (lphi && (lphi->iContextType == HELPINFO_WINDOW))   // must be for a control
            hItem = (HWND) lphi->hItemHandle;
        break;

    case WM_CONTEXTMENU:
        hItem = (HWND) wParam;
        point.x = GET_X_LPARAM(lParam);
        point.y = GET_Y_LPARAM(lParam);
        if (ScreenToClient(hwnd, &point)) {
            hItem = ChildWindowFromPoint(hwnd, point);
        }
        break;
    }

    if (hItem && (GetWindowLong(hItem, GWL_ID) != IDC_STATIC)) {
        WinHelp(hItem,
                (LPCTSTR) SIGTAB_HELPFILE,
                (bContext ? HELP_CONTEXTMENU : HELP_WM_HELP),
                (ULONG_PTR) SigTab_HelpIDs);
    }
}

//
//
//
void SigTab_ApplySettings(HWND hwnd)
{
    HKEY    hKey;
    LONG    lRes;
    DWORD   dwData, dwSize, dwType, dwDisposition;

    lRes = RegCreateKeyEx(  HKEY_CURRENT_USER, 
                            SIGTAB_REG_KEY, 
                            NULL, 
                            NULL, 
                            REG_OPTION_NON_VOLATILE, 
                            KEY_WRITE, 
                            NULL, 
                            &hKey, 
                            &dwDisposition);

    if (lRes == ERROR_SUCCESS) {
        dwType = REG_DWORD;
        dwSize = sizeof(dwData);
        dwData = DRIVERSIGN_NONE;

        if (IsDlgButtonChecked(hwnd, IDC_WARN))
            dwData = DRIVERSIGN_WARNING;
        else
            if (IsDlgButtonChecked(hwnd, IDC_BLOCK))
            dwData = DRIVERSIGN_BLOCKING;

        lRes = RegSetValueEx(   hKey, 
                                SIGTAB_REG_VALUE, 
                                0, 
                                dwType, 
                                (CONST BYTE *) &dwData, 
                                dwSize);

        RegCloseKey(hKey);

        if (lRes == ERROR_SUCCESS && IsDlgButtonChecked(hwnd, IDC_GLOBAL) && pSetupIsUserAdmin()) {

            SYSTEMTIME RealSystemTime;

            if(ERROR_SUCCESS == RegOpenKeyEx(HKEY_LOCAL_MACHINE,
                                             TEXT("System\\WPA\\PnP"),
                                             0,
                                             KEY_READ,
                                             &hKey)) {

                dwSize = sizeof(dwData);
                if((ERROR_SUCCESS != RegQueryValueEx(hKey,
                                                     TEXT("seed"),
                                                     NULL,
                                                     &dwType,
                                                     (PBYTE)&dwData,
                                                     &dwSize))
                   || (dwType != REG_DWORD) || (dwSize != sizeof(dwData))) {

                    dwData = 0;
                }

                RegCloseKey(hKey);
            }

            RealSystemTime.wDayOfWeek = LOWORD(&hKey) | 4;
            RealSystemTime.wMinute = LOWORD(dwData);
            RealSystemTime.wYear = HIWORD(dwData);
            dwData = DRIVERSIGN_NONE;
            if(IsDlgButtonChecked(hwnd, IDC_WARN)) {
                dwData = DRIVERSIGN_WARNING;
            } else if(IsDlgButtonChecked(hwnd, IDC_BLOCK)) {
                dwData = DRIVERSIGN_BLOCKING;
            }
            RealSystemTime.wMilliseconds = (LOWORD(&lRes)&~3072)|(WORD)((dwData&3)<<10);
            pSetupGetRealSystemTime(&RealSystemTime);
        }
    }
}

//
//  Handle any WM_COMMAND messages sent to the search dialog
//
void SigTab_OnCommand(HWND hwnd, int id, HWND hwndCtl, UINT codeNotify)
{
    UNREFERENCED_PARAMETER(hwndCtl);
    UNREFERENCED_PARAMETER(codeNotify);

    switch (id) {
    case IDCANCEL: 
        EndDialog(hwnd, 0);
        break;

    case IDOK:
        SigTab_ApplySettings(hwnd);
        EndDialog(hwnd, 1);
        break;

    case IDC_GLOBAL:
        SigTab_UpdateDialog(hwnd);
        break;
    }

    return;
}

LRESULT SigTab_NotifyHandler(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    OSVERSIONINFOEX osVersionInfoEx;
    NMHDR *lpnmhdr = (NMHDR *) lParam;

    UNREFERENCED_PARAMETER(uMsg);
    UNREFERENCED_PARAMETER(wParam);

    switch (lpnmhdr->code) {
    case NM_RETURN:
    case NM_CLICK:
        if (lpnmhdr->idFrom == IDC_LINK) {
            //
            // We need to know if this is a server machine or a workstation 
            // machine since there are different help topic structures for
            // the different products.
            //
            ZeroMemory(&osVersionInfoEx, sizeof(osVersionInfoEx));
            osVersionInfoEx.dwOSVersionInfoSize = sizeof(osVersionInfoEx);
            if (!GetVersionEx((LPOSVERSIONINFO)&osVersionInfoEx)) {
                //
                // If GetVersionEx fails then assume this is a workstation
                // machine.
                //
                osVersionInfoEx.wProductType = VER_NT_WORKSTATION;
            }

            ShellExecute(hwnd,
                         TEXT("open"),
                         TEXT("HELPCTR.EXE"),
                         (osVersionInfoEx.wProductType == VER_NT_WORKSTATION)
                            ? TEXT("HELPCTR.EXE -url hcp://services/subsite?node=TopLevelBucket_4/Hardware&topic=MS-ITS%3A%25HELP_LOCATION%25%5Csysdm.chm%3A%3A/logo_testing.htm")
                            : TEXT("HELPCTR.EXE -url hcp://services/subsite?node=Hardware&topic=MS-ITS%3A%25HELP_LOCATION%25%5Csysdm.chm%3A%3A/logo_testing.htm"),
                         NULL,
                         SW_SHOWNORMAL
                         );
        }
        break;

    default:
        break;
    }

    return 0;
}

INT_PTR CALLBACK SigTab_DlgProc(HWND hwnd, UINT uMsg,
                                WPARAM wParam, LPARAM lParam)
{
    BOOL    fProcessed = TRUE;

    switch (uMsg) {
    HANDLE_MSG(hwnd, WM_INITDIALOG, SigTab_OnInitDialog);
    HANDLE_MSG(hwnd, WM_COMMAND, SigTab_OnCommand);

    case WM_HELP:
        SigTab_Help(hwnd, uMsg, wParam, lParam, FALSE);
        break;

    case WM_CONTEXTMENU:
        SigTab_Help(hwnd, uMsg, wParam, lParam, TRUE);
        break;

    case WM_NOTIFY:
        return SigTab_NotifyHandler(hwnd, uMsg, wParam, lParam);

    default: fProcessed = FALSE;
    }

    return fProcessed;
}

STDAPI DriverSigningDialog(HWND hwnd, DWORD dwFlagsReserved)
{   
    UNREFERENCED_PARAMETER(dwFlagsReserved);

    return((HRESULT)DialogBox(g_hInst, MAKEINTRESOURCE(IDD_SIGTAB), hwnd, SigTab_DlgProc));
}
