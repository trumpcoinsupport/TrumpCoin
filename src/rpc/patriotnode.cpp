// Copyright (c) 2009-2012 The Bitcoin developers
// Copyright (c) 2015-2020 The TrumpCoin developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "activepatriotnode.h"
#include "db.h"
#include "evo/deterministicmns.h"
#include "init.h"
#include "key_io.h"
#include "patriotnode-payments.h"
#include "patriotnode-sync.h"
#include "patriotnodeconfig.h"
#include "patriotnodeman.h"
#include "netbase.h"
#include "rpc/server.h"
#ifdef ENABLE_WALLET
#include "wallet/rpcwallet.h"
#endif

#include <univalue.h>

#include <boost/tokenizer.hpp>

UniValue mnping(const JSONRPCRequest& request)
{
    if (request.fHelp || !request.params.empty()) {
        throw std::runtime_error(
            "mnping \n"
            "\nSend patriotnode ping. Only for remote patriotnodes on Regtest\n"

            "\nResult:\n"
            "{\n"
            "  \"sent\":           (string YES|NO) Whether the ping was sent and, if not, the error.\n"
            "}\n"

            "\nExamples:\n" +
            HelpExampleCli("mnping", "") + HelpExampleRpc("mnping", ""));
    }

    if (!Params().IsRegTestNet()) {
        throw JSONRPCError(RPC_MISC_ERROR, "command available only for RegTest network");
    }

    if (!fPatriotNode) {
        throw JSONRPCError(RPC_MISC_ERROR, "this is not a patriotnode");
    }

    UniValue ret(UniValue::VOBJ);
    std::string strError;
    ret.pushKV("sent", activePatriotnode.SendPatriotnodePing(strError) ?
                       "YES" : strprintf("NO (%s)", strError));
    return ret;
}

UniValue initpatriotnode(const JSONRPCRequest& request)
{
    if (request.fHelp || (request.params.size() < 2|| request.params.size() > 3)) {
        throw std::runtime_error(
                "initpatriotnode ( \"privkey\" \"address\" deterministic )\n"
                "\nInitialize patriotnode on demand if it's not already initialized.\n"
                "\nArguments:\n"
                "1. privkey          (string, required) The patriotnode private key.\n"
                "2. address          (string, required) The IP:Port of this patriotnode.\n"
                "3. deterministic    (boolean, optional, default=false) Init as DPN.\n"

                "\nResult:\n"
                " success                      (string) if the patriotnode initialization succeeded.\n"

                "\nExamples:\n" +
                HelpExampleCli("initpatriotnode", "\"9247iC59poZmqBYt9iDh9wDam6v9S1rW5XekjLGyPnDhrDkP4AK\" \"187.24.32.124:15110\"") +
                HelpExampleRpc("initpatriotnode", "\"9247iC59poZmqBYt9iDh9wDam6v9S1rW5XekjLGyPnDhrDkP4AK\" \"187.24.32.124:15110\""));
    }

    std::string _strPatriotNodePrivKey = request.params[0].get_str();
    std::string _strPatriotNodeAddr = request.params[1].get_str();
    bool fDeterministic = request.params.size() > 2 && request.params[2].get_bool();
    if (fDeterministic) {
        if (!activePatriotnodeManager) {
            activePatriotnodeManager = new CActiveDeterministicPatriotnodeManager();
            RegisterValidationInterface(activePatriotnodeManager);
        }
        auto res = activePatriotnodeManager->SetOperatorKey(_strPatriotNodePrivKey);
        if (!res) throw std::runtime_error(res.getError());
        activePatriotnodeManager->Init();
        if (activePatriotnodeManager->GetState() == CActiveDeterministicPatriotnodeManager::PATRIOTNODE_ERROR) {
            throw std::runtime_error(activePatriotnodeManager->GetStatus());
        }
        return "success";
    }
    // legacy
    auto res = initPatriotnode(_strPatriotNodePrivKey, _strPatriotNodeAddr, false);
    if (!res) throw std::runtime_error(res.getError());
    return "success";
}

UniValue getcachedblockhashes(const JSONRPCRequest& request)
{
    if (request.fHelp || request.params.size() > 0)
        throw std::runtime_error(
            "getcachedblockhashes \n"
            "\nReturn the block hashes cached in the patriotnode manager\n"

            "\nResult:\n"
            "[\n"
            "  ...\n"
            "  \"xxxx\",   (string) hash at Index d (height modulo max cache size)\n"
            "  ...\n"
            "]\n"

            "\nExamples:\n" +
            HelpExampleCli("getcachedblockhashes", "") + HelpExampleRpc("getcachedblockhashes", ""));

    std::vector<uint256> vCacheCopy = mnodeman.GetCachedBlocks();
    UniValue ret(UniValue::VARR);
    for (int i = 0; (unsigned) i < vCacheCopy.size(); i++) {
        ret.push_back(vCacheCopy[i].ToString());
    }
    return ret;
}

static inline bool filter(const std::string& str, const std::string& strFilter)
{
    return str.find(strFilter) != std::string::npos;
}

static inline bool filterPatriotnode(const UniValue& dmno, const std::string& strFilter, bool fEnabled)
{
    return strFilter.empty() || (filter("ENABLED", strFilter) && fEnabled)
                             || (filter("POSE_BANNED", strFilter) && !fEnabled)
                             || (filter(dmno["proTxHash"].get_str(), strFilter))
                             || (filter(dmno["collateralHash"].get_str(), strFilter))
                             || (filter(dmno["collateralAddress"].get_str(), strFilter))
                             || (filter(dmno["dmnstate"]["ownerAddress"].get_str(), strFilter))
                             || (filter(dmno["dmnstate"]["operatorAddress"].get_str(), strFilter))
                             || (filter(dmno["dmnstate"]["votingAddress"].get_str(), strFilter));
}

UniValue listpatriotnodes(const JSONRPCRequest& request)
{
    if (request.fHelp || (request.params.size() > 1))
        throw std::runtime_error(
            "listpatriotnodes ( \"filter\" )\n"
            "\nGet a ranked list of patriotnodes\n"

            "\nArguments:\n"
            "1. \"filter\"    (string, optional) Filter search text. Partial match by txhash, status, or addr.\n"

            // !TODO: update for DPNs
            "\nResult:\n"
            "[\n"
            "  {\n"
            "    \"rank\": n,                             (numeric) Patriotnode Rank (or 0 if not enabled)\n"
            "    \"type\": \"legacy\"|\"deterministic\",  (string) type of patriotnode\n"
            "    \"txhash\": \"hash\",                    (string) Collateral transaction hash\n"
            "    \"outidx\": n,                           (numeric) Collateral transaction output index\n"
            "    \"pubkey\": \"key\",                     (string) Patriotnode public key used for message broadcasting\n"
            "    \"status\": s,                           (string) Status (ENABLED/EXPIRED/REMOVE/etc)\n"
            "    \"addr\": \"addr\",                      (string) Patriotnode TrumpCoin address\n"
            "    \"version\": v,                          (numeric) Patriotnode protocol version\n"
            "    \"lastseen\": ttt,     (numeric) The time in seconds since epoch (Jan 1 1970 GMT) of the last seen\n"
            "    \"activetime\": ttt,   (numeric) The time in seconds since epoch (Jan 1 1970 GMT) patriotnode has been active\n"
            "    \"lastpaid\": ttt,     (numeric) The time in seconds since epoch (Jan 1 1970 GMT) patriotnode was last paid\n"
            "  }\n"
            "  ,...\n"
            "]\n"

            "\nExamples:\n" +
            HelpExampleCli("listpatriotnodes", "") + HelpExampleRpc("listpatriotnodes", ""));


    const std::string& strFilter = request.params.size() > 0 ? request.params[0].get_str() : "";
    UniValue ret(UniValue::VARR);

    if (deterministicPNManager->LegacyPNObsolete()) {
        auto mnList = deterministicPNManager->GetListAtChainTip();
        mnList.ForEachPN(false, [&](const CDeterministicPNCPtr& dmn) {
            UniValue obj(UniValue::VOBJ);
            dmn->ToJson(obj);
            bool fEnabled = dmn->pdmnState->nPoSeBanHeight == -1;
            if (filterPatriotnode(obj, strFilter, fEnabled)) {
                ret.push_back(obj);
            }
        });
        return ret;
    }

    // Legacy patriotnodes (!TODO: remove when transition to dmn is complete)
    const CBlockIndex* chainTip = GetChainTip();
    if (!chainTip) return "[]";
    int nHeight = chainTip->nHeight;
    auto mnList = deterministicPNManager->GetListAtChainTip();

    std::vector<std::pair<int64_t, PatriotnodeRef>> vPatriotnodeRanks = mnodeman.GetPatriotnodeRanks(nHeight);
    for (int pos=0; pos < (int) vPatriotnodeRanks.size(); pos++) {
        const auto& s = vPatriotnodeRanks[pos];
        UniValue obj(UniValue::VOBJ);
        const CPatriotnode& mn = *(s.second);

        if (!mn.mnPayeeScript.empty()) {
            // Deterministic patriotnode
            auto dmn = mnList.GetPNByCollateral(mn.vin.prevout);
            if (dmn) {
                UniValue obj(UniValue::VOBJ);
                dmn->ToJson(obj);
                bool fEnabled = dmn->pdmnState->nPoSeBanHeight == -1;
                if (filterPatriotnode(obj, strFilter, fEnabled)) {
                    // Added for backward compatibility with legacy patriotnodes
                    obj.pushKV("type", "deterministic");
                    obj.pushKV("txhash", obj["proTxHash"].get_str());
                    obj.pushKV("addr", obj["dmnstate"]["payoutAddress"].get_str());
                    obj.pushKV("status", fEnabled ? "ENABLED" : "POSE_BANNED");
                    obj.pushKV("rank", fEnabled ? pos : 0);
                    ret.push_back(obj);
                }
            }
            continue;
        }

        std::string strVin = mn.vin.prevout.ToStringShort();
        std::string strTxHash = mn.vin.prevout.hash.ToString();
        uint32_t oIdx = mn.vin.prevout.n;

        if (strFilter != "" && strTxHash.find(strFilter) == std::string::npos &&
            mn.Status().find(strFilter) == std::string::npos &&
            EncodeDestination(mn.pubKeyCollateralAddress.GetID()).find(strFilter) == std::string::npos) continue;

        std::string strStatus = mn.Status();
        std::string strHost;
        int port;
        SplitHostPort(mn.addr.ToString(), port, strHost);
        CNetAddr node;
        LookupHost(strHost.c_str(), node, false);
        std::string strNetwork = GetNetworkName(node.GetNetwork());

        obj.pushKV("rank", (strStatus == "ENABLED" ? pos : -1));
        obj.pushKV("type", "legacy");
        obj.pushKV("network", strNetwork);
        obj.pushKV("txhash", strTxHash);
        obj.pushKV("outidx", (uint64_t)oIdx);
        obj.pushKV("pubkey", EncodeDestination(mn.pubKeyPatriotnode.GetID()));
        obj.pushKV("status", strStatus);
        obj.pushKV("addr", EncodeDestination(mn.pubKeyCollateralAddress.GetID()));
        obj.pushKV("version", mn.protocolVersion);
        obj.pushKV("lastseen", (int64_t)mn.lastPing.sigTime);
        obj.pushKV("activetime", (int64_t)(mn.lastPing.sigTime - mn.sigTime));
        obj.pushKV("lastpaid", (int64_t)mnodeman.GetLastPaid(s.second, chainTip));

        ret.push_back(obj);
    }

    return ret;
}

UniValue getpatriotnodecount (const JSONRPCRequest& request)
{
    if (request.fHelp || (request.params.size() > 0))
        throw std::runtime_error(
            "getpatriotnodecount\n"
            "\nGet patriotnode count values\n"

            "\nResult:\n"
            "{\n"
            "  \"total\": n,        (numeric) Total patriotnodes\n"
            "  \"stable\": n,       (numeric) Stable count\n"
            "  \"enabled\": n,      (numeric) Enabled patriotnodes\n"
            "  \"inqueue\": n,      (numeric) Patriotnodes in queue\n"
            "  \"ipv4\": n,         (numeric) Number of IPv4 patriotnodes\n"
            "  \"ipv6\": n,         (numeric) Number of IPv6 patriotnodes\n"
            "  \"onion\": n         (numeric) Number of Tor patriotnodes\n"
            "}\n"

            "\nExamples:\n" +
            HelpExampleCli("getpatriotnodecount", "") + HelpExampleRpc("getpatriotnodecount", ""));

    UniValue obj(UniValue::VOBJ);
    int nCount = 0;
    int ipv4 = 0, ipv6 = 0, onion = 0;

    const CBlockIndex* pChainTip = GetChainTip();
    if (!pChainTip) return "unknown";

    mnodeman.GetNextPatriotnodeInQueueForPayment(pChainTip->nHeight, true, nCount, pChainTip);
    int total = mnodeman.CountNetworks(ipv4, ipv6, onion);

    obj.pushKV("total", total);
    obj.pushKV("stable", mnodeman.stable_size());
    obj.pushKV("enabled", mnodeman.CountEnabled());
    obj.pushKV("inqueue", nCount);
    obj.pushKV("ipv4", ipv4);
    obj.pushKV("ipv6", ipv6);
    obj.pushKV("onion", onion);

    return obj;
}

UniValue patriotnodecurrent(const JSONRPCRequest& request)
{
    if (request.fHelp || (request.params.size() != 0))
        throw std::runtime_error(
            "patriotnodecurrent\n"
            "\nGet current patriotnode winner (scheduled to be paid next).\n"

            "\nResult:\n"
            "{\n"
            "  \"protocol\": xxxx,        (numeric) Protocol version\n"
            "  \"txhash\": \"xxxx\",      (string) Collateral transaction hash\n"
            "  \"pubkey\": \"xxxx\",      (string) PN Public key\n"
            "  \"lastseen\": xxx,         (numeric) Time since epoch of last seen\n"
            "  \"activeseconds\": xxx,    (numeric) Seconds PN has been active\n"
            "}\n"

            "\nExamples:\n" +
            HelpExampleCli("patriotnodecurrent", "") + HelpExampleRpc("patriotnodecurrent", ""));

    const CBlockIndex* pChainTip = GetChainTip();
    if (!pChainTip) return "unknown";

    int nCount = 0;
    PatriotnodeRef winner = mnodeman.GetNextPatriotnodeInQueueForPayment(pChainTip->nHeight + 1, true, nCount, pChainTip);
    if (winner) {
        UniValue obj(UniValue::VOBJ);
        obj.pushKV("protocol", (int64_t)winner->protocolVersion);
        obj.pushKV("txhash", winner->vin.prevout.hash.ToString());
        obj.pushKV("pubkey", EncodeDestination(winner->pubKeyCollateralAddress.GetID()));
        obj.pushKV("lastseen", winner->lastPing.IsNull() ? winner->sigTime : (int64_t)winner->lastPing.sigTime);
        obj.pushKV("activeseconds", winner->lastPing.IsNull() ? 0 : (int64_t)(winner->lastPing.sigTime - winner->sigTime));
        return obj;
    }

    throw std::runtime_error("unknown");
}

bool StartPatriotnodeEntry(UniValue& statusObjRet, CPatriotnodeBroadcast& mnbRet, bool& fSuccessRet, const CPatriotnodeConfig::CPatriotnodeEntry& mne, std::string& errorMessage, std::string strCommand = "")
{
    int nIndex;
    if(!mne.castOutputIndex(nIndex)) {
        return false;
    }

    CTxIn vin = CTxIn(uint256S(mne.getTxHash()), uint32_t(nIndex));
    CPatriotnode* pmn = mnodeman.Find(vin.prevout);
    if (pmn != NULL) {
        if (strCommand == "missing") return false;
        if (strCommand == "disabled" && pmn->IsEnabled()) return false;
    }

    fSuccessRet = CPatriotnodeBroadcast::Create(mne.getIp(), mne.getPrivKey(), mne.getTxHash(), mne.getOutputIndex(), errorMessage, mnbRet, false, mnodeman.GetBestHeight());

    statusObjRet.pushKV("alias", mne.getAlias());
    statusObjRet.pushKV("result", fSuccessRet ? "success" : "failed");
    statusObjRet.pushKV("error", fSuccessRet ? "" : errorMessage);

    return true;
}

void RelayPNB(CPatriotnodeBroadcast& mnb, const bool fSuccess, int& successful, int& failed)
{
    if (fSuccess) {
        successful++;
        mnodeman.UpdatePatriotnodeList(mnb);
        mnb.Relay();
    } else {
        failed++;
    }
}

void RelayPNB(CPatriotnodeBroadcast& mnb, const bool fSucces)
{
    int successful = 0, failed = 0;
    return RelayPNB(mnb, fSucces, successful, failed);
}

void SerializePNB(UniValue& statusObjRet, const CPatriotnodeBroadcast& mnb, const bool fSuccess, int& successful, int& failed)
{
    if(fSuccess) {
        successful++;
        CDataStream ssMnb(SER_NETWORK, PROTOCOL_VERSION);
        ssMnb << mnb;
        statusObjRet.pushKV("hex", HexStr(ssMnb));
    } else {
        failed++;
    }
}

void SerializePNB(UniValue& statusObjRet, const CPatriotnodeBroadcast& mnb, const bool fSuccess)
{
    int successful = 0, failed = 0;
    return SerializePNB(statusObjRet, mnb, fSuccess, successful, failed);
}

UniValue startpatriotnode(const JSONRPCRequest& request)
{
    // Skip after legacy obsolete. !TODO: remove when transition to DPN is complete
    if (deterministicPNManager->LegacyPNObsolete()) {
        throw JSONRPCError(RPC_MISC_ERROR, "startpatriotnode is not supported when deterministic patriotnode list is active (DIP3)");
    }

    CWallet * const pwallet = GetWalletForJSONRPCRequest(request);

    if (!EnsureWalletIsAvailable(pwallet, request.fHelp))
        return NullUniValue;

    std::string strCommand;
    if (request.params.size() >= 1) {
        strCommand = request.params[0].get_str();

        // Backwards compatibility with legacy 'patriotnode' super-command forwarder
        if (strCommand == "start") strCommand = "local";
        if (strCommand == "start-alias") strCommand = "alias";
        if (strCommand == "start-all") strCommand = "all";
        if (strCommand == "start-many") strCommand = "many";
        if (strCommand == "start-missing") strCommand = "missing";
        if (strCommand == "start-disabled") strCommand = "disabled";
    }

    if (request.fHelp || request.params.size() < 2 || request.params.size() > 4 ||
        (request.params.size() == 2 && (strCommand != "local" && strCommand != "all" && strCommand != "many" && strCommand != "missing" && strCommand != "disabled")) ||
        ( (request.params.size() == 3 || request.params.size() == 4) && strCommand != "alias"))
        throw std::runtime_error(
            "startpatriotnode \"local|all|many|missing|disabled|alias\" lockwallet ( \"alias\" reload_conf )\n"
            "\nAttempts to start one or more patriotnode(s)\n"

            "\nArguments:\n"
            "1. set         (string, required) Specify which set of patriotnode(s) to start.\n"
            "2. lockwallet  (boolean, required) Lock wallet after completion.\n"
            "3. alias       (string) Patriotnode alias. Required if using 'alias' as the set.\n"
            "4. reload_conf (boolean) if true and \"alias\" was selected, reload the patriotnodes.conf data from disk"

            "\nResult: (for 'local' set):\n"
            "\"status\"     (string) Patriotnode status message\n"

            "\nResult: (for other sets):\n"
            "{\n"
            "  \"overall\": \"xxxx\",     (string) Overall status message\n"
            "  \"detail\": [\n"
            "    {\n"
            "      \"node\": \"xxxx\",    (string) Node name or alias\n"
            "      \"result\": \"xxxx\",  (string) 'success' or 'failed'\n"
            "      \"error\": \"xxxx\"    (string) Error message, if failed\n"
            "    }\n"
            "    ,...\n"
            "  ]\n"
            "}\n"

            "\nExamples:\n" +
            HelpExampleCli("startpatriotnode", "\"alias\" \"0\" \"my_mn\"") + HelpExampleRpc("startpatriotnode", "\"alias\" \"0\" \"my_mn\""));

    bool fLock = (request.params[1].get_str() == "true" ? true : false);

    EnsureWalletIsUnlocked(pwallet);

    if (strCommand == "local") {
        if (!fPatriotNode) throw std::runtime_error("you must set patriotnode=1 in the configuration\n");

        if (activePatriotnode.GetStatus() != ACTIVE_PATRIOTNODE_STARTED) {
            activePatriotnode.ResetStatus();
            if (fLock)
                pwallet->Lock();
        }

        return activePatriotnode.GetStatusMessage();
    }

    if (strCommand == "all" || strCommand == "many" || strCommand == "missing" || strCommand == "disabled") {
        if ((strCommand == "missing" || strCommand == "disabled") &&
            (patriotnodeSync.RequestedPatriotnodeAssets <= PATRIOTNODE_SYNC_LIST ||
                patriotnodeSync.RequestedPatriotnodeAssets == PATRIOTNODE_SYNC_FAILED)) {
            throw std::runtime_error("You can't use this command until patriotnode list is synced\n");
        }

        int successful = 0;
        int failed = 0;

        UniValue resultsObj(UniValue::VARR);

        for (CPatriotnodeConfig::CPatriotnodeEntry mne : patriotnodeConfig.getEntries()) {
            UniValue statusObj(UniValue::VOBJ);
            CPatriotnodeBroadcast mnb;
            std::string errorMessage;
            bool fSuccess = false;
            if (!StartPatriotnodeEntry(statusObj, mnb, fSuccess, mne, errorMessage, strCommand))
                continue;
            resultsObj.push_back(statusObj);
            RelayPNB(mnb, fSuccess, successful, failed);
        }
        if (fLock)
            pwallet->Lock();

        UniValue returnObj(UniValue::VOBJ);
        returnObj.pushKV("overall", strprintf("Successfully started %d patriotnodes, failed to start %d, total %d", successful, failed, successful + failed));
        returnObj.pushKV("detail", resultsObj);

        return returnObj;
    }

    if (strCommand == "alias") {
        std::string alias = request.params[2].get_str();

        // Check reload param
        if(request.params[3].getBool()) {
            patriotnodeConfig.clear();
            std::string error;
            if (!patriotnodeConfig.read(error)) {
                throw std::runtime_error("Error reloading patriotnode.conf, " + error);
            }
        }

        bool found = false;

        UniValue resultsObj(UniValue::VARR);
        UniValue statusObj(UniValue::VOBJ);

        for (CPatriotnodeConfig::CPatriotnodeEntry mne : patriotnodeConfig.getEntries()) {
            if (mne.getAlias() == alias) {
                CPatriotnodeBroadcast mnb;
                found = true;
                std::string errorMessage;
                bool fSuccess = false;
                if (!StartPatriotnodeEntry(statusObj, mnb, fSuccess, mne, errorMessage, strCommand))
                        continue;
                RelayPNB(mnb, fSuccess);
                break;
            }
        }

        if (fLock)
            pwallet->Lock();

        if(!found) {
            statusObj.pushKV("alias", alias);
            statusObj.pushKV("result", "failed");
            statusObj.pushKV("error", "Could not find alias in config. Verify with listpatriotnodeconf.");
        }

        return statusObj;
    }
    return NullUniValue;
}

UniValue createpatriotnodekey(const JSONRPCRequest& request)
{
    if (request.fHelp || (request.params.size() != 0))
        throw std::runtime_error(
            "createpatriotnodekey\n"
            "\nCreate a new patriotnode private key\n"

            "\nResult:\n"
            "\"key\"    (string) Patriotnode private key\n"

            "\nExamples:\n" +
            HelpExampleCli("createpatriotnodekey", "") + HelpExampleRpc("createpatriotnodekey", ""));

    CKey secret;
    secret.MakeNewKey(false);

    return KeyIO::EncodeSecret(secret);
}

UniValue getpatriotnodeoutputs(const JSONRPCRequest& request)
{
    CWallet * const pwallet = GetWalletForJSONRPCRequest(request);

    if (!EnsureWalletIsAvailable(pwallet, request.fHelp))
        return NullUniValue;

    if (request.fHelp || (request.params.size() != 0))
        throw std::runtime_error(
            "getpatriotnodeoutputs\n"
            "\nPrint all patriotnode transaction outputs\n"

            "\nResult:\n"
            "[\n"
            "  {\n"
            "    \"txhash\": \"xxxx\",    (string) output transaction hash\n"
            "    \"outputidx\": n       (numeric) output index number\n"
            "  }\n"
            "  ,...\n"
            "]\n"

            "\nExamples:\n" +
            HelpExampleCli("getpatriotnodeoutputs", "") + HelpExampleRpc("getpatriotnodeoutputs", ""));

    // Find possible candidates
    CWallet::AvailableCoinsFilter coinsFilter;
    coinsFilter.fIncludeDelegated = false;
    coinsFilter.nMaxOutValue = Params().GetConsensus().nPNCollateralAmt;
    coinsFilter.nMinOutValue = coinsFilter.nMaxOutValue;
    coinsFilter.fIncludeLocked = true;
    std::vector<COutput> possibleCoins;
    pwallet->AvailableCoins(&possibleCoins, nullptr, coinsFilter);

    UniValue ret(UniValue::VARR);
    for (COutput& out : possibleCoins) {
        UniValue obj(UniValue::VOBJ);
        obj.pushKV("txhash", out.tx->GetHash().ToString());
        obj.pushKV("outputidx", out.i);
        ret.push_back(obj);
    }

    return ret;
}

UniValue listpatriotnodeconf(const JSONRPCRequest& request)
{
    std::string strFilter = "";

    if (request.params.size() == 1) strFilter = request.params[0].get_str();

    if (request.fHelp || (request.params.size() > 1))
        throw std::runtime_error(
            "listpatriotnodeconf ( \"filter\" )\n"
            "\nPrint patriotnode.conf in JSON format\n"

            "\nArguments:\n"
            "1. \"filter\"    (string, optional) Filter search text. Partial match on alias, address, txHash, or status.\n"

            "\nResult:\n"
            "[\n"
            "  {\n"
            "    \"alias\": \"xxxx\",        (string) patriotnode alias\n"
            "    \"address\": \"xxxx\",      (string) patriotnode IP address\n"
            "    \"privateKey\": \"xxxx\",   (string) patriotnode private key\n"
            "    \"txHash\": \"xxxx\",       (string) transaction hash\n"
            "    \"outputIndex\": n,       (numeric) transaction output index\n"
            "    \"status\": \"xxxx\"        (string) patriotnode status\n"
            "  }\n"
            "  ,...\n"
            "]\n"

            "\nExamples:\n" +
            HelpExampleCli("listpatriotnodeconf", "") + HelpExampleRpc("listpatriotnodeconf", ""));

    std::vector<CPatriotnodeConfig::CPatriotnodeEntry> mnEntries;
    mnEntries = patriotnodeConfig.getEntries();

    UniValue ret(UniValue::VARR);

    for (CPatriotnodeConfig::CPatriotnodeEntry mne : patriotnodeConfig.getEntries()) {
        int nIndex;
        if(!mne.castOutputIndex(nIndex))
            continue;
        CTxIn vin = CTxIn(uint256S(mne.getTxHash()), uint32_t(nIndex));
        CPatriotnode* pmn = mnodeman.Find(vin.prevout);

        std::string strStatus = pmn ? pmn->Status() : "MISSING";

        if (strFilter != "" && mne.getAlias().find(strFilter) == std::string::npos &&
            mne.getIp().find(strFilter) == std::string::npos &&
            mne.getTxHash().find(strFilter) == std::string::npos &&
            strStatus.find(strFilter) == std::string::npos) continue;

        UniValue mnObj(UniValue::VOBJ);
        mnObj.pushKV("alias", mne.getAlias());
        mnObj.pushKV("address", mne.getIp());
        mnObj.pushKV("privateKey", mne.getPrivKey());
        mnObj.pushKV("txHash", mne.getTxHash());
        mnObj.pushKV("outputIndex", mne.getOutputIndex());
        mnObj.pushKV("status", strStatus);
        ret.push_back(mnObj);
    }

    return ret;
}

UniValue getpatriotnodestatus(const JSONRPCRequest& request)
{
    if (request.fHelp || (request.params.size() != 0))
        throw std::runtime_error(
            "getpatriotnodestatus\n"
            "\nPrint patriotnode status\n"

            "\nResult (if legacy patriotnode):\n"
            "{\n"
            "  \"txhash\": \"xxxx\",      (string) Collateral transaction hash\n"
            "  \"outputidx\": n,          (numeric) Collateral transaction output index number\n"
            "  \"netaddr\": \"xxxx\",     (string) Patriotnode network address\n"
            "  \"addr\": \"xxxx\",        (string) TrumpCoin address for patriotnode payments\n"
            "  \"status\": \"xxxx\",      (string) Patriotnode status\n"
            "  \"message\": \"xxxx\"      (string) Patriotnode status message\n"
            "}\n"
            "\n"
            "\nResult (if deterministic patriotnode):\n"
            "{\n"
            "... !TODO ...\n"
            "}\n"

            "\nExamples:\n" +
            HelpExampleCli("getpatriotnodestatus", "") + HelpExampleRpc("getpatriotnodestatus", ""));

    if (!fPatriotNode)
        throw JSONRPCError(RPC_MISC_ERROR, _("This is not a patriotnode."));

    bool fLegacyPN = (activePatriotnode.vin != nullopt);
    bool fDeterministicPN = (activePatriotnodeManager != nullptr);

    if (!fLegacyPN && !fDeterministicPN) {
        throw JSONRPCError(RPC_MISC_ERROR, _("Active Patriotnode not initialized."));
    }

    if (fDeterministicPN) {
        if (!deterministicPNManager->IsDIP3Enforced()) {
            // this should never happen as ProTx transactions are not accepted yet
            throw JSONRPCError(RPC_MISC_ERROR, _("Deterministic patriotnodes are not enforced yet"));
        }
        const CActivePatriotnodeInfo* amninfo = activePatriotnodeManager->GetInfo();
        UniValue mnObj(UniValue::VOBJ);
        auto dmn = deterministicPNManager->GetListAtChainTip().GetPNByOperatorKey(amninfo->keyIDOperator);
        if (dmn) {
            dmn->ToJson(mnObj);
        }
        mnObj.pushKV("netaddr", amninfo->service.ToString());
        mnObj.pushKV("status", activePatriotnodeManager->GetStatus());
        return mnObj;
    }

    // Legacy code !TODO: remove when transition to DPN is complete
    if (deterministicPNManager->LegacyPNObsolete()) {
        throw JSONRPCError(RPC_MISC_ERROR, _("Legacy Patriotnode is obsolete."));
    }

    CPatriotnode* pmn = mnodeman.Find(activePatriotnode.vin->prevout);

    if (pmn) {
        UniValue mnObj(UniValue::VOBJ);
        mnObj.pushKV("txhash", activePatriotnode.vin->prevout.hash.ToString());
        mnObj.pushKV("outputidx", (uint64_t)activePatriotnode.vin->prevout.n);
        mnObj.pushKV("netaddr", activePatriotnode.service.ToString());
        mnObj.pushKV("addr", EncodeDestination(pmn->pubKeyCollateralAddress.GetID()));
        mnObj.pushKV("status", activePatriotnode.GetStatus());
        mnObj.pushKV("message", activePatriotnode.GetStatusMessage());
        return mnObj;
    }
    throw std::runtime_error("Patriotnode not found in the list of available patriotnodes. Current status: "
                        + activePatriotnode.GetStatusMessage());
}

UniValue getpatriotnodewinners(const JSONRPCRequest& request)
{
    if (request.fHelp || request.params.size() > 2)
        throw std::runtime_error(
            "getpatriotnodewinners ( blocks \"filter\" )\n"
            "\nPrint the patriotnode winners for the last n blocks\n"

            "\nArguments:\n"
            "1. blocks      (numeric, optional) Number of previous blocks to show (default: 10)\n"
            "2. filter      (string, optional) Search filter matching PN address\n"

            "\nResult (single winner):\n"
            "[\n"
            "  {\n"
            "    \"nHeight\": n,           (numeric) block height\n"
            "    \"winner\": {\n"
            "      \"address\": \"xxxx\",    (string) TrumpCoin PN Address\n"
            "      \"nVotes\": n,          (numeric) Number of votes for winner\n"
            "    }\n"
            "  }\n"
            "  ,...\n"
            "]\n"

            "\nResult (multiple winners):\n"
            "[\n"
            "  {\n"
            "    \"nHeight\": n,           (numeric) block height\n"
            "    \"winner\": [\n"
            "      {\n"
            "        \"address\": \"xxxx\",  (string) TrumpCoin PN Address\n"
            "        \"nVotes\": n,        (numeric) Number of votes for winner\n"
            "      }\n"
            "      ,...\n"
            "    ]\n"
            "  }\n"
            "  ,...\n"
            "]\n"

            "\nExamples:\n" +
            HelpExampleCli("getpatriotnodewinners", "") + HelpExampleRpc("getpatriotnodewinners", ""));

    int nHeight = WITH_LOCK(cs_main, return chainActive.Height());
    if (nHeight < 0) return "[]";

    int nLast = 10;
    std::string strFilter = "";

    if (request.params.size() >= 1)
        nLast = atoi(request.params[0].get_str());

    if (request.params.size() == 2)
        strFilter = request.params[1].get_str();

    UniValue ret(UniValue::VARR);

    for (int i = nHeight - nLast; i < nHeight + 20; i++) {
        UniValue obj(UniValue::VOBJ);
        obj.pushKV("nHeight", i);

        std::string strPayment = GetRequiredPaymentsString(i);
        if (strFilter != "" && strPayment.find(strFilter) == std::string::npos) continue;

        if (strPayment.find(',') != std::string::npos) {
            UniValue winner(UniValue::VARR);
            boost::char_separator<char> sep(",");
            boost::tokenizer< boost::char_separator<char> > tokens(strPayment, sep);
            for (const std::string& t : tokens) {
                UniValue addr(UniValue::VOBJ);
                std::size_t pos = t.find(":");
                std::string strAddress = t.substr(0,pos);
                uint64_t nVotes = atoi(t.substr(pos+1));
                addr.pushKV("address", strAddress);
                addr.pushKV("nVotes", nVotes);
                winner.push_back(addr);
            }
            obj.pushKV("winner", winner);
        } else if (strPayment.find("Unknown") == std::string::npos) {
            UniValue winner(UniValue::VOBJ);
            std::size_t pos = strPayment.find(":");
            std::string strAddress = strPayment.substr(0,pos);
            uint64_t nVotes = atoi(strPayment.substr(pos+1));
            winner.pushKV("address", strAddress);
            winner.pushKV("nVotes", nVotes);
            obj.pushKV("winner", winner);
        } else {
            UniValue winner(UniValue::VOBJ);
            winner.pushKV("address", strPayment);
            winner.pushKV("nVotes", 0);
            obj.pushKV("winner", winner);
        }

            ret.push_back(obj);
    }

    return ret;
}

UniValue getpatriotnodescores(const JSONRPCRequest& request)
{
    if (request.fHelp || request.params.size() > 1)
        throw std::runtime_error(
            "getpatriotnodescores ( blocks )\n"
            "\nPrint list of winning patriotnode by score\n"

            "\nArguments:\n"
            "1. blocks      (numeric, optional) Show the last n blocks (default 10)\n"

            "\nResult:\n"
            "{\n"
            "  xxxx: \"xxxx\"   (numeric : string) Block height : Patriotnode hash\n"
            "  ,...\n"
            "}\n"

            "\nExamples:\n" +
            HelpExampleCli("getpatriotnodescores", "") + HelpExampleRpc("getpatriotnodescores", ""));

    int nLast = 10;

    if (request.params.size() == 1) {
        try {
            nLast = std::stoi(request.params[0].get_str());
        } catch (const std::invalid_argument&) {
            throw std::runtime_error("Exception on param 2");
        }
    }

    std::vector<std::pair<PatriotnodeRef, int>> vMnScores = mnodeman.GetMnScores(nLast);
    if (vMnScores.empty()) return "unknown";

    UniValue obj(UniValue::VOBJ);
    for (const auto& p : vMnScores) {
        const PatriotnodeRef& mn = p.first;
        const int nHeight = p.second;
        obj.pushKV(strprintf("%d", nHeight), mn->vin.prevout.hash.ToString().c_str());
    }
    return obj;
}

bool DecodeHexMnb(CPatriotnodeBroadcast& mnb, std::string strHexMnb) {

    if (!IsHex(strHexMnb))
        return false;

    std::vector<unsigned char> mnbData(ParseHex(strHexMnb));
    CDataStream ssData(mnbData, SER_NETWORK, PROTOCOL_VERSION);
    try {
        ssData >> mnb;
    }
    catch (const std::exception&) {
        return false;
    }

    return true;
}
UniValue createpatriotnodebroadcast(const JSONRPCRequest& request)
{
    CWallet * const pwallet = GetWalletForJSONRPCRequest(request);

    if (!EnsureWalletIsAvailable(pwallet, request.fHelp))
        return NullUniValue;

    std::string strCommand;
    if (request.params.size() >= 1)
        strCommand = request.params[0].get_str();
    if (request.fHelp || (strCommand != "alias" && strCommand != "all") || (strCommand == "alias" && request.params.size() < 2))
        throw std::runtime_error(
            "createpatriotnodebroadcast \"command\" ( \"alias\")\n"
            "\nCreates a patriotnode broadcast message for one or all patriotnodes configured in patriotnode.conf\n" +
            HelpRequiringPassphrase(pwallet) + "\n"

            "\nArguments:\n"
            "1. \"command\"      (string, required) \"alias\" for single patriotnode, \"all\" for all patriotnodes\n"
            "2. \"alias\"        (string, required if command is \"alias\") Alias of the patriotnode\n"

            "\nResult (all):\n"
            "{\n"
            "  \"overall\": \"xxx\",        (string) Overall status message indicating number of successes.\n"
            "  \"detail\": [                (array) JSON array of broadcast objects.\n"
            "    {\n"
            "      \"alias\": \"xxx\",      (string) Alias of the patriotnode.\n"
            "      \"success\": true|false, (boolean) Success status.\n"
            "      \"hex\": \"xxx\"         (string, if success=true) Hex encoded broadcast message.\n"
            "      \"error_message\": \"xxx\"   (string, if success=false) Error message, if any.\n"
            "    }\n"
            "    ,...\n"
            "  ]\n"
            "}\n"

            "\nResult (alias):\n"
            "{\n"
            "  \"alias\": \"xxx\",      (string) Alias of the patriotnode.\n"
            "  \"success\": true|false, (boolean) Success status.\n"
            "  \"hex\": \"xxx\"         (string, if success=true) Hex encoded broadcast message.\n"
            "  \"error_message\": \"xxx\"   (string, if success=false) Error message, if any.\n"
            "}\n"

            "\nExamples:\n" +
            HelpExampleCli("createpatriotnodebroadcast", "alias mymn1") + HelpExampleRpc("createpatriotnodebroadcast", "alias mymn1"));

    EnsureWalletIsUnlocked(pwallet);

    if (strCommand == "alias")
    {
        // wait for reindex and/or import to finish
        if (fImporting || fReindex)
            throw JSONRPCError(RPC_INTERNAL_ERROR, "Wait for reindex and/or import to finish");

        std::string alias = request.params[1].get_str();
        bool found = false;

        UniValue statusObj(UniValue::VOBJ);
        statusObj.pushKV("alias", alias);

        for (CPatriotnodeConfig::CPatriotnodeEntry mne : patriotnodeConfig.getEntries()) {
            if(mne.getAlias() == alias) {
                CPatriotnodeBroadcast mnb;
                found = true;
                std::string errorMessage;
                bool fSuccess = false;
                if (!StartPatriotnodeEntry(statusObj, mnb, fSuccess, mne, errorMessage, strCommand))
                        continue;
                SerializePNB(statusObj, mnb, fSuccess);
                break;
            }
        }

        if(!found) {
            statusObj.pushKV("success", false);
            statusObj.pushKV("error_message", "Could not find alias in config. Verify with listpatriotnodeconf.");
        }

        return statusObj;
    }

    if (strCommand == "all")
    {
        // wait for reindex and/or import to finish
        if (fImporting || fReindex)
            throw JSONRPCError(RPC_INTERNAL_ERROR, "Wait for reindex and/or import to finish");

        int successful = 0;
        int failed = 0;

        UniValue resultsObj(UniValue::VARR);

        for (CPatriotnodeConfig::CPatriotnodeEntry mne : patriotnodeConfig.getEntries()) {
            UniValue statusObj(UniValue::VOBJ);
            CPatriotnodeBroadcast mnb;
            std::string errorMessage;
            bool fSuccess = false;
            if (!StartPatriotnodeEntry(statusObj, mnb, fSuccess, mne, errorMessage, strCommand))
                    continue;
            SerializePNB(statusObj, mnb, fSuccess, successful, failed);
            resultsObj.push_back(statusObj);
        }

        UniValue returnObj(UniValue::VOBJ);
        returnObj.pushKV("overall", strprintf("Successfully created broadcast messages for %d patriotnodes, failed to create %d, total %d", successful, failed, successful + failed));
        returnObj.pushKV("detail", resultsObj);

        return returnObj;
    }
    return NullUniValue;
}

UniValue decodepatriotnodebroadcast(const JSONRPCRequest& request)
{
    if (request.fHelp || request.params.size() != 1)
        throw std::runtime_error(
            "decodepatriotnodebroadcast \"hexstring\"\n"
            "\nCommand to decode patriotnode broadcast messages\n"

            "\nArgument:\n"
            "1. \"hexstring\"        (string) The hex encoded patriotnode broadcast message\n"

            "\nResult:\n"
            "{\n"
            "  \"vin\": \"xxxx\"                (string) The unspent output which is holding the patriotnode collateral\n"
            "  \"addr\": \"xxxx\"               (string) IP address of the patriotnode\n"
            "  \"pubkeycollateral\": \"xxxx\"   (string) Collateral address's public key\n"
            "  \"pubkeypatriotnode\": \"xxxx\"   (string) Patriotnode's public key\n"
            "  \"vchsig\": \"xxxx\"             (string) Base64-encoded signature of this message (verifiable via pubkeycollateral)\n"
            "  \"sigtime\": \"nnn\"             (numeric) Signature timestamp\n"
            "  \"sigvalid\": \"xxx\"            (string) \"true\"/\"false\" whether or not the mnb signature checks out.\n"
            "  \"protocolversion\": \"nnn\"     (numeric) Patriotnode's protocol version\n"
            "  \"nMessVersion\": \"nnn\"        (numeric) PNB Message version number\n"
            "  \"lastping\" : {                 (object) JSON object with information about the patriotnode's last ping\n"
            "      \"vin\": \"xxxx\"            (string) The unspent output of the patriotnode which is signing the message\n"
            "      \"blockhash\": \"xxxx\"      (string) Current chaintip blockhash minus 12\n"
            "      \"sigtime\": \"nnn\"         (numeric) Signature time for this ping\n"
            "      \"sigvalid\": \"xxx\"        (string) \"true\"/\"false\" whether or not the mnp signature checks out.\n"
            "      \"vchsig\": \"xxxx\"         (string) Base64-encoded signature of this ping (verifiable via pubkeypatriotnode)\n"
            "      \"nMessVersion\": \"nnn\"    (numeric) PNP Message version number\n"
            "  }\n"
            "}\n"

            "\nExamples:\n" +
            HelpExampleCli("decodepatriotnodebroadcast", "hexstring") + HelpExampleRpc("decodepatriotnodebroadcast", "hexstring"));

    CPatriotnodeBroadcast mnb;

    if (!DecodeHexMnb(mnb, request.params[0].get_str()))
        throw JSONRPCError(RPC_DESERIALIZATION_ERROR, "Patriotnode broadcast message decode failed");

    UniValue resultObj(UniValue::VOBJ);

    resultObj.pushKV("vin", mnb.vin.prevout.ToString());
    resultObj.pushKV("addr", mnb.addr.ToString());
    resultObj.pushKV("pubkeycollateral", EncodeDestination(mnb.pubKeyCollateralAddress.GetID()));
    resultObj.pushKV("pubkeypatriotnode", EncodeDestination(mnb.pubKeyPatriotnode.GetID()));
    resultObj.pushKV("vchsig", mnb.GetSignatureBase64());
    resultObj.pushKV("sigtime", mnb.sigTime);
    resultObj.pushKV("sigvalid", mnb.CheckSignature() ? "true" : "false");
    resultObj.pushKV("protocolversion", mnb.protocolVersion);
    resultObj.pushKV("nMessVersion", mnb.nMessVersion);

    UniValue lastPingObj(UniValue::VOBJ);
    lastPingObj.pushKV("vin", mnb.lastPing.vin.prevout.ToString());
    lastPingObj.pushKV("blockhash", mnb.lastPing.blockHash.ToString());
    lastPingObj.pushKV("sigtime", mnb.lastPing.sigTime);
    lastPingObj.pushKV("sigvalid", mnb.lastPing.CheckSignature(mnb.pubKeyPatriotnode.GetID()) ? "true" : "false");
    lastPingObj.pushKV("vchsig", mnb.lastPing.GetSignatureBase64());
    lastPingObj.pushKV("nMessVersion", mnb.lastPing.nMessVersion);

    resultObj.pushKV("lastping", lastPingObj);

    return resultObj;
}

UniValue relaypatriotnodebroadcast(const JSONRPCRequest& request)
{
    if (request.fHelp || request.params.size() != 1)
        throw std::runtime_error(
            "relaypatriotnodebroadcast \"hexstring\"\n"
            "\nCommand to relay patriotnode broadcast messages\n"

            "\nArguments:\n"
            "1. \"hexstring\"        (string) The hex encoded patriotnode broadcast message\n"

            "\nExamples:\n" +
            HelpExampleCli("relaypatriotnodebroadcast", "hexstring") + HelpExampleRpc("relaypatriotnodebroadcast", "hexstring"));


    CPatriotnodeBroadcast mnb;

    if (!DecodeHexMnb(mnb, request.params[0].get_str()))
        throw JSONRPCError(RPC_DESERIALIZATION_ERROR, "Patriotnode broadcast message decode failed");

    if(!mnb.CheckSignature())
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Patriotnode broadcast signature verification failed");

    mnodeman.UpdatePatriotnodeList(mnb);
    mnb.Relay();

    return strprintf("Patriotnode broadcast sent (service %s, vin %s)", mnb.addr.ToString(), mnb.vin.ToString());
}

static const CRPCCommand commands[] =
{ //  category              name                         actor (function)            okSafe argNames
  //  --------------------- ---------------------------  --------------------------  ------ --------
    { "patriotnode",         "createpatriotnodebroadcast", &createpatriotnodebroadcast, true,  {"command","alias"} },
    { "patriotnode",         "createpatriotnodekey",       &createpatriotnodekey,       true,  {} },
    { "patriotnode",         "decodepatriotnodebroadcast", &decodepatriotnodebroadcast, true,  {"hexstring"} },
    { "patriotnode",         "getpatriotnodecount",        &getpatriotnodecount,        true,  {} },
    { "patriotnode",         "getpatriotnodeoutputs",      &getpatriotnodeoutputs,      true,  {} },
    { "patriotnode",         "getpatriotnodescores",       &getpatriotnodescores,       true,  {"blocks"} },
    { "patriotnode",         "getpatriotnodestatus",       &getpatriotnodestatus,       true,  {} },
    { "patriotnode",         "getpatriotnodewinners",      &getpatriotnodewinners,      true,  {"blocks","filter"} },
    { "patriotnode",         "initpatriotnode",            &initpatriotnode,            true,  {"privkey","address","deterministic"} },
    { "patriotnode",         "listpatriotnodeconf",        &listpatriotnodeconf,        true,  {"filter"} },
    { "patriotnode",         "listpatriotnodes",           &listpatriotnodes,           true,  {"filter"} },
    { "patriotnode",         "patriotnodecurrent",         &patriotnodecurrent,         true,  {} },
    { "patriotnode",         "relaypatriotnodebroadcast",  &relaypatriotnodebroadcast,  true,  {"hexstring"}  },
    { "patriotnode",         "startpatriotnode",           &startpatriotnode,           true,  {"set","lockwallet","alias","reload_conf"} },

    /* Not shown in help */
    { "hidden",             "getcachedblockhashes",      &getcachedblockhashes,      true,  {} },
    { "hidden",             "mnping",                    &mnping,                    true,  {} },
};

void RegisterPatriotnodeRPCCommands(CRPCTable &tableRPC)
{
    for (unsigned int vcidx = 0; vcidx < ARRAYLEN(commands); vcidx++)
        tableRPC.appendCommand(commands[vcidx].name, &commands[vcidx]);
}
