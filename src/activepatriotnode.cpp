// Copyright (c) 2014-2016 The Dash developers
// Copyright (c) 2015-2018 The PIVX developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "activepatriotnode.h"
#include "addrman.h"
#include "patriotnode.h"
#include "patriotnodeconfig.h"
#include "patriotnodeman.h"
#include "protocol.h"
#include "spork.h"

//
// Bootup the Patriotnode, look for a 1000 TrumpCoin input and register on the network
//
void CActivePatriotnode::ManageStatus()
{
    std::string errorMessage;

    if (!fPatriotNode) return;

    if (fDebug) LogPrintf("CActivePatriotnode::ManageStatus() - Begin\n");

    //need correct blocks to send ping
    if (Params().NetworkID() != CBaseChainParams::REGTEST && !patriotnodeSync.IsBlockchainSynced()) {
        status = ACTIVE_PATRIOTNODE_SYNC_IN_PROCESS;
        LogPrintf("CActivePatriotnode::ManageStatus() - %s\n", GetStatus());
        return;
    }

    if (status == ACTIVE_PATRIOTNODE_SYNC_IN_PROCESS) status = ACTIVE_PATRIOTNODE_INITIAL;

    if (status == ACTIVE_PATRIOTNODE_INITIAL) {
        CPatriotnode* pmn;
        pmn = mnodeman.Find(pubKeyPatriotnode);
        if (pmn != NULL) {
            pmn->Check();
            if (pmn->IsEnabled() && pmn->protocolVersion == PROTOCOL_VERSION) EnableHotColdPatriotNode(pmn->vin, pmn->addr);
        }
    }

    if (status != ACTIVE_PATRIOTNODE_STARTED) {
        // Set defaults
        status = ACTIVE_PATRIOTNODE_NOT_CAPABLE;
        notCapableReason = "";

        if (pwalletMain->IsLocked()) {
            notCapableReason = "Wallet is locked.";
            LogPrintf("CActivePatriotnode::ManageStatus() - not capable: %s\n", notCapableReason);
            return;
        }

        if (pwalletMain->GetBalance() == 0) {
            notCapableReason = "Hot node, waiting for remote activation.";
            LogPrintf("CActivePatriotnode::ManageStatus() - not capable: %s\n", notCapableReason);
            return;
        }

        if (strPatriotNodeAddr.empty()) {
            if (!GetLocal(service)) {
                notCapableReason = "Can't detect external address. Please use the patriotnodeaddr configuration option.";
                LogPrintf("CActivePatriotnode::ManageStatus() - not capable: %s\n", notCapableReason);
                return;
            }
        } else {
            service = CService(strPatriotNodeAddr);
        }

        // The service needs the correct default port to work properly
        if(!CPatriotnodeBroadcast::CheckDefaultPort(strPatriotNodeAddr, errorMessage, "CActivePatriotnode::ManageStatus()"))
            return;

        LogPrintf("CActivePatriotnode::ManageStatus() - Checking inbound connection to '%s'\n", service.ToString());

        CNode* pnode = ConnectNode((CAddress)service, NULL, false);
        if (!pnode) {
            notCapableReason = "Could not connect to " + service.ToString();
            LogPrintf("CActivePatriotnode::ManageStatus() - not capable: %s\n", notCapableReason);
            return;
        }
        pnode->Release();

        // Choose coins to use
        CPubKey pubKeyCollateralAddress;
        CKey keyCollateralAddress;

        if (GetPatriotNodeVin(vin, pubKeyCollateralAddress, keyCollateralAddress)) {
            if (GetInputAge(vin) < PATRIOTNODE_MIN_CONFIRMATIONS) {
                status = ACTIVE_PATRIOTNODE_INPUT_TOO_NEW;
                notCapableReason = strprintf("%s - %d confirmations", GetStatus(), GetInputAge(vin));
                LogPrintf("CActivePatriotnode::ManageStatus() - %s\n", notCapableReason);
                return;
            }

            LOCK(pwalletMain->cs_wallet);
            pwalletMain->LockCoin(vin.prevout);

            // send to all nodes
            CPubKey pubKeyPatriotnode;
            CKey keyPatriotnode;

            if (!obfuScationSigner.SetKey(strPatriotNodePrivKey, errorMessage, keyPatriotnode, pubKeyPatriotnode)) {
                notCapableReason = "Error upon calling SetKey: " + errorMessage;
                LogPrintf("Register::ManageStatus() - %s\n", notCapableReason);
                return;
            }

            CPatriotnodeBroadcast mnb;
            if (!CreateBroadcast(vin, service, keyCollateralAddress, pubKeyCollateralAddress, keyPatriotnode, pubKeyPatriotnode, errorMessage, mnb)) {
                notCapableReason = "Error on Register: " + errorMessage;
                LogPrintf("CActivePatriotnode::ManageStatus() - %s\n", notCapableReason);
                return;
            }

            //send to all peers
            LogPrintf("CActivePatriotnode::ManageStatus() - Relay broadcast vin = %s\n", vin.ToString());
            mnb.Relay();

            LogPrintf("CActivePatriotnode::ManageStatus() - Is capable master node!\n");
            status = ACTIVE_PATRIOTNODE_STARTED;

            return;
        } else {
            notCapableReason = "Could not find suitable coins!";
            LogPrintf("CActivePatriotnode::ManageStatus() - %s\n", notCapableReason);
            return;
        }
    }

    //send to all peers
    if (!SendPatriotnodePing(errorMessage)) {
        LogPrintf("CActivePatriotnode::ManageStatus() - Error on Ping: %s\n", errorMessage);
    }
}

std::string CActivePatriotnode::GetStatus()
{
    switch (status) {
    case ACTIVE_PATRIOTNODE_INITIAL:
        return "Node just started, not yet activated";
    case ACTIVE_PATRIOTNODE_SYNC_IN_PROCESS:
        return "Sync in progress. Must wait until sync is complete to start Patriotnode";
    case ACTIVE_PATRIOTNODE_INPUT_TOO_NEW:
        return strprintf("Patriotnode input must have at least %d confirmations", PATRIOTNODE_MIN_CONFIRMATIONS);
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
    if (status != ACTIVE_PATRIOTNODE_STARTED) {
        errorMessage = "Patriotnode is not in a running status";
        return false;
    }

    CPubKey pubKeyPatriotnode;
    CKey keyPatriotnode;

    if (!obfuScationSigner.SetKey(strPatriotNodePrivKey, errorMessage, keyPatriotnode, pubKeyPatriotnode)) {
        errorMessage = strprintf("Error upon calling SetKey: %s\n", errorMessage);
        return false;
    }

    LogPrintf("CActivePatriotnode::SendPatriotnodePing() - Relay Patriotnode Ping vin = %s\n", vin.ToString());

    CPatriotnodePing mnp(vin);
    if (!mnp.Sign(keyPatriotnode, pubKeyPatriotnode)) {
        errorMessage = "Couldn't sign Patriotnode Ping";
        return false;
    }

    // Update lastPing for our patriotnode in Patriotnode list
    CPatriotnode* pmn = mnodeman.Find(vin);
    if (pmn != NULL) {
        if (pmn->IsPingedWithin(PATRIOTNODE_PING_SECONDS, mnp.sigTime)) {
            errorMessage = "Too early to send Patriotnode Ping";
            return false;
        }

        pmn->lastPing = mnp;
        mnodeman.mapSeenPatriotnodePing.insert(make_pair(mnp.GetHash(), mnp));

        //mnodeman.mapSeenPatriotnodeBroadcast.lastPing is probably outdated, so we'll update it
        CPatriotnodeBroadcast mnb(*pmn);
        uint256 hash = mnb.GetHash();
        if (mnodeman.mapSeenPatriotnodeBroadcast.count(hash)) mnodeman.mapSeenPatriotnodeBroadcast[hash].lastPing = mnp;

        mnp.Relay();

        /*
         * IT'S SAFE TO REMOVE THIS IN FURTHER VERSIONS
         * AFTER MIGRATION TO V12 IS DONE
         */

        if (IsSporkActive(SPORK_10_PATRIOTNODE_PAY_UPDATED_NODES)) return true;
        // for migration purposes ping our node on old patriotnodes network too
        std::string retErrorMessage;
        std::vector<unsigned char> vchPatriotNodeSignature;
        int64_t patriotNodeSignatureTime = GetAdjustedTime();

        std::string strMessage = service.ToString() + boost::lexical_cast<std::string>(patriotNodeSignatureTime) + boost::lexical_cast<std::string>(false);

        if (!obfuScationSigner.SignMessage(strMessage, retErrorMessage, vchPatriotNodeSignature, keyPatriotnode)) {
            errorMessage = "dseep sign message failed: " + retErrorMessage;
            return false;
        }

        if (!obfuScationSigner.VerifyMessage(pubKeyPatriotnode, vchPatriotNodeSignature, strMessage, retErrorMessage)) {
            errorMessage = "dseep verify message failed: " + retErrorMessage;
            return false;
        }

        LogPrint("patriotnode", "dseep - relaying from active mn, %s \n", vin.ToString().c_str());
        LOCK(cs_vNodes);
        BOOST_FOREACH (CNode* pnode, vNodes)
            pnode->PushMessage("dseep", vin, vchPatriotNodeSignature, patriotNodeSignatureTime, false);

        /*
         * END OF "REMOVE"
         */

        return true;
    } else {
        // Seems like we are trying to send a ping while the Patriotnode is not registered in the network
        errorMessage = "Obfuscation Patriotnode List doesn't include our Patriotnode, shutting down Patriotnode pinging service! " + vin.ToString();
        status = ACTIVE_PATRIOTNODE_NOT_CAPABLE;
        notCapableReason = errorMessage;
        return false;
    }
}

bool CActivePatriotnode::CreateBroadcast(std::string strService, std::string strKeyPatriotnode, std::string strTxHash, std::string strOutputIndex, std::string& errorMessage, CPatriotnodeBroadcast &mnb, bool fOffline)
{
    CTxIn vin;
    CPubKey pubKeyCollateralAddress;
    CKey keyCollateralAddress;
    CPubKey pubKeyPatriotnode;
    CKey keyPatriotnode;

    //need correct blocks to send ping
    if (!fOffline && !patriotnodeSync.IsBlockchainSynced()) {
        errorMessage = "Sync in progress. Must wait until sync is complete to start Patriotnode";
        LogPrintf("CActivePatriotnode::CreateBroadcast() - %s\n", errorMessage);
        return false;
    }

    if (!obfuScationSigner.SetKey(strKeyPatriotnode, errorMessage, keyPatriotnode, pubKeyPatriotnode)) {
        errorMessage = strprintf("Can't find keys for patriotnode %s - %s", strService, errorMessage);
        LogPrintf("CActivePatriotnode::CreateBroadcast() - %s\n", errorMessage);
        return false;
    }

    if (!GetPatriotNodeVin(vin, pubKeyCollateralAddress, keyCollateralAddress, strTxHash, strOutputIndex)) {
        errorMessage = strprintf("Could not allocate vin %s:%s for patriotnode %s", strTxHash, strOutputIndex, strService);
        LogPrintf("CActivePatriotnode::CreateBroadcast() - %s\n", errorMessage);
        return false;
    }

    CService service = CService(strService);

    // The service needs the correct default port to work properly
    if(!CPatriotnodeBroadcast::CheckDefaultPort(strService, errorMessage, "CActivePatriotnode::CreateBroadcast()"))
        return false;

    addrman.Add(CAddress(service), CNetAddr("127.0.0.1"), 2 * 60 * 60);

    return CreateBroadcast(vin, CService(strService), keyCollateralAddress, pubKeyCollateralAddress, keyPatriotnode, pubKeyPatriotnode, errorMessage, mnb);
}

bool CActivePatriotnode::CreateBroadcast(CTxIn vin, CService service, CKey keyCollateralAddress, CPubKey pubKeyCollateralAddress, CKey keyPatriotnode, CPubKey pubKeyPatriotnode, std::string& errorMessage, CPatriotnodeBroadcast &mnb)
{
	// wait for reindex and/or import to finish
	if (fImporting || fReindex) return false;

    CPatriotnodePing mnp(vin);
    if (!mnp.Sign(keyPatriotnode, pubKeyPatriotnode)) {
        errorMessage = strprintf("Failed to sign ping, vin: %s", vin.ToString());
        LogPrintf("CActivePatriotnode::CreateBroadcast() -  %s\n", errorMessage);
        mnb = CPatriotnodeBroadcast();
        return false;
    }

    mnb = CPatriotnodeBroadcast(service, vin, pubKeyCollateralAddress, pubKeyPatriotnode, PROTOCOL_VERSION);
    mnb.lastPing = mnp;
    if (!mnb.Sign(keyCollateralAddress)) {
        errorMessage = strprintf("Failed to sign broadcast, vin: %s", vin.ToString());
        LogPrintf("CActivePatriotnode::CreateBroadcast() - %s\n", errorMessage);
        mnb = CPatriotnodeBroadcast();
        return false;
    }

    /*
     * IT'S SAFE TO REMOVE THIS IN FURTHER VERSIONS
     * AFTER MIGRATION TO V12 IS DONE
     */

    if (IsSporkActive(SPORK_10_PATRIOTNODE_PAY_UPDATED_NODES)) return true;
    // for migration purposes inject our node in old patriotnodes' list too
    std::string retErrorMessage;
    std::vector<unsigned char> vchPatriotNodeSignature;
    int64_t patriotNodeSignatureTime = GetAdjustedTime();
    std::string donationAddress = "";
    int donationPercantage = 0;

    std::string vchPubKey(pubKeyCollateralAddress.begin(), pubKeyCollateralAddress.end());
    std::string vchPubKey2(pubKeyPatriotnode.begin(), pubKeyPatriotnode.end());

    std::string strMessage = service.ToString() + boost::lexical_cast<std::string>(patriotNodeSignatureTime) + vchPubKey + vchPubKey2 + boost::lexical_cast<std::string>(PROTOCOL_VERSION) + donationAddress + boost::lexical_cast<std::string>(donationPercantage);

    if (!obfuScationSigner.SignMessage(strMessage, retErrorMessage, vchPatriotNodeSignature, keyCollateralAddress)) {
        errorMessage = "dsee sign message failed: " + retErrorMessage;
        LogPrintf("CActivePatriotnode::Register() - Error: %s\n", errorMessage.c_str());
        return false;
    }

    if (!obfuScationSigner.VerifyMessage(pubKeyCollateralAddress, vchPatriotNodeSignature, strMessage, retErrorMessage)) {
        errorMessage = "dsee verify message failed: " + retErrorMessage;
        LogPrintf("CActivePatriotnode::Register() - Error: %s\n", errorMessage.c_str());
        return false;
    }

    LOCK(cs_vNodes);
    BOOST_FOREACH (CNode* pnode, vNodes)
        pnode->PushMessage("dsee", vin, service, vchPatriotNodeSignature, patriotNodeSignatureTime, pubKeyCollateralAddress, pubKeyPatriotnode, -1, -1, patriotNodeSignatureTime, PROTOCOL_VERSION, donationAddress, donationPercantage);

    /*
     * END OF "REMOVE"
     */

    return true;
}

bool CActivePatriotnode::GetPatriotNodeVin(CTxIn& vin, CPubKey& pubkey, CKey& secretKey)
{
    return GetPatriotNodeVin(vin, pubkey, secretKey, "", "");
}

bool CActivePatriotnode::GetPatriotNodeVin(CTxIn& vin, CPubKey& pubkey, CKey& secretKey, std::string strTxHash, std::string strOutputIndex)
{
	// wait for reindex and/or import to finish
	if (fImporting || fReindex) return false;

    // Find possible candidates
    TRY_LOCK(pwalletMain->cs_wallet, fWallet);
    if (!fWallet) return false;

    vector<COutput> possibleCoins = SelectCoinsPatriotnode();
    COutput* selectedOutput;

    // Find the vin
    if (!strTxHash.empty()) {
        // Let's find it
        uint256 txHash(strTxHash);
        int outputIndex;
        try {
            outputIndex = std::stoi(strOutputIndex.c_str());
        } catch (const std::exception& e) {
            LogPrintf("%s: %s on strOutputIndex\n", __func__, e.what());
            return false;
        }

        bool found = false;
        BOOST_FOREACH (COutput& out, possibleCoins) {
            if (out.tx->GetHash() == txHash && out.i == outputIndex) {
                selectedOutput = &out;
                found = true;
                break;
            }
        }
        if (!found) {
            LogPrintf("CActivePatriotnode::GetPatriotNodeVin - Could not locate valid vin\n");
            return false;
        }
    } else {
        // No output specified,  Select the first one
        if (possibleCoins.size() > 0) {
            selectedOutput = &possibleCoins[0];
        } else {
            LogPrintf("CActivePatriotnode::GetPatriotNodeVin - Could not locate specified vin from possible list\n");
            return false;
        }
    }

    // At this point we have a selected output, retrieve the associated info
    return GetVinFromOutput(*selectedOutput, vin, pubkey, secretKey);
}


// Extract Patriotnode vin information from output
bool CActivePatriotnode::GetVinFromOutput(COutput out, CTxIn& vin, CPubKey& pubkey, CKey& secretKey)
{
	// wait for reindex and/or import to finish
	if (fImporting || fReindex) return false;

    CScript pubScript;

    vin = CTxIn(out.tx->GetHash(), out.i);
    pubScript = out.tx->vout[out.i].scriptPubKey; // the inputs PubKey

    CTxDestination address1;
    ExtractDestination(pubScript, address1);
    CBitcoinAddress address2(address1);

    CKeyID keyID;
    if (!address2.GetKeyID(keyID)) {
        LogPrintf("CActivePatriotnode::GetPatriotNodeVin - Address does not refer to a key\n");
        return false;
    }

    if (!pwalletMain->GetKey(keyID, secretKey)) {
        LogPrintf("CActivePatriotnode::GetPatriotNodeVin - Private key for address is not known\n");
        return false;
    }

    pubkey = secretKey.GetPubKey();
    return true;
}

// get all possible outputs for running Patriotnode
vector<COutput> CActivePatriotnode::SelectCoinsPatriotnode()
{
    vector<COutput> vCoins;
    vector<COutput> filteredCoins;
    vector<COutPoint> confLockedCoins;

    // Temporary unlock MN coins from patriotnode.conf
    if (GetBoolArg("-mnconflock", true)) {
        uint256 mnTxHash;
        BOOST_FOREACH (CPatriotnodeConfig::CPatriotnodeEntry mne, patriotnodeConfig.getEntries()) {
            mnTxHash.SetHex(mne.getTxHash());

            int nIndex;
            if(!mne.castOutputIndex(nIndex))
                continue;

            COutPoint outpoint = COutPoint(mnTxHash, nIndex);
            confLockedCoins.push_back(outpoint);
            pwalletMain->UnlockCoin(outpoint);
        }
    }

    // Retrieve all possible outputs
    pwalletMain->AvailableCoins(vCoins);

    // Lock MN coins from patriotnode.conf back if they where temporary unlocked
    if (!confLockedCoins.empty()) {
        BOOST_FOREACH (COutPoint outpoint, confLockedCoins)
            pwalletMain->LockCoin(outpoint);
    }

    // Filter
    BOOST_FOREACH (const COutput& out, vCoins) {
        if (out.tx->vout[out.i].nValue == 5000 * COIN) { //exactly
            filteredCoins.push_back(out);
        }
    }
    return filteredCoins;
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
