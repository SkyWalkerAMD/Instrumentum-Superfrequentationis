// SPDX-License-Identifier: GPL-2.0-only
#include "umcpanel.h"
#include <QComboBox>
#include <QDateTime>
#include <QFile>
#include <QFileDialog>
#include <QFutureWatcher>
#include <QHeaderView>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSaveFile>
#include <QTableWidget>
#include <QVBoxLayout>
#include <QtConcurrent>
#include <cmath>
#include <utility>

using namespace octool::core;
namespace {
bool number(const QJsonValue &v, unsigned maximum, unsigned &out) {
    if(!v.isDouble()) return false;
    const double n=v.toDouble();
    if(!std::isfinite(n) || n<0 || n>maximum || std::floor(n)!=n) return false;
    out=unsigned(n); return true;
}
bool hexWord(const QJsonValue &v, unsigned &out) {
    if (!v.isString()) return false;
    const auto text = v.toString();
    if (text.size() != 10 || !text.startsWith("0x")) return false;
    unsigned value = 0;
    for (int i = 2; i < text.size(); ++i) {
        const auto c = text.at(i).unicode();
        const unsigned digit = c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : c >= 'A' && c <= 'F' ? c - 'A' + 10 : 16;
        if (digit > 15) return false;
        value = (value << 4) | digit;
    }
    out = value; return true;
}
bool cliCapture(const QJsonObject &report, QJsonObject &out) {
    unsigned version = 0, error = 0, bank = 0, slot = 0, cpu = 0;
    if (!number(report.value("schema_version"), 1, version) || version != 1 ||
        report.value("command").toString() != "amd-umc-read" ||
        !report.value("ok").isBool() || !report.value("ok").toBool() ||
        !number(report.value("error"), 0, error) || !report.value("data").isObject()) return false;
    const auto data = report.value("data").toObject();
    if (!number(data.value("bank"), 22, bank) || !number(data.value("refresh_slot"), 15, slot) ||
        !number(data.value("cpu"), UINT32_MAX, cpu) || !data.value("registers").isArray() ||
        !data.value("pci").isObject()) return false;
    const auto pci = data.value("pci").toObject(); unsigned domain = 0, bus = 0, device = 0, function = 0;
    if (!number(pci.value("domain"), 0, domain) || !number(pci.value("bus"), 255, bus) ||
        !number(pci.value("device"), 31, device) || !number(pci.value("function"), 7, function)) return false;
    const auto registers = data.value("registers").toArray();
    if (registers.size() != 56) return false;
    QJsonArray converted;
    for (const auto &item : registers) {
        if (!item.isObject()) return false;
        const auto reg = item.toObject(); unsigned offset = 0, raw = 0;
        if (!hexWord(reg.value("offset"), offset) || !hexWord(reg.value("raw"), raw)) return false;
        converted.append(QJsonObject{{"offset", double(offset)}, {"value", double(raw)}});
    }
    out = QJsonObject{{"format", "octool-amd-umc-v1"}, {"bank", int(bank)}, {"refresh_slot", int(slot)},
        {"registers", converted}, {"cpu", double(cpu)}, {"bus", int(bus)}, {"device", int(device)}, {"function", int(function)},
        {"origin", "Imported CLI report; origin and platform identity unverified"}};
    return true;
}
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
    table_=new QTableWidget(0,8,this); table_->setObjectName("umcTable");
    table_->setHorizontalHeaderLabels({"Group","Field","ID","Offset","Bits","Register (hex)","Encoded value","State"});
    table_->setEditTriggers(QAbstractItemView::NoEditTriggers); table_->verticalHeader()->hide();
    table_->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    table_->horizontalHeader()->setStretchLastSection(true); layout->addWidget(table_,1);
    status_=new QLabel("Ready. No controller registers have been read.",this); status_->setWordWrap(true); status_->setObjectName("umcStatus");
    layout->addWidget(status_);
    connect(readButton,&QPushButton::clicked,this,[this] { read(); });
    connect(open,&QPushButton::clicked,this,[this] {
        const auto path=QFileDialog::getOpenFileName(this,"Open UMC snapshot",{},"UMC snapshot (*.json)"); if(path.isEmpty()) return;
        QFile file(path);
        if(!file.open(QIODevice::ReadOnly)) { clear(); status_->setText(file.errorString()); return; }
        const auto bytes=file.read(65537);
        if(file.error()!=QFileDevice::NoError) { clear(); status_->setText(file.errorString()); return; }
        loadCapture(bytes);
    });
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
void UmcPanel::clear() { table_->setRowCount(0); capture_.clear(); save_->setEnabled(false); }
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
    clear(); status_->setText("Invalid UMC snapshot. No hardware access performed.");
    if(bytes.isEmpty() || bytes.size()>65536) return false;
    QJsonParseError error; const auto doc=QJsonDocument::fromJson(bytes,&error);
    if(error.error!=QJsonParseError::NoError || !doc.isObject()) return false;
    auto obj=doc.object(); unsigned bank=0,slot=0;
    if (obj.contains("schema_version")) {
        QJsonObject converted;
        if (!cliCapture(obj, converted)) return false;
        obj = converted;
    }
    if(obj.value("format").toString()!="octool-amd-umc-v1" || !number(obj.value("bank"),22,bank) ||
        !number(obj.value("refresh_slot"),15,slot) || !obj.value("registers").isArray()) return false;
    const auto regs=obj.value("registers").toArray(); if(regs.isEmpty() || regs.size()>56) return false;
    std::vector<UmcRegister> registers;
    for(const auto &value:regs) {
        if(!value.isObject()) return false;
        const auto reg=value.toObject(); unsigned offset=0,raw=0;
        if(!number(reg.value("offset"),UINT32_MAX,offset) || !number(reg.value("value"),UINT32_MAX,raw)) return false;
        UmcRegister r; r.offset=offset; r.value=raw; registers.push_back(r);
    }
    const auto decoded=decodeAmdUmc(registers,slot); if(decoded.error) return false;
    present(decoded); capture_=QJsonDocument(obj).toJson(); save_->setEnabled(true);
    status_->setText(QString("Offline snapshot · bank %1 / refresh slot %2 · %3 registers. Platform identity and capture origin are unverified. Missing values remain blank.")
        .arg(bank).arg(slot).arg(regs.size())); return true;
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
            present(r.decoded); capture_=serialize(r); save_->setEnabled(true);
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
