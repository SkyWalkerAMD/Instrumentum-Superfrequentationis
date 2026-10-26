// SPDX-License-Identifier: GPL-2.0-only
#include "platformpanels.h"
#include "core/amd_smu.h"
#include "core/amd_curve.h"
#include "core/amd_topology.h"
#include "core/spd.h"
#include "platform/linux_inventory.h"
#include "platform/linux_inventory_native.h"
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
        "Package HWP edits change shared hints for the physical package containing the selected CPU; selection flags remain unchanged. "
        "Package and PECI rows are stored hints; selection flags are sampled separately. PECI override blocks the affected edit. "
        "HWP history records events since reset or clear; it does not show current performance. "
        "Maximum non-turbo ratio is a fixed platform value. Package events show current status and history since the last reset or clear. "
        "Energy counters wrap; joules are counter readings, not watts or lifetime totals. DRAM telemetry requires BIOS RAPL mode 0. "
        "PL4 is a peak power limit on 06_B7; zero is shown as raw configuration and cannot be requested. "
        "Core/cache, V/F points, turbo groups and Ring/LLC have separate tabs.", layout, this);
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
void IntelControlsPanel::invalidate() { ++revision_; snapshot_ = {}; table_->setRowCount(0); field_->clear(); value_->clear(); }
void IntelControlsPanel::present(const octool::core::IntelSnapshot &snapshot) {
    invalidate(); snapshot_ = snapshot;
    for (const auto &r : snapshot.readings) {
        QString value=r.decoded ? QString::number(r.value, 'g', 12) : "—";
        if (r.decoded && r.msr==0x1b1 && r.unit=="0 / 1")
            value=QString::fromStdString(r.name).endsWith(" history") ? (r.value?"Recorded":"Not recorded") : (r.value?"Active":"Inactive");
        if (r.decoded && r.msr==0x777 && r.unit=="0 / 1") value=r.value?"Recorded":"Not recorded";
        append(table_, {QString::fromStdString(r.name), value, QString::fromStdString(r.unit),
            hexValue(r.msr, 4), r.error ? "—" : hexValue(r.raw, 8), r.error ? failure(r.error) : r.editable && !snapshot.error ? "Editable" : "Read only / locked"});
        if (r.editable && !snapshot.error) field_->addItem(QString::fromStdString(r.name)+" ("+QString::fromStdString(r.unit)+")", int(r.field));
    }
    const auto &id = snapshot.identity;
    status_->setText(snapshot.error ? failure(snapshot.error) : QString("CPU %1 · family %2h / model %3h / stepping %4 · %5. %6")
        .arg(snapshot.cpu).arg(id.family, 0, 16).arg(id.model, 0, 16).arg(id.stepping)
        .arg(QDateTime::currentDateTime().toString(Qt::ISODate))
        .arg(snapshot.rapl ? "RAPL profile recognized." : "No verified RAPL profile for this CPU; no package limit access."));
}
void IntelControlsPanel::refresh() {
    invalidate();
    quint64 cpu = 0; if (!parseNumber(cpu_->text(), 10, UINT32_MAX, cpu)) { status_->setText("Invalid logical CPU."); return; }
    setEnabled(false); status_->setText("Reading controls…"); const auto revision=revision_;
    auto *watcher = new QFutureWatcher<octool::core::IntelSnapshot>(this);
    connect(watcher, &QFutureWatcher<octool::core::IntelSnapshot>::finished, this, [this, watcher, revision] {
        if (revision==revision_) present(watcher->result());
        else { invalidate(); status_->setText("CPU changed during the read; result discarded. Read again."); }
        setEnabled(true); watcher->deleteLater();
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
    const auto preparedRevision=revision_;
    QString confirmation=QString("CPU %1: set %2 to %3?\nPower and time values round down to the register encoding. An HWP window of 0 lets hardware choose. A fresh comparison precedes the write.")
        .arg(snapshot.cpu).arg(field_->currentText()).arg(value, 0, 'g', 12);
    if (octool::core::isIntelHwpPackageField(field)) confirmation += "\nThis changes a shared package hint and can affect other logical CPUs using package control. The value is checked against the selected CPU's capabilities; selection flags are preserved.";
    if (QMessageBox::question(this, "Apply Intel control", confirmation, QMessageBox::Yes | QMessageBox::Cancel, QMessageBox::Cancel) != QMessageBox::Yes) return;
    if (preparedRevision!=revision_) { status_->setText("CPU changed. Read again before applying a control."); return; }
    invalidate(); setEnabled(false); status_->setText("Checking, applying and reading back…"); const auto revision=revision_;
    auto *watcher = new QFutureWatcher<octool::core::IntelControlUpdate>(this);
    connect(watcher, &QFutureWatcher<octool::core::IntelControlUpdate>::finished, this, [this, watcher, revision, snapshot] {
        const auto r = watcher->result(); setEnabled(true);
        if (revision!=revision_) {
            invalidate(); status_->setText(QString("CPU changed during the operation for CPU %1; result discarded. ").arg(snapshot.cpu)+
                (r.writeAttempted?"A settings write was attempted. Read the original CPU again.":"No settings write was attempted."));
        } else if (!r.error && r.verified) {
            present(r.after);
            status_->setText(QString("CPU %1: %2 Full register readback verified; configured value %3. OS power management may later change it.")
                .arg(r.after.cpu).arg(r.unchanged ? "Already set; no write needed." : "Applied.")
                .arg(r.readbackValue, 0, 'g', 12));
        } else {
            invalidate();
            status_->setText(failure(r.error ? r.error : -EIO) + " Read a new snapshot." +
                (r.writeAttempted ? " The write may have taken effect; readback was not verified. No retry or rollback was attempted." : " No write was attempted."));
        }
        watcher->deleteLater();
    });
    auto access = access_; auto cancelled = cancelled_;
    watcher->setFuture(QtConcurrent::run([access, cancelled, snapshot, field, value] {
        octool::core::IntelControlUpdate result;
        const int e = access->transaction([&](octool::core::HardwareSession &s) { result = octool::core::applyIntelControl(s, snapshot, field, value); return result.error; }, 10000, cancelled.get());
        if (e) result.error = e; return result;
    }));
}
AmdTuningPanel::AmdTuningPanel(std::shared_ptr<HardwareAccess> access, QWidget *parent, unsigned cpu)
    : QWidget(parent), access_(std::move(access)), cancelled_(std::make_shared<std::atomic<bool>>(false)) {
    setObjectName("amdTuning"); auto *layout = new QVBoxLayout(this);
    description("AMD tuning · firmware commands and per-core / CCD curve controls\n"
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
    auto *curve = new QHBoxLayout;
    curveCcd_ = edit("curveCcd", "", this); curveCore_ = edit("curveCore", "", this);
    curveCcd_->setPlaceholderText("CCD 0..15"); curveCore_->setPlaceholderText("Cores: 0 or 0,2,7");
    curve->addWidget(curveCcd_); curve->addWidget(curveCore_);
    auto *readCurveButton = new QPushButton("Read curve", this); readCurveButton->setObjectName("curveRead");
    curve->addWidget(readCurveButton);
    form->addRow("Shimada curve (decimal firmware indices)", curve);
    connect(readCurveButton, &QPushButton::clicked, this, [this] { readCurve(); });
    auto *curveEdit = new QHBoxLayout;
    curveMargin_ = edit("curveMargin", "", this); curveMargin_->setPlaceholderText("CO count -50..50");
    curveEdit->addWidget(curveMargin_);
    auto *applyCurveButton = new QPushButton("Apply curve to selected cores…", this); applyCurveButton->setObjectName("curveApply");
    curveEdit->addWidget(applyCurveButton); form->addRow("Read selected cores first · counts, not mV", curveEdit);
    connect(applyCurveButton, &QPushButton::clicked, this, [this] { applyCurve(); });
    auto *allCurve = new QHBoxLayout;
    allCurveMargin_ = edit("allCurveMargin", "", this); allCurveMargin_->setPlaceholderText("CO count -50..50");
    allCurve->addWidget(allCurveMargin_);
    auto *readAllCurveCaps = new QPushButton("Read capability", this); readAllCurveCaps->setObjectName("allCurveCapsRead"); allCurve->addWidget(readAllCurveCaps);
    auto *applyAllCurve = new QPushButton("Apply curve to all cores…", this); applyAllCurve->setObjectName("allCurveApply"); allCurve->addWidget(applyAllCurve);
    form->addRow("Shimada all-core curve · acknowledgement only", allCurve);
    connect(readAllCurveCaps, &QPushButton::clicked, this, [this] { readPboCapabilities(); });
    connect(applyAllCurve, &QPushButton::clicked, this, [this] { applyAllCoreCurve(); });
    auto *scalar = new QHBoxLayout;
    pboScalar_ = edit("pboScalar", "", this); pboScalar_->setPlaceholderText("Integer multiplier 1..10");
    scalar->addWidget(pboScalar_);
    auto *readScalar = new QPushButton("Read scalar", this); readScalar->setObjectName("pboScalarRead"); scalar->addWidget(readScalar);
    auto *applyScalar = new QPushButton("Apply scalar…", this); applyScalar->setObjectName("pboScalarApply"); scalar->addWidget(applyScalar);
    form->addRow("Shimada PBO scalar · read before applying", scalar);
    connect(readScalar, &QPushButton::clicked, this, [this] { readPboScalar(); });
    connect(applyScalar, &QPushButton::clicked, this, [this] { applyPboScalar(); });
    auto *fmax = new QHBoxLayout;
    pboFmax_ = edit("pboFmax", "", this); pboFmax_->setPlaceholderText("Requested ceiling in MHz"); fmax->addWidget(pboFmax_);
    auto *caps = new QPushButton("Read PBO capability", this); caps->setObjectName("pboCapsRead"); fmax->addWidget(caps);
    auto *applyFmax = new QPushButton("Apply Fmax…", this); applyFmax->setObjectName("pboFmaxApply"); fmax->addWidget(applyFmax);
    form->addRow("Shimada Fmax · command acknowledgement only", fmax);
    connect(caps, &QPushButton::clicked, this, [this] { readPboCapabilities(); });
    connect(applyFmax, &QPushButton::clicked, this, [this] { applyPboFmax(); });
    auto *limits = new QHBoxLayout;
    pboLimit_ = new QComboBox(this); pboLimit_->setObjectName("pboLimit");
    pboLimit_->addItem("PPT (W)", int(octool::core::AmdPboLimit::Ppt));
    pboLimit_->addItem("TDC (A)", int(octool::core::AmdPboLimit::Tdc));
    pboLimit_->addItem("EDC (A)", int(octool::core::AmdPboLimit::Edc));
    pboLimit_->addItem("Temperature (°C)", int(octool::core::AmdPboLimit::Temperature));
    limits->addWidget(pboLimit_);
    pboLimitValue_ = edit("pboLimitValue", "", this); pboLimitValue_->setPlaceholderText("Integer 1..2000 W"); limits->addWidget(pboLimitValue_);
    auto *applyLimit = new QPushButton("Apply limit…", this); applyLimit->setObjectName("pboLimitApply"); limits->addWidget(applyLimit);
    form->addRow("Shimada PBO limits · read capability first", limits);
    connect(pboLimit_, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this] {
        const auto kind = octool::core::AmdPboLimit(pboLimit_->currentData().toInt());
        pboLimitValue_->clear();
        pboLimitValue_->setPlaceholderText(QString("Integer 1..%1 %2").arg(octool::core::amdPboLimitMaximum(kind)).arg(octool::core::amdPboLimitUnit(kind)));
    });
    connect(applyLimit, &QPushButton::clicked, this, [this] { applyPboLimit(); });
    auto *ocMode = new QHBoxLayout;
    ocMode_ = new QComboBox(this); ocMode_->setObjectName("ocMode");
    ocMode_->addItem("Disabled",0); ocMode_->addItem("Enabled",1); ocMode->addWidget(ocMode_);
    auto *ocRead = new QPushButton("Read OC capability",this); ocRead->setObjectName("ocCapsRead"); ocMode->addWidget(ocRead);
    auto *ocApply = new QPushButton("Apply mode…",this); ocApply->setObjectName("ocModeApply"); ocMode->addWidget(ocApply);
    form->addRow("Requested manual OC mode · read first",ocMode);
    connect(ocRead,&QPushButton::clicked,this,[this]{readPboCapabilities();});
    connect(ocApply,&QPushButton::clicked,this,[this]{applyOcMode();});
    auto *ocFrequency = new QHBoxLayout;
    ocFrequencyScope_ = new QComboBox(this); ocFrequencyScope_->setObjectName("ocFrequencyScope");
    ocFrequencyScope_->addItem("One firmware core",int(octool::core::AmdOcFrequencyScope::Core));
    ocFrequencyScope_->addItem("All cores",int(octool::core::AmdOcFrequencyScope::AllCores));
    ocFrequencyScope_->addItem("Selected cores in CCD",int(octool::core::AmdOcFrequencyScope::Cores)); ocFrequency->addWidget(ocFrequencyScope_);
    ocCcd_=edit("ocCcd","",this); ocCcd_->setPlaceholderText("CCD 0..15"); ocFrequency->addWidget(ocCcd_);
    ocCore_=edit("ocCore","",this); ocCore_->setPlaceholderText("Core 0..7"); ocFrequency->addWidget(ocCore_);
    ocMHz_=edit("ocMHz","",this); ocMHz_->setPlaceholderText("MHz"); ocFrequency->addWidget(ocMHz_);
    auto *ocFrequencyApply=new QPushButton("Apply frequency…",this); ocFrequencyApply->setObjectName("ocFrequencyApply"); ocFrequency->addWidget(ocFrequencyApply);
    form->addRow("Manual OC frequency · mode must be enabled",ocFrequency);
    connect(ocFrequencyScope_,QOverload<int>::of(&QComboBox::currentIndexChanged),this,[this]{
        const auto scope=octool::core::AmdOcFrequencyScope(ocFrequencyScope_->currentData().toInt());
        const bool indexed=scope!=octool::core::AmdOcFrequencyScope::AllCores;
        ocCore_->clear();if(!indexed)ocCcd_->clear();
        ocCore_->setPlaceholderText(scope==octool::core::AmdOcFrequencyScope::Cores?"Cores: 0,2,7":"Core 0..7");
        ocCcd_->setEnabled(indexed);ocCore_->setEnabled(indexed);
    });
    connect(ocFrequencyApply,&QPushButton::clicked,this,[this]{applyOcFrequency();});
    auto *rails = new QHBoxLayout;
    auto *rail0 = edit("pboRail0", "", this); rail0->setPlaceholderText("Rail 0 mV"); rails->addWidget(rail0);
    auto *rail1 = edit("pboRail1", "", this); rail1->setPlaceholderText("Rail 1 mV"); rails->addWidget(rail1);
    auto *encodeRails = new QPushButton("Preview encoding", this); encodeRails->setObjectName("pboRailsEncode"); rails->addWidget(encodeRails);
    form->addRow("Legacy rails · offline preview only", rails);
    connect(encodeRails, &QPushButton::clicked, this, [this, rail0, rail1] {
        quint64 a = 0, b = 0;
        if (!parseNumber(rail0->text(), 10, 2800, a) || !parseNumber(rail1->text(), 10, 2800, b)) {
            status_->setText("Enter both rail inputs. Nonzero values require 250..2800 mV in 5 mV steps; zero mirrors the other rail. Encoding only."); return;
        }
        const auto r = octool::core::encodeLegacyAmdPboRails(unsigned(a), unsigned(b));
        if (r.error) { status_->setText("Invalid rail encoding: use zero or 250..2800 mV in exact 5 mV steps. These are encoding limits, not recommended voltages."); return; }
        invalidateCurve();
        append(table_, {"Rail encoding", r.empty ? "Both zero: no command" : hexValue(r.argument, 4)});
        append(table_, {"Effective inputs (mV)", QString("%1 / %2").arg(r.rail0Millivolts).arg(r.rail1Millivolts)});
        append(table_, {"Zero input mirrored", r.mirrored ? "Yes" : "No"});
        status_->setText("Offline preview. No hardware access. Physical rail mapping and the dual-rail setting protocol remain unverified; sending is unavailable.");
    });
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
    auto clear = [this] { invalidateCurve(); status_->setText("Inputs changed. Previous results cleared."); };
    for (auto *e : {cpu_, bus_, device_, function_, curveCcd_, curveCore_}) connect(e, &QLineEdit::textChanged, this, clear);
    connect(profile_, QOverload<int>::of(&QComboBox::currentIndexChanged), this, clear);
}
AmdTuningPanel::~AmdTuningPanel() { cancelled_->store(true); }
void AmdTuningPanel::invalidateCurve() { ++curveRevision_; curveSnapshot_ = {}; pboSnapshot_ = {}; pboCaps_ = {}; table_->setRowCount(0); }
void AmdTuningPanel::readPboCapabilities() {
    using namespace octool::core;
    invalidateCurve();
    quint64 cpu = 0, bus = 0, device = 0, function = 0;
    if (profile_->currentData().toInt() != int(SmuProfile::Shimada) ||
        !parseNumber(cpu_->text(), 10, UINT32_MAX, cpu) || !parseNumber(bus_->text(), 16, 0, bus) ||
        !parseNumber(device_->text(), 16, 0, device) || !parseNumber(function_->text(), 16, 0, function)) {
        status_->setText("PBO capability requires Shimada at 0000:00:00.0 and a valid logical CPU. No query sent."); return;
    }
    SmuTarget target; target.cpu = unsigned(cpu); target.profile = SmuProfile::Shimada;
    const auto revision = curveRevision_; setEnabled(false); status_->setText("Reading PBO capability…");
    auto *watcher = new QFutureWatcher<AmdPboCapabilities>(this);
    connect(watcher, &QFutureWatcher<AmdPboCapabilities>::finished, this, [this, watcher, target, revision] {
        const auto r = watcher->result();
        if (revision != curveRevision_) status_->setText("Inputs changed while reading. Read PBO capability again.");
        else if (r.error || !r.valid) status_->setText(failure(r.error ? r.error : -EIO));
        else {
            pboCaps_ = r; pboTarget_ = target;
            append(table_, {"PBO capability raw", hexValue(r.raw, 4)});
            append(table_, {"PBO available", r.raw & 4u ? "Yes" : "No"});
            append(table_, {"Manual OC enabled", r.raw & 1u ? "Yes" : "No"});
            append(table_, {"Current Fmax", "Unavailable: no verified readback"});
            append(table_, {"Power limits available", r.raw & 2u ? "Yes" : "No"});
            append(table_, {"PPT / TDC / EDC / temperature commands", amdPboLimitsAvailable(r.raw) ? "Available; effective limits unverified" : "Unavailable"});
            append(table_, {"Manual OC frequency commands", r.raw & 1u ? "Mode enabled; frequency readback unavailable" : "Mode disabled; explicit mode change required"});
            append(table_, {"All-core curve command", amdAllCoreCurveAvailable(r.raw) ? "Available; per-core readback unverified" : "Unavailable"});
            status_->setText((amdPboFmaxAvailable(r.raw) ?
                "Fmax command available. Enter a MHz ceiling. Firmware acceptance does not verify the effective limit or operating frequency." :
                "Fmax unavailable: PBO must be available and manual OC disabled. No mode change was requested.") +
                QString(amdPboLimitsAvailable(r.raw) ? " PBO limit commands available; select one limit to change." : " PBO limit commands unavailable.") +
                QString(r.raw & 1u ? " Manual OC is enabled." : " Manual OC is disabled."));
        }
        setEnabled(true); watcher->deleteLater();
    });
    const auto access = access_; const auto cancelled = cancelled_;
    watcher->setFuture(QtConcurrent::run([access, cancelled, target] {
        AmdPboCapabilities out;
        const int error = access->transaction([&](HardwareSession &s) { out = readShimadaPboCapabilities(s, target); return out.error; }, 10000, cancelled.get());
        if (error) { out.error = error; out.valid = false; } return out;
    }));
}
void AmdTuningPanel::applyAllCoreCurve() {
    using namespace octool::core;
    if (!pboCaps_.valid || pboCaps_.error || !amdAllCoreCurveAvailable(pboCaps_.raw)) {
        status_->setText("Read capability first. All-core curve requires PBO available and manual OC disabled."); return;
    }
    const auto input = allCurveMargin_->text(); bool ok = false;
    const int margin = input.toInt(&ok, 10); std::uint32_t argument = 0;
    if (!ok || encodeShimadaAllCoreCurve(margin, argument)) {
        status_->setText("All-core curve requires an integer CO count -50..50, not mV."); return;
    }
    const auto target = pboTarget_; const auto before = pboCaps_; const auto prepared = curveRevision_;
    if (QMessageBox::question(this, "Apply AMD all-core curve",
        QString("CPU %1 · firmware all-core scope\nNew CO count: %2 (not mV)\n"
                "This overwrites individual core curve settings and can affect stability. "
                "Only firmware acceptance is checked; the affected physical cores and their final values are unverified. "
                "No automatic mode change, retry or rollback. Apply to all cores?").arg(target.cpu).arg(margin),
        QMessageBox::Yes|QMessageBox::Cancel, QMessageBox::Cancel) != QMessageBox::Yes) return;
    if (prepared != curveRevision_ || input != allCurveMargin_->text()) {
        invalidateCurve(); status_->setText("Inputs changed. Read capability again."); return;
    }
    invalidateCurve(); const auto revision = curveRevision_; setEnabled(false);
    status_->setText("Checking capability and submitting all-core curve count…");
    auto *watcher = new QFutureWatcher<AmdCurveAllUpdate>(this);
    connect(watcher, &QFutureWatcher<AmdCurveAllUpdate>::finished, this, [this, watcher, revision, target] {
        const auto r = watcher->result();
        if (revision == curveRevision_) {
            append(table_, {"Scope", "Firmware all cores"});
            append(table_, {"Requested CO count", QString::number(r.requestedMargin)});
            append(table_, {"Setting command", r.accepted ? "Firmware accepted" : r.writeAttempted ? "Attempted; not confirmed" : "Not sent"});
            append(table_, {"Firmware response", hexValue(r.response, 4)});
            append(table_, {"Per-core readback", "Unavailable / not verified"});
            append(table_, {"Affected physical core count", "Unknown"});
        }
        status_->setText((r.error ? failure(r.error) + (r.writeAttempted ? " Curve settings may have changed. No automatic retry." : " No setting sent.") :
            QString("Firmware accepted all-core CO count %1. Per-core values remain unverified.").arg(r.requestedMargin)) +
            (revision == curveRevision_ ? " Read capability again before another edit." : QString(" Inputs changed; result belongs to CPU %1.").arg(target.cpu)));
        setEnabled(true); watcher->deleteLater();
    });
    const auto access = access_; const auto cancelled = cancelled_;
    watcher->setFuture(QtConcurrent::run([access, cancelled, target, before, margin] {
        AmdCurveAllUpdate out;
        const int error = access->transaction([&](HardwareSession &s) { out = setShimadaAllCoreCurve(s,target,before.raw,margin); return out.error; },10000,cancelled.get());
        if (error) out.error = error;
        return out;
    }));
}
void AmdTuningPanel::applyPboFmax() {
    using namespace octool::core;
    if (!pboCaps_.valid || pboCaps_.error || !amdPboFmaxAvailable(pboCaps_.raw)) {
        status_->setText("Read PBO capability first. Fmax requires PBO available and manual OC disabled."); return;
    }
    quint64 mhz = 0;
    if (!parseNumber(pboFmax_->text(), 10, 0xfffff, mhz) || !mhz) {
        status_->setText("Fmax requires integer MHz 1..1048575. This is the encoding range, not a recommended operating range."); return;
    }
    const auto target = pboTarget_; const auto before = pboCaps_; const auto prepared = curveRevision_;
    const auto input = pboFmax_->text();
    if (QMessageBox::question(this, "Apply AMD Fmax ceiling",
        QString("CPU %1 · Shimada shared Fmax ceiling: %2 MHz\n"
                "Changing this limit can affect voltage, power, heat and stability. Firmware acceptance can be checked; actual Fmax cannot be read back. "
                "No automatic mode change, retry or rollback. Apply this ceiling?").arg(target.cpu).arg(mhz),
        QMessageBox::Yes|QMessageBox::Cancel, QMessageBox::Cancel) != QMessageBox::Yes) return;
    if (prepared != curveRevision_ || input != pboFmax_->text()) { invalidateCurve(); status_->setText("Inputs changed. Read PBO capability again."); return; }
    invalidateCurve(); const auto revision = curveRevision_; setEnabled(false); status_->setText("Checking capability and submitting Fmax ceiling…");
    auto *watcher = new QFutureWatcher<AmdPboFmaxUpdate>(this);
    connect(watcher, &QFutureWatcher<AmdPboFmaxUpdate>::finished, this, [this, watcher, revision, target] {
        const auto r = watcher->result();
        if (revision == curveRevision_) {
            append(table_, {"Requested Fmax ceiling (MHz)", QString::number(r.requestedMHz)});
            append(table_, {"Setting command", r.accepted ? "Firmware accepted" : r.writeAttempted ? "Attempted; not confirmed" : "Not sent"});
            append(table_, {"Firmware response", hexValue(r.response, 4)});
            append(table_, {"Actual Fmax readback", "Unavailable / not verified"});
        }
        status_->setText((r.error ? failure(r.error) + (r.writeAttempted ? " The limit may have changed. No automatic retry." : " No setting sent.") :
            QString("Firmware accepted %1 MHz. Actual Fmax and operating frequency remain unverified.").arg(r.requestedMHz)) +
            (revision == curveRevision_ ? " Read capability again before another edit." : QString(" Inputs changed; result belongs to CPU %1.").arg(target.cpu)));
        setEnabled(true); watcher->deleteLater();
    });
    const auto access = access_; const auto cancelled = cancelled_;
    watcher->setFuture(QtConcurrent::run([access, cancelled, target, before, mhz] {
        AmdPboFmaxUpdate out;
        const int error = access->transaction([&](HardwareSession &s) { out = setShimadaPboFmax(s, target, before.raw, unsigned(mhz)); return out.error; }, 10000, cancelled.get());
        if (error) out.error = error;
        return out;
    }));
}
void AmdTuningPanel::applyPboLimit() {
    using namespace octool::core;
    if (!pboCaps_.valid || pboCaps_.error || !amdPboLimitsAvailable(pboCaps_.raw)) {
        status_->setText("Read PBO capability first. Limits require power limits available and manual OC disabled."); return;
    }
    const auto kind = AmdPboLimit(pboLimit_->currentData().toInt());
    quint64 value = 0;
    if (!parseNumber(pboLimitValue_->text(), 10, amdPboLimitMaximum(kind), value) || !value) {
        status_->setText(QString("Enter integer %1 in 1..%2 %3. These are application bounds, not recommended operating limits.")
            .arg(amdPboLimitName(kind)).arg(amdPboLimitMaximum(kind)).arg(amdPboLimitUnit(kind))); return;
    }
    const auto target = pboTarget_; const auto before = pboCaps_; const auto prepared = curveRevision_;
    const auto input = pboLimitValue_->text();
    if (QMessageBox::question(this, "Apply AMD PBO limit",
        QString("CPU %1 · shared %2 limit: %3 %4\n"
                "This changes a package power, current or temperature limit and can affect stability. "
                "Firmware acceptance can be checked; the effective limit cannot be read back. "
                "No automatic mode change, retry or rollback. Apply this one limit?")
            .arg(target.cpu).arg(amdPboLimitName(kind)).arg(value).arg(amdPboLimitUnit(kind)),
        QMessageBox::Yes|QMessageBox::Cancel, QMessageBox::Cancel) != QMessageBox::Yes) return;
    if (prepared != curveRevision_ || input != pboLimitValue_->text() || int(kind) != pboLimit_->currentData().toInt()) {
        invalidateCurve(); status_->setText("Inputs changed. Read PBO capability again."); return;
    }
    invalidateCurve(); const auto revision = curveRevision_; setEnabled(false); status_->setText("Checking capability and submitting PBO limit…");
    auto *watcher = new QFutureWatcher<AmdPboLimitUpdate>(this);
    connect(watcher, &QFutureWatcher<AmdPboLimitUpdate>::finished, this, [this, watcher, revision, target] {
        const auto r = watcher->result();
        if (revision == curveRevision_) {
            append(table_, {QString("Requested %1 (%2)").arg(amdPboLimitName(r.limit)).arg(amdPboLimitUnit(r.limit)), QString::number(r.requested)});
            append(table_, {"Setting command", r.accepted ? "Firmware accepted" : r.writeAttempted ? "Attempted; not confirmed" : "Not sent"});
            append(table_, {"Firmware response", hexValue(r.response, 4)});
            append(table_, {"Effective limit readback", "Unavailable / not verified"});
        }
        status_->setText((r.error ? failure(r.error) + (r.writeAttempted ? " The limit may have changed. No automatic retry." : " No setting sent.") :
            QString("Firmware accepted %1 = %2 %3. The effective limit remains unverified.")
                .arg(amdPboLimitName(r.limit)).arg(r.requested).arg(amdPboLimitUnit(r.limit))) +
            (revision == curveRevision_ ? " Read capability again before another edit." : QString(" Inputs changed; result belongs to CPU %1.").arg(target.cpu)));
        setEnabled(true); watcher->deleteLater();
    });
    const auto access = access_; const auto cancelled = cancelled_;
    watcher->setFuture(QtConcurrent::run([access, cancelled, target, before, kind, value] {
        AmdPboLimitUpdate out;
        const int error = access->transaction([&](HardwareSession &s) { out = setShimadaPboLimit(s, target, before.raw, kind, unsigned(value)); return out.error; }, 10000, cancelled.get());
        if (error) out.error = error;
        return out;
    }));
}
void AmdTuningPanel::applyOcMode() {
    using namespace octool::core;
    if(!pboCaps_.valid || pboCaps_.error || ocMode_->currentIndex()<0) {
        status_->setText("Read OC capability before changing manual OC mode."); return;
    }
    const bool enabled=ocMode_->currentData().toInt()!=0;
    const auto target=pboTarget_;const auto before=pboCaps_;const auto prepared=curveRevision_;
    if(QMessageBox::question(this,"Change AMD manual OC mode",
        QString("CPU %1 · requested manual OC mode: %2\n"
                "Mode changes affect boost behavior, voltage, power and stability. Only the mode flag will be checked. "
                "No frequency, VID or scalar command is sent. Disabling does not guarantee all defaults are restored. Apply this mode?")
            .arg(target.cpu).arg(enabled?"Enabled":"Disabled"),QMessageBox::Yes|QMessageBox::Cancel,QMessageBox::Cancel)!=QMessageBox::Yes)return;
    if(prepared!=curveRevision_ || ocMode_->currentIndex()<0 || enabled!=(ocMode_->currentData().toInt()!=0)) {
        invalidateCurve();status_->setText("Inputs changed. Read OC capability again.");return;
    }
    invalidateCurve();const auto revision=curveRevision_;setEnabled(false);status_->setText("Checking capability and changing manual OC mode…");
    auto *watcher=new QFutureWatcher<AmdOcModeUpdate>(this);
    connect(watcher,&QFutureWatcher<AmdOcModeUpdate>::finished,this,[this,watcher,revision,target,enabled]{
        const auto r=watcher->result();
        if(revision==curveRevision_) {
            append(table_,{"Requested manual OC mode",enabled?"Enabled":"Disabled"});
            append(table_,{"Setting command",r.unchanged?"Not sent; already requested mode":r.accepted?"Firmware accepted":r.writeAttempted?"Attempted; not confirmed":"Not sent"});
            append(table_,{"Mode flag readback",r.after.valid && !r.after.error?(r.after.raw & 1u?"Enabled":"Disabled"):"Unavailable"});
            append(table_,{"Mode verification",r.verified && !r.error?"Mode bit matches":"Not verified"});
            append(table_,{"Capability before / after",QString("%1 / %2").arg(r.before.valid?hexValue(r.before.raw,4):"Unavailable").arg(r.after.valid?hexValue(r.after.raw,4):"Unavailable")});
        }
        status_->setText((r.error?failure(r.error)+(r.writeAttempted?". Mode may have changed; read again before another action.":". No mode setting sent."):
            QString("Manual OC mode flag %1. Frequency, voltage and stability remain unverified; other defaults are not confirmed.").arg(r.unchanged?"already matched":"readback matches"))+
            (revision==curveRevision_?" Read capability again before another edit.":QString(" Inputs changed; result belongs to CPU %1.").arg(target.cpu)));
        setEnabled(true);watcher->deleteLater();
    });
    const auto access=access_;const auto cancelled=cancelled_;
    watcher->setFuture(QtConcurrent::run([access,cancelled,target,before,enabled]{
        AmdOcModeUpdate out;out.requestedEnabled=enabled;
        const int e=access->transaction([&](HardwareSession &s){out=setShimadaOcMode(s,target,before.raw,enabled);return out.error;},10000,cancelled.get());
        if(e)out.error=e;
        return out;
    }));
}
void AmdTuningPanel::applyOcFrequency() {
    using namespace octool::core;
    if(!pboCaps_.valid || pboCaps_.error || !(pboCaps_.raw & 1u)) {
        status_->setText("Read OC capability first. Manual OC must already be enabled; no mode change was requested.");return;
    }
    const auto scope=AmdOcFrequencyScope(ocFrequencyScope_->currentData().toInt());
    quint64 ccd=0,core=0,mhz=0;std::uint32_t command=0,argument=0;unsigned coreMask=0;
    if(ocFrequencyScope_->currentIndex()<0 ||
       (scope!=AmdOcFrequencyScope::AllCores && !parseNumber(ocCcd_->text(),10,15,ccd)) ||
       (scope==AmdOcFrequencyScope::Core && !parseNumber(ocCore_->text(),10,7,core)) ||
       (scope==AmdOcFrequencyScope::Cores && !parseAmdCurveCores(ocCore_->text().toStdString(),coreMask)) ||
       !parseNumber(ocMHz_->text(),10,0xfffff,mhz) || encodeShimadaOcFrequency(scope==AmdOcFrequencyScope::Cores?AmdOcFrequencyScope::Core:scope,unsigned(ccd),unsigned(core),unsigned(mhz),command,argument)) {
        status_->setText("Enter firmware CCD 0..15 and core 0..7, or an ascending core list such as 0,2,7. MHz must be 1..1048575 (encoding bounds). No command sent.");return;
    }
    const auto target=pboTarget_;const auto before=pboCaps_;const auto prepared=curveRevision_;
    const auto ccdText=ocCcd_->text(),coreText=ocCore_->text(),mhzText=ocMHz_->text();
    const auto scopeText=scope==AmdOcFrequencyScope::AllCores?QString("All cores"):
        QString("Firmware CCD %1 / %2 %3 (physical mapping unverified)").arg(ccd).arg(scope==AmdOcFrequencyScope::Cores?"cores":"core").arg(coreText);
    if(QMessageBox::question(this,"Set AMD manual OC frequency",
        QString("CPU %1 · %2 · requested %3 MHz\n"
                "This can affect voltage, heat, power and stability. Firmware acceptance can be checked; current frequency cannot be read back. "
                "Selected cores are processed in order; a failure stops further commands and earlier changes remain. "
                "No mode or VID command, automatic retry or rollback. Apply this frequency?").arg(target.cpu).arg(scopeText).arg(mhz),
        QMessageBox::Yes|QMessageBox::Cancel,QMessageBox::Cancel)!=QMessageBox::Yes)return;
    if(prepared!=curveRevision_ || int(scope)!=ocFrequencyScope_->currentData().toInt() || ocFrequencyScope_->currentIndex()<0 ||
       ccdText!=ocCcd_->text() || coreText!=ocCore_->text() || mhzText!=ocMHz_->text()) {
        invalidateCurve();status_->setText("Inputs changed. Read OC capability again.");return;
    }
    invalidateCurve();const auto revision=curveRevision_;setEnabled(false);status_->setText("Checking capability and submitting manual OC frequency…");
    if(scope==AmdOcFrequencyScope::Cores) {
        auto *watcher=new QFutureWatcher<AmdOcCoreFrequenciesUpdate>(this);
        connect(watcher,&QFutureWatcher<AmdOcCoreFrequenciesUpdate>::finished,this,[this,watcher,revision,target,scopeText,mhz,ccd,coreMask]{
            const auto r=watcher->result();
            if(revision==curveRevision_) {
                append(table_,{"Requested target",scopeText});
                append(table_,{"Requested frequency (MHz)",QString::number(mhz)});
                append(table_,{"Attempted core mask",hexValue(r.attemptedMask,1)});
                append(table_,{"Accepted core mask",hexValue(r.acceptedMask,1)});
                append(table_,{"Stopped at core",r.failedCore<8?QString::number(r.failedCore):"None"});
                for(unsigned n=0;n<8;++n)if(coreMask & (1u<<n)) {
                    const auto &v=r.cores[n];
                    const QString state=v.accepted?"Firmware accepted":v.writeAttempted?"Attempted; not confirmed":"Not sent";
                    append(table_,{QString("CCD %1 / core %2").arg(ccd).arg(n),state+
                        ((r.visitedMask & (1u<<n))?QString(" · response %1 · %2").arg(hexValue(v.response,4)).arg(amdPboPhaseName(v.phase)):QString())});
                }
                append(table_,{"Current frequency readback","Unavailable / not verified"});
            }
            status_->setText((r.error?failure(r.error)+(r.attemptedMask?". Partial changes may remain; no automatic retry.":". No frequency setting sent."):
                QString("Firmware accepted selected cores at %1 MHz. Current frequencies and physical mapping remain unverified.").arg(mhz))+
                (revision==curveRevision_?" Read capability again before another edit.":QString(" Inputs changed; result belongs to CPU %1.").arg(target.cpu)));
            setEnabled(true);watcher->deleteLater();
        });
        const auto access=access_;const auto cancelled=cancelled_;
        watcher->setFuture(QtConcurrent::run([access,cancelled,target,before,ccd,coreMask,mhz]{
            AmdOcCoreFrequenciesUpdate out;
            const int e=access->transaction([&](HardwareSession &s){out=setShimadaOcCoreFrequencies(s,target,before.raw,unsigned(ccd),coreMask,unsigned(mhz));return out.error;},10000,cancelled.get());
            if(e)out.error=e;
            return out;
        }));
        return;
    }
    auto *watcher=new QFutureWatcher<AmdOcFrequencyUpdate>(this);
    connect(watcher,&QFutureWatcher<AmdOcFrequencyUpdate>::finished,this,[this,watcher,revision,target,scopeText,mhz]{
        const auto r=watcher->result();
        if(revision==curveRevision_) {
            append(table_,{"Requested target",scopeText});
            append(table_,{"Requested frequency (MHz)",QString::number(mhz)});
            append(table_,{"Setting command",r.accepted?"Firmware accepted":r.writeAttempted?"Attempted; not confirmed":"Not sent"});
            append(table_,{"Firmware response",hexValue(r.response,4)});
            append(table_,{"Current frequency readback","Unavailable / not verified"});
        }
        status_->setText((r.error?failure(r.error)+(r.writeAttempted?". Frequency may have changed; no automatic retry.":". No frequency setting sent."):
            QString("Firmware accepted %1 MHz. Current frequency and physical target mapping remain unverified.").arg(mhz))+
            (revision==curveRevision_?" Read capability again before another edit.":QString(" Inputs changed; result belongs to CPU %1.").arg(target.cpu)));
        setEnabled(true);watcher->deleteLater();
    });
    const auto access=access_;const auto cancelled=cancelled_;
    watcher->setFuture(QtConcurrent::run([access,cancelled,target,before,scope,ccd,core,mhz]{
        AmdOcFrequencyUpdate out;out.scope=scope;out.ccd=unsigned(ccd);out.core=unsigned(core);out.requestedMHz=unsigned(mhz);
        const int e=access->transaction([&](HardwareSession &s){out=setShimadaOcFrequency(s,target,before.raw,scope,unsigned(ccd),unsigned(core),unsigned(mhz));return out.error;},10000,cancelled.get());
        if(e)out.error=e;
        return out;
    }));
}
void AmdTuningPanel::readPboScalar() {
    using namespace octool::core;
    invalidateCurve();
    quint64 cpu = 0, bus = 0, device = 0, function = 0;
    if (profile_->currentData().toInt() != int(SmuProfile::Shimada) ||
        !parseNumber(cpu_->text(), 10, UINT32_MAX, cpu) || !parseNumber(bus_->text(), 16, 0, bus) ||
        !parseNumber(device_->text(), 16, 0, device) || !parseNumber(function_->text(), 16, 0, function)) {
        status_->setText("PBO scalar requires Shimada at 0000:00:00.0 and a valid logical CPU. No query sent."); return;
    }
    SmuTarget target; target.cpu = unsigned(cpu); target.profile = SmuProfile::Shimada;
    const auto revision = curveRevision_;
    setEnabled(false); status_->setText("Reading PBO scalar…");
    auto *watcher = new QFutureWatcher<AmdPboScalar>(this);
    connect(watcher, &QFutureWatcher<AmdPboScalar>::finished, this, [this, watcher, target, revision] {
        const auto r = watcher->result();
        if (revision != curveRevision_) status_->setText("Inputs changed while reading. Read PBO scalar again.");
        else {
            append(table_, {"PBO scalar raw", r.rawValid ? hexValue(r.raw, 4) : "—"});
            append(table_, {"PBO scalar multiplier", r.valid ? QString::number(r.multiplier, 'g', 9) : "Unavailable"});
            append(table_, {"Firmware response", hexValue(r.response, 4)});
            if (r.error || !r.valid) status_->setText(failure(r.error ? r.error : -EIO) + " PBO scalar unavailable. Read again before editing.");
            else {
                pboSnapshot_ = r; pboTarget_ = target;
                status_->setText("PBO scalar read. This shared firmware setting is not a measured voltage or power value. Writes support integer multipliers 1..10; zero's policy meaning is unknown.");
            }
        }
        setEnabled(true); watcher->deleteLater();
    });
    const auto access = access_; const auto cancelled = cancelled_;
    watcher->setFuture(QtConcurrent::run([access, cancelled, target] {
        AmdPboScalar out;
        const int error = access->transaction([&](HardwareSession &s) { out = readShimadaPboScalar(s, target); return out.error; }, 10000, cancelled.get());
        if (error) { out.error = error; out.valid = false; }
        return out;
    }));
}
void AmdTuningPanel::applyPboScalar() {
    using namespace octool::core;
    if (!pboSnapshot_.valid || pboSnapshot_.error) { status_->setText("Read PBO scalar before applying a change."); return; }
    quint64 multiplier = 0;
    if (!parseNumber(pboScalar_->text(), 10, 10, multiplier) || !multiplier) {
        status_->setText("PBO scalar requires an integer multiplier 1..10."); return;
    }
    const auto target = pboTarget_; const auto before = pboSnapshot_; const auto prepared = curveRevision_;
    if (QMessageBox::question(this, "Apply AMD PBO scalar",
        QString("Shimada family 1Ah/model 08h · logical CPU %1\nSet shared PBO scalar from %2 to %3?\n"
                "Changing this policy may increase voltage, power and heat. The old value will be checked and the result read back. No automatic OC enable, retry or rollback.")
            .arg(target.cpu).arg(before.multiplier, 0, 'g', 9).arg(multiplier),
        QMessageBox::Yes|QMessageBox::Cancel, QMessageBox::Cancel) != QMessageBox::Yes) return;
    if (prepared != curveRevision_) { status_->setText("Inputs changed. Read PBO scalar again."); return; }
    invalidateCurve(); const auto revision = curveRevision_;
    setEnabled(false); status_->setText("Comparing, updating and reading back PBO scalar…");
    auto *watcher = new QFutureWatcher<AmdPboScalarUpdate>(this);
    connect(watcher, &QFutureWatcher<AmdPboScalarUpdate>::finished, this, [this, watcher, revision, target] {
        const auto r = watcher->result();
        if (revision == curveRevision_) {
            append(table_, {"PBO scalar previous raw", r.before.rawValid ? hexValue(r.before.raw, 4) : "—"});
            append(table_, {"PBO scalar readback raw", r.after.rawValid ? hexValue(r.after.raw, 4) : "—"});
            append(table_, {"PBO scalar multiplier", r.after.valid ? QString::number(r.after.multiplier, 'g', 9) : "Unavailable"});
            append(table_, {"Setting command", r.unchanged ? "Unchanged; no setting sent" : r.accepted ? "Firmware accepted" : r.writeAttempted ? "Attempted; completion unknown" : "Not sent"});
            append(table_, {"Readback", r.verified && !r.error ? "Matched" : "Not verified"});
        }
        status_->setText((r.error ? failure(r.error) + QString(" Stopped at %1. ").arg(amdPboPhaseName(r.phase)) +
            (r.writeAttempted ? "The setting may have changed. No retry or rollback." : "No setting command sent.") :
            QString("PBO scalar %1; firmware readback matched. Physical voltage, power and stability are not measured.").arg(r.unchanged ? "unchanged" : "updated")) +
            (revision == curveRevision_ ? " Read again before another edit." : QString(" Inputs changed; result belongs to CPU %1. Read again.").arg(target.cpu)));
        setEnabled(true); watcher->deleteLater();
    });
    const auto access = access_; const auto cancelled = cancelled_;
    watcher->setFuture(QtConcurrent::run([access, cancelled, target, before, multiplier] {
        AmdPboScalarUpdate out;
        const int error = access->transaction([&](HardwareSession &s) { out = setShimadaPboScalar(s, target, before.raw, unsigned(multiplier)); return out.error; }, 10000, cancelled.get());
        if (error) { out.error = error; out.verified = false; }
        return out;
    }));
}
void AmdTuningPanel::readCurve() {
    using namespace octool::core;
    invalidateCurve();
    quint64 cpu = 0, bus = 0, device = 0, function = 0, ccd = 0;
    unsigned coreMask = 0;
    if (profile_->currentData().toInt() != int(SmuProfile::Shimada) ||
        !parseNumber(cpu_->text(), 10, UINT32_MAX, cpu) || !parseNumber(bus_->text(), 16, 0, bus) ||
        !parseNumber(device_->text(), 16, 0, device) || !parseNumber(function_->text(), 16, 0, function) ||
        !parseNumber(curveCcd_->text(), 10, 15, ccd) || !parseAmdCurveCores(curveCore_->text().toStdString(), coreMask)) {
        status_->setText("Curve query requires Shimada at 0000:00:00.0, a valid logical CPU, decimal firmware CCD 0..15 and ascending core indices 0..7 (e.g. 0,2,7). No query sent.");
        return;
    }
    SmuTarget target; target.cpu = unsigned(cpu); target.profile = SmuProfile::Shimada;
    setEnabled(false); status_->setText("Reading curve for the specified firmware CCD/core…");
    const unsigned revision = curveRevision_;
    auto *watcher = new QFutureWatcher<AmdCurveSnapshot>(this);
    connect(watcher, &QFutureWatcher<AmdCurveSnapshot>::finished, this, [this, watcher, target, revision] {
        const auto r = watcher->result();
        if (revision != curveRevision_) {
            status_->setText("Inputs changed while reading. Read selected cores again.");
        } else if (r.error || !r.valid) {
            status_->setText(failure(r.error ? r.error : -EIO) +
                " Curve value unavailable. No retry performed.");
        } else {
            curveSnapshot_ = r; curveTarget_ = target;
            const bool single = !(r.coreMask & (r.coreMask-1));
            append(table_, {"Firmware CCD", QString::number(r.ccd)});
            for (unsigned core = 0; core < 8; ++core) if (r.coreMask & (1u << core)) {
                const qint64 raw = r.raw[core] <= 0x7fffffffu ? qint64(r.raw[core]) : qint64(r.raw[core]) - (Q_INT64_C(1) << 32);
                if (single) {
                    append(table_, {"Firmware core in CCD", QString::number(core)});
                    append(table_, {"Firmware response", hexValue(1, 4)});
                    append(table_, {"Curve raw (hex)", hexValue(r.raw[core], 4)});
                    append(table_, {"Curve raw (signed decimal)", QString::number(raw)});
                } else append(table_, {QString("Firmware core %1").arg(core), QString("%1 · signed %2").arg(hexValue(r.raw[core], 4)).arg(raw)});
            }
            status_->setText("Curve query completed. Raw firmware value, not mV. Physical-core mapping is unverified. Review selected cores and count before applying.");
        }
        setEnabled(true); watcher->deleteLater();
    });
    const auto access = access_; const auto cancelled = cancelled_;
    watcher->setFuture(QtConcurrent::run([access, cancelled, target, ccd, coreMask] {
        AmdCurveSnapshot out;
        const int error = access->transaction([&](HardwareSession &s) {
            out = readShimadaCurves(s, target, unsigned(ccd), coreMask); return out.error;
        }, 10000, cancelled.get());
        if (error) { out.error = error; out.valid = false; out.raw = {}; }
        return out;
    }));
}
void AmdTuningPanel::applyCurve() {
    using namespace octool::core;
    if (!curveSnapshot_.valid) { status_->setText("Read selected cores before applying a curve count."); return; }
    bool ok = false; const int margin = curveMargin_->text().toInt(&ok, 10);
    if (!ok || margin < -50 || margin > 50) { status_->setText("Curve margin requires an integer CO count -50..50, not mV."); return; }
    const auto expected = curveSnapshot_; const auto target = curveTarget_; const unsigned revision = curveRevision_;
    QStringList cores;
    for (unsigned core = 0; core < 8; ++core) if (expected.coreMask & (1u << core)) cores << QString::number(core);
    if (QMessageBox::question(this, "Apply AMD curve counts",
        QString("Shimada family 1Ah/model 08h · logical CPU %1\nFirmware CCD %2 · cores %3\nNew CO count: %4 (not mV)\n"
                "Firmware indices are entered manually; physical-core mapping is unverified.\n"
                "Old values will be rechecked, then each change read back. Stops on first failure; earlier changes may remain. No automatic retry or rollback.")
            .arg(target.cpu).arg(expected.ccd).arg(cores.join(",")).arg(margin),
        QMessageBox::Yes|QMessageBox::Cancel, QMessageBox::Cancel) != QMessageBox::Yes) return;
    if (revision != curveRevision_) { status_->setText("Inputs changed. Read selected cores again."); return; }
    curveSnapshot_ = {}; table_->setRowCount(0); setEnabled(false); status_->setText("Rechecking and updating selected curve counts…");
    auto *watcher = new QFutureWatcher<AmdCurveUpdate>(this);
    connect(watcher, &QFutureWatcher<AmdCurveUpdate>::finished, this, [this, watcher, expected, revision] {
        const auto r = watcher->result();
        if (revision == curveRevision_) {
            append(table_, {"Firmware CCD", QString::number(expected.ccd)});
            for (unsigned core = 0; core < 8; ++core) if (expected.coreMask & (1u << core)) {
                const unsigned bit = 1u << core;
                const QString state = r.unchangedMask & bit ? "Unchanged; rechecked" : r.verifiedMask & bit ? "Readback matched" :
                    r.acceptedMask & bit ? "Accepted; readback not confirmed" : r.attemptedMask & bit ? "Attempted; not confirmed" : "No setting command sent";
                append(table_, {QString("Firmware core %1").arg(core), state});
                if (r.beforeMask & bit) append(table_, {QString("Core %1 previous raw").arg(core), hexValue(r.before[core], 4)});
                if (r.afterMask & bit) append(table_, {QString("Core %1 readback raw").arg(core), hexValue(r.after[core], 4)});
            }
        }
        status_->setText((r.error ? failure(r.error) + QString(" Stopped at %1, core %2. Earlier changes may remain. No retry or rollback.")
            .arg(amdCurvePhaseName(r.phase)).arg(r.core < 8 ? QString::number(r.core) : "—") :
            QString("Selected curve counts verified by firmware readback. Physical voltage, stability and physical-core mapping are not measured.")) +
            (revision == curveRevision_ ? " Read again before another edit." : " Inputs changed; read selected cores again."));
        setEnabled(true); watcher->deleteLater();
    });
    const auto access = access_; const auto cancelled = cancelled_;
    watcher->setFuture(QtConcurrent::run([access, cancelled, target, expected, margin] {
        AmdCurveUpdate out;
        const int error = access->transaction([&](HardwareSession &s) { out = setShimadaCurves(s, target, expected, margin); return out.error; }, 10000, cancelled.get());
        if (error) out.error = error;
        return out;
    }));
}
void AmdTuningPanel::readTopology() {
    using namespace octool::core;
    invalidateCurve();
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
    invalidateCurve();
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
        "DDR4 XMP 2.0, DDR5 XMP 3.0 and EXPO 1.0 basic profiles show stored voltages and timings. "
        "XMP 3.0 secondary timings include separate ps and nCK lower limits. "
        "CAS support lists and XMP command rates describe stored requirements. "
        "Base SPD also includes write delays and revision-specific secondary timings. "
        "Supported geometry includes stacked packages and confirmed DDR4/DDR5 mixed-rank layouts; XMP 2.0 reports recommended DIMMs per channel. "
        "XMP 2.0 has no profile checksum; DDR5 profiles include CRC checks. "
        "XMP user slots and stored EXPO enhanced timings are readable; their enablement is unknown. "
        "Timing writes, PMIC / VRM controls and board-specific clocks are not yet recovered.",layout,this);
    auto *buttons=new QHBoxLayout; auto *read=new QPushButton("Read once",this); read->setObjectName("inventoryRead"); buttons->addWidget(read);
    auto *open=new QPushButton("Open SPD dump…",this); open->setObjectName("spdOpen"); buttons->addWidget(open); layout->addLayout(buttons);
    table_=table({"Device", "Item", "Value", "Unit", "Status", "Source"},this,layout); table_->setObjectName("inventoryTable");
    status_=description("Ready. No inventory scan has been performed.",layout,this); status_->setObjectName("inventoryStatus");
    copyButton(this,buttons,table_,status_);
    connect(read,&QPushButton::clicked,this,[this] { refresh(); });
    connect(open,&QPushButton::clicked,this,[this] {
        const QString path=QFileDialog::getOpenFileName(this,"Open binary SPD dump",{},"SPD dump (*.bin *.spd);;All files (*)"); if(path.isEmpty()) return;
        loadSpdFile(path);
    });
}
bool MemoryBoardPanel::loadSpdFile(const QString &path) {
    table_->setRowCount(0);
    std::vector<std::uint8_t> bytes;
    const int error = octool::platform::readBoundedFile(path.toStdString(), bytes, 4096);
    if (error) { status_->setText("SPD file: " + failure(error)); return false; }
    showSpd(path, QByteArray(reinterpret_cast<const char *>(bytes.data()), int(bytes.size())));
    status_->setText("Offline SPD file: " + path + " · Stored profiles; current settings are not measured. Check each section's status.");
    return true;
}
void MemoryBoardPanel::showSpd(const QString &path,const QByteArray &bytes,const QString &error) {
    if(!error.isEmpty()) { append(table_,{"SPD","EEPROM","","",error,path}); return; }
    const auto *first=reinterpret_cast<const std::uint8_t *>(bytes.constData());
    const auto decoded=octool::core::decodeSpd(std::vector<std::uint8_t>(first,first+bytes.size()));
    const QString state=decoded.error ? failure(decoded.error) : decoded.crcChecked ? "Base CRC valid" : "CRC not verified";
    append(table_,{"SPD","Captured bytes",QString::number(bytes.size()),"bytes",state,path});
    for(const auto &field:decoded.fields) append(table_,{"SPD",QString::fromStdString(field.name),QString::fromStdString(field.value),"",state,path});
    if (decoded.error) return;
    const auto showCas = [&](const octool::core::SpdCas &cas, const QString &group, const QString &integrity) {
        if (!cas.captured) return;
        QString raw; for (auto byte : cas.raw) raw += QString("%1").arg(byte,2,16,QChar('0'));
        append(table_,{group,"CAS bitmap",raw,"hex",integrity,path});
        if (!cas.decoded) {
            append(table_,{group,"Supported CAS latencies","Not decoded","nCK","Unsupported SPD revision",path}); return;
        }
        append(table_,{group,"CAS encoding",QString::fromStdString(cas.encoding),"",integrity,path});
        const QString status = cas.reservedBitsSet ? "Reserved bit set; defined bits listed" : integrity;
        if (cas.latencies.empty()) append(table_,{group,"Supported CAS latencies","None encoded","nCK",status,path});
        // Keep the list readable without widening the entire table for forty CLs.
        for (unsigned i = 0; i < cas.latencies.size(); i += 6) {
            QStringList chunk;
            for (unsigned j = i; j < cas.latencies.size() && j < i+6; ++j) chunk << QString::number(cas.latencies[j]);
            const QString name = i ? QString("Supported CAS latencies (%1)").arg(i/6+1) : "Supported CAS latencies";
            append(table_,{group,name,chunk.join(", "),"nCK",status,path});
        }
    };
    showCas(decoded.cas,"SPD",state);
    const auto &xmp = decoded.xmp;
    const QString headerState = !xmp.inspected ? "Header not captured" : !xmp.present ? "Not present" :
        !xmp.headerCaptured ? "Header truncated" : xmp.crc.checked && !xmp.crc.valid ? "Header CRC mismatch" :
        xmp.revision != (decoded.memoryType == 0x0c ? 0x20u : 0x30u) ? "Unsupported revision" :
        xmp.crcSupported ? "Header CRC valid" : "No XMP checksum provided";
    append(table_,{"XMP", "Header", xmp.headerCaptured ? QString("Revision 0x%1").arg(xmp.revision,2,16,QChar('0')) : QString(), "", headerState, path});
    const auto showProfile = [&](const octool::core::SpdXmpProfile &p) {
        const QString group = QString(p.enableKnown ? "XMP %1" : "XMP User %1").arg(p.index);
        QString status = p.blockedByExpo ? (p.enabled ? "Conflicting XMP enable bit / EXPO region" : "EXPO region") :
            !p.enabled ? "Disabled" : p.crc.checked && !p.crc.valid ? "Profile CRC mismatch" :
            p.error ? failure(p.error) : xmp.crcSupported ? "Profile CRC valid" : "No XMP checksum provided";
        if (!p.enableKnown) {
            const QString storage = p.storageState == "overlap_expo" ? "Occupied by EXPO" :
                p.storageState == "not_captured" ? "Not captured" : p.storageState == "incomplete" ? "Incomplete capture" :
                p.storageState == "all_zero" ? "All bytes zero" : p.storageState == "all_ff" ? "All bytes FF" : "Stored data";
            status = p.error ? (p.crc.checked && !p.crc.valid ? "Profile CRC mismatch" : failure(p.error)) :
                p.values.empty() ? storage : "CRC OK; enable unknown";
            append(table_,{group,"Storage",storage,"",status,path});
            append(table_,{group,"Captured bytes",QString::number(qulonglong(p.raw.size())),"bytes",status,path});
        }
        if (p.nameSupported) append(table_,{group, "Profile name", p.nameValid ? QString::fromStdString(p.name) : "Invalid name bytes", "", status, path});
        else append(table_,{group,"Stored profile",!p.enableKnown ? "Enablement unknown" : p.enabled ? "Enabled" : "Disabled","",status,path});
        if (p.timebaseRaw >= 0) append(table_,{group,"Timebase byte",QString("0x%1").arg(p.timebaseRaw,2,16,QChar('0')),"hex",
            (p.timebaseRaw & 15) ? "Unsupported timebase" : "125 ps MTB / 1 ps FTB",path});
        if (p.recommendedDimmsPerChannel >= 0) append(table_,{group,"Recommended DIMMs per channel",QString::number(p.recommendedDimmsPerChannel),"DIMMs","Stored recommendation; installed count not measured",path});
        if (p.crc.checked) append(table_,{group,"CRC stored / computed",QString("%1 / %2").arg(p.crc.stored,4,16,QChar('0')).arg(p.crc.computed,4,16,QChar('0')), "", status, path});
        showCas(p.cas,group,status);
        if (p.commandRate.captured) {
            const auto &rate = p.commandRate;
            const QString value = !rate.recognized ? QString("Unknown code %1").arg(rate.code) :
                rate.code ? QString("%1N").arg(rate.code) : "Not specified";
            append(table_,{group,"Command rate",value,"",status,path});
            append(table_,{group,"Command rate byte",QString("0x%1").arg(rate.raw,2,16,QChar('0')),"hex",status,path});
        }
        for (const auto &v : p.values) append(table_,{group,QString::fromStdString(v.name),QString::number(v.value),QString::fromStdString(v.unit),status,path});
    };
    for (const auto &p : xmp.profiles) showProfile(p);
    for (const auto &p : xmp.userProfiles) showProfile(p);
    if (decoded.memoryType != 0x12) return;
    const auto &expo = decoded.expo;
    const QString expoState = !expo.inspected ? "Block not captured" : !expo.present ? "Not present" :
        !expo.crc.checked ? "Block truncated" : !expo.crc.valid ? "Block CRC mismatch" :
        expo.revision != 0x10 ? "Unsupported revision" : "Block CRC valid";
    append(table_,{"EXPO", "Header", expo.crc.checked ? QString("Revision 0x%1").arg(expo.revision,2,16,QChar('0')) : QString(), "", expoState, path});
    if (expo.crc.checked) {
        append(table_,{"EXPO","CRC stored / computed",QString("%1 / %2").arg(expo.crc.stored,4,16,QChar('0')).arg(expo.crc.computed,4,16,QChar('0')), "", expoState, path});
        append(table_,{"EXPO","Configuration / feature bytes",QString("%1 / %2").arg(expo.configurationRaw,2,16,QChar('0')).arg(expo.featuresRaw,2,16,QChar('0')), "hex", expoState, path});
    }
    for (const auto &p : expo.profiles) {
        const QString group = QString("EXPO %1").arg(p.index);
        const QString status = !p.enabled ? "Disabled" : p.error ? failure(p.error) : "Block CRC valid";
        append(table_,{group,"Stored profile",p.enabled ? "Enabled" : "Disabled","",status,path});
        for (const auto &v : p.values) append(table_,{group,QString::fromStdString(v.name),QString::number(v.value),QString::fromStdString(v.unit),status,path});
        for (const auto &v : p.storedEnhancedTimings)
            append(table_,{group,"Stored enhanced " + QString::fromStdString(v.name),QString::number(v.value),QString::fromStdString(v.unit),"CRC OK; enable unknown",path});
    }
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
