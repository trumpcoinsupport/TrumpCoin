// Copyright (c) 2014-2016 The Dash Developers
// Copyright (c) 2016-2018 The PIVX developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "patriotnodelist.h"
#include "ui_patriotnodelist.h"

#include "activepatriotnode.h"
#include "clientmodel.h"
#include "guiutil.h"
#include "init.h"
#include "patriotnode-sync.h"
#include "patriotnodeconfig.h"
#include "patriotnodeman.h"
#include "sync.h"
#include "wallet.h"
#include "walletmodel.h"
#include "askpassphrasedialog.h"

#include <QMessageBox>
#include <QTimer>

CCriticalSection cs_patriotnodes;

PatriotnodeList::PatriotnodeList(QWidget* parent) : QWidget(parent),
                                                  ui(new Ui::PatriotnodeList),
                                                  clientModel(0),
                                                  walletModel(0)
{
    ui->setupUi(this);

    ui->startButton->setEnabled(false);

    int columnAliasWidth = 100;
    int columnAddressWidth = 200;
    int columnProtocolWidth = 60;
    int columnStatusWidth = 80;
    int columnActiveWidth = 130;
    int columnLastSeenWidth = 130;

    ui->tableWidgetMyPatriotnodes->setAlternatingRowColors(true);
    ui->tableWidgetMyPatriotnodes->setColumnWidth(0, columnAliasWidth);
    ui->tableWidgetMyPatriotnodes->setColumnWidth(1, columnAddressWidth);
    ui->tableWidgetMyPatriotnodes->setColumnWidth(2, columnProtocolWidth);
    ui->tableWidgetMyPatriotnodes->setColumnWidth(3, columnStatusWidth);
    ui->tableWidgetMyPatriotnodes->setColumnWidth(4, columnActiveWidth);
    ui->tableWidgetMyPatriotnodes->setColumnWidth(5, columnLastSeenWidth);

    ui->tableWidgetMyPatriotnodes->setContextMenuPolicy(Qt::CustomContextMenu);

    QAction* startAliasAction = new QAction(tr("Start alias"), this);
    contextMenu = new QMenu();
    contextMenu->addAction(startAliasAction);
    connect(ui->tableWidgetMyPatriotnodes, SIGNAL(customContextMenuRequested(const QPoint&)), this, SLOT(showContextMenu(const QPoint&)));
    connect(startAliasAction, SIGNAL(triggered()), this, SLOT(on_startButton_clicked()));

    timer = new QTimer(this);
    connect(timer, SIGNAL(timeout()), this, SLOT(updateMyNodeList()));
    timer->start(1000);

    // Fill MN list
    fFilterUpdated = true;
    nTimeFilterUpdated = GetTime();
}

PatriotnodeList::~PatriotnodeList()
{
    delete ui;
}

void PatriotnodeList::setClientModel(ClientModel* model)
{
    this->clientModel = model;
}

void PatriotnodeList::setWalletModel(WalletModel* model)
{
    this->walletModel = model;
}

void PatriotnodeList::showContextMenu(const QPoint& point)
{
    QTableWidgetItem* item = ui->tableWidgetMyPatriotnodes->itemAt(point);
    if (item) contextMenu->exec(QCursor::pos());
}

void PatriotnodeList::StartAlias(std::string strAlias)
{
    std::string strStatusHtml;
    strStatusHtml += "<center>Alias: " + strAlias;

    BOOST_FOREACH (CPatriotnodeConfig::CPatriotnodeEntry mne, patriotnodeConfig.getEntries()) {
        if (mne.getAlias() == strAlias) {
            std::string strError;
            CPatriotnodeBroadcast mnb;

            bool fSuccess = CPatriotnodeBroadcast::Create(mne.getIp(), mne.getPrivKey(), mne.getTxHash(), mne.getOutputIndex(), strError, mnb);

            if (fSuccess) {
                strStatusHtml += "<br>Successfully started patriotnode.";
                mnodeman.UpdatePatriotnodeList(mnb);
                mnb.Relay();
            } else {
                strStatusHtml += "<br>Failed to start patriotnode.<br>Error: " + strError;
            }
            break;
        }
    }
    strStatusHtml += "</center>";

    QMessageBox msg;
    msg.setText(QString::fromStdString(strStatusHtml));
    msg.exec();

    updateMyNodeList(true);
}

void PatriotnodeList::StartAll(std::string strCommand)
{
    int nCountSuccessful = 0;
    int nCountFailed = 0;
    std::string strFailedHtml;

    BOOST_FOREACH (CPatriotnodeConfig::CPatriotnodeEntry mne, patriotnodeConfig.getEntries()) {
        std::string strError;
        CPatriotnodeBroadcast mnb;

        int nIndex;
        if(!mne.castOutputIndex(nIndex))
            continue;

        CTxIn txin = CTxIn(uint256S(mne.getTxHash()), uint32_t(nIndex));
        CPatriotnode* pmn = mnodeman.Find(txin);

        if (strCommand == "start-missing" && pmn) continue;

        bool fSuccess = CPatriotnodeBroadcast::Create(mne.getIp(), mne.getPrivKey(), mne.getTxHash(), mne.getOutputIndex(), strError, mnb);

        if (fSuccess) {
            nCountSuccessful++;
            mnodeman.UpdatePatriotnodeList(mnb);
            mnb.Relay();
        } else {
            nCountFailed++;
            strFailedHtml += "\nFailed to start " + mne.getAlias() + ". Error: " + strError;
        }
    }
    pwalletMain->Lock();

    std::string returnObj;
    returnObj = strprintf("Successfully started %d patriotnodes, failed to start %d, total %d", nCountSuccessful, nCountFailed, nCountFailed + nCountSuccessful);
    if (nCountFailed > 0) {
        returnObj += strFailedHtml;
    }

    QMessageBox msg;
    msg.setText(QString::fromStdString(returnObj));
    msg.exec();

    updateMyNodeList(true);
}

void PatriotnodeList::updateMyPatriotnodeInfo(QString strAlias, QString strAddr, CPatriotnode* pmn)
{
    LOCK(cs_mnlistupdate);
    bool fOldRowFound = false;
    int nNewRow = 0;

    for (int i = 0; i < ui->tableWidgetMyPatriotnodes->rowCount(); i++) {
        if (ui->tableWidgetMyPatriotnodes->item(i, 0)->text() == strAlias) {
            fOldRowFound = true;
            nNewRow = i;
            break;
        }
    }

    if (nNewRow == 0 && !fOldRowFound) {
        nNewRow = ui->tableWidgetMyPatriotnodes->rowCount();
        ui->tableWidgetMyPatriotnodes->insertRow(nNewRow);
    }

    QTableWidgetItem* aliasItem = new QTableWidgetItem(strAlias);
    QTableWidgetItem* addrItem = new QTableWidgetItem(pmn ? QString::fromStdString(pmn->addr.ToString()) : strAddr);
    QTableWidgetItem* protocolItem = new QTableWidgetItem(QString::number(pmn ? pmn->protocolVersion : -1));
    QTableWidgetItem* statusItem = new QTableWidgetItem(QString::fromStdString(pmn ? pmn->GetStatus() : "MISSING"));
    GUIUtil::DHMSTableWidgetItem* activeSecondsItem = new GUIUtil::DHMSTableWidgetItem(pmn ? (pmn->lastPing.sigTime - pmn->sigTime) : 0);
    QTableWidgetItem* lastSeenItem = new QTableWidgetItem(QString::fromStdString(DateTimeStrFormat("%Y-%m-%d %H:%M", pmn ? pmn->lastPing.sigTime : 0)));
    QTableWidgetItem* pubkeyItem = new QTableWidgetItem(QString::fromStdString(pmn ? CBitcoinAddress(pmn->pubKeyCollateralAddress.GetID()).ToString() : ""));

    ui->tableWidgetMyPatriotnodes->setItem(nNewRow, 0, aliasItem);
    ui->tableWidgetMyPatriotnodes->setItem(nNewRow, 1, addrItem);
    ui->tableWidgetMyPatriotnodes->setItem(nNewRow, 2, protocolItem);
    ui->tableWidgetMyPatriotnodes->setItem(nNewRow, 3, statusItem);
    ui->tableWidgetMyPatriotnodes->setItem(nNewRow, 4, activeSecondsItem);
    ui->tableWidgetMyPatriotnodes->setItem(nNewRow, 5, lastSeenItem);
    ui->tableWidgetMyPatriotnodes->setItem(nNewRow, 6, pubkeyItem);
}

void PatriotnodeList::updateMyNodeList(bool fForce)
{
    static int64_t nTimeMyListUpdated = 0;

    // automatically update my patriotnode list only once in MY_PATRIOTNODELIST_UPDATE_SECONDS seconds,
    // this update still can be triggered manually at any time via button click
    int64_t nSecondsTillUpdate = nTimeMyListUpdated + MY_PATRIOTNODELIST_UPDATE_SECONDS - GetTime();
    ui->secondsLabel->setText(QString::number(nSecondsTillUpdate));

    if (nSecondsTillUpdate > 0 && !fForce) return;
    nTimeMyListUpdated = GetTime();

    ui->tableWidgetMyPatriotnodes->setSortingEnabled(false);
    BOOST_FOREACH (CPatriotnodeConfig::CPatriotnodeEntry mne, patriotnodeConfig.getEntries()) {
        int nIndex;
        if(!mne.castOutputIndex(nIndex))
            continue;

        CTxIn txin = CTxIn(uint256S(mne.getTxHash()), uint32_t(nIndex));
        CPatriotnode* pmn = mnodeman.Find(txin);
        updateMyPatriotnodeInfo(QString::fromStdString(mne.getAlias()), QString::fromStdString(mne.getIp()), pmn);
    }
    ui->tableWidgetMyPatriotnodes->setSortingEnabled(true);

    // reset "timer"
    ui->secondsLabel->setText("0");
}

void PatriotnodeList::on_startButton_clicked()
{
    // Find selected node alias
    QItemSelectionModel* selectionModel = ui->tableWidgetMyPatriotnodes->selectionModel();
    QModelIndexList selected = selectionModel->selectedRows();

    if (selected.count() == 0) return;

    QModelIndex index = selected.at(0);
    int nSelectedRow = index.row();
    std::string strAlias = ui->tableWidgetMyPatriotnodes->item(nSelectedRow, 0)->text().toStdString();

    // Display message box
    QMessageBox::StandardButton retval = QMessageBox::question(this, tr("Confirm patriotnode start"),
        tr("Are you sure you want to start patriotnode %1?").arg(QString::fromStdString(strAlias)),
        QMessageBox::Yes | QMessageBox::Cancel,
        QMessageBox::Cancel);

    if (retval != QMessageBox::Yes) return;

    WalletModel::EncryptionStatus encStatus = walletModel->getEncryptionStatus();

    if (encStatus == walletModel->Locked || encStatus == walletModel->UnlockedForAnonymizationOnly) {
        WalletModel::UnlockContext ctx(walletModel->requestUnlock(AskPassphraseDialog::Context::Unlock_Full));

        if (!ctx.isValid()) return; // Unlock wallet was cancelled

        StartAlias(strAlias);
        return;
    }

    StartAlias(strAlias);
}

void PatriotnodeList::on_startAllButton_clicked()
{
    // Display message box
    QMessageBox::StandardButton retval = QMessageBox::question(this, tr("Confirm all patriotnodes start"),
        tr("Are you sure you want to start ALL patriotnodes?"),
        QMessageBox::Yes | QMessageBox::Cancel,
        QMessageBox::Cancel);

    if (retval != QMessageBox::Yes) return;

    WalletModel::EncryptionStatus encStatus = walletModel->getEncryptionStatus();

    if (encStatus == walletModel->Locked || encStatus == walletModel->UnlockedForAnonymizationOnly) {
        WalletModel::UnlockContext ctx(walletModel->requestUnlock(AskPassphraseDialog::Context::Unlock_Full));

        if (!ctx.isValid()) return; // Unlock wallet was cancelled

        StartAll();
        return;
    }

    StartAll();
}

void PatriotnodeList::on_startMissingButton_clicked()
{
    if (!patriotnodeSync.IsPatriotnodeListSynced()) {
        QMessageBox::critical(this, tr("Command is not available right now"),
            tr("You can't use this command until patriotnode list is synced"));
        return;
    }

    // Display message box
    QMessageBox::StandardButton retval = QMessageBox::question(this,
        tr("Confirm missing patriotnodes start"),
        tr("Are you sure you want to start MISSING patriotnodes?"),
        QMessageBox::Yes | QMessageBox::Cancel,
        QMessageBox::Cancel);

    if (retval != QMessageBox::Yes) return;

    WalletModel::EncryptionStatus encStatus = walletModel->getEncryptionStatus();

    if (encStatus == walletModel->Locked || encStatus == walletModel->UnlockedForAnonymizationOnly) {
        WalletModel::UnlockContext ctx(walletModel->requestUnlock(AskPassphraseDialog::Context::Unlock_Full));

        if (!ctx.isValid()) return; // Unlock wallet was cancelled

        StartAll("start-missing");
        return;
    }

    StartAll("start-missing");
}

void PatriotnodeList::on_tableWidgetMyPatriotnodes_itemSelectionChanged()
{
    if (ui->tableWidgetMyPatriotnodes->selectedItems().count() > 0) {
        ui->startButton->setEnabled(true);
    }
}

void PatriotnodeList::on_UpdateButton_clicked()
{
    updateMyNodeList(true);
}
