// Copyright (c) 2014-2015 The Dash developers
// Copyright (c) 2015-2020 The TrumpCoin developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef PATRIOTNODE_H
#define PATRIOTNODE_H

#include "key_io.h"
#include "key.h"
#include "messagesigner.h"
#include "net.h"
#include "serialize.h"
#include "sync.h"
#include "timedata.h"
#include "util/system.h"

/* Depth of the block pinged by patriotnodes */
static const unsigned int PNPING_DEPTH = 12;

class CPatriotnode;
class CPatriotnodeBroadcast;
class CPatriotnodePing;

typedef std::shared_ptr<CPatriotnode> PatriotnodeRef;

class CDeterministicPN;
typedef std::shared_ptr<const CDeterministicPN> CDeterministicPNCPtr;

int PatriotnodeMinPingSeconds();
int PatriotnodeBroadcastSeconds();
int PatriotnodeCollateralMinConf();
int PatriotnodePingSeconds();
int PatriotnodeExpirationSeconds();
int PatriotnodeRemovalSeconds();

//
// The Patriotnode Ping Class : Contains a different serialize method for sending pings from patriotnodes throughout the network
//

class CPatriotnodePing : public CSignedMessage
{
public:
    CTxIn vin;
    uint256 blockHash;
    int64_t sigTime; //mnb message times

    CPatriotnodePing();
    CPatriotnodePing(const CTxIn& newVin, const uint256& nBlockHash, uint64_t _sigTime);

    SERIALIZE_METHODS(CPatriotnodePing, obj) { READWRITE(obj.vin, obj.blockHash, obj.sigTime, obj.vchSig, obj.nMessVersion); }

    uint256 GetHash() const;

    // override CSignedMessage functions
    uint256 GetSignatureHash() const override { return GetHash(); }
    std::string GetStrMessage() const override;
    const CTxIn GetVin() const { return vin; };
    bool IsNull() const { return blockHash.IsNull() || vin.prevout.IsNull(); }

    bool CheckAndUpdate(int& nDos, int nChainHeight, bool fRequireAvailable = true, bool fCheckSigTimeOnly = false);
    void Relay();

    CPatriotnodePing& operator=(const CPatriotnodePing& other) = default;

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
// The Patriotnode Class. It contains the input of the 5000 TRUMP, signature to prove
// it's the one who own that ip address and code for calculating the payment election.
//
class CPatriotnode : public CSignedMessage
{
private:
    // critical section to protect the inner data structures
    mutable RecursiveMutex cs;
    bool fCollateralSpent{false};

public:
    enum state {
        PATRIOTNODE_PRE_ENABLED,
        PATRIOTNODE_ENABLED,
        PATRIOTNODE_EXPIRED,
        PATRIOTNODE_REMOVE,
        PATRIOTNODE_VIN_SPENT,
    };

    CTxIn vin;
    CService addr;
    CPubKey pubKeyCollateralAddress;
    CPubKey pubKeyPatriotnode;
    int64_t sigTime; //mnb message time
    int protocolVersion;
    int nScanningErrorCount;
    int nLastScanningErrorBlockHeight;
    CPatriotnodePing lastPing;

    explicit CPatriotnode();
    CPatriotnode(const CPatriotnode& other);

    // Initialize from DPN. Used by the compatibility code.
    CPatriotnode(const CDeterministicPNCPtr& dmn, int64_t registeredTime, const uint256& registeredHash);

    // override CSignedMessage functions
    uint256 GetSignatureHash() const override;
    std::string GetStrMessage() const override;
    const CTxIn GetVin() const { return vin; };
    CPubKey GetPubKey() const { return pubKeyPatriotnode; }

    void SetLastPing(const CPatriotnodePing& _lastPing) { WITH_LOCK(cs, lastPing = _lastPing;); }

    CPatriotnode& operator=(const CPatriotnode& other)
    {
        nMessVersion = other.nMessVersion;
        vchSig = other.vchSig;
        vin = other.vin;
        addr = other.addr;
        pubKeyCollateralAddress = other.pubKeyCollateralAddress;
        pubKeyPatriotnode = other.pubKeyPatriotnode;
        sigTime = other.sigTime;
        lastPing = other.lastPing;
        protocolVersion = other.protocolVersion;
        nScanningErrorCount = other.nScanningErrorCount;
        nLastScanningErrorBlockHeight = other.nLastScanningErrorBlockHeight;
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

    arith_uint256 CalculateScore(const uint256& hash) const;

    SERIALIZE_METHODS(CPatriotnode, obj)
    {
        LOCK(obj.cs);
        READWRITE(obj.vin, obj.addr, obj.pubKeyCollateralAddress);
        READWRITE(obj.pubKeyPatriotnode, obj.vchSig, obj.sigTime, obj.protocolVersion);
        READWRITE(obj.lastPing, obj.nScanningErrorCount, obj.nLastScanningErrorBlockHeight);

        if (obj.protocolVersion == MIN_BIP155_PROTOCOL_VERSION) {
            bool dummyIsBIP155Addr = false;
            READWRITE(dummyIsBIP155Addr);
        }
    }

    template <typename Stream>
    CPatriotnode(deserialize_type, Stream& s) {
        Unserialize(s);
    }

    bool UpdateFromNewBroadcast(CPatriotnodeBroadcast& mnb, int chainHeight);

    CPatriotnode::state GetActiveState() const;

    bool IsBroadcastedWithin(int seconds)
    {
        return (GetAdjustedTime() - sigTime) < seconds;
    }

    bool IsPingedWithin(int seconds, int64_t now = -1) const
    {
        now == -1 ? now = GetAdjustedTime() : now;
        return lastPing.IsNull() ? false : now - lastPing.sigTime < seconds;
    }

    void SetSpent()
    {
        LOCK(cs);
        fCollateralSpent = true;
    }

    void Disable()
    {
        LOCK(cs);
        sigTime = 0;
        lastPing = CPatriotnodePing();
    }

    bool IsEnabled() const
    {
        return GetActiveState() == PATRIOTNODE_ENABLED;
    }

    bool IsPreEnabled() const
    {
        return GetActiveState() == PATRIOTNODE_PRE_ENABLED;
    }

    bool IsAvailableState() const
    {
        state s = GetActiveState();
        return s == PATRIOTNODE_ENABLED || s == PATRIOTNODE_PRE_ENABLED;
    }

    std::string Status() const
    {
        auto activeState = GetActiveState();
        if (activeState == CPatriotnode::PATRIOTNODE_PRE_ENABLED) return "PRE_ENABLED";
        if (activeState == CPatriotnode::PATRIOTNODE_ENABLED)     return "ENABLED";
        if (activeState == CPatriotnode::PATRIOTNODE_EXPIRED)     return "EXPIRED";
        if (activeState == CPatriotnode::PATRIOTNODE_VIN_SPENT)   return "VIN_SPENT";
        if (activeState == CPatriotnode::PATRIOTNODE_REMOVE)      return "REMOVE";
        return strprintf("INVALID_%d", activeState);
    }

    bool IsValidNetAddr() const;

    /// Is the input associated with collateral public key? (and there is 5000 TRUMP - checking if valid patriotnode)
    bool IsInputAssociatedWithPubkey() const;

    /*
     * This is used only by the compatibility code for DPN, which don't share the public key (but the keyid).
     * Used by the payment-logic to include the necessary information in a temporary PatriotnodeRef object
     * (which is not indexed in the maps of the legacy manager).
     * A non-empty mnPayeeScript identifies this object as a "deterministic" patriotnode.
     * Note: this is the single payout for the patriotnode (if the dmn is configured to pay a portion of the reward
     * to the operator, this is done only after the disabling of the legacy system).
     */
    CScript mnPayeeScript{};
    CScript GetPayeeScript() const {
        return mnPayeeScript.empty() ? GetScriptForDestination(pubKeyCollateralAddress.GetID())
                                     : mnPayeeScript;
    }
};


//
// The Patriotnode Broadcast Class : Contains a different serialize method for sending patriotnodes through the network
//

class CPatriotnodeBroadcast : public CPatriotnode
{
public:
    CPatriotnodeBroadcast();
    CPatriotnodeBroadcast(CService newAddr, CTxIn newVin, CPubKey newPubkey, CPubKey newPubkey2, int protocolVersionIn, const CPatriotnodePing& _lastPing);
    CPatriotnodeBroadcast(const CPatriotnode& mn);

    bool CheckAndUpdate(int& nDoS, int nChainHeight);
    bool CheckInputsAndAdd(int chainHeight, int& nDos);

    uint256 GetHash() const;

    void Relay();

    // special sign/verify
    bool Sign(const CKey& key, const CPubKey& pubKey);
    bool CheckSignature() const;

    SERIALIZE_METHODS(CPatriotnodeBroadcast, obj)
    {
        READWRITE(obj.vin);
        READWRITE(obj.addr);
        READWRITE(obj.pubKeyCollateralAddress);
        READWRITE(obj.pubKeyPatriotnode);
        READWRITE(obj.vchSig);
        READWRITE(obj.sigTime);
        READWRITE(obj.protocolVersion);
        READWRITE(obj.lastPing);
        READWRITE(obj.nMessVersion);
    }

    /// Create Patriotnode broadcast, needs to be relayed manually after that
    static bool Create(const CTxIn& vin, const CService& service, const CKey& keyCollateralAddressNew, const CPubKey& pubKeyCollateralAddressNew, const CKey& keyPatriotnodeNew, const CPubKey& pubKeyPatriotnodeNew, std::string& strErrorRet, CPatriotnodeBroadcast& mnbRet);
    static bool Create(const std::string& strService, const std::string& strKey, const std::string& strTxHash, const std::string& strOutputIndex, std::string& strErrorRet, CPatriotnodeBroadcast& mnbRet, bool fOffline, int chainHeight);
    static bool CheckDefaultPort(CService service, std::string& strErrorRet, const std::string& strContext);
};

// Temporary function used for payment compatibility code.
// Returns a shared pointer to a patriotnode object initialized from a DPN.
PatriotnodeRef MakePatriotnodeRefForDPN(const CDeterministicPNCPtr& dmn);

#endif
