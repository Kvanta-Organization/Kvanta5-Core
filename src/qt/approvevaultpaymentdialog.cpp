#include <qt/approvevaultpaymentdialog.h>

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

#include <algorithm>
#include <set>

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

    QString txid = FindJsonStringRecursive(
        doc.isObject() ? QJsonValue(doc.object()) : QJsonValue(doc.array()),
        txid_keys);

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

static int FindJsonIntRecursive(const QJsonValue& value, const QStringList& key_names)
{
    if (value.isObject()) {
        const QJsonObject obj = value.toObject();

        for (const QString& key : key_names) {
            const QJsonValue candidate = obj.value(key);
            if (candidate.isDouble()) {
                return candidate.toInt();
            }
        }

        int best = 0;
        for (auto it = obj.constBegin(); it != obj.constEnd(); ++it) {
            best = std::max(best, FindJsonIntRecursive(it.value(), key_names));
        }
        return best;
    }

    if (value.isArray()) {
        int best = 0;
        for (const QJsonValue& entry : value.toArray()) {
            best = std::max(best, FindJsonIntRecursive(entry, key_names));
        }
        return best;
    }

    return 0;
}

static int ExtractVaultPackageTotalSigners(const QJsonDocument& doc)
{
    const QStringList total_keys{
        QStringLiteral("total"),
        QStringLiteral("total_signers"),
        QStringLiteral("n"),
        QStringLiteral("signers_total")
    };

    return FindJsonIntRecursive(
        doc.isObject() ? QJsonValue(doc.object()) : QJsonValue(doc.array()),
        total_keys);
}

static int ExtractVaultPackageRequiredSignatures(const QJsonDocument& doc)
{
    const QStringList required_keys{
        QStringLiteral("required"),
        QStringLiteral("sigsrequired"),
        QStringLiteral("signatures_required"),
        QStringLiteral("required_signatures"),
        QStringLiteral("m")
    };

    return FindJsonIntRecursive(
        doc.isObject() ? QJsonValue(doc.object()) : QJsonValue(doc.array()),
        required_keys);
}

static void CountApprovalsRecursive(const QJsonValue& value, std::set<int>& signer_indices, int& max_array_count)
{
    if (value.isObject()) {
        const QJsonObject obj = value.toObject();

        const QJsonValue approvals_value = obj.value(QStringLiteral("approvals"));
        if (approvals_value.isArray()) {
            const QJsonArray approvals = approvals_value.toArray();
            max_array_count = std::max(max_array_count, approvals.size());

            for (const QJsonValue& approval_value : approvals) {
                if (!approval_value.isObject()) {
                    continue;
                }

                const QJsonObject approval = approval_value.toObject();

                if (approval.value(QStringLiteral("signer_index")).isDouble()) {
                    signer_indices.insert(approval.value(QStringLiteral("signer_index")).toInt());
                } else if (approval.value(QStringLiteral("signer")).isDouble()) {
                    signer_indices.insert(approval.value(QStringLiteral("signer")).toInt());
                } else if (approval.value(QStringLiteral("index")).isDouble()) {
                    signer_indices.insert(approval.value(QStringLiteral("index")).toInt());
                }
            }
        }

        for (auto it = obj.constBegin(); it != obj.constEnd(); ++it) {
            CountApprovalsRecursive(it.value(), signer_indices, max_array_count);
        }
    } else if (value.isArray()) {
        for (const QJsonValue& entry : value.toArray()) {
            CountApprovalsRecursive(entry, signer_indices, max_array_count);
        }
    }
}

static int ExtractVaultPackageApprovalCount(const QJsonDocument& doc)
{
    std::set<int> signer_indices;
    int max_array_count{0};

    CountApprovalsRecursive(
        doc.isObject() ? QJsonValue(doc.object()) : QJsonValue(doc.array()),
        signer_indices,
        max_array_count);

    if (!signer_indices.empty()) {
        return static_cast<int>(signer_indices.size());
    }

    return max_array_count;
}

static bool VaultPackageHasRequiredApprovals(const QJsonDocument& doc)
{
    const int approvals = ExtractVaultPackageApprovalCount(doc);
    const int required = ExtractVaultPackageRequiredSignatures(doc);

    return required > 0 && approvals >= required;
}

static QString VaultPackageApprovalSummary(const QJsonDocument& doc)
{
    const int approvals = ExtractVaultPackageApprovalCount(doc);
    const int required = ExtractVaultPackageRequiredSignatures(doc);
    const int total = ExtractVaultPackageTotalSigners(doc);

    if (required > 0 && total > 0) {
        return QObject::tr("%1 approval(s), %2 required, %3 total signer(s)")
            .arg(approvals)
            .arg(required)
            .arg(total);
    }

    if (required > 0) {
        return QObject::tr("%1 approval(s), %2 required")
            .arg(approvals)
            .arg(required);
    }

    if (total > 0) {
        return QObject::tr("%1 approval(s), %2 total signer(s)")
            .arg(approvals)
            .arg(total);
    }

    return QObject::tr("%1 approval(s)").arg(approvals);
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

static QString BuildVaultPaymentFilename(const QString& txid, const QString& stage_suffix)
{
    if (stage_suffix.isEmpty()) {
        return txid + QStringLiteral(".kv5vaulttx.json");
    }

    return txid + QStringLiteral(".") + stage_suffix + QStringLiteral(".kv5vaulttx.json");
}

static QString UniqueVaultPaymentPath(const QString& txid, const QString& stage_suffix)
{
    const QString dir = Kvanta5VaultPaymentsDir();
    if (dir.isEmpty()) {
        return {};
    }

    const QString wanted_name = BuildVaultPaymentFilename(txid, stage_suffix);
    const QString wanted_path = QDir(dir).filePath(wanted_name);

    if (!QFileInfo::exists(wanted_path)) {
        return wanted_path;
    }

    for (int n = 2; n < 10000; ++n) {
        QString candidate_name;

        if (stage_suffix.isEmpty()) {
            candidate_name = txid + QStringLiteral(".duplicate-%1.kv5vaulttx.json").arg(n);
        } else {
            candidate_name = txid + QStringLiteral(".") + stage_suffix +
                QStringLiteral(".duplicate-%1.kv5vaulttx.json").arg(n);
        }

        const QString candidate_path = QDir(dir).filePath(candidate_name);
        if (!QFileInfo::exists(candidate_path)) {
            return candidate_path;
        }
    }

    return {};
}

static QString SaveVaultPaymentPackageToStageFile(
    const QString& package_json,
    const QString& stage_suffix,
    QString* txid_out = nullptr,
    QString* error_out = nullptr)
{
    QString parse_error;
    const QJsonDocument doc = ParseVaultPackageJson(package_json, &parse_error);
    if (doc.isNull()) {
        if (error_out) *error_out = parse_error;
        return {};
    }

    const QString txid = ExtractVaultPackageTxid(doc);
    if (txid.isEmpty()) {
        if (error_out) {
            *error_out = QObject::tr("Vault payment package JSON does not contain a usable transaction id.");
        }
        return {};
    }

    const QString path = UniqueVaultPaymentPath(txid, stage_suffix);
    if (path.isEmpty()) {
        if (error_out) {
            *error_out = QObject::tr("Could not allocate a unique vault payment filename in the vault_payments folder.");
        }
        return {};
    }

    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        if (error_out) {
            *error_out = QObject::tr("Could not write vault payment file:\n%1").arg(path);
        }
        return {};
    }

    file.write(package_json.toUtf8());
    file.write("\n");
    file.close();

    if (txid_out) {
        *txid_out = txid;
    }

    return path;
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

static QString BuildSignedStageSuffix(const QJsonDocument& doc)
{
    const int approvals = ExtractVaultPackageApprovalCount(doc);
    const int total = ExtractVaultPackageTotalSigners(doc);

    if (approvals > 0 && total > 0) {
        return QStringLiteral("signed-%1of%2").arg(approvals).arg(total);
    }

    if (approvals > 0) {
        return QStringLiteral("signed-%1").arg(approvals);
    }

    return QStringLiteral("signed");
}

ApproveVaultPaymentDialog::ApproveVaultPaymentDialog(WalletModel* wallet_model, QWidget* parent)
    : QDialog(parent),
      m_wallet_model(wallet_model)
{
    setWindowTitle(tr("Review / Approve Vault Payment"));
    resize(860, 660);

    auto* main_layout = new QVBoxLayout(this);

    auto* description = new QLabel(
        tr("Open a Kvanta5 vault payment file, review it, approve it with this wallet's signer key, "
           "then finalize and broadcast from this same screen once enough approvals are present. "
           "Approved and broadcasted stages are saved automatically in the vault_payments folder."),
        this);
    description->setWordWrap(true);
    main_layout->addWidget(description);

    m_package_text = new QTextEdit(this);
    m_package_text->setReadOnly(true);
    m_package_text->setPlaceholderText(tr("Vault payment file contents will appear here."));
    main_layout->addWidget(m_package_text, 3);

    m_status_text = new QTextEdit(this);
    m_status_text->setReadOnly(true);
    m_status_text->setMaximumHeight(170);
    m_status_text->setPlaceholderText(tr("Status will appear here."));
    main_layout->addWidget(m_status_text, 1);

    auto* button_layout = new QHBoxLayout();
    button_layout->addStretch();

    m_open_button = new QPushButton(tr("Open Payment File"), this);
    m_approve_button = new QPushButton(tr("Approve Payment"), this);
    m_save_button = new QPushButton(tr("Save Approved Stage Again"), this);
    m_finalize_button = new QPushButton(tr("Finalize"), this);
    m_broadcast_button = new QPushButton(tr("Broadcast"), this);
    auto* close_button = new QPushButton(tr("Close"), this);

    m_approve_button->setEnabled(false);
    m_save_button->setEnabled(false);
    m_finalize_button->setEnabled(false);
    m_broadcast_button->setEnabled(false);

    button_layout->addWidget(m_open_button);
    button_layout->addWidget(m_approve_button);
    button_layout->addWidget(m_save_button);
    button_layout->addWidget(m_finalize_button);
    button_layout->addWidget(m_broadcast_button);
    button_layout->addWidget(close_button);

    main_layout->addLayout(button_layout);

    connect(m_open_button, &QPushButton::clicked, this, &ApproveVaultPaymentDialog::openPackage);
    connect(m_approve_button, &QPushButton::clicked, this, &ApproveVaultPaymentDialog::approvePackage);
    connect(m_save_button, &QPushButton::clicked, this, &ApproveVaultPaymentDialog::savePackageAs);
    connect(m_finalize_button, &QPushButton::clicked, this, &ApproveVaultPaymentDialog::finalizePackage);
    connect(m_broadcast_button, &QPushButton::clicked, this, &ApproveVaultPaymentDialog::broadcastTransaction);
    connect(close_button, &QPushButton::clicked, this, &QDialog::reject);

    updateActionButtons();
}

void ApproveVaultPaymentDialog::updateActionButtons()
{
    const bool has_package = !m_package_json.trimmed().isEmpty();

    bool has_required_approvals = false;

    if (has_package) {
        QString parse_error;
        const QJsonDocument doc = ParseVaultPackageJson(m_package_json, &parse_error);
        if (!doc.isNull()) {
            has_required_approvals = VaultPackageHasRequiredApprovals(doc);
        }
    }

    if (m_approve_button) {
        m_approve_button->setEnabled(has_package && m_wallet_model != nullptr);
    }

    if (m_save_button) {
        m_save_button->setEnabled(has_package);
    }

    if (m_finalize_button) {
        m_finalize_button->setEnabled(has_package && has_required_approvals && m_wallet_model != nullptr);
    }

    if (m_broadcast_button) {
        m_broadcast_button->setEnabled(!m_final_tx_hex.trimmed().isEmpty() && m_wallet_model != nullptr);
    }
}

void ApproveVaultPaymentDialog::openPackage()
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
        QMessageBox::critical(
            this,
            tr("Open Failed"),
            tr("Could not open vault payment file:\n%1").arg(filename));
        return;
    }

    const QByteArray data = file.readAll();
    file.close();

    m_current_filename = filename;
    m_package_json = QString::fromUtf8(data);
    m_final_tx_hex.clear();

    QString parse_error;
    const QJsonDocument doc = ParseVaultPackageJson(m_package_json, &parse_error);

    m_package_text->setPlainText(m_package_json);

    if (doc.isNull()) {
        m_status_text->setPlainText(
            tr("Loaded vault payment file:\n%1\n\n%2")
                .arg(filename)
                .arg(parse_error));
    } else {
        const bool ready_to_finalize = VaultPackageHasRequiredApprovals(doc);

        m_status_text->setPlainText(
            tr("Loaded vault payment file:\n%1\n\n"
               "Approval status: %2\n\n"
               "%3")
                .arg(filename)
                .arg(VaultPackageApprovalSummary(doc))
                .arg(ready_to_finalize
                    ? tr("Enough approvals are present. You may finalize this vault payment.")
                    : tr("Review the contents, then click Approve Payment if everything looks correct.")));
    }

    updateActionButtons();
}

void ApproveVaultPaymentDialog::approvePackage()
{
    if (!m_wallet_model) {
        QMessageBox::critical(this, tr("Approve Vault Payment"), tr("Wallet model is not available."));
        return;
    }

    if (m_package_json.trimmed().isEmpty()) {
        QMessageBox::critical(this, tr("Approve Vault Payment"), tr("Open a vault payment file first."));
        return;
    }

    m_final_tx_hex.clear();

    QString error;
    const QString approved_json = m_wallet_model->approveVaultTxPackage(m_package_json, error);

    if (!error.isEmpty()) {
        QMessageBox::critical(this, tr("Approve Vault Payment Failed"), error);
        m_status_text->setPlainText(error);
        updateActionButtons();
        return;
    }

    m_package_json = approved_json;
    m_package_text->setPlainText(m_package_json);

    QString parse_error;
    const QJsonDocument doc = ParseVaultPackageJson(m_package_json, &parse_error);
    if (doc.isNull()) {
        QMessageBox::critical(this, tr("Approve Vault Payment"), parse_error);
        m_status_text->setPlainText(parse_error);
        updateActionButtons();
        return;
    }

    const QString stage_suffix = BuildSignedStageSuffix(doc);
    const bool ready_to_finalize = VaultPackageHasRequiredApprovals(doc);

    QString txid;
    QString save_error;
    const QString saved_path = SaveVaultPaymentPackageToStageFile(
        m_package_json,
        stage_suffix,
        &txid,
        &save_error);

    if (saved_path.isEmpty()) {
        QMessageBox::critical(this, tr("Save Approved Vault Payment"), save_error);
        m_status_text->setPlainText(
            tr("Vault payment approved, but saving the approved stage failed:\n%1")
                .arg(save_error));
        updateActionButtons();
        return;
    }

    m_current_filename = saved_path;

    m_status_text->setPlainText(
        tr("Vault payment approved successfully by this wallet.\n\n"
           "Saved approved stage:\n%1\n\n"
           "Transaction id:\n%2\n"
           "Approval stage: %3\n"
           "Approval status: %4\n\n"
           "%5")
            .arg(saved_path)
            .arg(txid)
            .arg(stage_suffix)
            .arg(VaultPackageApprovalSummary(doc))
            .arg(ready_to_finalize
                ? tr("Enough approvals are present. You may finalize and then broadcast this vault payment from this screen.")
                : tr("Send this file to the next signer.")));

    updateActionButtons();
}

void ApproveVaultPaymentDialog::savePackageAs()
{
    if (m_package_json.trimmed().isEmpty()) {
        QMessageBox::critical(this, tr("Save Vault Payment"), tr("There is no vault payment file to save."));
        return;
    }

    QString parse_error;
    const QJsonDocument doc = ParseVaultPackageJson(m_package_json, &parse_error);
    if (doc.isNull()) {
        QMessageBox::critical(this, tr("Save Vault Payment"), parse_error);
        return;
    }

    const QString stage_suffix = BuildSignedStageSuffix(doc);

    QString txid;
    QString save_error;
    const QString saved_path = SaveVaultPaymentPackageToStageFile(
        m_package_json,
        stage_suffix,
        &txid,
        &save_error);

    if (saved_path.isEmpty()) {
        QMessageBox::critical(this, tr("Save Failed"), save_error);
        return;
    }

    m_current_filename = saved_path;

    m_status_text->setPlainText(
        tr("Approved vault payment stage saved:\n%1\n\n"
           "Transaction id:\n%2\n"
           "Approval stage: %3\n"
           "Approval status: %4")
            .arg(saved_path)
            .arg(txid)
            .arg(stage_suffix)
            .arg(VaultPackageApprovalSummary(doc)));

    updateActionButtons();
}

void ApproveVaultPaymentDialog::finalizePackage()
{
    if (!m_wallet_model) {
        QMessageBox::critical(this, tr("Finalize Vault Payment"), tr("Wallet model is not available."));
        return;
    }

    if (m_package_json.trimmed().isEmpty()) {
        QMessageBox::critical(this, tr("Finalize Vault Payment"), tr("Open a vault payment file first."));
        return;
    }

    QString parse_error;
    const QJsonDocument doc = ParseVaultPackageJson(m_package_json, &parse_error);
    if (doc.isNull()) {
        QMessageBox::critical(this, tr("Finalize Vault Payment"), parse_error);
        return;
    }

    if (!VaultPackageHasRequiredApprovals(doc)) {
        QMessageBox::critical(
            this,
            tr("Finalize Vault Payment"),
            tr("This vault payment does not have enough approvals yet.\n\nApproval status: %1")
                .arg(VaultPackageApprovalSummary(doc)));
        m_final_tx_hex.clear();
        updateActionButtons();
        return;
    }

    QString error;
    m_final_tx_hex = m_wallet_model->finalizeVaultTxPackage(m_package_json, error);

    if (!error.isEmpty()) {
        QMessageBox::critical(this, tr("Finalize Failed"), error);
        m_status_text->setPlainText(error);
        m_final_tx_hex.clear();
        updateActionButtons();
        return;
    }

    m_status_text->setPlainText(
        tr("Vault payment finalized successfully.\n\n"
           "Approval status: %1\n\n"
           "This payment is ready to broadcast.")
            .arg(VaultPackageApprovalSummary(doc)));

    updateActionButtons();
}

void ApproveVaultPaymentDialog::broadcastTransaction()
{
    if (!m_wallet_model) {
        QMessageBox::critical(this, tr("Broadcast Vault Payment"), tr("Wallet model is not available."));
        return;
    }

    if (m_final_tx_hex.trimmed().isEmpty()) {
        QMessageBox::critical(this, tr("Broadcast Vault Payment"), tr("Finalize the vault payment first."));
        updateActionButtons();
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
    const QString broadcast_txid = m_wallet_model->broadcastTransactionHex(m_final_tx_hex, error);

    if (!error.isEmpty()) {
        QMessageBox::critical(this, tr("Broadcast Failed"), error);
        m_status_text->setPlainText(error);
        updateActionButtons();
        return;
    }

    QString save_error;
    const QString saved_path = SaveVaultPaymentPackageToBroadcastedFile(
        m_package_json,
        broadcast_txid,
        &save_error);

    if (saved_path.isEmpty()) {
        QMessageBox::warning(
            this,
            tr("Broadcast Saved Warning"),
            tr("Vault payment broadcast successfully, but saving the broadcasted stage failed:\n%1")
                .arg(save_error));

        m_status_text->setPlainText(
            tr("Vault payment broadcast successfully.\n\n"
               "Network transaction ID:\n%1\n\n"
               "Broadcasted stage save failed:\n%2")
                .arg(broadcast_txid)
                .arg(save_error));
    } else {
        m_current_filename = saved_path;

        m_status_text->setPlainText(
            tr("Vault payment broadcast successfully.\n\n"
               "Network transaction ID:\n%1\n\n"
               "Broadcasted package saved:\n%2")
                .arg(broadcast_txid)
                .arg(saved_path));
    }

    m_final_tx_hex.clear();
    updateActionButtons();
}
