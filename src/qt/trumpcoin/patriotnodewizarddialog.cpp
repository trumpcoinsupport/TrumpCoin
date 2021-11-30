// Copyright (c) 2019-2020 The TrumpCoin developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "qt/trumpcoin/patriotnodewizarddialog.h"
#include "qt/trumpcoin/forms/ui_patriotnodewizarddialog.h"

#include "activepatriotnode.h"
#include "clientmodel.h"
#include "key_io.h"
#include "optionsmodel.h"
#include "qt/trumpcoin/mnmodel.h"
#include "qt/trumpcoin/guitransactionsutils.h"
#include "qt/trumpcoin/qtutils.h"
#include "qt/walletmodeltransaction.h"

#include <QFile>
#include <QIntValidator>
#include <QHostAddress>
#include <QRegularExpression>

static inline QString formatParagraph(const QString& str) {
    return "<p align=\"justify\" style=\"text-align:center;\">" + str + "</p>";
}

static inline QString formatHtmlContent(const QString& str) {
    return "<html><body>" + str + "</body></html>";
}

PatriotNodeWizardDialog::PatriotNodeWizardDialog(WalletModel* model, ClientModel* _clientModel, QWidget *parent) :
    FocusedDialog(parent),
    ui(new Ui::PatriotNodeWizardDialog),
    icConfirm1(new QPushButton()),
    icConfirm3(new QPushButton()),
    icConfirm4(new QPushButton()),
    walletModel(model),
    clientModel(_clientModel)
{
    ui->setupUi(this);

    this->setStyleSheet(parent->styleSheet());
    setCssProperty(ui->frame, "container-dialog");
    ui->frame->setContentsMargins(10,10,10,10);

    setCssProperty({ui->labelLine1, ui->labelLine3}, "line-purple");
    setCssProperty({ui->groupBoxName, ui->groupContainer}, "container-border");
    setCssProperty({ui->pushNumber1, ui->pushNumber3, ui->pushNumber4}, "btn-number-check");
    setCssProperty({ui->pushName1, ui->pushName3, ui->pushName4}, "btn-name-check");

    ui->pushNumber1->setEnabled(false);
    ui->pushNumber3->setEnabled(false);
    ui->pushNumber4->setEnabled(false);
    ui->pushName1->setEnabled(false);
    ui->pushName3->setEnabled(false);
    ui->pushName4->setEnabled(false);

    // Frame 1
    setCssProperty(ui->labelTitle1, "text-title-dialog");
    setCssProperty(ui->labelMessage1a, "text-main-grey");
    setCssProperty(ui->labelMessage1b, "text-main-purple");

    QString collateralAmountStr = GUIUtil::formatBalance(clientModel->getPNCollateralRequiredAmount());
    ui->labelMessage1a->setText(formatHtmlContent(
                formatParagraph(tr("To create a TrumpCoin Patriotnode you must dedicate %1 (the unit of TrumpCoin) "
                        "to the network (however, these coins are still yours and will never leave your possession).").arg(collateralAmountStr)) +
                formatParagraph(tr("You can deactivate the node and unlock the coins at any time."))));

    // Frame 3
    setCssProperty(ui->labelTitle3, "text-title-dialog");
    setCssProperty(ui->labelMessage3, "text-main-grey");

    ui->labelMessage3->setText(formatHtmlContent(
                formatParagraph(tr("A transaction of %1 will be made").arg(collateralAmountStr)) +
                formatParagraph(tr("to a new empty address in your wallet.")) +
                formatParagraph(tr("The Address is labeled under the patriot node's name."))));

    initCssEditLine(ui->lineEditName);
    // PN alias must not contain spaces or "#" character
    QRegularExpression rx("^(?:(?![\\#\\s]).)*");
    ui->lineEditName->setValidator(new QRegularExpressionValidator(rx, ui->lineEditName));

    // Frame 4
    setCssProperty(ui->labelTitle4, "text-title-dialog");
    setCssProperty({ui->labelSubtitleIp, ui->labelSubtitlePort}, "text-title");
    setCssSubtitleScreen(ui->labelSubtitleAddressIp);

    initCssEditLine(ui->lineEditIpAddress);
    initCssEditLine(ui->lineEditPort);
    ui->stackedWidget->setCurrentIndex(pos);
    ui->lineEditPort->setEnabled(false);    // use default port number
    if (walletModel->isRegTestNetwork()) {
        ui->lineEditPort->setText("51476");
    } else if (walletModel->isTestNetwork()) {
        ui->lineEditPort->setText("51474");
    } else {
        ui->lineEditPort->setText("15110");
    }

    // Confirm icons
    ui->stackedIcon1->addWidget(icConfirm1);
    ui->stackedIcon3->addWidget(icConfirm3);
    ui->stackedIcon4->addWidget(icConfirm4);
    initBtn({icConfirm1, icConfirm3, icConfirm4});
    setCssProperty({icConfirm1, icConfirm3, icConfirm4}, "ic-step-confirm");

    // Connect btns
    setCssBtnPrimary(ui->btnNext);
    setCssProperty(ui->btnBack , "btn-dialog-cancel");
    ui->btnBack->setVisible(false);
    setCssProperty(ui->pushButtonSkip, "ic-close");

    connect(ui->pushButtonSkip, &QPushButton::clicked, this, &PatriotNodeWizardDialog::close);
    connect(ui->btnNext, &QPushButton::clicked, this, &PatriotNodeWizardDialog::accept);
    connect(ui->btnBack, &QPushButton::clicked, this, &PatriotNodeWizardDialog::onBackClicked);
}

void PatriotNodeWizardDialog::showEvent(QShowEvent *event)
{
    if (ui->btnNext) ui->btnNext->setFocus();
}

void PatriotNodeWizardDialog::accept()
{
    switch(pos) {
        case 0:{
            ui->stackedWidget->setCurrentIndex(1);
            ui->pushName4->setChecked(false);
            ui->pushName3->setChecked(true);
            ui->pushName1->setChecked(true);
            icConfirm1->setVisible(true);
            ui->pushNumber3->setChecked(true);
            ui->btnBack->setVisible(true);
            ui->lineEditName->setFocus();
            break;
        }
        case 1:{

            // No empty names accepted.
            if (ui->lineEditName->text().isEmpty()) {
                setCssEditLine(ui->lineEditName, false, true);
                return;
            }
            setCssEditLine(ui->lineEditName, true, true);

            ui->stackedWidget->setCurrentIndex(2);
            ui->pushName4->setChecked(false);
            ui->pushName3->setChecked(true);
            ui->pushName1->setChecked(true);
            icConfirm3->setVisible(true);
            ui->pushNumber4->setChecked(true);
            ui->btnBack->setVisible(true);
            ui->lineEditIpAddress->setFocus();
            break;
        }
        case 2:{

            // No empty address accepted
            if (ui->lineEditIpAddress->text().isEmpty()) {
                return;
            }

            icConfirm4->setVisible(true);
            ui->btnBack->setVisible(true);
            ui->btnBack->setVisible(true);
            isOk = createPN();
            QDialog::accept();
        }
    }
    pos++;
}

bool PatriotNodeWizardDialog::createPN()
{
    if (!walletModel) {
        returnStr = tr("walletModel not set");
        return false;
    }

    /**
     *
    1) generate the mn key.
    2) create the mn address.
    3) if there is a valid (unlocked) collateral utxo, use it
    4) otherwise create a receiving address and send a tx with 10k to it.
    5) get the collateral output.
    6) use those values on the patriotnode.conf
     */

    // validate IP address
    QString addressLabel = ui->lineEditName->text();
    if (addressLabel.isEmpty()) {
        returnStr = tr("address label cannot be empty");
        return false;
    }
    std::string alias = addressLabel.toStdString();

    QString addressStr = ui->lineEditIpAddress->text();
    QString portStr = ui->lineEditPort->text();
    if (addressStr.isEmpty() || portStr.isEmpty()) {
        returnStr = tr("IP or port cannot be empty");
        return false;
    }
    if (!PNModel::validatePNIP(addressStr)) {
        returnStr = tr("Invalid IP address");
        return false;
    }

    // ip + port
    std::string ipAddress = addressStr.toStdString();
    std::string port = portStr.toStdString();

    // create the mn key
    CKey secret;
    secret.MakeNewKey(false);
    std::string mnKeyString = KeyIO::EncodeSecret(secret);

    // Look for a valid collateral utxo
    COutPoint collateralOut;

    // If not found create a new collateral tx
    if (!walletModel->getPNCollateralCandidate(collateralOut)) {
        // New receive address
        auto r = walletModel->getNewAddress(alias);
        if (!r) {
            // generate address fail
            inform(tr(r.getError().c_str()));
            return false;
        }

        // const QString& addr, const QString& label, const CAmount& amount, const QString& message
        SendCoinsRecipient sendCoinsRecipient(
                QString::fromStdString(r.getObjResult()->ToString()),
                QString::fromStdString(alias),
                clientModel->getPNCollateralRequiredAmount(),
                "");

        // Send the 10 tx to one of your address
        QList<SendCoinsRecipient> recipients;
        recipients.append(sendCoinsRecipient);
        WalletModelTransaction currentTransaction(recipients);
        WalletModel::SendCoinsReturn prepareStatus;

        // no coincontrol, no P2CS delegations
        prepareStatus = walletModel->prepareTransaction(&currentTransaction, nullptr, false);

        QString returnMsg = tr("Unknown error");
        // process prepareStatus and on error generate message shown to user
        CClientUIInterface::MessageBoxFlags informType;
        returnMsg = GuiTransactionsUtils::ProcessSendCoinsReturn(
                this,
                prepareStatus,
                walletModel,
                informType, // this flag is not needed
                BitcoinUnits::formatWithUnit(walletModel->getOptionsModel()->getDisplayUnit(),
                                             currentTransaction.getTransactionFee()),
                true
        );

        if (prepareStatus.status != WalletModel::OK) {
            returnStr = tr("Prepare patriot node failed.\n\n%1\n").arg(returnMsg);
            return false;
        }

        WalletModel::SendCoinsReturn sendStatus = walletModel->sendCoins(currentTransaction);
        // process sendStatus and on error generate message shown to user
        returnMsg = GuiTransactionsUtils::ProcessSendCoinsReturn(
                this,
                sendStatus,
                walletModel,
                informType
        );

        if (sendStatus.status != WalletModel::OK) {
            returnStr = tr("Cannot send collateral transaction.\n\n%1").arg(returnMsg);
            return false;
        }

        // look for the tx index of the collateral
        CTransactionRef walletTx = currentTransaction.getTransaction();
        std::string txID = walletTx->GetHash().GetHex();
        int indexOut = -1;
        for (int i=0; i < (int)walletTx->vout.size(); i++) {
            const CTxOut& out = walletTx->vout[i];
            if (out.nValue == clientModel->getPNCollateralRequiredAmount()) {
                indexOut = i;
                break;
            }
        }
        if (indexOut == -1) {
            returnStr = tr("Invalid collateral output index");
            return false;
        }
        // save the collateral outpoint
        collateralOut = COutPoint(walletTx->GetHash(), indexOut);
    }

    // Update the conf file
    std::string strConfFile = "patriotnode.conf";
    std::string strDataDir = GetDataDir().string();
    fs::path conf_file_path(strConfFile);
    if (strConfFile != conf_file_path.filename().string()) {
        throw std::runtime_error(strprintf(_("patriotnode.conf %s resides outside data directory %s"), strConfFile, strDataDir));
    }

    fs::path pathBootstrap = GetDataDir() / strConfFile;
    if (!fs::exists(pathBootstrap)) {
        returnStr = tr("patriotnode.conf file doesn't exists");
        return false;
    }

    fs::path pathPatriotnodeConfigFile = GetPatriotnodeConfigFile();
    fsbridge::ifstream streamConfig(pathPatriotnodeConfigFile);

    if (!streamConfig.good()) {
        returnStr = tr("Invalid patriotnode.conf file");
        return false;
    }

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
                returnStr = tr("Error parsing patriotnode.conf file");
                return false;
            }
        }
        lineCopy += line + "\n";
    }

    if (lineCopy.size() == 0) {
        lineCopy = "# Patriotnode config file\n"
                   "# Format: alias IP:port patriotnodeprivkey collateral_output_txid collateral_output_index\n"
                   "# Example: mn1 127.0.0.2:15110 93HaYBVUCYjEMeeH1Y4sBGLALQZE1Yc1K64xiqgX37tGBDQL8Xg 2bcd3c84c84f87eaa86e4e56834c92927a07f9e18718810b92e0d0324456a67c 0"
                   "#";
    }
    lineCopy += "\n";

    streamConfig.close();

    std::string txID = collateralOut.hash.ToString();
    std::string indexOutStr = std::to_string(collateralOut.n);

    // Check IP address type
    QHostAddress hostAddress(addressStr);
    QAbstractSocket::NetworkLayerProtocol layerProtocol = hostAddress.protocol();
    if (layerProtocol == QAbstractSocket::IPv6Protocol) {
        ipAddress = "["+ipAddress+"]";
    }

    fs::path pathConfigFile = AbsPathForConfigVal(fs::path("patriotnode_temp.conf"));
    FILE* configFile = fopen(pathConfigFile.string().c_str(), "w");
    lineCopy += alias+" "+ipAddress+":"+port+" "+mnKeyString+" "+txID+" "+indexOutStr+"\n";
    fwrite(lineCopy.c_str(), std::strlen(lineCopy.c_str()), 1, configFile);
    fclose(configFile);

    fs::path pathOldConfFile = AbsPathForConfigVal(fs::path("old_patriotnode.conf"));
    if (fs::exists(pathOldConfFile)) {
        fs::remove(pathOldConfFile);
    }
    rename(pathPatriotnodeConfigFile, pathOldConfFile);

    fs::path pathNewConfFile = AbsPathForConfigVal(fs::path("patriotnode.conf"));
    rename(pathConfigFile, pathNewConfFile);

    mnEntry = patriotnodeConfig.add(alias, ipAddress+":"+port, mnKeyString, txID, indexOutStr);

    // Lock collateral output
    walletModel->lockCoin(collateralOut);

    returnStr = tr("Patriot node created! Wait %1 confirmations before starting it.").arg(PatriotnodeCollateralMinConf());
    return true;
}

void PatriotNodeWizardDialog::onBackClicked()
{
    if (pos == 0) return;
    pos--;
    switch(pos) {
        case 0:{
            ui->stackedWidget->setCurrentIndex(0);
            ui->btnNext->setFocus();
            ui->pushNumber1->setChecked(true);
            ui->pushNumber4->setChecked(false);
            ui->pushNumber3->setChecked(false);
            ui->pushName4->setChecked(false);
            ui->pushName3->setChecked(false);
            ui->pushName1->setChecked(true);
            icConfirm1->setVisible(false);
            ui->btnBack->setVisible(false);
            break;
        }
        case 1:{
            ui->stackedWidget->setCurrentIndex(1);
            ui->lineEditName->setFocus();
            ui->pushNumber4->setChecked(false);
            ui->pushNumber3->setChecked(true);
            ui->pushName4->setChecked(false);
            ui->pushName3->setChecked(true);
            icConfirm3->setVisible(false);

            break;
        }
    }
}

void PatriotNodeWizardDialog::inform(QString text)
{
    if (!snackBar)
        snackBar = new SnackBar(nullptr, this);
    snackBar->setText(text);
    snackBar->resize(this->width(), snackBar->height());
    openDialog(snackBar, this);
}

QSize BUTTON_SIZE = QSize(22, 22);
void PatriotNodeWizardDialog::initBtn(std::initializer_list<QPushButton*> args)
{
    for (QPushButton* btn : args) {
        btn->setMinimumSize(BUTTON_SIZE);
        btn->setMaximumSize(BUTTON_SIZE);
        btn->move(0, 0);
        btn->show();
        btn->raise();
        btn->setVisible(false);
    }
}

PatriotNodeWizardDialog::~PatriotNodeWizardDialog()
{
    if (snackBar) delete snackBar;
    delete ui;
}
