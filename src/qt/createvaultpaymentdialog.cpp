#include <qt/createvaultpaymentdialog.h>

#include <qt/sendcoinsrecipient.h>
#include <qt/walletmodel.h>

#include <common/args.h>
#include <key_io.h>
#include <policy/feerate.h>
#include <primitives/transaction.h>
#include <util/fs.h>
#include <util/moneystr.h>
#include <wallet/coincontrol.h>

#include <algorithm>
#include <optional>

#include <QAbstractItemView>
#include <QCheckBox>
#include <QComboBox>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFont>
#include <QFormLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QIODevice>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QSignalBlocker>
#include <QStackedWidget>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QTextEdit>
#include <QVBoxLayout>

static QString FormatKv5Amount(const CAmount amount)
{
    return QString::fromStdString(FormatMoney(amount));
}

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

    /*
     * Never overwrite vault payment stage files. Duplicate stage creation is
     * allowed, but it must produce a new filename.
     */
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

CreateVaultPaymentDialog::CreateVaultPaymentDialog(WalletModel* wallet_model, QWidget* parent)
    : QDialog(parent),
      m_wallet_model(wallet_model)
{
    setWindowTitle(tr("Create Vault Payment"));
    setMinimumSize(760, 520);
    resize(880, 640);

    auto* main_layout = new QVBoxLayout(this);
    main_layout->setContentsMargins(12, 10, 12, 10);
    main_layout->setSpacing(8);

    auto* title = new QLabel(tr("Create Kvanta5 Vault Payment"), this);
    QFont title_font = title->font();
    title_font.setPointSize(title_font.pointSize() + 3);
    title_font.setBold(true);
    title->setFont(title_font);
    main_layout->addWidget(title);

    auto* description = new QLabel(
        tr("Create a P2QR multi-signature vault payment package. "
           "Choose the vault and recipients, select funding UTXOs, then review and create the package."),
        this);
    description->setWordWrap(true);
    main_layout->addWidget(description);

    auto* step_label = new QLabel(this);
    QFont step_font = step_label->font();
    step_font.setBold(true);
    step_label->setFont(step_font);
    main_layout->addWidget(step_label);

    auto* pages = new QStackedWidget(this);
    pages->setObjectName(QStringLiteral("vaultPaymentWizardPages"));
    main_layout->addWidget(pages, 1);

    /*
     * Page 1: Vault + Recipients
     */
    auto* payment_page = new QWidget(this);
    auto* payment_layout = new QVBoxLayout(payment_page);
    payment_layout->setContentsMargins(0, 0, 0, 0);
    payment_layout->setSpacing(8);

    auto* from_group = new QGroupBox(tr("Send From Vault"), payment_page);
    auto* from_layout = new QVBoxLayout(from_group);

    auto* from_form = new QFormLayout();
    m_send_from_combo = new QComboBox(from_group);
    from_form->addRow(tr("Vault"), m_send_from_combo);
    from_layout->addLayout(from_form);

    m_signers_table = new QTableWidget(from_group);
    m_signers_table->setColumnCount(3);
    m_signers_table->setHorizontalHeaderLabels({
        tr("Signer"),
        tr("Signer Address"),
        tr("Local Key")
    });
    m_signers_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_signers_table->setSelectionMode(QAbstractItemView::SingleSelection);
    m_signers_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_signers_table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    m_signers_table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    m_signers_table->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    m_signers_table->setMaximumHeight(105);
    from_layout->addWidget(m_signers_table);

    payment_layout->addWidget(from_group);

    auto* recipients_group = new QGroupBox(tr("Recipients"), payment_page);
    auto* recipients_layout = new QVBoxLayout(recipients_group);

    auto* recipients_help = new QLabel(
        tr("Enter destination address and amount. Leave no partially filled recipient rows."),
        recipients_group);
    recipients_help->setWordWrap(true);
    recipients_layout->addWidget(recipients_help);

    m_recipients_table = new QTableWidget(recipients_group);
    m_recipients_table->setColumnCount(3);
    m_recipients_table->setHorizontalHeaderLabels({
        tr("Address"),
        tr("Amount"),
        tr("")
    });
    m_recipients_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_recipients_table->setSelectionMode(QAbstractItemView::SingleSelection);
    m_recipients_table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_recipients_table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    m_recipients_table->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    m_recipients_table->setMinimumHeight(140);
    recipients_layout->addWidget(m_recipients_table, 1);

    auto* recipient_button_layout = new QHBoxLayout();
    m_add_recipient_button = new QPushButton(tr("Add Recipient"), recipients_group);
    recipient_button_layout->addWidget(m_add_recipient_button);
    recipient_button_layout->addStretch();
    recipients_layout->addLayout(recipient_button_layout);

    payment_layout->addWidget(recipients_group, 1);

    pages->addWidget(payment_page);

    /*
     * Page 2: UTXOs + Fee/Change
     */
    auto* funding_page = new QWidget(this);
    auto* funding_layout = new QVBoxLayout(funding_page);
    funding_layout->setContentsMargins(0, 0, 0, 0);
    funding_layout->setSpacing(8);

    auto* fee_change_group = new QGroupBox(tr("Fee and Change"), funding_page);
    auto* fee_change_form = new QFormLayout(fee_change_group);

    m_fee_rate_edit = new QLineEdit(fee_change_group);
    m_fee_rate_edit->setPlaceholderText(tr("Fee rate in quarks/kvB, for example 1000"));
    m_fee_rate_edit->setText(QStringLiteral("1000"));
    m_fee_rate_edit->setToolTip(tr(
        "Explicit vault transaction fee rate in quarks per kvB. "
        "This rate is applied to the estimated final signed P2QR multisig transaction size before this wallet signs."));
    fee_change_form->addRow(tr("Fee rate"), m_fee_rate_edit);

    m_subtract_fee_checkbox = new QCheckBox(tr("Subtract fee from recipient amount(s)"), fee_change_group);
    m_subtract_fee_checkbox->setToolTip(tr(
        "Only enable this for send-max style vault payments. "
        "If disabled, the wallet preserves recipient amounts and takes the fee from change."));
    fee_change_form->addRow(QString(), m_subtract_fee_checkbox);

    m_change_mode_combo = new QComboBox(fee_change_group);
    m_change_mode_combo->addItem(tr("Return change to selected vault"), QStringLiteral("same_vault"));
    m_change_mode_combo->addItem(tr("Use custom change address"), QStringLiteral("custom"));
    fee_change_form->addRow(tr("Change"), m_change_mode_combo);

    m_custom_change_edit = new QLineEdit(fee_change_group);
    m_custom_change_edit->setPlaceholderText(tr("Custom Kvanta5 change address"));
    m_custom_change_edit->setEnabled(false);
    m_custom_change_edit->setToolTip(tr(
        "Use with care. For vault safety, this should normally be another known vault address. "
        "If left on the default mode, change returns to the selected vault."));
    fee_change_form->addRow(tr("Custom change"), m_custom_change_edit);

    funding_layout->addWidget(fee_change_group);

    auto* utxo_group = new QGroupBox(tr("Vault UTXO Selection"), funding_page);
    auto* utxo_layout = new QVBoxLayout(utxo_group);

    auto* utxo_help = new QLabel(
        tr("Select exact UTXOs to spend. This table is filtered to the selected vault. "
           "Click a row to toggle it. Vault payments do not use automatic coin selection."),
        utxo_group);
    utxo_help->setWordWrap(true);
    utxo_layout->addWidget(utxo_help);

    m_utxo_table = new QTableWidget(utxo_group);
    m_utxo_table->setColumnCount(8);
    m_utxo_table->setHorizontalHeaderLabels({
        tr("Use"),
        tr("Label"),
        tr("Amount"),
        tr("Confirmations"),
        tr("Type"),
        tr("Address"),
        tr("TXID"),
        tr("Vout")
    });
    m_utxo_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_utxo_table->setSelectionMode(QAbstractItemView::SingleSelection);
    m_utxo_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_utxo_table->horizontalHeader()->setStretchLastSection(false);
    m_utxo_table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    m_utxo_table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    m_utxo_table->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    m_utxo_table->horizontalHeader()->setSectionResizeMode(3, QHeaderView::ResizeToContents);
    m_utxo_table->horizontalHeader()->setSectionResizeMode(4, QHeaderView::ResizeToContents);
    m_utxo_table->horizontalHeader()->setSectionResizeMode(5, QHeaderView::Stretch);
    m_utxo_table->horizontalHeader()->setSectionResizeMode(6, QHeaderView::Stretch);
    m_utxo_table->horizontalHeader()->setSectionResizeMode(7, QHeaderView::ResizeToContents);
    m_utxo_table->setMinimumHeight(190);
    utxo_layout->addWidget(m_utxo_table, 1);

    auto* utxo_buttons = new QHBoxLayout();
    m_refresh_button = new QPushButton(tr("Refresh"), utxo_group);
    m_select_all_button = new QPushButton(tr("Select All"), utxo_group);
    m_clear_button = new QPushButton(tr("Clear Selection"), utxo_group);

    utxo_buttons->addWidget(m_refresh_button);
    utxo_buttons->addWidget(m_select_all_button);
    utxo_buttons->addWidget(m_clear_button);
    utxo_buttons->addStretch();
    utxo_layout->addLayout(utxo_buttons);

    funding_layout->addWidget(utxo_group, 1);

    auto* summary_group = new QGroupBox(tr("Funding Summary"), funding_page);
    auto* summary_grid = new QGridLayout(summary_group);
    summary_grid->setColumnStretch(1, 1);

    m_selected_utxos_label = new QLabel(tr("0"), summary_group);
    m_selected_amount_label = new QLabel(FormatKv5Amount(0), summary_group);
    m_recipient_amount_label = new QLabel(FormatKv5Amount(0), summary_group);
    m_fee_policy_label = new QLabel(tr("Fee rate: 1000 quarks/kvB"), summary_group);
    m_change_hint_label = new QLabel(tr("Select a vault, select UTXOs, and enter recipients."), summary_group);
    m_change_hint_label->setWordWrap(true);

    summary_grid->addWidget(new QLabel(tr("Selected UTXOs:"), summary_group), 0, 0);
    summary_grid->addWidget(m_selected_utxos_label, 0, 1);
    summary_grid->addWidget(new QLabel(tr("Selected total:"), summary_group), 1, 0);
    summary_grid->addWidget(m_selected_amount_label, 1, 1);
    summary_grid->addWidget(new QLabel(tr("Recipient total:"), summary_group), 2, 0);
    summary_grid->addWidget(m_recipient_amount_label, 2, 1);
    summary_grid->addWidget(new QLabel(tr("Fee policy:"), summary_group), 3, 0);
    summary_grid->addWidget(m_fee_policy_label, 3, 1);
    summary_grid->addWidget(new QLabel(tr("Status:"), summary_group), 4, 0);
    summary_grid->addWidget(m_change_hint_label, 4, 1);

    funding_layout->addWidget(summary_group);

    pages->addWidget(funding_page);

    /*
     * Page 3: Review + Create
     */
    auto* review_page = new QWidget(this);
    auto* review_layout = new QVBoxLayout(review_page);
    review_layout->setContentsMargins(0, 0, 0, 0);
    review_layout->setSpacing(8);

    auto* review_group = new QGroupBox(tr("Review and Create Package"), review_page);
    auto* review_group_layout = new QVBoxLayout(review_group);

    auto* review_help = new QLabel(
        tr("Review the vault payment details. If everything looks right, create the vault payment package. "
           "The package will be saved automatically in the vault_payments folder."),
        review_group);
    review_help->setWordWrap(true);
    review_group_layout->addWidget(review_help);

    m_status_text = new QTextEdit(review_group);
    m_status_text->setReadOnly(true);
    m_status_text->setPlaceholderText(tr("Review and package creation status will appear here."));
    review_group_layout->addWidget(m_status_text, 1);

    review_layout->addWidget(review_group, 1);

    pages->addWidget(review_page);

    auto update_review_text = [this]() {
        if (!m_status_text) {
            return;
        }

        QString recipient_error;
        const std::optional<CAmount> recipient_total = recipientsAmount(&recipient_error);

        bool fee_rate_ok = false;
        const qlonglong fee_rate_quarks_per_kvb =
            m_fee_rate_edit ? m_fee_rate_edit->text().trimmed().toLongLong(&fee_rate_ok) : 0;

        QString custom_change_text = tr("Return change to selected vault");
        if (m_change_mode_combo &&
            m_change_mode_combo->currentData().toString() == QStringLiteral("custom")) {
            custom_change_text = m_custom_change_edit ? m_custom_change_edit->text().trimmed() : QString{};
            if (custom_change_text.isEmpty()) {
                custom_change_text = tr("(custom change address not entered)");
            }
        }

        QString review = tr("Vault Payment Review\n\n"
                            "Vault program:\n%1\n\n"
                            "Recipients:\n%2\n\n"
                            "Selected UTXOs:\n%3\n\n"
                            "Selected total:\n%4 KV5\n\n"
                            "Recipient total:\n%5\n\n"
                            "Fee rate:\n%6\n\n"
                            "Subtract fee from amount:\n%7\n\n"
                            "Change:\n%8\n\n"
                            "Save folder:\n%9")
            .arg(selectedVaultProgram().isEmpty() ? tr("(none)") : selectedVaultProgram())
            .arg(m_recipients_table ? m_recipients_table->rowCount() : 0)
            .arg(selectedVaultUtxoCount())
            .arg(FormatKv5Amount(selectedVaultAmount()))
            .arg(recipient_total ? FormatKv5Amount(*recipient_total) + QStringLiteral(" KV5") : tr("invalid: %1").arg(recipient_error))
            .arg((fee_rate_ok && fee_rate_quarks_per_kvb > 0) ?
                tr("%1 quarks/kvB").arg(fee_rate_quarks_per_kvb) :
                tr("invalid"))
            .arg((m_subtract_fee_checkbox && m_subtract_fee_checkbox->isChecked()) ? tr("yes") : tr("no"))
            .arg(custom_change_text)
            .arg(Kvanta5VaultPaymentsDir());

        m_status_text->setPlainText(review);
    };

    auto* button_layout = new QHBoxLayout();
    button_layout->addStretch();

    auto* back_button = new QPushButton(tr("Back"), this);
    back_button->setObjectName(QStringLiteral("vaultWizardBackButton"));

    auto* next_button = new QPushButton(tr("Next"), this);
    next_button->setObjectName(QStringLiteral("vaultWizardNextButton"));

    m_cancel_button = new QPushButton(tr("Cancel"), this);
    m_create_button = new QPushButton(tr("Create Vault Payment Package"), this);
    m_close_button = new QPushButton(tr("Close"), this);

    m_create_button->setEnabled(false);
    m_create_button->setVisible(false);
    m_close_button->setVisible(false);

    button_layout->addWidget(back_button);
    button_layout->addWidget(next_button);
    button_layout->addWidget(m_cancel_button);
    button_layout->addWidget(m_create_button);
    button_layout->addWidget(m_close_button);
    main_layout->addLayout(button_layout);

    auto update_wizard_buttons = [this, pages, step_label, back_button, next_button, update_review_text]() {
        const int page = pages->currentIndex();

        if (step_label) {
            if (page == 0) {
                step_label->setText(tr("Step 1 of 3: Vault and Recipients"));
            } else if (page == 1) {
                step_label->setText(tr("Step 2 of 3: Funding, Fee, and Change"));
            } else {
                step_label->setText(tr("Step 3 of 3: Review and Create"));
            }
        }

        if (back_button) {
            back_button->setEnabled(page > 0 && !m_package_created);
            back_button->setVisible(!m_package_created);
        }

        if (next_button) {
            next_button->setEnabled(page < 2 && !m_package_created);
            next_button->setVisible(!m_package_created && page < 2);
            next_button->setDefault(page < 2);
        }

        if (m_create_button) {
            m_create_button->setVisible(!m_package_created && page == 2);
            if (page == 2) {
                m_create_button->setDefault(true);
            }
        }

        if (page == 2 && !m_package_created) {
            update_review_text();
        }
    };

    connect(back_button, &QPushButton::clicked, this, [pages, update_wizard_buttons]() {
        const int page = pages->currentIndex();
        if (page > 0) {
            pages->setCurrentIndex(page - 1);
        }
        update_wizard_buttons();
    });

    connect(next_button, &QPushButton::clicked, this, [pages, update_wizard_buttons]() {
        const int page = pages->currentIndex();
        if (page < 2) {
            pages->setCurrentIndex(page + 1);
        }
        update_wizard_buttons();
    });

    connect(m_cancel_button, &QPushButton::clicked, this, &QDialog::reject);
    connect(m_create_button, &QPushButton::clicked, this, &CreateVaultPaymentDialog::createPackage);
    connect(m_close_button, &QPushButton::clicked, this, &QDialog::accept);

    connect(m_send_from_combo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &CreateVaultPaymentDialog::selectedVaultChanged);

    connect(m_add_recipient_button, &QPushButton::clicked, this, &CreateVaultPaymentDialog::addRecipientRow);

    connect(m_refresh_button, &QPushButton::clicked, this, &CreateVaultPaymentDialog::refreshVaultsAndUtxos);
    connect(m_select_all_button, &QPushButton::clicked, this, &CreateVaultPaymentDialog::selectAllVaultUtxos);
    connect(m_clear_button, &QPushButton::clicked, this, &CreateVaultPaymentDialog::clearVaultUtxoSelection);

    connect(m_fee_rate_edit, &QLineEdit::textChanged, this, &CreateVaultPaymentDialog::updateSummary);
    connect(m_subtract_fee_checkbox, &QCheckBox::stateChanged, this, &CreateVaultPaymentDialog::updateSummary);
    connect(m_change_mode_combo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &CreateVaultPaymentDialog::changeModeChanged);
    connect(m_custom_change_edit, &QLineEdit::textChanged, this, &CreateVaultPaymentDialog::updateSummary);
    connect(m_utxo_table, &QTableWidget::itemChanged, this, &CreateVaultPaymentDialog::updateSummary);

    connect(m_utxo_table, &QTableWidget::cellClicked, this, [this](int row, int column) {
        if (!m_utxo_table || row < 0 || row >= m_utxo_table->rowCount()) {
            return;
        }

        /*
         * The checkbox itself already toggles via itemChanged. For the rest
         * of the row, make one click toggle the UTXO too.
         */
        if (column == 0) {
            return;
        }

        QTableWidgetItem* use_item = m_utxo_table->item(row, 0);
        if (!use_item) {
            return;
        }

        const QSignalBlocker blocker(m_utxo_table);
        use_item->setCheckState(use_item->checkState() == Qt::Checked ? Qt::Unchecked : Qt::Checked);
        updateSummary();
    });

    addRecipientRow();
    refreshVaultsAndUtxos();
    update_wizard_buttons();
}

QString CreateVaultPaymentDialog::selectedVaultProgram() const
{
    if (!m_send_from_combo || m_send_from_combo->currentIndex() < 0) {
        return {};
    }

    return m_send_from_combo->currentData().toString();
}

CAmount CreateVaultPaymentDialog::selectedVaultAmount() const
{
    CAmount total{0};

    if (!m_utxo_table) {
        return total;
    }

    for (int row = 0; row < m_utxo_table->rowCount(); ++row) {
        const QTableWidgetItem* use_item = m_utxo_table->item(row, 0);
        if (!use_item || use_item->checkState() != Qt::Checked) {
            continue;
        }

        if (row < 0 || row >= m_visible_vault_utxos.size()) {
            continue;
        }

        total += m_visible_vault_utxos[row].amount;
    }

    return total;
}

int CreateVaultPaymentDialog::selectedVaultUtxoCount() const
{
    int count{0};

    if (!m_utxo_table) {
        return count;
    }

    for (int row = 0; row < m_utxo_table->rowCount(); ++row) {
        const QTableWidgetItem* use_item = m_utxo_table->item(row, 0);
        if (use_item && use_item->checkState() == Qt::Checked) {
            ++count;
        }
    }

    return count;
}

std::optional<CAmount> CreateVaultPaymentDialog::recipientsAmount(QString* error_out) const
{
    CAmount total{0};

    if (!m_recipients_table) {
        if (error_out) *error_out = tr("Recipient table is not available.");
        return std::nullopt;
    }

    for (int row = 0; row < m_recipients_table->rowCount(); ++row) {
        auto* address_edit = qobject_cast<QLineEdit*>(m_recipients_table->cellWidget(row, 0));
        auto* amount_edit = qobject_cast<QLineEdit*>(m_recipients_table->cellWidget(row, 1));

        if (!address_edit || !amount_edit) {
            if (error_out) *error_out = tr("Recipient row %1 is invalid.").arg(row + 1);
            return std::nullopt;
        }

        const QString address = address_edit->text().trimmed();
        const QString amount_qstr = amount_edit->text().trimmed();

        if (address.isEmpty() && amount_qstr.isEmpty()) {
            continue;
        }

        if (address.isEmpty()) {
            if (error_out) *error_out = tr("Recipient row %1 is missing an address.").arg(row + 1);
            return std::nullopt;
        }

        if (!m_wallet_model || !m_wallet_model->validateAddress(address)) {
            if (error_out) *error_out = tr("Recipient row %1 has an invalid address.").arg(row + 1);
            return std::nullopt;
        }

        const std::optional<CAmount> parsed_amount = ParseMoney(amount_qstr.toStdString());
        if (!parsed_amount || *parsed_amount <= 0) {
            if (error_out) *error_out = tr("Recipient row %1 has an invalid amount.").arg(row + 1);
            return std::nullopt;
        }

        total += *parsed_amount;
    }

    if (total <= 0) {
        if (error_out) *error_out = tr("Enter at least one recipient amount greater than zero.");
        return std::nullopt;
    }

    return total;
}

void CreateVaultPaymentDialog::lockAfterPackageCreated()
{
    m_package_created = true;

    if (m_send_from_combo) m_send_from_combo->setEnabled(false);
    if (m_signers_table) m_signers_table->setEnabled(false);

    if (m_recipients_table) m_recipients_table->setEnabled(false);
    if (m_add_recipient_button) m_add_recipient_button->setEnabled(false);

    if (m_fee_rate_edit) m_fee_rate_edit->setEnabled(false);
    if (m_subtract_fee_checkbox) m_subtract_fee_checkbox->setEnabled(false);

    if (m_change_mode_combo) m_change_mode_combo->setEnabled(false);
    if (m_custom_change_edit) m_custom_change_edit->setEnabled(false);

    if (m_utxo_table) m_utxo_table->setEnabled(false);

    if (m_refresh_button) m_refresh_button->setEnabled(false);
    if (m_select_all_button) m_select_all_button->setEnabled(false);
    if (m_clear_button) m_clear_button->setEnabled(false);

    if (auto* back_button = findChild<QPushButton*>(QStringLiteral("vaultWizardBackButton"))) {
        back_button->setEnabled(false);
        back_button->setVisible(false);
    }

    if (auto* next_button = findChild<QPushButton*>(QStringLiteral("vaultWizardNextButton"))) {
        next_button->setEnabled(false);
        next_button->setVisible(false);
    }

    if (m_create_button) {
        m_create_button->setEnabled(false);
        m_create_button->setVisible(false);
    }

    if (m_cancel_button) {
        m_cancel_button->setEnabled(false);
        m_cancel_button->setVisible(false);
    }

    if (m_close_button) {
        m_close_button->setEnabled(true);
        m_close_button->setVisible(true);
        m_close_button->setDefault(true);
        m_close_button->setFocus();
    }

    if (m_change_hint_label) {
        m_change_hint_label->setText(tr("Vault payment package created. Close this window to continue."));
    }
}

void CreateVaultPaymentDialog::refreshVaultsAndUtxos()
{
    if (!m_wallet_model) {
        if (m_create_button) {
            m_create_button->setEnabled(false);
        }
        return;
    }

    const QString previous_program = selectedVaultProgram();

    m_vaults = m_wallet_model->listVaults();
    m_all_vault_utxos = m_wallet_model->listVaultUTXOs();

    m_send_from_combo->blockSignals(true);
    m_send_from_combo->clear();

    int restore_index = -1;

    for (int i = 0; i < m_vaults.size(); ++i) {
        const auto& vault = m_vaults[i];

        const QString label = vault.label.isEmpty() ? tr("(unlabeled vault)") : vault.label;
        const QString item_text = tr("%1 | %2-of-%3 | %4 | %5 KV5")
            .arg(label)
            .arg(vault.required)
            .arg(vault.total)
            .arg(vault.type)
            .arg(FormatKv5Amount(vault.balance));

        m_send_from_combo->addItem(item_text, vault.program);

        if (!previous_program.isEmpty() && vault.program == previous_program) {
            restore_index = i;
        }
    }

    if (restore_index >= 0) {
        m_send_from_combo->setCurrentIndex(restore_index);
    } else if (m_send_from_combo->count() > 0) {
        m_send_from_combo->setCurrentIndex(0);
    }

    m_send_from_combo->blockSignals(false);

    selectedVaultChanged();

    if (m_status_text) {
        m_status_text->setPlainText(
            tr("Loaded %1 vault(s) and %2 vault UTXO(s).\n\n"
               "Vault payment files will be saved automatically in:\n%3")
                .arg(m_vaults.size())
                .arg(m_all_vault_utxos.size())
                .arg(Kvanta5VaultPaymentsDir()));
    }
}

void CreateVaultPaymentDialog::selectedVaultChanged()
{
    const QString program = selectedVaultProgram();

    m_signers_table->blockSignals(true);
    m_signers_table->setRowCount(0);

    for (const auto& vault : m_vaults) {
        if (vault.program != program) {
            continue;
        }

        for (int row = 0; row < vault.signers.size(); ++row) {
            const auto& signer = vault.signers[row];

            m_signers_table->insertRow(row);
            m_signers_table->setItem(row, 0, new QTableWidgetItem(QString::number(signer.signer_index)));

            QString signer_address_short = signer.address;
            if (signer_address_short.size() > 44) {
                signer_address_short = signer_address_short.left(20) + QStringLiteral("...") + signer_address_short.right(20);
            }

            m_signers_table->setItem(row, 1, new QTableWidgetItem(signer_address_short));
            m_signers_table->setItem(row, 2, new QTableWidgetItem(signer.has_local_private_key ? tr("Yes") : tr("No")));
        }

        break;
    }

    m_signers_table->blockSignals(false);

    refreshVaultUtxos();
    updateSummary();
}

void CreateVaultPaymentDialog::refreshVaultUtxos()
{
    const QString program = selectedVaultProgram();

    m_visible_vault_utxos.clear();

    for (const auto& utxo : m_all_vault_utxos) {
        if (utxo.program == program) {
            m_visible_vault_utxos.push_back(utxo);
        }
    }

    m_utxo_table->blockSignals(true);
    m_utxo_table->setRowCount(0);

    for (int row = 0; row < m_visible_vault_utxos.size(); ++row) {
        const auto& utxo = m_visible_vault_utxos[row];

        m_utxo_table->insertRow(row);

        auto* use_item = new QTableWidgetItem();
        use_item->setFlags(Qt::ItemIsUserCheckable | Qt::ItemIsEnabled | Qt::ItemIsSelectable);
        use_item->setCheckState(Qt::Unchecked);
        m_utxo_table->setItem(row, 0, use_item);

        m_utxo_table->setItem(row, 1, new QTableWidgetItem(utxo.label));
        m_utxo_table->setItem(row, 2, new QTableWidgetItem(FormatKv5Amount(utxo.amount)));
        m_utxo_table->setItem(row, 3, new QTableWidgetItem(QString::number(utxo.confirmations)));
        m_utxo_table->setItem(row, 4, new QTableWidgetItem(utxo.type));
        m_utxo_table->setItem(row, 5, new QTableWidgetItem(utxo.address));
        m_utxo_table->setItem(row, 6, new QTableWidgetItem(QString::fromStdString(utxo.outpoint.hash.ToString())));
        m_utxo_table->setItem(row, 7, new QTableWidgetItem(QString::number(utxo.outpoint.n)));
    }

    /*
     * Most vault payments will have exactly one vault UTXO available. Do not
     * force users to find and tick a tiny checkbox in that common case.
     */
    if (m_visible_vault_utxos.size() == 1) {
        QTableWidgetItem* use_item = m_utxo_table->item(0, 0);
        if (use_item) {
            use_item->setCheckState(Qt::Checked);
        }
    }

    m_utxo_table->blockSignals(false);

    updateSummary();
}

void CreateVaultPaymentDialog::selectAllVaultUtxos()
{
    if (!m_utxo_table) return;

    m_utxo_table->blockSignals(true);
    for (int row = 0; row < m_utxo_table->rowCount(); ++row) {
        QTableWidgetItem* use_item = m_utxo_table->item(row, 0);
        if (use_item) {
            use_item->setCheckState(Qt::Checked);
        }
    }
    m_utxo_table->blockSignals(false);

    updateSummary();
}

void CreateVaultPaymentDialog::clearVaultUtxoSelection()
{
    if (!m_utxo_table) return;

    m_utxo_table->blockSignals(true);
    for (int row = 0; row < m_utxo_table->rowCount(); ++row) {
        QTableWidgetItem* use_item = m_utxo_table->item(row, 0);
        if (use_item) {
            use_item->setCheckState(Qt::Unchecked);
        }
    }
    m_utxo_table->blockSignals(false);

    updateSummary();
}

void CreateVaultPaymentDialog::addRecipientRow()
{
    const int row = m_recipients_table->rowCount();
    m_recipients_table->insertRow(row);

    auto* address_edit = new QLineEdit(this);
    address_edit->setPlaceholderText(tr("Destination Kvanta5 address"));

    auto* amount_edit = new QLineEdit(this);
    amount_edit->setPlaceholderText(tr("Amount, for example 1.00000000"));

    auto* remove_button = new QPushButton(tr("Remove"), this);

    m_recipients_table->setCellWidget(row, 0, address_edit);
    m_recipients_table->setCellWidget(row, 1, amount_edit);
    m_recipients_table->setCellWidget(row, 2, remove_button);

    connect(address_edit, &QLineEdit::textChanged, this, &CreateVaultPaymentDialog::updateSummary);
    connect(amount_edit, &QLineEdit::textChanged, this, &CreateVaultPaymentDialog::updateSummary);

    connect(remove_button, &QPushButton::clicked, this, [this, remove_button]() {
        for (int r = 0; r < m_recipients_table->rowCount(); ++r) {
            if (m_recipients_table->cellWidget(r, 2) == remove_button) {
                m_recipients_table->removeRow(r);
                break;
            }
        }

        if (m_recipients_table->rowCount() == 0) {
            addRecipientRow();
        }

        updateSummary();
    });

    updateSummary();
}

void CreateVaultPaymentDialog::changeModeChanged()
{
    const bool custom = m_change_mode_combo &&
        m_change_mode_combo->currentData().toString() == QStringLiteral("custom");

    if (m_custom_change_edit) {
        m_custom_change_edit->setEnabled(custom);
    }

    updateSummary();
}

void CreateVaultPaymentDialog::updateSummary()
{
    if (m_package_created) {
        if (m_create_button) {
            m_create_button->setEnabled(false);
        }

        if (m_change_hint_label) {
            m_change_hint_label->setText(tr("Vault payment package created. Close this window to continue."));
        }

        return;
    }

    const CAmount selected_total = selectedVaultAmount();
    const int selected_count = selectedVaultUtxoCount();

    if (m_selected_utxos_label) {
        m_selected_utxos_label->setText(QString::number(selected_count));
    }

    if (m_selected_amount_label) {
        m_selected_amount_label->setText(FormatKv5Amount(selected_total));
    }

    QString recipient_error;
    const std::optional<CAmount> recipient_total = recipientsAmount(&recipient_error);

    if (m_recipient_amount_label) {
        m_recipient_amount_label->setText(recipient_total ? FormatKv5Amount(*recipient_total) : tr("invalid"));
    }

    bool fee_rate_ok = false;
    const qlonglong fee_rate_quarks_per_kvb =
        m_fee_rate_edit ? m_fee_rate_edit->text().trimmed().toLongLong(&fee_rate_ok) : 0;

    if (m_fee_policy_label) {
        if (fee_rate_ok && fee_rate_quarks_per_kvb > 0) {
            m_fee_policy_label->setText(
                tr("Fee rate: %1 quarks/kvB").arg(fee_rate_quarks_per_kvb));
        } else {
            m_fee_policy_label->setText(tr("Fee rate: invalid"));
        }
    }

    bool can_create = true;
    QString hint;

    if (selectedVaultProgram().isEmpty()) {
        can_create = false;
        hint = tr("Select a vault to spend from.");
    } else if (!recipient_total) {
        can_create = false;
        hint = recipient_error;
    } else if (selected_count == 0) {
        can_create = false;
        hint = tr("Select at least one UTXO from the selected vault.");
    } else if (!fee_rate_ok || fee_rate_quarks_per_kvb <= 0) {
        can_create = false;
        hint = tr("Enter a valid fee rate greater than zero.");
    } else if (selected_total < *recipient_total) {
        can_create = false;
        hint = tr("Selected vault UTXOs do not cover recipient total before fees.");
    } else {
        const CAmount before_fee_change = selected_total - *recipient_total;

        if (m_change_mode_combo &&
            m_change_mode_combo->currentData().toString() == QStringLiteral("custom")) {
            const QString custom_change = m_custom_change_edit ? m_custom_change_edit->text().trimmed() : QString{};

            if (custom_change.isEmpty()) {
                can_create = false;
                hint = tr("Enter a custom change address, or return change to the selected vault.");
            } else if (!m_wallet_model || !m_wallet_model->validateAddress(custom_change)) {
                can_create = false;
                hint = tr("Custom change address is invalid.");
            } else {
                hint = tr(
                    "Custom change enabled. Verify the change address is intentional. "
                    "Recipient total leaves approximately %1 KV5 before final vault fee.")
                    .arg(FormatKv5Amount(before_fee_change));
            }
        } else if (m_subtract_fee_checkbox && m_subtract_fee_checkbox->isChecked()) {
            hint = tr("Ready. Subtract-fee mode is enabled, so recipient amount(s) may be reduced to pay the final vault fee.");
        } else {
            hint = tr("Ready. Approximate change before final vault fee: %1 KV5. Change returns to the selected vault.")
                .arg(FormatKv5Amount(before_fee_change));
        }
    }

    if (m_change_hint_label) {
        m_change_hint_label->setText(hint);
    }

    if (m_create_button) {
        m_create_button->setEnabled(can_create);
    }
}

void CreateVaultPaymentDialog::createPackage()
{
    if (m_package_created) {
        QMessageBox::information(
            this,
            tr("Create Vault Payment"),
            tr("A vault payment package has already been created from this window. Close this window and open a new one to create another package."));
        return;
    }

    if (!m_wallet_model) {
        QMessageBox::critical(this, tr("Create Vault Payment"), tr("Wallet model is not available."));
        return;
    }

    if (selectedVaultProgram().isEmpty()) {
        QMessageBox::critical(this, tr("Create Vault Payment"), tr("Select a vault to spend from."));
        return;
    }

    QString recipient_error;
    const std::optional<CAmount> recipient_total = recipientsAmount(&recipient_error);
    if (!recipient_total) {
        QMessageBox::critical(this, tr("Create Vault Payment"), recipient_error);
        return;
    }

    bool fee_rate_ok = false;
    const qlonglong fee_rate_quarks_per_kvb =
        m_fee_rate_edit ? m_fee_rate_edit->text().trimmed().toLongLong(&fee_rate_ok) : 0;

    if (!fee_rate_ok || fee_rate_quarks_per_kvb <= 0) {
        QMessageBox::critical(
            this,
            tr("Create Vault Payment"),
            tr("Enter a valid fee rate greater than zero, in quarks per kvB."));
        return;
    }

    if (selectedVaultUtxoCount() <= 0) {
        QMessageBox::critical(
            this,
            tr("Create Vault Payment"),
            tr("Select at least one UTXO from the selected vault."));
        return;
    }

    const CAmount selected_total = selectedVaultAmount();

    if (selected_total < *recipient_total) {
        QMessageBox::critical(
            this,
            tr("Create Vault Payment"),
            tr("Selected vault UTXOs do not cover recipient total before fees."));
        return;
    }

    QList<SendCoinsRecipient> recipients;

    for (int row = 0; row < m_recipients_table->rowCount(); ++row) {
        auto* address_edit = qobject_cast<QLineEdit*>(m_recipients_table->cellWidget(row, 0));
        auto* amount_edit = qobject_cast<QLineEdit*>(m_recipients_table->cellWidget(row, 1));

        if (!address_edit || !amount_edit) {
            continue;
        }

        const QString address = address_edit->text().trimmed();
        const QString amount_qstr = amount_edit->text().trimmed();

        if (address.isEmpty() && amount_qstr.isEmpty()) {
            continue;
        }

        const std::optional<CAmount> parsed_amount = ParseMoney(amount_qstr.toStdString());
        if (!parsed_amount || *parsed_amount <= 0) {
            QMessageBox::critical(
                this,
                tr("Create Vault Payment"),
                tr("Recipient row %1 has an invalid amount.").arg(row + 1));
            return;
        }

        SendCoinsRecipient recipient;
        recipient.address = address;
        recipient.amount = *parsed_amount;
        recipient.fSubtractFeeFromAmount =
            m_subtract_fee_checkbox && m_subtract_fee_checkbox->isChecked();
        recipients.append(recipient);
    }

    if (recipients.empty()) {
        QMessageBox::critical(this, tr("Create Vault Payment"), tr("Add at least one recipient."));
        return;
    }

    wallet::CCoinControl coin_control;
    coin_control.m_feerate = CFeeRate(static_cast<CAmount>(fee_rate_quarks_per_kvb));

    if (m_change_mode_combo &&
        m_change_mode_combo->currentData().toString() == QStringLiteral("custom")) {
        const QString custom_change = m_custom_change_edit ? m_custom_change_edit->text().trimmed() : QString{};

        if (custom_change.isEmpty()) {
            QMessageBox::critical(this, tr("Create Vault Payment"), tr("Enter a custom change address."));
            return;
        }

        if (!m_wallet_model->validateAddress(custom_change)) {
            QMessageBox::critical(this, tr("Create Vault Payment"), tr("Custom change address is invalid."));
            return;
        }

        const CTxDestination change_dest = DecodeDestination(custom_change.toStdString());
        if (!IsValidDestination(change_dest)) {
            QMessageBox::critical(this, tr("Create Vault Payment"), tr("Custom change address could not be decoded."));
            return;
        }

        coin_control.destChange = change_dest;
    }

    int selected_inputs{0};
    for (int row = 0; row < m_utxo_table->rowCount(); ++row) {
        const QTableWidgetItem* use_item = m_utxo_table->item(row, 0);
        if (!use_item || use_item->checkState() != Qt::Checked) {
            continue;
        }

        if (row < 0 || row >= m_visible_vault_utxos.size()) {
            continue;
        }

        coin_control.Select(m_visible_vault_utxos[row].outpoint);
        ++selected_inputs;
    }

    if (selected_inputs <= 0) {
        QMessageBox::critical(
            this,
            tr("Create Vault Payment"),
            tr("No valid vault UTXOs were selected."));
        return;
    }

    if (m_create_button) {
        m_create_button->setEnabled(false);
    }

    if (m_status_text) {
        m_status_text->setPlainText(tr(
            "Creating vault payment package...\n\n"
            "Vault program: %1\n"
            "Recipients: %2\n"
            "Selected UTXOs: %3\n"
            "Selected total: %4 KV5\n"
            "Recipient total: %5 KV5\n"
            "Fee rate: %6 quarks/kvB\n"
            "Subtract fee from amount: %7")
                .arg(selectedVaultProgram())
                .arg(recipients.size())
                .arg(selected_inputs)
                .arg(FormatKv5Amount(selected_total))
                .arg(FormatKv5Amount(*recipient_total))
                .arg(fee_rate_quarks_per_kvb)
                .arg((m_subtract_fee_checkbox && m_subtract_fee_checkbox->isChecked()) ? tr("yes") : tr("no")));
    }

    QString error;
    const QString package_json = m_wallet_model->createVaultTxPackage(
        recipients,
        coin_control,
        error);

    if (!error.isEmpty()) {
        QMessageBox::critical(this, tr("Create Vault Payment Failed"), error);
        if (m_status_text) {
            m_status_text->setPlainText(error);
        }
        updateSummary();
        return;
    }

    QString txid;
    QString save_error;
    const QString saved_path = SaveVaultPaymentPackageToStageFile(
        package_json,
        QString{},
        &txid,
        &save_error);

    if (saved_path.isEmpty()) {
        QMessageBox::critical(this, tr("Save Failed"), save_error);
        if (m_status_text) {
            m_status_text->setPlainText(save_error);
        }
        updateSummary();
        return;
    }

    if (m_status_text) {
        m_status_text->setPlainText(
            tr("Vault payment package created successfully.\n\n"
               "Saved:\n%1\n\n"
               "Transaction id:\n%2\n\n"
               "Vault program:\n%3\n\n"
               "Recipients:\n%4\n\n"
               "Selected UTXOs:\n%5\n\n"
               "Selected total:\n%6 KV5\n\n"
               "Recipient total:\n%7 KV5\n\n"
               "Fee rate used:\n%8 quarks/kvB\n\n"
               "Send this file to another vault signer for approval.")
                .arg(saved_path)
                .arg(txid)
                .arg(selectedVaultProgram())
                .arg(recipients.size())
                .arg(selected_inputs)
                .arg(FormatKv5Amount(selected_total))
                .arg(FormatKv5Amount(*recipient_total))
                .arg(fee_rate_quarks_per_kvb));
    }

    lockAfterPackageCreated();
}
