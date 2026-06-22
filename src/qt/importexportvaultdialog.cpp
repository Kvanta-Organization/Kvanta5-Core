#include <qt/importexportvaultdialog.h>

#include <qt/walletmodel.h>

#include <util/moneystr.h>

#include <QAbstractItemView>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QTextEdit>
#include <QVBoxLayout>

static QString FormatKv5AmountForVaultBackupDialog(const CAmount amount)
{
    return QString::fromStdString(FormatMoney(amount));
}

ImportExportVaultDialog::ImportExportVaultDialog(WalletModel* wallet_model, QWidget* parent)
    : QDialog(parent),
      m_wallet_model(wallet_model)
{
    setWindowTitle(tr("Import / Export Vault"));
    resize(900, 620);

    auto* main_layout = new QVBoxLayout(this);

    auto* title = new QLabel(tr("Import / Export Kvanta5 Vault"), this);
    QFont title_font = title->font();
    title_font.setPointSize(title_font.pointSize() + 3);
    title_font.setBold(true);
    title->setFont(title_font);
    main_layout->addWidget(title);

    m_wallet_label = new QLabel(this);
    main_layout->addWidget(m_wallet_label);

    auto* description = new QLabel(
        tr("Export selected Kvanta5 multisig vaults to .kv5vaultbackup.json backup files, "
           "or import a vault backup file into this wallet."),
        this);
    description->setWordWrap(true);
    main_layout->addWidget(description);

    auto* vaults_group = new QGroupBox(tr("Known Vaults"), this);
    auto* vaults_layout = new QVBoxLayout(vaults_group);

    m_export_hint_label = new QLabel(tr("Select a vault below to export its backup package."), this);
    m_export_hint_label->setWordWrap(true);
    vaults_layout->addWidget(m_export_hint_label);

    m_vaults_table = new QTableWidget(this);
    m_vaults_table->setColumnCount(7);
    m_vaults_table->setHorizontalHeaderLabels({
        tr("Label"),
        tr("Balance"),
        tr("Policy"),
        tr("Local Signers"),
        tr("Type"),
        tr("Address"),
        tr("Program")
    });
    m_vaults_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_vaults_table->setSelectionMode(QAbstractItemView::SingleSelection);
    m_vaults_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_vaults_table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    m_vaults_table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    m_vaults_table->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    m_vaults_table->horizontalHeader()->setSectionResizeMode(3, QHeaderView::ResizeToContents);
    m_vaults_table->horizontalHeader()->setSectionResizeMode(4, QHeaderView::ResizeToContents);
    m_vaults_table->horizontalHeader()->setSectionResizeMode(5, QHeaderView::Stretch);
    m_vaults_table->horizontalHeader()->setSectionResizeMode(6, QHeaderView::Stretch);
    m_vaults_table->setMinimumHeight(170);
    vaults_layout->addWidget(m_vaults_table);

    main_layout->addWidget(vaults_group);

    auto* export_group = new QGroupBox(tr("Export Selected Vault Backup"), this);
    auto* export_form = new QFormLayout(export_group);

    m_export_file_edit = new QLineEdit(this);
    m_export_file_edit->setPlaceholderText(tr("Destination .kv5vaultbackup.json file"));

    m_export_browse_button = new QPushButton(tr("Browse..."), this);

    auto* export_file_layout = new QHBoxLayout();
    export_file_layout->addWidget(m_export_file_edit, 1);
    export_file_layout->addWidget(m_export_browse_button);

    export_form->addRow(tr("Backup file"), export_file_layout);

    m_export_button = new QPushButton(tr("Export Selected Vault Backup"), this);
    export_form->addRow(QString(), m_export_button);

    main_layout->addWidget(export_group);

    auto* import_group = new QGroupBox(tr("Import Vault Backup"), this);
    auto* import_form = new QFormLayout(import_group);

    m_import_file_edit = new QLineEdit(this);
    m_import_file_edit->setPlaceholderText(tr("Source .kv5vaultbackup.json file"));

    m_import_browse_button = new QPushButton(tr("Browse..."), this);

    auto* import_file_layout = new QHBoxLayout();
    import_file_layout->addWidget(m_import_file_edit, 1);
    import_file_layout->addWidget(m_import_browse_button);

    m_import_label_edit = new QLineEdit(this);
    m_import_label_edit->setPlaceholderText(tr("Optional restored vault label"));

    import_form->addRow(tr("Backup file"), import_file_layout);
    import_form->addRow(tr("Label"), m_import_label_edit);

    m_import_button = new QPushButton(tr("Import Vault Backup"), this);
    import_form->addRow(QString(), m_import_button);

    main_layout->addWidget(import_group);

    m_status_text = new QTextEdit(this);
    m_status_text->setReadOnly(true);
    m_status_text->setMaximumHeight(130);
    m_status_text->setPlaceholderText(tr("Import/export status will appear here."));
    main_layout->addWidget(m_status_text);

    auto* buttons_layout = new QHBoxLayout();
    buttons_layout->addStretch();

    m_refresh_button = new QPushButton(tr("Refresh Vault List"), this);
    m_close_button = new QPushButton(tr("Close"), this);

    buttons_layout->addWidget(m_refresh_button);
    buttons_layout->addWidget(m_close_button);

    main_layout->addLayout(buttons_layout);

    connect(m_refresh_button, &QPushButton::clicked, this, &ImportExportVaultDialog::refreshVaults);
    connect(m_export_browse_button, &QPushButton::clicked, this, &ImportExportVaultDialog::browseExportFile);
    connect(m_export_button, &QPushButton::clicked, this, &ImportExportVaultDialog::exportSelectedVault);
    connect(m_import_browse_button, &QPushButton::clicked, this, &ImportExportVaultDialog::browseImportFile);
    connect(m_import_button, &QPushButton::clicked, this, &ImportExportVaultDialog::importVaultBackup);
    connect(m_close_button, &QPushButton::clicked, this, &QDialog::accept);

    connect(m_vaults_table, &QTableWidget::currentCellChanged, this, [this]() {
        const int row = selectedVaultRow();
        if (row < 0 || row >= m_vaults.size()) {
            return;
        }

        if (m_export_file_edit && m_export_file_edit->text().trimmed().isEmpty()) {
            m_export_file_edit->setText(QDir::home().filePath(defaultExportFilename(m_vaults[row])));
        }
    });

    if (m_wallet_model) {
        m_wallet_label->setText(tr("Active wallet: %1").arg(m_wallet_model->getWalletName()));
    } else {
        m_wallet_label->setText(tr("Active wallet: none"));
    }

    refreshVaults();
}

QString ImportExportVaultDialog::defaultExportFilename(const WalletModel::VaultInfo& vault) const
{
    QString label = vault.label.trimmed();
    if (label.isEmpty()) {
        label = QStringLiteral("vault");
    }

    QString safe_label;
    safe_label.reserve(label.size());

    for (const QChar ch : label) {
        if (ch.isLetterOrNumber() || ch == QLatin1Char('-') || ch == QLatin1Char('_')) {
            safe_label.append(ch);
        } else {
            safe_label.append(QLatin1Char('_'));
        }
    }

    QString program_part = vault.program;
    if (program_part.size() > 16) {
        program_part = program_part.left(16);
    }

    return safe_label + QStringLiteral("-") + program_part + QStringLiteral(".kv5vaultbackup.json");
}

int ImportExportVaultDialog::selectedVaultRow() const
{
    if (!m_vaults_table) {
        return -1;
    }

    return m_vaults_table->currentRow();
}

QString ImportExportVaultDialog::selectedVaultAddress() const
{
    const int row = selectedVaultRow();
    if (row < 0 || row >= m_vaults.size()) {
        return {};
    }

    return m_vaults[row].address;
}

void ImportExportVaultDialog::refreshVaults()
{
    if (!m_wallet_model) {
        m_vaults.clear();
        m_vaults_table->setRowCount(0);
        m_status_text->setPlainText(tr("Wallet model is not available."));
        m_export_button->setEnabled(false);
        m_import_button->setEnabled(false);
        return;
    }

    m_vaults = m_wallet_model->listVaults();

    m_vaults_table->blockSignals(true);
    m_vaults_table->clearSelection();
    m_vaults_table->setRowCount(0);

    for (int row = 0; row < m_vaults.size(); ++row) {
        const WalletModel::VaultInfo& vault = m_vaults[row];

        int local_signers{0};
        for (const WalletModel::VaultSignerInfo& signer : vault.signers) {
            if (signer.has_local_private_key) {
                ++local_signers;
            }
        }

        QString short_program = vault.program;
        if (short_program.size() > 24) {
            short_program = short_program.left(12) + QStringLiteral("...") + short_program.right(12);
        }

        m_vaults_table->insertRow(row);
        m_vaults_table->setItem(row, 0, new QTableWidgetItem(vault.label.isEmpty() ? tr("(unlabeled)") : vault.label));
        m_vaults_table->setItem(row, 1, new QTableWidgetItem(FormatKv5AmountForVaultBackupDialog(vault.balance)));
        m_vaults_table->setItem(row, 2, new QTableWidgetItem(tr("%1-of-%2").arg(vault.required).arg(vault.total)));
        m_vaults_table->setItem(row, 3, new QTableWidgetItem(tr("%1 / %2").arg(local_signers).arg(vault.total)));
        m_vaults_table->setItem(row, 4, new QTableWidgetItem(vault.type));
        m_vaults_table->setItem(row, 5, new QTableWidgetItem(vault.address));
        m_vaults_table->setItem(row, 6, new QTableWidgetItem(short_program));

        for (int col = 0; col < m_vaults_table->columnCount(); ++col) {
            QTableWidgetItem* item = m_vaults_table->item(row, col);
            if (item) {
                item->setFlags(item->flags() & ~Qt::ItemIsEditable);
                item->setToolTip(tr(
                    "Label: %1\n"
                    "Address: %2\n"
                    "Program: %3\n"
                    "Policy: %4-of-%5\n"
                    "Balance: %6 KV5\n"
                    "Local signers: %7 / %8")
                    .arg(vault.label.isEmpty() ? tr("(unlabeled)") : vault.label)
                    .arg(vault.address)
                    .arg(vault.program)
                    .arg(vault.required)
                    .arg(vault.total)
                    .arg(FormatKv5AmountForVaultBackupDialog(vault.balance))
                    .arg(local_signers)
                    .arg(vault.total));
            }
        }
    }

    m_vaults_table->blockSignals(false);

    if (!m_vaults.empty()) {
        m_vaults_table->setCurrentCell(0, 0);
        m_export_file_edit->setText(QDir::home().filePath(defaultExportFilename(m_vaults[0])));
    } else {
        m_export_file_edit->clear();
    }

    m_export_button->setEnabled(!m_vaults.empty());
    m_import_button->setEnabled(true);

    m_status_text->setPlainText(
        tr("Loaded %1 vault(s). Select a vault to export, or choose a backup file to import.")
            .arg(m_vaults.size()));
}

void ImportExportVaultDialog::browseExportFile()
{
    QString start_path = m_export_file_edit ? m_export_file_edit->text().trimmed() : QString{};
    if (start_path.isEmpty()) {
        const int row = selectedVaultRow();
        if (row >= 0 && row < m_vaults.size()) {
            start_path = QDir::home().filePath(defaultExportFilename(m_vaults[row]));
        } else {
            start_path = QDir::home().filePath(QStringLiteral("vault.kv5vaultbackup.json"));
        }
    }

    QString filename = QFileDialog::getSaveFileName(
        this,
        tr("Export Kvanta5 Vault Backup"),
        start_path,
        tr("Kvanta5 Vault Backup Files (*.kv5vaultbackup.json);;JSON Files (*.json);;All Files (*)"));

    if (filename.isEmpty()) {
        return;
    }

    if (!filename.endsWith(QStringLiteral(".kv5vaultbackup.json"), Qt::CaseInsensitive)) {
        filename += QStringLiteral(".kv5vaultbackup.json");
    }

    m_export_file_edit->setText(filename);
}

void ImportExportVaultDialog::exportSelectedVault()
{
    if (!m_wallet_model) {
        QMessageBox::critical(this, tr("Export Vault Backup"), tr("Wallet model is not available."));
        return;
    }

    const QString address = selectedVaultAddress();
    if (address.isEmpty()) {
        QMessageBox::critical(this, tr("Export Vault Backup"), tr("Select a vault to export."));
        return;
    }

    const QString filename = m_export_file_edit ? m_export_file_edit->text().trimmed() : QString{};
    if (filename.isEmpty()) {
        QMessageBox::critical(this, tr("Export Vault Backup"), tr("Choose a destination backup filename."));
        return;
    }

    QString error;
    const QString result = m_wallet_model->dumpKvanta5VaultBackup(address, filename, error);

    if (!error.isEmpty()) {
        QMessageBox::critical(this, tr("Export Vault Backup Failed"), error);
        m_status_text->setPlainText(error);
        return;
    }

    m_status_text->setPlainText(
        tr("Vault backup exported successfully.\n\n"
           "File:\n%1\n\n"
           "%2")
            .arg(filename)
            .arg(result));

    refreshVaults();
}

void ImportExportVaultDialog::browseImportFile()
{
    const QString filename = QFileDialog::getOpenFileName(
        this,
        tr("Import Kvanta5 Vault Backup"),
        QDir::homePath(),
        tr("Kvanta5 Vault Backup Files (*.kv5vaultbackup.json);;JSON Files (*.json);;All Files (*)"));

    if (!filename.isEmpty()) {
        m_import_file_edit->setText(filename);
    }
}

void ImportExportVaultDialog::importVaultBackup()
{
    if (!m_wallet_model) {
        QMessageBox::critical(this, tr("Import Vault Backup"), tr("Wallet model is not available."));
        return;
    }

    const QString filename = m_import_file_edit ? m_import_file_edit->text().trimmed() : QString{};
    const QString label = m_import_label_edit ? m_import_label_edit->text().trimmed() : QString{};

    if (filename.isEmpty()) {
        QMessageBox::critical(this, tr("Import Vault Backup"), tr("Choose a vault backup file to import."));
        return;
    }

    QString error;
    const QString result = m_wallet_model->importKvanta5VaultBackup(filename, label, error);

    if (!error.isEmpty()) {
        QMessageBox::critical(this, tr("Import Vault Backup Failed"), error);
        m_status_text->setPlainText(error);
        return;
    }

    m_status_text->setPlainText(
        tr("Vault backup imported successfully.\n\n"
           "File:\n%1\n\n"
           "%2")
            .arg(filename)
            .arg(result));

    refreshVaults();
}
