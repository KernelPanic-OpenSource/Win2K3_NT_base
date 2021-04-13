//////////////////////////////////////////////////////////////////////////////
// ppm2pps
//
// Copyright (c) 1996-1999 Microsoft Corporation
//
//     Dump contents of ppm file
//
//////////////////////////////////////////////////////////////////////////////

#include <nt.h>
#include <ntrtl.h>
#include <nturtl.h>
#include <windows.h>
#include <imagehlp.h>
#include <ctype.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "gen.h"

// string to put in front of all error messages so that BUILD can find them.
const char *ErrMsgPrefix = "NMAKE :  U8603: 'PPM2PPS' ";

//
// global variables
BOOL fDebug;            // global debug flag
BOOL fDumpCCheck;       // when set build ppswind.h for checking sizes/offsets
HANDLE hFile;
HANDLE hMapFile = NULL;

PRBTREE pFunctions = NULL;
PRBTREE pStructures = NULL;
PRBTREE pTypedefs = NULL;
PKNOWNTYPES NIL = NULL;

// function prototypes
void __cdecl ErrMsg(char *pch, ...);
void __cdecl ExitErrMsg(BOOL bSysError, char *pch, ...);
BOOL DumpTypedefs(FILE *filePpsfile,            // file to write output
                   BOOL fDumpNamedOnly,         // when set don't do unnamed 
                   PRBTREE pHead);              // known types function list
BOOL DumpStructures(FILE *filePpsfile,          // file to write output
                   BOOL fDumpNamedOnly,         // when set don't do unnamed 
                   PRBTREE pHead);              // known types function list
BOOL DumpFunctions(FILE *filePpsfile,           // file to write output
                   BOOL fDumpNamedOnly,         // when set don't do unnamed
                   PRBTREE pHead);              // known types function list
void Usage(char *s);
BOOL ParseArguments(int argc, char *argv[], char *sPpmfile, char *sPpsfile,
                    BOOL *pfDumpNamedOnly, BOOL *pfDebug, 
                    BOOL *pfDumpCCheck);
BOOL DumpCCheckHeader(PRBTREE pTypedefs,   // typedef lsit
                      PRBTREE pStructs);   // structs sit

                        
int __cdecl main(int argc, char *argv[])
{
    void *pvPpmData = NULL;
    BOOL fDumpNamedOnly;
    char sPpmfile[MAX_PATH];
    char sPpsfile[MAX_PATH];
    FILE *pfilePpsfile;

    try {

    if (! ParseArguments(argc, argv, sPpmfile, sPpsfile, 
                                    &fDumpNamedOnly, &fDebug, &fDumpCCheck))
    {
        Usage(argv[0]);
        return(-1);
    }
    
    if (*sPpmfile)
    {
        PCVMHEAPHEADER pHeader;

        pvPpmData = MapPpmFile(sPpmfile, TRUE);

        pHeader = (PCVMHEAPHEADER)pvPpmData;

        pFunctions = &pHeader->FuncsList;
        pTypedefs =  &pHeader->TypeDefsList;
        pStructures =&pHeader->StructsList;
        NIL         =&pHeader->NIL;
    
        pfilePpsfile = fopen(sPpsfile, "w");
        if (pfilePpsfile == 0)
        {
            ErrMsg("ERROR - Could not open output file %s\n", sPpsfile);
            CloseHandle(hFile);
            CloseHandle(hMapFile);
            return(-1);
        }
    
        if (DumpFunctions(pfilePpsfile, fDumpNamedOnly, 
                                                    pFunctions))
        {
            if (DumpStructures(pfilePpsfile, fDumpNamedOnly, 
                                                    pStructures))
            {
                DumpTypedefs(pfilePpsfile,fDumpNamedOnly, pTypedefs);
            }
        }
        fclose(pfilePpsfile);
    }
    
    if (fDumpCCheck && pTypedefs && pStructures)
    {
        DumpCCheckHeader(pTypedefs, pStructures);
    }
        
    CloseHandle(hFile);
    CloseHandle(hMapFile);

   } except(EXCEPTION_EXECUTE_HANDLER) {
       ExitErrMsg(FALSE,
                  "ExceptionCode=%x\n",
                  GetExceptionCode()
                  );
       }

    return(0);
}

void
DumpFuncinfo(FILE *pfilePpsfile, PFUNCINFO pf)
{
    while (pf) {
        int i;

        fprintf(pfilePpsfile,
                "%s %s%s %s %s %s",
                TokenString[pf->tkDirection],
                (pf->fIsPtr64) ? "__ptr64 " : "",
                TokenString[pf->tkPreMod],
                TokenString[pf->tkSUE],
                pf->sType,
                TokenString[pf->tkPrePostMod]
                );
        i = pf->IndLevel;
        while (i--) {
            fprintf(pfilePpsfile, "*");
        }
        fprintf(pfilePpsfile,
                "%s %s",
                TokenString[pf->tkPostMod],
                (pf->sName) ? pf->sName : ""
                );

        pf = pf->pfuncinfoNext;
        if (pf) {
            fprintf(pfilePpsfile, ", ");
        }
    }
}


/////////////////////////////////////////////////////////////////////////////
//
//  DumpCCheckHeader
//
//      dump header file that can be used to check sizes in ppm file against
//      those that are generated by C
//
//      returns TRUE on success
//
/////////////////////////////////////////////////////////////////////////////
BOOL DumpCCheckHeader(PRBTREE pTypedefs,   // typedef lsit
                      PRBTREE pStructs)    // structs lsit

{
    PKNOWNTYPES pknwntyp, pknwntypBasic;
    FILE *pfile;
    
    pfile = fopen("ppswind.h", "w");
    if (pfile == NULL) 
    {
        ErrMsg("Error opening ppwind.h for output\n");
        return(FALSE);
    }
    
    fprintf(pfile, "CCHECKSIZE cchecksize[] = {\n");

//
// typedefs
    pknwntyp = pTypedefs->pLastNodeInserted;

    while (pknwntyp) {
        if ((! isdigit(*pknwntyp->TypeName)) &&
            (strcmp(pknwntyp->TypeName,"...")) &&
            (strcmp(pknwntyp->TypeName,"()")) && 
            (strcmp(pknwntyp->BasicType, szFUNC)) &&
            (pknwntyp->Size > 0) &&
            (pknwntyp->dwScopeLevel == 0)) {

            pknwntypBasic = GetBasicType(pknwntyp->TypeName, 
                                     pTypedefs, pStructs);

            if (! ( (pknwntypBasic == NULL) || 
                    ( (! strcmp(pknwntypBasic->BaseName, szVOID)) &&
                      (pknwntypBasic->pmeminfo == NULL)))) {
 
                fprintf(pfile, " { %4d, sizeof(%s), \"%s\"}, \n",
                    pknwntyp->Size,
                    pknwntyp->TypeName,
                    pknwntyp->TypeName);        
            }
        }
        pknwntyp = pknwntyp->Next;
    }
    
    
//
// structs
    pknwntyp = pStructs->pLastNodeInserted;

    while (pknwntyp) {
        if ((! isdigit(*pknwntyp->TypeName) &&
            (pknwntyp->pmeminfo)))
        {
            if (!(pknwntyp->Flags & BTI_ANONYMOUS) && (pknwntyp->Size > 0) && (pknwntyp->dwScopeLevel == 0)) {
                fprintf(pfile, " { %4d, sizeof(%s %s), \"%s %s\" }, \n",
                    pknwntyp->Size,
                    pknwntyp->BaseName,
                    pknwntyp->TypeName,
                    pknwntyp->BaseName,
                    pknwntyp->TypeName);
            }
        }
        pknwntyp = pknwntyp->Next;
    }

    fprintf(pfile, " {0xffffffff, 0xffffffff,  \"\"}\n");
    fprintf(pfile,"\n};\n");
    
//
// structs fields
    fprintf(pfile, "CCHECKOFFSET ccheckoffset[] = {\n");

    pknwntyp = pStructs->pLastNodeInserted;

    while (pknwntyp) {
        if (! isdigit(*pknwntyp->TypeName)) 
        {
            if (!(pknwntyp->Flags & BTI_ANONYMOUS) && !(pknwntyp->Flags & BTI_VIRTUALONLY) && (pknwntyp->Size > 0) && (pknwntyp->dwScopeLevel == 0)) {
                PMEMBERINFO pmeminfo = pknwntyp->pmeminfo;
                while (pmeminfo != NULL) {
                    if ((pmeminfo->sName != NULL) && (*pmeminfo->sName != 0) && !(pmeminfo->bIsBitfield))
                    { 
                        fprintf(pfile, " { %4d, (long) (& (((%s %s *)0)->%s)), \"%s\", \"%s\" },\n",
                            pmeminfo->dwOffset,
                            pknwntyp->BaseName,
                            pknwntyp->TypeName,
                            pmeminfo->sName,
                            pknwntyp->TypeName,                   
                            pmeminfo->sName);
                    }
                    pmeminfo = pmeminfo->pmeminfoNext;
                }
                
            }
        }
        pknwntyp = pknwntyp->Next;
    }
    
    fprintf(pfile, " {0xffffffff, 0xffffffff, \"\", \"\"}\n");
    fprintf(pfile,"\n};\n");
    fclose(pfile);
    return(TRUE);
}

/////////////////////////////////////////////////////////////////////////////
//
//  DumpTypedefs
//
//      dump structures from ppm file to output file
//
//      returns TRUE on success
//
/////////////////////////////////////////////////////////////////////////////
BOOL DumpTypedefs(FILE *pfilePpsfile,            // file to write output
                   BOOL fDumpNamedOnly,          // when set don't do unnamed 
                   PRBTREE pHead)                // known types function list
{
    KNOWNTYPES *pknwntyp;

    pknwntyp = pHead->pLastNodeInserted;

    fprintf(pfilePpsfile,"[Typedefs]\n\n");
    while (pknwntyp) {
        fprintf(pfilePpsfile,
                   "%2.1x|%2.1x|%2.1x|%s|%s|%s|%s|%s|",
                   pknwntyp->Flags,
                   pknwntyp->IndLevel,
                   pknwntyp->Size,
                   pknwntyp->BasicType,
                   pknwntyp->BaseName ? pknwntyp->BaseName : szNULL,
                   pknwntyp->FuncRet ? pknwntyp->FuncRet : szNULL,
                   pknwntyp->FuncMod ? pknwntyp->FuncMod : szNULL,
                   pknwntyp->TypeName
                   );
        DumpFuncinfo(pfilePpsfile, pknwntyp->pfuncinfo);
        fprintf(pfilePpsfile, "|\n");

        pknwntyp = pknwntyp->Next;
    }
    return(TRUE);
}


/////////////////////////////////////////////////////////////////////////////
//
//  DumpStructures
//
//      dump structures from ppm file to output file
//
//      returns TRUE on success
//
/////////////////////////////////////////////////////////////////////////////
BOOL DumpStructures(FILE *pfilePpsfile,          // file to write output
                   BOOL fDumpNamedOnly,          // when set don't do unnamed 
                   PRBTREE pHead)                // known types function list
{
    KNOWNTYPES *pknwntyp;
    DWORD dw;
    PMEMBERINFO pmeminfo;

    pknwntyp = pHead->pLastNodeInserted;

    fprintf(pfilePpsfile,"[Structures]\n\n");
    while (pknwntyp) {
        if (! fDumpNamedOnly || ! isdigit(*pknwntyp->TypeName)) {
            fprintf(pfilePpsfile,
                   "%2.1x|%2.1x|%2.1x|%s|%s|%s|%s|%s|",
                   pknwntyp->Flags,
                   pknwntyp->IndLevel,
                   pknwntyp->Size,                
                   pknwntyp->BasicType,
                   pknwntyp->BaseName ? pknwntyp->BaseName : szNULL,
                   pknwntyp->FuncRet ? pknwntyp->FuncRet : szNULL,
                   pknwntyp->FuncMod ? pknwntyp->FuncMod : szNULL,
                   pknwntyp->TypeName);

            // dump out the structure member info, if present
            pmeminfo = pknwntyp->pmeminfo;
            while (pmeminfo) {
                int i;

                fprintf(pfilePpsfile, "%s", pmeminfo->sType);
                i = pmeminfo->IndLevel;
                if (i) {
                    fprintf(pfilePpsfile, " ");
                    while (i--) {
                        fprintf(pfilePpsfile, "*");
                    }
                }
                if (pmeminfo->sName) {
                    fprintf(pfilePpsfile, " %s", pmeminfo->sName);
                }
                fprintf(pfilePpsfile, " @ %d|", pmeminfo->dwOffset);
                pmeminfo = pmeminfo->pmeminfoNext;
            }

            // dump out the function info, if present
            DumpFuncinfo(pfilePpsfile, pknwntyp->pfuncinfo);

            fprintf(pfilePpsfile, "\n");
        }

        pknwntyp = pknwntyp->Next;
    }
    return(TRUE);
}


/////////////////////////////////////////////////////////////////////////////
//
//  DumpFunctions
//
//      dump fucntion prototypes from ppm file to output file
//
//      returns TRUE on success
//
/////////////////////////////////////////////////////////////////////////////
BOOL DumpFunctions(FILE *pfilePpsfile,            // file to write output
                   BOOL fDumpNamedOnly,           // when set don't do unnamed
                   PRBTREE pHead)                 // known types function list
{
    KNOWNTYPES *pknwntyp;
    PFUNCINFO pf;

    pknwntyp = pHead->pLastNodeInserted;

    fprintf(pfilePpsfile,"[Functions]\n\n");
    while (pknwntyp) {
        fprintf(pfilePpsfile,
                   "%s|%s|%s|%s|",
                   (pknwntyp->Flags & BTI_DLLEXPORT) ? "dllexport" : "",
                   pknwntyp->FuncRet,
                   pknwntyp->FuncMod ? pknwntyp->FuncMod : szNULL,
                   pknwntyp->TypeName
                   );
        DumpFuncinfo(pfilePpsfile, pknwntyp->pfuncinfo);
        fprintf(pfilePpsfile, "|\n");
        pknwntyp = pknwntyp->Next;
    }
    return(TRUE);
}

/////////////////////////////////////////////////////////////////////////////
//
//  Usgae
//
//      tells how to use
//
//
/////////////////////////////////////////////////////////////////////////////
void Usage(char *s)        // name of command invoked
{
    printf("Usage:\n");
    printf("    %s -d -n -x <ppm file> <pps output file>\n", s);
    printf("        -d set debug flag\n");
    printf("        -n dumps only named structs/enums/unions\n");
    printf("        -x creates ppswind.h for size/offset checking\n");
}

/////////////////////////////////////////////////////////////////////////////
//
//  ParseArgumaners
//
//      parse arguments
//
//      returnms FALSE on syntax error
//
/////////////////////////////////////////////////////////////////////////////

BOOL ParseArguments(int argc, char *argv[], char *sPpmfile, char *sPpsfile,
                    BOOL *pfDumpNamedOnly, BOOL *pfDebug, 
                    BOOL *pfDumpCCheck)
{
    int i;
    
    *sPpmfile = 0;
    *sPpsfile = 0;
    *pfDumpNamedOnly = FALSE;
    *pfDebug = FALSE;
    *pfDumpCCheck = FALSE;
    
    for (i = 1; i < argc; i++)
    {
        if (*argv[i] == '-')
        {
            switch(tolower(argv[i][1]))
            {
                case 'd':
                {
                    *pfDebug = TRUE;
                    break;
                }
                
                case 'n':
                {
                    *pfDumpNamedOnly = TRUE;
                    break;
                }
                
                case 'x':
                {
                    *pfDumpCCheck = TRUE;
                    break;
                }
                
                default:
                {
                    return(FALSE);
                }
            }
        } else {
            if (lstrlenA(argv[i]) >= MAX_PATH)
            {
                return(FALSE);
            }
            if (*sPpmfile == 0)
            {
                strcpy(sPpmfile, argv[i]);
            } else if (*sPpsfile == 0)
            {
                strcpy(sPpsfile, argv[i]);
            } else {
                return(FALSE);
            }
        }        
    }
    return( *pfDumpCCheck || ((*sPpmfile != 0) && (*sPpsfile != 0)));
}

#pragma warning(push)
#pragma warning(disable:4702)
void
HandlePreprocessorDirective(
    char *p
    )
{
    ExitErrMsg(FALSE, "Preprocessor directives not allowed by ppm2pps\n");
}
#pragma warning(pop)
