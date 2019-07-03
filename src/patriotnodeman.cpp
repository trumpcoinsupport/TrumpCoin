// Copyright (c) 2014-2015 The Dash developers
// Copyright (c) 2015-2018 The PIVX developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "patriotnodeman.h"
#include "activepatriotnode.h"
#include "addrman.h"
#include "patriotnode.h"
#include "obfuscation.h"
#include "spork.h"
#include "util.h"
#include <boost/filesystem.hpp>
#include <boost/lexical_cast.hpp>

#define MN_WINNER_MINIMUM_AGE 8000    // Age in seconds. This should be > PATRIOTNODE_REMOVAL_SECONDS to avoid misconfigured new nodes in the list.

/** Patriotnode manager */
CPatriotnodeMan mnodeman;

struct CompareLastPaid {
    bool operator()(const pair<int64_t, CTxIn>& t1,
        const pair<int64_t, CTxIn>& t2) const
    {
        return t1.first < t2.first;
    }
};

struct CompareScoreTxIn {
    bool operator()(const pair<int64_t, CTxIn>& t1,
        const pair<int64_t, CTxIn>& t2) const
    {
        return t1.first < t2.first;
    }
};

struct CompareScoreMN {
    bool operator()(const pair<int64_t, CPatriotnode>& t1,
        const pair<int64_t, CPatriotnode>& t2) const
    {
        return t1.first < t2.first;
    }
};

//
// CPatriotnodeDB
//

CPatriotnodeDB::CPatriotnodeDB()
{
    pathMN = GetDataDir() / "mncache.dat";
    strMagicMessage = "PatriotnodeCache";
}

bool CPatriotnodeDB::Write(const CPatriotnodeMan& mnodemanToSave)
{
    int64_t nStart = GetTimeMillis();

    // serialize, checksum data up to that point, then append checksum
    CDataStream ssPatriotnodes(SER_DISK, CLIENT_VERSION);
    ssPatriotnodes << strMagicMessage;                   // patriotnode cache file specific magic message
    ssPatriotnodes << FLATDATA(Params().MessageStart()); // network specific magic number
    ssPatriotnodes << mnodemanToSave;
    uint256 hash = Hash(ssPatriotnodes.begin(), ssPatriotnodes.end());
    ssPatriotnodes << hash;

    // open output file, and associate with CAutoFile
    FILE* file = fopen(pathMN.string().c_str(), "wb");
    CAutoFile fileout(file, SER_DISK, CLIENT_VERSION);
    if (fileout.IsNull())
        return error("%s : Failed to open file %s", __func__, pathMN.string());

    // Write and commit header, data
    try {
        fileout << ssPatriotnodes;
    } catch (std::exception& e) {
        return error("%s : Serialize or I/O error - %s", __func__, e.what());
    }
    //    FileCommit(fileout);
    fileout.fclose();

    LogPrint("patriotnode","Written info to mncache.dat  %dms\n", GetTimeMillis() - nStart);
    LogPrint("patriotnode","  %s\n", mnodemanToSave.ToString());

    return true;
}

CPatriotnodeDB::ReadResult CPatriotnodeDB::Read(CPatriotnodeMan& mnodemanToLoad, bool fDryRun)
{
    int64_t nStart = GetTimeMillis();
    // open input file, and associate with CAutoFile
    FILE* file = fopen(pathMN.string().c_str(), "rb");
    CAutoFile filein(file, SER_DISK, CLIENT_VERSION);
    if (filein.IsNull()) {
        error("%s : Failed to open file %s", __func__, pathMN.string());
        return FileError;
    }

    // use file size to size memory buffer
    int fileSize = boost::filesystem::file_size(pathMN);
    int dataSize = fileSize - sizeof(uint256);
    // Don't try to resize to a negative number if file is small
    if (dataSize < 0)
        dataSize = 0;
    vector<unsigned char> vchData;
    vchData.resize(dataSize);
    uint256 hashIn;

    // read data and checksum from file
    try {
        filein.read((char*)&vchData[0], dataSize);
        filein >> hashIn;
    } catch (std::exception& e) {
        error("%s : Deserialize or I/O error - %s", __func__, e.what());
        return HashReadError;
    }
    filein.fclose();

    CDataStream ssPatriotnodes(vchData, SER_DISK, CLIENT_VERSION);

    // verify stored checksum matches input data
    uint256 hashTmp = Hash(ssPatriotnodes.begin(), ssPatriotnodes.end());
    if (hashIn != hashTmp) {
        error("%s : Checksum mismatch, data corrupted", __func__);
        return IncorrectHash;
    }

    unsigned char pchMsgTmp[4];
    std::string strMagicMessageTmp;
    try {
        // de-serialize file header (patriotnode cache file specific magic message) and ..

        ssPatriotnodes >> strMagicMessageTmp;

        // ... verify the message matches predefined one
        if (strMagicMessage != strMagicMessageTmp) {
            error("%s : Invalid patriotnode cache magic message", __func__);
            return IncorrectMagicMessage;
        }

        // de-serialize file header (network specific magic number) and ..
        ssPatriotnodes >> FLATDATA(pchMsgTmp);

        // ... verify the network matches ours
        if (memcmp(pchMsgTmp, Params().MessageStart(), sizeof(pchMsgTmp))) {
            error("%s : Invalid network magic number", __func__);
            return IncorrectMagicNumber;
        }
        // de-serialize data into CPatriotnodeMan object
        ssPatriotnodes >> mnodemanToLoad;
    } catch (std::exception& e) {
        mnodemanToLoad.Clear();
        error("%s : Deserialize or I/O error - %s", __func__, e.what());
        return IncorrectFormat;
    }

    LogPrint("patriotnode","Loaded info from mncache.dat  %dms\n", GetTimeMillis() - nStart);
    LogPrint("patriotnode","  %s\n", mnodemanToLoad.ToString());
    if (!fDryRun) {
        LogPrint("patriotnode","Patriotnode manager - cleaning....\n");
        mnodemanToLoad.CheckAndRemove(true);
        LogPrint("patriotnode","Patriotnode manager - result:\n");
        LogPrint("patriotnode","  %s\n", mnodemanToLoad.ToString());
    }

    return Ok;
}

void DumpPatriotnodes()
{
    int64_t nStart = GetTimeMillis();

    CPatriotnodeDB mndb;
    CPatriotnodeMan tempMnodeman;

    LogPrint("patriotnode","Verifying mncache.dat format...\n");
    CPatriotnodeDB::ReadResult readResult = mndb.Read(tempMnodeman, true);
    // there was an error and it was not an error on file opening => do not proceed
    if (readResult == CPatriotnodeDB::FileError)
        LogPrint("patriotnode","Missing patriotnode cache file - mncache.dat, will try to recreate\n");
    else if (readResult != CPatriotnodeDB::Ok) {
        LogPrint("patriotnode","Error reading mncache.dat: ");
        if (readResult == CPatriotnodeDB::IncorrectFormat)
            LogPrint("patriotnode","magic is ok but data has invalid format, will try to recreate\n");
        else {
            LogPrint("patriotnode","file format is unknown or invalid, please fix it manually\n");
            return;
        }
    }
    LogPrint("patriotnode","Writting info to mncache.dat...\n");
    mndb.Write(mnodeman);

    LogPrint("patriotnode","Patriotnode dump finished  %dms\n", GetTimeMillis() - nStart);
}

CPatriotnodeMan::CPatriotnodeMan()
{
    nDsqCount = 0;
}

bool CPatriotnodeMan::Add(CPatriotnode& mn)
{
    LOCK(cs);

    if (!mn.IsEnabled())
        return false;

    CPatriotnode* pmn = Find(mn.vin);
    if (pmn == NULL) {
        LogPrint("patriotnode", "CPatriotnodeMan: Adding new Patriotnode %s - %i now\n", mn.vin.prevout.hash.ToString(), size() + 1);
        vPatriotnodes.push_back(mn);
        return true;
    }

    return false;
}

void CPatriotnodeMan::AskForMN(CNode* pnode, CTxIn& vin)
{
    std::map<COutPoint, int64_t>::iterator i = mWeAskedForPatriotnodeListEntry.find(vin.prevout);
    if (i != mWeAskedForPatriotnodeListEntry.end()) {
        int64_t t = (*i).second;
        if (GetTime() < t) return; // we've asked recently
    }

    // ask for the mnb info once from the node that sent mnp

    LogPrint("patriotnode", "CPatriotnodeMan::AskForMN - Asking node for missing entry, vin: %s\n", vin.prevout.hash.ToString());
    pnode->PushMessage("dseg", vin);
    int64_t askAgain = GetTime() + PATRIOTNODE_MIN_MNP_SECONDS;
    mWeAskedForPatriotnodeListEntry[vin.prevout] = askAgain;
}

void CPatriotnodeMan::Check()
{
    LOCK(cs);

    BOOST_FOREACH (CPatriotnode& mn, vPatriotnodes) {
        mn.Check();
    }
}

void CPatriotnodeMan::CheckAndRemove(bool forceExpiredRemoval)
{
    Check();

    LOCK(cs);

    //remove inactive and outdated
    vector<CPatriotnode>::iterator it = vPatriotnodes.begin();
    while (it != vPatriotnodes.end()) {
        if ((*it).activeState == CPatriotnode::PATRIOTNODE_REMOVE ||
            (*it).activeState == CPatriotnode::PATRIOTNODE_VIN_SPENT ||
            (forceExpiredRemoval && (*it).activeState == CPatriotnode::PATRIOTNODE_EXPIRED) ||
            (*it).protocolVersion < patriotnodePayments.GetMinPatriotnodePaymentsProto()) {
            LogPrint("patriotnode", "CPatriotnodeMan: Removing inactive Patriotnode %s - %i now\n", (*it).vin.prevout.hash.ToString(), size() - 1);

            //erase all of the broadcasts we've seen from this vin
            // -- if we missed a few pings and the node was removed, this will allow is to get it back without them
            //    sending a brand new mnb
            map<uint256, CPatriotnodeBroadcast>::iterator it3 = mapSeenPatriotnodeBroadcast.begin();
            while (it3 != mapSeenPatriotnodeBroadcast.end()) {
                if ((*it3).second.vin == (*it).vin) {
                    patriotnodeSync.mapSeenSyncMNB.erase((*it3).first);
                    mapSeenPatriotnodeBroadcast.erase(it3++);
                } else {
                    ++it3;
                }
            }

            // allow us to ask for this patriotnode again if we see another ping
            map<COutPoint, int64_t>::iterator it2 = mWeAskedForPatriotnodeListEntry.begin();
            while (it2 != mWeAskedForPatriotnodeListEntry.end()) {
                if ((*it2).first == (*it).vin.prevout) {
                    mWeAskedForPatriotnodeListEntry.erase(it2++);
                } else {
                    ++it2;
                }
            }

            it = vPatriotnodes.erase(it);
        } else {
            ++it;
        }
    }

    // check who's asked for the Patriotnode list
    map<CNetAddr, int64_t>::iterator it1 = mAskedUsForPatriotnodeList.begin();
    while (it1 != mAskedUsForPatriotnodeList.end()) {
        if ((*it1).second < GetTime()) {
            mAskedUsForPatriotnodeList.erase(it1++);
        } else {
            ++it1;
        }
    }

    // check who we asked for the Patriotnode list
    it1 = mWeAskedForPatriotnodeList.begin();
    while (it1 != mWeAskedForPatriotnodeList.end()) {
        if ((*it1).second < GetTime()) {
            mWeAskedForPatriotnodeList.erase(it1++);
        } else {
            ++it1;
        }
    }

    // check which Patriotnodes we've asked for
    map<COutPoint, int64_t>::iterator it2 = mWeAskedForPatriotnodeListEntry.begin();
    while (it2 != mWeAskedForPatriotnodeListEntry.end()) {
        if ((*it2).second < GetTime()) {
            mWeAskedForPatriotnodeListEntry.erase(it2++);
        } else {
            ++it2;
        }
    }

    // remove expired mapSeenPatriotnodeBroadcast
    map<uint256, CPatriotnodeBroadcast>::iterator it3 = mapSeenPatriotnodeBroadcast.begin();
    while (it3 != mapSeenPatriotnodeBroadcast.end()) {
        if ((*it3).second.lastPing.sigTime < GetTime() - (PATRIOTNODE_REMOVAL_SECONDS * 2)) {
            mapSeenPatriotnodeBroadcast.erase(it3++);
            patriotnodeSync.mapSeenSyncMNB.erase((*it3).second.GetHash());
        } else {
            ++it3;
        }
    }

    // remove expired mapSeenPatriotnodePing
    map<uint256, CPatriotnodePing>::iterator it4 = mapSeenPatriotnodePing.begin();
    while (it4 != mapSeenPatriotnodePing.end()) {
        if ((*it4).second.sigTime < GetTime() - (PATRIOTNODE_REMOVAL_SECONDS * 2)) {
            mapSeenPatriotnodePing.erase(it4++);
        } else {
            ++it4;
        }
    }
}

void CPatriotnodeMan::Clear()
{
    LOCK(cs);
    vPatriotnodes.clear();
    mAskedUsForPatriotnodeList.clear();
    mWeAskedForPatriotnodeList.clear();
    mWeAskedForPatriotnodeListEntry.clear();
    mapSeenPatriotnodeBroadcast.clear();
    mapSeenPatriotnodePing.clear();
    nDsqCount = 0;
}

int CPatriotnodeMan::stable_size ()
{
    int nStable_size = 0;
    int nMinProtocol = ActiveProtocol();
    int64_t nPatriotnode_Min_Age = MN_WINNER_MINIMUM_AGE;
    int64_t nPatriotnode_Age = 0;

    BOOST_FOREACH (CPatriotnode& mn, vPatriotnodes) {
        if (mn.protocolVersion < nMinProtocol) {
            continue; // Skip obsolete versions
        }
        if (IsSporkActive (SPORK_8_PATRIOTNODE_PAYMENT_ENFORCEMENT)) {
            nPatriotnode_Age = GetAdjustedTime() - mn.sigTime;
            if ((nPatriotnode_Age) < nPatriotnode_Min_Age) {
                continue; // Skip patriotnodes younger than (default) 8000 sec (MUST be > PATRIOTNODE_REMOVAL_SECONDS)
            }
        }
        mn.Check ();
        if (!mn.IsEnabled ())
            continue; // Skip not-enabled patriotnodes

        nStable_size++;
    }

    return nStable_size;
}

int CPatriotnodeMan::CountEnabled(int protocolVersion)
{
    int i = 0;
    protocolVersion = protocolVersion == -1 ? patriotnodePayments.GetMinPatriotnodePaymentsProto() : protocolVersion;

    BOOST_FOREACH (CPatriotnode& mn, vPatriotnodes) {
        mn.Check();
        if (mn.protocolVersion < protocolVersion || !mn.IsEnabled()) continue;
        i++;
    }

    return i;
}

void CPatriotnodeMan::CountNetworks(int protocolVersion, int& ipv4, int& ipv6, int& onion)
{
    protocolVersion = protocolVersion == -1 ? patriotnodePayments.GetMinPatriotnodePaymentsProto() : protocolVersion;

    BOOST_FOREACH (CPatriotnode& mn, vPatriotnodes) {
        mn.Check();
        std::string strHost;
        int port;
        SplitHostPort(mn.addr.ToString(), port, strHost);
        CNetAddr node = CNetAddr(strHost, false);
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
}

void CPatriotnodeMan::DsegUpdate(CNode* pnode)
{
    LOCK(cs);

    if (Params().NetworkID() == CBaseChainParams::MAIN) {
        if (!(pnode->addr.IsRFC1918() || pnode->addr.IsLocal())) {
            std::map<CNetAddr, int64_t>::iterator it = mWeAskedForPatriotnodeList.find(pnode->addr);
            if (it != mWeAskedForPatriotnodeList.end()) {
                if (GetTime() < (*it).second) {
                    LogPrint("patriotnode", "dseg - we already asked peer %i for the list; skipping...\n", pnode->GetId());
                    return;
                }
            }
        }
    }

    pnode->PushMessage("dseg", CTxIn());
    int64_t askAgain = GetTime() + PATRIOTNODES_DSEG_SECONDS;
    mWeAskedForPatriotnodeList[pnode->addr] = askAgain;
}

CPatriotnode* CPatriotnodeMan::Find(const CScript& payee)
{
    LOCK(cs);
    CScript payee2;

    BOOST_FOREACH (CPatriotnode& mn, vPatriotnodes) {
        payee2 = GetScriptForDestination(mn.pubKeyCollateralAddress.GetID());
        if (payee2 == payee)
            return &mn;
    }
    return NULL;
}

CPatriotnode* CPatriotnodeMan::Find(const CTxIn& vin)
{
    LOCK(cs);

    BOOST_FOREACH (CPatriotnode& mn, vPatriotnodes) {
        if (mn.vin.prevout == vin.prevout)
            return &mn;
    }
    return NULL;
}


CPatriotnode* CPatriotnodeMan::Find(const CPubKey& pubKeyPatriotnode)
{
    LOCK(cs);

    BOOST_FOREACH (CPatriotnode& mn, vPatriotnodes) {
        if (mn.pubKeyPatriotnode == pubKeyPatriotnode)
            return &mn;
    }
    return NULL;
}

//
// Deterministically select the oldest/best patriotnode to pay on the network
//
CPatriotnode* CPatriotnodeMan::GetNextPatriotnodeInQueueForPayment(int nBlockHeight, bool fFilterSigTime, int& nCount)
{
    LOCK(cs);

    CPatriotnode* pBestPatriotnode = NULL;
    std::vector<pair<int64_t, CTxIn> > vecPatriotnodeLastPaid;

    /*
        Make a vector with all of the last paid times
    */

    int nMnCount = CountEnabled();
    BOOST_FOREACH (CPatriotnode& mn, vPatriotnodes) {
        mn.Check();
        if (!mn.IsEnabled()) continue;

        // //check protocol version
        if (mn.protocolVersion < patriotnodePayments.GetMinPatriotnodePaymentsProto()) continue;

        //it's in the list (up to 8 entries ahead of current block to allow propagation) -- so let's skip it
        if (patriotnodePayments.IsScheduled(mn, nBlockHeight)) continue;

        //it's too new, wait for a cycle
        if (fFilterSigTime && mn.sigTime + (nMnCount * 2.6 * 60) > GetAdjustedTime()) continue;

        //make sure it has as many confirmations as there are patriotnodes
        if (mn.GetPatriotnodeInputAge() < nMnCount) continue;

        vecPatriotnodeLastPaid.push_back(make_pair(mn.SecondsSincePayment(), mn.vin));
    }

    nCount = (int)vecPatriotnodeLastPaid.size();

    //when the network is in the process of upgrading, don't penalize nodes that recently restarted
    if (fFilterSigTime && nCount < nMnCount / 3) return GetNextPatriotnodeInQueueForPayment(nBlockHeight, false, nCount);

    // Sort them high to low
    sort(vecPatriotnodeLastPaid.rbegin(), vecPatriotnodeLastPaid.rend(), CompareLastPaid());

    // Look at 1/10 of the oldest nodes (by last payment), calculate their scores and pay the best one
    //  -- This doesn't look at who is being paid in the +8-10 blocks, allowing for double payments very rarely
    //  -- 1/100 payments should be a double payment on mainnet - (1/(3000/10))*2
    //  -- (chance per block * chances before IsScheduled will fire)
    int nTenthNetwork = CountEnabled() / 10;
    int nCountTenth = 0;
    uint256 nHigh = 0;
    BOOST_FOREACH (PAIRTYPE(int64_t, CTxIn) & s, vecPatriotnodeLastPaid) {
        CPatriotnode* pmn = Find(s.second);
        if (!pmn) break;

        uint256 n = pmn->CalculateScore(1, nBlockHeight - 100);
        if (n > nHigh) {
            nHigh = n;
            pBestPatriotnode = pmn;
        }
        nCountTenth++;
        if (nCountTenth >= nTenthNetwork) break;
    }
    return pBestPatriotnode;
}

CPatriotnode* CPatriotnodeMan::FindRandomNotInVec(std::vector<CTxIn>& vecToExclude, int protocolVersion)
{
    LOCK(cs);

    protocolVersion = protocolVersion == -1 ? patriotnodePayments.GetMinPatriotnodePaymentsProto() : protocolVersion;

    int nCountEnabled = CountEnabled(protocolVersion);
    LogPrint("patriotnode", "CPatriotnodeMan::FindRandomNotInVec - nCountEnabled - vecToExclude.size() %d\n", nCountEnabled - vecToExclude.size());
    if (nCountEnabled - vecToExclude.size() < 1) return NULL;

    int rand = GetRandInt(nCountEnabled - vecToExclude.size());
    LogPrint("patriotnode", "CPatriotnodeMan::FindRandomNotInVec - rand %d\n", rand);
    bool found;

    BOOST_FOREACH (CPatriotnode& mn, vPatriotnodes) {
        if (mn.protocolVersion < protocolVersion || !mn.IsEnabled()) continue;
        found = false;
        BOOST_FOREACH (CTxIn& usedVin, vecToExclude) {
            if (mn.vin.prevout == usedVin.prevout) {
                found = true;
                break;
            }
        }
        if (found) continue;
        if (--rand < 1) {
            return &mn;
        }
    }

    return NULL;
}

CPatriotnode* CPatriotnodeMan::GetCurrentPatriotNode(int mod, int64_t nBlockHeight, int minProtocol)
{
    int64_t score = 0;
    CPatriotnode* winner = NULL;

    // scan for winner
    BOOST_FOREACH (CPatriotnode& mn, vPatriotnodes) {
        mn.Check();
        if (mn.protocolVersion < minProtocol || !mn.IsEnabled()) continue;

        // calculate the score for each Patriotnode
        uint256 n = mn.CalculateScore(mod, nBlockHeight);
        int64_t n2 = n.GetCompact(false);

        // determine the winner
        if (n2 > score) {
            score = n2;
            winner = &mn;
        }
    }

    return winner;
}

int CPatriotnodeMan::GetPatriotnodeRank(const CTxIn& vin, int64_t nBlockHeight, int minProtocol, bool fOnlyActive)
{
    std::vector<pair<int64_t, CTxIn> > vecPatriotnodeScores;
    int64_t nPatriotnode_Min_Age = MN_WINNER_MINIMUM_AGE;
    int64_t nPatriotnode_Age = 0;

    //make sure we know about this block
    uint256 hash = 0;
    if (!GetBlockHash(hash, nBlockHeight)) return -1;

    // scan for winner
    BOOST_FOREACH (CPatriotnode& mn, vPatriotnodes) {
        if (mn.protocolVersion < minProtocol) {
            LogPrint("patriotnode","Skipping Patriotnode with obsolete version %d\n", mn.protocolVersion);
            continue;                                                       // Skip obsolete versions
        }

        if (IsSporkActive(SPORK_8_PATRIOTNODE_PAYMENT_ENFORCEMENT)) {
            nPatriotnode_Age = GetAdjustedTime() - mn.sigTime;
            if ((nPatriotnode_Age) < nPatriotnode_Min_Age) {
                if (fDebug) LogPrint("patriotnode","Skipping just activated Patriotnode. Age: %ld\n", nPatriotnode_Age);
                continue;                                                   // Skip patriotnodes younger than (default) 1 hour
            }
        }
        if (fOnlyActive) {
            mn.Check();
            if (!mn.IsEnabled()) continue;
        }
        uint256 n = mn.CalculateScore(1, nBlockHeight);
        int64_t n2 = n.GetCompact(false);

        vecPatriotnodeScores.push_back(make_pair(n2, mn.vin));
    }

    sort(vecPatriotnodeScores.rbegin(), vecPatriotnodeScores.rend(), CompareScoreTxIn());

    int rank = 0;
    BOOST_FOREACH (PAIRTYPE(int64_t, CTxIn) & s, vecPatriotnodeScores) {
        rank++;
        if (s.second.prevout == vin.prevout) {
            return rank;
        }
    }

    return -1;
}

std::vector<pair<int, CPatriotnode> > CPatriotnodeMan::GetPatriotnodeRanks(int64_t nBlockHeight, int minProtocol)
{
    std::vector<pair<int64_t, CPatriotnode> > vecPatriotnodeScores;
    std::vector<pair<int, CPatriotnode> > vecPatriotnodeRanks;

    //make sure we know about this block
    uint256 hash = 0;
    if (!GetBlockHash(hash, nBlockHeight)) return vecPatriotnodeRanks;

    // scan for winner
    BOOST_FOREACH (CPatriotnode& mn, vPatriotnodes) {
        mn.Check();

        if (mn.protocolVersion < minProtocol) continue;

        if (!mn.IsEnabled()) {
            vecPatriotnodeScores.push_back(make_pair(9999, mn));
            continue;
        }

        uint256 n = mn.CalculateScore(1, nBlockHeight);
        int64_t n2 = n.GetCompact(false);

        vecPatriotnodeScores.push_back(make_pair(n2, mn));
    }

    sort(vecPatriotnodeScores.rbegin(), vecPatriotnodeScores.rend(), CompareScoreMN());

    int rank = 0;
    BOOST_FOREACH (PAIRTYPE(int64_t, CPatriotnode) & s, vecPatriotnodeScores) {
        rank++;
        vecPatriotnodeRanks.push_back(make_pair(rank, s.second));
    }

    return vecPatriotnodeRanks;
}

CPatriotnode* CPatriotnodeMan::GetPatriotnodeByRank(int nRank, int64_t nBlockHeight, int minProtocol, bool fOnlyActive)
{
    std::vector<pair<int64_t, CTxIn> > vecPatriotnodeScores;

    // scan for winner
    BOOST_FOREACH (CPatriotnode& mn, vPatriotnodes) {
        if (mn.protocolVersion < minProtocol) continue;
        if (fOnlyActive) {
            mn.Check();
            if (!mn.IsEnabled()) continue;
        }

        uint256 n = mn.CalculateScore(1, nBlockHeight);
        int64_t n2 = n.GetCompact(false);

        vecPatriotnodeScores.push_back(make_pair(n2, mn.vin));
    }

    sort(vecPatriotnodeScores.rbegin(), vecPatriotnodeScores.rend(), CompareScoreTxIn());

    int rank = 0;
    BOOST_FOREACH (PAIRTYPE(int64_t, CTxIn) & s, vecPatriotnodeScores) {
        rank++;
        if (rank == nRank) {
            return Find(s.second);
        }
    }

    return NULL;
}

void CPatriotnodeMan::ProcessPatriotnodeConnections()
{
    //we don't care about this for regtest
    if (Params().NetworkID() == CBaseChainParams::REGTEST) return;

    LOCK(cs_vNodes);
    BOOST_FOREACH (CNode* pnode, vNodes) {
        if (pnode->fObfuScationMaster) {
            if (obfuScationPool.pSubmittedToPatriotnode != NULL && pnode->addr == obfuScationPool.pSubmittedToPatriotnode->addr) continue;
            LogPrint("patriotnode","Closing Patriotnode connection peer=%i \n", pnode->GetId());
            pnode->fObfuScationMaster = false;
            pnode->Release();
        }
    }
}

void CPatriotnodeMan::ProcessMessage(CNode* pfrom, std::string& strCommand, CDataStream& vRecv)
{
    if (fLiteMode) return; //disable all Obfuscation/Patriotnode related functionality
    if (!patriotnodeSync.IsBlockchainSynced()) return;

    LOCK(cs_process_message);

    if (strCommand == "mnb") { //Patriotnode Broadcast
        CPatriotnodeBroadcast mnb;
        vRecv >> mnb;

        if (mapSeenPatriotnodeBroadcast.count(mnb.GetHash())) { //seen
            patriotnodeSync.AddedPatriotnodeList(mnb.GetHash());
            return;
        }
        mapSeenPatriotnodeBroadcast.insert(make_pair(mnb.GetHash(), mnb));

        int nDoS = 0;
        if (!mnb.CheckAndUpdate(nDoS)) {
            if (nDoS > 0)
                Misbehaving(pfrom->GetId(), nDoS);

            //failed
            return;
        }

        // make sure the vout that was signed is related to the transaction that spawned the Patriotnode
        //  - this is expensive, so it's only done once per Patriotnode
        if (!obfuScationSigner.IsVinAssociatedWithPubkey(mnb.vin, mnb.pubKeyCollateralAddress)) {
            LogPrintf("CPatriotnodeMan::ProcessMessage() : mnb - Got mismatched pubkey and vin\n");
            Misbehaving(pfrom->GetId(), 33);
            return;
        }

        // make sure it's still unspent
        //  - this is checked later by .check() in many places and by ThreadCheckObfuScationPool()
        if (mnb.CheckInputsAndAdd(nDoS)) {
            // use this as a peer
            addrman.Add(CAddress(mnb.addr), pfrom->addr, 2 * 60 * 60);
            patriotnodeSync.AddedPatriotnodeList(mnb.GetHash());
        } else {
            LogPrint("patriotnode","mnb - Rejected Patriotnode entry %s\n", mnb.vin.prevout.hash.ToString());

            if (nDoS > 0)
                Misbehaving(pfrom->GetId(), nDoS);
        }
    }

    else if (strCommand == "mnp") { //Patriotnode Ping
        CPatriotnodePing mnp;
        vRecv >> mnp;

        LogPrint("patriotnode", "mnp - Patriotnode ping, vin: %s\n", mnp.vin.prevout.hash.ToString());

        if (mapSeenPatriotnodePing.count(mnp.GetHash())) return; //seen
        mapSeenPatriotnodePing.insert(make_pair(mnp.GetHash(), mnp));

        int nDoS = 0;
        if (mnp.CheckAndUpdate(nDoS)) return;

        if (nDoS > 0) {
            // if anything significant failed, mark that node
            Misbehaving(pfrom->GetId(), nDoS);
        } else {
            // if nothing significant failed, search existing Patriotnode list
            CPatriotnode* pmn = Find(mnp.vin);
            // if it's known, don't ask for the mnb, just return
            if (pmn != NULL) return;
        }

        // something significant is broken or mn is unknown,
        // we might have to ask for a patriotnode entry once
        AskForMN(pfrom, mnp.vin);

    } else if (strCommand == "dseg") { //Get Patriotnode list or specific entry

        CTxIn vin;
        vRecv >> vin;

        if (vin == CTxIn()) { //only should ask for this once
            //local network
            bool isLocal = (pfrom->addr.IsRFC1918() || pfrom->addr.IsLocal());

            if (!isLocal && Params().NetworkID() == CBaseChainParams::MAIN) {
                std::map<CNetAddr, int64_t>::iterator i = mAskedUsForPatriotnodeList.find(pfrom->addr);
                if (i != mAskedUsForPatriotnodeList.end()) {
                    int64_t t = (*i).second;
                    if (GetTime() < t) {
                        LogPrintf("CPatriotnodeMan::ProcessMessage() : dseg - peer already asked me for the list\n");
                        Misbehaving(pfrom->GetId(), 34);
                        return;
                    }
                }
                int64_t askAgain = GetTime() + PATRIOTNODES_DSEG_SECONDS;
                mAskedUsForPatriotnodeList[pfrom->addr] = askAgain;
            }
        } //else, asking for a specific node which is ok


        int nInvCount = 0;

        BOOST_FOREACH (CPatriotnode& mn, vPatriotnodes) {
            if (mn.addr.IsRFC1918()) continue; //local network

            if (mn.IsEnabled()) {
                LogPrint("patriotnode", "dseg - Sending Patriotnode entry - %s \n", mn.vin.prevout.hash.ToString());
                if (vin == CTxIn() || vin == mn.vin) {
                    CPatriotnodeBroadcast mnb = CPatriotnodeBroadcast(mn);
                    uint256 hash = mnb.GetHash();
                    pfrom->PushInventory(CInv(MSG_PATRIOTNODE_ANNOUNCE, hash));
                    nInvCount++;

                    if (!mapSeenPatriotnodeBroadcast.count(hash)) mapSeenPatriotnodeBroadcast.insert(make_pair(hash, mnb));

                    if (vin == mn.vin) {
                        LogPrint("patriotnode", "dseg - Sent 1 Patriotnode entry to peer %i\n", pfrom->GetId());
                        return;
                    }
                }
            }
        }

        if (vin == CTxIn()) {
            pfrom->PushMessage("ssc", PATRIOTNODE_SYNC_LIST, nInvCount);
            LogPrint("patriotnode", "dseg - Sent %d Patriotnode entries to peer %i\n", nInvCount, pfrom->GetId());
        }
    }
    /*
     * IT'S SAFE TO REMOVE THIS IN FURTHER VERSIONS
     * AFTER MIGRATION TO V12 IS DONE
     */

    // Light version for OLD MASSTERNODES - fake pings, no self-activation
    else if (strCommand == "dsee") { //ObfuScation Election Entry

        if (IsSporkActive(SPORK_10_PATRIOTNODE_PAY_UPDATED_NODES)) return;

        CTxIn vin;
        CService addr;
        CPubKey pubkey;
        CPubKey pubkey2;
        vector<unsigned char> vchSig;
        int64_t sigTime;
        int count;
        int current;
        int64_t lastUpdated;
        int protocolVersion;
        CScript donationAddress;
        int donationPercentage;
        std::string strMessage;

        vRecv >> vin >> addr >> vchSig >> sigTime >> pubkey >> pubkey2 >> count >> current >> lastUpdated >> protocolVersion >> donationAddress >> donationPercentage;

        // make sure signature isn't in the future (past is OK)
        if (sigTime > GetAdjustedTime() + 60 * 60) {
            LogPrintf("CPatriotnodeMan::ProcessMessage() : dsee - Signature rejected, too far into the future %s\n", vin.prevout.hash.ToString());
            Misbehaving(pfrom->GetId(), 1);
            return;
        }

        std::string vchPubKey(pubkey.begin(), pubkey.end());
        std::string vchPubKey2(pubkey2.begin(), pubkey2.end());

        strMessage = addr.ToString() + boost::lexical_cast<std::string>(sigTime) + vchPubKey + vchPubKey2 + boost::lexical_cast<std::string>(protocolVersion) + donationAddress.ToString() + boost::lexical_cast<std::string>(donationPercentage);

        if (protocolVersion < patriotnodePayments.GetMinPatriotnodePaymentsProto()) {
            LogPrintf("CPatriotnodeMan::ProcessMessage() : dsee - ignoring outdated Patriotnode %s protocol version %d < %d\n", vin.prevout.hash.ToString(), protocolVersion, patriotnodePayments.GetMinPatriotnodePaymentsProto());
            Misbehaving(pfrom->GetId(), 1);
            return;
        }

        CScript pubkeyScript;
        pubkeyScript = GetScriptForDestination(pubkey.GetID());

        if (pubkeyScript.size() != 25) {
            LogPrintf("CPatriotnodeMan::ProcessMessage() : dsee - pubkey the wrong size\n");
            Misbehaving(pfrom->GetId(), 100);
            return;
        }

        CScript pubkeyScript2;
        pubkeyScript2 = GetScriptForDestination(pubkey2.GetID());

        if (pubkeyScript2.size() != 25) {
            LogPrintf("CPatriotnodeMan::ProcessMessage() : dsee - pubkey2 the wrong size\n");
            Misbehaving(pfrom->GetId(), 100);
            return;
        }

        if (!vin.scriptSig.empty()) {
            LogPrintf("CPatriotnodeMan::ProcessMessage() : dsee - Ignore Not Empty ScriptSig %s\n", vin.prevout.hash.ToString());
            Misbehaving(pfrom->GetId(), 100);
            return;
        }

        std::string errorMessage = "";
        if (!obfuScationSigner.VerifyMessage(pubkey, vchSig, strMessage, errorMessage)) {
            LogPrintf("CPatriotnodeMan::ProcessMessage() : dsee - Got bad Patriotnode address signature\n");
            Misbehaving(pfrom->GetId(), 100);
            return;
        }

        if (Params().NetworkID() == CBaseChainParams::MAIN) {
            if (addr.GetPort() != 15110) return;
        } else if (addr.GetPort() == 15110)
            return;

        //search existing Patriotnode list, this is where we update existing Patriotnodes with new dsee broadcasts
        CPatriotnode* pmn = this->Find(vin);
        if (pmn != NULL) {
            // count == -1 when it's a new entry
            //   e.g. We don't want the entry relayed/time updated when we're syncing the list
            // mn.pubkey = pubkey, IsVinAssociatedWithPubkey is validated once below,
            //   after that they just need to match
            if (count == -1 && pmn->pubKeyCollateralAddress == pubkey && (GetAdjustedTime() - pmn->nLastDsee > PATRIOTNODE_MIN_MNB_SECONDS)) {
                if (pmn->protocolVersion > GETHEADERS_VERSION && sigTime - pmn->lastPing.sigTime < PATRIOTNODE_MIN_MNB_SECONDS) return;
                if (pmn->nLastDsee < sigTime) { //take the newest entry
                    LogPrint("patriotnode", "dsee - Got updated entry for %s\n", vin.prevout.hash.ToString());
                    if (pmn->protocolVersion < GETHEADERS_VERSION) {
                        pmn->pubKeyPatriotnode = pubkey2;
                        pmn->sigTime = sigTime;
                        pmn->sig = vchSig;
                        pmn->protocolVersion = protocolVersion;
                        pmn->addr = addr;
                        //fake ping
                        pmn->lastPing = CPatriotnodePing(vin);
                    }
                    pmn->nLastDsee = sigTime;
                    pmn->Check();
                    if (pmn->IsEnabled()) {
                        TRY_LOCK(cs_vNodes, lockNodes);
                        if (!lockNodes) return;
                        BOOST_FOREACH (CNode* pnode, vNodes)
                            if (pnode->nVersion >= patriotnodePayments.GetMinPatriotnodePaymentsProto())
                                pnode->PushMessage("dsee", vin, addr, vchSig, sigTime, pubkey, pubkey2, count, current, lastUpdated, protocolVersion, donationAddress, donationPercentage);
                    }
                }
            }

            return;
        }

        static std::map<COutPoint, CPubKey> mapSeenDsee;
        if (mapSeenDsee.count(vin.prevout) && mapSeenDsee[vin.prevout] == pubkey) {
            LogPrint("patriotnode", "dsee - already seen this vin %s\n", vin.prevout.ToString());
            return;
        }
        mapSeenDsee.insert(make_pair(vin.prevout, pubkey));
        // make sure the vout that was signed is related to the transaction that spawned the Patriotnode
        //  - this is expensive, so it's only done once per Patriotnode
        if (!obfuScationSigner.IsVinAssociatedWithPubkey(vin, pubkey)) {
            LogPrintf("CPatriotnodeMan::ProcessMessage() : dsee - Got mismatched pubkey and vin\n");
            Misbehaving(pfrom->GetId(), 100);
            return;
        }


        LogPrint("patriotnode", "dsee - Got NEW OLD Patriotnode entry %s\n", vin.prevout.hash.ToString());

        // make sure it's still unspent
        //  - this is checked later by .check() in many places and by ThreadCheckObfuScationPool()

        CValidationState state;
        CMutableTransaction tx = CMutableTransaction();
        CTxOut vout = CTxOut(4999.99 * COIN, obfuScationPool.collateralPubKey);
        tx.vin.push_back(vin);
        tx.vout.push_back(vout);

        bool fAcceptable = false;
        {
            TRY_LOCK(cs_main, lockMain);
            if (!lockMain) return;
            fAcceptable = AcceptableInputs(mempool, state, CTransaction(tx), false, NULL);
        }

        if (fAcceptable) {
            if (GetInputAge(vin) < PATRIOTNODE_MIN_CONFIRMATIONS) {
                LogPrintf("CPatriotnodeMan::ProcessMessage() : dsee - Input must have least %d confirmations\n", PATRIOTNODE_MIN_CONFIRMATIONS);
                Misbehaving(pfrom->GetId(), 20);
                return;
            }

            // verify that sig time is legit in past
            // should be at least not earlier than block when 1000 TrumpCoin tx got PATRIOTNODE_MIN_CONFIRMATIONS
            uint256 hashBlock = 0;
            CTransaction tx2;
            GetTransaction(vin.prevout.hash, tx2, hashBlock, true);
            BlockMap::iterator mi = mapBlockIndex.find(hashBlock);
            if (mi != mapBlockIndex.end() && (*mi).second) {
                CBlockIndex* pMNIndex = (*mi).second;                                                        // block for 10000 TRUMP tx -> 1 confirmation
                CBlockIndex* pConfIndex = chainActive[pMNIndex->nHeight + PATRIOTNODE_MIN_CONFIRMATIONS - 1]; // block where tx got PATRIOTNODE_MIN_CONFIRMATIONS
                if (pConfIndex->GetBlockTime() > sigTime) {
                    LogPrint("patriotnode","mnb - Bad sigTime %d for Patriotnode %s (%i conf block is at %d)\n",
                        sigTime, vin.prevout.hash.ToString(), PATRIOTNODE_MIN_CONFIRMATIONS, pConfIndex->GetBlockTime());
                    return;
                }
            }

            // use this as a peer
            addrman.Add(CAddress(addr), pfrom->addr, 2 * 60 * 60);

            // add Patriotnode
            CPatriotnode mn = CPatriotnode();
            mn.addr = addr;
            mn.vin = vin;
            mn.pubKeyCollateralAddress = pubkey;
            mn.sig = vchSig;
            mn.sigTime = sigTime;
            mn.pubKeyPatriotnode = pubkey2;
            mn.protocolVersion = protocolVersion;
            // fake ping
            mn.lastPing = CPatriotnodePing(vin);
            mn.Check(true);
            // add v11 patriotnodes, v12 should be added by mnb only
            if (protocolVersion < GETHEADERS_VERSION) {
                LogPrint("patriotnode", "dsee - Accepted OLD Patriotnode entry %i %i\n", count, current);
                Add(mn);
            }
            if (mn.IsEnabled()) {
                TRY_LOCK(cs_vNodes, lockNodes);
                if (!lockNodes) return;
                BOOST_FOREACH (CNode* pnode, vNodes)
                    if (pnode->nVersion >= patriotnodePayments.GetMinPatriotnodePaymentsProto())
                        pnode->PushMessage("dsee", vin, addr, vchSig, sigTime, pubkey, pubkey2, count, current, lastUpdated, protocolVersion, donationAddress, donationPercentage);
            }
        } else {
            LogPrint("patriotnode","dsee - Rejected Patriotnode entry %s\n", vin.prevout.hash.ToString());

            int nDoS = 0;
            if (state.IsInvalid(nDoS)) {
                LogPrint("patriotnode","dsee - %s from %i %s was not accepted into the memory pool\n", tx.GetHash().ToString().c_str(),
                    pfrom->GetId(), pfrom->cleanSubVer.c_str());
                if (nDoS > 0)
                    Misbehaving(pfrom->GetId(), nDoS);
            }
        }
    }

    else if (strCommand == "dseep") { //ObfuScation Election Entry Ping

        if (IsSporkActive(SPORK_10_PATRIOTNODE_PAY_UPDATED_NODES)) return;

        CTxIn vin;
        vector<unsigned char> vchSig;
        int64_t sigTime;
        bool stop;
        vRecv >> vin >> vchSig >> sigTime >> stop;

        //LogPrint("patriotnode","dseep - Received: vin: %s sigTime: %lld stop: %s\n", vin.ToString().c_str(), sigTime, stop ? "true" : "false");

        if (sigTime > GetAdjustedTime() + 60 * 60) {
            LogPrintf("CPatriotnodeMan::ProcessMessage() : dseep - Signature rejected, too far into the future %s\n", vin.prevout.hash.ToString());
            Misbehaving(pfrom->GetId(), 1);
            return;
        }

        if (sigTime <= GetAdjustedTime() - 60 * 60) {
            LogPrintf("CPatriotnodeMan::ProcessMessage() : dseep - Signature rejected, too far into the past %s - %d %d \n", vin.prevout.hash.ToString(), sigTime, GetAdjustedTime());
            Misbehaving(pfrom->GetId(), 1);
            return;
        }

        std::map<COutPoint, int64_t>::iterator i = mWeAskedForPatriotnodeListEntry.find(vin.prevout);
        if (i != mWeAskedForPatriotnodeListEntry.end()) {
            int64_t t = (*i).second;
            if (GetTime() < t) return; // we've asked recently
        }

        // see if we have this Patriotnode
        CPatriotnode* pmn = this->Find(vin);
        if (pmn != NULL && pmn->protocolVersion >= patriotnodePayments.GetMinPatriotnodePaymentsProto()) {
            // LogPrint("patriotnode","dseep - Found corresponding mn for vin: %s\n", vin.ToString().c_str());
            // take this only if it's newer
            if (sigTime - pmn->nLastDseep > PATRIOTNODE_MIN_MNP_SECONDS) {
                std::string strMessage = pmn->addr.ToString() + boost::lexical_cast<std::string>(sigTime) + boost::lexical_cast<std::string>(stop);

                std::string errorMessage = "";
                if (!obfuScationSigner.VerifyMessage(pmn->pubKeyPatriotnode, vchSig, strMessage, errorMessage)) {
                    LogPrint("patriotnode","dseep - Got bad Patriotnode address signature %s \n", vin.prevout.hash.ToString());
                    //Misbehaving(pfrom->GetId(), 100);
                    return;
                }

                // fake ping for v11 patriotnodes, ignore for v12
                if (pmn->protocolVersion < GETHEADERS_VERSION) pmn->lastPing = CPatriotnodePing(vin);
                pmn->nLastDseep = sigTime;
                pmn->Check();
                if (pmn->IsEnabled()) {
                    TRY_LOCK(cs_vNodes, lockNodes);
                    if (!lockNodes) return;
                    LogPrint("patriotnode", "dseep - relaying %s \n", vin.prevout.hash.ToString());
                    BOOST_FOREACH (CNode* pnode, vNodes)
                        if (pnode->nVersion >= patriotnodePayments.GetMinPatriotnodePaymentsProto())
                            pnode->PushMessage("dseep", vin, vchSig, sigTime, stop);
                }
            }
            return;
        }

        LogPrint("patriotnode", "dseep - Couldn't find Patriotnode entry %s peer=%i\n", vin.prevout.hash.ToString(), pfrom->GetId());

        AskForMN(pfrom, vin);
    }

    /*
     * END OF "REMOVE"
     */
}

void CPatriotnodeMan::Remove(CTxIn vin)
{
    LOCK(cs);

    vector<CPatriotnode>::iterator it = vPatriotnodes.begin();
    while (it != vPatriotnodes.end()) {
        if ((*it).vin == vin) {
            LogPrint("patriotnode", "CPatriotnodeMan: Removing Patriotnode %s - %i now\n", (*it).vin.prevout.hash.ToString(), size() - 1);
            vPatriotnodes.erase(it);
            break;
        }
        ++it;
    }
}

void CPatriotnodeMan::UpdatePatriotnodeList(CPatriotnodeBroadcast mnb)
{
	mapSeenPatriotnodePing.insert(make_pair(mnb.lastPing.GetHash(), mnb.lastPing));
	mapSeenPatriotnodeBroadcast.insert(make_pair(mnb.GetHash(), mnb));
	patriotnodeSync.AddedPatriotnodeList(mnb.GetHash());

    LogPrint("patriotnode","CPatriotnodeMan::UpdatePatriotnodeList() -- patriotnode=%s\n", mnb.vin.prevout.ToString());

    CPatriotnode* pmn = Find(mnb.vin);
    if (pmn == NULL) {
        CPatriotnode mn(mnb);
        Add(mn);
    } else {
    	pmn->UpdateFromNewBroadcast(mnb);
    }
}

std::string CPatriotnodeMan::ToString() const
{
    std::ostringstream info;

    info << "Patriotnodes: " << (int)vPatriotnodes.size() << ", peers who asked us for Patriotnode list: " << (int)mAskedUsForPatriotnodeList.size() << ", peers we asked for Patriotnode list: " << (int)mWeAskedForPatriotnodeList.size() << ", entries in Patriotnode list we asked for: " << (int)mWeAskedForPatriotnodeListEntry.size() << ", nDsqCount: " << (int)nDsqCount;

    return info.str();
}
