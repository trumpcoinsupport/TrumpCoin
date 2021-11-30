// Copyright (c) 2018-2021 The Dash Core developers
// Copyright (c) 2021 The TrumpCoin developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef TrumpCoin_DETERMINISTICPNS_H
#define TrumpCoin_DETERMINISTICPNS_H

#include "arith_uint256.h"
#include "dbwrapper.h"
#include "evo/evodb.h"
#include "evo/providertx.h"
#include "saltedhasher.h"
#include "sync.h"

#include <immer/map.hpp>
#include <immer/map_transient.hpp>

#include <unordered_map>

class CBlock;
class CBlockIndex;
class CValidationState;

class CDeterministicPNState
{
public:
    int nRegisteredHeight{-1};
    int nLastPaidHeight{0};
    int nPoSePenalty{0};
    int nPoSeRevivedHeight{-1};
    int nPoSeBanHeight{-1};
    uint16_t nRevocationReason{ProUpRevPL::REASON_NOT_SPECIFIED};

    // the block hash X blocks after registration, used in quorum calculations
    uint256 confirmedHash;
    // sha256(proTxHash, confirmedHash) to speed up quorum calculations
    // please note that this is NOT a double-sha256 hash
    uint256 confirmedHashWithProRegTxHash;

    CKeyID keyIDOwner;
    CKeyID keyIDOperator;
    CKeyID keyIDVoting;
    CService addr;
    CScript scriptPayout;
    CScript scriptOperatorPayout;

public:
    CDeterministicPNState() {}
    explicit CDeterministicPNState(const ProRegPL& pl)
    {
        keyIDOwner = pl.keyIDOwner;
        keyIDOperator = pl.keyIDOperator;
        keyIDVoting = pl.keyIDVoting;
        addr = pl.addr;
        scriptPayout = pl.scriptPayout;
        scriptOperatorPayout = pl.scriptOperatorPayout;
    }
    template <typename Stream>
    CDeterministicPNState(deserialize_type, Stream& s) { s >> *this; }

    SERIALIZE_METHODS(CDeterministicPNState, obj)
    {
        READWRITE(obj.nRegisteredHeight);
        READWRITE(obj.nLastPaidHeight);
        READWRITE(obj.nPoSePenalty);
        READWRITE(obj.nPoSeRevivedHeight);
        READWRITE(obj.nPoSeBanHeight);
        READWRITE(obj.nRevocationReason);
        READWRITE(obj.confirmedHash);
        READWRITE(obj.confirmedHashWithProRegTxHash);
        READWRITE(obj.keyIDOwner);
        READWRITE(obj.keyIDOperator);
        READWRITE(obj.keyIDVoting);
        READWRITE(obj.addr);
        READWRITE(obj.scriptPayout);
        READWRITE(obj.scriptOperatorPayout);
    }

    void ResetOperatorFields()
    {
        keyIDOperator = CKeyID();
        addr = CService();
        scriptOperatorPayout = CScript();
        nRevocationReason = ProUpRevPL::REASON_NOT_SPECIFIED;
    }
    void BanIfNotBanned(int height)
    {
        if (nPoSeBanHeight == -1) {
            nPoSeBanHeight = height;
        }
    }
    void UpdateConfirmedHash(const uint256& _proTxHash, const uint256& _confirmedHash)
    {
        confirmedHash = _confirmedHash;
        CSHA256 h;
        h.Write(_proTxHash.begin(), _proTxHash.size());
        h.Write(_confirmedHash.begin(), _confirmedHash.size());
        h.Finalize(confirmedHashWithProRegTxHash.begin());
    }

public:
    std::string ToString() const;
    void ToJson(UniValue& obj) const;
};
typedef std::shared_ptr<CDeterministicPNState> CDeterministicPNStatePtr;
typedef std::shared_ptr<const CDeterministicPNState> CDeterministicPNStateCPtr;

class CDeterministicPNStateDiff
{
public:
    enum Field : uint32_t {
        Field_nRegisteredHeight                 = 0x0001,
        Field_nLastPaidHeight                   = 0x0002,
        Field_nPoSePenalty                      = 0x0004,
        Field_nPoSeRevivedHeight                = 0x0008,
        Field_nPoSeBanHeight                    = 0x0010,
        Field_nRevocationReason                 = 0x0020,
        Field_confirmedHash                     = 0x0040,
        Field_confirmedHashWithProRegTxHash     = 0x0080,
        Field_keyIDOwner                        = 0x0100,
        Field_keyIDOperator                     = 0x0200,
        Field_keyIDVoting                       = 0x0400,
        Field_addr                              = 0x0800,
        Field_scriptPayout                      = 0x1000,
        Field_scriptOperatorPayout              = 0x2000,
    };

#define DPN_STATE_DIFF_ALL_FIELDS \
    DPN_STATE_DIFF_LINE(nRegisteredHeight) \
    DPN_STATE_DIFF_LINE(nLastPaidHeight) \
    DPN_STATE_DIFF_LINE(nPoSePenalty) \
    DPN_STATE_DIFF_LINE(nPoSeRevivedHeight) \
    DPN_STATE_DIFF_LINE(nPoSeBanHeight) \
    DPN_STATE_DIFF_LINE(nRevocationReason) \
    DPN_STATE_DIFF_LINE(confirmedHash) \
    DPN_STATE_DIFF_LINE(confirmedHashWithProRegTxHash) \
    DPN_STATE_DIFF_LINE(keyIDOwner) \
    DPN_STATE_DIFF_LINE(keyIDOperator) \
    DPN_STATE_DIFF_LINE(keyIDVoting) \
    DPN_STATE_DIFF_LINE(addr) \
    DPN_STATE_DIFF_LINE(scriptPayout) \
    DPN_STATE_DIFF_LINE(scriptOperatorPayout)

public:
    uint32_t fields{0};
    // we reuse the state class, but only the members as noted by fields are valid
    CDeterministicPNState state;

public:
    CDeterministicPNStateDiff() {}
    CDeterministicPNStateDiff(const CDeterministicPNState& a, const CDeterministicPNState& b)
    {
#define DPN_STATE_DIFF_LINE(f) if (a.f != b.f) { state.f = b.f; fields |= Field_##f; }
        DPN_STATE_DIFF_ALL_FIELDS
#undef DPN_STATE_DIFF_LINE
    }

    SERIALIZE_METHODS(CDeterministicPNStateDiff, obj)
    {
        READWRITE(VARINT(obj.fields));
#define DPN_STATE_DIFF_LINE(f) if (obj.fields & Field_##f) READWRITE(obj.state.f);
        DPN_STATE_DIFF_ALL_FIELDS
#undef DPN_STATE_DIFF_LINE
    }

    void ApplyToState(CDeterministicPNState& target) const
    {
#define DPN_STATE_DIFF_LINE(f) if (fields & Field_##f) target.f = state.f;
        DPN_STATE_DIFF_ALL_FIELDS
#undef DPN_STATE_DIFF_LINE
    }
};

class CDeterministicPN
{
private:
    uint64_t internalId{std::numeric_limits<uint64_t>::max()};

public:
    CDeterministicPN() = delete; // no default constructor, must specify internalId
    CDeterministicPN(uint64_t _internalId) : internalId(_internalId)
    {
        // only non-initial values
        assert(_internalId != std::numeric_limits<uint64_t>::max());
    }
    // TODO: can be removed in a future version
    CDeterministicPN(const CDeterministicPN& mn, uint64_t _internalId) : CDeterministicPN(mn) {
        // only non-initial values
        assert(_internalId != std::numeric_limits<uint64_t>::max());
        internalId = _internalId;
    }

    template <typename Stream>
    CDeterministicPN(deserialize_type, Stream& s)
    {
        s >> *this;
    }

    uint256 proTxHash;
    COutPoint collateralOutpoint;
    uint16_t nOperatorReward;
    CDeterministicPNStateCPtr pdmnState;

public:
    SERIALIZE_METHODS(CDeterministicPN, obj)
    {
        READWRITE(obj.proTxHash);
        READWRITE(VARINT(obj.internalId));
        READWRITE(obj.collateralOutpoint);
        READWRITE(obj.nOperatorReward);
        READWRITE(obj.pdmnState);
    }

    uint64_t GetInternalId() const;

    std::string ToString() const;
    void ToJson(UniValue& obj) const;
};

typedef std::shared_ptr<const CDeterministicPN> CDeterministicPNCPtr;

class CDeterministicPNListDiff;

template <typename Stream, typename K, typename T, typename Hash, typename Equal>
void SerializeImmerMap(Stream& os, const immer::map<K, T, Hash, Equal>& m)
{
    WriteCompactSize(os, m.size());
    for (typename immer::map<K, T, Hash, Equal>::const_iterator mi = m.begin(); mi != m.end(); ++mi)
        Serialize(os, (*mi));
}

template <typename Stream, typename K, typename T, typename Hash, typename Equal>
void UnserializeImmerMap(Stream& is, immer::map<K, T, Hash, Equal>& m)
{
    m = immer::map<K, T, Hash, Equal>();
    unsigned int nSize = ReadCompactSize(is);
    for (unsigned int i = 0; i < nSize; i++) {
        std::pair<K, T> item;
        Unserialize(is, item);
        m = m.set(item.first, item.second);
    }
}

// For some reason the compiler is not able to choose the correct Serialize/Deserialize methods without a specialized
// version of SerReadWrite. It otherwise always chooses the version that calls a.Serialize()
template<typename Stream, typename K, typename T, typename Hash, typename Equal>
inline void SerReadWrite(Stream& s, const immer::map<K, T, Hash, Equal>& m, CSerActionSerialize ser_action)
{
    ::SerializeImmerMap(s, m);
}

template<typename Stream, typename K, typename T, typename Hash, typename Equal>
inline void SerReadWrite(Stream& s, immer::map<K, T, Hash, Equal>& obj, CSerActionUnserialize ser_action)
{
    ::UnserializeImmerMap(s, obj);
}

class CDeterministicPNList
{
public:
    typedef immer::map<uint256, CDeterministicPNCPtr> MnMap;
    typedef immer::map<uint64_t, uint256> MnInternalIdMap;
    typedef immer::map<uint256, std::pair<uint256, uint32_t> > MnUniquePropertyMap;

private:
    uint256 blockHash;
    int nHeight{-1};
    uint32_t nTotalRegisteredCount{0};
    MnMap mnMap;
    MnInternalIdMap mnInternalIdMap;

    // map of unique properties like address and keys
    // we keep track of this as checking for duplicates would otherwise be painfully slow
    MnUniquePropertyMap mnUniquePropertyMap;

public:
    CDeterministicPNList() {}
    explicit CDeterministicPNList(const uint256& _blockHash, int _height, uint32_t _totalRegisteredCount) :
        blockHash(_blockHash),
        nHeight(_height),
        nTotalRegisteredCount(_totalRegisteredCount)
    {
    }

    template<typename Stream>
    void Serialize(Stream& s) const
    {
        s << blockHash;
        s << nHeight;
        s << nTotalRegisteredCount;
        // Serialize the map as a vector
        WriteCompactSize(s, mnMap.size());
        for (const auto& p : mnMap) {
            s << *p.second;
        }
    }

    template<typename Stream>
    void Unserialize(Stream& s) {
        mnMap = MnMap();
        mnUniquePropertyMap = MnUniquePropertyMap();
        mnInternalIdMap = MnInternalIdMap();

        s >> blockHash;
        s >> nHeight;
        s >> nTotalRegisteredCount;
        size_t cnt = ReadCompactSize(s);
        for (size_t i = 0; i < cnt; i++) {
            AddPN(std::make_shared<CDeterministicPN>(deserialize, s), false);
        }
    }

public:
    size_t GetAllPNsCount() const
    {
        return mnMap.size();
    }

    size_t GetValidPNsCount() const
    {
        size_t count = 0;
        for (const auto& p : mnMap) {
            if (IsPNValid(p.second)) {
                count++;
            }
        }
        return count;
    }

    template <typename Callback>
    void ForEachPN(bool onlyValid, Callback&& cb) const
    {
        for (const auto& p : mnMap) {
            if (!onlyValid || IsPNValid(p.second)) {
                cb(p.second);
            }
        }
    }

public:
    const uint256& GetBlockHash() const      { return blockHash; }
    int GetHeight() const                    { return nHeight; }
    uint32_t GetTotalRegisteredCount() const { return nTotalRegisteredCount; }
    void SetHeight(int _height)                  { nHeight = _height; }
    void SetBlockHash(const uint256& _blockHash) { blockHash = _blockHash; }

    bool IsPNValid(const uint256& proTxHash) const;
    bool IsPNPoSeBanned(const uint256& proTxHash) const;
    bool IsPNValid(const CDeterministicPNCPtr& dmn) const;
    bool IsPNPoSeBanned(const CDeterministicPNCPtr& dmn) const;

    bool HasPN(const uint256& proTxHash) const
    {
        return GetPN(proTxHash) != nullptr;
    }
    bool HasPNByCollateral(const COutPoint& collateralOutpoint) const
    {
        return GetPNByCollateral(collateralOutpoint) != nullptr;
    }
    bool HasValidPNByCollateral(const COutPoint& collateralOutpoint) const
    {
        return GetValidPNByCollateral(collateralOutpoint) != nullptr;
    }
    CDeterministicPNCPtr GetPN(const uint256& proTxHash) const;
    CDeterministicPNCPtr GetValidPN(const uint256& proTxHash) const;
    CDeterministicPNCPtr GetPNByOperatorKey(const CKeyID& keyID);
    CDeterministicPNCPtr GetPNByCollateral(const COutPoint& collateralOutpoint) const;
    CDeterministicPNCPtr GetValidPNByCollateral(const COutPoint& collateralOutpoint) const;
    CDeterministicPNCPtr GetPNByService(const CService& service) const;
    CDeterministicPNCPtr GetPNByInternalId(uint64_t internalId) const;
    CDeterministicPNCPtr GetPNPayee() const;

    /**
     * Calculates the projected PN payees for the next *count* blocks. The result is not guaranteed to be correct
     * as PoSe banning might occur later
     * @param count
     * @return
     */
    std::vector<CDeterministicPNCPtr> GetProjectedPNPayees(unsigned int nCount) const;

    /**
     * Calculate a quorum based on the modifier. The resulting list is deterministically sorted by score
     * @param maxSize
     * @param modifier
     * @return
     */
    std::vector<CDeterministicPNCPtr> CalculateQuorum(size_t maxSize, const uint256& modifier) const;
    std::vector<std::pair<arith_uint256, CDeterministicPNCPtr>> CalculateScores(const uint256& modifier) const;

    /**
     * Calculates the maximum penalty which is allowed at the height of this PN list. It is dynamic and might change
     * for every block.
     * @return
     */
    int CalcMaxPoSePenalty() const;

    /**
     * Returns a the given percentage from the max penalty for this PN list. Always use this method to calculate the
     * value later passed to PoSePunish. The percentage should be high enough to take per-block penalty decreasing for PNs
     * into account. This means, if you want to accept 2 failures per payment cycle, you should choose a percentage that
     * is higher then 50%, e.g. 66%.
     * @param percent
     * @return
     */
    int CalcPenalty(int percent) const;

    /**
     * Punishes a PN for misbehavior. If the resulting penalty score of the PN reaches the max penalty, it is banned.
     * Penalty scores are only increased when the PN is not already banned, which means that after banning the penalty
     * might appear lower then the current max penalty, while the PN is still banned.
     * @param proTxHash
     * @param penalty
     */
    void PoSePunish(const uint256& proTxHash, int penalty, bool debugLogs);

    /**
     * Decrease penalty score of PN by 1.
     * Only allowed on non-banned PNs.
     * @param proTxHash
     */
    void PoSeDecrease(const uint256& proTxHash);

    CDeterministicPNListDiff BuildDiff(const CDeterministicPNList& to) const;
    CDeterministicPNList ApplyDiff(const CBlockIndex* pindex, const CDeterministicPNListDiff& diff) const;

    void AddPN(const CDeterministicPNCPtr& dmn, bool fBumpTotalCount = true);
    void UpdatePN(const CDeterministicPNCPtr& oldDmn, const CDeterministicPNStateCPtr& pdmnState);
    void UpdatePN(const uint256& proTxHash, const CDeterministicPNStateCPtr& pdmnState);
    void UpdatePN(const CDeterministicPNCPtr& oldDmn, const CDeterministicPNStateDiff& stateDiff);
    void RemovePN(const uint256& proTxHash);

    template <typename T>
    bool HasUniqueProperty(const T& v) const
    {
        return mnUniquePropertyMap.count(::SerializeHash(v)) != 0;
    }
    template <typename T>
    CDeterministicPNCPtr GetUniquePropertyPN(const T& v) const
    {
        auto p = mnUniquePropertyMap.find(::SerializeHash(v));
        if (!p) {
            return nullptr;
        }
        return GetPN(p->first);
    }

private:
    template <typename T>
    void AddUniqueProperty(const CDeterministicPNCPtr& dmn, const T& v)
    {
        static const T nullValue;
        assert(v != nullValue);

        auto hash = ::SerializeHash(v);
        auto oldEntry = mnUniquePropertyMap.find(hash);
        assert(!oldEntry || oldEntry->first == dmn->proTxHash);
        std::pair<uint256, uint32_t> newEntry(dmn->proTxHash, 1);
        if (oldEntry) {
            newEntry.second = oldEntry->second + 1;
        }
        mnUniquePropertyMap = mnUniquePropertyMap.set(hash, newEntry);
    }
    template <typename T>
    void DeleteUniqueProperty(const CDeterministicPNCPtr& dmn, const T& oldValue)
    {
        static const T nullValue;
        assert(oldValue != nullValue);

        auto oldHash = ::SerializeHash(oldValue);
        auto p = mnUniquePropertyMap.find(oldHash);
        assert(p && p->first == dmn->proTxHash);
        if (p->second == 1) {
            mnUniquePropertyMap = mnUniquePropertyMap.erase(oldHash);
        } else {
            mnUniquePropertyMap = mnUniquePropertyMap.set(oldHash, std::make_pair(dmn->proTxHash, p->second - 1));
        }
    }
    template <typename T>
    void UpdateUniqueProperty(const CDeterministicPNCPtr& dmn, const T& oldValue, const T& newValue)
    {
        if (oldValue == newValue) {
            return;
        }
        static const T nullValue;

        if (oldValue != nullValue) {
            DeleteUniqueProperty(dmn, oldValue);
        }

        if (newValue != nullValue) {
            AddUniqueProperty(dmn, newValue);
        }
    }
};

class CDeterministicPNListDiff
{
public:
    int nHeight{-1}; //memory only

    std::vector<CDeterministicPNCPtr> addedPNs;
    // keys are all relating to the internalId of PNs
    std::map<uint64_t, CDeterministicPNStateDiff> updatedPNs;
    std::set<uint64_t> removedMns;

public:
    template<typename Stream>
    void Serialize(Stream& s) const
    {
        s << addedPNs;
        WriteCompactSize(s, updatedPNs.size());
        for (const auto& p : updatedPNs) {
            s << VARINT(p.first);
            s << p.second;
        }
        WriteCompactSize(s, removedMns.size());
        for (const auto& p : removedMns) {
            s << VARINT(p);
        }
    }

    template<typename Stream>
    void Unserialize(Stream& s)
    {
        updatedPNs.clear();
        removedMns.clear();

        size_t tmp;
        uint64_t tmp2;
        s >> addedPNs;
        tmp = ReadCompactSize(s);
        for (size_t i = 0; i < tmp; i++) {
            CDeterministicPNStateDiff diff;
            s >> VARINT(tmp2);
            s >> diff;
            updatedPNs.emplace(tmp2, std::move(diff));
        }
        tmp = ReadCompactSize(s);
        for (size_t i = 0; i < tmp; i++) {
            s >> VARINT(tmp2);
            removedMns.emplace(tmp2);
        }
    }

public:
    bool HasChanges() const
    {
        return !addedPNs.empty() || !updatedPNs.empty() || !removedMns.empty();
    }
};

class CDeterministicPNManager
{
    static const int DISK_SNAPSHOT_PERIOD = 1440; // once per day
    static const int DISK_SNAPSHOTS = 3; // keep cache for 3 disk snapshots to have 2 full days covered
    static const int LIST_DIFFS_CACHE_SIZE = DISK_SNAPSHOT_PERIOD * DISK_SNAPSHOTS;

public:
    mutable RecursiveMutex cs;

private:
    CEvoDB& evoDb;

    std::unordered_map<uint256, CDeterministicPNList, StaticSaltedHasher> mnListsCache;
    std::unordered_map<uint256, CDeterministicPNListDiff, StaticSaltedHasher> mnListDiffsCache;
    const CBlockIndex* tipIndex{nullptr};

public:
    explicit CDeterministicPNManager(CEvoDB& _evoDb);

    bool ProcessBlock(const CBlock& block, const CBlockIndex* pindex, CValidationState& state, bool fJustCheck);
    bool UndoBlock(const CBlock& block, const CBlockIndex* pindex);

    void UpdatedBlockTip(const CBlockIndex* pindex);

    // the returned list will not contain the correct block hash (we can't know it yet as the coinbase TX is not updated yet)
    bool BuildNewListFromBlock(const CBlock& block, const CBlockIndex* pindexPrev, CValidationState& state, CDeterministicPNList& mnListRet, bool debugLogs);
    void DecreasePoSePenalties(CDeterministicPNList& mnList);

    // to return a valid list, it must have been built first, so never call it with a block not-yet connected (e.g. from CheckBlock).
    CDeterministicPNList GetListForBlock(const CBlockIndex* pindex);
    CDeterministicPNList GetListAtChainTip();

    // Whether DPNs are enforced at provided height, or at the chain-tip
    bool IsDIP3Enforced(int nHeight) const;
    bool IsDIP3Enforced() const;

    // Whether Legacy PNs are disabled at provided height, or at the chain-tip
    bool LegacyPNObsolete(int nHeight) const;
    bool LegacyPNObsolete() const;

private:
    void CleanupCache(int nHeight);
};

extern std::unique_ptr<CDeterministicPNManager> deterministicPNManager;

#endif //TrumpCoin_DETERMINISTICPNS_H
