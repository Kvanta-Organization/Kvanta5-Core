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

#include <qt/createp2qrmultisigdialog.h>
#include <qt/forms/ui_createp2qrmultisigdialog.h>
#include <qt/walletmodel.h>

#include <algorithm>

#include <QClipboard>
#include <QDialogButtonBox>
#include <QGuiApplication>
#include <QMessageBox>
#include <QPushButton>

namespace {

static constexpr const char* KVANTA5_VAULT_CREATED_PROPERTY{"kvanta5_vault_created"};
static constexpr const char* KVANTA5_CLOSE_BUTTON_OBJECT_NAME{"kvanta5_close_vault_dialog_button"};

static bool VaultAlreadyCreated(const QWidget* widget)
{
    return widget && widget->property(KVANTA5_VAULT_CREATED_PROPERTY).toBool();
}

static void SetCreateVaultInputsLocked(Ui::CreateP2QRMultisigDialog* ui, const bool locked)
{
    if (!ui) return;

    const bool enabled = !locked;

    ui->vault_label_edit->setEnabled(enabled);
    ui->required_spinbox->setEnabled(enabled);
    ui->total_signers_spinbox->setEnabled(enabled);

    ui->generate_local_signer_button->setEnabled(enabled);
    ui->copy_local_address_button->setEnabled(enabled);

    ui->local_address_edit->setEnabled(enabled);
    ui->local_wrapped_address_edit->setEnabled(enabled);
    ui->other_addresses_edit->setEnabled(enabled);
    ui->include_wrapped_checkbox->setEnabled(enabled);

    /*
     * Keep the result text usable after success so the user can select/copy
     * the created vault address, wrapped address, and policy details.
     */
    ui->result_text->setEnabled(true);
    ui->result_text->setReadOnly(true);
}

static QString FindAlreadyUsedLocalSignerAddress(const WalletModel* wallet_model, const QStringList& candidate_addresses)
{
    if (!wallet_model) return QString{};

    const QVector<WalletModel::VaultInfo> vaults = wallet_model->listVaults();

    for (const WalletModel::VaultInfo& vault : vaults) {
        for (const WalletModel::VaultSignerInfo& signer : vault.signers) {
            if (!signer.has_local_private_key) {
                continue;
            }

            const QString existing_address = signer.address.trimmed();
            if (existing_address.isEmpty()) {
                continue;
            }

            for (const QString& candidate_address : candidate_addresses) {
                if (QString::compare(existing_address, candidate_address.trimmed(), Qt::CaseInsensitive) == 0) {
                    return existing_address;
                }
            }
        }
    }

    return QString{};
}

} // namespace

CreateP2QRMultisigDialog::CreateP2QRMultisigDialog(WalletModel* wallet_model, QWidget* parent)
    : QDialog(parent),
      ui(new Ui::CreateP2QRMultisigDialog),
      m_wallet_model(wallet_model)
{
    ui->setupUi(this);

    setProperty(KVANTA5_VAULT_CREATED_PROPERTY, false);

    QPushButton* create_button = ui->buttonBox->button(QDialogButtonBox::Ok);
    if (create_button) {
        create_button->setText(tr("Create Vault"));
        create_button->setEnabled(false);
    }

    QPushButton* cancel_button = ui->buttonBox->button(QDialogButtonBox::Cancel);
    if (cancel_button) {
        cancel_button->setText(tr("Cancel"));
    }

    QPushButton* close_button = new QPushButton(tr("Close"), ui->buttonBox);
    close_button->setObjectName(KVANTA5_CLOSE_BUTTON_OBJECT_NAME);
    close_button->setVisible(false);
    ui->buttonBox->addButton(close_button, QDialogButtonBox::RejectRole);

    connect(ui->buttonBox, &QDialogButtonBox::accepted, this, &CreateP2QRMultisigDialog::createVault);
    connect(ui->buttonBox, &QDialogButtonBox::rejected, this, [this]() {
        reject();
    });

    connect(ui->copy_local_address_button, &QPushButton::clicked, this, [this]() {
        if (VaultAlreadyCreated(this)) return;
        QGuiApplication::clipboard()->setText(ui->local_address_edit->text());
    });

    connect(ui->generate_local_signer_button, &QPushButton::clicked, this, &CreateP2QRMultisigDialog::generateLocalSigner);

    connect(ui->other_addresses_edit, &QPlainTextEdit::textChanged, this, [this]() {
        updateThresholdSummary();
    });

    connect(ui->total_signers_spinbox, QOverload<int>::of(&QSpinBox::valueChanged), this, [this]() {
        updateThresholdSummary();
    });

    connect(ui->required_spinbox, QOverload<int>::of(&QSpinBox::valueChanged), this, [this]() {
        updateThresholdSummary();
    });

    generateLocalSigner();
    updateThresholdSummary();
}

CreateP2QRMultisigDialog::~CreateP2QRMultisigDialog()
{
    delete ui;
}

void CreateP2QRMultisigDialog::reject()
{
    if (!VaultAlreadyCreated(this)) {
        QMessageBox::information(
            this,
            tr("No Vault Was Created"),
            tr("No Vault Was Created"));
    }

    QDialog::reject();
}

void CreateP2QRMultisigDialog::generateLocalSigner()
{
    if (!m_wallet_model) return;

    if (VaultAlreadyCreated(this)) {
        return;
    }

    QString error;
    const QString label = ui->vault_label_edit->text().trimmed().isEmpty()
        ? tr("P2QR Multisig Vault Signer")
        : ui->vault_label_edit->text().trimmed() + tr(" Signer");

    m_local_signer = m_wallet_model->createP2QRSigner(label, error);

    if (!error.isEmpty()) {
        QMessageBox::critical(this, tr("Signer generation failed"), error);
        return;
    }

    ui->local_address_edit->setText(QString::fromStdString(m_local_signer.address));
    ui->local_wrapped_address_edit->setText(QString::fromStdString(m_local_signer.wrapped_address));

    updateThresholdSummary();
}

QStringList CreateP2QRMultisigDialog::collectAddresses() const
{
    QStringList addresses;

    const QString local_address = ui->local_address_edit->text().trimmed();
    if (!local_address.isEmpty()) {
        addresses << local_address;
    }

    const QStringList lines = ui->other_addresses_edit->toPlainText().split('\n');
    for (const QString& line : lines) {
        const QString trimmed = line.trimmed();
        if (!trimmed.isEmpty()) {
            addresses << trimmed;
        }
    }

    addresses.removeDuplicates();
    return addresses;
}

void CreateP2QRMultisigDialog::updateThresholdSummary()
{
    const QStringList addresses = collectAddresses();
    const int entered_signers = addresses.size();
    const int total_signers = ui->total_signers_spinbox->value();

    ui->required_spinbox->setMinimum(1);
    ui->required_spinbox->setMaximum(total_signers);

    const int required = ui->required_spinbox->value();

    QString summary = tr("%1-of-%2 vault, %3 of %2 signer address%4 entered")
        .arg(required)
        .arg(total_signers)
        .arg(entered_signers)
        .arg(entered_signers == 1 ? "" : "es");

    if (entered_signers < total_signers) {
        const int missing = total_signers - entered_signers;
        summary += tr(" — add %1 more signer address%2")
            .arg(missing)
            .arg(missing == 1 ? "" : "es");
    } else if (entered_signers > total_signers) {
        const int extra = entered_signers - total_signers;
        summary += tr(" — remove %1 extra signer address%2")
            .arg(extra)
            .arg(extra == 1 ? "" : "es");
    }

    const QString already_used_local_signer = VaultAlreadyCreated(this)
        ? QString{}
        : FindAlreadyUsedLocalSignerAddress(m_wallet_model, addresses);

    if (!already_used_local_signer.isEmpty()) {
        summary += tr(" — one local signer address is already assigned to another vault");
    }

    ui->threshold_summary_label->setText(summary);

    QPushButton* create_button = ui->buttonBox->button(QDialogButtonBox::Ok);
    if (!create_button) return;

    if (VaultAlreadyCreated(this)) {
        create_button->setEnabled(false);
        return;
    }

    create_button->setEnabled(
        entered_signers == total_signers &&
        total_signers >= 2 &&
        required >= 1 &&
        required <= total_signers &&
        !ui->local_address_edit->text().trimmed().isEmpty() &&
        already_used_local_signer.isEmpty());
}

void CreateP2QRMultisigDialog::createVault()
{
    if (!m_wallet_model) return;

    if (VaultAlreadyCreated(this)) {
        return;
    }

    QPushButton* create_button = ui->buttonBox->button(QDialogButtonBox::Ok);
    if (create_button) {
        create_button->setEnabled(false);
    }

    const QStringList addresses = collectAddresses();
    const int required = ui->required_spinbox->value();
    const int total_signers = ui->total_signers_spinbox->value();

    if (total_signers < 2) {
        QMessageBox::critical(this, tr("Invalid vault"), tr("A multisig vault needs at least two total signers."));
        updateThresholdSummary();
        return;
    }

    if (required < 1 || required > total_signers) {
        QMessageBox::critical(
            this,
            tr("Invalid vault"),
            tr("Required approvals must be between 1 and the total number of signers."));
        updateThresholdSummary();
        return;
    }

    if (addresses.size() != total_signers) {
        QMessageBox::critical(
            this,
            tr("Invalid vault"),
            tr("This is configured as a %1-of-%2 vault, but %3 signer address%4 were entered.")
                .arg(required)
                .arg(total_signers)
                .arg(addresses.size())
                .arg(addresses.size() == 1 ? "" : "es"));
        updateThresholdSummary();
        return;
    }

    const QString already_used_local_signer = FindAlreadyUsedLocalSignerAddress(m_wallet_model, addresses);
    if (!already_used_local_signer.isEmpty()) {
        QMessageBox::critical(
            this,
            tr("Local signer already used"),
            tr("One of the signer addresses belongs to this wallet and is already assigned to an existing vault.\n\n"
               "Local signer keys may only be used for one vault to avoid UTXO ownership and signing confusion.\n\n"
               "Generate a new local signer for this vault, or remove the reused local signer address."));
        updateThresholdSummary();
        return;
    }

    QString label = ui->vault_label_edit->text().trimmed();
    if (label.isEmpty()) {
        label = tr("P2QR Multisig Vault");
    }

    QString error;
    const interfaces::Kvanta5P2QRMultisigInfo info = m_wallet_model->createP2QRMultisig(
        required,
        addresses,
        label,
        ui->include_wrapped_checkbox->isChecked(),
        error);

    if (!error.isEmpty()) {
        QMessageBox::critical(this, tr("Vault creation failed"), error);
        updateThresholdSummary();
        return;
    }

    setResultText(info);

    setProperty(KVANTA5_VAULT_CREATED_PROPERTY, true);
    SetCreateVaultInputsLocked(ui, true);

    QMessageBox::information(
        this,
        tr("Vault Created!"),
        tr("Vault Created!"));

    QDialog::accept();
}

void CreateP2QRMultisigDialog::setResultText(const interfaces::Kvanta5P2QRMultisigInfo& info)
{
    QString text;
    text += tr("Vault created successfully.\n\n");

    text += tr("Vault type:\n%1-of-%2 Kvanta5 P2QR Multi-Sig Vault\n\n")
        .arg(info.required)
        .arg(info.total);

    text += tr("Required approvals:\n%1 of %2 signer approvals required to spend\n\n")
        .arg(info.required)
        .arg(info.total);

    text += tr("Native P2QR address:\n%1\n\n")
        .arg(QString::fromStdString(info.address));

    text += tr("Program:\n%1\n\n")
        .arg(QString::fromStdString(info.program));

    if (!info.wrapped_address.empty()) {
        text += tr("Wrapped mining compatibility address:\n%1\n\n")
            .arg(QString::fromStdString(info.wrapped_address));
    }

    text += tr("Canonical signer addresses used by wallet policy:\n");
    for (const std::string& signer_address : info.signer_addresses) {
        text += QString::fromStdString(signer_address) + "\n";
    }  

    ui->result_text->setPlainText(text);
}
