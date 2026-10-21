// SPDX-License-Identifier: GPL-2.0-only
#include "amdpmpanel.h"
#include "core/amd_pm_native.h"
#include "platform/linux_amd_pm.h"
#include "platform/linux_inventory_native.h"
#include <QApplication>
#include <QCheckBox>
#include <QClipboard>
#include <QComboBox>
#include <QFileDialog>
#include <QFutureWatcher>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSignalBlocker>
#include <QTableWidget>
#include <QVBoxLayout>
#include <QtConcurrent>
#include <cerrno>
#include <cmath>
#include <cstring>
#include <locale>
#include <sstream>

using namespace octool::core;
namespace {
QString hex(std::uint32_t n) { return QString("0x%1").arg(n, 8, 16, QChar('0')); }
QString number(double n) { return QString::number(n, 'g', 10); }
}
AmdPmPanel::AmdPmPanel(QWidget *parent, AmdPmReader reader, std::shared_ptr<HardwareAccess> access, unsigned cpu)
    : QWidget(parent), reader_(reader ? reader : octool::platform::linuxAmdPmReader()), access_(access),
      cancelled_(std::make_shared<std::atomic<bool>>(false)) {
    auto *layout = new QVBoxLayout(this);
    auto *intro = new QLabel("AMD PM telemetry: eight exact Shimada table versions. Read the installed ryzen_smu driver, "
        "or open a saved table. Driver reads may request a firmware transfer; no driver is loaded automatically. "
        "Clock storage values and clocks scaled with a supplied BCLK are shown separately. "
        "Original monitors show stored values; their units, active state and physical targets are unverified. "
        "GMI, DIMM and PCIe slots are storage indices, not installed devices or physical port numbers.", this);
    intro->setWordWrap(true); layout->addWidget(intro);
    controls_ = new QWidget(this); auto *controls = new QVBoxLayout(controls_); controls->setContentsMargins(0,0,0,0);
    auto *buttons = new QHBoxLayout;
    read_ = new QPushButton("Read installed driver", controls_); read_->setObjectName("pmRead"); buttons->addWidget(read_);
    auto *open = new QPushButton("Open PM snapshot...", controls_); open->setObjectName("pmOpen"); buttons->addWidget(open);
    auto *raw = new QPushButton("Open raw table...", controls_); raw->setObjectName("pmOpenRaw"); buttons->addWidget(raw);
    copy_ = new QPushButton("Copy JSON snapshot", controls_); copy_->setObjectName("pmCopy"); buttons->addWidget(copy_);
    controls->addLayout(buttons);
    auto *native = new QHBoxLayout;
    native->addWidget(new QLabel("Native capture: logical CPU", controls_));
    cpu_ = new QLineEdit(QString::number(cpu), controls_); cpu_->setObjectName("pmCpu"); cpu_->setMaximumWidth(100); native->addWidget(cpu_);
    auto *readNativeButton = new QPushButton("Read native PM table", controls_); readNativeButton->setObjectName("pmNativeRead");
    readNativeButton->setEnabled(bool(access_)); native->addWidget(readNativeButton);
    auto *nativeScope = new QLabel("Shimada 1Ah/08h; firmware transfer, no tuning change requested", controls_);
    nativeScope->setWordWrap(true); native->addWidget(nativeScope); controls->addLayout(native);
    connect(readNativeButton, &QPushButton::clicked, this, &AmdPmPanel::readNative);
    auto *inputs = new QHBoxLayout;
    inputs->addWidget(new QLabel("Raw table version", controls_));
    version_ = new QComboBox(controls_); version_->setObjectName("pmVersion"); version_->addItem("Select exact version", 0u);
    for (const auto &v : amdPmLayouts()) version_->addItem(hex(v.version), v.version);
    inputs->addWidget(version_);
    scale_ = new QCheckBox("Scale clocks with supplied BCLK", controls_); scale_->setObjectName("pmScale"); inputs->addWidget(scale_);
    bclk_ = new QLineEdit(controls_); bclk_->setObjectName("pmBclk"); bclk_->setPlaceholderText("MHz"); bclk_->setMaximumWidth(140); inputs->addWidget(bclk_);
    controls->addLayout(inputs); layout->addWidget(controls_);
    auto *audit = new QHBoxLayout;
    audit->addWidget(new QLabel("Original fused CCD input (1..16)", controls_));
    layoutCcds_ = new QLineEdit(controls_); layoutCcds_->setObjectName("pmLayoutCcds"); layoutCcds_->setMaximumWidth(60); audit->addWidget(layoutCcds_);
    auto *auditButton = new QPushButton("Audit core/CCD layout", controls_); auditButton->setObjectName("pmLayoutAudit"); audit->addWidget(auditButton);
    copyAudit_ = new QPushButton("Copy layout diagnostic", controls_); copyAudit_->setObjectName("pmLayoutCopy"); audit->addWidget(copyAudit_);
    controls->addLayout(audit);
    connect(auditButton, &QPushButton::clicked, this, [this] { auditLayout(); });
    connect(copyAudit_, &QPushButton::clicked, this, [this] { QApplication::clipboard()->setText(QString::fromStdString(layoutAuditJson_)); });
    connect(layoutCcds_, &QLineEdit::textChanged, this, [this] { layoutAuditJson_.clear(); copyAudit_->setEnabled(false); showFields(); });
    auto *viewRow = new QHBoxLayout;
    viewRow->addWidget(new QLabel("Display", this));
    view_ = new QComboBox(this); view_->setObjectName("pmView");
    view_->addItems({"Public telemetry", "Limit monitor (original mapping)",
        "Power (original mapping)", "Workload (original mapping)", "Power model (original mapping)",
        "Voltage policy (original mapping)", "Limit contributors (original mapping)",
        "Link bandwidth (original mapping)", "Internal clocks (original mapping)",
        "GMI stored arrays", "DIMM stored arrays", "PCIe stored arrays"});
    viewRow->addWidget(view_); viewRow->addStretch();
    layout->addLayout(viewRow);
    metadata_ = new QLabel(this); metadata_->setObjectName("pmMetadata"); metadata_->setTextFormat(Qt::PlainText); layout->addWidget(metadata_);
    table_ = new QTableWidget(0, 7, this); table_->setObjectName("pmTable");
    table_->setHorizontalHeaderLabels({"Field", "Stored value", "Unit", "Scaled clock (MHz)", "Raw word", "Offset", "Status"});
    table_->setEditTriggers(QAbstractItemView::NoEditTriggers); table_->verticalHeader()->hide();
    table_->horizontalHeader()->setStretchLastSection(true); layout->addWidget(table_);
    status_ = new QLabel(this); status_->setObjectName("pmStatus"); status_->setWordWrap(true); status_->setTextFormat(Qt::PlainText); layout->addWidget(status_);
    connect(read_, &QPushButton::clicked, this, &AmdPmPanel::readDriver);
    connect(open, &QPushButton::clicked, this, [this] {
        const auto path = QFileDialog::getOpenFileName(this, "Open PM snapshot", {}, "PM JSON (*.json);;All files (*)");
        if (!path.isEmpty()) loadSnapshotFile(path);
    });
    connect(raw, &QPushButton::clicked, this, [this] {
        if (!version_->currentData().toUInt()) { clear(); status_->setText("Select an exact version before opening a raw table."); return; }
        const auto path = QFileDialog::getOpenFileName(this, "Open raw PM table", {}, "PM data (*.bin *.dat);;All files (*)");
        if (!path.isEmpty()) loadRawFile(path, version_->currentData().toUInt());
    });
    connect(copy_, &QPushButton::clicked, this, [this] { QApplication::clipboard()->setText(QString::fromStdString(nativeCapture_.empty() ? amdPmJson(snapshot_) : nativeCapture_)); });
    connect(cpu_, &QLineEdit::textChanged, this, [this] { clear(); });
    connect(version_, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this] { clear(); });
    connect(scale_, &QCheckBox::toggled, this, [this] { clear(); });
    connect(bclk_, &QLineEdit::textChanged, this, [this] { clear(); });
    connect(view_, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this] { showFields(); });
    clear();
}
AmdPmPanel::~AmdPmPanel() { cancelled_->store(true); }
void AmdPmPanel::clear() {
    ++revision_; nativeCapture_.clear();
    snapshot_ = AmdPmSnapshot(); table_->setRowCount(0); metadata_->clear(); copy_->setEnabled(false);
    layoutAuditJson_.clear(); copyAudit_->setEnabled(false);
    table_->resizeColumnsToContents();
    status_->setText("Ready. No PM data has been read.");
}
void AmdPmPanel::readNative() {
    if (busy_ || !access_) return;
    clear(); AmdPmOptions opt; if (!options(opt)) return;
    quint64 cpu = 0;
    if (!parseNumber(cpu_->text(), 10, 1048575, cpu)) { status_->setText("Enter a valid logical CPU for native capture."); return; }
    SmuTarget target; target.cpu = unsigned(cpu); target.profile = SmuProfile::Shimada;
    const auto revision = revision_;
    busy_ = true; controls_->setEnabled(false); status_->setText("Reading native PM metadata and requesting one firmware transfer...");
    auto *watcher = new QFutureWatcher<AmdPmNativeRead>(this);
    connect(watcher, &QFutureWatcher<AmdPmNativeRead>::finished, this, [this, watcher, revision] {
        const auto r = watcher->result(); watcher->deleteLater(); busy_ = false; controls_->setEnabled(true);
        if (revision != revision_) { clear(); status_->setText("Inputs changed during capture; old native PM result discarded."); return; }
        present(r.snapshot, QString("Native PM: phase %1, %2 bytes read, transfer %3, metadata rechecked %4. Freshness and atomicity are unverified.")
            .arg(amdPmNativePhaseName(r.phase)).arg(qulonglong(r.bytesRead)).arg(r.transferAccepted ? "accepted" : "not confirmed")
            .arg(r.metadataRechecked ? "yes" : "no"));
        nativeCapture_ = "{\"schema_version\":1,\"command\":\"amd-pm-native-read\",\"data\":{\"native\":" + amdPmNativeJson(r) +
            ",\"pm\":" + amdPmJson(r.snapshot) + "}}";
        copy_->setEnabled(true); // Failures also carry useful transport diagnostics, with no raw buffer.
    });
    const auto access = access_; const auto cancelled = cancelled_;
    watcher->setFuture(QtConcurrent::run([access, cancelled, target, opt] {
        AmdPmNativeRead r; r.cpu = target.cpu;
        const int error = access->transaction([&](HardwareSession &s) { r = readShimadaPmNative(s, target, opt); return r.error; }, 10000, cancelled.get());
        if (error && !r.error) { r.error = error; r.snapshot.error = error; r.snapshot.detail = "Native PM transaction could not start"; }
        return r;
    }));
}
void AmdPmPanel::auditLayout() {
    layoutAuditJson_.clear(); copyAudit_->setEnabled(false);
    bool ok = false; const auto text = layoutCcds_->text(); const auto ccds = text.toUInt(&ok, 10);
    if (!ok || text.isEmpty() || text.trimmed() != text || ccds < 1 || ccds > 16 || text.startsWith('+')) {
        status_->setText("Enter explicit original fused CCD input 1..16. It is not inferred from the table version or Linux CPU count."); return;
    }
    if (!snapshot_.version) { status_->setText("Read or open a PM table before auditing its layout."); return; }
    const auto r = auditLegacyAmdPmLayout(snapshot_.version, snapshot_.capturedBytes, ccds);
    table_->setRowCount(0);
    table_->setHorizontalHeaderLabels({"Original row", "First byte", "End byte (exclusive)", "Storage slots", "Outside table (words)", "Core/CCD aliases", "Public-field aliases"});
    for (const auto &range : r.ranges) {
        const int row = table_->rowCount(); table_->insertRow(row);
        const QStringList cells{QString::fromStdString(range.group + " / " + range.label), hex(range.first), hex(range.end),
            QString::number(range.slotCount), QString::number(range.outsideWords), QString::number(range.overlapWords), QString::number(range.publicFieldConflicts)};
        for (int col = 0; col < cells.size(); ++col) table_->setItem(row, col, new QTableWidgetItem(cells[col]));
    }
    table_->resizeColumnsToContents(); layoutAuditJson_ = amdPmLayoutAuditJson(r); copyAudit_->setEnabled(true);
    status_->setText(QString("Layout diagnostic: %1 core slots / %2 CCD slots; %3 words outside capture, %4 core/CCD aliases, %5 public-field aliases. %6%7"
        "No core/CCD values decoded. In-bounds ranges do not prove a valid firmware layout or physical mapping.")
        .arg(r.coreSlots).arg(r.ccdSlots).arg(r.outsideWords).arg(r.overlappingWords).arg(r.publicFieldConflicts)
        .arg(r.constructorFallback ? "Original constructor fell back to 16/2. " : "")
        .arg(r.error ? QString("Metadata error %1. ").arg(r.error) : ""));
}
bool AmdPmPanel::options(AmdPmOptions &out) {
    out.hasBclk = scale_->isChecked();
    if (!out.hasBclk) return true;
    const auto text = bclk_->text().toStdString(); std::istringstream n(text); n.imbue(std::locale::classic());
    if (text.empty() || text.find_first_of(" \t\r\n") != std::string::npos || !(n >> out.bclkMHz) || !n.eof() || !std::isfinite(out.bclkMHz) || out.bclkMHz <= 0) {
        status_->setText("Supplied BCLK must be a positive finite number in MHz."); return false;
    }
    return true;
}
void AmdPmPanel::readDriver() {
    if (busy_) return;
    clear(); AmdPmOptions opt; if (!options(opt)) return;
    busy_ = true; controls_->setEnabled(false); status_->setText("Reading the installed driver's PM table...");
    auto *watcher = new QFutureWatcher<AmdPmDriverRead>(this);
    connect(watcher, &QFutureWatcher<AmdPmDriverRead>::finished, this, [this, watcher] {
        const auto result = watcher->result(); watcher->deleteLater(); busy_ = false; controls_->setEnabled(true);
        present(result.snapshot, "Installed ryzen_smu driver; freshness and sample timing are controlled by that driver.");
    });
    const auto reader = reader_;
    // Worker owns its reader/options and never captures UI objects. Closing the
    // panel disconnects presentation; it does not claim to abort a kernel read.
    watcher->setFuture(QtConcurrent::run([reader, opt] { return readAmdPmDriver(reader, opt); }));
}
bool AmdPmPanel::loadSnapshotFile(const QString &path) {
    if (busy_) return false;
    clear(); std::vector<std::uint8_t> bytes; AmdPmSnapshot result;
    result.error = octool::platform::readBoundedFile(path.toStdString(), bytes, amdPmCaptureMaximumBytes);
    if (result.error) result.detail = "Cannot read PM snapshot file";
    else result = parseAmdPmCapture(std::string(bytes.begin(), bytes.end()));
    present(result, "Offline snapshot: " + path); return !result.error;
}
bool AmdPmPanel::loadRawFile(const QString &path, std::uint32_t version) {
    if (busy_) return false;
    clear(); AmdPmOptions opt; if (!options(opt)) return false;
    AmdPmSnapshot result; result.version = version; const auto expected = amdPmTableSize(version);
    if (!expected) { result.error = -ENOTSUP; result.detail = "Select a supported exact raw table version"; }
    else {
        std::vector<std::uint8_t> bytes; result.error = octool::platform::readBoundedFile(path.toStdString(), bytes, expected);
        if (result.error) result.detail = "Cannot read bounded raw PM table";
        else result = decodeAmdPm(bytes, version, opt);
    }
    present(result, "Offline raw table: " + path); return !result.error;
}
void AmdPmPanel::present(const AmdPmSnapshot &s, const QString &source) {
    snapshot_ = s; table_->setRowCount(0);
    const QSignalBlocker a(version_), b(scale_), c(bclk_);
    const int index = version_->findData(s.version); version_->setCurrentIndex(index < 0 ? 0 : index);
    scale_->setChecked(s.options.hasBclk); bclk_->setText(s.options.hasBclk ? QString::number(s.options.bclkMHz, 'g', 17) : QString());
    metadata_->setText(QString("Table %1 | captured %2 / expected %3 bytes | no table checksum")
        .arg(hex(s.version)).arg(qulonglong(s.capturedBytes)).arg(qulonglong(s.expectedBytes)));
    showFields(); copy_->setEnabled(!s.fields.empty());
    QString status = source;
    if (s.error) status += "\n" + QString::fromStdString(s.detail) + " (" + QString::fromLocal8Bit(std::strerror(-s.error)) + ")";
    else status += "\nStored values decoded. BCLK correction uses the supplied value. Original-monitor units, active policies and physical targets are unverified.";
    status_->setText(status);
}
void AmdPmPanel::showFields() {
    layoutAuditJson_.clear(); copyAudit_->setEnabled(false);
    table_->setHorizontalHeaderLabels({"Field", "Stored value", "Unit", "Scaled clock (MHz)", "Raw word", "Offset", "Status"});
    const auto &s = snapshot_;
    const int index = view_->currentIndex();
    const bool original = index != 0;
    const char *groups[] = {"power", "workload", "power_model", "voltage_policy", "limit_contributors",
        "link_bandwidth", "internal_clocks", "gmi_storage", "dimm_storage", "pcie_storage"};
    const auto &fields = index == 0 ? s.fields : index == 1 ? s.originalLimits :
        index < 7 ? s.originalPackage : index < 9 ? s.originalFabric : s.originalArrays;
    table_->setRowCount(0);
    for (const auto &f : fields) {
        if (index >= 2 && (index > 11 || f.group != groups[index-2])) continue;
        const int row = table_->rowCount(); table_->insertRow(row);
        const QString state = f.error ? "Non-finite stored value" : f.scaleError ? "Clock scaling out of range" :
            original ? "Units unverified" : f.clock && !s.options.hasBclk ? "BCLK not supplied" : "Decoded";
        const QString label = QString::fromStdString(f.name) +
            (f.slotCount > 1 ? QString(" [slot %1]").arg(f.slot) : QString());
        const QStringList values{label, f.error ? "Unavailable" : number(f.value), original ? QString("Unverified") : QString::fromStdString(f.unit),
            f.scaled ? number(f.scaledValue) : QString("—"), hex(f.raw), hex(f.offset), state};
        for (int col = 0; col < values.size(); ++col) table_->setItem(row, col, new QTableWidgetItem(values[col]));
    }
    table_->resizeColumnsToContents();
}
