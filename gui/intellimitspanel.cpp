// SPDX-License-Identifier: GPL-2.0-only
#include "intellimitspanel.h"
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
QString failure(int error) { return QString("Failed (%1): %2.").arg(error).arg(QString::fromLocal8Bit(std::strerror(-error))); }
}
IntelLimitsPanel::IntelLimitsPanel(std::shared_ptr<HardwareAccess> access, QWidget *parent, unsigned cpu)
    : QWidget(parent), access_(std::move(access)), cancelled_(std::make_shared<std::atomic<bool>>(false)) {
    setObjectName("intelLimits"); auto *layout=new QVBoxLayout(this);
    auto *scope=new QLabel("Package performance limit reasons\n"
        "Read active constraints and recorded events through the selected logical CPU. "
        "History records events since its last clear; current status is a momentary snapshot.",this);
    scope->setWordWrap(true); layout->addWidget(scope);
    auto *buttons=new QHBoxLayout;
    buttons->addWidget(new QLabel("Logical CPU",this));
    cpu_=new QLineEdit(QString::number(cpu),this); cpu_->setObjectName("limitsCpu"); buttons->addWidget(cpu_);
    auto *read=new QPushButton("Read reasons",this); read->setObjectName("limitsRead"); buttons->addWidget(read);
    auto *copy=new QPushButton("Copy snapshot",this); copy->setObjectName("limitsCopy"); buttons->addWidget(copy);
    layout->addLayout(buttons);
    table_=new QTableWidget(0,3,this); table_->setObjectName("limitsTable");
    table_->setHorizontalHeaderLabels({"Reason","Current status","History"});
    table_->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    table_->setEditTriggers(QAbstractItemView::NoEditTriggers); layout->addWidget(table_);
    raw_=new QLabel(this); raw_->setObjectName("limitsRaw"); raw_->setWordWrap(true); layout->addWidget(raw_);
    auto *edit=new QHBoxLayout; edit->addWidget(new QLabel("History to clear",this));
    reason_=new QComboBox(this); reason_->setObjectName("limitsReason"); reason_->addItem("All listed reasons","all");
    for (const auto &r:intelLimitReasons()) reason_->addItem(QString::fromLatin1(r.label),QString::fromLatin1(r.key));
    edit->addWidget(reason_);
    clear_=new QPushButton("Clear history…",this); clear_->setObjectName("limitsClear"); edit->addWidget(clear_); layout->addLayout(edit);
    auto *note=new QLabel("Clearing history does not remove a limit or change protection settings. An ongoing event may immediately be recorded again.",this);
    note->setWordWrap(true); layout->addWidget(note);
    status_=new QLabel("Ready. Read the reasons before clearing history.",this);
    status_->setObjectName("limitsStatus"); status_->setTextFormat(Qt::PlainText); status_->setWordWrap(true); layout->addWidget(status_);
    invalidate();
    connect(cpu_,&QLineEdit::textChanged,this,[this] { invalidate(); status_->setText("CPU changed. Read the reasons again."); });
    connect(reason_,QOverload<int>::of(&QComboBox::currentIndexChanged),this,[this] { ++revision_; });
    connect(read,&QPushButton::clicked,this,[this] { refresh(); });
    connect(clear_,&QPushButton::clicked,this,[this] { clearHistory(); });
    connect(copy,&QPushButton::clicked,this,[this] {
        QString text=status_->text()+"\n"+raw_->text()+"\nReason\tCurrent status\tHistory\n";
        for (int row=0;row<table_->rowCount();++row) {
            for (int col=0;col<table_->columnCount();++col) text+=(col?"\t":"")+table_->item(row,col)->text();
            text+="\n";
        }
        QApplication::clipboard()->setText(text);
    });
}
IntelLimitsPanel::~IntelLimitsPanel() { cancelled_->store(true); }
void IntelLimitsPanel::invalidate() {
    ++revision_; snapshot_={}; table_->setRowCount(0); raw_->clear(); reason_->setEnabled(false); clear_->setEnabled(false);
}
void IntelLimitsPanel::present(const IntelLimitsSnapshot &snapshot) {
    invalidate(); snapshot_=snapshot;
    if (!intelLimitsValid(snapshot)) { status_->setText(failure(snapshot.error?snapshot.error:-ENODATA)+" No reasons displayed."); return; }
    table_->setRowCount(int(intelLimitReasons().size())); int row=0;
    for (const auto &r:intelLimitReasons()) {
        table_->setItem(row,0,new QTableWidgetItem(QString::fromLatin1(r.label)));
        table_->setItem(row,1,new QTableWidgetItem((snapshot.raw & (UINT64_C(1)<<r.bit))?"Active":"Inactive"));
        table_->setItem(row,2,new QTableWidgetItem((snapshot.raw & (UINT64_C(1)<<(r.bit+16)))?"Recorded":"Not recorded"));
        ++row;
    }
    reason_->setEnabled(true); clear_->setEnabled(true);
    raw_->setText(QString("PERF_LIMIT_REASONS: %1").arg(hexValue(snapshot.raw,8)));
    status_->setText(QString("CPU %1 · package limit reasons read.").arg(snapshot.cpu));
}
void IntelLimitsPanel::refresh() {
    quint64 cpu=0; invalidate();
    if (!parseNumber(cpu_->text(),10,1048575,cpu)) { status_->setText("Invalid logical CPU."); return; }
    setEnabled(false); status_->setText("Reading performance limit reasons…"); const auto revision=revision_;
    auto *watcher=new QFutureWatcher<IntelLimitsSnapshot>(this);
    connect(watcher,&QFutureWatcher<IntelLimitsSnapshot>::finished,this,[this,watcher,revision] {
        if (revision==revision_) present(watcher->result());
        else { invalidate(); status_->setText("Selection changed during the read; result discarded. Read again."); }
        setEnabled(true); watcher->deleteLater();
    });
    const auto access=access_; const auto cancelled=cancelled_;
    watcher->setFuture(QtConcurrent::run([access,cancelled,cpu] {
        IntelLimitsSnapshot out;
        const int error=access->transaction([&](HardwareSession &s) { out=readIntelLimits(s,unsigned(cpu)); return out.error; },5000,cancelled.get());
        if (error) out.error=error;
        return out;
    }));
}
void IntelLimitsPanel::clearHistory() {
    if (!intelLimitsValid(snapshot_)) return;
    const auto reason=reason_->currentData().toString().toStdString();
    if (!intelLimitLogMask(reason)) { status_->setText("Select a listed reason."); return; }
    const auto old=snapshot_; const auto preparedRevision=revision_;
    if (QMessageBox::question(this,"Clear recorded limit events",QString("CPU %1 · package event history\nClear: %2\n"
        "The selected recorded events will be cleared and read back. Active limits and protection settings stay in effect.")
        .arg(old.cpu).arg(reason_->currentText()),QMessageBox::Yes|QMessageBox::Cancel,QMessageBox::Cancel)!=QMessageBox::Yes) return;
    if (preparedRevision!=revision_) { status_->setText("Selection changed. Review it before clearing history."); return; }
    invalidate(); setEnabled(false); status_->setText("Checking the snapshot, clearing history and reading back…");
    const auto revision=revision_; auto *watcher=new QFutureWatcher<IntelLimitsClear>(this);
    connect(watcher,&QFutureWatcher<IntelLimitsClear>::finished,this,[this,watcher,revision,old] {
        const auto out=watcher->result(); setEnabled(true);
        if (revision!=revision_) {
            invalidate(); status_->setText(QString("Selection changed during the operation for CPU %1; displayed result discarded. ").arg(old.cpu)+
                (out.writeAttempted?"A history clear was attempted. Read the original CPU again.":"No history clear was attempted."));
        } else if (!out.error && out.verified) {
            present(out.after); status_->setText(status_->text()+(out.unchanged?" Selected history already clear; no write performed.":" Selected history clear in readback; other recorded events preserved."));
        } else if (out.stillSetMask && intelLimitsValid(out.after) && out.error==-EAGAIN) {
            present(out.after); status_->setText("Selected history is still set after the clear attempt. An event may have been recorded again. No retry was performed.");
        } else {
            invalidate(); status_->setText(failure(out.error?out.error:-EIO)+(out.writeAttempted?
                " A history clear was attempted; the final state is not verified. No retry was performed.":" No history clear was attempted."));
        }
        watcher->deleteLater();
    });
    const auto access=access_; const auto cancelled=cancelled_;
    watcher->setFuture(QtConcurrent::run([access,cancelled,old,reason] {
        IntelLimitsClear out;
        const int error=access->transaction([&](HardwareSession &s) { out=clearIntelLimitLogs(s,old,reason); return out.error; },5000,cancelled.get());
        if (error) out.error=error;
        return out;
    }));
}
