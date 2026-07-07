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


#ifndef KVANTA5_QT_APPROVEVAULTPAYMENTDIALOG_H
#define KVANTA5_QT_APPROVEVAULTPAYMENTDIALOG_H

#include <QDialog>

class QPushButton;
class QTextEdit;
class WalletModel;

class ApproveVaultPaymentDialog : public QDialog
{
    Q_OBJECT

public:
    explicit ApproveVaultPaymentDialog(WalletModel* wallet_model, QWidget* parent = nullptr);

private:
    void openPackage();
    void approvePackage();
    void savePackageAs();
    void finalizePackage();
    void broadcastTransaction();

    void updateActionButtons();

    WalletModel* m_wallet_model{nullptr};

    QTextEdit* m_package_text{nullptr};
    QTextEdit* m_status_text{nullptr};

    QPushButton* m_open_button{nullptr};
    QPushButton* m_approve_button{nullptr};
    QPushButton* m_save_button{nullptr};
    QPushButton* m_finalize_button{nullptr};
    QPushButton* m_broadcast_button{nullptr};

    QString m_current_filename;
    QString m_package_json;
    QString m_final_tx_hex;
};

#endif // KVANTA5_QT_APPROVEVAULTPAYMENTDIALOG_H
