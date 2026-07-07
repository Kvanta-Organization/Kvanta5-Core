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


#ifndef KVANTA5_QT_CREATEVAULTPAYMENTDIALOG_H
#define KVANTA5_QT_CREATEVAULTPAYMENTDIALOG_H

#include <qt/walletmodel.h>


#include <optional>

#include <QDialog>
#include <QVector>

class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QTableWidget;
class QTextEdit;

class CreateVaultPaymentDialog : public QDialog
{
    Q_OBJECT

public:
    explicit CreateVaultPaymentDialog(WalletModel* wallet_model, QWidget* parent = nullptr);

private Q_SLOTS:
    void createPackage();
    void refreshVaultsAndUtxos();
    void refreshVaultUtxos();
    void selectAllVaultUtxos();
    void clearVaultUtxoSelection();
    void addRecipientRow();
    void updateSummary();
    void selectedVaultChanged();
    void changeModeChanged();

private:
    QString selectedVaultProgram() const;
    CAmount selectedVaultAmount() const;
    int selectedVaultUtxoCount() const;
    std::optional<CAmount> recipientsAmount(QString* error_out = nullptr) const;

    void lockAfterPackageCreated();

    WalletModel* m_wallet_model{nullptr};

    QComboBox* m_send_from_combo{nullptr};
    QTableWidget* m_signers_table{nullptr};

    QTableWidget* m_recipients_table{nullptr};
    QPushButton* m_add_recipient_button{nullptr};

    QLineEdit* m_fee_rate_edit{nullptr};
    QCheckBox* m_subtract_fee_checkbox{nullptr};

    QComboBox* m_change_mode_combo{nullptr};
    QLineEdit* m_custom_change_edit{nullptr};

    QTableWidget* m_utxo_table{nullptr};

    QLabel* m_selected_utxos_label{nullptr};
    QLabel* m_selected_amount_label{nullptr};
    QLabel* m_recipient_amount_label{nullptr};
    QLabel* m_fee_policy_label{nullptr};
    QLabel* m_change_hint_label{nullptr};

    QTextEdit* m_status_text{nullptr};

    QPushButton* m_refresh_button{nullptr};
    QPushButton* m_select_all_button{nullptr};
    QPushButton* m_clear_button{nullptr};
    QPushButton* m_create_button{nullptr};
    QPushButton* m_cancel_button{nullptr};
    QPushButton* m_close_button{nullptr};

    bool m_package_created{false};

    QVector<WalletModel::VaultInfo> m_vaults;
    QVector<WalletModel::VaultUTXO> m_all_vault_utxos;
    QVector<WalletModel::VaultUTXO> m_visible_vault_utxos;
};

#endif // KVANTA5_QT_CREATEVAULTPAYMENTDIALOG_H
