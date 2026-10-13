// SPDX-License-Identifier: GPL-2.0-only
#include "intelavxpanel.h"
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
IntelAvxPanel::IntelAvxPanel(std::shared_ptr<HardwareAccess> access, QWidget *parent, unsigned cpu)
    : QWidget(parent), access_(std::move(access)), cancelled_(std::make_shared<std::atomic<bool>>(false)) {
    setObjectName("intelAvx"); auto *layout=new QVBoxLayout(this);
    auto *scope=new QLabel("AVX2 ratio offset - Raptor Lake-S\n"
        "Reduce the AVX2 ratio relative to the core ratio limit by 0 to 31 bins. Zero requests no offset. "
        "This shared processor setting is accessed through the selected logical CPU.",this);
    scope->setWordWrap(true); layout->addWidget(scope);
    auto *buttons=new QHBoxLayout;
    buttons->addWidget(new QLabel("Logical CPU",this));
    cpu_=new QLineEdit(QString::number(cpu),this); cpu_->setObjectName("avxCpu"); buttons->addWidget(cpu_);
    auto *read=new QPushButton("Read configuration",this); read->setObjectName("avxRead"); buttons->addWidget(read);
    auto *copy=new QPushButton("Copy snapshot",this); copy->setObjectName("avxCopy"); buttons->addWidget(copy);
    layout->addLayout(buttons);
    table_=new QTableWidget(0,3,this); table_->setObjectName("avxTable");
    table_->setHorizontalHeaderLabels({"Setting","Configured value","Access"});
    table_->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    table_->setEditTriggers(QAbstractItemView::NoEditTriggers); layout->addWidget(table_);
    raw_=new QLabel(this); raw_->setObjectName("avxRaw"); raw_->setWordWrap(true); layout->addWidget(raw_);
    auto *edit=new QHBoxLayout; edit->addWidget(new QLabel("AVX2 negative ratio offset (bins)",this));
    value_=new QLineEdit(this); value_->setObjectName("avxValue"); value_->setPlaceholderText("0..31"); edit->addWidget(value_);
    apply_=new QPushButton("Apply offset…",this); apply_->setObjectName("avxApply"); edit->addWidget(apply_); layout->addLayout(edit);
    auto *note=new QLabel("Readback verifies configuration. The resulting frequency and stability depend on the workload, firmware and other limits.",this);
    note->setWordWrap(true); layout->addWidget(note);
    status_=new QLabel("Ready. Read the configuration before making a change.",this);
    status_->setObjectName("avxStatus"); status_->setTextFormat(Qt::PlainText); status_->setWordWrap(true); layout->addWidget(status_);
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
IntelAvxPanel::~IntelAvxPanel() { cancelled_->store(true); }
void IntelAvxPanel::invalidate() {
    ++revision_; snapshot_={}; table_->setRowCount(0); raw_->clear(); value_->clear(); value_->setEnabled(false); apply_->setEnabled(false);
}
void IntelAvxPanel::present(const IntelAvxSnapshot &snapshot) {
    invalidate(); snapshot_=snapshot;
    if (snapshot.error || !snapshot.valid) { status_->setText(failure(snapshot.error?snapshot.error:-ENODATA)+" No settings displayed."); return; }
    const bool editable=intelAvxEditable(snapshot);
    table_->setRowCount(1); table_->setItem(0,0,new QTableWidgetItem("AVX2 negative ratio offset"));
    table_->setItem(0,1,new QTableWidgetItem(QString::number(intelAvx2Offset(snapshot.response.data))+" bins"));
    table_->setItem(0,2,new QTableWidgetItem(editable?"Available for editing":"Firmware OC lock set"));
    value_->setText(QString::number(intelAvx2Offset(snapshot.response.data))); value_->setEnabled(editable); apply_->setEnabled(editable);
    raw_->setText(QString("Configuration: %1 · FLEX_RATIO: %2").arg(hexValue(snapshot.response.data,4)).arg(hexValue(snapshot.flexRatio,8)));
    status_->setText(QString("CPU %1 · AVX2 configuration read. %2").arg(snapshot.cpu).arg(editable?"Enter an offset to apply.":"Firmware overclocking lock prevents changes."));
}
void IntelAvxPanel::refresh() {
    quint64 cpu=0; invalidate();
    if (!parseNumber(cpu_->text(),10,1048575,cpu)) { status_->setText("Invalid logical CPU."); return; }
    setEnabled(false); status_->setText("Reading AVX2 configuration…"); const auto revision=revision_;
    auto *watcher=new QFutureWatcher<IntelAvxSnapshot>(this);
    connect(watcher,&QFutureWatcher<IntelAvxSnapshot>::finished,this,[this,watcher,revision] {
        if (revision==revision_) present(watcher->result());
        else { invalidate(); status_->setText("CPU changed during the read; result discarded. Read again."); }
        setEnabled(true); watcher->deleteLater();
    });
    const auto access=access_; const auto cancelled=cancelled_;
    watcher->setFuture(QtConcurrent::run([access,cancelled,cpu] {
        IntelAvxSnapshot out;
        const int error=access->transaction([&](HardwareSession &s) { out=readIntelAvx(s,unsigned(cpu)); return out.error; },5000,cancelled.get());
        if (error) out.error=error;
        return out;
    }));
}
void IntelAvxPanel::apply() {
    if (!intelAvxEditable(snapshot_)) return;
    quint64 bins=0;
    if (!parseNumber(value_->text(),10,31,bins)) { status_->setText("Offset must be an integer from 0 to 31."); return; }
    const auto old=snapshot_; const auto preparedRevision=revision_;
    if (QMessageBox::question(this,"Apply AVX2 offset",QString("CPU %1 · shared AVX2 configuration\nOffset: %2 → %3 bins.\n"
        "This changes AVX2 performance and can affect stability. The offset will be applied and read back.")
        .arg(old.cpu).arg(intelAvx2Offset(old.response.data)).arg(bins),QMessageBox::Yes|QMessageBox::Cancel,QMessageBox::Cancel)!=QMessageBox::Yes) return;
    if (preparedRevision!=revision_) { status_->setText("CPU changed. Read the configuration again."); return; }
    invalidate(); setEnabled(false); status_->setText("Checking current settings, applying and verifying readback…");
    const auto revision=revision_; auto *watcher=new QFutureWatcher<IntelAvxUpdate>(this);
    connect(watcher,&QFutureWatcher<IntelAvxUpdate>::finished,this,[this,watcher,revision,old] {
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
    watcher->setFuture(QtConcurrent::run([access,cancelled,old,bins] {
        IntelAvxUpdate out;
        const int error=access->transaction([&](HardwareSession &s) { out=applyIntelAvx(s,old,unsigned(bins)); return out.error; },5000,cancelled.get());
        if (error) out.error=error;
        return out;
    }));
}
