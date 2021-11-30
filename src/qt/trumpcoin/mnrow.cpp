// Copyright (c) 2019-2020 The TrumpCoin developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "qt/trumpcoin/mnrow.h"
#include "qt/trumpcoin/forms/ui_mnrow.h"
#include "qt/trumpcoin/qtutils.h"

PNRow::PNRow(QWidget *parent) :
    QWidget(parent),
    ui(new Ui::PNRow)
{
    ui->setupUi(this);
    setCssProperty(ui->labelAddress, "text-list-body2");
    setCssProperty(ui->labelName, "text-list-title1");
    setCssProperty(ui->labelDate, "text-list-caption-medium");
    ui->lblDivisory->setStyleSheet("background-color:#bababa;");
}

void PNRow::updateView(QString address, const QString& label, QString status, bool wasCollateralAccepted)
{
    ui->labelName->setText(label);
    address = address.size() < 40 ? address : address.left(20) + "..." + address.right(20);
    ui->labelAddress->setText(address);
    if (!wasCollateralAccepted) status = tr("Collateral tx not found");
    ui->labelDate->setText(tr("Status: %1").arg(status));
}

PNRow::~PNRow()
{
    delete ui;
}
