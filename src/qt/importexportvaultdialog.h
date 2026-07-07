// Copyright (c) 2015-2026 The Bitcoin Core developers
// Copyright (c) 2026 The Kvanta5 Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KVANTA5_QT_IMPORTEXPORTVAULTDIALOG_H
#define KVANTA5_QT_IMPORTEXPORTVAULTDIALOG_H

#include <qt/walletmodel.h>

#include <QDialog>
#include <QVector>

class QLabel;
class QLineEdit;
class QPushButton;
class QTableWidget;
class QTextEdit;

class ImportExportVaultDialog : public QDialog
{
    Q_OBJECT

public:
    explicit ImportExportVaultDialog(WalletModel* wallet_model, QWidget* parent = nullptr);

private Q_SLOTS:
    void refreshVaults();
    void browseExportFile();
    void exportSelectedVault();
    void browseImportFile();
    void importVaultBackup();

private:
    int selectedVaultRow() const;
    QString selectedVaultAddress() const;
    QString defaultExportFilename(const WalletModel::VaultInfo& vault) const;

    WalletModel* m_wallet_model{nullptr};

    QLabel* m_wallet_label{nullptr};
    QLabel* m_export_hint_label{nullptr};

    QTableWidget* m_vaults_table{nullptr};

    QLineEdit* m_export_file_edit{nullptr};
    QPushButton* m_export_browse_button{nullptr};
    QPushButton* m_export_button{nullptr};

    QLineEdit* m_import_file_edit{nullptr};
    QLineEdit* m_import_label_edit{nullptr};
    QPushButton* m_import_browse_button{nullptr};
    QPushButton* m_import_button{nullptr};

    QTextEdit* m_status_text{nullptr};

    QPushButton* m_refresh_button{nullptr};
    QPushButton* m_close_button{nullptr};

    QVector<WalletModel::VaultInfo> m_vaults;
};

#endif // KVANTA5_QT_IMPORTEXPORTVAULTDIALOG_H
