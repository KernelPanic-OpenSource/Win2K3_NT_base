//+-------------------------------------------------------------------------
//
//  Microsoft Windows
//
//  Copyright (C) Microsoft Corporation
//
//  File:       selclass.c
//
//--------------------------------------------------------------------------

#include "newdevp.h"   

int CALLBACK
ClassListCompare(
    LPARAM lParam1,
    LPARAM lParam2,
    LPARAM lParamSort
    )
{
    TCHAR ClassDescription1[LINE_LEN];
    TCHAR ClassDescription2[LINE_LEN];

    UNREFERENCED_PARAMETER(lParamSort);

    //
    // Check if the 1st item is GUID_DEVCLASS_UNKNOWN
    //
    if (IsEqualGUID((LPGUID)lParam1, &GUID_DEVCLASS_UNKNOWN)) {
        return -1;
    }

    //
    // Check if the 2nd item is GUID_DEVCLASS_UNKNOWN
    //
    if (IsEqualGUID((LPGUID)lParam2, &GUID_DEVCLASS_UNKNOWN)) {
        return 1;
    }

    if (SetupDiGetClassDescription((LPGUID)lParam1,
                                   ClassDescription1,
                                   LINE_LEN,
                                   NULL
                                   ) &&
        SetupDiGetClassDescription((LPGUID)lParam2,
                                   ClassDescription2,
                                   LINE_LEN,
                                   NULL
                                   )) {
    
        return (lstrcmpi(ClassDescription1, ClassDescription2));
    }

    return 0;
}

void 
InitNDW_PickClassDlg(
    HWND hwndClassList,
    PNEWDEVWIZ NewDevWiz
    )
{
    LPGUID ClassGuid, lpClassGuidSelected;
    GUID ClassGuidSelected;
    int    lvIndex;
    DWORD  ClassGuidNum;
    LV_ITEM lviItem;
    TCHAR ClassDescription[LINE_LEN];

    SendMessage(hwndClassList, WM_SETREDRAW, FALSE, 0L);

    //
    // Clear the Class List
    //
    ListView_DeleteAllItems(hwndClassList);

    lviItem.mask = LVIF_TEXT | LVIF_PARAM;
    lviItem.iItem = -1;
    lviItem.iSubItem = 0;

    ClassGuid = NewDevWiz->ClassGuidList;
    ClassGuidNum = NewDevWiz->ClassGuidNum;

    //
    // Keep track of previosuly selected item
    //
    if (IsEqualGUID(&NewDevWiz->lvClassGuidSelected, &GUID_NULL)) {
        
        lpClassGuidSelected = NULL;
    }
    
    else {
        
        ClassGuidSelected = NewDevWiz->lvClassGuidSelected;
        NewDevWiz->lvClassGuidSelected = GUID_NULL;
        lpClassGuidSelected = &ClassGuidSelected;
    }


    while (ClassGuidNum--) {
        
        if (SetupDiGetClassDescription(ClassGuid,
                                       ClassDescription,
                                       SIZECHARS(ClassDescription),
                                       NULL
                                       ))
        {
            if (IsEqualGUID(ClassGuid, &GUID_DEVCLASS_UNKNOWN)) {

                //
                // We need to special case the UNKNOWN class and to give it a 
                // special icon (blank) and special text (Show All Devices).
                //
                LoadString(hNewDev, 
                           IDS_SHOWALLDEVICES, 
                           ClassDescription, 
                           SIZECHARS(ClassDescription)
                           );
                lviItem.iImage = g_BlankIconIndex;                
                lviItem.mask |= LVIF_IMAGE;

            } else if (SetupDiGetClassImageIndex(&NewDevWiz->ClassImageList,
                                           ClassGuid,
                                           &lviItem.iImage
                                           )) {

                lviItem.mask |= LVIF_IMAGE;
            
            } else {
                
                lviItem.mask &= ~LVIF_IMAGE;
            }

            lviItem.pszText = ClassDescription;
            lviItem.lParam = (LPARAM) ClassGuid;
            lvIndex = ListView_InsertItem(hwndClassList, &lviItem);

            //
            // check for previous selection
            //
            if (lpClassGuidSelected &&
                IsEqualGUID(lpClassGuidSelected, ClassGuid))
            {
                ListView_SetItemState(hwndClassList,
                                      lvIndex,
                                      LVIS_SELECTED|LVIS_FOCUSED,
                                      LVIS_SELECTED|LVIS_FOCUSED
                                      );

                lpClassGuidSelected = NULL;
            }
        }

        ClassGuid++;
    }

    //
    // Sort the list
    //
    ListView_SortItems(hwndClassList, (PFNLVCOMPARE)ClassListCompare, NULL);

    //
    // if previous selection wasn't found select first in list.
    //
    if (IsEqualGUID(&NewDevWiz->lvClassGuidSelected, &GUID_NULL)) {
        
        lvIndex = 0;
        ListView_SetItemState(hwndClassList,
                              lvIndex,
                              LVIS_SELECTED|LVIS_FOCUSED,
                              LVIS_SELECTED|LVIS_FOCUSED
                              );
    }

    //
    // previous selection was found, fetch its current index
    //
    else {
        
        lvIndex = ListView_GetNextItem(hwndClassList,
                                       -1,
                                       LVNI_SELECTED
                                       );
    }


    //
    // scroll the selected item into view.
    //
    ListView_EnsureVisible(hwndClassList, lvIndex, FALSE);
    ListView_SetColumnWidth(hwndClassList, 0, LVSCW_AUTOSIZE_USEHEADER);

    SendMessage(hwndClassList, WM_SETREDRAW, TRUE, 0L);
}

INT_PTR CALLBACK
NDW_PickClassDlgProc(
    HWND hDlg, 
    UINT wMsg, 
    WPARAM wParam, 
    LPARAM lParam
    )
{
    HWND hwndClassList = GetDlgItem(hDlg, IDC_NDW_PICKCLASS_CLASSLIST);
    HWND hwndParentDlg = GetParent(hDlg);
    PNEWDEVWIZ NewDevWiz = (PNEWDEVWIZ)GetWindowLongPtr(hDlg, DWLP_USER);

    switch (wMsg) {
       
    case WM_INITDIALOG: {
           
        LPPROPSHEETPAGE lppsp = (LPPROPSHEETPAGE)lParam;
        LV_COLUMN lvcCol;

        NewDevWiz = (PNEWDEVWIZ)lppsp->lParam;
        SetWindowLongPtr(hDlg, DWLP_USER, (LONG_PTR)NewDevWiz);
        SetDlgText(hDlg, IDC_NDW_TEXT, IDS_NDW_PICKCLASS1, IDS_NDW_PICKCLASS1);

        //
        // Get the Class Icon Image Lists.  We do this only the first
        // time this dialog is initialized.
        //
        if (NewDevWiz->ClassImageList.cbSize) {
            ListView_SetImageList(hwndClassList,
                                  NewDevWiz->ClassImageList.ImageList,
                                  LVSIL_SMALL
                                  );
        }

        //
        // Insert a column for the class list
        //
        lvcCol.mask = LVCF_FMT | LVCF_WIDTH;
        lvcCol.fmt = LVCFMT_LEFT;
        lvcCol.iSubItem = 0;
        ListView_InsertColumn(hwndClassList, 0, (LV_COLUMN FAR *)&lvcCol);

        //
        // Save the class before the user chooses one. This will be restored
        // in the event the install is cancelled.
        //
        NewDevWiz->SavedClassGuid = NewDevWiz->DeviceInfoData.ClassGuid;

        break;
    }


    case WM_DESTROY:
        break;

    case WM_NOTIFY:
        switch (((NMHDR FAR *)lParam)->code) {

        //
        // This dialog is being activated.  Each time we are activated
        // we free up the current DeviceInfo and create a new one. Although
        // inefficient, its necessary to reenumerate the class list.
        //
        case PSN_SETACTIVE:

            PropSheet_SetWizButtons(hwndParentDlg, PSWIZB_BACK | PSWIZB_NEXT);
            NewDevWiz->PrevPage = IDD_NEWDEVWIZ_SELECTCLASS;

            //
            // If we have DeviceInfo from going forward delete it.
            //
            if (NewDevWiz->ClassGuidSelected) {

                SetClassGuid(NewDevWiz->hDeviceInfo,
                             &NewDevWiz->DeviceInfoData,
                             &NewDevWiz->SavedClassGuid
                             );
            }

            NewDevWiz->ClassGuidSelected = NULL;

            NdwBuildClassInfoList(NewDevWiz, 0);
            InitNDW_PickClassDlg(hwndClassList, NewDevWiz);
            if (NewDevWiz->InstallType == NDWTYPE_FOUNDNEW) {
                SetTimer(hDlg, INSTALL_COMPLETE_CHECK_TIMERID, INSTALL_COMPLETE_CHECK_TIMEOUT, NULL);
            }
            break;

        case PSN_RESET:
            KillTimer(hDlg, INSTALL_COMPLETE_CHECK_TIMERID);
            SetClassGuid(NewDevWiz->hDeviceInfo,
                         &NewDevWiz->DeviceInfoData,
                         &NewDevWiz->SavedClassGuid
                         );
            break;



        case PSN_WIZBACK:
            NewDevWiz->PrevPage = IDD_NEWDEVWIZ_SELECTCLASS;
               
            if (NewDevWiz->EnterInto == IDD_NEWDEVWIZ_SELECTCLASS) {
                   
                SetDlgMsgResult(hDlg, wMsg, NewDevWiz->EnterFrom);
            }
            KillTimer(hDlg, INSTALL_COMPLETE_CHECK_TIMERID);
            break;



        case PSN_WIZNEXT: {
               
            LPGUID  ClassGuidSelected;

            SetDlgMsgResult(hDlg, wMsg, IDD_NEWDEVWIZ_SELECTDEVICE);

            KillTimer(hDlg, INSTALL_COMPLETE_CHECK_TIMERID);

            if (IsEqualGUID(&NewDevWiz->lvClassGuidSelected, &GUID_NULL)) {
                   
                NewDevWiz->ClassGuidSelected = NULL;
                break;
            }

            ClassGuidSelected = &NewDevWiz->lvClassGuidSelected;
            NewDevWiz->ClassGuidSelected = ClassGuidSelected;

            //
            // Add a new element to the DeviceInfo from the GUID and class name
            //
            NewDevWiz->DeviceInfoData.cbSize = sizeof(SP_DEVINFO_DATA);

            if (!SetupDiGetClassDescription(NewDevWiz->ClassGuidSelected,
                                            NewDevWiz->ClassDescription,
                                            SIZECHARS(NewDevWiz->ClassDescription),
                                            NULL
                                            )
                ||
                !SetupDiClassNameFromGuid(NewDevWiz->ClassGuidSelected,
                                          NewDevWiz->ClassName,
                                          SIZECHARS(NewDevWiz->ClassName),
                                          NULL
                                          ))
            {
                // unhandled error!
                NewDevWiz->ClassGuidSelected = NULL;
                break;
            }

            if (IsEqualGUID(NewDevWiz->ClassGuidSelected, &GUID_DEVCLASS_UNKNOWN)) {
                   
                ClassGuidSelected = (LPGUID)&GUID_NULL;
            }


            SetClassGuid(NewDevWiz->hDeviceInfo,
                         &NewDevWiz->DeviceInfoData,
                         ClassGuidSelected
                         );

            break;
        }

        case NM_DBLCLK:
            PropSheet_PressButton(hwndParentDlg, PSBTN_NEXT);
            break;

        case LVN_ITEMCHANGED: {
               
            LPNM_LISTVIEW   lpnmlv = (LPNM_LISTVIEW)lParam;

            if ((lpnmlv->uChanged & LVIF_STATE)) {
                   
                if (lpnmlv->uNewState & LVIS_SELECTED) {
                       
                    NewDevWiz->lvClassGuidSelected = *((LPGUID)lpnmlv->lParam);
                }
                   
                else if (IsEqualGUID((LPGUID)lpnmlv->lParam,
                                        &NewDevWiz->lvClassGuidSelected
                                        ))
                {
                    NewDevWiz->lvClassGuidSelected = GUID_NULL;
                }
            }

            break;
        }
        }
        break;


    case WM_SYSCOLORCHANGE:
        _OnSysColorChange(hDlg, wParam, lParam);

        //
        // Update the ImageList Background color
        //
        ImageList_SetBkColor((HIMAGELIST)SendMessage(GetDlgItem(hDlg, IDC_NDW_PICKCLASS_CLASSLIST), LVM_GETIMAGELIST, (WPARAM)(LVSIL_SMALL), 0L),
                                GetSysColor(COLOR_WINDOW));
        break;

    case WM_TIMER:
        if (INSTALL_COMPLETE_CHECK_TIMERID == wParam) {
            if (IsInstallComplete(NewDevWiz->hDeviceInfo, &NewDevWiz->DeviceInfoData)) {
                PropSheet_PressButton(GetParent(hDlg), PSBTN_CANCEL);
            }
        }
        break;

    default:
        return(FALSE);
    }

    return(TRUE);
}

//
// The real select device page is in setupapi.  his page is a blank page which 
// never shows its face to have a consistent place to jump to when the class 
// is known.
//
INT_PTR CALLBACK
NDW_SelectDeviceDlgProc(
    HWND hDlg, 
    UINT wMsg, 
    WPARAM wParam, 
    LPARAM lParam
    )
{
    HWND hwndParentDlg = GetParent(hDlg);
    PNEWDEVWIZ NewDevWiz = (PNEWDEVWIZ)GetWindowLongPtr(hDlg, DWLP_USER);

    UNREFERENCED_PARAMETER(wParam);

    switch (wMsg) {
       
    case WM_INITDIALOG: {
           
        LPPROPSHEETPAGE lppsp = (LPPROPSHEETPAGE)lParam;
        NewDevWiz = (PNEWDEVWIZ)lppsp->lParam;
        SetWindowLongPtr(hDlg, DWLP_USER, (LONG_PTR)NewDevWiz);
        break;
    }

    case WM_DESTROY:
        break;


    case WM_NOTIFY:
        switch (((NMHDR FAR *)lParam)->code) {

        case PSN_SETACTIVE: {
            int PrevPage, BackUpPage;

            PrevPage = NewDevWiz->PrevPage;
            NewDevWiz->PrevPage = IDD_NEWDEVWIZ_SELECTDEVICE;
            BackUpPage = NewDevWiz->EnterInto == IDD_NEWDEVWIZ_SELECTDEVICE
                           ? NewDevWiz->EnterFrom : IDD_NEWDEVWIZ_SELECTCLASS;

            if (!NewDevWiz->ClassGuidSelected || PrevPage == IDD_WIZARDEXT_SELECT) {

                //
                // going backwards, cleanup and backup
                //
                SetupDiSetSelectedDriver(NewDevWiz->hDeviceInfo,
                                         &NewDevWiz->DeviceInfoData,
                                         NULL
                                         );

                SetupDiDestroyDriverInfoList(NewDevWiz->hDeviceInfo,
                                             &NewDevWiz->DeviceInfoData,
                                             SPDIT_COMPATDRIVER
                                             );

                SetupDiDestroyDriverInfoList(NewDevWiz->hDeviceInfo,
                                             &NewDevWiz->DeviceInfoData,
                                             SPDIT_CLASSDRIVER
                                             );

                //
                // Cleanup the WizExtSelect Page
                //
                if (NewDevWiz->WizExtSelect.hPropSheet) {
                       
                    PropSheet_RemovePage(GetParent(hDlg),
                                         (WPARAM)-1,
                                         NewDevWiz->WizExtSelect.hPropSheet
                                         );
                }

                SetDlgMsgResult(hDlg, wMsg, BackUpPage);
                break;
            }


            //
            // Set the Cursor to an Hourglass
            //
            SetCursor(LoadCursor(NULL, IDC_WAIT));

            NewDevWiz->WizExtSelect.hPropSheet = CreateWizExtPage(IDD_WIZARDEXT_SELECT,
                                                                  WizExtSelectDlgProc,
                                                                  NewDevWiz
                                                                  );
            
            if (NewDevWiz->WizExtSelect.hPropSheet) {
                   
                PropSheet_AddPage(hwndParentDlg, NewDevWiz->WizExtSelect.hPropSheet);
                SetDlgMsgResult(hDlg, wMsg, IDD_WIZARDEXT_SELECT);
            }

            else {
                
                SetDlgMsgResult(hDlg, wMsg, BackUpPage);
            }

            break;
        }

        }
        break;

    default:
        return(FALSE);
    }

    return(TRUE);
}

INT_PTR CALLBACK
WizExtSelectDlgProc(
    HWND hDlg, 
    UINT wMsg, 
    WPARAM wParam, 
    LPARAM lParam
    )
{
    HWND hwndParentDlg = GetParent(hDlg);
    PNEWDEVWIZ NewDevWiz = (PNEWDEVWIZ )GetWindowLongPtr(hDlg, DWLP_USER);
    int PrevPageId;
    PSP_INSTALLWIZARD_DATA  InstallWizard;

    UNREFERENCED_PARAMETER(wParam);

    switch (wMsg) {
       
    case WM_INITDIALOG: {
           
        LPPROPSHEETPAGE lppsp = (LPPROPSHEETPAGE)lParam;
        NewDevWiz = (PNEWDEVWIZ )lppsp->lParam;
        SetWindowLongPtr(hDlg, DWLP_USER, (LONG_PTR)NewDevWiz);
        break;
    }

    case WM_DESTROY:
        break;


    case WM_NOTIFY:
       
        switch (((NMHDR FAR *)lParam)->code) {
           
        case PSN_SETACTIVE:

            PrevPageId = NewDevWiz->PrevPage;
            NewDevWiz->PrevPage = IDD_WIZARDEXT_SELECT;

            if (PrevPageId == IDD_NEWDEVWIZ_SELECTDEVICE) {
            
                SP_DEVINSTALL_PARAMS  DeviceInstallParams;

                //
                // Moving forward on first page
                //
                // Prepare to call the class installer, for class install wizard pages.
                // and Add in setup's SelectDevice wizard page.
                //
                InstallWizard = &NewDevWiz->InstallDynaWiz;
                memset(InstallWizard, 0, sizeof(SP_INSTALLWIZARD_DATA));
                InstallWizard->ClassInstallHeader.InstallFunction = DIF_INSTALLWIZARD;
                InstallWizard->ClassInstallHeader.cbSize = sizeof(SP_CLASSINSTALL_HEADER);
                InstallWizard->hwndWizardDlg = GetParent(hDlg);

                if (!SetupDiSetClassInstallParams(NewDevWiz->hDeviceInfo,
                                                  &NewDevWiz->DeviceInfoData,
                                                  &InstallWizard->ClassInstallHeader,
                                                  sizeof(SP_INSTALLWIZARD_DATA)
                                                  ))
                {
                    SetDlgMsgResult(hDlg, wMsg, IDD_WIZARDEXT_SELECT);
                    break;
                }


                SetupDiSetSelectedDriver(NewDevWiz->hDeviceInfo,
                                         &NewDevWiz->DeviceInfoData,
                                         NULL
                                         );

                SetupDiDestroyDriverInfoList(NewDevWiz->hDeviceInfo,
                                             &NewDevWiz->DeviceInfoData,
                                             SPDIT_COMPATDRIVER
                                             );

                SetupDiDestroyDriverInfoList(NewDevWiz->hDeviceInfo,
                                             &NewDevWiz->DeviceInfoData,
                                             SPDIT_CLASSDRIVER
                                             );
                
                //
                // Get current DeviceInstall parameters, and then set the fields
                // we wanted changed from default
                //
                DeviceInstallParams.cbSize = sizeof(SP_DEVINSTALL_PARAMS);
                  
                if (!SetupDiGetDeviceInstallParams(NewDevWiz->hDeviceInfo,
                                                   &NewDevWiz->DeviceInfoData,
                                                   &DeviceInstallParams
                                                   ))
                {
                    SetDlgMsgResult(hDlg, wMsg, IDD_WIZARDEXT_SELECT);
                    break;
                }


                DeviceInstallParams.Flags |= DI_SHOWCLASS | DI_SHOWCOMPAT | DI_SHOWOEM | DI_CLASSINSTALLPARAMS;

                DeviceInstallParams.DriverPath[0] = TEXT('\0');

                if (IsEqualGUID(NewDevWiz->ClassGuidSelected, &GUID_DEVCLASS_UNKNOWN)) {
                      
                    DeviceInstallParams.FlagsEx &= ~DI_FLAGSEX_FILTERCLASSES;
                }
                  
                else {
                
                    DeviceInstallParams.FlagsEx |= DI_FLAGSEX_FILTERCLASSES;
                }

                //
                // Check to see if we should show all class drivers or only similar
                // drivers for this device.
                //
                if (GetBusInformation(NewDevWiz->DeviceInfoData.DevInst) & BIF_SHOWSIMILARDRIVERS) {
                    
                    DeviceInstallParams.FlagsEx |= DI_FLAGSEX_FILTERSIMILARDRIVERS;
                
                } else {

                    DeviceInstallParams.FlagsEx &= ~DI_FLAGSEX_FILTERSIMILARDRIVERS;
                }

                DeviceInstallParams.hwndParent = hwndParentDlg;
                if (!SetupDiSetDeviceInstallParams(NewDevWiz->hDeviceInfo,
                                                   &NewDevWiz->DeviceInfoData,
                                                   &DeviceInstallParams
                                                   ))
                {
                    SetDlgMsgResult(hDlg, wMsg, IDD_WIZARDEXT_SELECT);
                    break;
                }

                InstallWizard->DynamicPageFlags = 0;
                NewDevWiz->SelectDevicePage = NULL;

                NewDevWiz->SelectDevicePage = SetupDiGetWizardPage(NewDevWiz->hDeviceInfo,
                                                                   &NewDevWiz->DeviceInfoData,
                                                                   InstallWizard,
                                                                   SPWPT_SELECTDEVICE,
                                                                   SPWP_USE_DEVINFO_DATA
                                                                   );

                PropSheet_AddPage(hwndParentDlg, NewDevWiz->SelectDevicePage);

                //
                // Clear the class install parameters.
                //
                SetupDiSetClassInstallParams(NewDevWiz->hDeviceInfo,
                                             &NewDevWiz->DeviceInfoData,
                                             NULL,
                                             0
                                             );

                //
                // Add the end page, which is the select end page.
                //
                NewDevWiz->WizExtSelect.hPropSheetEnd = CreateWizExtPage(IDD_WIZARDEXT_SELECT_END,
                                                                          WizExtSelectEndDlgProc,
                                                                          NewDevWiz
                                                                          );

                PropSheet_AddPage(hwndParentDlg, NewDevWiz->WizExtSelect.hPropSheetEnd);

                PropSheet_PressButton(hwndParentDlg, PSBTN_NEXT);

            }

            else {
                //
                // Moving backwards on first page
                //
                // Clean up proppages added.
                //
                if (NewDevWiz->SelectDevicePage) {
                      
                    PropSheet_RemovePage(hwndParentDlg,
                                         (WPARAM)-1,
                                         NewDevWiz->SelectDevicePage
                                         );
                      
                    NewDevWiz->SelectDevicePage = NULL;
                }


                if (NewDevWiz->WizExtSelect.hPropSheetEnd) {
                      
                    PropSheet_RemovePage(hwndParentDlg,
                                         (WPARAM)-1,
                                         NewDevWiz->WizExtSelect.hPropSheetEnd
                                         );
                      
                    NewDevWiz->WizExtSelect.hPropSheetEnd = NULL;
                }

                //
                // Jump back
                //
                SetDlgMsgResult(hDlg, wMsg, IDD_NEWDEVWIZ_SELECTDEVICE);
            }
            break;

        case PSN_WIZNEXT:
          SetDlgMsgResult(hDlg, wMsg, 0);
          break;
        }
        break;

    default:
        return(FALSE);
    }

    return(TRUE);
}

INT_PTR CALLBACK
WizExtSelectEndDlgProc(
    HWND hDlg, 
    UINT wMsg, 
    WPARAM wParam, 
    LPARAM lParam
    )
{
    HWND hwndParentDlg = GetParent(hDlg);
    PNEWDEVWIZ NewDevWiz = (PNEWDEVWIZ )GetWindowLongPtr(hDlg, DWLP_USER);
    int PrevPageId;

    UNREFERENCED_PARAMETER(wParam);

    switch (wMsg) {
       
    case WM_INITDIALOG: {
           
        LPPROPSHEETPAGE lppsp = (LPPROPSHEETPAGE)lParam;
        NewDevWiz = (PNEWDEVWIZ )lppsp->lParam;
        SetWindowLongPtr(hDlg, DWLP_USER, (LONG_PTR)NewDevWiz);
        break;
    }

    case WM_DESTROY:
        break;


   case WM_NOTIFY:
       
       switch (((NMHDR FAR *)lParam)->code) {
           
       case PSN_SETACTIVE:

           PrevPageId = NewDevWiz->PrevPage;
           NewDevWiz->PrevPage = IDD_WIZARDEXT_SELECT_END;

           if (PrevPageId == IDD_WIZARDEXT_SELECT) {
               //
               // Moving forward 
               //
               SetDlgMsgResult(hDlg, wMsg, IDD_NEWDEVWIZ_INSTALLDEV);

           } else {
                //
                // Moving backwards 
                //
                // Clean up proppages added.
                //
                if (NewDevWiz->WizExtSelect.hPropSheetEnd) {
                    
                    PropSheet_RemovePage(hwndParentDlg,
                                         (WPARAM)-1,
                                         NewDevWiz->WizExtSelect.hPropSheetEnd
                                         );
                    NewDevWiz->WizExtSelect.hPropSheetEnd = NULL;
                }

                //
                // Jump back
                //
                NewDevWiz->PrevPage = IDD_WIZARDEXT_SELECT;
                SetDlgMsgResult(hDlg, wMsg, IDD_DYNAWIZ_SELECTDEV_PAGE);
            }

           break;

       case PSN_WIZNEXT:
           SetDlgMsgResult(hDlg, wMsg, 0);
           break;
           
       }
       break;

    default:
        return(FALSE);
    }

    return(TRUE);
}

