/*
Copyright (c) Microsoft Corporation
generate comctl tool
based on gennt32t
*/
#pragma warning( disable : 4786) //disable identifier is too long for debugging error
#pragma warning( disable : 4503) //disable decorated name is too long
#include <nt.h>
#include <ntrtl.h>
#include <nturtl.h>
#include <windows.h>
#include <imagehlp.h>
#include <ctype.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>

#include <iostream>
#include <fstream>
#include <iomanip>
#include <string>
#include <sstream>
#include <set>
#include <map>

extern "C" {

#include "gen.h"

#if !defined(NUMBER_OF)
#define NUMBER_OF(x) (sizeof(x)/sizeof((x)[0]))
#endif

// string to put in front of all error messages so that BUILD can find them.
const char *ErrMsgPrefix = "NMAKE :  U8603: 'GENCOMCTLT' ";

void
HandlePreprocessorDirective(
   char *p
   )
{
   ExitErrMsg(FALSE, "Preprocessor directives not allowed by gencomctlt.\n");
}

}

using namespace std;
typedef string String;

PRBTREE pFunctions = NULL;
PRBTREE pStructures = NULL;
PRBTREE pTypedefs = NULL;

void ExtractCVMHeader(PCVMHEAPHEADER pHeader) {
   pFunctions = &pHeader->FuncsList;
   pTypedefs =  &pHeader->TypeDefsList;
   pStructures =&pHeader->StructsList;
}

// globals so debugging works
PKNOWNTYPES pFunction; 
PFUNCINFO   pfuncinfo;

void DumpFunctionDeclarationsHeader(void)
{
    //PKNOWNTYPES pFunction; 
    //PFUNCINFO   pfuncinfo;

    cout << "///////////////////////////////////////////\n";
    cout << "// This file is autogenerated by gencomctlt. \n";
    cout << "// Do not edit                             \n";
    cout << "///////////////////////////////////////////\n";
    cout << '\n' << '\n';

    cout << "#include \"windows.h\"\n";
    cout << "#include \"commctrl.h\"\n\n";

    cout << "///////////////////////////////////////////\n";
    cout << "//  Functions                            //\n";
    cout << "///////////////////////////////////////////\n";
    for (
        pFunction = pFunctions->pLastNodeInserted;
        pFunction != NULL
            && pFunction->TypeName != NULL
            && strcmp(pFunction->TypeName, "MarkerFunction_8afccfaa_27e7_45d5_8ff7_7ac0b970789d") != 0 ;
        pFunction = pFunction->Next)
    {
    /*
    for now, just like print out commctrl as a demo/test of understanding the tool
    tomorrow, print out what we actually need
    */
        cout << pFunction->FuncRet << ' ';
        cout << pFunction->FuncMod << ' '; // __stdcall
        cout << pFunction->TypeName << "(\n"; // function name
        pfuncinfo = pFunction->pfuncinfo;
        if (pfuncinfo == NULL || pfuncinfo->sType == NULL || pfuncinfo->sName == NULL)
        {
            cout << "void";
        }
        else
        {
            for ( ; pfuncinfo != NULL ; pfuncinfo = pfuncinfo->pfuncinfoNext )
            {
                cout << ' ' << pfuncinfo->sType << ' ' << pfuncinfo->sName << ",\n";
            }
        }
        cout << ")\n";
    }
    cout << '\n' << '\n';
}

int __cdecl main(int argc, char*argv[])
{
    ExtractCVMHeader(MapPpmFile(argv[1], TRUE));
    DumpFunctionDeclarationsHeader();
    return 0;
}
