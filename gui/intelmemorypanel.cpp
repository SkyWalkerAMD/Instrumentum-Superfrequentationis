// SPDX-License-Identifier: GPL-2.0-only
#include "intelmemorypanel.h"
#include "platform/linux_inventory_native.h"
#include <QApplication>
#include <QClipboard>
#include <QComboBox>
#include <QFile>
#include <QFileDialog>
#include <QFutureWatcher>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSaveFile>
#include <QTableWidget>
#include <QVBoxLayout>
#include <QtConcurrent>
#include <cerrno>
#include <utility>

using namespace octool::core;
IntelMemoryPanel::IntelMemoryPanel(std::shared_ptr<HardwareAccess> access, QWidget *parent, unsigned cpu)
    : QWidget(parent), access_(std::move(access)), cancelled_(std::make_shared<std::atomic<bool>>(false)) {
    setObjectName("intelMemory"); auto *layout = new QVBoxLayout(this);
    auto *heading = new QLabel("Intel memory timings · Raptor Lake-S\n"
        "Read one controller channel or open a saved snapshot. Supported hardware: CPU 06_B7 and host bridge 8086:A700. "
        "DDR4 uses channel 0; DDR5 uses channel 0 or 1. Controller/channel indices are not motherboard DIMM labels.", this);
    heading->setWordWrap(true); layout->addWidget(heading);
    auto *target = new QHBoxLayout;
    target->addWidget(new QLabel("Logical CPU",this)); cpu_ = new QLineEdit(QString::number(cpu),this);
    cpu_->setObjectName("imcCpu"); cpu_->setMaximumWidth(100); target->addWidget(cpu_);
    target->addWidget(new QLabel("Controller",this)); controller_ = new QComboBox(this); controller_->addItems({"0","1"});
    controller_->setObjectName("imcController"); target->addWidget(controller_);
    target->addWidget(new QLabel("Channel",this)); channel_ = new QComboBox(this); channel_->addItems({"0","1"});
    channel_->setObjectName("imcChannel"); target->addWidget(channel_); target->addStretch(); layout->addLayout(target);
    auto *buttons = new QHBoxLayout;
    auto *readButton = new QPushButton("Read once",this); readButton->setObjectName("imcRead"); buttons->addWidget(readButton);
    auto *open = new QPushButton("Open snapshot…",this); buttons->addWidget(open);
    copy_ = new QPushButton("Copy snapshot",this); copy_->setObjectName("imcCopy"); copy_->setEnabled(false); buttons->addWidget(copy_);
    save_ = new QPushButton("Save snapshot…",this); save_->setObjectName("imcSave"); save_->setEnabled(false); buttons->addWidget(save_);
    layout->addLayout(buttons);
    context_ = new QLabel("No timing snapshot loaded.",this); context_->setObjectName("imcContext"); context_->setWordWrap(true); layout->addWidget(context_);
    table_ = new QTableWidget(0,6,this); table_->setObjectName("imcTable");
    table_->setHorizontalHeaderLabels({"Timing","Value","Unit","Offset","Bits","Register (hex)"});
    table_->setEditTriggers(QAbstractItemView::NoEditTriggers); table_->verticalHeader()->hide();
    table_->verticalHeader()->setDefaultSectionSize(25);
    table_->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    table_->horizontalHeader()->setStretchLastSection(true); layout->addWidget(table_,1);
    status_ = new QLabel("Ready. No controller registers have been read.",this); status_->setObjectName("imcStatus"); status_->setWordWrap(true); layout->addWidget(status_);
    connect(readButton,&QPushButton::clicked,this,[this] { read(); });
    connect(open,&QPushButton::clicked,this,[this] {
        const auto path = QFileDialog::getOpenFileName(this,"Open Intel memory snapshot",{},"JSON snapshot (*.json)"); if (path.isEmpty()) return;
        std::vector<std::uint8_t> bytes;
        const auto error = octool::platform::readBoundedFile(QFile::encodeName(path).toStdString(),bytes,65536);
        if (error) { clear(); status_->setText(QString("Snapshot could not be read (%1). Select a regular JSON file up to 64 KiB.").arg(error)); return; }
        loadCapture(QByteArray(reinterpret_cast<const char *>(bytes.data()),int(bytes.size())));
    });
    connect(copy_,&QPushButton::clicked,this,[this] { if (!capture_.isEmpty()) QApplication::clipboard()->setText(QString::fromUtf8(capture_)); });
    connect(save_,&QPushButton::clicked,this,[this] {
        if (capture_.isEmpty()) return;
        const auto path = QFileDialog::getSaveFileName(this,"Save Intel memory snapshot","octool-intel-memory.json","JSON snapshot (*.json)"); if (path.isEmpty()) return;
        QSaveFile file(path);
        if (!file.open(QIODevice::WriteOnly) || file.write(capture_) != capture_.size() || !file.commit()) status_->setText("Snapshot could not be saved: " + file.errorString());
        else status_->setText("Snapshot saved: " + path);
    });
    auto changed = [this] { clear(); status_->setText("Target changed. Read or open a new snapshot."); };
    connect(cpu_,&QLineEdit::textChanged,this,changed);
    connect(controller_,QOverload<int>::of(&QComboBox::currentIndexChanged),this,changed);
    connect(channel_,QOverload<int>::of(&QComboBox::currentIndexChanged),this,changed);
}
IntelMemoryPanel::~IntelMemoryPanel() { cancelled_->store(true); }
void IntelMemoryPanel::clear() {
    ++generation_; table_->setRowCount(0); capture_.clear(); copy_->setEnabled(false); save_->setEnabled(false);
    context_->setText("No timing snapshot loaded.");
}
QByteArray IntelMemoryPanel::captureBytes() const { return capture_; }
void IntelMemoryPanel::present(const IntelMemorySnapshot &s) {
    const auto &fields = intelMemoryFields(s.version); table_->setRowCount(int(fields.size()));
    for (std::size_t i = 0; i < fields.size(); ++i) {
        const auto &f = fields[i]; const auto &v = s.values[i]; std::uint64_t raw = 0;
        for (const auto &r : s.registers) if (r.offset == f.offset) raw = r.raw;
        auto unit = QString::fromLatin1(intelMemoryUnit(s,f));
        if (unit == "encoded") unit = "Encoded";
        if (unit == "MC_DCLK") unit = "MC DCLK";
        if (unit == "1024_DCLK") unit = "1024 DCLK";
        if (unit == "tREFI_count") unit = "tREFI count";
        const bool pinMask = unit == "odt_pin_mask";
        if (pinMask) unit = "ODT pin mask";
        const QStringList values = {QString::fromLatin1(f.name),v.error == -ENODEV ? "Rank absent" : v.error ? "Not used" : pinMask ? hexValue(v.encoded,1) : QString::number(v.encoded),
            v.error ? "—" : unit,hexValue(f.offset,4),
            QString("%1:%2").arg(f.high).arg(f.low),hexValue(raw,8)};
        for (int col = 0; col < values.size(); ++col) table_->setItem(int(i),col,new QTableWidgetItem(values[col]));
    }
    context_->setText(QString("%1 · controller %2 / channel %3 · rank mask %4 · Gear2 %5 / Gear4 %6 · command-stretch encoding %7 · snapshot v%8")
        .arg(s.dramType ? "DDR5" : "DDR4").arg(s.target.controller).arg(s.target.channel).arg(hexValue(s.rankMask,1))
        .arg(s.gear2 ? "on" : "off").arg(s.gear4 ? "on" : "off").arg(s.commandStretch).arg(s.version));
    capture_ = QByteArray::fromStdString(intelMemoryJson(s)); copy_->setEnabled(true); save_->setEnabled(true);
    status_->setText((s.offline ? QString("Offline snapshot; capture origin unverified. ") : QString("Two matching register reads completed; the capture is not atomic. ")) +
        (s.noGear2Divide || s.noGear4Divide ? "Gear parameter overrides are active; tCK fields retain their register encoding. " : "Programmed timing values; units are shown per field. ") +
        (s.version == 1 ? "Legacy v1: RTL and power-down timings were not captured. " : "RTL uses QCLK; tCSL and tCSH use MC DCLK. ") +
        (s.version < 3 ? "Refresh policy, self-refresh and ZQ fields were not captured. " :
            "tREFIx9 uses 1024 DCLK; tZQCAL uses 2*tCK unless overridden. Self-refresh delays without documented units remain encoded. ") +
        (s.version < 4 ? "DDRIO low-power fields were not captured. " :
            "DDRIO flags permit power-mode indications; they do not report the current power state. ") +
        (s.version < 5 ? "ODT matrix and write-data delays were not captured. " :
            "Stored ODT masks select pins 3:0; matrix activation is unverified. DDR5 uses non-target ODT. "
            "Write-delay add/subtract counts use DCLK; the 1QCLK flag is a Gear2 option. No effective tCWL or IOL is derived. ") +
        "tWRPRE is the write-command-to-precharge interval. No settings were changed.");
}
bool IntelMemoryPanel::loadCapture(const QByteArray &bytes) {
    if (busy_) return false;
    clear(); const auto snapshot = parseIntelMemoryCapture(bytes.toStdString());
    if (snapshot.error || !snapshot.valid) { status_->setText("Invalid Intel memory snapshot: " + QString::fromStdString(snapshot.detail)); return false; }
    present(snapshot); return true;
}
void IntelMemoryPanel::read() {
    if (busy_) return;
    clear(); quint64 cpu = 0;
    if (!parseNumber(cpu_->text(),10,UINT32_MAX,cpu)) { status_->setText("Invalid logical CPU. No hardware access performed."); return; }
    IntelMemoryTarget target; target.cpu = unsigned(cpu); target.controller = unsigned(controller_->currentIndex()); target.channel = unsigned(channel_->currentIndex());
    const auto generation = generation_; busy_ = true; setEnabled(false); status_->setText("Reading the selected memory controller channel…");
    auto *watcher = new QFutureWatcher<IntelMemorySnapshot>(this);
    connect(watcher,&QFutureWatcher<IntelMemorySnapshot>::finished,this,[this,watcher,generation] {
        const auto result = watcher->result(); busy_ = false; setEnabled(true);
        if (generation != generation_) status_->setText("Target changed during the read; result discarded.");
        else if (result.error || !result.valid) status_->setText(QString("Read failed (%1). No timing result published.").arg(result.error));
        else present(result);
        watcher->deleteLater();
    });
    const auto access = access_; const auto cancelled = cancelled_;
    watcher->setFuture(QtConcurrent::run([access,cancelled,target] {
        IntelMemorySnapshot result;
        const auto error = access->transaction([&](HardwareSession &s) { result = readIntelMemory(s,target); return result.error; },10000,cancelled.get());
        if (error) result.error = error;
        return result;
    }));
}
