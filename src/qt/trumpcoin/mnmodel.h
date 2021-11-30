// Copyright (c) 2019-2020 The TrumpCoin developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef PNMODEL_H
#define PNMODEL_H

#include <QAbstractTableModel>
#include "patriotnode.h"
#include "patriotnodeconfig.h"
#include "qt/walletmodel.h"

class PNModel : public QAbstractTableModel
{
    Q_OBJECT

public:
    explicit PNModel(QObject *parent, WalletModel* _model);
    ~PNModel() override {
        nodes.clear();
        collateralTxAccepted.clear();
    }

    enum ColumnIndex {
        ALIAS = 0,  /**< User specified PN alias */
        ADDRESS = 1, /**< Node address */
        PROTO_VERSION = 2, /**< Node protocol version */
        STATUS = 3, /**< Node status */
        ACTIVE_TIMESTAMP = 4, /**<  */
        PUB_KEY = 5,
        COLLATERAL_ID = 6,
        COLLATERAL_OUT_INDEX = 7,
        PRIV_KEY = 8,
        WAS_COLLATERAL_ACCEPTED = 9
    };

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    int columnCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    QModelIndex index(int row, int column, const QModelIndex& parent) const override;
    bool removeMn(const QModelIndex& index);
    bool addMn(CPatriotnodeConfig::CPatriotnodeEntry* entry);
    void updatePNList();


    bool isPNsNetworkSynced();
    // Returns the PN activeState field.
    int getPNState(QString alias);
    // Checks if the patriotnode is inactive
    bool isPNInactive(QString mnAlias);
    // Patriotnode is active if it's in PRE_ENABLED OR ENABLED state
    bool isPNActive(QString mnAlias);
    // Patriotnode collateral has enough confirmations
    bool isPNCollateralMature(QString mnAlias);
    // Validate string representing a patriotnode IP address
    static bool validatePNIP(const QString& addrStr);


private:
    WalletModel* walletModel;
    // alias mn node ---> pair <ip, patriot node>
    QMap<QString, std::pair<QString, CPatriotnode*>> nodes;
    QMap<std::string, bool> collateralTxAccepted;
};

#endif // PNMODEL_H
