// SPDX-License-Identifier: GPL-2.0-only
#include "inteluncorepanel.h"
#include <QApplication>
#include <QClipboard>
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
}
IntelUncorePanel::IntelUncorePanel(std::shared_ptr<HardwareAccess> access, QWidget *parent, unsigned cpu)
    : QWidget(parent), access_(std::move(access)), cancelled_(std::make_shared<std::atomic<bool>>(false)) {
    setObjectName("intelUncore"); auto *layout = new QVBoxLayout(this);
    auto *scope = new QLabel("Intel Ring / LLC ratio range - Raptor Lake-S and Sapphire Rapids\n"
        "The selected CPU locates a shared Ring / LLC domain. Enter minimum and maximum ratios together; "
        "actual frequency is not measured here. Firmware and OS power management may restrict or change these limits.", this);
    scope->setWordWrap(true); layout->addWidget(scope);
    auto *buttons = new QHBoxLayout;
    buttons->addWidget(new QLabel("Logical CPU", this));
    cpu_ = new QLineEdit(QString::number(cpu), this); cpu_->setObjectName("uncoreCpu"); buttons->addWidget(cpu_);
    auto *read = new QPushButton("Read range", this); read->setObjectName("uncoreRead"); buttons->addWidget(read);
    auto *copy = new QPushButton("Copy snapshot", this); copy->setObjectName("uncoreCopy"); buttons->addWidget(copy);
    layout->addLayout(buttons);
    table_ = new QTableWidget(0, 3, this); table_->setObjectName("uncoreTable");
    table_->setHorizontalHeaderLabels({"Limit", "Configured ratio", "State"});
    table_->verticalHeader()->hide(); table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table_->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    table_->horizontalHeader()->setStretchLastSection(true); layout->addWidget(table_, 1);
    raw_ = new QLabel(this); raw_->setObjectName("uncoreRaw"); raw_->setWordWrap(true); layout->addWidget(raw_);
    auto *edit = new QHBoxLayout;
    edit->addWidget(new QLabel("Minimum ratio", this));
    minimum_ = new QLineEdit(this); minimum_->setObjectName("uncoreMinimum"); minimum_->setPlaceholderText("1..127"); edit->addWidget(minimum_);
    edit->addWidget(new QLabel("Maximum ratio", this));
    maximum_ = new QLineEdit(this); maximum_->setObjectName("uncoreMaximum"); maximum_->setPlaceholderText("1..127"); edit->addWidget(maximum_);
    apply_ = new QPushButton("Apply range…", this); apply_->setObjectName("uncoreApply"); apply_->setEnabled(false); edit->addWidget(apply_);
    layout->addLayout(edit);
    status_ = new QLabel("Ready. Read the current range before making a change.", this);
    status_->setObjectName("uncoreStatus"); status_->setWordWrap(true); status_->setTextFormat(Qt::PlainText); layout->addWidget(status_);
    connect(cpu_, &QLineEdit::textChanged, this, [this] { invalidate(); status_->setText("CPU changed. Read the range again."); });
    connect(read, &QPushButton::clicked, this, [this] { refresh(); });
    connect(apply_, &QPushButton::clicked, this, [this] { apply(); });
    connect(copy, &QPushButton::clicked, this, [this] {
        QString text = status_->text()+"\n"+raw_->text()+"\nLimit\tConfigured ratio\tState\n";
        for (int row = 0; row < table_->rowCount(); ++row) {
            for (int col = 0; col < table_->columnCount(); ++col) text += (col ? "\t" : "") + table_->item(row, col)->text();
            text += "\n";
        }
        QApplication::clipboard()->setText(text);
    });
}
IntelUncorePanel::~IntelUncorePanel() { cancelled_->store(true); }
void IntelUncorePanel::invalidate() {
    snapshot_ = {}; table_->setRowCount(0); minimum_->clear(); maximum_->clear(); raw_->clear(); apply_->setEnabled(false);
}
void IntelUncorePanel::present(const IntelUncoreSnapshot &snapshot) {
    invalidate(); snapshot_ = snapshot;
    if (snapshot.error || !snapshot.valid) {
        status_->setText(failure(snapshot.error ? snapshot.error : -ENODATA)+" No settings displayed."); return;
    }
    const bool editable = intelUncoreEditable(snapshot);
    const unsigned values[] = {intelUncoreMinimum(snapshot.raw), intelUncoreMaximum(snapshot.raw)};
    for (int row = 0; row < 2; ++row) {
        table_->insertRow(row);
        table_->setItem(row, 0, new QTableWidgetItem(row ? "Maximum" : "Minimum"));
        table_->setItem(row, 1, new QTableWidgetItem(QString::number(values[row])));
        table_->setItem(row, 2, new QTableWidgetItem(editable ? "Range available for editing" : "Unsupported range - read only"));
    }
    minimum_->setText(QString::number(values[0])); maximum_->setText(QString::number(values[1]));
    apply_->setEnabled(editable);
    raw_->setText(QString("MSR 0x620: %1").arg(hexValue(snapshot.raw, 8)));
    status_->setText(QString("CPU %1 · shared Ring / LLC ratio limits read. %2").arg(snapshot.cpu).arg(editable
        ? "Minimum must not exceed maximum. Firmware limits still apply."
        : "Zero or inverted limits cannot be edited with this profile."));
}
void IntelUncorePanel::refresh() {
    quint64 cpu = 0; invalidate();
    if (!parseNumber(cpu_->text(), 10, 1048575, cpu)) { status_->setText("Invalid logical CPU."); return; }
    setEnabled(false); status_->setText("Reading Ring / LLC range…");
    auto *watcher = new QFutureWatcher<IntelUncoreSnapshot>(this);
    connect(watcher, &QFutureWatcher<IntelUncoreSnapshot>::finished, this, [this, watcher] {
        present(watcher->result()); setEnabled(true); watcher->deleteLater();
    });
    const auto access = access_; const auto cancelled = cancelled_;
    watcher->setFuture(QtConcurrent::run([access, cancelled, cpu] {
        IntelUncoreSnapshot out;
        const int error = access->transaction([&](HardwareSession &s) { out = readIntelUncore(s, unsigned(cpu)); return out.error; }, 5000, cancelled.get());
        if (error) out.error = error;
        return out;
    }));
}
void IntelUncorePanel::apply() {
    quint64 minimum = 0, maximum = 0; std::uint64_t encoded = 0;
    if (!intelUncoreEditable(snapshot_) || !parseNumber(minimum_->text(), 10, 127, minimum) ||
        !parseNumber(maximum_->text(), 10, 127, maximum) ||
        encodeIntelUncoreRange(snapshot_.raw, unsigned(minimum), unsigned(maximum), encoded)) {
        status_->setText("Enter two integer ratios from 1 to 127, with minimum no greater than maximum."); return;
    }
    const auto old = snapshot_;
    if (QMessageBox::question(this, "Apply Ring / LLC range",
        QString("CPU %1 · shared Ring / LLC domain\nMinimum: %2 → %3; maximum: %4 → %5.\n"
                "Operating settings can affect stability. Both bounds are applied together and read back.")
        .arg(old.cpu).arg(intelUncoreMinimum(old.raw)).arg(minimum).arg(intelUncoreMaximum(old.raw)).arg(maximum),
        QMessageBox::Yes | QMessageBox::Cancel, QMessageBox::Cancel) != QMessageBox::Yes) return;
    invalidate(); setEnabled(false); status_->setText("Checking current settings, applying the range and verifying readback…");
    auto *watcher = new QFutureWatcher<IntelUncoreUpdate>(this);
    connect(watcher, &QFutureWatcher<IntelUncoreUpdate>::finished, this, [this, watcher] {
        const auto out = watcher->result(); setEnabled(true);
        if (!out.error && out.verified) {
            present(out.after);
            status_->setText(status_->text() + (out.unchanged ? " Requested range already present; no write performed."
                : " Full register readback verified; other bits preserved."));
        } else {
            invalidate(); status_->setText(failure(out.error ? out.error : -EIO) + (out.writeAttempted
                ? " A write was attempted; the final setting is not verified. No retry or rollback was performed." : " No settings write was attempted."));
        }
        watcher->deleteLater();
    });
    const auto access = access_; const auto cancelled = cancelled_;
    watcher->setFuture(QtConcurrent::run([access, cancelled, old, minimum, maximum] {
        IntelUncoreUpdate out;
        const int error = access->transaction([&](HardwareSession &s) {
            out = applyIntelUncoreRange(s, old, unsigned(minimum), unsigned(maximum)); return out.error;
        }, 5000, cancelled.get());
        if (error) out.error = error;
        return out;
    }));
}
