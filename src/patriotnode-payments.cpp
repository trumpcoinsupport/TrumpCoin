// Copyright (c) 2014-2015 The Dash developers
// Copyright (c) 2015-2018 The PIVX developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "patriotnode-payments.h"
#include "addrman.h"
#include "patriotnode-budget.h"
#include "patriotnode-sync.h"
#include "patriotnodeman.h"
#include "obfuscation.h"
#include "spork.h"
#include "sync.h"
#include "util.h"
#include "utilmoneystr.h"
#include <boost/filesystem.hpp>
#include <boost/lexical_cast.hpp>

/** Object for who's going to get paid on which blocks */
CPatriotnodePayments patriotnodePayments;

CCriticalSection cs_vecPayments;
CCriticalSection cs_mapPatriotnodeBlocks;
CCriticalSection cs_mapPatriotnodePayeeVotes;

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
    ssObj << strMagicMessage;                   // patriotnode cache file specific magic message
    ssObj << FLATDATA(Params().MessageStart()); // network specific magic number
    ssObj << objToSave;
    uint256 hash = Hash(ssObj.begin(), ssObj.end());
    ssObj << hash;

    // open output file, and associate with CAutoFile
    FILE* file = fopen(pathDB.string().c_str(), "wb");
    CAutoFile fileout(file, SER_DISK, CLIENT_VERSION);
    if (fileout.IsNull())
        return error("%s : Failed to open file %s", __func__, pathDB.string());

    // Write and commit header, data
    try {
        fileout << ssObj;
    } catch (std::exception& e) {
        return error("%s : Serialize or I/O error - %s", __func__, e.what());
    }
    fileout.fclose();

    LogPrint("patriotnode","Written info to mnpayments.dat  %dms\n", GetTimeMillis() - nStart);

    return true;
}

CPatriotnodePaymentDB::ReadResult CPatriotnodePaymentDB::Read(CPatriotnodePayments& objToLoad, bool fDryRun)
{
    int64_t nStart = GetTimeMillis();
    // open input file, and associate with CAutoFile
    FILE* file = fopen(pathDB.string().c_str(), "rb");
    CAutoFile filein(file, SER_DISK, CLIENT_VERSION);
    if (filein.IsNull()) {
        error("%s : Failed to open file %s", __func__, pathDB.string());
        return FileError;
    }

    // use file size to size memory buffer
    int fileSize = boost::filesystem::file_size(pathDB);
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

    CDataStream ssObj(vchData, SER_DISK, CLIENT_VERSION);

    // verify stored checksum matches input data
    uint256 hashTmp = Hash(ssObj.begin(), ssObj.end());
    if (hashIn != hashTmp) {
        error("%s : Checksum mismatch, data corrupted", __func__);
        return IncorrectHash;
    }

    unsigned char pchMsgTmp[4];
    std::string strMagicMessageTmp;
    try {
        // de-serialize file header (patriotnode cache file specific magic message) and ..
        ssObj >> strMagicMessageTmp;

        // ... verify the message matches predefined one
        if (strMagicMessage != strMagicMessageTmp) {
            error("%s : Invalid patriotnode payement cache magic message", __func__);
            return IncorrectMagicMessage;
        }


        // de-serialize file header (network specific magic number) and ..
        ssObj >> FLATDATA(pchMsgTmp);

        // ... verify the network matches ours
        if (memcmp(pchMsgTmp, Params().MessageStart(), sizeof(pchMsgTmp))) {
            error("%s : Invalid network magic number", __func__);
            return IncorrectMagicNumber;
        }

        // de-serialize data into CPatriotnodePayments object
        ssObj >> objToLoad;
    } catch (std::exception& e) {
        objToLoad.Clear();
        error("%s : Deserialize or I/O error - %s", __func__, e.what());
        return IncorrectFormat;
    }

    LogPrint("patriotnode","Loaded info from mnpayments.dat  %dms\n", GetTimeMillis() - nStart);
    LogPrint("patriotnode","  %s\n", objToLoad.ToString());
    if (!fDryRun) {
        LogPrint("patriotnode","Patriotnode payments manager - cleaning....\n");
        objToLoad.CleanPaymentList();
        LogPrint("patriotnode","Patriotnode payments manager - result:\n");
        LogPrint("patriotnode","  %s\n", objToLoad.ToString());
    }

    return Ok;
}

void DumpPatriotnodePayments()
{
    int64_t nStart = GetTimeMillis();

    CPatriotnodePaymentDB paymentdb;
    CPatriotnodePayments tempPayments;

    LogPrint("patriotnode","Verifying mnpayments.dat format...\n");
    CPatriotnodePaymentDB::ReadResult readResult = paymentdb.Read(tempPayments, true);
    // there was an error and it was not an error on file opening => do not proceed
    if (readResult == CPatriotnodePaymentDB::FileError)
        LogPrint("patriotnode","Missing budgets file - mnpayments.dat, will try to recreate\n");
    else if (readResult != CPatriotnodePaymentDB::Ok) {
        LogPrint("patriotnode","Error reading mnpayments.dat: ");
        if (readResult == CPatriotnodePaymentDB::IncorrectFormat)
            LogPrint("patriotnode","magic is ok but data has invalid format, will try to recreate\n");
        else {
            LogPrint("patriotnode","file format is unknown or invalid, please fix it manually\n");
            return;
        }
    }
    LogPrint("patriotnode","Writting info to mnpayments.dat...\n");
    paymentdb.Write(patriotnodePayments);

    LogPrint("patriotnode","Budget dump finished  %dms\n", GetTimeMillis() - nStart);
}

bool IsBlockValueValid(const CBlock& block, CAmount nExpectedValue, CAmount nMinted)
{
    CBlockIndex* pindexPrev = chainActive.Tip();
    if (pindexPrev == NULL) return true;

    int nHeight = 0;
    if (pindexPrev->GetBlockHash() == block.hashPrevBlock) {
        nHeight = pindexPrev->nHeight + 1;
    } else { //out of order
        BlockMap::iterator mi = mapBlockIndex.find(block.hashPrevBlock);
        if (mi != mapBlockIndex.end() && (*mi).second)
            nHeight = (*mi).second->nHeight + 1;
    }

    if (nHeight == 0) {
        LogPrint("patriotnode","IsBlockValueValid() : WARNING: Couldn't find previous block\n");
    }

    //LogPrintf("XX69----------> IsBlockValueValid(): nMinted: %d, nExpectedValue: %d\n", FormatMoney(nMinted), FormatMoney(nExpectedValue));

    if (!patriotnodeSync.IsSynced()) { //there is no budget data to use to check anything
        //super blocks will always be on these blocks, max 100 per budgeting
        if (nHeight % GetBudgetPaymentCycleBlocks() < 100) {
            return true;
        } else {
            if (nMinted > nExpectedValue) {
                return false;
            }
        }
    } else { // we're synced and have data so check the budget schedule

        //are these blocks even enabled
        if (!IsSporkActive(SPORK_13_ENABLE_SUPERBLOCKS)) {
            return nMinted <= nExpectedValue;
        }

        if (budget.IsBudgetPaymentBlock(nHeight)) {
            //the value of the block is evaluated in CheckBlock
            return true;
        } else {
            if (nMinted > nExpectedValue) {
                return false;
            }
        }
    }

    return true;
}

bool IsBlockPayeeValid(const CBlock& block, int nBlockHeight)
{
    TrxValidationStatus transactionStatus = TrxValidationStatus::InValid;

    if (!patriotnodeSync.IsSynced()) { //there is no budget data to use to check anything -- find the longest chain
        LogPrint("mnpayments", "Client not synced, skipping block payee checks\n");
        return true;
    }

    const CTransaction& txNew = (nBlockHeight > Params().LAST_POW_BLOCK() ? block.vtx[1] : block.vtx[0]);

    //check if it's a budget block
    if (IsSporkActive(SPORK_13_ENABLE_SUPERBLOCKS)) {
        if (budget.IsBudgetPaymentBlock(nBlockHeight)) {
            transactionStatus = budget.IsTransactionValid(txNew, nBlockHeight);
            if (transactionStatus == TrxValidationStatus::Valid) {
                return true;
            }

            if (transactionStatus == TrxValidationStatus::InValid) {
                LogPrint("patriotnode","Invalid budget payment detected %s\n", txNew.ToString().c_str());
                if (IsSporkActive(SPORK_9_PATRIOTNODE_BUDGET_ENFORCEMENT))
                    return false;

                LogPrint("patriotnode","Budget enforcement is disabled, accepting block\n");
            }
        }
    }

    // If we end here the transaction was either TrxValidationStatus::InValid and Budget enforcement is disabled, or
    // a double budget payment (status = TrxValidationStatus::DoublePayment) was detected, or no/not enough patriotnode
    // votes (status = TrxValidationStatus::VoteThreshold) for a finalized budget were found
    // In all cases a patriotnode will get the payment for this block

    //check for patriotnode payee
    if (patriotnodePayments.IsTransactionValid(txNew, nBlockHeight))
        return true;
    LogPrint("patriotnode","Invalid mn payment detected %s\n", txNew.ToString().c_str());

    if (IsSporkActive(SPORK_8_PATRIOTNODE_PAYMENT_ENFORCEMENT))
        return false;
    LogPrint("patriotnode","Patriotnode payment enforcement is disabled, accepting block\n");

    return true;
}


void FillBlockPayee(CMutableTransaction& txNew, CAmount nFees, bool fProofOfStake, bool fZTRUMPStake)
{
    CBlockIndex* pindexPrev = chainActive.Tip();
    if (!pindexPrev) return;

    if (IsSporkActive(SPORK_13_ENABLE_SUPERBLOCKS) && budget.IsBudgetPaymentBlock(pindexPrev->nHeight + 1)) {
        budget.FillBlockPayee(txNew, nFees, fProofOfStake);
    } else {
        patriotnodePayments.FillBlockPayee(txNew, nFees, fProofOfStake, fZTRUMPStake);
    }
}

std::string GetRequiredPaymentsString(int nBlockHeight)
{
    if (IsSporkActive(SPORK_13_ENABLE_SUPERBLOCKS) && budget.IsBudgetPaymentBlock(nBlockHeight)) {
        return budget.GetRequiredPaymentsString(nBlockHeight);
    } else {
        return patriotnodePayments.GetRequiredPaymentsString(nBlockHeight);
    }
}

void CPatriotnodePayments::FillBlockPayee(CMutableTransaction& txNew, int64_t nFees, bool fProofOfStake, bool fZTRUMPStake)
{
    CBlockIndex* pindexPrev = chainActive.Tip();
    if (!pindexPrev) return;

    bool hasPayment = true;
    CScript payee;

    //spork
    if (!patriotnodePayments.GetBlockPayee(pindexPrev->nHeight + 1, payee)) {
        //no patriotnode detected
        CPatriotnode* winningNode = mnodeman.GetCurrentPatriotNode(1);
        if (winningNode) {
            payee = GetScriptForDestination(winningNode->pubKeyCollateralAddress.GetID());
        } else {
            LogPrint("patriotnode","CreateNewBlock: Failed to detect patriotnode to pay\n");
            hasPayment = false;
        }
    }

    CAmount blockValue = GetBlockValue(pindexPrev->nHeight);
    CAmount patriotnodePayment = GetPatriotnodePayment(pindexPrev->nHeight, blockValue, 0, fZTRUMPStake);

    if (hasPayment) {
        if (fProofOfStake) {
            /**For Proof Of Stake vout[0] must be null
             * Stake reward can be split into many different outputs, so we must
             * use vout.size() to align with several different cases.
             * An additional output is appended as the patriotnode payment
             */
            unsigned int i = txNew.vout.size();
            txNew.vout.resize(i + 1);
            txNew.vout[i].scriptPubKey = payee;
            txNew.vout[i].nValue = patriotnodePayment;

            //subtract mn payment from the stake reward
            if (!txNew.vout[1].IsZerocoinMint())
                txNew.vout[i - 1].nValue -= patriotnodePayment;
        } else {
            txNew.vout.resize(2);
            txNew.vout[1].scriptPubKey = payee;
            txNew.vout[1].nValue = patriotnodePayment;
            txNew.vout[0].nValue = blockValue - patriotnodePayment;
        }

        CTxDestination address1;
        ExtractDestination(payee, address1);
        CBitcoinAddress address2(address1);

        LogPrint("patriotnode","Patriotnode payment of %s to %s\n", FormatMoney(patriotnodePayment).c_str(), address2.ToString().c_str());
    }
}

int CPatriotnodePayments::GetMinPatriotnodePaymentsProto()
{
    if (IsSporkActive(SPORK_10_PATRIOTNODE_PAY_UPDATED_NODES))
        return ActiveProtocol();                          // Allow only updated peers
    else
        return MIN_PEER_PROTO_VERSION_BEFORE_ENFORCEMENT; // Also allow old peers as long as they are allowed to run
}

void CPatriotnodePayments::ProcessMessagePatriotnodePayments(CNode* pfrom, std::string& strCommand, CDataStream& vRecv)
{
    if (!patriotnodeSync.IsBlockchainSynced()) return;

    if (fLiteMode) return; //disable all Obfuscation/Patriotnode related functionality


    if (strCommand == "mnget") { //Patriotnode Payments Request Sync
        if (fLiteMode) return;   //disable all Obfuscation/Patriotnode related functionality

        int nCountNeeded;
        vRecv >> nCountNeeded;

        if (Params().NetworkID() == CBaseChainParams::MAIN) {
            if (pfrom->HasFulfilledRequest("mnget")) {
                LogPrintf("CPatriotnodePayments::ProcessMessagePatriotnodePayments() : mnget - peer already asked me for the list\n");
                Misbehaving(pfrom->GetId(), 20);
                return;
            }
        }

        pfrom->FulfilledRequest("mnget");
        patriotnodePayments.Sync(pfrom, nCountNeeded);
        LogPrint("mnpayments", "mnget - Sent Patriotnode winners to peer %i\n", pfrom->GetId());
    } else if (strCommand == "mnw") { //Patriotnode Payments Declare Winner
        //this is required in litemodef
        CPatriotnodePaymentWinner winner;
        vRecv >> winner;

        if (pfrom->nVersion < ActiveProtocol()) return;

        int nHeight;
        {
            TRY_LOCK(cs_main, locked);
            if (!locked || chainActive.Tip() == NULL) return;
            nHeight = chainActive.Tip()->nHeight;
        }

        if (patriotnodePayments.mapPatriotnodePayeeVotes.count(winner.GetHash())) {
            LogPrint("mnpayments", "mnw - Already seen - %s bestHeight %d\n", winner.GetHash().ToString().c_str(), nHeight);
            patriotnodeSync.AddedPatriotnodeWinner(winner.GetHash());
            return;
        }

        int nFirstBlock = nHeight - (mnodeman.CountEnabled() * 1.25);
        if (winner.nBlockHeight < nFirstBlock || winner.nBlockHeight > nHeight + 20) {
            LogPrint("mnpayments", "mnw - winner out of range - FirstBlock %d Height %d bestHeight %d\n", nFirstBlock, winner.nBlockHeight, nHeight);
            return;
        }

        std::string strError = "";
        if (!winner.IsValid(pfrom, strError)) {
            // if(strError != "") LogPrint("patriotnode","mnw - invalid message - %s\n", strError);
            return;
        }

        if (!patriotnodePayments.CanVote(winner.vinPatriotnode.prevout, winner.nBlockHeight)) {
            //  LogPrint("patriotnode","mnw - patriotnode already voted - %s\n", winner.vinPatriotnode.prevout.ToStringShort());
            return;
        }

        if (!winner.SignatureValid()) {
            if (patriotnodeSync.IsSynced()) {
                LogPrintf("CPatriotnodePayments::ProcessMessagePatriotnodePayments() : mnw - invalid signature\n");
                Misbehaving(pfrom->GetId(), 20);
            }
            // it could just be a non-synced patriotnode
            mnodeman.AskForMN(pfrom, winner.vinPatriotnode);
            return;
        }

        CTxDestination address1;
        ExtractDestination(winner.payee, address1);
        CBitcoinAddress address2(address1);

        //   LogPrint("mnpayments", "mnw - winning vote - Addr %s Height %d bestHeight %d - %s\n", address2.ToString().c_str(), winner.nBlockHeight, nHeight, winner.vinPatriotnode.prevout.ToStringShort());

        if (patriotnodePayments.AddWinningPatriotnode(winner)) {
            winner.Relay();
            patriotnodeSync.AddedPatriotnodeWinner(winner.GetHash());
        }
    }
}

bool CPatriotnodePaymentWinner::Sign(CKey& keyPatriotnode, CPubKey& pubKeyPatriotnode)
{
    std::string errorMessage;
    std::string strPatriotNodeSignMessage;

    std::string strMessage = vinPatriotnode.prevout.ToStringShort() +
                             boost::lexical_cast<std::string>(nBlockHeight) +
                             payee.ToString();

    if (!obfuScationSigner.SignMessage(strMessage, errorMessage, vchSig, keyPatriotnode)) {
        LogPrint("patriotnode","CPatriotnodePing::Sign() - Error: %s\n", errorMessage.c_str());
        return false;
    }

    if (!obfuScationSigner.VerifyMessage(pubKeyPatriotnode, vchSig, strMessage, errorMessage)) {
        LogPrint("patriotnode","CPatriotnodePing::Sign() - Error: %s\n", errorMessage.c_str());
        return false;
    }

    return true;
}

bool CPatriotnodePayments::GetBlockPayee(int nBlockHeight, CScript& payee)
{
    if (mapPatriotnodeBlocks.count(nBlockHeight)) {
        return mapPatriotnodeBlocks[nBlockHeight].GetPayee(payee);
    }

    return false;
}

// Is this patriotnode scheduled to get paid soon?
// -- Only look ahead up to 8 blocks to allow for propagation of the latest 2 winners
bool CPatriotnodePayments::IsScheduled(CPatriotnode& mn, int nNotBlockHeight)
{
    LOCK(cs_mapPatriotnodeBlocks);

    int nHeight;
    {
        TRY_LOCK(cs_main, locked);
        if (!locked || chainActive.Tip() == NULL) return false;
        nHeight = chainActive.Tip()->nHeight;
    }

    CScript mnpayee;
    mnpayee = GetScriptForDestination(mn.pubKeyCollateralAddress.GetID());

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
    uint256 blockHash = 0;
    if (!GetBlockHash(blockHash, winnerIn.nBlockHeight - 100)) {
        return false;
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

    mapPatriotnodeBlocks[winnerIn.nBlockHeight].AddPayee(winnerIn.payee, 1);

    return true;
}

bool CPatriotnodeBlockPayees::IsTransactionValid(const CTransaction& txNew)
{
    LOCK(cs_vecPayments);

    int nMaxSignatures = 0;
    int nPatriotnode_Drift_Count = 0;

    std::string strPayeesPossible = "";

    CAmount nReward = GetBlockValue(nBlockHeight);

    if (IsSporkActive(SPORK_8_PATRIOTNODE_PAYMENT_ENFORCEMENT)) {
        // Get a stable number of patriotnodes by ignoring newly activated (< 8000 sec old) patriotnodes
        nPatriotnode_Drift_Count = mnodeman.stable_size() + Params().PatriotnodeCountDrift();
    }
    else {
        //account for the fact that all peers do not see the same patriotnode count. A allowance of being off our patriotnode count is given
        //we only need to look at an increased patriotnode count because as count increases, the reward decreases. This code only checks
        //for mnPayment >= required, so it only makes sense to check the max node count allowed.
        nPatriotnode_Drift_Count = mnodeman.size() + Params().PatriotnodeCountDrift();
    }

    CAmount requiredPatriotnodePayment = GetPatriotnodePayment(nBlockHeight, nReward, nPatriotnode_Drift_Count, txNew.IsZerocoinSpend());

    //require at least 6 signatures
    BOOST_FOREACH (CPatriotnodePayee& payee, vecPayments)
        if (payee.nVotes >= nMaxSignatures && payee.nVotes >= MNPAYMENTS_SIGNATURES_REQUIRED)
            nMaxSignatures = payee.nVotes;

    // if we don't have at least 6 signatures on a payee, approve whichever is the longest chain
    if (nMaxSignatures < MNPAYMENTS_SIGNATURES_REQUIRED) return true;

    BOOST_FOREACH (CPatriotnodePayee& payee, vecPayments) {
        bool found = false;
        BOOST_FOREACH (CTxOut out, txNew.vout) {
            if (payee.scriptPubKey == out.scriptPubKey) {
                if(out.nValue >= requiredPatriotnodePayment)
                    found = true;
                else
                    LogPrint("patriotnode","Patriotnode payment is out of drift range. Paid=%s Min=%s\n", FormatMoney(out.nValue).c_str(), FormatMoney(requiredPatriotnodePayment).c_str());
            }
        }

        if (payee.nVotes >= MNPAYMENTS_SIGNATURES_REQUIRED) {
            if (found) return true;

            CTxDestination address1;
            ExtractDestination(payee.scriptPubKey, address1);
            CBitcoinAddress address2(address1);

            if (strPayeesPossible == "") {
                strPayeesPossible += address2.ToString();
            } else {
                strPayeesPossible += "," + address2.ToString();
            }
        }
    }

    LogPrint("patriotnode","CPatriotnodePayments::IsTransactionValid - Missing required payment of %s to %s\n", FormatMoney(requiredPatriotnodePayment).c_str(), strPayeesPossible.c_str());
    return false;
}

std::string CPatriotnodeBlockPayees::GetRequiredPaymentsString()
{
    LOCK(cs_vecPayments);

    std::string ret = "Unknown";

    BOOST_FOREACH (CPatriotnodePayee& payee, vecPayments) {
        CTxDestination address1;
        ExtractDestination(payee.scriptPubKey, address1);
        CBitcoinAddress address2(address1);

        if (ret != "Unknown") {
            ret += ", " + address2.ToString() + ":" + boost::lexical_cast<std::string>(payee.nVotes);
        } else {
            ret = address2.ToString() + ":" + boost::lexical_cast<std::string>(payee.nVotes);
        }
    }

    return ret;
}

std::string CPatriotnodePayments::GetRequiredPaymentsString(int nBlockHeight)
{
    LOCK(cs_mapPatriotnodeBlocks);

    if (mapPatriotnodeBlocks.count(nBlockHeight)) {
        return mapPatriotnodeBlocks[nBlockHeight].GetRequiredPaymentsString();
    }

    return "Unknown";
}

bool CPatriotnodePayments::IsTransactionValid(const CTransaction& txNew, int nBlockHeight)
{
    LOCK(cs_mapPatriotnodeBlocks);

    if (mapPatriotnodeBlocks.count(nBlockHeight)) {
        return mapPatriotnodeBlocks[nBlockHeight].IsTransactionValid(txNew);
    }

    return true;
}

void CPatriotnodePayments::CleanPaymentList()
{
    LOCK2(cs_mapPatriotnodePayeeVotes, cs_mapPatriotnodeBlocks);

    int nHeight;
    {
        TRY_LOCK(cs_main, locked);
        if (!locked || chainActive.Tip() == NULL) return;
        nHeight = chainActive.Tip()->nHeight;
    }

    //keep up to five cycles for historical sake
    int nLimit = std::max(int(mnodeman.size() * 1.25), 1000);

    std::map<uint256, CPatriotnodePaymentWinner>::iterator it = mapPatriotnodePayeeVotes.begin();
    while (it != mapPatriotnodePayeeVotes.end()) {
        CPatriotnodePaymentWinner winner = (*it).second;

        if (nHeight - winner.nBlockHeight > nLimit) {
            LogPrint("mnpayments", "CPatriotnodePayments::CleanPaymentList - Removing old Patriotnode payment - block %d\n", winner.nBlockHeight);
            patriotnodeSync.mapSeenSyncMNW.erase((*it).first);
            mapPatriotnodePayeeVotes.erase(it++);
            mapPatriotnodeBlocks.erase(winner.nBlockHeight);
        } else {
            ++it;
        }
    }
}

bool CPatriotnodePaymentWinner::IsValid(CNode* pnode, std::string& strError)
{
    CPatriotnode* pmn = mnodeman.Find(vinPatriotnode);

    if (!pmn) {
        strError = strprintf("Unknown Patriotnode %s", vinPatriotnode.prevout.hash.ToString());
        LogPrint("patriotnode","CPatriotnodePaymentWinner::IsValid - %s\n", strError);
        mnodeman.AskForMN(pnode, vinPatriotnode);
        return false;
    }

    if (pmn->protocolVersion < ActiveProtocol()) {
        strError = strprintf("Patriotnode protocol too old %d - req %d", pmn->protocolVersion, ActiveProtocol());
        LogPrint("patriotnode","CPatriotnodePaymentWinner::IsValid - %s\n", strError);
        return false;
    }

    int n = mnodeman.GetPatriotnodeRank(vinPatriotnode, nBlockHeight - 100, ActiveProtocol());

    if (n > MNPAYMENTS_SIGNATURES_TOTAL) {
        //It's common to have patriotnodes mistakenly think they are in the top 10
        // We don't want to print all of these messages, or punish them unless they're way off
        if (n > MNPAYMENTS_SIGNATURES_TOTAL * 2) {
            strError = strprintf("Patriotnode not in the top %d (%d)", MNPAYMENTS_SIGNATURES_TOTAL * 2, n);
            LogPrint("patriotnode","CPatriotnodePaymentWinner::IsValid - %s\n", strError);
            //if (patriotnodeSync.IsSynced()) Misbehaving(pnode->GetId(), 20);
        }
        return false;
    }

    return true;
}

bool CPatriotnodePayments::ProcessBlock(int nBlockHeight)
{
    if (!fPatriotNode) return false;

    //reference node - hybrid mode

    int n = mnodeman.GetPatriotnodeRank(activePatriotnode.vin, nBlockHeight - 100, ActiveProtocol());

    if (n == -1) {
        LogPrint("mnpayments", "CPatriotnodePayments::ProcessBlock - Unknown Patriotnode\n");
        return false;
    }

    if (n > MNPAYMENTS_SIGNATURES_TOTAL) {
        LogPrint("mnpayments", "CPatriotnodePayments::ProcessBlock - Patriotnode not in the top %d (%d)\n", MNPAYMENTS_SIGNATURES_TOTAL, n);
        return false;
    }

    if (nBlockHeight <= nLastBlockHeight) return false;

    CPatriotnodePaymentWinner newWinner(activePatriotnode.vin);

    if (budget.IsBudgetPaymentBlock(nBlockHeight)) {
        //is budget payment block -- handled by the budgeting software
    } else {
        LogPrint("patriotnode","CPatriotnodePayments::ProcessBlock() Start nHeight %d - vin %s. \n", nBlockHeight, activePatriotnode.vin.prevout.hash.ToString());

        // pay to the oldest MN that still had no payment but its input is old enough and it was active long enough
        int nCount = 0;
        CPatriotnode* pmn = mnodeman.GetNextPatriotnodeInQueueForPayment(nBlockHeight, true, nCount);

        if (pmn != NULL) {
            LogPrint("patriotnode","CPatriotnodePayments::ProcessBlock() Found by FindOldestNotInVec \n");

            newWinner.nBlockHeight = nBlockHeight;

            CScript payee = GetScriptForDestination(pmn->pubKeyCollateralAddress.GetID());
            newWinner.AddPayee(payee);

            CTxDestination address1;
            ExtractDestination(payee, address1);
            CBitcoinAddress address2(address1);

            LogPrint("patriotnode","CPatriotnodePayments::ProcessBlock() Winner payee %s nHeight %d. \n", address2.ToString().c_str(), newWinner.nBlockHeight);
        } else {
            LogPrint("patriotnode","CPatriotnodePayments::ProcessBlock() Failed to find patriotnode to pay\n");
        }
    }

    std::string errorMessage;
    CPubKey pubKeyPatriotnode;
    CKey keyPatriotnode;

    if (!obfuScationSigner.SetKey(strPatriotNodePrivKey, errorMessage, keyPatriotnode, pubKeyPatriotnode)) {
        LogPrint("patriotnode","CPatriotnodePayments::ProcessBlock() - Error upon calling SetKey: %s\n", errorMessage.c_str());
        return false;
    }

    LogPrint("patriotnode","CPatriotnodePayments::ProcessBlock() - Signing Winner\n");
    if (newWinner.Sign(keyPatriotnode, pubKeyPatriotnode)) {
        LogPrint("patriotnode","CPatriotnodePayments::ProcessBlock() - AddWinningPatriotnode\n");

        if (AddWinningPatriotnode(newWinner)) {
            newWinner.Relay();
            nLastBlockHeight = nBlockHeight;
            return true;
        }
    }

    return false;
}

void CPatriotnodePaymentWinner::Relay()
{
    CInv inv(MSG_PATRIOTNODE_WINNER, GetHash());
    RelayInv(inv);
}

bool CPatriotnodePaymentWinner::SignatureValid()
{
    CPatriotnode* pmn = mnodeman.Find(vinPatriotnode);

    if (pmn != NULL) {
        std::string strMessage = vinPatriotnode.prevout.ToStringShort() +
                                 boost::lexical_cast<std::string>(nBlockHeight) +
                                 payee.ToString();

        std::string errorMessage = "";
        if (!obfuScationSigner.VerifyMessage(pmn->pubKeyPatriotnode, vchSig, strMessage, errorMessage)) {
            return error("CPatriotnodePaymentWinner::SignatureValid() - Got bad Patriotnode address signature %s\n", vinPatriotnode.prevout.hash.ToString());
        }

        return true;
    }

    return false;
}

void CPatriotnodePayments::Sync(CNode* node, int nCountNeeded)
{
    LOCK(cs_mapPatriotnodePayeeVotes);

    int nHeight;
    {
        TRY_LOCK(cs_main, locked);
        if (!locked || chainActive.Tip() == NULL) return;
        nHeight = chainActive.Tip()->nHeight;
    }

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
    node->PushMessage("ssc", PATRIOTNODE_SYNC_MNW, nInvCount);
}

std::string CPatriotnodePayments::ToString() const
{
    std::ostringstream info;

    info << "Votes: " << (int)mapPatriotnodePayeeVotes.size() << ", Blocks: " << (int)mapPatriotnodeBlocks.size();

    return info.str();
}


int CPatriotnodePayments::GetOldestBlock()
{
    LOCK(cs_mapPatriotnodeBlocks);

    int nOldestBlock = std::numeric_limits<int>::max();

    std::map<int, CPatriotnodeBlockPayees>::iterator it = mapPatriotnodeBlocks.begin();
    while (it != mapPatriotnodeBlocks.end()) {
        if ((*it).first < nOldestBlock) {
            nOldestBlock = (*it).first;
        }
        it++;
    }

    return nOldestBlock;
}


int CPatriotnodePayments::GetNewestBlock()
{
    LOCK(cs_mapPatriotnodeBlocks);

    int nNewestBlock = 0;

    std::map<int, CPatriotnodeBlockPayees>::iterator it = mapPatriotnodeBlocks.begin();
    while (it != mapPatriotnodeBlocks.end()) {
        if ((*it).first > nNewestBlock) {
            nNewestBlock = (*it).first;
        }
        it++;
    }

    return nNewestBlock;
}
