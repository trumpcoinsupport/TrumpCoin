// Copyright (c) 2014-2015 The Dash developers
// Copyright (c) 2015-2020 The TrumpCoin developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

// clang-format off
#include "activepatriotnode.h"
#include "budget/budgetmanager.h"
#include "evo/deterministicmns.h"
#include "patriotnode-sync.h"
#include "patriotnode-payments.h"
#include "patriotnode.h"
#include "patriotnodeman.h"
#include "netmessagemaker.h"
#include "spork.h"
#include "util/system.h"
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

bool CPatriotnodeSync::IsSporkListSynced()
{
    return RequestedPatriotnodeAssets > PATRIOTNODE_SYNC_SPORKS;
}

bool CPatriotnodeSync::IsPatriotnodeListSynced()
{
    return RequestedPatriotnodeAssets > PATRIOTNODE_SYNC_LIST;
}

bool CPatriotnodeSync::NotCompleted()
{
    return (!IsSynced() && (
            !IsSporkListSynced() ||
            sporkManager.IsSporkActive(SPORK_8_PATRIOTNODE_PAYMENT_ENFORCEMENT) ||
            sporkManager.IsSporkActive(SPORK_9_PATRIOTNODE_BUDGET_ENFORCEMENT) ||
            sporkManager.IsSporkActive(SPORK_13_ENABLE_SUPERBLOCKS)));
}

bool CPatriotnodeSync::IsBlockchainSynced()
{
    int64_t now = GetTime();

    // if the last call to this function was more than 60 minutes ago (client was in sleep mode) reset the sync process
    if (now > lastProcess + 60 * 60) {
        Reset();
        fBlockchainSynced = false;
    }
    lastProcess = now;

    if (fBlockchainSynced) return true;

    if (fImporting || fReindex) return false;

    int64_t blockTime = 0;
    {
        TRY_LOCK(g_best_block_mutex, lock);
        if (!lock) return false;
        blockTime = g_best_block_time;
    }

    if (blockTime + 60 * 60 < lastProcess)
        return false;

    fBlockchainSynced = true;

    return true;
}

bool CPatriotnodeSync::IsBlockchainSyncedReadOnly() const
{
    return fBlockchainSynced;
}

void CPatriotnodeSync::Reset()
{
    fBlockchainSynced = false;
    lastProcess = 0;
    lastPatriotnodeList = 0;
    lastPatriotnodeWinner = 0;
    lastBudgetItem = 0;
    mapSeenSyncPNB.clear();
    mapSeenSyncPNW.clear();
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

void CPatriotnodeSync::AddedPatriotnodeList(const uint256& hash)
{
    if (mnodeman.mapSeenPatriotnodeBroadcast.count(hash)) {
        if (mapSeenSyncPNB[hash] < PATRIOTNODE_SYNC_THRESHOLD) {
            lastPatriotnodeList = GetTime();
            mapSeenSyncPNB[hash]++;
        }
    } else {
        lastPatriotnodeList = GetTime();
        mapSeenSyncPNB.emplace(hash, 1);
    }
}

void CPatriotnodeSync::AddedPatriotnodeWinner(const uint256& hash)
{
    if (patriotnodePayments.mapPatriotnodePayeeVotes.count(hash)) {
        if (mapSeenSyncPNW[hash] < PATRIOTNODE_SYNC_THRESHOLD) {
            lastPatriotnodeWinner = GetTime();
            mapSeenSyncPNW[hash]++;
        }
    } else {
        lastPatriotnodeWinner = GetTime();
        mapSeenSyncPNW.emplace(hash, 1);
    }
}

void CPatriotnodeSync::AddedBudgetItem(const uint256& hash)
{
    if (g_budgetman.HaveProposal(hash) ||
            g_budgetman.HaveSeenProposalVote(hash) ||
            g_budgetman.HaveFinalizedBudget(hash) ||
            g_budgetman.HaveSeenFinalizedBudgetVote(hash)) {
        if (mapSeenSyncBudget[hash] < PATRIOTNODE_SYNC_THRESHOLD) {
            lastBudgetItem = GetTime();
            mapSeenSyncBudget[hash]++;
        }
    } else {
        lastBudgetItem = GetTime();
        mapSeenSyncBudget.emplace(hash, 1);
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

int CPatriotnodeSync::GetNextAsset(int currentAsset)
{
    if (currentAsset > PATRIOTNODE_SYNC_FINISHED) {
        LogPrintf("%s - invalid asset %d\n", __func__, currentAsset);
        return PATRIOTNODE_SYNC_FAILED;
    }
    switch (currentAsset) {
    case (PATRIOTNODE_SYNC_INITIAL):
    case (PATRIOTNODE_SYNC_FAILED):
        return PATRIOTNODE_SYNC_SPORKS;
    case (PATRIOTNODE_SYNC_SPORKS):
        return deterministicPNManager->LegacyPNObsolete() ? PATRIOTNODE_SYNC_BUDGET : PATRIOTNODE_SYNC_LIST;
    case (PATRIOTNODE_SYNC_LIST):
        return deterministicPNManager->LegacyPNObsolete() ? PATRIOTNODE_SYNC_BUDGET : PATRIOTNODE_SYNC_PNW;
    case (PATRIOTNODE_SYNC_PNW):
        return PATRIOTNODE_SYNC_BUDGET;
    case (PATRIOTNODE_SYNC_BUDGET):
    default:
        return PATRIOTNODE_SYNC_FINISHED;
    }
}

void CPatriotnodeSync::SwitchToNextAsset()
{
    if (RequestedPatriotnodeAssets == PATRIOTNODE_SYNC_INITIAL ||
            RequestedPatriotnodeAssets == PATRIOTNODE_SYNC_FAILED) {
        ClearFulfilledRequest();
    }
    const int nextAsset = GetNextAsset(RequestedPatriotnodeAssets);
    if (nextAsset == PATRIOTNODE_SYNC_FINISHED) {
        LogPrintf("%s - Sync has finished\n", __func__);
    }
    RequestedPatriotnodeAssets = nextAsset;
    RequestedPatriotnodeAttempt = 0;
    nAssetSyncStarted = GetTime();
}

std::string CPatriotnodeSync::GetSyncStatus()
{
    switch (patriotnodeSync.RequestedPatriotnodeAssets) {
    case PATRIOTNODE_SYNC_INITIAL:
        return _("PNs synchronization pending...");
    case PATRIOTNODE_SYNC_SPORKS:
        return _("Synchronizing sporks...");
    case PATRIOTNODE_SYNC_LIST:
        return _("Synchronizing patriotnodes...");
    case PATRIOTNODE_SYNC_PNW:
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
    if (strCommand == NetMsgType::SYNCSTATUSCOUNT) { //Sync status count
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
        case (PATRIOTNODE_SYNC_PNW):
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

        LogPrint(BCLog::PATRIOTNODE, "CPatriotnodeSync:ProcessMessage - ssc - got inventory count %d %d\n", nItemID, nCount);
    }
}

void CPatriotnodeSync::ClearFulfilledRequest()
{
    g_connman->ForEachNode([](CNode* pnode) {
        pnode->ClearFulfilledRequest("getspork");
        pnode->ClearFulfilledRequest("mnsync");
        pnode->ClearFulfilledRequest("mnwsync");
        pnode->ClearFulfilledRequest("busync");
    });
}

void CPatriotnodeSync::Process()
{
    static int tick = 0;
    const bool isRegTestNet = Params().IsRegTestNet();

    if (tick++ % PATRIOTNODE_SYNC_TIMEOUT != 0) return;

    if (IsSynced()) {
        /*
            Resync if we lose all patriotnodes (except the local one in case the node is a PN)
            from sleep/wake or failure to sync originally
        */
        if (mnodeman.CountEnabled() <= 1 && !isRegTestNet) {
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

    if (RequestedPatriotnodeAssets == PATRIOTNODE_SYNC_INITIAL) SwitchToNextAsset();

    // sporks synced but blockchain is not, wait until we're almost at a recent block to continue
    if (!IsBlockchainSynced() &&
        RequestedPatriotnodeAssets > PATRIOTNODE_SYNC_SPORKS) return;

    // Skip after legacy obsolete. !TODO: remove when transition to DPN is complete
    bool fLegacyMnObsolete = deterministicPNManager->LegacyPNObsolete();

    CPatriotnodeSync* sync = this;

    // New sync architecture, regtest only for now.
    if (isRegTestNet) {
        g_connman->ForEachNode([sync](CNode* pnode){
            return sync->SyncRegtest(pnode);
        });
        return;
    }

    // Mainnet sync
    g_connman->ForEachNodeInRandomOrderContinueIf([sync, fLegacyMnObsolete](CNode* pnode){
        return sync->SyncWithNode(pnode, fLegacyMnObsolete);
    });
}

bool CPatriotnodeSync::SyncWithNode(CNode* pnode, bool fLegacyMnObsolete)
{
    CNetMsgMaker msgMaker(pnode->GetSendVersion());

    //set to synced
    if (RequestedPatriotnodeAssets == PATRIOTNODE_SYNC_SPORKS) {
        if (pnode->HasFulfilledRequest("getspork")) return true;
        pnode->FulfilledRequest("getspork");

        g_connman->PushMessage(pnode, msgMaker.Make(NetMsgType::GETSPORKS)); //get current network sporks
        if (RequestedPatriotnodeAttempt >= 2) SwitchToNextAsset();
        RequestedPatriotnodeAttempt++;
        return false;
    }

    if (pnode->nVersion >= ActiveProtocol()) {
        if (RequestedPatriotnodeAssets == PATRIOTNODE_SYNC_LIST) {
            if (fLegacyMnObsolete) {
                SwitchToNextAsset();
                return false;
            }

            LogPrint(BCLog::PATRIOTNODE, "CPatriotnodeSync::Process() - lastPatriotnodeList %lld (GetTime() - PATRIOTNODE_SYNC_TIMEOUT) %lld\n", lastPatriotnodeList, GetTime() - PATRIOTNODE_SYNC_TIMEOUT);
            if (lastPatriotnodeList > 0 && lastPatriotnodeList < GetTime() - PATRIOTNODE_SYNC_TIMEOUT * 8 && RequestedPatriotnodeAttempt >= PATRIOTNODE_SYNC_THRESHOLD) {
                // hasn't received a new item in the last 40 seconds AND has sent at least a minimum of PATRIOTNODE_SYNC_THRESHOLD GETPNLIST requests,
                // so we'll move to the next asset.
                SwitchToNextAsset();
                return false;
            }

            // timeout
            if (lastPatriotnodeList == 0 &&
                (RequestedPatriotnodeAttempt >= PATRIOTNODE_SYNC_THRESHOLD * 3 || GetTime() - nAssetSyncStarted > PATRIOTNODE_SYNC_TIMEOUT * 5)) {
                if (sporkManager.IsSporkActive(SPORK_8_PATRIOTNODE_PAYMENT_ENFORCEMENT)) {
                    LogPrintf("CPatriotnodeSync::Process - ERROR - Sync has failed on %s, will retry later\n", "PATRIOTNODE_SYNC_LIST");
                    RequestedPatriotnodeAssets = PATRIOTNODE_SYNC_FAILED;
                    RequestedPatriotnodeAttempt = 0;
                    lastFailure = GetTime();
                    nCountFailures++;
                } else {
                    SwitchToNextAsset();
                }
                return false;
            }

            // Don't request mnlist initial sync to more than 8 randomly ordered peers in this round
            if (RequestedPatriotnodeAttempt >= PATRIOTNODE_SYNC_THRESHOLD * 4) return false;

            // Request mnb sync if we haven't requested it yet.
            if (pnode->HasFulfilledRequest("mnsync")) return true;

            // Try to request PN list sync.
            if (!mnodeman.RequestMnList(pnode)) {
                return true; // Failed, try next peer.
            }

            // Mark sync requested.
            pnode->FulfilledRequest("mnsync");
            // Increase the sync attempt count
            RequestedPatriotnodeAttempt++;

            return false; // sleep 1 second before do another request round.
        }

        if (RequestedPatriotnodeAssets == PATRIOTNODE_SYNC_PNW) {
            if (fLegacyMnObsolete) {
                SwitchToNextAsset();
                return false;
            }

            if (lastPatriotnodeWinner > 0 && lastPatriotnodeWinner < GetTime() - PATRIOTNODE_SYNC_TIMEOUT * 2 && RequestedPatriotnodeAttempt >= PATRIOTNODE_SYNC_THRESHOLD) { //hasn't received a new item in the last five seconds, so we'll move to the
                SwitchToNextAsset();
                return false;
            }

            // timeout
            if (lastPatriotnodeWinner == 0 &&
                (RequestedPatriotnodeAttempt >= PATRIOTNODE_SYNC_THRESHOLD * 3 || GetTime() - nAssetSyncStarted > PATRIOTNODE_SYNC_TIMEOUT * 5)) {
                if (sporkManager.IsSporkActive(SPORK_8_PATRIOTNODE_PAYMENT_ENFORCEMENT)) {
                    LogPrintf("CPatriotnodeSync::Process - ERROR - Sync has failed on %s, will retry later\n", "PATRIOTNODE_SYNC_PNW");
                    RequestedPatriotnodeAssets = PATRIOTNODE_SYNC_FAILED;
                    RequestedPatriotnodeAttempt = 0;
                    lastFailure = GetTime();
                    nCountFailures++;
                } else {
                    SwitchToNextAsset();
                }
                return false;
            }

            // Don't request mnw initial sync to more than 6 randomly ordered peers in this round.
            if (RequestedPatriotnodeAttempt >= PATRIOTNODE_SYNC_THRESHOLD * 3) return false;

            // Request mnw sync if we haven't requested it yet.
            if (pnode->HasFulfilledRequest("mnwsync")) return true;

            // Mark sync requested.
            pnode->FulfilledRequest("mnwsync");

            // Sync mn winners
            int nMnCount = mnodeman.CountEnabled();
            g_connman->PushMessage(pnode, msgMaker.Make(NetMsgType::GETPNWINNERS, nMnCount));
            RequestedPatriotnodeAttempt++;

            return false; // sleep 1 second before do another request round.
        }

        if (RequestedPatriotnodeAssets == PATRIOTNODE_SYNC_BUDGET) {
            // We'll start rejecting votes if we accidentally get set as synced too soon
            if (lastBudgetItem > 0 && lastBudgetItem < GetTime() - PATRIOTNODE_SYNC_TIMEOUT * 10 && RequestedPatriotnodeAttempt >= PATRIOTNODE_SYNC_THRESHOLD) {
                // Hasn't received a new item in the last fifty seconds and more than PATRIOTNODE_SYNC_THRESHOLD requests were sent,
                // so we'll move to the next asset
                SwitchToNextAsset();

                // Try to activate our patriotnode if possible
                activePatriotnode.ManageStatus();
                return false;
            }

            // timeout
            if (lastBudgetItem == 0 &&
                (RequestedPatriotnodeAttempt >= PATRIOTNODE_SYNC_THRESHOLD * 3 || GetTime() - nAssetSyncStarted > PATRIOTNODE_SYNC_TIMEOUT * 5)) {
                // maybe there is no budgets at all, so just finish syncing
                SwitchToNextAsset();
                activePatriotnode.ManageStatus();
                return false;
            }

            // Don't request budget initial sync to more than 6 randomly ordered peers in this round.
            if (RequestedPatriotnodeAttempt >= PATRIOTNODE_SYNC_THRESHOLD * 3) return false;

            // Request bud sync if we haven't requested it yet.
            if (pnode->HasFulfilledRequest("busync")) return true;

            // Mark sync requested.
            pnode->FulfilledRequest("busync");

            // Sync proposals, finalizations and votes
            uint256 n;
            g_connman->PushMessage(pnode, msgMaker.Make(NetMsgType::BUDGETVOTESYNC, n));
            RequestedPatriotnodeAttempt++;

            return false; // sleep 1 second before do another request round.
        }
    }

    return true;
}
