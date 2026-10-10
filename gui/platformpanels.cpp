// SPDX-License-Identifier: GPL-2.0-only
#include "platformpanels.h"
#include "core/amd_smu.h"
#include "core/amd_topology.h"
#include "core/spd.h"
#include "platform/linux_inventory.h"
#include <QApplication>
#include <QClipboard>
#include <QComboBox>
#include <QDateTime>
#include <QFile>
#include <QFileDialog>
#include <QFormLayout>
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
QString failure(int error) {
    return QString("Failed (%1): %2").arg(error).arg(QString::fromLocal8Bit(std::strerror(-error)));
}
QLabel *description(const QString &text, QVBoxLayout *layout, QWidget *parent) {
    auto *label = new QLabel(text, parent); label->setWordWrap(true); label->setTextFormat(Qt::PlainText);
    layout->addWidget(label); return label;
}
QTableWidget *table(const QStringList &headers, QWidget *parent, QVBoxLayout *layout) {
    auto *t = new QTableWidget(0, headers.size(), parent); t->setHorizontalHeaderLabels(headers);
    t->setEditTriggers(QAbstractItemView::NoEditTriggers); t->verticalHeader()->hide();
    t->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    t->horizontalHeader()->setStretchLastSection(true); layout->addWidget(t, 1); return t;
}
void append(QTableWidget *t, const QStringList &cells) {
    const int row = t->rowCount(); t->insertRow(row);
    for (int col = 0; col < cells.size(); ++col) t->setItem(row, col, new QTableWidgetItem(cells[col]));
}
void copyButton(QWidget *parent, QHBoxLayout *buttons, QTableWidget *t, QLabel *status) {
    auto *copy = new QPushButton("Copy snapshot", parent); buttons->addWidget(copy);
    QObject::connect(copy, &QPushButton::clicked, parent, [t, status] {
        QString text = status->text()+"\n";
        for (int col = 0; col < t->columnCount(); ++col) text += t->horizontalHeaderItem(col)->text() + (col+1 == t->columnCount() ? "\n" : "\t");
        for (int row = 0; row < t->rowCount(); ++row)
            for (int col = 0; col < t->columnCount(); ++col) text += (t->item(row, col) ? t->item(row, col)->text() : "") + (col+1 == t->columnCount() ? "\n" : "\t");
        QApplication::clipboard()->setText(text);
    });
}
QLineEdit *edit(const char *name, const QString &value, QWidget *parent) {
    auto *e = new QLineEdit(value, parent); e->setObjectName(name); return e;
}
}
IntelControlsPanel::IntelControlsPanel(std::shared_ptr<HardwareAccess> access, QWidget *parent, unsigned cpu)
    : QWidget(parent), access_(std::move(access)), cancelled_(std::make_shared<std::atomic<bool>>(false)) {
    setObjectName("intelControls"); auto *layout = new QVBoxLayout(this);
    description("Intel Controls · package power limits and hardware performance requests\n"
        "HWP levels are performance indices, not MHz. BIOS locks and OS power management may restrict or overwrite changes. "
        "Voltage / VF, per-core turbo ratios and fabric controls are not yet recovered.", layout, this);
    auto *buttons = new QHBoxLayout; buttons->addWidget(new QLabel("Logical CPU", this));
    cpu_ = edit("intelCpu", QString::number(cpu), this); buttons->addWidget(cpu_);
    auto *read = new QPushButton("Read once", this); read->setObjectName("intelRead"); buttons->addWidget(read);
    layout->addLayout(buttons);
    table_ = table({"Control", "Value", "Unit", "MSR", "Raw (hex)", "State"}, this, layout); table_->setObjectName("intelTable");
    auto *modify = new QHBoxLayout; field_ = new QComboBox(this); field_->setObjectName("intelField"); modify->addWidget(field_, 2);
    value_ = edit("intelValue", "", this); value_->setPlaceholderText("New value"); modify->addWidget(value_);
    auto *write = new QPushButton("Apply selected…", this); write->setObjectName("intelApply"); modify->addWidget(write); layout->addLayout(modify);
    status_ = description("Ready. Read a snapshot before changing a control.", layout, this); status_->setObjectName("intelStatus");
    copyButton(this, buttons, table_, status_);
    connect(cpu_, &QLineEdit::textChanged, this, [this] { invalidate(); status_->setText("CPU changed. Read again."); });
    connect(read, &QPushButton::clicked, this, [this] { refresh(); });
    connect(write, &QPushButton::clicked, this, [this] { apply(); });
}
IntelControlsPanel::~IntelControlsPanel() { cancelled_->store(true); }
void IntelControlsPanel::invalidate() { snapshot_ = {}; table_->setRowCount(0); field_->clear(); value_->clear(); }
void IntelControlsPanel::present(const octool::core::IntelSnapshot &snapshot) {
    invalidate(); snapshot_ = snapshot;
    for (const auto &r : snapshot.readings) {
        append(table_, {QString::fromStdString(r.name), r.decoded ? QString::number(r.value, 'g', 12) : "—", QString::fromStdString(r.unit),
            hexValue(r.msr, 4), r.error ? "—" : hexValue(r.raw, 8), r.error ? failure(r.error) : r.editable ? "Editable" : "Read only / locked"});
        if (r.editable) field_->addItem(QString::fromStdString(r.name)+" ("+QString::fromStdString(r.unit)+")", int(r.field));
    }
    const auto &id = snapshot.identity;
    status_->setText(snapshot.error ? failure(snapshot.error) : QString("CPU %1 · family %2h / model %3h / stepping %4 · %5. %6")
        .arg(snapshot.cpu).arg(id.family, 0, 16).arg(id.model, 0, 16).arg(id.stepping)
        .arg(QDateTime::currentDateTime().toString(Qt::ISODate))
        .arg(snapshot.rapl ? "RAPL profile recognized." : "No verified RAPL profile for this CPU; no package limit access."));
}
void IntelControlsPanel::refresh() {
    quint64 cpu = 0; if (!parseNumber(cpu_->text(), 10, UINT32_MAX, cpu)) { status_->setText("Invalid logical CPU."); return; }
    invalidate(); setEnabled(false); status_->setText("Reading controls…");
    auto *watcher = new QFutureWatcher<octool::core::IntelSnapshot>(this);
    connect(watcher, &QFutureWatcher<octool::core::IntelSnapshot>::finished, this, [this, watcher] {
        present(watcher->result()); setEnabled(true); watcher->deleteLater();
    });
    auto access = access_; auto cancelled = cancelled_;
    watcher->setFuture(QtConcurrent::run([access, cancelled, cpu] {
        octool::core::IntelSnapshot result;
        const int e = access->transaction([&](octool::core::HardwareSession &s) { result = octool::core::readIntelControls(s, unsigned(cpu)); return result.error; }, 10000, cancelled.get());
        if (e) result.error = e; return result;
    }));
}
void IntelControlsPanel::apply() {
    bool ok = false; const double value = value_->text().toDouble(&ok);
    if (!ok || field_->currentIndex() < 0) { status_->setText("Read a snapshot and enter a valid value."); return; }
    const auto field = octool::core::IntelField(field_->currentData().toInt()); const auto snapshot = snapshot_;
    if (QMessageBox::question(this, "Apply Intel control", QString("CPU %1: set %2 to %3?\nPower and time values round down to the register encoding. A fresh comparison precedes the write.")
        .arg(snapshot.cpu).arg(field_->currentText()).arg(value, 0, 'g', 12), QMessageBox::Yes | QMessageBox::Cancel, QMessageBox::Cancel) != QMessageBox::Yes) return;
    setEnabled(false); status_->setText("Checking and submitting…");
    auto *watcher = new QFutureWatcher<octool::core::UpdateResult>(this);
    connect(watcher, &QFutureWatcher<octool::core::UpdateResult>::finished, this, [this, watcher] {
        const auto r = watcher->result(); invalidate(); setEnabled(true);
        status_->setText((r.error ? failure(r.error) : "Write submitted successfully (not read back).") +
            QString(" %1 register(s) submitted. Read a new snapshot.").arg(r.completed) +
            (r.error && r.writeAttempted ? " A failed write may have reached the hardware; it was not retried." : "")); watcher->deleteLater();
    });
    auto access = access_; auto cancelled = cancelled_;
    watcher->setFuture(QtConcurrent::run([access, cancelled, snapshot, field, value] {
        octool::core::UpdateResult result;
        const int e = access->transaction([&](octool::core::HardwareSession &s) { result = octool::core::applyIntelControl(s, snapshot, field, value); return result.error; }, 10000, cancelled.get());
        if (e) result.error = e; return result;
    }));
}
AmdTuningPanel::AmdTuningPanel(std::shared_ptr<HardwareAccess> access, QWidget *parent, unsigned cpu)
    : QWidget(parent), access_(std::move(access)), cancelled_(std::make_shared<std::atomic<bool>>(false)) {
    setObjectName("amdTuning"); auto *layout = new QVBoxLayout(this);
    description("AMD tuning / SMUIO · recovered BIOS mailbox\n"
        "Probe checks CPU family and the selected PCI device without sending a command. "
        "Arguments are firmware encodings in hex; physical units and board-specific presets are still being verified. "
        "Shimada control names come from the original program, with no target-machine validation yet.", layout, this);
    auto *form = new QFormLayout;
    cpu_ = edit("smuCpu", QString::number(cpu), this); form->addRow("Logical CPU (decimal)", cpu_);
    auto *bdf = new QHBoxLayout; bus_ = edit("smuBus", "00", this); device_ = edit("smuDevice", "00", this); function_ = edit("smuFunction", "0", this);
    for (auto *e : {bus_, device_, function_}) bdf->addWidget(e);
    form->addRow("PCI domain 0000 · bus / device / function (hex)", bdf);
    profile_ = new QComboBox(this); profile_->setObjectName("smuProfile");
    profile_->addItem("Shimada · AMD 1022:153a", int(octool::core::SmuProfile::Shimada));
    profile_->addItem("Phoenix · AMD 1022:14e8 · query only", int(octool::core::SmuProfile::Phoenix));
    profile_->addItem("GPT · AMD 1022:1122 · query only", int(octool::core::SmuProfile::Gpt)); form->addRow("Original profile", profile_);
    command_ = new QComboBox(this); command_->setObjectName("smuCommand");
    auto commands = [this] {
        command_->clear(); command_->addItem("Get SMU version (02)", 2); command_->addItem("Test message (01)", 1);
        if (profile_->currentData().toInt() != int(octool::core::SmuProfile::Shimada)) return;
        const std::pair<const char *, int> list[] = {{"Enable OC",0x24},{"Disable OC",0x25},{"All-core frequency",0x26},
            {"Per-core frequency",0x27},{"OC VID",0x28},{"Boost limit frequency",0x29},{"All-core boost limit",0x2b},
            {"OC capability",0x2c},{"FIT scalar",0x2f},{"Per-core PSM margin",0x35},{"All-core PSM margin",0x36},
            {"CPPC supported registers",0x39},{"CPPC nominal frequency",0x3a},{"TDC limit",0x3c},{"EDC limit",0x3d},
            {"PPT limit",0x3e},{"TjMax",0x3f},{"CPPC lowest frequency",0x40}};
        for (const auto &c : list) command_->addItem(QString::fromLatin1(c.first)+" ("+QString::number(c.second,16)+")", c.second);
    };
    commands(); connect(profile_, QOverload<int>::of(&QComboBox::currentIndexChanged), this, commands);
    form->addRow("Command", command_);
    auto *frequency = new QHBoxLayout;
    auto *ccd = edit("smuCcd", "0", this), *core = edit("smuCore", "0", this), *mhz = edit("smuMhz", "", this);
    for (const auto &item : {std::make_pair("CCD",ccd), std::make_pair("Core in CCD",core), std::make_pair("MHz",mhz)}) {
        frequency->addWidget(new QLabel(item.first,this)); frequency->addWidget(item.second);
    }
    auto *encode = new QPushButton("Prepare frequency",this); encode->setObjectName("smuEncodeFrequency"); frequency->addWidget(encode);
    form->addRow("Shimada frequency (decimal firmware indices)",frequency);
    auto *args = new QHBoxLayout;
    for (unsigned i = 0; i < 6; ++i) { args_[i] = edit(qPrintable(QString("smuArg%1").arg(i)), "0", this); args_[i]->setPlaceholderText(QString("Arg%1").arg(i)); args->addWidget(args_[i]); }
    form->addRow("Arg0 … Arg5 (32-bit hex)", args); layout->addLayout(form);
    auto *buttons = new QHBoxLayout; auto *probe = new QPushButton("Probe", this); probe->setObjectName("smuProbe"); buttons->addWidget(probe);
    auto *send = new QPushButton("Send command…", this); send->setObjectName("smuSend"); buttons->addWidget(send); layout->addLayout(buttons);
    auto *topology = new QPushButton("Read CPU topology",this); topology->setObjectName("smuTopology"); buttons->addWidget(topology);
    connect(topology,&QPushButton::clicked,this,[this] { readTopology(); });
    table_ = table({"Item", "Result"}, this, layout); table_->setObjectName("smuTable");
    status_ = description("Ready. No hardware access has been performed.", layout, this); status_->setObjectName("smuStatus");
    copyButton(this, buttons, table_, status_);
    connect(encode,&QPushButton::clicked,this,[this,ccd,core,mhz] {
        using namespace octool::core;
        quint64 ccdValue=0, coreValue=0, frequencyValue=0; std::uint32_t encoded=0;
        if (profile_->currentData().toInt()!=int(SmuProfile::Shimada) ||
            !parseNumber(ccd->text(),10,15,ccdValue) || !parseNumber(core->text(),10,7,coreValue) ||
            !parseNumber(mhz->text(),10,0xfffff,frequencyValue) ||
            encodeShimadaCoreFrequency(unsigned(ccdValue),unsigned(coreValue),unsigned(frequencyValue),encoded)) {
            status_->setText("Shimada only: CCD 0..15, core 0..7, MHz 1..1048575 (encoding limits). No command sent."); return;
        }
        command_->setCurrentIndex(command_->findData(0x27));
        args_[0]->setText(QString::number(encoded,16));
        for (unsigned i=1;i<6;++i) args_[i]->setText("0");
        table_->setRowCount(0);
        status_->setText("Frequency command prepared. CCD/core are firmware indices, not Linux CPU numbers; their presence is not verified. Review before sending.");
    });
    connect(probe, &QPushButton::clicked, this, [this] { submit(true); }); connect(send, &QPushButton::clicked, this, [this] { submit(false); });
    auto clear = [this] { table_->setRowCount(0); status_->setText("Inputs changed. Probe again to check the target."); };
    for (auto *e : {cpu_, bus_, device_, function_}) connect(e, &QLineEdit::textChanged, this, clear);
    connect(profile_, QOverload<int>::of(&QComboBox::currentIndexChanged), this, clear);
}
AmdTuningPanel::~AmdTuningPanel() { cancelled_->store(true); }
void AmdTuningPanel::readTopology() {
    using namespace octool::core;
    quint64 cpu=0;
    if(!parseNumber(cpu_->text(),10,UINT32_MAX,cpu)) { table_->setRowCount(0); status_->setText("Invalid logical CPU."); return; }
    table_->setRowCount(0); setEnabled(false); status_->setText("Reading selected CPU topology…");
    auto *watcher=new QFutureWatcher<AmdTopology>(this);
    connect(watcher,&QFutureWatcher<AmdTopology>::finished,this,[this,watcher] {
        const auto t=watcher->result(); setEnabled(true);
        if(t.error) status_->setText(failure(t.error)+" Extended AMD topology is unavailable or inconsistent; no firmware target inferred.");
        else {
            append(table_,{"Logical CPU",QString::number(t.cpu)});
            append(table_,{"Extended APIC ID",hexValue(t.apicId,4)});
            append(table_,{"CPUID socket ID",QString::number(t.socketId)});
            append(table_,{"CPUID CCD within socket",QString::number(t.ccdInSocket)});
            append(table_,{"CPUID core within CCD",QString::number(t.coreInCcd)});
            append(table_,{"Thread within core",QString::number(t.threadInCore)});
            for(const auto &l:t.levels) append(table_,{QString("CPUID level %1").arg(l.type),
                QString("%1 logical processors · APIC shift %2 · global ID %3").arg(l.logicalCount).arg(l.shift).arg(l.globalId)});
            status_->setText("CPUID topology read. Sparse IDs are preserved. Firmware CCD/core mapping is not assumed; no command was prepared or sent.");
        }
        watcher->deleteLater();
    });
    const auto access=access_; const auto cancelled=cancelled_;
    watcher->setFuture(QtConcurrent::run([access,cancelled,cpu] {
        AmdTopology out;
        const int error=access->transaction([&](HardwareSession &s) { out=readAmdTopology(s,unsigned(cpu)); return out.error; },10000,cancelled.get());
        if(error) out.error=error;
        return out;
    }));
}
void AmdTuningPanel::submit(bool probeOnly) {
    using namespace octool::core;
    SmuTarget target; target.profile = SmuProfile(profile_->currentData().toInt());
    quint64 cpu, bus, device, function;
    if (!parseNumber(cpu_->text(),10,UINT32_MAX,cpu) || !parseNumber(bus_->text(),16,255,bus) ||
        !parseNumber(device_->text(),16,31,device) || !parseNumber(function_->text(),16,7,function)) { status_->setText("Invalid CPU or PCI target."); return; }
    target.cpu = unsigned(cpu); target.bus = unsigned(bus); target.device = unsigned(device); target.function = unsigned(function);
    SmuCommand command; command.message = std::uint32_t(command_->currentData().toUInt());
    QString arguments;
    for (unsigned i = 0; i < 6; ++i) { quint64 value;
        if (!parseNumber(args_[i]->text(),16,UINT32_MAX,value)) { status_->setText("Invalid 32-bit hexadecimal argument."); return; }
        command.args[i] = std::uint32_t(value); arguments += hexValue(value,4)+" ";
    }
    if (!probeOnly && QMessageBox::question(this,"Send AMD firmware command",QString("%1\nTarget 0000:%2:%3.%4\n%5\nArguments: %6\nThis sends a firmware command; no automatic retry or rollback.")
        .arg(profile_->currentText()).arg(bus,2,16,QChar('0')).arg(device,2,16,QChar('0')).arg(function,0,16)
        .arg(command_->currentText()).arg(arguments),QMessageBox::Yes|QMessageBox::Cancel,QMessageBox::Cancel) != QMessageBox::Yes) return;
    struct Result { SmuProbe probe; SmuReply reply; int error = 0; };
    table_->setRowCount(0); setEnabled(false); status_->setText(probeOnly ? "Checking identity…" : "Waiting for firmware…");
    auto *watcher = new QFutureWatcher<Result>(this);
    connect(watcher,&QFutureWatcher<Result>::finished,this,[this,watcher,probeOnly] {
        const auto r=watcher->result(); const auto &id=r.probe.identity;
        append(table_,{"CPU",QString("Family %1h / model %2h / stepping %3").arg(id.family,0,16).arg(id.model,0,16).arg(id.stepping)});
        append(table_,{"PCI identity",hexValue(r.probe.pciIdentity,4)});
        if (!probeOnly) {
            append(table_,{"Firmware response",hexValue(r.reply.response,4)});
            if (!r.error) for (unsigned i=0;i<6;++i) append(table_,{QString("Arg%1").arg(i),hexValue(r.reply.args[i],4)});
        }
        status_->setText((r.error ? failure(r.error) : probeOnly ? "CPU family and PCI ID match the selected original profile. No command sent." : "Firmware accepted the command (response 1). Hardware effect is not measured.")+
            QString(r.error && r.reply.messageAttempted ? " Command completion may be unknown; no retry performed." : ""));
        setEnabled(true); watcher->deleteLater();
    });
    auto access=access_; auto cancelled=cancelled_;
    watcher->setFuture(QtConcurrent::run([access,cancelled,target,command,probeOnly] {
        Result out; out.error=access->transaction([&](HardwareSession &s) {
            out.probe=probeSmu(s,target); if (out.probe.error) return out.probe.error;
            if (!probeOnly) { out.reply=sendSmu(s,target,command); return out.reply.error; } return 0;
        },10000,cancelled.get()); return out;
    }));
}
MemoryBoardPanel::MemoryBoardPanel(QWidget *parent) : QWidget(parent) {
    setObjectName("memoryBoard"); auto *layout=new QVBoxLayout(this);
    description("Memory and motherboard · BIOS identity, kernel sensors and DDR4 / DDR5 SPD\n"
        "SPD describes the module, not the currently trained timings. Sensors depend on the installed kernel driver. "
        "Base CRC is checked when the full block is available. Timing writes, PMIC / VRM controls and board-specific clocks are not yet recovered.",layout,this);
    auto *buttons=new QHBoxLayout; auto *read=new QPushButton("Read once",this); read->setObjectName("inventoryRead"); buttons->addWidget(read);
    auto *open=new QPushButton("Open SPD dump…",this); open->setObjectName("spdOpen"); buttons->addWidget(open); layout->addLayout(buttons);
    table_=table({"Device", "Item", "Value", "Unit", "Status", "Source"},this,layout); table_->setObjectName("inventoryTable");
    status_=description("Ready. No inventory scan has been performed.",layout,this); status_->setObjectName("inventoryStatus");
    copyButton(this,buttons,table_,status_);
    connect(read,&QPushButton::clicked,this,[this] { refresh(); });
    connect(open,&QPushButton::clicked,this,[this] {
        const QString path=QFileDialog::getOpenFileName(this,"Open binary SPD dump",{},"SPD dump (*.bin *.spd);;All files (*)"); if(path.isEmpty()) return;
        QFile f(path); if(!f.open(QIODevice::ReadOnly)) { status_->setText(f.errorString()); return; }
        const QByteArray bytes=f.read(4097); if(bytes.size()>4096 || f.error()!=QFileDevice::NoError) { status_->setText("SPD dump exceeds 4096 bytes or could not be read."); return; }
        table_->setRowCount(0); showSpd(path,bytes); status_->setText("Offline SPD file: "+path+" · "+QDateTime::currentDateTime().toString(Qt::ISODate));
    });
}
void MemoryBoardPanel::showSpd(const QString &path,const QByteArray &bytes,const QString &error) {
    if(!error.isEmpty()) { append(table_,{"SPD","EEPROM","","",error,path}); return; }
    const auto *first=reinterpret_cast<const std::uint8_t *>(bytes.constData());
    const auto decoded=octool::core::decodeSpd(std::vector<std::uint8_t>(first,first+bytes.size()));
    const QString state=decoded.error ? failure(decoded.error) : decoded.crcChecked ? "Base CRC valid" : "CRC not verified";
    append(table_,{"SPD","Captured bytes",QString::number(bytes.size()),"bytes",state,path});
    for(const auto &field:decoded.fields) append(table_,{"SPD",QString::fromStdString(field.name),QString::fromStdString(field.value),"",state,path});
}
void MemoryBoardPanel::refresh() {
    table_->setRowCount(0); setEnabled(false); status_->setText("Reading BIOS, hwmon and bound SPD devices…");
    auto *watcher=new QFutureWatcher<octool::platform::InventorySnapshot>(this);
    connect(watcher,&QFutureWatcher<octool::platform::InventorySnapshot>::finished,this,[this,watcher] {
        const auto r=watcher->result(); for(const auto &row:r.rows) append(table_,{row.group,row.name,row.value,row.unit,row.status,row.source});
        for(const auto &spd:r.spd) showSpd(spd.path,spd.bytes,spd.error);
        if(r.spd.isEmpty()) append(table_,{"SPD","EEPROM","","","No ee1004 / spd5118 device exposed; an offline dump can be opened.","/sys/bus/i2c/drivers"});
        status_->setText(QString("Snapshot: %1 · %2 sensor / BIOS rows, %3 SPD device(s). Per-row errors are retained.")
            .arg(QDateTime::currentDateTime().toString(Qt::ISODate)).arg(r.rows.size()).arg(r.spd.size()));
        setEnabled(true); watcher->deleteLater();
    });
    watcher->setFuture(QtConcurrent::run([] { return octool::platform::linuxInventory(); }));
}
