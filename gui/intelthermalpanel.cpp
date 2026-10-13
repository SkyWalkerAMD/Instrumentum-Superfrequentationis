// SPDX-License-Identifier: GPL-2.0-only
#include "intelthermalpanel.h"
#include <QApplication>
#include <QClipboard>
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
using namespace octool::core;
namespace {
QString failure(int error) { return QString("Failed (%1): %2.").arg(error).arg(QString::fromLocal8Bit(std::strerror(-error))); }
}
IntelThermalPanel::IntelThermalPanel(std::shared_ptr<HardwareAccess> access, QWidget *parent, unsigned cpu)
    : QWidget(parent), access_(std::move(access)), cancelled_(std::make_shared<std::atomic<bool>>(false)) {
    setObjectName("intelThermal"); auto *layout=new QVBoxLayout(this);
    auto *scope=new QLabel("TCC activation temperature\n"
        "Set the thermal activation threshold below the processor default temperature (TjMax). "
        "Zero offset uses the default threshold. Access is through the selected logical CPU.",this);
    scope->setWordWrap(true); layout->addWidget(scope);
    auto *buttons=new QHBoxLayout;
    buttons->addWidget(new QLabel("Logical CPU",this));
    cpu_=new QLineEdit(QString::number(cpu),this); cpu_->setObjectName("thermalCpu"); buttons->addWidget(cpu_);
    auto *read=new QPushButton("Read configuration",this); read->setObjectName("thermalRead"); buttons->addWidget(read);
    auto *copy=new QPushButton("Copy snapshot",this); copy->setObjectName("thermalCopy"); buttons->addWidget(copy);
    layout->addLayout(buttons);
    table_=new QTableWidget(0,3,this); table_->setObjectName("thermalTable");
    table_->setHorizontalHeaderLabels({"Setting","Configured value","Access"});
    table_->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    table_->setEditTriggers(QAbstractItemView::NoEditTriggers); layout->addWidget(table_);
    raw_=new QLabel(this); raw_->setObjectName("thermalRaw"); raw_->setWordWrap(true); layout->addWidget(raw_);
    auto *edit=new QHBoxLayout; edit->addWidget(new QLabel("TCC offset below TjMax (C)",this));
    value_=new QLineEdit(this); value_->setObjectName("thermalValue"); value_->setPlaceholderText("0..63"); edit->addWidget(value_);
    apply_=new QPushButton("Apply offset…",this); apply_->setObjectName("thermalApply"); edit->addWidget(apply_); layout->addLayout(edit);
    auto *note=new QLabel("Readback verifies configuration. Thermal behavior also depends on firmware, cooling and workload.",this);
    note->setWordWrap(true); layout->addWidget(note);
    status_=new QLabel("Ready. Read the configuration before making a change.",this);
    status_->setObjectName("thermalStatus"); status_->setTextFormat(Qt::PlainText); status_->setWordWrap(true); layout->addWidget(status_);
    invalidate();
    connect(cpu_,&QLineEdit::textChanged,this,[this] { invalidate(); status_->setText("CPU changed. Read the configuration again."); });
    connect(read,&QPushButton::clicked,this,[this] { refresh(); });
    connect(apply_,&QPushButton::clicked,this,[this] { apply(); });
    connect(copy,&QPushButton::clicked,this,[this] {
        QString text=status_->text()+"\n"+raw_->text()+"\nSetting\tConfigured value\tAccess\n";
        for (int row=0;row<table_->rowCount();++row) {
            for (int col=0;col<table_->columnCount();++col) text+=(col?"\t":"")+table_->item(row,col)->text();
            text+="\n";
        }
        QApplication::clipboard()->setText(text);
    });
}
IntelThermalPanel::~IntelThermalPanel() { cancelled_->store(true); }
void IntelThermalPanel::invalidate() {
    ++revision_; snapshot_={}; table_->setRowCount(0); raw_->clear(); value_->clear(); value_->setEnabled(false); apply_->setEnabled(false);
}
void IntelThermalPanel::present(const IntelThermalSnapshot &snapshot) {
    invalidate(); snapshot_=snapshot;
    if (snapshot.error || !snapshot.valid) { status_->setText(failure(snapshot.error?snapshot.error:-ENODATA)+" No settings displayed."); return; }
    const bool editable=intelThermalEditable(snapshot);
    table_->setRowCount(3);
    const QStringList names={"Default activation temperature (TjMax)","TCC offset below TjMax","Configured activation temperature"};
    const int temperatures[]={int(intelDefaultTccTemperature(snapshot.raw)),int(intelTccOffset(snapshot.raw)),intelTccTemperature(snapshot.raw)};
    for (int row=0;row<3;++row) {
        table_->setItem(row,0,new QTableWidgetItem(names[row]));
        table_->setItem(row,1,new QTableWidgetItem(QString::number(temperatures[row])+" C"));
        table_->setItem(row,2,new QTableWidgetItem(row==1 && editable?"Available for editing":"Read only"));
    }
    value_->setText(QString::number(intelTccOffset(snapshot.raw))); value_->setEnabled(editable); apply_->setEnabled(editable);
    raw_->setText(QString("TEMPERATURE_TARGET: %1 · PLATFORM_INFO: %2").arg(hexValue(snapshot.raw,8)).arg(hexValue(snapshot.platformInfo,8)));
    status_->setText(QString("CPU %1 · TCC configuration read. %2").arg(snapshot.cpu).arg(editable?"Enter an offset to apply.":"Offset unavailable: locked, not programmable, or invalid current threshold."));
}
void IntelThermalPanel::refresh() {
    quint64 cpu=0; invalidate();
    if (!parseNumber(cpu_->text(),10,1048575,cpu)) { status_->setText("Invalid logical CPU."); return; }
    setEnabled(false); status_->setText("Reading TCC configuration…"); const auto revision=revision_;
    auto *watcher=new QFutureWatcher<IntelThermalSnapshot>(this);
    connect(watcher,&QFutureWatcher<IntelThermalSnapshot>::finished,this,[this,watcher,revision] {
        if (revision==revision_) present(watcher->result());
        else { invalidate(); status_->setText("CPU changed during the read; result discarded. Read again."); }
        setEnabled(true); watcher->deleteLater();
    });
    const auto access=access_; const auto cancelled=cancelled_;
    watcher->setFuture(QtConcurrent::run([access,cancelled,cpu] {
        IntelThermalSnapshot out;
        const int error=access->transaction([&](HardwareSession &s) { out=readIntelThermal(s,unsigned(cpu)); return out.error; },5000,cancelled.get());
        if (error) out.error=error;
        return out;
    }));
}
void IntelThermalPanel::apply() {
    if (!intelThermalEditable(snapshot_)) return;
    quint64 degrees=0;
    if (!parseNumber(value_->text(),10,63,degrees)) { status_->setText("Offset must be an integer from 0 to 63 C."); return; }
    if (degrees>=intelDefaultTccTemperature(snapshot_.raw)) { status_->setText("Offset must be below TjMax; activation temperature must stay positive."); return; }
    const auto old=snapshot_; const auto preparedRevision=revision_;
    if (QMessageBox::question(this,"Apply TCC offset",QString("CPU %1 · thermal activation threshold\nOffset: %2 → %3 C below TjMax.\nNew activation temperature: %4 C.\n"
        "This changes when the processor starts thermal throttling. The offset will be applied and read back.")
        .arg(old.cpu).arg(intelTccOffset(old.raw)).arg(degrees).arg(intelDefaultTccTemperature(old.raw)-unsigned(degrees)),QMessageBox::Yes|QMessageBox::Cancel,QMessageBox::Cancel)!=QMessageBox::Yes) return;
    if (preparedRevision!=revision_) { status_->setText("CPU changed. Read the configuration again."); return; }
    invalidate(); setEnabled(false); status_->setText("Checking current settings, applying and verifying readback…");
    const auto revision=revision_; auto *watcher=new QFutureWatcher<IntelThermalUpdate>(this);
    connect(watcher,&QFutureWatcher<IntelThermalUpdate>::finished,this,[this,watcher,revision,old] {
        const auto out=watcher->result(); setEnabled(true);
        if (revision!=revision_) {
            invalidate(); status_->setText(QString("CPU changed during the operation for CPU %1; displayed result discarded. ").arg(old.cpu)+
                (out.writeAttempted?"A settings write was attempted. Read the original CPU again.":"No settings write was attempted."));
        } else if (!out.error && out.verified) {
            present(out.after); status_->setText(status_->text()+(out.unchanged?" Requested offset already present; no settings write performed.":" Configuration readback verified; other bits preserved."));
        } else {
            invalidate(); status_->setText(failure(out.error?out.error:-EIO)+(out.writeAttempted?
                " A settings write was attempted; the final configuration is not verified. No retry or rollback was performed.":" No settings write was attempted."));
        }
        watcher->deleteLater();
    });
    const auto access=access_; const auto cancelled=cancelled_;
    watcher->setFuture(QtConcurrent::run([access,cancelled,old,degrees] {
        IntelThermalUpdate out;
        const int error=access->transaction([&](HardwareSession &s) { out=applyIntelThermal(s,old,unsigned(degrees)); return out.error; },5000,cancelled.get());
        if (error) out.error=error;
        return out;
    }));
}
