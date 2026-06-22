// Copyright (c) 2015-2026 The Bitcoin Core developers
// Copyright (c) 2026 The Kvanta5 Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KVANTA5_QT_CREATEP2QRMULTISIGDIALOG_H
#define KVANTA5_QT_CREATEP2QRMULTISIGDIALOG_H

#include <QDialog>

#include <interfaces/wallet.h>

namespace Ui {
class CreateP2QRMultisigDialog;
}

class WalletModel;

class CreateP2QRMultisigDialog : public QDialog
{
    Q_OBJECT

public:
    
    explicit CreateP2QRMultisigDialog(WalletModel* wallet_model, QWidget* parent = nullptr);
    ~CreateP2QRMultisigDialog();

private:
    void generateLocalSigner();
    void createVault();
    QStringList collectAddresses() const;
    void setResultText(const interfaces::Kvanta5P2QRMultisigInfo& info);
    void updateThresholdSummary();
    void reject() override;
    
    Ui::CreateP2QRMultisigDialog* ui;
    WalletModel* m_wallet_model{nullptr};
    interfaces::Kvanta5P2QRSignerInfo m_local_signer;
};

#endif // KVANTA5_QT_CREATEP2QRMULTISIGDIALOG_H
