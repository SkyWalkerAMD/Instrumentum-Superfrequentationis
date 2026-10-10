// SPDX-License-Identifier: GPL-2.0-only
#include "intelturbopanel.h"
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
QString kindName(IntelTurboKind kind) { return kind == IntelTurboKind::Primary ? "P-core / primary" : "E-core / secondary"; }
QString failure(int error) { return QString("Failed (%1): %2.").arg(error).arg(QString::fromLocal8Bit(std::strerror(-error))); }
}
IntelTurboPanel::IntelTurboPanel(std::shared_ptr<HardwareAccess> access, QWidget *parent, unsigned cpu)
    : QWidget(parent), access_(std::move(access)), cancelled_(std::make_shared<std::atomic<bool>>(false)) {
    setObjectName("intelTurbo"); auto *layout = new QVBoxLayout(this);
    auto *scope = new QLabel("Intel turbo ratio groups - Raptor Lake-S client profile\n"
        "Each group pairs a ratio with an active-core count threshold. These are package limits, not physical core IDs. "
        "Changing a ratio preserves the count thresholds and all other groups. Actual frequency is not measured here.", this);
    scope->setWordWrap(true); layout->addWidget(scope);
    auto *buttons = new QHBoxLayout;
    buttons->addWidget(new QLabel("Logical CPU", this));
    cpu_ = new QLineEdit(QString::number(cpu), this); cpu_->setObjectName("turboCpu"); buttons->addWidget(cpu_);
    kind_ = new QComboBox(this); kind_->setObjectName("turboKind");
    kind_->addItem("P-core / primary", int(IntelTurboKind::Primary));
    kind_->addItem("E-core / secondary", int(IntelTurboKind::Secondary)); buttons->addWidget(kind_);
    auto *read = new QPushButton("Read groups", this); read->setObjectName("turboRead"); buttons->addWidget(read);
    auto *copy = new QPushButton("Copy snapshot", this); copy->setObjectName("turboCopy"); buttons->addWidget(copy);
    layout->addLayout(buttons);
    table_ = new QTableWidget(0, 4, this); table_->setObjectName("turboTable");
    table_->setHorizontalHeaderLabels({"Group", "Active-core threshold", "Ratio limit", "State"});
    table_->verticalHeader()->hide(); table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table_->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    table_->horizontalHeader()->setStretchLastSection(true); layout->addWidget(table_, 1);
    raw_ = new QLabel(this); raw_->setObjectName("turboRaw"); raw_->setWordWrap(true); layout->addWidget(raw_);
    auto *edit = new QHBoxLayout;
    group_ = new QComboBox(this); group_->setObjectName("turboGroup"); edit->addWidget(group_);
    ratio_ = new QLineEdit(this); ratio_->setObjectName("turboRatio"); ratio_->setPlaceholderText("New ratio (1..85)"); edit->addWidget(ratio_);
    apply_ = new QPushButton("Apply selected group…", this); apply_->setObjectName("turboApply"); apply_->setEnabled(false); edit->addWidget(apply_);
    layout->addLayout(edit);
    status_ = new QLabel("Ready. Read groups before making a change. No hardware access has been performed.", this);
    status_->setObjectName("turboStatus"); status_->setWordWrap(true); status_->setTextFormat(Qt::PlainText); layout->addWidget(status_);
    const auto clear = [this] { invalidate(); status_->setText("Target changed. Read groups again."); };
    connect(cpu_, &QLineEdit::textChanged, this, clear);
    connect(kind_, QOverload<int>::of(&QComboBox::currentIndexChanged), this, clear);
    connect(group_, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this] { ratio_->clear(); });
    connect(read, &QPushButton::clicked, this, [this] { refresh(); });
    connect(apply_, &QPushButton::clicked, this, [this] { apply(); });
    connect(copy, &QPushButton::clicked, this, [this] {
        QString text = status_->text()+"\n"+raw_->text()+"\nGroup\tActive-core threshold\tRatio limit\tState\n";
        for (int row = 0; row < table_->rowCount(); ++row) {
            for (int col = 0; col < table_->columnCount(); ++col) text += (col ? "\t" : "") + table_->item(row, col)->text();
            text += "\n";
        }
        QApplication::clipboard()->setText(text);
    });
}
IntelTurboPanel::~IntelTurboPanel() { cancelled_->store(true); }
void IntelTurboPanel::invalidate() {
    snapshot_ = {}; table_->setRowCount(0); group_->clear(); ratio_->clear(); raw_->clear(); apply_->setEnabled(false);
}
void IntelTurboPanel::present(const IntelTurboSnapshot &snapshot) {
    invalidate(); snapshot_ = snapshot;
    if (snapshot.error || !snapshot.valid) {
        status_->setText(failure(snapshot.error ? snapshot.error : -ENODATA)+" No settings displayed."); return;
    }
    const bool editable = snapshot.programmable && !snapshot.locked && snapshot.layoutValid;
    for (unsigned i = 0; i < 8; ++i) {
        const auto count = intelTurboByte(snapshot.coreCounts, i), ratio = intelTurboByte(snapshot.ratios, i);
        const QString state = !count ? "Unused group" : !snapshot.layoutValid ? "Invalid layout - read only"
            : editable ? "Editable" : "Read only - firmware permissions";
        const QStringList values = {QString::number(i), QString::number(count), QString::number(ratio), state};
        const int row = table_->rowCount(); table_->insertRow(row);
        for (int col = 0; col < values.size(); ++col) table_->setItem(row, col, new QTableWidgetItem(values[col]));
        if (editable && count) group_->addItem(QString("Group %1 · up to %2 active cores").arg(i).arg(count), i);
    }
    apply_->setEnabled(editable && group_->count());
    raw_->setText(QString("Ratios %1: %2\nCore counts %3: %4")
        .arg(hexValue(intelTurboRatioMsr(snapshot.kind), 4)).arg(hexValue(snapshot.ratios, 8))
        .arg(hexValue(intelTurboCountMsr(snapshot.kind), 4)).arg(hexValue(snapshot.coreCounts, 8)));
    status_->setText(QString("CPU %1 · %2 · configured groups read. %3")
        .arg(snapshot.cpu).arg(kindName(snapshot.kind)).arg(!snapshot.layoutValid ? "The table does not satisfy the editing rules."
            : editable ? "Choose a group and enter a ratio; firmware limits still apply." : "Firmware permissions do not allow changes."));
}
void IntelTurboPanel::refresh() {
    quint64 cpu = 0; invalidate();
    if (!parseNumber(cpu_->text(), 10, 1048575, cpu)) { status_->setText("Invalid logical CPU."); return; }
    const auto kind = IntelTurboKind(kind_->currentData().toInt());
    setEnabled(false); status_->setText("Reading turbo ratio groups…");
    auto *watcher = new QFutureWatcher<IntelTurboSnapshot>(this);
    connect(watcher, &QFutureWatcher<IntelTurboSnapshot>::finished, this, [this, watcher] {
        present(watcher->result()); setEnabled(true); watcher->deleteLater();
    });
    const auto access = access_; const auto cancelled = cancelled_;
    watcher->setFuture(QtConcurrent::run([access, cancelled, cpu, kind] {
        IntelTurboSnapshot out;
        const int error = access->transaction([&](HardwareSession &s) { out = readIntelTurbo(s, unsigned(cpu), kind); return out.error; }, 5000, cancelled.get());
        if (error) out.error = error;
        return out;
    }));
}
void IntelTurboPanel::apply() {
    quint64 ratio = 0; std::uint64_t encoded = 0;
    const unsigned group = group_->currentData().toUInt();
    if (!snapshot_.valid || !apply_->isEnabled() || !parseNumber(ratio_->text(), 10, 85, ratio) ||
        encodeIntelTurboRatio(snapshot_, group, unsigned(ratio), encoded)) {
        status_->setText("Enter an integer ratio from 1 to 85, keeping active-group ratios non-increasing. Read an unlocked table first."); return;
    }
    const auto old = snapshot_;
    if (QMessageBox::question(this, "Apply turbo ratio group",
        QString("CPU %1 · %2 · group %3\nRatio: %4 → %5; active-core threshold stays %6.\n"
                "Operating settings can affect stability. One write is followed by a complete readback; errors are not retried.")
        .arg(old.cpu).arg(kindName(old.kind)).arg(group).arg(intelTurboByte(old.ratios, group)).arg(ratio)
        .arg(intelTurboByte(old.coreCounts, group)), QMessageBox::Yes | QMessageBox::Cancel, QMessageBox::Cancel) != QMessageBox::Yes) return;
    invalidate(); setEnabled(false); status_->setText("Checking fresh settings, applying one ratio and verifying the complete table…");
    auto *watcher = new QFutureWatcher<IntelTurboUpdate>(this);
    connect(watcher, &QFutureWatcher<IntelTurboUpdate>::finished, this, [this, watcher] {
        const auto out = watcher->result(); setEnabled(true);
        if (!out.error && out.verified) {
            present(out.after);
            status_->setText(status_->text() + (out.unchanged ? " Requested ratio already present; no write performed."
                : " Full table readback verified; core counts and other ratios preserved."));
        } else {
            invalidate(); status_->setText(failure(out.error ? out.error : -EIO) + (out.writeAttempted
                ? " A write was attempted; the final setting is not verified. No retry or rollback was performed." : " No settings write was attempted."));
        }
        watcher->deleteLater();
    });
    const auto access = access_; const auto cancelled = cancelled_;
    watcher->setFuture(QtConcurrent::run([access, cancelled, old, group, ratio] {
        IntelTurboUpdate out;
        const int error = access->transaction([&](HardwareSession &s) {
            out = applyIntelTurboRatio(s, old, group, unsigned(ratio)); return out.error;
        }, 5000, cancelled.get());
        if (error) out.error = error;
        return out;
    }));
}
