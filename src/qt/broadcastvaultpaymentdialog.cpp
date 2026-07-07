// Copyright (c) 2015-2026 The Bitcoin Core developers
// Copyright (c) 2026 The Kvanta5 Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qt/broadcastvaultpaymentdialog.h>

#include <qt/walletmodel.h>

#include <common/args.h>
#include <util/fs.h>

#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QIODevice>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QTextEdit>
#include <QVBoxLayout>

static QString FindJsonStringRecursive(const QJsonValue& value, const QStringList& key_names)
{
    if (value.isObject()) {
        const QJsonObject obj = value.toObject();

        for (const QString& key : key_names) {
            const QJsonValue candidate = obj.value(key);
            if (candidate.isString()) {
                const QString text = candidate.toString().trimmed();
                if (!text.isEmpty()) {
                    return text;
                }
            }
        }

        for (auto it = obj.constBegin(); it != obj.constEnd(); ++it) {
            const QString found = FindJsonStringRecursive(it.value(), key_names);
            if (!found.isEmpty()) {
                return found;
            }
        }
    }

    if (value.isArray()) {
        const QJsonArray arr = value.toArray();
        for (const QJsonValue& entry : arr) {
            const QString found = FindJsonStringRecursive(entry, key_names);
            if (!found.isEmpty()) {
                return found;
            }
        }
    }

    return {};
}

static QJsonDocument ParseVaultPackageJson(const QString& package_json, QString* error_out = nullptr)
{
    QJsonParseError parse_error;
    const QJsonDocument doc = QJsonDocument::fromJson(package_json.toUtf8(), &parse_error);

    if (parse_error.error != QJsonParseError::NoError || doc.isNull()) {
        if (error_out) {
            *error_out = QObject::tr("Invalid vault payment JSON: %1").arg(parse_error.errorString());
        }
        return {};
    }

    return doc;
}

static QString ExtractVaultPackageTxid(const QJsonDocument& doc)
{
    const QStringList txid_keys{
        QStringLiteral("txid"),
        QStringLiteral("tx_id"),
        QStringLiteral("transaction_id"),
        QStringLiteral("transactionid"),
        QStringLiteral("hash"),
        QStringLiteral("tx_hash"),
        QStringLiteral("transaction_hash")
    };

    QString txid = FindJsonStringRecursive(doc.isObject() ? QJsonValue(doc.object()) : QJsonValue(doc.array()), txid_keys);
    txid = txid.trimmed();

    if (txid.size() != 64) {
        return {};
    }

    for (const QChar ch : txid) {
        if (!ch.isDigit() &&
            !(ch >= QLatin1Char('a') && ch <= QLatin1Char('f')) &&
            !(ch >= QLatin1Char('A') && ch <= QLatin1Char('F'))) {
            return {};
        }
    }

    return txid.toLower();
}

static QString Kvanta5VaultPaymentsDir()
{
    const fs::path dir = gArgs.GetDataDirNet() / "vault_payments";
    const QString path = QString::fromStdString(fs::PathToString(dir));

    QDir qdir(path);
    if (!qdir.exists() && !qdir.mkpath(QStringLiteral("."))) {
        return {};
    }

    return path;
}

static QString UniqueVaultPaymentPath(const QString& txid, const QString& stage_suffix)
{
    const QString dir = Kvanta5VaultPaymentsDir();
    if (dir.isEmpty()) {
        return {};
    }

    const QString wanted_name = txid + QStringLiteral(".") + stage_suffix + QStringLiteral(".kv5vaulttx.json");
    const QString wanted_path = QDir(dir).filePath(wanted_name);

    if (!QFileInfo::exists(wanted_path)) {
        return wanted_path;
    }

    for (int n = 2; n < 10000; ++n) {
        const QString candidate_name = txid + QStringLiteral(".") + stage_suffix +
            QStringLiteral(".duplicate-%1.kv5vaulttx.json").arg(n);

        const QString candidate_path = QDir(dir).filePath(candidate_name);
        if (!QFileInfo::exists(candidate_path)) {
            return candidate_path;
        }
    }

    return {};
}

static QString SaveVaultPaymentPackageToBroadcastedFile(
    const QString& package_json,
    const QString& broadcast_txid,
    QString* error_out = nullptr)
{
    QString parse_error;
    QJsonDocument doc = ParseVaultPackageJson(package_json, &parse_error);
    if (doc.isNull()) {
        if (error_out) *error_out = parse_error;
        return {};
    }

    const QString package_txid = ExtractVaultPackageTxid(doc);
    const QString txid = package_txid.isEmpty() ? broadcast_txid.trimmed().toLower() : package_txid;

    if (txid.size() != 64) {
        if (error_out) {
            *error_out = QObject::tr("Could not determine transaction id for broadcasted vault payment file.");
        }
        return {};
    }

    if (doc.isObject()) {
        QJsonObject obj = doc.object();
        obj.insert(QStringLiteral("broadcasted"), true);
        obj.insert(QStringLiteral("broadcast_txid"), broadcast_txid);
        obj.insert(QStringLiteral("broadcast_stage"), QStringLiteral("broadcasted"));
        doc.setObject(obj);
    }

    const QString path = UniqueVaultPaymentPath(txid, QStringLiteral("broadcasted"));
    if (path.isEmpty()) {
        if (error_out) {
            *error_out = QObject::tr("Could not allocate a unique broadcasted vault payment filename.");
        }
        return {};
    }

    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        if (error_out) {
            *error_out = QObject::tr("Could not write broadcasted vault payment file:\n%1").arg(path);
        }
        return {};
    }

    file.write(doc.toJson(QJsonDocument::Indented));
    file.write("\n");
    file.close();

    return path;
}

BroadcastVaultPaymentDialog::BroadcastVaultPaymentDialog(WalletModel* wallet_model, QWidget* parent)
    : QDialog(parent),
      m_wallet_model(wallet_model)
{
    setWindowTitle(tr("Broadcast Completed Vault Payment"));
    resize(760, 620);

    auto* main_layout = new QVBoxLayout(this);

    auto* description = new QLabel(
        tr("Open an approved Kvanta5 vault payment file. If it has enough approvals, this will finalize and broadcast the vault payment. "
           "After broadcast, the package is saved in vault_payments with a broadcasted transaction-id filename."),
        this);
    description->setWordWrap(true);
    main_layout->addWidget(description);

    m_package_text = new QTextEdit(this);
    m_package_text->setReadOnly(true);
    m_package_text->setPlaceholderText(tr("Vault payment file contents will appear here."));
    main_layout->addWidget(m_package_text, 3);

    m_status_text = new QTextEdit(this);
    m_status_text->setReadOnly(true);
    m_status_text->setMaximumHeight(150);
    m_status_text->setPlaceholderText(tr("Status will appear here."));
    main_layout->addWidget(m_status_text, 1);

    auto* button_layout = new QHBoxLayout();
    button_layout->addStretch();

    m_open_button = new QPushButton(tr("Open Payment File"), this);
    m_finalize_button = new QPushButton(tr("Finalize"), this);
    m_broadcast_button = new QPushButton(tr("Broadcast"), this);
    auto* close_button = new QPushButton(tr("Close"), this);

    m_finalize_button->setEnabled(false);
    m_broadcast_button->setEnabled(false);

    button_layout->addWidget(m_open_button);
    button_layout->addWidget(m_finalize_button);
    button_layout->addWidget(m_broadcast_button);
    button_layout->addWidget(close_button);

    main_layout->addLayout(button_layout);

    connect(m_open_button, &QPushButton::clicked, this, &BroadcastVaultPaymentDialog::openPackage);
    connect(m_finalize_button, &QPushButton::clicked, this, &BroadcastVaultPaymentDialog::finalizePackage);
    connect(m_broadcast_button, &QPushButton::clicked, this, &BroadcastVaultPaymentDialog::broadcastTransaction);
    connect(close_button, &QPushButton::clicked, this, &QDialog::reject);
}

void BroadcastVaultPaymentDialog::openPackage()
{
    const QString filename = QFileDialog::getOpenFileName(
        this,
        tr("Open Kvanta5 Vault Payment File"),
        Kvanta5VaultPaymentsDir(),
        tr("Kvanta5 Vault Payment Files (*.kv5vaulttx.json);;JSON Files (*.json);;All Files (*)"));

    if (filename.isEmpty()) {
        return;
    }

    QFile file(filename);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        QMessageBox::critical(this, tr("Open Failed"), tr("Could not open vault payment file:\n%1").arg(filename));
        return;
    }

    m_package_json = QString::fromUtf8(file.readAll());
    file.close();

    m_final_tx_hex.clear();

    m_package_text->setPlainText(m_package_json);
    m_status_text->setPlainText(tr("Loaded vault payment file:\n%1\n\nClick Finalize.").arg(filename));

    m_finalize_button->setEnabled(true);
    m_broadcast_button->setEnabled(false);
}

void BroadcastVaultPaymentDialog::finalizePackage()
{
    if (!m_wallet_model) {
        QMessageBox::critical(this, tr("Broadcast Vault Payment"), tr("Wallet model is not available."));
        return;
    }

    if (m_package_json.trimmed().isEmpty()) {
        QMessageBox::critical(this, tr("Broadcast Vault Payment"), tr("Open a vault payment file first."));
        return;
    }

    QString error;
    m_final_tx_hex = m_wallet_model->finalizeVaultTxPackage(m_package_json, error);

    if (!error.isEmpty()) {
        QMessageBox::critical(this, tr("Finalize Failed"), error);
        m_status_text->setPlainText(error);
        m_broadcast_button->setEnabled(false);
        return;
    }

    m_status_text->setPlainText(
        tr("Vault payment finalized successfully.\n\nTransaction hex:\n%1\n\nClick Broadcast to send it to the network.")
            .arg(m_final_tx_hex));

    m_broadcast_button->setEnabled(true);
}

void BroadcastVaultPaymentDialog::broadcastTransaction()
{
    if (!m_wallet_model) {
        QMessageBox::critical(this, tr("Broadcast Vault Payment"), tr("Wallet model is not available."));
        return;
    }

    if (m_final_tx_hex.trimmed().isEmpty()) {
        QMessageBox::critical(this, tr("Broadcast Vault Payment"), tr("Finalize the vault payment first."));
        return;
    }

    const QMessageBox::StandardButton confirm = QMessageBox::question(
        this,
        tr("Broadcast Vault Payment"),
        tr("Broadcast this finalized vault payment to the Kvanta5 network?"));

    if (confirm != QMessageBox::Yes) {
        return;
    }

    QString error;
    const QString txid = m_wallet_model->broadcastTransactionHex(m_final_tx_hex, error);

    if (!error.isEmpty()) {
        QMessageBox::critical(this, tr("Broadcast Failed"), error);
        m_status_text->setPlainText(error);
        return;
    }

    QString save_error;
    const QString saved_path = SaveVaultPaymentPackageToBroadcastedFile(
        m_package_json,
        txid,
        &save_error);

    if (saved_path.isEmpty()) {
        QMessageBox::warning(
            this,
            tr("Broadcast Saved Warning"),
            tr("Vault payment broadcast successfully, but saving the broadcasted stage failed:\n%1")
                .arg(save_error));

        m_status_text->setPlainText(
            tr("Vault payment broadcast successfully.\n\nTransaction ID:\n%1\n\nBroadcasted stage save failed:\n%2")
                .arg(txid)
                .arg(save_error));
    } else {
        m_status_text->setPlainText(
            tr("Vault payment broadcast successfully.\n\nTransaction ID:\n%1\n\nBroadcasted package saved:\n%2")
                .arg(txid)
                .arg(saved_path));
    }

    m_broadcast_button->setEnabled(false);
}
