// SPDX-License-Identifier: GPL-2.0-only
#include "intelclockpanel.h"
#include "platform/linux_inventory_native.h"
#include <QApplication>
#include <QClipboard>
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
#include <utility>

using namespace octool::core;
IntelClockPanel::IntelClockPanel(std::shared_ptr<HardwareAccess> access, QWidget *parent, unsigned cpu)
    : QWidget(parent), access_(std::move(access)), cancelled_(std::make_shared<std::atomic<bool>>(false)) {
    setObjectName("intelClock"); auto *layout = new QVBoxLayout(this);
    auto *heading = new QLabel("Intel clock information · Raptor Lake-S\n"
        "Read the reported base clock and the last memory-training (MRC) request. "
        "MRC values are historical requests, not current memory frequency or measured voltage. "
        "Supported hardware: CPU 06_B7 and host bridge 8086:A700.",this);
    heading->setWordWrap(true); layout->addWidget(heading);
    auto *target = new QHBoxLayout;
    target->addWidget(new QLabel("Logical CPU",this)); cpu_ = new QLineEdit(QString::number(cpu),this);
    cpu_->setObjectName("clockCpu"); cpu_->setMaximumWidth(100); target->addWidget(cpu_); target->addStretch(); layout->addLayout(target);
    auto *buttons = new QHBoxLayout;
    auto *readButton = new QPushButton("Read once",this); readButton->setObjectName("clockRead"); buttons->addWidget(readButton);
    auto *open = new QPushButton("Open snapshot…",this); buttons->addWidget(open);
    copy_ = new QPushButton("Copy snapshot",this); copy_->setObjectName("clockCopy"); copy_->setEnabled(false); buttons->addWidget(copy_);
    save_ = new QPushButton("Save snapshot…",this); save_->setObjectName("clockSave"); save_->setEnabled(false); buttons->addWidget(save_);
    layout->addLayout(buttons);
    table_ = new QTableWidget(0,3,this); table_->setObjectName("clockTable");
    table_->setHorizontalHeaderLabels({"Information","Value","Unit / interpretation"});
    raw_ = new QTableWidget(0,3,this); raw_->setObjectName("clockRaw");
    raw_->setHorizontalHeaderLabels({"PCU register","Offset","Raw word (hex)"});
    for (auto *table : {table_,raw_}) {
        table->setEditTriggers(QAbstractItemView::NoEditTriggers); table->verticalHeader()->hide();
        table->verticalHeader()->setDefaultSectionSize(30);
        table->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
        table->horizontalHeader()->setStretchLastSection(true);
    }
    layout->addWidget(table_,3); layout->addWidget(raw_,2);
    status_ = new QLabel("Ready. Read or open a clock snapshot.",this); status_->setObjectName("clockStatus"); status_->setWordWrap(true); layout->addWidget(status_);
    connect(readButton,&QPushButton::clicked,this,[this] { read(); });
    connect(open,&QPushButton::clicked,this,[this] {
        const auto path = QFileDialog::getOpenFileName(this,"Open Intel clock snapshot",{},"JSON snapshot (*.json)"); if (path.isEmpty()) return;
        std::vector<std::uint8_t> bytes;
        const auto error = octool::platform::readBoundedFile(QFile::encodeName(path).toStdString(),bytes,65536);
        if (error) { clear(); status_->setText(QString("Snapshot could not be read (%1). Select a regular JSON file up to 64 KiB.").arg(error)); return; }
        loadCapture(QByteArray(reinterpret_cast<const char *>(bytes.data()),int(bytes.size())));
    });
    connect(copy_,&QPushButton::clicked,this,[this] { if (!capture_.isEmpty()) QApplication::clipboard()->setText(QString::fromUtf8(capture_)); });
    connect(save_,&QPushButton::clicked,this,[this] {
        if (capture_.isEmpty()) return;
        const auto path = QFileDialog::getSaveFileName(this,"Save Intel clock snapshot","octool-intel-clock.json","JSON snapshot (*.json)"); if (path.isEmpty()) return;
        QSaveFile file(path);
        if (!file.open(QIODevice::WriteOnly) || file.write(capture_) != capture_.size() || !file.commit()) status_->setText("Snapshot could not be saved: " + file.errorString());
        else status_->setText("Snapshot saved: " + path);
    });
    connect(cpu_,&QLineEdit::textChanged,this,[this] { clear(); status_->setText("Target changed. Read or open a new snapshot."); });
}
IntelClockPanel::~IntelClockPanel() { cancelled_->store(true); }
void IntelClockPanel::clear() {
    ++generation_; table_->setRowCount(0); raw_->setRowCount(0); capture_.clear(); copy_->setEnabled(false); save_->setEnabled(false);
}
QByteArray IntelClockPanel::captureBytes() const { return capture_; }
void IntelClockPanel::present(const IntelClockSnapshot &s) {
    table_->setRowCount(8); raw_->setRowCount(3);
    const auto mrc = [&s](unsigned value) { return s.mrcError ? QString("Unavailable") : QString::number(value); };
    const QList<QStringList> rows = {
        {"Reported BCLK",s.bclkError ? "Unavailable" : QString::number(s.bclkKhz),"kHz"},
        {"MRC Gear request",mrc(s.mrcGear),"ratio"},
        {"MRC reference clock",s.mrcError ? "Unavailable" : s.mrcReference ? "100" : "133.33","MHz steps"},
        {"MRC QCLK ratio",mrc(s.mrcRatio),"encoded"},
        {"MRC PLL shutdown code",s.mrcError ? "Unavailable" : s.mrcRatio ? "No" : "Yes","Historical encoding"},
        {"MRC VDDQ TX request",mrc(s.mrcVddqMillivolts),"mV"},
        {"MRC VDDQ TX IccMax",mrc(s.mrcIccMaxEncoded),"encoded; current conversion unconfirmed"},
        {"Current memory frequency","Unavailable","Not reported by these registers"}
    };
    for (int row = 0; row < rows.size(); ++row) for (int col = 0; col < 3; ++col) table_->setItem(row,col,new QTableWidgetItem(rows[row][col]));
    const auto &specs = intelClockRegisters(); const char *names[] = {"MC_BIOS_REQ","MC_BIOS_DATA","BCLK_FREQ"};
    for (unsigned row = 0; row < 3; ++row) {
        std::uint64_t raw = 0; for (const auto &r : s.registers) if (r.offset == specs[row].offset) raw = r.raw;
        const QStringList values = {QString::fromLatin1(names[row]),hexValue(specs[row].offset,4),hexValue(raw,8)};
        for (int col = 0; col < 3; ++col) raw_->setItem(int(row),col,new QTableWidgetItem(values[col]));
    }
    capture_ = QByteArray::fromStdString(intelClockJson(s)); copy_->setEnabled(true); save_->setEnabled(true);
    status_->setText((s.offline ? QString("Offline snapshot; capture origin unverified. ") : QString("Two matching register reads completed; the capture is not atomic. ")) +
        (s.bclkError || s.mrcError ? QString("Some fields are unavailable. ") : QString()) +
        "The last MRC request does not establish current memory speed, live Gear or measured rail voltage.");
}
bool IntelClockPanel::loadCapture(const QByteArray &bytes) {
    if (busy_) return false;
    clear(); const auto snapshot = parseIntelClockCapture(bytes.toStdString());
    if (snapshot.error || !snapshot.valid) { status_->setText("Invalid Intel clock snapshot: " + QString::fromStdString(snapshot.detail)); return false; }
    present(snapshot); return true;
}
void IntelClockPanel::read() {
    if (busy_) return;
    clear(); quint64 cpu = 0;
    if (!parseNumber(cpu_->text(),10,1048575,cpu)) { status_->setText("Invalid logical CPU. No hardware access performed."); return; }
    const auto generation = generation_; busy_ = true; setEnabled(false); status_->setText("Reading clock information…");
    auto *watcher = new QFutureWatcher<IntelClockSnapshot>(this);
    connect(watcher,&QFutureWatcher<IntelClockSnapshot>::finished,this,[this,watcher,generation] {
        const auto result = watcher->result(); busy_ = false; setEnabled(true);
        if (generation != generation_) status_->setText("Target changed during the read; result discarded.");
        else if (result.error || !result.valid) status_->setText(QString("Read failed (%1). No clock result published.").arg(result.error));
        else present(result);
        watcher->deleteLater();
    });
    const auto access = access_; const auto cancelled = cancelled_;
    watcher->setFuture(QtConcurrent::run([access,cancelled,cpu] {
        IntelClockSnapshot result;
        const auto error = access->transaction([&](HardwareSession &s) { result = readIntelClock(s,unsigned(cpu)); return result.error; },10000,cancelled.get());
        if (error) result.error = error;
        return result;
    }));
}
