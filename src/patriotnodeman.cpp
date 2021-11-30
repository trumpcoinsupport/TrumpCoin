// Copyright (c) 2014-2015 The Dash developers
// Copyright (c) 2015-2020 The TrumpCoin developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "patriotnodeman.h"

#include "addrman.h"
#include "evo/deterministicmns.h"
#include "fs.h"
#include "patriotnode-payments.h"
#include "patriotnode-sync.h"
#include "patriotnode.h"
#include "messagesigner.h"
#include "netbase.h"
#include "netmessagemaker.h"
#include "net_processing.h"
#include "spork.h"

#include <boost/thread/thread.hpp>

#define PN_WINNER_MINIMUM_AGE 8000    // Age in seconds. This should be > PATRIOTNODE_REMOVAL_SECONDS to avoid misconfigured new nodes in the list.

/** Patriotnode manager */
CPatriotnodeMan mnodeman;
/** Keep track of the active Patriotnode */
CActivePatriotnode activePatriotnode;

struct CompareScorePN {
    template <typename T>
    bool operator()(const std::pair<int64_t, T>& t1,
        const std::pair<int64_t, T>& t2) const
    {
        return t1.first < t2.first;
    }
};

//
// CPatriotnodeDB
//

static const int PATRIOTNODE_DB_VERSION = 1;
static const int PATRIOTNODE_DB_VERSION_BIP155 = 2;

CPatriotnodeDB::CPatriotnodeDB()
{
    pathPN = GetDataDir() / "mncache.dat";
    strMagicMessage = "PatriotnodeCache";
}

bool CPatriotnodeDB::Write(const CPatriotnodeMan& mnodemanToSave)
{
    int64_t nStart = GetTimeMillis();
    const auto& params = Params();

    // serialize, checksum data up to that point, then append checksum
    // Always done in the latest format.
    CDataStream ssPatriotnodes(SER_DISK, CLIENT_VERSION | ADDRV2_FORMAT);
    ssPatriotnodes << PATRIOTNODE_DB_VERSION_BIP155;
    ssPatriotnodes << strMagicMessage;                   // patriotnode cache file specific magic message
    ssPatriotnodes << params.MessageStart(); // network specific magic number
    ssPatriotnodes << mnodemanToSave;
    uint256 hash = Hash(ssPatriotnodes.begin(), ssPatriotnodes.end());
    ssPatriotnodes << hash;

    // open output file, and associate with CAutoFile
    FILE* file = fsbridge::fopen(pathPN, "wb");
    CAutoFile fileout(file, SER_DISK, CLIENT_VERSION);
    if (fileout.IsNull())
        return error("%s : Failed to open file %s", __func__, pathPN.string());

    // Write and commit header, data
    try {
        fileout << ssPatriotnodes;
    } catch (const std::exception& e) {
        return error("%s : Serialize or I/O error - %s", __func__, e.what());
    }
    //    FileCommit(fileout);
    fileout.fclose();

    LogPrint(BCLog::PATRIOTNODE,"Written info to mncache.dat  %dms\n", GetTimeMillis() - nStart);
    LogPrint(BCLog::PATRIOTNODE,"  %s\n", mnodemanToSave.ToString());

    return true;
}

CPatriotnodeDB::ReadResult CPatriotnodeDB::Read(CPatriotnodeMan& mnodemanToLoad)
{
    int64_t nStart = GetTimeMillis();
    // open input file, and associate with CAutoFile
    FILE* file = fsbridge::fopen(pathPN, "rb");
    CAutoFile filein(file, SER_DISK, CLIENT_VERSION);
    if (filein.IsNull()) {
        error("%s : Failed to open file %s", __func__, pathPN.string());
        return FileError;
    }

    // use file size to size memory buffer
    int fileSize = fs::file_size(pathPN);
    int dataSize = fileSize - sizeof(uint256);
    // Don't try to resize to a negative number if file is small
    if (dataSize < 0)
        dataSize = 0;
    std::vector<unsigned char> vchData;
    vchData.resize(dataSize);
    uint256 hashIn;

    // read data and checksum from file
    try {
        filein.read((char*)vchData.data(), dataSize);
        filein >> hashIn;
    } catch (const std::exception& e) {
        error("%s : Deserialize or I/O error - %s", __func__, e.what());
        return HashReadError;
    }
    filein.fclose();

    const auto& params = Params();
    // serialize, checksum data up to that point, then append checksum
    CDataStream ssPatriotnodes(vchData, SER_DISK,  CLIENT_VERSION);

    // verify stored checksum matches input data
    uint256 hashTmp = Hash(ssPatriotnodes.begin(), ssPatriotnodes.end());
    if (hashIn != hashTmp) {
        error("%s : Checksum mismatch, data corrupted", __func__);
        return IncorrectHash;
    }

    int version;
    std::string strMagicMessageTmp;
    try {
        // de-serialize file header
        ssPatriotnodes >> version;
        ssPatriotnodes >> strMagicMessageTmp;

        // ... verify the message matches predefined one
        if (strMagicMessage != strMagicMessageTmp) {
            error("%s : Invalid patriotnode cache magic message", __func__);
            return IncorrectMagicMessage;
        }

        // de-serialize file header (network specific magic number) and ..
        std::vector<unsigned char> pchMsgTmp(4);
        ssPatriotnodes >> MakeSpan(pchMsgTmp);

        // ... verify the network matches ours
        if (memcmp(pchMsgTmp.data(), params.MessageStart(), pchMsgTmp.size()) != 0) {
            error("%s : Invalid network magic number", __func__);
            return IncorrectMagicNumber;
        }
        // de-serialize data into CPatriotnodeMan object.
        if (version == PATRIOTNODE_DB_VERSION_BIP155) {
            OverrideStream<CDataStream> s(&ssPatriotnodes, ssPatriotnodes.GetType(), ssPatriotnodes.GetVersion() | ADDRV2_FORMAT);
            s >> mnodemanToLoad;
        } else {
            // Old format
            ssPatriotnodes >> mnodemanToLoad;
        }
    } catch (const std::exception& e) {
        mnodemanToLoad.Clear();
        error("%s : Deserialize or I/O error - %s", __func__, e.what());
        return IncorrectFormat;
    }

    LogPrint(BCLog::PATRIOTNODE,"Loaded info from mncache.dat (dbversion=%d) %dms\n", version, GetTimeMillis() - nStart);
    LogPrint(BCLog::PATRIOTNODE,"  %s\n", mnodemanToLoad.ToString());

    return Ok;
}

void DumpPatriotnodes()
{
    int64_t nStart = GetTimeMillis();

    CPatriotnodeDB mndb;
    LogPrint(BCLog::PATRIOTNODE,"Writing info to mncache.dat...\n");
    mndb.Write(mnodeman);

    LogPrint(BCLog::PATRIOTNODE,"Patriotnode dump finished  %dms\n", GetTimeMillis() - nStart);
}

CPatriotnodeMan::CPatriotnodeMan():
        cvLastBlockHashes(CACHED_BLOCK_HASHES, UINT256_ZERO),
        nDsqCount(0)
{}

bool CPatriotnodeMan::Add(CPatriotnode& mn)
{
    // Skip after legacy obsolete. !TODO: remove when transition to DPN is complete
    if (deterministicPNManager->LegacyPNObsolete()) {
        return false;
    }

    if (deterministicPNManager->GetListAtChainTip().HasPNByCollateral(mn.vin.prevout)) {
        LogPrint(BCLog::PATRIOTNODE, "ERROR: Not Adding Patriotnode %s as the collateral is already registered with a DPN\n",
                mn.vin.prevout.ToString());
        return false;
    }

    LOCK(cs);

    if (!mn.IsAvailableState())
        return false;

    const auto& it = mapPatriotnodes.find(mn.vin.prevout);
    if (it == mapPatriotnodes.end()) {
        LogPrint(BCLog::PATRIOTNODE, "Adding new Patriotnode %s\n", mn.vin.prevout.ToString());
        mapPatriotnodes.emplace(mn.vin.prevout, std::make_shared<CPatriotnode>(mn));
        LogPrint(BCLog::PATRIOTNODE, "Patriotnode added. New total count: %d\n", mapPatriotnodes.size());
        return true;
    }

    return false;
}

void CPatriotnodeMan::AskForPN(CNode* pnode, const CTxIn& vin)
{
    // Skip after legacy obsolete. !TODO: remove when transition to DPN is complete
    if (deterministicPNManager->LegacyPNObsolete()) {
        return;
    }

    std::map<COutPoint, int64_t>::iterator i = mWeAskedForPatriotnodeListEntry.find(vin.prevout);
    if (i != mWeAskedForPatriotnodeListEntry.end()) {
        int64_t t = (*i).second;
        if (GetTime() < t) return; // we've asked recently
    }

    // ask for the mnb info once from the node that sent mnp

    LogPrint(BCLog::PATRIOTNODE, "CPatriotnodeMan::AskForPN - Asking node for missing entry, vin: %s\n", vin.prevout.hash.ToString());
    g_connman->PushMessage(pnode, CNetMsgMaker(pnode->GetSendVersion()).Make(NetMsgType::GETPNLIST, vin));
    int64_t askAgain = GetTime() + PatriotnodeMinPingSeconds();
    mWeAskedForPatriotnodeListEntry[vin.prevout] = askAgain;
}

int CPatriotnodeMan::CheckAndRemove(bool forceExpiredRemoval)
{
    // Skip after legacy obsolete. !TODO: remove when transition to DPN is complete
    if (deterministicPNManager->LegacyPNObsolete()) {
        LogPrint(BCLog::PATRIOTNODE, "Removing all legacy mn due to SPORK 21\n");
        Clear();
        return 0;
    }

    // !TODO: can be removed after enforcement
    bool reject_v0 = Params().GetConsensus().NetworkUpgradeActive(GetBestHeight(), Consensus::UPGRADE_V5_3);

    LOCK(cs);

    //remove inactive and outdated (or replaced by DPN)
    auto it = mapPatriotnodes.begin();
    while (it != mapPatriotnodes.end()) {
        PatriotnodeRef& mn = it->second;
        auto activeState = mn->GetActiveState();
        if (activeState == CPatriotnode::PATRIOTNODE_REMOVE ||
            activeState == CPatriotnode::PATRIOTNODE_VIN_SPENT ||
            (forceExpiredRemoval && activeState == CPatriotnode::PATRIOTNODE_EXPIRED) ||
            mn->protocolVersion < ActiveProtocol() ||
            (reject_v0 && mn->nMessVersion != MessageVersion::MESS_VER_HASH)) {
            LogPrint(BCLog::PATRIOTNODE, "Removing inactive (legacy) Patriotnode %s\n", it->first.ToString());
            //erase all of the broadcasts we've seen from this vin
            // -- if we missed a few pings and the node was removed, this will allow is to get it back without them
            //    sending a brand new mnb
            std::map<uint256, CPatriotnodeBroadcast>::iterator it3 = mapSeenPatriotnodeBroadcast.begin();
            while (it3 != mapSeenPatriotnodeBroadcast.end()) {
                if (it3->second.vin == it->second->vin) {
                    patriotnodeSync.mapSeenSyncPNB.erase((*it3).first);
                    it3 = mapSeenPatriotnodeBroadcast.erase(it3);
                } else {
                    ++it3;
                }
            }

            // allow us to ask for this patriotnode again if we see another ping
            std::map<COutPoint, int64_t>::iterator it2 = mWeAskedForPatriotnodeListEntry.begin();
            while (it2 != mWeAskedForPatriotnodeListEntry.end()) {
                if (it2->first == it->first) {
                    it2 = mWeAskedForPatriotnodeListEntry.erase(it2);
                } else {
                    ++it2;
                }
            }

            it = mapPatriotnodes.erase(it);
            LogPrint(BCLog::PATRIOTNODE, "Patriotnode removed.\n");
        } else {
            ++it;
        }
    }
    LogPrint(BCLog::PATRIOTNODE, "New total patriotnode count: %d\n", mapPatriotnodes.size());

    // check who's asked for the Patriotnode list
    std::map<CNetAddr, int64_t>::iterator it1 = mAskedUsForPatriotnodeList.begin();
    while (it1 != mAskedUsForPatriotnodeList.end()) {
        if ((*it1).second < GetTime()) {
            it1 = mAskedUsForPatriotnodeList.erase(it1);
        } else {
            ++it1;
        }
    }

    // check who we asked for the Patriotnode list
    it1 = mWeAskedForPatriotnodeList.begin();
    while (it1 != mWeAskedForPatriotnodeList.end()) {
        if ((*it1).second < GetTime()) {
            it1 = mWeAskedForPatriotnodeList.erase(it1);
        } else {
            ++it1;
        }
    }

    // check which Patriotnodes we've asked for
    std::map<COutPoint, int64_t>::iterator it2 = mWeAskedForPatriotnodeListEntry.begin();
    while (it2 != mWeAskedForPatriotnodeListEntry.end()) {
        if ((*it2).second < GetTime()) {
            it2 = mWeAskedForPatriotnodeListEntry.erase(it2);
        } else {
            ++it2;
        }
    }

    // remove expired mapSeenPatriotnodeBroadcast
    std::map<uint256, CPatriotnodeBroadcast>::iterator it3 = mapSeenPatriotnodeBroadcast.begin();
    while (it3 != mapSeenPatriotnodeBroadcast.end()) {
        if ((*it3).second.lastPing.sigTime < GetTime() - (PatriotnodeRemovalSeconds() * 2)) {
            patriotnodeSync.mapSeenSyncPNB.erase((*it3).second.GetHash());
            it3 = mapSeenPatriotnodeBroadcast.erase(it3);
        } else {
            ++it3;
        }
    }

    // remove expired mapSeenPatriotnodePing
    std::map<uint256, CPatriotnodePing>::iterator it4 = mapSeenPatriotnodePing.begin();
    while (it4 != mapSeenPatriotnodePing.end()) {
        if ((*it4).second.sigTime < GetTime() - (PatriotnodeRemovalSeconds() * 2)) {
            it4 = mapSeenPatriotnodePing.erase(it4);
        } else {
            ++it4;
        }
    }

    return mapPatriotnodes.size();
}

void CPatriotnodeMan::Clear()
{
    LOCK(cs);
    mapPatriotnodes.clear();
    mAskedUsForPatriotnodeList.clear();
    mWeAskedForPatriotnodeList.clear();
    mWeAskedForPatriotnodeListEntry.clear();
    mapSeenPatriotnodeBroadcast.clear();
    mapSeenPatriotnodePing.clear();
    nDsqCount = 0;
}

int CPatriotnodeMan::stable_size() const
{
    int nStable_size = 0;
    int nMinProtocol = ActiveProtocol();

    for (const auto& it : mapPatriotnodes) {
        const PatriotnodeRef& mn = it.second;
        if (mn->protocolVersion < nMinProtocol) {
            continue; // Skip obsolete versions
        }
        if (sporkManager.IsSporkActive (SPORK_8_PATRIOTNODE_PAYMENT_ENFORCEMENT)) {
            if (GetAdjustedTime() - mn->sigTime < PN_WINNER_MINIMUM_AGE) {
                continue; // Skip patriotnodes younger than (default) 8000 sec (MUST be > PATRIOTNODE_REMOVAL_SECONDS)
            }
        }

        if (!mn->IsEnabled ())
            continue; // Skip not-enabled patriotnodes

        nStable_size++;
    }

    return nStable_size;
}

int CPatriotnodeMan::CountEnabled(int protocolVersion) const
{
    int i = 0;
    protocolVersion = protocolVersion == -1 ? ActiveProtocol() : protocolVersion;

    for (const auto& it : mapPatriotnodes) {
        const PatriotnodeRef& mn = it.second;
        if (mn->protocolVersion < protocolVersion || !mn->IsEnabled()) continue;
        i++;
    }

    return i;
}

int CPatriotnodeMan::CountNetworks(int& ipv4, int& ipv6, int& onion) const
{
    LOCK(cs);
    for (const auto& it : mapPatriotnodes) {
        const PatriotnodeRef& mn = it.second;
        std::string strHost;
        int port;
        SplitHostPort(mn->addr.ToString(), port, strHost);
        CNetAddr node;
        LookupHost(strHost.c_str(), node, false);
        int nNetwork = node.GetNetwork();
        switch (nNetwork) {
            case 1 :
                ipv4++;
                break;
            case 2 :
                ipv6++;
                break;
            case 3 :
                onion++;
                break;
        }
    }
    return mapPatriotnodes.size();
}

bool CPatriotnodeMan::RequestMnList(CNode* pnode)
{
    // Skip after legacy obsolete. !TODO: remove when transition to DPN is complete
    if (deterministicPNManager->LegacyPNObsolete()) {
        return false;
    }

    LOCK(cs);
    if (Params().NetworkIDString() == CBaseChainParams::MAIN) {
        if (!(pnode->addr.IsRFC1918() || pnode->addr.IsLocal())) {
            std::map<CNetAddr, int64_t>::iterator it = mWeAskedForPatriotnodeList.find(pnode->addr);
            if (it != mWeAskedForPatriotnodeList.end()) {
                if (GetTime() < (*it).second) {
                    LogPrint(BCLog::PATRIOTNODE, "dseg - we already asked peer %i for the list; skipping...\n", pnode->GetId());
                    return false;
                }
            }
        }
    }

    g_connman->PushMessage(pnode, CNetMsgMaker(pnode->GetSendVersion()).Make(NetMsgType::GETPNLIST, CTxIn()));
    int64_t askAgain = GetTime() + PATRIOTNODES_REQUEST_SECONDS;
    mWeAskedForPatriotnodeList[pnode->addr] = askAgain;
    return true;
}

CPatriotnode* CPatriotnodeMan::Find(const COutPoint& collateralOut)
{
    LOCK(cs);
    auto it = mapPatriotnodes.find(collateralOut);
    return it != mapPatriotnodes.end() ? it->second.get() : nullptr;
}

const CPatriotnode* CPatriotnodeMan::Find(const COutPoint& collateralOut) const
{
    LOCK(cs);
    auto const& it = mapPatriotnodes.find(collateralOut);
    return it != mapPatriotnodes.end() ? it->second.get() : nullptr;
}

CPatriotnode* CPatriotnodeMan::Find(const CPubKey& pubKeyPatriotnode)
{
    LOCK(cs);

    for (auto& it : mapPatriotnodes) {
        PatriotnodeRef& mn = it.second;
        if (mn->pubKeyPatriotnode == pubKeyPatriotnode)
            return mn.get();
    }
    return nullptr;
}

void CPatriotnodeMan::CheckSpentCollaterals(const std::vector<CTransactionRef>& vtx)
{
    // Skip after legacy obsolete. !TODO: remove when transition to DPN is complete
    if (deterministicPNManager->LegacyPNObsolete()) {
        return;
    }

    LOCK(cs);
    for (const auto& tx : vtx) {
        for (const auto& in : tx->vin) {
            auto it = mapPatriotnodes.find(in.prevout);
            if (it != mapPatriotnodes.end()) {
                it->second->SetSpent();
            }
        }
    }
}

static bool canSchedulePN(bool fFilterSigTime, const PatriotnodeRef& mn, int minProtocol,
                          int nMnCount, int nBlockHeight)
{
    // check protocol version
    if (mn->protocolVersion < minProtocol) return false;

    // it's in the list (up to 8 entries ahead of current block to allow propagation) -- so let's skip it
    if (patriotnodePayments.IsScheduled(*mn, nBlockHeight)) return false;

    // it's too new, wait for a cycle
    if (fFilterSigTime && mn->sigTime + (nMnCount * 2.6 * 60) > GetAdjustedTime()) return false;

    // make sure it has as many confirmations as there are patriotnodes
    if (pcoinsTip->GetCoinDepthAtHeight(mn->vin.prevout, nBlockHeight) < nMnCount) return false;

    return true;
}

//
// Deterministically select the oldest/best patriotnode to pay on the network
//
PatriotnodeRef CPatriotnodeMan::GetNextPatriotnodeInQueueForPayment(int nBlockHeight, bool fFilterSigTime, int& nCount, const CBlockIndex* pChainTip) const
{
    // Skip after legacy obsolete. !TODO: remove when transition to DPN is complete
    if (deterministicPNManager->LegacyPNObsolete()) {
        LogPrintf("%s: ERROR - called after legacy system disabled\n", __func__);
        return nullptr;
    }

    AssertLockNotHeld(cs_main);
    const CBlockIndex* BlockReading = (pChainTip == nullptr ? GetChainTip() : pChainTip);
    if (!BlockReading) return nullptr;

    PatriotnodeRef pBestPatriotnode = nullptr;
    std::vector<std::pair<int64_t, PatriotnodeRef> > vecPatriotnodeLastPaid;

    CDeterministicPNList mnList;
    if (deterministicPNManager->IsDIP3Enforced()) {
        mnList = deterministicPNManager->GetListAtChainTip();
    }

    /*
        Make a vector with all of the last paid times
    */
    int minProtocol = ActiveProtocol();
    int nMnCount = mnList.GetValidPNsCount();
    {
        LOCK(cs);
        nMnCount += CountEnabled();
        for (const auto& it : mapPatriotnodes) {
            if (!it.second->IsEnabled()) continue;
            if (canSchedulePN(fFilterSigTime, it.second, minProtocol, nMnCount, nBlockHeight)) {
                vecPatriotnodeLastPaid.emplace_back(SecondsSincePayment(it.second, BlockReading), it.second);
            }
        }
    }
    // Add deterministic patriotnodes to the vector
    mnList.ForEachPN(true, [&](const CDeterministicPNCPtr& dmn) {
        const PatriotnodeRef mn = MakePatriotnodeRefForDPN(dmn);
        if (canSchedulePN(fFilterSigTime, mn, minProtocol, nMnCount, nBlockHeight)) {
            vecPatriotnodeLastPaid.emplace_back(SecondsSincePayment(mn, BlockReading), mn);
        }
    });

    nCount = (int)vecPatriotnodeLastPaid.size();

    //when the network is in the process of upgrading, don't penalize nodes that recently restarted
    if (fFilterSigTime && nCount < nMnCount / 3) return GetNextPatriotnodeInQueueForPayment(nBlockHeight, false, nCount, BlockReading);

    // Sort them high to low
    sort(vecPatriotnodeLastPaid.rbegin(), vecPatriotnodeLastPaid.rend(), CompareScorePN());

    // Look at 1/10 of the oldest nodes (by last payment), calculate their scores and pay the best one
    //  -- This doesn't look at who is being paid in the +8-10 blocks, allowing for double payments very rarely
    //  -- 1/100 payments should be a double payment on mainnet - (1/(3000/10))*2
    //  -- (chance per block * chances before IsScheduled will fire)
    int nTenthNetwork = nMnCount / 10;
    int nCountTenth = 0;
    arith_uint256 nHigh = ARITH_UINT256_ZERO;
    const uint256& hash = GetHashAtHeight(nBlockHeight - 101);
    for (const auto& s: vecPatriotnodeLastPaid) {
        const PatriotnodeRef pmn = s.second;
        if (!pmn) break;

        const arith_uint256& n = pmn->CalculateScore(hash);
        if (n > nHigh) {
            nHigh = n;
            pBestPatriotnode = pmn;
        }
        nCountTenth++;
        if (nCountTenth >= nTenthNetwork) break;
    }
    return pBestPatriotnode;
}

PatriotnodeRef CPatriotnodeMan::GetCurrentPatriotNode(const uint256& hash) const
{
    int minProtocol = ActiveProtocol();
    int64_t score = 0;
    PatriotnodeRef winner = nullptr;

    // scan for winner
    for (const auto& it : mapPatriotnodes) {
        const PatriotnodeRef& mn = it.second;
        if (mn->protocolVersion < minProtocol || !mn->IsEnabled()) continue;
        // calculate the score of the patriotnode
        const int64_t n = mn->CalculateScore(hash).GetCompact(false);
        // determine the winner
        if (n > score) {
            score = n;
            winner = mn;
        }
    }

    // scan also dmns
    if (deterministicPNManager->IsDIP3Enforced()) {
        auto mnList = deterministicPNManager->GetListAtChainTip();
        mnList.ForEachPN(true, [&](const CDeterministicPNCPtr& dmn) {
            const PatriotnodeRef mn = MakePatriotnodeRefForDPN(dmn);
            // calculate the score of the patriotnode
            const int64_t n = mn->CalculateScore(hash).GetCompact(false);
            // determine the winner
            if (n > score) {
                score = n;
                winner = mn;
            }
        });
    }

    return winner;
}

std::vector<std::pair<PatriotnodeRef, int>> CPatriotnodeMan::GetMnScores(int nLast) const
{
    std::vector<std::pair<PatriotnodeRef, int>> ret;
    int nChainHeight = GetBestHeight();
    if (nChainHeight < 0) return ret;

    for (int nHeight = nChainHeight - nLast; nHeight < nChainHeight + 20; nHeight++) {
        const uint256& hash = GetHashAtHeight(nHeight - 101);
        PatriotnodeRef winner = GetCurrentPatriotNode(hash);
        if (winner) {
            ret.emplace_back(winner, nHeight);
        }
    }
    return ret;
}

int CPatriotnodeMan::GetPatriotnodeRank(const CTxIn& vin, int64_t nBlockHeight) const
{
    const uint256& hash = GetHashAtHeight(nBlockHeight - 1);
    // height outside range
    if (hash == UINT256_ZERO) return -1;

    // scan for winner
    int minProtocol = ActiveProtocol();
    std::vector<std::pair<int64_t, CTxIn> > vecPatriotnodeScores;
    {
        LOCK(cs);
        for (const auto& it : mapPatriotnodes) {
            const PatriotnodeRef& mn = it.second;
            if (!mn->IsEnabled()) {
                continue; // Skip not enabled
            }
            if (mn->protocolVersion < minProtocol) {
                LogPrint(BCLog::PATRIOTNODE,"Skipping Patriotnode with obsolete version %d\n", mn->protocolVersion);
                continue; // Skip obsolete versions
            }
            if (sporkManager.IsSporkActive(SPORK_8_PATRIOTNODE_PAYMENT_ENFORCEMENT) &&
                    GetAdjustedTime() - mn->sigTime < PN_WINNER_MINIMUM_AGE) {
                continue; // Skip patriotnodes younger than (default) 1 hour
            }
            vecPatriotnodeScores.emplace_back(mn->CalculateScore(hash).GetCompact(false), mn->vin);
        }
    }

    // scan also dmns
    if (deterministicPNManager->IsDIP3Enforced()) {
        auto mnList = deterministicPNManager->GetListAtChainTip();
        mnList.ForEachPN(true, [&](const CDeterministicPNCPtr& dmn) {
            const PatriotnodeRef mn = MakePatriotnodeRefForDPN(dmn);
            vecPatriotnodeScores.emplace_back(mn->CalculateScore(hash).GetCompact(false), mn->vin);
        });
    }

    sort(vecPatriotnodeScores.rbegin(), vecPatriotnodeScores.rend(), CompareScorePN());

    int rank = 0;
    for (std::pair<int64_t, CTxIn> & s : vecPatriotnodeScores) {
        rank++;
        if (s.second.prevout == vin.prevout) {
            return rank;
        }
    }

    return -1;
}

std::vector<std::pair<int64_t, PatriotnodeRef>> CPatriotnodeMan::GetPatriotnodeRanks(int nBlockHeight) const
{
    std::vector<std::pair<int64_t, PatriotnodeRef>> vecPatriotnodeScores;
    const uint256& hash = GetHashAtHeight(nBlockHeight - 1);
    // height outside range
    if (hash == UINT256_ZERO) return vecPatriotnodeScores;
    {
        LOCK(cs);
        // scan for winner
        for (const auto& it : mapPatriotnodes) {
            const PatriotnodeRef mn = it.second;
            const uint32_t score = mn->IsEnabled() ? mn->CalculateScore(hash).GetCompact(false) : 9999;

            vecPatriotnodeScores.emplace_back(score, mn);
        }
    }
    // scan also dmns
    if (deterministicPNManager->IsDIP3Enforced()) {
        auto mnList = deterministicPNManager->GetListAtChainTip();
        mnList.ForEachPN(false, [&](const CDeterministicPNCPtr& dmn) {
            const PatriotnodeRef mn = MakePatriotnodeRefForDPN(dmn);
            const uint32_t score = mnList.IsPNValid(dmn) ? mn->CalculateScore(hash).GetCompact(false) : 9999;

            vecPatriotnodeScores.emplace_back(score, mn);
        });
    }
    sort(vecPatriotnodeScores.rbegin(), vecPatriotnodeScores.rend(), CompareScorePN());
    return vecPatriotnodeScores;
}

int CPatriotnodeMan::ProcessPNBroadcast(CNode* pfrom, CPatriotnodeBroadcast& mnb)
{
    const uint256& mnbHash = mnb.GetHash();
    if (mapSeenPatriotnodeBroadcast.count(mnbHash)) { //seen
        patriotnodeSync.AddedPatriotnodeList(mnbHash);
        return 0;
    }

    int chainHeight = GetBestHeight();
    const auto& consensus = Params().GetConsensus();
    // Check if mnb contains a ADDRv2 and reject it if the new NU wasn't enforced.
    if (!mnb.addr.IsAddrV1Compatible() &&
        !consensus.NetworkUpgradeActive(chainHeight, Consensus::UPGRADE_V5_3)) {
        LogPrint(BCLog::PATRIOTNODE, "mnb - received a ADDRv2 before enforcement\n");
        return 33;
    }

    int nDoS = 0;
    if (!mnb.CheckAndUpdate(nDoS, GetBestHeight())) {
        return nDoS;
    }

    // make sure the vout that was signed is related to the transaction that spawned the Patriotnode
    //  - this is expensive, so it's only done once per Patriotnode
    if (!mnb.IsInputAssociatedWithPubkey()) {
        LogPrint(BCLog::PATRIOTNODE, "%s : mnb - Got mismatched pubkey and vin\n", __func__);
        return 33;
    }

    // now that did the basic mnb checks, can add it.
    mapSeenPatriotnodeBroadcast.emplace(mnbHash, mnb);

    // make sure it's still unspent
    //  - this is checked later by .check() in many places and by ThreadCheckObfuScationPool()
    if (mnb.CheckInputsAndAdd(chainHeight, nDoS)) {
        // use this as a peer
        g_connman->AddNewAddress(CAddress(mnb.addr, NODE_NETWORK), pfrom->addr, 2 * 60 * 60);
        patriotnodeSync.AddedPatriotnodeList(mnbHash);
    } else {
        LogPrint(BCLog::PATRIOTNODE,"mnb - Rejected Patriotnode entry %s\n", mnb.vin.prevout.hash.ToString());
        return nDoS;
    }
    // All good
    return 0;
}

int CPatriotnodeMan::ProcessPNPing(CNode* pfrom, CPatriotnodePing& mnp)
{
    const uint256& mnpHash = mnp.GetHash();
    if (mapSeenPatriotnodePing.count(mnpHash)) return 0; //seen

    int nDoS = 0;
    if (mnp.CheckAndUpdate(nDoS, GetBestHeight())) return 0;

    if (nDoS > 0) {
        // if anything significant failed, mark that node
        return nDoS;
    } else {
        // if nothing significant failed, search existing Patriotnode list
        CPatriotnode* pmn = Find(mnp.vin.prevout);
        // if it's known, don't ask for the mnb, just return
        if (pmn != NULL) return 0;
    }

    // something significant is broken or mn is unknown,
    // we might have to ask for the mn entry (while we aren't syncing).
    if (patriotnodeSync.IsSynced()) {
        AskForPN(pfrom, mnp.vin);
    }

    // All good
    return 0;
}

void CPatriotnodeMan::BroadcastInvPN(CPatriotnode* mn, CNode* pfrom)
{
    CPatriotnodeBroadcast mnb = CPatriotnodeBroadcast(*mn);
    const uint256& hash = mnb.GetHash();
    pfrom->PushInventory(CInv(MSG_PATRIOTNODE_ANNOUNCE, hash));

    // Add to mapSeenPatriotnodeBroadcast in case that isn't there for some reason.
    if (!mapSeenPatriotnodeBroadcast.count(hash)) mapSeenPatriotnodeBroadcast.emplace(hash, mnb);
}

int CPatriotnodeMan::ProcessGetPNList(CNode* pfrom, CTxIn& vin)
{
    // Single PN request
    if (!vin.IsNull()) {
        CPatriotnode* mn = Find(vin.prevout);
        if (!mn || !mn->IsEnabled()) return 0; // Nothing to return.

        // Relay the PN.
        BroadcastInvPN(mn, pfrom);
        LogPrint(BCLog::PATRIOTNODE, "dseg - Sent 1 Patriotnode entry to peer %i\n", pfrom->GetId());
        return 0;
    }

    // Check if the node asked for mn list sync before.
    bool isLocal = (pfrom->addr.IsRFC1918() || pfrom->addr.IsLocal());
    if (!isLocal) {
        auto itAskedUsPNList = mAskedUsForPatriotnodeList.find(pfrom->addr);
        if (itAskedUsPNList != mAskedUsForPatriotnodeList.end()) {
            int64_t t = (*itAskedUsPNList).second;
            if (GetTime() < t) {
                LogPrintf("CPatriotnodeMan::ProcessMessage() : dseg - peer already asked me for the list\n");
                return 20;
            }
        }
        int64_t askAgain = GetTime() + PATRIOTNODES_REQUEST_SECONDS;
        mAskedUsForPatriotnodeList[pfrom->addr] = askAgain;
    }

    int nInvCount = 0;
    {
        LOCK(cs);
        for (auto& it : mapPatriotnodes) {
            PatriotnodeRef& mn = it.second;
            if (mn->addr.IsRFC1918()) continue; //local network
            if (mn->IsEnabled()) {
                LogPrint(BCLog::PATRIOTNODE, "dseg - Sending Patriotnode entry - %s \n", mn->vin.prevout.hash.ToString());
                BroadcastInvPN(mn.get(), pfrom);
                nInvCount++;
            }
        }
    }

    g_connman->PushMessage(pfrom, CNetMsgMaker(pfrom->GetSendVersion()).Make(NetMsgType::SYNCSTATUSCOUNT, PATRIOTNODE_SYNC_LIST, nInvCount));
    LogPrint(BCLog::PATRIOTNODE, "dseg - Sent %d Patriotnode entries to peer %i\n", nInvCount, pfrom->GetId());

    // All good
    return 0;
}

void CPatriotnodeMan::ProcessMessage(CNode* pfrom, std::string& strCommand, CDataStream& vRecv)
{
    int banScore = ProcessMessageInner(pfrom, strCommand, vRecv);
    if (banScore > 0) {
        LOCK(cs_main);
        Misbehaving(pfrom->GetId(), banScore);
    }
}

int CPatriotnodeMan::ProcessMessageInner(CNode* pfrom, std::string& strCommand, CDataStream& vRecv)
{
    if (fLiteMode) return 0; //disable all Patriotnode related functionality
    if (!patriotnodeSync.IsBlockchainSynced()) return 0;

    // Skip after legacy obsolete. !TODO: remove when transition to DPN is complete
    if (deterministicPNManager->LegacyPNObsolete()) {
        LogPrint(BCLog::PATRIOTNODE, "%s: skip obsolete message %s\n", __func__, strCommand);
        return 0;
    }

    LOCK(cs_process_message);

    if (strCommand == NetMsgType::PNBROADCAST) {
        CPatriotnodeBroadcast mnb;
        vRecv >> mnb;
        {
            // Clear inv request
            LOCK(cs_main);
            g_connman->RemoveAskFor(mnb.GetHash(), MSG_PATRIOTNODE_ANNOUNCE);
        }
        return ProcessPNBroadcast(pfrom, mnb);

    } else if (strCommand == NetMsgType::PNBROADCAST2) {
        if (!Params().GetConsensus().NetworkUpgradeActive(GetBestHeight(), Consensus::UPGRADE_V5_3)) {
            LogPrint(BCLog::PATRIOTNODE, "%s: mnb2 not enabled pre-V5.3 enforcement\n", __func__);
            return 30;
        }
        CPatriotnodeBroadcast mnb;
        OverrideStream<CDataStream> s(&vRecv, vRecv.GetType(), vRecv.GetVersion() | ADDRV2_FORMAT);
        s >> mnb;
        {
            // Clear inv request
            LOCK(cs_main);
            g_connman->RemoveAskFor(mnb.GetHash(), MSG_PATRIOTNODE_ANNOUNCE);
        }

        // For now, let's not process mnb2 with pre-BIP155 node addr format.
        if (mnb.addr.IsAddrV1Compatible()) {
            LogPrint(BCLog::PATRIOTNODE, "%s: mnb2 with pre-BIP155 node addr format rejected\n", __func__);
            return 30;
        }

        return ProcessPNBroadcast(pfrom, mnb);

    } else if (strCommand == NetMsgType::PNPING) {
        //Patriotnode Ping
        CPatriotnodePing mnp;
        vRecv >> mnp;
        LogPrint(BCLog::PNPING, "mnp - Patriotnode ping, vin: %s\n", mnp.vin.prevout.hash.ToString());
        {
            // Clear inv request
            LOCK(cs_main);
            g_connman->RemoveAskFor(mnp.GetHash(), MSG_PATRIOTNODE_PING);
        }
        return ProcessPNPing(pfrom, mnp);

    } else if (strCommand == NetMsgType::GETPNLIST) {
        //Get Patriotnode list or specific entry
        CTxIn vin;
        vRecv >> vin;
        return ProcessGetPNList(pfrom, vin);
    }
    // Nothing to report
    return 0;
}

void CPatriotnodeMan::Remove(const COutPoint& collateralOut)
{
    LOCK(cs);
    const auto it = mapPatriotnodes.find(collateralOut);
    if (it != mapPatriotnodes.end()) {
        mapPatriotnodes.erase(it);
    }
}

void CPatriotnodeMan::UpdatePatriotnodeList(CPatriotnodeBroadcast& mnb)
{
    // Skip after legacy obsolete. !TODO: remove when transition to DPN is complete
    if (deterministicPNManager->LegacyPNObsolete()) {
        LogPrint(BCLog::PATRIOTNODE, "Removing all legacy mn due to SPORK 21\n");
        return;
    }

    mapSeenPatriotnodePing.emplace(mnb.lastPing.GetHash(), mnb.lastPing);
    mapSeenPatriotnodeBroadcast.emplace(mnb.GetHash(), mnb);
    patriotnodeSync.AddedPatriotnodeList(mnb.GetHash());

    LogPrint(BCLog::PATRIOTNODE,"%s -- patriotnode=%s\n", __func__, mnb.vin.prevout.ToString());

    CPatriotnode* pmn = Find(mnb.vin.prevout);
    if (pmn == NULL) {
        CPatriotnode mn(mnb);
        Add(mn);
    } else {
        pmn->UpdateFromNewBroadcast(mnb, GetBestHeight());
    }
}

int64_t CPatriotnodeMan::SecondsSincePayment(const PatriotnodeRef& mn, const CBlockIndex* BlockReading) const
{
    int64_t sec = (GetAdjustedTime() - GetLastPaid(mn, BlockReading));
    int64_t month = 60 * 60 * 24 * 30;
    if (sec < month) return sec; //if it's less than 30 days, give seconds

    CHashWriter ss(SER_GETHASH, PROTOCOL_VERSION);
    ss << mn->vin;
    ss << mn->sigTime;
    const arith_uint256& hash = UintToArith256(ss.GetHash());

    // return some deterministic value for unknown/unpaid but force it to be more than 30 days old
    return month + hash.GetCompact(false);
}

int64_t CPatriotnodeMan::GetLastPaid(const PatriotnodeRef& mn, const CBlockIndex* BlockReading) const
{
    if (BlockReading == nullptr) return false;

    const CScript& mnpayee = mn->GetPayeeScript();

    CHashWriter ss(SER_GETHASH, PROTOCOL_VERSION);
    ss << mn->vin;
    ss << mn->sigTime;
    uint256 hash = ss.GetHash();

    // use a deterministic offset to break a tie -- 2.5 minutes
    int64_t nOffset = UintToArith256(hash).GetCompact(false) % 150;

    int nMnCount = CountEnabled() * 1.25;
    for (int n = 0; n < nMnCount; n++) {
        const auto& it = patriotnodePayments.mapPatriotnodeBlocks.find(BlockReading->nHeight);
        if (it != patriotnodePayments.mapPatriotnodeBlocks.end()) {
            // Search for this payee, with at least 2 votes. This will aid in consensus
            // allowing the network to converge on the same payees quickly, then keep the same schedule.
            if (it->second.HasPayeeWithVotes(mnpayee, 2))
                return BlockReading->nTime + nOffset;
        }
        BlockReading = BlockReading->pprev;

        if (BlockReading == nullptr || BlockReading->nHeight <= 0) {
            break;
        }
    }

    return 0;
}

std::string CPatriotnodeMan::ToString() const
{
    std::ostringstream info;
    info << "Patriotnodes: " << (int)mapPatriotnodes.size()
         << ", peers who asked us for Patriotnode list: " << (int)mAskedUsForPatriotnodeList.size()
         << ", peers we asked for Patriotnode list: " << (int)mWeAskedForPatriotnodeList.size()
         << ", entries in Patriotnode list we asked for: " << (int)mWeAskedForPatriotnodeListEntry.size();
    return info.str();
}

void CPatriotnodeMan::CacheBlockHash(const CBlockIndex* pindex)
{
    cvLastBlockHashes.Set(pindex->nHeight, pindex->GetBlockHash());
}

void CPatriotnodeMan::UncacheBlockHash(const CBlockIndex* pindex)
{
    cvLastBlockHashes.Set(pindex->nHeight, UINT256_ZERO);
}

uint256 CPatriotnodeMan::GetHashAtHeight(int nHeight) const
{
    // return zero if outside bounds
    if (nHeight < 0) {
        LogPrint(BCLog::PATRIOTNODE, "%s: Negative height. Returning 0\n",  __func__);
        return UINT256_ZERO;
    }
    int nCurrentHeight = GetBestHeight();
    if (nHeight > nCurrentHeight) {
        LogPrint(BCLog::PATRIOTNODE, "%s: height %d over current height %d. Returning 0\n",
                __func__, nHeight, nCurrentHeight);
        return UINT256_ZERO;
    }

    if (nHeight > nCurrentHeight - (int) CACHED_BLOCK_HASHES) {
        // Use cached hash
        return cvLastBlockHashes.Get(nHeight);
    } else {
        // Use chainActive
        LOCK(cs_main);
        return chainActive[nHeight]->GetBlockHash();
    }
}

bool CPatriotnodeMan::IsWithinDepth(const uint256& nHash, int depth) const
{
    // Sanity checks
    if (nHash.IsNull()) {
        return error("%s: Called with null hash\n", __func__);
    }
    if (depth < 0 || (unsigned) depth >= CACHED_BLOCK_HASHES) {
        return error("%s: Invalid depth %d. Cached block hashes: %d\n", __func__, depth, CACHED_BLOCK_HASHES);
    }
    // Check last depth blocks to find one with matching hash
    const int nCurrentHeight = GetBestHeight();
    int nStopHeight = std::max(0, nCurrentHeight - depth);
    for (int i = nCurrentHeight; i >= nStopHeight; i--) {
        if (GetHashAtHeight(i) == nHash)
            return true;
    }
    return false;
}

void ThreadCheckPatriotnodes()
{
    if (fLiteMode) return; //disable all Patriotnode related functionality

    // Make this thread recognisable as the wallet flushing thread
    util::ThreadRename("trumpcoin-patriotnodeman");
    LogPrintf("Patriotnodes thread started\n");

    unsigned int c = 0;

    try {
        // first clean up stale patriotnode payments data
        patriotnodePayments.CleanPaymentList(mnodeman.CheckAndRemove(), mnodeman.GetBestHeight());

        // Startup-only, clean any stored seen PN broadcast with an invalid service that
        // could have been invalidly stored on a previous release
        auto itSeenPNB = mnodeman.mapSeenPatriotnodeBroadcast.begin();
        while (itSeenPNB != mnodeman.mapSeenPatriotnodeBroadcast.end()) {
            if (!itSeenPNB->second.addr.IsValid()) {
                itSeenPNB = mnodeman.mapSeenPatriotnodeBroadcast.erase(itSeenPNB);
            } else {
                itSeenPNB++;
            }
        }

        while (true) {

            if (ShutdownRequested()) {
                break;
            }

            MilliSleep(1000);
            boost::this_thread::interruption_point();

            // try to sync from all available nodes, one step at a time
            patriotnodeSync.Process();

            if (patriotnodeSync.IsBlockchainSynced()) {
                c++;

                // check if we should activate or ping every few minutes,
                // start right after sync is considered to be done
                if (c % (PatriotnodePingSeconds()/2) == 0)
                    activePatriotnode.ManageStatus();

                if (c % (PatriotnodePingSeconds()/5) == 0) {
                    patriotnodePayments.CleanPaymentList(mnodeman.CheckAndRemove(), mnodeman.GetBestHeight());
                }
            }
        }
    } catch (boost::thread_interrupted&) {
        // nothing, thread interrupted.
    }
}
