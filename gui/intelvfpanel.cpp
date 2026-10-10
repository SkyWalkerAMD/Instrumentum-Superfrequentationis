// SPDX-License-Identifier: GPL-2.0-only
#include "intelvfpanel.h"
#include <QApplication>
#include <QClipboard>
#include <QComboBox>
#include <QDateTime>
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

IntelVfPanel::IntelVfPanel(std::shared_ptr<HardwareAccess> access, QWidget *parent, unsigned cpu)
    : QWidget(parent), access_(std::move(access)), cancelled_(std::make_shared<std::atomic<bool>>(false)) {
    setObjectName("intelVf"); auto *layout = new QVBoxLayout(this);
    auto *scope = new QLabel("Intel V/F points - Raptor Lake-S client profile\n"
        "Read the configured ratio and voltage offset for one point or candidates 1..15. "
        "Firmware may reject individual points; failed values stay blank. "
        "The logical CPU selects where the query runs, not a physical core's private curve. "
        "V/F editing and Xeon W790 / W890 profiles are not yet available.", this);
    scope->setWordWrap(true); layout->addWidget(scope);
    auto *buttons = new QHBoxLayout;
    buttons->addWidget(new QLabel("Logical CPU", this));
    cpu_ = new QLineEdit(QString::number(cpu), this); cpu_->setObjectName("vfCpu"); buttons->addWidget(cpu_);
    domain_ = new QComboBox(this); domain_->setObjectName("vfDomain");
    domain_->addItem("Core", int(octool::core::IntelOcDomain::Core));
    domain_->addItem("Cache / ring", int(octool::core::IntelOcDomain::Cache)); buttons->addWidget(domain_);
    point_ = new QComboBox(this); point_->setObjectName("vfPoint"); point_->addItem("All candidates (1..15)", 0);
    for (unsigned i = 1; i <= 15; ++i) point_->addItem(QString("Point %1").arg(i), i);
    buttons->addWidget(point_);
    auto *read = new QPushButton("Read V/F points", this); read->setObjectName("vfRead"); buttons->addWidget(read);
    auto *copy = new QPushButton("Copy snapshot", this); copy->setObjectName("vfCopy"); buttons->addWidget(copy);
    layout->addLayout(buttons);
    table_ = new QTableWidget(0, 5, this); table_->setObjectName("vfTable");
    table_->setHorizontalHeaderLabels({"Point", "Ratio", "Offset (mV)", "Raw data", "Result"});
    table_->setEditTriggers(QAbstractItemView::NoEditTriggers); table_->verticalHeader()->hide();
    table_->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    table_->horizontalHeader()->setStretchLastSection(true); layout->addWidget(table_, 1);
    auto *units = new QLabel("Ratios and offsets are configured values. Actual frequency and voltage are not measured here. "
        "Reading submits firmware queries without changing V/F settings or override mode.", this);
    units->setWordWrap(true); layout->addWidget(units);
    status_ = new QLabel("Ready. No hardware access has been performed.", this); status_->setObjectName("vfStatus");
    status_->setTextFormat(Qt::PlainText); status_->setWordWrap(true); layout->addWidget(status_);
    const auto clear = [this] { table_->setRowCount(0); status_->setText("Target changed. Read V/F points again."); };
    connect(cpu_, &QLineEdit::textChanged, this, clear);
    connect(domain_, QOverload<int>::of(&QComboBox::currentIndexChanged), this, clear);
    connect(point_, QOverload<int>::of(&QComboBox::currentIndexChanged), this, clear);
    connect(read, &QPushButton::clicked, this, [this] { refresh(); });
    connect(copy, &QPushButton::clicked, this, [this] {
        QString text = status_->text()+"\nPoint\tRatio\tOffset (mV)\tRaw data\tResult\n";
        for (int row = 0; row < table_->rowCount(); ++row) {
            for (int col = 0; col < table_->columnCount(); ++col) text += (col ? "\t" : "") + table_->item(row, col)->text();
            text += "\n";
        }
        QApplication::clipboard()->setText(text);
    });
}
IntelVfPanel::~IntelVfPanel() { cancelled_->store(true); }
void IntelVfPanel::present(const octool::core::IntelVfSnapshot &s) {
    table_->setRowCount(0); unsigned valid = 0;
    for (const auto &point : s.points) {
        const auto &r = point.response; const bool ok = !r.error && r.completed;
        if (ok) ++valid;
        QString result = "Read";
        if (!ok) {
            result = QString("Failed (%1)").arg(r.error);
            if (r.completed) result += QString(" - firmware 0x%1").arg(r.firmwareStatus, 2, 16, QChar('0'));
            else result += " - query incomplete";
        }
        const QStringList values = {QString::number(point.point), ok ? QString::number(r.data & 255) : "",
            ok ? QString::number(octool::core::intelOcOffsetMillivolts(r.data), 'g', 12) : "", ok ? hexValue(r.data, 4) : "", result};
        const int row = table_->rowCount(); table_->insertRow(row);
        for (int col = 0; col < values.size(); ++col) table_->setItem(row, col, new QTableWidgetItem(values[col]));
    }
    const QString selection = s.selectedPoint ? QString("point %1").arg(s.selectedPoint) : "candidates 1..15";
    QString text = QString("CPU %1 - %2 - %3 - %4 valid / %5 requested. %6")
        .arg(s.cpu).arg(s.domain == octool::core::IntelOcDomain::Core ? "Core" : "Cache / ring")
        .arg(selection).arg(valid).arg(s.selectedPoint ? 1 : 15)
        .arg(s.scanCompleted ? "Queries completed." : "Queries incomplete.");
    if (s.error) text += QString(" Failed (%1): %2.").arg(s.error).arg(QString::fromLocal8Bit(std::strerror(-s.error)));
    status_->setText(text+" "+QDateTime::currentDateTime().toString(Qt::ISODate));
}
void IntelVfPanel::refresh() {
    quint64 cpu = 0; table_->setRowCount(0);
    if (!parseNumber(cpu_->text(), 10, 1048575, cpu)) { status_->setText("Invalid logical CPU."); return; }
    const auto domain = octool::core::IntelOcDomain(domain_->currentData().toInt());
    const unsigned point = point_->currentData().toUInt();
    setEnabled(false); status_->setText("Reading V/F points...");
    auto *watcher = new QFutureWatcher<octool::core::IntelVfSnapshot>(this);
    connect(watcher, &QFutureWatcher<octool::core::IntelVfSnapshot>::finished, this, [this, watcher] {
        present(watcher->result()); setEnabled(true); watcher->deleteLater();
    });
    const auto access = access_; const auto cancelled = cancelled_;
    watcher->setFuture(QtConcurrent::run([access, cancelled, cpu, domain, point] {
        octool::core::IntelVfSnapshot out; out.cpu = unsigned(cpu); out.domain = domain; out.selectedPoint = point;
        const int error = access->transaction([&](octool::core::HardwareSession &s) {
            out = octool::core::readIntelVf(s, unsigned(cpu), domain, point); return out.error;
        }, 5000, cancelled.get());
        if (error) out.error = error;
        return out;
    }));
}
