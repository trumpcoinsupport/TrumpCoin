// Copyright (c) 2014-2016 The Dash developers
// Copyright (c) 2015-2018 The PIVX developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef ACTIVEPATRIOTNODE_H
#define ACTIVEPATRIOTNODE_H

#include "init.h"
#include "key.h"
#include "patriotnode.h"
#include "net.h"
#include "obfuscation.h"
#include "sync.h"
#include "wallet.h"

#define ACTIVE_PATRIOTNODE_INITIAL 0 // initial state
#define ACTIVE_PATRIOTNODE_SYNC_IN_PROCESS 1
#define ACTIVE_PATRIOTNODE_INPUT_TOO_NEW 2
#define ACTIVE_PATRIOTNODE_NOT_CAPABLE 3
#define ACTIVE_PATRIOTNODE_STARTED 4

// Responsible for activating the Patriotnode and pinging the network
class CActivePatriotnode
{
private:
    // critical section to protect the inner data structures
    mutable CCriticalSection cs;

    /// Ping Patriotnode
    bool SendPatriotnodePing(std::string& errorMessage);

    /// Create Patriotnode broadcast, needs to be relayed manually after that
    bool CreateBroadcast(CTxIn vin, CService service, CKey key, CPubKey pubKey, CKey keyPatriotnode, CPubKey pubKeyPatriotnode, std::string& errorMessage, CPatriotnodeBroadcast &mnb);

    /// Get 1000 TRUMP input that can be used for the Patriotnode
    bool GetPatriotNodeVin(CTxIn& vin, CPubKey& pubkey, CKey& secretKey, std::string strTxHash, std::string strOutputIndex);
    bool GetVinFromOutput(COutput out, CTxIn& vin, CPubKey& pubkey, CKey& secretKey);

public:
    // Initialized by init.cpp
    // Keys for the main Patriotnode
    CPubKey pubKeyPatriotnode;

    // Initialized while registering Patriotnode
    CTxIn vin;
    CService service;

    int status;
    std::string notCapableReason;

    CActivePatriotnode()
    {
        status = ACTIVE_PATRIOTNODE_INITIAL;
    }

    /// Manage status of main Patriotnode
    void ManageStatus();
    std::string GetStatus();

    /// Create Patriotnode broadcast, needs to be relayed manually after that
    bool CreateBroadcast(std::string strService, std::string strKey, std::string strTxHash, std::string strOutputIndex, std::string& errorMessage, CPatriotnodeBroadcast &mnb, bool fOffline = false);

    /// Get 1000 TRUMP input that can be used for the Patriotnode
    bool GetPatriotNodeVin(CTxIn& vin, CPubKey& pubkey, CKey& secretKey);
    vector<COutput> SelectCoinsPatriotnode();

    /// Enable cold wallet mode (run a Patriotnode with no funds)
    bool EnableHotColdPatriotNode(CTxIn& vin, CService& addr);
};

#endif
