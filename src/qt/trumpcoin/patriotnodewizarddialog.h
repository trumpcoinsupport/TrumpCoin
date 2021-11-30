// Copyright (c) 2019 The TrumpCoin developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef PATRIOTNODEWIZARDDIALOG_H
#define PATRIOTNODEWIZARDDIALOG_H

#include "walletmodel.h"
#include "qt/trumpcoin/focuseddialog.h"
#include "qt/trumpcoin/snackbar.h"
#include "patriotnodeconfig.h"
#include "qt/trumpcoin/pwidget.h"

class WalletModel;
class ClientModel;

namespace Ui {
class PatriotNodeWizardDialog;
class QPushButton;
}

class PatriotNodeWizardDialog : public FocusedDialog, public PWidget::Translator
{
    Q_OBJECT

public:
    explicit PatriotNodeWizardDialog(WalletModel* walletMode,
                                    ClientModel* clientModel,
                                    QWidget *parent = nullptr);
    ~PatriotNodeWizardDialog();
    void showEvent(QShowEvent *event) override;
    QString translate(const char *msg) override { return tr(msg); }

    QString returnStr = "";
    bool isOk = false;
    CPatriotnodeConfig::CPatriotnodeEntry* mnEntry = nullptr;

private Q_SLOTS:
    void accept() override;
    void onBackClicked();
private:
    Ui::PatriotNodeWizardDialog *ui;
    QPushButton* icConfirm1;
    QPushButton* icConfirm3;
    QPushButton* icConfirm4;
    SnackBar *snackBar = nullptr;
    int pos = 0;

    WalletModel* walletModel{nullptr};
    ClientModel* clientModel{nullptr};
    bool createPN();
    void inform(QString text);
    void initBtn(std::initializer_list<QPushButton*> args);
};

#endif // PATRIOTNODEWIZARDDIALOG_H
