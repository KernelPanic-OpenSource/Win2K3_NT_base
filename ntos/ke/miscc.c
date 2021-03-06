/*++

Copyright (c) 1989-1992  Microsoft Corporation

Module Name:

    miscc.c

Abstract:

    This module implements machine independent miscellaneous kernel functions.

Author:

    David N. Cutler (davec) 13-May-1989

Environment:

    Kernel mode only.

Revision History:

--*/

#include "ki.h"

#pragma alloc_text(PAGE, KeAddSystemServiceTable)
#pragma alloc_text(PAGE, KeRemoveSystemServiceTable)
#pragma alloc_text(PAGE, KeQueryActiveProcessors)
#pragma alloc_text(PAGE, KeQueryLogicalProcessorInformation)
#pragma alloc_text(PAGELK, KiCalibrateTimeAdjustment)

#if !defined(_AMD64_)

ULONGLONG
KeQueryInterruptTime (
    VOID
    )

/*++

Routine Description:

    This function returns the current interrupt time by determining when the
    time is stable and then returning its value.

Arguments:

    CurrentTime - Supplies a pointer to a variable that will receive the
        current system time.

Return Value:

    None.

--*/

{

    LARGE_INTEGER CurrentTime;

    KiQueryInterruptTime(&CurrentTime);
    return CurrentTime.QuadPart;
}

VOID
KeQuerySystemTime (
    OUT PLARGE_INTEGER CurrentTime
    )

/*++

Routine Description:

    This function returns the current system time by determining when the
    time is stable and then returning its value.

Arguments:

    CurrentTime - Supplies a pointer to a variable that will receive the
        current system time.

Return Value:

    None.

--*/

{

    KiQuerySystemTime(CurrentTime);
    return;
}

VOID
KeQueryTickCount (
    OUT PLARGE_INTEGER CurrentCount
    )

/*++

Routine Description:

    This function returns the current tick count by determining when the
    count is stable and then returning its value.

Arguments:

    CurrentCount - Supplies a pointer to a variable that will receive the
        current tick count.

Return Value:

    None.

--*/

{

    KiQueryTickCount(CurrentCount);
    return;
}

#endif

ULONG
KeQueryTimeIncrement (
    VOID
    )

/*++

Routine Description:

    This function returns the time increment value in 100ns units. This
    is the value that is added to the system time at each interval clock
    interrupt.

Arguments:

    None.

Return Value:

    The time increment value is returned as the function value.

--*/

{

    return KeMaximumIncrement;
}

VOID
KeEnableInterrupts (
    IN BOOLEAN Enable
    )

/*++

Routine Description:

    This function enables interrupts based on the specified enable state.

Arguments:

    Enable - Supplies a boolean value that determines whether interrupts
        are to be enabled.

Return Value:

    None.

--*/

{

    if (Enable != FALSE) {
        _enable();
    }

    return;
}

VOID
KeSetDmaIoCoherency (
    IN ULONG Attributes
    )

/*++

Routine Description:

    This function sets (enables/disables) DMA I/O coherency with data
    caches.

Arguments:

    Attributes - Supplies the set of DMA I/O coherency attributes for
        the host system.

Return Value:

    None.

--*/

{

    KiDmaIoCoherency = Attributes;
}

#if defined(_AMD64_) || defined(_X86_)

#pragma alloc_text(INIT, KeSetProfileIrql)

VOID
KeSetProfileIrql (
    IN KIRQL ProfileIrql
    )

/*++

Routine Description:

    This function sets the profile IRQL.

    N.B. There are only two valid values for the profile IRQL which are
        PROFILE_LEVEL and HIGH_LEVEL.

Arguments:

    Irql - Supplies the synchronization IRQL value.

Return Value:

    None.

--*/

{

    ASSERT((ProfileIrql == PROFILE_LEVEL) || (ProfileIrql == HIGH_LEVEL));

    KiProfileIrql = ProfileIrql;
}

#endif

VOID
KeSetSystemTime (
    IN PLARGE_INTEGER NewTime,
    OUT PLARGE_INTEGER OldTime,
    IN BOOLEAN AdjustInterruptTime,
    IN PLARGE_INTEGER HalTimeToSet OPTIONAL
    )

/*++

Routine Description:

    This function sets the system time to the specified value and updates
    timer queue entries to reflect the difference between the old system
    time and the new system time.

Arguments:

    NewTime - Supplies a pointer to a variable that specifies the new system
        time.

    OldTime - Supplies a pointer to a variable that will receive the previous
        system time.

    AdjustInterruptTime - If TRUE the amount of time being adjusted is
        also applied to InterruptTime and TickCount.

    HalTimeToSet - Supplies an optional time that if specified is to be used
        to set the time in the realtime clock.

Return Value:

    None.

--*/

{

    LIST_ENTRY AbsoluteListHead;
    LIST_ENTRY ExpiredListHead;
    ULONG Index;
    PLIST_ENTRY ListHead;
    PLIST_ENTRY NextEntry;
    KIRQL OldIrql1;
    KIRQL OldIrql2;
    LARGE_INTEGER TimeDelta;
    TIME_FIELDS TimeFields;
    PKTIMER Timer;

    ASSERT((NewTime->HighPart & 0xf0000000) == 0);

    ASSERT(KeGetCurrentIrql() < DISPATCH_LEVEL);

    //
    // If a realtime clock value is specified, then convert the time value
    // to time fields.
    //

    if (ARGUMENT_PRESENT(HalTimeToSet)) {
        RtlTimeToTimeFields(HalTimeToSet, &TimeFields);
    }

    //
    // Set affinity to the processor that keeps the system time, raise IRQL
    // to dispatcher level and lock the dispatcher database, then raise IRQL
    // to HIGH_LEVEL to synchronize with the clock interrupt routine.
    //

    KeSetSystemAffinityThread((KAFFINITY)1);
    KiLockDispatcherDatabase(&OldIrql1);
    KeRaiseIrql(HIGH_LEVEL, &OldIrql2);

    //
    // Save the previous system time, set the new system time, and set
    // the realtime clock, if a time value is specified.
    //

    KiQuerySystemTime(OldTime);
    SharedUserData->SystemTime.High2Time = NewTime->HighPart;
    SharedUserData->SystemTime.LowPart   = NewTime->LowPart;
    SharedUserData->SystemTime.High1Time = NewTime->HighPart;

    if (ARGUMENT_PRESENT(HalTimeToSet)) {
        ExCmosClockIsSane = HalSetRealTimeClock(&TimeFields);
    }

    //
    // Compute the difference between the previous system time and the new
    // system time.
    //

    TimeDelta.QuadPart = NewTime->QuadPart - OldTime->QuadPart;

    //
    // Update the boot time to reflect the delta. This keeps time based
    // on boot time constant
    //

    KeBootTime.QuadPart = KeBootTime.QuadPart + TimeDelta.QuadPart;

    //
    // Track the overall bias applied to the boot time.
    //

    KeBootTimeBias = KeBootTimeBias + TimeDelta.QuadPart;

    //
    // Lower IRQL to dispatch level and if needed adjust the physical
    // system interrupt time.
    //

    KeLowerIrql(OldIrql2);
    if (AdjustInterruptTime) {

        //
        // Adjust the physical time of the system
        //

        AdjustInterruptTime = KeAdjustInterruptTime (TimeDelta.QuadPart);
    }

    //
    // If the physical interrupt time of the system was not adjusted,
    // recompute any absolute timers in the system for the new
    // system time.
    //

    if (!AdjustInterruptTime) {

        //
        // Remove all absolute timers from the timer queue so their due time
        // can be recomputed.
        //

        InitializeListHead(&AbsoluteListHead);
        for (Index = 0; Index < TIMER_TABLE_SIZE; Index += 1) {
            ListHead = &KiTimerTableListHead[Index];
            NextEntry = ListHead->Flink;
            while (NextEntry != ListHead) {
                Timer = CONTAINING_RECORD(NextEntry, KTIMER, TimerListEntry);
                NextEntry = NextEntry->Flink;
                if (Timer->Header.Absolute != FALSE) {
                    RemoveEntryList(&Timer->TimerListEntry);
                    InsertTailList(&AbsoluteListHead, &Timer->TimerListEntry);
                }
            }
        }

        //
        // Recompute the due time and reinsert all absolute timers in the timer
        // tree. If a timer has already expired, then insert the timer in the
        // expired timer list.
        //

        InitializeListHead(&ExpiredListHead);
        while (AbsoluteListHead.Flink != &AbsoluteListHead) {
            Timer = CONTAINING_RECORD(AbsoluteListHead.Flink, KTIMER, TimerListEntry);
            KiRemoveTreeTimer(Timer);
            Timer->DueTime.QuadPart -= TimeDelta.QuadPart;
            if (KiReinsertTreeTimer(Timer, Timer->DueTime) == FALSE) {
                Timer->Header.Inserted = TRUE;
                InsertTailList(&ExpiredListHead, &Timer->TimerListEntry);
            }
        }

        //
        // If any of the attempts to reinsert a timer failed, then timers have
        // already expired and must be processed.
        //
        // N.B. The following function returns with the dispatcher database
        //      unlocked.
        //

        KiTimerListExpire(&ExpiredListHead, OldIrql1);

    } else {

        KiUnlockDispatcherDatabase(OldIrql1);

    }


    //
    // Set affinity back to its original value.
    //

    KeRevertToUserAffinityThread();

    return;
}

BOOLEAN
KeAdjustInterruptTime (
    IN LONGLONG TimeDelta
    )

/*++

Routine Description:

    This function moves the physical interrupt time of the system foreward by
    the specified time delta after a system wake has occurred.

Arguments:

    TimeDelta - Supplies the time delta to be added to the interrupt time, tick
        count and the perforamnce counter in 100ns units.

Return Value:

    None.

--*/

{

    ADJUST_INTERRUPT_TIME_CONTEXT Adjust;

    //
    // Time can only be moved forward.
    //

    if (TimeDelta < 0) {
        return FALSE;

    } else {
        Adjust.KiNumber = KeNumberProcessors;
        Adjust.HalNumber = KeNumberProcessors;
        Adjust.Adjustment = (ULONGLONG) TimeDelta;
        Adjust.Barrier = 1;
        KeIpiGenericCall((PKIPI_BROADCAST_WORKER)KiCalibrateTimeAdjustment,
                         (ULONG_PTR)(&Adjust));

        return TRUE;
    }
}

VOID
KiCalibrateTimeAdjustment (
    PADJUST_INTERRUPT_TIME_CONTEXT Adjust
    )

/*++

Routine Description:

    This function calibrates the adjustment of time on all processors.

Arguments:

    Adjust - Supplies the operation context.

Return Value:

    None.

--*/

{

    ULONG cl;
    ULONG divisor;
    BOOLEAN Enable;
    LARGE_INTEGER InterruptTime;
    ULARGE_INTEGER li;
    LARGE_INTEGER NewTickCount;
    ULONG NewTickOffset;
    LARGE_INTEGER PerfCount;
    LARGE_INTEGER PerfFreq;
    LARGE_INTEGER SetTime;

    //
    // As each processor arrives, decrement the remaining processor count. If
    // this is the last processor to arrive, then compute the time change, and
    // signal all processor when to apply the performance counter change.
    //

    if (InterlockedDecrement((PLONG)&Adjust->KiNumber)) {
        Enable = KeDisableInterrupts();

        //
        // It is possible to deadlock here if one or more of the
        // other processors gets and processes a freeze request
        // while this processor has interrupts disabled.  Poll
        // for IPI_FREEZE requests until all processors are known
        // to be in this code and hence wont be requesting a
        // freeze.
        //

        do {
            KiPollFreezeExecution();
        } while (Adjust->KiNumber != (ULONG)-1);

        //
        // Wait to perform the time set
        //

        while (Adjust->Barrier) ;

    } else {

        //
        // Set timer expiration dpc to scan the timer queues once for any
        // expired timers.
        //

        KeRemoveQueueDpc(&KiTimerExpireDpc);
        KeInsertQueueDpc(&KiTimerExpireDpc,
                         ULongToPtr(KiQueryLowTickCount() - TIMER_TABLE_SIZE),
                         NULL);

        //
        // Disable interrupts and indicate that this processor is now
        // in final portion of this code.
        //

        Enable = KeDisableInterrupts();
        InterlockedDecrement((PLONG) &Adjust->KiNumber);

        //
        // Adjust Interrupt Time.
        //

        InterruptTime.QuadPart = KeQueryInterruptTime() + Adjust->Adjustment;
        SetTime.QuadPart = Adjust->Adjustment;

        //
        // Get the current times
        //

        PerfCount = KeQueryPerformanceCounter(&PerfFreq);

        //
        // Compute performance counter for current SetTime
        //

        //
        // Multiply SetTime * PerfCount and obtain 96bit result
        // in cl, li.LowPart, li.HighPart.  Then divide the 96bit
        // result by 10,000,000 to get new performance counter value.
        //

        li.QuadPart = RtlEnlargedUnsignedMultiply((ULONG)SetTime.LowPart,
                                                  (ULONG)PerfFreq.LowPart).QuadPart;

        cl = li.LowPart;
        li.QuadPart =
            li.HighPart + RtlEnlargedUnsignedMultiply((ULONG)SetTime.LowPart,
                                                      (ULONG)PerfFreq.HighPart).QuadPart;

        li.QuadPart =
            li.QuadPart + RtlEnlargedUnsignedMultiply((ULONG)SetTime.HighPart,
                                                      (ULONG)PerfFreq.LowPart).QuadPart;

        li.HighPart = li.HighPart + SetTime.HighPart * PerfFreq.HighPart;
        divisor = 10000000;
        Adjust->NewCount.HighPart = RtlEnlargedUnsignedDivide(li,
                                                              divisor,
                                                              &li.HighPart);

        li.LowPart = cl;
        Adjust->NewCount.LowPart = RtlEnlargedUnsignedDivide(li,
                                                             divisor,
                                                             NULL);

        Adjust->NewCount.QuadPart += PerfCount.QuadPart;

        //
        // Compute tick count and tick offset for current InterruptTime
        //

        NewTickCount = RtlExtendedLargeIntegerDivide(InterruptTime,
                                                     KeMaximumIncrement,
                                                     &NewTickOffset);

        //
        // Apply changes to InterruptTime, TickCount, TickOffset, and the
        // performance counter.
        //

        KiTickOffset = KeMaximumIncrement - NewTickOffset;
        KeInterruptTimeBias += Adjust->Adjustment;
        SharedUserData->TickCount.High2Time = NewTickCount.HighPart;

#if defined(_WIN64)

        SharedUserData->TickCountQuad       = NewTickCount.QuadPart;

#else

        SharedUserData->TickCount.LowPart   = NewTickCount.LowPart;
        SharedUserData->TickCount.High1Time = NewTickCount.HighPart;

#endif

#if defined(_IA64_)

        KeTickCount = NewTickCount;

#elif defined(_X86_)

        KeTickCount.High2Time = NewTickCount.HighPart;
        KeTickCount.LowPart   = NewTickCount.LowPart;
        KeTickCount.High1Time = NewTickCount.HighPart;

#endif

        SharedUserData->InterruptTime.High2Time = InterruptTime.HighPart;
        SharedUserData->InterruptTime.LowPart   = InterruptTime.LowPart;
        SharedUserData->InterruptTime.High1Time = InterruptTime.HighPart;

        //
        // Apply the performance counter change.
        //

        Adjust->Barrier = 0;
    }

    HalCalibratePerformanceCounter((LONG volatile *)&Adjust->HalNumber,
                                   (ULONGLONG) Adjust->NewCount.QuadPart);

    KeEnableInterrupts(Enable);
}

VOID
KeSetTimeIncrement (
    IN ULONG MaximumIncrement,
    IN ULONG MinimumIncrement
    )

/*++

Routine Description:

    This function sets the time increment value in 100ns units. This
    value is added to the system time at each interval clock interrupt.

Arguments:

    MaximumIncrement - Supplies the maximum time between clock interrupts
        in 100ns units supported by the host HAL.

    MinimumIncrement - Supplies the minimum time between clock interrupts
        in 100ns units supported by the host HAL.

Return Value:

    None.

--*/

{

    KeMaximumIncrement = MaximumIncrement;
    KeMinimumIncrement = max(MinimumIncrement, 10 * 1000);
    KeTimeAdjustment = MaximumIncrement;
    KeTimeIncrement = MaximumIncrement;
    KiTickOffset = MaximumIncrement;

#if defined(_IA64_)
    KiMaxIntervalPerTimerInterrupt = MaximumIncrement * (TIMER_TABLE_SIZE - 1);
#endif

}

BOOLEAN
KeAddSystemServiceTable(
    IN PULONG_PTR Base,
    IN PULONG Count OPTIONAL,
    IN ULONG Limit,
    IN PUCHAR Number,
    IN ULONG Index
    )

/*++

Routine Description:

    This function allows the caller to add a system service table
    to the system

Arguments:

    Base - Supplies the address of the system service table dispatch
        table.

    Count - Supplies an optional pointer to a table of per system service
        counters.

    Limit - Supplies the limit of the service table. Services greater
        than or equal to this limit will fail.

    Arguments - Supplies the address of the argument count table.

    Index - Supplies index of the service table.

Return Value:

    TRUE - The operation was successful.

    FALSE - the operation failed. A service table is already bound to
        the specified location, or the specified index is larger than
        the maximum allowed index.

--*/

{

    PAGED_CODE();

    //
    // If a system service table is already defined for the specified
    // index, then return FALSE. Otherwise, establish the new system
    // service table.
    //

    if ((Index > NUMBER_SERVICE_TABLES - 1) ||
        (KeServiceDescriptorTable[Index].Base != NULL) ||
        (KeServiceDescriptorTableShadow[Index].Base != NULL)) {
        return FALSE;

    } else {

        //
        // If the service table index is equal to the Win32 table, then
        // only update the shadow system service table. Otherwise, both
        // the shadow and static system service tables are updated.
        //

        KeServiceDescriptorTableShadow[Index].Base = Base;
        KeServiceDescriptorTableShadow[Index].Count = Count;
        KeServiceDescriptorTableShadow[Index].Limit = Limit;

        //
        // The global pointer associated with the table base is
        // placed just before the service table.
        //

#if defined(_IA64_)

        KeServiceDescriptorTableShadow[Index].TableBaseGpOffset =
                                        (LONG)(*(Base-1) - (ULONG_PTR)Base);

#endif

        KeServiceDescriptorTableShadow[Index].Number = Number;
        if (Index != 1) {
            KeServiceDescriptorTable[Index].Base = Base;
            KeServiceDescriptorTable[Index].Count = Count;
            KeServiceDescriptorTable[Index].Limit = Limit;

#if defined(_IA64_)

            KeServiceDescriptorTable[Index].TableBaseGpOffset =
                                        (LONG)(*(Base-1) - (ULONG_PTR)Base);

#endif

            KeServiceDescriptorTable[Index].Number = Number;
        }

        return TRUE;
    }
}

BOOLEAN
KeRemoveSystemServiceTable(
    IN ULONG Index
    )

/*++

Routine Description:

    This function allows the caller to remove a system service table
    from the system. This can only be called at system shutdown.

Arguments:

    Index - Supplies index of the service table.

Return Value:

    TRUE - The operation was successful.

    FALSE - the operation failed. A service table is is not bound or is illegal to remove

--*/

{

    PAGED_CODE();

    if ((Index > NUMBER_SERVICE_TABLES - 1) ||
        ((KeServiceDescriptorTable[Index].Base == NULL) &&
         (KeServiceDescriptorTableShadow[Index].Base == NULL))) {

        return FALSE;

    } else {
        KeServiceDescriptorTableShadow[Index].Base = NULL;
        KeServiceDescriptorTableShadow[Index].Count = 0;
        KeServiceDescriptorTableShadow[Index].Limit = 0;

#if defined(_IA64_)

        KeServiceDescriptorTableShadow[Index].TableBaseGpOffset = 0;

#endif

        KeServiceDescriptorTableShadow[Index].Number = 0;
        if (Index != 1) {
            KeServiceDescriptorTable[Index].Base = NULL;
            KeServiceDescriptorTable[Index].Count = 0;
            KeServiceDescriptorTable[Index].Limit = 0;

#if defined(_IA64_)

            KeServiceDescriptorTable[Index].TableBaseGpOffset = 0;

#endif

            KeServiceDescriptorTable[Index].Number = 0;
        }

        return TRUE;
    }
}

KAFFINITY
KeQueryActiveProcessors(
    VOID
    )

/*++

Routine Description:

    This function returns the current set of active processors
    in the system.

Arguments:

    None.

Return Value:

    KAFFINITY bitmask representing the set of active processors

--*/

{
    PAGED_CODE();

    return(KeActiveProcessors);
}

NTSTATUS
KeQueryLogicalProcessorInformation(
    OUT PVOID  SystemInformation,
    IN  ULONG  SystemInformationLength,
    OUT PULONG ReturnedLength
    )

/*++

Routine Description:

    This function returns information about the logical processors in
    the system and is invoked via NtQuerySystemInformation.  It runs
    in an existing try/except block.

    A group of structures will be written to the output
    buffer describing groups of logical processors, and the
    relationship between them.

    Currently it returns information about the logical processors that
    are produced by individual processor cores and the logical
    processors associated with individual NUMA nodes.  The former
    makes it possible for an application to understand the
    relationship between logical processors and physical processors in
    hyperthreading scenarios which supports some licensing and
    performance optimization scenarios.

    This function may be extended in the future to support multicore
    processors and platform caches.

Arguments:

    SystemInformation - A pointer to a buffer which receives the
        specified information.  The buffer will be will be filled by
        this function with SYSTEM_LOGICAL_PROCESSOR_INFORMATION
        structures.

    SystemInformationLength - Specifies the length in bytes of the system
        information buffer.

    ReturnLength - A pointer which receives the number of bytes necessary to
        return all of the information records available.

Return Value:

    NTSTATUS

--*/

{
    PSYSTEM_LOGICAL_PROCESSOR_INFORMATION Output;
    KAFFINITY ActiveProcessors;
    KAFFINITY Mask;
    PKPRCB Prcb;
    NTSTATUS Status = STATUS_SUCCESS;
    ULONG CurrentLength;
    ULONG i;
    UCHAR Flags;

#if defined(KE_MULTINODE)
    PKNODE Node;
#endif

    PAGED_CODE();

    CurrentLength = 0;
    Output = SystemInformation;

    ActiveProcessors = KeActiveProcessors;
    i = 0;

    for (; ActiveProcessors; ActiveProcessors >>= 1 , i++) {

        if ((ActiveProcessors & 1) == 0) {
            continue;
        }

        Prcb = KiProcessorBlock[i];

        Flags = 0;

#if defined(NT_SMT) 
        //
        // Ignore logical processors that are not the master of their
        // thread set.  As a result, only one PRCB per physical
        // processor will be further interrogated.
        //

        if (Prcb != Prcb->MultiThreadSetMaster) {
            continue;
        }

        Mask = Prcb->MultiThreadProcessorSet;

        //
        // Determine if this physical processor is exposing multiple
        // logical processors.  If so, mark it as a SMT relationship.
        //
        if (Prcb->SetMember != Mask) {
            Flags = LTP_PC_SMT;
        }
#else
        Mask = Prcb->SetMember;
#endif
            
        CurrentLength += sizeof(SYSTEM_LOGICAL_PROCESSOR_INFORMATION);
        if (CurrentLength <= SystemInformationLength) {

            Output->ProcessorMask = Mask;
            Output->Relationship = RelationProcessorCore;
            Output->Reserved[0] = Output->Reserved[1] = 0;
            Output->ProcessorCore.Flags = Flags;
            Output++;
        } else {
            Status = STATUS_INFO_LENGTH_MISMATCH;
        }
    }

    //
    // Add records indicating the association of logical processors
    // with NUMA nodes.
    //

#if defined(KE_MULTINODE)
    for (i = 0; i < KeNumberNodes; i++) {
        Node = KeNodeBlock[i];

        if (Node->ProcessorMask == 0) {
            continue;
        }

        CurrentLength += sizeof(SYSTEM_LOGICAL_PROCESSOR_INFORMATION);
        if (CurrentLength <= SystemInformationLength) {

            Output->ProcessorMask = Node->ProcessorMask;
            Output->Relationship = RelationNumaNode;
            Output->Reserved[0] = Output->Reserved[1] = 0;
            Output->NumaNode.NodeNumber = i;
            Output++;
        } else {
            Status = STATUS_INFO_LENGTH_MISMATCH;
        }
    }
#else
    CurrentLength += sizeof(SYSTEM_LOGICAL_PROCESSOR_INFORMATION);
    if (CurrentLength <= SystemInformationLength) {
            
        Output->ProcessorMask = KeActiveProcessors;
        Output->Relationship = RelationNumaNode;
        Output->NumaNode.NodeNumber = 0;
        Output++;
    } else {
        Status = STATUS_INFO_LENGTH_MISMATCH;
    }
#endif

    //
    // Additional topology information would be added here such as
    // multicore and platform caches.
    // 

    //
    // Always return how long the buffer needed to be for the API to
    // be successful.
    //

    *ReturnedLength = CurrentLength;
    return Status;
}

#undef KeIsAttachedProcess

BOOLEAN
KeIsAttachedProcess(
    VOID
    )

/*++

Routine Description:

    This function determines if the current thread is attached to a process.

Arguments:

    None.

Return Value:

    TRUE is returned if the current thread is attached to a process. Otherwise,
    FALSE is returned.

--*/

{
    return KiIsAttachedProcess() ? TRUE : FALSE;
}

ULONG
KeGetRecommendedSharedDataAlignment (
    VOID
    )

/*++

Routine Description:

    This function returns the size of the largest cache line in the system.
    This value should be used as a recommended alignment / granularity for
    shared data.

Arguments:

    None.

Return Value:

    The size of the largest cache line in the system is returned as the
    function value.

--*/

{
    return KeLargestCacheLine;
}

PKPRCB
KeGetPrcb(
    ULONG ProcessorNumber
    )

/*++

Routine Description:

    This function returns the address of the Processor Control Block (PRCB)
    for the specified processor.

Arguments:

    ProcessorNumber - Supplies the number of the processor the PRCB
    is to be returned for.

Return Value:

    Returns the address of the requested PRCB or NULL if ProcessorNumber
    is not valid.

--*/

{

    ASSERT(ProcessorNumber < MAXIMUM_PROCESSORS);

    if (ProcessorNumber < (ULONG)KeNumberProcessors) {
        return KiProcessorBlock[ProcessorNumber];
    }

    return NULL;
}

NTSTATUS
KeCopySafe(
    VOID UNALIGNED *Destination,
    CONST VOID UNALIGNED *Source,
    SIZE_T Length
    )

/*++

Routine Description:

    This function attempts to safely copy a block of memory. If an exception
    occurs the exception status is returned.

Arguments:

    Destination - Supplies a pointer to the destination memory.

    Source - Supplies a pointer to the source memory.

    Length - Supplies the size of memory in bytes to be copied.

Return Value:

    Return the status of the copy.

--*/

{
    NTSTATUS Status = STATUS_SUCCESS;

    try {
        RtlCopyMemory(Destination, Source, Length);
    } __except(EXCEPTION_EXECUTE_HANDLER) {

          Status = _exception_code();
    }

    return Status;
}

typedef struct _KNMI_HANDLER_CALLBACK {
    struct _KNMI_HANDLER_CALLBACK * Next;
    PNMI_CALLBACK                   Callback;
    PVOID                           Context;
    PVOID                           Handle;
} KNMI_HANDLER_CALLBACK, *PKNMI_HANDLER_CALLBACK;

PKNMI_HANDLER_CALLBACK KiNmiCallbackListHead;
KSPIN_LOCK KiNmiCallbackListLock;

BOOLEAN
KiHandleNmi(
    VOID
    )

/*++

Routine Description:

    This routine is called to process the list of registered Non-Maskable-
    Interrupt (NMI) handlers in the system.  This routine is called from
    the NMI interrupt vector, the IRQL is unknown and must be treated as
    if at HIGH_LEVEL.   Neither this function or any called function can
    alter system IRQL.

    The list of handlers must be edited in such a way that it is always
    valid.   This routine cannot acquire a lock before transiting the list.

Arguments:

    None.

Return Value:

    Returns TRUE is any handler on the list claims to have handled the
    interrupt, FALSE otherwise.

--*/

{
    BOOLEAN Handled;
    PKNMI_HANDLER_CALLBACK Handler;

    Handler = KiNmiCallbackListHead;
    Handled = FALSE;

    while (Handler) {
        Handled |= Handler->Callback(Handler->Context, Handled);
        Handler = Handler->Next;
    }

    return Handled;
}

PVOID
KeRegisterNmiCallback(
    PNMI_CALLBACK   CallbackRoutine,
    PVOID           Context
    )

/*++

Routine Description:

    This routine is called to add a callback to the list of Non-Maskable-
    Interrupt (NMI) handlers.

    This routine must be called at IRQL < DISPATCH_LEVEL.

    List insertion must be such that the list is ALWAYS valid, an NMI
    could occur during insertion and the NMI handler must be able to
    safely transit the list.

Arguments:

    CallbackRoutine supplies a pointer to the routine to be called on NMI.
    Context         supplies an arbitary value which will be passed
                    to the CallbackRoutine.

Return Value:

    Returns an arbitary handle that must be passed to KeDeregisterNmiCallback
    or NULL if registration was unsuccessful.

--*/

{
    PKNMI_HANDLER_CALLBACK Handler;
    PKNMI_HANDLER_CALLBACK Next;
    KIRQL OldIrql;

    ASSERT(KeGetCurrentIrql() < DISPATCH_LEVEL);

    //
    // Allocate memory for the callback object.
    //

    Handler = ExAllocatePoolWithTag(NonPagedPool,
                                    sizeof(KNMI_HANDLER_CALLBACK),
                                    'IMNK');

    if (Handler == NULL) {
        return Handler;
    }

    //
    // Fill in the non-protected elements.
    //

    Handler->Callback = CallbackRoutine;
    Handler->Context = Context;
    Handler->Handle = Handler;

    //
    // Insert the handler onto the front of the list.
    //

    KeAcquireSpinLock(&KiNmiCallbackListLock, &OldIrql);
    Handler->Next = KiNmiCallbackListHead;

    //
    // Because the lock is held, the following can't fail but is needed
    // to ensure the compiler doesn't store KiNmiCallbackList before
    // storing Handler->Next because the NMI handler may run down this
    // list and does not (can not) take the lock.
    //

    Next = InterlockedCompareExchangePointer(&KiNmiCallbackListHead,
                                             Handler,
                                             Handler->Next);
    ASSERT(Next == Handler->Next);

    KeReleaseSpinLock(&KiNmiCallbackListLock, OldIrql);

    //
    // Return the address of this handler as an opaque handle.
    //

    return Handler->Handle;
}


NTSTATUS
KeDeregisterNmiCallback(
    PVOID Handle
    )

/*++

Routine Description:

    This routine is called to remove a callback from the list of Non-
    Maskable-Interrupt callbacks.

    This routine must be called at IRQL < DISPATCH_LEVEL.

    List removal must be such that the list is ALWAYS valid, an NMI
    could occur during removal and the NMI handler must be able to
    safely transit the list.

Arguments:

    Handle  supplied an opaque handle to the callback object that was
            returned by KeRegisterNmiCallback.

Return Value:

    Returns STATUS_SUCCESS if the object was successfully removed from
    the list.   STATUS_INVALID_HANDLE otherwise.

--*/

{
    PKNMI_HANDLER_CALLBACK Handler;
    PKNMI_HANDLER_CALLBACK *PreviousNext;
    KIRQL OldIrql;

#if !defined(NT_UP)

    KAFFINITY ActiveProcessors;
    KAFFINITY CurrentAffinity;

#endif


    ASSERT(KeGetCurrentIrql() < DISPATCH_LEVEL);

    KeAcquireSpinLock(&KiNmiCallbackListLock, &OldIrql);


    //
    // Find the handler given the list of handlers.
    //
    // N.B. In the current implementation, the handle is the address
    // of the handler however this code is designed for a more opaque
    // handle.
    //

    PreviousNext = &KiNmiCallbackListHead;

    for (Handler = *PreviousNext;
         Handler;
         PreviousNext = &Handler->Next, Handler = Handler->Next) {

        if (Handler->Handle == Handle) {
            ASSERT(Handle == Handler);
            break;
        }
    }

    if ((Handler == NULL) || (Handler->Handle != Handle)) {
        KeReleaseSpinLock(&KiNmiCallbackListLock, OldIrql);
        return STATUS_INVALID_HANDLE;
    }

    //
    // Remove this handler from the list.
    //

    *PreviousNext = Handler->Next;

    KeReleaseSpinLock(&KiNmiCallbackListLock, OldIrql);

    //
    // Cycle through each processor in the system to ensure that any
    // NMI which has begun execution on another processor has completed
    // execution before releasing the memory for the NMI callback object.
    //

#if !defined(NT_UP)

    ActiveProcessors = KeActiveProcessors;
    for (CurrentAffinity = 1; ActiveProcessors; CurrentAffinity <<= 1) {

        if (ActiveProcessors & CurrentAffinity) {
            ActiveProcessors &= ~CurrentAffinity;

            KeSetSystemAffinityThread(CurrentAffinity);
        }
    }
    KeRevertToUserAffinityThread();

#endif

    ExFreePoolWithTag(Handler, 'INMK');
    return STATUS_SUCCESS;
}

