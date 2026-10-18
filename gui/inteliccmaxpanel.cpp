// SPDX-License-Identifier: GPL-2.0-only
#include "inteliccmaxpanel.h"
#include <QApplication>
#include <QClipboard>
#include <QDoubleSpinBox>
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
IntelIccMaxPanel::IntelIccMaxPanel(std::shared_ptr<HardwareAccess> access, QWidget *parent, unsigned cpu)
    : QWidget(parent), access_(std::move(access)), cancelled_(std::make_shared<std::atomic<bool>>(false)) {
    setObjectName("intelIccMax"); auto *layout=new QVBoxLayout(this);
    auto *scope=new QLabel("IA current limit - Alder Lake\n"
        "ICCmax limits the shared IA supply rail used by P cores, E cores and ring. "
        "The selected logical CPU locates the mailbox; this is not a per-core limit or a current measurement.",this);
    scope->setWordWrap(true); layout->addWidget(scope);
    auto *buttons=new QHBoxLayout;
    buttons->addWidget(new QLabel("Logical CPU",this));
    cpu_=new QLineEdit(QString::number(cpu),this); cpu_->setObjectName("iccMaxCpu"); buttons->addWidget(cpu_);
    auto *read=new QPushButton("Read configuration",this); read->setObjectName("iccMaxRead"); buttons->addWidget(read);
    auto *copy=new QPushButton("Copy snapshot",this); copy->setObjectName("iccMaxCopy"); buttons->addWidget(copy);
    layout->addLayout(buttons);
    table_=new QTableWidget(0,3,this); table_->setObjectName("iccMaxTable");
    table_->setHorizontalHeaderLabels({"Setting","Configured value","Access"});
    table_->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    table_->setEditTriggers(QAbstractItemView::NoEditTriggers); layout->addWidget(table_);
    raw_=new QLabel(this); raw_->setObjectName("iccMaxRaw"); raw_->setWordWrap(true); layout->addWidget(raw_);
    auto *edit=new QHBoxLayout; edit->addWidget(new QLabel("IA ICCmax",this));
    value_=new QDoubleSpinBox(this); value_->setObjectName("iccMaxValue");
    value_->setRange(1,511.75); value_->setDecimals(2); value_->setSingleStep(0.25); value_->setSuffix(" A"); edit->addWidget(value_);
    apply_=new QPushButton("Apply setting…",this); apply_->setObjectName("iccMaxApply"); edit->addWidget(apply_); layout->addLayout(edit);
    auto *note=new QLabel("Enter 1..511.75 A in exact 0.25 A steps. The range describes the input format, not recommended current. A set mode flag, sub-1 A/default codes and OC-locked configurations are read only. Mode-flag semantics remain unverified.",this);
    note->setWordWrap(true); layout->addWidget(note);
    status_=new QLabel("Ready. Read the configuration before making a change.",this);
    status_->setObjectName("iccMaxStatus"); status_->setTextFormat(Qt::PlainText); status_->setWordWrap(true); layout->addWidget(status_);
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
IntelIccMaxPanel::~IntelIccMaxPanel() { cancelled_->store(true); }
void IntelIccMaxPanel::invalidate() {
    ++revision_; snapshot_={}; table_->setRowCount(0); raw_->clear(); value_->setValue(1); value_->setEnabled(false); apply_->setEnabled(false);
}
void IntelIccMaxPanel::present(const IntelIccMaxSnapshot &snapshot) {
    invalidate(); snapshot_=snapshot;
    if (snapshot.error || !snapshot.valid) { status_->setText(failure(snapshot.error?snapshot.error:-ENODATA)+" No settings displayed."); return; }
    const bool editable=intelIccMaxEditable(snapshot);
    table_->setRowCount(3);
    const bool modeBit=intelIccMaxModeBit31(snapshot.response.data);
    const QString reason=(snapshot.flexRatio & (UINT64_C(1)<<20)) ? "Firmware OC lock set" : modeBit ? "Mode flag set; read only" :
        intelIccMaxAmps(snapshot.response.data)<1 ? "Default/sub-1 A code; read only" : "Available for editing";
    const QStringList labels{"IA ICCmax encoded limit", "Mode flag (bit 31)", "IA SVID address"};
    const QStringList values{QString::number(intelIccMaxAmps(snapshot.response.data),'f',2)+" A", (modeBit ? QString("Set / unverified") : QString("Clear / unverified")), QString::number(snapshot.address)};
    for (int row=0;row<3;++row) {
        table_->setItem(row,0,new QTableWidgetItem(labels[row]));
        table_->setItem(row,1,new QTableWidgetItem(values[row]));
        table_->setItem(row,2,new QTableWidgetItem(row ? "Read only" : reason));
    }
    value_->setValue(intelIccMaxAmps(snapshot.response.data)); value_->setEnabled(editable); apply_->setEnabled(editable);
    raw_->setText(QString("Configuration: %1 · VR topology: %2 · FLEX_RATIO: %3")
        .arg(hexValue(snapshot.response.data,4)).arg(hexValue(snapshot.topology.data,4)).arg(hexValue(snapshot.flexRatio,8)));
    status_->setText(QString("CPU %1 · shared IA rail. %2. Readback verifies the stored field; current enforcement and mode-flag semantics are unverified.").arg(snapshot.cpu).arg(reason));
}
void IntelIccMaxPanel::refresh() {
    quint64 cpu=0; invalidate();
    if (!parseNumber(cpu_->text(),10,1048575,cpu)) { status_->setText("Invalid logical CPU."); return; }
    setEnabled(false); status_->setText("Reading IA ICCmax configuration…"); const auto revision=revision_;
    auto *watcher=new QFutureWatcher<IntelIccMaxSnapshot>(this);
    connect(watcher,&QFutureWatcher<IntelIccMaxSnapshot>::finished,this,[this,watcher,revision] {
        if (revision==revision_) present(watcher->result());
        else { invalidate(); status_->setText("CPU changed during the read; result discarded. Read again."); }
        setEnabled(true); watcher->deleteLater();
    });
    const auto access=access_; const auto cancelled=cancelled_;
    watcher->setFuture(QtConcurrent::run([access,cancelled,cpu] {
        IntelIccMaxSnapshot out;
        const int error=access->transaction([&](HardwareSession &s) { out=readIntelIccMax(s,unsigned(cpu)); return out.error; },5000,cancelled.get());
        if (error) out.error=error;
        return out;
    }));
}
void IntelIccMaxPanel::apply() {
    if (!intelIccMaxEditable(snapshot_)) return;
    const double amps=value_->value(); std::uint32_t encoded=0;
    if (encodeIntelIccMax(amps,snapshot_.response.data,encoded)) { status_->setText("Use exact 0.25 A steps within 1..511.75 A."); return; }
    const auto old=snapshot_; const auto preparedRevision=revision_;
    if (QMessageBox::question(this,"Apply shared IA current limit",QString("CPU %1 · shared IA rail (P cores, E cores and ring)\nIA ICCmax: %2 A → %3 A.\n"
        "This changes the configured current limit and can affect power, temperature and stability. The configuration will be applied and read back.")
        .arg(old.cpu).arg(intelIccMaxAmps(old.response.data),0,'f',2).arg(amps,0,'f',2),QMessageBox::Yes|QMessageBox::Cancel,QMessageBox::Cancel)!=QMessageBox::Yes) return;
    if (preparedRevision!=revision_) { status_->setText("CPU changed. Read the configuration again."); return; }
    invalidate(); setEnabled(false); status_->setText("Checking current settings, applying and verifying readback…");
    const auto revision=revision_; auto *watcher=new QFutureWatcher<IntelIccMaxUpdate>(this);
    connect(watcher,&QFutureWatcher<IntelIccMaxUpdate>::finished,this,[this,watcher,revision,old] {
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
    watcher->setFuture(QtConcurrent::run([access,cancelled,old,amps] {
        IntelIccMaxUpdate out;
        const int error=access->transaction([&](HardwareSession &s) { out=applyIntelIccMax(s,old,amps); return out.error; },5000,cancelled.get());
        if (error) out.error=error;
        return out;
    }));
}
