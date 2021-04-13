/*++

Copyright (c) Microsoft Corporation.  All rights reserved.

Module Name:

    devinst.c

Abstract:

    Device Installer routines.

Author:

    Lonny McMichael (lonnym) 1-Aug-1995

Revision History:

--*/

#include "precomp.h"
#pragma hdrstop


//
// Private prototypes.
//
DWORD
pSetupDiGetCoInstallerList(
    IN     HDEVINFO                 DeviceInfoSet,     OPTIONAL
    IN     PSP_DEVINFO_DATA         DeviceInfoData,    OPTIONAL
    IN     CONST GUID              *ClassGuid,         OPTIONAL
    IN OUT PDEVINSTALL_PARAM_BLOCK  InstallParamBlock,
    IN OUT PVERIFY_CONTEXT          VerifyContext      OPTIONAL
    );


//
// Private logging data
// these must be mirrored from setupapi.h
//
static LPCTSTR pSetupDiDifStrings[] = {
    NULL, // no DIF code
    TEXT("DIF_SELECTDEVICE"),
    TEXT("DIF_INSTALLDEVICE"),
    TEXT("DIF_ASSIGNRESOURCES"),
    TEXT("DIF_PROPERTIES"),
    TEXT("DIF_REMOVE"),
    TEXT("DIF_FIRSTTIMESETUP"),
    TEXT("DIF_FOUNDDEVICE"),
    TEXT("DIF_SELECTCLASSDRIVERS"),
    TEXT("DIF_VALIDATECLASSDRIVERS"),
    TEXT("DIF_INSTALLCLASSDRIVERS"),
    TEXT("DIF_CALCDISKSPACE"),
    TEXT("DIF_DESTROYPRIVATEDATA"),
    TEXT("DIF_VALIDATEDRIVER"),
    TEXT("DIF_MOVEDEVICE"),  // obsolete
    TEXT("DIF_DETECT"),
    TEXT("DIF_INSTALLWIZARD"),
    TEXT("DIF_DESTROYWIZARDDATA"),
    TEXT("DIF_PROPERTYCHANGE"),
    TEXT("DIF_ENABLECLASS"),
    TEXT("DIF_DETECTVERIFY"),
    TEXT("DIF_INSTALLDEVICEFILES"),
    TEXT("DIF_UNREMOVE"),
    TEXT("DIF_SELECTBESTCOMPATDRV"),
    TEXT("DIF_ALLOW_INSTALL"),
    TEXT("DIF_REGISTERDEVICE"),
    TEXT("DIF_NEWDEVICEWIZARD_PRESELECT"),
    TEXT("DIF_NEWDEVICEWIZARD_SELECT"),
    TEXT("DIF_NEWDEVICEWIZARD_PREANALYZE"),
    TEXT("DIF_NEWDEVICEWIZARD_POSTANALYZE"),
    TEXT("DIF_NEWDEVICEWIZARD_FINISHINSTALL"),
    TEXT("DIF_UNUSED1"),
    TEXT("DIF_INSTALLINTERFACES"),
    TEXT("DIF_DETECTCANCEL"),
    TEXT("DIF_REGISTER_COINSTALLERS"),
    TEXT("DIF_ADDPROPERTYPAGE_ADVANCED"),
    TEXT("DIF_ADDPROPERTYPAGE_BASIC"),
    TEXT("DIF_RESERVED1"),  // aka, DIF_GETWINDOWSUPDATEINFO
    TEXT("DIF_TROUBLESHOOTER"),
    TEXT("DIF_POWERMESSAGEWAKE"),
    TEXT("DIF_ADDREMOTEPROPERTYPAGE_ADVANCED"),
    TEXT("DIF_UPDATEDRIVER_UI"),
    TEXT("DIF_RESERVED2")   // aka, DIF_INTERFACE_TO_DEVICE
    //
    // append new DIF codes here (don't forget comma's)
    //
};

DWORD FilterLevelOnInstallerError(
    IN DWORD PrevLevel,
    IN DWORD Err)
/*++

Routine Description:

    Allow downgrading of error level depending on returned error
    from class/co/default installer
    and current state

Arguments:

    PrevLevel - initial level
    Err       - error to check

Return Value:

    New level

--*/
{
    DWORD Level = PrevLevel;
    if(Level == DRIVER_LOG_ERROR) {
        switch(Err) {
            case ERROR_DUPLICATE_FOUND:
                //
                // not an error as such
                //
                Level = DRIVER_LOG_WARNING;
                break;

            case ERROR_REQUIRES_INTERACTIVE_WINDOWSTATION:
                //
                // if returned during gui-setup
                // or during server-side setup
                // demote error to warning
                //
                if(GuiSetupInProgress ||
                              (GlobalSetupFlags & PSPGF_NONINTERACTIVE)) {

                    Level = DRIVER_LOG_WARNING;
                }
                break;
        }
    }
    return Level;
}


//
// ANSI version
//
BOOL
WINAPI
SetupDiGetDeviceInstallParamsA(
    IN  HDEVINFO                DeviceInfoSet,
    IN  PSP_DEVINFO_DATA        DeviceInfoData,          OPTIONAL
    OUT PSP_DEVINSTALL_PARAMS_A DeviceInstallParams
    )
{
    SP_DEVINSTALL_PARAMS_W deviceInstallParams;
    DWORD rc;

    try {

        deviceInstallParams.cbSize = sizeof(SP_DEVINSTALL_PARAMS_W);

        rc = GLE_FN_CALL(FALSE,
                         SetupDiGetDeviceInstallParamsW(DeviceInfoSet,
                                                        DeviceInfoData,
                                                        &deviceInstallParams)
                        );

        if(rc != NO_ERROR) {
            leave;
        }

        rc = pSetupDiDevInstParamsUnicodeToAnsi(&deviceInstallParams,
                                                DeviceInstallParams
                                               );

    } except(pSetupExceptionFilter(GetExceptionCode())) {
        pSetupExceptionHandler(GetExceptionCode(), ERROR_INVALID_PARAMETER, &rc);
    }

    SetLastError(rc);
    return (rc == NO_ERROR);
}


BOOL
WINAPI
SetupDiGetDeviceInstallParams(
    IN  HDEVINFO              DeviceInfoSet,
    IN  PSP_DEVINFO_DATA      DeviceInfoData,          OPTIONAL
    OUT PSP_DEVINSTALL_PARAMS DeviceInstallParams
    )
/*++

Routine Description:

    This routine retrieves installation parameters for a device information set
    (globally), or a particular device information element.

Arguments:

    DeviceInfoSet - Supplies a handle to the device information set containing
        installation parameters to be retrieved.

    DeviceInfoData - Optionally, supplies the address of a SP_DEVINFO_DATA
        structure containing installation parameters to be retrieved.  If this
        parameter is not specified, then the installation parameters retrieved
        will be associated with the device information set itself (for the
        global class driver list).

    DeviceInstallParams - Supplies the address of a SP_DEVINSTALL_PARAMS
        structure that will receive the installation parameters.  The cbSize
        field of this structure must be set to the size, in bytes, of a
        SP_DEVINSTALL_PARAMS structure before calling this API.

Return Value:

    If the function succeeds, the return value is TRUE.
    If the function fails, the return value is FALSE.  To get extended error
    information, call GetLastError.

--*/

{
    PDEVICE_INFO_SET pDeviceInfoSet = NULL;
    DWORD Err;
    PDEVINFO_ELEM DevInfoElem;

    try {

        if(!(pDeviceInfoSet = AccessDeviceInfoSet(DeviceInfoSet))) {
            Err = ERROR_INVALID_HANDLE;
            leave;
        }

        if(DeviceInfoData) {
            //
            // Then we are to retrieve installation parameters for a particular
            // device.
            //
            if(!(DevInfoElem = FindAssociatedDevInfoElem(pDeviceInfoSet,
                                                         DeviceInfoData,
                                                         NULL))) {
                Err = ERROR_INVALID_PARAMETER;
            } else {
                Err = GetDevInstallParams(pDeviceInfoSet,
                                          &(DevInfoElem->InstallParamBlock),
                                          DeviceInstallParams
                                         );
            }

        } else {
            //
            // Retrieve installation parameters for the global class driver list.
            //
            Err = GetDevInstallParams(pDeviceInfoSet,
                                      &(pDeviceInfoSet->InstallParamBlock),
                                      DeviceInstallParams
                                     );
        }

    } except(pSetupExceptionFilter(GetExceptionCode())) {
        pSetupExceptionHandler(GetExceptionCode(), ERROR_INVALID_PARAMETER, &Err);
    }

    if(pDeviceInfoSet) {
        UnlockDeviceInfoSet(pDeviceInfoSet);
    }

    SetLastError(Err);
    return(Err == NO_ERROR);
}


//
// ANSI version
//
BOOL
WINAPI
SetupDiGetClassInstallParamsA(
    IN  HDEVINFO                DeviceInfoSet,
    IN  PSP_DEVINFO_DATA        DeviceInfoData,         OPTIONAL
    OUT PSP_CLASSINSTALL_HEADER ClassInstallParams,     OPTIONAL
    IN  DWORD                   ClassInstallParamsSize,
    OUT PDWORD                  RequiredSize            OPTIONAL
    )
{
    PDEVICE_INFO_SET pDeviceInfoSet = NULL;
    PDEVINFO_ELEM DevInfoElem;
    PDEVINSTALL_PARAM_BLOCK InstallParamBlock;
    DI_FUNCTION Function;
    DWORD Err;

    try {

        if(!(pDeviceInfoSet = AccessDeviceInfoSet(DeviceInfoSet))) {
            Err = ERROR_INVALID_HANDLE;
            leave;
        }

        Err = NO_ERROR; // assume success

        if(DeviceInfoData) {
            //
            // Then we are to retrieve installation parameters for a particular
            // device.
            //
            if(DevInfoElem = FindAssociatedDevInfoElem(pDeviceInfoSet,DeviceInfoData,NULL)) {
                InstallParamBlock = &DevInfoElem->InstallParamBlock;
            } else {
                Err = ERROR_INVALID_PARAMETER;
                leave;
            }
        } else {
            //
            // Retrieve installation parameters for the global class driver
            // list.
            //
            InstallParamBlock = &pDeviceInfoSet->InstallParamBlock;
        }

        //
        // While we're in a try/except, go ahead and do some preliminary
        // validation on the caller-supplied buffer...
        //
        if(ClassInstallParams) {

            if((ClassInstallParamsSize < sizeof(SP_CLASSINSTALL_HEADER)) ||
               (ClassInstallParams->cbSize != sizeof(SP_CLASSINSTALL_HEADER))) {

                Err = ERROR_INVALID_USER_BUFFER;
                leave;
            }

        } else if(ClassInstallParamsSize) {
            Err = ERROR_INVALID_USER_BUFFER;
            leave;
        }

        MYASSERT(InstallParamBlock);

        if(InstallParamBlock->ClassInstallHeader) {
            Function = InstallParamBlock->ClassInstallHeader->InstallFunction;
        } else {
            Err = ERROR_NO_CLASSINSTALL_PARAMS;
            leave;
        }

        //
        // For DIF_SELECTDEVICE we need special processing since the structure
        // that goes with it is ansi/unicode specific.
        //
        if(Function == DIF_SELECTDEVICE) {

            SP_SELECTDEVICE_PARAMS_W SelectDeviceParams;

            SelectDeviceParams.ClassInstallHeader.cbSize = sizeof(SP_CLASSINSTALL_HEADER);

            Err = GLE_FN_CALL(FALSE,
                              SetupDiGetClassInstallParamsW(
                                  DeviceInfoSet,
                                  DeviceInfoData,
                                  (PSP_CLASSINSTALL_HEADER)&SelectDeviceParams,
                                  sizeof(SP_SELECTDEVICE_PARAMS_W),
                                  NULL)
                             );

            if(Err == NO_ERROR) {
                //
                // We successfully retrieved the Unicode form of the Select
                // Device parameters.  Store the required size for the ANSI
                // version in the output parameter (if requested).
                //
                if(RequiredSize) {
                    *RequiredSize = sizeof(SP_SELECTDEVICE_PARAMS_A);
                }

                if(ClassInstallParamsSize < sizeof(SP_SELECTDEVICE_PARAMS_A)) {
                    Err = ERROR_INSUFFICIENT_BUFFER;
                } else {
                    Err = pSetupDiSelDevParamsUnicodeToAnsi(
                              &SelectDeviceParams,
                              (PSP_SELECTDEVICE_PARAMS_A)ClassInstallParams
                              );
                }
            }

        } else {

            Err = GLE_FN_CALL(FALSE,
                              SetupDiGetClassInstallParamsW(
                                  DeviceInfoSet,
                                  DeviceInfoData,
                                  ClassInstallParams,
                                  ClassInstallParamsSize,
                                  RequiredSize)
                             );
        }

    } except(pSetupExceptionFilter(GetExceptionCode())) {
        pSetupExceptionHandler(GetExceptionCode(), ERROR_INVALID_PARAMETER, &Err);
    }

    if(pDeviceInfoSet) {
        UnlockDeviceInfoSet(pDeviceInfoSet);
    }

    SetLastError(Err);
    return (Err == NO_ERROR);
}


BOOL
WINAPI
SetupDiGetClassInstallParams(
    IN  HDEVINFO                DeviceInfoSet,
    IN  PSP_DEVINFO_DATA        DeviceInfoData,         OPTIONAL
    OUT PSP_CLASSINSTALL_HEADER ClassInstallParams,     OPTIONAL
    IN  DWORD                   ClassInstallParamsSize,
    OUT PDWORD                  RequiredSize            OPTIONAL
    )
/*++

Routine Description:

    This routine retrieves class installer parameters for a device information
    set (globally), or a particular device information element.  These
    parameters are specific to a particular device installer function code
    (DI_FUNCTION) that will be stored in the ClassInstallHeader field located
    at the beginning of the parameter buffer.

Arguments:

    DeviceInfoSet - Supplies a handle to the device information set containing
        class installer parameters to be retrieved.

    DeviceInfoData - Optionally, supplies the address of a SP_DEVINFO_DATA
        structure containing class installer parameters to be retrieved.  If
        this parameter is not specified, then the class installer parameters
        retrieved will be associated with the device information set itself
        (for the global class driver list).

    ClassInstallParams - Optionally, supplies the address of a buffer
        containing a class install header structure.  This structure must have
        its cbSize field set to sizeof(SP_CLASSINSTALL_HEADER) on input, or the
        buffer is considered to be invalid.  On output, the InstallFunction
        field will be filled in with the DI_FUNCTION code for the class install
        parameters being retrieved, and if the buffer is large enough, it will
        receive the class installer parameters structure specific to that
        function code.

        If this parameter is not specified, then ClassInstallParamsSize must be
        zero.  This would be done if the caller simply wants to determine how
        large a buffer is required.

    ClassInstallParamsSize - Supplies the size, in bytes, of the
        ClassInstallParams buffer, or zero, if ClassInstallParams is not
        supplied.  If the buffer is supplied, it must be _at least_ as large as
        sizeof(SP_CLASSINSTALL_HEADER).

    RequiredSize - Optionally, supplies the address of a variable that receives
        the number of bytes required to store the class installer parameters.

Return Value:

    If the function succeeds, the return value is TRUE.
    If the function fails, the return value is FALSE.  To get extended error
    information, call GetLastError.

--*/

{
    PDEVICE_INFO_SET pDeviceInfoSet = NULL;
    DWORD Err;
    PDEVINFO_ELEM DevInfoElem;

    try {

        if(!(pDeviceInfoSet = AccessDeviceInfoSet(DeviceInfoSet))) {
            Err = ERROR_INVALID_HANDLE;
            leave;
        }

        if(DeviceInfoData) {
            //
            // Then we are to retrieve installation parameters for a particular
            // device.
            //
            if(!(DevInfoElem = FindAssociatedDevInfoElem(pDeviceInfoSet,
                                                         DeviceInfoData,
                                                         NULL))) {
                Err = ERROR_INVALID_PARAMETER;
            } else {
                Err = GetClassInstallParams(&(DevInfoElem->InstallParamBlock),
                                            ClassInstallParams,
                                            ClassInstallParamsSize,
                                            RequiredSize
                                           );
            }
        } else {
            //
            // Retrieve installation parameters for the global class driver
            // list.
            //
            Err = GetClassInstallParams(&(pDeviceInfoSet->InstallParamBlock),
                                        ClassInstallParams,
                                        ClassInstallParamsSize,
                                        RequiredSize
                                       );
        }

    } except(pSetupExceptionFilter(GetExceptionCode())) {
        pSetupExceptionHandler(GetExceptionCode(), ERROR_INVALID_PARAMETER, &Err);
    }

    if(pDeviceInfoSet) {
        UnlockDeviceInfoSet(pDeviceInfoSet);
    }

    SetLastError(Err);
    return (Err == NO_ERROR);
}


DWORD
_SetupDiSetDeviceInstallParams(
    IN HDEVINFO              DeviceInfoSet,
    IN PSP_DEVINFO_DATA      DeviceInfoData,     OPTIONAL
    IN PSP_DEVINSTALL_PARAMS DeviceInstallParams,
    IN BOOL                  MsgHandlerIsNativeCharWidth
    )
{
    PDEVICE_INFO_SET pDeviceInfoSet = NULL;
    DWORD Err;
    PDEVINFO_ELEM DevInfoElem;

    try {

        if(!(pDeviceInfoSet = AccessDeviceInfoSet(DeviceInfoSet))) {
            Err = ERROR_INVALID_HANDLE;
            leave;
        }

        if(DeviceInfoData) {
            //
            // Then we are to set installation parameters for a particular
            // device.
            //
            if(!(DevInfoElem = FindAssociatedDevInfoElem(pDeviceInfoSet,
                                                         DeviceInfoData,
                                                         NULL))) {
                Err = ERROR_INVALID_PARAMETER;
            } else {
                Err = SetDevInstallParams(pDeviceInfoSet,
                                          DeviceInstallParams,
                                          &(DevInfoElem->InstallParamBlock),
                                          MsgHandlerIsNativeCharWidth
                                         );
            }

        } else {
            //
            // Set installation parameters for the global class driver list.
            //
            Err = SetDevInstallParams(pDeviceInfoSet,
                                      DeviceInstallParams,
                                      &(pDeviceInfoSet->InstallParamBlock),
                                      MsgHandlerIsNativeCharWidth
                                     );
        }

    } except(pSetupExceptionFilter(GetExceptionCode())) {
        pSetupExceptionHandler(GetExceptionCode(), ERROR_INVALID_PARAMETER, &Err);
    }

    if(pDeviceInfoSet) {
        UnlockDeviceInfoSet(pDeviceInfoSet);
    }

    return Err;
}


//
// ANSI version
//
BOOL
WINAPI
SetupDiSetDeviceInstallParamsA(
    IN HDEVINFO                DeviceInfoSet,
    IN PSP_DEVINFO_DATA        DeviceInfoData,     OPTIONAL
    IN PSP_DEVINSTALL_PARAMS_A DeviceInstallParams
    )
{
    DWORD Err;
    SP_DEVINSTALL_PARAMS_W UnicodeDeviceInstallParams;

    try {

        Err = pSetupDiDevInstParamsAnsiToUnicode(DeviceInstallParams,
                                                 &UnicodeDeviceInstallParams
                                                );
        if(Err != NO_ERROR) {
            leave;
        }

        Err = _SetupDiSetDeviceInstallParams(DeviceInfoSet,
                                             DeviceInfoData,
                                             &UnicodeDeviceInstallParams,
                                             FALSE
                                            );

    } except(pSetupExceptionFilter(GetExceptionCode())) {
        pSetupExceptionHandler(GetExceptionCode(), ERROR_INVALID_PARAMETER, &Err);
    }

    SetLastError(Err);
    return (Err == NO_ERROR);
}


BOOL
WINAPI
SetupDiSetDeviceInstallParams(
    IN HDEVINFO              DeviceInfoSet,
    IN PSP_DEVINFO_DATA      DeviceInfoData,     OPTIONAL
    IN PSP_DEVINSTALL_PARAMS DeviceInstallParams
    )
/*++

Routine Description:

    This routine sets installation parameters for a device information set
    (globally), or a particular device information element.

Arguments:

    DeviceInfoSet - Supplies a handle to the device information set containing
        installation parameters to be set.

    DeviceInfoData - Optionally, supplies the address of a SP_DEVINFO_DATA
        structure containing installation parameters to be set.  If this
        parameter is not specified, then the installation parameters set
        will be associated with the device information set itself (for the
        global class driver list).

    DeviceInstallParams - Supplies the address of a SP_DEVINSTALL_PARAMS
        structure containing the new values of the parameters.  The cbSize
        field of this structure must be set to the size, in bytes, of the
        structure before calling this API.

Return Value:

    If the function succeeds, the return value is TRUE.
    If the function fails, the return value is FALSE.  To get extended error
    information, call GetLastError.

Remarks:

    All parameters will be validated before any changes are made, so a return
    status of FALSE indicates that no parameters were modified.

--*/

{
    DWORD Err;

    try {

        Err = _SetupDiSetDeviceInstallParams(DeviceInfoSet,
                                             DeviceInfoData,
                                             DeviceInstallParams,
                                             TRUE
                                            );

    } except(pSetupExceptionFilter(GetExceptionCode())) {
        pSetupExceptionHandler(GetExceptionCode(), ERROR_INVALID_PARAMETER, &Err);
    }

    SetLastError(Err);
    return (Err == NO_ERROR);
}


//
// ANSI version
//
BOOL
WINAPI
SetupDiSetClassInstallParamsA(
    IN HDEVINFO                DeviceInfoSet,
    IN PSP_DEVINFO_DATA        DeviceInfoData,        OPTIONAL
    IN PSP_CLASSINSTALL_HEADER ClassInstallParams,    OPTIONAL
    IN DWORD                   ClassInstallParamsSize
    )
{
    DWORD Err;
    DI_FUNCTION Function;
    SP_SELECTDEVICE_PARAMS_W SelectParams;

    try {

        if(!ClassInstallParams) {
            //
            // Just pass it on to the unicode version since there's no thunking
            // to do. Note that the size must be 0.
            //
            if(ClassInstallParamsSize) {
                Err = ERROR_INVALID_PARAMETER;
                leave;
            }

            Err = GLE_FN_CALL(FALSE,
                              SetupDiSetClassInstallParamsW(
                                  DeviceInfoSet,
                                  DeviceInfoData,
                                  ClassInstallParams,
                                  ClassInstallParamsSize)
                             );

        } else {

            if(ClassInstallParams->cbSize == sizeof(SP_CLASSINSTALL_HEADER)) {
                Function = ClassInstallParams->InstallFunction;
            } else {
                //
                // Structure is invalid.
                //
                Err = ERROR_INVALID_PARAMETER;
                leave;
            }

            //
            // DIF_SELECTDEVICE is a special case since it has a structure that
            // needs to be translated from ansi to unicode.
            //
            // DIF_INTERFACE_TO_DEVICE has unicode structure but ansi not
            // supported (yet) - internal
            //
            // Others can just be passed on to the unicode version with no
            // changes to the parameters.
            //
            if(Function == DIF_SELECTDEVICE) {

                if(ClassInstallParamsSize >= sizeof(SP_SELECTDEVICE_PARAMS_A)) {

                    Err = pSetupDiSelDevParamsAnsiToUnicode(
                            (PSP_SELECTDEVICE_PARAMS_A)ClassInstallParams,
                            &SelectParams
                            );

                    if(Err != NO_ERROR) {
                        leave;
                    }

                    Err = GLE_FN_CALL(FALSE,
                                      SetupDiSetClassInstallParamsW(
                                          DeviceInfoSet,
                                          DeviceInfoData,
                                          (PSP_CLASSINSTALL_HEADER)&SelectParams,
                                          sizeof(SP_SELECTDEVICE_PARAMS_W))
                                     );
                } else {
                    Err = ERROR_INVALID_PARAMETER;
                    leave;
                }

            } else if(Function == DIF_INTERFACE_TO_DEVICE) {

                Err = ERROR_INVALID_PARAMETER;
                leave;

            } else {

                Err = GLE_FN_CALL(FALSE,
                                  SetupDiSetClassInstallParamsW(
                                      DeviceInfoSet,
                                      DeviceInfoData,
                                      ClassInstallParams,
                                      ClassInstallParamsSize)
                                 );
            }
        }

        //
        // If we get to here, then we have called
        // SetupDiSetClassInstallParamsW, although the result (stored in Err)
        // may be success or failure.
        //

    } except(pSetupExceptionFilter(GetExceptionCode())) {
        pSetupExceptionHandler(GetExceptionCode(), ERROR_INVALID_PARAMETER, &Err);
    }

    SetLastError(Err);
    return (Err == NO_ERROR);
}


BOOL
WINAPI
SetupDiSetClassInstallParams(
    IN HDEVINFO                DeviceInfoSet,
    IN PSP_DEVINFO_DATA        DeviceInfoData,        OPTIONAL
    IN PSP_CLASSINSTALL_HEADER ClassInstallParams,    OPTIONAL
    IN DWORD                   ClassInstallParamsSize
    )
/*++

Routine Description:

    This routine sets (or clears) class installer parameters for a device
    information set (globally), or a particular device information element.

Arguments:

    DeviceInfoSet - Supplies a handle to the device information set containing
        class installer parameters to be set.

    DeviceInfoData - Optionally, supplies the address of a SP_DEVINFO_DATA
        structure containing class installer parameters to be set.  If this
        parameter is not specified, then the class installer parameters to be
        set will be associated with the device information set itself (for the
        global class driver list).

    ClassInstallParams - Optionally, supplies the address of a buffer
        containing the class installer parameters to be used.    The
        SP_CLASSINSTALL_HEADER structure at the beginning of the buffer must
        have its cbSize field set to be sizeof(SP_CLASSINSTALL_HEADER), and the
        InstallFunction field must be set to the DI_FUNCTION code reflecting
        the type of parameters supplied in the rest of the buffer.

        If this parameter is not supplied, then the current class installer
        parameters (if any) will be cleared for the specified device
        information set or element.

        FUTURE-2002/06/17-lonnym -- Clearing class install params should be targeted
        ** Presently, we blindly clear _any_ class install params that have **
        ** been set, irrespective of their DIF code association.  There     **
        ** needs to be a way to clear class install params _only_ if the    **
        ** params are associated with a specified DIF code.                 **

    ClassInstallParamsSize - Supplies the size, in bytes, of the
        ClassInstallParams buffer.  If the buffer is not supplied (i.e., the
        class installer parameters are to be cleared), then this value must be
        zero.

Return Value:

    If the function succeeds, the return value is TRUE.
    If the function fails, the return value is FALSE.  To get extended error
    information, call GetLastError.

Remarks:

    All parameters will be validated before any changes are made, so a return
    status of FALSE indicates that no parameters were modified.

    A side effect of setting class installer parameters is that the
    DI_CLASSINSTALLPARAMS flag is set.  If for some reason, it is desired to
    set the parameters, but disable their use, then this flag must be cleared
    via SetupDiSetDeviceInstallParams.

    If the class installer parameters are cleared, then the
    DI_CLASSINSTALLPARAMS flag is reset.

--*/

{
    PDEVICE_INFO_SET pDeviceInfoSet = NULL;
    DWORD Err;
    PDEVINFO_ELEM DevInfoElem;

    try {

        if(!(pDeviceInfoSet = AccessDeviceInfoSet(DeviceInfoSet))) {
            Err = ERROR_INVALID_HANDLE;
            leave;
        }

        if(DeviceInfoData) {
            //
            // Then we are to set class installer parameters for a particular device.
            //
            if(!(DevInfoElem = FindAssociatedDevInfoElem(pDeviceInfoSet,
                                                         DeviceInfoData,
                                                         NULL))) {
                Err = ERROR_INVALID_PARAMETER;
            } else {
                Err = SetClassInstallParams(pDeviceInfoSet,
                                            ClassInstallParams,
                                            ClassInstallParamsSize,
                                            &(DevInfoElem->InstallParamBlock)
                                           );
            }

        } else {
            //
            // Set class installer parameters for the global class driver list.
            //
            Err = SetClassInstallParams(pDeviceInfoSet,
                                        ClassInstallParams,
                                        ClassInstallParamsSize,
                                        &(pDeviceInfoSet->InstallParamBlock)
                                       );
        }

    } except(pSetupExceptionFilter(GetExceptionCode())) {
        pSetupExceptionHandler(GetExceptionCode(), ERROR_INVALID_PARAMETER, &Err);
    }

    if(pDeviceInfoSet) {
        UnlockDeviceInfoSet(pDeviceInfoSet);
    }

    SetLastError(Err);
    return (Err == NO_ERROR);
}


BOOL
WINAPI
SetupDiCallClassInstaller(
    IN DI_FUNCTION      InstallFunction,
    IN HDEVINFO         DeviceInfoSet,
    IN PSP_DEVINFO_DATA DeviceInfoData OPTIONAL
    )
/*++

Routine Description:

    This routine calls the appropriate class installer with the specified
    installer function.

    Before calling the class installer, this routine will call any registered
    co-device installers (registration is either per-class or per-device;
    per-class installers are called first).  Any co-installer wishing to be
    called back once the class installer has finished installation may return
    ERROR_DI_POSTPROCESSING_REQUIRED.  Returning NO_ERROR will also allow
    installation to continue, but without a post-processing callback.
    Returning any other error code will cause the install action to be aborted
    (any co-installers already called that have requested post-processing will
    be called back, with InstallResult indicating the cause of failure).

    After the class installer has performed the installation (or we've done the
    default if ERROR_DI_DO_DEFAULT is returned), then we'll call any co-
    installers who have requested postprocessing.  The list of co-installers is
    treated like a stack, so the co-installers called last 'on the way in' are
    called first 'on the way out'.

Arguments:

    InstallFunction - Class installer function to call.  This can be one
        of the following values, or any other (class-specific) value:

        DIF_SELECTDEVICE - Select a driver to be installed.
        DIF_INSTALLDEVICE - Install the driver for the device.  (DeviceInfoData
            must be specified.)
        DIF_ASSIGNRESOURCES - ** PRESENTLY UNUSED ON WINDOWS NT **
        DIF_PROPERTIES - Display a properties dialog for the device.
            (DeviceInfoData must be specified.)
        DIF_REMOVE - Remove the device.  (DeviceInfoData must be specified.)
        DIF_FIRSTTIMESETUP - Perform first time setup initialization.  This
            is used only for the global class information associated with
            the device information set (i.e., DeviceInfoData not specified).
        DIF_FOUNDDEVICE - ** UNUSED ON WINDOWS NT **
        DIF_SELECTCLASSDRIVERS - Select drivers for all devices of the class
            associated with the device information set or element.
        DIF_VALIDATECLASSDRIVERS - Ensure all devices of the class associated
            with the device information set or element are ready to be
            installed.
        DIF_INSTALLCLASSDRIVERS - Install drivers for all devices of the
            class associated with the device information set or element.
        DIF_CALCDISKSPACE - Compute the amount of disk space required by
            drivers.
        DIF_DESTROYPRIVATEDATA - Destroy any private date referenced by
            the ClassInstallReserved installation parameter for the specified
            device information set or element.
        DIF_VALIDATEDRIVER - ** UNUSED ON WINDOWS NT **
        DIF_MOVEDEVICE - ** OBSOLETE **
        DIF_DETECT - Detect any devices of class associated with the device
            information set.
        DIF_INSTALLWIZARD - Add any pages necessary to the New Device Wizard
            for the class associated with the device information set or
            element.
            ** OBSOLETE--use DIF_NEWDEVICEWIZARD method instead **
        DIF_DESTROYWIZARDDATA - Destroy any private data allocated due to
            a DIF_INSTALLWIZARD message.
            ** OBSOLETE--not needed for DIF_NEWDEVICEWIZARD method **
        DIF_PROPERTYCHANGE - The device's properties are changing. The device
            is being enabled, disabled, or has had a resource change.
            (DeviceInfoData must be specified.)
        DIF_ENABLECLASS - ** UNUSED ON WINDOWS NT **
        DIF_DETECTVERIFY - The class installer should verify any devices it
            previously detected.  Non verified devices should be removed.
        DIF_INSTALLDEVICEFILES - The class installer should only install the
            driver files for the selected device.  (DeviceInfoData must be
            specified.)
        DIF_UNREMOVE - Unremoves a device from the system.  (DeviceInfoData
            mustbe specified.)
        DIF_SELECTBESTCOMPATDRV - Select the best driver from the device
            information element's compatible driver list.  (DeviceInfoData must
            be specified.)
        DIF_ALLOW_INSTALL - Determine whether or not the selected driver should
            be installed for the device.  (DeviceInfoData must be specified.)
        DIF_REGISTERDEVICE - The class installer should register the new,
            manually-installed, device information element (via
            SetupDiRegisterDeviceInfo) including, potentially, doing duplicate
            detection via the SPRDI_FIND_DUPS flag.  (DeviceInfoData must be
            specified.)
        DIF_NEWDEVICEWIZARD_PRESELECT - Allows class-/co-installers to supply
            wizard pages to be displayed before the Select Device page during
            "Add New Hardware" wizard.
        DIF_NEWDEVICEWIZARD_SELECT - Allows class-/co-installers to supply
            wizard pages to replace the default Select Device wizard page, as
            retrieved by SetupDiGetWizardPage(...SPWPT_SELECTDEVICE...)
        DIF_NEWDEVICEWIZARD_PREANALYZE - Allows class-/co-installers to supply
            wizard pages to be displayed before the analyze page.
        DIF_NEWDEVICEWIZARD_POSTANALYZE - Allows class-/co-installers to supply
            wizard pages to be displayed after the analyze page.
        DIF_NEWDEVICEWIZARD_FINISHINSTALL - Allows class-/co-installers
            (including device-specific co-installers) to supply wizard pages to
            be displayed after installation of the device has been performed
            (i.e., after DIF_INSTALLDEVICE has been processed), but prior to
            the wizard's finish page.  This message is sent not only for the
            "Add New Hardware" wizard, but also for the autodetection and "New
            Hardware Found" scenarios as well.
        DIF_UNUSED1 - ** PRESENTLY UNUSED ON WINDOWS NT **
        DIF_INSTALLINTERFACES - The class installer should create (and/or,
            potentially remove) device interfaces for this device information
            element.
        DIF_DETECTCANCEL - After the detection is stopped, if the class
            installer was invoked for DIF_DETECT, then it is invoked for
            DIF_DETECTCANCEL. This gives the class installer a chance to clean
            up anything it did during DIF_DETECT such as drivers setup to do
            detection at reboot, and private data. It is passed the same
            HDEVINFO as it was for DIF_DETECT.
        DIF_REGISTER_COINSTALLERS - Register device-specific co-installers so
            that they can be involved in the rest of the installation.
            (DeviceInfoData must be specified.)
        DIF_ADDPROPERTYPAGE_ADVANCED - Allows class-/co-installers to supply
            advanced property pages for a device.
        DIF_ADDPROPERTYPAGE_BASIC - Allows class-/co-installers to supply
            basic property pages for a device.
        DIF_TROUBLESHOOTER - Allows class-/co-installers to launch a
            troubleshooter for this device or to return CHM and HTM
            troubleshooter files that will get launched with a call to the
            HtmlHelp() API. If the class-/co-installer launches its own
            troubleshooter then it should return NO_ERROR, it should return
            ERROR_DI_DO_DEFAULT regardless of if it sets the CHM and HTM
            values.
        DIF_POWERMESSAGEWAKE - Allows class-/co-installers to specify text that
            will be displayed on the power tab in device manager. The class-/
            co-installer should return NO_ERROR if it specifies any text and
            ERROR_DI_DO_DEFAULT otherwise.
        DIF_ADDREMOTEPROPERTYPAGE_ADVANCED - Allows class-/co-installers to
            supply advanced proerty pages for a device that is on a remote
            machine.
        DIF_UPDATEDRIVER_UI - Allows a class-/co-installer to display their own
            UI when the user presses the update driver button in device
            manager.  The only real reason to do this is when the default
            update driver behavior could cause the device and/or machine not to
            work. We don't want IHVs providing their own UI for random reasons.
        DIF_INTERFACE_TO_DEVICE - For SWENUM device co-installers, get device
            for interface if it's different. Return NO_ERROR if handled
            ERROR_DI_DO_DEFAULT otherwise.

        (Note: remember to add new DIF_xxxx to pSetupDiDifStrings at start of
        file)

    DeviceInfoSet - Supplies a handle to the device information set to
        perform installation for.

    DeviceInfoData - Optionally, specifies a particular device information
        element whose class installer is to be called.  If this parameter
        is not specified, then the class installer for the device information
        set itself will be called (if the set has an associated class).

Return Value:

    If the function succeeds, the return value is TRUE.
    If the function fails, the return value is FALSE.  To get extended error
    information, call GetLastError.

Remarks:

    This function will attempt to load and call the class installer and co-
    installers (if any) for the class associated with the device information
    element or set specified.  If there is no class installer, or the class
    installer returns ERR_DI_DO_DEFAULT, then this function will call a default
    procedure for the specified class installer function.

--*/

{
    DWORD Err;

    try {

        Err = _SetupDiCallClassInstaller(
                  InstallFunction,
                  DeviceInfoSet,
                  DeviceInfoData,
                  CALLCI_LOAD_HELPERS | CALLCI_CALL_HELPERS
                  );

    } except(pSetupExceptionFilter(GetExceptionCode())) {
        pSetupExceptionHandler(GetExceptionCode(), ERROR_INVALID_PARAMETER, &Err);
    }

    SetLastError(Err);
    return (Err == NO_ERROR);
}


DWORD
_SetupDiCallClassInstaller(
    IN DI_FUNCTION      InstallFunction,
    IN HDEVINFO         DeviceInfoSet,
    IN PSP_DEVINFO_DATA DeviceInfoData,      OPTIONAL
    IN DWORD            Flags
    )
/*++

Routine Description:

    Worker routine for SetupDiCallClassInstaller that allows the caller to
    control what actions are taken when handling this install request.  In
    addition to the first three parameters (refer to the documentation for
    SetupDiCallClassInstaller for details), the following flags may be
    specified in the Flags parameter:

    CALLCI_LOAD_HELPERS - If helper modules (class installer, co-installers)
        haven't been loaded, load them so they can participate in handling
        this install request.

    CALLCI_CALL_HELPERS - Call the class installer/co-installers to give them
        a chance to handle this install request.  If this flag is not
        specified, then only the default action will be taken.

    CALLCI_ALLOW_DRVSIGN_UI - If an unsigned class installer or co-installer is
        encountered, perform standard non-driver signing behavior.  (WHQL
        doesn't have a certification program for class-/co-installers!)

        NTRAID#NTBUG9-166000-2000/08/18-JamieHun -- Driver signing policy for class installers?
        (lonnym): We should probably employ driver signing policy
        (instead of non-driver signing policy) for class installers of
        WHQL-approved classes.  However, the user experience here is
        problematic (e.g., driver signing popup trying to uninstall an unsigned
        driver package).

Return Values:

    If this function succeeds, the return value is NO_ERROR.  Otherwise, it is
    a Win32 error code indicating the cause of failure.

--*/

{
    PDEVICE_INFO_SET pDeviceInfoSet;
    BOOL MustAbort;
    DWORD Err;
    PDEVINFO_ELEM DevInfoElem;
    PDEVINSTALL_PARAM_BLOCK InstallParamBlock;
    HKEY hk;
    CONST GUID *ClassGuid;
    BOOL bRestoreDiQuietInstall;
    BOOL MuteError;
    PCOINSTALLER_INTERNAL_CONTEXT CoInstallerInternalContext;
    LONG i, CoInstallerCount;
    HWND hwndParent;
    TCHAR DescBuffer[LINE_LEN];
    PTSTR DeviceDesc;
    PSETUP_LOG_CONTEXT LogContext;
    DWORD slot_dif_code;
    BOOL ChangedThreadLogContext;
    PSETUP_LOG_CONTEXT SavedLogContext;
    DWORD LastErr;
    DWORD ErrorLevel;
    SPFUSIONINSTANCE spFusionInstance;
    VERIFY_CONTEXT VerifyContext;
    DWORD slot;
    CLASS_INSTALL_PROC ClassInstallerEntryPoint;
    HANDLE ClassInstallerFusionContext;
    BOOL UnlockDevInfoElem, UnlockDevInfoSet;

    ASSERT_HEAP_IS_VALID();

    //
    // DIF codes must be non-zero...
    //
    if(!InstallFunction) {
        return ERROR_INVALID_PARAMETER;
    }

#ifdef _X86_
    if(IsWow64) {
        //
        // This API not allowed in Wow64, class/co installers must/will be
        // native
        //
        return ERROR_IN_WOW64;
    }
#endif

    if(!(pDeviceInfoSet = AccessDeviceInfoSet(DeviceInfoSet))) {
        return ERROR_INVALID_HANDLE;
    }

    //
    // Initialize some variables before entering the try/except block...
    //
    Err = ERROR_DI_DO_DEFAULT;
    CoInstallerInternalContext = NULL;
    i = 0;
    CoInstallerCount = -1;      // value indicating count hasn't been retrieved
    hk = INVALID_HANDLE_VALUE;
    slot = 0;
    bRestoreDiQuietInstall = FALSE;
    MuteError = FALSE;
    slot_dif_code = 0;
    ChangedThreadLogContext = FALSE;
    SavedLogContext = NULL;
    ErrorLevel = DRIVER_LOG_ERROR;
    UnlockDevInfoElem = UnlockDevInfoSet = FALSE;
    ZeroMemory(&VerifyContext, sizeof(VerifyContext));

    try {

        if(DeviceInfoData) {
            //
            // Then we are to call the class installer for a particular device.
            //
            DevInfoElem = FindAssociatedDevInfoElem(pDeviceInfoSet,
                                                    DeviceInfoData,
                                                    NULL
                                                   );
            if(!DevInfoElem) {
                Err = ERROR_INVALID_PARAMETER;
                leave;
            }

            InstallParamBlock = &(DevInfoElem->InstallParamBlock);
            ClassGuid = &(DevInfoElem->ClassGuid);

            //
            // If the device information element isn't already locked, do so
            // now.  That will prevent it from being yanked out from under us
            // when calling a class-/co-installer.
            //
            if(!(DevInfoElem->DiElemFlags & DIE_IS_LOCKED)) {
                DevInfoElem->DiElemFlags |= DIE_IS_LOCKED;
                UnlockDevInfoElem = TRUE;
            }

        } else {

            DevInfoElem = NULL;
            InstallParamBlock = &(pDeviceInfoSet->InstallParamBlock);
            ClassGuid = pDeviceInfoSet->HasClassGuid
                          ? &(pDeviceInfoSet->ClassGuid)
                          : NULL;

            //
            // We don't have a device information element to lock, so we'll
            // lock the set itself...
            //
            if(!(pDeviceInfoSet->DiSetFlags & DISET_IS_LOCKED)) {
                pDeviceInfoSet->DiSetFlags |= DISET_IS_LOCKED;
                UnlockDevInfoSet = TRUE;
            }
        }

        //
        // Set the local log context before it gets used.
        //
        LogContext = InstallParamBlock->LogContext;

        //
        // If we are processing DIF_ALLOW_INSTALL then we need to make sure
        // that the DI_QUIETINSTALL flag is only set if we are doing a non-
        // interactive (server-side) install or we are in GUI mode setup.  In
        // any other case we need to remove the DI_QUIETINSTALL flag otherwise
        // class installers might think they can't display any UI and fail the
        // DIF_ALLOW_INSTALL.
        //
        if((InstallFunction == DIF_ALLOW_INSTALL) &&
           (InstallParamBlock->Flags & DI_QUIETINSTALL) &&
           !(InstallParamBlock->FlagsEx & DI_FLAGSEX_IN_SYSTEM_SETUP) &&
           !(GlobalSetupFlags & (PSPGF_NONINTERACTIVE|PSPGF_UNATTENDED_SETUP))) {

            InstallParamBlock->Flags &= ~DI_QUIETINSTALL;
            bRestoreDiQuietInstall = TRUE;
        }

        if(Flags & CALLCI_LOAD_HELPERS) {
            //
            // Retrieve the parent window handle, as we may need it below if we
            // need to popup UI due to unsigned class-/co-installers.
            //
            if(hwndParent = InstallParamBlock->hwndParent) {
               if(!IsWindow(hwndParent)) {
                    hwndParent = NULL;
               }
            }

            //
            // Retrieve a device description to use in case we need to give a
            // driver signing warn/block popup.
            //
            if(GetBestDeviceDesc(DeviceInfoSet, DeviceInfoData, DescBuffer)) {
                DeviceDesc = DescBuffer;
            } else {
                DeviceDesc = NULL;
            }

            //
            // If the class installer has not been loaded, then load it and
            // get the function address for the ClassInstall function.
            //
            if(!InstallParamBlock->hinstClassInstaller) {

                if(ClassGuid &&
                   (hk = SetupDiOpenClassRegKey(ClassGuid, KEY_READ)) != INVALID_HANDLE_VALUE) {

                    slot = AllocLogInfoSlot(LogContext, FALSE);

                    WriteLogEntry(LogContext,
                                  slot,
                                  MSG_LOG_CI_MODULE,
                                  NULL,
                                  DeviceDesc ? DeviceDesc : TEXT("")
                                 );

                    try {
                        Err = GetModuleEntryPoint(hk,
                                                  pszInstaller32,
                                                  pszCiDefaultProc,
                                                  &(InstallParamBlock->hinstClassInstaller),
                                                  &((FARPROC)InstallParamBlock->ClassInstallerEntryPoint),
                                                  &(InstallParamBlock->ClassInstallerFusionContext),
                                                  &MustAbort,
                                                  LogContext,
                                                  hwndParent,
                                                  ClassGuid,
                                                  SetupapiVerifyClassInstProblem,
                                                  DeviceDesc,
                                                  DRIVERSIGN_NONE,
                                                  TRUE,
                                                  &VerifyContext
                                                 );

                    } except(pSetupExceptionFilter(GetExceptionCode())) {

                        pSetupExceptionHandler(GetExceptionCode(),
                                               ERROR_INVALID_CLASS_INSTALLER,
                                               &Err
                                              );

                        InstallParamBlock->ClassInstallerEntryPoint = NULL;

                    }

                    if(slot) {
                        ReleaseLogInfoSlot(LogContext, slot);
                        slot = 0;
                    }

                    RegCloseKey(hk);
                    hk = INVALID_HANDLE_VALUE;

                    if((Err != NO_ERROR) && (Err != ERROR_DI_DO_DEFAULT)) {

                        if(!(InstallParamBlock->FlagsEx & DI_FLAGSEX_CI_FAILED)) {

                            TCHAR ClassName[MAX_GUID_STRING_LEN];
                            TCHAR Title[MAX_TITLE_LEN];

                            if(!SetupDiClassNameFromGuid(ClassGuid,
                                                         ClassName,
                                                         SIZECHARS(ClassName),
                                                         NULL)) {
                                //
                                // Use the ClassName buffer to hold the class
                                // GUID string (it's better than nothin')
                                //
                                pSetupStringFromGuid(ClassGuid,
                                                     ClassName,
                                                     SIZECHARS(ClassName)
                                                    );
                            }

                            //
                            // Write out an event log entry about this.
                            //
                            WriteLogEntry(LogContext,
                                          DRIVER_LOG_ERROR | SETUP_LOG_BUFFER,
                                          MSG_CI_LOADFAIL_ERROR,
                                          NULL,
                                          ClassName
                                         );

                            WriteLogError(LogContext, DRIVER_LOG_ERROR, Err);

                            MuteError = TRUE;

                            if(!(GlobalSetupFlags &
                                 (PSPGF_NONINTERACTIVE | PSPGF_UNATTENDED_SETUP))) {

                                if(!LoadString(MyDllModuleHandle,
                                               IDS_DEVICEINSTALLER,
                                               Title,
                                               SIZECHARS(Title))) {
                                    *Title = TEXT('\0');
                                }
                                FormatMessageBox(MyDllModuleHandle,
                                                 InstallParamBlock->hwndParent,
                                                 MSG_CI_LOADFAIL_ERROR,
                                                 Title,
                                                 MB_OK,
                                                 ClassName
                                                );
                            }

                            InstallParamBlock->FlagsEx |= DI_FLAGSEX_CI_FAILED;
                        }

                        Err = ERROR_INVALID_CLASS_INSTALLER;
                        leave;
                    }
                }
            }

            //
            // If we haven't retrieved a list of co-installers to call,
            // retrieve the list now.
            //
            if(InstallParamBlock->CoInstallerCount == -1) {

                slot = AllocLogInfoSlot(LogContext, FALSE);

                WriteLogEntry(LogContext,
                              slot,
                              MSG_LOG_COINST_MODULE,
                              NULL,
                              DeviceDesc
                             );

                Err = pSetupDiGetCoInstallerList(DeviceInfoSet,
                                                 DeviceInfoData,
                                                 ClassGuid,
                                                 InstallParamBlock,
                                                 &VerifyContext
                                                );

                if(slot) {
                    ReleaseLogInfoSlot(LogContext, slot);
                    slot = 0;
                }

                if(Err != NO_ERROR) {
                    leave;
                }

                MYASSERT(InstallParamBlock->CoInstallerCount >= 0);
            }
        }

        slot_dif_code = AllocLogInfoSlotOrLevel(LogContext,
                                                DRIVER_LOG_VERBOSE1,
                                                FALSE
                                               );

        if(slot_dif_code) {
            //
            // this is skipped if we know we would never log anything
            //
            // pass a string which we may log with an error or will log at
            // VERBOSE1 level
            //
            if(InstallFunction >= (sizeof(pSetupDiDifStrings)/sizeof(pSetupDiDifStrings[0]))) {
                //
                // This is a user-defined DIF code...
                //
                WriteLogEntry(LogContext,
                              slot_dif_code,
                              MSG_LOG_DI_UNUSED_FUNC,
                              NULL,
                              InstallFunction
                             );
            } else {
                //
                // use the string version of the DIF code
                //
                WriteLogEntry(LogContext,
                              slot_dif_code,
                              MSG_LOG_DI_FUNC,
                              NULL,
                              pSetupDiDifStrings[InstallFunction]
                             );
            }
        }

        //
        // do any pre DIF cleanup
        //
        switch(InstallFunction) {

            case DIF_REGISTER_COINSTALLERS:
                //
                // NTRAID#NTBUG9-644874-2002/06/17-lonnym -- DIF_REGISTER_COINSTALLERS is destructive upon error
                //
                hk = SetupDiOpenDevRegKey(DeviceInfoSet,
                                          DeviceInfoData,
                                          DICS_FLAG_GLOBAL,
                                          0,
                                          DIREG_DRV,
                                          KEY_WRITE
                                         );

                if(hk != INVALID_HANDLE_VALUE) {
                    //
                    // Clean up device SW key:
                    //
                    //  remove CoInstallers32  - can introduce unwanted
                    //                           co-installers
                    //
                    //  remove EnumPropPages32 - can introduce unwanted
                    //                           property pages
                    //
                    RegDeleteValue(hk, pszCoInstallers32);
                    RegDeleteValue(hk, pszEnumPropPages32);

                    RegCloseKey(hk);
                    hk = INVALID_HANDLE_VALUE;
                }

                break;

            case DIF_INSTALLDEVICE:
                //
                // NTRAID#NTBUG9-644997-2002/06/17-lonnym -- DIF_INSTALLDEVICE is destructive upon error
                //
                SetupDiSetDeviceRegistryProperty(DeviceInfoSet,
                                                 DeviceInfoData,
                                                 SPDRP_UPPERFILTERS,
                                                 NULL,
                                                 0
                                                );

                SetupDiSetDeviceRegistryProperty(DeviceInfoSet,
                                                 DeviceInfoData,
                                                 SPDRP_LOWERFILTERS,
                                                 NULL,
                                                 0
                                                );
                break;

            default:

                break;
        }

        if(Flags & CALLCI_CALL_HELPERS) {
            //
            // Push log context as thread's default.  This will cause orphaned
            // log sections to be merged.
            //
            ChangedThreadLogContext = SetThreadLogContext(LogContext,
                                                          &SavedLogContext
                                                         );

            if(ChangedThreadLogContext) {
                //
                // Add one more ref to protect log context against thread
                // freeing DeviceInfoSet.
                //
                RefLogContext(LogContext);
            }

            //
            // Before we go and try to call any co-installers, first remember
            // the class installer entry point and fusion context, because
            // we'll need it below, and we don't want to wait until the devinfo
            // set has already been unlocked before retrieving this
            // information.
            //
            ClassInstallerEntryPoint =
                InstallParamBlock->ClassInstallerEntryPoint;

            ClassInstallerFusionContext =
                InstallParamBlock->ClassInstallerFusionContext;

            //
            // Store the co-installer count in a local variable for later use.
            // We don't trust accessing it from the install parameter block
            // because after we release the lock (prior to calling the
            // co-installers), the device information element could get deleted
            // out from under us!
            //
            CoInstallerCount = InstallParamBlock->CoInstallerCount;

            //
            // Note:  CoInstallerCount may still be -1 here, because we may
            // have been asked to only call previously-loaded helper modules,
            // and not load any new ones (e.g., if we're simply going to call
            // them to do clean-up in preparation for unload).
            //

            if(CoInstallerCount > 0) {
                //
                // Allocate an array of co-installer context structures to be
                // used when calling (and potentially, re-calling) the entry
                // points.
                //
                CoInstallerInternalContext = MyMalloc(sizeof(COINSTALLER_INTERNAL_CONTEXT) * CoInstallerCount);
                if(!CoInstallerInternalContext) {
                    Err = ERROR_NOT_ENOUGH_MEMORY;
                    leave;
                }

                ZeroMemory(CoInstallerInternalContext,
                           sizeof(COINSTALLER_INTERNAL_CONTEXT) * CoInstallerCount
                          );

                //
                // Loop through our list of co-installers, storing the
                // necessary information about each one into our context list.
                // We must do this because we've no guarantee that the list of
                // co-installers won't change as a result of processing this
                // DIF request.  (We do, however, know that the set/element
                // won't be yanked out from under us, since we locked these
                // down.)
                //
                for(i = 0; i < CoInstallerCount; i++) {

                    CoInstallerInternalContext[i].CoInstallerEntryPoint =
                        InstallParamBlock->CoInstallerList[i].CoInstallerEntryPoint;

                    CoInstallerInternalContext[i].CoInstallerFusionContext =
                        InstallParamBlock->CoInstallerList[i].CoInstallerFusionContext;

                }

                //
                // Call each co-installer.  We must unlock the devinfo set
                // first, to avoid deadlocks.
                //
                UnlockDeviceInfoSet(pDeviceInfoSet);
                pDeviceInfoSet = NULL;

                for(i = 0; i < CoInstallerCount; i++) {

                    WriteLogEntry(LogContext,
                                  DRIVER_LOG_TIME,
                                  MSG_LOG_COINST_START,
                                  NULL,
                                  i + 1,
                                  CoInstallerCount
                                 );

                    spFusionEnterContext(
                        CoInstallerInternalContext[i].CoInstallerFusionContext,
                        &spFusionInstance
                        );

                    try {
                        Err = CoInstallerInternalContext[i].CoInstallerEntryPoint(
                                  InstallFunction,
                                  DeviceInfoSet,
                                  DeviceInfoData,
                                  &(CoInstallerInternalContext[i].Context)
                                 );
                    } finally {
                        spFusionLeaveContext(&spFusionInstance);
                    }

                    ASSERT_HEAP_IS_VALID();

                    if((Err != NO_ERROR) && (Err != ERROR_DI_POSTPROCESSING_REQUIRED)) {

                        ErrorLevel = FilterLevelOnInstallerError(ErrorLevel,
                                                                 Err
                                                                );

                        WriteLogEntry(LogContext,
                                      ErrorLevel | SETUP_LOG_BUFFER,
                                      MSG_LOG_COINST_END_ERROR,
                                      NULL,
                                      i + 1,
                                      CoInstallerCount
                                     );

                        WriteLogError(LogContext, ErrorLevel, Err);

                        MuteError = TRUE; // already logged it
                        leave;

                    } else {

                        WriteLogEntry(LogContext,
                                      DRIVER_LOG_VERBOSE1,
                                      MSG_LOG_COINST_END,
                                      NULL,
                                      i + 1,
                                      CoInstallerCount
                                     );

                        if(Err == ERROR_DI_POSTPROCESSING_REQUIRED) {
                            CoInstallerInternalContext[i].DoPostProcessing = TRUE;
                        }
                    }
                }
            }

            //
            // If there is a class installer entry point, then call it.
            //
            if(ClassInstallerEntryPoint) {
                //
                // Make sure we don't have the HDEVINFO locked.
                //
                if(pDeviceInfoSet) {
                    UnlockDeviceInfoSet(pDeviceInfoSet);
                    pDeviceInfoSet = NULL;
                }

                WriteLogEntry(LogContext,
                              DRIVER_LOG_TIME,
                              MSG_LOG_CI_START,
                              NULL
                             );

                spFusionEnterContext(ClassInstallerFusionContext,
                                     &spFusionInstance
                                    );

                try {
                    Err = ClassInstallerEntryPoint(InstallFunction,
                                                   DeviceInfoSet,
                                                   DeviceInfoData
                                                  );
                } finally {
                    spFusionLeaveContext(&spFusionInstance);
                }

                ASSERT_HEAP_IS_VALID();

                if((Err != NO_ERROR) && (Err != ERROR_DI_DO_DEFAULT)) {

                    ErrorLevel = FilterLevelOnInstallerError(ErrorLevel, Err);

                    WriteLogEntry(LogContext,
                                  ErrorLevel | SETUP_LOG_BUFFER,
                                  MSG_LOG_CI_END_ERROR,
                                  NULL
                                 );

                    WriteLogError(LogContext, ErrorLevel, Err);

                    MuteError = TRUE; // already logged it

                } else {

                    WriteLogEntry(LogContext,
                                  DRIVER_LOG_VERBOSE1,
                                  MSG_LOG_CI_END,
                                  NULL
                                 );
                }

                if(Err != ERROR_DI_DO_DEFAULT) {
                    //
                    // class installer handled
                    //
                    leave;
                }

            } else {
                Err = ERROR_DI_DO_DEFAULT;
            }
        }

        if(InstallParamBlock->Flags & DI_NODI_DEFAULTACTION) {
            //
            // We shouldn't provide a default action--just return the class
            // installer result.
            //
            leave;
        }

        Err = NO_ERROR;

        //
        // Make sure the devinfo set is unlocked before calling the appropriate
        // default handler routine...
        //
        if(pDeviceInfoSet) {
            UnlockDeviceInfoSet(pDeviceInfoSet);
            pDeviceInfoSet = NULL;
        }

        WriteLogEntry(LogContext,
                      DRIVER_LOG_VERBOSE1,
                      MSG_LOG_CI_DEF_START,
                      NULL
                     );

        switch(InstallFunction) {

            case DIF_SELECTDEVICE :

                Err = GLE_FN_CALL(FALSE,
                                  SetupDiSelectDevice(DeviceInfoSet,
                                                      DeviceInfoData)
                                 );
                break;

            case DIF_SELECTBESTCOMPATDRV :

                Err = GLE_FN_CALL(FALSE,
                                  SetupDiSelectBestCompatDrv(DeviceInfoSet,
                                                             DeviceInfoData)
                                 );

                if(Err == ERROR_NO_COMPAT_DRIVERS) {
                    ErrorLevel = DRIVER_LOG_WARNING;
                }
                break;

            case DIF_INSTALLDEVICE :

                Err = GLE_FN_CALL(FALSE,
                                  SetupDiInstallDevice(DeviceInfoSet,
                                                       DeviceInfoData)
                                 );
                break;

            case DIF_INSTALLDEVICEFILES :

                Err = GLE_FN_CALL(FALSE,
                                  SetupDiInstallDriverFiles(DeviceInfoSet,
                                                            DeviceInfoData)
                                 );
                break;

            case DIF_INSTALLINTERFACES :

                Err = GLE_FN_CALL(FALSE,
                                  SetupDiInstallDeviceInterfaces(
                                      DeviceInfoSet,
                                      DeviceInfoData)
                                 );
                break;

            case DIF_REGISTER_COINSTALLERS :

                Err = GLE_FN_CALL(FALSE,
                                  SetupDiRegisterCoDeviceInstallers(
                                      DeviceInfoSet,
                                      DeviceInfoData)
                                 );
                break;

            case DIF_REMOVE :

                Err = GLE_FN_CALL(FALSE,
                                  SetupDiRemoveDevice(DeviceInfoSet,
                                                      DeviceInfoData)
                                 );
                break;

            case DIF_UNREMOVE :

                Err = GLE_FN_CALL(FALSE,
                                  SetupDiUnremoveDevice(DeviceInfoSet,
                                                        DeviceInfoData)
                                 );
                break;

            case DIF_MOVEDEVICE :
                //
                // This device install action has been deprecated.
                //
                Err = ERROR_DI_FUNCTION_OBSOLETE;
                break;

            case DIF_PROPERTYCHANGE :

                Err = GLE_FN_CALL(FALSE,
                                  SetupDiChangeState(DeviceInfoSet,
                                                     DeviceInfoData)
                                 );
                break;

            case DIF_REGISTERDEVICE :

                Err = GLE_FN_CALL(FALSE,
                                  SetupDiRegisterDeviceInfo(DeviceInfoSet,
                                                            DeviceInfoData,
                                                            0,
                                                            NULL,
                                                            NULL,
                                                            NULL)
                                 );
                break;

            //
            // FUTURE-2002/06/18-lonnym -- End-of-life old Win9x netdi DIF codes
            //
            // These are Win9x messages for class installers such as the
            // Network, where the class installer will do all of the work.  If
            // no action is taken, ie, the class installer returns
            // ERROR_DI_DO_DEFAULT, then we return OK, since there is no
            // default action for these cases.
            //
            case DIF_SELECTCLASSDRIVERS:
            case DIF_VALIDATECLASSDRIVERS:
            case DIF_INSTALLCLASSDRIVERS:
                //
                // Let fall through to default handling...
                //

            default :
                //
                // If the DIF request didn't have a default handler, then let
                // the caller deal with it...
                //
                Err = ERROR_DI_DO_DEFAULT;
                break;
        }

        if(!MuteError) {

            if((Err != NO_ERROR) && (Err != ERROR_DI_DO_DEFAULT)) {

                ErrorLevel = FilterLevelOnInstallerError(ErrorLevel, Err);

                WriteLogEntry(LogContext,
                              ErrorLevel | SETUP_LOG_BUFFER,
                              MSG_LOG_CI_DEF_END_ERROR,
                              NULL
                             );

                WriteLogError(LogContext, ErrorLevel, Err);

                MuteError = TRUE; // already logged it

            } else {

                WriteLogEntry(LogContext,
                              DRIVER_LOG_VERBOSE1,
                              MSG_LOG_CI_DEF_END,
                              NULL
                             );
            }
        }

    } except(pSetupExceptionFilter(GetExceptionCode())) {
        pSetupExceptionHandler(GetExceptionCode(), ERROR_INVALID_PARAMETER, &Err);
    }

    if(hk != INVALID_HANDLE_VALUE) {
        RegCloseKey(hk);
    }

    if(slot) {
        ReleaseLogInfoSlot(LogContext, slot);
    }

    //
    // Free any context handles that may have been allocated while verifying/
    // loading the class installer and co-installers.
    //
    pSetupFreeVerifyContextMembers(&VerifyContext);

    ASSERT_HEAP_IS_VALID();

    //
    // Do a post-processing callback to any of the co-installers that requested
    // one.
    //
    for(i--; i >= 0; i--) {

        if(CoInstallerInternalContext[i].DoPostProcessing) {
            //
            // If we get to here, the HDEVINFO shouldn't be locked...
            //
            MYASSERT(!pDeviceInfoSet);

            CoInstallerInternalContext[i].Context.PostProcessing = TRUE;
            CoInstallerInternalContext[i].Context.InstallResult = Err;
            LastErr = Err;

            try {

                WriteLogEntry(LogContext,
                              DRIVER_LOG_TIME,
                              MSG_LOG_COINST_POST_START,
                              NULL,
                              i + 1
                             );

                spFusionEnterContext(
                    CoInstallerInternalContext[i].CoInstallerFusionContext,
                    &spFusionInstance
                    );

                try {
                    Err = CoInstallerInternalContext[i].CoInstallerEntryPoint(
                              InstallFunction,
                              DeviceInfoSet,
                              DevInfoElem ? DeviceInfoData : NULL,
                              &(CoInstallerInternalContext[i].Context)
                              );
                } finally {
                    spFusionLeaveContext(&spFusionInstance);
                }

                ASSERT_HEAP_IS_VALID();

                if((Err != LastErr) &&
                   ((LastErr != ERROR_DI_DO_DEFAULT) || (Err != NO_ERROR))) {
                    //
                    // If error status has changed (even to success)
                    // log this as an error
                    //
                    if(((LastErr == NO_ERROR) || (LastErr == ERROR_DI_DO_DEFAULT))
                        && (Err != NO_ERROR) && (Err != ERROR_DI_DO_DEFAULT)) {
                        WriteLogEntry(
                                  LogContext,
                                  ErrorLevel | SETUP_LOG_BUFFER,
                                  MSG_LOG_COINST_POST_END_ERROR,
                                  NULL,
                                  i+1);

                        WriteLogError(LogContext, ErrorLevel, Err);
                    } else {
                        WriteLogEntry(
                                  LogContext,
                                  DRIVER_LOG_WARNING | SETUP_LOG_BUFFER,
                                  MSG_LOG_COINST_POST_CHANGE_ERROR,
                                  NULL,
                                  i+1);

                        WriteLogError(LogContext, DRIVER_LOG_WARNING, Err);
                    }
                } else {
                    WriteLogEntry(
                              LogContext,
                              DRIVER_LOG_VERBOSE1,
                              MSG_LOG_COINST_POST_END,
                              NULL,
                              i+1);
                }

            } except(pSetupExceptionFilter(GetExceptionCode())) {

                pSetupExceptionHandler(GetExceptionCode(),
                                       ERROR_INVALID_PARAMETER,
                                       NULL
                                      );

                //
                // Ignore any co-installer that generates an exception during
                // post-processing.
                //
            }
        }
    }

    //
    // If we need to restore any state on the devinfo set or element, do that
    // now (we may need to re-acquire the lock before doing so)...
    //
    if(bRestoreDiQuietInstall
       || UnlockDevInfoElem
       || UnlockDevInfoSet) {

        if(!pDeviceInfoSet) {

            pDeviceInfoSet = AccessDeviceInfoSet(DeviceInfoSet);

            //
            // Since we had the set/element "pinned", we should've been able to
            // re-acquire the lock...
            //
            MYASSERT(pDeviceInfoSet);
        }

        try {
            //
            // Since we had the set/element "pinned", then our devinfo element,
            // and the pointer to the install parameter block should be the
            // same...
            //
#if ASSERTS_ON
            if(DevInfoElem) {

                MYASSERT(DevInfoElem == FindAssociatedDevInfoElem(
                                            pDeviceInfoSet,
                                            DeviceInfoData,
                                            NULL));

                MYASSERT(InstallParamBlock == &(DevInfoElem->InstallParamBlock));

            } else {
                MYASSERT(InstallParamBlock == &(pDeviceInfoSet->InstallParamBlock));
            }
#endif

            if(UnlockDevInfoElem) {
                MYASSERT(DevInfoElem);
                MYASSERT(DevInfoElem->DiElemFlags & DIE_IS_LOCKED);
                DevInfoElem->DiElemFlags &= ~DIE_IS_LOCKED;
            } else if(UnlockDevInfoSet) {
                MYASSERT(pDeviceInfoSet->DiSetFlags & DISET_IS_LOCKED);
                pDeviceInfoSet->DiSetFlags &= ~DISET_IS_LOCKED;
            }

            if(bRestoreDiQuietInstall) {
                InstallParamBlock->Flags |= DI_QUIETINSTALL;
            }

        } except(pSetupExceptionFilter(GetExceptionCode())) {
            pSetupExceptionHandler(GetExceptionCode(), ERROR_INVALID_PARAMETER, NULL);
        }
    }

    if(pDeviceInfoSet) {
        UnlockDeviceInfoSet(pDeviceInfoSet);
    }

    if(CoInstallerInternalContext) {
        MyFree(CoInstallerInternalContext);
    }

    //
    // If we just did a DIF_REGISTER_COINSTALLERS, then we invalidated our
    // current list of co-installers.  Clear our list, so it will be retrieved
    // next time.  (NOTE:  Normally, the default action will be taken (i.e.,
    // SetupDiRegisterCoDeviceInstallers), which will have already invalidated
    // the list.  The class installer may have handled this themselves,
    // however, so we'll invalidate the list here as well just to be safe.)
    //
    if(InstallFunction == DIF_REGISTER_COINSTALLERS) {
        InvalidateHelperModules(DeviceInfoSet, DeviceInfoData, IHM_COINSTALLERS_ONLY);
    }

    if(!MuteError && (Err != NO_ERROR) && (Err != ERROR_DI_DO_DEFAULT)) {

        ErrorLevel = FilterLevelOnInstallerError(ErrorLevel, Err);

        WriteLogEntry(LogContext,
                      ErrorLevel | SETUP_LOG_BUFFER,
                      MSG_LOG_CCI_ERROR,
                      NULL,
                      i + 1
                     );

        WriteLogError(LogContext, ErrorLevel, Err);
    }

    if(slot_dif_code) {
        ReleaseLogInfoSlot(LogContext, slot_dif_code);
    }

    if(ChangedThreadLogContext) {
        //
        // restore thread log context
        //
        SetThreadLogContext(SavedLogContext, NULL);
        DeleteLogContext(LogContext); // counter RefLogContext
    }

    return Err;
}


//
// ANSI version
//
BOOL
WINAPI
SetupDiInstallClassExA(
    IN HWND        hwndParent,         OPTIONAL
    IN PCSTR       InfFileName,        OPTIONAL
    IN DWORD       Flags,
    IN HSPFILEQ    FileQueue,          OPTIONAL
    IN CONST GUID *InterfaceClassGuid, OPTIONAL
    IN PVOID       Reserved1,
    IN PVOID       Reserved2
    )
{
    PCWSTR UnicodeInfFileName = NULL;
    DWORD rc;

    try {

        if(InfFileName) {
            rc = pSetupCaptureAndConvertAnsiArg(InfFileName,
                                                &UnicodeInfFileName
                                               );
            if(rc != NO_ERROR) {
                leave;
            }
        }

        rc = GLE_FN_CALL(FALSE,
                         SetupDiInstallClassExW(hwndParent,
                                                UnicodeInfFileName,
                                                Flags,
                                                FileQueue,
                                                InterfaceClassGuid,
                                                Reserved1,
                                                Reserved2)
                        );

    } except(pSetupExceptionFilter(GetExceptionCode())) {
        pSetupExceptionHandler(GetExceptionCode(), ERROR_INVALID_PARAMETER, &rc);
    }

    if(UnicodeInfFileName) {
        MyFree(UnicodeInfFileName);
    }

    SetLastError(rc);
    return (rc == NO_ERROR);
}

BOOL
WINAPI
SetupDiInstallClassEx(
    IN HWND        hwndParent,         OPTIONAL
    IN PCTSTR      InfFileName,        OPTIONAL
    IN DWORD       Flags,
    IN HSPFILEQ    FileQueue,          OPTIONAL
    IN CONST GUID *InterfaceClassGuid, OPTIONAL
    IN PVOID       Reserved1,
    IN PVOID       Reserved2
    )
/*++

Routine Description:

    This routine either:

        a) Installs a class installer by running the [ClassInstall32] section
           of the specified INF, or
        b) Installs an interface class specified in the InterfaceClassGuid
           parameter, running the install section for this class as listed in
           the [InterfaceInstall32] of the specified INF (if there is no entry,
           then installation simply involves creating the interface class
           subkey under the DeviceClasses key.

    If the InterfaceClassGuid parameter is specified, then we're installing an
    interface class (case b), otherwise, we're installing a class installer
    (case a).

Arguments:

    hwndParent - Optionally, supplies the handle of the parent window for any
        UI brought up as a result of installing this class.

    InfFileName - Optionally, supplies the name of the INF file containing a
        [ClassInstall32] section (if we're installing a class installer), or
        an [InterfaceInstall32] section with an entry for the specified
        interface class (if we're installing an interface class).  If
        installing a class installer, this parameter _must_ be supplied.

    Flags - Flags that control the installation.  May be a combination of the
        following:

        DI_NOVCP - This flag should be specified if HSPFILEQ is supplied.  This
            instructs SetupInstallFromInfSection to not create a queue of its
            own, and instead to use the caller-supplied one.  If this flag is
            specified, then no file copying will be done.

        DI_NOBROWSE - This flag should be specified if no file browsing should
            be allowed in the event a copy operation cannot find a specified
            file.  If the user supplies their own file queue, then this flag is
            ignored.

        DI_FORCECOPY - This flag should be specified if the files should always
            be copied, even if they're already present on the user's machine
            (i.e., don't ask the user if they want to keep their existing
            files).  If the user supplies their own file queue, then this flag
            is ignored.

        DI_QUIETINSTALL - This flag should be specified if UI should be
            suppressed unless absolutely necessary (i.e., no progress dialog).
            If the user supplies their own queue, then this flag is ignored.

            (NOTE:  During GUI-mode setup on Windows NT, quiet-install behavior
            is always employed in the absence of a user-supplied file queue.)

    FileQueue - If the DI_NOVCP flag is specified, then this parameter supplies
        a handle to a file queue where file operations are to be queued (but
        not committed).

    InterfaceClassGuid - Optionally, specifies the interface class to be
        installed.  If this parameter is not specified, then we are installing
        a class installer whose class is the class of the INF specified by
        InfFileName.

    Reserved1, Reserved2 - Reserved for future use.  Must be NULL.

Return Value:

    If the function succeeds, the return value is TRUE.
    If the function fails, the return value is FALSE.  To get extended error
    information, call GetLastError.

Remarks:

    This API is generally called by the "New Hardware Found" process when it
    installs a device of a new device setup class.

    Class installers may also use this API to install new interface classes.
    Note that interface class installation can also happen automatically as a
    result of installing device interfaces for a device instance (via
    SetupDiInstallDeviceInterfaces).

--*/

{
    HINF hInf = INVALID_HANDLE_VALUE;
    DWORD Err, ScanQueueResult;
    TCHAR ClassInstallSectionName[MAX_SECT_NAME_LEN];
    DWORD ClassInstallSectionNameLen;
    GUID ClassGuid;
    BOOL ClassGuidIsValid = FALSE;
    TCHAR ClassGuidStringBuffer[GUID_STRING_LEN];
    HKEY hKey = INVALID_HANDLE_VALUE;
    PSP_FILE_CALLBACK MsgHandler;
    PVOID MsgHandlerContext = NULL;
    BOOL KeyNewlyCreated = FALSE;
    PCTSTR ClassName;
    BOOL CloseFileQueue = FALSE;
    PTSTR SectionExtension;
    INFCONTEXT InterfaceClassInstallLine;
    PCTSTR UndecoratedInstallSection;
    DWORD InstallFlags;
    REGMOD_CONTEXT RegContext;
    BOOL NoProgressUI;
    PSETUP_LOG_CONTEXT LogContext = NULL;
    TCHAR szNewName[MAX_PATH];
    BOOL OemInfFileToCopy = FALSE;
    BOOL NullDriverInstall;
    HRESULT hr;

    try {
        //
        // Validate the flags.
        //
        if(Flags & ~(DI_NOVCP | DI_NOBROWSE | DI_FORCECOPY | DI_QUIETINSTALL)) {
            Err = ERROR_INVALID_FLAGS;
            leave;
        }

        //
        // If the caller didn't specify an interface class GUID (i.e., we're
        // installing a class installer), then they'd better have supplied us
        // with an INF filename.  Also, they have to pass NULL for the Reserved
        // arguments.
        //
        if((!InterfaceClassGuid && !InfFileName) || Reserved1 || Reserved2) {
            Err = ERROR_INVALID_PARAMETER;
            leave;
        }

        //
        // Make sure that the caller supplied us with a file queue, if
        // necessary.
        //
        if((Flags & DI_NOVCP) && (!FileQueue || (FileQueue == INVALID_HANDLE_VALUE))) {
            Err = ERROR_INVALID_PARAMETER;
            leave;
        }

        if(hwndParent && !IsWindow(hwndParent)) {
            hwndParent = NULL;
        }

        if(InfFileName) {
            //
            // Open the INF, and ensure that the same logging context is used
            // for all operations.
            //
            Err = GLE_FN_CALL(INVALID_HANDLE_VALUE,
                              hInf = SetupOpenInfFile(InfFileName,
                                                      NULL,
                                                      INF_STYLE_WIN4,
                                                      NULL)
                             );

            if(Err != NO_ERROR) {
                leave;
            }

            Err = InheritLogContext(((PLOADED_INF)hInf)->LogContext,
                                    &LogContext
                                   );
            if(Err != NO_ERROR) {
                //
                // Since we're using log context inheritance to create a log
                // context, this failure must be considered critical.
                //
                leave;
            }

        } else {
            //
            // No INF to worry about--just need a log context for the stuff
            // we're doing directly.
            //
            Err = CreateLogContext(NULL, TRUE, &LogContext);

            if(Err != NO_ERROR) {
                leave;
            }
        }

        if(InterfaceClassGuid) {
            //
            // Copy this GUID into our ClassGuid variable, which is used for
            // both installer and device interface classes.
            //
            CopyMemory(&ClassGuid, InterfaceClassGuid, sizeof(ClassGuid));
            ClassGuidIsValid = TRUE;

            //
            // Legacy (compatibility) class name is not needed for device
            // interface classes.
            //
            ClassName = NULL;

            pSetupStringFromGuid(&ClassGuid,
                                 ClassGuidStringBuffer,
                                 SIZECHARS(ClassGuidStringBuffer)
                                );

            WriteLogEntry(LogContext,
                          DRIVER_LOG_INFO,
                          MSG_LOG_DO_INTERFACE_CLASS_INSTALL,
                          NULL,       // text message
                          ClassGuidStringBuffer
                         );

        } else {

            PCTSTR pInfGuidString;

            //
            // Retrieve the class GUID from the INF.  If it has no class GUID,
            // then we can't install from it (even if it specifies the class
            // name).
            //
            if(!(pInfGuidString = pSetupGetVersionDatum(
                                      &((PLOADED_INF)hInf)->VersionBlock,
                                      pszClassGuid))
               || (pSetupGuidFromString(pInfGuidString, &ClassGuid) != NO_ERROR)) {

                Err = ERROR_INVALID_CLASS;
                leave;
            }

            ClassGuidIsValid = TRUE;

            if(!MYVERIFY(SUCCEEDED(StringCchCopy(ClassGuidStringBuffer,
                                                 SIZECHARS(ClassGuidStringBuffer),
                                                 pInfGuidString
                                                 )))) {
                //
                // "will never fail"
                // but if it does, fail securely
                //
                Err = ERROR_INVALID_CLASS;
                leave;
            }

            //
            // We'll need to get the class name out of the INF as well.
            //
            if(!(ClassName = pSetupGetVersionDatum(&((PLOADED_INF)hInf)->VersionBlock,
                                                   pszClass))) {
                Err = ERROR_INVALID_CLASS;
                leave;
            }

            WriteLogEntry(LogContext,
                          DRIVER_LOG_INFO,
                          MSG_LOG_DO_CLASS_INSTALL,
                          NULL,       // text message
                          ClassGuidStringBuffer,
                          ClassName
                         );
        }

        //
        // First, attempt to open the key (i.e., not create it).  If that
        // fails, then we'll try to create it.  That way, we can keep track of
        // whether clean-up is required if an error occurs.
        //
        if(CR_SUCCESS != CM_Open_Class_Key_Ex(&ClassGuid,
                                              ClassName,
                                              KEY_READ | KEY_WRITE,
                                              RegDisposition_OpenExisting,
                                              &hKey,
                                              InterfaceClassGuid ? CM_OPEN_CLASS_KEY_INTERFACE
                                                                 : CM_OPEN_CLASS_KEY_INSTALLER,
                                              NULL))
        {
            CONFIGRET cr;

            //
            // The key doesn't already exist--we've got to create it.
            //
            cr = CM_Open_Class_Key_Ex(&ClassGuid,
                                      ClassName,
                                      KEY_READ | KEY_WRITE,
                                      RegDisposition_OpenAlways,
                                      &hKey,
                                      (InterfaceClassGuid ? CM_OPEN_CLASS_KEY_INTERFACE
                                                          : CM_OPEN_CLASS_KEY_INSTALLER),
                                      NULL
                                     );

            if(cr != CR_SUCCESS) {
                hKey = INVALID_HANDLE_VALUE; // ensure key handle still invalid
                Err = CR_TO_SP(cr, ERROR_INVALID_DATA);
                leave;
            }

            KeyNewlyCreated = TRUE;
        }

        if(hInf == INVALID_HANDLE_VALUE) {
            //
            // We've done all we need to do to install this device interface.
            //
            leave;

        } else {
            //
            // Append the layout INF, if necessary.
            //
            SetupOpenAppendInfFile(NULL, hInf, NULL);
        }

        if(InterfaceClassGuid) {
            //
            // Look for an entry for this interface class in the
            // [InterfaceInstall32] section of the INF.
            //
            if(!SetupFindFirstLine(hInf,
                                   pszInterfaceInstall32,
                                   ClassGuidStringBuffer,
                                   &InterfaceClassInstallLine)) {
                //
                // No install entry in this INF--we're done.
                //
                leave;
            }

            //
            // Make sure the Flags field is zero.
            //
            if(SetupGetIntField(&InterfaceClassInstallLine, 2, (PINT)&InstallFlags) && InstallFlags) {
                Err = ERROR_BAD_INTERFACE_INSTALLSECT;
                leave;
            }

            if((!(UndecoratedInstallSection = pSetupGetField(&InterfaceClassInstallLine, 1)))
               || !(*UndecoratedInstallSection))
            {
                //
                // No install section was given--we're done.
                //
                leave;
            }

        } else {

            UndecoratedInstallSection = pszClassInstall32;

            ZeroMemory(&RegContext, sizeof(RegContext));
            RegContext.Flags |= INF_PFLAG_CLASSPROP;
            RegContext.ClassGuid = &ClassGuid;

            //
            // Leave RegContext.hMachine as NULL, since we don't support
            // remote installation of either device setup or device interface
            // classes.
            //
        }

        //
        // Get the 'real' (potentially OS/architecture-specific) class install
        // section name.
        //
        Err = GLE_FN_CALL(FALSE,
                          SetupDiGetActualSectionToInstall(
                              hInf,
                              UndecoratedInstallSection,
                              ClassInstallSectionName,
                              SIZECHARS(ClassInstallSectionName),
                              &ClassInstallSectionNameLen,
                              &SectionExtension)
                         );

        if(Err == NO_ERROR) {
            MYASSERT(ClassInstallSectionNameLen > 1);
            ClassInstallSectionNameLen--;   // don't want this to include null
        } else {
            leave;
        }

        //
        // Also say what section is about to be installed.
        //
        WriteLogEntry(LogContext,
                      DRIVER_LOG_VERBOSE,
                      MSG_LOG_CLASS_SECTION,
                      NULL,
                      ClassInstallSectionName
                     );

        //
        // If this is the undecorated name, then make sure that the section
        // actually exists.
        //
        if(!SectionExtension && (SetupGetLineCount(hInf, ClassInstallSectionName) == -1)) {

            Err = ERROR_SECTION_NOT_FOUND;

            WriteLogEntry(LogContext,
                          DRIVER_LOG_ERROR,
                          MSG_LOG_NOSECTION,
                          NULL,
                          ClassInstallSectionName
                         );
            leave;
        }

        if(!(Flags & DI_NOVCP)) {
            //
            // Since we may need to check the queued files to determine whether
            // file copy is necessary, we have to open our own queue, and
            // commit it ourselves.
            //
            Err = GLE_FN_CALL(INVALID_HANDLE_VALUE,
                              FileQueue = SetupOpenFileQueue()
                             );

            if(Err == NO_ERROR) {
                CloseFileQueue = TRUE;
            } else {
                leave;
            }

            NoProgressUI = (GuiSetupInProgress ||
                            (GlobalSetupFlags & PSPGF_NONINTERACTIVE) ||
                            (Flags & DI_QUIETINSTALL));

            if(!(MsgHandlerContext = SetupInitDefaultQueueCallbackEx(
                                         hwndParent,
                                         (NoProgressUI ? INVALID_HANDLE_VALUE : NULL),
                                         0,
                                         0,
                                         NULL))) {
                //
                // This routine doesn't set last error, but the only reason it
                // can fail is due to insufficient memory...
                //
                Err = ERROR_NOT_ENOUGH_MEMORY;
                leave;
            }

            MsgHandler = SetupDefaultQueueCallback;
        }

        //
        // Replace the file queue's log context with current, if it's never
        // been used.  Failure to inherit is OK, because we know the file queue
        // has its own log context.  Thus, the worst that can happen is the log
        // entries go to two different sections.
        //
        InheritLogContext(LogContext, &((PSP_FILE_QUEUE)FileQueue)->LogContext);

        Err = pSetupInstallFiles(hInf,
                                 NULL,
                                 ClassInstallSectionName,
                                 NULL,
                                 NULL,
                                 NULL,
                                 SP_COPY_NEWER_OR_SAME | SP_COPY_LANGUAGEAWARE |
                                     ((Flags & DI_NOBROWSE) ? SP_COPY_NOBROWSE : 0),
                                 NULL,
                                 FileQueue,
                                 //
                                 // This flag is ignored by pSetupInstallFiles
                                 // because we don't pass a callback here and we
                                 // pass a user-defined file queue. (In other words
                                 // we're not committing the queue so there's no
                                 // callback function to deal with, and the callback
                                 // would be the guy who would care about ansi vs unicode.)
                                 //
                                 TRUE
                                );

        if(CloseFileQueue && (Err == NO_ERROR)) {
            //
            // Call _SetupVerifyQueuedCatalogs separately (i.e., don't let it
            // happen automatically as a result of scanning/committing the
            // queue that happens below).  We do this beforehand so that we
            // know what unique name was generated when an OEM INF was
            // installed into %windir%\Inf (in case we need to delete the
            // INF/PNF/CAT files later if we encounter an error).
            //
            WriteLogEntry(LogContext,
                          DRIVER_LOG_TIME,
                          MSG_LOG_BEGIN_INSTCLASS_VERIFY_CAT_TIME,
                          NULL // text message
                         );

            //
            // (NOTE: We don't have the context in this routine to determine
            // whether or not the INF is from the internet.  For now, just
            // assume it isn't.)
            //
            Err = _SetupVerifyQueuedCatalogs(
                      hwndParent,
                      FileQueue,
                      VERCAT_INSTALL_INF_AND_CAT,
                      szNewName,
                      &OemInfFileToCopy
                     );

            WriteLogEntry(LogContext,
                          DRIVER_LOG_TIME,
                          MSG_LOG_END_INSTCLASS_VERIFY_CAT_TIME,
                          NULL // text message
                         );

            if(Err == NO_ERROR) {
                //
                // We successfully queued up the file operations--now we need
                // to commit the queue.  First off, though, we should check to
                // see if the files are already there.  (If the 'force copy'
                // flag is set, then we don't care if the files are already
                // there--we always need to copy them in that case.)
                //
                if(Flags & DI_FORCECOPY) {
                    //
                    // always copy the files.
                    //
                    ScanQueueResult = 0;

                } else {
                    //
                    // Determine whether the queue actually needs to be
                    // committed.
                    //
                    // ScanQueueResult can have 1 of 3 values:
                    //
                    // 0: Some files were missing or invalid (i.e., digital
                    //    signatures weren't verified;
                    //    Must commit queue.
                    //
                    // 1: All files to be copied are already present/valid, and
                    //    the queue is empty;
                    //    Can skip committing queue.
                    //
                    // 2: All files to be copied are already present/valid, but
                    //    del/ren queues not empty.  Must commit queue. The
                    //    copy queue will have been emptied, so only del/ren
                    //    functions will be performed.
                    //
                    if(!SetupScanFileQueue(FileQueue,
                                           SPQ_SCAN_FILE_VALIDITY | SPQ_SCAN_PRUNE_COPY_QUEUE,
                                           hwndParent,
                                           NULL,
                                           NULL,
                                           &ScanQueueResult)) {
                        //
                        // SetupScanFileQueue should really never
                        // fail when you don't ask it to call a
                        // callback routine, but if it does, just
                        // go ahead and commit the queue.
                        //
                        ScanQueueResult = 0;
                    }
                }

                if(ScanQueueResult != 1) {
                    //
                    // Copy enqueued files. In this case the callback is
                    // SetupDefaultQueueCallback, so we know it's native char
                    // width.
                    //
                    Err = GLE_FN_CALL(FALSE,
                                      _SetupCommitFileQueue(hwndParent,
                                                            FileQueue,
                                                            MsgHandler,
                                                            MsgHandlerContext,
                                                            TRUE)
                                     );
                }
            }
        }

        if(Err != NO_ERROR) {
            leave;
        }

        //
        // If we get to here, then the file copying was successful--now we can
        // perform the rest of the installation. We don't pass a callback so we
        // don't worry about ansi vs unicode issues here.
        //
        Err = GLE_FN_CALL(FALSE,
                          _SetupInstallFromInfSection(
                              NULL,
                              hInf,
                              ClassInstallSectionName,
                              SPINST_INIFILES
                              | SPINST_REGISTRY
                              | SPINST_INI2REG
                              | SPINST_BITREG
                              | SPINST_REGSVR
                              | SPINST_UNREGSVR
                              | SPINST_PROFILEITEMS,
                              hKey,
                              NULL,
                              0,
                              NULL,
                              NULL,
                              INVALID_HANDLE_VALUE,
                              NULL,
                              TRUE,
                              (InterfaceClassGuid ? NULL : &RegContext))
                         );

        if(Err != NO_ERROR) {
            leave;
        }

        //
        // The class installer might want to install a Services section.  This
        // allows device setup class registration to include installation of a
        // class-wide driver.
        //
        WriteLogEntry(LogContext,
                      DRIVER_LOG_TIME,
                      MSG_LOG_BEGIN_SERVICE_TIME,
                      NULL // text message
                     );

        //
        // The install section name is of the form:
        //
        //     ClassInstall32[.<ext>].Services
        //
        hr = StringCchCopy(
                 &(ClassInstallSectionName[ClassInstallSectionNameLen]),
                 SIZECHARS(ClassInstallSectionName) - ClassInstallSectionNameLen,
                 pszServicesSectionSuffix
                 );

        if(FAILED(hr)) {
            Err = HRESULT_CODE(hr);
            leave;
        }

        Err = InstallNtService(NULL,
                               hInf,
                               InfFileName,
                               ClassInstallSectionName,
                               NULL,
                               SPSVCINST_NO_DEVINST_CHECK,
                               &NullDriverInstall
                              );

    } except(pSetupExceptionFilter(GetExceptionCode())) {
        pSetupExceptionHandler(GetExceptionCode(), ERROR_INVALID_PARAMETER, &Err);
    }

    if(hKey != INVALID_HANDLE_VALUE) {

        RegCloseKey(hKey);

        //
        // NTRAID#NTBUG9-660148-2002/07/05-lonnym - need to clean up interface keys too!
        //
        if((Err != NO_ERROR) && KeyNewlyCreated && !InterfaceClassGuid) {
            //
            // We hit an error, and the class installer key didn't previously
            // exist, so we want to remove it.
            //
            CM_Delete_Class_Key_Ex(&ClassGuid,
                                   CM_DELETE_CLASS_SUBKEYS,
                                   NULL
                                   );
        }
    }

    if(CloseFileQueue) {
        SetupCloseFileQueue(FileQueue);
    }

    if(MsgHandlerContext) {
        SetupTermDefaultQueueCallback(MsgHandlerContext);
    }

    if(hInf != INVALID_HANDLE_VALUE) {
        SetupCloseInfFile(hInf);
    }

    if(Err == NO_ERROR) {
        //
        // If we're >= DRIVER_LOG_INFO, give a +ve affirmation of install.
        //
        WriteLogEntry(LogContext,
                      DRIVER_LOG_INFO,
                      MSG_LOG_CLASS_INSTALLED,
                      NULL,
                      NULL
                     );
    } else {
        //
        // Log an error about the failure encountered.
        //
        WriteLogEntry(LogContext,
                      DRIVER_LOG_ERROR | SETUP_LOG_BUFFER,
                      MSG_LOG_CLASS_ERROR_ENCOUNTERED,
                      NULL,
                      (ClassGuidIsValid ? ClassGuidStringBuffer : TEXT("*"))
                     );

        WriteLogError(LogContext, DRIVER_LOG_ERROR, Err);

        //
        // If we copied the OEM INF into the INF directory under a
        // newly-generated name, delete it now.
        //
        if(OemInfFileToCopy) {
            pSetupUninstallOEMInf(szNewName,
                                  LogContext,
                                  SUOI_FORCEDELETE,
                                  NULL
                                 );
        }
    }

    if(LogContext) {
        DeleteLogContext(LogContext);
    }

    SetLastError(Err);
    return (Err == NO_ERROR);
}


//
// ANSI version
//
BOOL
WINAPI
SetupDiInstallClassA(
    IN HWND     hwndParent,  OPTIONAL
    IN PCSTR    InfFileName,
    IN DWORD    Flags,
    IN HSPFILEQ FileQueue    OPTIONAL
    )
{
    PCWSTR UnicodeInfFileName = NULL;
    DWORD rc;

    try {

        rc = pSetupCaptureAndConvertAnsiArg(InfFileName, &UnicodeInfFileName);
        if(rc != NO_ERROR) {
            leave;
        }

        rc = GLE_FN_CALL(FALSE,
                         SetupDiInstallClassExW(hwndParent,
                                                UnicodeInfFileName,
                                                Flags,
                                                FileQueue,
                                                NULL,
                                                NULL,
                                                NULL)
                        );

    } except(pSetupExceptionFilter(GetExceptionCode())) {
        pSetupExceptionHandler(GetExceptionCode(), ERROR_INVALID_PARAMETER, &rc);
    }

    if(UnicodeInfFileName) {
        MyFree(UnicodeInfFileName);
    }

    SetLastError(rc);
    return (rc == NO_ERROR);
}

BOOL
WINAPI
SetupDiInstallClass(
    IN HWND     hwndParent,  OPTIONAL
    IN PCTSTR   InfFileName,
    IN DWORD    Flags,
    IN HSPFILEQ FileQueue    OPTIONAL
    )
/*++

Routine Description:

    This routine installs the [ClassInstall32] section of the specified INF.

Arguments:

    hwndParent - Optionally, supplies the handle of the parent window for any
        UI brought up as a result of installing this class.

    InfFileName - Supplies the name of the INF file containing a
        [ClassInstall32] section.

    Flags - Flags that control the installation.  May be a combination of the
        following:

        DI_NOVCP - This flag should be specified if HSPFILEQ is supplied.  This
            instructs SetupInstallFromInfSection to not create a queue of its
            own, and instead to use the caller-supplied one.  If this flag is
            specified, then no file copying will be done.

        DI_NOBROWSE - This flag should be specified if no file browsing should
            be allowed in the event a copy operation cannot find a specified
            file.  If the user supplies their own file queue, then this flag is
            ignored.

        DI_FORCECOPY - This flag should be specified if the files should always
            be copied, even if they're already present on the user's machine
            (i.e., don't ask the user if they want to keep their existing
            files).  If the user supplies their own file queue, then this flag
            is ignored.

        DI_QUIETINSTALL - This flag should be specified if UI should be
            suppressed unless absolutely necessary (i.e., no progress dialog).
            If the user supplies their own queue, then this flag is ignored.

            (NOTE:  During GUI-mode setup on Windows NT, quiet-install behavior
            is always employed in the absence of a user-supplied file queue.)

    FileQueue - If the DI_NOVCP flag is specified, then this parameter supplies
        a handle to a file queue where file operations are to be queued (but
        not committed).

Return Value:

    If the function succeeds, the return value is TRUE.
    If the function fails, the return value is FALSE.  To get extended error
    information, call GetLastError.

Remarks:

    This API is generally called by the "New Hardware Found" process when it
    installs a device of a new device setup class.

--*/
{
    DWORD Err;

    try {

        Err = GLE_FN_CALL(FALSE,
                          SetupDiInstallClassEx(hwndParent,
                                                InfFileName,
                                                Flags,
                                                FileQueue,
                                                NULL,
                                                NULL,
                                                NULL)
                         );

    } except(pSetupExceptionFilter(GetExceptionCode())) {
        pSetupExceptionHandler(GetExceptionCode(), ERROR_INVALID_PARAMETER, &Err);
    }

    SetLastError(Err);
    return (Err == NO_ERROR);
}


//
// ANSI version
//
BOOL
WINAPI
SetupDiGetHwProfileFriendlyNameA(
    IN  DWORD  HwProfile,
    OUT PSTR   FriendlyName,
    IN  DWORD  FriendlyNameSize,
    OUT PDWORD RequiredSize      OPTIONAL
    )
{
    DWORD Err;

    try {

        Err = GLE_FN_CALL(FALSE,
                          SetupDiGetHwProfileFriendlyNameExA(
                              HwProfile,
                              FriendlyName,
                              FriendlyNameSize,
                              RequiredSize,
                              NULL,
                              NULL)
                         );

    } except(pSetupExceptionFilter(GetExceptionCode())) {
        pSetupExceptionHandler(GetExceptionCode(), ERROR_INVALID_PARAMETER, &Err);
    }

    SetLastError(Err);
    return (Err == NO_ERROR);
}

BOOL
WINAPI
SetupDiGetHwProfileFriendlyName(
    IN  DWORD  HwProfile,
    OUT PTSTR  FriendlyName,
    IN  DWORD  FriendlyNameSize,
    OUT PDWORD RequiredSize      OPTIONAL
    )
/*++

Routine Description:

    See SetupDiGetHwProfileFriendlyNameEx for details.

--*/

{
    DWORD Err;

    try {

        Err = GLE_FN_CALL(FALSE,
                          SetupDiGetHwProfileFriendlyNameEx(
                              HwProfile,
                              FriendlyName,
                              FriendlyNameSize,
                              RequiredSize,
                              NULL,
                              NULL)
                         );

    } except(pSetupExceptionFilter(GetExceptionCode())) {
        pSetupExceptionHandler(GetExceptionCode(), ERROR_INVALID_PARAMETER, &Err);
    }

    SetLastError(Err);
    return (Err == NO_ERROR);
}


//
// ANSI version
//
BOOL
WINAPI
SetupDiGetHwProfileFriendlyNameExA(
    IN  DWORD  HwProfile,
    OUT PSTR   FriendlyName,
    IN  DWORD  FriendlyNameSize,
    OUT PDWORD RequiredSize,     OPTIONAL
    IN  PCSTR  MachineName,      OPTIONAL
    IN  PVOID  Reserved
    )
{
    WCHAR UnicodeName[MAX_PROFILE_LEN];
    PSTR AnsiName = NULL;
    DWORD rc;
    DWORD LocalRequiredSize;
    PCWSTR UnicodeMachineName = NULL;
    HRESULT hr;

    try {
        //
        // If you pass a NULL buffer pointer, the size had better be zero!
        //
        if(!FriendlyName && FriendlyNameSize) {
            rc = ERROR_INVALID_PARAMETER;
            leave;
        }

        if(MachineName) {
            rc = pSetupCaptureAndConvertAnsiArg(MachineName, &UnicodeMachineName);
            if(rc != NO_ERROR) {
                leave;
            }
        }

        rc = GLE_FN_CALL(FALSE,
                         SetupDiGetHwProfileFriendlyNameExW(
                             HwProfile,
                             UnicodeName,
                             SIZECHARS(UnicodeName),
                             &LocalRequiredSize,
                             UnicodeMachineName,
                             Reserved)
                        );

        if(rc != NO_ERROR) {
            leave;
        }

        AnsiName = pSetupUnicodeToAnsi(UnicodeName);

        if(!AnsiName) {
            rc = ERROR_NOT_ENOUGH_MEMORY;
            leave;
        }

        LocalRequiredSize = lstrlenA(AnsiName) + 1;

        if(RequiredSize) {
            *RequiredSize = LocalRequiredSize;
        }

        if(!FriendlyName) {
            rc = ERROR_INSUFFICIENT_BUFFER;
            leave;
        }

        hr = StringCchCopyA(FriendlyName,
                            (size_t)FriendlyNameSize,
                            AnsiName
                           );

        if(FAILED(hr)) {
            rc = HRESULT_CODE(hr);
        }

    } except(pSetupExceptionFilter(GetExceptionCode())) {
        pSetupExceptionHandler(GetExceptionCode(), ERROR_INVALID_PARAMETER, &rc);
    }

    if(AnsiName) {
        MyFree(AnsiName);
    }

    if(UnicodeMachineName) {
        MyFree(UnicodeMachineName);
    }

    SetLastError(rc);
    return (rc == NO_ERROR);
}

BOOL
WINAPI
SetupDiGetHwProfileFriendlyNameEx(
    IN  DWORD  HwProfile,
    OUT PTSTR  FriendlyName,
    IN  DWORD  FriendlyNameSize,
    OUT PDWORD RequiredSize,     OPTIONAL
    IN  PCTSTR MachineName,      OPTIONAL
    IN  PVOID  Reserved
    )
/*++

Routine Description:

    This routine retrieves the friendly name associated with a hardware profile
    ID.

Arguments:

    HwProfile - Supplies the hardware profile ID whose friendly name is to be
        retrieved.  If this parameter is 0, then the friendly name for the
        current hardware profile is retrieved.

    FriendlyName - Supplies the address of a character buffer that receives the
        friendly name of the hardware profile.

    FriendlyNameSize - Supplies the size, in characters, of the FriendlyName
        buffer.

    RequiredSize - Optionally, supplies the address of a variable that receives
        the number of characters required to store the friendly name (including
        terminating NULL).

    MachineName - Optionally, supplies the name of the remote machine
        containing the hardware profile whose friendly name is to be retrieved.
        If this parameter is not specified, the local machine is used.

    Reserved - Reserved for future use--must be NULL.

Return Value:

    If the function succeeds, the return value is TRUE.
    If the function fails, the return value is FALSE.  To get extended error
    information, call GetLastError.

--*/

{
    DWORD Err = ERROR_INVALID_HWPROFILE;
    HWPROFILEINFO HwProfInfo;
    ULONG i;
    CONFIGRET cr;
    size_t NameLen;
    HMACHINE hMachine = NULL;
    HRESULT hr;

    try {
        //
        // If you pass a NULL buffer pointer, the size had better be zero!
        //
        if(!FriendlyName && FriendlyNameSize) {
            Err = ERROR_INVALID_PARAMETER;
            leave;
        }

        //
        // Make sure the caller didn't pass us anything in the Reserved
        // parameter.
        //
        if(Reserved) {
            Err = ERROR_INVALID_PARAMETER;
            leave;
        }

        //
        // If the caller specified a remote machine name, connect to that
        // machine.
        //
        if(MachineName) {
            cr = CM_Connect_Machine(MachineName, &hMachine);
            if(cr != CR_SUCCESS) {
                Err = MapCrToSpError(cr, ERROR_INVALID_DATA);
                leave;
            }
        }

        //
        // If a hardware profile ID of 0 is specified, then retrieve
        // information about the current hardware profile, otherwise, enumerate
        // the hardware profiles, looking for the one specified.
        //
        if(HwProfile) {
            i = 0;
        } else {
            i = 0xFFFFFFFF;
        }

        do {

            if((cr = CM_Get_Hardware_Profile_Info_Ex(i, &HwProfInfo, 0, hMachine)) == CR_SUCCESS) {
                //
                // Hardware profile info retrieved--see if it's what we're
                // looking for.
                //
                if(!HwProfile || (HwProfInfo.HWPI_ulHWProfile == HwProfile)) {

                    hr = StringCchLength(HwProfInfo.HWPI_szFriendlyName,
                                         SIZECHARS(HwProfInfo.HWPI_szFriendlyName),
                                         &NameLen
                                        );

                    if(FAILED(hr)) {
                        //
                        // CM API gave us garbage!!!
                        //
                        MYASSERT(FALSE);
                        Err = ERROR_INVALID_DATA;
                        leave;
                    }

                    NameLen++;  // include terminating null char

                    if(RequiredSize) {
                        *RequiredSize = (DWORD)NameLen;
                    }

                    if((DWORD)NameLen > FriendlyNameSize) {
                        Err = ERROR_INSUFFICIENT_BUFFER;
                    } else {
                        Err = NO_ERROR;
                        CopyMemory(FriendlyName,
                                   HwProfInfo.HWPI_szFriendlyName,
                                   NameLen * sizeof(TCHAR)
                                  );
                    }

                    break;
                }
                //
                // This wasn't the profile we wanted--go on to the next one.
                //
                i++;

            } else if(!HwProfile || (cr != CR_NO_SUCH_VALUE)) {
                //
                // We should abort on any error other than CR_NO_SUCH_VALUE,
                // otherwise we might loop forever!
                //
                Err = ERROR_INVALID_DATA;
                break;
            }

        } while(cr != CR_NO_SUCH_VALUE);

    } except(pSetupExceptionFilter(GetExceptionCode())) {
        pSetupExceptionHandler(GetExceptionCode(), ERROR_INVALID_PARAMETER, &Err);
    }

    if(hMachine) {
        CM_Disconnect_Machine(hMachine);
    }

    SetLastError(Err);
    return (Err == NO_ERROR);
}


BOOL
WINAPI
SetupDiGetHwProfileList(
    OUT PDWORD HwProfileList,
    IN  DWORD  HwProfileListSize,
    OUT PDWORD RequiredSize,
    OUT PDWORD CurrentlyActiveIndex OPTIONAL
    )
/*++

Routine Description:

    This routine retrieves a list of all currently-defined hardware profile
    IDs.

Arguments:

    HwProfileList - Supplies the address of an array of DWORDs that will
        receive the list of currently defined hardware profile IDs.

    HwProfileListSize - Supplies the number of DWORDs in the HwProfileList
        array.

    RequiredSize - Supplies the address of a variable that receives the number
        of hardware profiles currently defined.  If this number is larger than
        HwProfileListSize, then the list will be truncated to fit the array
        size, and this value will indicate the array size that would be
        required to store the entire list (the function will fail, with
        GetLastError returning ERROR_INSUFFICIENT_BUFFER in that case).

    CurrentlyActiveIndex - Optionally, supplies the address of a variable that
        receives the index within the HwProfileList array of the currently
        active hardware profile.

Return Value:

    If the function succeeds, the return value is TRUE.
    If the function fails, the return value is FALSE.  To get extended error
    information, call GetLastError.

--*/

{
    DWORD Err;

    try {

        Err = GLE_FN_CALL(FALSE,
                          SetupDiGetHwProfileListEx(HwProfileList,
                                                    HwProfileListSize,
                                                    RequiredSize,
                                                    CurrentlyActiveIndex,
                                                    NULL,
                                                    NULL)
                         );

    } except(pSetupExceptionFilter(GetExceptionCode())) {
        pSetupExceptionHandler(GetExceptionCode(), ERROR_INVALID_PARAMETER, &Err);
    }

    SetLastError(Err);
    return (Err == NO_ERROR);
}


//
// ANSI version
//
BOOL
WINAPI
SetupDiGetHwProfileListExA(
    OUT PDWORD HwProfileList,
    IN  DWORD  HwProfileListSize,
    OUT PDWORD RequiredSize,
    OUT PDWORD CurrentlyActiveIndex, OPTIONAL
    IN  PCSTR  MachineName,          OPTIONAL
    IN  PVOID  Reserved
    )
{
    PCWSTR UnicodeMachineName = NULL;
    DWORD rc;

    try {

        if(MachineName) {
            rc = pSetupCaptureAndConvertAnsiArg(MachineName, &UnicodeMachineName);
            if(rc != NO_ERROR) {
                leave;
            }
        }

        rc = GLE_FN_CALL(FALSE,
                         SetupDiGetHwProfileListExW(HwProfileList,
                                                    HwProfileListSize,
                                                    RequiredSize,
                                                    CurrentlyActiveIndex,
                                                    UnicodeMachineName,
                                                    Reserved)
                        );

    } except(pSetupExceptionFilter(GetExceptionCode())) {
        pSetupExceptionHandler(GetExceptionCode(), ERROR_INVALID_PARAMETER, &rc);
    }

    if(UnicodeMachineName) {
        MyFree(UnicodeMachineName);
    }

    SetLastError(rc);
    return (rc == NO_ERROR);
}

BOOL
WINAPI
SetupDiGetHwProfileListEx(
    OUT PDWORD HwProfileList,
    IN  DWORD  HwProfileListSize,
    OUT PDWORD RequiredSize,
    OUT PDWORD CurrentlyActiveIndex, OPTIONAL
    IN  PCTSTR MachineName,          OPTIONAL
    IN  PVOID  Reserved
    )
/*++

Routine Description:

    This routine retrieves a list of all currently-defined hardware profile
    IDs.

Arguments:

    HwProfileList - Supplies the address of an array of DWORDs that will
        receive the list of currently defined hardware profile IDs.

    HwProfileListSize - Supplies the number of DWORDs in the HwProfileList
        array.

    RequiredSize - Supplies the address of a variable that receives the number
        of hardware profiles currently defined.  If this number is larger than
        HwProfileListSize, then the list will be truncated to fit the array
        size, and this value will indicate the array size that would be
        required to store the entire list (the function will fail, with
        GetLastError returning ERROR_INSUFFICIENT_BUFFER in that case).

    CurrentlyActiveIndex - Optionally, supplies the address of a variable that
        receives the index within the HwProfileList array of the currently
        active hardware profile.

    MachineName - Optionally, specifies the name of the remote machine to
        retrieve a list of hardware profiles for.

    Reserved - Reserved for future use--must be NULL.

Return Value:

    If the function succeeds, the return value is TRUE.
    If the function fails, the return value is FALSE.  To get extended error
    information, call GetLastError.

--*/

{
    DWORD Err = NO_ERROR;
    DWORD CurHwProfile;
    HWPROFILEINFO HwProfInfo;
    ULONG i;
    CONFIGRET cr = CR_SUCCESS;
    HMACHINE hMachine = NULL;

    try {
        //
        // Make sure the caller didn't pass us anything in the Reserved
        // parameter.
        //
        if(Reserved) {
            Err = ERROR_INVALID_PARAMETER;
            leave;
        }

        //
        // If the caller specified a null buffer pointer, its size had better
        // be zero.
        //
        if(!HwProfileList && HwProfileListSize) {
            Err = ERROR_INVALID_PARAMETER;
            leave;
        }

        //
        // If the caller specified a remote machine name, connect to that
        // machine now.
        //
        if(MachineName) {
            cr = CM_Connect_Machine(MachineName, &hMachine);
            if(cr != CR_SUCCESS) {
                Err = MapCrToSpError(cr, ERROR_INVALID_DATA);
                leave;
            }
        }

        //
        // First retrieve the currently active hardware profile ID, so we'll
        // know what to look for when we're enumerating all profiles (only need
        // to do this if the user wants the index of the currently active
        // hardware profile).
        //
        if(CurrentlyActiveIndex) {

            if((cr = CM_Get_Hardware_Profile_Info_Ex(0xFFFFFFFF, &HwProfInfo, 0, hMachine)) == CR_SUCCESS) {
                //
                // Store away the hardware profile ID.
                //
                CurHwProfile = HwProfInfo.HWPI_ulHWProfile;

            } else {
                Err = MapCrToSpError(cr, ERROR_INVALID_DATA);
                leave;
            }
        }

        //
        // Enumerate the hardware profiles, retrieving the ID for each.
        //
        i = 0;
        do {

            if((cr = CM_Get_Hardware_Profile_Info_Ex(i, &HwProfInfo, 0, hMachine)) == CR_SUCCESS) {
                if(i < HwProfileListSize) {
                    HwProfileList[i] = HwProfInfo.HWPI_ulHWProfile;
                }
                if(CurrentlyActiveIndex && (HwProfInfo.HWPI_ulHWProfile == CurHwProfile)) {
                    *CurrentlyActiveIndex = i;
                    //
                    // Clear the CurrentlyActiveIndex pointer, so we once we find the
                    // currently active profile, we won't have to keep comparing.
                    //
                    CurrentlyActiveIndex = NULL;
                }
                i++;
            }

        } while(cr == CR_SUCCESS);

        if(cr == CR_NO_MORE_HW_PROFILES) {
            //
            // Then we enumerated all hardware profiles.  Now see if we had
            // enough buffer to hold them all.
            //
            *RequiredSize = i;
            if(i > HwProfileListSize) {
                Err = ERROR_INSUFFICIENT_BUFFER;
            }
        } else {
            //
            // Something else happened (probably a key not present).
            //
            Err = MapCrToSpError(cr, ERROR_INVALID_DATA);
        }

    } except(pSetupExceptionFilter(GetExceptionCode())) {
        pSetupExceptionHandler(GetExceptionCode(), ERROR_INVALID_PARAMETER, &Err);
    }

    if(hMachine) {
        CM_Disconnect_Machine(hMachine);
    }

    SetLastError(Err);
    return (Err == NO_ERROR);
}


DWORD
pSetupDiGetCoInstallerList(
    IN     HDEVINFO                 DeviceInfoSet,     OPTIONAL
    IN     PSP_DEVINFO_DATA         DeviceInfoData,    OPTIONAL
    IN     CONST GUID              *ClassGuid,         OPTIONAL
    IN OUT PDEVINSTALL_PARAM_BLOCK  InstallParamBlock,
    IN OUT PVERIFY_CONTEXT          VerifyContext      OPTIONAL
    )
/*++

Routine Description:

    This routine retrieves the list of co-installers (both class- and
    device-specific) and stores the entry points and module handles in
    the supplied install param block.

Arguments:

    DeviceInfoSet - Supplies a handle to the device information set to retrieve
        co-installers into.  If DeviceInfoSet is not specified, then the
        InstallParamBlock specified below will be that of the set itself.

    DeviceInfoData - Optionally, specifies the device information element
        for which a list of co-installers is to be retrieved.

    ClassGuid - Optionally, supplies the address of the device setup class GUID
        for which class-specific co-installers are to be retrieved.

    InstallParamBlock - Supplies the address of the install param block where
        the co-installer list is to be stored.  This will either be the param
        block of the set itself (if DeviceInfoData isn't specified), or of
        the specified device information element.
            
    VerifyContext - optionally, supplies the address of a structure that caches
        various verification context handles.  These handles may be NULL (if
        not previously acquired, and they may be filled in upon return (in
        either success or failure) if they were acquired during the processing
        of this verification request.  It is the caller's responsibility to
        free these various context handles when they are no longer needed by
        calling pSetupFreeVerifyContextMembers.

Return Value:

    If the function succeeds, the return value is NO_ERROR, otherwise it is
    a Win32 error code indicating the cause of failure.

--*/
{
    HKEY hk[2] = {INVALID_HANDLE_VALUE, INVALID_HANDLE_VALUE};
    DWORD Err, RegDataType, KeyIndex;
    LONG i;
    PTSTR CoInstallerBuffer;
    DWORD CoInstallerBufferSize;
    PTSTR CurEntry;
    PCOINSTALLER_NODE CoInstallerList, TempCoInstallerList;
    DWORD CoInstallerListSize;
    TCHAR GuidString[GUID_STRING_LEN];
    TCHAR DescBuffer[LINE_LEN];
    PTSTR DeviceDesc;
    HWND hwndParent;
    BOOL MustAbort;

    MYASSERT(sizeof(GuidString) == sizeof(pszGuidNull));

    //
    // If there is already a list, then return success immediately.
    //
    if(InstallParamBlock->CoInstallerCount != -1) {
        return NO_ERROR;
    }

    //
    // Retrieve the parent window handle, as we may need it below if we need to
    // popup UI due to unsigned class-/co-installers.
    //
    if(hwndParent = InstallParamBlock->hwndParent) {
       if(!IsWindow(hwndParent)) {
            hwndParent = NULL;
       }
    }

    //
    // Retrieve a device description to use in case we need to give a driver
    // signing warn/block popup.
    //
    if(GetBestDeviceDesc(DeviceInfoSet, DeviceInfoData, DescBuffer)) {
        DeviceDesc = DescBuffer;
    } else {
        DeviceDesc = NULL;
    }

    //
    // Get the string form of the class GUID, because that will be the name of
    // the multisz value entry under HKLM\System\CCS\Control\CoDeviceInstallers
    // where class-specific co-installers will be registered
    //
    if(ClassGuid) {

        pSetupStringFromGuid(ClassGuid, GuidString, SIZECHARS(GuidString));

    } else {

        CopyMemory(GuidString, pszGuidNull, sizeof(pszGuidNull));
    }

    CoInstallerBuffer = NULL;
    CoInstallerBufferSize = 256 * sizeof(TCHAR);    // start out with 256-character buffer
    CoInstallerList = NULL;
    i = 0;

    try {
        //
        // Open the CoDeviceInstallers key, as well as the device's driver key (if
        // a devinfo element is specified).
        //
        Err = RegOpenKeyEx(HKEY_LOCAL_MACHINE,
                           pszPathCoDeviceInstallers,
                           0,
                           KEY_READ,
                           &(hk[0])
                          );

        if(Err != ERROR_SUCCESS) {
            hk[0] = INVALID_HANDLE_VALUE;
        }

        if(DeviceInfoData) {

            hk[1] = SetupDiOpenDevRegKey(DeviceInfoSet,
                                         DeviceInfoData,
                                         DICS_FLAG_GLOBAL,
                                         0,
                                         DIREG_DRV,
                                         KEY_READ
                                        );

        } else {
            hk[1] = INVALID_HANDLE_VALUE;
        }

        for(KeyIndex = 0; KeyIndex < 2; KeyIndex++) {
            //
            // If we couldn't open a key for this location, move on to the next
            // one.
            //
            if(hk[KeyIndex] == INVALID_HANDLE_VALUE) {
                continue;
            }

            //
            // Retrieve the multi-sz value containing the co-installer entries.
            //
            while(TRUE) {

                if(!CoInstallerBuffer) {
                    if(!(CoInstallerBuffer = MyMalloc(CoInstallerBufferSize))) {
                        Err = ERROR_NOT_ENOUGH_MEMORY;
                        break;
                    }
                }

                Err = RegQueryValueEx(hk[KeyIndex],
                                      (KeyIndex ? pszCoInstallers32
                                                : GuidString),
                                      NULL,
                                      &RegDataType,
                                      (PBYTE)CoInstallerBuffer,
                                      &CoInstallerBufferSize
                                     );

                if(Err == ERROR_MORE_DATA) {
                    //
                    // Buffer wasn't large enough--free current one and try again with new size.
                    //
                    MyFree(CoInstallerBuffer);
                    CoInstallerBuffer = NULL;
                } else {
                    break;
                }
            }

            //
            // Only out-of-memory errors are treated as fatal here.
            //
            if(Err == ERROR_NOT_ENOUGH_MEMORY) {
                leave;
            } else if(Err == ERROR_SUCCESS) {
                //
                // Make sure the buffer we got back looks valid.
                //
                if((RegDataType != REG_MULTI_SZ) || (CoInstallerBufferSize < sizeof(TCHAR))) {
                    Err = ERROR_INVALID_COINSTALLER;
                    leave;
                }

                //
                // Count the number of entries in this multi-sz list.
                //
                for(CoInstallerListSize = 0, CurEntry = CoInstallerBuffer;
                    *CurEntry;
                    CoInstallerListSize++, CurEntry += (lstrlen(CurEntry) + 1)
                   );

                if(!CoInstallerListSize) {
                    //
                    // List is empty, move on to next one.
                    //
                    continue;
                }

                //
                // Allocate (or reallocate) an array large enough to hold this
                // many co-installer entries.
                //
                if(CoInstallerList) {
                    TempCoInstallerList = MyRealloc(CoInstallerList,
                                                    (CoInstallerListSize + i) * sizeof(COINSTALLER_NODE)
                                                   );
                } else {
                    MYASSERT(i == 0);
                    TempCoInstallerList = MyMalloc(CoInstallerListSize * sizeof(COINSTALLER_NODE));
                }

                if(TempCoInstallerList) {
                    CoInstallerList = TempCoInstallerList;
                } else {
                    Err = ERROR_NOT_ENOUGH_MEMORY;
                    leave;
                }

                //
                // Now loop through the list and get the co-installer for each
                // entry.
                //
                for(CurEntry = CoInstallerBuffer; *CurEntry; CurEntry += (lstrlen(CurEntry) + 1)) {
                    //
                    // Initialize the hinstance to NULL, so we'll know whether
                    // or not we need to free the module if we hit an exception
                    // here.
                    //
                    CoInstallerList[i].hinstCoInstaller = NULL;

                    Err = GetModuleEntryPoint(INVALID_HANDLE_VALUE,
                                              CurEntry,
                                              pszCoInstallerDefaultProc,
                                              &(CoInstallerList[i].hinstCoInstaller),
                                              &((FARPROC)CoInstallerList[i].CoInstallerEntryPoint),
                                              &(CoInstallerList[i].CoInstallerFusionContext),
                                              &MustAbort,
                                              InstallParamBlock->LogContext,
                                              hwndParent,
                                              ClassGuid,
                                              SetupapiVerifyCoInstProblem,
                                              DeviceDesc,
                                              DRIVERSIGN_NONE,
                                              TRUE,
                                              VerifyContext
                                             );

                    if(Err == NO_ERROR) {
                        i++;
                    } else {
                        //
                        // If the error we encountered above causes us to abort
                        // (e.g., due to a driver signing problem), then get
                        // out now.  Otherwise, just skip this failed entry and
                        // move on to the next.
                        //
                        if(MustAbort) {
                            leave;
                        }
                    }
                }
            }
            if(CoInstallerBuffer) {
                MyFree(CoInstallerBuffer);
                CoInstallerBuffer = NULL;
            }
        }

        //
        // If we get to here then we've successfully retrieved the co-installer
        // list(s)
        //
        Err = NO_ERROR;

    } except(pSetupExceptionFilter(GetExceptionCode())) {
        pSetupExceptionHandler(GetExceptionCode(), ERROR_INVALID_COINSTALLER, &Err);
    }

    if(CoInstallerBuffer) {
        MyFree(CoInstallerBuffer);
    }

    for(KeyIndex = 0; KeyIndex < 2; KeyIndex++) {
        if(hk[KeyIndex] != INVALID_HANDLE_VALUE) {
            RegCloseKey(hk[KeyIndex]);
        }
    }

    if(Err == NO_ERROR) {
        InstallParamBlock->CoInstallerList  = CoInstallerList;
        InstallParamBlock->CoInstallerCount = i;
    } else if(CoInstallerList) {
        MyFree(CoInstallerList);
    }

    return Err;
}

