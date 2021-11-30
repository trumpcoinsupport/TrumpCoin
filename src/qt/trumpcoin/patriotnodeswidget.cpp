// Copyright (c) 2019-2020 The TrumpCoin developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "qt/trumpcoin/patriotnodeswidget.h"
#include "qt/trumpcoin/forms/ui_patriotnodeswidget.h"

#include "qt/trumpcoin/qtutils.h"
#include "qt/trumpcoin/mnrow.h"
#include "qt/trumpcoin/mninfodialog.h"
#include "qt/trumpcoin/patriotnodewizarddialog.h"

#include "activepatriotnode.h"
#include "clientmodel.h"
#include "fs.h"
#include "guiutil.h"
#include "init.h"
#include "patriotnode-sync.h"
#include "patriotnodeconfig.h"
#include "patriotnodeman.h"
#include "util/system.h"
#include "qt/trumpcoin/optionbutton.h"
#include <fstream>

#define DECORATION_SIZE 65
#define NUM_ITEMS 3
#define REQUEST_START_ALL 1
#define REQUEST_START_MISSING 2

class PNHolder : public FurListRow<QWidget*>
{
public:
    explicit PNHolder(bool _isLightTheme) : FurListRow(), isLightTheme(_isLightTheme) {}

    PNRow* createHolder(int pos) override
    {
        if (!cachedRow) cachedRow = new PNRow();
        return cachedRow;
    }

    void init(QWidget* holder,const QModelIndex &index, bool isHovered, bool isSelected) const override
    {
        PNRow* row = static_cast<PNRow*>(holder);
        QString label = index.data(Qt::DisplayRole).toString();
        QString address = index.sibling(index.row(), PNModel::ADDRESS).data(Qt::DisplayRole).toString();
        QString status = index.sibling(index.row(), PNModel::STATUS).data(Qt::DisplayRole).toString();
        bool wasCollateralAccepted = index.sibling(index.row(), PNModel::WAS_COLLATERAL_ACCEPTED).data(Qt::DisplayRole).toBool();
        row->updateView("Address: " + address, label, status, wasCollateralAccepted);
    }

    QColor rectColor(bool isHovered, bool isSelected) override
    {
        return getRowColor(isLightTheme, isHovered, isSelected);
    }

    ~PNHolder() override{}

    bool isLightTheme;
    PNRow* cachedRow = nullptr;
};

PatriotNodesWidget::PatriotNodesWidget(TrumpCoinGUI *parent) :
    PWidget(parent),
    ui(new Ui::PatriotNodesWidget),
    isLoading(false)
{
    ui->setupUi(this);

    delegate = new FurAbstractListItemDelegate(
            DECORATION_SIZE,
            new PNHolder(isLightTheme()),
            this
    );

    this->setStyleSheet(parent->styleSheet());

    /* Containers */
    setCssProperty(ui->left, "container");
    ui->left->setContentsMargins(0,20,0,20);
    setCssProperty(ui->right, "container-right");
    ui->right->setContentsMargins(20,20,20,20);

    /* Light Font */
    QFont fontLight;
    fontLight.setWeight(QFont::Light);

    /* Title */
    setCssTitleScreen(ui->labelTitle);
    ui->labelTitle->setFont(fontLight);
    setCssSubtitleScreen(ui->labelSubtitle1);

    /* Buttons */
    setCssBtnPrimary(ui->pushButtonSave);
    setCssBtnPrimary(ui->pushButtonStartAll);
    setCssBtnPrimary(ui->pushButtonStartMissing);

    /* Options */
    ui->btnAbout->setTitleClassAndText("btn-title-grey", tr("What is a Patriotnode?"));
    ui->btnAbout->setSubTitleClassAndText("text-subtitle", tr("FAQ explaining what Patriotnodes are"));
    ui->btnAboutController->setTitleClassAndText("btn-title-grey", tr("What is a Controller?"));
    ui->btnAboutController->setSubTitleClassAndText("text-subtitle", tr("FAQ explaining what is a Patriotnode Controller"));

    setCssProperty(ui->listMn, "container");
    ui->listMn->setItemDelegate(delegate);
    ui->listMn->setIconSize(QSize(DECORATION_SIZE, DECORATION_SIZE));
    ui->listMn->setMinimumHeight(NUM_ITEMS * (DECORATION_SIZE + 2));
    ui->listMn->setAttribute(Qt::WA_MacShowFocusRect, false);
    ui->listMn->setSelectionBehavior(QAbstractItemView::SelectRows);

    ui->emptyContainer->setVisible(false);
    setCssProperty(ui->pushImgEmpty, "img-empty-master");
    setCssProperty(ui->labelEmpty, "text-empty");

    connect(ui->pushButtonSave, &QPushButton::clicked, this, &PatriotNodesWidget::onCreatePNClicked);
    connect(ui->pushButtonStartAll, &QPushButton::clicked, [this]() {
        onStartAllClicked(REQUEST_START_ALL);
    });
    connect(ui->pushButtonStartMissing, &QPushButton::clicked, [this]() {
        onStartAllClicked(REQUEST_START_MISSING);
    });
    connect(ui->listMn, &QListView::clicked, this, &PatriotNodesWidget::onPNClicked);
    connect(ui->btnAbout, &OptionButton::clicked, [this](){window->openFAQ(SettingsFaqWidget::Section::PATRIOTNODE);});
    connect(ui->btnAboutController, &OptionButton::clicked, [this](){window->openFAQ(SettingsFaqWidget::Section::PNCONTROLLER);});
}

void PatriotNodesWidget::showEvent(QShowEvent *event)
{
    if (mnModel) mnModel->updatePNList();
    if (!timer) {
        timer = new QTimer(this);
        connect(timer, &QTimer::timeout, [this]() {mnModel->updatePNList();});
    }
    timer->start(30000);
}

void PatriotNodesWidget::hideEvent(QHideEvent *event)
{
    if (timer) timer->stop();
}

void PatriotNodesWidget::loadWalletModel()
{
    if (walletModel) {
        mnModel = new PNModel(this, walletModel);
        ui->listMn->setModel(mnModel);
        ui->listMn->setModelColumn(AddressTableModel::Label);
        updateListState();
    }
}

void PatriotNodesWidget::updateListState()
{
    bool show = mnModel->rowCount() > 0;
    ui->listMn->setVisible(show);
    ui->emptyContainer->setVisible(!show);
    ui->pushButtonStartAll->setVisible(show);
}

void PatriotNodesWidget::onPNClicked(const QModelIndex &index)
{
    ui->listMn->setCurrentIndex(index);
    QRect rect = ui->listMn->visualRect(index);
    QPoint pos = rect.topRight();
    pos.setX(pos.x() - (DECORATION_SIZE * 2));
    pos.setY(pos.y() + (DECORATION_SIZE * 1.5));
    if (!this->menu) {
        this->menu = new TooltipMenu(window, this);
        this->menu->setEditBtnText(tr("Start"));
        this->menu->setDeleteBtnText(tr("Delete"));
        this->menu->setCopyBtnText(tr("Info"));
        connect(this->menu, &TooltipMenu::message, this, &AddressesWidget::message);
        connect(this->menu, &TooltipMenu::onEditClicked, this, &PatriotNodesWidget::onEditPNClicked);
        connect(this->menu, &TooltipMenu::onDeleteClicked, this, &PatriotNodesWidget::onDeletePNClicked);
        connect(this->menu, &TooltipMenu::onCopyClicked, this, &PatriotNodesWidget::onInfoPNClicked);
        this->menu->adjustSize();
    } else {
        this->menu->hide();
    }
    this->index = index;
    menu->move(pos);
    menu->show();

    // Back to regular status
    ui->listMn->scrollTo(index);
    ui->listMn->clearSelection();
    ui->listMn->setFocus();
}

bool PatriotNodesWidget::checkPNsNetwork()
{
    bool isTierTwoSync = mnModel->isPNsNetworkSynced();
    if (!isTierTwoSync) inform(tr("Please wait until the node is fully synced"));
    return isTierTwoSync;
}

void PatriotNodesWidget::onEditPNClicked()
{
    if (walletModel) {
        if (!walletModel->isRegTestNetwork() && !checkPNsNetwork()) return;
        if (index.sibling(index.row(), PNModel::WAS_COLLATERAL_ACCEPTED).data(Qt::DisplayRole).toBool()) {
            // Start PN
            QString strAlias = this->index.data(Qt::DisplayRole).toString();
            if (ask(tr("Start Patriotnode"), tr("Are you sure you want to start patriotnode %1?\n").arg(strAlias))) {
                WalletModel::UnlockContext ctx(walletModel->requestUnlock());
                if (!ctx.isValid()) {
                    // Unlock wallet was cancelled
                    inform(tr("Cannot edit patriotnode, wallet locked"));
                    return;
                }
                startAlias(strAlias);
            }
        } else {
            inform(tr("Cannot start patriotnode, the collateral transaction has not been confirmed by the network yet.\n"
                    "Please wait few more minutes (patriotnode collaterals require %1 confirmations).").arg(PatriotnodeCollateralMinConf()));
        }
    }
}

void PatriotNodesWidget::startAlias(const QString& strAlias)
{
    QString strStatusHtml;
    strStatusHtml += "Alias: " + strAlias + " ";

    for (const auto& mne : patriotnodeConfig.getEntries()) {
        if (mne.getAlias() == strAlias.toStdString()) {
            std::string strError;
            strStatusHtml += (!startPN(mne, strError)) ? ("failed to start.\nError: " + QString::fromStdString(strError)) : "successfully started.";
            break;
        }
    }
    // update UI and notify
    updateModelAndInform(strStatusHtml);
}

void PatriotNodesWidget::updateModelAndInform(const QString& informText)
{
    mnModel->updatePNList();
    inform(informText);
}

bool PatriotNodesWidget::startPN(const CPatriotnodeConfig::CPatriotnodeEntry& mne, std::string& strError)
{
    CPatriotnodeBroadcast mnb;
    if (!CPatriotnodeBroadcast::Create(mne.getIp(), mne.getPrivKey(), mne.getTxHash(), mne.getOutputIndex(), strError, mnb, false, walletModel->getLastBlockProcessedNum()))
        return false;

    mnodeman.UpdatePatriotnodeList(mnb);
    if (activePatriotnode.pubKeyPatriotnode == mnb.GetPubKey()) {
        activePatriotnode.EnableHotColdPatriotNode(mnb.vin, mnb.addr);
    }
    mnb.Relay();
    return true;
}

void PatriotNodesWidget::onStartAllClicked(int type)
{
    if (!Params().IsRegTestNet() && !checkPNsNetwork()) return;     // skip on RegNet: so we can test even if tier two not synced

    if (isLoading) {
        inform(tr("Background task is being executed, please wait"));
    } else {
        std::unique_ptr<WalletModel::UnlockContext> pctx = std::make_unique<WalletModel::UnlockContext>(walletModel->requestUnlock());
        if (!pctx->isValid()) {
            warn(tr("Start ALL patriotnodes failed"), tr("Wallet unlock cancelled"));
            return;
        }
        isLoading = true;
        if (!execute(type, std::move(pctx))) {
            isLoading = false;
            inform(tr("Cannot perform Patriotnodes start"));
        }
    }
}

bool PatriotNodesWidget::startAll(QString& failText, bool onlyMissing)
{
    int amountOfMnFailed = 0;
    int amountOfMnStarted = 0;
    for (const auto& mne : patriotnodeConfig.getEntries()) {
        // Check for missing only
        QString mnAlias = QString::fromStdString(mne.getAlias());
        if (onlyMissing && !mnModel->isPNInactive(mnAlias)) {
            if (!mnModel->isPNActive(mnAlias))
                amountOfMnFailed++;
            continue;
        }

        if (!mnModel->isPNCollateralMature(mnAlias)) {
            amountOfMnFailed++;
            continue;
        }

        std::string strError;
        if (!startPN(mne, strError)) {
            amountOfMnFailed++;
        } else {
            amountOfMnStarted++;
        }
    }
    if (amountOfMnFailed > 0) {
        failText = tr("%1 Patriotnodes failed to start, %2 started").arg(amountOfMnFailed).arg(amountOfMnStarted);
        return false;
    }
    return true;
}

void PatriotNodesWidget::run(int type)
{
    bool isStartMissing = type == REQUEST_START_MISSING;
    if (type == REQUEST_START_ALL || isStartMissing) {
        QString failText;
        QString inform = startAll(failText, isStartMissing) ? tr("All Patriotnodes started!") : failText;
        QMetaObject::invokeMethod(this, "updateModelAndInform", Qt::QueuedConnection,
                                  Q_ARG(QString, inform));
    }

    isLoading = false;
}

void PatriotNodesWidget::onError(QString error, int type)
{
    if (type == REQUEST_START_ALL) {
        QMetaObject::invokeMethod(this, "inform", Qt::QueuedConnection,
                                  Q_ARG(QString, "Error starting all Patriotnodes"));
    }
}

void PatriotNodesWidget::onInfoPNClicked()
{
    WalletModel::UnlockContext ctx(walletModel->requestUnlock());
    if (!ctx.isValid()) {
        // Unlock wallet was cancelled
        inform(tr("Cannot show Patriotnode information, wallet locked"));
        return;
    }
    showHideOp(true);
    MnInfoDialog* dialog = new MnInfoDialog(window);
    QString label = index.data(Qt::DisplayRole).toString();
    QString address = index.sibling(index.row(), PNModel::ADDRESS).data(Qt::DisplayRole).toString();
    QString status = index.sibling(index.row(), PNModel::STATUS).data(Qt::DisplayRole).toString();
    QString txId = index.sibling(index.row(), PNModel::COLLATERAL_ID).data(Qt::DisplayRole).toString();
    QString outIndex = index.sibling(index.row(), PNModel::COLLATERAL_OUT_INDEX).data(Qt::DisplayRole).toString();
    QString pubKey = index.sibling(index.row(), PNModel::PUB_KEY).data(Qt::DisplayRole).toString();
    dialog->setData(pubKey, label, address, txId, outIndex, status);
    dialog->adjustSize();
    showDialog(dialog, 3, 17);
    if (dialog->exportPN) {
        if (ask(tr("Remote Patriotnode Data"),
                tr("You are just about to export the required data to run a Patriotnode\non a remote server to your clipboard.\n\n\n"
                   "You will only have to paste the data in the trumpcoin.conf file\nof your remote server and start it, "
                   "then start the Patriotnode using\nthis controller wallet (select the Patriotnode in the list and press \"start\").\n"
                ))) {
            // export data
            QString exportedPN = "patriotnode=1\n"
                                 "externalip=" + address.left(address.lastIndexOf(":")) + "\n" +
                                 "patriotnodeaddr=" + address + + "\n" +
                                 "patriotnodeprivkey=" + index.sibling(index.row(), PNModel::PRIV_KEY).data(Qt::DisplayRole).toString() + "\n";
            GUIUtil::setClipboard(exportedPN);
            inform(tr("Patriotnode data copied to the clipboard."));
        }
    }

    dialog->deleteLater();
}

void PatriotNodesWidget::onDeletePNClicked()
{
    QString txId = index.sibling(index.row(), PNModel::COLLATERAL_ID).data(Qt::DisplayRole).toString();
    QString outIndex = index.sibling(index.row(), PNModel::COLLATERAL_OUT_INDEX).data(Qt::DisplayRole).toString();
    QString qAliasString = index.data(Qt::DisplayRole).toString();
    std::string aliasToRemove = qAliasString.toStdString();

    if (!ask(tr("Delete Patriotnode"), tr("You are just about to delete Patriotnode:\n%1\n\nAre you sure?").arg(qAliasString)))
        return;

    std::string strConfFile = "patriotnode.conf";
    std::string strDataDir = GetDataDir().string();
    fs::path conf_file_path(strConfFile);
    if (strConfFile != conf_file_path.filename().string()) {
        throw std::runtime_error(strprintf(_("patriotnode.conf %s resides outside data directory %s"), strConfFile, strDataDir));
    }

    fs::path pathBootstrap = GetDataDir() / strConfFile;
    if (fs::exists(pathBootstrap)) {
        fs::path pathPatriotnodeConfigFile = GetPatriotnodeConfigFile();
        fsbridge::ifstream streamConfig(pathPatriotnodeConfigFile);

        if (!streamConfig.good()) {
            inform(tr("Invalid patriotnode.conf file"));
            return;
        }

        int lineNumToRemove = -1;
        int linenumber = 1;
        std::string lineCopy = "";
        for (std::string line; std::getline(streamConfig, line); linenumber++) {
            if (line.empty()) continue;

            std::istringstream iss(line);
            std::string comment, alias, ip, privKey, txHash, outputIndex;

            if (iss >> comment) {
                if (comment.at(0) == '#') continue;
                iss.str(line);
                iss.clear();
            }

            if (!(iss >> alias >> ip >> privKey >> txHash >> outputIndex)) {
                iss.str(line);
                iss.clear();
                if (!(iss >> alias >> ip >> privKey >> txHash >> outputIndex)) {
                    streamConfig.close();
                    inform(tr("Error parsing patriotnode.conf file"));
                    return;
                }
            }

            if (aliasToRemove == alias) {
                lineNumToRemove = linenumber;
            } else
                lineCopy += line + "\n";

        }

        if (lineCopy.size() == 0) {
            lineCopy = "# Patriotnode config file\n"
                                    "# Format: alias IP:port patriotnodeprivkey collateral_output_txid collateral_output_index\n"
                                    "# Example: mn1 127.0.0.2:15110 93HaYBVUCYjEMeeH1Y4sBGLALQZE1Yc1K64xiqgX37tGBDQL8Xg 2bcd3c84c84f87eaa86e4e56834c92927a07f9e18718810b92e0d0324456a67c 0\n";
        }

        streamConfig.close();

        if (lineNumToRemove != -1) {
            fs::path pathConfigFile = AbsPathForConfigVal(fs::path("patriotnode_temp.conf"));
            FILE* configFile = fsbridge::fopen(pathConfigFile, "w");
            fwrite(lineCopy.c_str(), std::strlen(lineCopy.c_str()), 1, configFile);
            fclose(configFile);

            fs::path pathOldConfFile = AbsPathForConfigVal(fs::path("old_patriotnode.conf"));
            if (fs::exists(pathOldConfFile)) {
                fs::remove(pathOldConfFile);
            }
            rename(pathPatriotnodeConfigFile, pathOldConfFile);

            fs::path pathNewConfFile = AbsPathForConfigVal(fs::path("patriotnode.conf"));
            rename(pathConfigFile, pathNewConfFile);

            // Unlock collateral
            bool convertOK = false;
            unsigned int indexOut = outIndex.toUInt(&convertOK);
            if (convertOK) {
                COutPoint collateralOut(uint256S(txId.toStdString()), indexOut);
                walletModel->unlockCoin(collateralOut);
            }

            // Remove alias
            patriotnodeConfig.remove(aliasToRemove);
            // Update list
            mnModel->removeMn(index);
            updateListState();
        }
    } else {
        inform(tr("patriotnode.conf file doesn't exists"));
    }
}

void PatriotNodesWidget::onCreatePNClicked()
{
    WalletModel::UnlockContext ctx(walletModel->requestUnlock());
    if (!ctx.isValid()) {
        // Unlock wallet was cancelled
        inform(tr("Cannot create Patriotnode controller, wallet locked"));
        return;
    }

    CAmount mnCollateralAmount = clientModel->getPNCollateralRequiredAmount();
    if (walletModel->getBalance() <= mnCollateralAmount) {
        inform(tr("Not enough balance to create a patriotnode, %1 required.")
            .arg(GUIUtil::formatBalance(mnCollateralAmount, BitcoinUnits::TRUMP)));
        return;
    }
    showHideOp(true);
    PatriotNodeWizardDialog *dialog = new PatriotNodeWizardDialog(walletModel, clientModel, window);
    if (openDialogWithOpaqueBackgroundY(dialog, window, 5, 7)) {
        if (dialog->isOk) {
            // Update list
            mnModel->addMn(dialog->mnEntry);
            updateListState();
            // add mn
            inform(dialog->returnStr);
        } else {
            warn(tr("Error creating patriotnode"), dialog->returnStr);
        }
    }
    dialog->deleteLater();
}

void PatriotNodesWidget::changeTheme(bool isLightTheme, QString& theme)
{
    static_cast<PNHolder*>(this->delegate->getRowFactory())->isLightTheme = isLightTheme;
}

PatriotNodesWidget::~PatriotNodesWidget()
{
    delete ui;
}
