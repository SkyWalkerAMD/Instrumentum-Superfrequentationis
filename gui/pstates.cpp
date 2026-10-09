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

PstateValue decodeFamily1aPstate(quint64 raw)
{
    return octool::core::decodeFamily1aPstate(raw);
}

namespace {
class AccessPstateReader final : public octool::core::PstateReader {
public:
    explicit AccessPstateReader(HardwareAccess &access) : access_(access) {}
    int cpuid(unsigned cpu, std::uint32_t leaf, std::uint32_t subleaf,
              std::uint32_t words[4]) override {
        const auto reply = access_.cpuid(cpu, leaf, subleaf);
        if (!reply.error)
            for (unsigned i = 0; i < 4; ++i) words[i] = reply.words[i];
        return reply.error;
    }
    int readMsr(unsigned cpu, std::uint32_t index, std::uint64_t &value) override {
        Request request;
        request.cpu = cpu;
        request.address = index;
        const auto reply = access_.execute(request);
        if (!reply.error) value = reply.value;
        return reply.error;
    }
private:
    HardwareAccess &access_;
};
} // namespace

static QString errorText(int error)
{
    return QString("FAILED (%1): %2").arg(error).arg(QString::fromLocal8Bit(std::strerror(-error)));
}

PstateSnapshot readAmdPstates(HardwareAccess &access, unsigned cpu)
{
    AccessPstateReader reader(access);
    const auto sample = octool::core::readAmdPstates(reader, cpu);
    PstateSnapshot snapshot;
    const QString target = QString("Logical CPU %1: ").arg(cpu);
    const QString description = sample.hasIdentity ?
        target + QString("family %1h, model %2h, stepping %3. ")
            .arg(sample.family, 0, 16).arg(sample.model, 0, 16).arg(sample.stepping) : target;
    using Status = octool::core::PstateStatus;
    switch (sample.status) {
    case Status::CpuReadFailed:
    case Status::LimitReadFailed:
        snapshot.status = description + errorText(sample.error); return snapshot;
    case Status::NotAmd:
        snapshot.status = target + "Requires an AMD Family 1Ah CPU. No MSR read performed.";
        return snapshot;
    case Status::UnsupportedFamily:
        snapshot.status = description + "This PStates decoder supports Family 1Ah only. No MSR read performed.";
        return snapshot;
    case Status::CapabilityUnavailable:
        snapshot.status = description + "Hardware P-state capability unavailable. No MSR read performed.";
        return snapshot;
    case Status::CapabilityNotAdvertised:
        snapshot.status = description + "Hardware P-states not advertised. No MSR read performed.";
        return snapshot;
    case Status::Complete:
        break;
    }
    for (unsigned i = 0; i < 8; ++i) {
        auto &row = snapshot.rows[int(i)];
        const auto &item = sample.rows[i];
        using RowStatus = octool::core::PstateRowStatus;
        if (item.status == RowStatus::AboveLimit) { row.status = "Above reported P-state limit"; continue; }
        if (item.status == RowStatus::ReadFailed) { row.status = errorText(item.error); continue; }
        if (item.status != RowStatus::Read) continue;
        row.read = true; row.raw = item.raw;
        auto decoded = decodeFamily1aPstate(row.raw);
        row.status = !decoded.enabled ? "Definition disabled" :
            decoded.validFrequency ? "Definition read" : "Reserved frequency ID";
    }
    snapshot.status = description + QString("P0-P%1 requested; %2 read failure(s). %3")
        .arg(sample.maximum).arg(sample.failures).arg(QDateTime::currentDateTime().toString(Qt::ISODate));
    return snapshot;
}

PstatesPanel::PstatesPanel(std::shared_ptr<HardwareAccess> access, QWidget *parent, unsigned initialCpu)
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
    cpu_ = new QLineEdit(QString::number(initialCpu), this); cpu_->setObjectName("pstateCpu"); controls->addWidget(cpu_);
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
