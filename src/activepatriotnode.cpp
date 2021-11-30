// Copyright (c) 2014-2016 The Dash developers
// Copyright (c) 2015-2020 The TrumpCoin developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "activepatriotnode.h"

#include "addrman.h"
#include "evo/providertx.h"
#include "patriotnode-sync.h"
#include "patriotnode.h"
#include "patriotnodeconfig.h"
#include "patriotnodeman.h"
#include "messagesigner.h"
#include "netbase.h"
#include "protocol.h"

// Keep track of the active Patriotnode
CActiveDeterministicPatriotnodeManager* activePatriotnodeManager{nullptr};

static bool GetLocalAddress(CService& addrRet)
{
    // First try to find whatever local address is specified by externalip option
    bool fFound = GetLocal(addrRet) && CActiveDeterministicPatriotnodeManager::IsValidNetAddr(addrRet);
    if (!fFound && Params().IsRegTestNet()) {
        if (Lookup("127.0.0.1", addrRet, GetListenPort(), false)) {
            fFound = true;
        }
    }
    if(!fFound) {
        // If we have some peers, let's try to find our local address from one of them
        g_connman->ForEachNodeContinueIf([&fFound, &addrRet](CNode* pnode) {
            if (pnode->addr.IsIPv4())
                fFound = GetLocal(addrRet, &pnode->addr) && CActiveDeterministicPatriotnodeManager::IsValidNetAddr(addrRet);
            return !fFound;
        });
    }
    return fFound;
}

std::string CActiveDeterministicPatriotnodeManager::GetStatus() const
{
    switch (state) {
        case PATRIOTNODE_WAITING_FOR_PROTX:    return "Waiting for ProTx to appear on-chain";
        case PATRIOTNODE_POSE_BANNED:          return "Patriotnode was PoSe banned";
        case PATRIOTNODE_REMOVED:              return "Patriotnode removed from list";
        case PATRIOTNODE_OPERATOR_KEY_CHANGED: return "Operator key changed or revoked";
        case PATRIOTNODE_PROTX_IP_CHANGED:     return "IP address specified in ProTx changed";
        case PATRIOTNODE_READY:                return "Ready";
        case PATRIOTNODE_ERROR:                return "Error. " + strError;
        default:                              return "Unknown";
    }
}

OperationResult CActiveDeterministicPatriotnodeManager::SetOperatorKey(const std::string& strPNOperatorPrivKey)
{

    LOCK(cs_main); // Lock cs_main so the node doesn't perform any action while we setup the Patriotnode
    LogPrintf("Initializing deterministic patriotnode...\n");
    if (strPNOperatorPrivKey.empty()) {
        return errorOut("ERROR: Patriotnode operator priv key cannot be empty.");
    }
    if (!CMessageSigner::GetKeysFromSecret(strPNOperatorPrivKey, info.keyOperator, info.keyIDOperator)) {
        return errorOut(_("Invalid mnoperatorprivatekey. Please see the documentation."));
    }
    return OperationResult(true);
}

OperationResult CActiveDeterministicPatriotnodeManager::GetOperatorKey(CKey& key, CKeyID& keyID, CDeterministicPNCPtr& dmn) const
{
    if (!IsReady()) {
        return errorOut("Active patriotnode not ready");
    }
    dmn = deterministicPNManager->GetListAtChainTip().GetValidPN(info.proTxHash);
    if (!dmn) {
        return errorOut(strprintf("Active patriotnode %s not registered or PoSe banned", info.proTxHash.ToString()));
    }
    if (info.keyIDOperator != dmn->pdmnState->keyIDOperator) {
        return errorOut("Active patriotnode operator key changed or revoked");
    }
    // return keys
    key = info.keyOperator;
    keyID = info.keyIDOperator;
    return OperationResult(true);
}

void CActiveDeterministicPatriotnodeManager::Init()
{
    // set patriotnode arg if called from RPC
    if (!fPatriotNode) {
        gArgs.ForceSetArg("-patriotnode", "1");
        fPatriotNode = true;
    }

    if (!deterministicPNManager->IsDIP3Enforced()) {
        state = PATRIOTNODE_ERROR;
        strError = "Evo upgrade is not active yet.";
        LogPrintf("%s -- ERROR: %s\n", __func__, strError);
        return;
    }

    LOCK(cs_main);

    // Check that our local network configuration is correct
    if (!fListen) {
        // listen option is probably overwritten by smth else, no good
        state = PATRIOTNODE_ERROR;
        strError = "Patriotnode must accept connections from outside. Make sure listen configuration option is not overwritten by some another parameter.";
        LogPrintf("%s ERROR: %s\n", __func__, strError);
        return;
    }

    if (!GetLocalAddress(info.service)) {
        state = PATRIOTNODE_ERROR;
        strError = "Can't detect valid external address. Please consider using the externalip configuration option if problem persists. Make sure to use IPv4 address only.";
        LogPrintf("%s ERROR: %s\n", __func__, strError);
        return;
    }

    CDeterministicPNList mnList = deterministicPNManager->GetListAtChainTip();

    CDeterministicPNCPtr dmn = mnList.GetPNByOperatorKey(info.keyIDOperator);
    if (!dmn) {
        // PN not appeared on the chain yet
        return;
    }

    if (!mnList.IsPNValid(dmn->proTxHash)) {
        if (mnList.IsPNPoSeBanned(dmn->proTxHash)) {
            state = PATRIOTNODE_POSE_BANNED;
        } else {
            state = PATRIOTNODE_REMOVED;
        }
        return;
    }

    LogPrintf("%s: proTxHash=%s, proTx=%s\n", __func__, dmn->proTxHash.ToString(), dmn->ToString());

    info.proTxHash = dmn->proTxHash;

    if (info.service != dmn->pdmnState->addr) {
        state = PATRIOTNODE_ERROR;
        strError = strprintf("Local address %s does not match the address from ProTx (%s)",
                             info.service.ToStringIPPort(), dmn->pdmnState->addr.ToStringIPPort());
        LogPrintf("%s ERROR: %s\n", __func__, strError);
        return;
    }

    if (!Params().IsRegTestNet()) {
        // Check socket connectivity
        const std::string& strService = info.service.ToString();
        LogPrintf("%s: Checking inbound connection to '%s'\n", __func__, strService);
        SOCKET hSocket;
        bool fConnected = ConnectSocket(info.service, hSocket, nConnectTimeout) && IsSelectableSocket(hSocket);
        CloseSocket(hSocket);

        if (!fConnected) {
            state = PATRIOTNODE_ERROR;
            LogPrintf("%s ERROR: Could not connect to %s\n", __func__, strService);
            return;
        }
    }

    state = PATRIOTNODE_READY;
}

void CActiveDeterministicPatriotnodeManager::Reset(patriotnode_state_t _state)
{
    state = _state;
    SetNullProTx();
    // PN might have reappeared in same block with a new ProTx
    Init();
}

void CActiveDeterministicPatriotnodeManager::UpdatedBlockTip(const CBlockIndex* pindexNew, const CBlockIndex* pindexFork, bool fInitialDownload)
{
    if (fInitialDownload)
        return;

    if (!fPatriotNode || !deterministicPNManager->IsDIP3Enforced())
        return;

    if (state == PATRIOTNODE_READY) {
        auto oldPNList = deterministicPNManager->GetListForBlock(pindexNew->pprev);
        auto newPNList = deterministicPNManager->GetListForBlock(pindexNew);
        if (!newPNList.IsPNValid(info.proTxHash)) {
            // PN disappeared from PN list
            Reset(PATRIOTNODE_REMOVED);
            return;
        }

        auto oldDmn = oldPNList.GetPN(info.proTxHash);
        auto newDmn = newPNList.GetPN(info.proTxHash);
        if (newDmn->pdmnState->keyIDOperator != oldDmn->pdmnState->keyIDOperator) {
            // PN operator key changed or revoked
            Reset(PATRIOTNODE_OPERATOR_KEY_CHANGED);
            return;
        }

        if (newDmn->pdmnState->addr != oldDmn->pdmnState->addr) {
            // PN IP changed
            Reset(PATRIOTNODE_PROTX_IP_CHANGED);
            return;
        }
    } else {
        // PN might have (re)appeared with a new ProTx or we've found some peers
        // and figured out our local address
        Init();
    }
}

bool CActiveDeterministicPatriotnodeManager::IsValidNetAddr(const CService& addrIn)
{
    // TODO: check IPv6 and TOR addresses
    return Params().IsRegTestNet() || (addrIn.IsIPv4() && IsReachable(addrIn) && addrIn.IsRoutable());
}


/********* LEGACY *********/

OperationResult initPatriotnode(const std::string& _strPatriotNodePrivKey, const std::string& _strPatriotNodeAddr, bool isFromInit)
{
    if (!isFromInit && fPatriotNode) {
        return errorOut( "ERROR: Patriotnode already initialized.");
    }

    LOCK(cs_main); // Lock cs_main so the node doesn't perform any action while we setup the Patriotnode
    LogPrintf("Initializing patriotnode, addr %s..\n", _strPatriotNodeAddr.c_str());

    if (_strPatriotNodePrivKey.empty()) {
        return errorOut("ERROR: Patriotnode priv key cannot be empty.");
    }

    if (_strPatriotNodeAddr.empty()) {
        return errorOut("ERROR: Empty patriotnodeaddr");
    }

    // Global params set
    strPatriotNodeAddr = _strPatriotNodeAddr;

    // Address parsing.
    const CChainParams& params = Params();
    int nPort = 0;
    int nDefaultPort = params.GetDefaultPort();
    std::string strHost;
    SplitHostPort(strPatriotNodeAddr, nPort, strHost);

    // Allow for the port number to be omitted here and just double check
    // that if a port is supplied, it matches the required default port.
    if (nPort == 0) nPort = nDefaultPort;
    if (nPort != nDefaultPort && !params.IsRegTestNet()) {
        return errorOut(strprintf(_("Invalid -patriotnodeaddr port %d, only %d is supported on %s-net."),
                                           nPort, nDefaultPort, Params().NetworkIDString()));
    }
    CService addrTest(LookupNumeric(strHost.c_str(), nPort));
    if (!addrTest.IsValid()) {
        return errorOut(strprintf(_("Invalid -patriotnodeaddr address: %s"), strPatriotNodeAddr));
    }

    // Peer port needs to match the patriotnode public one for IPv4 and IPv6.
    // Onion can run in other ports because those are behind a hidden service which has the public port fixed to the default port.
    if (nPort != GetListenPort() && !addrTest.IsTor()) {
        return errorOut(strprintf(_("Invalid -patriotnodeaddr port %d, isn't the same as the peer port %d"),
                                  nPort, GetListenPort()));
    }

    CKey key;
    CPubKey pubkey;
    if (!CMessageSigner::GetKeysFromSecret(_strPatriotNodePrivKey, key, pubkey)) {
        return errorOut(_("Invalid patriotnodeprivkey. Please see the documentation."));
    }

    activePatriotnode.pubKeyPatriotnode = pubkey;
    activePatriotnode.privKeyPatriotnode = key;
    activePatriotnode.service = addrTest;
    fPatriotNode = true;

    if (patriotnodeSync.IsBlockchainSynced()) {
        // Check if the patriotnode already exists in the list
        CPatriotnode* pmn = mnodeman.Find(pubkey);
        if (pmn) activePatriotnode.EnableHotColdPatriotNode(pmn->vin, pmn->addr);
    }

    return OperationResult(true);
}

//
// Bootup the Patriotnode, look for a 5000 TRUMP input and register on the network
//
void CActivePatriotnode::ManageStatus()
{
    if (!fPatriotNode) return;
    if (activePatriotnodeManager != nullptr) {
        // Deterministic patriotnode
        return;
    }

    // !TODO: Legacy patriotnodes - remove after enforcement
    LogPrint(BCLog::PATRIOTNODE, "CActivePatriotnode::ManageStatus() - Begin\n");

    // If a DPN has been registered with same collateral, disable me.
    CPatriotnode* pmn = mnodeman.Find(pubKeyPatriotnode);
    if (pmn && deterministicPNManager->GetListAtChainTip().HasPNByCollateral(pmn->vin.prevout)) {
        LogPrintf("%s: Disabling active legacy Patriotnode %s as the collateral is now registered with a DPN\n",
                         __func__, pmn->vin.prevout.ToString());
        status = ACTIVE_PATRIOTNODE_NOT_CAPABLE;
        notCapableReason = "Collateral registered with DPN";
        return;
    }

    //need correct blocks to send ping
    if (!Params().IsRegTestNet() && !patriotnodeSync.IsBlockchainSynced()) {
        status = ACTIVE_PATRIOTNODE_SYNC_IN_PROCESS;
        LogPrintf("CActivePatriotnode::ManageStatus() - %s\n", GetStatusMessage());
        return;
    }

    if (status == ACTIVE_PATRIOTNODE_SYNC_IN_PROCESS) status = ACTIVE_PATRIOTNODE_INITIAL;

    if (status == ACTIVE_PATRIOTNODE_INITIAL) {
        if (pmn) {
            if (pmn->protocolVersion != PROTOCOL_VERSION) {
                LogPrintf("%s: ERROR Trying to start a patriotnode running an old protocol version, "
                          "the controller and patriotnode wallets need to be running the latest release version.\n", __func__);
                return;
            }
            // Update vin and service
            EnableHotColdPatriotNode(pmn->vin, pmn->addr);
        }
    }

    if (status != ACTIVE_PATRIOTNODE_STARTED) {
        // Set defaults
        status = ACTIVE_PATRIOTNODE_NOT_CAPABLE;
        notCapableReason = "";

        LogPrintf("%s - Checking inbound connection for patriotnode to '%s'\n", __func__ , service.ToString());

        CAddress addr(service, NODE_NETWORK);
        if (!g_connman->IsNodeConnected(addr)) {
            CNode* node = g_connman->ConnectNode(addr);
            if (!node) {
                notCapableReason =
                        "Patriotnode address:port connection availability test failed, could not open a connection to the public patriotnode address (" +
                        service.ToString() + ")";
                LogPrintf("%s - not capable: %s\n", __func__, notCapableReason);
            } else {
                // don't leak allocated object in memory
                delete node;
            }
            return;
        }

        notCapableReason = "Waiting for start message from controller.";
        return;
    }

    //send to all peers
    std::string errorMessage;
    if (!SendPatriotnodePing(errorMessage)) {
        LogPrintf("CActivePatriotnode::ManageStatus() - Error on Ping: %s\n", errorMessage);
    }
}

void CActivePatriotnode::ResetStatus()
{
    status = ACTIVE_PATRIOTNODE_INITIAL;
    ManageStatus();
}

std::string CActivePatriotnode::GetStatusMessage() const
{
    switch (status) {
    case ACTIVE_PATRIOTNODE_INITIAL:
        return "Node just started, not yet activated";
    case ACTIVE_PATRIOTNODE_SYNC_IN_PROCESS:
        return "Sync in progress. Must wait until sync is complete to start Patriotnode";
    case ACTIVE_PATRIOTNODE_NOT_CAPABLE:
        return "Not capable patriotnode: " + notCapableReason;
    case ACTIVE_PATRIOTNODE_STARTED:
        return "Patriotnode successfully started";
    default:
        return "unknown";
    }
}

bool CActivePatriotnode::SendPatriotnodePing(std::string& errorMessage)
{
    if (vin == nullopt) {
        errorMessage = "Active Patriotnode not initialized";
        return false;
    }

    if (status != ACTIVE_PATRIOTNODE_STARTED) {
        errorMessage = "Patriotnode is not in a running status";
        return false;
    }

    if (!privKeyPatriotnode.IsValid() || !pubKeyPatriotnode.IsValid()) {
        errorMessage = "Error upon patriotnode key.\n";
        return false;
    }

    LogPrintf("CActivePatriotnode::SendPatriotnodePing() - Relay Patriotnode Ping vin = %s\n", vin->ToString());

    const uint256& nBlockHash = mnodeman.GetBlockHashToPing();
    CPatriotnodePing mnp(*vin, nBlockHash, GetAdjustedTime());
    if (!mnp.Sign(privKeyPatriotnode, pubKeyPatriotnode.GetID())) {
        errorMessage = "Couldn't sign Patriotnode Ping";
        return false;
    }

    // Update lastPing for our patriotnode in Patriotnode list
    CPatriotnode* pmn = mnodeman.Find(vin->prevout);
    if (pmn != NULL) {
        if (pmn->IsPingedWithin(PatriotnodePingSeconds(), mnp.sigTime)) {
            errorMessage = "Too early to send Patriotnode Ping";
            return false;
        }

        // SetLastPing locks the patriotnode cs, be careful with the lock order.
        pmn->SetLastPing(mnp);
        mnodeman.mapSeenPatriotnodePing.emplace(mnp.GetHash(), mnp);

        //mnodeman.mapSeenPatriotnodeBroadcast.lastPing is probably outdated, so we'll update it
        CPatriotnodeBroadcast mnb(*pmn);
        uint256 hash = mnb.GetHash();
        if (mnodeman.mapSeenPatriotnodeBroadcast.count(hash)) {
            // SetLastPing locks the patriotnode cs, be careful with the lock order.
            // TODO: check why are we double setting the last ping here..
            mnodeman.mapSeenPatriotnodeBroadcast[hash].SetLastPing(mnp);
        }

        mnp.Relay();
        return true;

    } else {
        // Seems like we are trying to send a ping while the Patriotnode is not registered in the network
        errorMessage = "Patriotnode List doesn't include our Patriotnode, shutting down Patriotnode pinging service! " + vin->ToString();
        status = ACTIVE_PATRIOTNODE_NOT_CAPABLE;
        notCapableReason = errorMessage;
        return false;
    }
}

// when starting a Patriotnode, this can enable to run as a hot wallet with no funds
bool CActivePatriotnode::EnableHotColdPatriotNode(CTxIn& newVin, CService& newService)
{
    if (!fPatriotNode) return false;

    status = ACTIVE_PATRIOTNODE_STARTED;

    //The values below are needed for signing mnping messages going forward
    vin = newVin;
    service = newService;

    LogPrintf("CActivePatriotnode::EnableHotColdPatriotNode() - Enabled! You may shut down the cold daemon.\n");

    return true;
}

void CActivePatriotnode::GetKeys(CKey& _privKeyPatriotnode, CPubKey& _pubKeyPatriotnode)
{
    if (!privKeyPatriotnode.IsValid() || !pubKeyPatriotnode.IsValid()) {
        throw std::runtime_error("Error trying to get patriotnode keys");
    }
    _privKeyPatriotnode = privKeyPatriotnode;
    _pubKeyPatriotnode = pubKeyPatriotnode;
}

bool GetActivePatriotnodeKeys(CKey& key, CKeyID& keyID, CTxIn& vin)
{
    if (activePatriotnodeManager != nullptr) {
        // deterministic mn
        CDeterministicPNCPtr dmn;
        auto res = activePatriotnodeManager->GetOperatorKey(key, keyID, dmn);
        if (!res) {
            LogPrint(BCLog::PNBUDGET,"%s: %s\n", __func__, res.getError());
            return false;
        }
        vin = CTxIn(dmn->collateralOutpoint);
        return true;
    }

    // legacy mn
    if (activePatriotnode.vin == nullopt) {
        LogPrint(BCLog::PNBUDGET,"%s: Active Patriotnode not initialized\n", __func__);
        return false;
    }
    if (activePatriotnode.GetStatus() != ACTIVE_PATRIOTNODE_STARTED) {
        LogPrint(BCLog::PNBUDGET,"%s: PN not started (%s)\n", __func__, activePatriotnode.GetStatusMessage());
        return false;
    }
    CPubKey mnPubKey;
    activePatriotnode.GetKeys(key, mnPubKey);
    keyID = mnPubKey.GetID();
    vin = *activePatriotnode.vin;
    return true;
}
