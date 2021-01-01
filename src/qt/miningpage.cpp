// Copyright (c) 2020 The Datacoin Core developers
// Copyright (c) 2016-2018 Duality Blockchain Solutions Developers
// Copyright (c) 2014-2018 The Dash Core Developers
// Copyright (c) 2009-2018 The Bitcoin Developers
// Copyright (c) 2009-2018 Satoshi Nakamoto
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qt/miningpage.h>
#include <qt/forms/ui_miningpage.h>
#include <qt/clientmodel.h>
#include <qt/guiutil.h>
#include <qt/walletmodel.h>

#include <bignum.h>
#include <miner.h>
#include <net.h>
#include <pow.h>
#include <prime/prime.h>
#include <rpc/server.h>
#include <univalue.h>
#include <util.h>
#include <utiltime.h>
#include <validation.h>

#include <boost/thread.hpp>
#include <stdio.h>

#include <QtDebug>

extern UniValue GetNetworkHashPS(int lookup, int height);

MiningPage::MiningPage(const PlatformStyle *platformStyle, QWidget *parent) :
    QWidget(parent),
    ui(new Ui::MiningPage),
    hasMiningprivkey(false)
{
    ui->setupUi(this);
    int nMaxUseThreads = GUIUtil::MaxThreads();

    /*
    std::string PrivAddress = gArgs.GetArg("-miningprivkey", "");
    if (!PrivAddress.empty())
    {
        CBitcoinSecret Secret;
        Secret.SetString(PrivAddress);
        if (Secret.IsValid())
        {
            CBitcoinAddress Address;
            Address.Set(Secret.GetKey().GetPubKey().GetID());
            ui->labelAddress->setText(QString("All mined coins will go to %1").arg(Address.ToString().c_str()));
            hasMiningprivkey = true;
        }
    }
    */

    ui->sliderCores->setMinimum(0);
    ui->sliderCores->setMaximum(nMaxUseThreads);
    ui->sliderCores->setValue(nMaxUseThreads);
    ui->labelNCores->setText(QString("%1").arg(nMaxUseThreads));
    const unsigned int nDefaultSieveExt = (TestNet()) ? nDefaultSieveExtensionsTestnet : nDefaultSieveExtensions;
    ui->sieveextensionsValue->setText(QString("%1").arg(nDefaultSieveExt));
    ui->sliderGraphSampleTime->setMaximum(0);
    ui->sliderGraphSampleTime->setMaximum(6);

    ui->sliderCores->setToolTip(tr("Use the slider to select the amount of CPU threads to use."));
    ui->labelNetHashRate->setToolTip(tr("This shows the overall hashrate of the Datacoin network."));
    ui->labelMinerHashRate->setToolTip(tr("This shows the hashrate of your CPU whilst mining."));
    ui->labelNextBlock->setToolTip(tr("This shows the average time between the blocks you have mined."));

    isMining = gArgs.GetBoolArg("-gen", false)? 1 : 0;

    QValidator *sieveextensionsValuevalidator = new QIntValidator(0, 20, this);
    ui->sieveextensionsValue->setValidator(sieveextensionsValuevalidator);
    QValidator *sievesizeValuevalidator = new QIntValidator(100000, 10000000, this);
    ui->sievesizeValue->setValidator(sievesizeValuevalidator);
    QValidator *sieveprimesValuevalidator = new QIntValidator(1000, 78498, this);
    ui->sieveprimesValue->setValidator(sieveprimesValuevalidator);
    QValidator *l1cacheValuevalidator = new QIntValidator(1000, 78498, this);
    ui->l1cacheValue->setValidator(l1cacheValuevalidator);

    connect(ui->sliderCores, SIGNAL(valueChanged(int)), this, SLOT(changeNumberOfCores(int)));
    connect(ui->sliderGraphSampleTime, SIGNAL(valueChanged(int)), this, SLOT(changeSampleTime(int)));
    connect(ui->pushSwitchMining, SIGNAL(clicked()), this, SLOT(switchMining()));
    connect(ui->pushButtonClearData, SIGNAL(clicked()), this, SLOT(clearHashRateData()));
    connect(ui->checkBoxShowGraph, SIGNAL(stateChanged(int)), this, SLOT(showHashRate(int)));

    ui->minerHashRateWidget->graphType = HashRateGraphWidget::GraphType::MINER_HASHRATE;
    ui->minerHashRateWidget->UpdateSampleTime(HashRateGraphWidget::SampleTime::FIVE_MINUTES);
    
    showHashMeterControls(false);

    updateUI(isMining);
    startTimer(8000);
}

MiningPage::~MiningPage()
{
    delete ui;
}

void MiningPage::setModel(WalletModel *model)
{
    this->model = model;
}

void MiningPage::setClientModel(ClientModel *model)
{
    this->clientModel = model;
}

void MiningPage::updateUI(bool fGenerate)
{

    qint64 NetworkHashrate = GUIUtil::GetNetworkHashPS(120, -1);
    // int64_t hashrate = (int64_t)dHashesPerSec;
    qint64 Hashrate = GUIUtil::GetHashRate();

    ui->labelNetHashRate->setText(GUIUtil::FormatHashRate(NetworkHashrate));
    ui->labelMinerHashRate->setText(GUIUtil::FormatHashRate(Hashrate));
    
    QString NextBlockTime;
    if (Hashrate == 0)
        NextBlockTime = QChar(L'∞');
    else
    {
        NextBlockTime = QString::number(dBlocksPerDay);
    }
    ui->labelNextBlock->setText(NextBlockTime);

    if (GUIUtil::GetHashRate() == 0) {
        ui->pushSwitchMining->setToolTip(tr("Click 'Start mining' to begin mining."));
        ui->pushSwitchMining->setText(tr("Start mining."));
        ui->pushSwitchMining->setEnabled(true);
     }
     else {
        ui->pushSwitchMining->setToolTip(tr("Click 'Stop mining' to finish mining."));
        ui->pushSwitchMining->setText(tr("Stop mining."));
        ui->pushSwitchMining->setEnabled(true);
    }
    ui->pushSwitchMining->setEnabled(true);

    QString status = QString("Not Mining Datacoin");
    if (fGenerate)
        status = QString("Mining with %1/%2 threads, sieve extensions %3, sieve size %4, sieve filter %5, L1 cache %6, hashrate: %7")
                .arg((int)ui->sliderCores->value())
                .arg(GUIUtil::MaxThreads())
                .arg(ui->sieveextensionsValue->text())
                .arg(ui->sievesizeValue->text())
                .arg(ui->sieveprimesValue->text())
                .arg(ui->l1cacheValue->text())
                .arg(GUIUtil::FormatHashRate(Hashrate));
    ui->miningStatistics->setText(status);
}

void MiningPage::restartMining(bool fGenerate, int nThreads)
{
    isMining = fGenerate;
    if (nThreads <= maxGenProc)
        nUseThreads = nThreads;

    // unlock wallet before mining

  #ifndef __linux__
    if (fGenerate && !hasMiningprivkey && !unlockContext.get())
    {
        this->unlockContext.reset(new WalletModel::UnlockContext(model->requestUnlock()));
        if (!unlockContext->isValid())
        {
            unlockContext.reset(nullptr);
            return;
        }
    }
  #endif

    SetMiningParams();
    GenerateDatacoins(true, nThreads, Params());

    // lock wallet after mining
    if (!fGenerate && !hasMiningprivkey)
        unlockContext.reset(nullptr);

    updateUI(fGenerate);
}

void MiningPage::SetMiningParams()
{
    // gArgs.SoftSetArg("-sieveextensions", ui->sieveextensionsValue->text().toStdString());
    nSieveExtensions = std::stoi(ui->sieveextensionsValue->text().toStdString());
    // gArgs.SoftSetArg("-sievesize", ui->sievesizeValue->text().toStdString());
    nSieveSize = std::stoi(ui->sievesizeValue->text().toStdString());
    // gArgs.SoftSetArg("-sievefilterprimes", ui->sieveprimesValue->text().toStdString());
    nSieveFilterPrimes = std::stoi(ui->sieveprimesValue->text().toStdString());
    // gArgs.SoftSetArg("-l1cachesize", ui->l1cacheValue->text().toStdString());
    nL1CacheSize = std::stoi(ui->l1cacheValue->text().toStdString());
}

void MiningPage::StartMiner()
{
    int nThreads = (int)ui->sliderCores->value();

    SetMiningParams();
    GenerateDatacoins(true, nThreads, Params());
    isMining = true;
    updateUI(isMining);
}

void MiningPage::StopMiner()
{
    isMining = false;
    int nThreads = (int)ui->sliderCores->value();
    GenerateDatacoins(false, nThreads, Params());
    updateUI(isMining);
}

void MiningPage::changeNumberOfCores(int i)
{
    // restartMining(isMining, i);

    ui->labelNCores->setText(QString("%1").arg(i));
    if (i == 0) {
        StopMiner();
    }
    else if (i > 0 && GUIUtil::GetHashRate() > 0) {  
        StartMiner();
    }
}

void MiningPage::switchMining()
{
    // restartMining(!isMining, ui->sliderCores->value());

    int64_t hashRate = GUIUtil::GetHashRate();
    int nThreads = (int)ui->sliderCores->value();
    
    if (hashRate > 0) {
        ui->pushSwitchMining->setText(tr("Stopping."));
        StopMiner();
    }
    else if (nThreads == 0 && hashRate == 0){
        ui->sliderCores->setValue(1);
        ui->pushSwitchMining->setText(tr("Starting."));
        StartMiner();
    }
    else {
        ui->pushSwitchMining->setText(tr("Starting."));
        StartMiner();
    }
}

void MiningPage::timerEvent(QTimerEvent *)
{
    updateUI(isMining);
}

void MiningPage::updateSievePrimes(int i)
{

    sievesizeValue = pow(2, i);
    qDebug() << "New header shift: " << QString("%1").arg(i) << ", new sieve size value: " << QString("%1").arg(sievesizeValue);
    ui->sievesizeValue->setText(QString("%1").arg(sievesizeValue));
}

void MiningPage::showHashRate(int i)
{
    if (i == 0) {
        ui->minerHashRateWidget->StopHashMeter();
        showHashMeterControls(false);
    }
    else {
        ui->minerHashRateWidget->StartHashMeter();
        showHashMeterControls(true);
    }
}

void MiningPage::showHashMeterControls(bool show)
{
    if (show == false) {
        ui->sliderGraphSampleTime->setVisible(false);
        ui->labelGraphSampleSize->setVisible(false);
        ui->pushButtonClearData->setVisible(false);
    }
    else {
        ui->sliderGraphSampleTime->setVisible(true);
        ui->labelGraphSampleSize->setVisible(true);
        ui->pushButtonClearData->setVisible(true);
    }
}

void MiningPage::changeSampleTime(int i)
{
    if (i == 0) {
        ui->minerHashRateWidget->UpdateSampleTime(HashRateGraphWidget::SampleTime::FIVE_MINUTES);
        ui->labelGraphSampleSize->setText(QString("5 minutes"));
    }
    else if (i == 1) {
        ui->minerHashRateWidget->UpdateSampleTime(HashRateGraphWidget::SampleTime::TEN_MINUTES);
        ui->labelGraphSampleSize->setText(QString("10 minutes"));
    }
    else if (i == 2) {
        ui->minerHashRateWidget->UpdateSampleTime(HashRateGraphWidget::SampleTime::THIRTY_MINUTES);
        ui->labelGraphSampleSize->setText(QString("30 minutes"));
    }
    else if (i == 3) {
        ui->minerHashRateWidget->UpdateSampleTime(HashRateGraphWidget::SampleTime::ONE_HOUR);
        ui->labelGraphSampleSize->setText(QString("1 hour"));
    }
    else if (i == 4) {
        ui->minerHashRateWidget->UpdateSampleTime(HashRateGraphWidget::SampleTime::EIGHT_HOURS);
        ui->labelGraphSampleSize->setText(QString("8 hours"));
    }
    else if (i == 5) {
        ui->minerHashRateWidget->UpdateSampleTime(HashRateGraphWidget::SampleTime::TWELVE_HOURS);
        ui->labelGraphSampleSize->setText(QString("12 hours"));
    }
    else if (i == 6) {
        ui->minerHashRateWidget->UpdateSampleTime(HashRateGraphWidget::SampleTime::ONE_DAY);
        ui->labelGraphSampleSize->setText(QString("1 day"));
    }
    else {
        ui->minerHashRateWidget->UpdateSampleTime(HashRateGraphWidget::SampleTime::ONE_DAY);
        ui->labelGraphSampleSize->setText(QString("1 day"));
    }
}

void MiningPage::clearHashRateData()
{
    ui->minerHashRateWidget->clear();
}
