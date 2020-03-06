// Copyright (c) 2009-2012 The Bitcoin developers
// Copyright (c) 2015-2018 The PIVX developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "activepatriotnode.h"
#include "db.h"
#include "init.h"
#include "main.h"
#include "patriotnode-budget.h"
#include "patriotnode-payments.h"
#include "patriotnodeconfig.h"
#include "patriotnodeman.h"
#include "rpcserver.h"
#include "utilmoneystr.h"

#include <univalue.h>

#include <boost/tokenizer.hpp>
#include <fstream>

UniValue getpoolinfo(const UniValue& params, bool fHelp)
{
    if (fHelp || params.size() != 0)
        throw runtime_error(
            "getpoolinfo\n"
            "\nReturns anonymous pool-related information\n"

            "\nResult:\n"
            "{\n"
            "  \"current\": \"addr\",    (string) TrumpCoin address of current patriotnode\n"
            "  \"state\": xxxx,        (string) unknown\n"
            "  \"entries\": xxxx,      (numeric) Number of entries\n"
            "  \"accepted\": xxxx,     (numeric) Number of entries accepted\n"
            "}\n"

            "\nExamples:\n" +
            HelpExampleCli("getpoolinfo", "") + HelpExampleRpc("getpoolinfo", ""));

    UniValue obj(UniValue::VOBJ);
    obj.push_back(Pair("current_patriotnode", mnodeman.GetCurrentPatriotNode()->addr.ToString()));
    obj.push_back(Pair("state", obfuScationPool.GetState()));
    obj.push_back(Pair("entries", obfuScationPool.GetEntriesCount()));
    obj.push_back(Pair("entries_accepted", obfuScationPool.GetCountEntriesAccepted()));
    return obj;
}

// This command is retained for backwards compatibility, but is depreciated.
// Future removal of this command is planned to keep things clean.
UniValue patriotnode(const UniValue& params, bool fHelp)
{
    string strCommand;
    if (params.size() >= 1)
        strCommand = params[0].get_str();

    if (fHelp ||
        (strCommand != "start" && strCommand != "start-alias" && strCommand != "start-many" && strCommand != "start-all" && strCommand != "start-missing" &&
            strCommand != "start-disabled" && strCommand != "list" && strCommand != "list-conf" && strCommand != "count" && strCommand != "enforce" &&
            strCommand != "debug" && strCommand != "current" && strCommand != "winners" && strCommand != "genkey" && strCommand != "connect" &&
            strCommand != "outputs" && strCommand != "status" && strCommand != "calcscore"))
        throw runtime_error(
            "patriotnode \"command\"...\n"
            "\nSet of commands to execute patriotnode related actions\n"
            "This command is depreciated, please see individual command documentation for future reference\n\n"

            "\nArguments:\n"
            "1. \"command\"        (string or set of strings, required) The command to execute\n"

            "\nAvailable commands:\n"
            "  count        - Print count information of all known patriotnodes\n"
            "  current      - Print info on current patriotnode winner\n"
            "  debug        - Print patriotnode status\n"
            "  genkey       - Generate new patriotnodeprivkey\n"
            "  outputs      - Print patriotnode compatible outputs\n"
            "  start        - Start patriotnode configured in trumpcoin.conf\n"
            "  start-alias  - Start single patriotnode by assigned alias configured in patriotnode.conf\n"
            "  start-<mode> - Start patriotnodes configured in patriotnode.conf (<mode>: 'all', 'missing', 'disabled')\n"
            "  status       - Print patriotnode status information\n"
            "  list         - Print list of all known patriotnodes (see patriotnodelist for more info)\n"
            "  list-conf    - Print patriotnode.conf in JSON format\n"
            "  winners      - Print list of patriotnode winners\n");

    if (strCommand == "list") {
        UniValue newParams(UniValue::VARR);
        // forward params but skip command
        for (unsigned int i = 1; i < params.size(); i++) {
            newParams.push_back(params[i]);
        }
        return listpatriotnodes(newParams, fHelp);
    }

    if (strCommand == "connect") {
        UniValue newParams(UniValue::VARR);
        // forward params but skip command
        for (unsigned int i = 1; i < params.size(); i++) {
            newParams.push_back(params[i]);
        }
        return patriotnodeconnect(newParams, fHelp);
    }

    if (strCommand == "count") {
        UniValue newParams(UniValue::VARR);
        // forward params but skip command
        for (unsigned int i = 1; i < params.size(); i++) {
            newParams.push_back(params[i]);
        }
        return getpatriotnodecount(newParams, fHelp);
    }

    if (strCommand == "current") {
        UniValue newParams(UniValue::VARR);
        // forward params but skip command
        for (unsigned int i = 1; i < params.size(); i++) {
            newParams.push_back(params[i]);
        }
        return patriotnodecurrent(newParams, fHelp);
    }

    if (strCommand == "debug") {
        UniValue newParams(UniValue::VARR);
        // forward params but skip command
        for (unsigned int i = 1; i < params.size(); i++) {
            newParams.push_back(params[i]);
        }
        return patriotnodedebug(newParams, fHelp);
    }

    if (strCommand == "start" || strCommand == "start-alias" || strCommand == "start-many" || strCommand == "start-all" || strCommand == "start-missing" || strCommand == "start-disabled") {
        return startpatriotnode(params, fHelp);
    }

    if (strCommand == "genkey") {
        UniValue newParams(UniValue::VARR);
        // forward params but skip command
        for (unsigned int i = 1; i < params.size(); i++) {
            newParams.push_back(params[i]);
        }
        return createpatriotnodekey(newParams, fHelp);
    }

    if (strCommand == "list-conf") {
        UniValue newParams(UniValue::VARR);
        // forward params but skip command
        for (unsigned int i = 1; i < params.size(); i++) {
            newParams.push_back(params[i]);
        }
        return listpatriotnodeconf(newParams, fHelp);
    }

    if (strCommand == "outputs") {
        UniValue newParams(UniValue::VARR);
        // forward params but skip command
        for (unsigned int i = 1; i < params.size(); i++) {
            newParams.push_back(params[i]);
        }
        return getpatriotnodeoutputs(newParams, fHelp);
    }

    if (strCommand == "status") {
        UniValue newParams(UniValue::VARR);
        // forward params but skip command
        for (unsigned int i = 1; i < params.size(); i++) {
            newParams.push_back(params[i]);
        }
        return getpatriotnodestatus(newParams, fHelp);
    }

    if (strCommand == "winners") {
        UniValue newParams(UniValue::VARR);
        // forward params but skip command
        for (unsigned int i = 1; i < params.size(); i++) {
            newParams.push_back(params[i]);
        }
        return getpatriotnodewinners(newParams, fHelp);
    }

    if (strCommand == "calcscore") {
        UniValue newParams(UniValue::VARR);
        // forward params but skip command
        for (unsigned int i = 1; i < params.size(); i++) {
            newParams.push_back(params[i]);
        }
        return getpatriotnodescores(newParams, fHelp);
    }

    return NullUniValue;
}

UniValue listpatriotnodes(const UniValue& params, bool fHelp)
{
    std::string strFilter = "";

    if (params.size() == 1) strFilter = params[0].get_str();

    if (fHelp || (params.size() > 1))
        throw runtime_error(
            "listpatriotnodes ( \"filter\" )\n"
            "\nGet a ranked list of patriotnodes\n"

            "\nArguments:\n"
            "1. \"filter\"    (string, optional) Filter search text. Partial match by txhash, status, or addr.\n"

            "\nResult:\n"
            "[\n"
            "  {\n"
            "    \"rank\": n,           (numeric) Patriotnode Rank (or 0 if not enabled)\n"
            "    \"txhash\": \"hash\",    (string) Collateral transaction hash\n"
            "    \"outidx\": n,         (numeric) Collateral transaction output index\n"
            "    \"status\": s,         (string) Status (ENABLED/EXPIRED/REMOVE/etc)\n"
            "    \"addr\": \"addr\",      (string) Patriotnode TrumpCoin address\n"
            "    \"version\": v,        (numeric) Patriotnode protocol version\n"
            "    \"lastseen\": ttt,     (numeric) The time in seconds since epoch (Jan 1 1970 GMT) of the last seen\n"
            "    \"activetime\": ttt,   (numeric) The time in seconds since epoch (Jan 1 1970 GMT) patriotnode has been active\n"
            "    \"lastpaid\": ttt,     (numeric) The time in seconds since epoch (Jan 1 1970 GMT) patriotnode was last paid\n"
            "  }\n"
            "  ,...\n"
            "]\n"

            "\nExamples:\n" +
            HelpExampleCli("listpatriotnodes", "") + HelpExampleRpc("listpatriotnodes", ""));

    UniValue ret(UniValue::VARR);
    int nHeight;
    {
        LOCK(cs_main);
        CBlockIndex* pindex = chainActive.Tip();
        if(!pindex) return 0;
        nHeight = pindex->nHeight;
    }
    std::vector<pair<int, CPatriotnode> > vPatriotnodeRanks = mnodeman.GetPatriotnodeRanks(nHeight);
    BOOST_FOREACH (PAIRTYPE(int, CPatriotnode) & s, vPatriotnodeRanks) {
        UniValue obj(UniValue::VOBJ);
        std::string strVin = s.second.vin.prevout.ToStringShort();
        std::string strTxHash = s.second.vin.prevout.hash.ToString();
        uint32_t oIdx = s.second.vin.prevout.n;

        CPatriotnode* mn = mnodeman.Find(s.second.vin);

        if (mn != NULL) {
            if (strFilter != "" && strTxHash.find(strFilter) == string::npos &&
                mn->Status().find(strFilter) == string::npos &&
                CBitcoinAddress(mn->pubKeyCollateralAddress.GetID()).ToString().find(strFilter) == string::npos) continue;

            std::string strStatus = mn->Status();
            std::string strHost;
            int port;
            SplitHostPort(mn->addr.ToString(), port, strHost);
            CNetAddr node = CNetAddr(strHost, false);
            std::string strNetwork = GetNetworkName(node.GetNetwork());

            obj.push_back(Pair("rank", (strStatus == "ENABLED" ? s.first : 0)));
            obj.push_back(Pair("network", strNetwork));
            obj.push_back(Pair("txhash", strTxHash));
            obj.push_back(Pair("outidx", (uint64_t)oIdx));
            obj.push_back(Pair("status", strStatus));
            obj.push_back(Pair("addr", CBitcoinAddress(mn->pubKeyCollateralAddress.GetID()).ToString()));
            obj.push_back(Pair("version", mn->protocolVersion));
            obj.push_back(Pair("ipport", mn->addr.ToString()));
            obj.push_back(Pair("lastseen", (int64_t)mn->lastPing.sigTime));
            obj.push_back(Pair("activetime", (int64_t)(mn->lastPing.sigTime - mn->sigTime)));
            obj.push_back(Pair("lastpaid", (int64_t)mn->GetLastPaid()));

            ret.push_back(obj);
        }
    }

    return ret;
}

UniValue patriotnodeconnect(const UniValue& params, bool fHelp)
{
    if (fHelp || (params.size() != 1))
        throw runtime_error(
            "patriotnodeconnect \"address\"\n"
            "\nAttempts to connect to specified patriotnode address\n"

            "\nArguments:\n"
            "1. \"address\"     (string, required) IP or net address to connect to\n"

            "\nExamples:\n" +
            HelpExampleCli("patriotnodeconnect", "\"192.168.0.6:15110\"") + HelpExampleRpc("patriotnodeconnect", "\"192.168.0.6:15110\""));

    std::string strAddress = params[0].get_str();

    CService addr = CService(strAddress);

    CNode* pnode = ConnectNode((CAddress)addr, NULL, false);
    if (pnode) {
        pnode->Release();
        return NullUniValue;
    } else {
        throw runtime_error("error connecting\n");
    }
}

UniValue getpatriotnodecount (const UniValue& params, bool fHelp)
{
    if (fHelp || (params.size() > 0))
        throw runtime_error(
            "getpatriotnodecount\n"
            "\nGet patriotnode count values\n"

            "\nResult:\n"
            "{\n"
            "  \"total\": n,        (numeric) Total patriotnodes\n"
            "  \"stable\": n,       (numeric) Stable count\n"
            "  \"obfcompat\": n,    (numeric) Obfuscation Compatible\n"
            "  \"enabled\": n,      (numeric) Enabled patriotnodes\n"
            "  \"inqueue\": n       (numeric) Patriotnodes in queue\n"
            "}\n"

            "\nExamples:\n" +
            HelpExampleCli("getpatriotnodecount", "") + HelpExampleRpc("getpatriotnodecount", ""));

    UniValue obj(UniValue::VOBJ);
    int nCount = 0;
    int ipv4 = 0, ipv6 = 0, onion = 0;

    if (chainActive.Tip())
        mnodeman.GetNextPatriotnodeInQueueForPayment(chainActive.Tip()->nHeight, true, nCount);

    mnodeman.CountNetworks(ActiveProtocol(), ipv4, ipv6, onion);

    obj.push_back(Pair("total", mnodeman.size()));
    obj.push_back(Pair("stable", mnodeman.stable_size()));
    obj.push_back(Pair("obfcompat", mnodeman.CountEnabled(ActiveProtocol())));
    obj.push_back(Pair("enabled", mnodeman.CountEnabled()));
    obj.push_back(Pair("inqueue", nCount));
    obj.push_back(Pair("ipv4", ipv4));
    obj.push_back(Pair("ipv6", ipv6));
    obj.push_back(Pair("onion", onion));

    return obj;
}

UniValue patriotnodecurrent (const UniValue& params, bool fHelp)
{
    if (fHelp || (params.size() != 0))
        throw runtime_error(
            "patriotnodecurrent\n"
            "\nGet current patriotnode winner\n"

            "\nResult:\n"
            "{\n"
            "  \"protocol\": xxxx,        (numeric) Protocol version\n"
            "  \"txhash\": \"xxxx\",      (string) Collateral transaction hash\n"
            "  \"pubkey\": \"xxxx\",      (string) MN Public key\n"
            "  \"lastseen\": xxx,       (numeric) Time since epoch of last seen\n"
            "  \"activeseconds\": xxx,  (numeric) Seconds MN has been active\n"
            "}\n"

            "\nExamples:\n" +
            HelpExampleCli("patriotnodecurrent", "") + HelpExampleRpc("patriotnodecurrent", ""));

    CPatriotnode* winner = mnodeman.GetCurrentPatriotNode(1);
    if (winner) {
        UniValue obj(UniValue::VOBJ);

        obj.push_back(Pair("protocol", (int64_t)winner->protocolVersion));
        obj.push_back(Pair("txhash", winner->vin.prevout.hash.ToString()));
        obj.push_back(Pair("pubkey", CBitcoinAddress(winner->pubKeyCollateralAddress.GetID()).ToString()));
        obj.push_back(Pair("lastseen", (winner->lastPing == CPatriotnodePing()) ? winner->sigTime : (int64_t)winner->lastPing.sigTime));
        obj.push_back(Pair("activeseconds", (winner->lastPing == CPatriotnodePing()) ? 0 : (int64_t)(winner->lastPing.sigTime - winner->sigTime)));
        return obj;
    }

    throw runtime_error("unknown");
}

UniValue patriotnodedebug (const UniValue& params, bool fHelp)
{
    if (fHelp || (params.size() != 0))
        throw runtime_error(
            "patriotnodedebug\n"
            "\nPrint patriotnode status\n"

            "\nResult:\n"
            "\"status\"     (string) Patriotnode status message\n"

            "\nExamples:\n" +
            HelpExampleCli("patriotnodedebug", "") + HelpExampleRpc("patriotnodedebug", ""));

    if (activePatriotnode.status != ACTIVE_PATRIOTNODE_INITIAL || !patriotnodeSync.IsSynced())
        return activePatriotnode.GetStatus();

    CTxIn vin = CTxIn();
    CPubKey pubkey;
    CKey key;
    if (!activePatriotnode.GetPatriotNodeVin(vin, pubkey, key))
        throw runtime_error("Missing patriotnode input, please look at the documentation for instructions on patriotnode creation\n");
    else
        return activePatriotnode.GetStatus();
}

UniValue startpatriotnode (const UniValue& params, bool fHelp)
{
    std::string strCommand;
    if (params.size() >= 1) {
        strCommand = params[0].get_str();

        // Backwards compatibility with legacy 'patriotnode' super-command forwarder
        if (strCommand == "start") strCommand = "local";
        if (strCommand == "start-alias") strCommand = "alias";
        if (strCommand == "start-all") strCommand = "all";
        if (strCommand == "start-many") strCommand = "many";
        if (strCommand == "start-missing") strCommand = "missing";
        if (strCommand == "start-disabled") strCommand = "disabled";
    }

    if (fHelp || params.size() < 2 || params.size() > 3 ||
        (params.size() == 2 && (strCommand != "local" && strCommand != "all" && strCommand != "many" && strCommand != "missing" && strCommand != "disabled")) ||
        (params.size() == 3 && strCommand != "alias"))
        throw runtime_error(
            "startpatriotnode \"local|all|many|missing|disabled|alias\" lockwallet ( \"alias\" )\n"
            "\nAttempts to start one or more patriotnode(s)\n"

            "\nArguments:\n"
            "1. set         (string, required) Specify which set of patriotnode(s) to start.\n"
            "2. lockwallet  (boolean, required) Lock wallet after completion.\n"
            "3. alias       (string) Patriotnode alias. Required if using 'alias' as the set.\n"

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

    bool fLock = (params[1].get_str() == "true" ? true : false);

    EnsureWalletIsUnlocked();

    if (strCommand == "local") {
        if (!fPatriotNode) throw runtime_error("you must set patriotnode=1 in the configuration\n");

        if (activePatriotnode.status != ACTIVE_PATRIOTNODE_STARTED) {
            activePatriotnode.status = ACTIVE_PATRIOTNODE_INITIAL; // TODO: consider better way
            activePatriotnode.ManageStatus();
            if (fLock)
                pwalletMain->Lock();
        }

        return activePatriotnode.GetStatus();
    }

    if (strCommand == "all" || strCommand == "many" || strCommand == "missing" || strCommand == "disabled") {
        if ((strCommand == "missing" || strCommand == "disabled") &&
            (patriotnodeSync.RequestedPatriotnodeAssets <= PATRIOTNODE_SYNC_LIST ||
                patriotnodeSync.RequestedPatriotnodeAssets == PATRIOTNODE_SYNC_FAILED)) {
            throw runtime_error("You can't use this command until patriotnode list is synced\n");
        }

        std::vector<CPatriotnodeConfig::CPatriotnodeEntry> mnEntries;
        mnEntries = patriotnodeConfig.getEntries();

        int successful = 0;
        int failed = 0;

        UniValue resultsObj(UniValue::VARR);

        BOOST_FOREACH (CPatriotnodeConfig::CPatriotnodeEntry mne, patriotnodeConfig.getEntries()) {
            std::string errorMessage;
            int nIndex;
            if(!mne.castOutputIndex(nIndex))
                continue;
            CTxIn vin = CTxIn(uint256(mne.getTxHash()), uint32_t(nIndex));
            CPatriotnode* pmn = mnodeman.Find(vin);
            CPatriotnodeBroadcast mnb;

            if (pmn != NULL) {
                if (strCommand == "missing") continue;
                if (strCommand == "disabled" && pmn->IsEnabled()) continue;
            }

            bool result = activePatriotnode.CreateBroadcast(mne.getIp(), mne.getPrivKey(), mne.getTxHash(), mne.getOutputIndex(), errorMessage, mnb);

            UniValue statusObj(UniValue::VOBJ);
            statusObj.push_back(Pair("alias", mne.getAlias()));
            statusObj.push_back(Pair("result", result ? "success" : "failed"));

            if (result) {
                successful++;
                statusObj.push_back(Pair("error", ""));
            } else {
                failed++;
                statusObj.push_back(Pair("error", errorMessage));
            }

            resultsObj.push_back(statusObj);
        }
        if (fLock)
            pwalletMain->Lock();

        UniValue returnObj(UniValue::VOBJ);
        returnObj.push_back(Pair("overall", strprintf("Successfully started %d patriotnodes, failed to start %d, total %d", successful, failed, successful + failed)));
        returnObj.push_back(Pair("detail", resultsObj));

        return returnObj;
    }

    if (strCommand == "alias") {
        std::string alias = params[2].get_str();

        bool found = false;
        int successful = 0;
        int failed = 0;

        UniValue resultsObj(UniValue::VARR);
        UniValue statusObj(UniValue::VOBJ);
        statusObj.push_back(Pair("alias", alias));

        BOOST_FOREACH (CPatriotnodeConfig::CPatriotnodeEntry mne, patriotnodeConfig.getEntries()) {
            if (mne.getAlias() == alias) {
                found = true;
                std::string errorMessage;
                CPatriotnodeBroadcast mnb;

                bool result = activePatriotnode.CreateBroadcast(mne.getIp(), mne.getPrivKey(), mne.getTxHash(), mne.getOutputIndex(), errorMessage, mnb);

                statusObj.push_back(Pair("result", result ? "successful" : "failed"));

                if (result) {
                    successful++;
                    mnodeman.UpdatePatriotnodeList(mnb);
                    mnb.Relay();
                } else {
                    failed++;
                    statusObj.push_back(Pair("errorMessage", errorMessage));
                }
                break;
            }
        }

        if (!found) {
            failed++;
            statusObj.push_back(Pair("result", "failed"));
            statusObj.push_back(Pair("error", "could not find alias in config. Verify with list-conf."));
        }

        resultsObj.push_back(statusObj);

        if (fLock)
            pwalletMain->Lock();

        UniValue returnObj(UniValue::VOBJ);
        returnObj.push_back(Pair("overall", strprintf("Successfully started %d patriotnodes, failed to start %d, total %d", successful, failed, successful + failed)));
        returnObj.push_back(Pair("detail", resultsObj));

        return returnObj;
    }
    return NullUniValue;
}

UniValue createpatriotnodekey (const UniValue& params, bool fHelp)
{
    if (fHelp || (params.size() != 0))
        throw runtime_error(
            "createpatriotnodekey\n"
            "\nCreate a new patriotnode private key\n"

            "\nResult:\n"
            "\"key\"    (string) Patriotnode private key\n"

            "\nExamples:\n" +
            HelpExampleCli("createpatriotnodekey", "") + HelpExampleRpc("createpatriotnodekey", ""));

    CKey secret;
    secret.MakeNewKey(false);

    return CBitcoinSecret(secret).ToString();
}

UniValue getpatriotnodeoutputs (const UniValue& params, bool fHelp)
{
    if (fHelp || (params.size() != 0))
        throw runtime_error(
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
    vector<COutput> possibleCoins = activePatriotnode.SelectCoinsPatriotnode();

    UniValue ret(UniValue::VARR);
    BOOST_FOREACH (COutput& out, possibleCoins) {
        UniValue obj(UniValue::VOBJ);
        obj.push_back(Pair("txhash", out.tx->GetHash().ToString()));
        obj.push_back(Pair("outputidx", out.i));
        ret.push_back(obj);
    }

    return ret;
}

UniValue listpatriotnodeconf (const UniValue& params, bool fHelp)
{
    std::string strFilter = "";

    if (params.size() == 1) strFilter = params[0].get_str();

    if (fHelp || (params.size() > 1))
        throw runtime_error(
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

    BOOST_FOREACH (CPatriotnodeConfig::CPatriotnodeEntry mne, patriotnodeConfig.getEntries()) {
        int nIndex;
        if(!mne.castOutputIndex(nIndex))
            continue;
        CTxIn vin = CTxIn(uint256(mne.getTxHash()), uint32_t(nIndex));
        CPatriotnode* pmn = mnodeman.Find(vin);

        std::string strStatus = pmn ? pmn->Status() : "MISSING";

        if (strFilter != "" && mne.getAlias().find(strFilter) == string::npos &&
            mne.getIp().find(strFilter) == string::npos &&
            mne.getTxHash().find(strFilter) == string::npos &&
            strStatus.find(strFilter) == string::npos) continue;

        UniValue mnObj(UniValue::VOBJ);
        mnObj.push_back(Pair("alias", mne.getAlias()));
        mnObj.push_back(Pair("address", mne.getIp()));
        mnObj.push_back(Pair("privateKey", mne.getPrivKey()));
        mnObj.push_back(Pair("txHash", mne.getTxHash()));
        mnObj.push_back(Pair("outputIndex", mne.getOutputIndex()));
        mnObj.push_back(Pair("status", strStatus));
        ret.push_back(mnObj);
    }

    return ret;
}

UniValue getpatriotnodestatus (const UniValue& params, bool fHelp)
{
    if (fHelp || (params.size() != 0))
        throw runtime_error(
            "getpatriotnodestatus\n"
            "\nPrint patriotnode status\n"

            "\nResult:\n"
            "{\n"
            "  \"txhash\": \"xxxx\",      (string) Collateral transaction hash\n"
            "  \"outputidx\": n,        (numeric) Collateral transaction output index number\n"
            "  \"netaddr\": \"xxxx\",     (string) Patriotnode network address\n"
            "  \"addr\": \"xxxx\",        (string) TrumpCoin address for patriotnode payments\n"
            "  \"status\": \"xxxx\",      (string) Patriotnode status\n"
            "  \"message\": \"xxxx\"      (string) Patriotnode status message\n"
            "}\n"

            "\nExamples:\n" +
            HelpExampleCli("getpatriotnodestatus", "") + HelpExampleRpc("getpatriotnodestatus", ""));

    if (!fPatriotNode) throw runtime_error("This is not a patriotnode");

    CPatriotnode* pmn = mnodeman.Find(activePatriotnode.vin);

    if (pmn) {
        UniValue mnObj(UniValue::VOBJ);
        mnObj.push_back(Pair("txhash", activePatriotnode.vin.prevout.hash.ToString()));
        mnObj.push_back(Pair("outputidx", (uint64_t)activePatriotnode.vin.prevout.n));
        mnObj.push_back(Pair("netaddr", activePatriotnode.service.ToString()));
        mnObj.push_back(Pair("addr", CBitcoinAddress(pmn->pubKeyCollateralAddress.GetID()).ToString()));
        mnObj.push_back(Pair("status", activePatriotnode.status));
        mnObj.push_back(Pair("message", activePatriotnode.GetStatus()));
        return mnObj;
    }
    throw runtime_error("Patriotnode not found in the list of available patriotnodes. Current status: "
                        + activePatriotnode.GetStatus());
}

UniValue getpatriotnodewinners (const UniValue& params, bool fHelp)
{
    if (fHelp || params.size() > 3)
        throw runtime_error(
            "getpatriotnodewinners ( blocks \"filter\" )\n"
            "\nPrint the patriotnode winners for the last n blocks\n"

            "\nArguments:\n"
            "1. blocks      (numeric, optional) Number of previous blocks to show (default: 10)\n"
            "2. filter      (string, optional) Search filter matching MN address\n"

            "\nResult (single winner):\n"
            "[\n"
            "  {\n"
            "    \"nHeight\": n,           (numeric) block height\n"
            "    \"winner\": {\n"
            "      \"address\": \"xxxx\",    (string) TrumpCoin MN Address\n"
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
            "        \"address\": \"xxxx\",  (string) TrumpCoin MN Address\n"
            "        \"nVotes\": n,        (numeric) Number of votes for winner\n"
            "      }\n"
            "      ,...\n"
            "    ]\n"
            "  }\n"
            "  ,...\n"
            "]\n"

            "\nExamples:\n" +
            HelpExampleCli("getpatriotnodewinners", "") + HelpExampleRpc("getpatriotnodewinners", ""));

    int nHeight;
    {
        LOCK(cs_main);
        CBlockIndex* pindex = chainActive.Tip();
        if(!pindex) return 0;
        nHeight = pindex->nHeight;
    }

    int nLast = 10;
    std::string strFilter = "";

    if (params.size() >= 1)
        nLast = atoi(params[0].get_str());

    if (params.size() == 2)
        strFilter = params[1].get_str();

    UniValue ret(UniValue::VARR);

    for (int i = nHeight - nLast; i < nHeight + 20; i++) {
        UniValue obj(UniValue::VOBJ);
        obj.push_back(Pair("nHeight", i));

        std::string strPayment = GetRequiredPaymentsString(i);
        if (strFilter != "" && strPayment.find(strFilter) == std::string::npos) continue;

        if (strPayment.find(',') != std::string::npos) {
            UniValue winner(UniValue::VARR);
            boost::char_separator<char> sep(",");
            boost::tokenizer< boost::char_separator<char> > tokens(strPayment, sep);
            BOOST_FOREACH (const string& t, tokens) {
                UniValue addr(UniValue::VOBJ);
                std::size_t pos = t.find(":");
                std::string strAddress = t.substr(0,pos);
                uint64_t nVotes = atoi(t.substr(pos+1));
                addr.push_back(Pair("address", strAddress));
                addr.push_back(Pair("nVotes", nVotes));
                winner.push_back(addr);
            }
            obj.push_back(Pair("winner", winner));
        } else if (strPayment.find("Unknown") == std::string::npos) {
            UniValue winner(UniValue::VOBJ);
            std::size_t pos = strPayment.find(":");
            std::string strAddress = strPayment.substr(0,pos);
            uint64_t nVotes = atoi(strPayment.substr(pos+1));
            winner.push_back(Pair("address", strAddress));
            winner.push_back(Pair("nVotes", nVotes));
            obj.push_back(Pair("winner", winner));
        } else {
            UniValue winner(UniValue::VOBJ);
            winner.push_back(Pair("address", strPayment));
            winner.push_back(Pair("nVotes", 0));
            obj.push_back(Pair("winner", winner));
        }

            ret.push_back(obj);
    }

    return ret;
}

UniValue getpatriotnodescores (const UniValue& params, bool fHelp)
{
    if (fHelp || params.size() > 1)
        throw runtime_error(
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

    if (params.size() == 1) {
        try {
            nLast = std::stoi(params[0].get_str());
        } catch (const boost::bad_lexical_cast &) {
            throw runtime_error("Exception on param 2");
        }
    }
    UniValue obj(UniValue::VOBJ);

    std::vector<CPatriotnode> vPatriotnodes = mnodeman.GetFullPatriotnodeVector();
    for (int nHeight = chainActive.Tip()->nHeight - nLast; nHeight < chainActive.Tip()->nHeight + 20; nHeight++) {
        uint256 nHigh = 0;
        CPatriotnode* pBestPatriotnode = NULL;
        BOOST_FOREACH (CPatriotnode& mn, vPatriotnodes) {
            uint256 n = mn.CalculateScore(1, nHeight - 100);
            if (n > nHigh) {
                nHigh = n;
                pBestPatriotnode = &mn;
            }
        }
        if (pBestPatriotnode)
            obj.push_back(Pair(strprintf("%d", nHeight), pBestPatriotnode->vin.prevout.hash.ToString().c_str()));
    }

    return obj;
}

bool DecodeHexMnb(CPatriotnodeBroadcast& mnb, std::string strHexMnb) {

    if (!IsHex(strHexMnb))
        return false;

    vector<unsigned char> mnbData(ParseHex(strHexMnb));
    CDataStream ssData(mnbData, SER_NETWORK, PROTOCOL_VERSION);
    try {
        ssData >> mnb;
    }
    catch (const std::exception&) {
        return false;
    }

    return true;
}
UniValue createpatriotnodebroadcast(const UniValue& params, bool fHelp)
{
    string strCommand;
    if (params.size() >= 1)
        strCommand = params[0].get_str();
    if (fHelp || (strCommand != "alias" && strCommand != "all") || (strCommand == "alias" && params.size() < 2))
        throw runtime_error(
            "createpatriotnodebroadcast \"command\" ( \"alias\")\n"
            "\nCreates a patriotnode broadcast message for one or all patriotnodes configured in patriotnode.conf\n" +
            HelpRequiringPassphrase() + "\n"

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

    EnsureWalletIsUnlocked();

    if (strCommand == "alias")
    {
        // wait for reindex and/or import to finish
        if (fImporting || fReindex)
            throw JSONRPCError(RPC_INTERNAL_ERROR, "Wait for reindex and/or import to finish");

        std::string alias = params[1].get_str();
        bool found = false;

        UniValue statusObj(UniValue::VOBJ);
        statusObj.push_back(Pair("alias", alias));

        BOOST_FOREACH(CPatriotnodeConfig::CPatriotnodeEntry mne, patriotnodeConfig.getEntries()) {
            if(mne.getAlias() == alias) {
                found = true;
                std::string errorMessage;
                CPatriotnodeBroadcast mnb;

                bool success = activePatriotnode.CreateBroadcast(mne.getIp(), mne.getPrivKey(), mne.getTxHash(), mne.getOutputIndex(), errorMessage, mnb, true);

                statusObj.push_back(Pair("success", success));
                if(success) {
                    CDataStream ssMnb(SER_NETWORK, PROTOCOL_VERSION);
                    ssMnb << mnb;
                    statusObj.push_back(Pair("hex", HexStr(ssMnb.begin(), ssMnb.end())));
                } else {
                    statusObj.push_back(Pair("error_message", errorMessage));
                }
                break;
            }
        }

        if(!found) {
            statusObj.push_back(Pair("success", false));
            statusObj.push_back(Pair("error_message", "Could not find alias in config. Verify with list-conf."));
        }

        return statusObj;

    }

    if (strCommand == "all")
    {
        // wait for reindex and/or import to finish
        if (fImporting || fReindex)
            throw JSONRPCError(RPC_INTERNAL_ERROR, "Wait for reindex and/or import to finish");

        std::vector<CPatriotnodeConfig::CPatriotnodeEntry> mnEntries;
        mnEntries = patriotnodeConfig.getEntries();

        int successful = 0;
        int failed = 0;

        UniValue resultsObj(UniValue::VARR);

        BOOST_FOREACH(CPatriotnodeConfig::CPatriotnodeEntry mne, patriotnodeConfig.getEntries()) {
            std::string errorMessage;

            CTxIn vin = CTxIn(uint256S(mne.getTxHash()), uint32_t(atoi(mne.getOutputIndex().c_str())));
            CPatriotnodeBroadcast mnb;

            bool success = activePatriotnode.CreateBroadcast(mne.getIp(), mne.getPrivKey(), mne.getTxHash(), mne.getOutputIndex(), errorMessage, mnb, true);

            UniValue statusObj(UniValue::VOBJ);
            statusObj.push_back(Pair("alias", mne.getAlias()));
            statusObj.push_back(Pair("success", success));

            if(success) {
                successful++;
                CDataStream ssMnb(SER_NETWORK, PROTOCOL_VERSION);
                ssMnb << mnb;
                statusObj.push_back(Pair("hex", HexStr(ssMnb.begin(), ssMnb.end())));
            } else {
                failed++;
                statusObj.push_back(Pair("error_message", errorMessage));
            }

            resultsObj.push_back(statusObj);
        }

        UniValue returnObj(UniValue::VOBJ);
        returnObj.push_back(Pair("overall", strprintf("Successfully created broadcast messages for %d patriotnodes, failed to create %d, total %d", successful, failed, successful + failed)));
        returnObj.push_back(Pair("detail", resultsObj));

        return returnObj;
    }
    return NullUniValue;
}

UniValue decodepatriotnodebroadcast(const UniValue& params, bool fHelp)
{
    if (fHelp || params.size() != 1)
        throw runtime_error(
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
            "  \"protocolversion\": \"nnn\"     (numeric) Patriotnode's protocol version\n"
            "  \"nlastdsq\": \"nnn\"            (numeric) The last time the patriotnode sent a DSQ message (for mixing) (DEPRECATED)\n"
            "  \"lastping\" : {                 (object) JSON object with information about the patriotnode's last ping\n"
            "      \"vin\": \"xxxx\"            (string) The unspent output of the patriotnode which is signing the message\n"
            "      \"blockhash\": \"xxxx\"      (string) Current chaintip blockhash minus 12\n"
            "      \"sigtime\": \"nnn\"         (numeric) Signature time for this ping\n"
            "      \"vchsig\": \"xxxx\"         (string) Base64-encoded signature of this ping (verifiable via pubkeypatriotnode)\n"
            "}\n"

            "\nExamples:\n" +
            HelpExampleCli("decodepatriotnodebroadcast", "hexstring") + HelpExampleRpc("decodepatriotnodebroadcast", "hexstring"));

    CPatriotnodeBroadcast mnb;

    if (!DecodeHexMnb(mnb, params[0].get_str()))
        throw JSONRPCError(RPC_DESERIALIZATION_ERROR, "Patriotnode broadcast message decode failed");

    if(!mnb.VerifySignature())
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Patriotnode broadcast signature verification failed");

    UniValue resultObj(UniValue::VOBJ);

    resultObj.push_back(Pair("vin", mnb.vin.prevout.ToString()));
    resultObj.push_back(Pair("addr", mnb.addr.ToString()));
    resultObj.push_back(Pair("pubkeycollateral", CBitcoinAddress(mnb.pubKeyCollateralAddress.GetID()).ToString()));
    resultObj.push_back(Pair("pubkeypatriotnode", CBitcoinAddress(mnb.pubKeyPatriotnode.GetID()).ToString()));
    resultObj.push_back(Pair("vchsig", EncodeBase64(&mnb.sig[0], mnb.sig.size())));
    resultObj.push_back(Pair("sigtime", mnb.sigTime));
    resultObj.push_back(Pair("protocolversion", mnb.protocolVersion));
    resultObj.push_back(Pair("nlastdsq", mnb.nLastDsq));

    UniValue lastPingObj(UniValue::VOBJ);
    lastPingObj.push_back(Pair("vin", mnb.lastPing.vin.prevout.ToString()));
    lastPingObj.push_back(Pair("blockhash", mnb.lastPing.blockHash.ToString()));
    lastPingObj.push_back(Pair("sigtime", mnb.lastPing.sigTime));
    lastPingObj.push_back(Pair("vchsig", EncodeBase64(&mnb.lastPing.vchSig[0], mnb.lastPing.vchSig.size())));

    resultObj.push_back(Pair("lastping", lastPingObj));

    return resultObj;
}

UniValue relaypatriotnodebroadcast(const UniValue& params, bool fHelp)
{
    if (fHelp || params.size() != 1)
        throw runtime_error(
            "relaypatriotnodebroadcast \"hexstring\"\n"
            "\nCommand to relay patriotnode broadcast messages\n"

            "\nArguments:\n"
            "1. \"hexstring\"        (string) The hex encoded patriotnode broadcast message\n"

            "\nExamples:\n" +
            HelpExampleCli("relaypatriotnodebroadcast", "hexstring") + HelpExampleRpc("relaypatriotnodebroadcast", "hexstring"));


    CPatriotnodeBroadcast mnb;

    if (!DecodeHexMnb(mnb, params[0].get_str()))
        throw JSONRPCError(RPC_DESERIALIZATION_ERROR, "Patriotnode broadcast message decode failed");

    if(!mnb.VerifySignature())
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Patriotnode broadcast signature verification failed");

    mnodeman.UpdatePatriotnodeList(mnb);
    mnb.Relay();

    return strprintf("Patriotnode broadcast sent (service %s, vin %s)", mnb.addr.ToString(), mnb.vin.ToString());
}

