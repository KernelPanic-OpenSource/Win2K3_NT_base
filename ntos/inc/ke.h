/*++ BUILD Version: 0028    // Increment this if a change has global effects

Copyright (c) 1989  Microsoft Corporation

Module Name:

    ke.h

Abstract:

    This module contains the public (external) header file for the kernel.

Author:

    David N. Cutler (davec) 27-Feb-1989

Revision History:

--*/

#ifndef _KE_
#define _KE_

//
// Define the default quantum decrement values.
//

#define CLOCK_QUANTUM_DECREMENT 3
#define WAIT_QUANTUM_DECREMENT 1
#define LOCK_OWNERSHIP_QUANTUM (WAIT_QUANTUM_DECREMENT * 4)

//
// Define the default ready skip and thread quantum values.
//

#define READY_SKIP_QUANTUM 2
#define THREAD_QUANTUM (READY_SKIP_QUANTUM * CLOCK_QUANTUM_DECREMENT)

//
// Define the round trip decrement count.
//

#define ROUND_TRIP_DECREMENT_COUNT 16

//
// Performance data collection enable definitions.
//
// A definition turns on the respective data collection.
//

//#define _COLLECT_FLUSH_SINGLE_CALLDATA_ 1
//#define _COLLECT_SET_EVENT_CALLDATA_ 1
//#define _COLLECT_WAIT_SINGLE_CALLDATA_ 1

//
// Define thread switch performance data structure.
//

typedef struct _KTHREAD_SWITCH_COUNTERS {
    ULONG FindAny;
    ULONG FindIdeal;
    ULONG FindLast;
    ULONG IdleAny;
    ULONG IdleCurrent;
    ULONG IdleIdeal;
    ULONG IdleLast;
    ULONG PreemptAny;
    ULONG PreemptCurrent;
    ULONG PreemptLast;
    ULONG SwitchToIdle;
} KTHREAD_SWITCH_COUNTERS, *PKTHREAD_SWITCH_COUNTERS;

//
// Public (external) constant definitions.
//

#define BASE_PRIORITY_THRESHOLD NORMAL_BASE_PRIORITY // fast path base threshold

// begin_ntddk begin_wdm begin_ntosp

#define THREAD_WAIT_OBJECTS 3           // Builtin usable wait blocks

// end_ntddk end_wdm end_ntosp

#define EVENT_WAIT_BLOCK 2              // Builtin event pair wait block
#define SEMAPHORE_WAIT_BLOCK 2          // Builtin semaphore wait block
#define TIMER_WAIT_BLOCK 3              // Builtin timer wait block

#if (EVENT_WAIT_BLOCK != SEMAPHORE_WAIT_BLOCK)

#error "wait event and wait semaphore must use same wait block"

#endif

//
// Define timer table size.
//

#define TIMER_TABLE_SIZE 256

//
// Get APC environment of current thread.
//

#define KeGetCurrentApcEnvironment() \
    KeGetCurrentThread()->ApcStateIndex

//
// begin_ntddk begin_nthal begin_ntosp begin_ntifs
//

#if defined(_X86_)

#define PAUSE_PROCESSOR _asm { rep nop }

#else

#define PAUSE_PROCESSOR

#endif

// end_ntddk end_nthal end_ntosp end_ntifs

// begin_nthal begin_ntosp

//
// Define macro to generate an affinity mask.
//

#if defined(_NTHAL_) || defined(_NTOSP_)

#define AFFINITY_MASK(n) ((ULONG_PTR)1 << (n))

#else

#if !defined(_WIN64)

#define KiAffinityArray KiMask32Array

#endif

extern const ULONG_PTR KiAffinityArray[];

#define AFFINITY_MASK(n) (KiAffinityArray[n])

#endif

// end_nthal end_ntosp

//
// Define macro to generate priority mask.
//

extern const ULONG KiMask32Array[];

#define PRIORITY_MASK(n) (KiMask32Array[n])

//
// Define query system time macro.
//
// The following AMD64 code reads an unaligned quadword value. The quadword
// value, however, is guaranteed to be within a cache line, and therefore,
// the value will be read atomically.
//

#if defined(_AMD64_)

#define KiQuerySystemTime(CurrentTime) \
    (CurrentTime)->QuadPart = *((LONG64 volatile *)(&SharedUserData->SystemTime))

#else

#define KiQuerySystemTime(CurrentTime) \
    while (TRUE) {                                                                  \
        (CurrentTime)->HighPart = SharedUserData->SystemTime.High1Time;             \
        (CurrentTime)->LowPart = SharedUserData->SystemTime.LowPart;                \
        if ((CurrentTime)->HighPart == SharedUserData->SystemTime.High2Time) break; \
        PAUSE_PROCESSOR                                                             \
    }

#endif

#if defined(_AMD64_)

#define KiQueryLowTickCount() SharedUserData->TickCount.LowPart

#else

#define KiQueryLowTickCount() KeTickCount.LowPart

#endif

//
// Enumerated kernel types
//
// Kernel object types.
//
//  N.B. There are really two types of event objects; NotificationEvent and
//       SynchronizationEvent. The type value for a notification event is 0,
//       and that for a synchronization event 1.
//
//  N.B. There are two types of new timer objects; NotificationTimer and
//       SynchronizationTimer. The type value for a notification timer is
//       8, and that for a synchronization timer is 9. These values are
//       very carefully chosen so that the dispatcher object type AND'ed
//       with 0x7 yields 0 or 1 for event objects and the timer objects.
//

#define DISPATCHER_OBJECT_TYPE_MASK 0x7

typedef enum _KOBJECTS {
    EventNotificationObject = 0,
    EventSynchronizationObject = 1,
    MutantObject = 2,
    ProcessObject = 3,
    QueueObject = 4,
    SemaphoreObject = 5,
    ThreadObject = 6,
    Spare1Object = 7,
    TimerNotificationObject = 8,
    TimerSynchronizationObject = 9,
    Spare2Object = 10,
    Spare3Object = 11,
    Spare4Object = 12,
    Spare5Object = 13,
    Spare6Object = 14,
    Spare7Object = 15,
    Spare8Object = 16,
    Spare9Object = 17,
    ApcObject,
    DpcObject,
    DeviceQueueObject,
    EventPairObject,
    InterruptObject,
    ProfileObject,
    ThreadedDpcObject,
    MaximumKernelObject
    } KOBJECTS;

#define KOBJECT_LOCK_BIT 0x80

C_ASSERT((MaximumKernelObject & KOBJECT_LOCK_BIT) == 0);

//
// APC environments.
//

// begin_ntosp

typedef enum _KAPC_ENVIRONMENT {
    OriginalApcEnvironment,
    AttachedApcEnvironment,
    CurrentApcEnvironment,
    InsertApcEnvironment
    } KAPC_ENVIRONMENT;

// begin_ntddk begin_wdm begin_nthal begin_ntminiport begin_ntifs begin_ntndis

//
// Interrupt modes.
//

typedef enum _KINTERRUPT_MODE {
    LevelSensitive,
    Latched
    } KINTERRUPT_MODE;

// end_ntddk end_wdm end_nthal end_ntminiport end_ntifs end_ntndis end_ntosp

//
// Process states.
//

typedef enum _KPROCESS_STATE {
    ProcessInMemory,
    ProcessOutOfMemory,
    ProcessInTransition,
    ProcessOutTransition,
    ProcessInSwap,
    ProcessOutSwap
    } KPROCESS_STATE;

//
// Thread scheduling states.
//

typedef enum _KTHREAD_STATE {
    Initialized,
    Ready,
    Running,
    Standby,
    Terminated,
    Waiting,
    Transition,
    DeferredReady
    } KTHREAD_STATE;

// begin_ntddk begin_wdm begin_nthal begin_ntifs begin_ntosp
//
// Wait reasons
//

typedef enum _KWAIT_REASON {
    Executive,
    FreePage,
    PageIn,
    PoolAllocation,
    DelayExecution,
    Suspended,
    UserRequest,
    WrExecutive,
    WrFreePage,
    WrPageIn,
    WrPoolAllocation,
    WrDelayExecution,
    WrSuspended,
    WrUserRequest,
    WrEventPair,
    WrQueue,
    WrLpcReceive,
    WrLpcReply,
    WrVirtualMemory,
    WrPageOut,
    WrRendezvous,
    Spare2,
    Spare3,
    Spare4,
    Spare5,
    Spare6,
    WrKernel,
    WrResource,
    WrPushLock,
    WrMutex,
    WrQuantumEnd,
    WrDispatchInt,
    WrPreempted,
    WrYieldExecution,
    MaximumWaitReason
    } KWAIT_REASON;

// end_ntddk end_wdm end_nthal

//
// Miscellaneous type definitions
//
// APC state
//

typedef struct _KAPC_STATE {
    LIST_ENTRY ApcListHead[MaximumMode];
    struct _KPROCESS *Process;
    BOOLEAN KernelApcInProgress;
    BOOLEAN KernelApcPending;
    BOOLEAN UserApcPending;
} KAPC_STATE, *PKAPC_STATE, *RESTRICTED_POINTER PRKAPC_STATE;

// end_ntifs end_ntosp

//
// Page frame
//

typedef ULONG KPAGE_FRAME;

//
// Wait block
//
// begin_ntddk begin_wdm begin_nthal begin_ntifs begin_ntosp

typedef struct _KWAIT_BLOCK {
    LIST_ENTRY WaitListEntry;
    struct _KTHREAD *RESTRICTED_POINTER Thread;
    PVOID Object;
    struct _KWAIT_BLOCK *RESTRICTED_POINTER NextWaitBlock;
    USHORT WaitKey;
    USHORT WaitType;
} KWAIT_BLOCK, *PKWAIT_BLOCK, *RESTRICTED_POINTER PRKWAIT_BLOCK;

// end_ntddk end_wdm end_nthal end_ntifs end_ntosp

//
// System service table descriptor.
//
// N.B. A system service number has a 12-bit service table offset and a
//      3-bit service table number.
//
// N.B. Descriptor table entries must be a power of 2 in size. Currently
//      this is 16 bytes on a 32-bit system and 32 bytes on a 64-bit
//      system.
//

#define NUMBER_SERVICE_TABLES 4
#define SERVICE_NUMBER_MASK ((1 << 12) -  1)

#if defined(_WIN64)

#define SERVICE_TABLE_SHIFT (12 - 5)
#define SERVICE_TABLE_MASK (((1 << 2) - 1) << 5)
#define SERVICE_TABLE_TEST (WIN32K_SERVICE_INDEX << 5)

#else

#define SERVICE_TABLE_SHIFT (12 - 4)
#define SERVICE_TABLE_MASK (((1 << 2) - 1) << 4)
#define SERVICE_TABLE_TEST (WIN32K_SERVICE_INDEX << 4)

#endif

typedef struct _KSERVICE_TABLE_DESCRIPTOR {
    PULONG_PTR Base;
    PULONG Count;
    ULONG Limit;

#if defined(_IA64_)

    LONG TableBaseGpOffset;

#endif

    PUCHAR Number;
} KSERVICE_TABLE_DESCRIPTOR, *PKSERVICE_TABLE_DESCRIPTOR;

//
// Procedure type definitions
//
// Debug routine
//

typedef
BOOLEAN
(*PKDEBUG_ROUTINE) (
    IN PKTRAP_FRAME TrapFrame,
    IN PKEXCEPTION_FRAME ExceptionFrame,
    IN PEXCEPTION_RECORD ExceptionRecord,
    IN PCONTEXT ContextRecord,
    IN KPROCESSOR_MODE PreviousMode,
    IN BOOLEAN SecondChance
    );

BOOLEAN
KdpStub (
    IN PKTRAP_FRAME TrapFrame,
    IN PKEXCEPTION_FRAME ExceptionFrame,
    IN PEXCEPTION_RECORD ExceptionRecord,
    IN PCONTEXT ContextRecord,
    IN KPROCESSOR_MODE PreviousMode,
    IN BOOLEAN SecondChance
    );

typedef
BOOLEAN
(*PKDEBUG_SWITCH_ROUTINE) (
    IN PEXCEPTION_RECORD ExceptionRecord,
    IN PCONTEXT ContextRecord,
    IN BOOLEAN SecondChance
    );

typedef enum {
    ContinueError = FALSE,
    ContinueSuccess = TRUE,
    ContinueProcessorReselected,
    ContinueNextProcessor
} KCONTINUE_STATUS;

// begin_ntddk begin_wdm begin_nthal begin_ntifs begin_ntosp
//
// Thread start function
//

typedef
VOID
(*PKSTART_ROUTINE) (
    IN PVOID StartContext
    );

// end_ntddk end_wdm end_nthal end_ntifs end_ntosp

//
// Thread system function
//

typedef
VOID
(*PKSYSTEM_ROUTINE) (
    IN PKSTART_ROUTINE StartRoutine OPTIONAL,
    IN PVOID StartContext OPTIONAL
    );

// begin_ntddk begin_wdm begin_nthal begin_ntifs begin_ntosp
//
// Kernel object structure definitions
//

//
// Device Queue object and entry
//

typedef struct _KDEVICE_QUEUE {
    CSHORT Type;
    CSHORT Size;
    LIST_ENTRY DeviceListHead;
    KSPIN_LOCK Lock;
    BOOLEAN Busy;
} KDEVICE_QUEUE, *PKDEVICE_QUEUE, *RESTRICTED_POINTER PRKDEVICE_QUEUE;

typedef struct _KDEVICE_QUEUE_ENTRY {
    LIST_ENTRY DeviceListEntry;
    ULONG SortKey;
    BOOLEAN Inserted;
} KDEVICE_QUEUE_ENTRY, *PKDEVICE_QUEUE_ENTRY, *RESTRICTED_POINTER PRKDEVICE_QUEUE_ENTRY;

// end_ntddk end_wdm end_nthal end_ntifs end_ntosp
//
// Event pair object
//

typedef struct _KEVENT_PAIR {
    CSHORT Type;
    CSHORT Size;
    KEVENT EventLow;
    KEVENT EventHigh;
} KEVENT_PAIR, *PKEVENT_PAIR, *RESTRICTED_POINTER PRKEVENT_PAIR;

// begin_nthal begin_ntddk begin_wdm begin_ntifs begin_ntosp
//
// Define the interrupt service function type and the empty struct
// type.
//
// end_ntddk end_wdm end_ntifs end_ntosp

struct _KINTERRUPT;

// begin_ntddk begin_wdm begin_ntifs begin_ntosp

typedef
BOOLEAN
(*PKSERVICE_ROUTINE) (
    IN struct _KINTERRUPT *Interrupt,
    IN PVOID ServiceContext
    );

// end_ntddk end_wdm end_ntifs end_ntosp

//
// Interrupt object
//
// N.B. The layout of this structure cannot change. It is exported to HALs
//      to short circuit interrupt dispatch.
//

typedef struct _KINTERRUPT {
    CSHORT Type;
    CSHORT Size;
    LIST_ENTRY InterruptListEntry;
    PKSERVICE_ROUTINE ServiceRoutine;
    PVOID ServiceContext;
    KSPIN_LOCK SpinLock;
    ULONG TickCount;
    PKSPIN_LOCK ActualLock;
    PKINTERRUPT_ROUTINE DispatchAddress;
    ULONG Vector;
    KIRQL Irql;
    KIRQL SynchronizeIrql;
    BOOLEAN FloatingSave;
    BOOLEAN Connected;
    CCHAR Number;
    BOOLEAN ShareVector;
    KINTERRUPT_MODE Mode;
    ULONG ServiceCount;
    ULONG DispatchCount;

#if defined(_AMD64_)

    PKTRAP_FRAME TrapFrame;

#endif

    ULONG DispatchCode[DISPATCH_LENGTH];
} KINTERRUPT;

typedef struct _KINTERRUPT *PKINTERRUPT, *RESTRICTED_POINTER PRKINTERRUPT; // ntndis ntosp

// begin_ntifs begin_ntddk begin_wdm begin_ntosp
//
// Mutant object
//

typedef struct _KMUTANT {
    DISPATCHER_HEADER Header;
    LIST_ENTRY MutantListEntry;
    struct _KTHREAD *RESTRICTED_POINTER OwnerThread;
    BOOLEAN Abandoned;
    UCHAR ApcDisable;
} KMUTANT, *PKMUTANT, *RESTRICTED_POINTER PRKMUTANT, KMUTEX, *PKMUTEX, *RESTRICTED_POINTER PRKMUTEX;

// end_ntddk end_wdm end_ntosp
//
// Queue object
//

#define ASSERT_QUEUE(Q) ASSERT(((Q)->Header.Type & ~KOBJECT_LOCK_BIT) == QueueObject);

// begin_ntosp

typedef struct _KQUEUE {
    DISPATCHER_HEADER Header;
    LIST_ENTRY EntryListHead;
    ULONG CurrentCount;
    ULONG MaximumCount;
    LIST_ENTRY ThreadListHead;
} KQUEUE, *PKQUEUE, *RESTRICTED_POINTER PRKQUEUE;

// end_ntosp

// begin_ntddk begin_wdm begin_ntosp
//
//
// Semaphore object
//

typedef struct _KSEMAPHORE {
    DISPATCHER_HEADER Header;
    LONG Limit;
} KSEMAPHORE, *PKSEMAPHORE, *RESTRICTED_POINTER PRKSEMAPHORE;

// end_ntddk end_wdm end_nthal end_ntifs end_ntosp

#if !defined(_X86_)

//
// ALIGNMENT_EXCEPTION_TABLE is used to track alignment exceptions in
// processes that are attached to a debugger.
//

#define ALIGNMENT_RECORDS_PER_TABLE 64
#define MAXIMUM_ALIGNMENT_TABLES    16

typedef struct _ALIGNMENT_EXCEPTION_RECORD {
    PVOID ProgramCounter;
    ULONG Count;
    BOOLEAN AutoFixup;
} ALIGNMENT_EXCEPTION_RECORD, *PALIGNMENT_EXCEPTION_RECORD;

typedef struct _ALIGNMENT_EXCEPTION_TABLE *PALIGNMENT_EXCEPTION_TABLE;
typedef struct _ALIGNMENT_EXCEPTION_TABLE {
    PALIGNMENT_EXCEPTION_TABLE Next;
    ALIGNMENT_EXCEPTION_RECORD RecordArray[ ALIGNMENT_RECORDS_PER_TABLE ];
} ALIGNMENT_EXCEPTION_TABLE;

#endif

// begin_nthal
//
// Define the maximum number of nodes supported.
//
// N.B. Node number must fit in the page color field of a PFN entry.
//

#define MAXIMUM_CCNUMA_NODES    16

// end_nthal
//
// Define node structure for multinode systems.
//

#define KeGetCurrentNode() (KeGetCurrentPrcb()->ParentNode)

typedef struct _KNODE {
    KAFFINITY ProcessorMask;            // Physical & Logical CPUs
    ULONG Color;                        // Public 0 based node color
    ULONG MmShiftedColor;               // MM private shifted color
    PFN_NUMBER FreeCount[2];            // # colored pages free
    SLIST_HEADER DeadStackList;         // MM per node dead stack list
    SLIST_HEADER PfnDereferenceSListHead; // MM per node deferred PFN freelist
    PSLIST_ENTRY PfnDeferredList;       // MM per node deferred PFN list
    UCHAR Seed;                         // Ideal Processor Seed
    UCHAR NodeNumber;
    struct _flags {
        BOOLEAN Removable;              // Node can be removed
    } Flags;

} KNODE, *PKNODE;

extern PKNODE KeNodeBlock[];

//
// Process object structure definition
//

typedef struct _KPROCESS {

    //
    // The dispatch header and profile listhead are fairly infrequently
    // referenced.
    //

    DISPATCHER_HEADER Header;
    LIST_ENTRY ProfileListHead;

    //
    // The following fields are referenced during context switches.
    //

    ULONG_PTR DirectoryTableBase[2];

#if defined(_X86_)

    KGDTENTRY LdtDescriptor;
    KIDTENTRY Int21Descriptor;
    USHORT IopmOffset;
    UCHAR Iopl;
    BOOLEAN Unused;

#endif

#if defined(_AMD64_)

    USHORT IopmOffset;

#endif

#if defined(_IA64_)

    REGION_MAP_INFO ProcessRegion;
    PREGION_MAP_INFO SessionMapInfo;
    ULONG_PTR SessionParentBase;

#endif // _IA64_

    volatile KAFFINITY ActiveProcessors;

    //
    // The following fields are referenced during clock interrupts.
    //

    ULONG KernelTime;
    ULONG UserTime;

    //
    // The following fields are referenced infrequently.
    //

    LIST_ENTRY ReadyListHead;
    SINGLE_LIST_ENTRY SwapListEntry;

#if defined(_X86_)

    PVOID VdmTrapcHandler;

#else

    PVOID Reserved1;

#endif

    LIST_ENTRY ThreadListHead;
    KSPIN_LOCK ProcessLock;
    KAFFINITY Affinity;
    USHORT StackCount;
    SCHAR BasePriority;
    SCHAR ThreadQuantum;
    BOOLEAN AutoAlignment;
    UCHAR State;
    UCHAR ThreadSeed;
    BOOLEAN DisableBoost;
    UCHAR PowerState;
    BOOLEAN DisableQuantum;
    UCHAR IdealNode;
    UCHAR Spare;

#if !defined(_X86_)

    PALIGNMENT_EXCEPTION_TABLE AlignmentExceptionTable;

#endif

} KPROCESS, *PKPROCESS, *RESTRICTED_POINTER PRKPROCESS;

//
// Thread object
//

typedef enum _ADJUST_REASON {
    AdjustNone = 0,
    AdjustUnwait = 1,
    AdjustBoost = 2
} ADJUST_REASON;

typedef struct _KTHREAD {

    //
    // The dispatcher header and mutant listhead are fairly infrequently
    // referenced.
    //

    DISPATCHER_HEADER Header;
    LIST_ENTRY MutantListHead;

    //
    // The following fields are referenced during context switches and wait
    // operatings. They have been carefully laid out to get the best cache
    // hit ratios.
    //

    PVOID InitialStack;
    PVOID StackLimit;
    PVOID KernelStack;

#if defined(_IA64_)

    PVOID InitialBStore;
    PVOID BStoreLimit;
    CCHAR Number;          // must match the size of Number in KPCR
                           // set to the processor number last time
                           // this thread uses the high fp register set
                           // see KiRestoreHighFPVolatile in trap.s for details
    BOOLEAN Spare3;
    PVOID KernelBStore;

#endif

    KSPIN_LOCK ThreadLock;
    ULONG ContextSwitches;
    volatile UCHAR State;
    UCHAR NpxState;
    KIRQL WaitIrql;
    KPROCESSOR_MODE WaitMode;
    PVOID Teb;
    KAPC_STATE ApcState;
    KSPIN_LOCK ApcQueueLock;
    LONG_PTR WaitStatus;
    PRKWAIT_BLOCK WaitBlockList;
    BOOLEAN Alertable;
    BOOLEAN WaitNext;
    UCHAR WaitReason;
    SCHAR Priority;
    UCHAR EnableStackSwap;
    volatile UCHAR SwapBusy;
    BOOLEAN Alerted[MaximumMode];
    union {
        LIST_ENTRY WaitListEntry;
        SINGLE_LIST_ENTRY SwapListEntry;
    };

    PRKQUEUE Queue;
    ULONG WaitTime;
    union {
        struct {
            SHORT KernelApcDisable;
            SHORT SpecialApcDisable;
        };

        ULONG CombinedApcDisable;
    };

    KTIMER Timer;
    KWAIT_BLOCK WaitBlock[THREAD_WAIT_OBJECTS + 1];
    LIST_ENTRY QueueListEntry;

    //
    // The following fields are referenced during ready thread and wait
    // completion.
    //

    UCHAR ApcStateIndex;
    BOOLEAN ApcQueueable;
    BOOLEAN Preempted;
    BOOLEAN ProcessReadyQueue;
    BOOLEAN KernelStackResident;
    CHAR Saturation;
    UCHAR IdealProcessor;
    volatile UCHAR NextProcessor;
    SCHAR BasePriority;
    UCHAR Spare4;
    SCHAR PriorityDecrement;
    SCHAR Quantum;
    BOOLEAN SystemAffinityActive;
    CCHAR PreviousMode;
    UCHAR ResourceIndex;
    UCHAR DisableBoost;
    KAFFINITY UserAffinity;
    PKPROCESS Process;
    KAFFINITY Affinity;

    //
    // The below fields are infrequently referenced.
    //

    PVOID ServiceTable;
    PKAPC_STATE ApcStatePointer[2];
    KAPC_STATE SavedApcState;
    PVOID CallbackStack;

#if defined(_IA64_)

    PVOID CallbackBStore;

#endif

    PVOID Win32Thread;
    PKTRAP_FRAME TrapFrame;
    ULONG KernelTime;
    ULONG UserTime;
    PVOID StackBase;
    KAPC SuspendApc;
    KSEMAPHORE SuspendSemaphore;
    PVOID TlsArray;
    PVOID LegoData;
    LIST_ENTRY ThreadListEntry;
    UCHAR LargeStack;
    UCHAR PowerState;
    UCHAR NpxIrql;
    UCHAR Spare5;
    BOOLEAN AutoAlignment;
    UCHAR Iopl;
    CCHAR FreezeCount;
    CCHAR SuspendCount;
    UCHAR Spare0[1];
    UCHAR UserIdealProcessor;
    volatile UCHAR DeferredProcessor;
    UCHAR AdjustReason;
    SCHAR AdjustIncrement;
    UCHAR Spare2[3];

} KTHREAD, *PKTHREAD, *RESTRICTED_POINTER PRKTHREAD;

//
// ccNUMA supported in multiprocessor PAE and WIN64 systems only.
//

#if (defined(_WIN64) || defined(_X86PAE_)) && !defined(NT_UP)

#define KE_MULTINODE

#endif

//
// Profile object structure definition
//

typedef struct _KPROFILE {
    CSHORT Type;
    CSHORT Size;
    LIST_ENTRY ProfileListEntry;
    PKPROCESS Process;
    PVOID RangeBase;
    PVOID RangeLimit;
    ULONG BucketShift;
    PVOID Buffer;
    ULONG Segment;
    KAFFINITY Affinity;
    CSHORT Source;
    BOOLEAN Started;
} KPROFILE, *PKPROFILE, *RESTRICTED_POINTER PRKPROFILE;

//
// Kernel control object functions
//
// APC object
//

// begin_ntosp

NTKERNELAPI
VOID
KeInitializeApc (
    IN PRKAPC Apc,
    IN PRKTHREAD Thread,
    IN KAPC_ENVIRONMENT Environment,
    IN PKKERNEL_ROUTINE KernelRoutine,
    IN PKRUNDOWN_ROUTINE RundownRoutine OPTIONAL,
    IN PKNORMAL_ROUTINE NormalRoutine OPTIONAL,
    IN KPROCESSOR_MODE ProcessorMode OPTIONAL,
    IN PVOID NormalContext OPTIONAL
    );

PLIST_ENTRY
KeFlushQueueApc (
    IN PKTHREAD Thread,
    IN KPROCESSOR_MODE ProcessorMode
    );

NTKERNELAPI
BOOLEAN
KeInsertQueueApc (
    IN PRKAPC Apc,
    IN PVOID SystemArgument1,
    IN PVOID SystemArgument2,
    IN KPRIORITY Increment
    );

BOOLEAN
KeRemoveQueueApc (
    IN PKAPC Apc
    );

VOID
KeGenericCallDpc (
    IN PKDEFERRED_ROUTINE Routine,
    IN PVOID Context
    );

VOID
KeSignalCallDpcDone (
    IN PVOID SystemArgument1
    );

LOGICAL
KeSignalCallDpcSynchronize (
    IN PVOID SystemArgument2
    );

// end_ntosp

// begin_ntddk begin_wdm begin_nthal begin_ntifs begin_ntosp
//
// DPC object
//

NTKERNELAPI
VOID
KeInitializeDpc (
    IN PRKDPC Dpc,
    IN PKDEFERRED_ROUTINE DeferredRoutine,
    IN PVOID DeferredContext
    );

// end_ntddk end_wdm end_nthal end_ntifs
NTKERNELAPI
VOID
KeInitializeThreadedDpc (
    IN PRKDPC Dpc,
    IN PKDEFERRED_ROUTINE DeferredRoutine,
    IN PVOID DeferredContext
    );
// begin_ntddk begin_wdm begin_nthal begin_ntifs

NTKERNELAPI
BOOLEAN
KeInsertQueueDpc (
    IN PRKDPC Dpc,
    IN PVOID SystemArgument1,
    IN PVOID SystemArgument2
    );

NTKERNELAPI
BOOLEAN
KeRemoveQueueDpc (
    IN PRKDPC Dpc
    );

// end_wdm

NTKERNELAPI
VOID
KeSetImportanceDpc (
    IN PRKDPC Dpc,
    IN KDPC_IMPORTANCE Importance
    );

NTKERNELAPI
VOID
KeSetTargetProcessorDpc (
    IN PRKDPC Dpc,
    IN CCHAR Number
    );

// begin_wdm

NTKERNELAPI
VOID
KeFlushQueuedDpcs (
    VOID
    );

//
// Device queue object
//

NTKERNELAPI
VOID
KeInitializeDeviceQueue (
    IN PKDEVICE_QUEUE DeviceQueue
    );

NTKERNELAPI
BOOLEAN
KeInsertDeviceQueue (
    IN PKDEVICE_QUEUE DeviceQueue,
    IN PKDEVICE_QUEUE_ENTRY DeviceQueueEntry
    );

NTKERNELAPI
BOOLEAN
KeInsertByKeyDeviceQueue (
    IN PKDEVICE_QUEUE DeviceQueue,
    IN PKDEVICE_QUEUE_ENTRY DeviceQueueEntry,
    IN ULONG SortKey
    );

NTKERNELAPI
PKDEVICE_QUEUE_ENTRY
KeRemoveDeviceQueue (
    IN PKDEVICE_QUEUE DeviceQueue
    );

NTKERNELAPI
PKDEVICE_QUEUE_ENTRY
KeRemoveByKeyDeviceQueue (
    IN PKDEVICE_QUEUE DeviceQueue,
    IN ULONG SortKey
    );

NTKERNELAPI
PKDEVICE_QUEUE_ENTRY
KeRemoveByKeyDeviceQueueIfBusy (
    IN PKDEVICE_QUEUE DeviceQueue,
    IN ULONG SortKey
    );

NTKERNELAPI
BOOLEAN
KeRemoveEntryDeviceQueue (
    IN PKDEVICE_QUEUE DeviceQueue,
    IN PKDEVICE_QUEUE_ENTRY DeviceQueueEntry
    );

// end_ntddk end_wdm end_ntifs end_ntosp

//
// Interrupt object
//

NTKERNELAPI                                         
VOID                                                
KeInitializeInterrupt (                             
    IN PKINTERRUPT Interrupt,                       
    IN PKSERVICE_ROUTINE ServiceRoutine,            
    IN PVOID ServiceContext,                        
    IN PKSPIN_LOCK SpinLock OPTIONAL,               
    IN ULONG Vector,                                
    IN KIRQL Irql,                                  
    IN KIRQL SynchronizeIrql,                       
    IN KINTERRUPT_MODE InterruptMode,               
    IN BOOLEAN ShareVector,                         
    IN CCHAR ProcessorNumber,                       
    IN BOOLEAN FloatingSave                         
    );

#if defined(_AMD64_)

#define NO_INTERRUPT_SPINLOCK ((PKSPIN_LOCK)-1I64)

#endif

                                                    
NTKERNELAPI                                         
BOOLEAN                                             
KeConnectInterrupt (                                
    IN PKINTERRUPT Interrupt                        
    );                                              

// end_nthal

NTKERNELAPI
BOOLEAN
KeDisconnectInterrupt (
    IN PKINTERRUPT Interrupt
    );

// begin_ntddk begin_wdm begin_nthal begin_ntosp

NTKERNELAPI
BOOLEAN
KeSynchronizeExecution (
    IN PKINTERRUPT Interrupt,
    IN PKSYNCHRONIZE_ROUTINE SynchronizeRoutine,
    IN PVOID SynchronizeContext
    );

NTKERNELAPI
KIRQL
KeAcquireInterruptSpinLock (
    IN PKINTERRUPT Interrupt
    );

NTKERNELAPI
VOID
KeReleaseInterruptSpinLock (
    IN PKINTERRUPT Interrupt,
    IN KIRQL OldIrql
    );

// end_ntddk end_wdm end_nthal end_ntosp

//
// Profile object
//

VOID
KeInitializeProfile (
    IN PKPROFILE Profile,
    IN PKPROCESS Process OPTIONAL,
    IN PVOID RangeBase,
    IN SIZE_T RangeSize,
    IN ULONG BucketSize,
    IN ULONG Segment,
    IN KPROFILE_SOURCE ProfileSource,
    IN KAFFINITY Affinity
    );

BOOLEAN
KeStartProfile (
    IN PKPROFILE Profile,
    IN PULONG Buffer
    );

BOOLEAN
KeStopProfile (
    IN PKPROFILE Profile
    );

VOID
KeSetIntervalProfile (
    IN ULONG Interval,
    IN KPROFILE_SOURCE Source
    );

ULONG
KeQueryIntervalProfile (
    IN KPROFILE_SOURCE Source
    );

// begin_ntddk begin_wdm begin_nthal begin_ntifs begin_ntosp
//
// Kernel dispatcher object functions
//
// Event Object
//

// end_wdm end_ntddk end_nthal end_ntifs end_ntosp

#if defined(_NTDRIVER_) || defined(_NTDDK_) || defined(_NTIFS_) || defined(_NTHAL_)

// begin_wdm begin_ntddk begin_nthal begin_ntifs begin_ntosp

NTKERNELAPI
VOID
KeInitializeEvent (
    IN PRKEVENT Event,
    IN EVENT_TYPE Type,
    IN BOOLEAN State
    );

NTKERNELAPI
VOID
KeClearEvent (
    IN PRKEVENT Event
    );

// end_wdm end_ntddk end_nthal end_ntifs end_ntosp

#else

#define KeInitializeEvent(_Event, _Type, _State)            \
    (_Event)->Header.Type = (UCHAR)_Type;                   \
    (_Event)->Header.Size =  sizeof(KEVENT) / sizeof(LONG); \
    (_Event)->Header.SignalState = _State;                  \
    InitializeListHead(&(_Event)->Header.WaitListHead)

#define KeClearEvent(Event) (Event)->Header.SignalState = 0

#endif

// begin_ntddk begin_ntifs begin_ntosp

NTKERNELAPI
LONG
KePulseEvent (
    IN PRKEVENT Event,
    IN KPRIORITY Increment,
    IN BOOLEAN Wait
    );

// end_ntddk end_ntifs end_ntosp

// begin_ntddk begin_wdm begin_nthal begin_ntifs begin_ntosp

NTKERNELAPI
LONG
KeReadStateEvent (
    IN PRKEVENT Event
    );

NTKERNELAPI
LONG
KeResetEvent (
    IN PRKEVENT Event
    );

NTKERNELAPI
LONG
KeSetEvent (
    IN PRKEVENT Event,
    IN KPRIORITY Increment,
    IN BOOLEAN Wait
    );

// end_ntddk end_wdm end_nthal end_ntifs end_ntosp

VOID
KeSetEventBoostPriority (
    IN PRKEVENT Event,
    IN PRKTHREAD *Thread OPTIONAL
    );

VOID
KeInitializeEventPair (
    IN PKEVENT_PAIR EventPair
    );

#define KeSetHighEventPair(EventPair, Increment, Wait) \
    KeSetEvent(&((EventPair)->EventHigh),              \
               Increment,                              \
               Wait)

#define KeSetLowEventPair(EventPair, Increment, Wait)  \
    KeSetEvent(&((EventPair)->EventLow),               \
               Increment,                              \
               Wait)

//
// Mutant object
//
// begin_ntifs

NTKERNELAPI
VOID
KeInitializeMutant (
    IN PRKMUTANT Mutant,
    IN BOOLEAN InitialOwner
    );

LONG
KeReadStateMutant (
    IN PRKMUTANT Mutant
    );

NTKERNELAPI
LONG
KeReleaseMutant (
    IN PRKMUTANT Mutant,
    IN KPRIORITY Increment,
    IN BOOLEAN Abandoned,
    IN BOOLEAN Wait
    );

// begin_ntddk begin_wdm begin_nthal begin_ntosp
//
// Mutex object
//

NTKERNELAPI
VOID
KeInitializeMutex (
    IN PRKMUTEX Mutex,
    IN ULONG Level
    );

NTKERNELAPI
LONG
KeReadStateMutex (
    IN PRKMUTEX Mutex
    );

NTKERNELAPI
LONG
KeReleaseMutex (
    IN PRKMUTEX Mutex,
    IN BOOLEAN Wait
    );

// end_ntddk end_wdm
//
// Queue Object.
//

NTKERNELAPI
VOID
KeInitializeQueue (
    IN PRKQUEUE Queue,
    IN ULONG Count OPTIONAL
    );

NTKERNELAPI
LONG
KeReadStateQueue (
    IN PRKQUEUE Queue
    );

NTKERNELAPI
LONG
KeInsertQueue (
    IN PRKQUEUE Queue,
    IN PLIST_ENTRY Entry
    );

NTKERNELAPI
LONG
KeInsertHeadQueue (
    IN PRKQUEUE Queue,
    IN PLIST_ENTRY Entry
    );

NTKERNELAPI
PLIST_ENTRY
KeRemoveQueue (
    IN PRKQUEUE Queue,
    IN KPROCESSOR_MODE WaitMode,
    IN PLARGE_INTEGER Timeout OPTIONAL
    );

PLIST_ENTRY
KeRundownQueue (
    IN PRKQUEUE Queue
    );

// begin_ntddk begin_wdm
//
// Semaphore object
//

NTKERNELAPI
VOID
KeInitializeSemaphore (
    IN PRKSEMAPHORE Semaphore,
    IN LONG Count,
    IN LONG Limit
    );

NTKERNELAPI
LONG
KeReadStateSemaphore (
    IN PRKSEMAPHORE Semaphore
    );

NTKERNELAPI
LONG
KeReleaseSemaphore (
    IN PRKSEMAPHORE Semaphore,
    IN KPRIORITY Increment,
    IN LONG Adjustment,
    IN BOOLEAN Wait
    );

// end_ntddk end_wdm end_nthal end_ntifs end_ntosp

//
// Process object
//

VOID
KeInitializeProcess (
    IN PRKPROCESS Process,
    IN KPRIORITY Priority,
    IN KAFFINITY Affinity,
    IN ULONG_PTR DirectoryTableBase[2],
    IN BOOLEAN Enable
    );

LOGICAL
KeForceAttachProcess (
    IN PKPROCESS Process
    );

// begin_ntifs begin_ntosp

NTKERNELAPI
VOID
KeAttachProcess (
    IN PRKPROCESS Process
    );

NTKERNELAPI
VOID
KeDetachProcess (
    VOID
    );

NTKERNELAPI
VOID
KeStackAttachProcess (
    IN PRKPROCESS PROCESS,
    OUT PRKAPC_STATE ApcState
    );

NTKERNELAPI
VOID
KeUnstackDetachProcess (
    IN PRKAPC_STATE ApcState
    );

// end_ntifs end_ntosp

#define KiIsAttachedProcess() \
    (KeGetCurrentThread()->ApcStateIndex == AttachedApcEnvironment)

#if !defined(_NTOSP_)

#define KeIsAttachedProcess() KiIsAttachedProcess()

#else

// begin_ntosp

NTKERNELAPI
BOOLEAN
KeIsAttachedProcess(
    VOID
    );

// end_ntosp

#endif

LONG
KeReadStateProcess (
    IN PRKPROCESS Process
    );

BOOLEAN
KeSetAutoAlignmentProcess (
    IN PRKPROCESS Process,
    IN BOOLEAN Enable
    );

LONG
KeSetProcess (
    IN PRKPROCESS Process,
    IN KPRIORITY Increment,
    IN BOOLEAN Wait
    );

KAFFINITY
KeSetAffinityProcess (
    IN PKPROCESS Process,
    IN KAFFINITY Affinity
    );

KPRIORITY
KeSetPriorityProcess (
    IN PKPROCESS Process,
    IN KPRIORITY BasePriority
    );

LOGICAL
KeSetDisableQuantumProcess (
    IN PKPROCESS Process,
    IN LOGICAL Disable
    );

#define KeTerminateProcess(Process) \
    (Process)->StackCount += 1;

//
// Thread object
//

NTSTATUS
KeInitializeThread (
    IN PKTHREAD Thread,
    IN PVOID KernelStack OPTIONAL,
    IN PKSYSTEM_ROUTINE SystemRoutine,
    IN PKSTART_ROUTINE StartRoutine OPTIONAL,
    IN PVOID StartContext OPTIONAL,
    IN PCONTEXT ContextFrame OPTIONAL,
    IN PVOID Teb OPTIONAL,
    IN PKPROCESS Process
    );

NTSTATUS
KeInitThread (
    IN PKTHREAD Thread,
    IN PVOID KernelStack OPTIONAL,
    IN PKSYSTEM_ROUTINE SystemRoutine,
    IN PKSTART_ROUTINE StartRoutine OPTIONAL,
    IN PVOID StartContext OPTIONAL,
    IN PCONTEXT ContextFrame OPTIONAL,
    IN PVOID Teb OPTIONAL,
    IN PKPROCESS Process
    );

VOID
KeUninitThread (
    IN PKTHREAD Thread
    );

VOID
KeStartThread (
    IN PKTHREAD Thread
    );

BOOLEAN
KeAlertThread (
    IN PKTHREAD Thread,
    IN KPROCESSOR_MODE ProcessorMode
    );

ULONG
KeAlertResumeThread (
    IN PKTHREAD Thread
    );

VOID
KeBoostPriorityThread (
    IN PKTHREAD Thread,
    IN KPRIORITY Increment
    );

// begin_ntosp

NTKERNELAPI                                         // ntddk wdm nthal ntifs
NTSTATUS                                            // ntddk wdm nthal ntifs
KeDelayExecutionThread (                            // ntddk wdm nthal ntifs
    IN KPROCESSOR_MODE WaitMode,                    // ntddk wdm nthal ntifs
    IN BOOLEAN Alertable,                           // ntddk wdm nthal ntifs
    IN PLARGE_INTEGER Interval                      // ntddk wdm nthal ntifs
    );                                              // ntddk wdm nthal ntifs
                                                    // ntddk wdm nthal ntifs
// end_ntosp

LOGICAL
KeSetDisableBoostThread (
    IN PKTHREAD Thread,
    IN LOGICAL Disable
    );

ULONG
KeForceResumeThread (
    IN PKTHREAD Thread
    );

VOID
KeFreezeAllThreads (
    VOID
    );

BOOLEAN
KeQueryAutoAlignmentThread (
    IN PKTHREAD Thread
    );

LONG
KeQueryBasePriorityThread (
    IN PKTHREAD Thread
    );

NTKERNELAPI                                         // ntddk wdm nthal ntifs
KPRIORITY                                           // ntddk wdm nthal ntifs
KeQueryPriorityThread (                             // ntddk wdm nthal ntifs
    IN PKTHREAD Thread                              // ntddk wdm nthal ntifs
    );                                              // ntddk wdm nthal ntifs
                                                    // ntddk wdm nthal ntifs
NTKERNELAPI                                         // ntddk wdm nthal ntifs
ULONG                                               // ntddk wdm nthal ntifs
KeQueryRuntimeThread (                              // ntddk wdm nthal ntifs
    IN PKTHREAD Thread,                             // ntddk wdm nthal ntifs
    OUT PULONG UserTime                             // ntddk wdm nthal ntifs
    );                                              // ntddk wdm nthal ntifs
                                                    // ntddk wdm nthal ntifs
BOOLEAN
KeReadStateThread (
    IN PKTHREAD Thread
    );

VOID
KeReadyThread (
    IN PKTHREAD Thread
    );

ULONG
KeResumeThread (
    IN PKTHREAD Thread
    );

// begin_nthal begin_ntosp

VOID
KeRevertToUserAffinityThread (
    VOID
    );

// end_nthal end_ntosp

VOID
KeRundownThread (
    VOID
    );

KAFFINITY
KeSetAffinityThread (
    IN PKTHREAD Thread,
    IN KAFFINITY Affinity
    );

// begin_nthal begin_ntosp

VOID
KeSetSystemAffinityThread (
    IN KAFFINITY Affinity
    );

// end_nthal end_ntosp

BOOLEAN
KeSetAutoAlignmentThread (
    IN PKTHREAD Thread,
    IN BOOLEAN Enable
    );

NTKERNELAPI                                         // ntddk nthal ntifs ntosp
LONG                                                // ntddk nthal ntifs ntosp
KeSetBasePriorityThread (                           // ntddk nthal ntifs ntosp
    IN PKTHREAD Thread,                             // ntddk nthal ntifs ntosp
    IN LONG Increment                               // ntddk nthal ntifs ntosp
    );                                              // ntddk nthal ntifs ntosp
                                                    // ntddk nthal ntifs ntosp

// begin_ntifs

NTKERNELAPI
UCHAR
KeSetIdealProcessorThread (
    IN PKTHREAD Thread,
    IN UCHAR Processor
    );

// begin_ntosp
NTKERNELAPI
BOOLEAN
KeSetKernelStackSwapEnable (
    IN BOOLEAN Enable
    );

// end_ntifs

NTKERNELAPI                                         // ntddk wdm nthal ntifs
KPRIORITY                                           // ntddk wdm nthal ntifs
KeSetPriorityThread (                               // ntddk wdm nthal ntifs
    IN PKTHREAD Thread,                             // ntddk wdm nthal ntifs
    IN KPRIORITY Priority                           // ntddk wdm nthal ntifs
    );                                              // ntddk wdm nthal ntifs
                                                    // ntddk wdm nthal ntifs

// end_ntosp

ULONG
KeSuspendThread (
    IN PKTHREAD
    );

NTKERNELAPI
VOID
KeTerminateThread (
    IN KPRIORITY Increment
    );

BOOLEAN
KeTestAlertThread (
    IN KPROCESSOR_MODE
    );

VOID
KeThawAllThreads (
    VOID
    );

// begin_ntddk begin_nthal begin_ntifs begin_ntosp

#if ((defined(_NTDRIVER_) || defined(_NTDDK_) || defined(_NTIFS_) ||defined(_NTHAL_)) && !defined(_NTSYSTEM_DRIVER_) || defined(_NTOSP_))

// begin_wdm

NTKERNELAPI
VOID
KeEnterCriticalRegion (
    VOID
    );

NTKERNELAPI
VOID
KeLeaveCriticalRegion (
    VOID
    );

NTKERNELAPI
BOOLEAN
KeAreApcsDisabled (
    VOID
    );

// end_wdm

#endif

// begin_wdm

//
// Timer object
//

NTKERNELAPI
VOID
KeInitializeTimer (
    IN PKTIMER Timer
    );

NTKERNELAPI
VOID
KeInitializeTimerEx (
    IN PKTIMER Timer,
    IN TIMER_TYPE Type
    );

NTKERNELAPI
BOOLEAN
KeCancelTimer (
    IN PKTIMER
    );

NTKERNELAPI
BOOLEAN
KeReadStateTimer (
    PKTIMER Timer
    );

NTKERNELAPI
BOOLEAN
KeSetTimer (
    IN PKTIMER Timer,
    IN LARGE_INTEGER DueTime,
    IN PKDPC Dpc OPTIONAL
    );

NTKERNELAPI
BOOLEAN
KeSetTimerEx (
    IN PKTIMER Timer,
    IN LARGE_INTEGER DueTime,
    IN LONG Period OPTIONAL,
    IN PKDPC Dpc OPTIONAL
    );

// end_ntddk end_nthal end_ntifs end_wdm end_ntosp

extern volatile KAFFINITY KiIdleSummary;

FORCEINLINE
BOOLEAN
KeIsSMTSetIdle (
    IN PKPRCB Prcb
    )

/*++

Routine Description:

    This routine determines whether the complete SMT set associated with the
    specified processor is idle.

Arguments:

    Prcb - Supplies a pointer to a processor control block (PRCB).

Return Value:

    If the specified SMT set is idle, then TRUE is returned. Otherwise, FALSE
    is returned.

--*/

{

#if !defined(NT_UP) && defined(_X86_)

    if ((KiIdleSummary & Prcb->MultiThreadProcessorSet) == Prcb->MultiThreadProcessorSet) {
        return TRUE;

    } else {
        return FALSE;
    }

#else

    UNREFERENCED_PARAMETER(Prcb);

    return TRUE;

#endif

}

/*++

KPROCESSOR_MODE
KeGetPreviousMode (
    VOID
    )

Routine Description:

    This function gets the threads previous mode from the trap frame


Arguments:

   None.

Return Value:

    KPROCESSOR_MODE - Previous mode for this thread.

--*/

#define KeGetPreviousMode() (KeGetCurrentThread()->PreviousMode)

/*++

KPROCESSOR_MODE
KeGetPReviousModeByThread (
    PKTHREAD xxCurrentThread
    )

Routine Description:

    This function gets the threads previous mode from the trap frame.


Arguments:

   xxCurrentThread - Current thread.

   N.B. This must be the current thread.

Return Value:

    KPROCESSOR_MODE - Previous mode for this thread.

--*/

#define KeGetPreviousModeByThread(xxCurrentThread)                          \
    (ASSERT (xxCurrentThread == KeGetCurrentThread ()),                     \
    (xxCurrentThread)->PreviousMode)

VOID
KeCheckForTimer(
    IN PVOID p,
    IN SIZE_T Size
    );

VOID
KeClearTimer (
    IN PKTIMER Timer
    );

ULONGLONG
KeQueryTimerDueTime (
    IN PKTIMER Timer
    );

//
// Wait functions
//

NTSTATUS
KiSetServerWaitClientEvent (
    IN PKEVENT SeverEvent,
    IN PKEVENT ClientEvent,
    IN ULONG WaitMode
    );

#define KeSetHighWaitLowEventPair(EventPair, WaitMode)                  \
    KiSetServerWaitClientEvent(&((EventPair)->EventHigh),               \
                               &((EventPair)->EventLow),                \
                               WaitMode)

#define KeSetLowWaitHighEventPair(EventPair, WaitMode)                  \
    KiSetServerWaitClientEvent(&((EventPair)->EventLow),                \
                               &((EventPair)->EventHigh),               \
                               WaitMode)

#define KeWaitForHighEventPair(EventPair, WaitMode, Alertable, TimeOut) \
    KeWaitForSingleObject(&((EventPair)->EventHigh),                    \
                          WrEventPair,                                  \
                          WaitMode,                                     \
                          Alertable,                                    \
                          TimeOut)

#define KeWaitForLowEventPair(EventPair, WaitMode, Alertable, TimeOut)  \
    KeWaitForSingleObject(&((EventPair)->EventLow),                     \
                          WrEventPair,                                  \
                          WaitMode,                                     \
                          Alertable,                                    \
                          TimeOut)

FORCEINLINE
VOID
KeWaitForContextSwap (
    IN PKTHREAD Thread
    )

/*++

Routine Description:

    This routine waits until context swap is idle for the specified thread.

Arguments:

    Thread - Supplies a pointer to a dispatcher object of type thread.

Return Value:

    None.

--*/

{

#if !defined(NT_UP)

    while (Thread->SwapBusy != FALSE) {
        KeYieldProcessor();
    }

#else

    UNREFERENCED_PARAMETER(Thread);

#endif

    return;
}

// begin_ntddk begin_wdm begin_nthal begin_ntifs begin_ntosp

#define KeWaitForMutexObject KeWaitForSingleObject

NTKERNELAPI
NTSTATUS
KeWaitForMultipleObjects (
    IN ULONG Count,
    IN PVOID Object[],
    IN WAIT_TYPE WaitType,
    IN KWAIT_REASON WaitReason,
    IN KPROCESSOR_MODE WaitMode,
    IN BOOLEAN Alertable,
    IN PLARGE_INTEGER Timeout OPTIONAL,
    IN PKWAIT_BLOCK WaitBlockArray OPTIONAL
    );

NTKERNELAPI
NTSTATUS
KeWaitForSingleObject (
    IN PVOID Object,
    IN KWAIT_REASON WaitReason,
    IN KPROCESSOR_MODE WaitMode,
    IN BOOLEAN Alertable,
    IN PLARGE_INTEGER Timeout OPTIONAL
    );

//
// Define interprocess interrupt generic call types.
//

typedef
ULONG_PTR
(*PKIPI_BROADCAST_WORKER)(
    IN ULONG_PTR Argument
    );

ULONG_PTR
KeIpiGenericCall (
    IN PKIPI_BROADCAST_WORKER BroadcastFunction,
    IN ULONG_PTR Context
    );

// end_ntosp end_ntddk end_wdm end_nthal end_ntifs

//
// Define internal kernel functions.
//
// N.B. These definitions are not public and are used elsewhere only under
//      very special circumstances.
//

// begin_ntddk begin_wdm begin_nthal begin_ntifs begin_ntndis begin_ntosp

//
// On X86 the following routines are defined in the HAL and imported by
// all other modules.
//

#if defined(_X86_) && !defined(_NTHAL_)

#define _DECL_HAL_KE_IMPORT  __declspec(dllimport)

#else

#define _DECL_HAL_KE_IMPORT

#endif

// end_ntddk end_wdm end_nthal end_ntifs end_ntndis end_ntosp

#if defined(NT_UP)

#define KeTestForWaitersQueuedSpinLock(Number) FALSE

#define KeAcquireQueuedSpinLockRaiseToSynch(Number) \
    KeRaiseIrqlToSynchLevel()

#define KeAcquireQueuedSpinLock(Number) \
    KeRaiseIrqlToDpcLevel()

#define KeReleaseQueuedSpinLock(Number, OldIrql) \
    KeLowerIrql(OldIrql)

#define KeTryToAcquireQueuedSpinLockRaiseToSynch(Number, OldIrql) \
    (*(OldIrql) = KeRaiseIrqlToSynchLevel(), TRUE)

#define KeTryToAcquireQueuedSpinLock(Number, OldIrql) \
    (KeRaiseIrql(DISPATCH_LEVEL, OldIrql), TRUE)

#define KeAcquireQueuedSpinLockAtDpcLevel(LockQueue)

#define KeReleaseQueuedSpinLockFromDpcLevel(LockQueue)

#define KeTryToAcquireQueuedSpinLockAtRaisedIrql(LockQueue) (TRUE)

#else // NT_UP

//
// Queued spin lock functions.
//

FORCEINLINE
LOGICAL
KeTestForWaitersQueuedSpinLock (
    IN KSPIN_LOCK_QUEUE_NUMBER Number
    )

{

    PKSPIN_LOCK Spinlock;
    PKPRCB Prcb;

    Prcb = KeGetCurrentPrcb();
    Spinlock =
        (PKSPIN_LOCK)((ULONG_PTR)Prcb->LockQueue[Number].Lock & ~(LOCK_QUEUE_WAIT | LOCK_QUEUE_OWNER));

    return (*Spinlock != 0);
}

VOID
FASTCALL
KeAcquireQueuedSpinLockAtDpcLevel (
    IN PKSPIN_LOCK_QUEUE LockQueue
    );

VOID
FASTCALL
KeReleaseQueuedSpinLockFromDpcLevel (
    IN PKSPIN_LOCK_QUEUE LockQueue
    );

LOGICAL
FASTCALL
KeTryToAcquireQueuedSpinLockAtRaisedIrql (
    IN PKSPIN_LOCK_QUEUE QueuedLock
    );

// begin_ntifs begin_ntosp

_DECL_HAL_KE_IMPORT
KIRQL
FASTCALL
KeAcquireQueuedSpinLock (
    IN KSPIN_LOCK_QUEUE_NUMBER Number
    );

_DECL_HAL_KE_IMPORT
VOID
FASTCALL
KeReleaseQueuedSpinLock (
    IN KSPIN_LOCK_QUEUE_NUMBER Number,
    IN KIRQL OldIrql
    );

_DECL_HAL_KE_IMPORT
LOGICAL
FASTCALL
KeTryToAcquireQueuedSpinLock(
    IN KSPIN_LOCK_QUEUE_NUMBER Number,
    IN PKIRQL OldIrql
    );

// end_ntifs end_ntosp

_DECL_HAL_KE_IMPORT
KIRQL
FASTCALL
KeAcquireQueuedSpinLockRaiseToSynch (
    IN KSPIN_LOCK_QUEUE_NUMBER Number
    );

_DECL_HAL_KE_IMPORT
LOGICAL
FASTCALL
KeTryToAcquireQueuedSpinLockRaiseToSynch(
    IN KSPIN_LOCK_QUEUE_NUMBER Number,
    IN PKIRQL OldIrql
    );

#endif  // NT_UP

#define KeQueuedSpinLockContext(n)  (&(KeGetCurrentPrcb()->LockQueue[n]))

//
// On Uni-processor systems there is no real Dispatcher Database Lock
// so raising to SYNCH won't help get the lock released any sooner.
//

#if defined(NT_UP)

#if defined(_X86_)

#define KiLockDispatcherDatabase(OldIrql) \
    *(OldIrql) = KeRaiseIrqlToDpcLevel()

#else

#define KiLockDispatcherDatabase(OldIrql) \
    *(OldIrql) = KeRaiseIrqlToSynchLevel()

#endif

#else   // NT_UP

#define KiLockDispatcherDatabase(OldIrql) \
    *(OldIrql) = KeAcquireQueuedSpinLockRaiseToSynch(LockQueueDispatcherLock)

#endif  // NT_UP

#if defined(NT_UP)

#define KiLockDispatcherDatabaseAtSynchLevel()
#define KiUnlockDispatcherDatabaseFromSynchLevel()

#else

#define KiLockDispatcherDatabaseAtSynchLevel() \
    KeAcquireQueuedSpinLockAtDpcLevel(&KeGetCurrentPrcb()->LockQueue[LockQueueDispatcherLock])

#define KiUnlockDispatcherDatabaseFromSynchLevel() \
    KeReleaseQueuedSpinLockFromDpcLevel(&KeGetCurrentPrcb()->LockQueue[LockQueueDispatcherLock])

#endif

VOID
FASTCALL
KiSetPriorityThread (
    IN PRKTHREAD Thread,
    IN KPRIORITY Priority
    );

// begin_ntddk begin_wdm begin_nthal begin_ntifs begin_ntndis begin_ntosp
//
// spin lock functions
//

#if defined(_X86_) && (defined(_WDMDDK_) || defined(WIN9X_COMPAT_SPINLOCK))

NTKERNELAPI
VOID
NTAPI
KeInitializeSpinLock (
    IN PKSPIN_LOCK SpinLock
    );

#else

__inline
VOID
NTAPI
KeInitializeSpinLock (
    IN PKSPIN_LOCK SpinLock
    ) 
{
    *SpinLock = 0;
}

#endif

#if defined(_X86_)

NTKERNELAPI
VOID
FASTCALL
KefAcquireSpinLockAtDpcLevel (
    IN PKSPIN_LOCK SpinLock
    );

NTKERNELAPI
VOID
FASTCALL
KefReleaseSpinLockFromDpcLevel (
    IN PKSPIN_LOCK SpinLock
    );

#define KeAcquireSpinLockAtDpcLevel(a)      KefAcquireSpinLockAtDpcLevel(a)
#define KeReleaseSpinLockFromDpcLevel(a)    KefReleaseSpinLockFromDpcLevel(a)

_DECL_HAL_KE_IMPORT
KIRQL
FASTCALL
KfAcquireSpinLock (
    IN PKSPIN_LOCK SpinLock
    );

_DECL_HAL_KE_IMPORT
VOID
FASTCALL
KfReleaseSpinLock (
    IN PKSPIN_LOCK SpinLock,
    IN KIRQL NewIrql
    );

// end_wdm end_ntddk

_DECL_HAL_KE_IMPORT
KIRQL
FASTCALL
KeAcquireSpinLockRaiseToSynch (
    IN PKSPIN_LOCK SpinLock
    );

// begin_wdm begin_ntddk

#define KeAcquireSpinLock(a,b)  *(b) = KfAcquireSpinLock(a)
#define KeReleaseSpinLock(a,b)  KfReleaseSpinLock(a,b)

NTKERNELAPI
BOOLEAN
FASTCALL
KeTestSpinLock (
    IN PKSPIN_LOCK SpinLock
    );

NTKERNELAPI
BOOLEAN
FASTCALL
KeTryToAcquireSpinLockAtDpcLevel (
    IN PKSPIN_LOCK SpinLock
    );

#else

//
// These functions are imported for IA64, ntddk, ntifs, nthal, ntosp, and wdm.
// They can be inlined for the system on AMD64.
//

#define KeAcquireSpinLock(SpinLock, OldIrql) \
    *(OldIrql) = KeAcquireSpinLockRaiseToDpc(SpinLock)

#if defined(_IA64_) || defined(_NTDRIVER_) || defined(_NTDDK_) || defined(_NTIFS_) || defined(_NTHAL_) || defined(_NTOSP_) || defined(_WDMDDK_)

// end_wdm end_ntddk

NTKERNELAPI
KIRQL
FASTCALL
KeAcquireSpinLockRaiseToSynch (
    IN PKSPIN_LOCK SpinLock
    );

// begin_wdm begin_ntddk

NTKERNELAPI
VOID
KeAcquireSpinLockAtDpcLevel (
    IN PKSPIN_LOCK SpinLock
    );

NTKERNELAPI
KIRQL
KeAcquireSpinLockRaiseToDpc (
    IN PKSPIN_LOCK SpinLock
    );

NTKERNELAPI
VOID
KeReleaseSpinLock (
    IN PKSPIN_LOCK SpinLock,
    IN KIRQL NewIrql
    );

NTKERNELAPI
VOID
KeReleaseSpinLockFromDpcLevel (
    IN PKSPIN_LOCK SpinLock
    );

NTKERNELAPI
BOOLEAN
FASTCALL
KeTestSpinLock (
    IN PKSPIN_LOCK SpinLock
    );

NTKERNELAPI
BOOLEAN
FASTCALL
KeTryToAcquireSpinLockAtDpcLevel (
    IN PKSPIN_LOCK SpinLock
    );

#else

#if defined(_AMD64_)

//
// The system version of these functions are defined in amd64.h for AMD64.
//

#endif

#endif

#endif

// end_wdm end_ntddk end_nthal end_ntifs

NTKERNELAPI
KIRQL
FASTCALL
KeAcquireSpinLockForDpc (
    IN PKSPIN_LOCK SpinLock
    );

NTKERNELAPI
VOID
FASTCALL
KeReleaseSpinLockForDpc (
    IN PKSPIN_LOCK SpinLock,
    IN KIRQL OldIrql
    );

// end_ntndis end_ntosp

#if !defined(_AMD64_)

BOOLEAN
KeTryToAcquireSpinLock (
    IN PKSPIN_LOCK SpinLock,
    OUT PKIRQL OldIrql
    );

#endif

//
// Enable and disable interrupts.
//
// begin_nthal
//

NTKERNELAPI
BOOLEAN
KeDisableInterrupts (
    VOID
    );

NTKERNELAPI
VOID
KeEnableInterrupts (
    IN BOOLEAN Enable
    );

// end_nthal

//
// Raise and lower IRQL functions.
//

#if defined(_NTDRIVER_) || defined(_NTDDK_) || defined(_NTIFS_) || defined(_NTHAL_) || !defined(_APIC_TPR_)

// begin_nthal begin_wdm begin_ntddk begin_ntifs begin_ntosp

#if defined(_X86_)

_DECL_HAL_KE_IMPORT
VOID
FASTCALL
KfLowerIrql (
    IN KIRQL NewIrql
    );

_DECL_HAL_KE_IMPORT
KIRQL
FASTCALL
KfRaiseIrql (
    IN KIRQL NewIrql
    );

// end_wdm

_DECL_HAL_KE_IMPORT
KIRQL
KeRaiseIrqlToDpcLevel(
    VOID
    );

// end_ntddk

_DECL_HAL_KE_IMPORT
KIRQL
KeRaiseIrqlToSynchLevel(
    VOID
    );

// begin_wdm begin_ntddk

#define KeLowerIrql(a)      KfLowerIrql(a)
#define KeRaiseIrql(a,b)    *(b) = KfRaiseIrql(a)

// end_wdm

// begin_wdm

#elif defined(_IA64_)

//
// These function are defined in IA64.h for the IA64 platform.
//


#elif defined(_AMD64_)

//
// These function are defined in amd64.h for the AMD64 platform.
//

#else

#error "no target architecture"

#endif

// end_nthal end_wdm end_ntddk end_ntifs end_ntosp

#else

extern PUCHAR HalpIRQLToTPR;
extern PUCHAR HalpVectorToIRQL;
#define APIC_TPR ((volatile ULONG *)0xFFFE0080)

#define KeGetCurrentIrql _KeGetCurrentIrql
#define KfLowerIrql _KfLowerIrql
#define KfRaiseIrql _KfRaiseIrql

KIRQL
FORCEINLINE
KeGetCurrentIrql (
    VOID
    )
{
    ULONG tprValue;
    KIRQL currentIrql;

    tprValue = *APIC_TPR;
    currentIrql = HalpVectorToIRQL[ tprValue / 16 ];
    return currentIrql;
}

VOID
FORCEINLINE
KfLowerIrql (
    IN KIRQL NewIrql
    )
{
    ULONG tprValue;

    ASSERT( NewIrql <= KeGetCurrentIrql() );

    tprValue = HalpIRQLToTPR[NewIrql];
    KeMemoryBarrier();
    *APIC_TPR = tprValue;
    *APIC_TPR;
    KeMemoryBarrier();
}   

KIRQL
FORCEINLINE
KfRaiseIrql (
    IN KIRQL NewIrql
    )
{
    KIRQL oldIrql;
    ULONG tprValue;

    oldIrql = KeGetCurrentIrql();
    ASSERT( NewIrql >= oldIrql );

    tprValue = HalpIRQLToTPR[NewIrql];

    KeMemoryBarrier();
    *APIC_TPR = tprValue;
    KeMemoryBarrier();

    return oldIrql;
}

KIRQL
FORCEINLINE
KeRaiseIrqlToDpcLevel (
    VOID
    )
{
    return KfRaiseIrql(DISPATCH_LEVEL);
}

KIRQL
FORCEINLINE
KeRaiseIrqlToSynchLevel (
    VOID
    )
{
    return KfRaiseIrql(SYNCH_LEVEL);
}

#define KeLowerIrql(a)      KfLowerIrql(a)
#define KeRaiseIrql(a,b)    *(b) = KfRaiseIrql(a)

#endif


// begin_ntddk begin_nthal begin_ntifs begin_ntosp
//
// Queued spin lock functions for "in stack" lock handles.
//
// The following three functions RAISE and LOWER IRQL when a queued
// in stack spin lock is acquired or released using these routines.
//

_DECL_HAL_KE_IMPORT
VOID
FASTCALL
KeAcquireInStackQueuedSpinLock (
    IN PKSPIN_LOCK SpinLock,
    IN PKLOCK_QUEUE_HANDLE LockHandle
    );

// end_ntddk end_nthal end_ntifs end_ntosp

_DECL_HAL_KE_IMPORT
VOID
FASTCALL
KeAcquireInStackQueuedSpinLockRaiseToSynch (
    IN PKSPIN_LOCK SpinLock,
    IN PKLOCK_QUEUE_HANDLE LockHandle
    );

// begin_ntddk begin_nthal begin_ntifs begin_ntosp

_DECL_HAL_KE_IMPORT
VOID
FASTCALL
KeReleaseInStackQueuedSpinLock (
    IN PKLOCK_QUEUE_HANDLE LockHandle
    );

//
// The following two functions do NOT raise or lower IRQL when a queued
// in stack spin lock is acquired or released using these functions.
//

NTKERNELAPI
VOID
FASTCALL
KeAcquireInStackQueuedSpinLockAtDpcLevel (
    IN PKSPIN_LOCK SpinLock,
    IN PKLOCK_QUEUE_HANDLE LockHandle
    );

NTKERNELAPI
VOID
FASTCALL
KeReleaseInStackQueuedSpinLockFromDpcLevel (
    IN PKLOCK_QUEUE_HANDLE LockHandle
    );

// end_ntddk end_nthal end_ntifs
//
// The following two functions conditionally raise or lower IRQL when a
// queued in-stack spin lock is acquired or released using these functions.
//

NTKERNELAPI
VOID
FASTCALL
KeAcquireInStackQueuedSpinLockForDpc (
    IN PKSPIN_LOCK SpinLock,
    IN PKLOCK_QUEUE_HANDLE LockHandle
    );

NTKERNELAPI
VOID
FASTCALL
KeReleaseInStackQueuedSpinLockForDpc (
    IN PKLOCK_QUEUE_HANDLE LockHandle
    );

// end_ntosp

//
// Initialize kernel in phase 1.
//

BOOLEAN
KeInitSystem(
    VOID
    );

VOID
KeNumaInitialize(
    VOID
    );

// begin_ntddk begin_wdm begin_nthal begin_ntifs begin_ntosp
//
// Miscellaneous kernel functions
//

typedef enum _KBUGCHECK_BUFFER_DUMP_STATE {
    BufferEmpty,
    BufferInserted,
    BufferStarted,
    BufferFinished,
    BufferIncomplete
} KBUGCHECK_BUFFER_DUMP_STATE;

typedef
VOID
(*PKBUGCHECK_CALLBACK_ROUTINE) (
    IN PVOID Buffer,
    IN ULONG Length
    );

typedef struct _KBUGCHECK_CALLBACK_RECORD {
    LIST_ENTRY Entry;
    PKBUGCHECK_CALLBACK_ROUTINE CallbackRoutine;
    PVOID Buffer;
    ULONG Length;
    PUCHAR Component;
    ULONG_PTR Checksum;
    UCHAR State;
} KBUGCHECK_CALLBACK_RECORD, *PKBUGCHECK_CALLBACK_RECORD;

#define KeInitializeCallbackRecord(CallbackRecord) \
    (CallbackRecord)->State = BufferEmpty

NTKERNELAPI
BOOLEAN
KeDeregisterBugCheckCallback (
    IN PKBUGCHECK_CALLBACK_RECORD CallbackRecord
    );

NTKERNELAPI
BOOLEAN
KeRegisterBugCheckCallback (
    IN PKBUGCHECK_CALLBACK_RECORD CallbackRecord,
    IN PKBUGCHECK_CALLBACK_ROUTINE CallbackRoutine,
    IN PVOID Buffer,
    IN ULONG Length,
    IN PUCHAR Component
    );

typedef enum _KBUGCHECK_CALLBACK_REASON {
    KbCallbackInvalid,
    KbCallbackReserved1,
    KbCallbackSecondaryDumpData,
    KbCallbackDumpIo,
} KBUGCHECK_CALLBACK_REASON;

typedef
VOID
(*PKBUGCHECK_REASON_CALLBACK_ROUTINE) (
    IN KBUGCHECK_CALLBACK_REASON Reason,
    IN struct _KBUGCHECK_REASON_CALLBACK_RECORD* Record,
    IN OUT PVOID ReasonSpecificData,
    IN ULONG ReasonSpecificDataLength
    );

typedef struct _KBUGCHECK_REASON_CALLBACK_RECORD {
    LIST_ENTRY Entry;
    PKBUGCHECK_REASON_CALLBACK_ROUTINE CallbackRoutine;
    PUCHAR Component;
    ULONG_PTR Checksum;
    KBUGCHECK_CALLBACK_REASON Reason;
    UCHAR State;
} KBUGCHECK_REASON_CALLBACK_RECORD, *PKBUGCHECK_REASON_CALLBACK_RECORD;

typedef struct _KBUGCHECK_SECONDARY_DUMP_DATA {
    IN PVOID InBuffer;
    IN ULONG InBufferLength;
    IN ULONG MaximumAllowed;
    OUT GUID Guid;
    OUT PVOID OutBuffer;
    OUT ULONG OutBufferLength;
} KBUGCHECK_SECONDARY_DUMP_DATA, *PKBUGCHECK_SECONDARY_DUMP_DATA;

typedef enum _KBUGCHECK_DUMP_IO_TYPE
{
    KbDumpIoInvalid,
    KbDumpIoHeader,
    KbDumpIoBody,
    KbDumpIoSecondaryData,
    KbDumpIoComplete
} KBUGCHECK_DUMP_IO_TYPE;

typedef struct _KBUGCHECK_DUMP_IO {
    IN ULONG64 Offset;
    IN PVOID Buffer;
    IN ULONG BufferLength;
    IN KBUGCHECK_DUMP_IO_TYPE Type;
} KBUGCHECK_DUMP_IO, *PKBUGCHECK_DUMP_IO;

NTKERNELAPI
BOOLEAN
KeDeregisterBugCheckReasonCallback (
    IN PKBUGCHECK_REASON_CALLBACK_RECORD CallbackRecord
    );

NTKERNELAPI
BOOLEAN
KeRegisterBugCheckReasonCallback (
    IN PKBUGCHECK_REASON_CALLBACK_RECORD CallbackRecord,
    IN PKBUGCHECK_REASON_CALLBACK_ROUTINE CallbackRoutine,
    IN KBUGCHECK_CALLBACK_REASON Reason,
    IN PUCHAR Component
    );

typedef
BOOLEAN
(*PNMI_CALLBACK)(
    IN PVOID    Context,
    IN BOOLEAN  Handled
    );

NTKERNELAPI
PVOID
KeRegisterNmiCallback(
    PNMI_CALLBACK   CallbackRoutine,
    PVOID           Context
    );

NTSTATUS
KeDeregisterNmiCallback(
    PVOID Handle
    );

// end_wdm

NTKERNELAPI
DECLSPEC_NORETURN
VOID
NTAPI
KeBugCheck (
    IN ULONG BugCheckCode
    );

// end_ntddk end_nthal end_ntifs end_ntosp

VOID
KeBugCheck2(
    IN ULONG BugCheckCode,
    IN ULONG_PTR BugCheckParameter1,
    IN ULONG_PTR BugCheckParameter2,
    IN ULONG_PTR BugCheckParameter3,
    IN ULONG_PTR BugCheckParameter4,
    IN PVOID SaveDataPage
    );

BOOLEAN
KeGetBugMessageText(
    IN ULONG MessageId,
    IN PANSI_STRING ReturnedString OPTIONAL
    );

// begin_ntddk begin_wdm begin_nthal begin_ntifs begin_ntosp

NTKERNELAPI
DECLSPEC_NORETURN
VOID
KeBugCheckEx(
    IN ULONG BugCheckCode,
    IN ULONG_PTR BugCheckParameter1,
    IN ULONG_PTR BugCheckParameter2,
    IN ULONG_PTR BugCheckParameter3,
    IN ULONG_PTR BugCheckParameter4
    );

// end_ntddk end_wdm end_ntifs end_ntosp

NTKERNELAPI
VOID
KeEnterKernelDebugger (
    VOID
    );

// end_nthal

typedef
PCHAR
(*PKE_BUGCHECK_UNICODE_TO_ANSI) (
    IN PUNICODE_STRING UnicodeString,
    OUT PCHAR AnsiBuffer,
    IN ULONG MaxAnsiLength
    );

VOID
KeDumpMachineState (
    IN PKPROCESSOR_STATE ProcessorState,
    IN PCHAR Buffer,
    IN PULONG_PTR BugCheckParameters,
    IN ULONG NumberOfParameters,
    IN PKE_BUGCHECK_UNICODE_TO_ANSI UnicodeToAnsiRoutine
    );

VOID
KeContextFromKframes (
    IN PKTRAP_FRAME TrapFrame,
    IN PKEXCEPTION_FRAME ExceptionFrame,
    IN OUT PCONTEXT ContextFrame
    );

VOID
KeContextToKframes (
    IN OUT PKTRAP_FRAME TrapFrame,
    IN OUT PKEXCEPTION_FRAME ExceptionFrame,
    IN PCONTEXT ContextFrame,
    IN ULONG ContextFlags,
    IN KPROCESSOR_MODE PreviousMode
    );

// begin_nthal

VOID
__cdecl
KeSaveStateForHibernate(
    IN PKPROCESSOR_STATE ProcessorState
    );

// end_nthal

VOID
KeCopyTrapDispatcher (
    VOID
    );

BOOLEAN
FASTCALL
KeInvalidAccessAllowed (
    IN PVOID TrapInformation OPTIONAL
    );

//
//  GDI TEB Batch Flush routine
//

typedef
VOID
(*PGDI_BATCHFLUSH_ROUTINE) (
    VOID
    );

//
// Find first set left in affinity mask.
//

#if defined(_WIN64)

#if defined(_AMD64_) && !defined(_X86AMD64_)

#define KeFindFirstSetLeftAffinity(Set, Member) BitScanReverse64(Member, Set)

#else

#define KeFindFirstSetLeftAffinity(Set, Member) {                      \
    ULONG _Mask_;                                                      \
    ULONG _Offset_ = 32;                                               \
    if ((_Mask_ = (ULONG)(Set >> 32)) == 0) {                          \
        _Offset_ = 0;                                                  \
        _Mask_ = (ULONG)Set;                                           \
    }                                                                  \
    KeFindFirstSetLeftMember(_Mask_, Member);                          \
    *(Member) += _Offset_;                                             \
}

#endif

#else

#define KeFindFirstSetLeftAffinity(Set, Member)                        \
    KeFindFirstSetLeftMember(Set, Member)

#endif // defined(_WIN64)

//
// Find first set left in 32-bit set.
//

#if defined(_WIN64)

#if defined(_AMD64_) && !defined(_X86AMD64_)

#define KeFindFirstSetLeftMember(Set, Member) BitScanReverse(Member, Set)

#else

extern const CCHAR KiFindFirstSetLeft[];

#define KeFindFirstSetLeftMember(Set, Member) {                        \
    ULONG _Mask;                                                       \
    ULONG _Offset = 16;                                                \
    if ((_Mask = Set >> 16) == 0) {                                    \
        _Offset = 0;                                                   \
        _Mask = Set;                                                   \
    }                                                                  \
    if (_Mask >> 8) {                                                  \
        _Offset += 8;                                                  \
    }                                                                  \
    *(Member) = KiFindFirstSetLeft[Set >> _Offset] + _Offset;          \
}

#endif

#else

FORCEINLINE
ULONG
KiFindFirstSetLeftMemberInt (
    ULONG Set
    )
{
    __asm {
        bsr eax, Set
    }
}

FORCEINLINE
void
KeFindFirstSetLeftMember (
    ULONG Set,
    PULONG Member
    )
{
    *Member = KiFindFirstSetLeftMemberInt (Set);
}

#endif

ULONG
KeFindNextRightSetAffinity (
    ULONG Number,
    KAFFINITY Set
    );

//
// Find first set right in 32-bit set.
//

extern const CCHAR KiFindFirstSetRight[];

#if defined(_X86_)

FORCEINLINE
ULONG
KeFindFirstSetRightMember (
    ULONG Set
    )
{
    __asm {
        bsf eax, Set
    }
}

#else

#define KeFindFirstSetRightMember(Set) \
    ((Set & 0xFF) ? KiFindFirstSetRight[Set & 0xFF] : \
    ((Set & 0xFF00) ? KiFindFirstSetRight[(Set >> 8) & 0xFF] + 8 : \
    ((Set & 0xFF0000) ? KiFindFirstSetRight[(Set >> 16) & 0xFF] + 16 : \
                           KiFindFirstSetRight[Set >> 24] + 24 )))
#endif

//
// TB Flush routines
//

extern volatile LONG KiTbFlushTimeStamp;

NTKERNELAPI
VOID
KeFlushEntireTb (
    IN BOOLEAN Invalid,
    IN BOOLEAN AllProcessors
    );

#if (defined(_M_IX86) || defined(_M_AMD64)) && defined(NT_UP) && \
    !defined(_NTDRIVER_) && !defined(_NTDDK_) && !defined(_NTIFS_) && !defined(_NTHAL_)

FORCEINLINE
VOID
KeFlushProcessTb (
    IN BOOLEAN AllProcessors
    )

{

    UNREFERENCED_PARAMETER(AllProcessors);

    KiFlushProcessTb();
    return;
}

FORCEINLINE
VOID
FASTCALL
KeFlushSingleTb (
    IN PVOID Virtual,
    IN BOOLEAN AllProcesors
    )

{

    UNREFERENCED_PARAMETER (AllProcesors);

#if _MSC_FULL_VER >= 13008806

#if defined(_M_AMD64)

    InvalidatePage(Virtual);

#else

    __asm {
        mov eax, Virtual
        invlpg [eax]
    }

#endif

#else

    KiFlushSingleTb(Virtual);

#endif

    return;
}

#define KeFlushMultipleTb(Number, Virtual, AllProcessors)                    \
{                                                                            \
    ULONG _Index_;                                                           \
    PVOID _VA_;                                                              \
                                                                             \
    for (_Index_ = 0; _Index_ < (Number); _Index_ += 1) {                    \
        _VA_ = (Virtual)[_Index_];                                           \
        KiFlushSingleTb(_VA_);                                               \
    }                                                                        \
}

#else

#if defined(_AMD64_) || defined(_X86_)

VOID
KeFlushProcessTb (
    IN BOOLEAN AllProcessors
    );

#else

#define KeFlushProcessTb(all) KeFlushEntireTb(FALSE, (all))

#endif

VOID
KeFlushMultipleTb (
    IN ULONG Number,
    IN PVOID *Virtual,
    IN BOOLEAN AllProcesors
    );

VOID
FASTCALL
KeFlushSingleTb (
    IN PVOID Virtual,
    IN BOOLEAN AllProcesors
    );

#endif

#if defined(_IA64_)

VOID
KeFlushMultipleTb64 (
    IN ULONG Number,
    IN PULONG_PTR Virtual,
    IN BOOLEAN AllProcesors
    );

HARDWARE_PTE
KeFlushSingleTb64 (
    IN ULONG_PTR Virtual,
    IN BOOLEAN AllProcesors
    );

#endif

// begin_nthal

BOOLEAN
KiIpiServiceRoutine (
    IN struct _KTRAP_FRAME *TrapFrame,
    IN struct _KEXCEPTION_FRAME *ExceptionFrame
    );

// end_nthal

BOOLEAN
KeFreezeExecution (
    IN PKTRAP_FRAME TrapFrame,
    IN PKEXCEPTION_FRAME ExceptionFrame
    );

KCONTINUE_STATUS
KeSwitchFrozenProcessor (
    IN ULONG ProcessorNumber
    );

VOID
KeGetNonVolatileContextPointers (
    IN PKNONVOLATILE_CONTEXT_POINTERS NonVolatileContext
    );

// begin_ntddk

#if defined(_AMD64_) || defined(_X86_)

NTKERNELAPI
BOOLEAN
KeInvalidateAllCaches (
    VOID
    );

#endif

// end_ntddk

#define DMA_READ_DCACHE_INVALIDATE 0x1              // nthal
#define DMA_READ_ICACHE_INVALIDATE 0x2              // nthal
#define DMA_WRITE_DCACHE_SNOOP 0x4                  // nthal
                                                    // nthal
NTKERNELAPI                                         // nthal
VOID                                                // nthal
KeSetDmaIoCoherency (                               // nthal
    IN ULONG Attributes                             // nthal
    );                                              // nthal
                                                    // nthal

#if defined(_AMD64_) || defined(_X86_)

NTKERNELAPI                                         // nthal
VOID                                                // nthal
KeSetProfileIrql (                                  // nthal
    IN KIRQL ProfileIrql                            // nthal
    );                                              // nthal
                                                    // nthal
#endif

#if defined(_IA64_)

ULONG
KeReadMbTimeStamp (
    VOID
    );

VOID
KeSynchronizeMemoryAccess (
    VOID
    );

#endif

//
// Interlocked read TB flush entire timestamp.
//

FORCEINLINE
ULONG
KeReadTbFlushTimeStamp (
    VOID
    )

{

#if defined(NT_UP)

    return KiTbFlushTimeStamp;

#else

    LONG Value;

    //
    // While the TB flush time stamp counter is being updated the high
    // order bit of the time stamp value is set. Otherwise, the bit is
    // clear.
    //

    KeMemoryBarrier();

    do {
    } while ((Value = KiTbFlushTimeStamp) < 0);

    return Value;

#endif

}

VOID
KeSetSystemTime (
    IN PLARGE_INTEGER NewTime,
    OUT PLARGE_INTEGER OldTime,
    IN BOOLEAN AdjustInterruptTime,
    IN PLARGE_INTEGER HalTimeToSet OPTIONAL
    );

#define SYSTEM_SERVICE_INDEX 0

// begin_ntosp

#define WIN32K_SERVICE_INDEX 1
#define IIS_SERVICE_INDEX 2

// end_ntosp

// begin_ntosp

NTKERNELAPI
BOOLEAN
KeAddSystemServiceTable(
    IN PULONG_PTR Base,
    IN PULONG Count OPTIONAL,
    IN ULONG Limit,
    IN PUCHAR Number,
    IN ULONG Index
    );

NTKERNELAPI
BOOLEAN
KeRemoveSystemServiceTable(
    IN ULONG Index
    );

// end_ntosp

// begin_ntddk begin_wdm begin_nthal begin_ntifs begin_ntosp

#if !defined(_AMD64_)

NTKERNELAPI
ULONGLONG
KeQueryInterruptTime (
    VOID
    );

NTKERNELAPI
VOID
KeQuerySystemTime (
    OUT PLARGE_INTEGER CurrentTime
    );

#endif

NTKERNELAPI
ULONG
KeQueryTimeIncrement (
    VOID
    );

NTKERNELAPI
ULONG
KeGetRecommendedSharedDataAlignment (
    VOID
    );

// end_wdm

NTKERNELAPI
KAFFINITY
KeQueryActiveProcessors (
    VOID
    );

// end_ntddk end_nthal end_ntifs end_ntosp

NTSTATUS
KeQueryLogicalProcessorInformation(
    OUT PVOID SystemInformation,
    IN ULONG SystemInformationLength,
    OUT PULONG ReturnLength
    );

PKPRCB
KeGetPrcb(
    IN ULONG ProcessorNumber
    );

BOOLEAN
KeAdjustInterruptTime (
    IN LONGLONG TimeDelta
    );

// begin_nthal

NTKERNELAPI
VOID
KeSetTimeIncrement (
    IN ULONG MaximumIncrement,
    IN ULONG MimimumIncrement
    );

// end_nthal

VOID
KeThawExecution (
    IN BOOLEAN Enable
    );

// begin_nthal begin_ntosp

//
// Define the firmware routine types
//

typedef enum _FIRMWARE_REENTRY {
    HalHaltRoutine,
    HalPowerDownRoutine,
    HalRestartRoutine,
    HalRebootRoutine,
    HalInteractiveModeRoutine,
    HalMaximumRoutine
} FIRMWARE_REENTRY, *PFIRMWARE_REENTRY;

// end_nthal end_ntosp

VOID
KeStartAllProcessors (
    VOID
    );

//
// Balance set manager thread startup function.
//

VOID
KeBalanceSetManager (
    IN PVOID Context
    );

VOID
KeSwapProcessOrStack (
    IN PVOID Context
    );

//
// User mode callback.
//

// begin_ntosp

NTKERNELAPI
NTSTATUS
KeUserModeCallback (
    IN ULONG ApiNumber,
    IN PVOID InputBuffer,
    IN ULONG InputLength,
    OUT PVOID *OutputBuffer,
    OUT PULONG OutputLength
    );

// end_ntosp

#if defined(_IA64_)

PVOID
KeSwitchKernelStack (
    IN PVOID StackBase,
    IN PVOID StackLimit,
    IN PVOID BStoreLimit
    );

#else

PVOID
KeSwitchKernelStack (
    IN PVOID StackBase,
    IN PVOID StackLimit
    );

#endif // defined(_IA64_)

NTSTATUS
KeRaiseUserException(
    IN NTSTATUS ExceptionCode
    );

// begin_nthal
//
// Find ARC configuration information function.
//

NTKERNELAPI
PCONFIGURATION_COMPONENT_DATA
KeFindConfigurationEntry (
    IN PCONFIGURATION_COMPONENT_DATA Child,
    IN CONFIGURATION_CLASS Class,
    IN CONFIGURATION_TYPE Type,
    IN PULONG Key OPTIONAL
    );

NTKERNELAPI
PCONFIGURATION_COMPONENT_DATA
KeFindConfigurationNextEntry (
    IN PCONFIGURATION_COMPONENT_DATA Child,
    IN CONFIGURATION_CLASS Class,
    IN CONFIGURATION_TYPE Type,
    IN PULONG Key OPTIONAL,
    IN PCONFIGURATION_COMPONENT_DATA *Resume
    );

// end_nthal

//
// External references to public kernel data structures
//

extern KAFFINITY KeActiveProcessors;
extern LARGE_INTEGER KeBootTime;
extern ULONGLONG KeBootTimeBias;
extern ULONG KeThreadDpcEnable;
extern ULONG KeErrorMask;
extern ULONGLONG KeInterruptTimeBias;
extern LIST_ENTRY KeBugCheckCallbackListHead;
extern LIST_ENTRY KeBugCheckReasonCallbackListHead;
extern KSPIN_LOCK KeBugCheckCallbackLock;
extern PGDI_BATCHFLUSH_ROUTINE KeGdiFlushUserBatch;
extern PLOADER_PARAMETER_BLOCK KeLoaderBlock;       // ntosp
extern ULONG KeMaximumIncrement;
extern ULONG KeMinimumIncrement;
extern NTSYSAPI CCHAR KeNumberProcessors;           // nthal ntosp
extern UCHAR KeNumberNodes;
extern USHORT KeProcessorArchitecture;
extern USHORT KeProcessorLevel;
extern USHORT KeProcessorRevision;
extern ULONG KeFeatureBits;
extern KSPIN_LOCK KiDispatcherLock;
extern ULONG KiDPCTimeout;
extern PKPRCB KiProcessorBlock[];
extern ULONG KiSpinlockTimeout;
extern ULONG KiStackProtectTime;
extern KTHREAD_SWITCH_COUNTERS KeThreadSwitchCounters;
extern ULONG KeLargestCacheLine;

#if defined(_IA64_)

VOID KiNormalSystemCall(VOID);

//
// IA64 CPL CATCHER
//

extern PVOID KeCplCatcher;

#endif

#if !defined(NT_UP)

extern ULONG KeRegisteredProcessors;
extern ULONG KeLicensedProcessors;
extern UCHAR KeProcessNodeSeed;

#endif

extern PULONG KeServiceCountTable;
extern KSERVICE_TABLE_DESCRIPTOR KeServiceDescriptorTable[NUMBER_SERVICE_TABLES];
extern KSERVICE_TABLE_DESCRIPTOR KeServiceDescriptorTableShadow[NUMBER_SERVICE_TABLES];

// begin_ntddk begin_wdm begin_nthal begin_ntifs begin_ntosp

#if defined(_IA64_)

extern volatile LARGE_INTEGER KeTickCount;

#elif defined(_X86_)

extern volatile KSYSTEM_TIME KeTickCount;

#endif

// end_ntddk end_wdm end_nthal end_ntifs end_ntosp

// begin_nthal

extern PVOID KeUserApcDispatcher;
extern PVOID KeUserCallbackDispatcher;
extern PVOID KeUserExceptionDispatcher;
extern PVOID KeRaiseUserExceptionDispatcher;
extern ULONG KeTimeAdjustment;
extern ULONG KeTimeIncrement;
extern BOOLEAN KeTimeSynchronization;

// end_nthal

// begin_ntddk begin_wdm begin_nthal begin_ntifs begin_ntosp

typedef enum _MEMORY_CACHING_TYPE_ORIG {
    MmFrameBufferCached = 2
} MEMORY_CACHING_TYPE_ORIG;

typedef enum _MEMORY_CACHING_TYPE {
    MmNonCached = FALSE,
    MmCached = TRUE,
    MmWriteCombined = MmFrameBufferCached,
    MmHardwareCoherentCached,
    MmNonCachedUnordered,       // IA64
    MmUSWCCached,
    MmMaximumCacheType
} MEMORY_CACHING_TYPE;

// end_ntddk end_wdm end_nthal end_ntifs end_ntosp

//
// Routine for setting memory type for physical address ranges.
//

#if defined(_X86_)

NTSTATUS
KeSetPhysicalCacheTypeRange (
    IN PHYSICAL_ADDRESS PhysicalAddress,
    IN ULONG NumberOfBytes,
    IN MEMORY_CACHING_TYPE CacheType
    );

#endif

//
// Routines for zeroing a physical page.
//
// These are defined as calls through a function pointer which is set to
// point at the optimal routine for this processor implementation.
//

#if defined(_X86_) || defined(_IA64_)

typedef
VOID
(FASTCALL *KE_ZERO_PAGE_ROUTINE)(
    IN PVOID PageBase,
    IN SIZE_T NumberOfBytes
    );

extern KE_ZERO_PAGE_ROUTINE KeZeroPages;
extern KE_ZERO_PAGE_ROUTINE KeZeroPagesFromIdleThread;

#else

#define KeZeroPagesFromIdleThread KeZeroPages

VOID
KeZeroPages (
    IN PVOID PageBase,
    IN SIZE_T NumberOfBytes
    );

#endif

#if defined(_IA64_)

VOID
KeEnableSessionSharing(
    PREGION_MAP_INFO SessionMapInfo,
    IN PFN_NUMBER SessionParentPage
    );

VOID
KeDetachSessionSpace(
    IN PREGION_MAP_INFO NullSessionMapInfo,
    IN PFN_NUMBER SessionParentPage
    );

VOID
KeAddSessionSpace(
    IN PKPROCESS Process,
    IN PREGION_MAP_INFO SessionMapInfo,
    IN PFN_NUMBER SessionParentPage
    );

VOID
KeAttachSessionSpace(
    IN PREGION_MAP_INFO SessionMapInfo,
    IN PFN_NUMBER SessionParentPage
    );

VOID
KeDisableSessionSharing(
    IN PREGION_MAP_INFO SessionMapInfo,
    IN PFN_NUMBER SessionParentPage
    );

NTSTATUS
KeFlushUserRseState (
    IN PKTRAP_FRAME TrapFrame
    );
VOID
KeSetLowPsrBit (
    IN UCHAR BitPosition,
    IN BOOLEAN Value
    );

#endif

//
// Verifier functions
//

NTSTATUS
KevUtilAddressToFileHeader (
    IN  PVOID Address,
    OUT UINT_PTR *OffsetIntoImage,
    OUT PUNICODE_STRING *DriverName,
    OUT BOOLEAN *InVerifierList
    );

//
// Define guarded mutex structure.
//

typedef struct _KGUARDED_MUTEX {
    LONG Count;
    PKTHREAD Owner;
    ULONG Contention;
    KEVENT Event;
    union {
        struct {
            SHORT KernelApcDisable;
            SHORT SpecialApcDisable;
        };

        ULONG CombinedApcDisable;
    };

} KGUARDED_MUTEX, *PKGUARDED_MUTEX;

#endif // _KE_
