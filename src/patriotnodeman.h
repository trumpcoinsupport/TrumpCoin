// Copyright (c) 2014-2015 The Dash developers
// Copyright (c) 2015-2018 The PIVX developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef PATRIOTNODEMAN_H
#define PATRIOTNODEMAN_H

#include "base58.h"
#include "key.h"
#include "main.h"
#include "patriotnode.h"
#include "net.h"
#include "sync.h"
#include "util.h"

#define PATRIOTNODES_DUMP_SECONDS (15 * 60)
#define PATRIOTNODES_DSEG_SECONDS (3 * 60 * 60)

using namespace std;

class CPatriotnodeMan;

extern CPatriotnodeMan mnodeman;
void DumpPatriotnodes();

/** Access to the MN database (mncache.dat)
 */
class CPatriotnodeDB
{
private:
    boost::filesystem::path pathMN;
    std::string strMagicMessage;

public:
    enum ReadResult {
        Ok,
        FileError,
        HashReadError,
        IncorrectHash,
        IncorrectMagicMessage,
        IncorrectMagicNumber,
        IncorrectFormat
    };

    CPatriotnodeDB();
    bool Write(const CPatriotnodeMan& mnodemanToSave);
    ReadResult Read(CPatriotnodeMan& mnodemanToLoad, bool fDryRun = false);
};

class CPatriotnodeMan
{
private:
    // critical section to protect the inner data structures
    mutable CCriticalSection cs;

    // critical section to protect the inner data structures specifically on messaging
    mutable CCriticalSection cs_process_message;

    // map to hold all MNs
    std::vector<CPatriotnode> vPatriotnodes;
    // who's asked for the Patriotnode list and the last time
    std::map<CNetAddr, int64_t> mAskedUsForPatriotnodeList;
    // who we asked for the Patriotnode list and the last time
    std::map<CNetAddr, int64_t> mWeAskedForPatriotnodeList;
    // which Patriotnodes we've asked for
    std::map<COutPoint, int64_t> mWeAskedForPatriotnodeListEntry;

public:
    // Keep track of all broadcasts I've seen
    map<uint256, CPatriotnodeBroadcast> mapSeenPatriotnodeBroadcast;
    // Keep track of all pings I've seen
    map<uint256, CPatriotnodePing> mapSeenPatriotnodePing;

    // keep track of dsq count to prevent patriotnodes from gaming obfuscation queue
    int64_t nDsqCount;

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action, int nType, int nVersion)
    {
        LOCK(cs);
        READWRITE(vPatriotnodes);
        READWRITE(mAskedUsForPatriotnodeList);
        READWRITE(mWeAskedForPatriotnodeList);
        READWRITE(mWeAskedForPatriotnodeListEntry);
        READWRITE(nDsqCount);

        READWRITE(mapSeenPatriotnodeBroadcast);
        READWRITE(mapSeenPatriotnodePing);
    }

    CPatriotnodeMan();
    CPatriotnodeMan(CPatriotnodeMan& other);

    /// Add an entry
    bool Add(CPatriotnode& mn);

    /// Ask (source) node for mnb
    void AskForMN(CNode* pnode, CTxIn& vin);

    /// Check all Patriotnodes
    void Check();

    /// Check all Patriotnodes and remove inactive
    void CheckAndRemove(bool forceExpiredRemoval = false);

    /// Clear Patriotnode vector
    void Clear();

    int CountEnabled(int protocolVersion = -1);

    void CountNetworks(int protocolVersion, int& ipv4, int& ipv6, int& onion);

    void DsegUpdate(CNode* pnode);

    /// Find an entry
    CPatriotnode* Find(const CScript& payee);
    CPatriotnode* Find(const CTxIn& vin);
    CPatriotnode* Find(const CPubKey& pubKeyPatriotnode);

    /// Find an entry in the patriotnode list that is next to be paid
    CPatriotnode* GetNextPatriotnodeInQueueForPayment(int nBlockHeight, bool fFilterSigTime, int& nCount);

    /// Find a random entry
    CPatriotnode* FindRandomNotInVec(std::vector<CTxIn>& vecToExclude, int protocolVersion = -1);

    /// Get the current winner for this block
    CPatriotnode* GetCurrentPatriotNode(int mod = 1, int64_t nBlockHeight = 0, int minProtocol = 0);

    std::vector<CPatriotnode> GetFullPatriotnodeVector()
    {
        Check();
        return vPatriotnodes;
    }

    std::vector<pair<int, CPatriotnode> > GetPatriotnodeRanks(int64_t nBlockHeight, int minProtocol = 0);
    int GetPatriotnodeRank(const CTxIn& vin, int64_t nBlockHeight, int minProtocol = 0, bool fOnlyActive = true);
    CPatriotnode* GetPatriotnodeByRank(int nRank, int64_t nBlockHeight, int minProtocol = 0, bool fOnlyActive = true);

    void ProcessPatriotnodeConnections();

    void ProcessMessage(CNode* pfrom, std::string& strCommand, CDataStream& vRecv);

    /// Return the number of (unique) Patriotnodes
    int size() { return vPatriotnodes.size(); }

    /// Return the number of Patriotnodes older than (default) 8000 seconds
    int stable_size ();

    std::string ToString() const;

    void Remove(CTxIn vin);

    int GetEstimatedPatriotnodes(int nBlock);

    /// Update patriotnode list and maps using provided CPatriotnodeBroadcast
    void UpdatePatriotnodeList(CPatriotnodeBroadcast mnb);
};

#endif
