/*++

Copyright (c) 1991  Microsoft Corporation

Module Name:

    ldrrsrc.c

Abstract:

    Loader API calls for accessing resource sections.

Author: 

    Steve Wood (stevewo) 16-Sep-1991

Revision History:

--*/

#include "ntrtlp.h"
#if defined(BLDR_KERNEL_RUNTIME)
#error "This file is not used in the boot loader runtime."
#endif
#if !defined(NTOS_KERNEL_RUNTIME) && !defined(BLDR_KERNEL_RUNTIME)
#define NTOS_USERMODE_RUNTIME
#endif
#if defined(NTOS_USERMODE_RUNTIME)
#include "wow64t.h"
#include "ntwow64.h"
#endif

#if defined(ALLOC_PRAGMA) && defined(NTOS_KERNEL_RUNTIME)
#pragma alloc_text(PAGE,LdrAccessResource)
#pragma alloc_text(PAGE,LdrpAccessResourceData)
#pragma alloc_text(PAGE,LdrpAccessResourceDataNoMultipleLanguage)
#pragma alloc_text(PAGE,LdrFindEntryForAddress)
#pragma alloc_text(PAGE,LdrFindResource_U)
#pragma alloc_text(PAGE,LdrFindResourceEx_U)
#pragma alloc_text(PAGE,LdrFindResourceDirectory_U)
#pragma alloc_text(PAGE,LdrpCompareResourceNames_U)
#pragma alloc_text(PAGE,LdrpSearchResourceSection_U)
#pragma alloc_text(PAGE,LdrEnumResources)
#endif

#define USE_RC_CHECKSUM

// winuser.h
#define IS_INTRESOURCE(_r) (((ULONG_PTR)(_r) >> 16) == 0)
#define RT_VERSION                         16
#define RT_MANIFEST                        24
#define CREATEPROCESS_MANIFEST_RESOURCE_ID  1
#define ISOLATIONAWARE_MANIFEST_RESOURCE_ID 2
#define MINIMUM_RESERVED_MANIFEST_RESOURCE_ID 1
#define MAXIMUM_RESERVED_MANIFEST_RESOURCE_ID 16

#define LDRP_MIN(x,y) (((x)<(y)) ? (x) : (y))

#define DPFLTR_LEVEL_STATUS(x) ((NT_SUCCESS(x) \
                                    || (x) == STATUS_OBJECT_NAME_NOT_FOUND    \
                                    || (x) == STATUS_RESOURCE_DATA_NOT_FOUND  \
                                    || (x) == STATUS_RESOURCE_TYPE_NOT_FOUND  \
                                    || (x) == STATUS_RESOURCE_NAME_NOT_FOUND  \
                                    ) \
                                ? DPFLTR_TRACE_LEVEL : DPFLTR_WARNING_LEVEL)

#ifdef NTOS_USERMODE_RUNTIME
#include <md5.h>

//
// The size in byte of the resource MD5 checksum.  16 bytes = 128 bits.
//
#define RESOURCE_CHECKSUM_SIZE          16
//
// The registry key path which stores the file version information for MUI files.
//
#define REG_MUI_PATH                                        L"Software\\Microsoft\\Windows\\CurrentVersion"
#define MUI_MUILANGUAGES_KEY_NAME       L"MUILanguages"
#define MUI_FILE_VERSION_KEY_NAME           L"FileVersions"

#define MUI_ALTERNATE_VERSION_KEY           L"MUIVer"
#define MUI_RC_CHECKSUM_DISABLE_KEY   L"ChecksumDisable"

#define MUI_NEUTRAL_LANGID  MAKELANGID( LANG_ENGLISH, SUBLANG_ENGLISH_US )

#ifdef MUI_MAGIC
#define MUI_COMPACT                     L"CMF"
#define CMF_64K_OFFSET                  (ULONG)65536
//
//  Locate target mui file in CMF file with CMFModule, Index data.
//  This will be replaced with function with sanity check
//
#define LDRP_GET_MODULE_OFFSET_FROM_CMF(CMFModule, wIndex)     ((ULONG)(((PCOMPACT_MUI)( (unsigned char*)(CMFModule) + \
    sizeof (COMPACT_MUI_RESOURCE) + sizeof (COMPACT_MUI) * (wIndex)))->ulpOffset) );

#define LDRP_GET_MODULE_FILESIZE_FROM_CMF(CMFModule, wIndex)     ((ULONG)(((PCOMPACT_MUI)( (unsigned char*)(CMFModule) + \
    sizeof (COMPACT_MUI_RESOURCE) + sizeof (COMPACT_MUI) * (wIndex)))->dwFileSize) );

//
// Number of CMF files
//
#define CMF_BLOCK_NUM       32
#endif

PALT_RESOURCE_MODULE AlternateResourceModules;
ULONG AlternateResourceModuleCount;
ULONG AltResMemBlockCount;
//
// ImpersonateLangId UI langid stores pervious impersonated language id.
//
LANGID UILangId, InstallLangId, ImpersonateLangId;

#define DWORD_ALIGNMENT(x) (((x)+3) & ~3)
#define  MEMBLOCKSIZE 32
#define  RESMODSIZE sizeof(ALT_RESOURCE_MODULE)

#define uint32 unsigned int

#if defined(_WIN64) || defined(BUILD_WOW6432)
extern ULONG NativePageSize;
#endif

//
// This macro ensures that correct UI language will be retrieved.
//
#define GET_UI_LANGID()                                                            \
{                                                                                                       \
    if (!UILangId ||                                                                         \
        ImpersonateLangId ||                                                             \
        NtCurrentTeb()->IsImpersonating)                                          \
    {                                                                                                   \
        if (NT_SUCCESS( NtQueryDefaultUILanguage( &UILangId ) ))                    \
        {                                                                                                \
            ImpersonateLangId = NtCurrentTeb()->IsImpersonating? UILangId : 0;          \
        }                                                                                                \
    }                                                                                                    \
}                                                                                                        \


#ifdef MUI_MAGIC
VOID
LdrpConvertVersionString(
    IN ULONGLONG ModuleVersion,
    OUT LPWSTR ModuleVersionStr
    );

BOOLEAN
LdrpOpenFileVersionKey(
    IN LPWSTR LangID,
    IN LPWSTR BaseDllName,
    IN ULONGLONG AltModuleVersion,
    IN LPWSTR AltModuleVersionStr,
    OUT PHANDLE pHandle);

BOOLEAN
LdrpGetRegValueKey(
    IN HANDLE Handle,
    IN LPWSTR KeyValueName,
    IN ULONG  KeyValueType,
    OUT PVOID Buffer,
    IN ULONG  BufferSize);

BOOLEAN
LdrpGetResourceChecksum(
    IN PVOID Module,
    OUT unsigned char** ppMD5Checksum
    );


BOOLEAN
LdrpCalcResourceChecksum(
    IN PVOID Module,
    IN PVOID AlternateModule,
    OUT unsigned char* MD5Checksum
    );


BOOLEAN
LdrpGetFileVersion(
    IN  PVOID      ImageBase,
    IN  LANGID     LangId,
    OUT PULONGLONG Version,
    OUT PVOID  *VersionResource,
    OUT ULONG *VersionResSize
    );



BOOLEAN
LdrpGetCMFNameFromModule(
    IN  PVOID Module,
    OUT LPWSTR *pCMFFileName,
    OUT PUSHORT wIndex
    )
/* ++
Routine description:
    If the file support CMF file for its resource storage, it should have "CompactMui" under
    VarInfo section uder Version resource. we search these block whether the module has this string.

Args.
    Module -     Base dll module, in this case Code module.
    CMFFileName  -   If the routine find CMF file, it will be stored here.
    wIndex  -   Module location inside CMF file.
*/
{

    NTSTATUS Status;
    ULONG_PTR IdPath[3];
    ULONG ResourceSize;
    PIMAGE_RESOURCE_DATA_ENTRY DataEntry;

    LONG BlockLen;
    LONG VarFileInfoSize;

    typedef struct tagVS_FIXEDFILEINFO
    {
        LONG   dwSignature;            /* e.g. 0xfeef04bd */
        LONG   dwStrucVersion;         /* e.g. 0x00000042 = "0.42" */
        LONG   dwFileVersionMS;        /* e.g. 0x00030075 = "3.75" */
        LONG   dwFileVersionLS;        /* e.g. 0x00000031 = "0.31" */
        LONG   dwProductVersionMS;     /* e.g. 0x00030010 = "3.10" */
        LONG   dwProductVersionLS;     /* e.g. 0x00000031 = "0.31" */
        LONG   dwFileFlagsMask;        /* = 0x3F for version "0.42" */
        LONG   dwFileFlags;            /* e.g. VFF_DEBUG | VFF_PRERELEASE */
        LONG   dwFileOS;               /* e.g. VOS_DOS_WINDOWS16 */
        LONG   dwFileType;             /* e.g. VFT_DRIVER */
        LONG   dwFileSubtype;          /* e.g. VFT2_DRV_KEYBOARD */
        LONG   dwFileDateMS;           /* e.g. 0 */
        LONG   dwFileDateLS;           /* e.g. 0 */
    } VS_FIXEDFILEINFO;

    struct
    {
        USHORT TotalSize;
        USHORT DataSize;
        USHORT Type;
        WCHAR Name[16];              // L"VS_VERSION_INFO" + unicode null terminator
        // Note that the previous 4 members has 16*2 + 3*2 = 38 bytes.
        // So that compiler will silently add a 2 bytes padding to make
        // FixedFileInfo to align in DWORD boundary.
        VS_FIXEDFILEINFO FixedFileInfo;
    } *Resource;

    typedef struct tagVERBLOCK
    {
        USHORT wTotalLen;
        USHORT wValueLen;
        USHORT wType;
        WCHAR szKey[1];
        // BYTE[] padding
        // WORD value;
    } VERBLOCK;
    VERBLOCK *pVerBlock;

    IdPath[0] = RT_VERSION;
    IdPath[1] = 1;
    IdPath[2] = MUI_NEUTRAL_LANGID;   // suppose, all localized OS use english resource code, is it corrret ???

    //
    // find the version resource data entry
    //
    try
    {
        Status = LdrFindResource_U(Module,IdPath,3,&DataEntry);

        if( !NT_SUCCESS(Status))
        {
            if (!InstallLangId){
                Status = NtQueryInstallUILanguage( &InstallLangId);

                if (!NT_SUCCESS( Status )) {
                    //
                    //  Failed to get Intall LangID.  AltResource not enabled.
                    //
                    return FALSE;
                    }
            }

            //
            // InstallLangId vs 0 -> when user develop the application for their language resource,
            // and then use language neutral case. if it is different from installlangeId. it fails.
            // 0 is neutral lang id so it will search UI language, Installed Language ID.
            // 01/14/02; usiing InstalllangID instead of 0, we need to provide solution to localized application
            // mui developer;their code is same with UI language  REVIST.
            //
            if (InstallLangId != MUI_NEUTRAL_LANGID)
            {
                 IdPath[2] = InstallLangId;
                 Status = LdrFindResource_U(Module,IdPath,3,&DataEntry);
            }
        }
    } except(EXCEPTION_EXECUTE_HANDLER)
    {
        Status = STATUS_UNSUCCESSFUL;
    }
    if(!NT_SUCCESS(Status))
    {
        return (FALSE);
    }

    //
    // Access the version resource data.
    //
    try
    {
         Status = LdrpAccessResourceDataNoMultipleLanguage(
                    Module,
                    DataEntry,
                    &Resource,
                    &ResourceSize);
    } except(EXCEPTION_EXECUTE_HANDLER)
    {
        Status = STATUS_UNSUCCESSFUL;
    }

    if(!NT_SUCCESS(Status))
    {
        return FALSE;
    }

    try
    {
        if((ResourceSize < sizeof(*Resource))
            || _wcsicmp(Resource->Name,L"VS_VERSION_INFO") != 0)
        {
            DbgPrint(("LDR: Warning: invalid version resource\n"));
            return FALSE;
        }
    } except(EXCEPTION_EXECUTE_HANDLER) {
        DbgPrint(("LDR: Exception encountered processing bogus version resource\n"));
        return FALSE;
    }

    ResourceSize -= DWORD_ALIGNMENT(sizeof(*Resource));

    //
    // Get the beginning address of the children of the version information.
    //
    pVerBlock = (VERBLOCK*)(Resource + 1);
    while ((LONG)ResourceSize > 0)
    {
        if (wcscmp(pVerBlock->szKey, L"VarFileInfo") == 0)
        {
            //
            // Find VarFileInfo block. Search the ResourceChecksum block.
            //
            VarFileInfoSize = pVerBlock->wTotalLen;
            BlockLen =DWORD_ALIGNMENT(sizeof(*pVerBlock) -1 + sizeof(L"VarFileInfo"));
            VarFileInfoSize -= BlockLen;
            pVerBlock = (VERBLOCK*)((unsigned char*)pVerBlock + BlockLen);
            while (VarFileInfoSize > 0)
            {
                if (wcscmp(pVerBlock->szKey, MUI_COMPACT) == 0)
                {
                    *wIndex = *(PUSHORT)DWORD_ALIGNMENT((UINT_PTR)(pVerBlock->szKey) + sizeof(MUI_COMPACT));
                    *pCMFFileName = (PVOID) DWORD_ALIGNMENT((UINT_PTR)(pVerBlock->szKey) + sizeof(MUI_COMPACT) + sizeof (ULONG) );
                    return (TRUE);
                }
                BlockLen = DWORD_ALIGNMENT(pVerBlock->wTotalLen);
                pVerBlock = (VERBLOCK*)((unsigned char*)pVerBlock + BlockLen);
                VarFileInfoSize -= BlockLen;
            }
            return (FALSE);
        }
        BlockLen = DWORD_ALIGNMENT(pVerBlock->wTotalLen);
        pVerBlock = (VERBLOCK*)((unsigned char*)pVerBlock + BlockLen);
        ResourceSize -= BlockLen;
    }

    return (FALSE);
}


BOOLEAN
LdrpCompareResourceChecksumInCMF(
    IN LPWSTR szLangIdDir,
    IN PVOID Module,
    IN ULONGLONG ModuleVersion,
    IN PVOID AlternateModule,
    IN PVOID CMFModule,
    IN USHORT wIndex,
    IN ULONGLONG AltModuleVersion,
    IN LPWSTR BaseDllName
    )
/*++

Routine Description:

    In the case that the version for the original module is different from that
    of the alternate module, check if the alternate module can still be used
    for the original version.

    First, the function will look at the registry to see if there is information
    cached for the module.

    In the case that the information is not cached for this module,
    this function will retrieve the MD5 resource checksum for the alternate
    resource module from CMF module instead of Alterate, which is overall efficient
    rather than retrieve the version from Module. And then check if the MD5 resource
    checksum is embeded in the original module.  If MD5 resource checksum is not in the original
    moduel, it will enumerate all resources in the module to calculate the
    MD5 checksum.

Arguments:

    szLangIdDir - Supplies a language of the resource to be loaded.

    Module - The original module.

    ModuleVersion - The version for the original version.

    CMFModule - The CMF file module.

    wIndex   - The target Module location insde CMFModule.

    AltModuleVersion - The version for the alternate module.

    BaseDllName - The name of the DLL.

Return Value:

    Ture if the alternate module can be used.

    Otherwise, return false.

--*/
{

    // Flag to indicate if the alternate resource can be used for this module.
    ULONG UseAlternateResource = 0;

    unsigned char* ModuleChecksum;                      // The 128-bit MD5 resource checksum for the module.
    unsigned char  CalculatedModuleChecksum[16];        // The calculated 128-bit MD5 resource checksum for the module.
    unsigned char  AlternateModuleChecksum[16];             // The 128-bit MD5 resource checksum embeded in the alternate module.

    WCHAR ModuleVersionStr[17];                         // The string for the 16 heximal digit version.
    WCHAR AltModuleVersionStr[17];

    HANDLE Handle = NULL;                                      // The registry which caches the information for this module.
    // Flag to indicate if we have retrieved or calucated the MD5 resource checksum for the original module successfully.
    BOOLEAN FoundModuleChecksum;

    UNICODE_STRING BufferString;

    PCOMPACT_MUI pcmui;

    //
    // Check the cached information in the registry first.
    //
    LdrpConvertVersionString(AltModuleVersion, AltModuleVersionStr);
    //
    // Open the version information key under:
    //      HKCU\Control Panel\International\MUI\FileVersions\<LangID>\<BaseDllName>
    //
    if (LdrpOpenFileVersionKey(szLangIdDir, BaseDllName, AltModuleVersion, AltModuleVersionStr, &Handle))
    {
        LdrpConvertVersionString(ModuleVersion, ModuleVersionStr);
        //
        // Try to check if this module exists in version information.
        // If yes, see if the AlternateModule can be used.
        //

        //
        // Get the cached version information in the registry to see if the original module can re-use the alternative module.
        //
        if (LdrpGetRegValueKey(Handle, ModuleVersionStr, REG_DWORD, &UseAlternateResource, sizeof(UseAlternateResource)))
        {
            // Get the cached information.  Let's bail and return the cached result in UseAlternativeResource.
            goto exit;
        }
    }

    //
    // When we are here, we know that we either:
    //  1. Can't open the registry key which cached the information. Or
    //  2. This file has never been looked before.
    //
    // Get the resource checksum for the alternate module.
    //

    try
    {
        pcmui = (PCOMPACT_MUI)((unsigned char*)CMFModule + sizeof (COMPACT_MUI_RESOURCE) + 
               sizeof(COMPACT_MUI) * wIndex);
       
    }
    except(EXCEPTION_EXECUTE_HANDLER) {
        DbgPrint(("LDR: Exception encountered processing bogus CMF module\n"));
        goto exit; 
    }

    if ( pcmui->Checksum )
    {
        memcpy(AlternateModuleChecksum, pcmui->Checksum, RESOURCE_CHECKSUM_SIZE);
        //
        // First, check if the resource checksum is built in the module.
        //
        if (!(FoundModuleChecksum = LdrpGetResourceChecksum(Module, &ModuleChecksum))) {
            //
            // If not, calculate the resource checksum for the current module.
            //
            if (FoundModuleChecksum = LdrpCalcResourceChecksum(Module, AlternateModule, CalculatedModuleChecksum))
            {
                ModuleChecksum = CalculatedModuleChecksum;
            }
        }
        if (FoundModuleChecksum)
        {
            if (memcmp(ModuleChecksum, AlternateModuleChecksum, RESOURCE_CHECKSUM_SIZE) == 0)
            {
                //
                // If the checksums are equal, the working version is the module version.
                //
                UseAlternateResource = 1;
            }
        }
    }
    if (Handle != NULL) {
        // If we find the version registry key successfully, cache the result in the registry.
        //
        // Write the working module information into registry.
        //
        RtlInitUnicodeString(&BufferString, ModuleVersionStr);
        NtSetValueKey(Handle, &BufferString, 0, REG_DWORD, &UseAlternateResource, sizeof(UseAlternateResource));
    }
exit:
    if (Handle != NULL)
    {
        NtClose(Handle);
    }

    return ((BOOLEAN)(UseAlternateResource));



}


BOOLEAN
LdrpVerifyAlternateResourceModuleInCMF(
    IN PWSTR szLangIdDir,  // Language DIR
    IN PVOID Module,       // Code file module
    IN PVOID AlternateModule,
    IN PVOID CMFModule,    // CMF file module
    IN USHORT wIndex,      // Compared MUI file index in CMF file.
    IN PWSTR BaseDllName
    )
/*++

Routine Description:

    This function verifies if the alternate resource module has the same
    version of the base module. For the Alternate, it refer the header in CMFModule
    rather than VERSION resource block.


Arguments:

    szLangIdDir - The language ID path.
    Module - The handle of the base module.
    CMFModule - The handle of the CMF Module.
    wIndex  -  The index of target MUI in CMF module.
    BaseDllName - The file name of base DLL.

Return Value:

    TBD.

--*/
{
    ULONGLONG ModuleVersion;
    ULONGLONG AltModuleVersion;
    NTSTATUS Status;
    PCOMPACT_MUI pcmui;
    int     RetryCount =0;
    LANGID   newLangID;
    LANGID   preLangID = 0;


    try 
    {
        pcmui = (PCOMPACT_MUI)((unsigned char*)CMFModule + sizeof (COMPACT_MUI_RESOURCE) + 
            sizeof(COMPACT_MUI) * wIndex);
        // There is no sanity check for AltModuleVersion value.
        AltModuleVersion = ((ULONGLONG)pcmui->dwFileVersionMS << 32) |
            (ULONGLONG)pcmui->dwFileVersionLS;
    }
    except(EXCEPTION_EXECUTE_HANDLER) {
        DbgPrint(("LDR: Exception encountered processing bogus CMF Module\n"));
        return FALSE;
    }


     //
     // Some component is
     //
    while (RetryCount < 3 )  {

        switch(RetryCount) {
            case 0:
                newLangID = MUI_NEUTRAL_LANGID;
                break;

            case 1:
                if (!InstallLangId){

                Status = NtQueryInstallUILanguage( &InstallLangId);

                if (!NT_SUCCESS( Status )) {
                    //
                    //  Failed to get Install LangID.  AltResource not enabled.
                    //
                    return FALSE;
                    }
                }
                newLangID = InstallLangId;
                break;

            case 2:
                if (MUI_NEUTRAL_LANGID != 0x409 ) {
                    newLangID = 0x409;
                    }
                break;
            }

       if ( newLangID != preLangID) {
          if (LdrpGetFileVersion(Module, newLangID, &ModuleVersion, NULL, NULL)){
               break;
            }
        }
       preLangID = newLangID;
       RetryCount++;
    }

    if (RetryCount >= 3) {
        return FALSE;
        }

    if (ModuleVersion == AltModuleVersion){
        return TRUE;
        }
    else
    {
#ifdef USE_RC_CHECKSUM
        return LdrpCompareResourceChecksumInCMF(szLangIdDir, Module, ModuleVersion, AlternateModule, CMFModule, wIndex, AltModuleVersion, BaseDllName);
#else
        return FALSE;
#endif
    }
}

BOOLEAN
LdrpSetAlternateResourceModuleHandleInCMF(
     IN PVOID Module,
     IN PVOID AlternateModule,
     IN PVOID CMFModule,
     IN PWSTR pwszCMFFileName,
     IN LANGID LangId)
{
    /*++

Routine Description:

    This function records the handle of the base module and alternate
    resource module in an array. In addition to this works of AlternateModule, this
    monitor CMF cache, which hold the data of CMF Module, CMF Name, Reference count.

Arguments:

    Module - The handle of the base module.
    AlternateModule - The handle of the Alternate Module.
    CMFModule  -  The handle of the CMF module.
    pwszCMFFileName - The CMF file name.

Return Value:
    True/ False


--*/
    PALT_RESOURCE_MODULE NewModules;

    if (!LangId) {
        GET_UI_LANGID();
        LangId = UILangId;
        }

    if (!LangId) {
        return FALSE;
        }

    if (AlternateResourceModules == NULL){
        //
        //  Allocate memory of initial size MEMBLOCKSIZE.
        //
        NewModules = (PALT_RESOURCE_MODULE)RtlAllocateHeap(
                        RtlProcessHeap(),
                        HEAP_ZERO_MEMORY,
                        RESMODSIZE * MEMBLOCKSIZE);
        if (!NewModules){
            return FALSE;
            }
        AlternateResourceModules = NewModules;
        AltResMemBlockCount = MEMBLOCKSIZE;
        }
    else
    if (AlternateResourceModuleCount >= AltResMemBlockCount ){
        //
        //  ReAllocate another chunk of memory.
        //
        NewModules = (PALT_RESOURCE_MODULE)RtlReAllocateHeap(
                        RtlProcessHeap(),
                        0,
                        AlternateResourceModules,
                        (AltResMemBlockCount + MEMBLOCKSIZE) * RESMODSIZE
                        );

        if (!NewModules){
            return FALSE;
            }
        AlternateResourceModules = NewModules;
        AltResMemBlockCount += MEMBLOCKSIZE;
        }

    AlternateResourceModules[AlternateResourceModuleCount].ModuleBase = Module;
    AlternateResourceModules[AlternateResourceModuleCount].AlternateModule = AlternateModule;
    AlternateResourceModules[AlternateResourceModuleCount].LangId = LangId ? LangId : UILangId;
    AlternateResourceModules[AlternateResourceModuleCount].CMFModule = CMFModule;
    AlternateResourceModuleCount++;

    return TRUE;
}


PVOID
LdrpLoadAlternateResourceModule(
    IN LANGID LangId,
    IN PVOID Module,
    IN LPCWSTR PathToAlternateModule OPTIONAL
    )

/*++

Routine Description:
    Extend LdrLoadAlternateResourceModule with LangId to load
    language specific MUI alternative modules
    Once MUI_MAGIC is enabled, this API should become a public API
    and replace LdrLoadAlternateResourceModule

    This function does the acutally loading into memory of the alternate
    resource module, or loads from the table if it was loaded before.


Arguments:
    LangId - Overwrite default UI language if it isn't zero
    Module - The handle of the base module.
    PathToAlternateModule - Optional path from which module is being loaded.

Return Value:

    Handle to the alternate resource module.

--*/

{
    PVOID AlternateModule, DllBase;
    PLDR_DATA_TABLE_ENTRY Entry;
    HANDLE FileHandle, MappingHandle;
    PIMAGE_NT_HEADERS NtHeaders;
    NTSTATUS Status;
    OBJECT_ATTRIBUTES ObjectAttributes;
    UNICODE_STRING AltDllName;
    PVOID FreeBuffer;
    LPWSTR BaseDllName = NULL, p;
    WCHAR DllPathName[DOS_MAX_PATH_LENGTH];
    ULONG DllPathNameLength, BaseDllNameLength, CopyCount;
    ULONG Digit;
    int i, RetryCount;
    WCHAR AltModulePath[DOS_MAX_PATH_LENGTH];
    WCHAR AltModulePathMUI[DOS_MAX_PATH_LENGTH];
    WCHAR AltModulePathFallback[DOS_MAX_PATH_LENGTH];
    IO_STATUS_BLOCK IoStatusBlock;
    RTL_RELATIVE_NAME_U RelativeName;
    SIZE_T ViewSize;
    LARGE_INTEGER SectionOffset;
    WCHAR LangIdDir[6];
    PVOID ReturnValue = NULL;
    char szBaseName[20];

    //
    // The full path of the current MUI file that we are searching.
    //
    UNICODE_STRING CurrentAltModuleFile;
    UNICODE_STRING SystemRoot;

    //
    // The current MUI folder that we are searching.
    //
    UNICODE_STRING CurrentAltModulePath;
    WCHAR CurrentAltModulePathBuffer[DOS_MAX_PATH_LENGTH];

    //
    // The string contains the first MUI folder that we will search.
    // This is the folder which lives under the folder of the base DLL.
    // AltDllMUIPath = [the folder of the base DLL] + "\mui" + "\[UI Language]";
    //      E.g. if the base DLL is "c:\winnt\system32\ntdll.dll" and UI language is 0411,
    //      AltDllMUIPath will be "c:\winnt\system32\mui\0411\"
    //
    UNICODE_STRING AltDllMUIPath;
    WCHAR AltDllMUIPathBuffer[DOS_MAX_PATH_LENGTH];

    //
    // MUI Redir
    //
    UNICODE_STRING BaseDllNameUstr;
    UNICODE_STRING StaticStringAltModulePathRedirected;
    UNICODE_STRING DynamicStringAltModulePathRedirected;
    PUNICODE_STRING FullPathStringFoundAltModulePathRedirected = NULL;
    BOOLEAN fRedirMUI = FALSE;
    PVOID LockCookie = NULL;
    BOOLEAN fIsCMFFile = FALSE;
    LPWSTR CMFFileName = NULL;
    USHORT wIndex;
    PVOID  CMFModule = NULL;
    PVOID  pX64KBase = NULL;
    ULONG ulMuiFileSize;
    ULONG ulOffset;


    // bail out early if this isn't a MUI-enabled system
    if (!LdrAlternateResourcesEnabled()) {
        return NULL;
        }

    if (!LangId) {
        GET_UI_LANGID();
        LangId = UILangId;
        }

    LdrLockLoaderLock(LDR_LOCK_LOADER_LOCK_FLAG_RAISE_ON_ERRORS, NULL, &LockCookie);
    __try {
        //
        // Look at the cache of the alternate module first.
        //
        AlternateModule = LdrpGetAlternateResourceModuleHandle(Module, LangId);
        if (AlternateModule == NO_ALTERNATE_RESOURCE_MODULE) {
            //
            //  We tried to load this module before but failed. Don't try
            //  again in the future.
            //
            ReturnValue =  NULL;
            __leave;

        } else if (AlternateModule > 0) {
            //
            //  We found the previously loaded match
            //
            ReturnValue = AlternateModule;
            __leave;

        }

        AlternateModule = NULL;

        if (ARGUMENT_PRESENT(PathToAlternateModule)) {
            //
            //  Caller suplied path.
            //

            p = wcsrchr(PathToAlternateModule, L'\\');

            if (p == NULL){ // ReturnValue == NULL;
                __leave;
             }


            p++;

            DllPathNameLength = (ULONG)(p - PathToAlternateModule) * sizeof(WCHAR);

            RtlCopyMemory(
                DllPathName,
                PathToAlternateModule,
                DllPathNameLength);

            BaseDllName = p;
            BaseDllNameLength = wcslen(p);

        } else {
            //
            //  Try to get full dll path from Ldr data table.
            //

            Status = LdrFindEntryForAddress(Module, &Entry);
            if (!NT_SUCCESS(Status)) { // ReturnValue = NULL;
                __leave;
             }


            DllPathNameLength = Entry->FullDllName.Length - Entry->BaseDllName.Length;

            RtlCopyMemory(
                DllPathName,
                Entry->FullDllName.Buffer,
                DllPathNameLength);

            BaseDllName = Entry->BaseDllName.Buffer;
            BaseDllNameLength = Entry->BaseDllName.Length;
        }

        // if module support CMF for MUI file, we replace BaseDllName(resource dll) with CMF name.
        if (LdrpGetCMFNameFromModule(Module, &CMFFileName, &wIndex))
        {
            fIsCMFFile = TRUE;
            BaseDllName = CMFFileName;
            BaseDllNameLength = wcslen(CMFFileName);

        }


        DllPathName[DllPathNameLength / sizeof(WCHAR)] = UNICODE_NULL;

        //
        // dll redirection for the dll to be loaded xiaoyuw@10/31/2000
        //
        StaticStringAltModulePathRedirected.Buffer = AltModulePath;  // reuse the array instead of define another array
        StaticStringAltModulePathRedirected.Length = 0;
        StaticStringAltModulePathRedirected.MaximumLength = sizeof(AltModulePath);

        DynamicStringAltModulePathRedirected.Buffer = NULL;
        DynamicStringAltModulePathRedirected.Length = 0;
        DynamicStringAltModulePathRedirected.MaximumLength = 0;

        BaseDllNameUstr.Buffer = AltModulePathMUI; // reuse the array instead of define another array
        BaseDllNameUstr.Length = 0;
        BaseDllNameUstr.MaximumLength = sizeof(AltModulePathMUI);

        RtlAppendUnicodeToString(&BaseDllNameUstr, BaseDllName);

        if (!fIsCMFFile){
            RtlAppendUnicodeToString(&BaseDllNameUstr, L".mui");
        }

        Status = RtlDosApplyFileIsolationRedirection_Ustr(
                            RTL_DOS_APPLY_FILE_REDIRECTION_USTR_FLAG_RESPECT_DOT_LOCAL,
                            &BaseDllNameUstr, NULL,
                            &StaticStringAltModulePathRedirected,
                            &DynamicStringAltModulePathRedirected,
                            &FullPathStringFoundAltModulePathRedirected,
                            NULL,NULL, NULL);
        if (!NT_SUCCESS(Status)) // no redirection info found for this string
        {
            if (Status != STATUS_SXS_KEY_NOT_FOUND)
                goto error_exit;

            //
            //  Generate the langid directory like "0804\"
            //
            if (!LangId) {
                GET_UI_LANGID();
                if (!UILangId){
                    goto error_exit;
                    }
                else {
                    LangId = UILangId;
                    }
            }

            CopyCount = 0;
            for (i = 12; i >= 0; i -= 4) {
                Digit = ((LangId >> i) & 0xF);
                if (Digit >= 10) {
                    LangIdDir[CopyCount++] = (WCHAR) (Digit - 10 + L'A');
                } else {
                    LangIdDir[CopyCount++] = (WCHAR) (Digit + L'0');
                }
            }

            LangIdDir[CopyCount++] = L'\\';
            LangIdDir[CopyCount++] = UNICODE_NULL;

            //
            // Create the MUI path under the directory of the base DLL.
            //
            AltDllMUIPath.Buffer = AltDllMUIPathBuffer;
            AltDllMUIPath.Length = 0;
            AltDllMUIPath.MaximumLength = sizeof(AltDllMUIPathBuffer);

            RtlAppendUnicodeToString(&AltDllMUIPath, DllPathName);  // e.g. "c:\winnt\system32\"
            RtlAppendUnicodeToString(&AltDllMUIPath, L"mui\\");     // e.g. "c:\winnt\system32\mui\"
            RtlAppendUnicodeToString(&AltDllMUIPath, LangIdDir);    // e.g. "c:\winnt\system32\mui\0411\"

            CurrentAltModulePath.Buffer = CurrentAltModulePathBuffer;
            CurrentAltModulePath.Length = 0;
            CurrentAltModulePath.MaximumLength = sizeof(CurrentAltModulePathBuffer);
        } else {
            fRedirMUI = TRUE;

            //set CurrentAltModuleFile and CurrentAltModulePath
            CurrentAltModuleFile.Buffer = AltModulePathMUI;
            CurrentAltModuleFile.Length = 0;
            CurrentAltModuleFile.MaximumLength = sizeof(AltModulePathMUI);

            RtlCopyUnicodeString(&CurrentAltModuleFile, FullPathStringFoundAltModulePathRedirected);
        }


        //
        //  Try name with .mui extesion first.
        //
        RetryCount = 0;
        while (RetryCount < 3){
            if ( ! fRedirMUI )
            {

                switch (RetryCount)
                {
                    case 0:
                        //
                        //  Generate the first path under the folder of the base DLL
                        //      (e.g. c:\winnt\system32\mui\0804\ntdll.dll.mui)
                        //
                        CurrentAltModuleFile.Buffer = AltModulePathMUI;
                        CurrentAltModuleFile.Length = 0;
                        CurrentAltModuleFile.MaximumLength = sizeof(AltModulePathMUI);

                        RtlCopyUnicodeString(&CurrentAltModuleFile, &AltDllMUIPath);    // e.g. "c:\winnt\system32\mui\0411\"
                        RtlCopyUnicodeString(&CurrentAltModulePath, &AltDllMUIPath);

                        RtlAppendUnicodeToString(&CurrentAltModuleFile, BaseDllName);   // e.g. "c:\winnt\system32\mui\0411\ntdll.dll"
                        if (!fIsCMFFile)
                        {
                            RtlAppendUnicodeToString(&CurrentAltModuleFile, L".mui");       // e.g. "c:\winnt\system32\mui\0411\ntdll.dll.mui"
                        }
                        break;
                    case 1:
                        //
                        //  Generate the second path c:\winnt\system32\mui\0804\ntdll.dll.mui
                        //
                        CurrentAltModuleFile.Buffer = AltModulePath;
                        CurrentAltModuleFile.Length = 0;
                        CurrentAltModuleFile.MaximumLength = sizeof(AltModulePath);

                        RtlCopyUnicodeString(&CurrentAltModuleFile, &AltDllMUIPath);    // e.g. "c:\winnt\system32\mui\0411\"
                        RtlAppendUnicodeToString(&CurrentAltModuleFile, BaseDllName);   // e.g. "c:\winnt\system32\mui\0411\ntdll.dll"
                        break;
                    case 2:
                        //
                        //  Generate path c:\winnt\mui\fallback\0804\foo.exe.mui
                        //
                        CurrentAltModuleFile.Buffer = AltModulePathFallback;
                        CurrentAltModuleFile.Length = 0;
                        CurrentAltModuleFile.MaximumLength = sizeof(AltModulePathFallback);

                        RtlInitUnicodeString(&SystemRoot, USER_SHARED_DATA->NtSystemRoot);    // e.g. "c:\winnt\system32\"
                        RtlAppendUnicodeStringToString(&CurrentAltModuleFile, &SystemRoot);   // e.g. "c:\winnt\system32\"
                        RtlAppendUnicodeToString(&CurrentAltModuleFile, L"\\mui\\fallback\\");  // e.g. "c:\winnt\system32\mui\fallback\"
                        RtlAppendUnicodeToString(&CurrentAltModuleFile, LangIdDir);             // e.g. "c:\winnt\system32\mui\fallback\0411\"

                        RtlCopyUnicodeString(&CurrentAltModulePath, &CurrentAltModuleFile);

                        RtlAppendUnicodeToString(&CurrentAltModuleFile, BaseDllName);           // e.g. "c:\winnt\system32\mui\fallback\0411\ntdll.dll"
                        if(!fIsCMFFile)
                        {
                            RtlAppendUnicodeToString(&CurrentAltModuleFile, L".mui");           // e.g. "c:\winnt\system32\mui\fallback\0411\ntdll.dll.mui"
                        }

                        break;
                }
            }

            if (!RtlDosPathNameToRelativeNtPathName_U(
                        CurrentAltModuleFile.Buffer,
                        &AltDllName,
                        NULL,
                        &RelativeName)) {
                goto error_exit;
            }

            FreeBuffer = AltDllName.Buffer;
            if (RelativeName.RelativeName.Length != 0) {
                AltDllName = RelativeName.RelativeName;
            } else {
                RelativeName.ContainingDirectory = NULL;
            }

            InitializeObjectAttributes(
                &ObjectAttributes,
                &AltDllName,
                OBJ_CASE_INSENSITIVE,
                RelativeName.ContainingDirectory,
                NULL
                );

            Status = NtCreateFile(
                    &FileHandle,
                    (ACCESS_MASK) GENERIC_READ | SYNCHRONIZE | FILE_READ_ATTRIBUTES,
                    &ObjectAttributes,
                    &IoStatusBlock,
                    NULL,
                    0L,
                    FILE_SHARE_READ | FILE_SHARE_DELETE,
                    FILE_OPEN,
                    0L,
                    NULL,
                    0L
                    );

            RtlReleaseRelativeName(&RelativeName);
            RtlFreeHeap(RtlProcessHeap(), 0, FreeBuffer);

            if (NT_SUCCESS(Status)) {
                goto CreateSection;
            }

            if (fRedirMUI) { // definitely failed
                goto error_exit;
            }

            if (Status != STATUS_OBJECT_NAME_NOT_FOUND && RetryCount == 0) {
                //
                //  Error other than the file name with .mui not found.
                //  Most likely directory is missing.  Skip file name w/o .mui
                //  and goto fallback directory.
                //
                RetryCount++;
            }

            RetryCount++;
        }

        // No alternate resource was found during the iterations.  Fail!
        goto error_exit;

    CreateSection:
        Status = NtCreateSection(
                    &MappingHandle,
                    STANDARD_RIGHTS_REQUIRED | SECTION_QUERY | SECTION_MAP_READ,
                    NULL,
                    NULL,
                    PAGE_WRITECOPY,
                    SEC_COMMIT,
                    FileHandle
                    );

	 NtClose( FileHandle );
	
        if (!NT_SUCCESS(Status)) {
            goto error_exit;
        }
        
        SectionOffset.LowPart = 0;
        SectionOffset.HighPart = 0;
        ViewSize = 0;
        DllBase = NULL;

        Status = NtMapViewOfSection(
                    MappingHandle,
                    NtCurrentProcess(),
                    &DllBase,
                    0L,
                    0L,
                    &SectionOffset,
                    &ViewSize,
                    ViewShare,
                    0L,
                    PAGE_WRITECOPY
                    );
        if (!(fIsCMFFile && NT_SUCCESS(Status)) ) {
            NtClose(MappingHandle);
            }
        if (!NT_SUCCESS(Status)){
            goto error_exit;
        }

        if (fIsCMFFile)
        {
            CMFModule = DllBase;

            try 
            {
                ulOffset = LDRP_GET_MODULE_OFFSET_FROM_CMF(CMFModule, wIndex);
                ulMuiFileSize = LDRP_GET_MODULE_FILESIZE_FROM_CMF(CMFModule, wIndex);
            }
            except(EXCEPTION_EXECUTE_HANDLER) {
               NtClose(MappingHandle);
               DbgPrint(("LDR: Exception encountered processing bogus CMF Module\n"));
               goto error_exit;
            }
            
            //
            // MapView section offset should start on the based of system granularity(64K), and when we
            // set this with specified view size, it map with round to 4K page base.
            //
            SectionOffset.LowPart = (ulOffset >> 16)*CMF_64K_OFFSET ;  // get 64K base
            SectionOffset.HighPart = 0;
            ViewSize = (ulOffset & (CMF_64K_OFFSET-1) ) + ulMuiFileSize; // size from 64K offset, we can have 4K based mem.

            DllBase = NULL;

            Status = NtMapViewOfSection(
                        MappingHandle,
                        NtCurrentProcess(),
                        &DllBase,
                        0L,
                        0L,
                        &SectionOffset,
                        &ViewSize,
                        ViewShare,
                        0L,
                        PAGE_WRITECOPY
                        );

            NtClose(MappingHandle);

            if (!NT_SUCCESS(Status)){
                goto error_exit;
              }
            pX64KBase = DllBase;
            DllBase = (unsigned char*)pX64KBase + (ulOffset & (CMF_64K_OFFSET-1) ) ;

        }

        NtHeaders = RtlImageNtHeader(DllBase);
        if (!NtHeaders) {
            if (fIsCMFFile) {
                DllBase = CMFModule;
            }
            NtUnmapViewOfSection(NtCurrentProcess(), (PVOID) DllBase);
            goto error_exit;
        }

        AlternateModule = LDR_VIEW_TO_DATAFILE(DllBase);

        //
        // Verify althenative module and module version, checksum.
        // In case of using CMF,Version, Checksum is saved in CMF header. we can use it.
        //

        if (fIsCMFFile)
        {
            if (!LdrpVerifyAlternateResourceModuleInCMF(LangIdDir, Module, AlternateModule, CMFModule, wIndex, BaseDllName) )
            {
                    NtUnmapViewOfSection(NtCurrentProcess(), (PVOID) CMFModule);
                    goto error_exit;
            }
            // Finally, we don't need CMFModule anymore.
            NtUnmapViewOfSection(NtCurrentProcess(), CMFModule);

        }
        else
        {
            if(!LdrpVerifyAlternateResourceModule(LangIdDir, Module, AlternateModule, BaseDllName, LangId))
            {
                NtUnmapViewOfSection(NtCurrentProcess(), (PVOID) DllBase);
                goto error_exit;
             }

        }
        LdrpSetAlternateResourceModuleHandleInCMF(Module, AlternateModule, pX64KBase, NULL, LangId);
#if DBG
        for ( i=0; i< 16; i++) {
            if (!BaseDllName[i])
               break;
            szBaseName[i] = (char) BaseDllName[i];
        }
        szBaseName[i] = 0;
        DbgPrintEx(
            DPFLTR_LDR_ID,
            DPFLTR_TRACE_LEVEL,
            "LDR: MUI module for %s %x is %x, Lang: %x, Impersonation:%d\n",
            szBaseName, Module, AlternateModule, LangId, NtCurrentTeb()->IsImpersonating
            );

#endif

        ReturnValue = AlternateModule;
        __leave;



error_exit:
        if (BaseDllName != NULL) {
            //
            // If we looked for a MUI file and couldn't find one keep track.  If
            // we couldn't get the base dll name (e.g. someone passing in a
            // mapped image with the low bit set but no path name), we don't want
            // to "remember" that there's no MUI.
            //

            LdrpSetAlternateResourceModuleHandleInCMF(Module, NO_ALTERNATE_RESOURCE_MODULE, NULL, NULL, LangId);
#if DBG
            for ( i=0; i< 16; i++){
                if (!BaseDllName[i])
                    break;
                szBaseName[i] = (char) BaseDllName[i];
            }
            szBaseName[i] = 0;
            DbgPrintEx(
                DPFLTR_LDR_ID,
                DPFLTR_TRACE_LEVEL,
                "LDR: No MUI module for %s %x, Lang: %x, Impersonation:%d\n",
                szBaseName, Module, LangId, NtCurrentTeb()->IsImpersonating);

#endif
        }

        ReturnValue = NULL;

    } __finally {
        Status = LdrUnlockLoaderLock(LDR_UNLOCK_LOADER_LOCK_FLAG_RAISE_ON_ERRORS, LockCookie);
    }

    return ReturnValue;

    // Compiler *should* be smart enough to recognize that we don't need a return here...
}

NTSTATUS LdrpLoadResourceFromAlternativeModule(
   IN PVOID DllHandle,
   IN ULONG_PTR* ResourceIdPath,
   IN ULONG ResourceIdPathLength,
   IN ULONG Flags,
   OUT PVOID *ResourceDirectoryOrData )
/*++

Routine Description:

    This function locates the address of the specified resource in the
    specified module's MUI resource DLL and returns its address.

Arguments:

    DllHandle - Supplies a handle to the image file that the resource is
        contained in.

    ResourceIdPath - Supplies a pointer to an array of 32-bit resource
        identifiers.  Each identifier is either an integer or a pointer
        to a null terminated string (PSZ) that specifies a resource
        name.  The array is used to traverse the directory structure
        contained in the resource section in the image file specified by
        the DllHandle parameter.

    ResourceIdPathLength - Supplies the number of elements in the
        ResourceIdPath array.

    Flags -
        LDRP_FIND_RESOURCE_DIRECTORY
        searching for a resource directory, otherwise the caller is
        searching for a resource data entry.

        LDR_FIND_RESOURCE_LANGUAGE_EXACT
        searching for a resource with, and only with, the language id
        specified in ResourceIdPath, otherwise the caller wants the routine
        to come up with default when specified langid is not found.

        LDR_FIND_RESOURCE_LANGUAGE_REDIRECT_VERSION
        searching for a resource version in main and alternative
        modules paths

    FindDirectoryEntry - Supplies a boolean that is TRUE if caller is
        searching for a resource directory, otherwise the caller is
        searching for a resource data entry.

    ExactLangMatchOnly - Supplies a boolean that is TRUE if caller is
        searching for a resource with, and only with, the language id
        specified in ResourceIdPath, otherwise the caller wants the routine
        to come up with default when specified langid is not found.

    ResourceDirectoryOrData - Supplies a pointer to a variable that will
        receive the address of the resource directory or data entry in
        the resource data section of the image file specified by the
        DllHandle parameter.

Return Value:

    TBD

--*/

{
    NTSTATUS Status = STATUS_RESOURCE_DATA_NOT_FOUND;
    PVOID AltResourceDllHandle = NULL;
    LANGID MUIDirLang;

    if (3 != ResourceIdPathLength)
        return Status;

    GET_UI_LANGID();

    if (!UILangId){
        return Status;
    }

    if (!InstallLangId){
        Status = NtQueryInstallUILanguage (&InstallLangId);
        if (!NT_SUCCESS( Status )) {
            //
            //  Failed to get Install LangID.  AltResource not enabled.
            //
            return FALSE;
        }
    }


    if (ResourceIdPath[2]) {
        //
        // Arabic/Hebrew MUI files may contain resources with LANG ID different than 401/40d.
        // e.g. Comdlg32.dll has two sets of Arabic/Hebrew resources one mirrored (401/40d)
        // and one flipped (801/80d).
        //
        if( (ResourceIdPath[2] != UILangId) &&
            ((PRIMARYLANGID ( UILangId) == LANG_ARABIC) || (PRIMARYLANGID ( UILangId) == LANG_HEBREW)) &&
            (PRIMARYLANGID (ResourceIdPath[2]) == PRIMARYLANGID (UILangId))
          ) {
            ResourceIdPath[2] = UILangId;
        }
    }
    else {
        ResourceIdPath[2] = UILangId;
    }

    // Don't load alternative modules for console process if UI language doesn't match system locale
    // In this case, we always load English
    if (NtCurrentPeb()->ProcessParameters->ConsoleHandle &&
        LANGIDFROMLCID(NtCurrentTeb()->CurrentLocale) != ResourceIdPath[2])
    {
        ResourceIdPath[2] = MUI_NEUTRAL_LANGID;
        UILangId = MUI_NEUTRAL_LANGID;
    }

        //
        // Bug #246044 WeiWu 12/07/00
        // BiDi modules use version block FileDescription field to store LRM markers,
        // LDR_FIND_RESOURCE_LANGUAGE_REDIRECT_VERSION will allow lpk.dll to get version resource from MUI alternative modules.
        //
    if ((ResourceIdPath[0] != RT_VERSION) ||
        (Flags & LDR_FIND_RESOURCE_LANGUAGE_REDIRECT_VERSION)) {

RESOURCE_TRY_AGAIN:
        //
        //  Load alternate resource dll when:
        //      1. language is neutral
        //         or
        //         Given language is not tried.
        //      and
        //      2. the resource to load is not a version info.
        //
        AltResourceDllHandle=LdrpLoadAlternateResourceModule(
                                (LANGID) ResourceIdPath[2],
                                DllHandle,
                                NULL);

        if (!AltResourceDllHandle){
            //
            //  Alternate resource dll not available.
            //  Skip this lookup.
            //
            if ((LANGID) ResourceIdPath[2] != InstallLangId){
                ResourceIdPath[2] = InstallLangId;
                //UILangId = InstallLangId;
                goto RESOURCE_TRY_AGAIN;
                }
            else {
               return Status;
            }
        }
        //
        // Add fallback steps here for alternative module search
        // 1) Given langid
        // 2) Primary langid of given langid if 2 != 1
        // 3) System installed langid if 3 != 1
        //
        MUIDirLang = (LANGID)ResourceIdPath[2];

SearchResourceSection:
        Status = LdrpSearchResourceSection_U(
                    AltResourceDllHandle,
                    ResourceIdPath,
                    3,
                    Flags | LDR_FIND_RESOURCE_LANGUAGE_EXACT,
                    (PVOID *)ResourceDirectoryOrData
                    );
        if (!NT_SUCCESS(Status) ) {
               if ((LANGID) ResourceIdPath[2] != 0x409) {
                    // some English components are not localized. but this unlocalized mui file
                    // is saved under \mui\fallback\%LocalizedLang%\ so we just repeat search.
                    // this is a temporary hack, we'll have a better solution
                    ResourceIdPath[2] = 0x409;
                    goto SearchResourceSection;
               }

               if ( (LANGID) MUIDirLang != InstallLangId) {
                    ResourceIdPath[2] = InstallLangId;
                    goto RESOURCE_TRY_AGAIN;
                }
            }
       }

    return Status;
}
#endif  // #ifdef MUI_MAGIC
#endif  // #ifdef NTOS_USERMODE_RUNTIME


#if defined(_X86_) && defined(NTOS_USERMODE_RUNTIME)
// appcompat: There's some code that depends on the Win2k instruction stream - duplicate it here.
__declspec(naked)
#endif
NTSTATUS
LdrAccessResource(
    IN PVOID DllHandle,
    IN const IMAGE_RESOURCE_DATA_ENTRY* ResourceDataEntry,
    OUT PVOID *Address OPTIONAL,
    OUT PULONG Size OPTIONAL
    )

/*++

Routine Description:

    This function locates the address of the specified resource in the
    specified DLL and returns its address.

Arguments:

    DllHandle - Supplies a handle to the image file that the resource is
        contained in.

    ResourceDataEntry - Supplies a pointer to the resource data entry in
        the resource data section of the image file specified by the
        DllHandle parameter.  This pointer should have been one returned
        by the LdrFindResource function.

    Address - Optional pointer to a variable that will receive the
        address of the resource specified by the first two parameters.

    Size - Optional pointer to a variable that will receive the size of
        the resource specified by the first two parameters.

Return Value:

    TBD

--*/

{
#if defined(_X86_) && defined(NTOS_USERMODE_RUNTIME)
    __asm {
        push [esp+0x10]       // Size
        push [esp+0x10]       // Address
        push [esp+0x10]       // ResourceDataEntry
        push [esp+0x10]       // DllHandle
        call LdrpAccessResourceData
        ret  16
    }
#else

    NTSTATUS Status;
    RTL_PAGED_CODE();

    Status =
        LdrpAccessResourceData(
          DllHandle,
          ResourceDataEntry,
          Address,
          Size
          );

#if DBG
    if (!NT_SUCCESS(Status)) {
        KdPrintEx((DPFLTR_LDR_ID, DPFLTR_LEVEL_STATUS(Status), "LDR: %s() exiting 0x%08lx\n", __FUNCTION__, Status));
    }
#endif
    return Status;
#endif
}

NTSTATUS
LdrpAccessResourceDataNoMultipleLanguage(
    IN PVOID DllHandle,
    IN const IMAGE_RESOURCE_DATA_ENTRY* ResourceDataEntry,
    OUT PVOID *Address OPTIONAL,
    OUT PULONG Size OPTIONAL
    )

/*++

Routine Description:

    This function returns the data necessary to actually examine the
    contents of a particular resource, without allowing for the .mui
    feature. It used to be the tail of LdrpAccessResourceData, from
    which it is now called.

Arguments:

    DllHandle - Supplies a handle to the image file that the resource is
        contained in.

    ResourceDataEntry - Supplies a pointer to the resource data entry in
        the resource data directory of the image file specified by the
        DllHandle parameter.  This pointer should have been one returned
        by the LdrFindResource function.

    Address - Optional pointer to a variable that will receive the
        address of the resource specified by the first two parameters.

    Size - Optional pointer to a variable that will receive the size of
        the resource specified by the first two parameters.


Return Value:

    TBD

--*/

{
    PIMAGE_RESOURCE_DIRECTORY ResourceDirectory;
    ULONG ResourceSize;
    PIMAGE_NT_HEADERS NtHeaders;
    ULONG_PTR VirtualAddressOffset;
    PIMAGE_SECTION_HEADER NtSection;
    NTSTATUS Status = STATUS_SUCCESS;

    RTL_PAGED_CODE();

    try {
        ResourceDirectory = (PIMAGE_RESOURCE_DIRECTORY)
            RtlImageDirectoryEntryToData(DllHandle,
                                         TRUE,
                                         IMAGE_DIRECTORY_ENTRY_RESOURCE,
                                         &ResourceSize
                                         );
        if (!ResourceDirectory) {
            return STATUS_RESOURCE_DATA_NOT_FOUND;
        }

        if (LDR_IS_DATAFILE(DllHandle)) {
            ULONG ResourceRVA;
            DllHandle = LDR_DATAFILE_TO_VIEW(DllHandle);
            NtHeaders = RtlImageNtHeader( DllHandle );
            if (NtHeaders->OptionalHeader.Magic == IMAGE_NT_OPTIONAL_HDR32_MAGIC) {
                ResourceRVA=((PIMAGE_NT_HEADERS32)NtHeaders)->OptionalHeader.DataDirectory[ IMAGE_DIRECTORY_ENTRY_RESOURCE ].VirtualAddress;
            } else if (NtHeaders->OptionalHeader.Magic == IMAGE_NT_OPTIONAL_HDR64_MAGIC) {
                ResourceRVA=((PIMAGE_NT_HEADERS64)NtHeaders)->OptionalHeader.DataDirectory[ IMAGE_DIRECTORY_ENTRY_RESOURCE ].VirtualAddress;
            } else {
                ResourceRVA = 0;
            }

            if (!ResourceRVA) {
                return STATUS_RESOURCE_DATA_NOT_FOUND;
                }

            VirtualAddressOffset = (ULONG_PTR)DllHandle + ResourceRVA - (ULONG_PTR)ResourceDirectory;

            //
            // Now, we must check to see if the resource is not in the
            // same section as the resource table.  If it's in .rsrc1,
            // we've got to adjust the RVA in the ResourceDataEntry
            // to point to the correct place in the non-VA data file.
            //
            NtSection = RtlSectionTableFromVirtualAddress( NtHeaders, DllHandle, ResourceRVA);

            if (!NtSection) {
                return STATUS_RESOURCE_DATA_NOT_FOUND;
            }

            if ( ResourceDataEntry->OffsetToData > NtSection->Misc.VirtualSize ) {
                ULONG rva;

                rva = NtSection->VirtualAddress;
                NtSection = RtlSectionTableFromVirtualAddress(NtHeaders,
                                                             DllHandle,
                                                             ResourceDataEntry->OffsetToData
                                                             );
                if (!NtSection) {
                    return STATUS_RESOURCE_DATA_NOT_FOUND;
                }
                VirtualAddressOffset +=
                        ((ULONG_PTR)NtSection->VirtualAddress - rva) -
                        ((ULONG_PTR)RtlAddressInSectionTable ( NtHeaders, DllHandle, NtSection->VirtualAddress ) - (ULONG_PTR)ResourceDirectory);
            }
        } else {
            VirtualAddressOffset = 0;
        }

        if (ARGUMENT_PRESENT( Address )) {
            *Address = (PVOID)( (PCHAR)DllHandle +
                                (ResourceDataEntry->OffsetToData - VirtualAddressOffset)
                              );
        }

        if (ARGUMENT_PRESENT( Size )) {
            *Size = ResourceDataEntry->Size;
        }

    }    except (EXCEPTION_EXECUTE_HANDLER) {
        Status = GetExceptionCode();
    }

#if DBG
    if (!NT_SUCCESS(Status)) {
        KdPrintEx((DPFLTR_LDR_ID, DPFLTR_LEVEL_STATUS(Status), "LDR: %s() exiting 0x%08lx\n", __FUNCTION__, Status));
    }
#endif
    return Status;
}


NTSTATUS
LdrpAccessResourceData(
    IN PVOID DllHandle,
    IN const IMAGE_RESOURCE_DATA_ENTRY* ResourceDataEntry,
    OUT PVOID *Address OPTIONAL,
    OUT PULONG Size OPTIONAL
    )

/*++

Routine Description:

    This function returns the data necessary to actually examine the
    contents of a particular resource.

Arguments:

    DllHandle - Supplies a handle to the image file that the resource is
        contained in.

    ResourceDataEntry - Supplies a pointer to the resource data entry in
   the resource data directory of the image file specified by the
        DllHandle parameter.  This pointer should have been one returned
        by the LdrFindResource function.

    Address - Optional pointer to a variable that will receive the
        address of the resource specified by the first two parameters.

    Size - Optional pointer to a variable that will receive the size of
        the resource specified by the first two parameters.


Return Value:

    TBD

--*/

{
    PIMAGE_RESOURCE_DIRECTORY ResourceDirectory;
    ULONG ResourceSize;
    PIMAGE_NT_HEADERS NtHeaders;
    NTSTATUS Status = STATUS_SUCCESS;

    RTL_PAGED_CODE();

#ifdef NTOS_USERMODE_RUNTIME
    ResourceDirectory = (PIMAGE_RESOURCE_DIRECTORY)
        RtlImageDirectoryEntryToData(DllHandle,
                                     TRUE,
                                     IMAGE_DIRECTORY_ENTRY_RESOURCE,
                                     &ResourceSize
                                     );
    if (!ResourceDirectory) {
        Status = STATUS_RESOURCE_DATA_NOT_FOUND;
        goto Exit;
    }

    if ((ULONG_PTR)ResourceDataEntry < (ULONG_PTR) ResourceDirectory ){
        DllHandle = LdrLoadAlternateResourceModule (DllHandle, NULL);
#ifdef MUI_MAGIC
        if (!DllHandle){
            if (!InstallLangId){
               Status = NtQueryInstallUILanguage (&InstallLangId);

               if (!NT_SUCCESS( Status )) {
                goto Exit;
                }
            }
            if (InstallLangId != UILangId) {
                DllHandle = LdrpLoadAlternateResourceModule (InstallLangId, DllHandle, NULL);
            }
        }
#endif
    } else{
        NtHeaders = RtlImageNtHeader(LDR_DATAFILE_TO_VIEW(DllHandle));
        if (NtHeaders) {
            // Find the bounds of the image so we can see if this resource entry is in an alternate
            // resource dll.

            ULONG_PTR ImageStart = (ULONG_PTR)LDR_DATAFILE_TO_VIEW(DllHandle);
            SIZE_T ImageSize = 0;

            if (LDR_IS_DATAFILE(DllHandle)) {

                // mapped as datafile.  Ask mm for the size

                NTSTATUS xStatus;
                MEMORY_BASIC_INFORMATION MemInfo;

                xStatus = NtQueryVirtualMemory(
                            NtCurrentProcess(),
                            (PVOID) ImageStart,
                            MemoryBasicInformation,
                            &MemInfo,
                            sizeof(MemInfo),
                            NULL
                            );

                if ( !NT_SUCCESS(xStatus) ) {
                    ImageSize = 0;
                } else {
                    ImageSize = MemInfo.RegionSize;
                }
            } else {
                ImageSize = ((PIMAGE_NT_HEADERS32)NtHeaders)->OptionalHeader.SizeOfImage;
            }

            if (!(((ULONG_PTR)ResourceDataEntry >= ImageStart) && ((ULONG_PTR)ResourceDataEntry < (ImageStart + ImageSize)))) {
                // Doesn't fall within the specified image.  Must be an alternate dll.
            DllHandle = LdrLoadAlternateResourceModule (DllHandle, NULL);
#ifdef MUI_MAGIC
            if (!DllHandle) {
                if (!InstallLangId){
                   Status = NtQueryInstallUILanguage (&InstallLangId);
                   if (!NT_SUCCESS( Status )) {
                        goto Exit;
                        }
                    }
                if (InstallLangId != UILangId) {
                    DllHandle = LdrpLoadAlternateResourceModule (InstallLangId, DllHandle, NULL);
                    }
                }
#endif
            }
        }
    }

    if (!DllHandle){
        Status = STATUS_RESOURCE_DATA_NOT_FOUND;
        goto Exit;
    }
#endif

    Status =
        LdrpAccessResourceDataNoMultipleLanguage(
            DllHandle,
            ResourceDataEntry,
            Address,
            Size
            );

#ifdef NTOS_USERMODE_RUNTIME
Exit:
#endif
    if (!NT_SUCCESS(Status)) {
        KdPrintEx((DPFLTR_LDR_ID, DPFLTR_LEVEL_STATUS(Status), "LDR: %s() exiting 0x%08lx\n", __FUNCTION__, Status));
    }
    return Status;
}


NTSTATUS
LdrFindEntryForAddress(
    IN PVOID Address,
    OUT PLDR_DATA_TABLE_ENTRY *TableEntry
    )
/*++

Routine Description:

    This function returns the load data table entry that describes the virtual
    address range that contains the passed virtual address.

Arguments:

    Address - Supplies a 32-bit virtual address.

    TableEntry - Supplies a pointer to the variable that will receive the
        address of the loader data table entry.


Return Value:

    Status

--*/
{
    PPEB_LDR_DATA Ldr;
    PLIST_ENTRY Head, Next;
    PLDR_DATA_TABLE_ENTRY Entry;
    PIMAGE_NT_HEADERS NtHeaders;
    PVOID ImageBase;
    PVOID EndOfImage;
    NTSTATUS Status;

    Ldr = NtCurrentPeb()->Ldr;
    if (Ldr == NULL) {
        Status = STATUS_NO_MORE_ENTRIES;
        goto Exit;
        }

    Entry = (PLDR_DATA_TABLE_ENTRY) Ldr->EntryInProgress;
    if (Entry != NULL) {
        NtHeaders = RtlImageNtHeader( Entry->DllBase );
        if (NtHeaders != NULL) {
            ImageBase = (PVOID)Entry->DllBase;

            EndOfImage = (PVOID)
                ((ULONG_PTR)ImageBase + NtHeaders->OptionalHeader.SizeOfImage);

            if ((ULONG_PTR)Address >= (ULONG_PTR)ImageBase && (ULONG_PTR)Address < (ULONG_PTR)EndOfImage) {
                *TableEntry = Entry;
                Status = STATUS_SUCCESS;
                goto Exit;
                }
            }
        }

    Head = &Ldr->InMemoryOrderModuleList;
    Next = Head->Flink;
    while ( Next != Head ) {
        Entry = CONTAINING_RECORD( Next, LDR_DATA_TABLE_ENTRY, InMemoryOrderLinks );

        NtHeaders = RtlImageNtHeader( Entry->DllBase );
        if (NtHeaders != NULL) {
            ImageBase = (PVOID)Entry->DllBase;

            EndOfImage = (PVOID)
                ((ULONG_PTR)ImageBase + NtHeaders->OptionalHeader.SizeOfImage);

            if ((ULONG_PTR)Address >= (ULONG_PTR)ImageBase && (ULONG_PTR)Address < (ULONG_PTR)EndOfImage) {
                *TableEntry = Entry;
                Status = STATUS_SUCCESS;
                goto Exit;
                }
            }

        Next = Next->Flink;
        }

    Status = STATUS_NO_MORE_ENTRIES;
Exit:
    if (!NT_SUCCESS(Status)) {
        KdPrintEx((DPFLTR_LDR_ID, DPFLTR_LEVEL_STATUS(Status), "LDR: %s() exiting 0x%08lx\n", __FUNCTION__, Status));
    }
    return( Status );
}


NTSTATUS
LdrFindResource_U(
    IN PVOID DllHandle,
    IN const ULONG_PTR* ResourceIdPath,
    IN ULONG ResourceIdPathLength,
    OUT PIMAGE_RESOURCE_DATA_ENTRY *ResourceDataEntry
    )

/*++

Routine Description:

    This function locates the address of the specified resource in the
    specified DLL and returns its address.

Arguments:

    DllHandle - Supplies a handle to the image file that the resource is
        contained in.

    ResourceIdPath - Supplies a pointer to an array of 32-bit resource
        identifiers.  Each identifier is either an integer or a pointer
        to a STRING structure that specifies a resource name.  The array
        is used to traverse the directory structure contained in the
        resource section in the image file specified by the DllHandle
        parameter.

    ResourceIdPathLength - Supplies the number of elements in the
        ResourceIdPath array.

    ResourceDataEntry - Supplies a pointer to a variable that will
        receive the address of the resource data entry in the resource
        data section of the image file specified by the DllHandle
        parameter.

Return Value:

    TBD

--*/

{
    RTL_PAGED_CODE();

    return LdrpSearchResourceSection_U(
      DllHandle,
      ResourceIdPath,
      ResourceIdPathLength,
      0,                // Look for a leaf node, ineaxt lang match
      (PVOID *)ResourceDataEntry
      );
}

NTSTATUS
LdrFindResourceEx_U(
    IN ULONG Flags,
    IN PVOID DllHandle,
    IN const ULONG_PTR* ResourceIdPath,
    IN ULONG ResourceIdPathLength,
    OUT PIMAGE_RESOURCE_DATA_ENTRY *ResourceDataEntry
    )

/*++

Routine Description:

    This function locates the address of the specified resource in the
    specified DLL and returns its address.

Arguments:
    Flags -
        LDRP_FIND_RESOURCE_DIRECTORY
        searching for a resource directory, otherwise the caller is
        searching for a resource data entry.

        LDR_FIND_RESOURCE_LANGUAGE_EXACT
        searching for a resource with, and only with, the language id
        specified in ResourceIdPath, otherwise the caller wants the routine
        to come up with default when specified langid is not found.

        LDR_FIND_RESOURCE_LANGUAGE_REDIRECT_VERSION
        searching for a resource version in both main and alternative
        module paths

    DllHandle - Supplies a handle to the image file that the resource is
        contained in.

    ResourceIdPath - Supplies a pointer to an array of 32-bit resource
        identifiers.  Each identifier is either an integer or a pointer
        to a STRING structure that specifies a resource name.  The array
        is used to traverse the directory structure contained in the
        resource section in the image file specified by the DllHandle
        parameter.

    ResourceIdPathLength - Supplies the number of elements in the
        ResourceIdPath array.

    ResourceDataEntry - Supplies a pointer to a variable that will
        receive the address of the resource data entry in the resource
        data section of the image file specified by the DllHandle
        parameter.

Return Value:

    TBD

--*/

{
    RTL_PAGED_CODE();

    return LdrpSearchResourceSection_U(
      DllHandle,
      ResourceIdPath,
      ResourceIdPathLength,
      Flags,
      (PVOID *)ResourceDataEntry
      );
}



NTSTATUS
LdrFindResourceDirectory_U(
    IN PVOID DllHandle,
    IN const ULONG_PTR* ResourceIdPath,
    IN ULONG ResourceIdPathLength,
    OUT PIMAGE_RESOURCE_DIRECTORY *ResourceDirectory
    )

/*++

Routine Description:

    This function locates the address of the specified resource directory in
    specified DLL and returns its address.

Arguments:

    DllHandle - Supplies a handle to the image file that the resource
        directory is contained in.

    ResourceIdPath - Supplies a pointer to an array of 32-bit resource
        identifiers.  Each identifier is either an integer or a pointer
        to a STRING structure that specifies a resource name.  The array
        is used to traverse the directory structure contained in the
        resource section in the image file specified by the DllHandle
        parameter.

    ResourceIdPathLength - Supplies the number of elements in the
        ResourceIdPath array.

    ResourceDirectory - Supplies a pointer to a variable that will
        receive the address of the resource directory specified by
        ResourceIdPath in the resource data section of the image file
        the DllHandle parameter.

Return Value:

    TBD

--*/

{
    RTL_PAGED_CODE();

    return LdrpSearchResourceSection_U(
      DllHandle,
      ResourceIdPath,
      ResourceIdPathLength,
      LDRP_FIND_RESOURCE_DIRECTORY,                 // Look for a directory node
      (PVOID *)ResourceDirectory
      );
}


LONG
LdrpCompareResourceNames_U(
    IN ULONG_PTR ResourceName,
    IN const IMAGE_RESOURCE_DIRECTORY* ResourceDirectory,
    IN const IMAGE_RESOURCE_DIRECTORY_ENTRY* ResourceDirectoryEntry
    )
{
    LONG li;
    PIMAGE_RESOURCE_DIR_STRING_U ResourceNameString;

    if (ResourceName & LDR_RESOURCE_ID_NAME_MASK) {
        if (!ResourceDirectoryEntry->NameIsString) {
            return( -1 );
            }

        ResourceNameString = (PIMAGE_RESOURCE_DIR_STRING_U)
            ((PCHAR)ResourceDirectory + ResourceDirectoryEntry->NameOffset);

        li = wcsncmp( (LPWSTR)ResourceName,
            ResourceNameString->NameString,
            ResourceNameString->Length
          );

        if (!li && wcslen((PWSTR)ResourceName) != ResourceNameString->Length) {
       return( 1 );
       }

   return(li);
        }
    else {
        if (ResourceDirectoryEntry->NameIsString) {
            return( 1 );
            }

        return( (ULONG)(ResourceName - ResourceDirectoryEntry->Name) );
        }
}

// Language ids are 16bits so any value with any bits
// set above 16 should be ok, and this value only has
// to fit in a ULONG_PTR. 0x10000 should be sufficient.
// The value used is actually 0xFFFF regardless of 32bit or 64bit,
// I guess assuming this is not an actual langid, which it isn't,
// due to the relatively small number of languages, around 70.
#define  USE_FIRSTAVAILABLE_LANGID   (0xFFFFFFFF & ~LDR_RESOURCE_ID_NAME_MASK)

NTSTATUS
LdrpSearchResourceSection_U(
    IN PVOID DllHandle,
    IN const ULONG_PTR* ResourceIdPath,
    IN ULONG ResourceIdPathLength,
    IN ULONG Flags,
    OUT PVOID *ResourceDirectoryOrData
    )

/*++

Routine Description:

    This function locates the address of the specified resource in the
    specified DLL and returns its address.

Arguments:

    DllHandle - Supplies a handle to the image file that the resource is
        contained in.

    ResourceIdPath - Supplies a pointer to an array of 32-bit resource
        identifiers.  Each identifier is either an integer or a pointer
        to a null terminated string (PSZ) that specifies a resource
        name.  The array is used to traverse the directory structure
        contained in the resource section in the image file specified by
        the DllHandle parameter.

    ResourceIdPathLength - Supplies the number of elements in the
        ResourceIdPath array.

    Flags -
        LDRP_FIND_RESOURCE_DIRECTORY
        searching for a resource directory, otherwise the caller is
        searching for a resource data entry.

        LDR_FIND_RESOURCE_LANGUAGE_EXACT
        searching for a resource with, and only with, the language id
        specified in ResourceIdPath, otherwise the caller wants the routine
        to come up with default when specified langid is not found.

        LDR_FIND_RESOURCE_LANGUAGE_REDIRECT_VERSION
        searching for a resource version in main and alternative
        modules paths

    FindDirectoryEntry - Supplies a boolean that is TRUE if caller is
        searching for a resource directory, otherwise the caller is
        searching for a resource data entry.

    ExactLangMatchOnly - Supplies a boolean that is TRUE if caller is
        searching for a resource with, and only with, the language id
        specified in ResourceIdPath, otherwise the caller wants the routine
        to come up with default when specified langid is not found.

    ResourceDirectoryOrData - Supplies a pointer to a variable that will
        receive the address of the resource directory or data entry in
        the resource data section of the image file specified by the
        DllHandle parameter.

Return Value:

    TBD

--*/

{
    NTSTATUS Status;
    PIMAGE_RESOURCE_DIRECTORY LanguageResourceDirectory, ResourceDirectory, TopResourceDirectory;
    PIMAGE_RESOURCE_DIRECTORY_ENTRY ResourceDirEntLow;
    PIMAGE_RESOURCE_DIRECTORY_ENTRY ResourceDirEntMiddle;
    PIMAGE_RESOURCE_DIRECTORY_ENTRY ResourceDirEntHigh;
    PIMAGE_RESOURCE_DATA_ENTRY ResourceEntry;
    USHORT n, half;
    LONG dir;
    ULONG size;
    ULONG_PTR ResourceIdRetry;
    ULONG RetryCount;
    LANGID NewLangId;
    const ULONG_PTR* IdPath = ResourceIdPath;
    ULONG IdPathLength = ResourceIdPathLength;
    BOOLEAN fIsNeutral = FALSE;
    LANGID GivenLanguage;
#ifdef NTOS_USERMODE_RUNTIME
    LCID DefaultThreadLocale, DefaultSystemLocale;
    PVOID AltResourceDllHandle = NULL;
    ULONG_PTR UIResourceIdPath[3];
#endif

    RTL_PAGED_CODE();

    try {
        TopResourceDirectory = (PIMAGE_RESOURCE_DIRECTORY)
            RtlImageDirectoryEntryToData(DllHandle,
                                         TRUE,
                                         IMAGE_DIRECTORY_ENTRY_RESOURCE,
                                         &size
                                         );
        if (!TopResourceDirectory) {
            return( STATUS_RESOURCE_DATA_NOT_FOUND );
        }

        ResourceDirectory = TopResourceDirectory;
        ResourceIdRetry = USE_FIRSTAVAILABLE_LANGID;
        RetryCount = 0;
        ResourceEntry = NULL;
        LanguageResourceDirectory = NULL;
        while (ResourceDirectory != NULL && ResourceIdPathLength--) {
            //
            // If search path includes a language id, then attempt to
            // match the following language ids in this order:
            //
            //   (0)  use given language id
            //   (1)  use primary language of given language id
            //   (2)  use id 0  (neutral resource)
            //   (4)  use user UI language
            //
            // If the PRIMARY language id is ZERO, then ALSO attempt to
            // match the following language ids in this order:
            //
            //   (3)  use thread language id for console app
            //   (4)  use user UI language
            //   (5)  use lang id of TEB for windows app if it is different from user locale
            //   (6)  use UI lang from exe resource
            //   (7)  use primary UI lang from exe resource
            //   (8)  use Install Language
            //   (9)  use lang id from user's locale id
            //   (10)  use primary language of user's locale id
            //   (11) use lang id from system default locale id
            //   (12) use lang id of system default locale id
            //   (13) use primary language of system default locale id
            //   (14) use US English lang id
            //   (15) use any lang id that matches requested info
            //
            if (ResourceIdPathLength == 0 && IdPathLength == 3) {
                LanguageResourceDirectory = ResourceDirectory;
                }

            if (LanguageResourceDirectory != NULL) {
                GivenLanguage = (LANGID)IdPath[ 2 ];
                fIsNeutral = (PRIMARYLANGID( GivenLanguage ) == LANG_NEUTRAL);
TryNextLangId:
                switch( RetryCount++ ) {
#if defined(NTOS_KERNEL_RUNTIME)
                    case 0:     // Use given language id
                        NewLangId = GivenLanguage;
                        break;

                    case 1:     // Use primary language of given language id
                        NewLangId = PRIMARYLANGID( GivenLanguage );
                        break;

                    case 2:     // Use id 0  (neutral resource)
                        NewLangId = 0;
                        break;

                    case 3:     // Use user's default UI language
                        NewLangId = (LANGID)ResourceIdRetry;
                        break;

                    case 4:     // Use native UI language
                        if ( !fIsNeutral ) {
                            // Stop looking - Not in the neutral case
                            goto ReturnFailure;
                            break;
                        }
                        NewLangId = PsInstallUILanguageId;
                        break;

                    case 5:     // Use default system locale
                        NewLangId = LANGIDFROMLCID(PsDefaultSystemLocaleId);
                        break;

                    case 6:
                        // Use US English language
                        NewLangId = MAKELANGID( LANG_ENGLISH, SUBLANG_ENGLISH_US );
                        break;

                    case 7:     // Take any lang id that matches
                        NewLangId = USE_FIRSTAVAILABLE_LANGID;
                        break;

#elif defined(NTOS_USERMODE_RUNTIME)
                    case 0:     // Use given language id
                        NewLangId = GivenLanguage;
                        break;

                    case 1:     // Use primary language of given language id
                        if ( Flags & LDR_FIND_RESOURCE_LANGUAGE_EXACT) {
                            //
                            //  Did not find an exact language match.
                            //  Stop looking.
                            //
                            goto ReturnFailure;
                        }
                        NewLangId = PRIMARYLANGID( GivenLanguage );
                        break;

                    case 2:     // Use id 0  (neutral resource)
                        NewLangId = 0;
                        break;

                    case 3:     // Use thread langid if caller is a console app
                        if ( !fIsNeutral ) {
                            // Stop looking - Not in the neutral case
                            NewLangId = (LANGID)ResourceIdRetry;
                            break;
                        }

                        if (NtCurrentPeb()->ProcessParameters->ConsoleHandle)
                        {
                            NewLangId = LANGIDFROMLCID(NtCurrentTeb()->CurrentLocale);
                        }
                        else
                        {
                            NewLangId = (LANGID)ResourceIdRetry;
                        }
                        break;

                    case 4:     // Use user's default UI language
                        GET_UI_LANGID();
                        if (!UILangId){
                            NewLangId = (LANGID)ResourceIdRetry;
                            break;
                        }

                        // Don't load alternative modules for console process if UI language doesn't match system locale
                        if (NtCurrentPeb()->ProcessParameters->ConsoleHandle &&
                            LANGIDFROMLCID(NtCurrentTeb()->CurrentLocale) != UILangId)
                        {
                            NewLangId = (LANGID)ResourceIdRetry;
                            break;
                        }

                        NewLangId = UILangId;

                        //
                        // Arabic/Hebrew MUI files may contain resources with LANG ID different than 401/40d.
                        // e.g. Comdlg32.dll has two sets of Arabic/Hebrew resources one mirrored (401/40d)
                        // and one flipped (801/80d).
                        //
                        if( !fIsNeutral &&
                            ((PRIMARYLANGID (GivenLanguage) == LANG_ARABIC) || (PRIMARYLANGID (GivenLanguage) == LANG_HEBREW)) &&
                            (PRIMARYLANGID (GivenLanguage) == PRIMARYLANGID (NewLangId))) {
                                NewLangId = GivenLanguage;
                        }

                            //
                            // Bug #246044 WeiWu 12/07/00
                            // BiDi modules use version block FileDescription field to store LRM markers,
                            // LDR_FIND_RESOURCE_LANGUAGE_REDIRECT_VERSION will allow lpk.dll to get version resource from MUI alternative modules.
                            //
                        if ( ( (IdPath[0] != RT_VERSION) && (IdPath[0] != RT_MANIFEST) ) ||
                            (Flags & LDR_FIND_RESOURCE_LANGUAGE_REDIRECT_VERSION)) {
                            //
                            //  Load alternate resource dll when:
                            //      1. language is neutral
                            //         or
                            //         Given language is not tried.
                            //      and
                            //      2. the resource to load is not a version info.
                            //
                            AltResourceDllHandle=LdrLoadAlternateResourceModule(
                                                    DllHandle,
                                                    NULL);

                            if (!AltResourceDllHandle){
                                //
                                //  Alternate resource dll not available.
                                //  Skip this lookup.
                                //
                                NewLangId = (LANGID)ResourceIdRetry;
                                break;
                            }

                            //
                            //  Map to alternate resource dll and search
                            //  it instead.
                            //

                            UIResourceIdPath[0]=IdPath[0];
                            UIResourceIdPath[1]=IdPath[1];
                            UIResourceIdPath[2]=NewLangId;

                            Status = LdrpSearchResourceSection_U(
                                        AltResourceDllHandle,
                                        UIResourceIdPath,
                                        3,
                                        Flags | LDR_FIND_RESOURCE_LANGUAGE_EXACT,
                                        (PVOID *)ResourceDirectoryOrData
                                        );

                            if (NT_SUCCESS(Status)){
                                //
                                // We sucessfully found alternate resource,
                                // return it.
                                //
                                return Status;
                            }
                        }
                        //
                        //  Caller does not want alternate resource, or
                        //  alternate resource not found.
                        //
                        NewLangId = (LANGID)ResourceIdRetry;
                        break;

                    case 5:     // Use langid of the thread locale if caller is a Windows app and thread locale is different from user locale
                        if ( !fIsNeutral ) {
                            // Stop looking - Not in the neutral case
                            goto ReturnFailure;
                            break;
                        }

                        if (!NtCurrentPeb()->ProcessParameters->ConsoleHandle && NtCurrentTeb()){
                            Status = NtQueryDefaultLocale(
                                        TRUE,
                                        &DefaultThreadLocale
                                        );
                            if (NT_SUCCESS( Status ) &&
                                DefaultThreadLocale !=
                                NtCurrentTeb()->CurrentLocale) {
                                //
                                // Thread locale is different from
                                // default locale.
                                //
                                NewLangId = LANGIDFROMLCID(NtCurrentTeb()->CurrentLocale);
                                break;
                            }
                        }


                        NewLangId = (LANGID)ResourceIdRetry;
                        break;

                    case 6:   // UI language from the executable resource

                        if (!UILangId){
                            NewLangId = (LANGID)ResourceIdRetry;
                        } else {
                            NewLangId = UILangId;
                        }
                        break;

                    case 7:   // Parimary lang of UI language from the executable resource

                        if (!UILangId){
                            NewLangId = (LANGID)ResourceIdRetry;
                        } else {
                            NewLangId = PRIMARYLANGID( (LANGID) UILangId );
                        }
                        break;

                    case 8:   // Use install -native- language
                        //
                        // Thread locale is the same as the user locale, then let's
                        // try loading the native (install) ui language resources.
                        //
                        if (!InstallLangId){
                            Status = NtQueryInstallUILanguage(&InstallLangId);
                            if (!NT_SUCCESS( Status )) {
                                //
                                // Failed reading key.  Skip this lookup.
                                //
                                NewLangId = (LANGID)ResourceIdRetry;
                                break;

                            }
                        }

                        NewLangId = InstallLangId;
                        break;

                    case 9:     // Use lang id from locale in TEB
                        if (SUBLANGID( GivenLanguage ) == SUBLANG_SYS_DEFAULT) {
                            // Skip over all USER locale options
                            DefaultThreadLocale = 0;
                            RetryCount += 2;
                            break;
                        }

                        if (NtCurrentTeb() != NULL) {
                            NewLangId = LANGIDFROMLCID(NtCurrentTeb()->CurrentLocale);
                        }
                        break;

                    case 10:     // Use User's default locale
                        Status = NtQueryDefaultLocale( TRUE, &DefaultThreadLocale );
                        if (NT_SUCCESS( Status )) {
                            NewLangId = LANGIDFROMLCID(DefaultThreadLocale);
                            break;
                            }

                        RetryCount++;
                        break;

                    case 11:     // Use primary language of User's default locale
                        NewLangId = PRIMARYLANGID( (LANGID)ResourceIdRetry );
                        break;

                    case 12:     // Use System default locale
                        Status = NtQueryDefaultLocale( FALSE, &DefaultSystemLocale );
                        if (!NT_SUCCESS( Status )) {
                            RetryCount++;
                            break;
                        }
                        if (DefaultSystemLocale != DefaultThreadLocale) {
                            NewLangId = LANGIDFROMLCID(DefaultSystemLocale);
                            break;
                        }

                        RetryCount += 2;
                        // fall through

                    case 14:     // Use US English language
                        NewLangId = MAKELANGID( LANG_ENGLISH, SUBLANG_ENGLISH_US );
                        break;

                    case 13:     // Use primary language of System default locale
                        NewLangId = PRIMARYLANGID( (LANGID)ResourceIdRetry );
                        break;

                    case 15:     // Take any lang id that matches
                        NewLangId = USE_FIRSTAVAILABLE_LANGID;
                        break;
#else
#error "Unknown environment."
#endif
                    default:    // No lang ids to match
                        goto ReturnFailure;
                        break;
                }

                //
                // If looking for a specific language id and same as the
                // one we just looked up, then skip it.
                //
                if (NewLangId != USE_FIRSTAVAILABLE_LANGID &&
                    NewLangId == ResourceIdRetry
                   ) {
                    goto TryNextLangId;
                    }

                //
                // Try this new language Id
                //
                ResourceIdRetry = (ULONG_PTR)NewLangId;
                ResourceIdPath = &ResourceIdRetry;
                ResourceDirectory = LanguageResourceDirectory;
                }

            n = ResourceDirectory->NumberOfNamedEntries;
            ResourceDirEntLow = (PIMAGE_RESOURCE_DIRECTORY_ENTRY)(ResourceDirectory+1);
            if (!(*ResourceIdPath & LDR_RESOURCE_ID_NAME_MASK)) { // No string(name),so we need ID
                ResourceDirEntLow += n;
                n = ResourceDirectory->NumberOfIdEntries;
                }

            if (!n) {
                ResourceDirectory = NULL;
                goto NotFound;  // Resource directory contains zero types or names or langID.
                }

            if (LanguageResourceDirectory != NULL &&
                *ResourceIdPath == USE_FIRSTAVAILABLE_LANGID
               ) {
                ResourceDirectory = NULL;
                ResourceIdRetry = ResourceDirEntLow->Name;
                ResourceEntry = (PIMAGE_RESOURCE_DATA_ENTRY)
                    ((PCHAR)TopResourceDirectory +
                            ResourceDirEntLow->OffsetToData
                    );

                break;
                }

            ResourceDirectory = NULL;
            ResourceDirEntHigh = ResourceDirEntLow + n - 1;
            while (ResourceDirEntLow <= ResourceDirEntHigh) {
                if ((half = (n >> 1)) != 0) {
                    ResourceDirEntMiddle = ResourceDirEntLow;
                    if (*(PUCHAR)&n & 1) {
                        ResourceDirEntMiddle += half;
                        }
                    else {
                        ResourceDirEntMiddle += half - 1;
                        }
                    dir = LdrpCompareResourceNames_U( *ResourceIdPath,
                                                      TopResourceDirectory,
                                                      ResourceDirEntMiddle
                                                    );
                    if (!dir) {
                        if (ResourceDirEntMiddle->DataIsDirectory) {
                            ResourceDirectory = (PIMAGE_RESOURCE_DIRECTORY)
                    ((PCHAR)TopResourceDirectory +
                                    ResourceDirEntMiddle->OffsetToDirectory
                                );
                            }
                        else {
                            ResourceDirectory = NULL;
                            ResourceEntry = (PIMAGE_RESOURCE_DATA_ENTRY)
                                ((PCHAR)TopResourceDirectory +
                                 ResourceDirEntMiddle->OffsetToData
                                );
                            }

                        break;
                        }
                    else {
                        if (dir < 0) {  // Order in the resource: Name, ID.
                            ResourceDirEntHigh = ResourceDirEntMiddle - 1;
                            if (*(PUCHAR)&n & 1) {
                                n = half;
                                }
                            else {
                                n = half - 1;
                                }
                            }
                        else {
                            ResourceDirEntLow = ResourceDirEntMiddle + 1;
                            n = half;
                            }
                        }
                    }
                else {
                    if (n != 0) {
                        dir = LdrpCompareResourceNames_U( *ResourceIdPath,
                          TopResourceDirectory,
                                                          ResourceDirEntLow
                                                        );
                        if (!dir) {   // find, or it fail to set ResourceDirectory so go to NotFound.
                            if (ResourceDirEntLow->DataIsDirectory) {
                                ResourceDirectory = (PIMAGE_RESOURCE_DIRECTORY)
                                    ((PCHAR)TopResourceDirectory +
                                        ResourceDirEntLow->OffsetToDirectory
                                    );
                                }
                            else {
                                ResourceEntry = (PIMAGE_RESOURCE_DATA_ENTRY)
                                    ((PCHAR)TopResourceDirectory +
                      ResourceDirEntLow->OffsetToData
                                    );
                                }
                            }
                        }

                    break;
                    }
                }

            ResourceIdPath++;
            }

        if (ResourceEntry != NULL && !(Flags & LDRP_FIND_RESOURCE_DIRECTORY)) {
            *ResourceDirectoryOrData = (PVOID)ResourceEntry;
            Status = STATUS_SUCCESS;
            }
        else
        if (ResourceDirectory != NULL && (Flags & LDRP_FIND_RESOURCE_DIRECTORY)) {
            *ResourceDirectoryOrData = (PVOID)ResourceDirectory;
            Status = STATUS_SUCCESS;
            }
        else {
NotFound:
            switch( IdPathLength - ResourceIdPathLength) {
                case 3:     Status = STATUS_RESOURCE_LANG_NOT_FOUND; break;
                case 2:     Status = STATUS_RESOURCE_NAME_NOT_FOUND; break;
                case 1:     Status = STATUS_RESOURCE_TYPE_NOT_FOUND; break;
                default:    Status = STATUS_INVALID_PARAMETER; break;
                }
            }

        if (Status == STATUS_RESOURCE_LANG_NOT_FOUND &&
            LanguageResourceDirectory != NULL
           ) {
            ResourceEntry = NULL;
            goto TryNextLangId;
ReturnFailure: ;
            Status = STATUS_RESOURCE_LANG_NOT_FOUND;
            }
#ifdef NTOS_USERMODE_RUNTIME
#ifdef MUI_MAGIC
        //
        // Load resource from alternative modules if
        // resource type doesn't exist in the main DLL.
        //
        else if (Status == STATUS_RESOURCE_TYPE_NOT_FOUND &&
            3 == IdPathLength ) {
                GET_UI_LANGID();

                if (UILangId) {
                    UIResourceIdPath[0]=IdPath[0];
                    UIResourceIdPath[1]=IdPath[1];
                    UIResourceIdPath[2]=UILangId;

//                  ASSERT (IdPath[0] != RT_VERSION);
                    if (IdPath[0] != RT_MANIFEST && IdPath[0] != RT_VERSION) {
                        Status = LdrpLoadResourceFromAlternativeModule(DllHandle,
                                    UIResourceIdPath,
                                    3,
                                    Flags,
                                    ResourceDirectoryOrData );
                    } else {
#if DBG
                            //
                            // Althernative module loading will invoke version resource loading for resource checksum
                            // and CMF info, so explicitly loading version resource from alternative module could cause loader deadlock
                            // Besides, in most cases, version resource should always exist in the main binary, we don't expect it to go
                            // through alternative module.
                            //
                            DbgPrintEx(DPFLTR_LDR_ID, DPFLTR_ERROR_LEVEL,
                                 "LDR: Module %p load version from alternative module, potential deadlocks!\n", DllHandle);
#endif
                        }

                }
            }
#endif
#endif
        }
    except (EXCEPTION_EXECUTE_HANDLER) {
        Status = GetExceptionCode();
        }

    return Status;
}

#if defined(NTOS_USERMODE_RUNTIME)

NTSTATUS
LdrpNativeReadVirtualMemory(
    IN HANDLE ProcessHandle,
    IN NATIVE_ULONG_PTR BaseAddress,
    OUT PVOID Buffer,
    IN SIZE_T BufferSize,
    OUT PSIZE_T NumberOfBytesRead OPTIONAL
    )
//
// NtWow64ReadVirtualMemory64 takes both 64bit pointers and 64bit sizes,
// but only 64bit pointers make sense for us. You can't read 64sizes into
// 32bit address spaces.
//
// This function is like NtReadVirtualMemory/NtWow64ReadVirtualMemory64
//  but the remote address is an integer and the sizes match the local address space.
//
{
    NTSTATUS Status;
#if defined(BUILD_WOW6432)
    NATIVE_SIZE_T NativeNumberOfBytesRead;

    Status = NtWow64ReadVirtualMemory64(ProcessHandle, (NATIVE_PVOID)BaseAddress, Buffer, BufferSize, &NativeNumberOfBytesRead);
    if (ARGUMENT_PRESENT(NumberOfBytesRead)) {
        *NumberOfBytesRead = (SIZE_T)NativeNumberOfBytesRead;
    }
#else
    Status = NtReadVirtualMemory(ProcessHandle, (NATIVE_PVOID)BaseAddress, Buffer, BufferSize, NumberOfBytesRead);
#endif
    return Status;
}

#if defined(BUILD_WOW6432)
#define LdrpNativeQueryVirtualMemory NtWow64QueryVirtualMemory64
#else
#define LdrpNativeQueryVirtualMemory NtQueryVirtualMemory
#endif

VOID
NTAPI
LdrDestroyOutOfProcessImage(
    IN OUT PLDR_OUT_OF_PROCESS_IMAGE Image
    )
/*++

Routine Description:

Arguments:

    Image -


Return Value:

    None.

--*/
{
    NTSTATUS Status = STATUS_SUCCESS;

    RtlFreeBuffer(&Image->HeadersBuffer);
    Image->Flags = 0;
    Image->ProcessHandle = NULL;
    Image->DllHandle = 0;
}

NTSTATUS
NTAPI
LdrCreateOutOfProcessImage(
    IN ULONG                      Flags,
    IN HANDLE                     ProcessHandle,
    IN ULONG64                    DllHandle,
    OUT PLDR_OUT_OF_PROCESS_IMAGE Image
    )
/*++

Routine Description:

    This function initialized the out parameter Image for use with
    other functions, like LdrFindOutOfProcessResource.

    It reads in the headers, in order to work with many existing inproc
        RtlImage* functions, without reading them in for every operation.

    When you are done with the Image, pass it to LdrDestroyOutOfProcessImage.

Arguments:

    Flags - fiddle with the behavior of the function
          LDR_DLL_MAPPED_AS_DATA  - "flat" memory mapping of file
          LDR_DLL_MAPPED_AS_IMAGE - SEC_IMAGE was passed to NtCreateSection, inter section padding
                                    reflected in offsets stored on disk is reflected in the address
                                    space this is the simpler situation
          LDR_DLL_MAPPED_AS_UNFORMATED_IMAGE - LDR_DLL_MAPPED_AS_IMAGE but LdrpWx86FormatVirtualImage
                                               hasn't run yet.

    ProcessHandle - The process DllHandle is in a mapped section in

    DllHandle - the base address of a mapped view in process ProcessHandle
        for legacy reasons, the lowest bit of this implies LDR_DLL_MAPPED_AS_DATA

    Image - an opaque object that you can pass to other "OutOfProcessImage" functions.


Return Value:

    NTSTATUS

--*/
{
    NATIVE_ULONG_PTR     RemoteAddress = 0;
    PRTL_BUFFER          Buffer = NULL;
    PIMAGE_DOS_HEADER    DosHeader = NULL;
    SIZE_T               Headers32Offset = 0;
    PIMAGE_NT_HEADERS32  Headers32 = NULL;
    SIZE_T               BytesRead = 0;
    SIZE_T               BytesToRead = 0;
    SIZE_T               Offset = 0;
    SIZE_T               InitialReadSize = 4096;
                     C_ASSERT(PAGE_SIZE >= 4096);
    NTSTATUS             Status = STATUS_SUCCESS;

    KdPrintEx((
        DPFLTR_LDR_ID,
        DPFLTR_TRACE_LEVEL,
        "LDR: %s(%lx, %p, %p, %p) beginning\n",
        __FUNCTION__,
        Flags,
        ProcessHandle,
        DllHandle,
        Image
        ));

    // if this assertion triggers, you probably passed a handle instead of a base address
    ASSERT(DllHandle >= 0xffff);

    // Unformated images are only ever 32bit on 64bit.
    // The memory manager doesn't "spread them out", ntdll.dll does it.
    // There is only a short span of time between the mapping of the image and
    // the code in ntdll.dll reformating it, we leave it to our caller to know
    // if they are in that path.
#if !defined(_WIN64) && !defined(BUILD_WOW6432)
    if ((Flags & LDR_DLL_MAPPED_AS_MASK) == LDR_DLL_MAPPED_AS_UNFORMATED_IMAGE) {
        Flags = (Flags & ~LDR_DLL_MAPPED_AS_MASK) | LDR_DLL_MAPPED_AS_IMAGE;
    }
#endif

    if (LDR_IS_DATAFILE_INTEGER(DllHandle)) {
        DllHandle = LDR_DATAFILE_TO_VIEW_INTEGER(DllHandle);
        ASSERT((Flags & LDR_DLL_MAPPED_AS_MASK) == 0 || (Flags & LDR_DLL_MAPPED_AS_MASK) == LDR_DLL_MAPPED_AS_DATA);
        Flags |= LDR_DLL_MAPPED_AS_DATA;
    }

    Image->Flags = Flags;
    Image->ProcessHandle = ProcessHandle;
    Image->DllHandle = DllHandle;

    RemoteAddress = (NATIVE_ULONG_PTR)DllHandle;
    Buffer = &Image->HeadersBuffer;
    RtlInitBuffer(Buffer, NULL, 0);

    if (ProcessHandle == NtCurrentProcess()) {
        Status = STATUS_SUCCESS;
        goto Exit;
    }

    //
    // first read 4k since that is generally enough and we can avoid
    // multiple calls to NtReadVirtualMemory
    //
    // 4k is also the smallest page NT has run on, so even if the .exe is
    // smaller than 4k, we should be able to read 4k
    //
    // The memory manager only allows two pages of headers, so it may be
    // faster to always read 2 * native_page_size or always 16k instead
    // the initial 4k read.
    //
    BytesToRead = InitialReadSize;
    if (!NT_SUCCESS(Status = RtlEnsureBufferSize(0, Buffer, Offset + BytesToRead))) {
        goto Exit;
    }
    Status = LdrpNativeReadVirtualMemory(ProcessHandle, RemoteAddress + Offset, Buffer->Buffer + Offset, BytesToRead, &BytesRead);
    if (Status == STATUS_PARTIAL_COPY && BytesRead != 0) {
        InitialReadSize = BytesRead;
    }
    else if (!NT_SUCCESS(Status)) {
        KdPrintEx((DPFLTR_LDR_ID, DPFLTR_ERROR_LEVEL, "LDR: %s(): NtReadVirtualMemory failed.\n", __FUNCTION__));
        goto Exit;
    }

    BytesToRead = sizeof(*DosHeader);
    if (Offset + BytesToRead > InitialReadSize) {
        if (!NT_SUCCESS(Status = RtlEnsureBufferSize(0, Buffer, Offset + BytesToRead))) {
            goto Exit;
        }
        if (!NT_SUCCESS(Status = LdrpNativeReadVirtualMemory(ProcessHandle, RemoteAddress + Offset, Buffer->Buffer + Offset, BytesToRead, &BytesRead))) {
            KdPrintEx((DPFLTR_LDR_ID, DPFLTR_ERROR_LEVEL, "LDR: %s(): NtReadVirtualMemory failed.\n", __FUNCTION__));
            goto Exit;
        }
        if (BytesToRead != BytesRead) {
            goto ReadTruncated;
        }
    }
    DosHeader = (PIMAGE_DOS_HEADER)Buffer->Buffer;
    if (DosHeader->e_magic != IMAGE_DOS_SIGNATURE) {
        goto InvalidImageFormat;
    }

    if (DosHeader->e_lfanew >= RTLP_IMAGE_MAX_DOS_HEADER) {
        goto InvalidImageFormat;
    }

    Offset += DosHeader->e_lfanew;

    BytesToRead =  RTL_SIZEOF_THROUGH_FIELD(IMAGE_NT_HEADERS32, FileHeader);
    {
        C_ASSERT(RTL_SIZEOF_THROUGH_FIELD(IMAGE_NT_HEADERS32, FileHeader)
            == RTL_SIZEOF_THROUGH_FIELD(IMAGE_NT_HEADERS64, FileHeader));
    }
    if (Offset + BytesToRead > InitialReadSize) {
        if (!NT_SUCCESS(Status = RtlEnsureBufferSize(0, Buffer, Offset + BytesToRead))) {
            goto Exit;
        }
        if (!NT_SUCCESS(Status = LdrpNativeReadVirtualMemory(ProcessHandle, RemoteAddress + Offset, Buffer->Buffer + Offset, BytesToRead, &BytesRead))) {
            goto Exit;
        }
        if (BytesToRead != BytesRead) {
            goto ReadTruncated;
        }
    }
    Headers32Offset = Offset;
    Headers32 = (PIMAGE_NT_HEADERS32)(Buffer->Buffer + Headers32Offset); // correct for 64bit too
    if (Headers32->Signature != IMAGE_NT_SIGNATURE) {
        goto InvalidImageFormat;
    }

    Offset += BytesToRead;
    BytesToRead = Headers32->FileHeader.SizeOfOptionalHeader
        + Headers32->FileHeader.NumberOfSections * IMAGE_SIZEOF_SECTION_HEADER;

    if (Offset + BytesToRead > InitialReadSize) {
        if (!NT_SUCCESS(Status = RtlEnsureBufferSize(0, Buffer, Offset + BytesToRead))) {
            goto Exit;
        }
        if (!NT_SUCCESS(Status = LdrpNativeReadVirtualMemory(ProcessHandle, RemoteAddress + Offset, Buffer->Buffer + Offset, BytesToRead, &BytesRead))) {
            goto Exit;
        }
        if (BytesToRead != BytesRead) {
            goto ReadTruncated;
        }
    }
    Headers32 = (PIMAGE_NT_HEADERS32)(Buffer->Buffer + Headers32Offset); // correct for 64bit too
    if (Headers32->OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR32_MAGIC
        && Headers32->OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR64_MAGIC
        ) {
        goto InvalidImageFormat;
    }
#if defined(_WIN64) || defined(BUILD_WOW6432)
    if ((Image->Flags & LDR_DLL_MAPPED_AS_MASK) == LDR_DLL_MAPPED_AS_UNFORMATED_IMAGE) {

        // This test is copied from ntdll.dll's conditional call to LdrpWx86FormatVirtualImage.
        if (
              Headers32->FileHeader.Machine == IMAGE_FILE_MACHINE_I386
           && Headers32->OptionalHeader.Magic == IMAGE_NT_OPTIONAL_HDR32_MAGIC
           && Headers32->OptionalHeader.SectionAlignment < NativePageSize
           ) {
                NTSTATUS st;
                NATIVE_MEMORY_BASIC_INFORMATION MemoryInformation;

                st =
                    LdrpNativeQueryVirtualMemory(
                        ProcessHandle,
                       (NATIVE_PVOID)RemoteAddress,
                       MemoryBasicInformation,
                       &MemoryInformation,
                       sizeof MemoryInformation,
                       NULL);

                if ((! NT_SUCCESS(st))
                    || ((MemoryInformation.Protect != PAGE_READONLY) && 
                        (MemoryInformation.Protect != PAGE_EXECUTE_READ))
                    ) {

                    Image->Flags = (Image->Flags & ~LDR_DLL_MAPPED_AS_MASK) | LDR_DLL_MAPPED_AS_DATA;
                } else {
                    Image->Flags = (Image->Flags & ~LDR_DLL_MAPPED_AS_MASK) | LDR_DLL_MAPPED_AS_IMAGE;
                }
        } else {
            Image->Flags = (Image->Flags & ~LDR_DLL_MAPPED_AS_MASK) | LDR_DLL_MAPPED_AS_IMAGE;
        }
    }
#endif
    Status = STATUS_SUCCESS;
Exit:
    if (!NT_SUCCESS(Status)) {
        LdrDestroyOutOfProcessImage(Image);
    }
    KdPrintEx((DPFLTR_LDR_ID, DPFLTR_LEVEL_STATUS(Status), "LDR: %s() exiting 0x%08lx\n", __FUNCTION__, Status));
    return Status;

ReadTruncated:
    Status = STATUS_UNSUCCESSFUL;
    goto Exit;

InvalidImageFormat:
    Status = STATUS_INVALID_IMAGE_FORMAT;
    goto Exit;
}

NTSTATUS
NTAPI
LdrFindCreateProcessManifest(
    IN ULONG                         Flags,
    IN OUT PLDR_OUT_OF_PROCESS_IMAGE Image,
    IN const ULONG_PTR*              IdPath,
    IN ULONG                         IdPathLength,
    OUT PIMAGE_RESOURCE_DATA_ENTRY   OutDataEntry
    )
/*++

Routine Description:

    This function is like LdrFindResource_U, but it can load resources
      from a file mapped into another process. It only works as much
      as has been needed.

Arguments:

    Flags -
        LDR_FIND_RESOURCE_LANGUAGE_CAN_FALLBACK - if the specified langid is not found,
                                            fallback on the usual or any strategy,
                                            the current implementation always loads the
                                            first langid
        LDR_FIND_RESOURCE_LANGUAGE_EXACT - only load the resource with exactly
                                            specified langid

    ProcessHandle - The process the DllHandle is valid in. Passed to NtReadVirtualMemory.

    DllHandle - Same as LdrFindResource_U
    ResourceIdPath - Same as LdrFindResource_U
    ResourceIdPathLength - Same as LdrFindResource_U
    OutDataEntry - Similar to LdrFindResource_U, but returned by value instead of address.

Return Value:

    NTSTATUS

--*/
{
    NTSTATUS Status = STATUS_SUCCESS;
    SIZE_T BytesRead = 0;
    SIZE_T BytesToRead = 0;
#if DBG
    ULONG Line = __LINE__;
#endif

    // we depend on these values sorting first
    C_ASSERT(CREATEPROCESS_MANIFEST_RESOURCE_ID == 1);
    C_ASSERT(ISOLATIONAWARE_MANIFEST_RESOURCE_ID == 2);

#if DBG
    KdPrintEx((
        DPFLTR_LDR_ID,
        DPFLTR_TRACE_LEVEL,
        "LDR: %s(0x%lx, %p, %p[%Id, %Id, %Id], %lu, %p) beginning\n",
        __FUNCTION__,
        Flags,
        Image,
        IdPath,
        // 3 is the usual number, type, id/name, language
        (IdPath != NULL && IdPathLength > 0) ? IdPath[0] : 0,
        (IdPath != NULL && IdPathLength > 1) ? IdPath[1] : 0,
        (IdPath != NULL && IdPathLength > 2) ? IdPath[2] : 0,
        IdPathLength,
        OutDataEntry
        ));
#endif

#define LDRP_CHECK_PARAMETER(x) if (!(x)) { ASSERT(x); return STATUS_INVALID_PARAMETER; }
    LDRP_CHECK_PARAMETER(Image != NULL);
    LDRP_CHECK_PARAMETER(Image->DllHandle != 0);
    LDRP_CHECK_PARAMETER(Image->ProcessHandle != NULL);
    LDRP_CHECK_PARAMETER(OutDataEntry != NULL);
    LDRP_CHECK_PARAMETER(IdPath != NULL);

    LDRP_CHECK_PARAMETER((Image->Flags & LDR_DLL_MAPPED_AS_MASK) != LDR_DLL_MAPPED_AS_UNFORMATED_IMAGE);

    // not all flags are implemented (only image vs. data is)
    LDRP_CHECK_PARAMETER((Flags & LDR_FIND_RESOURCE_LANGUAGE_EXACT) == 0);
    LDRP_CHECK_PARAMETER((Flags & LDRP_FIND_RESOURCE_DIRECTORY   ) == 0);

    RtlZeroMemory(OutDataEntry, sizeof(OutDataEntry));

    if (Image->ProcessHandle == NtCurrentProcess()) {
        PVOID  DirectoryOrData = NULL;
        PVOID  DllHandle = (PVOID)(ULONG_PTR)Image->DllHandle;

        Status = LdrpSearchResourceSection_U(
            (Image->Flags & LDR_DLL_MAPPED_AS_DATA)
                ? LDR_VIEW_TO_DATAFILE(DllHandle)
                : DllHandle,
            IdPath,
            IdPathLength,
            Flags,
            &DirectoryOrData
            );
        if (NT_SUCCESS(Status) && DirectoryOrData != NULL && OutDataEntry != NULL) {
            *OutDataEntry = *(PIMAGE_RESOURCE_DATA_ENTRY)DirectoryOrData;
        }
        goto Exit;
    }

    //
    // All we handle cross process currently is finding the first resource id,
    // first langid, of a given type.
    //
    // And we only handle numbers, not strings/names.
    //
    LDRP_CHECK_PARAMETER(Image->HeadersBuffer.Buffer != NULL);
    LDRP_CHECK_PARAMETER(IdPathLength == 3); // type, id/name, langid
    LDRP_CHECK_PARAMETER(IdPath[0] != 0); // type
    LDRP_CHECK_PARAMETER(IdPath[1] == 0 || IdPath[1] == CREATEPROCESS_MANIFEST_RESOURCE_ID); // just find first id
    LDRP_CHECK_PARAMETER(IdPath[2] == 0); // first langid

    // no strings/names, just numbers
    LDRP_CHECK_PARAMETER(IS_INTRESOURCE(IdPath[0]));
    LDRP_CHECK_PARAMETER(IS_INTRESOURCE(IdPath[1]));
    LDRP_CHECK_PARAMETER(IS_INTRESOURCE(IdPath[2]));

    __try {
        USHORT n = 0;
        USHORT half = 0;
        LONG   dir = 0;

        SIZE_T                        TopDirectorySize = 0;
        ULONG                         Size = 0;
        PIMAGE_RESOURCE_DIRECTORY     Directory = NULL;
        NATIVE_ULONG_PTR              RemoteDirectoryAddress = 0;
        UCHAR                         DirectoryBuffer[
                                          sizeof(IMAGE_RESOURCE_DIRECTORY)
                                          + sizeof(IMAGE_RESOURCE_DIRECTORY_ENTRY)
                                          ];
        PIMAGE_RESOURCE_DIRECTORY     TopDirectory = NULL;
        NATIVE_ULONG_PTR              RemoteTopDirectoryAddress = 0;
        RTL_BUFFER                    TopDirectoryBuffer = {0};
        UCHAR                         TopStaticDirectoryBuffer[256];
        C_ASSERT(sizeof(TopStaticDirectoryBuffer) >= sizeof(IMAGE_RESOURCE_DIRECTORY));

        IMAGE_RESOURCE_DATA_ENTRY     DataEntry;
        NATIVE_ULONG_PTR              RemoteDataEntryAddress = 0;

        PIMAGE_RESOURCE_DIRECTORY_ENTRY DirectoryEntry = NULL;

        PIMAGE_RESOURCE_DIRECTORY_ENTRY DirEntLow = NULL;
        PIMAGE_RESOURCE_DIRECTORY_ENTRY DirEntMiddle = NULL;
        PIMAGE_RESOURCE_DIRECTORY_ENTRY DirEntHigh = NULL;
        __try {
            RtlInitBuffer(&TopDirectoryBuffer, TopStaticDirectoryBuffer, sizeof(TopStaticDirectoryBuffer));
            Status = RtlEnsureBufferSize(0, &TopDirectoryBuffer, TopDirectoryBuffer.StaticSize);
            ASSERT(NT_SUCCESS(Status));

            RemoteTopDirectoryAddress = (ULONG_PTR)
                RtlImageDirectoryEntryToData(Image->HeadersBuffer.Buffer,
                                            (Image->Flags & LDR_DLL_MAPPED_AS_DATA) ? FALSE : TRUE,
                                             IMAGE_DIRECTORY_ENTRY_RESOURCE,
                                             &Size
                                             );

            if (RemoteTopDirectoryAddress == 0) {
                Status = STATUS_RESOURCE_DATA_NOT_FOUND;
                __leave;
            }
            //
            // rebase..
            //
            RemoteTopDirectoryAddress =
                LDR_DATAFILE_TO_VIEW_INTEGER((NATIVE_ULONG_PTR)Image->DllHandle)
                + RemoteTopDirectoryAddress
                - ((ULONG_PTR)Image->HeadersBuffer.Buffer);

            Status = LdrpNativeReadVirtualMemory(Image->ProcessHandle, RemoteTopDirectoryAddress, TopDirectoryBuffer.Buffer, TopDirectoryBuffer.Size, &BytesRead);
            if (Status == STATUS_PARTIAL_COPY && BytesRead >= sizeof(*TopDirectory)) {
                // nothing
            }
            else if (!NT_SUCCESS(Status)) {
                __leave;
            }


            TopDirectory = (PIMAGE_RESOURCE_DIRECTORY)TopDirectoryBuffer.Buffer;

            //
            // determine the size of the entire directory, including the named entries,
            // since they occur before the  numbered ones (note that we currently
            // don't optimize away reading of the named ones, even though we never
            // search them)
            //
            TopDirectorySize = sizeof(*TopDirectory)
                + (TopDirectory->NumberOfIdEntries + TopDirectory->NumberOfNamedEntries)
                   * sizeof(IMAGE_RESOURCE_DIRECTORY_ENTRY);

            //
            // now check the result of NtReadVirtualMemory again, if our guess was
            // big enough, but the read was not, error
            //
            if (TopDirectorySize <= TopDirectoryBuffer.Size
                && BytesRead < TopDirectorySize) {
                // REVIEW STATUS_PARTIAL_COPY is only a warning. Is it a strong enough return value?
                // Should we return STATUS_INVALID_IMAGE_FORMAT or STATUS_ACCESS_DENIED instead?
                // There are other places in this file where we propogate STATUS_PARTIAL_COPY, if
                // zero bytes are actually read.
                if (Status == STATUS_PARTIAL_COPY) {
                    __leave;
                }
#if DBG
                Line = __LINE__;
                DbgPrintEx(DPFLTR_LDR_ID, DPFLTR_ERROR_LEVEL,
                           "LDR: %s(): Line %lu NtReadVirtualMemory() succeeded, but returned too few bytes (0x%Ix out of 0x%Ix).\n",
                           __FUNCTION__, Line, BytesRead, BytesToRead);
#endif
                Status = STATUS_UNSUCCESSFUL;
                __leave;
            }

            //
            // if our initial guessed size was too small, read the correct size
            //
            if (TopDirectorySize > TopDirectoryBuffer.Size) {
                KdPrintEx((
                    DPFLTR_LDR_ID,
                    DPFLTR_ERROR_LEVEL, // otherwise we'll never see it
                    "LDR: %s(): %Id was not enough of a preread for a resource directory, %Id required.\n",
                    __FUNCTION__,
                    TopDirectoryBuffer.Size,
                    TopDirectorySize
                    ));
                Status = RtlEnsureBufferSize(0, &TopDirectoryBuffer, TopDirectorySize);
                if (!NT_SUCCESS(Status)) {
                    __leave;
                }
                Status = LdrpNativeReadVirtualMemory(Image->ProcessHandle, RemoteTopDirectoryAddress, TopDirectoryBuffer.Buffer, TopDirectoryBuffer.Size, &BytesRead);
                if (!NT_SUCCESS(Status)) {
                    __leave;
                }
                if (BytesRead != TopDirectoryBuffer.Size) {
#if DBG
                    Line = __LINE__;
                    DbgPrintEx(DPFLTR_LDR_ID, DPFLTR_ERROR_LEVEL,
                               "LDR: %s(): Line %lu NtReadVirtualMemory() succeeded, but returned too few bytes (0x%Ix out of 0x%Ix).\n",
                               __FUNCTION__, Line, BytesRead, BytesToRead);
#endif
                    Status = STATUS_UNSUCCESSFUL;
                    __leave;
                }

                TopDirectory = (PIMAGE_RESOURCE_DIRECTORY) TopDirectoryBuffer.Buffer;
            }

            // point to start of named entries
            DirEntLow = (PIMAGE_RESOURCE_DIRECTORY_ENTRY)(TopDirectory + 1);

            // move past named entries to numbered entries
            DirEntLow += TopDirectory->NumberOfNamedEntries;

            n = TopDirectory->NumberOfIdEntries;

            if (n == 0) {
                Status = STATUS_RESOURCE_TYPE_NOT_FOUND;
                __leave;
            }

            DirectoryEntry = NULL;
            Directory = NULL;
            DirEntHigh = DirEntLow + n - 1;
            while (DirEntLow <= DirEntHigh) {
                if ((half = (n >> 1)) != 0) {
                    DirEntMiddle = DirEntLow;
                    if (n & 1) {
                        DirEntMiddle += half;
                    }
                    else {
                        DirEntMiddle += half - 1;
                    }
                    if (DirEntMiddle->NameIsString) {
                        KdPrintEx((DPFLTR_LDR_ID, DPFLTR_ERROR_LEVEL, "LDR: No strings expected in %s().\n", __FUNCTION__));
                        ASSERT(FALSE);
                        Status = STATUS_INVALID_PARAMETER;
                        __leave;
                    }
                    dir = LdrpCompareResourceNames_U( *IdPath,
                                                      TopDirectory,
                                                      DirEntMiddle
                                                    );
                    if (dir == 0) {
                        if (DirEntMiddle->DataIsDirectory) {
                            Directory = (PIMAGE_RESOURCE_DIRECTORY)
                                    (((PCHAR)TopDirectory)
                                        + DirEntMiddle->OffsetToDirectory);
                        }
                        else {
                            KdPrintEx((DPFLTR_LDR_ID, DPFLTR_ERROR_LEVEL, "LDR: %s(): First id in resource path is expected to be a directory.\n", __FUNCTION__));
                            Status = STATUS_INVALID_PARAMETER;
                            __leave;

                            /* This is what you do if we allow specifying the id and language,
                            which we might do in the future.
                            Directory = NULL;
                            Entry = (PIMAGE_RESOURCE_DATA_ENTRY)
                                     (((PCHAR)TopDirectory)
                                        + DirEntMiddle->OffsetToData);
                            */
                        }
                        break;
                    }
                    else {
                        if (dir < 0) {
                            DirEntHigh = DirEntMiddle - 1;
                            if (n & 1) {
                                n = half;
                            }
                            else {
                                n = half - 1;
                            }
                        }
                        else {
                            DirEntLow = DirEntMiddle + 1;
                            n = half;
                        }
                    }
                }
                else {
                    if (n != 0) {
                        if (DirEntLow->NameIsString) {
                            KdPrintEx((DPFLTR_LDR_ID, DPFLTR_ERROR_LEVEL, "LDR: %s() No strings expected.\n", __FUNCTION__));
                            Status = STATUS_INVALID_PARAMETER;
                            __leave;
                        }
                        dir = LdrpCompareResourceNames_U( *IdPath,
                                                  TopDirectory,
                                                  DirEntLow
                                                 );
                        if (dir == 0) {
                            if (DirEntLow->DataIsDirectory) {
                                Directory = (PIMAGE_RESOURCE_DIRECTORY)
                                        (((PCHAR)TopDirectory)
                                            + DirEntLow->OffsetToDirectory);
                            }
                            else {
                                KdPrintEx((DPFLTR_LDR_ID, DPFLTR_ERROR_LEVEL, "LDR: %s() First id in resource path is expected to be a directory", __FUNCTION__));
                                Status = STATUS_INVALID_PARAMETER;
                                __leave;

                                /*
                                Entry = (PIMAGE_RESOURCE_DATA_ENTRY)
                                        (((PCHAR)TopDirectory)
                                            + DirEntLow->OffsetToData);
                                            */
                            }
                        }
                    }
                    break;
                }
            }
            //
            // ok, now we have found address of the type's name/id directory (or not)
            //
            if (Directory == NULL) {
                Status = STATUS_RESOURCE_TYPE_NOT_FOUND;
                __leave;
            }

            //
            // we copied the binary search and didn't quite compute what we want,
            // it found the local address, change this to an offset and apply
            // to the remote address ("rebase")
            //
            RemoteDirectoryAddress =
                  RemoteTopDirectoryAddress
                + ((ULONG_PTR)Directory)
                - ((ULONG_PTR)TopDirectory);

            //
            // Now do the read of both the directory and the first entry.
            //
            Directory = (PIMAGE_RESOURCE_DIRECTORY)&DirectoryBuffer[0];
            Status = LdrpNativeReadVirtualMemory(Image->ProcessHandle, RemoteDirectoryAddress, Directory, sizeof(DirectoryBuffer), &BytesRead);
            if (!NT_SUCCESS(Status)) {
                KdPrintEx((DPFLTR_LDR_ID, DPFLTR_ERROR_LEVEL, "LDR: %s() NtReadVirtualMemory failed.", __FUNCTION__));
                __leave;
            }
            if (BytesRead != sizeof(DirectoryBuffer)) {
#if DBG
                Line = __LINE__;
                DbgPrintEx(DPFLTR_LDR_ID, DPFLTR_ERROR_LEVEL,
                           "LDR: %s(): Line %lu NtReadVirtualMemory() succeeded, but returned too few bytes (0x%Ix out of 0x%Ix).\n",
                           __FUNCTION__, Line, BytesRead, BytesToRead);
#endif
                Status = STATUS_UNSUCCESSFUL;
                __leave;
            }
            if ((Directory->NumberOfNamedEntries + Directory->NumberOfIdEntries) == 0) {
                Status = STATUS_RESOURCE_NAME_NOT_FOUND;
                __leave;
            }

            if (IdPath[1] == CREATEPROCESS_MANIFEST_RESOURCE_ID && Directory->NumberOfNamedEntries != 0) {
                KdPrintEx((
                    DPFLTR_LDR_ID,
                    DPFLTR_ERROR_LEVEL,
                    "LDR: %s() caller asked for id==1 but there are named entries we are not bothering to skip.\n",
                    __FUNCTION__
                    ));
                Status = STATUS_RESOURCE_NAME_NOT_FOUND;
                __leave;
            }

            //
            // grab the entry for the first id
            //
            DirectoryEntry = (PIMAGE_RESOURCE_DIRECTORY_ENTRY)(Directory + 1);
            if (!DirectoryEntry->DataIsDirectory) {
                KdPrintEx((DPFLTR_LDR_ID, DPFLTR_ERROR_LEVEL, "LDR: Second level of resource directory is expected to be a directory\n"));
                Status = STATUS_INVALID_IMAGE_FORMAT; // REVIEW too strong?
                __leave;
            }

            //
            // If there is more than one entry, ensure no conflicts.
            //
            if (Directory->NumberOfIdEntries > 1
                && DirectoryEntry->Id >= MINIMUM_RESERVED_MANIFEST_RESOURCE_ID
                && DirectoryEntry->Id <= MAXIMUM_RESERVED_MANIFEST_RESOURCE_ID
                ) {
                NATIVE_ULONG_PTR RemoteDirectoryEntryPointer = 0;
                IMAGE_RESOURCE_DIRECTORY_ENTRY   DirectoryEntries[
                                                    MAXIMUM_RESERVED_MANIFEST_RESOURCE_ID
                                                    - MINIMUM_RESERVED_MANIFEST_RESOURCE_ID
                                                    + 1];
                ULONG ResourceId;
                ULONG NumberOfEntriesToCheck;
                ULONG CountOfReservedManifestIds;

                C_ASSERT(MINIMUM_RESERVED_MANIFEST_RESOURCE_ID == 1);

                RemoteDirectoryEntryPointer = RemoteDirectoryAddress + sizeof(IMAGE_RESOURCE_DIRECTORY);

                NumberOfEntriesToCheck = LDRP_MIN(RTL_NUMBER_OF(DirectoryEntries), Directory->NumberOfIdEntries);
                BytesToRead = sizeof(IMAGE_RESOURCE_DIRECTORY_ENTRY) * NumberOfEntriesToCheck;
                ASSERT(BytesToRead <= sizeof(DirectoryEntries));

                Status = LdrpNativeReadVirtualMemory(Image->ProcessHandle, RemoteDirectoryEntryPointer, &DirectoryEntries[0], BytesToRead, &BytesRead);
                if (!NT_SUCCESS(Status)) {
                    KdPrintEx((DPFLTR_LDR_ID, DPFLTR_ERROR_LEVEL, "LDR: %s() NtReadVirtualMemory failed.", __FUNCTION__));
                    __leave;
                }
                if (BytesRead != BytesToRead) {
#if DBG
                    Line = __LINE__;
                    DbgPrintEx(DPFLTR_LDR_ID, DPFLTR_ERROR_LEVEL,
                               "LDR: %s(): Line %lu NtReadVirtualMemory() succeeded, but returned too few bytes (0x%Ix out of 0x%Ix).\n",
                               __FUNCTION__, Line, BytesRead, BytesToRead);
#endif
                    Status = STATUS_UNSUCCESSFUL;
                    __leave;
                }

                CountOfReservedManifestIds = 0;

                for (ResourceId = MINIMUM_RESERVED_MANIFEST_RESOURCE_ID;
                     ResourceId != MINIMUM_RESERVED_MANIFEST_RESOURCE_ID + NumberOfEntriesToCheck;
                     ResourceId += 1
                    ) {
                    if (DirectoryEntries[ResourceId - MINIMUM_RESERVED_MANIFEST_RESOURCE_ID].Id >= MINIMUM_RESERVED_MANIFEST_RESOURCE_ID
                        && DirectoryEntries[ResourceId - MINIMUM_RESERVED_MANIFEST_RESOURCE_ID].Id <= MAXIMUM_RESERVED_MANIFEST_RESOURCE_ID
                        ) {
                        CountOfReservedManifestIds += 1;
                        if (CountOfReservedManifestIds > 1) {
#if DBG
                            DbgPrintEx(
                                DPFLTR_LDR_ID,
                                DPFLTR_ERROR_LEVEL,
                                "LDR: %s() multiple reserved manifest resource ids present\n",
                                __FUNCTION__
                                );
#endif
                            Status = STATUS_INVALID_PARAMETER;
                            __leave;
                        }
                    }
                }
            }

            if (IdPath[1] == CREATEPROCESS_MANIFEST_RESOURCE_ID && DirectoryEntry->Id != CREATEPROCESS_MANIFEST_RESOURCE_ID) {
                Status = STATUS_RESOURCE_NAME_NOT_FOUND;
                __leave;
            }

            //
            // now get address of langid directory
            //
            RemoteDirectoryAddress = RemoteTopDirectoryAddress + DirectoryEntry->OffsetToDirectory;

            //
            // now read the langid directory and its first entry
            //
            Status = LdrpNativeReadVirtualMemory(Image->ProcessHandle, RemoteDirectoryAddress, Directory, sizeof(DirectoryBuffer), &BytesRead);
            if (!NT_SUCCESS(Status)) {
                KdPrintEx((DPFLTR_LDR_ID, DPFLTR_ERROR_LEVEL, "LDR: %s() NtReadVirtualMemory failed.", __FUNCTION__));
                __leave;
            }
            if (BytesRead != sizeof(DirectoryBuffer)) {
#if DBG
                DbgPrintEx(DPFLTR_LDR_ID, DPFLTR_ERROR_LEVEL,
                           "LDR: %s(): Line %lu NtReadVirtualMemory() succeeded, but returned too few bytes (0x%Ix out of 0x%Ix).\n",
                           __FUNCTION__, Line, BytesRead, BytesToRead);
#endif
                Status = STATUS_UNSUCCESSFUL;
                __leave;
            }
            if ((Directory->NumberOfNamedEntries + Directory->NumberOfIdEntries) == 0) {
                Status = STATUS_RESOURCE_LANG_NOT_FOUND;
                __leave;
            }

            //
            // look at the langid directory's first entry
            //
            if (DirectoryEntry->DataIsDirectory) {
                KdPrintEx((DPFLTR_LDR_ID, DPFLTR_ERROR_LEVEL, "LDR: Third level of resource directory is not expected to be a directory\n"));
                Status = STATUS_INVALID_IMAGE_FORMAT; // REVIEW too strong?
                __leave;
            }
            RemoteDataEntryAddress =
                  RemoteTopDirectoryAddress
                + DirectoryEntry->OffsetToData;

            //
            // read the data entry
            //
            Status = LdrpNativeReadVirtualMemory(Image->ProcessHandle, RemoteDataEntryAddress, &DataEntry, sizeof(DataEntry), &BytesRead);
            if (!NT_SUCCESS(Status)) {
                KdPrintEx((DPFLTR_LDR_ID, DPFLTR_ERROR_LEVEL, "LDR: %s() NtReadVirtualMemory failed.", __FUNCTION__));
                __leave;
            }
            if (BytesRead != sizeof(DataEntry)) {
#if DBG
                DbgPrintEx(DPFLTR_LDR_ID, DPFLTR_ERROR_LEVEL,
                           "LDR: %s(): Line %lu NtReadVirtualMemory() succeeded, but returned too few bytes (0x%Ix out of 0x%Ix).\n",
                           __FUNCTION__, Line, BytesRead, BytesToRead);
#endif
                Status = STATUS_UNSUCCESSFUL;
                __leave;
            }

            *OutDataEntry = DataEntry;
            Status = STATUS_SUCCESS;
        }
        __finally {
            RtlFreeBuffer(&TopDirectoryBuffer);
        }
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        Status = GetExceptionCode();
    }
Exit:
#if DBG
    //
    // Fix/raid dcpromo, msiexec, etc..
    // DPFLTR_LEVEL_STATUS filters all forms of resource not found.
    //
    if (DPFLTR_LEVEL_STATUS(Status) == DPFLTR_ERROR_LEVEL) {
        KdPrintEx((
            DPFLTR_LDR_ID,
            DPFLTR_LEVEL_STATUS(Status),
            "LDR: %s(0x%lx, %p, %p[%Id, %Id, %Id], %lu, %p) failed %08x\n",
            __FUNCTION__,
            Flags,
            Image,
            IdPath,
            // 3 is the usual number, type, id/name, language
            (IdPath != NULL && IdPathLength > 0) ? IdPath[0] : 0,
            (IdPath != NULL && IdPathLength > 1) ? IdPath[1] : 0,
            (IdPath != NULL && IdPathLength > 2) ? IdPath[2] : 0,
            IdPathLength,
            OutDataEntry,
            Status
            ));
        KdPrintEx((
            DPFLTR_LDR_ID,
            DPFLTR_LEVEL_STATUS(Status),
            "LDR: %s() returning Status:0x%lx IMAGE_RESOURCE_DATA_ENTRY:{OffsetToData=%#lx, Size=%#lx}\n",
            __FUNCTION__,
            Status,
            (OutDataEntry != NULL) ? OutDataEntry->OffsetToData : 0,
            (OutDataEntry != NULL) ? OutDataEntry->Size : 0
            ));
    }
#endif
    return Status;
}

NTSTATUS
NTAPI
LdrAccessOutOfProcessResource(
    IN ULONG                            Flags,
    IN OUT PLDR_OUT_OF_PROCESS_IMAGE    Image, // currently only IN
    IN const IMAGE_RESOURCE_DATA_ENTRY* DataEntry,
    OUT PULONG64                        Address OPTIONAL,
    OUT PULONG                          Size OPTIONAL
    )
/*++

Routine Description:

    This function is like LdrAccessResource, but it works on images
    mapped out of process.

Arguments:

    Flags -

    Image - an opaque object representing an image or file mapped into another process,
        created with LdrCreateOutOfProcessImage.

    DataEntry - Same as LdrAccessResource, but returned by-value from LdrFindOutOfProcessResource

    Address - Same as LdrAccessResource

    Size - Same as LdrAccessResource

Return Value:

    NTSTATUS

--*/
{
    NTSTATUS Status = 0;
    PVOID LocalAddress = 0;
    ULONG_PTR LocalHeaders = 0;

    ASSERT(Image != NULL);
    ASSERT(Image->DllHandle != 0);
    ASSERT(Image->ProcessHandle != NULL);

    if (ARGUMENT_PRESENT(Address)) {
        *Address = 0;
    }

    if (Image->ProcessHandle != NtCurrentProcess()) {
        ASSERT(Image->HeadersBuffer.Buffer != NULL);
        LocalHeaders = (ULONG_PTR)Image->HeadersBuffer.Buffer;
    } else {
        LocalHeaders = (ULONG_PTR)Image->DllHandle;
    }
    if ((Image->Flags & LDR_DLL_MAPPED_AS_DATA) != 0) {
        LocalHeaders = LDR_VIEW_TO_DATAFILE_INTEGER(LocalHeaders);
    }

    Status = LdrpAccessResourceDataNoMultipleLanguage(
        (PVOID)LocalHeaders,
        DataEntry,
        &LocalAddress,
        Size
        );

    if (NT_SUCCESS(Status)
        && ARGUMENT_PRESENT(Address)
        && LocalAddress != NULL
        ) {
        //
        // rebase if out of proc, else for inproc Image->DllHandle - LocalHeaders == 0.
        //
        *Address = LDR_DATAFILE_TO_VIEW_INTEGER(Image->DllHandle)
                + ((ULONG_PTR)LocalAddress)
                - LDR_DATAFILE_TO_VIEW_INTEGER(LocalHeaders)
                ;
    }

#if DBG
    if (!NT_SUCCESS(Status)) {
        DbgPrintEx(DPFLTR_LDR_ID, DPFLTR_LEVEL_STATUS(Status), "LDR: %s() exiting 0x%08lx\n", __FUNCTION__, Status);
    }
#endif

    return Status;
}

#endif

NTSTATUS
LdrEnumResources(
    IN PVOID DllHandle,
    IN const ULONG_PTR* ResourceIdPath,
    IN ULONG ResourceIdPathLength,
    IN OUT PULONG NumberOfResources,
    OUT PLDR_ENUM_RESOURCE_ENTRY Resources OPTIONAL
    )
{
    NTSTATUS Status;
    PIMAGE_RESOURCE_DIRECTORY TopResourceDirectory;
    PIMAGE_RESOURCE_DIRECTORY TypeResourceDirectory;
    PIMAGE_RESOURCE_DIRECTORY NameResourceDirectory;
    PIMAGE_RESOURCE_DIRECTORY LangResourceDirectory;
    PIMAGE_RESOURCE_DIRECTORY_ENTRY TypeResourceDirectoryEntry;
    PIMAGE_RESOURCE_DIRECTORY_ENTRY NameResourceDirectoryEntry;
    PIMAGE_RESOURCE_DIRECTORY_ENTRY LangResourceDirectoryEntry;
    ULONG TypeDirectoryIndex, NumberOfTypeDirectoryEntries;
    ULONG NameDirectoryIndex, NumberOfNameDirectoryEntries;
    ULONG LangDirectoryIndex, NumberOfLangDirectoryEntries;
    BOOLEAN ScanTypeDirectory;
    BOOLEAN ScanNameDirectory;
    BOOLEAN ReturnThisResource;
    PIMAGE_RESOURCE_DIR_STRING_U ResourceNameString;
    ULONG_PTR TypeResourceNameOrId;
    ULONG_PTR NameResourceNameOrId;
    ULONG_PTR LangResourceNameOrId;
    PLDR_ENUM_RESOURCE_ENTRY ResourceInfo;
    PIMAGE_RESOURCE_DATA_ENTRY ResourceDataEntry;
    ULONG ResourceIndex, MaxResourceIndex;
    ULONG Size;

    ResourceIndex = 0;
    if (!ARGUMENT_PRESENT( Resources )) {
        MaxResourceIndex = 0;
        }
    else {
        MaxResourceIndex = *NumberOfResources;
        }
    *NumberOfResources = 0;

    TopResourceDirectory = (PIMAGE_RESOURCE_DIRECTORY)
        RtlImageDirectoryEntryToData( DllHandle,
                                      TRUE,
                                      IMAGE_DIRECTORY_ENTRY_RESOURCE,
                                      &Size
                                    );
    if (!TopResourceDirectory) {
        return STATUS_RESOURCE_DATA_NOT_FOUND;
        }

    TypeResourceDirectory = TopResourceDirectory;
    TypeResourceDirectoryEntry = (PIMAGE_RESOURCE_DIRECTORY_ENTRY)(TypeResourceDirectory+1);
    NumberOfTypeDirectoryEntries = TypeResourceDirectory->NumberOfNamedEntries +
                                   TypeResourceDirectory->NumberOfIdEntries;
    TypeDirectoryIndex = 0;
    Status = STATUS_SUCCESS;
    for (TypeDirectoryIndex=0;
         TypeDirectoryIndex<NumberOfTypeDirectoryEntries;
         TypeDirectoryIndex++, TypeResourceDirectoryEntry++
        ) {
        if (ResourceIdPathLength > 0) {
            ScanTypeDirectory = LdrpCompareResourceNames_U( ResourceIdPath[ 0 ],
                                                            TopResourceDirectory,
                                                            TypeResourceDirectoryEntry
                                                          ) == 0;
            }
        else {
            ScanTypeDirectory = TRUE;
            }
        if (ScanTypeDirectory) {
            if (!TypeResourceDirectoryEntry->DataIsDirectory) {
                return STATUS_INVALID_IMAGE_FORMAT;
                }
            if (TypeResourceDirectoryEntry->NameIsString) {
                ResourceNameString = (PIMAGE_RESOURCE_DIR_STRING_U)
                    ((PCHAR)TopResourceDirectory + TypeResourceDirectoryEntry->NameOffset);

                TypeResourceNameOrId = (ULONG_PTR)ResourceNameString;
                }
            else {
                TypeResourceNameOrId = (ULONG_PTR)TypeResourceDirectoryEntry->Id;
                }

            NameResourceDirectory = (PIMAGE_RESOURCE_DIRECTORY)
                ((PCHAR)TopResourceDirectory + TypeResourceDirectoryEntry->OffsetToDirectory);
            NameResourceDirectoryEntry = (PIMAGE_RESOURCE_DIRECTORY_ENTRY)(NameResourceDirectory+1);
            NumberOfNameDirectoryEntries = NameResourceDirectory->NumberOfNamedEntries +
                                           NameResourceDirectory->NumberOfIdEntries;
            for (NameDirectoryIndex=0;
                 NameDirectoryIndex<NumberOfNameDirectoryEntries;
                 NameDirectoryIndex++, NameResourceDirectoryEntry++
                ) {
                if (ResourceIdPathLength > 1) {
                    ScanNameDirectory = LdrpCompareResourceNames_U( ResourceIdPath[ 1 ],
                                                                    TopResourceDirectory,
                                                                    NameResourceDirectoryEntry
                                                                  ) == 0;
                    }
                else {
                    ScanNameDirectory = TRUE;
                    }
                if (ScanNameDirectory) {
                    if (!NameResourceDirectoryEntry->DataIsDirectory) {
                        return STATUS_INVALID_IMAGE_FORMAT;
                        }

                    if (NameResourceDirectoryEntry->NameIsString) {
                        ResourceNameString = (PIMAGE_RESOURCE_DIR_STRING_U)
                            ((PCHAR)TopResourceDirectory + NameResourceDirectoryEntry->NameOffset);

                        NameResourceNameOrId = (ULONG_PTR)ResourceNameString;
                        }
                    else {
                        NameResourceNameOrId = (ULONG_PTR)NameResourceDirectoryEntry->Id;
                        }

                    LangResourceDirectory = (PIMAGE_RESOURCE_DIRECTORY)
                        ((PCHAR)TopResourceDirectory + NameResourceDirectoryEntry->OffsetToDirectory);

                    LangResourceDirectoryEntry = (PIMAGE_RESOURCE_DIRECTORY_ENTRY)(LangResourceDirectory+1);
                    NumberOfLangDirectoryEntries = LangResourceDirectory->NumberOfNamedEntries +
                                                   LangResourceDirectory->NumberOfIdEntries;
                    LangDirectoryIndex = 0;
                    for (LangDirectoryIndex=0;
                         LangDirectoryIndex<NumberOfLangDirectoryEntries;
                         LangDirectoryIndex++, LangResourceDirectoryEntry++
                        ) {
                        if (ResourceIdPathLength > 2) {
                            ReturnThisResource = LdrpCompareResourceNames_U( ResourceIdPath[ 2 ],
                                                                             TopResourceDirectory,
                                                                             LangResourceDirectoryEntry
                                                                           ) == 0;
                            }
                        else {
                            ReturnThisResource = TRUE;
                            }
                        if (ReturnThisResource) {
                            if (LangResourceDirectoryEntry->DataIsDirectory) {
                                return STATUS_INVALID_IMAGE_FORMAT;
                                }

                            if (LangResourceDirectoryEntry->NameIsString) {
                                ResourceNameString = (PIMAGE_RESOURCE_DIR_STRING_U)
                                    ((PCHAR)TopResourceDirectory + LangResourceDirectoryEntry->NameOffset);

                                LangResourceNameOrId = (ULONG_PTR)ResourceNameString;
                                }
                            else {
                                LangResourceNameOrId = (ULONG_PTR)LangResourceDirectoryEntry->Id;
                                }

                            ResourceDataEntry = (PIMAGE_RESOURCE_DATA_ENTRY)
                                    ((PCHAR)TopResourceDirectory + LangResourceDirectoryEntry->OffsetToData);

                            ResourceInfo = &Resources[ ResourceIndex++ ];
                            if (ResourceIndex <= MaxResourceIndex) {
                                ResourceInfo->Path[ 0 ].NameOrId = TypeResourceNameOrId;
                                ResourceInfo->Path[ 1 ].NameOrId = NameResourceNameOrId;
                                ResourceInfo->Path[ 2 ].NameOrId = LangResourceNameOrId;
                                ResourceInfo->Data = (PVOID)((ULONG_PTR)DllHandle + ResourceDataEntry->OffsetToData);
                                ResourceInfo->Size = ResourceDataEntry->Size;
                                ResourceInfo->Reserved = 0;
                                }
                            else {
                                Status = STATUS_INFO_LENGTH_MISMATCH;
                                }
                            }
                        }
                    }
                }
            }
        }

    *NumberOfResources = ResourceIndex;
    return Status;
}

#ifdef NTOS_USERMODE_RUNTIME

BOOLEAN
LdrAlternateResourcesEnabled(
    VOID
    )

/*++

Routine Description:

    This function determines if the althernate resources are enabled.

Arguments:

    None.

Return Value:

    True - Alternate Resource enabled.
    False - Alternate Resource not enabled.

--*/

{
    NTSTATUS Status;

    GET_UI_LANGID();

    if (!UILangId){
        return FALSE;
        }

    if (!InstallLangId){
        Status = NtQueryInstallUILanguage( &InstallLangId);

        if (!NT_SUCCESS( Status )) {
            //
            //  Failed to get Intall LangID.  AltResource not enabled.
            //
            return FALSE;
            }
        }
#ifndef MUI_MAGIC
    if (UILangId == InstallLangId) {
        //
        //  UI Lang matches Installed Lang. AltResource not enabled.
        //
        return FALSE;
        }
#endif
    return TRUE;
}

PVOID
LdrGetAlternateResourceModuleHandle(
    IN PVOID Module
    )
{
    return LdrpGetAlternateResourceModuleHandle(Module, 0);
}


PVOID
LdrpGetAlternateResourceModuleHandle(
    IN PVOID Module,
    IN LANGID LangId
    )
/*++

Routine Description:

    This function gets the alternate resource module from the table
    containing the handle.

Arguments:

    Module - Module of which alternate resource module needs to loaded.

Return Value:

   Handle of the alternate resource module.

--*/

{
    ULONG ModuleIndex;

    if (!LangId) {
        GET_UI_LANGID();
        LangId = UILangId;
        }

    if (!LangId) {
        return NULL;
        }

    for (ModuleIndex = 0;
         ModuleIndex < AlternateResourceModuleCount;
         ModuleIndex++ ){
        if (AlternateResourceModules[ModuleIndex].ModuleBase == Module &&
           AlternateResourceModules[ModuleIndex].LangId == LangId ){
            return AlternateResourceModules[ModuleIndex].AlternateModule;
        }
    }
    return NULL;
}

BOOLEAN
LdrpGetFileVersion(
    IN  PVOID      ImageBase,
    IN  LANGID     LangId,
    OUT PULONGLONG Version,
    OUT PVOID  *VersionResource,
    OUT ULONG *VersionResSize

    )

/*++

Routine Description:

    Get the version stamp out of the VS_FIXEDFILEINFO resource in a PE
    image.

Arguments:

    ImageBase - supplies the address in memory where the file is mapped in.

    Version - receives 64bit version number, or 0 if the file is not
        a PE image or has no version data.

Return Value:

    None.

--*/

{
    PIMAGE_RESOURCE_DATA_ENTRY DataEntry;
    NTSTATUS Status;
    ULONG_PTR IdPath[3];
    ULONG ResourceSize;


    typedef struct tagVS_FIXEDFILEINFO
    {
        LONG   dwSignature;            /* e.g. 0xfeef04bd */
        LONG   dwStrucVersion;         /* e.g. 0x00000042 = "0.42" */
        LONG   dwFileVersionMS;        /* e.g. 0x00030075 = "3.75" */
        LONG   dwFileVersionLS;        /* e.g. 0x00000031 = "0.31" */
        LONG   dwProductVersionMS;     /* e.g. 0x00030010 = "3.10" */
        LONG   dwProductVersionLS;     /* e.g. 0x00000031 = "0.31" */
        LONG   dwFileFlagsMask;        /* = 0x3F for version "0.42" */
        LONG   dwFileFlags;            /* e.g. VFF_DEBUG | VFF_PRERELEASE */
        LONG   dwFileOS;               /* e.g. VOS_DOS_WINDOWS16 */
        LONG   dwFileType;             /* e.g. VFT_DRIVER */
        LONG   dwFileSubtype;          /* e.g. VFT2_DRV_KEYBOARD */
        LONG   dwFileDateMS;           /* e.g. 0 */
        LONG   dwFileDateLS;           /* e.g. 0 */
    } VS_FIXEDFILEINFO;

    struct {
        USHORT TotalSize;
        USHORT DataSize;
        USHORT Type;
        WCHAR Name[16];              // L"VS_VERSION_INFO" + unicode null terminator
        VS_FIXEDFILEINFO FixedFileInfo;
    } *Resource;

    *Version = 0;


    IdPath[0] = RT_VERSION;
    IdPath[1] = 1;
    IdPath[2] = LangId;

    try {
          Status = LdrpSearchResourceSection_U(
                    ImageBase,
                    IdPath,
                    3,
                    LDR_FIND_RESOURCE_LANGUAGE_EXACT,
                    &DataEntry);
    } except(EXCEPTION_EXECUTE_HANDLER) {
        Status = STATUS_UNSUCCESSFUL;
    }

    if(!NT_SUCCESS(Status)) {
        return FALSE;
    }

    try {  // we only interested in current ImageBase, if this DataEntry is not of this one, it should fail.
        Status = LdrpAccessResourceDataNoMultipleLanguage(
                    ImageBase,
                    DataEntry,
                    &Resource,
                    &ResourceSize);
    } except(EXCEPTION_EXECUTE_HANDLER) {
        Status = STATUS_UNSUCCESSFUL;
    }

    if(!NT_SUCCESS(Status)) {
        return FALSE;
    }

    try {
        if((ResourceSize >= sizeof(*Resource))
            && !_wcsicmp(Resource->Name,L"VS_VERSION_INFO")) {

            *Version = ((ULONGLONG)Resource->FixedFileInfo.dwFileVersionMS << 32)
                     | (ULONGLONG)Resource->FixedFileInfo.dwFileVersionLS;

            if (VersionResource)
            {
                *VersionResource = Resource;
            }

            if (VersionResSize)
            {
                *VersionResSize = ResourceSize;
            }
          
        } else {
            DbgPrint(("LDR: Warning: invalid version resource\n"));
            return FALSE;
        }
    } except(EXCEPTION_EXECUTE_HANDLER) {
        DbgPrint(("LDR: Exception encountered processing bogus version resource\n"));
        return FALSE;
    }
    return TRUE;
}

BOOLEAN
LdrpGetResourceChecksum(
    IN PVOID Module,
    OUT unsigned char** ppMD5Checksum
    )
{
    NTSTATUS Status;
    ULONG_PTR IdPath[3];
    ULONG ResourceSize;
    PIMAGE_RESOURCE_DATA_ENTRY DataEntry;

    LONG BlockLen;
    LONG VarFileInfoSize;
    ULONGLONG version;

    typedef struct tagVS_FIXEDFILEINFO
    {
        LONG   dwSignature;            /* e.g. 0xfeef04bd */
        LONG   dwStrucVersion;         /* e.g. 0x00000042 = "0.42" */
        LONG   dwFileVersionMS;        /* e.g. 0x00030075 = "3.75" */
        LONG   dwFileVersionLS;        /* e.g. 0x00000031 = "0.31" */
        LONG   dwProductVersionMS;     /* e.g. 0x00030010 = "3.10" */
        LONG   dwProductVersionLS;     /* e.g. 0x00000031 = "0.31" */
        LONG   dwFileFlagsMask;        /* = 0x3F for version "0.42" */
        LONG   dwFileFlags;            /* e.g. VFF_DEBUG | VFF_PRERELEASE */
        LONG   dwFileOS;               /* e.g. VOS_DOS_WINDOWS16 */
        LONG   dwFileType;             /* e.g. VFT_DRIVER */
        LONG   dwFileSubtype;          /* e.g. VFT2_DRV_KEYBOARD */
        LONG   dwFileDateMS;           /* e.g. 0 */
        LONG   dwFileDateLS;           /* e.g. 0 */
    } VS_FIXEDFILEINFO;

    struct
    {
        USHORT TotalSize;
        USHORT DataSize;
        USHORT Type;
        WCHAR Name[16];              // L"VS_VERSION_INFO" + unicode null terminator
        // Note that the previous 4 members has 16*2 + 3*2 = 38 bytes.
        // So that compiler will silently add a 2 bytes padding to make
        // FixedFileInfo to align in DWORD boundary.
        VS_FIXEDFILEINFO FixedFileInfo;
    } *Resource;

    typedef struct tagVERBLOCK
    {
        USHORT wTotalLen;
        USHORT wValueLen;
        USHORT wType;
        WCHAR szKey[1];
        // BYTE[] padding
        // WORD value;
    } VERBLOCK;
    VERBLOCK *pVerBlock;

   //
   // We look for Module first with UILangID, then with Netral lang ID. this can cover our current scenario of searching
   // If we want to look for more language version, we can give 0 for a lang ID and we change the LdrpGetFileVersion(cancel 
   // LDR_FIND_RESOURCE_LANGUAGE_EXACT)
   //
   if(!LdrpGetFileVersion(Module, UILangId, &version, &Resource, &ResourceSize))
   {
       if(!LdrpGetFileVersion(Module, MUI_NEUTRAL_LANGID, &version, &Resource, &ResourceSize)) 
       {
            return FALSE;
       }
    }    

    ResourceSize -= DWORD_ALIGNMENT(sizeof(*Resource));

    //
    // Get the beginning address of the children of the version information.
    //
    pVerBlock = (VERBLOCK*)(Resource + 1);

    while ((LONG)ResourceSize > 0)
    {
       if (wcscmp(pVerBlock->szKey, L"VarFileInfo") == 0)
        {
            //
            // Find VarFileInfo block. Search the ResourceChecksum block.
            //
            VarFileInfoSize = pVerBlock->wTotalLen;
            BlockLen =DWORD_ALIGNMENT(sizeof(*pVerBlock) -1 + sizeof(L"VarFileInfo"));
            VarFileInfoSize -= BlockLen;
            pVerBlock = (VERBLOCK*)((unsigned char*)pVerBlock + BlockLen);
            while (VarFileInfoSize > 0)
            {
               if (wcscmp(pVerBlock->szKey, L"ResourceChecksum") == 0)
                {
                    *ppMD5Checksum = (unsigned char*)DWORD_ALIGNMENT((UINT_PTR)(pVerBlock->szKey) + sizeof(L"ResourceChecksum"));
                    return (TRUE);
                }
                BlockLen = DWORD_ALIGNMENT(pVerBlock->wTotalLen);
                pVerBlock = (VERBLOCK*)((unsigned char*)pVerBlock + BlockLen);
                VarFileInfoSize -= BlockLen;
            }
            return (FALSE);
        }
        BlockLen = DWORD_ALIGNMENT(pVerBlock->wTotalLen);
        pVerBlock = (VERBLOCK*)((unsigned char*)pVerBlock + BlockLen);
        ResourceSize -= BlockLen;
    }
    return (FALSE);
}

BOOLEAN
LdrpCalcResourceChecksum(
    IN PVOID Module,
    IN PVOID AlternateModule,
    OUT unsigned char* MD5Checksum
    )
/*++
Rountine Description:
    Enumerate resources in the specified module, and generate a MD5 checksum.

    Calculate the checksum only on the based of resource types contained AlternateModule so that
    checksum won't change if unlocalized resource types are changed.
    
--*/
{
    // The top resource directory.
    PIMAGE_RESOURCE_DIRECTORY TopDirectory;
    PIMAGE_RESOURCE_DIRECTORY AltTopDirectory;

    // The resource type directory.
    PIMAGE_RESOURCE_DIRECTORY TypeDirectory;
    
    // The resource name directory.
    PIMAGE_RESOURCE_DIRECTORY NameDirectory;
    // The resource language directory.
    PIMAGE_RESOURCE_DIRECTORY LangDirectory;

    PIMAGE_RESOURCE_DIRECTORY_ENTRY TypeDirectoryEntry;
    PIMAGE_RESOURCE_DIRECTORY_ENTRY NameDirectoryEntry;
    PIMAGE_RESOURCE_DIRECTORY_ENTRY LangDirectoryEntry;
    PIMAGE_RESOURCE_DIRECTORY_ENTRY AltTypeDirectoryEntry;

    PIMAGE_RESOURCE_DATA_ENTRY ResourceDataEntry;

    ULONG Size;
    ULONG NumTypeDirectoryEntries;
    ULONG NumNameDirectoryEntries;
    ULONG NumLangDirectoryEntries;
    ULONG AltNumTypeDirectoryEntries;

    PVOID ResourceData;
    ULONG ResourceSize;

    ULONG i, j, k, altI;

    PIMAGE_RESOURCE_DIR_STRING_U ResourceString_U;
    WCHAR   	 ResourceStringName[260] ;
    BOOLEAN    fIsTypeFound;
    LANGID ChecksumLangID;
    ULONGLONG Version;

    try
    {

	MD5_CTX ChecksumContext;
	MD5Init(&ChecksumContext);

        //
        // we specify the langauge ID for checksum calculation.
        // First, we search with InstallLangID, If it succeed, InsallID will be used, if not, English used.
        // InatallLangID is already set in LdrAlternateResourcesEnabled and LdrpVerifyAlternateResourceModule.
        //
        ChecksumLangID = MUI_NEUTRAL_LANGID;

        if (InstallLangId != MUI_NEUTRAL_LANGID)
        {
            if (LdrpGetFileVersion(Module, InstallLangId, &Version, NULL, NULL))
            {
                ChecksumLangID = InstallLangId;
            }
        }
        
        //
        //We first get the Resource Type entry point for AlternateModule, which will be compared Module's resource types.
        // 

        AltTopDirectory = (PIMAGE_RESOURCE_DIRECTORY)
            RtlImageDirectoryEntryToData( AlternateModule,
                                          TRUE,
                                          IMAGE_DIRECTORY_ENTRY_RESOURCE,
                                          &Size
                                        );
        if (!AltTopDirectory)
        {
            return (FALSE);
        }

	 AltTypeDirectoryEntry =  (PIMAGE_RESOURCE_DIRECTORY_ENTRY)(AltTopDirectory+1);
        AltNumTypeDirectoryEntries = AltTopDirectory->NumberOfNamedEntries +
                                       AltTopDirectory->NumberOfIdEntries;
	
        //
        // TopDirectory is our reference point to directory offsets.
        //
        TopDirectory = (PIMAGE_RESOURCE_DIRECTORY)
            RtlImageDirectoryEntryToData( Module,
                                          TRUE,
                                          IMAGE_DIRECTORY_ENTRY_RESOURCE,
                                          &Size
                                        );
        if (!TopDirectory)
        {
            return (FALSE);
        }

        //
        // Point to the children of the TopResourceDirecotry.
        // This is the beginning of the type resource directory.
        //
        TypeDirectoryEntry = (PIMAGE_RESOURCE_DIRECTORY_ENTRY)(TopDirectory+1);

        //
        // Get the total number of the types (named resource types + ID resource types)
        //
        NumTypeDirectoryEntries = TopDirectory->NumberOfNamedEntries +
                                       TopDirectory->NumberOfIdEntries;
        for (i=0; i<NumTypeDirectoryEntries; i++, TypeDirectoryEntry++)
        {
            if (!TypeDirectoryEntry->NameIsString)
            {
                // If the directory type is an ID, check if this is a version info.
                if (TypeDirectoryEntry->Id == RT_VERSION)
                {
                    //
                    // If this is a version info, just skip it.
                    // When calculation checksum for resources, version info should not be
                    // included, since they will always be updated when a new version
                    // of the file is created.
                    //
                    continue;
                }
             }
	      else	
	      {
	    		// 
	    		// when name is string, we need to create new string terminated by zero so we can compare this string 
	    		// with AlternateMoudule's resource types by using LdrpCompareResourceNames_U. 
	    		// the ResourceString_U->Length is not terminated by zero so we use local WCHAR array.
	    		//
	    		ResourceString_U = (PIMAGE_RESOURCE_DIR_STRING_U)
           				 ((PCHAR)TopDirectory + TypeDirectoryEntry->NameOffset);
			if (ResourceString_U->Length < sizeof(ResourceStringName)/sizeof(ResourceStringName[0]) )
			{
				memcpy(ResourceStringName, ResourceString_U->NameString, ResourceString_U->Length* sizeof(WCHAR) );
				ResourceStringName[ResourceString_U->Length] = UNICODE_NULL;
			}
			else
			{    //
				//resource string lengh is over maximum resource string length of checksum calculation. 
				//the lenght is set by checksum creating tools, not a sdk doc.
				//
				continue;
			}
	    	  }
	    
	      for (altI=0; altI <AltNumTypeDirectoryEntries; altI++, AltTypeDirectoryEntry++)
	      {
	    		if(TypeDirectoryEntry->NameIsString)
    			{
				fIsTypeFound = LdrpCompareResourceNames_U((ULONG_PTR)ResourceStringName, AltTopDirectory,
				 				AltTypeDirectoryEntry) == 0;
    			}
	    		else
	    		{
				fIsTypeFound = LdrpCompareResourceNames_U(TypeDirectoryEntry->Id, AltTopDirectory,
				 					AltTypeDirectoryEntry) == 0;

	    		}

			if(fIsTypeFound)
			{
				// resource type in Module is in the AlternateModule.
				break;
			}
			

	      }
	    // AltTypeDirectoryEntry -= altI; // this is same with below, but use below for readibility.
	    AltTypeDirectoryEntry =  (PIMAGE_RESOURCE_DIRECTORY_ENTRY)(AltTopDirectory+1);
	    	    
	    // resource type in Module is not in the AlternateModule.
	    if (altI >= AltNumTypeDirectoryEntries)
	    {
			continue;
	    }
	    

            NameDirectory = (PIMAGE_RESOURCE_DIRECTORY)
                ((PCHAR)TopDirectory + TypeDirectoryEntry->OffsetToDirectory);

            //
            // Point to the children of this TypeResourceDirecotry.
            // This will be the beginning of the name resource directory.
            //
            NameDirectoryEntry = (PIMAGE_RESOURCE_DIRECTORY_ENTRY)(NameDirectory+1);

            //
            // Get the total number of the names for the specified type (named resource + ID resource)
            //
            NumNameDirectoryEntries = NameDirectory->NumberOfNamedEntries +
                                           NameDirectory->NumberOfIdEntries;
            for (j=0; j<NumNameDirectoryEntries; j++, NameDirectoryEntry++ )
            {
                LangDirectory = (PIMAGE_RESOURCE_DIRECTORY)
                    ((PCHAR)TopDirectory + NameDirectoryEntry->OffsetToDirectory);

                LangDirectoryEntry = (PIMAGE_RESOURCE_DIRECTORY_ENTRY)(LangDirectory+1);
                NumLangDirectoryEntries = LangDirectory->NumberOfNamedEntries +
                                               LangDirectory->NumberOfIdEntries;
                for (k=0; k<NumLangDirectoryEntries; k++, LangDirectoryEntry++)
                {
                    NTSTATUS Status;

                    if (LangDirectoryEntry->Id != ChecksumLangID)
                    { 
                    //
                    // we calculate the resource checksum (1) All Resource && (2) English  &&(3) Except Version.
                    //
                        continue;
                     }
                   

                    ResourceDataEntry = (PIMAGE_RESOURCE_DATA_ENTRY)
                            ((PCHAR)TopDirectory + LangDirectoryEntry->OffsetToData);

                    Status = LdrpAccessResourceDataNoMultipleLanguage(
                                            Module,
                                            (const PIMAGE_RESOURCE_DATA_ENTRY)ResourceDataEntry,
                                            &ResourceData,
                                            &ResourceSize
                                            );

                    if (!NT_SUCCESS(Status))
                    {
                        return (FALSE);
                    }
                    MD5Update(&ChecksumContext, (unsigned char*)ResourceData, ResourceSize);
                }
            }
        }

        MD5Final(&ChecksumContext);
        memcpy(MD5Checksum, ChecksumContext.digest, RESOURCE_CHECKSUM_SIZE);
    } except (EXCEPTION_EXECUTE_HANDLER)
    {
        return (FALSE);
    }
    return (TRUE);
}

BOOLEAN
LdrpGetRegValueKey(
    IN HANDLE Handle,
    IN LPWSTR KeyValueName,
    IN ULONG  KeyValueType,
    OUT PVOID Buffer,
    IN ULONG  BufferSize)
/*++

Routine Description:

    This function returns the the registry key value for MUI versioning.

Arguments:

    Handle - Supplies a handle to the registry which contains MUI versioning
        information.

    KeyValueName - the key name. The values are used to retreive original versiong,
        working version and MUI version.

    KeyValueType - the type of the key value.

    Buffer - pointer to a variable that will receive the retrieved information.

    BufferSize - The size of the buffer.

Return Value:

    False if the query of the registry fails.

--*/

{
    NTSTATUS Status;
    UNICODE_STRING KeyValueString;

    CHAR KeyValueBuffer[sizeof(KEY_VALUE_PARTIAL_INFORMATION) + 128 * sizeof(WCHAR)];
    PKEY_VALUE_PARTIAL_INFORMATION KeyValueInformation;
    ULONG ResultLength;

    RtlInitUnicodeString(&KeyValueString, KeyValueName);
    KeyValueInformation = (PKEY_VALUE_PARTIAL_INFORMATION)KeyValueBuffer;
    Status = NtQueryValueKey( Handle,
                              &KeyValueString,
                              KeyValuePartialInformation,
                              KeyValueInformation,
                              sizeof( KeyValueBuffer ),
                              &ResultLength
                            );

    if (!NT_SUCCESS(Status) || KeyValueInformation->Type != KeyValueType)
    {
        return (FALSE);
    }

    memcpy(Buffer, KeyValueInformation->Data, BufferSize);
    return (TRUE);
}

NTSTATUS
LdrpCreateKey(
    IN PUNICODE_STRING KeyName,
    IN HANDLE  ParentHandle,
    OUT PHANDLE ChildHandle
    )
/*++

Routine Description:

    Creates a registry key for writting.
    This is a thin wrapper over NtCreateKey().

Arguments:

    KeyName        - Name of the key to create
    ParentHandle    - Handle of parent key
    ChildHandle     - Pointer to where the handle is returned

Return Value:

    Status of create/open

--*/
{
    NTSTATUS            status;
    OBJECT_ATTRIBUTES   objectAttributes;

    //
    // Initialize the OBJECT Attributes to a known value
    //
    InitializeObjectAttributes(
        &objectAttributes,
        KeyName,
        OBJ_CASE_INSENSITIVE,
        ParentHandle,
        NULL
        );

    //
    // Create the key here
    //
    *ChildHandle = 0;
    status = NtCreateKey(
        ChildHandle,
        KEY_READ | KEY_WRITE,
        &objectAttributes,
        0,
        NULL,
        REG_OPTION_NON_VOLATILE,
        NULL
        );

    return (status);
}


NTSTATUS
LdrpOpenKey(
    IN PUNICODE_STRING KeyName,
    IN HANDLE  ParentHandle,
    OUT PHANDLE ChildHandle
    )
/*++

Routine Description:

    Open a registry key. This is a thin wrapper of NtOpenKey().

Arguments:

    KeyName        - Name of the key to create
    ParentHandle    - Handle of parent key
    ChildHandle     - Pointer to where the handle is returned

Return Value:

    Status of open registry.

--*/
{
    NTSTATUS            status;
    OBJECT_ATTRIBUTES   objectAttributes;

    //
    // Initialize the OBJECT Attributes to a known value
    //
    InitializeObjectAttributes(
        &objectAttributes,
        KeyName,
        OBJ_CASE_INSENSITIVE,
        ParentHandle,
        NULL
        );

    //
    // Create the key here
    //
    *ChildHandle = 0;
    status = NtOpenKey(ChildHandle, KEY_ALL_ACCESS, &objectAttributes);

    return (status);
}

BOOLEAN
LdrpOpenFileVersionKey(
    IN LPWSTR LangID,
    IN LPWSTR BaseDllName,
    IN ULONGLONG AltModuleVersion,
    IN LPWSTR AltModuleVersionStr,
    OUT PHANDLE pHandle)
/*++
Routine Description:

    Open the registry key which contains the versioning information for the specified alternate resource module.

Arguments:

    LangID          - The UI langauge of the resource.
    BaseDllName     - The name of the base DLL.
    AltModulePath   - The full path of the alternate resource module.
    pHandle          - The registry key which stores the version information for this alternate resource module

Return Value:

    Return TRUE if succeeds in opening/creating the key. Otherwise return FALSE.

--*/
{
    BOOLEAN Result = FALSE;
    HANDLE NlsHandle = NULL, MuiHandle = NULL, VersionHandle = NULL, LangHandle = NULL, DllKeyHandle = NULL;
    UNICODE_STRING BufferString;
    NTSTATUS Status;
    PKEY_BASIC_INFORMATION KeyInfo;
    ULONG ResultLength, Index;

    CHAR ValueBuffer[sizeof(KEY_BASIC_INFORMATION) + 32];
    WCHAR buffer[32];   // Temp string buffer.

    ULONGLONG CachedAlternateVersion;

    CHAR KeyFullInfoBuffer[sizeof(KEY_VALUE_PARTIAL_INFORMATION ) + DOS_MAX_PATH_LENGTH * sizeof(WCHAR)];
    PKEY_VALUE_PARTIAL_INFORMATION KeyFullInfo = (PKEY_VALUE_PARTIAL_INFORMATION )KeyFullInfoBuffer;

    ULONG ChecksumDisabled;
    HANDLE UserKeyHandle;              // HKEY_CURRENT_USER equivalent
    ULONG rc;

    *pHandle = NULL;

    rc = RtlOpenCurrentUser(MAXIMUM_ALLOWED, &UserKeyHandle);
    if (!NT_SUCCESS(rc))
    {
        return (FALSE);
    }


    // Open registry REG_MUI_PATH
    //
    RtlInitUnicodeString(&BufferString, REG_MUI_PATH);
    if (!NT_SUCCESS(LdrpCreateKey(&BufferString, UserKeyHandle, &NlsHandle)))
    {
        goto Exit;
    }

    //
    // Open/Create registry in REG_MUI_PATH\MUILanguages
    //
    RtlInitUnicodeString(&BufferString, MUI_MUILANGUAGES_KEY_NAME);
    if (!NT_SUCCESS(LdrpCreateKey(&BufferString, NlsHandle, &MuiHandle)))
    {
        goto Exit;
    }

    //
    // Open/Create REG_MUI_PATH\MUILanguages\FileVersions
    //
    RtlInitUnicodeString(&BufferString, MUI_FILE_VERSION_KEY_NAME);
    if (!NT_SUCCESS(LdrpCreateKey(&BufferString, MuiHandle, &VersionHandle)))
    {
        goto Exit;
    }

    if (LdrpGetRegValueKey(VersionHandle, MUI_RC_CHECKSUM_DISABLE_KEY, REG_DWORD, &ChecksumDisabled, sizeof(ChecksumDisabled)) &&
        ChecksumDisabled == 1)
    {
        goto Exit;
    }
    //
    // Open/Create "\\Registry\\Machine\\System\\CurrentControlSet\\Control\\Nls\\MUILanguages\\FileVersions\\<LangID>"
    //
    RtlInitUnicodeString(&BufferString, LangID);
    if (!NT_SUCCESS(LdrpCreateKey(&BufferString, VersionHandle, &LangHandle)))
    {
        goto Exit;
    }

    //
    // Open/Create "\\Registry\\Machine\\System\\CurrentControlSet\\Control\\Nls\\MUILanguages\\FileVersions\\<LandID>\\<Name of DLL>"
    //
    RtlInitUnicodeString(&BufferString, BaseDllName);
    if (!NT_SUCCESS(LdrpCreateKey(&BufferString, LangHandle, &DllKeyHandle)))
    {
        goto Exit;
    }

    if (!LdrpGetRegValueKey(DllKeyHandle, MUI_ALTERNATE_VERSION_KEY, REG_QWORD, &CachedAlternateVersion, sizeof(CachedAlternateVersion)))
    {
        RtlInitUnicodeString(&BufferString, MUI_ALTERNATE_VERSION_KEY);
        Result = NT_SUCCESS(NtSetValueKey(DllKeyHandle, &BufferString, 0, REG_QWORD, &AltModuleVersion, sizeof(AltModuleVersion)));
        if (Result)
        {
            *pHandle = DllKeyHandle;
        }
        goto Exit;
    }

    if (CachedAlternateVersion == AltModuleVersion)
    {
        *pHandle = DllKeyHandle;
        Result = TRUE;
    } else
    {
        //
        // Open/Create "\Registry\Machine\System\CurrentControlSet\Control\Nls\MUILanguages\FileVersions
        //      \<LandID>\<Name of DLL>\<AltVersionStr>"
        //
        RtlInitUnicodeString(&BufferString, AltModuleVersionStr);
        Result = NT_SUCCESS(LdrpCreateKey(&BufferString, DllKeyHandle, pHandle));
    }
Exit:
    if (UserKeyHandle)  {NtClose(UserKeyHandle);}
    if (NlsHandle)      {NtClose(NlsHandle);}
    if (MuiHandle)      {NtClose(MuiHandle);}
    if (VersionHandle)  {NtClose(VersionHandle);}
    if (LangHandle)     {NtClose(LangHandle);}
    // If DllKeyHandle is the handle that we are going to return,
    // we can not close it.
    if (DllKeyHandle && *pHandle != DllKeyHandle)
    {
        NtClose(DllKeyHandle);
    }
    return (Result);
}

VOID
LdrpConvertVersionString(
    IN ULONGLONG ModuleVersion,
    OUT LPWSTR ModuleVersionStr
    )
/*++

Routine Description:
    Convert a 64-bit version information into a Unicode string.

Arguments:

    ModuleVersion - The 64-b8t version information.

    ModuleVersionStr - The converted string.

Return Value:

    None.
--*/
{
    LPWSTR StringStart = ModuleVersionStr;
    WCHAR digit;
    // Put the null-terminated char at the end of the converted string.
    ModuleVersionStr[16] = L'\0';
    ModuleVersionStr += 15;
    while (ModuleVersionStr >= StringStart)
    {
        digit = (WCHAR)(ModuleVersion & (ULONGLONG)0xf);
        *ModuleVersionStr-- = (digit < 10 ? digit + '0' : (digit - 10) + 'a');
        ModuleVersion >>= 4;
    }
}

BOOLEAN
LdrpCompareResourceChecksum(
    IN LPWSTR LangID,
    IN PVOID Module,
    IN ULONGLONG ModuleVersion,
    IN PVOID AlternateModule,
    IN ULONGLONG AltModuleVersion,
    IN LPWSTR BaseDllName
    )
/*++

Routine Description:

    In the case that the version for the original module is different from that
    of the alternate module, check if the alternate module can still be used
    for the original version.

    First, the function will look at the registry to see if there is information
    cached for the module.

    In the case that the information is not cached for this module,
    this function will retrieve the MD5 resource checksum from the alternate
    resource module.  And then check if the MD5 resource checksum is embeded
    in the original module.  If MD5 resource checksum is not in the original
    moduel, it will enumerate all resources in the module to calculate the
    MD5 checksum.

Arguments:

    LangID - Supplies a language of the resource to be loaded.

    Module - The original module.

    ModuleVersion - The version for the original version.

    AlternateModule - The alternate module.

    AltModuleVersion - The version for the alternate module.

    BaseDllName - The name of the DLL.

Return Value:

    Ture if the alternate module can be used.

    Otherwise, return false.

--*/
{
    // Flag to indicate if the alternate resource can be used for this module.
    ULONG UseAlternateResource = 0;

    unsigned char* ModuleChecksum;                      // The 128-bit MD5 resource checksum for the module.
    unsigned char  CalculatedModuleChecksum[RESOURCE_CHECKSUM_SIZE];        // The calculated 128-bit MD5 resource checksum for the module.
    unsigned char* AlternateModuleChecksum;             // The 128-bit MD5 resource checksum embeded in the alternate module.

    WCHAR ModuleVersionStr[17];                         // The string for the 16 heximal digit version.
    WCHAR AltModuleVersionStr[17];

    HANDLE Handle = NULL;                                      // The registry which caches the information for this module.
    // Flag to indicate if we have retrieved or calucated the MD5 resource checksum for the original module successfully.
    BOOLEAN FoundModuleChecksum;

    UNICODE_STRING BufferString;

    //
    // Check the cached information in the registry first.
    //
    LdrpConvertVersionString(AltModuleVersion, AltModuleVersionStr);
    //
    // Open the version information key under:
    //      HKCU\Control Panel\International\MUI\FileVersions\<LangID>\<BaseDllName>
    //
    if (LdrpOpenFileVersionKey(LangID, BaseDllName, AltModuleVersion, AltModuleVersionStr, &Handle))
    {
        LdrpConvertVersionString(ModuleVersion, ModuleVersionStr);
        //
        // Try to check if this module exists in version information.
        // If yes, see if the AlternateModule can be used.
        //

        //
        // Get the cached version information in the registry to see if the original module can re-use the alternative module.
        //
        if (LdrpGetRegValueKey(Handle, ModuleVersionStr, REG_DWORD, &UseAlternateResource, sizeof(UseAlternateResource)))
        {
            // Get the cached information.  Let's bail and return the cached result in UseAlternativeResource.
            goto exit;
        }
    }

    //
    // When we are here, we know that we either:
    //  1. Can't open the registry key which cached the information. Or
    //  2. This file has never been looked before.
    //
    // Get the resource checksum for the alternate module.
    //
    if (LdrpGetResourceChecksum(AlternateModule, &AlternateModuleChecksum))
    {
        //
        // First, check if the resource checksum is built in the module.
        //
        if (!(FoundModuleChecksum = LdrpGetResourceChecksum(Module, &ModuleChecksum))) {
            //
            // If not, calculate the resource checksum for the current module.
            //
            if (FoundModuleChecksum = LdrpCalcResourceChecksum(Module, AlternateModule, CalculatedModuleChecksum))
            {
                ModuleChecksum = CalculatedModuleChecksum;
            }
        }
        if (FoundModuleChecksum)
        {
            if (memcmp(ModuleChecksum, AlternateModuleChecksum, RESOURCE_CHECKSUM_SIZE) == 0)
            {
                //
                // If the checksums are equal, the working version is the module version.
                //
                UseAlternateResource = 1;
            }
        }
    }
    if (Handle != NULL) {
        // If we find the version registry key successfully, cache the result in the registry.
        //
        // Write the working module information into registry.
        //
        RtlInitUnicodeString(&BufferString, ModuleVersionStr);
        NtSetValueKey(Handle, &BufferString, 0, REG_DWORD, &UseAlternateResource, sizeof(UseAlternateResource));
    }
exit:
    if (Handle != NULL)
    {
        NtClose(Handle);
    }

    return ((BOOLEAN)(UseAlternateResource));
}

BOOLEAN
LdrpVerifyAlternateResourceModule(
    IN PWSTR szLangIdDir,
    IN PVOID Module,
    IN PVOID AlternateModule,
    IN LPWSTR BaseDllName,
    IN LANGID LangId
    )

/*++

Routine Description:

    This function verifies if the alternate resource module has the same
    version of the base module.

Arguments:

    Module - The handle of the base module.
    AlternateModule - The handle of the alternate resource module
    BaseDllName - The file name of base DLL.

Return Value:

    TBD.

--*/

{
    ULONGLONG ModuleVersion;
    ULONGLONG AltModuleVersion;
    NTSTATUS Status;
    int     RetryCount =0;
    LANGID  newLangID;
    LANGID  preLangID =0;

    if (!LangId) {
        GET_UI_LANGID();

        if (!UILangId){
            return FALSE;
            }
        LangId = UILangId;
        }
    // we don't have reproces with other language ID when it fail.
    if (!LdrpGetFileVersion(AlternateModule, LangId, &AltModuleVersion, NULL, NULL)){
        //
        // Some English language componet is not localized yet all, so we have to search
        // Eng case. ( JPN, GER ... case ?.)
        //
        if (LangId == MUI_NEUTRAL_LANGID ||
            !LdrpGetFileVersion(AlternateModule, MUI_NEUTRAL_LANGID, &AltModuleVersion, NULL, NULL) ){
                return FALSE;
            }
        }


    //
    // when we install localized langneutral as first one, InstalllangID isn't not of code module resource.
    // 0x409(eng) has more chance to be find because code module is Eng. anyway, GetFielVersion
    // will search with language neutral (0) if it fail.
    // 01/14/02; usiing InstalllangID instead of 0, we need to provide solution to localized application
    // mui developer;their code can be same with UI language. REVIST
    //

    while (RetryCount < 3 )  {

        switch(RetryCount) {
            case 0:
                newLangID = MUI_NEUTRAL_LANGID;
                break;

            case 1:
                if (!InstallLangId){

                Status = NtQueryInstallUILanguage( &InstallLangId);

                if (!NT_SUCCESS( Status )) {
                    //
                    //  Failed to get Intall LangID.  AltResource not enabled.
                    //
                    return FALSE;
                    }
                }
                newLangID = InstallLangId;
                break;

            case 2:
                if (MUI_NEUTRAL_LANGID != 0x409 ) {// just in case, MUI_NEUTRAL_LANGID isn't Eng.
                   newLangID = 0x409;
                    }
                break;
            }

           if ( newLangID != preLangID) {
              if (LdrpGetFileVersion(Module, newLangID, &ModuleVersion, NULL, NULL)){
                    break;
                }
            }
           preLangID = newLangID;
           RetryCount++;
        }

    if (RetryCount >= 3) {
        return FALSE;
        }

    if (ModuleVersion == AltModuleVersion){
        return TRUE;
        }
    else
    {

#ifdef USE_RC_CHECKSUM
        return (LdrpCompareResourceChecksum(szLangIdDir, Module, ModuleVersion, AlternateModule, AltModuleVersion, BaseDllName));
#else
        return FALSE;
#endif
    }
}

BOOLEAN
LdrpSetAlternateResourceModuleHandle(
    IN PVOID Module,
    IN PVOID AlternateModule,
    IN LANGID LangId
    )

/*++

Routine Description:

    This function records the handle of the base module and alternate
    resource module in an array.

Arguments:

    Module - The handle of the base module.
    AlternateModule - The handle of the alternate resource module

Return Value:

    TBD.

--*/

{
    PALT_RESOURCE_MODULE NewModules;

    if (AlternateResourceModules == NULL){
        //
        //  Allocate memory of initial size MEMBLOCKSIZE.
        //
        NewModules = (PALT_RESOURCE_MODULE)RtlAllocateHeap(
                        RtlProcessHeap(),
                        HEAP_ZERO_MEMORY,
                        RESMODSIZE * MEMBLOCKSIZE);
        if (!NewModules){
            return FALSE;
            }
        AlternateResourceModules = NewModules;
        AltResMemBlockCount = MEMBLOCKSIZE;
        }
    else
    if (AlternateResourceModuleCount >= AltResMemBlockCount ){
        //
        //  ReAllocate another chunk of memory.
        //
        NewModules = (PALT_RESOURCE_MODULE)RtlReAllocateHeap(
                        RtlProcessHeap(),
                        0,
                        AlternateResourceModules,
                        (AltResMemBlockCount + MEMBLOCKSIZE) * RESMODSIZE
                        );

        if (!NewModules){
            return FALSE;
            }
        AlternateResourceModules = NewModules;
        AltResMemBlockCount += MEMBLOCKSIZE;
        }

    AlternateResourceModules[AlternateResourceModuleCount].ModuleBase = Module;
    AlternateResourceModules[AlternateResourceModuleCount].AlternateModule = AlternateModule;
    AlternateResourceModules[AlternateResourceModuleCount].LangId = LangId? LangId : UILangId;

    AlternateResourceModuleCount++;

    return TRUE;

}

PVOID
LdrLoadAlternateResourceModule(
    IN PVOID Module,
    IN LPCWSTR PathToAlternateModule OPTIONAL
    )

/*++

Routine Description:

    This function does the acutally loading into memory of the alternate
    resource module, or loads from the table if it was loaded before.

Arguments:
    Module - The handle of the base module.
    PathToAlternateModule - Optional path from which module is being loaded.

Return Value:

    Handle to the alternate resource module.

--*/

{
#ifdef MUI_MAGIC
    return LdrpLoadAlternateResourceModule(0, Module, PathToAlternateModule);
#else
    PVOID AlternateModule, DllBase;
    PLDR_DATA_TABLE_ENTRY Entry;
    HANDLE FileHandle, MappingHandle;
    PIMAGE_NT_HEADERS NtHeaders;
    NTSTATUS Status;
    OBJECT_ATTRIBUTES ObjectAttributes;
    UNICODE_STRING AltDllName;
    PVOID FreeBuffer;
    LPWSTR BaseDllName = NULL, p;
    WCHAR DllPathName[DOS_MAX_PATH_LENGTH];
    ULONG DllPathNameLength, BaseDllNameLength, CopyCount;
    ULONG Digit;
    int i, RetryCount;
    WCHAR AltModulePath[DOS_MAX_PATH_LENGTH];
    WCHAR AltModulePathMUI[DOS_MAX_PATH_LENGTH];
    WCHAR AltModulePathFallback[DOS_MAX_PATH_LENGTH];
    IO_STATUS_BLOCK IoStatusBlock;
    RTL_RELATIVE_NAME_U RelativeName;
    SIZE_T ViewSize;
    LARGE_INTEGER SectionOffset;
    WCHAR LangIdDir[6];
    PVOID ReturnValue = NULL;

    //
    // The full path of the current MUI file that we are searching.
    //
    UNICODE_STRING CurrentAltModuleFile;
    UNICODE_STRING SystemRoot;

    //
    // The current MUI folder that we are searching.
    //
    UNICODE_STRING CurrentAltModulePath;
    WCHAR CurrentAltModulePathBuffer[DOS_MAX_PATH_LENGTH];

    //
    // The string contains the first MUI folder that we will search.
    // This is the folder which lives under the folder of the base DLL.
    // AltDllMUIPath = [the folder of the base DLL] + "\mui" + "\[UI Language]";
    //      E.g. if the base DLL is "c:\winnt\system32\ntdll.dll" and UI language is 0411,
    //      AltDllMUIPath will be "c:\winnt\system32\mui\0411\"
    //
    UNICODE_STRING AltDllMUIPath;
    WCHAR AltDllMUIPathBuffer[DOS_MAX_PATH_LENGTH];

    //
    // MUI Redir
    //
    UNICODE_STRING BaseDllNameUstr;
    UNICODE_STRING StaticStringAltModulePathRedirected;
    UNICODE_STRING DynamicStringAltModulePathRedirected;
    PUNICODE_STRING FullPathStringFoundAltModulePathRedirected = NULL;
    BOOLEAN fRedirMUI = FALSE;
    PVOID LockCookie = NULL;

    // bail out early if this isn't a MUI-enabled system
    if (!LdrAlternateResourcesEnabled()) {
        return NULL;
        }

    LdrLockLoaderLock(LDR_LOCK_LOADER_LOCK_FLAG_RAISE_ON_ERRORS, NULL, &LockCookie);
    __try {
        //
        // Look at the cache of the alternate module first.
        //
        AlternateModule = LdrpGetAlternateResourceModuleHandle(Module, 0);
        if (AlternateModule == NO_ALTERNATE_RESOURCE_MODULE) {
            //
            //  We tried to load this module before but failed. Don't try
            //  again in the future.
            //
            ReturnValue = NULL;
            __leave;
        } else if (AlternateModule > 0) {
            //
            //  We found the previously loaded match
            //
            ReturnValue = AlternateModule;
            __leave;
        }

        AlternateModule = NULL;

        if (ARGUMENT_PRESENT(PathToAlternateModule)) {
            //
            //  Caller suplied path.
            //

            p = wcsrchr(PathToAlternateModule, L'\\');

            if (p == NULL)
                goto error_exit;

            p++;

            DllPathNameLength = (ULONG)(p - PathToAlternateModule) * sizeof(WCHAR);

            RtlCopyMemory(
                DllPathName,
                PathToAlternateModule,
                DllPathNameLength);

            BaseDllName = p;
            BaseDllNameLength = wcslen(p);

        } else {
            //
            //  Try to get full dll path from Ldr data table.
            //

            Status = LdrFindEntryForAddress(Module, &Entry);
            if (!NT_SUCCESS(Status))
                goto error_exit;

            DllPathNameLength = Entry->FullDllName.Length - Entry->BaseDllName.Length;

            RtlCopyMemory(
                DllPathName,
                Entry->FullDllName.Buffer,
                DllPathNameLength);

            BaseDllName = Entry->BaseDllName.Buffer;
            BaseDllNameLength = Entry->BaseDllName.Length;
        }

        DllPathName[DllPathNameLength / sizeof(WCHAR)] = UNICODE_NULL;

        //
        // dll redirection for the dll to be loaded xiaoyuw@10/31/2000
        //
        StaticStringAltModulePathRedirected.Buffer = AltModulePath;  // reuse the array instead of define another array
        StaticStringAltModulePathRedirected.Length = 0;
        StaticStringAltModulePathRedirected.MaximumLength = sizeof(AltModulePath);

        DynamicStringAltModulePathRedirected.Buffer = NULL;
        DynamicStringAltModulePathRedirected.Length = 0;
        DynamicStringAltModulePathRedirected.MaximumLength = 0;

        BaseDllNameUstr.Buffer = AltModulePathMUI; // reuse the array instead of define another array
        BaseDllNameUstr.Length = 0;
        BaseDllNameUstr.MaximumLength = sizeof(AltModulePathMUI);

        RtlAppendUnicodeToString(&BaseDllNameUstr, BaseDllName);
        RtlAppendUnicodeToString(&BaseDllNameUstr, L".mui");

        Status = RtlDosApplyFileIsolationRedirection_Ustr(
                            RTL_DOS_APPLY_FILE_REDIRECTION_USTR_FLAG_RESPECT_DOT_LOCAL,
                            &BaseDllNameUstr, NULL,
                            &StaticStringAltModulePathRedirected,
                            &DynamicStringAltModulePathRedirected,
                            &FullPathStringFoundAltModulePathRedirected,
                            NULL,NULL, NULL);
        if (!NT_SUCCESS(Status)) // no redirection info found for this string
        {
            if (Status != STATUS_SXS_KEY_NOT_FOUND)
                goto error_exit;

            //
            //  Generate the langid directory like "0804\"
            //
            GET_UI_LANGID();
            if (!UILangId){
                goto error_exit;
                }

            CopyCount = 0;
            for (i = 12; i >= 0; i -= 4) {
                Digit = ((UILangId >> i) & 0xF);
                if (Digit >= 10) {
                    LangIdDir[CopyCount++] = (WCHAR) (Digit - 10 + L'A');
                } else {
                    LangIdDir[CopyCount++] = (WCHAR) (Digit + L'0');
                }
            }

            LangIdDir[CopyCount++] = L'\\';
            LangIdDir[CopyCount++] = UNICODE_NULL;

            //
            // Create the MUI path under the directory of the base DLL.
            //
            AltDllMUIPath.Buffer = AltDllMUIPathBuffer;
            AltDllMUIPath.Length = 0;
            AltDllMUIPath.MaximumLength = sizeof(AltDllMUIPathBuffer);

            RtlAppendUnicodeToString(&AltDllMUIPath, DllPathName);  // e.g. "c:\winnt\system32\"
            RtlAppendUnicodeToString(&AltDllMUIPath, L"mui\\");     // e.g. "c:\winnt\system32\mui\"
            RtlAppendUnicodeToString(&AltDllMUIPath, LangIdDir);    // e.g. "c:\winnt\system32\mui\0411\"

            CurrentAltModulePath.Buffer = CurrentAltModulePathBuffer;
            CurrentAltModulePath.Length = 0;
            CurrentAltModulePath.MaximumLength = sizeof(CurrentAltModulePathBuffer);
        } else {
            fRedirMUI = TRUE;

            //set CurrentAltModuleFile and CurrentAltModulePath
            CurrentAltModuleFile.Buffer = AltModulePathMUI;
            CurrentAltModuleFile.Length = 0;
            CurrentAltModuleFile.MaximumLength = sizeof(AltModulePathMUI);

            RtlCopyUnicodeString(&CurrentAltModuleFile, FullPathStringFoundAltModulePathRedirected);
        }


        //
        //  Try name with .mui extesion first.
        //
        RetryCount = 0;
        while (RetryCount < 3){
            if ( ! fRedirMUI )
            {

                //
                // Once MUI_MAGIC is enabled, we should optimize the search order for system MUI files
                //
                switch (RetryCount)
                {
                    case 0:
                        //
                        //  Generate the first path under the folder of the base DLL
                        //      (e.g. c:\winnt\system32\mui\0804\ntdll.dll.mui)
                        //
                        CurrentAltModuleFile.Buffer = AltModulePathMUI;
                        CurrentAltModuleFile.Length = 0;
                        CurrentAltModuleFile.MaximumLength = sizeof(AltModulePathMUI);

                        RtlCopyUnicodeString(&CurrentAltModuleFile, &AltDllMUIPath);    // e.g. "c:\winnt\system32\mui\0411\"
                        RtlCopyUnicodeString(&CurrentAltModulePath, &AltDllMUIPath);

                        RtlAppendUnicodeToString(&CurrentAltModuleFile, BaseDllName);   // e.g. "c:\winnt\system32\mui\0411\ntdll.dll"
                        RtlAppendUnicodeToString(&CurrentAltModuleFile, L".mui");       // e.g. "c:\winnt\system32\mui\0411\ntdll.dll.mui"
                        break;
                    case 1:
                        //
                        //  Generate the second path c:\winnt\system32\mui\0804\ntdll.dll
                        //
                        CurrentAltModuleFile.Buffer = AltModulePath;
                        CurrentAltModuleFile.Length = 0;
                        CurrentAltModuleFile.MaximumLength = sizeof(AltModulePath);

                        RtlCopyUnicodeString(&CurrentAltModuleFile, &AltDllMUIPath);    // e.g. "c:\winnt\system32\mui\0411\"
                        RtlAppendUnicodeToString(&CurrentAltModuleFile, BaseDllName);   // e.g. "c:\winnt\system32\mui\0411\ntdll.dll"
                        break;
                    case 2:
                        //
                        //  Generate path c:\winnt\mui\fallback\0804\foo.exe.mui
                        //
                        CurrentAltModuleFile.Buffer = AltModulePathFallback;
                        CurrentAltModuleFile.Length = 0;
                        CurrentAltModuleFile.MaximumLength = sizeof(AltModulePathFallback);

                        RtlInitUnicodeString(&SystemRoot, USER_SHARED_DATA->NtSystemRoot);    // e.g. "c:\winnt\system32\"
                        RtlAppendUnicodeStringToString(&CurrentAltModuleFile, &SystemRoot);   // e.g. "c:\winnt\system32\"
                        RtlAppendUnicodeToString(&CurrentAltModuleFile, L"\\mui\\fallback\\");  // e.g. "c:\winnt\system32\mui\fallback\"
                        RtlAppendUnicodeToString(&CurrentAltModuleFile, LangIdDir);             // e.g. "c:\winnt\system32\mui\fallback\0411\"

                        RtlCopyUnicodeString(&CurrentAltModulePath, &CurrentAltModuleFile);

                        RtlAppendUnicodeToString(&CurrentAltModuleFile, BaseDllName);           // e.g. "c:\winnt\system32\mui\fallback\0411\ntdll.dll"
                        RtlAppendUnicodeToString(&CurrentAltModuleFile, L".mui");               // e.g. "c:\winnt\system32\mui\fallback\0411\ntdll.dll.mui"

                        break;
                }
            }

            if (!RtlDosPathNameToRelativeNtPathName_U(
                        CurrentAltModuleFile.Buffer,
                        &AltDllName,
                        NULL,
                        &RelativeName)) {
                goto error_exit;
            }

            FreeBuffer = AltDllName.Buffer;
            if (RelativeName.RelativeName.Length != 0) {
                AltDllName = RelativeName.RelativeName;
            } else {
                RelativeName.ContainingDirectory = NULL;
            }

            InitializeObjectAttributes(
                &ObjectAttributes,
                &AltDllName,
                OBJ_CASE_INSENSITIVE,
                RelativeName.ContainingDirectory,
                NULL
                );

            Status = NtCreateFile(
                    &FileHandle,
                    (ACCESS_MASK) GENERIC_READ | SYNCHRONIZE | FILE_READ_ATTRIBUTES,
                    &ObjectAttributes,
                    &IoStatusBlock,
                    NULL,
                    0L,
                    FILE_SHARE_READ | FILE_SHARE_DELETE,
                    FILE_OPEN,
                    0L,
                    NULL,
                    0L
                    );

            RtlReleaseRelativeName(&RelativeName);
            RtlFreeHeap(RtlProcessHeap(), 0, FreeBuffer);

            if (NT_SUCCESS(Status)) {
                goto CreateSection;
            }

            if (fRedirMUI) { // definitely failed
                goto error_exit;
            }

            if (Status != STATUS_OBJECT_NAME_NOT_FOUND && RetryCount == 0) {
                //
                //  Error other than the file name with .mui not found.
                //  Most likely directory is missing.  Skip file name w/o .mui
                //  and goto fallback directory.
                //
                RetryCount++;
            }

            RetryCount++;
        }

        // No alternate resource was found during the iterations.  Fail!
        goto error_exit;

    CreateSection:
        Status = NtCreateSection(
                    &MappingHandle,
                    STANDARD_RIGHTS_REQUIRED | SECTION_QUERY | SECTION_MAP_READ,
                    NULL,
                    NULL,
                    PAGE_WRITECOPY,
                    SEC_COMMIT,
                    FileHandle
                    );

        NtClose( FileHandle );

        if (!NT_SUCCESS(Status)) {
            goto error_exit;
        }

        SectionOffset.LowPart = 0;
        SectionOffset.HighPart = 0;
        ViewSize = 0;
        DllBase = NULL;

        Status = NtMapViewOfSection(
                    MappingHandle,
                    NtCurrentProcess(),
                    &DllBase,
                    0L,
                    0L,
                    &SectionOffset,
                    &ViewSize,
                    ViewShare,
                    0L,
                    PAGE_WRITECOPY
                    );

        NtClose(MappingHandle);

        if (!NT_SUCCESS(Status)){
            goto error_exit;
        }

        NtHeaders = RtlImageNtHeader(DllBase);
        if (!NtHeaders) {
            NtUnmapViewOfSection(NtCurrentProcess(), (PVOID) DllBase);
            goto error_exit;
        }

        AlternateModule = LDR_VIEW_TO_DATAFILE(DllBase);

        if(!LdrpVerifyAlternateResourceModule(LangIdDir, Module, AlternateModule, BaseDllName, 0)) {
            NtUnmapViewOfSection(NtCurrentProcess(), (PVOID) DllBase);
            goto error_exit;
            }

        LdrpSetAlternateResourceModuleHandle(Module, AlternateModule, 0);
        ReturnValue = AlternateModule;
        __leave;

error_exit:
        if (BaseDllName != NULL) {
            //
            // If we looked for a MUI file and couldn't find one keep track.  If
            // we couldn't get the base dll name (e.g. someone passing in a
            // mapped image with the low bit set but no path name), we don't want
            // to "remember" that there's no MUI.
            //

            LdrpSetAlternateResourceModuleHandle(Module, NO_ALTERNATE_RESOURCE_MODULE, 0);
        }

        ReturnValue = NULL;
    } __finally {
        Status = LdrUnlockLoaderLock(LDR_UNLOCK_LOADER_LOCK_FLAG_RAISE_ON_ERRORS, LockCookie);
    }

    return ReturnValue;
#endif
}

BOOLEAN
LdrUnloadAlternateResourceModule(
    IN PVOID Module
    )

/*++

Routine Description:

    This function unmaps an alternate resource module from the process'
    address space and updates alternate resource module table.

Arguments:

    Module - handle of the base module.

Return Value:

    TBD.

--*/

{
    ULONG ModuleIndex;
    PALT_RESOURCE_MODULE AltModule;
    NTSTATUS Status;
    PVOID LockCookie = NULL;
    BOOLEAN ReturnValue = TRUE;

    LdrLockLoaderLock(LDR_LOCK_LOADER_LOCK_FLAG_RAISE_ON_ERRORS, NULL, &LockCookie);
    __try {
        if (AlternateResourceModuleCount == 0) {
            ReturnValue = TRUE;
            __leave;
        }

        for (ModuleIndex = AlternateResourceModuleCount;
             ModuleIndex > 0;
             ModuleIndex--) {
             if (AlternateResourceModules[ModuleIndex-1].ModuleBase == Module &&
               AlternateResourceModules[ModuleIndex-1].LangId == UILangId) {
                break;
            }
        }

        if (ModuleIndex == 0) {
            ReturnValue = FALSE;
            __leave;
        }

        //
        //  Adjust to the actual index
        //
        ModuleIndex --;

        AltModule = &AlternateResourceModules[ModuleIndex];
        if (AltModule->AlternateModule != NO_ALTERNATE_RESOURCE_MODULE) {
#ifdef MUI_MAGIC

           if ( AltModule->CMFModule != NULL) {
                 NtUnmapViewOfSection(NtCurrentProcess(), AltModule->CMFModule);

                }
          else
               {   // when MUI does not use CMF file, we just unmap AltModule.
#endif
                    NtUnmapViewOfSection(
                        NtCurrentProcess(),
                        LDR_DATAFILE_TO_VIEW(AltModule->AlternateModule));
#ifdef MUI_MAGIC
                }
#endif
        }

        if (ModuleIndex != AlternateResourceModuleCount - 1) {
            //
            //  Consolidate the array.  Skip this if unloaded item
            //  is the last element.
            //
            RtlMoveMemory(
                AltModule,
                AltModule + 1,
                (AlternateResourceModuleCount - ModuleIndex - 1) * RESMODSIZE);
        }

        AlternateResourceModuleCount--;

        if (AlternateResourceModuleCount == 0){
            RtlFreeHeap(
                RtlProcessHeap(),
                0,
                AlternateResourceModules
                );
            AlternateResourceModules = NULL;
            AltResMemBlockCount = 0;
        } else {
            if (AlternateResourceModuleCount < AltResMemBlockCount - MEMBLOCKSIZE) {
                AltModule = (PALT_RESOURCE_MODULE)RtlReAllocateHeap(
                                RtlProcessHeap(),
                                0,
                                AlternateResourceModules,
                                (AltResMemBlockCount - MEMBLOCKSIZE) * RESMODSIZE);

                if (!AltModule) {
                    ReturnValue = FALSE;
                    __leave;
                }

                AlternateResourceModules = AltModule;
                AltResMemBlockCount -= MEMBLOCKSIZE;
            }
        }
    } __finally {
        LdrUnlockLoaderLock(LDR_UNLOCK_LOADER_LOCK_FLAG_RAISE_ON_ERRORS, LockCookie);
    }

    return ReturnValue;
}


BOOLEAN
LdrFlushAlternateResourceModules(
    VOID
    )

/*++

Routine Description:

    This function unmaps all the alternate resouce modules for the
    process address space. This function would be used mainly by
    CSRSS, and any sub-systems that are permanent during logon and
    logoff.


Arguments:

    None

Return Value:

    TRUE  : Successful
    FALSE : Failed

--*/
{
    ULONG ModuleIndex;
    PALT_RESOURCE_MODULE AltModule;
    NTSTATUS Status;
    PVOID LockCookie = NULL;

    //
    // Grab the loader lock
    //

    Status = LdrLockLoaderLock(LDR_LOCK_LOADER_LOCK_FLAG_RAISE_ON_ERRORS, NULL, &LockCookie);
    if (!NT_SUCCESS(Status)) {
        // This function erroneously doesn't have any way to communicate failure statuses up so
        // we're stuck with just returning false.
        return FALSE;
    }
    __try {
        if (AlternateResourceModuleCount > 0) {
            //
            // Let's unmap the alternate resource modules from the process
            // address space
            //
            for (ModuleIndex=0;
                 ModuleIndex<AlternateResourceModuleCount;
                 ModuleIndex++) {

                AltModule = &AlternateResourceModules[ModuleIndex];


                if (AltModule->AlternateModule != NO_ALTERNATE_RESOURCE_MODULE) {
#ifdef MUI_MAGIC
                    if (AltModule->CMFModule)
                        NtUnmapViewOfSection(NtCurrentProcess(), AltModule->CMFModule);
                    else
#endif
                    NtUnmapViewOfSection(NtCurrentProcess(),
                                         LDR_DATAFILE_TO_VIEW(AltModule->AlternateModule));
                }
            }


            //
            // Cleanup alternate resource modules memory
            //
            RtlFreeHeap(RtlProcessHeap(), 0, AlternateResourceModules);
            AlternateResourceModules = NULL;
            AlternateResourceModuleCount = 0;
            AltResMemBlockCount = 0;
        }

        //
        // Re-Initialize the UI language for the current process,
        //
        UILangId = 0;
    } __finally {
        LdrUnlockLoaderLock(LDR_UNLOCK_LOADER_LOCK_FLAG_RAISE_ON_ERRORS, LockCookie);
    }

    return TRUE;
}
#endif
