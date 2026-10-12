// SPDX-License-Identifier: GPL-2.0-only
#include "intelenergypanel.h"
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
bool settingEnabled(IntelEnergyField field, std::uint64_t data) {
    switch (field) {
    case IntelEnergyField::EfficientTurbo: return intelEfficientTurbo(data);
    case IntelEnergyField::RaceToHalt: return intelRaceToHalt(data);
    }
    return false;
}
}
IntelEnergyPanel::IntelEnergyPanel(std::shared_ptr<HardwareAccess> access, QWidget *parent, unsigned cpu)
    : QWidget(parent), access_(std::move(access)), cancelled_(std::make_shared<std::atomic<bool>>(false)) {
    setObjectName("intelEnergy"); auto *layout = new QVBoxLayout(this);
    auto *scope = new QLabel("Intel energy optimization - Raptor Lake-S\n"
        "Energy Efficient Turbo allows energy-aware performance selection. Race to Halt favors completing work "
        "sooner to enter idle states. These controls are accessed through the selected logical CPU; "
        "availability and behavior depend on firmware and OS power policy.", this);
    scope->setWordWrap(true); layout->addWidget(scope);
    auto *buttons = new QHBoxLayout;
    buttons->addWidget(new QLabel("Logical CPU", this));
    cpu_ = new QLineEdit(QString::number(cpu), this); cpu_->setObjectName("energyCpu"); buttons->addWidget(cpu_);
    auto *read = new QPushButton("Read configuration", this); read->setObjectName("energyRead"); buttons->addWidget(read);
    auto *copy = new QPushButton("Copy snapshot", this); copy->setObjectName("energyCopy"); buttons->addWidget(copy);
    layout->addLayout(buttons);
    table_ = new QTableWidget(0, 3, this); table_->setObjectName("energyTable");
    table_->setHorizontalHeaderLabels({"Setting", "Configured state", "Access"});
    table_->verticalHeader()->hide(); table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table_->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    table_->horizontalHeader()->setStretchLastSection(true); layout->addWidget(table_, 1);
    raw_ = new QLabel(this); raw_->setObjectName("energyRaw"); raw_->setWordWrap(true); layout->addWidget(raw_);
    auto *edit = new QHBoxLayout;
    edit->addWidget(new QLabel("Setting", this));
    field_ = new QComboBox(this); field_->setObjectName("energyField");
    field_->addItem("Energy Efficient Turbo", int(IntelEnergyField::EfficientTurbo));
    field_->addItem("Race to Halt", int(IntelEnergyField::RaceToHalt));
    edit->addWidget(field_, 1);
    value_ = new QComboBox(this); value_->setObjectName("energyValue");
    value_->addItem("Disabled", 0); value_->addItem("Enabled", 1); edit->addWidget(value_);
    apply_ = new QPushButton("Apply setting…", this); apply_->setObjectName("energyApply"); edit->addWidget(apply_);
    layout->addLayout(edit);
    auto *note = new QLabel("Readback verifies the selected CPU access only. It does not measure energy savings or performance, "
        "or confirm the configuration of other cores.", this);
    note->setWordWrap(true); layout->addWidget(note);
    status_ = new QLabel("Ready. Read the configuration before making a change.", this);
    status_->setObjectName("energyStatus"); status_->setWordWrap(true); status_->setTextFormat(Qt::PlainText); layout->addWidget(status_);
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
IntelEnergyPanel::~IntelEnergyPanel() { cancelled_->store(true); }
void IntelEnergyPanel::invalidate() {
    ++revision_;
    snapshot_ = {}; table_->setRowCount(0); raw_->clear(); value_->setCurrentIndex(-1); value_->setEnabled(false); apply_->setEnabled(false);
}
void IntelEnergyPanel::selectCurrent() {
    if (!snapshot_.valid || snapshot_.error) { value_->setCurrentIndex(-1); return; }
    value_->setCurrentIndex(settingEnabled(IntelEnergyField(field_->currentData().toInt()), snapshot_.raw) ? 1 : 0);
}
void IntelEnergyPanel::present(const IntelEnergySnapshot &snapshot) {
    invalidate(); snapshot_ = snapshot;
    if (snapshot.error || !snapshot.valid) {
        status_->setText(failure(snapshot.error ? snapshot.error : -ENODATA) + " No settings displayed."); return;
    }
    const bool editable = intelEnergyEditable(snapshot);
    for (int row = 0; row < field_->count(); ++row) {
        table_->insertRow(row);
        table_->setItem(row, 0, new QTableWidgetItem(field_->itemText(row)));
        table_->setItem(row, 1, new QTableWidgetItem(state(settingEnabled(IntelEnergyField(field_->itemData(row).toInt()), snapshot.raw))));
        table_->setItem(row, 2, new QTableWidgetItem(editable ? "Available for editing" : "Read only"));
    }
    selectCurrent(); value_->setEnabled(editable); apply_->setEnabled(editable);
    raw_->setText(QString("MSR 0x1FC: %1").arg(hexValue(snapshot.raw, 8)));
    status_->setText(QString("CPU %1 · energy configuration read. Choose one setting to change; other settings are preserved.").arg(snapshot.cpu));
}
void IntelEnergyPanel::refresh() {
    quint64 cpu = 0; invalidate();
    if (!parseNumber(cpu_->text(), 10, 1048575, cpu)) { status_->setText("Invalid logical CPU."); return; }
    setEnabled(false); status_->setText("Reading energy configuration…");
    const auto revision = revision_;
    auto *watcher = new QFutureWatcher<IntelEnergySnapshot>(this);
    connect(watcher, &QFutureWatcher<IntelEnergySnapshot>::finished, this, [this, watcher, revision] {
        if (revision == revision_) present(watcher->result());
        else { invalidate(); status_->setText("CPU changed during the read; result discarded. Read again."); }
        setEnabled(true); watcher->deleteLater();
    });
    const auto access = access_; const auto cancelled = cancelled_;
    watcher->setFuture(QtConcurrent::run([access, cancelled, cpu] {
        IntelEnergySnapshot out;
        const int error = access->transaction([&](HardwareSession &s) { out = readIntelEnergy(s, unsigned(cpu)); return out.error; }, 5000, cancelled.get());
        if (error) out.error = error;
        return out;
    }));
}
void IntelEnergyPanel::apply() {
    if (!intelEnergyEditable(snapshot_) || value_->currentIndex() < 0 || field_->currentIndex() < 0) return;
    const auto old = snapshot_;
    const auto preparedRevision = revision_;
    const auto field = IntelEnergyField(field_->currentData().toInt());
    const auto enabled = unsigned(value_->currentData().toInt());
    const bool before = settingEnabled(field, old.raw);
    if (QMessageBox::question(this, "Apply processor configuration",
        QString("CPU %1 · energy configuration\n%2: %3 → %4.\n"
            "This can change performance and power use. The selected setting will be applied and read back.")
        .arg(old.cpu).arg(field_->currentText()).arg(state(before)).arg(state(enabled != 0)),
        QMessageBox::Yes | QMessageBox::Cancel, QMessageBox::Cancel) != QMessageBox::Yes) return;
    if (preparedRevision != revision_) { status_->setText("CPU changed. Read the configuration again."); return; }
    invalidate(); setEnabled(false); status_->setText("Checking current settings, applying and verifying readback…");
    const auto revision = revision_;
    auto *watcher = new QFutureWatcher<IntelEnergyUpdate>(this);
    connect(watcher, &QFutureWatcher<IntelEnergyUpdate>::finished, this, [this, watcher, revision, old] {
        const auto out = watcher->result(); setEnabled(true);
        if (revision != revision_) {
            invalidate(); status_->setText(QString("CPU changed during the operation for CPU %1; displayed result discarded. ")
                .arg(old.cpu) + (out.writeAttempted ? "A settings write was attempted. Read the original CPU again." : "No settings write was attempted."));
        } else if (!out.error && out.verified) {
            present(out.after);
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
        IntelEnergyUpdate out;
        const int error = access->transaction([&](HardwareSession &s) { out = applyIntelEnergy(s, old, field, enabled); return out.error; }, 5000, cancelled.get());
        if (error) out.error = error;
        return out;
    }));
}
