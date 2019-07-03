// Copyright (c) 2014-2016 The Dash Developers
// Copyright (c) 2016-2017 The PIVX developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef PATRIOTNODELIST_H
#define PATRIOTNODELIST_H

#include "patriotnode.h"
#include "platformstyle.h"
#include "sync.h"
#include "util.h"

#include <QMenu>
#include <QTimer>
#include <QWidget>

#define MY_PATRIOTNODELIST_UPDATE_SECONDS 60
#define PATRIOTNODELIST_UPDATE_SECONDS 15
#define PATRIOTNODELIST_FILTER_COOLDOWN_SECONDS 3

namespace Ui
{
class PatriotnodeList;
}

class ClientModel;
class WalletModel;

QT_BEGIN_NAMESPACE
class QModelIndex;
QT_END_NAMESPACE

/** Patriotnode Manager page widget */
class PatriotnodeList : public QWidget
{
    Q_OBJECT

public:
    explicit PatriotnodeList(QWidget* parent = 0);
    ~PatriotnodeList();

    void setClientModel(ClientModel* clientModel);
    void setWalletModel(WalletModel* walletModel);
    void StartAlias(std::string strAlias);
    void StartAll(std::string strCommand = "start-all");

private:
    QMenu* contextMenu;
    int64_t nTimeFilterUpdated;
    bool fFilterUpdated;

public Q_SLOTS:
    void updateMyPatriotnodeInfo(QString strAlias, QString strAddr, CPatriotnode* pmn);
    void updateMyNodeList(bool fForce = false);

Q_SIGNALS:

private:
    QTimer* timer;
    Ui::PatriotnodeList* ui;
    ClientModel* clientModel;
    WalletModel* walletModel;
    CCriticalSection cs_mnlistupdate;
    QString strCurrentFilter;

private Q_SLOTS:
    void showContextMenu(const QPoint&);
    void on_startButton_clicked();
    void on_startAllButton_clicked();
    void on_startMissingButton_clicked();
    void on_tableWidgetMyPatriotnodes_itemSelectionChanged();
    void on_UpdateButton_clicked();
};
#endif // PATRIOTNODELIST_H
