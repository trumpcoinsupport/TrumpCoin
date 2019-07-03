// Copyright (c) 2014-2015 The Dash developers
// Copyright (c) 2015-2017 The PIVX developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef PATRIOTNODE_PAYMENTS_H
#define PATRIOTNODE_PAYMENTS_H

#include "key.h"
#include "main.h"
#include "patriotnode.h"
#include <boost/lexical_cast.hpp>

using namespace std;

extern CCriticalSection cs_vecPayments;
extern CCriticalSection cs_mapPatriotnodeBlocks;
extern CCriticalSection cs_mapPatriotnodePayeeVotes;

class CPatriotnodePayments;
class CPatriotnodePaymentWinner;
class CPatriotnodeBlockPayees;

extern CPatriotnodePayments patriotnodePayments;

#define MNPAYMENTS_SIGNATURES_REQUIRED 6
#define MNPAYMENTS_SIGNATURES_TOTAL 10

void ProcessMessagePatriotnodePayments(CNode* pfrom, std::string& strCommand, CDataStream& vRecv);
bool IsBlockPayeeValid(const CBlock& block, int nBlockHeight);
std::string GetRequiredPaymentsString(int nBlockHeight);
bool IsBlockValueValid(const CBlock& block, CAmount nExpectedValue, CAmount nMinted);
void FillBlockPayee(CMutableTransaction& txNew, CAmount nFees, bool fProofOfStake, bool fZTRUMPStake);

void DumpPatriotnodePayments();

/** Save Patriotnode Payment Data (mnpayments.dat)
 */
class CPatriotnodePaymentDB
{
private:
    boost::filesystem::path pathDB;
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
    ReadResult Read(CPatriotnodePayments& objToLoad, bool fDryRun = false);
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

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action, int nType, int nVersion)
    {
        READWRITE(scriptPubKey);
        READWRITE(nVotes);
    }
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

    void AddPayee(CScript payeeIn, int nIncrement)
    {
        LOCK(cs_vecPayments);

        BOOST_FOREACH (CPatriotnodePayee& payee, vecPayments) {
            if (payee.scriptPubKey == payeeIn) {
                payee.nVotes += nIncrement;
                return;
            }
        }

        CPatriotnodePayee c(payeeIn, nIncrement);
        vecPayments.push_back(c);
    }

    bool GetPayee(CScript& payee)
    {
        LOCK(cs_vecPayments);

        int nVotes = -1;
        BOOST_FOREACH (CPatriotnodePayee& p, vecPayments) {
            if (p.nVotes > nVotes) {
                payee = p.scriptPubKey;
                nVotes = p.nVotes;
            }
        }

        return (nVotes > -1);
    }

    bool HasPayeeWithVotes(CScript payee, int nVotesReq)
    {
        LOCK(cs_vecPayments);

        BOOST_FOREACH (CPatriotnodePayee& p, vecPayments) {
            if (p.nVotes >= nVotesReq && p.scriptPubKey == payee) return true;
        }

        return false;
    }

    bool IsTransactionValid(const CTransaction& txNew);
    std::string GetRequiredPaymentsString();

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action, int nType, int nVersion)
    {
        READWRITE(nBlockHeight);
        READWRITE(vecPayments);
    }
};

// for storing the winning payments
class CPatriotnodePaymentWinner
{
public:
    CTxIn vinPatriotnode;

    int nBlockHeight;
    CScript payee;
    std::vector<unsigned char> vchSig;

    CPatriotnodePaymentWinner()
    {
        nBlockHeight = 0;
        vinPatriotnode = CTxIn();
        payee = CScript();
    }

    CPatriotnodePaymentWinner(CTxIn vinIn)
    {
        nBlockHeight = 0;
        vinPatriotnode = vinIn;
        payee = CScript();
    }

    uint256 GetHash()
    {
        CHashWriter ss(SER_GETHASH, PROTOCOL_VERSION);
        ss << payee;
        ss << nBlockHeight;
        ss << vinPatriotnode.prevout;

        return ss.GetHash();
    }

    bool Sign(CKey& keyPatriotnode, CPubKey& pubKeyPatriotnode);
    bool IsValid(CNode* pnode, std::string& strError);
    bool SignatureValid();
    void Relay();

    void AddPayee(CScript payeeIn)
    {
        payee = payeeIn;
    }


    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action, int nType, int nVersion)
    {
        READWRITE(vinPatriotnode);
        READWRITE(nBlockHeight);
        READWRITE(payee);
        READWRITE(vchSig);
    }

    std::string ToString()
    {
        std::string ret = "";
        ret += vinPatriotnode.ToString();
        ret += ", " + boost::lexical_cast<std::string>(nBlockHeight);
        ret += ", " + payee.ToString();
        ret += ", " + boost::lexical_cast<std::string>((int)vchSig.size());
        return ret;
    }
};

//
// Patriotnode Payments Class
// Keeps track of who should get paid for which blocks
//

class CPatriotnodePayments
{
private:
    int nSyncedFromPeer;
    int nLastBlockHeight;

public:
    std::map<uint256, CPatriotnodePaymentWinner> mapPatriotnodePayeeVotes;
    std::map<int, CPatriotnodeBlockPayees> mapPatriotnodeBlocks;
    std::map<uint256, int> mapPatriotnodesLastVote; //prevout.hash + prevout.n, nBlockHeight

    CPatriotnodePayments()
    {
        nSyncedFromPeer = 0;
        nLastBlockHeight = 0;
    }

    void Clear()
    {
        LOCK2(cs_mapPatriotnodeBlocks, cs_mapPatriotnodePayeeVotes);
        mapPatriotnodeBlocks.clear();
        mapPatriotnodePayeeVotes.clear();
    }

    bool AddWinningPatriotnode(CPatriotnodePaymentWinner& winner);
    bool ProcessBlock(int nBlockHeight);

    void Sync(CNode* node, int nCountNeeded);
    void CleanPaymentList();
    int LastPayment(CPatriotnode& mn);

    bool GetBlockPayee(int nBlockHeight, CScript& payee);
    bool IsTransactionValid(const CTransaction& txNew, int nBlockHeight);
    bool IsScheduled(CPatriotnode& mn, int nNotBlockHeight);

    bool CanVote(COutPoint outPatriotnode, int nBlockHeight)
    {
        LOCK(cs_mapPatriotnodePayeeVotes);

        if (mapPatriotnodesLastVote.count(outPatriotnode.hash + outPatriotnode.n)) {
            if (mapPatriotnodesLastVote[outPatriotnode.hash + outPatriotnode.n] == nBlockHeight) {
                return false;
            }
        }

        //record this patriotnode voted
        mapPatriotnodesLastVote[outPatriotnode.hash + outPatriotnode.n] = nBlockHeight;
        return true;
    }

    int GetMinPatriotnodePaymentsProto();
    void ProcessMessagePatriotnodePayments(CNode* pfrom, std::string& strCommand, CDataStream& vRecv);
    std::string GetRequiredPaymentsString(int nBlockHeight);
    void FillBlockPayee(CMutableTransaction& txNew, int64_t nFees, bool fProofOfStake, bool fZTRUMPStake);
    std::string ToString() const;
    int GetOldestBlock();
    int GetNewestBlock();

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action, int nType, int nVersion)
    {
        READWRITE(mapPatriotnodePayeeVotes);
        READWRITE(mapPatriotnodeBlocks);
    }
};


#endif
