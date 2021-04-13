/*++
                                                                                
Copyright (c) 1998-1999 Microsoft Corporation

Module Name:

    gennt32t.cpp

Abstract:
    
    Generates NT32 headers for use in the NT64 build.
    
Author:

    mzoran  5-8-98

Revision History:

--*/

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

// string to put in front of all error messages so that BUILD can find them.
const char *ErrMsgPrefix = "NMAKE :  U8603: 'GENNT32T' ";

#pragma warning(push)
#pragma warning(disable:4702)
void
HandlePreprocessorDirective(
   char *p
   )
{
   ExitErrMsg(FALSE, "Preprocessor directives not allowed by gennt32t.\n");
}
#pragma warning(pop)

}

using namespace std;
typedef string String;

PRBTREE pFunctions = NULL;
PRBTREE pStructures = NULL;
PRBTREE pTypedefs = NULL;
PKNOWNTYPES NIL = NULL;

void ExtractCVMHeader(PCVMHEAPHEADER pHeader) {
   pFunctions = &pHeader->FuncsList;
   pTypedefs =  &pHeader->TypeDefsList;
   pStructures =&pHeader->StructsList;
   NIL         =&pHeader->NIL; 
}

 
void GetType(PKNOWNTYPES pTypes, char *pPrepend, ostream & oType, ostream & oName, BOOL bAddTypeName) {
  
   while(1) {
      PKNOWNTYPES pBasicType = NULL;
      assert(pTypes->TypeName != NULL);
      
      oType << "/* " << pTypes->TypeName << " */";
                          
      if(pTypes->Flags & BTI_ISARRAY)
         oName << '[' << pTypes->dwArrayElements << ']';
      if (pTypes->IndLevel > 0) {
         oType << GetHostPointerName(pTypes->Flags & BTI_PTR64);
         return;
      }
      if(!(BTI_NOTDERIVED & pTypes->Flags)) {
         
         if (strcmp(pTypes->BaseName, "enum") == 0) {
            if (bAddTypeName)
               oType << "enum " << pPrepend << pTypes->TypeName << " {} \n";
            else
               oType << "_int32 \n";
            return;
         }

         else if (strcmp(pTypes->BaseName, "union") == 0 ||
             strcmp(pTypes->BaseName, "struct") == 0) {

            oType << "\n#pragma pack(" << pTypes->dwCurrentPacking 
                  << ")\n";
            if (bAddTypeName) 
              oType << pTypes->BaseName << " " << pPrepend << pTypes->TypeName;
            else
              oType << pTypes->BaseName << " ";

            if (NULL != pTypes->pmeminfo) {
               oType << "{\n";
               PMEMBERINFO pmeminfo = pTypes->pmeminfo;
               do {
                  ostringstream oMemberType("");
                  ostringstream oMemberName("");
                  PKNOWNTYPES pMemberType = pmeminfo->pkt;
                  if(pmeminfo->sName != NULL)
                     oMemberName << pmeminfo->sName;
                  if (pmeminfo->bIsArray) 
                     oMemberName << '[' << pmeminfo->ArrayElements << ']';
                  if (pmeminfo->IndLevel > 0) {
                     oMemberType << GetHostPointerName(pmeminfo->bIsPtr64);
                  }
                  else {
                     GetType(pMemberType, pPrepend, oMemberType, oMemberName, FALSE);
                     if (pmeminfo->bIsBitfield) 
                        oMemberName << " : " << pmeminfo->BitsRequired;
                  }
                  oType << oMemberType.str() << " " << oMemberName.str() << ";\n";
                  pmeminfo = pmeminfo->pmeminfoNext;
               } while(NULL != pmeminfo);
               oType << "}\n";
            }
            
            return;
         }
         else {
            pBasicType = pTypes->pTypedefBase;
            if (NULL == pBasicType) {
               oType << GetHostPointerName(pTypes->Flags & BTI_PTR64);
               return;
            }
            pTypes = pBasicType;
         }
      }
      else {
         char Buffer[MAX_PATH];
         oType << GetHostTypeName(pTypes, Buffer);
         return;
      }      
   }
}

void DumpTypesHeader(void) {
    PKNOWNTYPES pTypes; 

    cout << "///////////////////////////////////////////\n";
    cout << "// This file is autogenerated by gennt32t. \n";
    cout << "// Do not edit                             \n";
    cout << "///////////////////////////////////////////\n";
    cout << '\n' << '\n';

    cout << "#include <guiddef.h>\n\n";

    cout << "#pragma pack(push, gennt32t)\n\n";

    cout << "///////////////////////////////////////////\n";
    cout << "//  Structures                             \n";
    cout << "///////////////////////////////////////////\n";
    for(pTypes = pStructures->pLastNodeInserted; pTypes != NULL; pTypes = pTypes->Next) {
      if (pTypes->TypeName != NULL && !(pTypes->Flags & BTI_NOTDERIVED)) {
         ostringstream oType;
         ostringstream oName;
         GetType(pTypes, "NT32", oType, oName, TRUE);
         cout << "/* " << pTypes->TypeName << " */";
         cout << oType.str() << "\n";
         cout << oName.str() << ";" << "\n";
         cout << "/* End of definition for " << pTypes->TypeName << " */\n";
         cout << '\n';
      }    }
    cout << '\n' << '\n';

    cout << "///////////////////////////////////////////\n";
    cout << "//  TypeDefs                               \n";
    cout << "///////////////////////////////////////////\n";
    for(pTypes = pTypedefs->pLastNodeInserted; pTypes != NULL; pTypes = pTypes->Next) {
      if (pTypes->TypeName != NULL && !(pTypes->Flags & BTI_NOTDERIVED)) {
         ostringstream oType;
         ostringstream oName;
         oName << "NT32" << pTypes->TypeName << " ";
         GetType(pTypes, "NT32", oType, oName, FALSE);
         cout << "/* " << pTypes->TypeName << " */" << " typedef \n";
         cout << oType.str() << "\n";
         cout << oName.str() << "\n";
         cout << ";" << "\n";
         cout << "/* End of definition for " << pTypes->TypeName << " */\n";
         cout << '\n';
      }
    }
    cout << '\n' << '\n';

    cout << "#pragma pack(pop, gennt32t)\n\n";

}

int _cdecl main(int argc, char*argv[]) {
    ExtractCVMHeader(MapPpmFile(argv[1], TRUE));
    DumpTypesHeader();
    return 0;
}
