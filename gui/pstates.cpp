// SPDX-License-Identifier: GPL-2.0-only
#include "pstates.h"
#include <QApplication>
#include <QClipboard>
#include <QDateTime>
#include <QFutureWatcher>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QTableWidget>
#include <QVBoxLayout>
#include <QtConcurrent>
#include <cerrno>
#include <cstring>

// Frequency definition: Linux cpupower helpers/amd.c, AMD PPR 57238 p235.
// The PPR describes model 02h. Only the Linux-supported family-wide frequency
// rule is used here; model-specific VID/current interpretations are excluded.
PstateValue decodeFamily1aPstate(quint64 raw)
{
    PstateValue value;
    value.enabled = (raw >> 63) != 0;
    const unsigned fid = unsigned(raw & 0xfff);
    value.validFrequency = value.enabled && fid >= 0x10;
    if (value.validFrequency) value.frequencyMHz = fid * 5;
    return value;
}

static QString errorText(int error)
{
    return QString("FAILED (%1): %2").arg(error).arg(QString::fromLocal8Bit(std::strerror(-error)));
}

PstateSnapshot readAmdPstates(HardwareAccess &access, unsigned cpu)
{
    PstateSnapshot snapshot;
    const QString target = QString("Logical CPU %1: ").arg(cpu);
    auto root = access.cpuid(cpu, 0);
    if (root.error) { snapshot.status = target + errorText(root.error); return snapshot; }
    char vendor[13]{};
    std::memcpy(vendor, &root.words[1], 4);
    std::memcpy(vendor + 4, &root.words[3], 4);
    std::memcpy(vendor + 8, &root.words[2], 4);
    if (std::strcmp(vendor, "AuthenticAMD") || root.words[0] < 1) {
        snapshot.status = target + "Requires an AMD Family 1Ah CPU. No MSR read performed.";
        return snapshot;
    }
    auto identity = access.cpuid(cpu, 1);
    if (identity.error) { snapshot.status = target + errorText(identity.error); return snapshot; }
    const unsigned baseFamily = (identity.words[0] >> 8) & 0xf;
    const unsigned family = baseFamily + (baseFamily == 0xf ? (identity.words[0] >> 20) & 0xff : 0);
    const unsigned model = ((identity.words[0] >> 4) & 0xf) |
        ((baseFamily == 6 || baseFamily == 0xf) ? (identity.words[0] >> 12) & 0xf0 : 0);
    const unsigned stepping = identity.words[0] & 0xf;
    const QString description = target + QString("family %1h, model %2h, stepping %3. ")
        .arg(family, 0, 16).arg(model, 0, 16).arg(stepping);
    if (family != 0x1a) {
        snapshot.status = description + "This PStates decoder supports Family 1Ah only. No MSR read performed.";
        return snapshot;
    }
    auto extended = access.cpuid(cpu, 0x80000000);
    if (extended.error) { snapshot.status = description + errorText(extended.error); return snapshot; }
    if (extended.words[0] < 0x80000007) {
        snapshot.status = description + "Hardware P-state capability unavailable. No MSR read performed.";
        return snapshot;
    }
    auto power = access.cpuid(cpu, 0x80000007);
    if (power.error) { snapshot.status = description + errorText(power.error); return snapshot; }
    if (!(power.words[3] & (1u << 7))) {
        snapshot.status = description + "Hardware P-states not advertised. No MSR read performed.";
        return snapshot;
    }
    Request request;
    request.cpu = cpu; request.address = 0xc0010061;
    auto limit = access.execute(request);
    if (limit.error) { snapshot.status = description + errorText(limit.error); return snapshot; }
    const unsigned maximum = unsigned((limit.value >> 4) & 7);
    unsigned failures = 0;
    for (unsigned i = 0; i < 8; ++i) {
        auto &row = snapshot.rows[int(i)];
        if (i > maximum) { row.status = "Above reported P-state limit"; continue; }
        request.address = 0xc0010064u + i;
        auto result = access.execute(request);
        if (result.error) { row.status = errorText(result.error); ++failures; continue; }
        row.read = true; row.raw = result.value;
        auto decoded = decodeFamily1aPstate(row.raw);
        row.status = !decoded.enabled ? "Definition disabled" :
            decoded.validFrequency ? "Definition read" : "Reserved frequency ID";
    }
    snapshot.status = description + QString("P0-P%1 requested; %2 read failure(s). %3")
        .arg(maximum).arg(failures).arg(QDateTime::currentDateTime().toString(Qt::ISODate));
    return snapshot;
}

PstatesPanel::PstatesPanel(std::shared_ptr<HardwareAccess> access, QWidget *parent)
    : QWidget(parent), access_(std::move(access))
{
    auto *layout = new QVBoxLayout(this);
    auto *description = new QLabel("AMD CPU Functions / PStates — read only\n"
        "Legacy P-state definitions for AMD Family 1Ah. Configured MHz is not a live clock measurement; "
        "CPPC / amd-pstate may control the running frequency independently.\n"
        "Voltage and current conversion are awaiting verification. The complete raw MSR is preserved.", this);
    description->setWordWrap(true); layout->addWidget(description);
    auto *controls = new QHBoxLayout;
    controls->addWidget(new QLabel("Logical CPU (decimal)", this));
    cpu_ = new QLineEdit("0", this); cpu_->setObjectName("pstateCpu"); controls->addWidget(cpu_);
    auto *read = new QPushButton("Read once", this); read->setObjectName("pstateRead");
    controls->addWidget(read);
    auto *copy = new QPushButton("Copy snapshot", this); controls->addWidget(copy);
    layout->addLayout(controls);
    table_ = new QTableWidget(8, 6, this); table_->setObjectName("pstateTable");
    table_->setHorizontalHeaderLabels({"P-state", "MSR", "Raw value (hex)", "Enabled", "Configured MHz", "Status"});
    table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table_->verticalHeader()->hide();
    table_->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    table_->horizontalHeader()->setStretchLastSection(true);
    layout->addWidget(table_, 1);
    status_ = new QLabel("Ready. No CPUID or MSR access has been performed.", this);
    status_->setObjectName("pstateStatus"); status_->setWordWrap(true); layout->addWidget(status_);
    clearRows();
    connect(read, &QPushButton::clicked, this, [this] { refresh(); });
    connect(cpu_, &QLineEdit::textChanged, this, [this] {
        clearRows(); status_->setText("CPU selection changed. Read again to obtain a new snapshot.");
    });
    connect(copy, &QPushButton::clicked, this, [this] {
        QString text = "OCTool AMD PStates (read only)\n" + status_->text() + "\n";
        for (int column = 0; column < table_->columnCount(); ++column)
            text += table_->horizontalHeaderItem(column)->text() + (column == 5 ? "\n" : "\t");
        for (int row = 0; row < table_->rowCount(); ++row)
            for (int column = 0; column < table_->columnCount(); ++column)
                text += table_->item(row, column)->text() + (column == 5 ? "\n" : "\t");
        QApplication::clipboard()->setText(text);
    });
}

void PstatesPanel::clearRows()
{
    for (int row = 0; row < 8; ++row)
        for (int column = 0; column < 6; ++column)
            table_->setItem(row, column, new QTableWidgetItem(column == 0 ? QString("P%1").arg(row) :
                column == 1 ? hexValue(0xc0010064u + unsigned(row), 4) : column == 5 ? "Not read" : "—"));
}

void PstatesPanel::refresh()
{
    quint64 number = 0;
    if (!parseNumber(cpu_->text(), 10, UINT32_MAX, number)) {
        status_->setText("Invalid logical CPU: enter an unsigned decimal number."); return;
    }
    clearRows(); setEnabled(false); status_->setText("Reading CPU identity and P-state definitions…");
    auto *watcher = new QFutureWatcher<PstateSnapshot>(this);
    connect(watcher, &QFutureWatcher<PstateSnapshot>::finished, this, [this, watcher] {
        const auto snapshot = watcher->result();
        for (int row = 0; row < snapshot.rows.size(); ++row) {
            const auto &item = snapshot.rows[row];
            table_->item(row, 5)->setText(item.status);
            if (!item.read) continue;
            auto decoded = decodeFamily1aPstate(item.raw);
            table_->item(row, 2)->setText(hexValue(item.raw, 8));
            table_->item(row, 3)->setText(decoded.enabled ? "Yes" : "No");
            if (decoded.validFrequency) table_->item(row, 4)->setText(QString::number(decoded.frequencyMHz));
        }
        status_->setText(snapshot.status); setEnabled(true); watcher->deleteLater();
    });
    auto access = access_;
    watcher->setFuture(QtConcurrent::run([access, number] { return readAmdPstates(*access, unsigned(number)); }));
}
