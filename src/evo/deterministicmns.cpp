// Copyright (c) 2018-2021 The Dash Core developers
// Copyright (c) 2021 The TrumpCoin developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "evo/deterministicmns.h"

#include "chainparams.h"
#include "consensus/upgrades.h"
#include "core_io.h"
#include "evo/specialtx.h"
#include "key_io.h"
#include "guiinterface.h"
#include "patriotnode.h" // for PatriotnodeCollateralMinConf
#include "patriotnodeman.h" // for mnodeman (!TODO: remove)
#include "script/standard.h"
#include "spork.h"
#include "sync.h"
#include "validation.h"
#include "validationinterface.h"

#include <univalue.h>

static const std::string DB_LIST_SNAPSHOT = "dmn_S";
static const std::string DB_LIST_DIFF = "dmn_D";

std::unique_ptr<CDeterministicPNManager> deterministicPNManager;

std::string CDeterministicPNState::ToString() const
{
    CTxDestination dest;
    std::string payoutAddress = "unknown";
    std::string operatorPayoutAddress = "none";
    if (ExtractDestination(scriptPayout, dest)) {
        payoutAddress = EncodeDestination(dest);
    }
    if (ExtractDestination(scriptOperatorPayout, dest)) {
        operatorPayoutAddress = EncodeDestination(dest);
    }

    return strprintf("CDeterministicPNState(nRegisteredHeight=%d, nLastPaidHeight=%d, nPoSePenalty=%d, nPoSeRevivedHeight=%d, nPoSeBanHeight=%d, nRevocationReason=%d, ownerAddress=%s, operatorAddress=%s, votingAddress=%s, addr=%s, payoutAddress=%s, operatorPayoutAddress=%s)",
        nRegisteredHeight, nLastPaidHeight, nPoSePenalty, nPoSeRevivedHeight, nPoSeBanHeight, nRevocationReason,
        EncodeDestination(keyIDOwner), EncodeDestination(keyIDOperator), EncodeDestination(keyIDVoting), addr.ToStringIPPort(), payoutAddress, operatorPayoutAddress);
}

void CDeterministicPNState::ToJson(UniValue& obj) const
{
    obj.clear();
    obj.setObject();
    obj.pushKV("service", addr.ToStringIPPort());
    obj.pushKV("registeredHeight", nRegisteredHeight);
    obj.pushKV("lastPaidHeight", nLastPaidHeight);
    obj.pushKV("PoSePenalty", nPoSePenalty);
    obj.pushKV("PoSeRevivedHeight", nPoSeRevivedHeight);
    obj.pushKV("PoSeBanHeight", nPoSeBanHeight);
    obj.pushKV("revocationReason", nRevocationReason);
    obj.pushKV("ownerAddress", EncodeDestination(keyIDOwner));
    obj.pushKV("operatorAddress", keyIDOperator == CKeyID() ? "" : EncodeDestination(keyIDOperator));
    obj.pushKV("votingAddress", EncodeDestination(keyIDVoting));

    CTxDestination dest1;
    if (ExtractDestination(scriptPayout, dest1)) {
        obj.pushKV("payoutAddress", EncodeDestination(dest1));
    }
    CTxDestination dest2;
    if (ExtractDestination(scriptOperatorPayout, dest2)) {
        obj.pushKV("operatorPayoutAddress", EncodeDestination(dest2));
    }
}

uint64_t CDeterministicPN::GetInternalId() const
{
    // can't get it if it wasn't set yet
    assert(internalId != std::numeric_limits<uint64_t>::max());
    return internalId;
}

std::string CDeterministicPN::ToString() const
{
    return strprintf("CDeterministicPN(proTxHash=%s, collateralOutpoint=%s, nOperatorReward=%f, state=%s", proTxHash.ToString(), collateralOutpoint.ToStringShort(), (double)nOperatorReward / 100, pdmnState->ToString());
}

void CDeterministicPN::ToJson(UniValue& obj) const
{
    obj.clear();
    obj.setObject();

    UniValue stateObj;
    pdmnState->ToJson(stateObj);

    obj.pushKV("proTxHash", proTxHash.ToString());
    obj.pushKV("collateralHash", collateralOutpoint.hash.ToString());
    obj.pushKV("collateralIndex", (int)collateralOutpoint.n);

    std::string collateralAddressStr = "";
    Coin coin;
    if (GetUTXOCoin(collateralOutpoint, coin)) {
        CTxDestination dest;
        if (ExtractDestination(coin.out.scriptPubKey, dest)) {
            collateralAddressStr = EncodeDestination(dest);
        }
    }
    obj.pushKV("collateralAddress", collateralAddressStr);
    obj.pushKV("operatorReward", (double)nOperatorReward / 100);
    obj.pushKV("dmnstate", stateObj);
}

bool CDeterministicPNList::IsPNValid(const uint256& proTxHash) const
{
    auto p = mnMap.find(proTxHash);
    if (p == nullptr) {
        return false;
    }
    return IsPNValid(*p);
}

bool CDeterministicPNList::IsPNPoSeBanned(const uint256& proTxHash) const
{
    auto p = mnMap.find(proTxHash);
    if (p == nullptr) {
        return false;
    }
    return IsPNPoSeBanned(*p);
}

bool CDeterministicPNList::IsPNValid(const CDeterministicPNCPtr& dmn) const
{
    return !IsPNPoSeBanned(dmn);
}

bool CDeterministicPNList::IsPNPoSeBanned(const CDeterministicPNCPtr& dmn) const
{
    assert(dmn);
    const CDeterministicPNState& state = *dmn->pdmnState;
    return state.nPoSeBanHeight != -1;
}

CDeterministicPNCPtr CDeterministicPNList::GetPN(const uint256& proTxHash) const
{
    auto p = mnMap.find(proTxHash);
    if (p == nullptr) {
        return nullptr;
    }
    return *p;
}

CDeterministicPNCPtr CDeterministicPNList::GetValidPN(const uint256& proTxHash) const
{
    auto dmn = GetPN(proTxHash);
    if (dmn && !IsPNValid(dmn)) {
        return nullptr;
    }
    return dmn;
}

CDeterministicPNCPtr CDeterministicPNList::GetPNByOperatorKey(const CKeyID& keyID)
{
    for (const auto& p : mnMap) {
        if (p.second->pdmnState->keyIDOperator == keyID) {
            return p.second;
        }
    }
    return nullptr;
}

CDeterministicPNCPtr CDeterministicPNList::GetPNByCollateral(const COutPoint& collateralOutpoint) const
{
    return GetUniquePropertyPN(collateralOutpoint);
}

CDeterministicPNCPtr CDeterministicPNList::GetValidPNByCollateral(const COutPoint& collateralOutpoint) const
{
    auto dmn = GetPNByCollateral(collateralOutpoint);
    if (dmn && !IsPNValid(dmn)) {
        return nullptr;
    }
    return dmn;
}

CDeterministicPNCPtr CDeterministicPNList::GetPNByService(const CService& service) const
{
    return GetUniquePropertyPN(service);
}

CDeterministicPNCPtr CDeterministicPNList::GetPNByInternalId(uint64_t internalId) const
{
    auto proTxHash = mnInternalIdMap.find(internalId);
    if (!proTxHash) {
        return nullptr;
    }
    return GetPN(*proTxHash);
}

static int CompareByLastPaidGetHeight(const CDeterministicPN& dmn)
{
    int height = dmn.pdmnState->nLastPaidHeight;
    if (dmn.pdmnState->nPoSeRevivedHeight != -1 && dmn.pdmnState->nPoSeRevivedHeight > height) {
        height = dmn.pdmnState->nPoSeRevivedHeight;
    } else if (height == 0) {
        height = dmn.pdmnState->nRegisteredHeight;
    }
    return height;
}

static bool CompareByLastPaid(const CDeterministicPN& _a, const CDeterministicPN& _b)
{
    int ah = CompareByLastPaidGetHeight(_a);
    int bh = CompareByLastPaidGetHeight(_b);
    if (ah == bh) {
        return _a.proTxHash < _b.proTxHash;
    } else {
        return ah < bh;
    }
}
static bool CompareByLastPaid(const CDeterministicPNCPtr& _a, const CDeterministicPNCPtr& _b)
{
    return CompareByLastPaid(*_a, *_b);
}

CDeterministicPNCPtr CDeterministicPNList::GetPNPayee() const
{
    if (mnMap.size() == 0) {
        return nullptr;
    }

    CDeterministicPNCPtr best;
    ForEachPN(true, [&](const CDeterministicPNCPtr& dmn) {
        if (!best || CompareByLastPaid(dmn, best)) {
            best = dmn;
        }
    });

    return best;
}

std::vector<CDeterministicPNCPtr> CDeterministicPNList::GetProjectedPNPayees(unsigned int nCount) const
{
    if (nCount > GetValidPNsCount()) {
        nCount = GetValidPNsCount();
    }

    std::vector<CDeterministicPNCPtr> result;
    result.reserve(nCount);

    ForEachPN(true, [&](const CDeterministicPNCPtr& dmn) {
        result.emplace_back(dmn);
    });
    std::sort(result.begin(), result.end(), [&](const CDeterministicPNCPtr& a, const CDeterministicPNCPtr& b) {
        return CompareByLastPaid(a, b);
    });

    result.resize(nCount);

    return result;
}

std::vector<CDeterministicPNCPtr> CDeterministicPNList::CalculateQuorum(size_t maxSize, const uint256& modifier) const
{
    auto scores = CalculateScores(modifier);

    // sort is descending order
    std::sort(scores.rbegin(), scores.rend(), [](const std::pair<arith_uint256, CDeterministicPNCPtr>& a, std::pair<arith_uint256, CDeterministicPNCPtr>& b) {
        if (a.first == b.first) {
            // this should actually never happen, but we should stay compatible with how the non deterministic PNs did the sorting
            return a.second->collateralOutpoint < b.second->collateralOutpoint;
        }
        return a.first < b.first;
    });

    // take top maxSize entries and return it
    std::vector<CDeterministicPNCPtr> result;
    result.resize(std::min(maxSize, scores.size()));
    for (size_t i = 0; i < result.size(); i++) {
        result[i] = std::move(scores[i].second);
    }
    return result;
}

std::vector<std::pair<arith_uint256, CDeterministicPNCPtr>> CDeterministicPNList::CalculateScores(const uint256& modifier) const
{
    std::vector<std::pair<arith_uint256, CDeterministicPNCPtr>> scores;
    scores.reserve(GetAllPNsCount());
    ForEachPN(true, [&](const CDeterministicPNCPtr& dmn) {
        if (dmn->pdmnState->confirmedHash.IsNull()) {
            // we only take confirmed PNs into account to avoid hash grinding on the ProRegTxHash to sneak PNs into a
            // future quorums
            return;
        }
        // calculate sha256(sha256(proTxHash, confirmedHash), modifier) per PN
        // Please note that this is not a double-sha256 but a single-sha256
        // The first part is already precalculated (confirmedHashWithProRegTxHash)
        // TODO When https://github.com/bitcoin/bitcoin/pull/13191 gets backported, implement something that is similar but for single-sha256
        uint256 h;
        CSHA256 sha256;
        sha256.Write(dmn->pdmnState->confirmedHashWithProRegTxHash.begin(), dmn->pdmnState->confirmedHashWithProRegTxHash.size());
        sha256.Write(modifier.begin(), modifier.size());
        sha256.Finalize(h.begin());

        scores.emplace_back(UintToArith256(h), dmn);
    });

    return scores;
}

int CDeterministicPNList::CalcMaxPoSePenalty() const
{
    // Maximum PoSe penalty is dynamic and equals the number of registered PNs
    // It's however at least 100.
    // This means that the max penalty is usually equal to a full payment cycle
    return std::max(100, (int)GetAllPNsCount());
}

int CDeterministicPNList::CalcPenalty(int percent) const
{
    assert(percent > 0);
    return (CalcMaxPoSePenalty() * percent) / 100;
}

void CDeterministicPNList::PoSePunish(const uint256& proTxHash, int penalty, bool debugLogs)
{
    assert(penalty > 0);

    auto dmn = GetPN(proTxHash);
    if (!dmn) {
        throw(std::runtime_error(strprintf("%s: Can't find a patriotnode with proTxHash=%s", __func__, proTxHash.ToString())));
    }

    int maxPenalty = CalcMaxPoSePenalty();

    auto newState = std::make_shared<CDeterministicPNState>(*dmn->pdmnState);
    newState->nPoSePenalty += penalty;
    newState->nPoSePenalty = std::min(maxPenalty, newState->nPoSePenalty);

    if (debugLogs) {
        LogPrintf("CDeterministicPNList::%s -- punished PN %s, penalty %d->%d (max=%d)\n",
                  __func__, proTxHash.ToString(), dmn->pdmnState->nPoSePenalty, newState->nPoSePenalty, maxPenalty);
    }

    if (newState->nPoSePenalty >= maxPenalty && newState->nPoSeBanHeight == -1) {
        newState->nPoSeBanHeight = nHeight;
        if (debugLogs) {
            LogPrintf("CDeterministicPNList::%s -- banned PN %s at height %d\n",
                      __func__, proTxHash.ToString(), nHeight);
        }
    }
    UpdatePN(proTxHash, newState);
}

void CDeterministicPNList::PoSeDecrease(const uint256& proTxHash)
{
    auto dmn = GetPN(proTxHash);
    if (!dmn) {
        throw(std::runtime_error(strprintf("%s: Can't find a patriotnode with proTxHash=%s", __func__, proTxHash.ToString())));
    }
    assert(dmn->pdmnState->nPoSePenalty > 0 && dmn->pdmnState->nPoSeBanHeight == -1);

    auto newState = std::make_shared<CDeterministicPNState>(*dmn->pdmnState);
    newState->nPoSePenalty--;
    UpdatePN(proTxHash, newState);
}

CDeterministicPNListDiff CDeterministicPNList::BuildDiff(const CDeterministicPNList& to) const
{
    CDeterministicPNListDiff diffRet;

    to.ForEachPN(false, [&](const CDeterministicPNCPtr& toPtr) {
        auto fromPtr = GetPN(toPtr->proTxHash);
        if (fromPtr == nullptr) {
            diffRet.addedPNs.emplace_back(toPtr);
        } else if (fromPtr != toPtr || fromPtr->pdmnState != toPtr->pdmnState) {
            CDeterministicPNStateDiff stateDiff(*fromPtr->pdmnState, *toPtr->pdmnState);
            if (stateDiff.fields) {
                diffRet.updatedPNs.emplace(toPtr->GetInternalId(), std::move(stateDiff));
            }
        }
    });
    ForEachPN(false, [&](const CDeterministicPNCPtr& fromPtr) {
        auto toPtr = to.GetPN(fromPtr->proTxHash);
        if (toPtr == nullptr) {
            diffRet.removedMns.emplace(fromPtr->GetInternalId());
        }
    });

    // added PNs need to be sorted by internalId so that these are added in correct order when the diff is applied later
    // otherwise internalIds will not match with the original list
    std::sort(diffRet.addedPNs.begin(), diffRet.addedPNs.end(), [](const CDeterministicPNCPtr& a, const CDeterministicPNCPtr& b) {
        return a->GetInternalId() < b->GetInternalId();
    });

    return diffRet;
}

CDeterministicPNList CDeterministicPNList::ApplyDiff(const CBlockIndex* pindex, const CDeterministicPNListDiff& diff) const
{
    CDeterministicPNList result = *this;
    result.blockHash = pindex->GetBlockHash();
    result.nHeight = pindex->nHeight;

    for (const auto& id : diff.removedMns) {
        auto dmn = result.GetPNByInternalId(id);
        if (!dmn) {
            throw(std::runtime_error(strprintf("%s: can't find a removed patriotnode, id=%d", __func__, id)));
        }
        result.RemovePN(dmn->proTxHash);
    }
    for (const auto& dmn : diff.addedPNs) {
        result.AddPN(dmn);
    }
    for (const auto& p : diff.updatedPNs) {
        auto dmn = result.GetPNByInternalId(p.first);
        result.UpdatePN(dmn, p.second);
    }

    return result;
}

void CDeterministicPNList::AddPN(const CDeterministicPNCPtr& dmn, bool fBumpTotalCount)
{
    assert(dmn != nullptr);

    if (mnMap.find(dmn->proTxHash)) {
        throw(std::runtime_error(strprintf("%s: can't add a duplicate patriotnode with the same proTxHash=%s", __func__, dmn->proTxHash.ToString())));
    }
    if (mnInternalIdMap.find(dmn->GetInternalId())) {
        throw(std::runtime_error(strprintf("%s: can't add a duplicate patriotnode with the same internalId=%d", __func__, dmn->GetInternalId())));
    }
    if (HasUniqueProperty(dmn->pdmnState->addr)) {
        throw(std::runtime_error(strprintf("%s: can't add a patriotnode with a duplicate address %s", __func__, dmn->pdmnState->addr.ToStringIPPort())));
    }
    if (HasUniqueProperty(dmn->pdmnState->keyIDOwner) || HasUniqueProperty(dmn->pdmnState->keyIDOperator)) {
        throw(std::runtime_error(strprintf("%s: can't add a patriotnode with a duplicate key (%s or %s)", __func__, EncodeDestination(dmn->pdmnState->keyIDOwner), EncodeDestination(dmn->pdmnState->keyIDOperator))));
    }

    mnMap = mnMap.set(dmn->proTxHash, dmn);
    mnInternalIdMap = mnInternalIdMap.set(dmn->GetInternalId(), dmn->proTxHash);
    AddUniqueProperty(dmn, dmn->collateralOutpoint);
    if (dmn->pdmnState->addr != CService()) {
        AddUniqueProperty(dmn, dmn->pdmnState->addr);
    }
    AddUniqueProperty(dmn, dmn->pdmnState->keyIDOwner);
    AddUniqueProperty(dmn, dmn->pdmnState->keyIDOperator);

    if (fBumpTotalCount) {
        // nTotalRegisteredCount acts more like a checkpoint, not as a limit,
        nTotalRegisteredCount = std::max(dmn->GetInternalId() + 1, (uint64_t)nTotalRegisteredCount);
    }
}

void CDeterministicPNList::UpdatePN(const CDeterministicPNCPtr& oldDmn, const CDeterministicPNStateCPtr& pdmnState)
{
    assert(oldDmn != nullptr);

    if (HasUniqueProperty(oldDmn->pdmnState->addr) && GetUniquePropertyPN(oldDmn->pdmnState->addr)->proTxHash != oldDmn->proTxHash) {
        throw(std::runtime_error(strprintf("%s: can't update a patriotnode with a duplicate address %s", __func__, oldDmn->pdmnState->addr.ToStringIPPort())));
    }

    auto dmn = std::make_shared<CDeterministicPN>(*oldDmn);
    auto oldState = dmn->pdmnState;
    dmn->pdmnState = pdmnState;
    mnMap = mnMap.set(oldDmn->proTxHash, dmn);

    UpdateUniqueProperty(dmn, oldState->addr, pdmnState->addr);
    UpdateUniqueProperty(dmn, oldState->keyIDOwner, pdmnState->keyIDOwner);
    UpdateUniqueProperty(dmn, oldState->keyIDOperator, pdmnState->keyIDOperator);
}

void CDeterministicPNList::UpdatePN(const uint256& proTxHash, const CDeterministicPNStateCPtr& pdmnState)
{
    auto oldDmn = mnMap.find(proTxHash);
    if (!oldDmn) {
        throw(std::runtime_error(strprintf("%s: Can't find a patriotnode with proTxHash=%s", __func__, proTxHash.ToString())));
    }
    UpdatePN(*oldDmn, pdmnState);
}

void CDeterministicPNList::UpdatePN(const CDeterministicPNCPtr& oldDmn, const CDeterministicPNStateDiff& stateDiff)
{
    assert(oldDmn != nullptr);
    auto oldState = oldDmn->pdmnState;
    auto newState = std::make_shared<CDeterministicPNState>(*oldState);
    stateDiff.ApplyToState(*newState);
    UpdatePN(oldDmn, newState);
}

void CDeterministicPNList::RemovePN(const uint256& proTxHash)
{
    auto dmn = GetPN(proTxHash);
    if (!dmn) {
        throw(std::runtime_error(strprintf("%s: Can't find a patriotnode with proTxHash=%s", __func__, proTxHash.ToString())));
    }
    DeleteUniqueProperty(dmn, dmn->collateralOutpoint);
    if (dmn->pdmnState->addr != CService()) {
        DeleteUniqueProperty(dmn, dmn->pdmnState->addr);
    }
    DeleteUniqueProperty(dmn, dmn->pdmnState->keyIDOwner);
    DeleteUniqueProperty(dmn, dmn->pdmnState->keyIDOperator);

    mnMap = mnMap.erase(proTxHash);
    mnInternalIdMap = mnInternalIdMap.erase(dmn->GetInternalId());
}

CDeterministicPNManager::CDeterministicPNManager(CEvoDB& _evoDb) :
    evoDb(_evoDb)
{
}

bool CDeterministicPNManager::ProcessBlock(const CBlock& block, const CBlockIndex* pindex, CValidationState& _state, bool fJustCheck)
{
    int nHeight = pindex->nHeight;
    if (!IsDIP3Enforced(nHeight)) {
        // nothing to do
        return true;
    }

    CDeterministicPNList oldList, newList;
    CDeterministicPNListDiff diff;

    try {
        LOCK(cs);

        if (!BuildNewListFromBlock(block, pindex->pprev, _state, newList, true)) {
            // pass the state returned by the function above
            return false;
        }

        if (fJustCheck) {
            return true;
        }

        if (newList.GetHeight() == -1) {
            newList.SetHeight(nHeight);
        }

        newList.SetBlockHash(block.GetHash());

        oldList = GetListForBlock(pindex->pprev);
        diff = oldList.BuildDiff(newList);

        evoDb.Write(std::make_pair(DB_LIST_DIFF, newList.GetBlockHash()), diff);
        if ((nHeight % DISK_SNAPSHOT_PERIOD) == 0 || oldList.GetHeight() == -1) {
            evoDb.Write(std::make_pair(DB_LIST_SNAPSHOT, newList.GetBlockHash()), newList);
            mnListsCache.emplace(newList.GetBlockHash(), newList);
            LogPrintf("CDeterministicPNManager::%s -- Wrote snapshot. nHeight=%d, mapCurPNs.allPNsCount=%d\n",
                __func__, nHeight, newList.GetAllPNsCount());
        }

        diff.nHeight = pindex->nHeight;
        mnListDiffsCache.emplace(pindex->GetBlockHash(), diff);
    } catch (const std::exception& e) {
        LogPrintf("CDeterministicPNManager::%s -- internal error: %s\n", __func__, e.what());
        return _state.DoS(100, false, REJECT_INVALID, "failed-dmn-block");
    }

    // Don't hold cs while calling signals
    if (diff.HasChanges()) {
        GetMainSignals().NotifyPatriotnodeListChanged(false, oldList, diff);
        uiInterface.NotifyPatriotnodeListChanged(newList);
    }

    LOCK(cs);
    CleanupCache(nHeight);

    return true;
}

bool CDeterministicPNManager::UndoBlock(const CBlock& block, const CBlockIndex* pindex)
{
    if (!IsDIP3Enforced(pindex->nHeight)) {
        // nothing to do
        return true;
    }

    const uint256& blockHash = block.GetHash();

    CDeterministicPNList curList;
    CDeterministicPNList prevList;
    CDeterministicPNListDiff diff;
    {
        LOCK(cs);
        evoDb.Read(std::make_pair(DB_LIST_DIFF, blockHash), diff);

        if (diff.HasChanges()) {
            // need to call this before erasing
            curList = GetListForBlock(pindex);
            prevList = GetListForBlock(pindex->pprev);
        }

        mnListsCache.erase(blockHash);
        mnListDiffsCache.erase(blockHash);
    }

    if (diff.HasChanges()) {
        auto inversedDiff = curList.BuildDiff(prevList);
        GetMainSignals().NotifyPatriotnodeListChanged(true, curList, inversedDiff);
        uiInterface.NotifyPatriotnodeListChanged(prevList);
    }

    return true;
}

void CDeterministicPNManager::UpdatedBlockTip(const CBlockIndex* pindex)
{
    LOCK(cs);
    tipIndex = pindex;
}

bool CDeterministicPNManager::BuildNewListFromBlock(const CBlock& block, const CBlockIndex* pindexPrev, CValidationState& _state, CDeterministicPNList& mnListRet, bool debugLogs)
{
    AssertLockHeld(cs);

    int nHeight = pindexPrev->nHeight + 1;

    CDeterministicPNList oldList = GetListForBlock(pindexPrev);
    CDeterministicPNList newList = oldList;
    newList.SetBlockHash(UINT256_ZERO); // we can't know the final block hash, so better not return a (invalid) block hash
    newList.SetHeight(nHeight);

    auto payee = oldList.GetPNPayee();

    // we iterate the oldList here and update the newList
    // this is only valid as long these have not diverged at this point, which is the case as long as we don't add
    // code above this loop that modifies newList
    oldList.ForEachPN(false, [&](const CDeterministicPNCPtr& dmn) {
        if (!dmn->pdmnState->confirmedHash.IsNull()) {
            // already confirmed
            return;
        }
        // this works on the previous block, so confirmation will happen one block after nPatriotnodeMinimumConfirmations
        // has been reached, but the block hash will then point to the block at nPatriotnodeMinimumConfirmations
        int nConfirmations = pindexPrev->nHeight - dmn->pdmnState->nRegisteredHeight;
        if (nConfirmations >= PatriotnodeCollateralMinConf()) {
            auto newState = std::make_shared<CDeterministicPNState>(*dmn->pdmnState);
            newState->UpdateConfirmedHash(dmn->proTxHash, pindexPrev->GetBlockHash());
            newList.UpdatePN(dmn->proTxHash, newState);
        }
    });

    DecreasePoSePenalties(newList);

    // we skip the coinbase
    for (int i = 1; i < (int)block.vtx.size(); i++) {
        const CTransaction& tx = *block.vtx[i];

        if (tx.nType == CTransaction::TxType::PROREG) {
            ProRegPL pl;
            if (!GetTxPayload(tx, pl)) {
                return _state.DoS(100, false, REJECT_INVALID, "bad-protx-payload");
            }

            auto dmn = std::make_shared<CDeterministicPN>(newList.GetTotalRegisteredCount());
            dmn->proTxHash = tx.GetHash();

            // collateralOutpoint is either pointing to an external collateral or to the ProRegTx itself
            dmn->collateralOutpoint = pl.collateralOutpoint.hash.IsNull() ? COutPoint(tx.GetHash(), pl.collateralOutpoint.n)
                                                                          : pl.collateralOutpoint;

            // if the collateral outpoint appears in the legacy patriotnode list, remove the old node
            // !TODO: remove this when the transition to DPN is complete
            CPatriotnode* old_mn = mnodeman.Find(dmn->collateralOutpoint);
            if (old_mn) {
                old_mn->SetSpent();
                mnodeman.CheckAndRemove();
            }

            Coin coin;
            const CAmount collAmt = Params().GetConsensus().nPNCollateralAmt;
            if (!pl.collateralOutpoint.hash.IsNull() && (!GetUTXOCoin(pl.collateralOutpoint, coin) || coin.out.nValue != collAmt)) {
                // should actually never get to this point as CheckProRegTx should have handled this case.
                // We do this additional check nevertheless to be 100% sure
                return _state.DoS(100, false, REJECT_INVALID, "bad-protx-collateral");
            }

            auto replacedDmn = newList.GetPNByCollateral(dmn->collateralOutpoint);
            if (replacedDmn != nullptr) {
                // This might only happen with a ProRegTx that refers an external collateral
                // In that case the new ProRegTx will replace the old one. This means the old one is removed
                // and the new one is added like a completely fresh one, which is also at the bottom of the payment list
                newList.RemovePN(replacedDmn->proTxHash);
                if (debugLogs) {
                    LogPrintf("CDeterministicPNManager::%s -- PN %s removed from list because collateral was used for a new ProRegTx. collateralOutpoint=%s, nHeight=%d, mapCurPNs.allPNsCount=%d\n",
                              __func__, replacedDmn->proTxHash.ToString(), dmn->collateralOutpoint.ToStringShort(), nHeight, newList.GetAllPNsCount());
                }
            }

            if (newList.HasUniqueProperty(pl.addr)) {
                return _state.DoS(100, false, REJECT_DUPLICATE, "bad-protx-dup-IP-address");
            }
            if (newList.HasUniqueProperty(pl.keyIDOwner)) {
                return _state.DoS(100, false, REJECT_DUPLICATE, "bad-protx-dup-owner-key");
            }
            if (newList.HasUniqueProperty(pl.keyIDOperator)) {
                return _state.DoS(100, false, REJECT_DUPLICATE, "bad-protx-dup-operator-key");
            }

            dmn->nOperatorReward = pl.nOperatorReward;

            auto dmnState = std::make_shared<CDeterministicPNState>(pl);
            dmnState->nRegisteredHeight = nHeight;
            if (pl.addr == CService()) {
                // start in banned pdmnState as we need to wait for a ProUpServTx
                dmnState->nPoSeBanHeight = nHeight;
            }
            dmn->pdmnState = dmnState;

            newList.AddPN(dmn);

            if (debugLogs) {
                LogPrintf("CDeterministicPNManager::%s -- PN %s added at height %d: %s\n",
                    __func__, tx.GetHash().ToString(), nHeight, pl.ToString());
            }

        } else if (tx.nType == CTransaction::TxType::PROUPSERV) {
            ProUpServPL pl;
            if (!GetTxPayload(tx, pl)) {
                return _state.DoS(100, false, REJECT_INVALID, "bad-protx-payload");
            }

            if (newList.HasUniqueProperty(pl.addr) && newList.GetUniquePropertyPN(pl.addr)->proTxHash != pl.proTxHash) {
                return _state.DoS(100, false, REJECT_DUPLICATE, "bad-protx-dup-addr");
            }

            CDeterministicPNCPtr dmn = newList.GetPN(pl.proTxHash);
            if (!dmn) {
                return _state.DoS(100, false, REJECT_INVALID, "bad-protx-hash");
            }
            if (dmn->nOperatorReward == 0 && !pl.scriptOperatorPayout.empty()) {
                // operator payout address can not be set if the operator reward is 0
                return _state.DoS(100, false, REJECT_INVALID, "bad-protx-operator-payee");
            }
            auto newState = std::make_shared<CDeterministicPNState>(*dmn->pdmnState);
            newState->addr = pl.addr;
            newState->scriptOperatorPayout = pl.scriptOperatorPayout;

            if (newState->nPoSeBanHeight != -1) {
                // only revive when all keys are set
                if (!newState->keyIDOperator.IsNull() && !newState->keyIDVoting.IsNull() && !newState->keyIDOwner.IsNull()) {
                    newState->nPoSePenalty = 0;
                    newState->nPoSeBanHeight = -1;
                    newState->nPoSeRevivedHeight = nHeight;

                    if (debugLogs) {
                        LogPrintf("CDeterministicPNManager::%s -- PN %s revived at height %d\n",
                            __func__, pl.proTxHash.ToString(), nHeight);
                    }
                }
            }

            newList.UpdatePN(pl.proTxHash, newState);
            if (debugLogs) {
                LogPrintf("CDeterministicPNManager::%s -- PN %s updated at height %d: %s\n",
                    __func__, pl.proTxHash.ToString(), nHeight, pl.ToString());
            }

        } else if (tx.nType == CTransaction::TxType::PROUPREG) {
            ProUpRegPL pl;
            if (!GetTxPayload(tx, pl)) {
                return _state.DoS(100, false, REJECT_INVALID, "bad-protx-payload");
            }

            CDeterministicPNCPtr dmn = newList.GetPN(pl.proTxHash);
            if (!dmn) {
                return _state.DoS(100, false, REJECT_INVALID, "bad-protx-hash");
            }
            if (newList.HasUniqueProperty(pl.keyIDOperator) && newList.GetUniquePropertyPN(pl.keyIDOperator)->proTxHash != pl.proTxHash) {
                return _state.DoS(100, false, REJECT_DUPLICATE, "bad-protx-dup-operator-key");
            }
            auto newState = std::make_shared<CDeterministicPNState>(*dmn->pdmnState);
            if (newState->keyIDOperator != pl.keyIDOperator) {
                // reset all operator related fields and put PN into PoSe-banned state in case the operator key changes
                newState->ResetOperatorFields();
                newState->BanIfNotBanned(nHeight);
            }
            newState->keyIDOperator = pl.keyIDOperator;
            newState->keyIDVoting = pl.keyIDVoting;
            newState->scriptPayout = pl.scriptPayout;

            newList.UpdatePN(pl.proTxHash, newState);

            if (debugLogs) {
                LogPrintf("CDeterministicPNManager::%s -- PN %s updated at height %d: %s\n",
                    __func__, pl.proTxHash.ToString(), nHeight, pl.ToString());
            }

        } else if (tx.nType == CTransaction::TxType::PROUPREV) {
            ProUpRevPL pl;
            if (!GetTxPayload(tx, pl)) {
                return _state.DoS(100, false, REJECT_INVALID, "bad-protx-payload");
            }

            CDeterministicPNCPtr dmn = newList.GetPN(pl.proTxHash);
            if (!dmn) {
                return _state.DoS(100, false, REJECT_INVALID, "bad-protx-hash");
            }
            auto newState = std::make_shared<CDeterministicPNState>(*dmn->pdmnState);
            newState->ResetOperatorFields();
            newState->BanIfNotBanned(nHeight);
            newState->nRevocationReason = pl.nReason;

            newList.UpdatePN(pl.proTxHash, newState);

            if (debugLogs) {
                LogPrintf("CDeterministicPNManager::%s -- PN %s updated at height %d: %s\n",
                    __func__, pl.proTxHash.ToString(), nHeight, pl.ToString());
            }
        }

    }

    // check if any existing PN collateral is spent by this transaction
    // we skip the coinbase
    for (int i = 1; i < (int)block.vtx.size(); i++) {
        const CTransaction& tx = *block.vtx[i];
        for (const auto& in : tx.vin) {
            auto dmn = newList.GetPNByCollateral(in.prevout);
            if (dmn && dmn->collateralOutpoint == in.prevout) {
                newList.RemovePN(dmn->proTxHash);
                if (debugLogs) {
                    LogPrintf("CDeterministicPNManager::%s -- PN %s removed from list because collateral was spent. collateralOutpoint=%s, nHeight=%d, mapCurPNs.allPNsCount=%d\n",
                              __func__, dmn->proTxHash.ToString(), dmn->collateralOutpoint.ToStringShort(), nHeight, newList.GetAllPNsCount());
                }
            }
        }
    }

    // The payee for the current block was determined by the previous block's list but it might have disappeared in the
    // current block. We still pay that PN one last time however.
    if (payee && newList.HasPN(payee->proTxHash)) {
        auto newState = std::make_shared<CDeterministicPNState>(*newList.GetPN(payee->proTxHash)->pdmnState);
        newState->nLastPaidHeight = nHeight;
        newList.UpdatePN(payee->proTxHash, newState);
    }

    mnListRet = std::move(newList);

    return true;
}

void CDeterministicPNManager::DecreasePoSePenalties(CDeterministicPNList& mnList)
{
    std::vector<uint256> toDecrease;
    toDecrease.reserve(mnList.GetValidPNsCount() / 10);
    // only iterate and decrease for valid ones (not PoSe banned yet)
    // if a PN ever reaches the maximum, it stays in PoSe banned state until revived
    mnList.ForEachPN(true, [&](const CDeterministicPNCPtr& dmn) {
        if (dmn->pdmnState->nPoSePenalty > 0 && dmn->pdmnState->nPoSeBanHeight == -1) {
            toDecrease.emplace_back(dmn->proTxHash);
        }
    });

    for (const auto& proTxHash : toDecrease) {
        mnList.PoSeDecrease(proTxHash);
    }
}

CDeterministicPNList CDeterministicPNManager::GetListForBlock(const CBlockIndex* pindex)
{
    LOCK(cs);

    // Return early before enforcement
    if (!IsDIP3Enforced(pindex->nHeight)) {
        return {};
    }

    CDeterministicPNList snapshot;
    std::list<const CBlockIndex*> listDiffIndexes;

    while (true) {
        // try using cache before reading from disk
        auto itLists = mnListsCache.find(pindex->GetBlockHash());
        if (itLists != mnListsCache.end()) {
            snapshot = itLists->second;
            break;
        }

        if (evoDb.Read(std::make_pair(DB_LIST_SNAPSHOT, pindex->GetBlockHash()), snapshot)) {
            mnListsCache.emplace(pindex->GetBlockHash(), snapshot);
            break;
        }

        // no snapshot found yet, check diffs
        auto itDiffs = mnListDiffsCache.find(pindex->GetBlockHash());
        if (itDiffs != mnListDiffsCache.end()) {
            listDiffIndexes.emplace_front(pindex);
            pindex = pindex->pprev;
            continue;
        }

        CDeterministicPNListDiff diff;
        if (!evoDb.Read(std::make_pair(DB_LIST_DIFF, pindex->GetBlockHash()), diff)) {
            // no snapshot and no diff on disk means that it's initial snapshot (empty list)
            // If we get here, then this must be the block before the enforcement of DIP3.
            if (!IsActivationHeight(pindex->nHeight + 1, Params().GetConsensus(), Consensus::UPGRADE_V6_0)) {
                std::string err = strprintf("No patriotnode list data found for block %s at height %d. "
                                            "Possible corrupt database.", pindex->GetBlockHash().ToString(), pindex->nHeight);
                throw std::runtime_error(err);
            }
            snapshot = CDeterministicPNList(pindex->GetBlockHash(), -1, 0);
            mnListsCache.emplace(pindex->GetBlockHash(), snapshot);
            break;
        }

        diff.nHeight = pindex->nHeight;
        mnListDiffsCache.emplace(pindex->GetBlockHash(), std::move(diff));
        listDiffIndexes.emplace_front(pindex);
        pindex = pindex->pprev;
    }

    for (const auto& diffIndex : listDiffIndexes) {
        const auto& diff = mnListDiffsCache.at(diffIndex->GetBlockHash());
        if (diff.HasChanges()) {
            snapshot = snapshot.ApplyDiff(diffIndex, diff);
        } else {
            snapshot.SetBlockHash(diffIndex->GetBlockHash());
            snapshot.SetHeight(diffIndex->nHeight);
        }
    }

    if (tipIndex) {
        // always keep a snapshot for the tip
        if (snapshot.GetBlockHash() == tipIndex->GetBlockHash()) {
            mnListsCache.emplace(snapshot.GetBlockHash(), snapshot);
        } else {
            // !TODO: keep snapshots for yet alive quorums
        }
    }

    return snapshot;
}

CDeterministicPNList CDeterministicPNManager::GetListAtChainTip()
{
    LOCK(cs);
    if (!tipIndex) {
        return {};
    }
    return GetListForBlock(tipIndex);
}

bool CDeterministicPNManager::IsDIP3Enforced(int nHeight) const
{
    return Params().GetConsensus().NetworkUpgradeActive(nHeight, Consensus::UPGRADE_V6_0);
}

bool CDeterministicPNManager::IsDIP3Enforced() const
{
    int tipHeight = WITH_LOCK(cs, return tipIndex ? tipIndex->nHeight : -1;);
    return IsDIP3Enforced(tipHeight);
}

bool CDeterministicPNManager::LegacyPNObsolete(int nHeight) const
{
    return nHeight > sporkManager.GetSporkValue(SPORK_21_LEGACY_PNS_MAX_HEIGHT);
}

bool CDeterministicPNManager::LegacyPNObsolete() const
{
    int tipHeight = WITH_LOCK(cs, return tipIndex ? tipIndex->nHeight : -1;);
    return LegacyPNObsolete(tipHeight);
}

void CDeterministicPNManager::CleanupCache(int nHeight)
{
    AssertLockHeld(cs);

    std::vector<uint256> toDeleteLists;
    std::vector<uint256> toDeleteDiffs;
    for (const auto& p : mnListsCache) {
        if (p.second.GetHeight() + LIST_DIFFS_CACHE_SIZE < nHeight) {
            toDeleteLists.emplace_back(p.first);
            continue;
        }
        // !TODO: llmq cache cleanup
    }
    for (const auto& h : toDeleteLists) {
        mnListsCache.erase(h);
    }
    for (const auto& p : mnListDiffsCache) {
        if (p.second.nHeight + LIST_DIFFS_CACHE_SIZE < nHeight) {
            toDeleteDiffs.emplace_back(p.first);
        }
    }
    for (const auto& h : toDeleteDiffs) {
        mnListDiffsCache.erase(h);
    }
}
