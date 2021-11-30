// Copyright (c) 2014-2015 The Dash developers
// Copyright (c) 2015-2020 The TrumpCoin developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or https://www.opensource.org/licenses/mit-license.php.

#ifndef PATRIOTNODE_SYNC_H
#define PATRIOTNODE_SYNC_H

#include "net.h"    // for NodeId
#include "uint256.h"

#include <atomic>
#include <string>
#include <map>

#define PATRIOTNODE_SYNC_INITIAL 0
#define PATRIOTNODE_SYNC_SPORKS 1
#define PATRIOTNODE_SYNC_LIST 2
#define PATRIOTNODE_SYNC_PNW 3
#define PATRIOTNODE_SYNC_BUDGET 4
#define PATRIOTNODE_SYNC_BUDGET_PROP 10
#define PATRIOTNODE_SYNC_BUDGET_FIN 11
#define PATRIOTNODE_SYNC_FAILED 998
#define PATRIOTNODE_SYNC_FINISHED 999

#define PATRIOTNODE_SYNC_TIMEOUT 5
#define PATRIOTNODE_SYNC_THRESHOLD 2

class CPatriotnodeSync;
extern CPatriotnodeSync patriotnodeSync;

struct TierTwoPeerData {
    // map of message --> last request timestamp, bool hasResponseArrived.
    std::map<const char*, std::pair<int64_t, bool>> mapMsgData;
};

//
// CPatriotnodeSync : Sync patriotnode assets in stages
//

class CPatriotnodeSync
{
public:
    std::map<uint256, int> mapSeenSyncPNB;
    std::map<uint256, int> mapSeenSyncPNW;
    std::map<uint256, int> mapSeenSyncBudget;

    int64_t lastPatriotnodeList;
    int64_t lastPatriotnodeWinner;
    int64_t lastBudgetItem;
    int64_t lastFailure;
    int nCountFailures;

    std::atomic<int64_t> lastProcess;
    std::atomic<bool> fBlockchainSynced;

    // sum of all counts
    int sumPatriotnodeList;
    int sumPatriotnodeWinner;
    int sumBudgetItemProp;
    int sumBudgetItemFin;
    // peers that reported counts
    int countPatriotnodeList;
    int countPatriotnodeWinner;
    int countBudgetItemProp;
    int countBudgetItemFin;

    // Count peers we've requested the list from
    int RequestedPatriotnodeAssets;
    int RequestedPatriotnodeAttempt;

    // Time when current patriotnode asset sync started
    int64_t nAssetSyncStarted;

    CPatriotnodeSync();

    void AddedPatriotnodeList(const uint256& hash);
    void AddedPatriotnodeWinner(const uint256& hash);
    void AddedBudgetItem(const uint256& hash);
    void SwitchToNextAsset();
    std::string GetSyncStatus();
    void ProcessMessage(CNode* pfrom, std::string& strCommand, CDataStream& vRecv);
    bool IsBudgetFinEmpty();
    bool IsBudgetPropEmpty();

    void Reset();
    void Process();
    /*
     * Process sync with a single node.
     * If it returns false, the Process() step is complete.
     * Otherwise Process() calls it again for a different node.
     */
    bool SyncWithNode(CNode* pnode, bool fLegacyMnObsolete);
    bool IsSynced();
    bool NotCompleted();
    bool IsSporkListSynced();
    bool IsPatriotnodeListSynced();
    bool IsBlockchainSynced();
    void ClearFulfilledRequest();

    bool IsBlockchainSyncedReadOnly() const;

    // Sync message dispatcher
    bool MessageDispatcher(CNode* pfrom, std::string& strCommand, CDataStream& vRecv);

private:

    // Tier two sync node state
    // map of nodeID --> TierTwoPeerData
    std::map<NodeId, TierTwoPeerData> peersSyncState;
    static int GetNextAsset(int currentAsset);

    void SyncRegtest(CNode* pnode);

    template <typename... Args>
    void RequestDataTo(CNode* pnode, const char* msg, bool forceRequest, Args&&... args);

    template <typename... Args>
    void PushMessage(CNode* pnode, const char* msg, Args&&... args);

    // update peer sync state data
    bool UpdatePeerSyncState(const NodeId& id, const char* msg, const int nextSyncStatus);

    // Check if an update is needed
    void CheckAndUpdateSyncStatus();
};

#endif
