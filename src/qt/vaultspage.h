#ifndef KVANTA5_QT_VAULTSPAGE_H
#define KVANTA5_QT_VAULTSPAGE_H

#include <QWidget>
#include <QVector>

class QLabel;
class QPushButton;
class QTableWidget;
class PlatformStyle;
#include <qt/walletmodel.h>

class VaultsPage : public QWidget
{
    Q_OBJECT

public:
    explicit VaultsPage(const PlatformStyle* platform_style, QWidget* parent = nullptr);

    void setModel(WalletModel* model);

private Q_SLOTS:
    void createVault();
    void createVaultPayment();
    void approveVaultPayment();
    void importExportVault();
    void broadcastVaultPayment();
    void refreshVaults();
    void showSelectedVaultDetails();

private:
    void updateWalletLabel();
    void populateVaultTable();

    const PlatformStyle* m_platform_style{nullptr};
    WalletModel* m_wallet_model{nullptr};

    QLabel* m_wallet_label{nullptr};
    QLabel* m_summary_label{nullptr};

    QTableWidget* m_vaults_table{nullptr};

    QPushButton* m_create_vault_button{nullptr};
    QPushButton* m_create_payment_button{nullptr};
    QPushButton* m_approve_button{nullptr};
    QPushButton* m_import_export_button{nullptr};
    QPushButton* m_refresh_button{nullptr};

    QVector<WalletModel::VaultInfo> m_vaults;
};

#endif // KVANTA5_QT_VAULTSPAGE_H
