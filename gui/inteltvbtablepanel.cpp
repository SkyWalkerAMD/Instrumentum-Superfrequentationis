// SPDX-License-Identifier: GPL-2.0-only
#include "inteltvbtablepanel.h"
#include <QApplication>
#include <QClipboard>
#include <QComboBox>
#include <QFutureWatcher>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QTableWidget>
#include <QVBoxLayout>
#include <QtConcurrent>
#include <cerrno>
#include <cstring>
#include <utility>

using namespace octool::core;
IntelTvbTablePanel::IntelTvbTablePanel(std::shared_ptr<HardwareAccess> access, QWidget *parent, unsigned cpu)
    : QWidget(parent), access_(std::move(access)), cancelled_(std::make_shared<std::atomic<bool>>(false)) {
    setObjectName("intelTvbTable"); auto *layout = new QVBoxLayout(this);
    auto *scope = new QLabel("Intel Thermal Velocity Boost thresholds - Raptor Lake-S\n"
        "Read temperature thresholds and ratio reductions by active P-core count. "
        "Choose one entry or query all eight candidates. Firmware may reject entries that are unavailable.", this);
    scope->setWordWrap(true); layout->addWidget(scope);
    auto *buttons = new QHBoxLayout;
    buttons->addWidget(new QLabel("Logical CPU", this));
    cpu_ = new QLineEdit(QString::number(cpu), this); cpu_->setObjectName("tvbTableCpu"); buttons->addWidget(cpu_);
    buttons->addWidget(new QLabel("Active P-cores", this));
    entry_ = new QComboBox(this); entry_->setObjectName("tvbTableEntry"); entry_->addItem("All 8 candidates", 0);
    for (int n = 1; n <= 8; ++n) entry_->addItem(QString::number(n), n);
    buttons->addWidget(entry_);
    auto *read = new QPushButton("Read thresholds", this); read->setObjectName("tvbTableRead"); buttons->addWidget(read);
    auto *copy = new QPushButton("Copy table", this); copy->setObjectName("tvbTableCopy"); buttons->addWidget(copy);
    layout->addLayout(buttons);
    table_ = new QTableWidget(0, 7, this); table_->setObjectName("tvbThresholdTable");
    table_->setHorizontalHeaderLabels({"Active P-cores", "Temp A (°C)", "Down bins A", "Temp B (°C)", "Down bins B", "Raw", "Result"});
    table_->verticalHeader()->hide(); table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table_->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    table_->horizontalHeader()->setStretchLastSection(true); layout->addWidget(table_, 1);
    auto *edit = new QHBoxLayout;
    prepare_ = new QPushButton("Prepare selected entry", this); prepare_->setObjectName("tvbTablePrepare"); edit->addWidget(prepare_);
    field_ = new QComboBox(this); field_->setObjectName("tvbTableField");
    field_->addItem("Temperature A (°C)", int(IntelTvbTableField::TemperatureA));
    field_->addItem("Down bins A", int(IntelTvbTableField::DownBinsA));
    field_->addItem("Temperature B (°C)", int(IntelTvbTableField::TemperatureB));
    field_->addItem("Down bins B", int(IntelTvbTableField::DownBinsB)); edit->addWidget(field_, 1);
    value_ = new QLineEdit(this); value_->setObjectName("tvbTableValue"); value_->setMaximumWidth(100); edit->addWidget(value_);
    apply_ = new QPushButton("Apply field…", this); apply_->setObjectName("tvbTableApply"); edit->addWidget(apply_);
    layout->addLayout(edit);
    auto *note = new QLabel("Each row describes an active P-core count entry, not a physical core or measured frequency. "
        "Select one entry and prepare it to edit a field. Editing profile: temperatures 0–100°C; down bins 0–10. "
        "Readback verifies configuration, not TVB activity. Sequential reads are not an atomic snapshot.", this);
    note->setWordWrap(true); layout->addWidget(note);
    status_ = new QLabel("Ready. Read the thresholds to populate the table.", this);
    status_->setObjectName("tvbTableStatus"); status_->setWordWrap(true); status_->setTextFormat(Qt::PlainText); layout->addWidget(status_);
    invalidate(); status_->setText("Ready. Read thresholds, or select one entry and prepare it for editing.");
    connect(cpu_, &QLineEdit::textChanged, this, [this] { invalidate(); });
    connect(entry_, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this] { invalidate(); });
    connect(read, &QPushButton::clicked, this, [this] { refresh(); });
    connect(prepare_, &QPushButton::clicked, this, [this] { prepare(); });
    connect(field_, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this] { selectCurrent(); });
    connect(apply_, &QPushButton::clicked, this, [this] { apply(); });
    connect(copy, &QPushButton::clicked, this, [this] {
        QString text = status_->text()+"\n";
        for (int col = 0; col < table_->columnCount(); ++col) text += (col ? "\t" : "")+table_->horizontalHeaderItem(col)->text();
        text += "\n";
        for (int row = 0; row < table_->rowCount(); ++row) {
            for (int col = 0; col < table_->columnCount(); ++col) text += (col ? "\t" : "")+table_->item(row,col)->text();
            text += "\n";
        }
        QApplication::clipboard()->setText(text);
    });
}
IntelTvbTablePanel::~IntelTvbTablePanel() { cancelled_->store(true); }
void IntelTvbTablePanel::invalidate() {
    ++revision_; snapshot_ = {}; table_->setRowCount(0); value_->clear(); value_->setEnabled(false); apply_->setEnabled(false);
    prepare_->setEnabled(entry_->currentIndex() > 0);
    status_->setText("Selection changed. Read the thresholds or prepare the selected entry again.");
}
void IntelTvbTablePanel::selectCurrent() {
    if (!snapshot_.valid || snapshot_.error || field_->currentIndex() < 0) { value_->clear(); return; }
    const auto fields = decodeIntelTvbThresholds(snapshot_.value.data);
    const unsigned values[] = {fields.temperatureA,fields.downBinsA,fields.temperatureB,fields.downBinsB};
    value_->setText(QString::number(values[field_->currentIndex()]));
}
void IntelTvbTablePanel::presentPrepared(const IntelTvbTableEditSnapshot &snapshot) {
    invalidate(); snapshot_ = snapshot;
    if (snapshot.error || !snapshot.valid) {
        status_->setText(QString("Preparation failed (%1). Control status: %2; entry status: %3. Read again.")
            .arg(snapshot.error).arg(snapshot.configuration.response.firmwareStatus).arg(snapshot.value.firmwareStatus)); return;
    }
    IntelTvbTableSnapshot table; table.cpu = snapshot.configuration.cpu; table.identity = snapshot.configuration.identity;
    table.selectedActiveCores = snapshot.activeCores; table.scanCompleted = true;
    IntelTvbTableRow row; row.activeCores = snapshot.activeCores; row.response = snapshot.value; table.rows.push_back(row);
    present(table); selectCurrent();
    const bool editable = intelTvbTableEditable(snapshot);
    value_->setEnabled(editable); apply_->setEnabled(editable);
    status_->setText(QString("CPU %1 · active P-core count %2 · %3")
        .arg(table.cpu).arg(snapshot.activeCores).arg(editable
            ? "Prepared. Choose one field; the other three bytes will be preserved."
            : !intelTvbEditable(snapshot.configuration) ? "OC locked - read only."
            : "Values outside the supported editing profile - read only."));
}
void IntelTvbTablePanel::prepare() {
    quint64 cpu = 0; invalidate();
    if (!parseNumber(cpu_->text(),10,1048575,cpu) || entry_->currentIndex() <= 0) {
        status_->setText("Enter a valid CPU and select one active P-core count (1–8)."); return;
    }
    const unsigned entry = unsigned(entry_->currentData().toUInt()), revision = revision_;
    setEnabled(false); status_->setText("Reading the entry, TVB configuration and OC lock…");
    auto *watcher = new QFutureWatcher<IntelTvbTableEditSnapshot>(this);
    connect(watcher, &QFutureWatcher<IntelTvbTableEditSnapshot>::finished, this, [this,watcher,revision] {
        if (revision == revision_) presentPrepared(watcher->result());
        else { invalidate(); status_->setText("Selection changed during preparation; result discarded. Prepare again."); }
        setEnabled(true); watcher->deleteLater();
    });
    const auto access = access_; const auto cancelled = cancelled_;
    watcher->setFuture(QtConcurrent::run([access,cancelled,cpu,entry] {
        IntelTvbTableEditSnapshot out; out.configuration.cpu = unsigned(cpu); out.activeCores = entry;
        const int error = access->transaction([&](HardwareSession &s) {
            out = readIntelTvbTableEdit(s,unsigned(cpu),entry); return out.error;
        },5000,cancelled.get());
        if (error) out.error = error;
        return out;
    }));
}
void IntelTvbTablePanel::apply() {
    if (!intelTvbTableEditable(snapshot_) || field_->currentIndex() < 0) return;
    const auto field = IntelTvbTableField(field_->currentData().toInt()); quint64 value = 0;
    const unsigned maximum = field == IntelTvbTableField::TemperatureA || field == IntelTvbTableField::TemperatureB ? 100 : 10;
    if (!parseNumber(value_->text(),10,maximum,value)) {
        status_->setText(QString("Enter an integer from 0 to %1 for the selected field.").arg(maximum)); return;
    }
    const auto old = snapshot_; const auto preparedRevision = revision_;
    std::uint32_t expected = 0;
    if (encodeIntelTvbTable(field,unsigned(value),old.value.data,expected)) return;
    if (QMessageBox::question(this,"Apply TVB threshold field",
        QString("CPU %1 · active P-core count %2\n%3 → %4\nEntry: %5 → %6\n"
            "Apply this field and verify the complete entry? This changes temperature-dependent frequency behavior.")
        .arg(old.configuration.cpu).arg(old.activeCores).arg(field_->currentText()).arg(value)
        .arg(hexValue(old.value.data,4)).arg(hexValue(expected,4)),
        QMessageBox::Yes | QMessageBox::Cancel,QMessageBox::Cancel) != QMessageBox::Yes) return;
    if (preparedRevision != revision_) { status_->setText("Selection changed. Prepare the entry again."); return; }
    invalidate(); setEnabled(false); status_->setText("Checking current settings, applying and verifying the complete entry…");
    const auto revision = revision_;
    auto *watcher = new QFutureWatcher<IntelTvbTableUpdate>(this);
    connect(watcher,&QFutureWatcher<IntelTvbTableUpdate>::finished,this,[this,watcher,revision,old] {
        const auto out = watcher->result(); setEnabled(true);
        if (revision != revision_) {
            invalidate(); status_->setText(QString("Selection changed during the operation for CPU %1 / count %2; displayed result discarded. ")
                .arg(old.configuration.cpu).arg(old.activeCores) + (out.writeAttempted
                ? "A settings write was attempted. Prepare the original entry again." : "No settings write was attempted."));
        } else if (!out.error && out.verified) {
            presentPrepared(out.readback);
            status_->setText(status_->text() + (out.unchanged ? " Requested value already present; no settings write performed."
                : " Complete entry readback verified; other fields preserved."));
        } else {
            invalidate(); status_->setText(QString("Operation failed (%1). ").arg(out.error ? out.error : -EIO) + (out.writeAttempted
                ? "A settings write was attempted; the final configuration is not verified. No retry or rollback was performed."
                : "No settings write was attempted. Prepare again."));
        }
        watcher->deleteLater();
    });
    const auto access = access_; const auto cancelled = cancelled_;
    watcher->setFuture(QtConcurrent::run([access,cancelled,old,field,value] {
        IntelTvbTableUpdate out;
        const int error = access->transaction([&](HardwareSession &s) {
            out = applyIntelTvbTable(s,old,field,unsigned(value)); return out.error;
        },5000,cancelled.get());
        if (error) out.error = error;
        return out;
    }));
}
void IntelTvbTablePanel::present(const IntelTvbTableSnapshot &snapshot) {
    table_->setRowCount(0); unsigned validCount = 0;
    for (const auto &row : snapshot.rows) {
        const bool valid = intelTvbTableRowValid(row);
        const auto fields = decodeIntelTvbThresholds(row.response.data);
        const QString unavailable = "—";
        const QString result = valid ? "Read" : row.response.completed
            ? QString("Firmware status %1").arg(row.response.firmwareStatus)
            : QString("Read failed (%1)").arg(row.response.error);
        const QStringList cells{QString::number(row.activeCores),
            valid ? QString::number(fields.temperatureA) : unavailable,
            valid ? QString::number(fields.downBinsA) : unavailable,
            valid ? QString::number(fields.temperatureB) : unavailable,
            valid ? QString::number(fields.downBinsB) : unavailable,
            valid ? hexValue(row.response.data,4) : unavailable, result};
        const int n = table_->rowCount(); table_->insertRow(n);
        for (int col = 0; col < cells.size(); ++col) table_->setItem(n,col,new QTableWidgetItem(cells[col]));
        if (valid) ++validCount;
    }
    const unsigned requested = snapshot.selectedActiveCores ? 1 : 8;
    status_->setText(QString("CPU %1 · %2 of %3 requested entries read. %4%5")
        .arg(snapshot.cpu).arg(validCount).arg(requested)
        .arg(snapshot.scanCompleted ? "Scan completed. " : "Scan stopped; unqueried entries are omitted. ")
        .arg(snapshot.error ? QString("Error %1: %2. No settings changed.").arg(snapshot.error)
            .arg(QString::fromLocal8Bit(std::strerror(-snapshot.error))) : "No settings changed."));
}
void IntelTvbTablePanel::refresh() {
    quint64 cpu = 0; invalidate();
    if (!parseNumber(cpu_->text(),10,1048575,cpu) || entry_->currentIndex() < 0) {
        status_->setText("Invalid CPU or active-core selection."); return;
    }
    const unsigned entry = unsigned(entry_->currentData().toUInt()), revision = revision_;
    setEnabled(false); status_->setText("Reading TVB thresholds…");
    auto *watcher = new QFutureWatcher<IntelTvbTableSnapshot>(this);
    connect(watcher, &QFutureWatcher<IntelTvbTableSnapshot>::finished, this, [this,watcher,revision] {
        if (revision == revision_) present(watcher->result());
        else { invalidate(); status_->setText("Selection changed during the read; result discarded. Read again."); }
        setEnabled(true); watcher->deleteLater();
    });
    const auto access = access_; const auto cancelled = cancelled_;
    watcher->setFuture(QtConcurrent::run([access,cancelled,cpu,entry] {
        IntelTvbTableSnapshot out;
        const int error = access->transaction([&](HardwareSession &s) {
            out = readIntelTvbTable(s,unsigned(cpu),entry); return out.error;
        },5000,cancelled.get());
        if (error) out.error = error;
        return out;
    }));
}
