// SPDX-License-Identifier: GPL-2.0-only
#include "inteltvbtablepanel.h"
#include <QApplication>
#include <QClipboard>
#include <QComboBox>
#include <QFutureWatcher>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QTableWidget>
#include <QVBoxLayout>
#include <QtConcurrent>
#include <cstring>
#include <utility>

using namespace octool::core;
IntelTvbTablePanel::IntelTvbTablePanel(std::shared_ptr<HardwareAccess> access, QWidget *parent, unsigned cpu)
    : QWidget(parent), access_(std::move(access)), cancelled_(std::make_shared<std::atomic<bool>>(false)) {
    setObjectName("intelTvbTable"); auto *layout = new QVBoxLayout(this);
    auto *scope = new QLabel("Intel Thermal Velocity Boost thresholds - Raptor Lake-S\n"
        "Read temperature thresholds and ratio reductions by active P-core count. "
        "Choose one entry or query all eight candidates. Firmware may reject entries that are unavailable.", this);
    scope->setWordWrap(true); layout->addWidget(scope);
    auto *buttons = new QHBoxLayout;
    buttons->addWidget(new QLabel("Logical CPU", this));
    cpu_ = new QLineEdit(QString::number(cpu), this); cpu_->setObjectName("tvbTableCpu"); buttons->addWidget(cpu_);
    buttons->addWidget(new QLabel("Active P-cores", this));
    entry_ = new QComboBox(this); entry_->setObjectName("tvbTableEntry"); entry_->addItem("All 8 candidates", 0);
    for (int n = 1; n <= 8; ++n) entry_->addItem(QString::number(n), n);
    buttons->addWidget(entry_);
    auto *read = new QPushButton("Read thresholds", this); read->setObjectName("tvbTableRead"); buttons->addWidget(read);
    auto *copy = new QPushButton("Copy table", this); copy->setObjectName("tvbTableCopy"); buttons->addWidget(copy);
    layout->addLayout(buttons);
    table_ = new QTableWidget(0, 7, this); table_->setObjectName("tvbThresholdTable");
    table_->setHorizontalHeaderLabels({"Active P-cores", "Temp A (°C)", "Down bins A", "Temp B (°C)", "Down bins B", "Raw", "Result"});
    table_->verticalHeader()->hide(); table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table_->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    table_->horizontalHeader()->setStretchLastSection(true); layout->addWidget(table_, 1);
    auto *note = new QLabel("Read only. Each row describes a configuration entry, not a physical core or measured frequency. "
        "Temperature A and B each have a configured ratio reduction. Values do not prove that TVB is active; "
        "the table is sampled sequentially and is not an atomic snapshot.", this);
    note->setWordWrap(true); layout->addWidget(note);
    status_ = new QLabel("Ready. Read the thresholds to populate the table.", this);
    status_->setObjectName("tvbTableStatus"); status_->setWordWrap(true); status_->setTextFormat(Qt::PlainText); layout->addWidget(status_);
    connect(cpu_, &QLineEdit::textChanged, this, [this] { invalidate(); });
    connect(entry_, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this] { invalidate(); });
    connect(read, &QPushButton::clicked, this, [this] { refresh(); });
    connect(copy, &QPushButton::clicked, this, [this] {
        QString text = status_->text()+"\n";
        for (int col = 0; col < table_->columnCount(); ++col) text += (col ? "\t" : "")+table_->horizontalHeaderItem(col)->text();
        text += "\n";
        for (int row = 0; row < table_->rowCount(); ++row) {
            for (int col = 0; col < table_->columnCount(); ++col) text += (col ? "\t" : "")+table_->item(row,col)->text();
            text += "\n";
        }
        QApplication::clipboard()->setText(text);
    });
}
IntelTvbTablePanel::~IntelTvbTablePanel() { cancelled_->store(true); }
void IntelTvbTablePanel::invalidate() {
    ++revision_; table_->setRowCount(0); status_->setText("Selection changed. Read the thresholds again.");
}
void IntelTvbTablePanel::present(const IntelTvbTableSnapshot &snapshot) {
    table_->setRowCount(0); unsigned validCount = 0;
    for (const auto &row : snapshot.rows) {
        const bool valid = intelTvbTableRowValid(row);
        const auto fields = decodeIntelTvbThresholds(row.response.data);
        const QString unavailable = "—";
        const QString result = valid ? "Read" : row.response.completed
            ? QString("Firmware status %1").arg(row.response.firmwareStatus)
            : QString("Read failed (%1)").arg(row.response.error);
        const QStringList cells{QString::number(row.activeCores),
            valid ? QString::number(fields.temperatureA) : unavailable,
            valid ? QString::number(fields.downBinsA) : unavailable,
            valid ? QString::number(fields.temperatureB) : unavailable,
            valid ? QString::number(fields.downBinsB) : unavailable,
            valid ? hexValue(row.response.data,4) : unavailable, result};
        const int n = table_->rowCount(); table_->insertRow(n);
        for (int col = 0; col < cells.size(); ++col) table_->setItem(n,col,new QTableWidgetItem(cells[col]));
        if (valid) ++validCount;
    }
    const unsigned requested = snapshot.selectedActiveCores ? 1 : 8;
    status_->setText(QString("CPU %1 · %2 of %3 requested entries read. %4%5")
        .arg(snapshot.cpu).arg(validCount).arg(requested)
        .arg(snapshot.scanCompleted ? "Scan completed. " : "Scan stopped; unqueried entries are omitted. ")
        .arg(snapshot.error ? QString("Error %1: %2. No settings changed.").arg(snapshot.error)
            .arg(QString::fromLocal8Bit(std::strerror(-snapshot.error))) : "No settings changed."));
}
void IntelTvbTablePanel::refresh() {
    quint64 cpu = 0; invalidate();
    if (!parseNumber(cpu_->text(),10,1048575,cpu) || entry_->currentIndex() < 0) {
        status_->setText("Invalid CPU or active-core selection."); return;
    }
    const unsigned entry = unsigned(entry_->currentData().toUInt()), revision = revision_;
    setEnabled(false); status_->setText("Reading TVB thresholds…");
    auto *watcher = new QFutureWatcher<IntelTvbTableSnapshot>(this);
    connect(watcher, &QFutureWatcher<IntelTvbTableSnapshot>::finished, this, [this,watcher,revision] {
        if (revision == revision_) present(watcher->result());
        else { invalidate(); status_->setText("Selection changed during the read; result discarded. Read again."); }
        setEnabled(true); watcher->deleteLater();
    });
    const auto access = access_; const auto cancelled = cancelled_;
    watcher->setFuture(QtConcurrent::run([access,cancelled,cpu,entry] {
        IntelTvbTableSnapshot out;
        const int error = access->transaction([&](HardwareSession &s) {
            out = readIntelTvbTable(s,unsigned(cpu),entry); return out.error;
        },5000,cancelled.get());
        if (error) out.error = error;
        return out;
    }));
}
