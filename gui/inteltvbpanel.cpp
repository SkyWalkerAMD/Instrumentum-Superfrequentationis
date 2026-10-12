// SPDX-License-Identifier: GPL-2.0-only
#include "inteltvbpanel.h"
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
namespace {
QString failure(int error) { return QString("Failed (%1): %2.").arg(error).arg(QString::fromLocal8Bit(std::strerror(-error))); }
QString state(bool enabled) { return enabled ? "Enabled" : "Disabled"; }
}
IntelTvbPanel::IntelTvbPanel(std::shared_ptr<HardwareAccess> access, QWidget *parent, unsigned cpu)
    : QWidget(parent), access_(std::move(access)), cancelled_(std::make_shared<std::atomic<bool>>(false)) {
    setObjectName("intelTvb"); auto *layout = new QVBoxLayout(this);
    auto *scope = new QLabel("Intel Thermal Velocity Boost - Raptor Lake-S\n"
        "Ratio clipping allows frequency reduction as temperature rises. Voltage optimization adjusts voltage with temperature. "
        "These are shared processor settings accessed through the selected logical CPU. Availability depends on the processor and firmware.", this);
    scope->setWordWrap(true); layout->addWidget(scope);
    auto *buttons = new QHBoxLayout;
    buttons->addWidget(new QLabel("Logical CPU", this));
    cpu_ = new QLineEdit(QString::number(cpu), this); cpu_->setObjectName("tvbCpu"); buttons->addWidget(cpu_);
    auto *read = new QPushButton("Read configuration", this); read->setObjectName("tvbRead"); buttons->addWidget(read);
    auto *copy = new QPushButton("Copy snapshot", this); copy->setObjectName("tvbCopy"); buttons->addWidget(copy);
    layout->addLayout(buttons);
    table_ = new QTableWidget(0, 3, this); table_->setObjectName("tvbTable");
    table_->setHorizontalHeaderLabels({"Setting", "Configured state", "Access"});
    table_->verticalHeader()->hide(); table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table_->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    table_->horizontalHeader()->setStretchLastSection(true); layout->addWidget(table_, 1);
    raw_ = new QLabel(this); raw_->setObjectName("tvbRaw"); raw_->setWordWrap(true); layout->addWidget(raw_);
    auto *edit = new QHBoxLayout;
    edit->addWidget(new QLabel("Setting", this));
    field_ = new QComboBox(this); field_->setObjectName("tvbField");
    field_->addItem("Ratio clipping", int(IntelTvbField::RatioClipping));
    field_->addItem("Voltage optimization", int(IntelTvbField::VoltageOptimization)); edit->addWidget(field_, 1);
    value_ = new QComboBox(this); value_->setObjectName("tvbValue");
    value_->addItem("Disabled", 0); value_->addItem("Enabled", 1); edit->addWidget(value_);
    apply_ = new QPushButton("Apply setting…", this); apply_->setObjectName("tvbApply"); edit->addWidget(apply_);
    layout->addLayout(edit);
    auto *note = new QLabel("Configuration readback does not measure TVB activity or performance. "
        "Use the TVB thresholds page to read the temperature and ratio reduction table.", this);
    note->setWordWrap(true); layout->addWidget(note);
    status_ = new QLabel("Ready. Read the configuration before making a change.", this);
    status_->setObjectName("tvbStatus"); status_->setWordWrap(true); status_->setTextFormat(Qt::PlainText); layout->addWidget(status_);
    invalidate();
    connect(cpu_, &QLineEdit::textChanged, this, [this] { invalidate(); status_->setText("CPU changed. Read the configuration again."); });
    connect(field_, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this] { selectCurrent(); });
    connect(read, &QPushButton::clicked, this, [this] { refresh(); });
    connect(apply_, &QPushButton::clicked, this, [this] { apply(); });
    connect(copy, &QPushButton::clicked, this, [this] {
        QString text = status_->text()+"\n"+raw_->text()+"\nSetting\tConfigured state\tAccess\n";
        for (int row = 0; row < table_->rowCount(); ++row) {
            for (int col = 0; col < table_->columnCount(); ++col) text += (col ? "\t" : "") + table_->item(row, col)->text();
            text += "\n";
        }
        QApplication::clipboard()->setText(text);
    });
}
IntelTvbPanel::~IntelTvbPanel() { cancelled_->store(true); }
void IntelTvbPanel::invalidate() {
    ++revision_;
    snapshot_ = {}; table_->setRowCount(0); raw_->clear(); value_->setCurrentIndex(-1); value_->setEnabled(false); apply_->setEnabled(false);
}
void IntelTvbPanel::selectCurrent() {
    if (!snapshot_.valid || snapshot_.error) { value_->setCurrentIndex(-1); return; }
    value_->setCurrentIndex((field_->currentData().toInt() == int(IntelTvbField::RatioClipping)
        ? intelTvbRatioClipping(snapshot_.response.data) : intelTvbVoltageOptimization(snapshot_.response.data)) ? 1 : 0);
}
void IntelTvbPanel::present(const IntelTvbSnapshot &snapshot) {
    invalidate(); snapshot_ = snapshot;
    if (snapshot.error || !snapshot.valid) {
        status_->setText(failure(snapshot.error ? snapshot.error : -ENODATA) +
            QString(" Firmware status: %1. No settings displayed.").arg(snapshot.response.firmwareStatus)); return;
    }
    const bool editable = intelTvbEditable(snapshot);
    const bool values[] = {intelTvbRatioClipping(snapshot.response.data), intelTvbVoltageOptimization(snapshot.response.data)};
    for (int row = 0; row < 2; ++row) {
        table_->insertRow(row);
        table_->setItem(row, 0, new QTableWidgetItem(row ? "Voltage optimization" : "Ratio clipping"));
        table_->setItem(row, 1, new QTableWidgetItem(state(values[row])));
        table_->setItem(row, 2, new QTableWidgetItem(editable ? "Available for editing" : "OC locked - read only"));
    }
    selectCurrent(); value_->setEnabled(editable); apply_->setEnabled(editable);
    raw_->setText(QString("Configuration: %1 · OC lock: %2").arg(hexValue(snapshot.response.data, 4)).arg(editable ? "clear" : "set"));
    status_->setText(QString("CPU %1 · TVB configuration read. %2").arg(snapshot.cpu).arg(editable
        ? "Choose one setting to change; other settings are preserved." : "Firmware overclocking lock prevents changes."));
}
void IntelTvbPanel::refresh() {
    quint64 cpu = 0; invalidate();
    if (!parseNumber(cpu_->text(), 10, 1048575, cpu)) { status_->setText("Invalid logical CPU."); return; }
    setEnabled(false); status_->setText("Reading TVB configuration…");
    const auto revision = revision_;
    auto *watcher = new QFutureWatcher<IntelTvbSnapshot>(this);
    connect(watcher, &QFutureWatcher<IntelTvbSnapshot>::finished, this, [this, watcher, revision] {
        if (revision == revision_) present(watcher->result());
        else { invalidate(); status_->setText("CPU changed during the read; result discarded. Read again."); }
        setEnabled(true); watcher->deleteLater();
    });
    const auto access = access_; const auto cancelled = cancelled_;
    watcher->setFuture(QtConcurrent::run([access, cancelled, cpu] {
        IntelTvbSnapshot out;
        const int error = access->transaction([&](HardwareSession &s) { out = readIntelTvb(s, unsigned(cpu)); return out.error; }, 5000, cancelled.get());
        if (error) out.error = error;
        return out;
    }));
}
void IntelTvbPanel::apply() {
    if (!intelTvbEditable(snapshot_) || value_->currentIndex() < 0 || field_->currentIndex() < 0) return;
    const auto old = snapshot_;
    const auto preparedRevision = revision_;
    const auto field = IntelTvbField(field_->currentData().toInt());
    const auto enabled = unsigned(value_->currentData().toInt());
    const bool before = field == IntelTvbField::RatioClipping ? intelTvbRatioClipping(old.response.data) : intelTvbVoltageOptimization(old.response.data);
    if (QMessageBox::question(this, "Apply TVB configuration",
        QString("CPU %1 · shared processor configuration\n%2: %3 → %4.\n"
            "This changes temperature-dependent frequency or voltage behavior and can affect stability. "
            "The selected setting will be applied and read back.")
        .arg(old.cpu).arg(field_->currentText()).arg(state(before)).arg(state(enabled != 0)),
        QMessageBox::Yes | QMessageBox::Cancel, QMessageBox::Cancel) != QMessageBox::Yes) return;
    if (preparedRevision != revision_) { status_->setText("CPU changed. Read the configuration again."); return; }
    invalidate(); setEnabled(false); status_->setText("Checking current settings, applying and verifying readback…");
    const auto revision = revision_;
    auto *watcher = new QFutureWatcher<IntelTvbUpdate>(this);
    connect(watcher, &QFutureWatcher<IntelTvbUpdate>::finished, this, [this, watcher, revision, old] {
        const auto out = watcher->result(); setEnabled(true);
        if (revision != revision_) {
            invalidate(); status_->setText(QString("CPU changed during the operation for CPU %1; displayed result discarded. ")
                .arg(old.cpu) + (out.writeAttempted ? "A settings write was attempted. Read the original CPU again." : "No settings write was attempted."));
        } else if (!out.error && out.verified) {
            present(out.readback);
            status_->setText(status_->text() + (out.unchanged ? " Requested state already present; no settings write performed."
                : " Configuration readback verified; other bits preserved."));
        } else {
            invalidate(); status_->setText(failure(out.error ? out.error : -EIO) + (out.writeAttempted
                ? " A settings write was attempted; the final configuration is not verified. No retry or rollback was performed."
                : " No settings write was attempted."));
        }
        watcher->deleteLater();
    });
    const auto access = access_; const auto cancelled = cancelled_;
    watcher->setFuture(QtConcurrent::run([access, cancelled, old, field, enabled] {
        IntelTvbUpdate out;
        const int error = access->transaction([&](HardwareSession &s) { out = applyIntelTvb(s, old, field, enabled); return out.error; }, 5000, cancelled.get());
        if (error) out.error = error;
        return out;
    }));
}
