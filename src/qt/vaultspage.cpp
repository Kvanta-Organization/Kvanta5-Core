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

#include <qt/vaultspage.h>

#include <qt/approvevaultpaymentdialog.h>
#include <qt/importexportvaultdialog.h>
#include <qt/createvaultpaymentdialog.h>
#include <qt/broadcastvaultpaymentdialog.h>
#include <qt/walletmodel.h>

#include <interfaces/wallet.h>
#include <util/moneystr.h>

#include <QFrame>
#include <QStyle>
#include <QAction>
#include <QApplication>
#include <QClipboard>
#include <QCheckBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFont>
#include <QFormLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPoint>
#include <QPushButton>
#include <QSpinBox>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QTextEdit>
#include <QVBoxLayout>
#include <QFileDialog>
#include <QTabWidget>


static QString FormatKv5AmountForVaultsPage(const CAmount amount)
{
    return QString::fromStdString(FormatMoney(amount));
}

static int VaultsPageRowFromContextPosition(const QTableWidget* table, const QPoint& pos)
{
    if (!table) {
        return -1;
    }

    QTableWidgetItem* item = table->itemAt(pos);
    if (!item) {
        return -1;
    }

    return item->row();
}

static void CenterDialogOnParent(QDialog& dialog, QWidget* parent)
{
    QWidget* anchor = parent ? parent->window() : nullptr;
    if (!anchor) {
        return;
    }

    dialog.adjustSize();

    const QRect parent_rect = anchor->geometry();
    const QSize dialog_size = dialog.size();

    const QPoint centered{
        parent_rect.x() + ((parent_rect.width() - dialog_size.width()) / 2),
        parent_rect.y() + ((parent_rect.height() - dialog_size.height()) / 2)
    };

    dialog.move(centered);
}

VaultsPage::VaultsPage(const PlatformStyle* platform_style, QWidget* parent)
    : QWidget(parent),
      m_platform_style(platform_style)
{
    auto* main_layout = new QVBoxLayout(this);

    auto* title = new QLabel(tr("Kvanta5 Multi-Sig Vaults"), this);
    QFont title_font = title->font();
    title_font.setPointSize(title_font.pointSize() + 4);
    title_font.setBold(true);
    title->setFont(title_font);
    main_layout->addWidget(title);

    m_wallet_label = new QLabel(tr("Active wallet: none"), this);
    QFont wallet_font = m_wallet_label->font();
    wallet_font.setBold(true);
    m_wallet_label->setFont(wallet_font);
    main_layout->addWidget(m_wallet_label);

    auto* description = new QLabel(
        tr("Create, track, approve, and broadcast Kvanta5 P2QR multi-signature vault payments. "
           "Vault payments are saved as .kv5vaulttx.json files so other signers can review and approve them."),
        this);
    description->setWordWrap(true);
    main_layout->addWidget(description);

    auto* actions_group = new QGroupBox(tr("Vault Actions"), this);
    auto* actions_layout = new QGridLayout(actions_group);

    m_create_vault_button = new QPushButton(tr("Create Vault"), this);
    m_create_payment_button = new QPushButton(tr("Create Vault Payment"), this);
    m_approve_button = new QPushButton(tr("Review / Approve Vault Payment"), this);
    m_import_export_button = new QPushButton(tr("Import / Export Vault"), this);
    m_refresh_button = new QPushButton(tr("Refresh Vault List"), this);

    actions_layout->addWidget(m_create_vault_button, 0, 0);
    actions_layout->addWidget(m_create_payment_button, 0, 1);
    actions_layout->addWidget(m_approve_button, 0, 2);
    actions_layout->addWidget(m_import_export_button, 0, 3);
    actions_layout->addWidget(m_refresh_button, 0, 4);

    main_layout->addWidget(actions_group);

    auto* vaults_group = new QGroupBox(tr("Known Vaults for Active Wallet"), this);
    auto* vaults_layout = new QVBoxLayout(vaults_group);

    m_summary_label = new QLabel(tr("No wallet loaded."), this);
    m_summary_label->setWordWrap(true);
    vaults_layout->addWidget(m_summary_label);

    m_vaults_table = new QTableWidget(this);
    m_vaults_table->setColumnCount(10);
    m_vaults_table->setHorizontalHeaderLabels({
        tr("Label"),
        tr("Balance"),
        tr("UTXOs"),
        tr("Spends"),
        tr("Policy"),
        tr("Local Signers"),
        tr("Type"),
        tr("Address"),
        tr("Program"),
        tr("Signers")
    });

    m_vaults_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_vaults_table->setSelectionMode(QAbstractItemView::SingleSelection);
    m_vaults_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_vaults_table->setContextMenuPolicy(Qt::CustomContextMenu);
    m_vaults_table->horizontalHeader()->setStretchLastSection(false);
    m_vaults_table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    m_vaults_table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    m_vaults_table->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    m_vaults_table->horizontalHeader()->setSectionResizeMode(3, QHeaderView::ResizeToContents);
    m_vaults_table->horizontalHeader()->setSectionResizeMode(4, QHeaderView::ResizeToContents);
    m_vaults_table->horizontalHeader()->setSectionResizeMode(5, QHeaderView::ResizeToContents);
    m_vaults_table->horizontalHeader()->setSectionResizeMode(6, QHeaderView::ResizeToContents);
    m_vaults_table->horizontalHeader()->setSectionResizeMode(7, QHeaderView::Stretch);
    m_vaults_table->horizontalHeader()->setSectionResizeMode(8, QHeaderView::Stretch);
    m_vaults_table->horizontalHeader()->setSectionResizeMode(9, QHeaderView::ResizeToContents);

    vaults_layout->addWidget(m_vaults_table);

    main_layout->addWidget(vaults_group);
    main_layout->addStretch();

    connect(m_create_vault_button, &QPushButton::clicked, this, &VaultsPage::createVault);
    connect(m_create_payment_button, &QPushButton::clicked, this, &VaultsPage::createVaultPayment);
    connect(m_approve_button, &QPushButton::clicked, this, &VaultsPage::approveVaultPayment);
    connect(m_import_export_button, &QPushButton::clicked, this, &VaultsPage::importExportVault);
    connect(m_refresh_button, &QPushButton::clicked, this, &VaultsPage::refreshVaults);

    connect(m_vaults_table, &QTableWidget::cellDoubleClicked, this, &VaultsPage::showSelectedVaultDetails);

    connect(m_vaults_table, &QWidget::customContextMenuRequested, this, [this](const QPoint& pos) {
        if (!m_vaults_table) {
            return;
        }

        const int row = VaultsPageRowFromContextPosition(m_vaults_table, pos);
        if (row < 0 || row >= m_vaults.size()) {
            return;
        }

        m_vaults_table->setCurrentCell(row, 7);

        const WalletModel::VaultInfo& vault = m_vaults[row];

        QMenu menu(this);

        QAction* copy_address_action = menu.addAction(tr("Copy Vault Address"));
        QAction* copy_program_action = menu.addAction(tr("Copy Vault Program"));
        menu.addSeparator();
        QAction* show_info_action = menu.addAction(tr("Show Vault Info"));

        QAction* selected_action = menu.exec(m_vaults_table->viewport()->mapToGlobal(pos));
        if (!selected_action) {
            return;
        }

        if (selected_action == copy_address_action) {
            QApplication::clipboard()->setText(vault.address);
            return;
        }

        if (selected_action == copy_program_action) {
            QApplication::clipboard()->setText(vault.program);
            return;
        }

        if (selected_action == show_info_action) {
            showSelectedVaultDetails();
            return;
        }
    });
}

void VaultsPage::setModel(WalletModel* model)
{
    /*
     * Wallet switching can briefly leave pages showing stale data while the
     * wallet view swaps models. Clear first, then attach the new model, then
     * reload vault inventory. This prevents a previous wallet's vault table
     * from remaining visible under a newly selected wallet.
     */
    m_vaults.clear();
    m_wallet_model = model;

    updateWalletLabel();
    populateVaultTable();

    const bool wallet_loaded = m_wallet_model != nullptr;
    if (m_create_vault_button) m_create_vault_button->setEnabled(wallet_loaded);
    if (m_create_payment_button) m_create_payment_button->setEnabled(wallet_loaded);
    if (m_approve_button) m_approve_button->setEnabled(wallet_loaded);
    if (m_import_export_button) m_import_export_button->setEnabled(wallet_loaded);
    if (m_refresh_button) m_refresh_button->setEnabled(wallet_loaded);

    if (wallet_loaded) {
        refreshVaults();
    }
}

void VaultsPage::updateWalletLabel()
{
    if (!m_wallet_label) {
        return;
    }

    if (m_wallet_model) {
        m_wallet_label->setText(tr("Active wallet: %1").arg(m_wallet_model->getWalletName()));
    } else {
        m_wallet_label->setText(tr("Active wallet: none"));
    }
}

void VaultsPage::refreshVaults()
{
    if (!m_wallet_model) {
        m_vaults.clear();
        populateVaultTable();
        return;
    }

    m_vaults = m_wallet_model->listVaults();
    populateVaultTable();
}

void VaultsPage::populateVaultTable()
{
    if (!m_vaults_table) {
        return;
    }

    m_vaults_table->blockSignals(true);
    m_vaults_table->clearSelection();
    m_vaults_table->setRowCount(0);

    CAmount total_balance{0};
    int total_utxos{0};
    int total_spends{0};

    for (int row = 0; row < m_vaults.size(); ++row) {
        const WalletModel::VaultInfo& vault = m_vaults[row];

        int local_signers{0};
        for (const WalletModel::VaultSignerInfo& signer : vault.signers) {
            if (signer.has_local_private_key) {
                ++local_signers;
            }
        }

        total_balance += vault.balance;
        total_utxos += vault.utxos;
        total_spends += vault.spend_count;

        m_vaults_table->insertRow(row);

        const QString label = vault.label.isEmpty() ? tr("(unlabeled)") : vault.label;
        const QString policy = tr("%1-of-%2").arg(vault.required).arg(vault.total);
        const QString local_signers_text = tr("%1 / %2").arg(local_signers).arg(vault.total);

        QString short_program = vault.program;
        if (short_program.size() > 24) {
            short_program = short_program.left(12) + QStringLiteral("...") + short_program.right(12);
        }

        auto* label_item = new QTableWidgetItem(label);
        auto* balance_item = new QTableWidgetItem(FormatKv5AmountForVaultsPage(vault.balance));
        auto* utxos_item = new QTableWidgetItem(QString::number(vault.utxos));
        auto* spends_item = new QTableWidgetItem(QString::number(vault.spend_count));
        auto* policy_item = new QTableWidgetItem(policy);
        auto* local_signers_item = new QTableWidgetItem(local_signers_text);
        auto* type_item = new QTableWidgetItem(vault.type);
        auto* address_item = new QTableWidgetItem(vault.address);
        auto* program_item = new QTableWidgetItem(short_program);
        auto* signers_item = new QTableWidgetItem(QString::number(vault.signers.size()));

        address_item->setData(Qt::UserRole + 1, vault.address);
        program_item->setData(Qt::UserRole + 1, vault.program);

        m_vaults_table->setItem(row, 0, label_item);
        m_vaults_table->setItem(row, 1, balance_item);
        m_vaults_table->setItem(row, 2, utxos_item);
        m_vaults_table->setItem(row, 3, spends_item);
        m_vaults_table->setItem(row, 4, policy_item);
        m_vaults_table->setItem(row, 5, local_signers_item);
        m_vaults_table->setItem(row, 6, type_item);
        m_vaults_table->setItem(row, 7, address_item);
        m_vaults_table->setItem(row, 8, program_item);
        m_vaults_table->setItem(row, 9, signers_item);

        for (int col = 0; col < m_vaults_table->columnCount(); ++col) {
            QTableWidgetItem* item = m_vaults_table->item(row, col);
            if (item) {
                item->setFlags(item->flags() & ~Qt::ItemIsEditable);
                item->setData(Qt::UserRole, vault.program);
                item->setToolTip(tr(
                    "Label: %1\n"
                    "Address: %2\n"
                    "Program: %3\n"
                    "Policy: %4\n"
                    "Balance: %5 KV5\n"
                    "UTXOs: %6\n"
                    "Spend count: %7\n"
                    "Local signers: %8\n\n"
                    "Right-click for copy/info actions.")
                    .arg(label)
                    .arg(vault.address)
                    .arg(vault.program)
                    .arg(policy)
                    .arg(FormatKv5AmountForVaultsPage(vault.balance))
                    .arg(vault.utxos)
                    .arg(vault.spend_count)
                    .arg(local_signers_text));
            }
        }
    }

    m_vaults_table->blockSignals(false);

    if (m_summary_label) {
        if (!m_wallet_model) {
            m_summary_label->setText(tr("No wallet loaded."));
        } else if (m_vaults.empty()) {
            m_summary_label->setText(tr("No known vaults found for this wallet. Create a vault or import/sign with a vault signer."));
        } else {
            m_summary_label->setText(tr(
                "Known vaults: %1 | Total vault balance: %2 KV5 | Vault UTXOs: %3 | Wallet-visible vault spends: %4")
                .arg(m_vaults.size())
                .arg(FormatKv5AmountForVaultsPage(total_balance))
                .arg(total_utxos)
                .arg(total_spends));
        }
    }
}

void VaultsPage::createVault()
{
    if (!m_wallet_model) {
        QMessageBox::critical(this, tr("Vaults"), tr("Wallet model is not available."));
        return;
    }

    QDialog dialog(this);
    dialog.setWindowTitle(tr("Create P2QR Multi-Sig Vault"));
    dialog.resize(760, 720);

    auto* layout = new QVBoxLayout(&dialog);

    auto* info = new QLabel(tr(
        "Create a Kvanta5 P2QR Multi-Sig Vault. This wallet generates signer #1. "
        "Paste the other signer P2QR addresses below."),
        &dialog);
    info->setWordWrap(true);
    layout->addWidget(info);

    auto* form = new QFormLayout();

    auto* label_edit = new QLineEdit(&dialog);
    label_edit->setPlaceholderText(tr("Treasury Vault"));

    auto* required_spin = new QSpinBox(&dialog);
    required_spin->setMinimum(1);
    required_spin->setMaximum(1024);
    required_spin->setValue(2);

    auto* total_spin = new QSpinBox(&dialog);
    total_spin->setMinimum(2);
    total_spin->setMaximum(1024);
    total_spin->setValue(3);

    auto* policy_status = new QLabel(&dialog);
    policy_status->setWordWrap(true);

    auto* total_layout = new QHBoxLayout();
    total_layout->addWidget(total_spin);
    total_layout->addWidget(policy_status, 1);

    form->addRow(tr("Vault label"), label_edit);
    form->addRow(tr("Required approvals"), required_spin);
    form->addRow(tr("Total signers"), total_layout);

    layout->addLayout(form);

    auto* local_group = new QGroupBox(tr("This wallet's signer address"), &dialog);
    auto* local_layout = new QVBoxLayout(local_group);

    auto* local_buttons = new QHBoxLayout();
    auto* generate_button = new QPushButton(tr("Generate New Local Signer"), &dialog);
    auto* copy_address_button = new QPushButton(tr("Copy Address"), &dialog);
    copy_address_button->setEnabled(false);

    local_buttons->addWidget(generate_button);
    local_buttons->addWidget(copy_address_button);
    local_layout->addLayout(local_buttons);

    auto* local_form = new QFormLayout();

    auto* local_address_edit = new QLineEdit(&dialog);
    local_address_edit->setReadOnly(true);

    auto* local_wrapped_edit = new QLineEdit(&dialog);
    local_wrapped_edit->setReadOnly(true);

    local_form->addRow(tr("Local P2QR signer address"), local_address_edit);
    local_form->addRow(tr("Local wrapped/mining compatibility address"), local_wrapped_edit);

    local_layout->addLayout(local_form);
    layout->addWidget(local_group);

    auto* other_group = new QGroupBox(tr("Other signer addresses"), &dialog);
    auto* other_layout = new QVBoxLayout(other_group);

    auto* other_help = new QLabel(tr("Paste one full P2QR signer address per line."), &dialog);
    other_help->setWordWrap(true);
    other_layout->addWidget(other_help);

    auto* other_addresses_edit = new QPlainTextEdit(&dialog);
    other_addresses_edit->setPlaceholderText(tr("Paste other signer addresses here, one per line"));
    other_addresses_edit->setMinimumHeight(90);
    other_layout->addWidget(other_addresses_edit);

    auto* wrapped_checkbox = new QCheckBox(tr("Include wrapped mining compatibility address"), &dialog);
    wrapped_checkbox->setChecked(true);
    other_layout->addWidget(wrapped_checkbox);

    layout->addWidget(other_group);

    auto* result_group = new QGroupBox(tr("Vault result"), &dialog);
    auto* result_layout = new QVBoxLayout(result_group);

    auto* result_text = new QPlainTextEdit(&dialog);
    result_text->setReadOnly(true);
    result_text->setPlaceholderText(tr("Vault address and policy details will appear here after creation."));
    result_text->setMinimumHeight(90);
    result_layout->addWidget(result_text);

    layout->addWidget(result_group);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Cancel | QDialogButtonBox::Ok, &dialog);
    buttons->button(QDialogButtonBox::Ok)->setText(tr("Create Vault"));
    buttons->button(QDialogButtonBox::Ok)->setEnabled(false);
    layout->addWidget(buttons);

    QString local_address;
    QString local_wrapped_address;
    bool vault_created{false};

    auto collect_signer_addresses = [&]() {
        QStringList signer_addresses;

        if (!local_address.trimmed().isEmpty()) {
            signer_addresses.append(local_address.trimmed());
        }

        const QStringList lines = other_addresses_edit->toPlainText().split(QLatin1Char('\n'));
        for (const QString& line : lines) {
            const QString signer_address = line.trimmed();
            if (!signer_address.isEmpty()) {
                signer_addresses.append(signer_address);
            }
        }

        signer_addresses.removeDuplicates();
        return signer_addresses;
    };

    auto lock_create_dialog_after_success = [&]() {
        vault_created = true;

        label_edit->setEnabled(false);
        required_spin->setEnabled(false);
        total_spin->setEnabled(false);
        generate_button->setEnabled(false);
        copy_address_button->setEnabled(false);
        local_address_edit->setEnabled(false);
        local_wrapped_edit->setEnabled(false);
        other_addresses_edit->setEnabled(false);
        wrapped_checkbox->setEnabled(false);

        buttons->button(QDialogButtonBox::Ok)->setEnabled(false);
        buttons->button(QDialogButtonBox::Cancel)->setText(tr("Close"));
    };

    auto update_policy_status = [&]() {
        const int total = total_spin->value();

        if (required_spin->maximum() != total) {
            required_spin->setMaximum(total);
        }

        const int required = required_spin->value();
        const QStringList signer_addresses = collect_signer_addresses();
        const int signer_count = signer_addresses.size();
        const int missing = total - signer_count;

        QString status = tr("%1-of-%2 vault, %3 of %4 signer address(es) entered")
            .arg(required)
            .arg(total)
            .arg(signer_count)
            .arg(total);

        if (missing > 0) {
            status += tr(" — add %1 more signer address(es)").arg(missing);
        } else if (signer_count > total) {
            status += tr(" — too many signer addresses");
        }

        policy_status->setText(status);

        const bool can_create =
            !vault_created &&
            !label_edit->text().trimmed().isEmpty() &&
            !local_address.trimmed().isEmpty() &&
            signer_count == total &&
            required >= 1 &&
            required <= total;

        buttons->button(QDialogButtonBox::Ok)->setEnabled(can_create);
    };

    auto generate_local_signer = [&]() {
        if (vault_created) {
            return;
        }

        QString error;
        const interfaces::Kvanta5P2QRSignerInfo signer_info =
            m_wallet_model->createP2QRSigner(label_edit->text().trimmed(), error);

        if (!error.isEmpty()) {
            QMessageBox::critical(&dialog, tr("Generate Local Signer Failed"), error);
            return;
        }

        local_address = QString::fromStdString(signer_info.address);
        local_wrapped_address = QString::fromStdString(signer_info.wrapped_address);

        local_address_edit->setText(local_address);
        local_wrapped_edit->setText(local_wrapped_address);
        copy_address_button->setEnabled(!local_address.isEmpty());

        update_policy_status();
    };

    connect(generate_button, &QPushButton::clicked, &dialog, generate_local_signer);

    connect(copy_address_button, &QPushButton::clicked, &dialog, [&]() {
        if (!local_address.isEmpty()) {
            QApplication::clipboard()->setText(local_address);
        }
    });

    connect(label_edit, &QLineEdit::textChanged, &dialog, update_policy_status);
    connect(required_spin, QOverload<int>::of(&QSpinBox::valueChanged), &dialog, update_policy_status);
    connect(total_spin, QOverload<int>::of(&QSpinBox::valueChanged), &dialog, update_policy_status);
    connect(other_addresses_edit, &QPlainTextEdit::textChanged, &dialog, update_policy_status);

    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);

    connect(&dialog, &QDialog::rejected, &dialog, [&]() {
        if (!vault_created) {
            QMessageBox::information(
                &dialog,
                tr("No Vault Was Created"),
                tr("No Vault Was Created"));
        }
    });

    connect(buttons, &QDialogButtonBox::accepted, &dialog, [&]() {
        if (vault_created) {
            return;
        }

        const QStringList signer_addresses = collect_signer_addresses();

        if (local_address.isEmpty()) {
            QMessageBox::critical(&dialog, tr("Create Vault"), tr("Generate this wallet's local signer first."));
            return;
        }

        if (signer_addresses.size() != total_spin->value()) {
            QMessageBox::critical(
                &dialog,
                tr("Create Vault"),
                tr("Expected %1 signer addresses, but got %2.")
                    .arg(total_spin->value())
                    .arg(signer_addresses.size()));
            return;
        }

        if (required_spin->value() > signer_addresses.size()) {
            QMessageBox::critical(
                &dialog,
                tr("Create Vault"),
                tr("Required approvals cannot exceed signer count."));
            return;
        }

        QString error;
        const interfaces::Kvanta5P2QRMultisigInfo vault_info =
            m_wallet_model->createP2QRMultisig(
                required_spin->value(),
                signer_addresses,
                label_edit->text().trimmed(),
                wrapped_checkbox->isChecked(),
                error);

        if (!error.isEmpty()) {
            QMessageBox::critical(&dialog, tr("Create Vault Failed"), error);
            result_text->setPlainText(error);
            return;
        }

        QString result;
        result += tr("Vault created successfully.\n\n");
        result += tr("Policy: %1-of-%2\n")
            .arg(required_spin->value())
            .arg(signer_addresses.size());
        result += tr("Vault address:\n%1\n\n")
            .arg(QString::fromStdString(vault_info.address));
        result += tr("Vault program:\n%1\n")
            .arg(QString::fromStdString(vault_info.program));

        if (wrapped_checkbox->isChecked()) {
            result += tr("\nWrapped/mining compatibility address:\n%1\n")
                .arg(QString::fromStdString(vault_info.wrapped_address));
        }

        result += tr("\nCanonical signer addresses used by wallet policy:\n");
        for (const std::string& signer_address : vault_info.signer_addresses) {
            result += tr("  %1\n").arg(QString::fromStdString(signer_address));
        }

        result_text->setPlainText(result);

        refreshVaults();

        lock_create_dialog_after_success();

        QMessageBox created_box(
            QMessageBox::Information,
            tr("Vault Created!"),
            tr("Vault Created!"),
            QMessageBox::Ok,
            &dialog);
        CenterDialogOnParent(created_box, &dialog);
        created_box.exec();

        dialog.accept();
    });

    generate_local_signer();
    update_policy_status();
    dialog.exec();
}

void VaultsPage::showSelectedVaultDetails()
{
    if (!m_vaults_table) {
        return;
    }

    const int row = m_vaults_table->currentRow();
    if (row < 0 || row >= m_vaults.size()) {
        return;
    }

    const WalletModel::VaultInfo& vault = m_vaults[row];

    QString details;
    details += tr("Label: %1\n").arg(vault.label.isEmpty() ? tr("(unlabeled)") : vault.label);
    details += tr("Address: %1\n").arg(vault.address);
    details += tr("Program: %1\n").arg(vault.program);
    details += tr("Type: %1\n").arg(vault.type);
    details += tr("Policy: %1-of-%2\n").arg(vault.required).arg(vault.total);
    details += tr("Balance: %1 KV5\n").arg(FormatKv5AmountForVaultsPage(vault.balance));
    details += tr("UTXOs: %1\n").arg(vault.utxos);
    details += tr("Spend count: %1\n\n").arg(vault.spend_count);
    details += tr("Signers:\n");

    for (const WalletModel::VaultSignerInfo& signer : vault.signers) {
        details += tr("  #%1  %2  Local key: %3\n")
            .arg(signer.signer_index)
            .arg(signer.address)
            .arg(signer.has_local_private_key ? tr("yes") : tr("no"));
    }

    QDialog details_dialog(this);
    details_dialog.setWindowTitle(tr("Vault Details"));
    details_dialog.setModal(true);
    details_dialog.resize(900, 520);

    auto* main_layout = new QVBoxLayout(&details_dialog);

    auto* content_layout = new QHBoxLayout();
    main_layout->addLayout(content_layout, 1);

    auto* icon_label = new QLabel(&details_dialog);
    icon_label->setPixmap(QApplication::style()->standardIcon(QStyle::SP_MessageBoxInformation).pixmap(32, 32));
    icon_label->setAlignment(Qt::AlignTop | Qt::AlignHCenter);
    icon_label->setFixedWidth(44);
    content_layout->addWidget(icon_label);

    auto* details_text = new QPlainTextEdit(&details_dialog);
    details_text->setReadOnly(true);
    details_text->setPlainText(details);
    details_text->setLineWrapMode(QPlainTextEdit::WidgetWidth);
    details_text->setFrameShape(QFrame::NoFrame);
    details_text->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    details_text->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    details_text->setStyleSheet(QStringLiteral(
        "QPlainTextEdit {"
        "  background: transparent;"
        "  border: none;"
        "}"
    ));

    /*
     * Use the normal application font instead of forcing monospace.
     * This keeps the old QMessageBox-style look while avoiding QMessageBox's
     * bad auto-sizing behavior with giant signer address lists.
     */
    details_text->setFont(details_dialog.font());

    content_layout->addWidget(details_text, 1);

    auto* buttons = new QDialogButtonBox(&details_dialog);
    QPushButton* copy_button = buttons->addButton(tr("Copy Details"), QDialogButtonBox::ActionRole);
    QPushButton* ok_button = buttons->addButton(QDialogButtonBox::Ok);
    ok_button->setDefault(true);

    main_layout->addWidget(buttons);

    connect(copy_button, &QPushButton::clicked, &details_dialog, [details]() {
        QApplication::clipboard()->setText(details);
    });

    connect(buttons, &QDialogButtonBox::accepted, &details_dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &details_dialog, &QDialog::reject);

    QWidget* anchor = window();
    if (anchor) {
        const QRect parent_rect = anchor->geometry();
        const QSize dialog_size = details_dialog.size();

        const QPoint centered{
            parent_rect.x() + ((parent_rect.width() - dialog_size.width()) / 2),
            parent_rect.y() + ((parent_rect.height() - dialog_size.height()) / 2)
        };

        details_dialog.move(centered);
    }

    details_dialog.exec();
}

void VaultsPage::createVaultPayment()
{
    if (!m_wallet_model) {
        QMessageBox::critical(this, tr("Vaults"), tr("Wallet model is not available."));
        return;
    }

    auto* dialog = new CreateVaultPaymentDialog(m_wallet_model, this);
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->show();
}

void VaultsPage::approveVaultPayment()
{
    if (!m_wallet_model) {
        QMessageBox::critical(this, tr("Vaults"), tr("Wallet model is not available."));
        return;
    }

    auto* dialog = new ApproveVaultPaymentDialog(m_wallet_model, this);
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->show();
}

void VaultsPage::broadcastVaultPayment()
{
    if (!m_wallet_model) {
        QMessageBox::critical(this, tr("Vaults"), tr("Wallet model is not available."));
        return;
    }

    auto* dialog = new BroadcastVaultPaymentDialog(m_wallet_model, this);
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->show();
}


void VaultsPage::importExportVault()
{
    if (!m_wallet_model) {
        QMessageBox::critical(this, tr("Vaults"), tr("Wallet model is not available."));
        return;
    }

    auto* dialog = new ImportExportVaultDialog(m_wallet_model, this);
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->show();
}
