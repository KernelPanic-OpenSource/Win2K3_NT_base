
        title "Amd64 startup"

;++
;
; Copyright (c) 2001 Microsoft Corporation
;
; Module Name:
;
;    xmstub.asm
;
; Abstract:
;
;    This module implements the code that starts secondary processors.  This
;    module is unique in that it is assembled by the i386 32-bit assembler,
;    because the Amd64 assembler does not assemble 16- or 32-bit x86 code.
;
;    The .obj file that is the result of assembling this module is fed
;    through a tool, DMPOBJ.EXE, that stores the contents of the relevant
;    section and generates a c file (startup.c) that can be included in the
;    64-bit compilation process.
;
; Author:
;
;    Forrest Foltz (forrestf) March 6, 2001
;
; Environment:
;
;    Kernel mode only.
;
; Revision History:
;
;--

.586p

include ksamd64.inc

RMSTUB SEGMENT DWORD PUBLIC USE16 'CODE'

;++
;
; VOID
; StartPx_RMStub
;
;   When a new processor is started, it starts in real mode and is sent to a
;   copy of this function which resides in low (<1MB) memory.
;
;   When this function is complete, it jumps to StartPx_PMStub.
;
;   At this point, cs will contain (start block physical address) / 16,
;   and ip == 0.
;
;    
;
; Arguments:
;   None
;
; Return Value:
;   Does not return, jumps to StartPx_PMStub
;--

StartPx_RMStub:

        jmp     spr10                   ; skip the processor start block

        db (ProcessorStartBlockLength - ($ - StartPx_RMStub)) dup (0)

spr10:  cli
        sub     eax, eax
        mov     ax, cs
        mov     ds, ax

        ;
        ; Load edi with the linear address of the processor start block.
        ;

        shl     eax, 4
        mov     edi, eax

        ;
        ; Load the 32-bit GDT.
        ; 

        db      066h
        lgdt    fword ptr ds:[PsbGdt32]

        ;
        ; Enter protected mode.  Note paging is still off.
        ;

        mov     eax, cr0
        or      eax, CR0_PE OR CR0_ET
        mov     cr0, eax

        ;
        ; Load ds
        ;

        mov     ax, 020h
        mov     ds, ax

        ;
        ; Load CS by performing a far jump to the protected mode target
        ; address
        ;

        db      066h
        jmp     DWORD PTR ds:[edi + PsbPmTarget]

RMSTUB ENDS

;++
;
; VOID
; StartPx_PMStub
;
;   When a new processor is started, it starts in real mode and is sent to a
;   copy of this function which resides in low (<1MB) memory.
;
;   When this function is complete, it jumps to StartPx_PMStub.
;
; Arguments:
;   None
;
; Return Value:
;   Does not return, jumps to StartPx_LMStub
;--


PMSTUB SEGMENT PARA PUBLIC 'CODE'

StartPx_PMStub:

        ;
        ; 32-bit protected-mode boot code goes here.  We are still executing
        ; the low-memory, identity-mapped copy of this code.
        ;
        ; edi -> linear address of PROCESSOR_START_BLOCK
        ;

        ;
        ; Enable PAE mode (requisite for LongMode), load the tiled CR3
        ;

        mov     eax, DWORD PTR [edi] + PsbProcessorState + PsCr4
        mov     cr4, eax

        mov     eax, DWORD PTR [edi] + PsbTiledCr3
        mov     cr3, eax

        ;
        ; Set the long mode enable syscall in the EFER msr
        ;

        mov     ecx, MSR_EFER
        rdmsr
        or      eax, MSR_LME OR MSR_SCE OR MSR_NXE
        wrmsr

        ;
        ; Enable paging and activate long mode
        ;

        mov     eax, cr0
        or      eax, CR0_PG OR CR0_WP OR CR0_AM OR CR0_NE
        mov     cr0, eax

        ;
        ; Still in 32-bit legacy mode until we branch to a long mode
        ; code selector.  This will branch to HalpLMStub in amd64s.asm.
        ;

        jmp     FAR PTR [edi] + PsbLmIdentityTarget

PMSTUB ENDS

        END
