// SPDX-License-Identifier: GPL-2.0-only
#include "intelbclkvoltagepanel.h"
#include <QApplication>
#include <QClipboard>
#include <QComboBox>
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
QString state(bool enabled) { return enabled ? "Enabled" : "Disabled"; }
QString failure(int error) { return QString("Failed (%1): %2.").arg(error).arg(QString::fromLocal8Bit(std::strerror(-error))); }
}
IntelBclkVoltagePanel::IntelBclkVoltagePanel(std::shared_ptr<HardwareAccess> access, QWidget *parent, unsigned cpu)
    : QWidget(parent), access_(std::move(access)), cancelled_(std::make_shared<std::atomic<bool>>(false)) {
    setObjectName("intelBclkVoltage"); auto *layout=new QVBoxLayout(this);
    auto *scope=new QLabel("BCLK adaptive voltage - Raptor Lake-S\n"
        "When enabled, the CPU voltage/frequency curve accounts for the base clock (BCLK). "
        "This shared processor setting is accessed through the selected logical CPU.",this);
    scope->setWordWrap(true); layout->addWidget(scope);
    auto *buttons=new QHBoxLayout;
    buttons->addWidget(new QLabel("Logical CPU",this));
    cpu_=new QLineEdit(QString::number(cpu),this); cpu_->setObjectName("bclkVoltageCpu"); buttons->addWidget(cpu_);
    auto *read=new QPushButton("Read configuration",this); read->setObjectName("bclkVoltageRead"); buttons->addWidget(read);
    auto *copy=new QPushButton("Copy snapshot",this); copy->setObjectName("bclkVoltageCopy"); buttons->addWidget(copy);
    layout->addLayout(buttons);
    table_=new QTableWidget(0,3,this); table_->setObjectName("bclkVoltageTable");
    table_->setHorizontalHeaderLabels({"Setting","Configured value","Access"});
    table_->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    table_->setEditTriggers(QAbstractItemView::NoEditTriggers); layout->addWidget(table_);
    raw_=new QLabel(this); raw_->setObjectName("bclkVoltageRaw"); raw_->setWordWrap(true); layout->addWidget(raw_);
    auto *edit=new QHBoxLayout; edit->addWidget(new QLabel("BCLK adaptive voltage",this));
    value_=new QComboBox(this); value_->setObjectName("bclkVoltageValue");
    value_->addItem("Disabled",0); value_->addItem("Enabled",1); edit->addWidget(value_);
    apply_=new QPushButton("Apply setting…",this); apply_->setObjectName("bclkVoltageApply"); edit->addWidget(apply_); layout->addLayout(edit);
    auto *note=new QLabel("Readback verifies the requested configuration. Actual voltage and stability also depend on CPU, firmware, clock and workload.",this);
    note->setWordWrap(true); layout->addWidget(note);
    status_=new QLabel("Ready. Read the configuration before making a change.",this);
    status_->setObjectName("bclkVoltageStatus"); status_->setTextFormat(Qt::PlainText); status_->setWordWrap(true); layout->addWidget(status_);
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
IntelBclkVoltagePanel::~IntelBclkVoltagePanel() { cancelled_->store(true); }
void IntelBclkVoltagePanel::invalidate() {
    ++revision_; snapshot_={}; table_->setRowCount(0); raw_->clear(); value_->setCurrentIndex(-1); value_->setEnabled(false); apply_->setEnabled(false);
}
void IntelBclkVoltagePanel::present(const IntelBclkVoltageSnapshot &snapshot) {
    invalidate(); snapshot_=snapshot;
    if (snapshot.error || !snapshot.valid) { status_->setText(failure(snapshot.error?snapshot.error:-ENODATA)+" No settings displayed."); return; }
    const bool editable=intelBclkVoltageEditable(snapshot);
    table_->setRowCount(1); table_->setItem(0,0,new QTableWidgetItem("BCLK adaptive voltage"));
    table_->setItem(0,1,new QTableWidgetItem(state(intelBclkVoltageEnabled(snapshot.response.data))));
    table_->setItem(0,2,new QTableWidgetItem(editable?"Available for editing":"Firmware OC lock set"));
    value_->setCurrentIndex(intelBclkVoltageEnabled(snapshot.response.data)?1:0); value_->setEnabled(editable); apply_->setEnabled(editable);
    raw_->setText(QString("Configuration: %1 · FLEX_RATIO: %2").arg(hexValue(snapshot.response.data,4)).arg(hexValue(snapshot.flexRatio,8)));
    status_->setText(QString("CPU %1 · BCLK adaptive voltage configuration read. %2").arg(snapshot.cpu).arg(editable?"Choose a state to apply.":"Firmware overclocking lock prevents changes."));
}
void IntelBclkVoltagePanel::refresh() {
    quint64 cpu=0; invalidate();
    if (!parseNumber(cpu_->text(),10,1048575,cpu)) { status_->setText("Invalid logical CPU."); return; }
    setEnabled(false); status_->setText("Reading BCLK adaptive voltage configuration…"); const auto revision=revision_;
    auto *watcher=new QFutureWatcher<IntelBclkVoltageSnapshot>(this);
    connect(watcher,&QFutureWatcher<IntelBclkVoltageSnapshot>::finished,this,[this,watcher,revision] {
        if (revision==revision_) present(watcher->result());
        else { invalidate(); status_->setText("CPU changed during the read; result discarded. Read again."); }
        setEnabled(true); watcher->deleteLater();
    });
    const auto access=access_; const auto cancelled=cancelled_;
    watcher->setFuture(QtConcurrent::run([access,cancelled,cpu] {
        IntelBclkVoltageSnapshot out;
        const int error=access->transaction([&](HardwareSession &s) { out=readIntelBclkVoltage(s,unsigned(cpu)); return out.error; },5000,cancelled.get());
        if (error) out.error=error;
        return out;
    }));
}
void IntelBclkVoltagePanel::apply() {
    if (!intelBclkVoltageEditable(snapshot_)) return;
    if (value_->currentIndex()<0) return;
    const unsigned enabled=unsigned(value_->currentData().toInt());
    const auto old=snapshot_; const auto preparedRevision=revision_;
    if (QMessageBox::question(this,"Apply BCLK adaptive voltage",QString("CPU %1 · shared processor configuration\nBCLK adaptive voltage: %2 → %3.\n"
        "This changes the voltage response to the base clock and can affect stability. The setting will be applied and read back.")
        .arg(old.cpu).arg(state(intelBclkVoltageEnabled(old.response.data))).arg(state(enabled!=0)),QMessageBox::Yes|QMessageBox::Cancel,QMessageBox::Cancel)!=QMessageBox::Yes) return;
    if (preparedRevision!=revision_) { status_->setText("CPU changed. Read the configuration again."); return; }
    invalidate(); setEnabled(false); status_->setText("Checking current settings, applying and verifying readback…");
    const auto revision=revision_; auto *watcher=new QFutureWatcher<IntelBclkVoltageUpdate>(this);
    connect(watcher,&QFutureWatcher<IntelBclkVoltageUpdate>::finished,this,[this,watcher,revision,old] {
        const auto out=watcher->result(); setEnabled(true);
        if (revision!=revision_) {
            invalidate(); status_->setText(QString("CPU changed during the operation for CPU %1; displayed result discarded. ").arg(old.cpu)+
                (out.writeAttempted?"A settings write was attempted. Read the original CPU again.":"No settings write was attempted."));
        } else if (!out.error && out.verified) {
            present(out.after); status_->setText(status_->text()+(out.unchanged?" Requested state already present; no settings write performed.":" Configuration readback verified; other bits preserved."));
        } else {
            invalidate(); status_->setText(failure(out.error?out.error:-EIO)+(out.writeAttempted?
                " A settings write was attempted; the final configuration is not verified. No retry or rollback was performed.":" No settings write was attempted."));
        }
        watcher->deleteLater();
    });
    const auto access=access_; const auto cancelled=cancelled_;
    watcher->setFuture(QtConcurrent::run([access,cancelled,old,enabled] {
        IntelBclkVoltageUpdate out;
        const int error=access->transaction([&](HardwareSession &s) { out=applyIntelBclkVoltage(s,old,enabled); return out.error; },5000,cancelled.get());
        if (error) out.error=error;
        return out;
    }));
}
