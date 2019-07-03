// Copyright (c) 2014-2015 The Dash developers
// Copyright (c) 2015-2018 The PIVX developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "patriotnode.h"
#include "addrman.h"
#include "patriotnodeman.h"
#include "obfuscation.h"
#include "sync.h"
#include "util.h"
#include <boost/lexical_cast.hpp>

// keep track of the scanning errors I've seen
map<uint256, int> mapSeenPatriotnodeScanningErrors;
// cache block hashes as we calculate them
std::map<int64_t, uint256> mapCacheBlockHashes;

//Get the last hash that matches the modulus given. Processed in reverse order
bool GetBlockHash(uint256& hash, int nBlockHeight)
{
    if (chainActive.Tip() == NULL) return false;

    if (nBlockHeight == 0)
        nBlockHeight = chainActive.Tip()->nHeight;

    if (mapCacheBlockHashes.count(nBlockHeight)) {
        hash = mapCacheBlockHashes[nBlockHeight];
        return true;
    }

    const CBlockIndex* BlockLastSolved = chainActive.Tip();
    const CBlockIndex* BlockReading = chainActive.Tip();

    if (BlockLastSolved == NULL || BlockLastSolved->nHeight == 0 || chainActive.Tip()->nHeight + 1 < nBlockHeight) return false;

    int nBlocksAgo = 0;
    if (nBlockHeight > 0) nBlocksAgo = (chainActive.Tip()->nHeight + 1) - nBlockHeight;
    assert(nBlocksAgo >= 0);

    int n = 0;
    for (unsigned int i = 1; BlockReading && BlockReading->nHeight > 0; i++) {
        if (n >= nBlocksAgo) {
            hash = BlockReading->GetBlockHash();
            mapCacheBlockHashes[nBlockHeight] = hash;
            return true;
        }
        n++;

        if (BlockReading->pprev == NULL) {
            assert(BlockReading);
            break;
        }
        BlockReading = BlockReading->pprev;
    }

    return false;
}

CPatriotnode::CPatriotnode()
{
    LOCK(cs);
    vin = CTxIn();
    addr = CService();
    pubKeyCollateralAddress = CPubKey();
    pubKeyPatriotnode = CPubKey();
    sig = std::vector<unsigned char>();
    activeState = PATRIOTNODE_ENABLED;
    sigTime = GetAdjustedTime();
    lastPing = CPatriotnodePing();
    cacheInputAge = 0;
    cacheInputAgeBlock = 0;
    unitTest = false;
    allowFreeTx = true;
    nActiveState = PATRIOTNODE_ENABLED,
    protocolVersion = PROTOCOL_VERSION;
    nLastDsq = 0;
    nScanningErrorCount = 0;
    nLastScanningErrorBlockHeight = 0;
    lastTimeChecked = 0;
    nLastDsee = 0;  // temporary, do not save. Remove after migration to v12
    nLastDseep = 0; // temporary, do not save. Remove after migration to v12
}

CPatriotnode::CPatriotnode(const CPatriotnode& other)
{
    LOCK(cs);
    vin = other.vin;
    addr = other.addr;
    pubKeyCollateralAddress = other.pubKeyCollateralAddress;
    pubKeyPatriotnode = other.pubKeyPatriotnode;
    sig = other.sig;
    activeState = other.activeState;
    sigTime = other.sigTime;
    lastPing = other.lastPing;
    cacheInputAge = other.cacheInputAge;
    cacheInputAgeBlock = other.cacheInputAgeBlock;
    unitTest = other.unitTest;
    allowFreeTx = other.allowFreeTx;
    nActiveState = PATRIOTNODE_ENABLED,
    protocolVersion = other.protocolVersion;
    nLastDsq = other.nLastDsq;
    nScanningErrorCount = other.nScanningErrorCount;
    nLastScanningErrorBlockHeight = other.nLastScanningErrorBlockHeight;
    lastTimeChecked = 0;
    nLastDsee = other.nLastDsee;   // temporary, do not save. Remove after migration to v12
    nLastDseep = other.nLastDseep; // temporary, do not save. Remove after migration to v12
}

CPatriotnode::CPatriotnode(const CPatriotnodeBroadcast& mnb)
{
    LOCK(cs);
    vin = mnb.vin;
    addr = mnb.addr;
    pubKeyCollateralAddress = mnb.pubKeyCollateralAddress;
    pubKeyPatriotnode = mnb.pubKeyPatriotnode;
    sig = mnb.sig;
    activeState = PATRIOTNODE_ENABLED;
    sigTime = mnb.sigTime;
    lastPing = mnb.lastPing;
    cacheInputAge = 0;
    cacheInputAgeBlock = 0;
    unitTest = false;
    allowFreeTx = true;
    nActiveState = PATRIOTNODE_ENABLED,
    protocolVersion = mnb.protocolVersion;
    nLastDsq = mnb.nLastDsq;
    nScanningErrorCount = 0;
    nLastScanningErrorBlockHeight = 0;
    lastTimeChecked = 0;
    nLastDsee = 0;  // temporary, do not save. Remove after migration to v12
    nLastDseep = 0; // temporary, do not save. Remove after migration to v12
}

//
// When a new patriotnode broadcast is sent, update our information
//
bool CPatriotnode::UpdateFromNewBroadcast(CPatriotnodeBroadcast& mnb)
{
    if (mnb.sigTime > sigTime) {
        pubKeyPatriotnode = mnb.pubKeyPatriotnode;
        pubKeyCollateralAddress = mnb.pubKeyCollateralAddress;
        sigTime = mnb.sigTime;
        sig = mnb.sig;
        protocolVersion = mnb.protocolVersion;
        addr = mnb.addr;
        lastTimeChecked = 0;
        int nDoS = 0;
        if (mnb.lastPing == CPatriotnodePing() || (mnb.lastPing != CPatriotnodePing() && mnb.lastPing.CheckAndUpdate(nDoS, false))) {
            lastPing = mnb.lastPing;
            mnodeman.mapSeenPatriotnodePing.insert(make_pair(lastPing.GetHash(), lastPing));
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
uint256 CPatriotnode::CalculateScore(int mod, int64_t nBlockHeight)
{
    if (chainActive.Tip() == NULL) return 0;

    uint256 hash = 0;
    uint256 aux = vin.prevout.hash + vin.prevout.n;

    if (!GetBlockHash(hash, nBlockHeight)) {
        LogPrint("patriotnode","CalculateScore ERROR - nHeight %d - Returned 0\n", nBlockHeight);
        return 0;
    }

    CHashWriter ss(SER_GETHASH, PROTOCOL_VERSION);
    ss << hash;
    uint256 hash2 = ss.GetHash();

    CHashWriter ss2(SER_GETHASH, PROTOCOL_VERSION);
    ss2 << hash;
    ss2 << aux;
    uint256 hash3 = ss2.GetHash();

    uint256 r = (hash3 > hash2 ? hash3 - hash2 : hash2 - hash3);

    return r;
}

void CPatriotnode::Check(bool forceCheck)
{
    if (ShutdownRequested()) return;

    if (!forceCheck && (GetTime() - lastTimeChecked < PATRIOTNODE_CHECK_SECONDS)) return;
    lastTimeChecked = GetTime();


    //once spent, stop doing the checks
    if (activeState == PATRIOTNODE_VIN_SPENT) return;


    if (!IsPingedWithin(PATRIOTNODE_REMOVAL_SECONDS)) {
        activeState = PATRIOTNODE_REMOVE;
        return;
    }

    if (!IsPingedWithin(PATRIOTNODE_EXPIRATION_SECONDS)) {
        activeState = PATRIOTNODE_EXPIRED;
        return;
    }

    if(lastPing.sigTime - sigTime < PATRIOTNODE_MIN_MNP_SECONDS){
    	activeState = PATRIOTNODE_PRE_ENABLED;
    	return;
    }

    if (!unitTest) {
        CValidationState state;
        CMutableTransaction tx = CMutableTransaction();
        CTxOut vout = CTxOut(4999.99 * COIN, obfuScationPool.collateralPubKey);
        tx.vin.push_back(vin);
        tx.vout.push_back(vout);

        {
            TRY_LOCK(cs_main, lockMain);
            if (!lockMain) return;

            if (!AcceptableInputs(mempool, state, CTransaction(tx), false, NULL)) {
                activeState = PATRIOTNODE_VIN_SPENT;
                return;
            }
        }
    }

    activeState = PATRIOTNODE_ENABLED; // OK
}

int64_t CPatriotnode::SecondsSincePayment()
{
    CScript pubkeyScript;
    pubkeyScript = GetScriptForDestination(pubKeyCollateralAddress.GetID());

    int64_t sec = (GetAdjustedTime() - GetLastPaid());
    int64_t month = 60 * 60 * 24 * 30;
    if (sec < month) return sec; //if it's less than 30 days, give seconds

    CHashWriter ss(SER_GETHASH, PROTOCOL_VERSION);
    ss << vin;
    ss << sigTime;
    uint256 hash = ss.GetHash();

    // return some deterministic value for unknown/unpaid but force it to be more than 30 days old
    return month + hash.GetCompact(false);
}

int64_t CPatriotnode::GetLastPaid()
{
    CBlockIndex* pindexPrev = chainActive.Tip();
    if (pindexPrev == NULL) return false;

    CScript mnpayee;
    mnpayee = GetScriptForDestination(pubKeyCollateralAddress.GetID());

    CHashWriter ss(SER_GETHASH, PROTOCOL_VERSION);
    ss << vin;
    ss << sigTime;
    uint256 hash = ss.GetHash();

    // use a deterministic offset to break a tie -- 2.5 minutes
    int64_t nOffset = hash.GetCompact(false) % 150;

    if (chainActive.Tip() == NULL) return false;

    const CBlockIndex* BlockReading = chainActive.Tip();

    int nMnCount = mnodeman.CountEnabled() * 1.25;
    int n = 0;
    for (unsigned int i = 1; BlockReading && BlockReading->nHeight > 0; i++) {
        if (n >= nMnCount) {
            return 0;
        }
        n++;

        if (patriotnodePayments.mapPatriotnodeBlocks.count(BlockReading->nHeight)) {
            /*
                Search for this payee, with at least 2 votes. This will aid in consensus allowing the network
                to converge on the same payees quickly, then keep the same schedule.
            */
            if (patriotnodePayments.mapPatriotnodeBlocks[BlockReading->nHeight].HasPayeeWithVotes(mnpayee, 2)) {
                return BlockReading->nTime + nOffset;
            }
        }

        if (BlockReading->pprev == NULL) {
            assert(BlockReading);
            break;
        }
        BlockReading = BlockReading->pprev;
    }

    return 0;
}

std::string CPatriotnode::GetStatus()
{
    switch (nActiveState) {
    case CPatriotnode::PATRIOTNODE_PRE_ENABLED:
        return "PRE_ENABLED";
    case CPatriotnode::PATRIOTNODE_ENABLED:
        return "ENABLED";
    case CPatriotnode::PATRIOTNODE_EXPIRED:
        return "EXPIRED";
    case CPatriotnode::PATRIOTNODE_OUTPOINT_SPENT:
        return "OUTPOINT_SPENT";
    case CPatriotnode::PATRIOTNODE_REMOVE:
        return "REMOVE";
    case CPatriotnode::PATRIOTNODE_WATCHDOG_EXPIRED:
        return "WATCHDOG_EXPIRED";
    case CPatriotnode::PATRIOTNODE_POSE_BAN:
        return "POSE_BAN";
    default:
        return "UNKNOWN";
    }
}

bool CPatriotnode::IsValidNetAddr()
{
    // TODO: regtest is fine with any addresses for now,
    // should probably be a bit smarter if one day we start to implement tests for this
    return Params().NetworkID() == CBaseChainParams::REGTEST ||
           (IsReachable(addr) && addr.IsRoutable());
}

CPatriotnodeBroadcast::CPatriotnodeBroadcast()
{
    vin = CTxIn();
    addr = CService();
    pubKeyCollateralAddress = CPubKey();
    pubKeyPatriotnode1 = CPubKey();
    sig = std::vector<unsigned char>();
    activeState = PATRIOTNODE_ENABLED;
    sigTime = GetAdjustedTime();
    lastPing = CPatriotnodePing();
    cacheInputAge = 0;
    cacheInputAgeBlock = 0;
    unitTest = false;
    allowFreeTx = true;
    protocolVersion = PROTOCOL_VERSION;
    nLastDsq = 0;
    nScanningErrorCount = 0;
    nLastScanningErrorBlockHeight = 0;
}

CPatriotnodeBroadcast::CPatriotnodeBroadcast(CService newAddr, CTxIn newVin, CPubKey pubKeyCollateralAddressNew, CPubKey pubKeyPatriotnodeNew, int protocolVersionIn)
{
    vin = newVin;
    addr = newAddr;
    pubKeyCollateralAddress = pubKeyCollateralAddressNew;
    pubKeyPatriotnode = pubKeyPatriotnodeNew;
    sig = std::vector<unsigned char>();
    activeState = PATRIOTNODE_ENABLED;
    sigTime = GetAdjustedTime();
    lastPing = CPatriotnodePing();
    cacheInputAge = 0;
    cacheInputAgeBlock = 0;
    unitTest = false;
    allowFreeTx = true;
    protocolVersion = protocolVersionIn;
    nLastDsq = 0;
    nScanningErrorCount = 0;
    nLastScanningErrorBlockHeight = 0;
}

CPatriotnodeBroadcast::CPatriotnodeBroadcast(const CPatriotnode& mn)
{
    vin = mn.vin;
    addr = mn.addr;
    pubKeyCollateralAddress = mn.pubKeyCollateralAddress;
    pubKeyPatriotnode = mn.pubKeyPatriotnode;
    sig = mn.sig;
    activeState = mn.activeState;
    sigTime = mn.sigTime;
    lastPing = mn.lastPing;
    cacheInputAge = mn.cacheInputAge;
    cacheInputAgeBlock = mn.cacheInputAgeBlock;
    unitTest = mn.unitTest;
    allowFreeTx = mn.allowFreeTx;
    protocolVersion = mn.protocolVersion;
    nLastDsq = mn.nLastDsq;
    nScanningErrorCount = mn.nScanningErrorCount;
    nLastScanningErrorBlockHeight = mn.nLastScanningErrorBlockHeight;
}

bool CPatriotnodeBroadcast::Create(std::string strService, std::string strKeyPatriotnode, std::string strTxHash, std::string strOutputIndex, std::string& strErrorRet, CPatriotnodeBroadcast& mnbRet, bool fOffline)
{
    CTxIn txin;
    CPubKey pubKeyCollateralAddressNew;
    CKey keyCollateralAddressNew;
    CPubKey pubKeyPatriotnodeNew;
    CKey keyPatriotnodeNew;

    //need correct blocks to send ping
    if (!fOffline && !patriotnodeSync.IsBlockchainSynced()) {
        strErrorRet = "Sync in progress. Must wait until sync is complete to start Patriotnode";
        LogPrint("patriotnode","CPatriotnodeBroadcast::Create -- %s\n", strErrorRet);
        return false;
    }

    if (!obfuScationSigner.GetKeysFromSecret(strKeyPatriotnode, keyPatriotnodeNew, pubKeyPatriotnodeNew)) {
        strErrorRet = strprintf("Invalid patriotnode key %s", strKeyPatriotnode);
        LogPrint("patriotnode","CPatriotnodeBroadcast::Create -- %s\n", strErrorRet);
        return false;
    }

    if (!pwalletMain->GetPatriotnodeVinAndKeys(txin, pubKeyCollateralAddressNew, keyCollateralAddressNew, strTxHash, strOutputIndex)) {
        strErrorRet = strprintf("Could not allocate txin %s:%s for patriotnode %s", strTxHash, strOutputIndex, strService);
        LogPrint("patriotnode","CPatriotnodeBroadcast::Create -- %s\n", strErrorRet);
        return false;
    }

    // The service needs the correct default port to work properly
    if(!CheckDefaultPort(strService, strErrorRet, "CPatriotnodeBroadcast::Create"))
        return false;

    return Create(txin, CService(strService), keyCollateralAddressNew, pubKeyCollateralAddressNew, keyPatriotnodeNew, pubKeyPatriotnodeNew, strErrorRet, mnbRet);
}

bool CPatriotnodeBroadcast::Create(CTxIn txin, CService service, CKey keyCollateralAddressNew, CPubKey pubKeyCollateralAddressNew, CKey keyPatriotnodeNew, CPubKey pubKeyPatriotnodeNew, std::string& strErrorRet, CPatriotnodeBroadcast& mnbRet)
{
    // wait for reindex and/or import to finish
    if (fImporting || fReindex) return false;

    LogPrint("patriotnode", "CPatriotnodeBroadcast::Create -- pubKeyCollateralAddressNew = %s, pubKeyPatriotnodeNew.GetID() = %s\n",
        CBitcoinAddress(pubKeyCollateralAddressNew.GetID()).ToString(),
        pubKeyPatriotnodeNew.GetID().ToString());

    CPatriotnodePing mnp(txin);
    if (!mnp.Sign(keyPatriotnodeNew, pubKeyPatriotnodeNew)) {
        strErrorRet = strprintf("Failed to sign ping, patriotnode=%s", txin.prevout.hash.ToString());
        LogPrint("patriotnode","CPatriotnodeBroadcast::Create -- %s\n", strErrorRet);
        mnbRet = CPatriotnodeBroadcast();
        return false;
    }

    mnbRet = CPatriotnodeBroadcast(service, txin, pubKeyCollateralAddressNew, pubKeyPatriotnodeNew, PROTOCOL_VERSION);

    if (!mnbRet.IsValidNetAddr()) {
        strErrorRet = strprintf("Invalid IP address %s, patriotnode=%s", mnbRet.addr.ToStringIP (), txin.prevout.hash.ToString());
        LogPrint("patriotnode","CPatriotnodeBroadcast::Create -- %s\n", strErrorRet);
        mnbRet = CPatriotnodeBroadcast();
        return false;
    }

    mnbRet.lastPing = mnp;
    if (!mnbRet.Sign(keyCollateralAddressNew)) {
        strErrorRet = strprintf("Failed to sign broadcast, patriotnode=%s", txin.prevout.hash.ToString());
        LogPrint("patriotnode","CPatriotnodeBroadcast::Create -- %s\n", strErrorRet);
        mnbRet = CPatriotnodeBroadcast();
        return false;
    }

    return true;
}

bool CPatriotnodeBroadcast::CheckDefaultPort(std::string strService, std::string& strErrorRet, std::string strContext)
{
    CService service = CService(strService);
    int nDefaultPort = Params().GetDefaultPort();

    if (service.GetPort() != nDefaultPort) {
        strErrorRet = strprintf("Invalid port %u for patriotnode %s, only %d is supported on %s-net.",
                                        service.GetPort(), strService, nDefaultPort, Params().NetworkIDString());
        LogPrint("patriotnode", "%s - %s\n", strContext, strErrorRet);
        return false;
    }

    return true;
}

bool CPatriotnodeBroadcast::CheckAndUpdate(int& nDos)
{
    // make sure signature isn't in the future (past is OK)
    if (sigTime > GetAdjustedTime() + 60 * 60) {
        LogPrint("patriotnode","mnb - Signature rejected, too far into the future %s\n", vin.prevout.hash.ToString());
        nDos = 1;
        return false;
    }

    // incorrect ping or its sigTime
    if(lastPing == CPatriotnodePing() || !lastPing.CheckAndUpdate(nDos, false, true))
    return false;

    if (protocolVersion < patriotnodePayments.GetMinPatriotnodePaymentsProto()) {
        LogPrint("patriotnode","mnb - ignoring outdated Patriotnode %s protocol version %d\n", vin.prevout.hash.ToString(), protocolVersion);
        return false;
    }

    CScript pubkeyScript;
    pubkeyScript = GetScriptForDestination(pubKeyCollateralAddress.GetID());

    if (pubkeyScript.size() != 25) {
        LogPrint("patriotnode","mnb - pubkey the wrong size\n");
        nDos = 100;
        return false;
    }

    CScript pubkeyScript2;
    pubkeyScript2 = GetScriptForDestination(pubKeyPatriotnode.GetID());

    if (pubkeyScript2.size() != 25) {
        LogPrint("patriotnode","mnb - pubkey2 the wrong size\n");
        nDos = 100;
        return false;
    }

    if (!vin.scriptSig.empty()) {
        LogPrint("patriotnode","mnb - Ignore Not Empty ScriptSig %s\n", vin.prevout.hash.ToString());
        return false;
    }

    std::string errorMessage = "";
    if (!obfuScationSigner.VerifyMessage(pubKeyCollateralAddress, sig, GetNewStrMessage(), errorMessage)
    		&& !obfuScationSigner.VerifyMessage(pubKeyCollateralAddress, sig, GetOldStrMessage(), errorMessage))
    {
        // don't ban for old patriotnodes, their sigs could be broken because of the bug
        nDos = protocolVersion < MIN_PEER_MNANNOUNCE ? 0 : 100;
        return error("CPatriotnodeBroadcast::CheckAndUpdate - Got bad Patriotnode address signature : %s", errorMessage);
    }

    if (Params().NetworkID() == CBaseChainParams::MAIN) {
        if (addr.GetPort() != 15110) return false;
    } else if (addr.GetPort() == 15110)
        return false;

    //search existing Patriotnode list, this is where we update existing Patriotnodes with new mnb broadcasts
    CPatriotnode* pmn = mnodeman.Find(vin);

    // no such patriotnode, nothing to update
    if (pmn == NULL) return true;

    // this broadcast is older or equal than the one that we already have - it's bad and should never happen
	// unless someone is doing something fishy
	// (mapSeenPatriotnodeBroadcast in CPatriotnodeMan::ProcessMessage should filter legit duplicates)
	if(pmn->sigTime >= sigTime) {
		return error("CPatriotnodeBroadcast::CheckAndUpdate - Bad sigTime %d for Patriotnode %20s %105s (existing broadcast is at %d)",
					  sigTime, addr.ToString(), vin.ToString(), pmn->sigTime);
    }

    // patriotnode is not enabled yet/already, nothing to update
    if (!pmn->IsEnabled()) return true;

    // mn.pubkey = pubkey, IsVinAssociatedWithPubkey is validated once below,
    //   after that they just need to match
    if (pmn->pubKeyCollateralAddress == pubKeyCollateralAddress && !pmn->IsBroadcastedWithin(PATRIOTNODE_MIN_MNB_SECONDS)) {
        //take the newest entry
        LogPrint("patriotnode","mnb - Got updated entry for %s\n", vin.prevout.hash.ToString());
        if (pmn->UpdateFromNewBroadcast((*this))) {
            pmn->Check();
            if (pmn->IsEnabled()) Relay();
        }
        patriotnodeSync.AddedPatriotnodeList(GetHash());
    }

    return true;
}

bool CPatriotnodeBroadcast::CheckInputsAndAdd(int& nDoS)
{
    // we are a patriotnode with the same vin (i.e. already activated) and this mnb is ours (matches our Patriotnode privkey)
    // so nothing to do here for us
    if (fPatriotNode && vin.prevout == activePatriotnode.vin.prevout && pubKeyPatriotnode == activePatriotnode.pubKeyPatriotnode)
        return true;

    // incorrect ping or its sigTime
    if(lastPing == CPatriotnodePing() || !lastPing.CheckAndUpdate(nDoS, false, true)) return false;

    // search existing Patriotnode list
    CPatriotnode* pmn = mnodeman.Find(vin);

    if (pmn != NULL) {
        // nothing to do here if we already know about this patriotnode and it's enabled
        if (pmn->IsEnabled()) return true;
        // if it's not enabled, remove old MN first and continue
        else
            mnodeman.Remove(pmn->vin);
    }

    CValidationState state;
    CMutableTransaction tx = CMutableTransaction();
    CTxOut vout = CTxOut(4999.99 * COIN, obfuScationPool.collateralPubKey);
    tx.vin.push_back(vin);
    tx.vout.push_back(vout);

    {
        TRY_LOCK(cs_main, lockMain);
        if (!lockMain) {
            // not mnb fault, let it to be checked again later
            mnodeman.mapSeenPatriotnodeBroadcast.erase(GetHash());
            patriotnodeSync.mapSeenSyncMNB.erase(GetHash());
            return false;
        }

        if (!AcceptableInputs(mempool, state, CTransaction(tx), false, NULL)) {
            //set nDos
            state.IsInvalid(nDoS);
            return false;
        }
    }

    LogPrint("patriotnode", "mnb - Accepted Patriotnode entry\n");

    if (GetInputAge(vin) < PATRIOTNODE_MIN_CONFIRMATIONS) {
        LogPrint("patriotnode","mnb - Input must have at least %d confirmations\n", PATRIOTNODE_MIN_CONFIRMATIONS);
        // maybe we miss few blocks, let this mnb to be checked again later
        mnodeman.mapSeenPatriotnodeBroadcast.erase(GetHash());
        patriotnodeSync.mapSeenSyncMNB.erase(GetHash());
        return false;
    }

    // verify that sig time is legit in past
    // should be at least not earlier than block when 1000 TRUMP tx got PATRIOTNODE_MIN_CONFIRMATIONS
    uint256 hashBlock = 0;
    CTransaction tx2;
    GetTransaction(vin.prevout.hash, tx2, hashBlock, true);
    BlockMap::iterator mi = mapBlockIndex.find(hashBlock);
    if (mi != mapBlockIndex.end() && (*mi).second) {
        CBlockIndex* pMNIndex = (*mi).second;                                                        // block for 1000 TrumpCoin tx -> 1 confirmation
        CBlockIndex* pConfIndex = chainActive[pMNIndex->nHeight + PATRIOTNODE_MIN_CONFIRMATIONS - 1]; // block where tx got PATRIOTNODE_MIN_CONFIRMATIONS
        if (pConfIndex->GetBlockTime() > sigTime) {
            LogPrint("patriotnode","mnb - Bad sigTime %d for Patriotnode %s (%i conf block is at %d)\n",
                sigTime, vin.prevout.hash.ToString(), PATRIOTNODE_MIN_CONFIRMATIONS, pConfIndex->GetBlockTime());
            return false;
        }
    }

    LogPrint("patriotnode","mnb - Got NEW Patriotnode entry - %s - %lli \n", vin.prevout.hash.ToString(), sigTime);
    CPatriotnode mn(*this);
    mnodeman.Add(mn);

    // if it matches our Patriotnode privkey, then we've been remotely activated
    if (pubKeyPatriotnode == activePatriotnode.pubKeyPatriotnode && protocolVersion == PROTOCOL_VERSION) {
        activePatriotnode.EnableHotColdPatriotNode(vin, addr);
    }

    bool isLocal = addr.IsRFC1918() || addr.IsLocal();
    if (Params().NetworkID() == CBaseChainParams::REGTEST) isLocal = false;

    if (!isLocal) Relay();

    return true;
}

void CPatriotnodeBroadcast::Relay()
{
    CInv inv(MSG_PATRIOTNODE_ANNOUNCE, GetHash());
    RelayInv(inv);
}

bool CPatriotnodeBroadcast::Sign(CKey& keyCollateralAddress)
{
    std::string errorMessage;
    sigTime = GetAdjustedTime();

    std::string strMessage;
    if(chainActive.Height() < Params().Zerocoin_Block_V2_Start())
    	strMessage = GetOldStrMessage();
    else
    	strMessage = GetNewStrMessage();

    if (!obfuScationSigner.SignMessage(strMessage, errorMessage, sig, keyCollateralAddress))
    	return error("CPatriotnodeBroadcast::Sign() - Error: %s", errorMessage);

    if (!obfuScationSigner.VerifyMessage(pubKeyCollateralAddress, sig, strMessage, errorMessage))
    	return error("CPatriotnodeBroadcast::Sign() - Error: %s", errorMessage);

    return true;
}


bool CPatriotnodeBroadcast::VerifySignature()
{
    std::string errorMessage;

    if(!obfuScationSigner.VerifyMessage(pubKeyCollateralAddress, sig, GetNewStrMessage(), errorMessage)
            && !obfuScationSigner.VerifyMessage(pubKeyCollateralAddress, sig, GetOldStrMessage(), errorMessage))
        return error("CPatriotnodeBroadcast::VerifySignature() - Error: %s", errorMessage);

    return true;
}

std::string CPatriotnodeBroadcast::GetOldStrMessage()
{
    std::string strMessage;

    std::string vchPubKey(pubKeyCollateralAddress.begin(), pubKeyCollateralAddress.end());
    std::string vchPubKey2(pubKeyPatriotnode.begin(), pubKeyPatriotnode.end());
    strMessage = addr.ToString() + boost::lexical_cast<std::string>(sigTime) + vchPubKey + vchPubKey2 + boost::lexical_cast<std::string>(protocolVersion);

    return strMessage;
}

std:: string CPatriotnodeBroadcast::GetNewStrMessage()
{
    std::string strMessage;

    strMessage = addr.ToString() + boost::lexical_cast<std::string>(sigTime) + pubKeyCollateralAddress.GetID().ToString() + pubKeyPatriotnode.GetID().ToString() + boost::lexical_cast<std::string>(protocolVersion);

    return strMessage;
}

CPatriotnodePing::CPatriotnodePing()
{
    vin = CTxIn();
    blockHash = uint256(0);
    sigTime = 0;
    vchSig = std::vector<unsigned char>();
}

CPatriotnodePing::CPatriotnodePing(CTxIn& newVin)
{
    vin = newVin;
    blockHash = chainActive[chainActive.Height() - 12]->GetBlockHash();
    sigTime = GetAdjustedTime();
    vchSig = std::vector<unsigned char>();
}


bool CPatriotnodePing::Sign(CKey& keyPatriotnode, CPubKey& pubKeyPatriotnode)
{
    std::string errorMessage;
    std::string strPatriotNodeSignMessage;

    sigTime = GetAdjustedTime();
    std::string strMessage = vin.ToString() + blockHash.ToString() + boost::lexical_cast<std::string>(sigTime);

    if (!obfuScationSigner.SignMessage(strMessage, errorMessage, vchSig, keyPatriotnode)) {
        LogPrint("patriotnode","CPatriotnodePing::Sign() - Error: %s\n", errorMessage);
        return false;
    }

    if (!obfuScationSigner.VerifyMessage(pubKeyPatriotnode, vchSig, strMessage, errorMessage)) {
        LogPrint("patriotnode","CPatriotnodePing::Sign() - Error: %s\n", errorMessage);
        return false;
    }

    return true;
}

bool CPatriotnodePing::VerifySignature(CPubKey& pubKeyPatriotnode, int &nDos) {
	std::string strMessage = vin.ToString() + blockHash.ToString() + boost::lexical_cast<std::string>(sigTime);
	std::string errorMessage = "";

	if(!obfuScationSigner.VerifyMessage(pubKeyPatriotnode, vchSig, strMessage, errorMessage)){
		nDos = 33;
		return error("CPatriotnodePing::VerifySignature - Got bad Patriotnode ping signature %s Error: %s", vin.ToString(), errorMessage);
	}
	return true;
}

bool CPatriotnodePing::CheckAndUpdate(int& nDos, bool fRequireEnabled, bool fCheckSigTimeOnly)
{
    if (sigTime > GetAdjustedTime() + 60 * 60) {
        LogPrint("patriotnode","CPatriotnodePing::CheckAndUpdate - Signature rejected, too far into the future %s\n", vin.prevout.hash.ToString());
        nDos = 1;
        return false;
    }

    if (sigTime <= GetAdjustedTime() - 60 * 60) {
        LogPrint("patriotnode","CPatriotnodePing::CheckAndUpdate - Signature rejected, too far into the past %s - %d %d \n", vin.prevout.hash.ToString(), sigTime, GetAdjustedTime());
        nDos = 1;
        return false;
    }

    if(fCheckSigTimeOnly) {
    	CPatriotnode* pmn = mnodeman.Find(vin);
    	if(pmn) return VerifySignature(pmn->pubKeyPatriotnode, nDos);
    	return true;
    }

    LogPrint("patriotnode", "CPatriotnodePing::CheckAndUpdate - New Ping - %s - %s - %lli\n", GetHash().ToString(), blockHash.ToString(), sigTime);

    // see if we have this Patriotnode
    CPatriotnode* pmn = mnodeman.Find(vin);
    if (pmn != NULL && pmn->protocolVersion >= patriotnodePayments.GetMinPatriotnodePaymentsProto()) {
        if (fRequireEnabled && !pmn->IsEnabled()) return false;

        // LogPrint("patriotnode","mnping - Found corresponding mn for vin: %s\n", vin.ToString());
        // update only if there is no known ping for this patriotnode or
        // last ping was more then PATRIOTNODE_MIN_MNP_SECONDS-60 ago comparing to this one
        if (!pmn->IsPingedWithin(PATRIOTNODE_MIN_MNP_SECONDS - 60, sigTime)) {
        	if (!VerifySignature(pmn->pubKeyPatriotnode, nDos))
                return false;

            BlockMap::iterator mi = mapBlockIndex.find(blockHash);
            if (mi != mapBlockIndex.end() && (*mi).second) {
                if ((*mi).second->nHeight < chainActive.Height() - 24) {
                    LogPrint("patriotnode","CPatriotnodePing::CheckAndUpdate - Patriotnode %s block hash %s is too old\n", vin.prevout.hash.ToString(), blockHash.ToString());
                    // Do nothing here (no Patriotnode update, no mnping relay)
                    // Let this node to be visible but fail to accept mnping

                    return false;
                }
            } else {
                if (fDebug) LogPrint("patriotnode","CPatriotnodePing::CheckAndUpdate - Patriotnode %s block hash %s is unknown\n", vin.prevout.hash.ToString(), blockHash.ToString());
                // maybe we stuck so we shouldn't ban this node, just fail to accept it
                // TODO: or should we also request this block?

                return false;
            }

            pmn->lastPing = *this;

            //mnodeman.mapSeenPatriotnodeBroadcast.lastPing is probably outdated, so we'll update it
            CPatriotnodeBroadcast mnb(*pmn);
            uint256 hash = mnb.GetHash();
            if (mnodeman.mapSeenPatriotnodeBroadcast.count(hash)) {
                mnodeman.mapSeenPatriotnodeBroadcast[hash].lastPing = *this;
            }

            pmn->Check(true);
            if (!pmn->IsEnabled()) return false;

            LogPrint("patriotnode", "CPatriotnodePing::CheckAndUpdate - Patriotnode ping accepted, vin: %s\n", vin.prevout.hash.ToString());

            Relay();
            return true;
        }
        LogPrint("patriotnode", "CPatriotnodePing::CheckAndUpdate - Patriotnode ping arrived too early, vin: %s\n", vin.prevout.hash.ToString());
        //nDos = 1; //disable, this is happening frequently and causing banned peers
        return false;
    }
    LogPrint("patriotnode", "CPatriotnodePing::CheckAndUpdate - Couldn't find compatible Patriotnode entry, vin: %s\n", vin.prevout.hash.ToString());

    return false;
}

void CPatriotnodePing::Relay()
{
    CInv inv(MSG_PATRIOTNODE_PING, GetHash());
    RelayInv(inv);
}
