// Copyright (c) 2019 The TrumpCoin developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef PNROW_H
#define PNROW_H

#include <QWidget>

namespace Ui {
class PNRow;
}

class PNRow : public QWidget
{
    Q_OBJECT

public:
    explicit PNRow(QWidget *parent = nullptr);
    ~PNRow();

    void updateView(QString address, const QString& label, QString status, bool wasCollateralAccepted);

Q_SIGNALS:
    void onMenuClicked();
private:
    Ui::PNRow *ui;
};

#endif // PNROW_H
