// Copyright (c) 2019-2020 The TrumpCoin developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "qt/trumpcoin/mnmodel.h"

#include "activepatriotnode.h"
#include "patriotnode-sync.h"
#include "patriotnodeman.h"
#include "net.h"        // for validatePatriotnodeIP
#include "sync.h"
#include "uint256.h"
#include "wallet/wallet.h"

PNModel::PNModel(QObject *parent, WalletModel* _model) :
    QAbstractTableModel(parent),
    walletModel(_model)
{
    updatePNList();
}

void PNModel::updatePNList()
{
    int end = nodes.size();
    nodes.clear();
    collateralTxAccepted.clear();
    for (const CPatriotnodeConfig::CPatriotnodeEntry& mne : patriotnodeConfig.getEntries()) {
        int nIndex;
        if (!mne.castOutputIndex(nIndex))
            continue;
        const uint256& txHash = uint256S(mne.getTxHash());
        CTxIn txIn(txHash, uint32_t(nIndex));
        CPatriotnode* pmn = mnodeman.Find(txIn.prevout);
        if (!pmn) {
            pmn = new CPatriotnode();
            pmn->vin = txIn;
        }
        nodes.insert(QString::fromStdString(mne.getAlias()), std::make_pair(QString::fromStdString(mne.getIp()), pmn));
        if (walletModel) {
            collateralTxAccepted.insert(mne.getTxHash(), walletModel->getWalletTxDepth(txHash) >= PatriotnodeCollateralMinConf());
        }
    }
    Q_EMIT dataChanged(index(0, 0, QModelIndex()), index(end, 5, QModelIndex()) );
}

int PNModel::rowCount(const QModelIndex &parent) const
{
    if (parent.isValid())
        return 0;
    return nodes.size();
}

int PNModel::columnCount(const QModelIndex &parent) const
{
    if (parent.isValid())
        return 0;
    return 6;
}


QVariant PNModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid())
            return QVariant();

    // rec could be null, always verify it.
    CPatriotnode* rec = static_cast<CPatriotnode*>(index.internalPointer());
    bool isAvailable = rec;
    int row = index.row();
    if (role == Qt::DisplayRole || role == Qt::EditRole) {
        switch (index.column()) {
            case ALIAS:
                return nodes.uniqueKeys().value(row);
            case ADDRESS:
                return nodes.values().value(row).first;
            case PUB_KEY:
                return (isAvailable) ? QString::fromStdString(nodes.values().value(row).second->pubKeyPatriotnode.GetHash().GetHex()) : "Not available";
            case COLLATERAL_ID:
                return (isAvailable) ? QString::fromStdString(rec->vin.prevout.hash.GetHex()) : "Not available";
            case COLLATERAL_OUT_INDEX:
                return (isAvailable) ? QString::number(rec->vin.prevout.n) : "Not available";
            case STATUS: {
                std::pair<QString, CPatriotnode*> pair = nodes.values().value(row);
                std::string status = "MISSING";
                if (pair.second) {
                    status = pair.second->Status();
                    // Quick workaround to the current Patriotnode status types.
                    // If the status is REMOVE and there is no pubkey associated to the Patriotnode
                    // means that the PN is not in the network list and was created in
                    // updatePNList(). Which.. denotes a not started patriotnode.
                    // This will change in the future with the PatriotnodeWrapper introduction.
                    if (status == "REMOVE" && !pair.second->pubKeyCollateralAddress.IsValid()) {
                        return "MISSING";
                    }
                }
                return QString::fromStdString(status);
            }
            case PRIV_KEY: {
                if (isAvailable) {
                    for (CPatriotnodeConfig::CPatriotnodeEntry mne : patriotnodeConfig.getEntries()) {
                        if (mne.getTxHash().compare(rec->vin.prevout.hash.GetHex()) == 0) {
                            return QString::fromStdString(mne.getPrivKey());
                        }
                    }
                }
                return "Not available";
            }
            case WAS_COLLATERAL_ACCEPTED:{
                return isAvailable && collateralTxAccepted.value(rec->vin.prevout.hash.GetHex());
            }
        }
    }
    return QVariant();
}

QModelIndex PNModel::index(int row, int column, const QModelIndex& parent) const
{
    Q_UNUSED(parent);
    std::pair<QString, CPatriotnode*> pair = nodes.values().value(row);
    CPatriotnode* data = pair.second;
    if (data) {
        return createIndex(row, column, data);
    } else if (!pair.first.isEmpty()) {
        return createIndex(row, column, nullptr);
    } else {
        return QModelIndex();
    }
}


bool PNModel::removeMn(const QModelIndex& modelIndex)
{
    QString alias = modelIndex.data(Qt::DisplayRole).toString();
    int idx = modelIndex.row();
    beginRemoveRows(QModelIndex(), idx, idx);
    nodes.take(alias);
    endRemoveRows();
    Q_EMIT dataChanged(index(idx, 0, QModelIndex()), index(idx, 5, QModelIndex()) );
    return true;
}

bool PNModel::addMn(CPatriotnodeConfig::CPatriotnodeEntry* mne)
{
    beginInsertRows(QModelIndex(), nodes.size(), nodes.size());
    int nIndex;
    if (!mne->castOutputIndex(nIndex))
        return false;

    CPatriotnode* pmn = mnodeman.Find(COutPoint(uint256S(mne->getTxHash()), uint32_t(nIndex)));
    nodes.insert(QString::fromStdString(mne->getAlias()), std::make_pair(QString::fromStdString(mne->getIp()), pmn));
    endInsertRows();
    return true;
}

int PNModel::getPNState(QString mnAlias)
{
    QMap<QString, std::pair<QString, CPatriotnode*>>::const_iterator it = nodes.find(mnAlias);
    if (it != nodes.end()) return it.value().second->GetActiveState();
    throw std::runtime_error(std::string("Patriotnode alias not found"));
}

bool PNModel::isPNInactive(QString mnAlias)
{
    int activeState = getPNState(mnAlias);
    return activeState == CPatriotnode::PATRIOTNODE_EXPIRED || activeState == CPatriotnode::PATRIOTNODE_REMOVE;
}

bool PNModel::isPNActive(QString mnAlias)
{
    int activeState = getPNState(mnAlias);
    return activeState == CPatriotnode::PATRIOTNODE_PRE_ENABLED || activeState == CPatriotnode::PATRIOTNODE_ENABLED;
}

bool PNModel::isPNCollateralMature(QString mnAlias)
{
    QMap<QString, std::pair<QString, CPatriotnode*>>::const_iterator it = nodes.find(mnAlias);
    if (it != nodes.end()) return collateralTxAccepted.value(it.value().second->vin.prevout.hash.GetHex());
    throw std::runtime_error(std::string("Patriotnode alias not found"));
}

bool PNModel::isPNsNetworkSynced()
{
    return patriotnodeSync.IsSynced();
}

bool PNModel::validatePNIP(const QString& addrStr)
{
    return validatePatriotnodeIP(addrStr.toStdString());
}
