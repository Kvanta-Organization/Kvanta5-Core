// Copyright (c) 2026 Kvanta5 Core Developers.
// Copyright (c) 2026 Kvanta5 Organization.
// All rights reserved.
//
// Kvanta5 Vault Component.
// Proprietary source-available code for review and security inspection only.
//
// No copying, modification, forking, redistribution, publication,
// sublicensing, reimplementation, derivative works, or commercial use is
// permitted without prior express written permission from the Kvanta5 Core
// Developers or an authorized representative of the Kvanta5 Organization.
//
// This file is NOT licensed under the MIT License.

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
