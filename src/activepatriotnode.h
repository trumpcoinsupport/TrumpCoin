// Copyright (c) 2014-2016 The Dash developers
// Copyright (c) 2015-2020 The TrumpCoin developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef ACTIVEPATRIOTNODE_H
#define ACTIVEPATRIOTNODE_H

#include "init.h"
#include "key.h"
#include "evo/deterministicmns.h"
#include "patriotnode.h"
#include "net.h"
#include "operationresult.h"
#include "sync.h"
#include "validationinterface.h"
#include "wallet/wallet.h"

class CActiveDeterministicPatriotnodeManager;

#define ACTIVE_PATRIOTNODE_INITIAL 0 // initial state
#define ACTIVE_PATRIOTNODE_SYNC_IN_PROCESS 1
#define ACTIVE_PATRIOTNODE_NOT_CAPABLE 3
#define ACTIVE_PATRIOTNODE_STARTED 4

extern CActiveDeterministicPatriotnodeManager* activePatriotnodeManager;

struct CActivePatriotnodeInfo
{
    // Keys for the active Patriotnode
    CKeyID keyIDOperator;
    CKey keyOperator;
    // Initialized while registering Patriotnode
    uint256 proTxHash{UINT256_ZERO};
    CService service;
};

class CActiveDeterministicPatriotnodeManager : public CValidationInterface
{
public:
    enum patriotnode_state_t {
        PATRIOTNODE_WAITING_FOR_PROTX,
        PATRIOTNODE_POSE_BANNED,
        PATRIOTNODE_REMOVED,
        PATRIOTNODE_OPERATOR_KEY_CHANGED,
        PATRIOTNODE_PROTX_IP_CHANGED,
        PATRIOTNODE_READY,
        PATRIOTNODE_ERROR,
    };

private:
    patriotnode_state_t state{PATRIOTNODE_WAITING_FOR_PROTX};
    std::string strError;
    CActivePatriotnodeInfo info;

public:
    virtual ~CActiveDeterministicPatriotnodeManager() = default;
    virtual void UpdatedBlockTip(const CBlockIndex* pindexNew, const CBlockIndex* pindexFork, bool fInitialDownload);

    void Init();
    void Reset(patriotnode_state_t _state);
    // Sets the Deterministic Patriotnode Operator's private/public key
    OperationResult SetOperatorKey(const std::string& strPNOperatorPrivKey);
    // If the active patriotnode is ready, and the keyID matches with the registered one,
    // return private key, keyID, and pointer to dmn.
    OperationResult GetOperatorKey(CKey& key, CKeyID& keyID, CDeterministicPNCPtr& dmn) const;
    void SetNullProTx() { info.proTxHash = UINT256_ZERO; }

    const CActivePatriotnodeInfo* GetInfo() const { return &info; }
    patriotnode_state_t GetState() const { return state; }
    std::string GetStatus() const;
    bool IsReady() const { return state == PATRIOTNODE_READY; }

    static bool IsValidNetAddr(const CService& addrIn);
};

// Responsible for initializing the patriotnode
OperationResult initPatriotnode(const std::string& strPatriotNodePrivKey, const std::string& strPatriotNodeAddr, bool isFromInit);


// Responsible for activating the Patriotnode and pinging the network (legacy PN list)
class CActivePatriotnode
{
private:
    int status;
    std::string notCapableReason;

public:

    CActivePatriotnode()
    {
        vin = nullopt;
        status = ACTIVE_PATRIOTNODE_INITIAL;
    }

    // Initialized by init.cpp
    // Keys for the main Patriotnode
    CPubKey pubKeyPatriotnode;
    CKey privKeyPatriotnode;

    // Initialized while registering Patriotnode
    Optional<CTxIn> vin{nullopt};
    CService service;

    /// Manage status of main Patriotnode
    void ManageStatus();
    void ResetStatus();
    std::string GetStatusMessage() const;
    int GetStatus() const { return status; }

    /// Ping Patriotnode
    bool SendPatriotnodePing(std::string& errorMessage);
    /// Enable cold wallet mode (run a Patriotnode with no funds)
    bool EnableHotColdPatriotNode(CTxIn& vin, CService& addr);

    void GetKeys(CKey& privKeyPatriotnode, CPubKey& pubKeyPatriotnode);
};

// Compatibility code: get keys for either legacy or deterministic patriotnode
bool GetActivePatriotnodeKeys(CKey& key, CKeyID& keyID, CTxIn& vin);

#endif
