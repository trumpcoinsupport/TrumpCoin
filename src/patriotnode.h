// Copyright (c) 2014-2015 The Dash developers
// Copyright (c) 2015-2018 The PIVX developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef PATRIOTNODE_H
#define PATRIOTNODE_H

#include "base58.h"
#include "key.h"
#include "main.h"
#include "net.h"
#include "sync.h"
#include "timedata.h"
#include "util.h"

#define PATRIOTNODE_MIN_CONFIRMATIONS 15
#define PATRIOTNODE_MIN_MNP_SECONDS (10 * 60)
#define PATRIOTNODE_MIN_MNB_SECONDS (5 * 60)
#define PATRIOTNODE_PING_SECONDS (5 * 60)
#define PATRIOTNODE_EXPIRATION_SECONDS (120 * 60)
#define PATRIOTNODE_REMOVAL_SECONDS (130 * 60)
#define PATRIOTNODE_CHECK_SECONDS 5

using namespace std;

class CPatriotnode;
class CPatriotnodeBroadcast;
class CPatriotnodePing;
extern map<int64_t, uint256> mapCacheBlockHashes;

bool GetBlockHash(uint256& hash, int nBlockHeight);


//
// The Patriotnode Ping Class : Contains a different serialize method for sending pings from patriotnodes throughout the network
//

class CPatriotnodePing
{
public:
    CTxIn vin;
    uint256 blockHash;
    int64_t sigTime; //mnb message times
    std::vector<unsigned char> vchSig;
    //removed stop

    CPatriotnodePing();
    CPatriotnodePing(CTxIn& newVin);

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action, int nType, int nVersion)
    {
        READWRITE(vin);
        READWRITE(blockHash);
        READWRITE(sigTime);
        READWRITE(vchSig);
    }

    bool CheckAndUpdate(int& nDos, bool fRequireEnabled = true, bool fCheckSigTimeOnly = false);
    bool Sign(CKey& keyPatriotnode, CPubKey& pubKeyPatriotnode);
    bool VerifySignature(CPubKey& pubKeyPatriotnode, int &nDos);
    void Relay();

    uint256 GetHash()
    {
        CHashWriter ss(SER_GETHASH, PROTOCOL_VERSION);
        ss << vin;
        ss << sigTime;
        return ss.GetHash();
    }

    void swap(CPatriotnodePing& first, CPatriotnodePing& second) // nothrow
    {
        // enable ADL (not necessary in our case, but good practice)
        using std::swap;

        // by swapping the members of two classes,
        // the two classes are effectively swapped
        swap(first.vin, second.vin);
        swap(first.blockHash, second.blockHash);
        swap(first.sigTime, second.sigTime);
        swap(first.vchSig, second.vchSig);
    }

    CPatriotnodePing& operator=(CPatriotnodePing from)
    {
        swap(*this, from);
        return *this;
    }
    friend bool operator==(const CPatriotnodePing& a, const CPatriotnodePing& b)
    {
        return a.vin == b.vin && a.blockHash == b.blockHash;
    }
    friend bool operator!=(const CPatriotnodePing& a, const CPatriotnodePing& b)
    {
        return !(a == b);
    }
};

//
// The Patriotnode Class. For managing the Obfuscation process. It contains the input of the 10000 TRUMP, signature to prove
// it's the one who own that ip address and code for calculating the payment election.
//
class CPatriotnode
{
private:
    // critical section to protect the inner data structures
    mutable CCriticalSection cs;
    int64_t lastTimeChecked;

public:
    enum state {
        PATRIOTNODE_PRE_ENABLED,
        PATRIOTNODE_ENABLED,
        PATRIOTNODE_EXPIRED,
        PATRIOTNODE_OUTPOINT_SPENT,
        PATRIOTNODE_REMOVE,
        PATRIOTNODE_WATCHDOG_EXPIRED,
        PATRIOTNODE_POSE_BAN,
        PATRIOTNODE_VIN_SPENT,
        PATRIOTNODE_POS_ERROR
    };

    CTxIn vin;
    CService addr;
    CPubKey pubKeyCollateralAddress;
    CPubKey pubKeyPatriotnode;
    CPubKey pubKeyCollateralAddress1;
    CPubKey pubKeyPatriotnode1;
    std::vector<unsigned char> sig;
    int activeState;
    int64_t sigTime; //mnb message time
    int cacheInputAge;
    int cacheInputAgeBlock;
    bool unitTest;
    bool allowFreeTx;
    int protocolVersion;
    int nActiveState;
    int64_t nLastDsq; //the dsq count from the last dsq broadcast of this node
    int nScanningErrorCount;
    int nLastScanningErrorBlockHeight;
    CPatriotnodePing lastPing;

    int64_t nLastDsee;  // temporary, do not save. Remove after migration to v12
    int64_t nLastDseep; // temporary, do not save. Remove after migration to v12

    CPatriotnode();
    CPatriotnode(const CPatriotnode& other);
    CPatriotnode(const CPatriotnodeBroadcast& mnb);


    void swap(CPatriotnode& first, CPatriotnode& second) // nothrow
    {
        // enable ADL (not necessary in our case, but good practice)
        using std::swap;

        // by swapping the members of two classes,
        // the two classes are effectively swapped
        swap(first.vin, second.vin);
        swap(first.addr, second.addr);
        swap(first.pubKeyCollateralAddress, second.pubKeyCollateralAddress);
        swap(first.pubKeyPatriotnode, second.pubKeyPatriotnode);
        swap(first.sig, second.sig);
        swap(first.activeState, second.activeState);
        swap(first.sigTime, second.sigTime);
        swap(first.lastPing, second.lastPing);
        swap(first.cacheInputAge, second.cacheInputAge);
        swap(first.cacheInputAgeBlock, second.cacheInputAgeBlock);
        swap(first.unitTest, second.unitTest);
        swap(first.allowFreeTx, second.allowFreeTx);
        swap(first.protocolVersion, second.protocolVersion);
        swap(first.nLastDsq, second.nLastDsq);
        swap(first.nScanningErrorCount, second.nScanningErrorCount);
        swap(first.nLastScanningErrorBlockHeight, second.nLastScanningErrorBlockHeight);
    }

    CPatriotnode& operator=(CPatriotnode from)
    {
        swap(*this, from);
        return *this;
    }
    friend bool operator==(const CPatriotnode& a, const CPatriotnode& b)
    {
        return a.vin == b.vin;
    }
    friend bool operator!=(const CPatriotnode& a, const CPatriotnode& b)
    {
        return !(a.vin == b.vin);
    }

    uint256 CalculateScore(int mod = 1, int64_t nBlockHeight = 0);

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action, int nType, int nVersion)
    {
        LOCK(cs);

        READWRITE(vin);
        READWRITE(addr);
        READWRITE(pubKeyCollateralAddress);
        READWRITE(pubKeyPatriotnode);
        READWRITE(sig);
        READWRITE(sigTime);
        READWRITE(protocolVersion);
        READWRITE(activeState);
        READWRITE(lastPing);
        READWRITE(cacheInputAge);
        READWRITE(cacheInputAgeBlock);
        READWRITE(unitTest);
        READWRITE(allowFreeTx);
        READWRITE(nLastDsq);
        READWRITE(nScanningErrorCount);
        READWRITE(nLastScanningErrorBlockHeight);
    }

    int64_t SecondsSincePayment();

    bool UpdateFromNewBroadcast(CPatriotnodeBroadcast& mnb);

    inline uint64_t SliceHash(uint256& hash, int slice)
    {
        uint64_t n = 0;
        memcpy(&n, &hash + slice * 64, 64);
        return n;
    }

    void Check(bool forceCheck = false);

    bool IsBroadcastedWithin(int seconds)
    {
        return (GetAdjustedTime() - sigTime) < seconds;
    }

    bool IsPingedWithin(int seconds, int64_t now = -1)
    {
        now == -1 ? now = GetAdjustedTime() : now;

        return (lastPing == CPatriotnodePing()) ? false : now - lastPing.sigTime < seconds;
    }

    void Disable()
    {
        sigTime = 0;
        lastPing = CPatriotnodePing();
    }

    bool IsEnabled()
    {
        return activeState == PATRIOTNODE_ENABLED;
    }

    int GetPatriotnodeInputAge()
    {
        if (chainActive.Tip() == NULL) return 0;

        if (cacheInputAge == 0) {
            cacheInputAge = GetInputAge(vin);
            cacheInputAgeBlock = chainActive.Tip()->nHeight;
        }

        return cacheInputAge + (chainActive.Tip()->nHeight - cacheInputAgeBlock);
    }

    std::string GetStatus();

    std::string Status()
    {
        std::string strStatus = "ACTIVE";

        if (activeState == CPatriotnode::PATRIOTNODE_ENABLED) strStatus = "ENABLED";
        if (activeState == CPatriotnode::PATRIOTNODE_EXPIRED) strStatus = "EXPIRED";
        if (activeState == CPatriotnode::PATRIOTNODE_VIN_SPENT) strStatus = "VIN_SPENT";
        if (activeState == CPatriotnode::PATRIOTNODE_REMOVE) strStatus = "REMOVE";
        if (activeState == CPatriotnode::PATRIOTNODE_POS_ERROR) strStatus = "POS_ERROR";

        return strStatus;
    }

    int64_t GetLastPaid();
    bool IsValidNetAddr();
};


//
// The Patriotnode Broadcast Class : Contains a different serialize method for sending patriotnodes through the network
//

class CPatriotnodeBroadcast : public CPatriotnode
{
public:
    CPatriotnodeBroadcast();
    CPatriotnodeBroadcast(CService newAddr, CTxIn newVin, CPubKey newPubkey, CPubKey newPubkey2, int protocolVersionIn);
    CPatriotnodeBroadcast(const CPatriotnode& mn);

    bool CheckAndUpdate(int& nDoS);
    bool CheckInputsAndAdd(int& nDos);
    bool Sign(CKey& keyCollateralAddress);
    bool VerifySignature();
    void Relay();
    std::string GetOldStrMessage();
    std::string GetNewStrMessage();

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action, int nType, int nVersion)
    {
        READWRITE(vin);
        READWRITE(addr);
        READWRITE(pubKeyCollateralAddress);
        READWRITE(pubKeyPatriotnode);
        READWRITE(sig);
        READWRITE(sigTime);
        READWRITE(protocolVersion);
        READWRITE(lastPing);
        READWRITE(nLastDsq);
    }

    uint256 GetHash()
    {
        CHashWriter ss(SER_GETHASH, PROTOCOL_VERSION);
        ss << sigTime;
        ss << pubKeyCollateralAddress;
        return ss.GetHash();
    }

    /// Create Patriotnode broadcast, needs to be relayed manually after that
    static bool Create(CTxIn vin, CService service, CKey keyCollateralAddressNew, CPubKey pubKeyCollateralAddressNew, CKey keyPatriotnodeNew, CPubKey pubKeyPatriotnodeNew, std::string& strErrorRet, CPatriotnodeBroadcast& mnbRet);
    static bool Create(std::string strService, std::string strKey, std::string strTxHash, std::string strOutputIndex, std::string& strErrorRet, CPatriotnodeBroadcast& mnbRet, bool fOffline = false);
    static bool CheckDefaultPort(std::string strService, std::string& strErrorRet, std::string strContext);
};

#endif
