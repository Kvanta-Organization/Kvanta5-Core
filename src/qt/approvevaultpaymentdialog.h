// Copyright (c) 2015-2026 The Bitcoin Core developers
// Copyright (c) 2026 The Kvanta5 Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.


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
