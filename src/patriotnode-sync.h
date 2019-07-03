// Copyright (c) 2014-2015 The Dash developers
// Copyright (c) 2015-2017 The PIVX developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef PATRIOTNODE_SYNC_H
#define PATRIOTNODE_SYNC_H

#define PATRIOTNODE_SYNC_INITIAL 0
#define PATRIOTNODE_SYNC_SPORKS 1
#define PATRIOTNODE_SYNC_LIST 2
#define PATRIOTNODE_SYNC_MNW 3
#define PATRIOTNODE_SYNC_BUDGET 4
#define PATRIOTNODE_SYNC_BUDGET_PROP 10
#define PATRIOTNODE_SYNC_BUDGET_FIN 11
#define PATRIOTNODE_SYNC_FAILED 998
#define PATRIOTNODE_SYNC_FINISHED 999

#define PATRIOTNODE_SYNC_TIMEOUT 5
#define PATRIOTNODE_SYNC_THRESHOLD 2

class CPatriotnodeSync;
extern CPatriotnodeSync patriotnodeSync;

//
// CPatriotnodeSync : Sync patriotnode assets in stages
//

class CPatriotnodeSync
{
public:
    std::map<uint256, int> mapSeenSyncMNB;
    std::map<uint256, int> mapSeenSyncMNW;
    std::map<uint256, int> mapSeenSyncBudget;

    int64_t lastPatriotnodeList;
    int64_t lastPatriotnodeWinner;
    int64_t lastBudgetItem;
    int64_t lastFailure;
    int nCountFailures;

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

    void AddedPatriotnodeList(uint256 hash);
    void AddedPatriotnodeWinner(uint256 hash);
    void AddedBudgetItem(uint256 hash);
    void GetNextAsset();
    std::string GetSyncStatus();
    void ProcessMessage(CNode* pfrom, std::string& strCommand, CDataStream& vRecv);
    bool IsBudgetFinEmpty();
    bool IsBudgetPropEmpty();

    void Reset();
    void Process();
    bool IsSynced();
    bool IsBlockchainSynced();
    bool IsPatriotnodeListSynced() { return RequestedPatriotnodeAssets > PATRIOTNODE_SYNC_LIST; }
    void ClearFulfilledRequest();
};

#endif
