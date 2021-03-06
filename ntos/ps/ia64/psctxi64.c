/*++

Copyright (c) 1998  Intel Corporation
Copyright (c) 1990  Microsoft Corporation

Module Name:

    psctxi64.c

Abstract:

    This module implements function to get and set the context of a thread.

Author:

    David N. Cutler (davec) 1-Oct-1990

Revision History:

--*/
#include "psp.h"
#include <ia64.h>

#define ALIGN_NATS(Result, Source, Start, AddressOffset, Mask)    \
    if (AddressOffset == Start) {                                       \
        Result = (ULONGLONG)Source;                                     \
    } else if (AddressOffset < Start) {                                 \
        Result = (ULONGLONG)(Source << (Start - AddressOffset));        \
    } else {                                                            \
        Result = (ULONGLONG)((Source >> (AddressOffset - Start)) |      \
                             (Source << (64 + Start - AddressOffset))); \
    }                                                                   \
    Result = Result & (ULONGLONG)Mask

#define EXTRACT_NATS(Result, Source, Start, AddressOffset, Mask)        \
    Result = (ULONGLONG)(Source & (ULONGLONG)Mask);                     \
    if (AddressOffset < Start) {                                        \
        Result = Result >> (Start - AddressOffset);                     \
    } else if (AddressOffset > Start) {                                 \
        Result = ((Result << (AddressOffset - Start)) |                 \
                  (Result >> (64 + Start - AddressOffset)));            \
    }


VOID
KiGetDebugContext (
    IN PKTRAP_FRAME TrapFrame,
    IN OUT PCONTEXT ContextFrame
    );

VOID
KiSetDebugContext (
    IN OUT PKTRAP_FRAME TrapFrame,
    IN PCONTEXT ContextFrame,
    IN KPROCESSOR_MODE ProcessorMode
    );


VOID
PspGetContext (
    IN PKTRAP_FRAME                   TrapFrame,
    IN PKNONVOLATILE_CONTEXT_POINTERS ContextPointers,
    IN OUT PCONTEXT                   ContextEM
    )

/*++

Routine Description:

    This function selectively moves the contents of the specified trap frame
    and nonvolatile context to the specified context record.

Arguments:

    TrapFrame -         Supplies a pointer to a trap frame.

    ContextPointers -   Supplies the address of context pointers record.

    ContextEM -         Supplies the address of a context record.

Return Value:

    None.

    N.B. The side effect of this routine is that the dirty user stacked
         registers that were flushed into the kernel backing store are
         copied backed into the user backing store and the trap frame
         will be modified as a result of that.

--*/

{

    ULONGLONG IntNats1, IntNats2 = 0;
    USHORT R1Offset;

    if (ContextEM->ContextFlags & CONTEXT_EXCEPTION_REQUEST) {

        ContextEM->ContextFlags |= CONTEXT_EXCEPTION_REPORTING;
        ContextEM->ContextFlags &= ~(CONTEXT_EXCEPTION_ACTIVE | CONTEXT_SERVICE_ACTIVE);

        if (TRAP_FRAME_TYPE(TrapFrame) == SYSCALL_FRAME) {

            ContextEM->ContextFlags |= CONTEXT_SERVICE_ACTIVE;

        } else if (TRAP_FRAME_TYPE(TrapFrame) != INTERRUPT_FRAME) {

            ContextEM->ContextFlags |= CONTEXT_EXCEPTION_ACTIVE;
        }
    }

    if ((ContextEM->ContextFlags & CONTEXT_CONTROL) == CONTEXT_CONTROL) {

        ContextEM->IntGp = TrapFrame->IntGp;
        ContextEM->IntSp = TrapFrame->IntSp;
        ContextEM->ApUNAT = TrapFrame->ApUNAT;
        ContextEM->BrRp = TrapFrame->BrRp;

        ContextEM->StFPSR = TrapFrame->StFPSR;
        ContextEM->StIPSR = TrapFrame->StIPSR;
        ContextEM->StIIP = TrapFrame->StIIP;
        ContextEM->StIFS = TrapFrame->StIFS;

        if (TRAP_FRAME_TYPE(TrapFrame) != SYSCALL_FRAME) {

            ContextEM->ApCCV = TrapFrame->ApCCV;
            ContextEM->SegCSD = TrapFrame->SegCSD;

        }

        //
        // Get RSE control states from the trap frame.
        //

        ContextEM->RsPFS = TrapFrame->RsPFS;
        ContextEM->RsRSC = TrapFrame->RsRSC;
        ContextEM->RsRNAT = TrapFrame->RsRNAT;

        ContextEM->RsBSP = RtlpRseShrinkBySOF (TrapFrame->RsBSP, TrapFrame->StIFS);
        ContextEM->RsBSPSTORE = ContextEM->RsBSP;

        //
        // Get preserved applicaton registers
        //

        ContextEM->ApLC = *ContextPointers->ApLC;
        ContextEM->ApEC = (*ContextPointers->ApEC >> PFS_EC_SHIFT) & PFS_EC_MASK;

        //
        // Get iA status
        //

        ContextEM->StFCR = __getReg(CV_IA64_AR21);
        ContextEM->Eflag = __getReg(CV_IA64_AR24);
        ContextEM->SegSSD = __getReg(CV_IA64_AR26);
        ContextEM->Cflag = __getReg(CV_IA64_AR27);
        ContextEM->StFSR = __getReg(CV_IA64_AR28);
        ContextEM->StFIR = __getReg(CV_IA64_AR29);
        ContextEM->StFDR = __getReg(CV_IA64_AR30);
        ContextEM->ApDCR = __getReg(CV_IA64_ApDCR);

    }

    if ((ContextEM->ContextFlags & CONTEXT_INTEGER) == CONTEXT_INTEGER) {

        ContextEM->Preds = TrapFrame->Preds;
        ContextEM->IntTeb = TrapFrame->IntTeb;
        ContextEM->IntV0 = TrapFrame->IntV0;

        ASSERT((TrapFrame->EOFMarker & ~0xffI64) == KTRAP_FRAME_EOF);

        if (TRAP_FRAME_TYPE(TrapFrame) != SYSCALL_FRAME) {

            ContextEM->IntT0 = TrapFrame->IntT0;
            ContextEM->IntT1 = TrapFrame->IntT1;
            ContextEM->IntT2 = TrapFrame->IntT2;
            ContextEM->IntT3 = TrapFrame->IntT3;
            ContextEM->IntT4 = TrapFrame->IntT4;

            //
            // t5 - t22
            //

            memcpy(&ContextEM->IntT5, &TrapFrame->IntT5, 18*sizeof(ULONGLONG));


            //
            // Get branch registers
            //

            ContextEM->BrT0 = TrapFrame->BrT0;
            ContextEM->BrT1 = TrapFrame->BrT1;
        }

        ContextEM->BrS0 = *ContextPointers->BrS0;
        ContextEM->BrS1 = *ContextPointers->BrS1;
        ContextEM->BrS2 = *ContextPointers->BrS2;
        ContextEM->BrS3 = *ContextPointers->BrS3;
        ContextEM->BrS4 = *ContextPointers->BrS4;

        //
        // Get integer registers s0 - s3 from exception frame.
        //

        ContextEM->IntS0 = *ContextPointers->IntS0;
        ContextEM->IntS1 = *ContextPointers->IntS1;
        ContextEM->IntS2 = *ContextPointers->IntS2;
        ContextEM->IntS3 = *ContextPointers->IntS3;
        IntNats2 |= (((*ContextPointers->IntS0Nat >> (((ULONG_PTR)ContextPointers->IntS0 & 0x1F8) >> 3)) & 0x1) << 4);
        IntNats2 |= (((*ContextPointers->IntS1Nat >> (((ULONG_PTR)ContextPointers->IntS1 & 0x1F8) >> 3)) & 0x1) << 5);
        IntNats2 |= (((*ContextPointers->IntS2Nat >> (((ULONG_PTR)ContextPointers->IntS2 & 0x1F8) >> 3)) & 0x1) << 6);
        IntNats2 |= (((*ContextPointers->IntS3Nat >> (((ULONG_PTR)ContextPointers->IntS3 & 0x1F8) >> 3)) & 0x1) << 7);

        //
        // Get the integer nats field in the context
        // *ContextPointers->IntNats has Nats for preserved regs
        //

        R1Offset = (USHORT)((ULONG_PTR)(&TrapFrame->IntGp) >> 3) & 0x3f;
        ALIGN_NATS(IntNats1, TrapFrame->IntNats, 1, R1Offset, 0xFFFFFF0E);

        ContextEM->IntNats = IntNats1 | IntNats2;

#ifdef DEBUG
        DbgPrint("PspGetContext INTEGER: R1Offset = 0x%x, TF->IntNats = 0x%I64x, IntNats1 = 0x%I64x\n",
               R1Offset, TrapFrame->IntNats, IntNats1);
#endif

    }

    if ((ContextEM->ContextFlags & CONTEXT_LOWER_FLOATING_POINT) == CONTEXT_LOWER_FLOATING_POINT) {

        ContextEM->StFPSR = TrapFrame->StFPSR;

        //
        // Get floating registers fs0 - fs19
        //

        ContextEM->FltS0 = *ContextPointers->FltS0;
        ContextEM->FltS1 = *ContextPointers->FltS1;
        ContextEM->FltS2 = *ContextPointers->FltS2;
        ContextEM->FltS3 = *ContextPointers->FltS3;

        ContextEM->FltS4 = *ContextPointers->FltS4;
        ContextEM->FltS5 = *ContextPointers->FltS5;
        ContextEM->FltS6 = *ContextPointers->FltS6;
        ContextEM->FltS7 = *ContextPointers->FltS7;

        ContextEM->FltS8 = *ContextPointers->FltS8;
        ContextEM->FltS9 = *ContextPointers->FltS9;
        ContextEM->FltS10 = *ContextPointers->FltS10;
        ContextEM->FltS11 = *ContextPointers->FltS11;

        ContextEM->FltS12 = *ContextPointers->FltS12;
        ContextEM->FltS13 = *ContextPointers->FltS13;
        ContextEM->FltS14 = *ContextPointers->FltS14;
        ContextEM->FltS15 = *ContextPointers->FltS15;

        ContextEM->FltS16 = *ContextPointers->FltS16;
        ContextEM->FltS17 = *ContextPointers->FltS17;
        ContextEM->FltS18 = *ContextPointers->FltS18;
        ContextEM->FltS19 = *ContextPointers->FltS19;

        
        if (TRAP_FRAME_TYPE(TrapFrame) != SYSCALL_FRAME) {

            //
            // Get floating registers ft0 - ft9 from trap frame.
            //

            RtlCopyIa64FloatRegisterContext(&ContextEM->FltT0,
                                            &TrapFrame->FltT0,
                                            sizeof(FLOAT128) * (10));

        }
    }

    if ((ContextEM->ContextFlags & CONTEXT_HIGHER_FLOATING_POINT) == CONTEXT_HIGHER_FLOATING_POINT) {

        ContextEM->StFPSR = TrapFrame->StFPSR;

        //
        // Get floating regs f32 - f127 from higher floating point save area
        //

        if (TrapFrame->PreviousMode == UserMode) {
            RtlCopyIa64FloatRegisterContext(
                &ContextEM->FltF32,
                (PFLOAT128)GET_HIGH_FLOATING_POINT_REGISTER_SAVEAREA(KeGetCurrentThread()->StackBase),
                96*sizeof(FLOAT128)
                );
        }
    }

    //
    // Get h/w debug register context
    //

    if ((ContextEM->ContextFlags & CONTEXT_DEBUG) == CONTEXT_DEBUG) {
        KiGetDebugContext(TrapFrame, ContextEM);
    }

    return;
}

VOID
PspSetContext (
    IN OUT PKTRAP_FRAME               TrapFrame,
    IN PKNONVOLATILE_CONTEXT_POINTERS ContextPointers,
    IN PCONTEXT                       ContextEM,
    IN KPROCESSOR_MODE                ProcessorMode
    )

/*++

Routine Description:

    This function selectively moves the contents of the specified context
    record to the specified trap frame and nonvolatile context.

    We're expecting a plabel to have been passed in the IIP (we won't have a valid
    Global pointer) and if we have a plabel we fill in the correct Global pointer and
    IIP. Technically, the GP is part of the CONTEXT_CONTROL with the EM architecture
    so we only need to check to see if CONTEXT_CONTROL has been specified.

Arguments:

    TrapFrame -           Supplies the address of the trap frame.

    ContextPointers -   Supplies the address of context pointers record.

    ContextEM -         Supplies the address of a context record.

    ProcessorMode -     Supplies the processor mode to use when sanitizing
                        the PSR and FSR.

Return Value:

    None.

--*/

{
    USHORT R1Offset;
    ULONGLONG NewBsp;

    //
    // Indicate the trap frame has been modified by a set context.
    // This is used by the emulation code to detect that the trap frame
    // has been changed after trap has occured.
    //

    TrapFrame->EOFMarker |= MODIFIED_FRAME;

    if ((ContextEM->ContextFlags & CONTEXT_CONTROL) == CONTEXT_CONTROL) {

        TrapFrame->IntGp = ContextEM->IntGp;
        TrapFrame->IntSp = ContextEM->IntSp;
        TrapFrame->ApUNAT = ContextEM->ApUNAT;
        TrapFrame->BrRp = ContextEM->BrRp;
        TrapFrame->ApCCV = ContextEM->ApCCV;
        TrapFrame->SegCSD = ContextEM->SegCSD;

        //
        // Set preserved applicaton registers.
        //

        *ContextPointers->ApLC = ContextEM->ApLC;
        *ContextPointers->ApEC &= ~((ULONGLONG)PFS_EC_MASK << PFS_EC_SHIFT);
        *ContextPointers->ApEC |= ((ContextEM->ApEC & PFS_EC_MASK) << PFS_EC_SHIFT);

        TrapFrame->StFPSR = SANITIZE_FSR(ContextEM->StFPSR, ProcessorMode);
        TrapFrame->StIIP = ContextEM->StIIP;
        TrapFrame->StIFS = SANITIZE_IFS(ContextEM->StIFS, ProcessorMode);
        TrapFrame->StIPSR = SANITIZE_PSR(ContextEM->StIPSR, ProcessorMode);
        TrapFrame->RsPFS = SANITIZE_PFS(ContextEM->RsPFS, ProcessorMode);

        //
        // If the BSP value is not being changed then preserve the 
        // preload count.  This is only necessary if KeFlushUserRseState
        // failed for some reason.
        //

        NewBsp = RtlpRseGrowBySOF (ContextEM->RsBSP, ContextEM->StIFS);

        if (TrapFrame->RsBSP == NewBsp) {
            
            RSC  Rsc;

            Rsc.ull = ZERO_PRELOAD_SIZE(SANITIZE_RSC(ContextEM->RsRSC, ProcessorMode));
            Rsc.sb.rsc_preload = ((struct _RSC *) &(TrapFrame->RsRSC))->rsc_preload;
            TrapFrame->RsRSC = Rsc.ull;

        } else {
            TrapFrame->RsRSC = ZERO_PRELOAD_SIZE(SANITIZE_RSC(ContextEM->RsRSC, ProcessorMode));
            TrapFrame->RsBSP = NewBsp;
        }

        TrapFrame->RsBSPSTORE = TrapFrame->RsBSP;
        TrapFrame->RsRNAT = ContextEM->RsRNAT;

#ifdef DEBUG
        DbgPrint ("PspSetContext CONTROL: TrapFrame->RsRNAT = 0x%I64x\n",
                TrapFrame->RsRNAT);
#endif

        //
        // Set and sanitize iA status
        //

        __setReg(CV_IA64_AR21, SANITIZE_AR21_FCR (ContextEM->StFCR, ProcessorMode));
        __setReg(CV_IA64_AR24, SANITIZE_AR24_EFLAGS (ContextEM->Eflag, ProcessorMode));
        __setReg(CV_IA64_AR26, ContextEM->SegSSD);
        __setReg(CV_IA64_AR27, SANITIZE_AR27_CFLG (ContextEM->Cflag, ProcessorMode));

        __setReg(CV_IA64_AR28, SANITIZE_AR28_FSR (ContextEM->StFSR, ProcessorMode));
        __setReg(CV_IA64_AR29, SANITIZE_AR29_FIR (ContextEM->StFIR, ProcessorMode));
        __setReg(CV_IA64_AR30, SANITIZE_AR30_FDR (ContextEM->StFDR, ProcessorMode));

    }

    if ((ContextEM->ContextFlags & CONTEXT_INTEGER) == CONTEXT_INTEGER) {

        TrapFrame->IntT0 = ContextEM->IntT0;
        TrapFrame->IntT1 = ContextEM->IntT1;
        TrapFrame->IntT2 = ContextEM->IntT2;
        TrapFrame->IntT3 = ContextEM->IntT3;
        TrapFrame->IntT4 = ContextEM->IntT4;
        TrapFrame->IntV0 = ContextEM->IntV0;
        TrapFrame->IntTeb = ContextEM->IntTeb;
        TrapFrame->Preds = ContextEM->Preds;

        //
        //  t5 - t2
        //

        RtlCopyMemory(&TrapFrame->IntT5, &ContextEM->IntT5, 18*sizeof(ULONGLONG));

        //
        // Set the integer nats fields
        //

        R1Offset = (USHORT)((ULONG_PTR)(&TrapFrame->IntGp) >> 3) & 0x3f;

        EXTRACT_NATS(TrapFrame->IntNats, ContextEM->IntNats,
                     1, R1Offset, 0xFFFFFF0E);

        //
        // Set the preserved integer NAT fields
        //

        *ContextPointers->IntS0 = ContextEM->IntS0;
        *ContextPointers->IntS1 = ContextEM->IntS1;
        *ContextPointers->IntS2 = ContextEM->IntS2;
        *ContextPointers->IntS3 = ContextEM->IntS3;

        *ContextPointers->IntS0Nat &= ~(0x1 << (((ULONG_PTR)ContextPointers->IntS0 & 0x1F8) >> 3));
        *ContextPointers->IntS1Nat &= ~(0x1 << (((ULONG_PTR)ContextPointers->IntS1 & 0x1F8) >> 3));
        *ContextPointers->IntS2Nat &= ~(0x1 << (((ULONG_PTR)ContextPointers->IntS2 & 0x1F8) >> 3));
        *ContextPointers->IntS3Nat &= ~(0x1 << (((ULONG_PTR)ContextPointers->IntS3 & 0x1F8) >> 3));

        *ContextPointers->IntS0Nat |= (((ContextEM->IntNats >> 4) & 0x1) << (((ULONG_PTR)ContextPointers->IntS0 & 0x1F8) >> 3));
        *ContextPointers->IntS1Nat |= (((ContextEM->IntNats >> 4) & 0x1) << (((ULONG_PTR)ContextPointers->IntS1 & 0x1F8) >> 3));
        *ContextPointers->IntS2Nat |= (((ContextEM->IntNats >> 4) & 0x1) << (((ULONG_PTR)ContextPointers->IntS2 & 0x1F8) >> 3));
        *ContextPointers->IntS3Nat |= (((ContextEM->IntNats >> 4) & 0x1) << (((ULONG_PTR)ContextPointers->IntS3 & 0x1F8) >> 3));

#ifdef DEBUG
        DbgPrint("PspSetContext INTEGER: R1Offset = 0x%x, TF->IntNats = 0x%I64x, Context->IntNats = 0x%I64x\n",
               R1Offset, TrapFrame->IntNats, ContextEM->IntNats);
#endif

        *ContextPointers->BrS0 = ContextEM->BrS0;
        *ContextPointers->BrS1 = ContextEM->BrS1;
        *ContextPointers->BrS2 = ContextEM->BrS2;
        *ContextPointers->BrS3 = ContextEM->BrS3;
        *ContextPointers->BrS4 = ContextEM->BrS4;
        TrapFrame->BrT0 = ContextEM->BrT0;
        TrapFrame->BrT1 = ContextEM->BrT1;
    }

    if ((ContextEM->ContextFlags & CONTEXT_LOWER_FLOATING_POINT) == CONTEXT_LOWER_FLOATING_POINT) {

        TrapFrame->StFPSR = SANITIZE_FSR(ContextEM->StFPSR, ProcessorMode);

        //
        // Set floating registers fs0 - fs19.
        //

        *ContextPointers->FltS0 = ContextEM->FltS0;
        *ContextPointers->FltS1 = ContextEM->FltS1;
        *ContextPointers->FltS2 = ContextEM->FltS2;
        *ContextPointers->FltS3 = ContextEM->FltS3;

        *ContextPointers->FltS4 = ContextEM->FltS4;
        *ContextPointers->FltS5 = ContextEM->FltS5;
        *ContextPointers->FltS6 = ContextEM->FltS6;
        *ContextPointers->FltS7 = ContextEM->FltS7;

        *ContextPointers->FltS8 = ContextEM->FltS8;
        *ContextPointers->FltS9 = ContextEM->FltS9;
        *ContextPointers->FltS10 = ContextEM->FltS10;
        *ContextPointers->FltS11 = ContextEM->FltS11;

        *ContextPointers->FltS12 = ContextEM->FltS12;
        *ContextPointers->FltS13 = ContextEM->FltS13;
        *ContextPointers->FltS14 = ContextEM->FltS14;
        *ContextPointers->FltS15 = ContextEM->FltS15;

        *ContextPointers->FltS16 = ContextEM->FltS16;
        *ContextPointers->FltS17 = ContextEM->FltS17;
        *ContextPointers->FltS18 = ContextEM->FltS18;
        *ContextPointers->FltS19 = ContextEM->FltS19;

        //
        // Set floating registers ft0 - ft9.
        //

        RtlCopyIa64FloatRegisterContext(&TrapFrame->FltT0,
                                        &ContextEM->FltT0,
                                        sizeof(FLOAT128) * (10));
    }

    if ((ContextEM->ContextFlags & CONTEXT_HIGHER_FLOATING_POINT) == CONTEXT_HIGHER_FLOATING_POINT) {


        TrapFrame->StFPSR = SANITIZE_FSR(ContextEM->StFPSR, ProcessorMode);

        if (ProcessorMode == UserMode) {

            //
            // Update the higher floating point save area (f32-f127) and
            // set the corresponding modified bit in the PSR to 1.
            //

            RtlCopyIa64FloatRegisterContext(
                (PFLOAT128)GET_HIGH_FLOATING_POINT_REGISTER_SAVEAREA(KeGetCurrentThread()->StackBase),
                &ContextEM->FltF32,
                96*sizeof(FLOAT128));

            //
            // set the dfh bit to force a reload of the high fp register
            // set on the next user access, and clear mfh to make sure
            // the changes are not over written.
            //

            TrapFrame->StIPSR |= (1i64 << PSR_DFH);
            TrapFrame->StIPSR &= ~(1i64 << PSR_MFH);
        }

    }

    //
    // Set debug register contents if specified.
    //

    if ((ContextEM->ContextFlags & CONTEXT_DEBUG) == CONTEXT_DEBUG) {
        KiSetDebugContext (TrapFrame, ContextEM, ProcessorMode);
    }

    return;
}

VOID
PspGetSetContextSpecialApcMain (
    IN PKAPC Apc,
    IN PKNORMAL_ROUTINE *NormalRoutine,
    IN PVOID *NormalContext,
    IN PVOID *SystemArgument1,
    IN PVOID *SystemArgument2
    )

/*++

Routine Description:

    This function either captures the user mode state of the current
    thread, or sets the user mode state of the current thread. The
    operation type is determined by the value of SystemArgument1. A
    zero value is used for get context, and a nonzero value is used
    for set context.

Arguments:

    Apc - Supplies a pointer to the APC control object that caused entry
          into this routine.

    NormalRoutine - Supplies a pointer to the normal routine function that
        was specified when the APC was initialized. This parameter is not
        used.

    NormalContext - Supplies a pointer to an arbitrary data structure that
        was specified when the APC was initialized. This parameter is not
        used.

    SystemArgument1, SystemArgument2 - Supplies a set of two pointers to two
        arguments that contain untyped data.
        The first arguement is used to distinguish between get and set requests.
            A value of zero  signifies that GetThreadContext was requested.
            A non-zero value signifies that SetThreadContext was requested.
        The Second arguement has the thread handle. The second arguement is
        not used.

Return Value:

    None.

--*/

{
    PGETSETCONTEXT                ContextInfo;
    KNONVOLATILE_CONTEXT_POINTERS ContextPointers;  // Not currently used, needed later.
    CONTEXT                       ContextRecord;
    ULONGLONG                     ControlPc;
    FRAME_POINTERS                EstablisherFrame;
    PRUNTIME_FUNCTION             FunctionEntry;
    BOOLEAN                       InFunction;
    PKTRAP_FRAME                  TrFrame1;
    ULONGLONG                     ImageBase;
    ULONGLONG                     TargetGp;
    PETHREAD                      Thread;

    UNREFERENCED_PARAMETER (NormalRoutine);
    UNREFERENCED_PARAMETER (NormalContext);
    UNREFERENCED_PARAMETER (SystemArgument2);

    //
    // Get the address of the context frame and compute the address of the
    // system entry trap frame.
    //

    ContextInfo = CONTAINING_RECORD (Apc, GETSETCONTEXT, Apc);

    Thread = Apc->SystemArgument2;

    TrFrame1 = NULL;

    if (ContextInfo->Mode == KernelMode) {
        TrFrame1 = Thread->Tcb.TrapFrame;
    }

    if (TrFrame1 == NULL) {
        TrFrame1 = PspGetBaseTrapFrame (Thread);
    }


    //
    // Capture the current thread context and set the initial control PC
    // value.
    //

    RtlCaptureContext(&ContextRecord);
    ControlPc = ContextRecord.BrRp;

    //
    // Initialize context pointers for the nonvolatile integer and floating
    // registers.
    //

    ContextPointers.FltS0 = &ContextRecord.FltS0;
    ContextPointers.FltS1 = &ContextRecord.FltS1;
    ContextPointers.FltS2 = &ContextRecord.FltS2;
    ContextPointers.FltS3 = &ContextRecord.FltS3;
    ContextPointers.FltS4 = &ContextRecord.FltS4;
    ContextPointers.FltS5 = &ContextRecord.FltS5;
    ContextPointers.FltS6 = &ContextRecord.FltS6;
    ContextPointers.FltS7 = &ContextRecord.FltS7;
    ContextPointers.FltS8 = &ContextRecord.FltS8;
    ContextPointers.FltS9 = &ContextRecord.FltS9;
    ContextPointers.FltS10 = &ContextRecord.FltS10;
    ContextPointers.FltS11 = &ContextRecord.FltS11;
    ContextPointers.FltS12 = &ContextRecord.FltS12;
    ContextPointers.FltS13 = &ContextRecord.FltS13;
    ContextPointers.FltS14 = &ContextRecord.FltS14;
    ContextPointers.FltS15 = &ContextRecord.FltS15;
    ContextPointers.FltS16 = &ContextRecord.FltS16;
    ContextPointers.FltS17 = &ContextRecord.FltS17;
    ContextPointers.FltS18 = &ContextRecord.FltS18;
    ContextPointers.FltS19 = &ContextRecord.FltS19;

    ContextPointers.IntS0 = &ContextRecord.IntS0;
    ContextPointers.IntS1 = &ContextRecord.IntS1;
    ContextPointers.IntS2 = &ContextRecord.IntS2;
    ContextPointers.IntS3 = &ContextRecord.IntS3;
    ContextPointers.IntSp = &ContextRecord.IntSp;

    ContextPointers.BrS0 = &ContextRecord.BrS0;
    ContextPointers.BrS1 = &ContextRecord.BrS1;
    ContextPointers.BrS2 = &ContextRecord.BrS2;
    ContextPointers.BrS3 = &ContextRecord.BrS3;
    ContextPointers.BrS4 = &ContextRecord.BrS4;

    ContextPointers.ApLC = &ContextRecord.ApLC;
    ContextPointers.ApEC = &ContextRecord.ApEC;

    //
    // Start with the frame specified by the context record and virtually
    // unwind call frames until the system entry trap frame is encountered.
    //

    do {

        //
        // Lookup the function table entry using the point at which control
        // left the procedure.
        //

        FunctionEntry = RtlLookupFunctionEntry(ControlPc, &ImageBase, &TargetGp);

        //
        // If there is a function table entry for the routine, then virtually
        // unwind to the caller of the current routine to obtain the address
        // where control left the caller.
        //

        if (FunctionEntry != NULL) {
            ControlPc = RtlVirtualUnwind(ImageBase,
                                         ControlPc,
                                         FunctionEntry,
                                         &ContextRecord,
                                         &InFunction,
                                         &EstablisherFrame,
                                         &ContextPointers);

        } else {

            ControlPc = ContextRecord.BrRp;
            ContextRecord.StIFS = ContextRecord.RsPFS;
            ContextRecord.RsBSP = RtlpRseShrinkBySOL (ContextRecord.RsBSP, ContextRecord.StIFS);
        }

    } while ((PVOID)ContextRecord.IntSp != TrFrame1);

    //
    // Process GetThreadContext or SetThreadContext as specified.
    //

    if (*SystemArgument1 != 0) {

        //
        // Set Context from proper Context mode
        //

        PspSetContext(TrFrame1, &ContextPointers, &ContextInfo->Context,
                      ContextInfo->Mode);

    } else {

        //
        // Get Context from proper Context mode
        //

        KeFlushUserRseState(TrFrame1);
        PspGetContext(TrFrame1, &ContextPointers, &ContextInfo->Context);

    }

    KeSetEvent(&ContextInfo->OperationComplete, 0, FALSE);
    return;
}
