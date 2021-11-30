// Copyright (c) 2014-2015 The Dash developers
// Copyright (c) 2015-2020 The TrumpCoin developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "patriotnode.h"

#include "addrman.h"
#include "init.h"
#include "patriotnode-sync.h"
#include "patriotnodeman.h"
#include "netbase.h"
#include "sync.h"
#include "wallet/wallet.h"

#define PATRIOTNODE_MIN_CONFIRMATIONS_REGTEST 1
#define PATRIOTNODE_MIN_PNP_SECONDS_REGTEST 90
#define PATRIOTNODE_MIN_PNB_SECONDS_REGTEST 25
#define PATRIOTNODE_PING_SECONDS_REGTEST 25
#define PATRIOTNODE_EXPIRATION_SECONDS_REGTEST 12 * 60
#define PATRIOTNODE_REMOVAL_SECONDS_REGTEST 13 * 60

#define PATRIOTNODE_MIN_CONFIRMATIONS 15
#define PATRIOTNODE_MIN_PNP_SECONDS (10 * 60)
#define PATRIOTNODE_MIN_PNB_SECONDS (5 * 60)
#define PATRIOTNODE_PING_SECONDS (5 * 60)
#define PATRIOTNODE_EXPIRATION_SECONDS (120 * 60)
#define PATRIOTNODE_REMOVAL_SECONDS (130 * 60)
#define PATRIOTNODE_CHECK_SECONDS 5

// keep track of the scanning errors I've seen
std::map<uint256, int> mapSeenPatriotnodeScanningErrors;


int PatriotnodeMinPingSeconds()
{
    return Params().IsRegTestNet() ? PATRIOTNODE_MIN_PNP_SECONDS_REGTEST : PATRIOTNODE_MIN_PNP_SECONDS;
}

int PatriotnodeBroadcastSeconds()
{
    return Params().IsRegTestNet() ? PATRIOTNODE_MIN_PNB_SECONDS_REGTEST : PATRIOTNODE_MIN_PNB_SECONDS;
}

int PatriotnodeCollateralMinConf()
{
    return Params().IsRegTestNet() ? PATRIOTNODE_MIN_CONFIRMATIONS_REGTEST : PATRIOTNODE_MIN_CONFIRMATIONS;
}

int PatriotnodePingSeconds()
{
    return Params().IsRegTestNet() ? PATRIOTNODE_PING_SECONDS_REGTEST : PATRIOTNODE_PING_SECONDS;
}

int PatriotnodeExpirationSeconds()
{
    return Params().IsRegTestNet() ? PATRIOTNODE_EXPIRATION_SECONDS_REGTEST : PATRIOTNODE_EXPIRATION_SECONDS;
}

int PatriotnodeRemovalSeconds()
{
    return Params().IsRegTestNet() ? PATRIOTNODE_REMOVAL_SECONDS_REGTEST : PATRIOTNODE_REMOVAL_SECONDS;
}

// Used for sigTime < maxTimeWindow
int64_t GetMaxTimeWindow(int chainHeight)
{
    bool isV5_3 = Params().GetConsensus().NetworkUpgradeActive(chainHeight, Consensus::UPGRADE_V5_3);
    return GetAdjustedTime() + (isV5_3 ? (60 * 2) : (60 * 60));
}


CPatriotnode::CPatriotnode() :
        CSignedMessage()
{
    LOCK(cs);
    vin = CTxIn();
    addr = CService();
    pubKeyCollateralAddress = CPubKey();
    pubKeyPatriotnode = CPubKey();
    sigTime = 0;
    lastPing = CPatriotnodePing();
    protocolVersion = PROTOCOL_VERSION;
    nScanningErrorCount = 0;
    nLastScanningErrorBlockHeight = 0;
    mnPayeeScript.clear();
}

CPatriotnode::CPatriotnode(const CPatriotnode& other) :
        CSignedMessage(other)
{
    LOCK(cs);
    vin = other.vin;
    addr = other.addr;
    pubKeyCollateralAddress = other.pubKeyCollateralAddress;
    pubKeyPatriotnode = other.pubKeyPatriotnode;
    sigTime = other.sigTime;
    lastPing = other.lastPing;
    protocolVersion = other.protocolVersion;
    nScanningErrorCount = other.nScanningErrorCount;
    nLastScanningErrorBlockHeight = other.nLastScanningErrorBlockHeight;
    mnPayeeScript = other.mnPayeeScript;
}

CPatriotnode::CPatriotnode(const CDeterministicPNCPtr& dmn, int64_t registeredTime, const uint256& registeredHash) :
        CSignedMessage()
{
    LOCK(cs);
    vin = CTxIn(dmn->collateralOutpoint);
    addr = dmn->pdmnState->addr;
    pubKeyCollateralAddress = CPubKey();
    pubKeyPatriotnode = CPubKey();
    sigTime = registeredTime;
    lastPing = CPatriotnodePing(vin, registeredHash, registeredTime);
    protocolVersion = PROTOCOL_VERSION;
    nScanningErrorCount = 0;
    nLastScanningErrorBlockHeight = 0;
    mnPayeeScript = dmn->pdmnState->scriptPayout;
}

uint256 CPatriotnode::GetSignatureHash() const
{
    int version = !addr.IsAddrV1Compatible() ? PROTOCOL_VERSION | ADDRV2_FORMAT : PROTOCOL_VERSION;
    CHashWriter ss(SER_GETHASH, version);
    ss << nMessVersion;
    ss << addr;
    ss << sigTime;
    ss << pubKeyCollateralAddress;
    ss << pubKeyPatriotnode;
    ss << protocolVersion;
    return ss.GetHash();
}

std::string CPatriotnode::GetStrMessage() const
{
    return (addr.ToString() +
            std::to_string(sigTime) +
            pubKeyCollateralAddress.GetID().ToString() +
            pubKeyPatriotnode.GetID().ToString() +
            std::to_string(protocolVersion)
    );
}

//
// When a new patriotnode broadcast is sent, update our information
//
bool CPatriotnode::UpdateFromNewBroadcast(CPatriotnodeBroadcast& mnb, int chainHeight)
{
    if (mnb.sigTime > sigTime) {
        // TODO: lock cs. Need to be careful as mnb.lastPing.CheckAndUpdate locks cs_main internally.
        nMessVersion = mnb.nMessVersion;
        pubKeyPatriotnode = mnb.pubKeyPatriotnode;
        pubKeyCollateralAddress = mnb.pubKeyCollateralAddress;
        sigTime = mnb.sigTime;
        vchSig = mnb.vchSig;
        protocolVersion = mnb.protocolVersion;
        addr = mnb.addr;
        int nDoS = 0;
        if (mnb.lastPing.IsNull() || (!mnb.lastPing.IsNull() && mnb.lastPing.CheckAndUpdate(nDoS, chainHeight, false))) {
            lastPing = mnb.lastPing;
            mnodeman.mapSeenPatriotnodePing.emplace(lastPing.GetHash(), lastPing);
        }
        return true;
    }
    return false;
}

//
// Deterministically calculate a given "score" for a Patriotnode depending on how close it's hash is to
// the proof of work for that block. The further away they are the better, the furthest will win the election
// and get paid this block
//
arith_uint256 CPatriotnode::CalculateScore(const uint256& hash) const
{
    CHashWriter ss(SER_GETHASH, PROTOCOL_VERSION);
    ss << hash;
    const arith_uint256& hash2 = UintToArith256(ss.GetHash());

    CHashWriter ss2(SER_GETHASH, PROTOCOL_VERSION);
    ss2 << hash;
    const arith_uint256& aux = UintToArith256(vin.prevout.hash) + vin.prevout.n;
    ss2 << aux;
    const arith_uint256& hash3 = UintToArith256(ss2.GetHash());

    return (hash3 > hash2 ? hash3 - hash2 : hash2 - hash3);
}

CPatriotnode::state CPatriotnode::GetActiveState() const
{
    LOCK(cs);
    if (fCollateralSpent) {
        return PATRIOTNODE_VIN_SPENT;
    }
    if (!IsPingedWithin(PatriotnodeRemovalSeconds())) {
        return PATRIOTNODE_REMOVE;
    }
    if (!IsPingedWithin(PatriotnodeExpirationSeconds())) {
        return PATRIOTNODE_EXPIRED;
    }
    if(lastPing.sigTime - sigTime < PatriotnodeMinPingSeconds()){
        return PATRIOTNODE_PRE_ENABLED;
    }
    return PATRIOTNODE_ENABLED;
}

bool CPatriotnode::IsValidNetAddr() const
{
    // TODO: regtest is fine with any addresses for now,
    // should probably be a bit smarter if one day we start to implement tests for this
    return Params().IsRegTestNet() ||
           (IsReachable(addr) && addr.IsRoutable());
}

bool CPatriotnode::IsInputAssociatedWithPubkey() const
{
    CScript payee;
    payee = GetScriptForDestination(pubKeyCollateralAddress.GetID());

    CTransactionRef txVin;
    uint256 hash;
    if(GetTransaction(vin.prevout.hash, txVin, hash, true)) {
        for (const CTxOut& out : txVin->vout) {
            if (out.nValue == Params().GetConsensus().nPNCollateralAmt &&
                out.scriptPubKey == payee)
                return true;
        }
    }

    return false;
}

CPatriotnodeBroadcast::CPatriotnodeBroadcast() :
        CPatriotnode()
{ }

CPatriotnodeBroadcast::CPatriotnodeBroadcast(CService newAddr, CTxIn newVin, CPubKey pubKeyCollateralAddressNew, CPubKey pubKeyPatriotnodeNew, int protocolVersionIn, const CPatriotnodePing& _lastPing) :
        CPatriotnode()
{
    vin = newVin;
    addr = newAddr;
    pubKeyCollateralAddress = pubKeyCollateralAddressNew;
    pubKeyPatriotnode = pubKeyPatriotnodeNew;
    protocolVersion = protocolVersionIn;
    lastPing = _lastPing;
    sigTime = lastPing.sigTime;
}

CPatriotnodeBroadcast::CPatriotnodeBroadcast(const CPatriotnode& mn) :
        CPatriotnode(mn)
{ }

bool CPatriotnodeBroadcast::Create(const std::string& strService,
                                  const std::string& strKeyPatriotnode,
                                  const std::string& strTxHash,
                                  const std::string& strOutputIndex,
                                  std::string& strErrorRet,
                                  CPatriotnodeBroadcast& mnbRet,
                                  bool fOffline,
                                  int chainHeight)
{
    CTxIn txin;
    CPubKey pubKeyCollateralAddressNew;
    CKey keyCollateralAddressNew;
    CPubKey pubKeyPatriotnodeNew;
    CKey keyPatriotnodeNew;

    //need correct blocks to send ping
    if (!fOffline && !patriotnodeSync.IsBlockchainSynced()) {
        strErrorRet = "Sync in progress. Must wait until sync is complete to start Patriotnode";
        LogPrint(BCLog::PATRIOTNODE,"CPatriotnodeBroadcast::Create -- %s\n", strErrorRet);
        return false;
    }

    if (!CMessageSigner::GetKeysFromSecret(strKeyPatriotnode, keyPatriotnodeNew, pubKeyPatriotnodeNew)) {
        strErrorRet = strprintf("Invalid patriotnode key %s", strKeyPatriotnode);
        LogPrint(BCLog::PATRIOTNODE,"CPatriotnodeBroadcast::Create -- %s\n", strErrorRet);
        return false;
    }

    std::string strError;
    // Use wallet-0 here. Legacy mnb creation can be removed after transition to DPN
    if (vpwallets.empty() || !vpwallets[0]->GetPatriotnodeVinAndKeys(txin, pubKeyCollateralAddressNew, keyCollateralAddressNew, strTxHash, strOutputIndex, strError)) {
        strErrorRet = strError; // GetPatriotnodeVinAndKeys logs this error. Only returned for GUI error notification.
        LogPrint(BCLog::PATRIOTNODE,"CPatriotnodeBroadcast::Create -- %s\n", strprintf("Could not allocate txin %s:%s for patriotnode %s", strTxHash, strOutputIndex, strService));
        return false;
    }

    int nPort = 0;
    int nDefaultPort = Params().GetDefaultPort();
    std::string strHost;
    SplitHostPort(strService, nPort, strHost);
    if (nPort == 0) nPort = nDefaultPort;
    CService _service(LookupNumeric(strHost.c_str(), nPort));

    // The service needs the correct default port to work properly
    if (!CheckDefaultPort(_service, strErrorRet, "CPatriotnodeBroadcast::Create"))
        return false;

    // Check if the PN has a ADDRv2 and reject it if the new NU wasn't enforced.
    if (!_service.IsAddrV1Compatible() &&
        !Params().GetConsensus().NetworkUpgradeActive(chainHeight, Consensus::UPGRADE_V5_3)) {
        strErrorRet = "Cannot start PN with a v2 address before the v5.3 enforcement";
        return false;
    }

    return Create(txin, _service, keyCollateralAddressNew, pubKeyCollateralAddressNew, keyPatriotnodeNew, pubKeyPatriotnodeNew, strErrorRet, mnbRet);
}

bool CPatriotnodeBroadcast::Create(const CTxIn& txin,
                                  const CService& service,
                                  const CKey& keyCollateralAddressNew,
                                  const CPubKey& pubKeyCollateralAddressNew,
                                  const CKey& keyPatriotnodeNew,
                                  const CPubKey& pubKeyPatriotnodeNew,
                                  std::string& strErrorRet,
                                  CPatriotnodeBroadcast& mnbRet)
{
    // wait for reindex and/or import to finish
    if (fImporting || fReindex) return false;

    LogPrint(BCLog::PATRIOTNODE, "CPatriotnodeBroadcast::Create -- pubKeyCollateralAddressNew = %s, pubKeyPatriotnodeNew.GetID() = %s\n",
             EncodeDestination(pubKeyCollateralAddressNew.GetID()),
        pubKeyPatriotnodeNew.GetID().ToString());

    // Get block hash to ping (TODO: move outside of this function)
    const uint256& nBlockHashToPing = mnodeman.GetBlockHashToPing();
    CPatriotnodePing mnp(txin, nBlockHashToPing, GetAdjustedTime());


    mnbRet = CPatriotnodeBroadcast(service, txin, pubKeyCollateralAddressNew, pubKeyPatriotnodeNew, PROTOCOL_VERSION, mnp);

    if (!mnbRet.IsValidNetAddr()) {
        strErrorRet = strprintf("Invalid IP address %s, patriotnode=%s", mnbRet.addr.ToStringIP (), txin.prevout.hash.ToString());
        LogPrint(BCLog::PATRIOTNODE,"CPatriotnodeBroadcast::Create -- %s\n", strErrorRet);
        mnbRet = CPatriotnodeBroadcast();
        return false;
    }



    return true;
}

bool CPatriotnodeBroadcast::Sign(const CKey& key, const CPubKey& pubKey)
{
    std::string strError = "";
    nMessVersion = MessageVersion::MESS_VER_HASH;
    const std::string strMessage = GetSignatureHash().GetHex();


    return true;
}

bool CPatriotnodeBroadcast::CheckSignature() const
{
    std::string strError = "";
    std::string strMessage = (
                            nMessVersion == MessageVersion::MESS_VER_HASH ?
                            GetSignatureHash().GetHex() :
                            GetStrMessage()
                            );


    return true;
}

bool CPatriotnodeBroadcast::CheckDefaultPort(CService service, std::string& strErrorRet, const std::string& strContext)
{
    int nDefaultPort = Params().GetDefaultPort();

    if (service.GetPort() != nDefaultPort && !Params().IsRegTestNet()) {
        strErrorRet = strprintf("Invalid port %u for patriotnode %s, only %d is supported on %s-net.",
            service.GetPort(), service.ToString(), nDefaultPort, Params().NetworkIDString());
        LogPrintf("%s - %s\n", strContext, strErrorRet);
        return false;
    }

    return true;
}

bool CPatriotnodeBroadcast::CheckAndUpdate(int& nDos, int nChainHeight)
{
    // make sure signature isn't in the future (past is OK)
    if (sigTime > GetMaxTimeWindow(nChainHeight)) {
        LogPrint(BCLog::PATRIOTNODE,"mnb - Signature rejected, too far into the future %s\n", vin.prevout.hash.ToString());
        nDos = 1;
        return false;
    }



    if (protocolVersion < ActiveProtocol()) {
        LogPrint(BCLog::PATRIOTNODE,"mnb - ignoring outdated Patriotnode %s protocol version %d\n", vin.prevout.hash.ToString(), protocolVersion);
        return false;
    }

    CScript pubkeyScript;
    pubkeyScript = GetScriptForDestination(pubKeyCollateralAddress.GetID());



    CScript pubkeyScript2;
    pubkeyScript2 = GetScriptForDestination(pubKeyPatriotnode.GetID());



    if (!vin.scriptSig.empty()) {
        LogPrint(BCLog::PATRIOTNODE,"mnb - Ignore Not Empty ScriptSig %s\n", vin.prevout.hash.ToString());
        return false;
    }

    std::string strError = "";


    if (Params().NetworkIDString() == CBaseChainParams::MAIN) {
        if (addr.GetPort() != 15110) return false;
    } else if (addr.GetPort() == 15110)
        return false;

    // incorrect ping or its sigTime
    if(lastPing.IsNull() || !lastPing.CheckAndUpdate(nDos, nChainHeight, false, true)) {
        return false;
    }

    //search existing Patriotnode list, this is where we update existing Patriotnodes with new mnb broadcasts
    CPatriotnode* pmn = mnodeman.Find(vin.prevout);

    // no such patriotnode, nothing to update
    if (pmn == NULL) return true;

    // this broadcast is older or equal than the one that we already have - it's bad and should never happen
    // unless someone is doing something fishy
    // (mapSeenPatriotnodeBroadcast in CPatriotnodeMan::ProcessMessage should filter legit duplicates)
    if(pmn->sigTime >= sigTime) {
        return error("%s : Bad sigTime %d for Patriotnode %20s %105s (existing broadcast is at %d)",
                      __func__, sigTime, addr.ToString(), vin.ToString(), pmn->sigTime);
    }

    // patriotnode is not enabled yet/already, nothing to update
    if (!pmn->IsEnabled()) return true;

    // mn.pubkey = pubkey, IsVinAssociatedWithPubkey is validated once below,
    //   after that they just need to match
    if (pmn->pubKeyCollateralAddress == pubKeyCollateralAddress && !pmn->IsBroadcastedWithin(PatriotnodeBroadcastSeconds())) {
        //take the newest entry
        LogPrint(BCLog::PATRIOTNODE,"mnb - Got updated entry for %s\n", vin.prevout.hash.ToString());
        if (pmn->UpdateFromNewBroadcast((*this), nChainHeight)) {
            if (pmn->IsEnabled()) Relay();
        }
        patriotnodeSync.AddedPatriotnodeList(GetHash());
    }

    return true;
}

bool CPatriotnodeBroadcast::CheckInputsAndAdd(int nChainHeight, int& nDoS)
{
    // incorrect ping or its sigTime
    if(lastPing.IsNull() || !lastPing.CheckAndUpdate(nDoS, nChainHeight, false, true)) {
        return false;
    }

    // search existing Patriotnode list
    CPatriotnode* pmn = mnodeman.Find(vin.prevout);
    if (pmn != NULL) {
        // nothing to do here if we already know about this patriotnode and it's enabled
        if (pmn->IsEnabled()) return true;
        // if it's not enabled, remove old PN first and continue
        else
            mnodeman.Remove(pmn->vin.prevout);
    }

    const Coin& collateralUtxo = pcoinsTip->AccessCoin(vin.prevout);
    if (collateralUtxo.IsSpent()) {
        LogPrint(BCLog::PATRIOTNODE,"mnb - vin %s spent\n", vin.prevout.ToString());
        return false;
    }

    LogPrint(BCLog::PATRIOTNODE, "mnb - Accepted Patriotnode entry\n");
    const int utxoHeight = collateralUtxo.nHeight;
    int collateralUtxoDepth = nChainHeight - utxoHeight + 1;
    if (collateralUtxoDepth < PatriotnodeCollateralMinConf()) {
        LogPrint(BCLog::PATRIOTNODE,"mnb - Input must have at least %d confirmations\n", PatriotnodeCollateralMinConf());
        // maybe we miss few blocks, let this mnb to be checked again later
        mnodeman.mapSeenPatriotnodeBroadcast.erase(GetHash());
        patriotnodeSync.mapSeenSyncPNB.erase(GetHash());
        return false;
    }

    // verify that sig time is legit in past
    // should be at least not earlier than block when 5000 TRUMP tx got PATRIOTNODE_MIN_CONFIRMATIONS
    CBlockIndex* pConfIndex = WITH_LOCK(cs_main, return chainActive[utxoHeight + PatriotnodeCollateralMinConf() - 1]); // block where tx got PATRIOTNODE_MIN_CONFIRMATIONS
    if (pConfIndex->GetBlockTime() > sigTime) {
        LogPrint(BCLog::PATRIOTNODE,"mnb - Bad sigTime %d for Patriotnode %s (%i conf block is at %d)\n",
            sigTime, vin.prevout.hash.ToString(), PatriotnodeCollateralMinConf(), pConfIndex->GetBlockTime());
        return false;
    }

    LogPrint(BCLog::PATRIOTNODE,"mnb - Got NEW Patriotnode entry - %s - %lli \n", vin.prevout.hash.ToString(), sigTime);
    CPatriotnode mn(*this);
    mnodeman.Add(mn);

    // if it matches our Patriotnode privkey, then we've been remotely activated
    if (pubKeyPatriotnode == activePatriotnode.pubKeyPatriotnode && protocolVersion == PROTOCOL_VERSION) {
        activePatriotnode.EnableHotColdPatriotNode(vin, addr);
    }

    // Relay only if we are synchronized and if the mnb address is not local.
    // Makes no sense to relay PNBs to the peers from where we are syncing them.
    bool isLocal = (addr.IsRFC1918() || addr.IsLocal()) && !Params().IsRegTestNet();
    if (!isLocal && patriotnodeSync.IsSynced()) Relay();

    return true;
}

void CPatriotnodeBroadcast::Relay()
{
    CInv inv(MSG_PATRIOTNODE_ANNOUNCE, GetHash());
    g_connman->RelayInv(inv);
}

uint256 CPatriotnodeBroadcast::GetHash() const
{
    CHashWriter ss(SER_GETHASH, PROTOCOL_VERSION);
    ss << sigTime;
    ss << pubKeyCollateralAddress;
    return ss.GetHash();
}

CPatriotnodePing::CPatriotnodePing() :
        CSignedMessage(),
        vin(),
        blockHash(),
        sigTime(0)
{ }

CPatriotnodePing::CPatriotnodePing(const CTxIn& newVin, const uint256& nBlockHash, uint64_t _sigTime) :
        CSignedMessage(),
        vin(newVin),
        blockHash(nBlockHash),
        sigTime(_sigTime)
{ }

uint256 CPatriotnodePing::GetHash() const
{
    CHashWriter ss(SER_GETHASH, PROTOCOL_VERSION);
    ss << vin;
    if (nMessVersion == MessageVersion::MESS_VER_HASH) ss << blockHash;
    ss << sigTime;
    return ss.GetHash();
}

std::string CPatriotnodePing::GetStrMessage() const
{
    return vin.ToString() + blockHash.ToString() + std::to_string(sigTime);
}

bool CPatriotnodePing::CheckAndUpdate(int& nDos, int nChainHeight, bool fRequireAvailable, bool fCheckSigTimeOnly)
{
    if (sigTime > GetMaxTimeWindow(nChainHeight)) {
        LogPrint(BCLog::PNPING,"%s: Signature rejected, too far into the future %s\n", __func__, vin.prevout.hash.ToString());
        nDos = 30;
        return false;
    }





    // Check if the ping block hash exists and it's within 24 blocks from the tip
    if (!mnodeman.IsWithinDepth(blockHash, 2 * PNPING_DEPTH)) {
        LogPrint(BCLog::PNPING,"%s: Patriotnode %s block hash %s is too old or has an invalid block hash\n",
                                        __func__, vin.prevout.hash.ToString(), blockHash.ToString());
        // don't ban peers relaying stale data before the active protocol enforcement
        nDos = 33;
        return false;
    }

    // see if we have this Patriotnode
    CPatriotnode* pmn = mnodeman.Find(vin.prevout);
    const bool isPatriotnodeFound = (pmn != nullptr);
    const bool isSignatureValid = (isPatriotnodeFound && CheckSignature(pmn->pubKeyPatriotnode.GetID()));

    if(fCheckSigTimeOnly) {
        if (isPatriotnodeFound && !isSignatureValid) {
            nDos = 33;
            return false;
        }
        return true;
    }

    LogPrint(BCLog::PNPING, "%s: New Ping - %s - %s - %lli\n", __func__, GetHash().ToString(), blockHash.ToString(), sigTime);

    if (isPatriotnodeFound && pmn->protocolVersion >= ActiveProtocol()) {

        // Update ping only if the patriotnode is in available state (pre-enabled or enabled)
        if (fRequireAvailable && !pmn->IsAvailableState()) {
            nDos = 20;
            return false;
        }

        // update only if there is no known ping for this patriotnode or
        // last ping was more then PATRIOTNODE_MIN_PNP_SECONDS-60 ago comparing to this one
        if (!pmn->IsPingedWithin(PatriotnodeMinPingSeconds() - 60, sigTime)) {
            if (!isSignatureValid) {
                nDos = 33;
                return false;
            }

            // ping have passed the basic checks, can be updated now
            mnodeman.mapSeenPatriotnodePing.emplace(GetHash(), *this);

            // SetLastPing locks patriotnode cs. Be careful with the lock ordering.
            pmn->SetLastPing(*this);

            //mnodeman.mapSeenPatriotnodeBroadcast.lastPing is probably outdated, so we'll update it
            CPatriotnodeBroadcast mnb(*pmn);
            const uint256& hash = mnb.GetHash();
            if (mnodeman.mapSeenPatriotnodeBroadcast.count(hash)) {
                mnodeman.mapSeenPatriotnodeBroadcast[hash].lastPing = *this;
            }

            if (!pmn->IsEnabled()) return false;

            LogPrint(BCLog::PNPING, "%s: Patriotnode ping accepted, vin: %s\n", __func__, vin.prevout.hash.ToString());

            Relay();
            return true;
        }
        LogPrint(BCLog::PNPING, "%s: Patriotnode ping arrived too early, vin: %s\n", __func__, vin.prevout.hash.ToString());
        //nDos = 1; //disable, this is happening frequently and causing banned peers
        return false;
    }
    LogPrint(BCLog::PNPING, "%s: Couldn't find compatible Patriotnode entry, vin: %s\n", __func__, vin.prevout.hash.ToString());

    return false;
}

void CPatriotnodePing::Relay()
{
    CInv inv(MSG_PATRIOTNODE_PING, GetHash());
    g_connman->RelayInv(inv);
}

PatriotnodeRef MakePatriotnodeRefForDPN(const CDeterministicPNCPtr& dmn)
{
    // create legacy patriotnode for DPN
    int refHeight = std::max(dmn->pdmnState->nRegisteredHeight, dmn->pdmnState->nPoSeRevivedHeight);
    const CBlockIndex* pindex = WITH_LOCK(cs_main, return mapBlockIndex.at(chainActive[refHeight]->GetBlockHash()); );
    return std::make_shared<CPatriotnode>(CPatriotnode(dmn, pindex->GetBlockTime(), pindex->GetBlockHash()));
}
