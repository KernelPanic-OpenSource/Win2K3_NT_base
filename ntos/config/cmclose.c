/*++

Copyright (c) 1991  Microsoft Corporation

Module Name:

    cmclose.c

Abstract:

    This module contains the close object method.

Author:

    Bryan M. Willman (bryanwi) 07-Jan-92

Revision History:

--*/

#include    "cmp.h"

VOID
CmpDelayedDerefKeys(
                    PLIST_ENTRY DelayedDeref
                    );

#ifdef ALLOC_PRAGMA
#pragma alloc_text(PAGE,CmpCloseKeyObject)
#endif

VOID
CmpCloseKeyObject(
    IN PEPROCESS Process OPTIONAL,
    IN PVOID Object,
    IN ACCESS_MASK GrantedAccess,
    IN ULONG_PTR ProcessHandleCount,
    IN ULONG_PTR SystemHandleCount
    )
/*++

Routine Description:

    This routine interfaces to the NT Object Manager.  It is invoked when
    a Key object (or Key Root object) is closed.

    It's function is to do cleanup processing by waking up any notifies
    pending on the handle.  This keeps the key object from hanging around
    forever because a synchronous notify is stuck on it somewhere.

    All other cleanup, in particular, the freeing of storage, will be
    done in CmpDeleteKeyObject.

Arguments:

    Process - ignored

    Object - supplies a pointer to a KeyRoot or Key, thus -> KEY_BODY.

    GrantedAccess, ProcessHandleCount, SystemHandleCount - ignored

Return Value:

    NONE.

--*/
{
    PCM_KEY_BODY        KeyBody;
    PCM_NOTIFY_BLOCK    NotifyBlock;

    PAGED_CODE();

    UNREFERENCED_PARAMETER (Process);
    UNREFERENCED_PARAMETER (GrantedAccess);
    UNREFERENCED_PARAMETER (ProcessHandleCount);

    CmKdPrintEx((DPFLTR_CONFIG_ID,CML_POOL,"CmpCloseKeyObject: Object = %p\n", Object));

    if( SystemHandleCount > 1 ) {
        //
        // There are still has open handles on this key. Do nothing
        //
        return;
    }

    CmpLockRegistry();

    KeyBody = (PCM_KEY_BODY)Object;

    //
    // Check the type, it will be something else if we are closing a predefined
    // handle key
    //
    if (KeyBody->Type == KEY_BODY_TYPE) {
        //
        // Clean up any outstanding notifies attached to the KeyBody
        //
        if (KeyBody->NotifyBlock != NULL) {
            //
            // Post all PostBlocks waiting on the NotifyBlock
            //
            NotifyBlock = KeyBody->NotifyBlock;
            if (IsListEmpty(&(NotifyBlock->PostList)) == FALSE) {
                LIST_ENTRY          DelayedDeref;
                //
                // we need to follow the rule here the hive lock
                // otherwise we could deadlock down in CmDeleteKeyObject. We don't acquire the kcb lock, 
                // but we make sure that in subsequent places where we get the hive lock we get it before 
                // the kcb lock, ie. we follow the precedence rule below. 
                //
                // NB: the order of these locks is First the hive lock, then the kcb lock
                //
                InitializeListHead(&DelayedDeref);
                CmLockHive((PCMHIVE)(KeyBody->KeyControlBlock->KeyHive));
                CmpPostNotify(NotifyBlock,
                              NULL,
                              0,
                              STATUS_NOTIFY_CLEANUP,
                              &DelayedDeref
#ifdef CM_NOTIFY_CHANGED_KCB_FULLPATH  
                              ,
                              NULL
#endif //CM_NOTIFY_CHANGED_KCB_FULLPATH  
                              );
                CmUnlockHive((PCMHIVE)(KeyBody->KeyControlBlock->KeyHive));
                //
                // finish the job started in CmpPostNotify (i.e. dereference the keybodies
                // we prevented. this may cause some notifyblocks to be freed
                //
                CmpDelayedDerefKeys(&DelayedDeref);
            }
        }
    }

    CmpUnlockRegistry();
    return;
}
