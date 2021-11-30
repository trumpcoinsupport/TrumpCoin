// Copyright (c) 2014-2015 The Dash developers
// Copyright (c) 2015-2020 The TrumpCoin developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "patriotnode-payments.h"
#include "validation.h"
#include "chainparams.h"
#include "evo/deterministicmns.h"
#include "fs.h"
#include "budget/budgetmanager.h"
#include "patriotnode-sync.h"
#include "patriotnodeman.h"
#include "netmessagemaker.h"
#include "net_processing.h"
#include "spork.h"
#include "sync.h"
#include "util/system.h"
#include "utilmoneystr.h"


/** Object for who's going to get paid on which blocks */
CPatriotnodePayments patriotnodePayments;

RecursiveMutex cs_vecPayments;
RecursiveMutex cs_mapPatriotnodeBlocks;
RecursiveMutex cs_mapPatriotnodePayeeVotes;

static const int PNPAYMENTS_DB_VERSION = 1;

//
// CPatriotnodePaymentDB
//

CPatriotnodePaymentDB::CPatriotnodePaymentDB()
{
    pathDB = GetDataDir() / "mnpayments.dat";
    strMagicMessage = "PatriotnodePayments";
}

bool CPatriotnodePaymentDB::Write(const CPatriotnodePayments& objToSave)
{
    int64_t nStart = GetTimeMillis();

    // serialize, checksum data up to that point, then append checksum
    CDataStream ssObj(SER_DISK, CLIENT_VERSION);
    ssObj << PNPAYMENTS_DB_VERSION;
    ssObj << strMagicMessage;                   // patriotnode cache file specific magic message
    ssObj << Params().MessageStart(); // network specific magic number
    ssObj << objToSave;
    uint256 hash = Hash(ssObj.begin(), ssObj.end());
    ssObj << hash;

    // open output file, and associate with CAutoFile
    FILE* file = fsbridge::fopen(pathDB, "wb");
    CAutoFile fileout(file, SER_DISK, CLIENT_VERSION);
    if (fileout.IsNull())
        return error("%s : Failed to open file %s", __func__, pathDB.string());

    // Write and commit header, data
    try {
        fileout << ssObj;
    } catch (const std::exception& e) {
        return error("%s : Serialize or I/O error - %s", __func__, e.what());
    }
    fileout.fclose();

    LogPrint(BCLog::PATRIOTNODE,"Written info to mnpayments.dat  %dms\n", GetTimeMillis() - nStart);

    return true;
}

CPatriotnodePaymentDB::ReadResult CPatriotnodePaymentDB::Read(CPatriotnodePayments& objToLoad)
{
    int64_t nStart = GetTimeMillis();
    // open input file, and associate with CAutoFile
    FILE* file = fsbridge::fopen(pathDB, "rb");
    CAutoFile filein(file, SER_DISK, CLIENT_VERSION);
    if (filein.IsNull()) {
        error("%s : Failed to open file %s", __func__, pathDB.string());
        return FileError;
    }

    // use file size to size memory buffer
    int fileSize = fs::file_size(pathDB);
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

    CDataStream ssObj(vchData, SER_DISK, CLIENT_VERSION);

    // verify stored checksum matches input data
    uint256 hashTmp = Hash(ssObj.begin(), ssObj.end());
    if (hashIn != hashTmp) {
        error("%s : Checksum mismatch, data corrupted", __func__);
        return IncorrectHash;
    }

    int version;
    std::string strMagicMessageTmp;
    try {
        // de-serialize file header
        ssObj >> version;
        ssObj >> strMagicMessageTmp;

        // ... verify the message matches predefined one
        if (strMagicMessage != strMagicMessageTmp) {
            error("%s : Invalid patriotnode payement cache magic message", __func__);
            return IncorrectMagicMessage;
        }

        // de-serialize file header (network specific magic number) and ..
        std::vector<unsigned char> pchMsgTmp(4);
        ssObj >> MakeSpan(pchMsgTmp);

        // ... verify the network matches ours
        if (memcmp(pchMsgTmp.data(), Params().MessageStart(), pchMsgTmp.size()) != 0) {
            error("%s : Invalid network magic number", __func__);
            return IncorrectMagicNumber;
        }

        // de-serialize data into CPatriotnodePayments object
        ssObj >> objToLoad;
    } catch (const std::exception& e) {
        objToLoad.Clear();
        error("%s : Deserialize or I/O error - %s", __func__, e.what());
        return IncorrectFormat;
    }

    LogPrint(BCLog::PATRIOTNODE,"Loaded info from mnpayments.dat (dbversion=%d) %dms\n", version, GetTimeMillis() - nStart);
    LogPrint(BCLog::PATRIOTNODE,"  %s\n", objToLoad.ToString());

    return Ok;
}

uint256 CPatriotnodePaymentWinner::GetHash() const
{
    CHashWriter ss(SER_GETHASH, PROTOCOL_VERSION);
    ss << std::vector<unsigned char>(payee.begin(), payee.end());
    ss << nBlockHeight;
    ss << vinPatriotnode.prevout;
    return ss.GetHash();
}

std::string CPatriotnodePaymentWinner::GetStrMessage() const
{
    return vinPatriotnode.prevout.ToStringShort() + std::to_string(nBlockHeight) + HexStr(payee);
}

bool CPatriotnodePaymentWinner::IsValid(CNode* pnode, std::string& strError, int chainHeight)
{
    int n = mnodeman.GetPatriotnodeRank(vinPatriotnode, nBlockHeight - 100);
    bool guard = Params().GetConsensus().NetworkUpgradeActive(chainHeight, Consensus::UPGRADE_V5_3);
    if ((guard && n < 1) || n > PNPAYMENTS_SIGNATURES_TOTAL) {
        //It's common to have patriotnodes mistakenly think they are in the top 10
        // We don't want to print all of these messages, or punish them unless they're way off
        if (n > PNPAYMENTS_SIGNATURES_TOTAL * 2) {
            strError = strprintf("Patriotnode not in the top %d (%d)", PNPAYMENTS_SIGNATURES_TOTAL * 2, n);
            LogPrint(BCLog::PATRIOTNODE,"CPatriotnodePaymentWinner::IsValid - %s\n", strError);
            //if (patriotnodeSync.IsSynced()) Misbehaving(pnode->GetId(), 20);
        }
        return false;
    }

    // Must be a P2PKH
    if (guard && !payee.IsPayToPublicKeyHash()) {
        LogPrint(BCLog::PATRIOTNODE, "%s - payee must be a P2PKH\n", __func__);
        return false;
    }

    return true;
}

void CPatriotnodePaymentWinner::Relay()
{
    CInv inv(MSG_PATRIOTNODE_WINNER, GetHash());
    g_connman->RelayInv(inv);
}

void DumpPatriotnodePayments()
{
    int64_t nStart = GetTimeMillis();

    CPatriotnodePaymentDB paymentdb;
    LogPrint(BCLog::PATRIOTNODE,"Writing info to mnpayments.dat...\n");
    paymentdb.Write(patriotnodePayments);

    LogPrint(BCLog::PATRIOTNODE,"Budget dump finished  %dms\n", GetTimeMillis() - nStart);
}

bool IsBlockValueValid(int nHeight, CAmount& nExpectedValue, CAmount nMinted, CAmount& nBudgetAmt)
{
    const Consensus::Params& consensus = Params().GetConsensus();
    if (!patriotnodeSync.IsSynced()) {
        //there is no budget data to use to check anything
        //super blocks will always be on these blocks, max 100 per budgeting
        if (nHeight % consensus.nBudgetCycleBlocks < 100) {
            if (Params().IsTestnet()) {
                return true;
            }
            nExpectedValue += g_budgetman.GetTotalBudget(nHeight);
        }
    } else {
        // we're synced and have data so check the budget schedule
        // if the superblock spork is enabled
        if (sporkManager.IsSporkActive(SPORK_13_ENABLE_SUPERBLOCKS)) {
            // add current payee amount to the expected block value
            if (g_budgetman.GetExpectedPayeeAmount(nHeight, nBudgetAmt)) {
                nExpectedValue += nBudgetAmt;
            }
        }
    }

    // !todo: remove after V5.3 enforcement and default it to true
    const bool isUpgradeEnforced = consensus.NetworkUpgradeActive(nHeight, Consensus::UPGRADE_V5_3);
    return (!isUpgradeEnforced || nMinted >= 0) && nMinted <= nExpectedValue;
}

bool IsBlockPayeeValid(const CBlock& block, const CBlockIndex* pindexPrev, int nHeight)
{
    int nBlockHeight = pindexPrev->nHeight + 1;
    TrxValidationStatus transactionStatus = TrxValidationStatus::InValid;

    if (!patriotnodeSync.IsSynced()) { //there is no budget data to use to check anything -- find the longest chain
        LogPrint(BCLog::PATRIOTNODE, "Client not synced, skipping block payee checks\n");
        return true;
    }

    const bool fPayCoinstake = Params().GetConsensus().NetworkUpgradeActive(nBlockHeight, Consensus::UPGRADE_POS) &&
                               !Params().GetConsensus().NetworkUpgradeActive(nBlockHeight, Consensus::UPGRADE_V6_0);
    const CTransaction& txNew = *(fPayCoinstake ? block.vtx[1] : block.vtx[0]);

    //check if it's a budget block
    if (sporkManager.IsSporkActive(SPORK_13_ENABLE_SUPERBLOCKS)) {
        if (g_budgetman.IsBudgetPaymentBlock(nBlockHeight)) {
            transactionStatus = g_budgetman.IsTransactionValid(txNew, block.GetHash(), nBlockHeight);
            if (transactionStatus == TrxValidationStatus::Valid) {
                return true;
            }

            if (transactionStatus == TrxValidationStatus::InValid) {
                LogPrint(BCLog::PATRIOTNODE,"Invalid budget payment detected %s\n", txNew.ToString().c_str());
                if (sporkManager.IsSporkActive(SPORK_9_PATRIOTNODE_BUDGET_ENFORCEMENT))
                    return false;

                LogPrint(BCLog::PATRIOTNODE,"Budget enforcement is disabled, accepting block\n");
            }
        }
    }

    // If we end here the transaction was either TrxValidationStatus::InValid and Budget enforcement is disabled, or
    // a double budget payment (status = TrxValidationStatus::DoublePayment) was detected, or no/not enough patriotnode
    // votes (status = TrxValidationStatus::VoteThreshold) for a finalized budget were found
    // In all cases a patriotnode will get the payment for this block

    //check for patriotnode payee
    if (patriotnodePayments.IsTransactionValid(txNew, pindexPrev, nHeight))
        return true;
    LogPrint(BCLog::PATRIOTNODE,"Invalid mn payment detected %s\n", txNew.ToString().c_str());

    if (sporkManager.IsSporkActive(SPORK_8_PATRIOTNODE_PAYMENT_ENFORCEMENT))
        return false;
    LogPrint(BCLog::PATRIOTNODE,"Patriotnode payment enforcement is disabled, accepting block\n");
    return true;
}


void FillBlockPayee(CMutableTransaction& txCoinbase, CMutableTransaction& txCoinstake, const CBlockIndex* pindexPrev, bool fProofOfStake)
{
    if (!sporkManager.IsSporkActive(SPORK_13_ENABLE_SUPERBLOCKS) ||           // if superblocks are not enabled
            // ... or this is not a superblock
            !g_budgetman.FillBlockPayee(txCoinbase, txCoinstake, pindexPrev->nHeight + 1, fProofOfStake) ) {
        // ... or there's no budget with enough votes, then pay a patriotnode
        patriotnodePayments.FillBlockPayee(txCoinbase, txCoinstake, pindexPrev, fProofOfStake);
    }
}

std::string GetRequiredPaymentsString(int nBlockHeight)
{
    if (sporkManager.IsSporkActive(SPORK_13_ENABLE_SUPERBLOCKS) && g_budgetman.IsBudgetPaymentBlock(nBlockHeight)) {
        return g_budgetman.GetRequiredPaymentsString(nBlockHeight);
    } else {
        return patriotnodePayments.GetRequiredPaymentsString(nBlockHeight);
    }
}

bool CPatriotnodePayments::GetPatriotnodeTxOuts(const CBlockIndex* pindexPrev, std::vector<CTxOut>& voutPatriotnodePaymentsRet) const
{


    // Legacy payment logic. !TODO: remove when transition to DPN is complete
    return GetLegacyPatriotnodeTxOut(pindexPrev->nHeight + 1, voutPatriotnodePaymentsRet);
}

bool CPatriotnodePayments::GetLegacyPatriotnodeTxOut(int nHeight, std::vector<CTxOut>& voutPatriotnodePaymentsRet) const
{
    voutPatriotnodePaymentsRet.clear();

    CScript payee;
    if (!GetBlockPayee(nHeight, payee)) {
        //no patriotnode detected
        const Consensus::Params& consensus = Params().GetConsensus();
        const uint256& hash = consensus.NetworkUpgradeActive(nHeight, Consensus::UPGRADE_V5_3) ?
                              mnodeman.GetHashAtHeight(nHeight - 1) : consensus.hashGenesisBlock;
        PatriotnodeRef winningNode = mnodeman.GetCurrentPatriotNode(hash);
        if (winningNode) {
            payee = winningNode->GetPayeeScript();
        } else {
            LogPrint(BCLog::PATRIOTNODE,"CreateNewBlock: Failed to detect patriotnode to pay\n");
            return false;
        }
    }
    voutPatriotnodePaymentsRet.emplace_back(GetPatriotnodePayment(nHeight), payee);
    return true;
}

static void SubtractMnPaymentFromCoinstake(CMutableTransaction& txCoinstake, CAmount patriotnodePayment, int stakerOuts)
{
    assert (stakerOuts >= 2);
    //subtract mn payment from the stake reward
    if (stakerOuts == 2) {
        // Majority of cases; do it quick and move on
        txCoinstake.vout[1].nValue -= patriotnodePayment;
    } else {
        // special case, stake is split between (stakerOuts-1) outputs
        unsigned int outputs = stakerOuts-1;
        CAmount mnPaymentSplit = patriotnodePayment / outputs;
        CAmount mnPaymentRemainder = patriotnodePayment - (mnPaymentSplit * outputs);
        for (unsigned int j=1; j<=outputs; j++) {
            txCoinstake.vout[j].nValue -= mnPaymentSplit;
        }
        // in case it's not an even division, take the last bit of dust from the last one
        txCoinstake.vout[outputs].nValue -= mnPaymentRemainder;
    }
}

void CPatriotnodePayments::FillBlockPayee(CMutableTransaction& txCoinbase, CMutableTransaction& txCoinstake, const CBlockIndex* pindexPrev, bool fProofOfStake) const
{
    std::vector<CTxOut> vecMnOuts;
    if (!GetPatriotnodeTxOuts(pindexPrev, vecMnOuts)) {
        return;
    }

    // Starting from TrumpCoin v6.0 patriotnode and budgets are paid in the coinbase tx
    const int nHeight = pindexPrev->nHeight + 1;
    bool fPayCoinstake = fProofOfStake && !Params().GetConsensus().NetworkUpgradeActive(nHeight, Consensus::UPGRADE_V6_0);

    // if PoS block pays the coinbase, clear it first
    if (fProofOfStake && !fPayCoinstake) txCoinbase.vout.clear();

    const int initial_cstake_outs = txCoinstake.vout.size();

    CAmount patriotnodePayment{0};
    for (const CTxOut& mnOut: vecMnOuts) {
        // Add the mn payment to the coinstake/coinbase tx
        if (fPayCoinstake) {
            txCoinstake.vout.emplace_back(mnOut);
        } else {
            txCoinbase.vout.emplace_back(mnOut);
        }
        patriotnodePayment += mnOut.nValue;
        CTxDestination payeeDest;
        ExtractDestination(mnOut.scriptPubKey, payeeDest);
        LogPrint(BCLog::PATRIOTNODE,"Patriotnode payment of %s to %s\n", FormatMoney(mnOut.nValue), EncodeDestination(payeeDest));
    }

    // Subtract mn payment value from the block reward
    if (fProofOfStake) {
        SubtractMnPaymentFromCoinstake(txCoinstake, patriotnodePayment, initial_cstake_outs);
    } else {
        txCoinbase.vout[0].nValue = rewward(nHeight) - patriotnodePayment;
    }
}

void CPatriotnodePayments::ProcessMessagePatriotnodePayments(CNode* pfrom, std::string& strCommand, CDataStream& vRecv)
{
    if (!patriotnodeSync.IsBlockchainSynced()) return;

    if (fLiteMode) return; //disable all Patriotnode related functionality

    // Skip after legacy obsolete. !TODO: remove when transition to DPN is complete
    if (deterministicPNManager->LegacyPNObsolete()) {
        LogPrint(BCLog::PATRIOTNODE, "mnw - skip obsolete message %s\n", strCommand);
        return;
    }


    if (strCommand == NetMsgType::GETPNWINNERS) { //Patriotnode Payments Request Sync
        if (fLiteMode) return;   //disable all Patriotnode related functionality

        int nCountNeeded;
        vRecv >> nCountNeeded;

        if (Params().NetworkIDString() == CBaseChainParams::MAIN) {
            if (pfrom->HasFulfilledRequest(NetMsgType::GETPNWINNERS)) {
                LogPrintf("CPatriotnodePayments::ProcessMessagePatriotnodePayments() : mnget - peer already asked me for the list\n");
                LOCK(cs_main);
                Misbehaving(pfrom->GetId(), 20);
                return;
            }
        }

        pfrom->FulfilledRequest(NetMsgType::GETPNWINNERS);
        patriotnodePayments.Sync(pfrom, nCountNeeded);
        LogPrint(BCLog::PATRIOTNODE, "mnget - Sent Patriotnode winners to peer %i\n", pfrom->GetId());
    } else if (strCommand == NetMsgType::PNWINNER) { //Patriotnode Payments Declare Winner
        //this is required in litemodef
        CPatriotnodePaymentWinner winner;
        vRecv >> winner;

        if (pfrom->nVersion < ActiveProtocol()) return;

        {
            // Clear inv request
            LOCK(cs_main);
            g_connman->RemoveAskFor(winner.GetHash(), MSG_PATRIOTNODE_WINNER);
        }

        int nHeight = mnodeman.GetBestHeight();

        if (patriotnodePayments.mapPatriotnodePayeeVotes.count(winner.GetHash())) {
            LogPrint(BCLog::PATRIOTNODE, "mnw - Already seen - %s bestHeight %d\n", winner.GetHash().ToString().c_str(), nHeight);
            patriotnodeSync.AddedPatriotnodeWinner(winner.GetHash());
            return;
        }

        int nFirstBlock = nHeight - (mnodeman.CountEnabled() * 1.25);
        if (winner.nBlockHeight < nFirstBlock || winner.nBlockHeight > nHeight + 20) {
            LogPrint(BCLog::PATRIOTNODE, "mnw - winner out of range - FirstBlock %d Height %d bestHeight %d\n", nFirstBlock, winner.nBlockHeight, nHeight);
            return;
        }

        // reject old signature version
        if (winner.nMessVersion != MessageVersion::MESS_VER_HASH) {
            LogPrint(BCLog::PATRIOTNODE, "mnw - rejecting old message version %d\n", winner.nMessVersion);
            return;
        }

        std::string strError = "";
        if (!winner.IsValid(pfrom, strError, nHeight)) {
            // if(strError != "") LogPrint(BCLog::PATRIOTNODE,"mnw - invalid message - %s\n", strError);
            return;
        }

        if (!patriotnodePayments.CanVote(winner.vinPatriotnode.prevout, winner.nBlockHeight)) {
            //  LogPrint(BCLog::PATRIOTNODE,"mnw - patriotnode already voted - %s\n", winner.vinPatriotnode.prevout.ToStringShort());
            return;
        }

        // See if this winner was signed with a dmn or a legacy patriotnode
        bool fDeterministic{false};
        Optional<CKeyID> mnKeyID = nullopt;
        auto mnList = deterministicPNManager->GetListAtChainTip();
        auto dmn = mnList.GetPNByCollateral(winner.vinPatriotnode.prevout);
        if (dmn) {
            fDeterministic = true;
            mnKeyID = Optional<CKeyID>(dmn->pdmnState->keyIDOperator);
        } else {
            const CPatriotnode* pmn = mnodeman.Find(winner.vinPatriotnode.prevout);
            if (pmn) {
                mnKeyID = Optional<CKeyID>(pmn->pubKeyPatriotnode.GetID());
            }
        }

        // Check not found PN
        if (mnKeyID == nullopt) {
            // If it's a DPN, the node is misbehaving.
            if (fDeterministic) {
                LOCK(cs_main);
                Misbehaving(pfrom->GetId(), 20);
            } else {
                // If it's not a DPN, then it could be a non-synced PN.
                // Let's try to request the PN to the node.
                // (the AskForPN() will only broadcast it every 10 min).

                // But, only ask for missing items after the initial syncing process is complete
                //   otherwise will think a full sync succeeded when they return a result
                if (pfrom && patriotnodeSync.IsSynced()) mnodeman.AskForPN(pfrom, winner.vinPatriotnode);
            }
            return;
        }

        if (!winner.CheckSignature(*mnKeyID)) {
            LogPrint(BCLog::PATRIOTNODE, "%s : mnw - invalid signature\n", __func__);
            LOCK(cs_main);
            Misbehaving(pfrom->GetId(), 20);
            return;
        }

        if (patriotnodePayments.AddWinningPatriotnode(winner)) {
            // Relay only if we are synchronized.
            // Makes no sense to relay PNWinners to the peers from where we are syncing them.
            if (patriotnodeSync.IsSynced()) winner.Relay();
            patriotnodeSync.AddedPatriotnodeWinner(winner.GetHash());
        }
    }
}

bool CPatriotnodePayments::GetBlockPayee(int nBlockHeight, CScript& payee) const
{
    const auto it = mapPatriotnodeBlocks.find(nBlockHeight);
    if (it != mapPatriotnodeBlocks.end()) {
        return it->second.GetPayee(payee);
    }

    return false;
}

// Is this patriotnode scheduled to get paid soon?
// -- Only look ahead up to 8 blocks to allow for propagation of the latest 2 winners
bool CPatriotnodePayments::IsScheduled(const CPatriotnode& mn, int nNotBlockHeight)
{
    LOCK(cs_mapPatriotnodeBlocks);

    int nHeight = mnodeman.GetBestHeight();

    const CScript& mnpayee = mn.GetPayeeScript();
    CScript payee;
    for (int64_t h = nHeight; h <= nHeight + 8; h++) {
        if (h == nNotBlockHeight) continue;
        if (mapPatriotnodeBlocks.count(h)) {
            if (mapPatriotnodeBlocks[h].GetPayee(payee)) {
                if (mnpayee == payee) {
                    return true;
                }
            }
        }
    }

    return false;
}

bool CPatriotnodePayments::AddWinningPatriotnode(CPatriotnodePaymentWinner& winnerIn)
{
    // check winner height
    if (winnerIn.nBlockHeight - 100 > mnodeman.GetBestHeight() + 1) {
        return error("%s: mnw - invalid height %d > %d", __func__, winnerIn.nBlockHeight - 100, mnodeman.GetBestHeight() + 1);
    }

    {
        LOCK2(cs_mapPatriotnodePayeeVotes, cs_mapPatriotnodeBlocks);

        if (mapPatriotnodePayeeVotes.count(winnerIn.GetHash())) {
            return false;
        }

        mapPatriotnodePayeeVotes[winnerIn.GetHash()] = winnerIn;

        if (!mapPatriotnodeBlocks.count(winnerIn.nBlockHeight)) {
            CPatriotnodeBlockPayees blockPayees(winnerIn.nBlockHeight);
            mapPatriotnodeBlocks[winnerIn.nBlockHeight] = blockPayees;
        }
    }

    CTxDestination addr;
    ExtractDestination(winnerIn.payee, addr);
    LogPrint(BCLog::PATRIOTNODE, "mnw - Adding winner %s for block %d\n", EncodeDestination(addr), winnerIn.nBlockHeight);
    mapPatriotnodeBlocks[winnerIn.nBlockHeight].AddPayee(winnerIn.payee, 1);

    return true;
}

bool CPatriotnodeBlockPayees::IsTransactionValid(const CTransaction& txNew, int nHeight)
{
    LOCK(cs_vecPayments);

    //require at least 6 signatures
    int nMaxSignatures = 0;
    for (CPatriotnodePayee& payee : vecPayments)
        if (payee.nVotes >= nMaxSignatures && payee.nVotes >= PNPAYMENTS_SIGNATURES_REQUIRED)
            nMaxSignatures = payee.nVotes;

    // if we don't have at least 6 signatures on a payee, approve whichever is the longest chain
    if (nMaxSignatures < PNPAYMENTS_SIGNATURES_REQUIRED) return true;

    std::string strPayeesPossible = "";
    CAmount requiredPatriotnodePayment = GetPatriotnodePayment(nHeight);

    for (CPatriotnodePayee& payee : vecPayments) {
        bool found = false;
        for (CTxOut out : txNew.vout) {
            if (payee.scriptPubKey == out.scriptPubKey) {
                if(out.nValue == requiredPatriotnodePayment)
                    found = true;
                else
                    LogPrintf("%s : Patriotnode payment value (%s) different from required value (%s).\n",
                            __func__, FormatMoney(out.nValue).c_str(), FormatMoney(requiredPatriotnodePayment).c_str());
            }
        }

        if (payee.nVotes >= PNPAYMENTS_SIGNATURES_REQUIRED) {
            if (found) return true;

            CTxDestination address1;
            ExtractDestination(payee.scriptPubKey, address1);

            if (strPayeesPossible != "")
                strPayeesPossible += ",";

            strPayeesPossible += EncodeDestination(address1);
        }
    }

    LogPrint(BCLog::PATRIOTNODE,"CPatriotnodePayments::IsTransactionValid - Missing required payment of %s to %s\n", FormatMoney(requiredPatriotnodePayment).c_str(), strPayeesPossible.c_str());
    return false;
}

std::string CPatriotnodeBlockPayees::GetRequiredPaymentsString()
{
    LOCK(cs_vecPayments);

    std::string ret = "";

    for (CPatriotnodePayee& payee : vecPayments) {
        CTxDestination address1;
        ExtractDestination(payee.scriptPubKey, address1);
        if (ret != "") {
            ret += ", ";
        }
        ret += EncodeDestination(address1) + ":" + std::to_string(payee.nVotes);
    }

    return ret.empty() ? "Unknown" : ret;
}

std::string CPatriotnodePayments::GetRequiredPaymentsString(int nBlockHeight)
{
    LOCK(cs_mapPatriotnodeBlocks);

    if (mapPatriotnodeBlocks.count(nBlockHeight)) {
        return mapPatriotnodeBlocks[nBlockHeight].GetRequiredPaymentsString();
    }

    return "Unknown";
}

bool CPatriotnodePayments::IsTransactionValid(const CTransaction& txNew, const CBlockIndex* pindexPrev, int nHeight)
{
    const int nBlockHeight = pindexPrev->nHeight + 1;
    if (deterministicPNManager->LegacyPNObsolete(nBlockHeight)) {
        std::vector<CTxOut> vecMnOuts;
        if (!GetPatriotnodeTxOuts(pindexPrev, vecMnOuts)) {
            // No patriotnode scheduled to be paid.
            return true;
        }

        for (const CTxOut& o : vecMnOuts) {
            if (std::find(txNew.vout.begin(), txNew.vout.end(), o) == txNew.vout.end()) {
                CTxDestination mnDest;
                const std::string& payee = ExtractDestination(o.scriptPubKey, mnDest) ? EncodeDestination(mnDest)
                                                                                      : HexStr(o.scriptPubKey);
                LogPrint(BCLog::PATRIOTNODE, "%s: Failed to find expected payee %s in block at height %d (tx %s)",
                                            __func__, payee, pindexPrev->nHeight + 1, txNew.GetHash().ToString());
                return false;
            }
        }
        // all the expected payees have been found in txNew outputs
        return true;
    }

    // Legacy payment logic. !TODO: remove when transition to DPN is complete
    LOCK(cs_mapPatriotnodeBlocks);

    if (mapPatriotnodeBlocks.count(nBlockHeight)) {
        return mapPatriotnodeBlocks[nBlockHeight].IsTransactionValid(txNew, nHeight);
    }

    return true;
}

void CPatriotnodePayments::CleanPaymentList(int mnCount, int nHeight)
{
    LOCK2(cs_mapPatriotnodePayeeVotes, cs_mapPatriotnodeBlocks);

    //keep up to five cycles for historical sake
    int nLimit = std::max(int(mnCount * 1.25), 1000);

    std::map<uint256, CPatriotnodePaymentWinner>::iterator it = mapPatriotnodePayeeVotes.begin();
    while (it != mapPatriotnodePayeeVotes.end()) {
        CPatriotnodePaymentWinner winner = (*it).second;

        if (nHeight - winner.nBlockHeight > nLimit) {
            LogPrint(BCLog::PATRIOTNODE, "CPatriotnodePayments::CleanPaymentList - Removing old Patriotnode payment - block %d\n", winner.nBlockHeight);
            patriotnodeSync.mapSeenSyncPNW.erase((*it).first);
            mapPatriotnodePayeeVotes.erase(it++);
            mapPatriotnodeBlocks.erase(winner.nBlockHeight);
        } else {
            ++it;
        }
    }
}

void CPatriotnodePayments::UpdatedBlockTip(const CBlockIndex *pindexNew, const CBlockIndex *pindexFork, bool fInitialDownload)
{
    if (patriotnodeSync.RequestedPatriotnodeAssets > PATRIOTNODE_SYNC_LIST) {
        ProcessBlock(pindexNew->nHeight + 10);
    }
}

void CPatriotnodePayments::ProcessBlock(int nBlockHeight)
{
    // No more mnw messages after transition to DPN
    if (deterministicPNManager->LegacyPNObsolete()) {
        return;
    }
    if (!fPatriotNode) return;

    // Get the active patriotnode (operator) key
    CKey mnKey; CKeyID mnKeyID; CTxIn mnVin;
    if (!GetActivePatriotnodeKeys(mnKey, mnKeyID, mnVin)) {
        return;
    }

    //reference node - hybrid mode
    int n = mnodeman.GetPatriotnodeRank(mnVin, nBlockHeight - 100);

    if (n == -1) {
        LogPrint(BCLog::PATRIOTNODE, "CPatriotnodePayments::ProcessBlock - Unknown Patriotnode\n");
        return;
    }

    if (n > PNPAYMENTS_SIGNATURES_TOTAL) {
        LogPrint(BCLog::PATRIOTNODE, "CPatriotnodePayments::ProcessBlock - Patriotnode not in the top %d (%d)\n", PNPAYMENTS_SIGNATURES_TOTAL, n);
        return;
    }

    if (nBlockHeight <= nLastBlockHeight) return;

    if (g_budgetman.IsBudgetPaymentBlock(nBlockHeight)) {
        //is budget payment block -- handled by the budgeting software
        return;
    }

    // pay to the oldest PN that still had no payment but its input is old enough and it was active long enough
    int nCount = 0;
    PatriotnodeRef pmn = mnodeman.GetNextPatriotnodeInQueueForPayment(nBlockHeight, true, nCount);

    if (pmn == nullptr) {
        LogPrint(BCLog::PATRIOTNODE,"%s: Failed to find patriotnode to pay\n", __func__);
        return;
    }

    CPatriotnodePaymentWinner newWinner(mnVin, nBlockHeight);
    newWinner.AddPayee(pmn->GetPayeeScript());
    if (!newWinner.Sign(mnKey, mnKeyID)) {
        LogPrintf("%s: Failed to sign patriotnode winner\n", __func__);
        return;
    }
    if (!AddWinningPatriotnode(newWinner)) {
        return;
    }
    newWinner.Relay();
    nLastBlockHeight = nBlockHeight;
}

void CPatriotnodePayments::Sync(CNode* node, int nCountNeeded)
{
    LOCK(cs_mapPatriotnodePayeeVotes);

    int nHeight = mnodeman.GetBestHeight();
    int nCount = (mnodeman.CountEnabled() * 1.25);
    if (nCountNeeded > nCount) nCountNeeded = nCount;

    int nInvCount = 0;
    std::map<uint256, CPatriotnodePaymentWinner>::iterator it = mapPatriotnodePayeeVotes.begin();
    while (it != mapPatriotnodePayeeVotes.end()) {
        CPatriotnodePaymentWinner winner = (*it).second;
        if (winner.nBlockHeight >= nHeight - nCountNeeded && winner.nBlockHeight <= nHeight + 20) {
            node->PushInventory(CInv(MSG_PATRIOTNODE_WINNER, winner.GetHash()));
            nInvCount++;
        }
        ++it;
    }
    g_connman->PushMessage(node, CNetMsgMaker(node->GetSendVersion()).Make(NetMsgType::SYNCSTATUSCOUNT, PATRIOTNODE_SYNC_PNW, nInvCount));
}

std::string CPatriotnodePayments::ToString() const
{
    std::ostringstream info;

    info << "Votes: " << (int)mapPatriotnodePayeeVotes.size() << ", Blocks: " << (int)mapPatriotnodeBlocks.size();

    return info.str();
}

bool IsCoinbaseValueValid(const CTransactionRef& tx, CAmount nBudgetAmt, CValidationState& _state)
{
    assert(tx->IsCoinBase());
    if (patriotnodeSync.IsSynced()) {
        const CAmount nCBaseOutAmt = tx->GetValueOut();
        if (nBudgetAmt > 0) {
            // Superblock
            if (nCBaseOutAmt != nBudgetAmt) {
                const std::string strError = strprintf("%s: invalid coinbase payment for budget (%s vs expected=%s)",
                                                       __func__, FormatMoney(nCBaseOutAmt), FormatMoney(nBudgetAmt));
                return _state.DoS(100, error(strError.c_str()), REJECT_INVALID, "bad-superblock-cb-amt");
            }
            return true;
        }
    }
    return true;
}

