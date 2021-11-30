// Copyright (c) 2014-2015 The Dash developers
// Copyright (c) 2015-2020 The TrumpCoin developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef PATRIOTNODE_PAYMENTS_H
#define PATRIOTNODE_PAYMENTS_H

#include "key.h"
#include "patriotnode.h"
#include "validationinterface.h"


extern RecursiveMutex cs_vecPayments;
extern RecursiveMutex cs_mapPatriotnodeBlocks;
extern RecursiveMutex cs_mapPatriotnodePayeeVotes;

class CPatriotnodePayments;
class CPatriotnodePaymentWinner;
class CPatriotnodeBlockPayees;
class CValidationState;

extern CPatriotnodePayments patriotnodePayments;

#define PNPAYMENTS_SIGNATURES_REQUIRED 6
#define PNPAYMENTS_SIGNATURES_TOTAL 10

bool IsBlockPayeeValid(const CBlock& block, const CBlockIndex* pindexPrev, int nHeight);
std::string GetRequiredPaymentsString(int nBlockHeight);
bool IsBlockValueValid(int nHeight, CAmount& nExpectedValue, CAmount nMinted, CAmount& nBudgetAmt);
void FillBlockPayee(CMutableTransaction& txCoinbase, CMutableTransaction& txCoinstake, const CBlockIndex* pindexPrev, bool fProofOfStake);

/**
 * Check coinbase output value for blocks after v6.0 enforcement.
 * It must pay the patriotnode for regular blocks and a proposal during superblocks.
 */
bool IsCoinbaseValueValid(const CTransactionRef& tx, CAmount nBudgetAmt, CValidationState& _state);

void DumpPatriotnodePayments();

/** Save Patriotnode Payment Data (mnpayments.dat)
 */
class CPatriotnodePaymentDB
{
private:
    fs::path pathDB;
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

    CPatriotnodePaymentDB();
    bool Write(const CPatriotnodePayments& objToSave);
    ReadResult Read(CPatriotnodePayments& objToLoad);
};

class CPatriotnodePayee
{
public:
    CScript scriptPubKey;
    int nVotes;

    CPatriotnodePayee()
    {
        scriptPubKey = CScript();
        nVotes = 0;
    }

    CPatriotnodePayee(CScript payee, int nVotesIn)
    {
        scriptPubKey = payee;
        nVotes = nVotesIn;
    }

    SERIALIZE_METHODS(CPatriotnodePayee, obj) { READWRITE(obj.scriptPubKey, obj.nVotes); }
};

// Keep track of votes for payees from patriotnodes
class CPatriotnodeBlockPayees
{
public:
    int nBlockHeight;
    std::vector<CPatriotnodePayee> vecPayments;

    CPatriotnodeBlockPayees()
    {
        nBlockHeight = 0;
        vecPayments.clear();
    }
    CPatriotnodeBlockPayees(int nBlockHeightIn)
    {
        nBlockHeight = nBlockHeightIn;
        vecPayments.clear();
    }

    void AddPayee(const CScript& payeeIn, int nIncrement)
    {
        LOCK(cs_vecPayments);

        for (CPatriotnodePayee& payee : vecPayments) {
            if (payee.scriptPubKey == payeeIn) {
                payee.nVotes += nIncrement;
                return;
            }
        }

        CPatriotnodePayee c(payeeIn, nIncrement);
        vecPayments.push_back(c);
    }

    bool GetPayee(CScript& payee) const
    {
        LOCK(cs_vecPayments);

        int nVotes = -1;
        for (const CPatriotnodePayee& p : vecPayments) {
            if (p.nVotes > nVotes) {
                payee = p.scriptPubKey;
                nVotes = p.nVotes;
            }
        }

        return (nVotes > -1);
    }

    bool HasPayeeWithVotes(const CScript& payee, int nVotesReq)
    {
        LOCK(cs_vecPayments);

        for (CPatriotnodePayee& p : vecPayments) {
            if (p.nVotes >= nVotesReq && p.scriptPubKey == payee) return true;
        }

        return false;
    }
	int nHeight;
    bool IsTransactionValid(const CTransaction& txNew, int nHeight);
    std::string GetRequiredPaymentsString();

    SERIALIZE_METHODS(CPatriotnodeBlockPayees, obj) { READWRITE(obj.nBlockHeight, obj.vecPayments); }
};

// for storing the winning payments
class CPatriotnodePaymentWinner : public CSignedMessage
{
public:
    CTxIn vinPatriotnode;
    int nBlockHeight;
    CScript payee;

    CPatriotnodePaymentWinner() :
        CSignedMessage(),
        vinPatriotnode(),
        nBlockHeight(0),
        payee()
    {}

    CPatriotnodePaymentWinner(const CTxIn& vinIn, int nHeight):
        CSignedMessage(),
        vinPatriotnode(vinIn),
        nBlockHeight(nHeight),
        payee()
    {}

    uint256 GetHash() const;

    // override CSignedMessage functions
    uint256 GetSignatureHash() const override { return GetHash(); }
    std::string GetStrMessage() const override;
    CTxIn GetVin() const { return vinPatriotnode; };

    bool IsValid(CNode* pnode, std::string& strError, int chainHeight);
    void Relay();

    void AddPayee(const CScript& payeeIn)
    {
        payee = payeeIn;
    }

    SERIALIZE_METHODS(CPatriotnodePaymentWinner, obj) { READWRITE(obj.vinPatriotnode, obj.nBlockHeight, obj.payee, obj.vchSig, obj.nMessVersion); }

    std::string ToString()
    {
        std::string ret = "";
        ret += vinPatriotnode.ToString();
        ret += ", " + std::to_string(nBlockHeight);
        ret += ", " + HexStr(payee);
        ret += ", " + std::to_string((int)vchSig.size());
        return ret;
    }
};

//
// Patriotnode Payments Class
// Keeps track of who should get paid for which blocks
//

class CPatriotnodePayments : public CValidationInterface
{
private:
    int nLastBlockHeight;

public:
    std::map<uint256, CPatriotnodePaymentWinner> mapPatriotnodePayeeVotes;
    std::map<int, CPatriotnodeBlockPayees> mapPatriotnodeBlocks;
    std::map<COutPoint, int> mapPatriotnodesLastVote; //prevout, nBlockHeight

    CPatriotnodePayments()
    {
        nLastBlockHeight = 0;
    }

    void Clear()
    {
        LOCK2(cs_mapPatriotnodeBlocks, cs_mapPatriotnodePayeeVotes);
        mapPatriotnodeBlocks.clear();
        mapPatriotnodePayeeVotes.clear();
    }

    void UpdatedBlockTip(const CBlockIndex *pindexNew, const CBlockIndex *pindexFork, bool fInitialDownload) override;

    bool AddWinningPatriotnode(CPatriotnodePaymentWinner& winner);
    void ProcessBlock(int nBlockHeight);

    void Sync(CNode* node, int nCountNeeded);
    void CleanPaymentList(int mnCount, int nHeight);

    // get the patriotnode payment outs for block built on top of pindexPrev
    bool GetPatriotnodeTxOuts(const CBlockIndex* pindexPrev, std::vector<CTxOut>& voutPatriotnodePaymentsRet) const;

    // can be removed after transition to DPN
    bool GetLegacyPatriotnodeTxOut(int nHeight, std::vector<CTxOut>& voutPatriotnodePaymentsRet) const;
    bool GetBlockPayee(int nBlockHeight, CScript& payee) const;

    bool IsTransactionValid(const CTransaction& txNew, const CBlockIndex* pindexPrev, int nHeight);
    bool IsScheduled(const CPatriotnode& mn, int nNotBlockHeight);

    bool CanVote(const COutPoint& outPatriotnode, int nBlockHeight)
    {
        LOCK(cs_mapPatriotnodePayeeVotes);

        if (mapPatriotnodesLastVote.count(outPatriotnode)) {
            if (mapPatriotnodesLastVote[outPatriotnode] == nBlockHeight) {
                return false;
            }
        }

        //record this patriotnode voted
        mapPatriotnodesLastVote[outPatriotnode] = nBlockHeight;
        return true;
    }

    void ProcessMessagePatriotnodePayments(CNode* pfrom, std::string& strCommand, CDataStream& vRecv);
    std::string GetRequiredPaymentsString(int nBlockHeight);
    void FillBlockPayee(CMutableTransaction& txCoinbase, CMutableTransaction& txCoinstake, const CBlockIndex* pindexPrev, bool fProofOfStake) const;
    std::string ToString() const;

    SERIALIZE_METHODS(CPatriotnodePayments, obj) { READWRITE(obj.mapPatriotnodePayeeVotes, obj.mapPatriotnodeBlocks); }
};


#endif
