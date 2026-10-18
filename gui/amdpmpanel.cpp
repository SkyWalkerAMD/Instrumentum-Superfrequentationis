// SPDX-License-Identifier: GPL-2.0-only
#include "amdpmpanel.h"
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
AmdPmPanel::AmdPmPanel(QWidget *parent, AmdPmReader reader) : QWidget(parent), reader_(reader ? reader : octool::platform::linuxAmdPmReader()) {
    auto *layout = new QVBoxLayout(this);
    auto *intro = new QLabel("AMD PM telemetry: eight exact Shimada table versions. Read the installed ryzen_smu driver, "
        "or open a saved table. Driver reads may request a firmware transfer; no driver is loaded automatically. "
        "Clock storage values and clocks scaled with a supplied BCLK are shown separately. "
        "The original limit monitor exposes stored floats with unverified units; its 0/1 labels do not identify physical targets.", this);
    intro->setWordWrap(true); layout->addWidget(intro);
    controls_ = new QWidget(this); auto *controls = new QVBoxLayout(controls_); controls->setContentsMargins(0,0,0,0);
    auto *buttons = new QHBoxLayout;
    read_ = new QPushButton("Read installed driver", controls_); read_->setObjectName("pmRead"); buttons->addWidget(read_);
    auto *open = new QPushButton("Open PM snapshot...", controls_); open->setObjectName("pmOpen"); buttons->addWidget(open);
    auto *raw = new QPushButton("Open raw table...", controls_); raw->setObjectName("pmOpenRaw"); buttons->addWidget(raw);
    copy_ = new QPushButton("Copy JSON snapshot", controls_); copy_->setObjectName("pmCopy"); buttons->addWidget(copy_);
    controls->addLayout(buttons);
    auto *inputs = new QHBoxLayout;
    inputs->addWidget(new QLabel("Raw table version", controls_));
    version_ = new QComboBox(controls_); version_->setObjectName("pmVersion"); version_->addItem("Select exact version", 0u);
    for (const auto &v : amdPmLayouts()) version_->addItem(hex(v.version), v.version);
    inputs->addWidget(version_);
    scale_ = new QCheckBox("Scale clocks with supplied BCLK", controls_); scale_->setObjectName("pmScale"); inputs->addWidget(scale_);
    bclk_ = new QLineEdit(controls_); bclk_->setObjectName("pmBclk"); bclk_->setPlaceholderText("MHz"); bclk_->setMaximumWidth(140); inputs->addWidget(bclk_);
    controls->addLayout(inputs); layout->addWidget(controls_);
    auto *viewRow = new QHBoxLayout;
    viewRow->addWidget(new QLabel("Display", this));
    view_ = new QComboBox(this); view_->setObjectName("pmView");
    view_->addItems({"Public telemetry", "Limit monitor (original mapping)"}); viewRow->addWidget(view_); viewRow->addStretch();
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
    connect(copy_, &QPushButton::clicked, this, [this] { QApplication::clipboard()->setText(QString::fromStdString(amdPmJson(snapshot_))); });
    connect(version_, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this] { clear(); });
    connect(scale_, &QCheckBox::toggled, this, [this] { clear(); });
    connect(bclk_, &QLineEdit::textChanged, this, [this] { clear(); });
    connect(view_, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this] { showFields(); });
    clear();
}
void AmdPmPanel::clear() {
    snapshot_ = AmdPmSnapshot(); table_->setRowCount(0); metadata_->clear(); copy_->setEnabled(false);
    table_->resizeColumnsToContents();
    status_->setText("Ready. No PM data has been read.");
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
    result.error = octool::platform::readBoundedFile(path.toStdString(), bytes, 65536);
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
    else status += "\nStored values decoded. BCLK correction uses the supplied value. Limit-monitor units and physical target identities are unverified.";
    status_->setText(status);
}
void AmdPmPanel::showFields() {
    const auto &s = snapshot_;
    const bool original = view_->currentIndex() == 1;
    table_->setRowCount(0);
    for (const auto &f : original ? s.originalLimits : s.fields) {
        const int row = table_->rowCount(); table_->insertRow(row);
        const QString state = f.error ? "Non-finite stored value" : f.scaleError ? "Clock scaling out of range" :
            original ? "Units unverified" : f.clock && !s.options.hasBclk ? "BCLK not supplied" : "Decoded";
        const QStringList values{QString::fromStdString(f.name), f.error ? "Unavailable" : number(f.value), original ? QString("Unverified") : QString::fromStdString(f.unit),
            f.scaled ? number(f.scaledValue) : QString("—"), hex(f.raw), hex(f.offset), state};
        for (int col = 0; col < values.size(); ++col) table_->setItem(row, col, new QTableWidgetItem(values[col]));
    }
    table_->resizeColumnsToContents();
}
