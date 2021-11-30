// Copyright (c) 2014-2015 The Dash developers
// Copyright (c) 2015-2020 The TrumpCoin developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef PATRIOTNODEMAN_H
#define PATRIOTNODEMAN_H

#include "activepatriotnode.h"
#include "cyclingvector.h"
#include "key.h"
#include "key_io.h"
#include "patriotnode.h"
#include "net.h"
#include "sync.h"
#include "util/system.h"

#define PATRIOTNODES_REQUEST_SECONDS (60 * 60) // One hour.

/** Maximum number of block hashes to cache */
static const unsigned int CACHED_BLOCK_HASHES = 200;

class CPatriotnodeMan;
class CActivePatriotnode;

extern CPatriotnodeMan mnodeman;
extern CActivePatriotnode activePatriotnode;

void DumpPatriotnodes();

/** Access to the PN database (mncache.dat)
 */
class CPatriotnodeDB
{
private:
    fs::path pathPN;
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
    ReadResult Read(CPatriotnodeMan& mnodemanToLoad);
};


class CPatriotnodeMan
{
private:
    // critical section to protect the inner data structures
    mutable RecursiveMutex cs;

    // critical section to protect the inner data structures specifically on messaging
    mutable RecursiveMutex cs_process_message;

    // map to hold all PNs (indexed by collateral outpoint)
    std::map<COutPoint, PatriotnodeRef> mapPatriotnodes;
    // who's asked for the Patriotnode list and the last time
    std::map<CNetAddr, int64_t> mAskedUsForPatriotnodeList;
    // who we asked for the Patriotnode list and the last time
    std::map<CNetAddr, int64_t> mWeAskedForPatriotnodeList;
    // which Patriotnodes we've asked for
    std::map<COutPoint, int64_t> mWeAskedForPatriotnodeListEntry;

    // Memory Only. Updated in NewBlock (blocks arrive in order)
    std::atomic<int> nBestHeight;

    // Memory Only. Cache last block hashes. Used to verify mn pings and winners.
    CyclingVector<uint256> cvLastBlockHashes;

    // Return the banning score (0 if no ban score increase is needed).
    int ProcessPNBroadcast(CNode* pfrom, CPatriotnodeBroadcast& mnb);
    int ProcessPNPing(CNode* pfrom, CPatriotnodePing& mnp);
    int ProcessMessageInner(CNode* pfrom, std::string& strCommand, CDataStream& vRecv);

    // Relay a PN
    void BroadcastInvPN(CPatriotnode* mn, CNode* pfrom);

public:
    // Keep track of all broadcasts I've seen
    std::map<uint256, CPatriotnodeBroadcast> mapSeenPatriotnodeBroadcast;
    // Keep track of all pings I've seen
    std::map<uint256, CPatriotnodePing> mapSeenPatriotnodePing;

    // keep track of dsq count to prevent patriotnodes from gaming obfuscation queue
    // TODO: Remove this from serialization
    int64_t nDsqCount;

    SERIALIZE_METHODS(CPatriotnodeMan, obj)
    {
        LOCK(obj.cs);
        READWRITE(obj.mapPatriotnodes);
        READWRITE(obj.mAskedUsForPatriotnodeList);
        READWRITE(obj.mWeAskedForPatriotnodeList);
        READWRITE(obj.mWeAskedForPatriotnodeListEntry);
        READWRITE(obj.nDsqCount);

        READWRITE(obj.mapSeenPatriotnodeBroadcast);
        READWRITE(obj.mapSeenPatriotnodePing);
    }

    CPatriotnodeMan();

    /// Add an entry
    bool Add(CPatriotnode& mn);

    /// Ask (source) node for mnb
    void AskForPN(CNode* pnode, const CTxIn& vin);

    /// Check all Patriotnodes and remove inactive. Return the total patriotnode count.
    int CheckAndRemove(bool forceExpiredRemoval = false);

    /// Clear Patriotnode vector
    void Clear();

    void SetBestHeight(int height) { nBestHeight.store(height, std::memory_order_release); };
    int GetBestHeight() const { return nBestHeight.load(std::memory_order_acquire); }

    int CountEnabled(int protocolVersion = -1) const;

    /// Count the number of nodes with a specific proto version for each network. Return the total.
    int CountNetworks(int& ipv4, int& ipv6, int& onion) const;

    bool RequestMnList(CNode* pnode);

    /// Find an entry
    CPatriotnode* Find(const COutPoint& collateralOut);
    const CPatriotnode* Find(const COutPoint& collateralOut) const;
    CPatriotnode* Find(const CPubKey& pubKeyPatriotnode);

    /// Check all transactions in a block, for spent patriotnode collateral outpoints (marking them as spent)
    void CheckSpentCollaterals(const std::vector<CTransactionRef>& vtx);

    /// Find an entry in the patriotnode list that is next to be paid
    PatriotnodeRef GetNextPatriotnodeInQueueForPayment(int nBlockHeight, bool fFilterSigTime, int& nCount, const CBlockIndex* pChainTip = nullptr) const;

    /// Get the winner for this block hash
    PatriotnodeRef GetCurrentPatriotNode(const uint256& hash) const;

    /// vector of pairs <patriotnode winner, height>
    std::vector<std::pair<PatriotnodeRef, int>> GetMnScores(int nLast) const;

    // Retrieve the known patriotnodes ordered by scoring without checking them. (Only used for listpatriotnodes RPC call)
    std::vector<std::pair<int64_t, PatriotnodeRef>> GetPatriotnodeRanks(int nBlockHeight) const;
    int GetPatriotnodeRank(const CTxIn& vin, int64_t nBlockHeight) const;

    void ProcessMessage(CNode* pfrom, std::string& strCommand, CDataStream& vRecv);

    // Process GETPNLIST message, returning the banning score (if 0, no ban score increase is needed)
    int ProcessGetPNList(CNode* pfrom, CTxIn& vin);

    /// Return the number of Patriotnodes older than (default) 8000 seconds
    int stable_size() const;

    std::string ToString() const;

    void Remove(const COutPoint& collateralOut);

    /// Update patriotnode list and maps using provided CPatriotnodeBroadcast
    void UpdatePatriotnodeList(CPatriotnodeBroadcast& mnb);

    /// Get the time a patriotnode was last paid
    int64_t GetLastPaid(const PatriotnodeRef& mn, const CBlockIndex* BlockReading) const;
    int64_t SecondsSincePayment(const PatriotnodeRef& mn, const CBlockIndex* BlockReading) const;

    // Block hashes cycling vector management
    void CacheBlockHash(const CBlockIndex* pindex);
    void UncacheBlockHash(const CBlockIndex* pindex);
    uint256 GetHashAtHeight(int nHeight) const;
    bool IsWithinDepth(const uint256& nHash, int depth) const;
    uint256 GetBlockHashToPing() const { return GetHashAtHeight(GetBestHeight() - PNPING_DEPTH); }
    std::vector<uint256> GetCachedBlocks() const { return cvLastBlockHashes.GetCache(); }
};

void ThreadCheckPatriotnodes();

#endif
