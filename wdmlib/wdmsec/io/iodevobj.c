/*++

Copyright (c) Microsoft Corporation. All rights reserved.

Module Name:

    IoDevObj.c

Abstract:

    This module contains functions for managing device objects.

Author:

    Adrian J. Oney  - April 21, 2002

Revision History:

--*/

#include "WlDef.h"
#include "IopDevObj.h"
#pragma hdrstop

#ifdef ALLOC_PRAGMA
#pragma alloc_text(PAGE, IoDevObjCreateDeviceSecure)
#pragma alloc_text(PAGE, IopDevObjAdjustNewDeviceParameters)
#pragma alloc_text(PAGE, IopDevObjApplyPostCreationSettings)
#endif


NTSTATUS
IoDevObjCreateDeviceSecure(
    IN  PDRIVER_OBJECT      DriverObject,
    IN  ULONG               DeviceExtensionSize,
    IN  PUNICODE_STRING     DeviceName              OPTIONAL,
    IN  DEVICE_TYPE         DeviceType,
    IN  ULONG               DeviceCharacteristics,
    IN  BOOLEAN             Exclusive,
    IN  PCUNICODE_STRING    DefaultSDDLString,
    IN  LPCGUID             DeviceClassGuid         OPTIONAL,
    OUT PDEVICE_OBJECT     *DeviceObject
    )
/*++

Routine Description:

    This routine creates a securable named device object. The security settings
    for the device object are retrieved from the registry or constructed using
    the passed in defaults if registry overrides are not available.

    It should be used
      1. To secure legacy device objects
      2. To secure raw PnP PDOs

    It should not be used to create FDOs, non-raw PDOs, or any unnamed objects.
    For those operations, IoCreateDevice should be used.

Arguments:

    DriverObject - A pointer to the driver object for this device.

    DeviceExtensionSize - Size, in bytes, of extension to device object;
        i.e., the size of the driver-specific data for this device object.

    DeviceName - Optional name that should be associated with this device.
        If the DeviceCharacteristics has the FILE_AUTOGENERATED_DEVICE_NAME
        flag set, this parameter is ignored.

    DeviceType - The type of device that the device object should represent.
        Possibly overriden by registry.

    DeviceCharacteristics - The characteristics for the device. Additional
        flags may be supplied by the registry.

    Exclusive - Indicates that the device object should be created with using
        the exclusive object attribute. Possibly overriden by registry.

        NOTE: This flag should not be used for WDM drivers.  Since only the
        PDO is named, it is the only device object in a devnode attachment
        stack that is openable.  However, since this device object is created
        by the underlying bus driver (which has no knowledge about what type
        of device this is), there is no way to know whether this flag should
        be set.  Therefore, this parameter should always be FALSE for WDM
        drivers.  Drivers attached to the PDO (e.g., the function driver) must
        enforce any exclusivity rules.

    DefaultSDDLString - In the absense of registry settings, this string
        specifies the security to supply for the device object.

        Only the subset of the SDDL format is currently supported. The format
        is:
          D:P(ACE)(ACE)(ACE), where (ACE) is (AceType;;Access;;;SID)

        Where:

          AceType - Only Allow ("A") is supported.
          Access - Rights specified in either hex format (0xnnnnnnnn), or via the
                   SDDL Generic/Standard abbreviations
          SID - Abbreviated security ID
                (WD, BA, SY, IU, RC, AU, NU, AN, BG, BU, LS, NS)
                The S-w-x-y-z form for SIDs is not supported

          The unimplemented ace fields are:

            AceFlags - Describes features such as inheritance for sub-objects
                (ie files) and containers (ie keys/folders). An example SDDL
                AceFlag string would be ("OICI"). While control over
                inheritance is crucial for registry keys and files, it's
                irrelevant for device objects. As such, this function supports
                no ACE flags.

            ObjectGuid - Used for describing rights that transcent the 32bit
                mask supplied by the OS. Typically used for Active Directory
                objects.

            InheritObjectGuid - - Used for describing rights that transcent the
                32bit mask supplied by the OS. Typically used for Active
                Directory objects.

        Example -
          "D:P(A;;GA;;;SY)" which is Allow System to have Generic All access.

    DeviceClassGuid - Supplies a device install class GUID. This class is
        looked up in the registry to see if any potential overrides exist.
        For legacy device objects, the caller may need to invent an appropriate
        class GUID (see IoCreateDeviceSecure documention on how to properly
        install a full class).

        Note that if no registry override exists, the registry will
        automatically be updated to *reflect* the default SDDL string.
        Therefore it is a very bad idea to use the same device class GUID with
        different DefaultSDDLString values (objects needing different default
        security should have different classes, or be secured via INFs where
        possible).

    DeviceObject - Pointer to the device object pointer this routine will
        return.

Return Value:

    NTSTATUS.

--*/
{
    PSECURITY_DESCRIPTOR securityDescriptor;
    STACK_CREATION_SETTINGS stackSettings, updateSettings;
    PDEVICE_OBJECT newDeviceObject;
    UNICODE_STRING classKeyName;
    DEVICE_TYPE finalDeviceType;
    ULONG finalCharacteristics;
    BOOLEAN finalExclusivity;
    ULONG disposition;
    NTSTATUS status;

    PAGED_CODE();

    //
    // Preinit for failure
    //
    *DeviceObject = NULL;
    newDeviceObject = NULL;

    //
    // The device object is securable only if it has a name. Therefore, we fail
    // the create call if the device doesn't have a name.
    //
    if (!(ARGUMENT_PRESENT(DeviceName) ||
        (DeviceCharacteristics & FILE_AUTOGENERATED_DEVICE_NAME))) {

        return STATUS_INVALID_PARAMETER;
    }

    if (ARGUMENT_PRESENT(DeviceClassGuid)) {

        //
        // Try to find the appropriate security descriptor for the device. First
        // look for an override in the registry using the class GUID. We will
        // create a section in the registry if one doesn't exist as well. This is
        // a clue to the system administrator that there is something to lock down
        // in the system.
        //
        status = PpRegStateReadCreateClassCreationSettings(
            DeviceClassGuid,
            DriverObject,
            &stackSettings
            );

        if (!NT_SUCCESS(status)) {

            return status;
        }

    } else {

        PpRegStateInitEmptyCreationSettings(&stackSettings);
    }

    //
    // If a registry setting wasn't specified, parse the default SDDL string.
    //
    if (!(stackSettings.Flags & DSIFLAG_SECURITY_DESCRIPTOR)) {

        //
        // Parse the SDDL string into a security descriptor, and mark it
        // "default" as well. SE_DACL_DEFAULT means the DACL came from a
        // "default" mechanism, typically implying parental inheritance or
        // object default security. In our case, the "default source" is the
        // library as opposed to the user or an INF.
        //
        status = SeSddlSecurityDescriptorFromSDDL(
            DefaultSDDLString,
            TRUE,
            &securityDescriptor
            );

        if (!NT_SUCCESS(status)) {

            goto Exit;
        }

        PpRegStateLoadSecurityDescriptor(
            securityDescriptor,
            &stackSettings
            );

        if (ARGUMENT_PRESENT(DeviceClassGuid)) {

            //
            // Update the registry with the default SDDL string so that the
            // admin knows what settings are being used for this class. Note
            // that we don't free updateSettings, as the security descriptor
            // is also used by stackSettings.
            //
            PpRegStateInitEmptyCreationSettings(&updateSettings);

            PpRegStateLoadSecurityDescriptor(
                securityDescriptor,
                &updateSettings
                );

            status = PpRegStateUpdateStackCreationSettings(
                DeviceClassGuid,
                &updateSettings
                );

            if (!NT_SUCCESS(status)) {

                goto Exit;
            }
        }
    }

    //
    // Fill out the default values
    //
    finalDeviceType = DeviceType;
    finalCharacteristics = DeviceCharacteristics;
    finalExclusivity = Exclusive;

    //
    // Adjust the parameters based on the registry overrides
    //
    IopDevObjAdjustNewDeviceParameters(
        &stackSettings,
        &finalDeviceType,
        &finalCharacteristics,
        &finalExclusivity
        );

    //
    // Create the device object. The newly created object should have the
    // DO_DEVICE_INITIALIZING flag on it, meaning it cannot be opened. We
    // therefore still have an opportunity to apply security.
    //
    status = IoCreateDevice(
        DriverObject,
        DeviceExtensionSize,
        DeviceName,
        finalDeviceType,
        finalCharacteristics,
        finalExclusivity,
        &newDeviceObject
        );

    if (!NT_SUCCESS(status)) {

        goto Exit;
    }

    ASSERT(newDeviceObject->Flags & DO_DEVICE_INITIALIZING);

    status = IopDevObjApplyPostCreationSettings(
        newDeviceObject,
        &stackSettings
        );

    if (!NT_SUCCESS(status)) {

        IoDeleteDevice(newDeviceObject);
        goto Exit;
    }

    *DeviceObject = newDeviceObject;

Exit:

    //
    // Clean up the security descriptor as appropriate.
    //
    PpRegStateFreeStackCreationSettings(&stackSettings);

    return status;
}


VOID
IopDevObjAdjustNewDeviceParameters(
    IN      PSTACK_CREATION_SETTINGS    StackCreationSettings,
    IN OUT  PDEVICE_TYPE                DeviceType,
    IN OUT  PULONG                      DeviceCharacteristics,
    IN OUT  PBOOLEAN                    Exclusive
    )
/*++

Routine Description:

    This routine adjusts a newly created device object to reflect the passed
    in stack creation settings.

Arguments:

    StackCreationSettings - Information reflecting the settings to apply.

    DeviceType - On input, the device type specified by the caller. This field
        is updated to reflect any changes specified in the registry.

    DeviceCharacteristics - On input, the characteristics specified by the
        caller. This field is updated to reflect any changes specified in the
        registry.

    Exclusive - On input, the exclusivity specified by the caller. This field
        is updated to reflect any changes specified in the registry.

Return Value:

    None.

--*/
{
    PAGED_CODE();

    if (StackCreationSettings->Flags & DSIFLAG_DEVICE_TYPE) {

        *DeviceType = StackCreationSettings->DeviceType;
    }

    if (StackCreationSettings->Flags & DSIFLAG_CHARACTERISTICS) {

        *DeviceCharacteristics = StackCreationSettings->Characteristics;
    }

    if (StackCreationSettings->Flags & DSIFLAG_EXCLUSIVE) {

        *Exclusive = (BOOLEAN) StackCreationSettings->Exclusivity;
    }
}


NTSTATUS
IopDevObjApplyPostCreationSettings(
    IN  PDEVICE_OBJECT              DeviceObject,
    IN  PSTACK_CREATION_SETTINGS    StackCreationSettings
    )
/*++

Routine Description:

    This routine adjusts a newly created device object to reflect the passed
    in stack creation settings.

Arguments:

    DeviceObject - Device object who's settings to adjust.

    StackCreationSettings - Information reflecting the settings to apply.

Return Value:

    NTSTATUS.

--*/
{
    SECURITY_INFORMATION securityInformation;
    ACCESS_MASK desiredAccess;
    BOOLEAN fromDefaultSource;
    NTSTATUS status;
    HANDLE handle;

    PAGED_CODE();

    if (!(StackCreationSettings->Flags & DSIFLAG_SECURITY_DESCRIPTOR)) {

        return STATUS_SUCCESS;
    }

    //
    // Get the corresponding securityInformation from the descriptor.
    //
    status = SeUtilSecurityInfoFromSecurityDescriptor(
        StackCreationSettings->SecurityDescriptor,
        &fromDefaultSource,
        &securityInformation
        );

    if (!NT_SUCCESS(status)) {

        return status;
    }

#ifdef _KERNELIMPLEMENTATION_

    status = ObSetSecurityObjectByPointer(
        DeviceObject,
        securityInformation,
        StackCreationSettings->SecurityDescriptor
        );

#else

    //
    // Since ObSetSecurityObjectByPointer isn't available on Win2K, we have to
    // use a rather sneaky trick. The device technically isn't openable yet.
    // However, ObOpenObjectByPointer doesn't bother doing any parse stuff.
    // Therefore, we can get a quick handle to the object, set the security
    // descriptor, and then dump the handle without the driver being any wiser.
    //
    SeSetSecurityAccessMask(securityInformation, &desiredAccess);

    status = ObOpenObjectByPointer(
        DeviceObject,
        OBJ_KERNEL_HANDLE,
        NULL,
        desiredAccess,
        *IoDeviceObjectType,
        KernelMode,
        &handle
        );

    if (!NT_SUCCESS(status)) {

        return status;
    }

    status = ZwSetSecurityObject(
        handle,
        securityInformation,
        StackCreationSettings->SecurityDescriptor
        );

    ZwClose(handle);

#endif // _KERNELIMPLEMENTATION_

    return status;
}



