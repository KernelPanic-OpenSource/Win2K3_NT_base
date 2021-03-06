/*++

Copyright (c) 1989  Microsoft Corporation

Module Name:

    dpcobj.c

Abstract:

    This module implements the kernel DPC object. Functions are provided
    to initialize, insert, and remove DPC objects.

Author:

    David N. Cutler (davec) 6-Mar-1989

Environment:

    Kernel mode only.

Revision History:


--*/

#include "ki.h"


//
// The following assert macro is used to check that an input DPC object is
// really a KDPC and not something else, like deallocated pool.
//

#define ASSERT_DPC(E) {                                                     \
    ASSERT(((E)->Type == DpcObject) || ((E)->Type == ThreadedDpcObject));   \
}

//
// Define deferred reverse barrier structure.
//

#define DEFERRED_REVERSE_BARRIER_SYNCHRONIZED 0x80000000

typedef struct _DEFERRED_REVERSE_BARRIER {
    ULONG Barrier;
    ULONG TotalProcessors;
} DEFERRED_REVERSE_BARRIER, *PDEFERRED_REVERSE_BARRIER;

FORCEINLINE
PKDPC_DATA
KiSelectDpcData (
    IN PKPRCB Prcb,
    IN PKDPC Dpc
    )

/*++

Routine Description:

    This function selects the appropriate DPC data structure in the specified
    processor control block based on the type of DPC and whether threaded DPCs
    are enabled.

Arguments:

    Prcb - Supplies the address of a processor control block.

    Dpc - Supplies the address of a control object of type DPC.

Return Value:

    The address of the appropriate DPC data structure is returned.

--*/

{

    //
    // If the DPC is a threaded DPC and thread DPCs are enabled, then set
    // the address of the threaded DPC data. Otherwise, set the address of
    // the normal DPC structure.
    //
    
    if ((Dpc->Type == (UCHAR)ThreadedDpcObject) &&                           
        (Prcb->ThreadDpcEnable != FALSE)) {                                 
                                                                            
        return &Prcb->DpcData[DPC_THREADED];

    } else {
        return &Prcb->DpcData[DPC_NORMAL];                                   
    }
}

VOID
KiInitializeDpc (
    IN PRKDPC Dpc,
    IN PKDEFERRED_ROUTINE DeferredRoutine,
    IN PVOID DeferredContext,
    IN KOBJECTS DpcType
    )

/*++

Routine Description:

    This function initializes a kernel DPC object. The deferred routine,
    context parameter, and object type are stored in the DPC object.

Arguments:

    Dpc - Supplies a pointer to a control object of type DPC.

    DeferredRoutine - Supplies a pointer to a function that is called when
        the DPC object is removed from the current processor's DPC queue.

    DeferredContext - Supplies a pointer to an arbitrary data structure which is
        to be passed to the function specified by the DeferredRoutine parameter.

    DpcType - Supplies the type of DPC object.

Return Value:

    None.

--*/

{

    //
    // Initialize standard control object header.
    //

    Dpc->Type = (UCHAR)DpcType;
    Dpc->Number = 0;
    Dpc->Importance = MediumImportance;

    //
    // Initialize deferred routine address and deferred context parameter.
    //

    Dpc->DeferredRoutine = DeferredRoutine;
    Dpc->DeferredContext = DeferredContext;
    Dpc->DpcData = NULL;
    return;
}

VOID
KeInitializeDpc (
    IN PRKDPC Dpc,
    IN PKDEFERRED_ROUTINE DeferredRoutine,
    IN PVOID DeferredContext
    )

/*++

Routine Description:

    This function initializes a kernel DPC object.

Arguments:

    Dpc - Supplies a pointer to a control object of type DPC.

    DeferredRoutine - Supplies a pointer to a function that is called when
        the DPC object is removed from the current processor's DPC queue.

    DeferredContext - Supplies a pointer to an arbitrary data structure which is
        to be passed to the function specified by the DeferredRoutine parameter.

Return Value:

    None.

--*/

{

    KiInitializeDpc(Dpc, DeferredRoutine, DeferredContext, DpcObject);
}

VOID
KeInitializeThreadedDpc (
    IN PRKDPC Dpc,
    IN PKDEFERRED_ROUTINE DeferredRoutine,
    IN PVOID DeferredContext
    )

/*++

Routine Description:

    This function initializes a kernel Threaded DPC object.

Arguments:

    Dpc - Supplies a pointer to a control object of type DPC.

    DeferredRoutine - Supplies a pointer to a function that is called when
        the DPC object is removed from the current processor's DPC queue.

    DeferredContext - Supplies a pointer to an arbitrary data structure which is
        to be passed to the function specified by the DeferredRoutine parameter.

Return Value:

    None.

--*/

{

    KiInitializeDpc(Dpc, DeferredRoutine, DeferredContext, ThreadedDpcObject);
}

BOOLEAN
KeInsertQueueDpc (
    IN PRKDPC Dpc,
    IN PVOID SystemArgument1,
    IN PVOID SystemArgument2
    )

/*++

Routine Description:

    This function inserts a DPC object into the DPC queue. If the DPC object
    is already in the DPC queue, then no operation is performed. Otherwise,
    the DPC object is inserted in the DPC queue and a dispatch interrupt is
    requested.

Arguments:

    Dpc - Supplies a pointer to a control object of type DPC.

    SystemArgument1, SystemArgument2  - Supply a set of two arguments that
        contain untyped data provided by the executive.

Return Value:

    If the DPC object is already in a DPC queue, then a value of FALSE is
    returned. Otherwise a value of TRUE is returned.

--*/

{

    PKDPC_DATA DpcData;
    BOOLEAN Inserted;
    KIRQL OldIrql;
    PKPRCB Prcb;

    ASSERT_DPC(Dpc);

    //
    // Disable interrupts and acquire the DPC queue lock for the specified
    // target processor.
    //
    // N.B. Disable interrupt cannot be used here since it causes the
    //      software interrupt request code to get confuses on some
    //      platforms.
    //

    KeRaiseIrql(HIGH_LEVEL, &OldIrql);

#if !defined(NT_UP)

    if (Dpc->Number >= MAXIMUM_PROCESSORS) {
        Prcb = KiProcessorBlock[Dpc->Number - MAXIMUM_PROCESSORS];

    } else {
        Prcb = KeGetCurrentPrcb();
    }

    DpcData = KiSelectDpcData(Prcb, Dpc);
    KiAcquireSpinLock(&DpcData->DpcLock);

#else

    Prcb = KeGetCurrentPrcb();
    DpcData = KiSelectDpcData(Prcb, Dpc);

#endif

    //
    // If the DPC object is not in a DPC queue, then store the system
    // arguments, insert the DPC object in the DPC queue, increment the
    // number of DPCs queued to the target processor, increment the DPC
    // queue depth, set the address of the DPC target DPC spinlock, and
    // request a dispatch interrupt if appropriate.
    //

    Inserted = FALSE;
    if (InterlockedCompareExchangePointer(&Dpc->DpcData,
                                          DpcData,
                                          NULL) == NULL) {

        DpcData->DpcQueueDepth += 1;
        DpcData->DpcCount += 1;
        Dpc->SystemArgument1 = SystemArgument1;
        Dpc->SystemArgument2 = SystemArgument2;

        //
        // If the DPC is of high importance, then insert the DPC at the
        // head of the DPC queue. Otherwise, insert the DPC at the end
        // of the DPC queue.
        //

        Inserted = TRUE;
        if (Dpc->Importance == HighImportance) {
            InsertHeadList(&DpcData->DpcListHead, &Dpc->DpcListEntry);

        } else {
            InsertTailList(&DpcData->DpcListHead, &Dpc->DpcListEntry);
        }

        KeMemoryBarrier();

        //
        // If the DPC is a normal DPC, then determine if a DPC interrupt
        // should be request. Otherwise, check if the DPC thread should
        // be activated.
        //

        if (DpcData == &Prcb->DpcData[DPC_THREADED]) {

            //
            // If the DPC thread is not active on the target processor and
            // a thread activation has not been requested, then request a
            // dispatch interrupt on the target processor.
            //

            if ((Prcb->DpcThreadActive == FALSE) &&
                (Prcb->DpcThreadRequested == FALSE)) {

                InterlockedExchange(&Prcb->DpcSetEventRequest, TRUE);
                Prcb->DpcThreadRequested = TRUE;
                Prcb->QuantumEnd = TRUE;
                KeMemoryBarrier();

#if defined(NT_UP)

                KiRequestSoftwareInterrupt(DISPATCH_LEVEL);
#else

                if (Prcb != KeGetCurrentPrcb()) {
                    KiIpiSend(AFFINITY_MASK((Dpc->Number - MAXIMUM_PROCESSORS)),
                                            IPI_DPC);
    
                } else {
                    KiRequestSoftwareInterrupt(DISPATCH_LEVEL);
                }
#endif

            }

        } else {

            //
            // If a DPC routine is not active on the target processor and
            // an interrupt has not been requested, then request a dispatch
            // interrupt on the target processor if appropriate.
            //
    
            if ((Prcb->DpcRoutineActive == FALSE) &&
                (Prcb->DpcInterruptRequested == FALSE)) {
    
                //
                // Request a dispatch interrupt on the current processor if
                // the DPC is not of low importance, the length of the DPC
                // queue has exceeded the maximum threshold, or if the DPC
                // request rate is below the minimum threshold.
                //

#if defined(NT_UP)

                if ((Dpc->Importance != LowImportance) ||
                    (DpcData->DpcQueueDepth >= Prcb->MaximumDpcQueueDepth) ||
                    (Prcb->DpcRequestRate < Prcb->MinimumDpcRate)) {
    
                    Prcb->DpcInterruptRequested = TRUE;
                    KiRequestSoftwareInterrupt(DISPATCH_LEVEL);
                }
    
                //
                // If the DPC is being queued to another processor and the
                // DPC is of high importance, or the length of the other
                // processor's DPC queue has exceeded the maximum threshold,
                // then request a dispatch interrupt.
                //

#else

                if (Prcb != KeGetCurrentPrcb()) {
                    if (((Dpc->Importance == HighImportance) ||
                         (DpcData->DpcQueueDepth >= Prcb->MaximumDpcQueueDepth))) {
    
                        Prcb->DpcInterruptRequested = TRUE;
                        KiIpiSend(AFFINITY_MASK((Dpc->Number - MAXIMUM_PROCESSORS)),
                                                IPI_DPC);
                    }
    
                } else {
    
                    //
                    // Request a dispatch interrupt on the current processor
                    // if the DPC is not of low importance, the length of the
                    // DPC queue has exceeded the maximum threshold, or if the
                    // DPC request rate is below the minimum threshold.
                    //
    
                    if ((Dpc->Importance != LowImportance) ||
                        (DpcData->DpcQueueDepth >= Prcb->MaximumDpcQueueDepth) ||
                        (Prcb->DpcRequestRate < Prcb->MinimumDpcRate)) {
    
                        Prcb->DpcInterruptRequested = TRUE;
                        KiRequestSoftwareInterrupt(DISPATCH_LEVEL);
                    }
                }

#endif

            }
        }
    }

    //
    // Release the DPC lock, enable interrupts, and return whether the
    // DPC was queued or not.
    //

#if !defined(NT_UP)

    KiReleaseSpinLock(&DpcData->DpcLock);

#endif

    KeLowerIrql(OldIrql);
    return Inserted;
}

BOOLEAN
KeRemoveQueueDpc (
    IN PRKDPC Dpc
    )

/*++

Routine Description:

    This function removes a DPC object from the DPC queue. If the DPC object
    is not in the DPC queue, then no operation is performed. Otherwise, the
    DPC object is removed from the DPC queue and its inserted state is set
    FALSE.

Arguments:

    Dpc - Supplies a pointer to a control object of type DPC.

Return Value:

    If the DPC object is not in the DPC queue, then a value of FALSE is
    returned. Otherwise a value of TRUE is returned.

--*/

{

    PKDPC_DATA DpcData;
    BOOLEAN Enable;

    ASSERT_DPC(Dpc);

    //
    // If the DPC object is in the DPC queue, then remove it from the queue
    // and set its inserted state to FALSE.
    //

    Enable = KeDisableInterrupts();
    DpcData = Dpc->DpcData;
    if (DpcData != NULL) {

        //
        // Acquire the DPC lock of the target processor.
        //

#if !defined(NT_UP)

        KiAcquireSpinLock(&DpcData->DpcLock);

#endif

        //
        // If the specified DPC is still in the DPC queue, then remove
        // it.
        //
        // N.B. It is possible for specified DPC to be removed from the
        //      specified DPC queue before the DPC lock is obtained.
        //
        //

        if (DpcData == Dpc->DpcData) {
            DpcData->DpcQueueDepth -= 1;
            RemoveEntryList(&Dpc->DpcListEntry);
            Dpc->DpcData = NULL;
        }

        //
        // Release the DPC lock of the target processor.
        //

#if !defined(NT_UP)

        KiReleaseSpinLock(&DpcData->DpcLock);

#endif

    }

    //
    // Enable interrupts and return whether the DPC was removed from a DPC
    // queue.
    //

    KeEnableInterrupts(Enable);
    return (DpcData != NULL ? TRUE : FALSE);
}

VOID
KeSetImportanceDpc (
    IN PRKDPC Dpc,
    IN KDPC_IMPORTANCE Importance
    )

/*++

Routine Description:

    This function sets the importance of a DPC.

Arguments:

    Dpc - Supplies a pointer to a control object of type DPC.

    Number - Supplies the importance of the DPC.

Return Value:

    None.

--*/

{

    //
    // Set the importance of the DPC.
    //

    Dpc->Importance = (UCHAR)Importance;
    return;
}

VOID
KeSetTargetProcessorDpc (
    IN PRKDPC Dpc,
    IN CCHAR Number
    )

/*++

Routine Description:

    This function sets the processor number to which the DPC is targeted.

Arguments:

    Dpc - Supplies a pointer to a control object of type DPC.

    Number - Supplies the target processor number.

Return Value:

    None.

--*/

{

    //
    // The target processor number is biased by the maximum number of
    // processors that are supported.
    //

    Dpc->Number = MAXIMUM_PROCESSORS + Number;
    return;
}

VOID
KeFlushQueuedDpcs (
    VOID
    )

/*++

Routine Description:

    This function causes all current DPCs on all processors to execute to
    completion. This function is used at driver unload to make sure all
    driver DPC processing has exited the driver image before the code and
    data is deleted.

Arguments:

    None.

Return Value:

    None.

--*/

{

#if !defined(NT_UP)

    PKTHREAD CurrentThread;
    KPRIORITY OldPriority;
    KAFFINITY ProcessorMask;
    KAFFINITY SentDpcMask;
    KAFFINITY CurrentProcessorMask;
    BOOLEAN SetAffinity;
    ULONG CurrentProcessor;
    KIRQL OldIrql;

#endif

    PKPRCB CurrentPrcb;

    PAGED_CODE ();

#if defined(NT_UP)

    CurrentPrcb = KeGetCurrentPrcb();
    if ((CurrentPrcb->DpcData[DPC_NORMAL].DpcQueueDepth > 0) ||
        (CurrentPrcb->DpcData[DPC_THREADED].DpcQueueDepth > 0)) {
        KiRequestSoftwareInterrupt(DISPATCH_LEVEL);
    }

#else

#if DBG
    if (KeActiveProcessors == (KAFFINITY)1) {
        CurrentPrcb = KeGetCurrentPrcb();
        if ((CurrentPrcb->DpcData[DPC_NORMAL].DpcQueueDepth > 0) ||
            (CurrentPrcb->DpcData[DPC_THREADED].DpcQueueDepth > 0)) {
            KiRequestSoftwareInterrupt(DISPATCH_LEVEL);
        }
        return;
    }
#endif

    //
    // Set the priority of this thread high so it will run quickly on the
    // target processor.
    //

    CurrentThread = KeGetCurrentThread();
    OldPriority = KeSetPriorityThread(CurrentThread, HIGH_PRIORITY);
    ProcessorMask = KeActiveProcessors;
    SetAffinity = FALSE;
    SentDpcMask = 0;

    //
    // Clear the current processor from the affinity set and switch to the
    // remaining processors in the affinity set so it can be guaranteed that
    // respective DPCs have been completed processed.
    //

    while (1) {
       CurrentPrcb = KeGetCurrentPrcb();
       CurrentProcessor = CurrentPrcb->Number;
       CurrentProcessorMask = AFFINITY_MASK(CurrentProcessor);

       //
       // See if there are DPCs that we haven't delivered yet. LowImportance DPCs
       // don't run straight away. We need to force those to run now. We only need to do this once
       // per processor.
       //

       if ((SentDpcMask & CurrentProcessorMask) == 0 &&
           (CurrentPrcb->DpcData[DPC_NORMAL].DpcQueueDepth > 0) || (CurrentPrcb->DpcData[DPC_THREADED].DpcQueueDepth > 0)) {

           SentDpcMask |= CurrentProcessorMask;

           KeRaiseIrql(DISPATCH_LEVEL, &OldIrql);

           KiIpiSend(CurrentProcessorMask, IPI_DPC);

           KeLowerIrql(OldIrql);

           //
           // If we have not swapped processors then the DPCs must have run to completion.
           // If we have swapped processors then just repeat this for the current processor.
           //
           if (KeGetCurrentPrcb() != CurrentPrcb) {
               continue;
           }
       }
       ProcessorMask &= ~CurrentProcessorMask;

       if (ProcessorMask == 0) {
           break;
       }

       KeSetSystemAffinityThread(ProcessorMask);
       SetAffinity = TRUE;
    }

    //
    // Restore the affinity of the current thread if it was changed.
    //

    if (SetAffinity) {
        KeRevertToUserAffinityThread ();
    }

    OldPriority = KeSetPriorityThread(CurrentThread, OldPriority);

#endif

}

VOID
KeGenericCallDpc (
    IN PKDEFERRED_ROUTINE Routine,
    IN PVOID Context
    )

/*++

Routine Description:

    This function acquires the DPC call lock, initializes a processor specific
    DPC for each process with the specified deferred routine and context, and
    queues the DPC for execution. When all DPCs routines have executed, the
    DPC call lock is released.

Arguments:

    Routine - Supplies the address of the deferred routine to be called.

    Context - Supplies the context that is passed to the deferred routine.

Return Value:

    None.

--*/

{

    ULONG Barrier;

#if !defined(NT_UP)

    PKDPC Dpc;
    ULONG Index;
    ULONG Limit;
    ULONG Number;

#endif

    KIRQL OldIrql;
    DEFERRED_REVERSE_BARRIER ReverseBarrier;

    ASSERT(KeGetCurrentIrql () < DISPATCH_LEVEL);

    Barrier = KeNumberProcessors;
    ReverseBarrier.Barrier = Barrier;
    ReverseBarrier.TotalProcessors = Barrier;

#if !defined(NT_UP)

    Index = 0;
    Limit = Barrier;

#endif

#if !defined(NT_UP)

    //
    // Switch to processor one to synchronize with other DPCs.
    //

    KeSetSystemAffinityThread(1);

    //
    // Acquire generic call DPC mutex.
    //

    ExAcquireFastMutex(&KiGenericCallDpcMutex);

#endif

    KeRaiseIrql(DISPATCH_LEVEL, &OldIrql);

    //
    // Loop through all the target processors, initialize the deferred
    // routine address, context parameter, barrier parameter, and queue
    // the DPC to the target processor.
    //

#if !defined(NT_UP)

    Number = KeGetCurrentProcessorNumber();
    do {

        //
        // If the target processor is not the current processor, then
        // initialize and queue the generic call DPC.
        //

        if (Number != Index) {
            Dpc = &KiProcessorBlock[Index]->CallDpc;
            Dpc->DeferredRoutine = Routine;
            Dpc->DeferredContext = Context;
            KeInsertQueueDpc(Dpc, &Barrier, &ReverseBarrier);
        }

        Index += 1;
    } while (Index < Limit);

#endif

    //
    // Call deferred routine on current procesor.
    //

    (Routine)(&KeGetCurrentPrcb()->CallDpc, Context, &Barrier, &ReverseBarrier);

    //
    // Wait for all target DPC routines to finish execution.
    //

#if !defined(NT_UP)

    while (*((ULONG volatile *)&Barrier) != 0) {
        KeYieldProcessor();
    }

#endif

    //
    // Release generic all DPC mutex and lower IRQL to previous level.
    //

    KeLowerIrql(OldIrql);

#if !defined(NT_UP)

    ExReleaseFastMutex(&KiGenericCallDpcMutex);
    KeRevertToUserAffinityThread();

#endif
    return;
}

VOID
KeSignalCallDpcDone (
    IN PVOID SystemArgument1
    )

/*++

Routine Description:

    This function decrements the generic DPC call barrier whose address
    was passed to the deferred DPC function as the first system argument.

Arguments:

    SystemArgument1 - Supplies the address of the call barrier.

    N.B. This must be the system argument value that is passed to the
         target deferred routine.

Return Value:

    None.

--*/

{

    InterlockedDecrement((LONG volatile *)SystemArgument1);
    return;
}

LOGICAL
KeSignalCallDpcSynchronize (
    IN PVOID SystemArgument2
    )

/*++

Routine Description:

    This function decrements the generic DPC call reverse barrier whose
    address was passed to the deferred DPC function as the second system
    argument.

Arguments:

    SystemArgument2 - Supplies the address of the call barrier.

    N.B. This must be the second system argument value that is passed to
         the target deferred routine.

Return Value:

    LOGICAL - Tie breaker value. One processor receives the value TRUE,
              all others receive FALSE.  

--*/

{

#if !defined(NT_UP)

    PDEFERRED_REVERSE_BARRIER ReverseBarrier = SystemArgument2;
    LONG volatile *Barrier;

    //
    // Wait while other processors exit any previous synchronization.
    //

    Barrier = (LONG volatile *)&ReverseBarrier->Barrier;
    while ((*Barrier & DEFERRED_REVERSE_BARRIER_SYNCHRONIZED) != 0) {
        KeYieldProcessor();
    }

    //
    // The barrier value is now of the form 1..MaxProcessors. Decrement this
    // processor's contribution and wait till the value goes to zero.
    //

    if (InterlockedDecrement(Barrier) == 0) {
        if (ReverseBarrier->TotalProcessors == 1) {
            InterlockedExchange(Barrier, ReverseBarrier->TotalProcessors);
        } else {
            InterlockedExchange(Barrier, DEFERRED_REVERSE_BARRIER_SYNCHRONIZED + 1);
        }
        return TRUE;
    }

    //
    // Wait until the last processor reaches this point.
    //

    while ((*Barrier & DEFERRED_REVERSE_BARRIER_SYNCHRONIZED) == 0) {
        KeYieldProcessor();
    }

    //
    // Signal other processors that the synchronization has occurred. If this
    // is the last processor, then reset the barrier.
    //

    if ((ULONG)InterlockedIncrement(Barrier) == (ReverseBarrier->TotalProcessors | DEFERRED_REVERSE_BARRIER_SYNCHRONIZED)) {
        InterlockedExchange(Barrier, ReverseBarrier->TotalProcessors);
    }

    return FALSE;

#else

    UNREFERENCED_PARAMETER(SystemArgument2);

    return TRUE;

#endif

}
