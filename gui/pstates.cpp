// SPDX-License-Identifier: GPL-2.0-only
#include "pstates.h"
#include <QApplication>
#include <QClipboard>
#include <QComboBox>
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
    explicit AccessPstateReader(octool::core::HardwareSession &access) : access_(access) {}
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
    octool::core::HardwareSession &access_;
};
} // namespace

static QString errorText(int error)
{
    return QString("FAILED (%1): %2").arg(error).arg(QString::fromLocal8Bit(std::strerror(-error)));
}

PstateSnapshot readAmdPstates(HardwareAccess &access, unsigned cpu)
{
    octool::core::PstateSnapshot sample;
    const int error = access.transaction([&](octool::core::HardwareSession &session) {
        AccessPstateReader reader(session); sample = octool::core::readAmdPstates(reader, cpu); return 0;
    });
    if (error) { sample.error = error; sample.status = octool::core::PstateStatus::CpuReadFailed; }
    PstateSnapshot snapshot;
    snapshot.family = sample.hasIdentity ? sample.family : 0;
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
        snapshot.status = target + "Requires an AMD Family 17h, 19h or 1Ah CPU. No MSR read performed.";
        return snapshot;
    case Status::UnsupportedFamily:
        snapshot.status = description + "This PStates decoder supports Family 17h, 19h and 1Ah. No MSR read performed.";
        return snapshot;
    case Status::CapabilityUnavailable:
        snapshot.status = description + "Hardware P-state capability unavailable. No MSR read performed.";
        return snapshot;
    case Status::CapabilityNotAdvertised:
        snapshot.status = description + "Hardware P-states not advertised. No MSR read performed.";
        return snapshot;
    case Status::InconsistentLimit:
        snapshot.status = description + "FAILED: inconsistent P-state limits. No definitions read.";
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
        row.decoded = octool::core::decodeAmdPstate(sample.family, row.raw);
        row.electrical = octool::core::decodeAmdPstateElectrical(sample.family, sample.model, sample.hypervisor, row.raw);
        using Frequency = octool::core::PstateFrequencyStatus;
        switch (row.decoded.frequencyStatus) {
        case Frequency::Valid: row.status = "Definition read"; break;
        case Frequency::Disabled: row.status = "Definition disabled"; break;
        case Frequency::ReservedFid: row.status = "Reserved frequency ID"; break;
        case Frequency::DividerOff: row.status = "Divider off"; break;
        case Frequency::UnverifiedDivider: row.status = "Unverified divider code"; break;
        default: row.status = "Unsupported frequency encoding"; break;
        }
    }
    snapshot.status = description + QString("P0-P%1 read pass; allowed non-boosted range P%2-P%1; %3 read failure(s). Limit %4. %5")
        .arg(sample.maximum).arg(sample.minimum).arg(sample.failures).arg(hexValue(sample.limitRaw, 8))
        .arg(QDateTime::currentDateTime().toString(Qt::ISODate));
    const auto encoding = octool::core::amdPstateElectricalEncoding(sample.family, sample.model);
    snapshot.status += encoding == octool::core::PstateElectricalEncoding::Unavailable ?
        " Voltage/current decoding unavailable for this model." : sample.hypervisor ?
        " Virtualized CPU: voltage/current decoding unavailable." :
        QString(" %1 VID voltage and expected single-core current descriptors; not live measurements.")
            .arg(QString::fromLatin1(octool::core::pstateElectricalEncodingName(encoding)).toUpper());
    return snapshot;
}

PstatesPanel::PstatesPanel(std::shared_ptr<HardwareAccess> access, QWidget *parent, unsigned initialCpu)
    : QWidget(parent), access_(std::move(access))
{
    auto *layout = new QVBoxLayout(this);
    auto *description = new QLabel("AMD CPU Functions / PStates — read only\n"
        "Legacy P-state definitions for AMD Family 17h, 19h and 1Ah. Configured MHz is not a live clock measurement; "
        "CPPC / amd-pstate may control the running frequency independently.\n"
        "Definitions show raw VID / Idd bits. The Voltage / current view decodes selected models only: "
        "stored VID voltage and expected maximum single-core current for ACPI _PSS, not measured mV / A or PBO limits. "
        "P-state writes remain unavailable.", this);
    description->setWordWrap(true); layout->addWidget(description);
    auto *controls = new QHBoxLayout;
    controls->addWidget(new QLabel("Logical CPU (decimal)", this));
    cpu_ = new QLineEdit(QString::number(initialCpu), this); cpu_->setObjectName("pstateCpu"); controls->addWidget(cpu_);
    auto *read = new QPushButton("Read once", this); read->setObjectName("pstateRead");
    controls->addWidget(read);
    auto *copy = new QPushButton("Copy snapshot", this); controls->addWidget(copy);
    view_ = new QComboBox(this); view_->setObjectName("pstateView");
    view_->addItems({"Definitions / raw fields", "Voltage / current descriptors"});
    controls->addWidget(view_);
    layout->addLayout(controls);
    table_ = new QTableWidget(8, 12, this); table_->setObjectName("pstateTable");
    table_->setHorizontalHeaderLabels({"P-state", "MSR", "Raw value (hex)", "Enabled", "Configured MHz", "Status",
        "VID bits", "Idd bits 29:22", "Div bits 31:30", "FID", "DFS ID", "Raw bit 32"});
    table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table_->verticalHeader()->hide();
    table_->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    table_->horizontalHeader()->setStretchLastSection(true);
    layout->addWidget(table_, 1);
    status_ = new QLabel("Ready. No CPUID or MSR access has been performed.", this);
    status_->setObjectName("pstateStatus"); status_->setWordWrap(true); layout->addWidget(status_);
    clearRows();
    connect(view_, static_cast<void(QComboBox::*)(int)>(&QComboBox::currentIndexChanged),
        this, [this](int) { renderSnapshot(); });
    connect(read, &QPushButton::clicked, this, [this] { refresh(); });
    connect(cpu_, &QLineEdit::textChanged, this, [this] {
        ++revision_;
        clearRows(); status_->setText("CPU selection changed. Read again to obtain a new snapshot.");
    });
    connect(copy, &QPushButton::clicked, this, [this] {
        QString text = "OCTool AMD PStates (read only)\n" + status_->text() + "\n";
        for (int column = 0; column < table_->columnCount(); ++column)
            text += table_->horizontalHeaderItem(column)->text() + (column+1 == table_->columnCount() ? "\n" : "\t");
        for (int row = 0; row < table_->rowCount(); ++row)
            for (int column = 0; column < table_->columnCount(); ++column)
                text += table_->item(row, column)->text() + (column+1 == table_->columnCount() ? "\n" : "\t");
        QApplication::clipboard()->setText(text);
    });
}

void PstatesPanel::clearRows()
{
    hasSnapshot_ = false; snapshot_ = PstateSnapshot{};
    renderSnapshot();
}

void PstatesPanel::renderSnapshot()
{
    const bool electrical = view_->currentIndex() == 1;
    table_->setColumnCount(electrical ? 8 : 12);
    if (electrical) table_->setHorizontalHeaderLabels({"P-state", "Complete VID", "VID voltage (mV)",
        "Voltage status", "Expected core (A)", "Current status", "Definition status", "Raw value (hex)"});
    else table_->setHorizontalHeaderLabels({"P-state", "MSR", "Raw value (hex)", "Enabled", "Configured MHz", "Status",
        "VID bits", "Idd bits 29:22", "Div bits 31:30", "FID", "DFS ID", "Raw bit 32"});
    if (!electrical && hasSnapshot_) {
        if (snapshot_.family == 0x1a) table_->horizontalHeaderItem(6)->setText("VID bits 32,21:14");
        else if (snapshot_.family == 0x17 || snapshot_.family == 0x19)
            table_->horizontalHeaderItem(6)->setText("VID low bits 21:14");
    }
    for (int row = 0; row < 8; ++row)
        for (int column = 0; column < table_->columnCount(); ++column)
            table_->setItem(row, column, new QTableWidgetItem(column == 0 ? QString("P%1").arg(row) :
                !electrical && column == 1 ? hexValue(0xc0010064u + unsigned(row), 4) :
                column == (electrical ? 6 : 5) ? "Not read" : "—"));
    if (!hasSnapshot_) return;
    for (int row = 0; row < snapshot_.rows.size(); ++row) {
        const auto &item = snapshot_.rows[row];
        table_->item(row, electrical ? 6 : 5)->setText(item.status);
        if (!item.read) continue;
        if (electrical) {
            const auto &e = item.electrical;
            if (e.hasVid) table_->item(row, 1)->setText(QString::number(e.vid));
            if (e.hasVoltage) table_->item(row, 2)->setText(QString::number(e.voltageMv, 'g', 12));
            table_->item(row, 3)->setText(QString::fromLatin1(octool::core::pstateElectricalStatusName(e.voltageStatus)).replace('_', ' '));
            if (e.hasCurrent) table_->item(row, 4)->setText(QString::number(e.expectedCoreCurrentA, 'g', 12));
            table_->item(row, 5)->setText(QString::fromLatin1(octool::core::pstateElectricalStatusName(e.currentStatus)).replace('_', ' '));
            table_->item(row, 7)->setText(hexValue(item.raw, 8));
            continue;
        }
        const auto &decoded = item.decoded;
        table_->item(row, 2)->setText(hexValue(item.raw, 8));
        table_->item(row, 3)->setText(decoded.enabled ? "Yes" : "No");
        if (decoded.validFrequency) table_->item(row, 4)->setText(QString::number(decoded.frequencyMHz, 'g', 12));
        table_->item(row, 6)->setText(QString::number(decoded.vidBits));
        table_->item(row, 7)->setText(QString::number(decoded.iddValueBits));
        table_->item(row, 8)->setText(QString::number(decoded.iddDivBits));
        table_->item(row, 9)->setText(QString::number(decoded.frequencyId));
        if (decoded.hasDivider) table_->item(row, 10)->setText(QString::number(decoded.dividerId));
        table_->item(row, 11)->setText(decoded.rawBit32 ? "1" : "0");
    }
}

void PstatesPanel::refresh()
{
    quint64 number = 0;
    if (!parseNumber(cpu_->text(), 10, UINT32_MAX, number)) {
        status_->setText("Invalid logical CPU: enter an unsigned decimal number."); return;
    }
    const auto revision = revision_;
    clearRows(); setEnabled(false); status_->setText("Reading CPU identity and P-state definitions…");
    auto *watcher = new QFutureWatcher<PstateSnapshot>(this);
    connect(watcher, &QFutureWatcher<PstateSnapshot>::finished, this, [this, watcher, revision] {
        const auto snapshot = watcher->result();
        if (revision != revision_) {
            clearRows(); status_->setText("CPU changed during the read. Result discarded; read again.");
            setEnabled(true); watcher->deleteLater(); return;
        }
        snapshot_ = snapshot; hasSnapshot_ = true; renderSnapshot();
        status_->setText(snapshot.status); setEnabled(true); watcher->deleteLater();
    });
    auto access = access_;
    watcher->setFuture(QtConcurrent::run([access, number] { return readAmdPstates(*access, unsigned(number)); }));
}
