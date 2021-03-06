/***
*stack.cpp - RTC support
*
*       Copyright (c) 1998-2001, Microsoft Corporation. All rights reserved.
*
*
*Revision History:
*       07-28-98  JWM   Module incorporated into CRTs (from KFrei)
*       05-11-99  KBF   Error if RTC support define not enabled
*       07-15-01  PML   Remove all ALPHA, MIPS, and PPC code
*
****/

#ifndef _RTC
#error  RunTime Check support not enabled!
#endif

#include "rtcpriv.h"

/* Stack Checking Calls */
void
__declspec(naked) 
_RTC_CheckEsp() 
{
    __asm 
    {
        jne esperror    ; 
        ret

    esperror:
        ; function prolog

        push ebp
        mov ebp, esp
        sub esp, __LOCAL_SIZE

        push eax        ; save the old return value
        push edx

        push ebx
        push esi
        push edi
    }

    _RTC_Failure(_ReturnAddress(), _RTC_CHKSTK);

    __asm 
    {
        ; function epilog

        pop edi
        pop esi
        pop ebx

        pop edx         ; restore the old return value
        pop eax

        mov esp, ebp
        pop ebp
        ret
    }
}

void __fastcall 
_RTC_CheckStackVars(void *frame, _RTC_framedesc *v)
{
    int i;
    for (i = 0; i < v->varCount; i++)
    {
        int *head = (int *)(((char *)frame) + v->variables[i].addr + v->variables[i].size);
        int *tail = (int *)(((char *)frame) + v->variables[i].addr - sizeof(int));
        
        if (*tail != 0xcccccccc || *head != 0xcccccccc) 
            _RTC_StackFailure(_ReturnAddress(), v->variables[i].name);
    }
}
