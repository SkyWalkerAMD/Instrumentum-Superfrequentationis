// SPDX-License-Identifier: GPL-2.0-only
#include "umcpanel.h"
#include "platform/linux_inventory_native.h"
#include <QCheckBox>
#include <QComboBox>
#include <QDateTime>
#include <QFile>
#include <QFileDialog>
#include <QFutureWatcher>
#include <QHeaderView>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSaveFile>
#include <QTableWidget>
#include <QVBoxLayout>
#include <QtConcurrent>
#include <utility>

using namespace octool::core;
namespace {
QByteArray serialize(const UmcSnapshot &s) {
    QJsonArray regs;
    for(const auto &r:s.registers) regs.append(QJsonObject{{"offset",double(r.offset)},{"value",double(r.value)}});
    return QJsonDocument(QJsonObject{{"format","octool-amd-umc-v1"},{"bank",int(s.target.bank)},
        {"refresh_slot",int(s.target.refreshSlot)},{"registers",regs},
        {"cpu",double(s.target.cpu)},{"family",int(s.identity.family)},{"model",int(s.identity.model)},
        {"stepping",int(s.identity.stepping)},{"pci_identity",double(s.pciIdentity)},
        {"bus",int(s.target.bus)},{"device",int(s.target.device)},{"function",int(s.target.function)},
        {"time",QDateTime::currentDateTimeUtc().toString(Qt::ISODate)},
        {"layout","original OCTool UMC encodings; platform semantics not certified"}}).toJson();
}
}
UmcPanel::UmcPanel(std::shared_ptr<HardwareAccess> access,QWidget *parent,unsigned cpu)
    : QWidget(parent),access_(std::move(access)),cancelled_(std::make_shared<std::atomic<bool>>(false)) {
    setObjectName("amdUmc"); auto *layout=new QVBoxLayout(this);
    auto *heading=new QLabel("AMD UMC · memory controller register snapshot\n"
        "Read support: original Shimada / Granite routing (AMD Family 1Ah and matching PCI ID). "
        "Bank is an address index, not a DIMM label. Refresh slot selects a register word. "
        "Values retain the original encoding; no universal cycles / MHz conversion is assumed. No timing settings are changed.",this);
    heading->setWordWrap(true); layout->addWidget(heading);
    auto *target=new QHBoxLayout;
    auto edit=[this,target](const char *label,const char *name,const QString &value) {
        target->addWidget(new QLabel(label,this)); auto *e=new QLineEdit(value,this); e->setObjectName(name);
        e->setMaximumWidth(100); target->addWidget(e); return e;
    };
    cpu_=edit("Logical CPU","umcCpu",QString::number(cpu));
    bus_=edit("Bus (hex)","umcBus","0"); device_=edit("Device (hex)","umcDevice","0");
    function_=edit("Function (hex)","umcFunction","0"); target->addStretch(); layout->addLayout(target);
    auto *buttons=new QHBoxLayout;
    buttons->addWidget(new QLabel("Bank",this)); bank_=new QComboBox(this); bank_->setObjectName("umcBank");
    for(unsigned i=0;i<=22;++i) bank_->addItem(QString("%1 · %2").arg(i).arg(hexValue(0x50000+i*0x100000,4)),int(i));
    buttons->addWidget(bank_); buttons->addWidget(new QLabel("Refresh slot",this));
    slot_=new QComboBox(this); slot_->setObjectName("umcSlot");
    for(unsigned i=0;i<16;++i) slot_->addItem(QString("%1 · +%2").arg(i).arg(hexValue((i/4)*0x100+(i%4)*4,2)),int(i));
    buttons->addWidget(slot_);
    auto *readButton=new QPushButton("Read once",this); readButton->setObjectName("umcRead"); buttons->addWidget(readButton);
    auto *open=new QPushButton("Open snapshot…",this); buttons->addWidget(open);
    save_=new QPushButton("Save snapshot…",this); save_->setObjectName("umcSave"); save_->setEnabled(false); buttons->addWidget(save_);
    layout->addLayout(buttons);
    auto *comparison=new QHBoxLayout;
    compare_=new QPushButton("Compare snapshot…",this); compare_->setObjectName("umcCompare"); compare_->setEnabled(false);
    resetComparison_=new QPushButton("Clear comparison",this); resetComparison_->setObjectName("umcClearComparison"); resetComparison_->setEnabled(false);
    differences_=new QCheckBox("Only differences",this); differences_->setObjectName("umcDifferences"); differences_->setChecked(true); differences_->setEnabled(false);
    comparison->addWidget(compare_); comparison->addWidget(resetComparison_); comparison->addWidget(differences_); comparison->addStretch(); layout->addLayout(comparison);
    table_=new QTableWidget(0,11,this); table_->setObjectName("umcTable");
    table_->setHorizontalHeaderLabels({"Group","Field","ID","Offset","Bits","Register (hex)","Encoded value","State","Compared register","Compared value","Comparison"});
    for (int col=8;col<11;++col) table_->hideColumn(col);
    table_->setEditTriggers(QAbstractItemView::NoEditTriggers); table_->verticalHeader()->hide();
    table_->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    table_->horizontalHeader()->setStretchLastSection(true); layout->addWidget(table_,1);
    status_=new QLabel("Ready. No controller registers have been read.",this); status_->setWordWrap(true); status_->setObjectName("umcStatus");
    layout->addWidget(status_);
    connect(readButton,&QPushButton::clicked,this,[this] { read(); });
    auto openFile=[this](bool comparing) {
        const auto path=QFileDialog::getOpenFileName(this,comparing ? "Compare UMC snapshot" : "Open UMC snapshot",{},"UMC snapshot (*.json)");
        if(path.isEmpty()) return;
        std::vector<std::uint8_t> bytes;
        const int error=octool::platform::readBoundedFile(QFile::encodeName(path).toStdString(),bytes,65536);
        if(error) {
            if(comparing) clearComparison(); else clear();
            status_->setText(QString("Snapshot could not be read (%1). Choose a regular JSON file of at most 64 KiB.").arg(error)); return;
        }
        const QByteArray input(reinterpret_cast<const char *>(bytes.data()),int(bytes.size()));
        if(comparing) compareCapture(input); else loadCapture(input);
    };
    connect(open,&QPushButton::clicked,this,[openFile] { openFile(false); });
    connect(compare_,&QPushButton::clicked,this,[openFile] { openFile(true); });
    connect(resetComparison_,&QPushButton::clicked,this,[this] { clearComparison(); status_->setText("Comparison cleared. Current snapshot retained; origin and platform identity unverified."); });
    connect(differences_,&QCheckBox::toggled,this,[this] { filterComparison(); });
    connect(save_,&QPushButton::clicked,this,[this] {
        if(capture_.isEmpty()) return;
        const auto path=QFileDialog::getSaveFileName(this,"Save UMC snapshot","octool-umc.json","UMC snapshot (*.json)"); if(path.isEmpty()) return;
        QSaveFile file(path);
        if(!file.open(QIODevice::WriteOnly) || file.write(capture_)!=capture_.size() || !file.commit()) {
            status_->setText("Snapshot could not be saved: "+file.errorString()); return;
        }
        status_->setText("Snapshot saved: "+path);
    });
    auto changed=[this] { clear(); status_->setText("Target changed. Read a new snapshot."); };
    for(auto *e:{cpu_,bus_,device_,function_}) connect(e,&QLineEdit::textChanged,this,changed);
    connect(bank_,QOverload<int>::of(&QComboBox::currentIndexChanged),this,changed);
    connect(slot_,QOverload<int>::of(&QComboBox::currentIndexChanged),this,changed);
}
UmcPanel::~UmcPanel() { cancelled_->store(true); }
void UmcPanel::clear() {
    table_->setRowCount(0); capture_.clear(); current_=UmcCapture{}; save_->setEnabled(false); compare_->setEnabled(false);
    clearComparison();
}
void UmcPanel::clearComparison() {
    comparison_=UmcComparison{}; resetComparison_->setEnabled(false); differences_->setEnabled(false);
    for(int col=8;col<11;++col) table_->hideColumn(col);
    table_->showColumn(4); table_->showColumn(7);
    for(int row=0;row<table_->rowCount();++row) {
        table_->setRowHidden(row,false);
        for(int col=8;col<11;++col) delete table_->takeItem(row,col);
    }
}
void UmcPanel::filterComparison() {
    for(int row=0;row<table_->rowCount();++row) {
        const auto *item=table_->item(row,10);
        const int state=item ? item->data(Qt::UserRole).toInt() : -1;
        table_->setRowHidden(row,differences_->isEnabled() && differences_->isChecked() &&
            (state==int(UmcChange::Unchanged) || state==int(UmcChange::MissingBoth)));
    }
}
QByteArray UmcPanel::captureBytes() const { return capture_; }
void UmcPanel::present(const UmcDecode &d) {
    table_->setRowCount(0);
    for(unsigned group=0;group<12;++group) for(const auto &v:d.values) {
        const auto &f=amdUmcFields().at(v.id); if(f.group!=group) continue;
        const int row=table_->rowCount(); table_->insertRow(row);
        const QStringList values={QString::fromLatin1(amdUmcGroupName(group)),QString::fromLatin1(f.name),QString::number(f.id),
            hexValue(v.offset,4),QString("%1:%2").arg(f.high).arg(f.low),v.error ? "—" : hexValue(v.raw,4),
            v.error ? "—" : QString::number(v.encoded),v.error ? "Missing register" : "Original encoding"};
        for(int col=0;col<values.size();++col) table_->setItem(row,col,new QTableWidgetItem(values[col]));
    }
}
bool UmcPanel::loadCapture(const QByteArray &bytes) {
    clear();
    const auto parsed=parseUmcCapture(bytes.toStdString());
    if(parsed.error) { status_->setText("Invalid UMC snapshot: "+QString::fromStdString(parsed.detail)); return false; }
    current_=parsed; present(current_.decoded); capture_=QByteArray::fromStdString(current_.snapshotJson);
    save_->setEnabled(true); compare_->setEnabled(true);
    status_->setText(QString("Offline snapshot · bank %1 / refresh slot %2 · %3 registers. Platform identity and capture origin are unverified. Missing values remain blank.")
        .arg(current_.target.bank).arg(current_.target.refreshSlot).arg(current_.registers.size())); return true;
}
bool UmcPanel::compareCapture(const QByteArray &bytes) {
    clearComparison();
    const auto other=parseUmcCapture(bytes.toStdString());
    if(current_.error || other.error) { status_->setText("Cannot compare: load two valid UMC snapshots. Current snapshot retained."); return false; }
    comparison_=compareUmcCaptures(current_,other);
    if(comparison_.error) { status_->setText("Cannot compare different banks or refresh slots. Current snapshot retained."); return false; }
    for(int row=0;row<table_->rowCount();++row) {
        const auto &v=comparison_.fields.at(table_->item(row,2)->text().toUInt());
        table_->setItem(row,8,new QTableWidgetItem(v.after.error ? "—" : hexValue(v.after.raw,4)));
        table_->setItem(row,9,new QTableWidgetItem(v.after.error ? "—" : QString::number(v.after.encoded)));
        const QString state=v.state==UmcChange::Changed ? "Value changed" : v.state==UmcChange::RawOnly ? "Register only" :
            v.state==UmcChange::MissingBefore ? "Missing current" : v.state==UmcChange::MissingAfter ? "Missing compared" :
            v.state==UmcChange::MissingBoth ? "Missing both" : "Unchanged";
        auto *item=new QTableWidgetItem(state); item->setData(Qt::UserRole,int(v.state)); table_->setItem(row,10,item);
    }
    for(int col=8;col<11;++col) table_->showColumn(col);
    table_->hideColumn(4); table_->hideColumn(7);
    resetComparison_->setEnabled(true); differences_->setEnabled(true); filterComparison();
    const auto &n=comparison_.counts;
    status_->setText(QString("Offline comparison · bank %1 / slot %2 · %3 values changed, %4 register-only, %5 unchanged, %6 missing current, %7 missing compared, %8 missing both. Matching indices do not verify the same physical channel. Save snapshot keeps the current capture.")
        .arg(current_.target.bank).arg(current_.target.refreshSlot).arg(n[1]).arg(n[2]).arg(n[0]).arg(n[3]).arg(n[4]).arg(n[5]));
    return true;
}
void UmcPanel::read() {
    clear(); UmcTarget t; quint64 cpu=0,bus=0,device=0,function=0;
    if(!parseNumber(cpu_->text(),10,UINT32_MAX,cpu) || !parseNumber(bus_->text(),16,255,bus) ||
        !parseNumber(device_->text(),16,31,device) || !parseNumber(function_->text(),16,7,function)) {
        status_->setText("Invalid logical CPU or PCI address. No hardware access performed."); return;
    }
    t.cpu=unsigned(cpu); t.bus=unsigned(bus); t.device=unsigned(device); t.function=unsigned(function);
    t.bank=unsigned(bank_->currentData().toInt()); t.refreshSlot=unsigned(slot_->currentData().toInt());
    setEnabled(false); status_->setText("Reading the selected UMC register bank…");
    auto *watcher=new QFutureWatcher<UmcSnapshot>(this);
    connect(watcher,&QFutureWatcher<UmcSnapshot>::finished,this,[this,watcher] {
        const auto r=watcher->result(); setEnabled(true);
        if(r.error) status_->setText(QString("Read failed (%1). %2 registers captured; no timing result published.").arg(r.error).arg(r.registers.size()));
        else {
            present(r.decoded); capture_=serialize(r); current_=parseUmcCapture(capture_.toStdString()); save_->setEnabled(true); compare_->setEnabled(true);
            status_->setText(QString("Read complete · bank %1 / refresh slot %2 · %3 registers. Values are legacy encodings; board / firmware semantics are not certified.")
                .arg(r.target.bank).arg(r.target.refreshSlot).arg(r.registers.size()));
        }
        watcher->deleteLater();
    });
    const auto access=access_; const auto cancelled=cancelled_;
    watcher->setFuture(QtConcurrent::run([access,cancelled,t] {
        UmcSnapshot out; out.target=t;
        const int error=access->transaction([&](HardwareSession &s) { out=readAmdUmc(s,t); return out.error; },10000,cancelled.get());
        if(error) out.error=error;
        return out;
    }));
}
