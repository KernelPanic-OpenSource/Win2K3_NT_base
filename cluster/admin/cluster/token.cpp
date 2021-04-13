/////////////////////////////////////////////////////////////////////////////
//
//  Copyright (c) 1996-2002 Microsoft Corporation
//
//  Module Name:
//      token.cpp
//
//  Description:
//      Definition of valid token strings.
//
//  Maintained By:
//      George Potts (GPotts)                 11-Apr-2002
//      David Potter (DavidP)                 11-JUL-2001
//      Vijayendra Vasu (vvasu)               20-OCT-1998
//
//  Revision History:
//      April 10, 2002  Updated for the security push.
//
//  Notes:
//
/////////////////////////////////////////////////////////////////////////////


/////////////////////////////////////////////////////////////////////////////
//  Include files
/////////////////////////////////////////////////////////////////////////////

#include "precomp.h"
#include "token.h"
#include <clusapi.h>

/////////////////////////////////////////////////////////////////////////////
//  Global variables
/////////////////////////////////////////////////////////////////////////////

// Separator and delimiters.

// Seperators are special types of tokens.
const CString OPTION_SEPARATOR( TEXT("/-") );
const CString OPTION_VALUE_SEPARATOR( TEXT(":") );
const CString PARAM_VALUE_SEPARATOR( TEXT("=") );
const CString VALUE_SEPARATOR( TEXT(",") );

const CString SEPERATORS( OPTION_SEPARATOR +
                          OPTION_VALUE_SEPARATOR +
                          PARAM_VALUE_SEPARATOR +
                          VALUE_SEPARATOR );        

// Delimiters mark the end of a token.
// Whitespaces and end of input are also delimiters.
// Note: "-" and "/" are not delimiters. They can occur in a token.
// For example: cluster-1 is a valid token.
const CString DELIMITERS( OPTION_VALUE_SEPARATOR + 
                          PARAM_VALUE_SEPARATOR +
                          VALUE_SEPARATOR );


// Lookup tables and their sizes.

const LookupStruct<ObjectType> objectLookupTable[] =
{
    // Default value
    { TEXT("Invalid Object"),   objInvalid },

    // Node
    { TEXT("Node"),             objNode },

    // Group
    { TEXT("ResourceGroup"),    objGroup },
    { TEXT("ResGroup"),         objGroup },
    { TEXT("Group"),            objGroup },

    // Resource
    { TEXT("Resource"),         objResource },
    { TEXT("Res"),              objResource },

    { TEXT("ResourceType"),     objResourceType },
    { TEXT("ResType"),          objResourceType },
    { TEXT("Type"),             objResourceType },

    // Network
    { TEXT("Network"),          objNetwork },
    { TEXT("Net"),              objNetwork },
    
    // Network Interface
    { TEXT("NetInt"),           objNetInterface },
    { TEXT("NetInterface"),     objNetInterface }
};

const size_t objectLookupTableSize = RTL_NUMBER_OF( objectLookupTable );

const LookupStruct<OptionType> optionLookupTable[] =
{
    { TEXT("Invalid Option"),       optInvalid },

    //   Common options
    { TEXT("?"),                    optHelp },
    { TEXT("Help"),                 optHelp },

    { TEXT("Create"),               optCreate },

    { TEXT("Delete"),               optDelete },
    { TEXT("Del"),                  optDelete },

    { TEXT("Move"),                 optMove },
    { TEXT("MoveTo"),               optMove },

    { TEXT("List"),                 optList },

    { TEXT("ListOwners"),           optListOwners },

    { TEXT("Online"),               optOnline },
    { TEXT("On"),                   optOnline },

    { TEXT("Offline"),              optOffline },
    { TEXT("Off"),                  optOffline },

    { TEXT("Properties"),           optProperties },
    { TEXT("Prop"),                 optProperties },
    { TEXT("Props"),                optProperties },

    { TEXT("PrivProperties"),       optPrivateProperties },
    { TEXT("PrivProp"),             optPrivateProperties },
    { TEXT("PrivProps"),            optPrivateProperties },
    { TEXT("Priv"),                 optPrivateProperties },

    { TEXT("Rename"),               optRename },
    { TEXT("Ren"),                  optRename },

    { TEXT("Status"),               optStatus },
    { TEXT("State"),                optStatus },
    { TEXT("Stat"),                 optStatus },


    // Cluster options
    { TEXT("Quorum"),               optQuorumResource },
    { TEXT("QuorumResource"),       optQuorumResource },

    { TEXT("Version"),              optVersion },
    { TEXT("Ver"),                  optVersion },

    { TEXT("SetFail"),              optSetFailureActions },
    { TEXT("SetFailureActions"),    optSetFailureActions },

    { TEXT("RegExt"),               optRegisterAdminExtensions },
    { TEXT("RegAdminExt"),          optRegisterAdminExtensions },

    { TEXT("UnRegExt"),             optUnregisterAdminExtensions },
    { TEXT("UnRegAdminExt"),        optUnregisterAdminExtensions },

    { TEXT("Add"),                  optAddNodes },
    { TEXT("AddNode"),              optAddNodes },
    { TEXT("AddNodes"),             optAddNodes },

    { TEXT("ChangePassword"),       optChangePassword },
    { TEXT("ChangePass"),           optChangePassword },

    { TEXT("ListNetPriority"),      optListNetPriority },
    { TEXT("ListNetPri"),           optListNetPriority },
    { TEXT("SetNetPriority"),       optSetNetPriority },
    { TEXT("SetNetPri"),            optSetNetPriority },

    // Node options
    { TEXT("Pause"),                optPause },

    { TEXT("Resume"),               optResume },

    { TEXT("Evict"),                optEvict },

    { TEXT("Force"),                optForceCleanup },
    { TEXT("ForceCleanup"),         optForceCleanup },

    { TEXT("Start"),                optStartService },

    { TEXT("Stop"),                 optStopService },


    // Group options
    { TEXT("SetOwners"),            optSetOwners },

    
    // Resource options 
    { TEXT("AddChk"),               optAddCheckPoints },
    { TEXT("AddCheck"),             optAddCheckPoints },
    { TEXT("AddChkPoints"),         optAddCheckPoints },
    { TEXT("AddCheckPoints"),       optAddCheckPoints },

    { TEXT("AddCryptoChk"),         optAddCryptoCheckPoints },
    { TEXT("AddCryptoCheck"),       optAddCryptoCheckPoints },
    { TEXT("AddCryptoChkPoints"),   optAddCryptoCheckPoints },
    { TEXT("AddCryptoCheckPoints"), optAddCryptoCheckPoints },

    { TEXT("AddDep"),               optAddDependency },
    { TEXT("AddDependency"),        optAddDependency },

    { TEXT("AddOwner"),             optAddOwner },

    { TEXT("Fail"),                 optFail },

    { TEXT("Chk"),                  optGetCheckPoints },
    { TEXT("Check"),                optGetCheckPoints },
    { TEXT("ChkPoints"),            optGetCheckPoints },
    { TEXT("CheckPoints"),          optGetCheckPoints },

    { TEXT("CryptoChk"),            optGetCryptoCheckPoints },
    { TEXT("CryptoCheck"),          optGetCryptoCheckPoints },
    { TEXT("CryptoChkPoints"),      optGetCryptoCheckPoints },
    { TEXT("CryptoCheckPoints"),    optGetCryptoCheckPoints },

    { TEXT("ListDep"),              optListDependencies },
    { TEXT("ListDependencies"),     optListDependencies },

    { TEXT("RemoveDep"),            optRemoveDependency },
    { TEXT("RemoveDependency"),     optRemoveDependency },

    { TEXT("RemoveOwner"),          optRemoveOwner },
    { TEXT("RemOwner"),             optRemoveOwner },

    { TEXT("RemoveChk"),            optRemoveCheckPoints },
    { TEXT("RemoveCheck"),          optRemoveCheckPoints },
    { TEXT("RemoveChkPoints"),      optRemoveCheckPoints },
    { TEXT("RemoveCheckPoints"),    optRemoveCheckPoints },


    { TEXT("RemoveCryptoChk"),          optRemoveCryptoCheckPoints },
    { TEXT("RemoveCryptoCheck"),        optRemoveCryptoCheckPoints },
    { TEXT("RemoveCryptoChkPoints"),    optRemoveCryptoCheckPoints },
    { TEXT("RemoveCryptoCheckPoints"),  optRemoveCryptoCheckPoints },


    // Resource type options
    { TEXT("ListOwners"),           optListOwners },


    // Network options
    { TEXT("ListInt"),              optListInterfaces },
    { TEXT("ListInterface"),        optListInterfaces },
    { TEXT("ListInterfaces"),       optListInterfaces }

};

const size_t optionLookupTableSize = RTL_NUMBER_OF( optionLookupTable );

const LookupStruct<ParameterType> paramLookupTable[] =
{
    { TEXT("Unknown parameter"),    paramUnknown },
    { TEXT("C"),                    paramCluster },
    { TEXT("Cluster"),              paramCluster },
    { TEXT("DisplayName"),          paramDisplayName },
    { TEXT("DLL"),                  paramDLLName },
    { TEXT("DLLName"),              paramDLLName },
    { TEXT("Group"),                paramGroupName },
    { TEXT("IsAlive"),              paramIsAlive },
    { TEXT("LooksAlive"),           paramLooksAlive },
    { TEXT("MaxLogSize"),           paramMaxLogSize },
    { TEXT("Net"),                  paramNetworkName },
    { TEXT("Network"),              paramNetworkName },
    { TEXT("Node"),                 paramNodeName },
    { TEXT("Path"),                 paramPath },
    { TEXT("ResourceType"),         paramResType },
    { TEXT("ResType"),              paramResType },
    { TEXT("Type"),                 paramResType },
    { TEXT("Separate"),             paramSeparate },
    { TEXT("UseDefault"),           paramUseDefault },
    { TEXT("Wait"),                 paramWait },
    { TEXT("User"),                 paramUser },
    { TEXT("Password"),             paramPassword },
    { TEXT("Pass"),                 paramPassword },
    { TEXT("IPAddress"),            paramIPAddress },
    { TEXT("IPAddr"),               paramIPAddress },
    { TEXT("Verbose"),              paramVerbose },
    { TEXT("Verb"),                 paramVerbose },
    { TEXT("Unattended"),           paramUnattend },
    { TEXT("Unattend"),             paramUnattend },
    { TEXT("Wiz"),                  paramWizard },
    { TEXT("Wizard"),               paramWizard },
    { TEXT("SkipDC"),               paramSkipDC },  // password change
    { TEXT("Test"),                 paramTest }, // password change
    { TEXT("Quiet"),                paramQuiet }, // password change
    { TEXT("Min"),                  paramMinimal },
    { TEXT("Minimum"),              paramMinimal }
};

const size_t paramLookupTableSize = RTL_NUMBER_OF( paramLookupTable );

const LookupStruct<ValueFormat> formatLookupTable[] =
{
    { TEXT(""),                     vfInvalid },
    { TEXT("BINARY"),               vfBinary },
    { TEXT("DWORD"),                vfDWord },
    { TEXT("STR"),                  vfSZ },
    { TEXT("STRING"),               vfSZ },
    { TEXT("EXPANDSTR"),            vfExpandSZ },
    { TEXT("EXPANDSTRING"),         vfExpandSZ },
    { TEXT("MULTISTR"),             vfMultiSZ },
    { TEXT("MULTISTRING"),          vfMultiSZ },
    { TEXT("SECURITY"),             vfSecurity },
    { TEXT("ULARGE"),               vfULargeInt }
};

const size_t formatLookupTableSize = RTL_NUMBER_OF( formatLookupTable );
