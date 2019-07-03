// Copyright (c) 2014-2015 The Dash developers
// Copyright (c) 2015-2017 The PIVX developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

// clang-format off
#include "main.h"
#include "activepatriotnode.h"
#include "patriotnode-sync.h"
#include "patriotnode-payments.h"
#include "patriotnode-budget.h"
#include "patriotnode.h"
#include "patriotnodeman.h"
#include "spork.h"
#include "util.h"
#include "addrman.h"
// clang-format on

class CPatriotnodeSync;
CPatriotnodeSync patriotnodeSync;

CPatriotnodeSync::CPatriotnodeSync()
{
    Reset();
}

bool CPatriotnodeSync::IsSynced()
{
    return RequestedPatriotnodeAssets == PATRIOTNODE_SYNC_FINISHED;
}

bool CPatriotnodeSync::IsBlockchainSynced()
{
    static bool fBlockchainSynced = false;
    static int64_t lastProcess = GetTime();

    // if the last call to this function was more than 60 minutes ago (client was in sleep mode) reset the sync process
    if (GetTime() - lastProcess > 60 * 60) {
        Reset();
        fBlockchainSynced = false;
    }
    lastProcess = GetTime();

    if (fBlockchainSynced) return true;

    if (fImporting || fReindex) return false;

    TRY_LOCK(cs_main, lockMain);
    if (!lockMain) return false;

    CBlockIndex* pindex = chainActive.Tip();
    if (pindex == NULL) return false;


    if (pindex->nTime + 60 * 60 < GetTime())
        return false;

    fBlockchainSynced = true;

    return true;
}

void CPatriotnodeSync::Reset()
{
    lastPatriotnodeList = 0;
    lastPatriotnodeWinner = 0;
    lastBudgetItem = 0;
    mapSeenSyncMNB.clear();
    mapSeenSyncMNW.clear();
    mapSeenSyncBudget.clear();
    lastFailure = 0;
    nCountFailures = 0;
    sumPatriotnodeList = 0;
    sumPatriotnodeWinner = 0;
    sumBudgetItemProp = 0;
    sumBudgetItemFin = 0;
    countPatriotnodeList = 0;
    countPatriotnodeWinner = 0;
    countBudgetItemProp = 0;
    countBudgetItemFin = 0;
    RequestedPatriotnodeAssets = PATRIOTNODE_SYNC_INITIAL;
    RequestedPatriotnodeAttempt = 0;
    nAssetSyncStarted = GetTime();
}

void CPatriotnodeSync::AddedPatriotnodeList(uint256 hash)
{
    if (mnodeman.mapSeenPatriotnodeBroadcast.count(hash)) {
        if (mapSeenSyncMNB[hash] < PATRIOTNODE_SYNC_THRESHOLD) {
            lastPatriotnodeList = GetTime();
            mapSeenSyncMNB[hash]++;
        }
    } else {
        lastPatriotnodeList = GetTime();
        mapSeenSyncMNB.insert(make_pair(hash, 1));
    }
}

void CPatriotnodeSync::AddedPatriotnodeWinner(uint256 hash)
{
    if (patriotnodePayments.mapPatriotnodePayeeVotes.count(hash)) {
        if (mapSeenSyncMNW[hash] < PATRIOTNODE_SYNC_THRESHOLD) {
            lastPatriotnodeWinner = GetTime();
            mapSeenSyncMNW[hash]++;
        }
    } else {
        lastPatriotnodeWinner = GetTime();
        mapSeenSyncMNW.insert(make_pair(hash, 1));
    }
}

void CPatriotnodeSync::AddedBudgetItem(uint256 hash)
{
    if (budget.mapSeenPatriotnodeBudgetProposals.count(hash) || budget.mapSeenPatriotnodeBudgetVotes.count(hash) ||
        budget.mapSeenFinalizedBudgets.count(hash) || budget.mapSeenFinalizedBudgetVotes.count(hash)) {
        if (mapSeenSyncBudget[hash] < PATRIOTNODE_SYNC_THRESHOLD) {
            lastBudgetItem = GetTime();
            mapSeenSyncBudget[hash]++;
        }
    } else {
        lastBudgetItem = GetTime();
        mapSeenSyncBudget.insert(make_pair(hash, 1));
    }
}

bool CPatriotnodeSync::IsBudgetPropEmpty()
{
    return sumBudgetItemProp == 0 && countBudgetItemProp > 0;
}

bool CPatriotnodeSync::IsBudgetFinEmpty()
{
    return sumBudgetItemFin == 0 && countBudgetItemFin > 0;
}

void CPatriotnodeSync::GetNextAsset()
{
    switch (RequestedPatriotnodeAssets) {
    case (PATRIOTNODE_SYNC_INITIAL):
    case (PATRIOTNODE_SYNC_FAILED): // should never be used here actually, use Reset() instead
        ClearFulfilledRequest();
        RequestedPatriotnodeAssets = PATRIOTNODE_SYNC_SPORKS;
        break;
    case (PATRIOTNODE_SYNC_SPORKS):
        RequestedPatriotnodeAssets = PATRIOTNODE_SYNC_LIST;
        break;
    case (PATRIOTNODE_SYNC_LIST):
        RequestedPatriotnodeAssets = PATRIOTNODE_SYNC_MNW;
        break;
    case (PATRIOTNODE_SYNC_MNW):
        RequestedPatriotnodeAssets = PATRIOTNODE_SYNC_BUDGET;
        break;
    case (PATRIOTNODE_SYNC_BUDGET):
        LogPrintf("CPatriotnodeSync::GetNextAsset - Sync has finished\n");
        RequestedPatriotnodeAssets = PATRIOTNODE_SYNC_FINISHED;
        break;
    }
    RequestedPatriotnodeAttempt = 0;
    nAssetSyncStarted = GetTime();
}

std::string CPatriotnodeSync::GetSyncStatus()
{
    switch (patriotnodeSync.RequestedPatriotnodeAssets) {
    case PATRIOTNODE_SYNC_INITIAL:
        return _("Synchronization pending...");
    case PATRIOTNODE_SYNC_SPORKS:
        return _("Synchronizing sporks...");
    case PATRIOTNODE_SYNC_LIST:
        return _("Synchronizing patriotnodes...");
    case PATRIOTNODE_SYNC_MNW:
        return _("Synchronizing patriotnode winners...");
    case PATRIOTNODE_SYNC_BUDGET:
        return _("Synchronizing budgets...");
    case PATRIOTNODE_SYNC_FAILED:
        return _("Synchronization failed");
    case PATRIOTNODE_SYNC_FINISHED:
        return _("Synchronization finished");
    }
    return "";
}

void CPatriotnodeSync::ProcessMessage(CNode* pfrom, std::string& strCommand, CDataStream& vRecv)
{
    if (strCommand == "ssc") { //Sync status count
        int nItemID;
        int nCount;
        vRecv >> nItemID >> nCount;

        if (RequestedPatriotnodeAssets >= PATRIOTNODE_SYNC_FINISHED) return;

        //this means we will receive no further communication
        switch (nItemID) {
        case (PATRIOTNODE_SYNC_LIST):
            if (nItemID != RequestedPatriotnodeAssets) return;
            sumPatriotnodeList += nCount;
            countPatriotnodeList++;
            break;
        case (PATRIOTNODE_SYNC_MNW):
            if (nItemID != RequestedPatriotnodeAssets) return;
            sumPatriotnodeWinner += nCount;
            countPatriotnodeWinner++;
            break;
        case (PATRIOTNODE_SYNC_BUDGET_PROP):
            if (RequestedPatriotnodeAssets != PATRIOTNODE_SYNC_BUDGET) return;
            sumBudgetItemProp += nCount;
            countBudgetItemProp++;
            break;
        case (PATRIOTNODE_SYNC_BUDGET_FIN):
            if (RequestedPatriotnodeAssets != PATRIOTNODE_SYNC_BUDGET) return;
            sumBudgetItemFin += nCount;
            countBudgetItemFin++;
            break;
        }

        LogPrint("patriotnode", "CPatriotnodeSync:ProcessMessage - ssc - got inventory count %d %d\n", nItemID, nCount);
    }
}

void CPatriotnodeSync::ClearFulfilledRequest()
{
    TRY_LOCK(cs_vNodes, lockRecv);
    if (!lockRecv) return;

    BOOST_FOREACH (CNode* pnode, vNodes) {
        pnode->ClearFulfilledRequest("getspork");
        pnode->ClearFulfilledRequest("mnsync");
        pnode->ClearFulfilledRequest("mnwsync");
        pnode->ClearFulfilledRequest("busync");
    }
}

void CPatriotnodeSync::Process()
{
    static int tick = 0;

    if (tick++ % PATRIOTNODE_SYNC_TIMEOUT != 0) return;

    if (IsSynced()) {
        /* 
            Resync if we lose all patriotnodes from sleep/wake or failure to sync originally
        */
        if (mnodeman.CountEnabled() == 0) {
            Reset();
        } else
            return;
    }

    //try syncing again
    if (RequestedPatriotnodeAssets == PATRIOTNODE_SYNC_FAILED && lastFailure + (1 * 60) < GetTime()) {
        Reset();
    } else if (RequestedPatriotnodeAssets == PATRIOTNODE_SYNC_FAILED) {
        return;
    }

    LogPrint("patriotnode", "CPatriotnodeSync::Process() - tick %d RequestedPatriotnodeAssets %d\n", tick, RequestedPatriotnodeAssets);

    if (RequestedPatriotnodeAssets == PATRIOTNODE_SYNC_INITIAL) GetNextAsset();

    // sporks synced but blockchain is not, wait until we're almost at a recent block to continue
    if (Params().NetworkID() != CBaseChainParams::REGTEST &&
        !IsBlockchainSynced() && RequestedPatriotnodeAssets > PATRIOTNODE_SYNC_SPORKS) return;

    TRY_LOCK(cs_vNodes, lockRecv);
    if (!lockRecv) return;

    BOOST_FOREACH (CNode* pnode, vNodes) {
        if (Params().NetworkID() == CBaseChainParams::REGTEST) {
            if (RequestedPatriotnodeAttempt <= 2) {
                pnode->PushMessage("getsporks"); //get current network sporks
            } else if (RequestedPatriotnodeAttempt < 4) {
                mnodeman.DsegUpdate(pnode);
            } else if (RequestedPatriotnodeAttempt < 6) {
                int nMnCount = mnodeman.CountEnabled();
                pnode->PushMessage("mnget", nMnCount); //sync payees
                uint256 n = 0;
                pnode->PushMessage("mnvs", n); //sync patriotnode votes
            } else {
                RequestedPatriotnodeAssets = PATRIOTNODE_SYNC_FINISHED;
            }
            RequestedPatriotnodeAttempt++;
            return;
        }

        //set to synced
        if (RequestedPatriotnodeAssets == PATRIOTNODE_SYNC_SPORKS) {
            if (pnode->HasFulfilledRequest("getspork")) continue;
            pnode->FulfilledRequest("getspork");

            pnode->PushMessage("getsporks"); //get current network sporks
            if (RequestedPatriotnodeAttempt >= 2) GetNextAsset();
            RequestedPatriotnodeAttempt++;

            return;
        }

        if (pnode->nVersion >= patriotnodePayments.GetMinPatriotnodePaymentsProto()) {
            if (RequestedPatriotnodeAssets == PATRIOTNODE_SYNC_LIST) {
                LogPrint("patriotnode", "CPatriotnodeSync::Process() - lastPatriotnodeList %lld (GetTime() - PATRIOTNODE_SYNC_TIMEOUT) %lld\n", lastPatriotnodeList, GetTime() - PATRIOTNODE_SYNC_TIMEOUT);
                if (lastPatriotnodeList > 0 && lastPatriotnodeList < GetTime() - PATRIOTNODE_SYNC_TIMEOUT * 2 && RequestedPatriotnodeAttempt >= PATRIOTNODE_SYNC_THRESHOLD) { //hasn't received a new item in the last five seconds, so we'll move to the
                    GetNextAsset();
                    return;
                }

                if (pnode->HasFulfilledRequest("mnsync")) continue;
                pnode->FulfilledRequest("mnsync");

                // timeout
                if (lastPatriotnodeList == 0 &&
                    (RequestedPatriotnodeAttempt >= PATRIOTNODE_SYNC_THRESHOLD * 3 || GetTime() - nAssetSyncStarted > PATRIOTNODE_SYNC_TIMEOUT * 5)) {
                    if (IsSporkActive(SPORK_8_PATRIOTNODE_PAYMENT_ENFORCEMENT)) {
                        LogPrintf("CPatriotnodeSync::Process - ERROR - Sync has failed, will retry later\n");
                        RequestedPatriotnodeAssets = PATRIOTNODE_SYNC_FAILED;
                        RequestedPatriotnodeAttempt = 0;
                        lastFailure = GetTime();
                        nCountFailures++;
                    } else {
                        GetNextAsset();
                    }
                    return;
                }

                if (RequestedPatriotnodeAttempt >= PATRIOTNODE_SYNC_THRESHOLD * 3) return;

                mnodeman.DsegUpdate(pnode);
                RequestedPatriotnodeAttempt++;
                return;
            }

            if (RequestedPatriotnodeAssets == PATRIOTNODE_SYNC_MNW) {
                if (lastPatriotnodeWinner > 0 && lastPatriotnodeWinner < GetTime() - PATRIOTNODE_SYNC_TIMEOUT * 2 && RequestedPatriotnodeAttempt >= PATRIOTNODE_SYNC_THRESHOLD) { //hasn't received a new item in the last five seconds, so we'll move to the
                    GetNextAsset();
                    return;
                }

                if (pnode->HasFulfilledRequest("mnwsync")) continue;
                pnode->FulfilledRequest("mnwsync");

                // timeout
                if (lastPatriotnodeWinner == 0 &&
                    (RequestedPatriotnodeAttempt >= PATRIOTNODE_SYNC_THRESHOLD * 3 || GetTime() - nAssetSyncStarted > PATRIOTNODE_SYNC_TIMEOUT * 5)) {
                    if (IsSporkActive(SPORK_8_PATRIOTNODE_PAYMENT_ENFORCEMENT)) {
                        LogPrintf("CPatriotnodeSync::Process - ERROR - Sync has failed, will retry later\n");
                        RequestedPatriotnodeAssets = PATRIOTNODE_SYNC_FAILED;
                        RequestedPatriotnodeAttempt = 0;
                        lastFailure = GetTime();
                        nCountFailures++;
                    } else {
                        GetNextAsset();
                    }
                    return;
                }

                if (RequestedPatriotnodeAttempt >= PATRIOTNODE_SYNC_THRESHOLD * 3) return;

                CBlockIndex* pindexPrev = chainActive.Tip();
                if (pindexPrev == NULL) return;

                int nMnCount = mnodeman.CountEnabled();
                pnode->PushMessage("mnget", nMnCount); //sync payees
                RequestedPatriotnodeAttempt++;

                return;
            }
        }

        if (pnode->nVersion >= ActiveProtocol()) {
            if (RequestedPatriotnodeAssets == PATRIOTNODE_SYNC_BUDGET) {
                
                // We'll start rejecting votes if we accidentally get set as synced too soon
                if (lastBudgetItem > 0 && lastBudgetItem < GetTime() - PATRIOTNODE_SYNC_TIMEOUT * 2 && RequestedPatriotnodeAttempt >= PATRIOTNODE_SYNC_THRESHOLD) { 
                    
                    // Hasn't received a new item in the last five seconds, so we'll move to the
                    GetNextAsset();

                    // Try to activate our patriotnode if possible
                    activePatriotnode.ManageStatus();

                    return;
                }

                // timeout
                if (lastBudgetItem == 0 &&
                    (RequestedPatriotnodeAttempt >= PATRIOTNODE_SYNC_THRESHOLD * 3 || GetTime() - nAssetSyncStarted > PATRIOTNODE_SYNC_TIMEOUT * 5)) {
                    // maybe there is no budgets at all, so just finish syncing
                    GetNextAsset();
                    activePatriotnode.ManageStatus();
                    return;
                }

                if (pnode->HasFulfilledRequest("busync")) continue;
                pnode->FulfilledRequest("busync");

                if (RequestedPatriotnodeAttempt >= PATRIOTNODE_SYNC_THRESHOLD * 3) return;

                uint256 n = 0;
                pnode->PushMessage("mnvs", n); //sync patriotnode votes
                RequestedPatriotnodeAttempt++;

                return;
            }
        }
    }
}
