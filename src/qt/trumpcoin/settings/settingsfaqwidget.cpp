// Copyright (c) 2019 The TrumpCoin developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#if defined(HAVE_CONFIG_H)
#include "config/trumpcoin-config.h"
#endif

#include "qt/trumpcoin/settings/settingsfaqwidget.h"
#include "qt/trumpcoin/settings/forms/ui_settingsfaqwidget.h"
#include "clientmodel.h"
#include "qt/trumpcoin/qtutils.h"

#include <QScrollBar>
#include <QMetaObject>

SettingsFaqWidget::SettingsFaqWidget(TrumpCoinGUI* parent, ClientModel* _model) :
    QDialog(parent),
    ui(new Ui::SettingsFaqWidget),
    clientModel(_model)
{
    ui->setupUi(this);
    this->setStyleSheet(parent->styleSheet());

#ifdef Q_OS_MAC
    ui->container->load("://bg-welcome");
    setCssProperty(ui->container, "container-welcome-no-image");
#else
    setCssProperty(ui->container, "container-welcome");
#endif
    setCssProperty(ui->labelTitle, "text-title-faq");
    setCssProperty(ui->labelWebLink, "text-content-white");

    // Content
    setCssProperty({
           ui->labelNumber_Intro,
           ui->labelNumber_UnspendableTRUMP,
           ui->labelNumber_Stake,
           ui->labelNumber_Support,
           ui->labelNumber_Patriotnode,
           ui->labelNumber_PNController
        }, "container-number-faq");

    setCssProperty({
              ui->labelSubtitle_Intro,
              ui->labelSubtitle_UnspendableTRUMP,
              ui->labelSubtitle_Stake,
              ui->labelSubtitle_Support,
              ui->labelSubtitle_Patriotnode,
              ui->labelSubtitle_PNController
            }, "text-subtitle-faq");


    setCssProperty({
              ui->labelContent_Intro,
              ui->labelContent_UnspendableTRUMP,
              ui->labelContent_Stake,
              ui->labelContent_Support,
              ui->labelContent_Patriotnode,
              ui->labelContent_PNController
            }, "text-content-faq");


    setCssProperty({
              ui->pushButton_Intro,
              ui->pushButton_UnspendableTRUMP,
              ui->pushButton_Stake,
              ui->pushButton_Support,
              ui->pushButton_Patriotnode,
              ui->pushButton_PNController
            }, "btn-faq-options");

    ui->labelContent_Support->setOpenExternalLinks(true);

    // Set FAQ content strings
    QString introContent = formatFAQContent(
        formatFAQParagraph(
            tr("TrumpCoin is a form of digital online money using blockchain technology "
               "that can be easily transferred globally, instantly, and with near "
               "zero fees. TrumpCoin incorporates market leading security & "
               "privacy and is also the first PoS (Proof of Stake) Cryptocurrency "
               "to implement Sapling(SHIELD), a zk-SNARKs based privacy protocol.")) +
        formatFAQParagraph(
            tr("TrumpCoin utilizes a Proof of Stake (PoS) consensus system algorithm, "
               "allowing all owners of TrumpCoin to participate in earning block rewards "
               "while securing the network with full node wallets, as well as to "
               "run Patriotnodes to create and vote on proposals.")));
    ui->labelContent_Intro->setText(introContent);

    QString unspendableTRUMPContent = formatFAQContent(
        formatFAQParagraph(
            tr("Newly received TrumpCoin requires 6 confirmations on the network "
               "to become eligible for spending which can take ~6 minutes.")) +
        formatFAQParagraph(
            tr("Your TrumpCoin wallet also needs to be completely synchronized "
               "to see and spend balances on the network.")));
    ui->labelContent_UnspendableTRUMP->setText(unspendableTRUMPContent);

    QString stakeContent = formatFAQContent(
        formatFAQOrderedList(
            formatFAQListItem(tr("Make sure your wallet is completely synchronized and you are using the latest release.")) +
            formatFAQListItem(tr("You must have a balance of TrumpCoin with a minimum of 480 confirmations.")) +
            formatFAQListItem(tr("Your wallet must stay online and be unlocked for staking purposes.")) +
            formatFAQListItem(tr("Once all those steps are followed staking should be enabled."))) +
        formatFAQParagraph(
            tr("You can see the status of staking in the wallet by mousing over the "
               "package icon in the row on the top left of the wallet interface. The "
               "package will be lit up and will state \"Staking Enabled\" to indicate "
               "it is staking. Using the command line interface (%1); the command %2 "
               "will confirm that staking is active.")
                .arg("trumpcoin-cli", "<span style=\"font-style:italic\">getstakingstatus</span>")));
    ui->labelContent_Stake->setText(stakeContent);

    QString supportContent = formatFAQContent(
        formatFAQParagraph(
            tr("We have support channels in most of our official chat groups, for example discord or telegram.")));
    ui->labelContent_Support->setText(supportContent);

    QString patriotnodeContent = formatFAQContent(
        formatFAQParagraph(
            tr("A patriotnode is a computer running a full node %1 wallet with a "
               "requirement of %2 secured collateral to provide extra services "
               "to the network and in return, receive a portion of the block reward "
               "regularly. These services include:")
                .arg(PACKAGE_NAME)
                .arg(GUIUtil::formatBalance(clientModel->getPNCollateralRequiredAmount(), BitcoinUnits::TRUMP)) +
            formatFAQUnorderedList(
                formatFAQListItem(tr("A decentralized governance (Proposal Voting)")) +
                formatFAQListItem(tr("A decentralized budgeting system (Treasury)")) +
                formatFAQListItem(tr("Validation of transactions within each block")) +
                formatFAQListItem(tr("Act as an additional full node in the network")))) +
        formatFAQParagraph(
            tr("For providing such services, patriotnodes are also paid a certain portion "
               "of reward for each block. This can serve as a passive income to the "
               "patriotnode owners minus their running cost.")) +
        formatFAQParagraph(
            tr("Patriotnode Perks:") +
            formatFAQUnorderedList(
                formatFAQListItem(tr("Participate in TrumpCoin Governance")) +
                formatFAQListItem(tr("Earn Patriotnode Rewards")) +
                formatFAQListItem(tr("Commodity option for future sale")) +
                formatFAQListItem(tr("Help secure the TrumpCoin network")))) +
        formatFAQParagraph(
            tr("Requirements:") +
            formatFAQUnorderedList(
                formatFAQListItem(tr("%1 per single Patriotnode instance")
                        .arg(GUIUtil::formatBalance(clientModel->getPNCollateralRequiredAmount(), BitcoinUnits::TRUMP))) +
                formatFAQListItem(tr("Must be stored in a core wallet")) +
                formatFAQListItem(tr("Need dedicated IP address")) +
                formatFAQListItem(tr("Patriotnode wallet to remain online")))));
    ui->labelContent_Patriotnode->setText(patriotnodeContent);

    QString mNControllerContent = formatFAQContent(
        formatFAQParagraph(
            tr("A Patriotnode Controller wallet is where the %1 collateral "
               "can reside during a Controller-Remote patriotnode setup. It is a wallet "
               "that can activate the remote patriotnode wallet(s) and allows you to keep "
               "your collateral coins offline while the remote patriotnode remains online.")
                    .arg(GUIUtil::formatBalance(clientModel->getPNCollateralRequiredAmount(), BitcoinUnits::TRUMP))));
    ui->labelContent_PNController->setText(mNControllerContent);


    // Exit button
    setCssProperty(ui->pushButtonExit, "btn-faq-exit");

    // Web Link
    setCssProperty(ui->pushButtonWebLink, "btn-faq-web");
    setCssProperty(ui->containerButtons, "container-faq-buttons");

    // Buttons
    connect(ui->pushButtonExit, &QPushButton::clicked, this, &SettingsFaqWidget::close);
    connect(ui->pushButton_Intro, &QPushButton::clicked, [this](){onFaqClicked(ui->widget_Intro);});
    connect(ui->pushButton_UnspendableTRUMP, &QPushButton::clicked, [this](){onFaqClicked(ui->widget_UnspendableTRUMP);});
    connect(ui->pushButton_Stake, &QPushButton::clicked, [this](){onFaqClicked(ui->widget_Stake);});
    connect(ui->pushButton_Support, &QPushButton::clicked, [this](){onFaqClicked(ui->widget_Support);});
    connect(ui->pushButton_Patriotnode, &QPushButton::clicked, [this](){onFaqClicked(ui->widget_Patriotnode);});
    connect(ui->pushButton_PNController, &QPushButton::clicked, [this](){onFaqClicked(ui->widget_PNController);});

    if (parent)
        connect(parent, &TrumpCoinGUI::windowResizeEvent, this, &SettingsFaqWidget::windowResizeEvent);
}

void SettingsFaqWidget::showEvent(QShowEvent *event)
{
    QPushButton* btn = getButtons()[section];
    QMetaObject::invokeMethod(btn, "setChecked", Qt::QueuedConnection, Q_ARG(bool, true));
    QMetaObject::invokeMethod(btn, "clicked", Qt::QueuedConnection);
}

void SettingsFaqWidget::setSection(Section _section)
{
    section = _section;
}

void SettingsFaqWidget::onFaqClicked(const QWidget* const widget)
{
    ui->scrollAreaFaq->verticalScrollBar()->setValue(widget->y());
}

void SettingsFaqWidget::windowResizeEvent(QResizeEvent* event)
{
    QWidget* w = qobject_cast<QWidget*>(parent());
    this->resize(w->width(), w->height());
    this->move(QPoint(0, 0));
}

std::vector<QPushButton*> SettingsFaqWidget::getButtons()
{
    return {
            ui->pushButton_Intro,
            ui->pushButton_UnspendableTRUMP,
            ui->pushButton_Stake,
            ui->pushButton_Support,
            ui->pushButton_Patriotnode,
            ui->pushButton_PNController
    };
}

SettingsFaqWidget::~SettingsFaqWidget()
{
    delete ui;
}


