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


#ifndef KVANTA5_QT_BROADCASTVAULTPAYMENTDIALOG_H
#define KVANTA5_QT_BROADCASTVAULTPAYMENTDIALOG_H

#include <QDialog>

class QPushButton;
class QTextEdit;
class WalletModel;

class BroadcastVaultPaymentDialog : public QDialog
{
    Q_OBJECT

public:
    explicit BroadcastVaultPaymentDialog(WalletModel* wallet_model, QWidget* parent = nullptr);

private:
    void openPackage();
    void finalizePackage();
    void broadcastTransaction();

    WalletModel* m_wallet_model{nullptr};

    QString m_package_json;
    QString m_final_tx_hex;

    QPushButton* m_open_button{nullptr};
    QPushButton* m_finalize_button{nullptr};
    QPushButton* m_broadcast_button{nullptr};
    QTextEdit* m_package_text{nullptr};
    QTextEdit* m_status_text{nullptr};
};

#endif // KVANTA5_QT_BROADCASTVAULTPAYMENTDIALOG_H
