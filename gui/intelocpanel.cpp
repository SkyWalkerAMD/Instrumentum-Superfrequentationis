// SPDX-License-Identifier: GPL-2.0-only
#include "intelocpanel.h"
#include <QApplication>
#include <QClipboard>
#include <QComboBox>
#include <QDateTime>
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

namespace {
QString failure(int error, const octool::core::IntelOcResponse &r) {
    QString text = QString("Failed (%1): %2.").arg(error).arg(QString::fromLocal8Bit(std::strerror(-error)));
    if (r.completed && r.firmwareStatus) text += QString(" Firmware status: 0x%1.").arg(r.firmwareStatus, 2, 16, QChar('0'));
    return text;
}
}
IntelOcPanel::IntelOcPanel(std::shared_ptr<HardwareAccess> access, QWidget *parent, unsigned cpu)
    : QWidget(parent), access_(std::move(access)), cancelled_(std::make_shared<std::atomic<bool>>(false)) {
    setObjectName("intelOc"); auto *layout = new QVBoxLayout(this);
    auto *scope = new QLabel("Intel core / cache voltage and maximum ratio · Raptor Lake-S client profile\n"
        "Read sends a firmware query. Each apply changes one setting in the selected domain and checks the full readback. "
        "BIOS locks and undervolt protection may reject changes. Xeon W790 / W890 and per-core VF controls use separate interfaces and are not enabled here.", this);
    scope->setWordWrap(true); layout->addWidget(scope);
    auto *buttons = new QHBoxLayout;
    buttons->addWidget(new QLabel("Logical CPU", this)); cpu_ = new QLineEdit(QString::number(cpu), this); cpu_->setObjectName("ocCpu"); buttons->addWidget(cpu_);
    domain_ = new QComboBox(this); domain_->setObjectName("ocDomain");
    domain_->addItem("Core", int(octool::core::IntelOcDomain::Core));
    domain_->addItem("Cache / ring", int(octool::core::IntelOcDomain::Cache)); buttons->addWidget(domain_);
    auto *read = new QPushButton("Read settings", this); read->setObjectName("ocRead"); buttons->addWidget(read);
    auto *copy = new QPushButton("Copy snapshot", this); buttons->addWidget(copy); layout->addLayout(buttons);
    table_ = new QTableWidget(0, 3, this); table_->setObjectName("ocTable");
    table_->setHorizontalHeaderLabels({"Setting", "Value", "Unit / meaning"});
    table_->setEditTriggers(QAbstractItemView::NoEditTriggers); table_->verticalHeader()->hide();
    table_->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    table_->horizontalHeader()->setStretchLastSection(true); layout->addWidget(table_, 1);
    auto *modify = new QHBoxLayout;
    modify->addWidget(new QLabel("New offset (mV)", this)); value_ = new QLineEdit(this); value_->setObjectName("ocOffset");
    value_->setPlaceholderText("Signed offset; no preset"); modify->addWidget(value_);
    apply_ = new QPushButton("Apply offset…", this); apply_->setObjectName("ocApply"); apply_->setEnabled(false); modify->addWidget(apply_); layout->addLayout(modify);
    auto *ratioRow = new QHBoxLayout;
    ratioRow->addWidget(new QLabel("Maximum OC ratio", this)); ratio_ = new QLineEdit(this); ratio_->setObjectName("ocRatio");
    ratio_->setPlaceholderText("Integer 1..85; no preset"); ratioRow->addWidget(ratio_);
    applyRatio_ = new QPushButton("Apply ratio…", this); applyRatio_->setObjectName("ocApplyRatio"); applyRatio_->setEnabled(false);
    ratioRow->addWidget(applyRatio_); layout->addLayout(ratioRow);
    auto *ratioScope = new QLabel("Ratio is a domain limit. Actual frequency depends on other limits and the reference clock. Per-core and active-core turbo tables are separate.", this);
    ratioScope->setWordWrap(true); layout->addWidget(ratioScope);
    status_ = new QLabel("Ready. Read settings before making a change. No hardware access has been performed.", this);
    status_->setObjectName("ocStatus"); status_->setWordWrap(true); status_->setTextFormat(Qt::PlainText); layout->addWidget(status_);
    const auto clear = [this] { invalidate(); status_->setText("Target changed. Read settings again."); };
    connect(cpu_, &QLineEdit::textChanged, this, clear);
    connect(domain_, QOverload<int>::of(&QComboBox::currentIndexChanged), this, clear);
    connect(read, &QPushButton::clicked, this, [this] { refresh(); });
    connect(apply_, &QPushButton::clicked, this, [this] { apply(); });
    connect(applyRatio_, &QPushButton::clicked, this, [this] { apply(true); });
    connect(copy, &QPushButton::clicked, this, [this] {
        QString text = status_->text()+"\n";
        for (int row = 0; row < table_->rowCount(); ++row)
            text += table_->item(row, 0)->text()+"\t"+table_->item(row, 1)->text()+"\t"+table_->item(row, 2)->text()+"\n";
        QApplication::clipboard()->setText(text);
    });
}
IntelOcPanel::~IntelOcPanel() { cancelled_->store(true); }
void IntelOcPanel::invalidate() {
    snapshot_ = {}; table_->setRowCount(0); value_->clear(); ratio_->clear();
    apply_->setEnabled(false); applyRatio_->setEnabled(false);
}
void IntelOcPanel::present(const octool::core::IntelOcSnapshot &snapshot) {
    using namespace octool::core;
    invalidate(); snapshot_ = snapshot;
    if (snapshot.error || !snapshot.valid) {
        status_->setText(failure(snapshot.error ? snapshot.error : -ENODATA, snapshot.response)+" No settings displayed."); return;
    }
    auto add = [this](const QString &name, const QString &value, const QString &unit) {
        const int row = table_->rowCount(); table_->insertRow(row);
        table_->setItem(row, 0, new QTableWidgetItem(name)); table_->setItem(row, 1, new QTableWidgetItem(value)); table_->setItem(row, 2, new QTableWidgetItem(unit));
    };
    const auto data = snapshot.response.data;
    add("Voltage offset", QString::number(intelOcOffsetMillivolts(data), 'g', 12), "mV (configured offset)");
    add("Voltage target", QString::number(double((data >> 8) & 4095) / 1024.0, 'g', 12), "V (encoded target, not measured voltage)");
    add("Target mode", data & (1u << 20) ? "Override" : "Adaptive", "Preserved on both applies");
    add("Maximum OC ratio", QString::number(data & 255), "Domain limit encoding; not measured frequency");
    add("Mailbox data", hexValue(data, 4), "Raw 32-bit setting");
    add("OC lock", snapshot.locked ? "Locked" : "Not locked", "Firmware may impose additional restrictions");
    apply_->setEnabled(!snapshot.locked); applyRatio_->setEnabled(!snapshot.locked);
    status_->setText(QString("CPU %1 · %2 · signature %3 · %4. Configured values read; physical voltage is not measured.")
        .arg(snapshot.cpu).arg(snapshot.domain == IntelOcDomain::Core ? "Core" : "Cache / ring")
        .arg(hexValue(snapshot.identity.signature, 4)).arg(QDateTime::currentDateTime().toString(Qt::ISODate)));
}
void IntelOcPanel::refresh() {
    using namespace octool::core;
    quint64 cpu = 0;
    if (!parseNumber(cpu_->text(), 10, UINT32_MAX, cpu)) { invalidate(); status_->setText("Invalid logical CPU."); return; }
    const auto domain = IntelOcDomain(domain_->currentData().toInt());
    invalidate(); setEnabled(false); status_->setText("Reading selected voltage domain…");
    auto *watcher = new QFutureWatcher<IntelOcSnapshot>(this);
    connect(watcher, &QFutureWatcher<IntelOcSnapshot>::finished, this, [this, watcher] {
        present(watcher->result()); setEnabled(true); watcher->deleteLater();
    });
    const auto access = access_; const auto cancelled = cancelled_;
    watcher->setFuture(QtConcurrent::run([access, cancelled, cpu, domain] {
        IntelOcSnapshot out;
        const int error = access->transaction([&](HardwareSession &s) { out = readIntelOc(s, unsigned(cpu), domain); return out.error; }, 5000, cancelled.get());
        if (error) out.error = error;
        return out;
    }));
}
void IntelOcPanel::apply(bool ratio) {
    using namespace octool::core;
    bool ok = false; std::uint32_t encoded = 0; quint64 requestedRatio = 0;
    const double requested = ratio ? 0.0 : value_->text().toDouble(&ok);
    int encodingError = 0;
    if (ratio) {
        ok = parseNumber(ratio_->text(), 10, 85, requestedRatio);
        encodingError = encodeIntelOcRatio(unsigned(requestedRatio), snapshot_.response.data, encoded);
    } else encodingError = encodeIntelOcOffset(requested, snapshot_.response.data, encoded);
    if (!ok || !snapshot_.valid || snapshot_.locked || encodingError) {
        status_->setText(ratio ? "Read an unlocked snapshot and enter an integer ratio from 1 to 85. This range is not an operating recommendation."
                              : "Read an unlocked snapshot and enter a finite offset within the register's encoding range."); return;
    }
    const auto old = snapshot_;
    const QString change = ratio ? QString("Maximum OC ratio: %1 → %2.\nVoltage offset, target and mode are preserved.").arg(old.response.data & 255).arg(requestedRatio)
        : QString("Offset: %1 mV → %2 mV after encoding.\nRatio, voltage target and mode are preserved.")
            .arg(intelOcOffsetMillivolts(old.response.data), 0, 'g', 12).arg(intelOcOffsetMillivolts(encoded), 0, 'g', 12);
    if (QMessageBox::question(this, ratio ? "Apply Intel maximum ratio" : "Apply Intel voltage offset",
        QString("CPU %1 · %2\n%3\nChanging operating settings can make the system unstable. The full setting is read back after one write; failures are not automatically retried.")
        .arg(old.cpu).arg(old.domain == IntelOcDomain::Core ? "Core" : "Cache / ring")
        .arg(change),
        QMessageBox::Yes | QMessageBox::Cancel, QMessageBox::Cancel) != QMessageBox::Yes) return;
    invalidate(); setEnabled(false); status_->setText("Comparing fresh settings, applying the change and checking readback…");
    auto *watcher = new QFutureWatcher<IntelOcUpdate>(this);
    connect(watcher, &QFutureWatcher<IntelOcUpdate>::finished, this, [this, watcher, ratio] {
        const auto r = watcher->result(); setEnabled(true);
        if (!r.error && r.verified) {
            present(r.readback);
            status_->setText(status_->text()+(r.writeAttempted ? (ratio ? " Ratio accepted and full setting read back unchanged outside the ratio field."
                : " Offset accepted and full setting read back unchanged outside the offset field.") : " Requested encoding already present; no settings-change command sent."));
        } else {
            invalidate(); status_->setText(failure(r.error ? r.error : -EIO, r.response)+
                (r.writeAttempted ? " A settings-change command was attempted; the final setting is not verified. No retry or rollback was performed." : " No settings-change command was sent."));
        }
        watcher->deleteLater();
    });
    const auto access = access_; const auto cancelled = cancelled_;
    watcher->setFuture(QtConcurrent::run([access, cancelled, old, requested, ratio, requestedRatio] {
        IntelOcUpdate out;
        const int error = access->transaction([&](HardwareSession &s) {
            out = ratio ? applyIntelOcRatio(s, old, unsigned(requestedRatio)) : applyIntelOcOffset(s, old, requested);
            return out.error;
        }, 5000, cancelled.get());
        if (error) out.error = error;
        return out;
    }));
}
