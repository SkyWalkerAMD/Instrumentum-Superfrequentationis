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
#include <QMessageBox>
#include <QPushButton>
#include <QTableWidget>
#include <QVBoxLayout>
#include <QtConcurrent>
#include <cerrno>
#include <cmath>
#include <cstring>
#include <utility>

IntelVfPanel::IntelVfPanel(std::shared_ptr<HardwareAccess> access, QWidget *parent, unsigned cpu)
    : QWidget(parent), access_(std::move(access)), cancelled_(std::make_shared<std::atomic<bool>>(false)) {
    setObjectName("intelVf"); auto *layout = new QVBoxLayout(this);
    auto *scope = new QLabel("Intel V/F points - Raptor Lake-S client profile\n"
        "Read the configured ratio and voltage offset for one point or candidates 1..15. "
        "Firmware may reject individual points; failed values stay blank. "
        "The logical CPU selects where the query runs, not a physical core's private curve. "
        "For editing, select one point and prepare its current settings. Xeon W790 / W890 profiles are not yet available.", this);
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
    auto *units = new QLabel("Ratios and offsets are configured values; actual frequency and voltage are not measured here. "
        "Offset editing requires unlocked, adaptive domain settings with zero target and global offset. "
        "Core editing also requires per-core override to be disabled. Preparation only queries these settings.", this);
    units->setWordWrap(true); layout->addWidget(units);
    context_ = new QLabel(this); context_->setObjectName("vfContext"); context_->setWordWrap(true); layout->addWidget(context_);
    auto *editor = new QHBoxLayout;
    auto *prepareButton = new QPushButton("Prepare selected point", this); prepareButton->setObjectName("vfPrepare"); editor->addWidget(prepareButton);
    offset_ = new QLineEdit(this); offset_->setObjectName("vfOffset"); offset_->setPlaceholderText("New offset (mV)"); editor->addWidget(offset_);
    apply_ = new QPushButton("Apply point offset…", this); apply_->setObjectName("vfApply"); apply_->setEnabled(false); editor->addWidget(apply_);
    layout->addLayout(editor);
    status_ = new QLabel("Ready. No hardware access has been performed.", this); status_->setObjectName("vfStatus");
    status_->setTextFormat(Qt::PlainText); status_->setWordWrap(true); layout->addWidget(status_);
    const auto clear = [this] { invalidateEdit(); table_->setRowCount(0); status_->setText("Target changed. Read V/F points again."); };
    connect(cpu_, &QLineEdit::textChanged, this, clear);
    connect(domain_, QOverload<int>::of(&QComboBox::currentIndexChanged), this, clear);
    connect(point_, QOverload<int>::of(&QComboBox::currentIndexChanged), this, clear);
    connect(read, &QPushButton::clicked, this, [this] { refresh(); });
    connect(prepareButton, &QPushButton::clicked, this, [this] { prepare(); });
    connect(apply_, &QPushButton::clicked, this, [this] { apply(); });
    connect(copy, &QPushButton::clicked, this, [this] {
        QString text = status_->text()+"\n"+context_->text()+"\nPoint\tRatio\tOffset (mV)\tRaw data\tResult\n";
        for (int row = 0; row < table_->rowCount(); ++row) {
            for (int col = 0; col < table_->columnCount(); ++col) text += (col ? "\t" : "") + table_->item(row, col)->text();
            text += "\n";
        }
        QApplication::clipboard()->setText(text);
    });
}
IntelVfPanel::~IntelVfPanel() { cancelled_->store(true); }
void IntelVfPanel::invalidateEdit() {
    editSnapshot_ = {}; context_->clear(); offset_->clear(); apply_->setEnabled(false);
}
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
    quint64 cpu = 0; invalidateEdit(); table_->setRowCount(0);
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
void IntelVfPanel::presentEdit(const octool::core::IntelVfEditSnapshot &s) {
    using namespace octool::core;
    invalidateEdit(); table_->setRowCount(0);
    if (s.error || !s.valid) {
        status_->setText(QString("Preparation failed (%1). No editable settings displayed.").arg(s.error ? s.error : -ENODATA)); return;
    }
    editSnapshot_ = s;
    IntelVfSnapshot view; view.cpu = s.cpu; view.domain = s.domain; view.identity = s.identity;
    view.selectedPoint = s.point; view.scanCompleted = true;
    IntelVfPoint point; point.point = s.point; point.response = s.value; view.points.push_back(point); present(view);
    const bool editable = intelVfEditable(s), locked = (s.flexRatio & (UINT64_C(1) << 20)) != 0;
    const QString reason = locked ? "Firmware is locked."
        : !intelVfDomainDefault(s) ? "Domain-wide voltage settings are not default; point editing is disabled."
        : s.domain == IntelOcDomain::Core && (s.control.data & 8) ? "Per-core override is enabled; core point editing is disabled."
        : "Prepared. Enter a new offset; firmware limits still apply.";
    context_->setText(QString("Control: %1 · Domain configuration: %2 · Lock register: %3\n%4")
        .arg(hexValue(s.control.data, 4)).arg(hexValue(s.legacy.data, 4)).arg(hexValue(s.flexRatio, 8)).arg(reason));
    apply_->setEnabled(editable);
}
void IntelVfPanel::prepare() {
    using namespace octool::core;
    invalidateEdit(); table_->setRowCount(0); quint64 cpu = 0;
    if (!parseNumber(cpu_->text(), 10, 1048575, cpu)) { status_->setText("Invalid logical CPU."); return; }
    const unsigned point = point_->currentData().toUInt();
    if (!point) { status_->setText("Select one point (1..15) before preparing an offset change."); return; }
    const auto domain = IntelOcDomain(domain_->currentData().toInt());
    setEnabled(false); status_->setText("Reading the point, lock, domain configuration and override state…");
    auto *watcher = new QFutureWatcher<IntelVfEditSnapshot>(this);
    connect(watcher, &QFutureWatcher<IntelVfEditSnapshot>::finished, this, [this, watcher] {
        presentEdit(watcher->result()); setEnabled(true); watcher->deleteLater();
    });
    const auto access = access_; const auto cancelled = cancelled_;
    watcher->setFuture(QtConcurrent::run([access, cancelled, cpu, domain, point] {
        IntelVfEditSnapshot out;
        const int error = access->transaction([&](HardwareSession &s) {
            out = readIntelVfEdit(s, unsigned(cpu), domain, point); return out.error;
        }, 5000, cancelled.get());
        if (error) out.error = error;
        return out;
    }));
}
void IntelVfPanel::apply() {
    using namespace octool::core;
    bool parsed = false; const double value = offset_->text().toDouble(&parsed);
    std::uint32_t encoded = 0;
    if (!intelVfEditable(editSnapshot_) || !apply_->isEnabled() || !parsed || !std::isfinite(value) ||
        encodeIntelOcOffset(value, editSnapshot_.value.data, encoded)) {
        status_->setText("Prepare an editable point and enter a finite offset from -1000 to 999.0234375 mV."); return;
    }
    const auto old = editSnapshot_;
    if (QMessageBox::question(this, "Apply V/F point offset",
        QString("CPU %1 · %2 · point %3\nOffset: %4 → %5 mV (encoded %6 mV).\n"
                "Only the selected point offset is submitted. Domain and override modes are kept. "
                "Operating settings can affect stability; the point and context will be read back. Errors are not retried.")
        .arg(old.cpu).arg(old.domain == IntelOcDomain::Core ? "Core" : "Cache / ring").arg(old.point)
        .arg(intelOcOffsetMillivolts(old.value.data), 0, 'g', 12).arg(value, 0, 'g', 12)
        .arg(intelOcOffsetMillivolts(encoded), 0, 'g', 12),
        QMessageBox::Yes | QMessageBox::Cancel, QMessageBox::Cancel) != QMessageBox::Yes) return;
    invalidateEdit(); table_->setRowCount(0); setEnabled(false); status_->setText("Checking current settings, applying the point offset and verifying readback…");
    auto *watcher = new QFutureWatcher<IntelVfUpdate>(this);
    connect(watcher, &QFutureWatcher<IntelVfUpdate>::finished, this, [this, watcher] {
        const auto out = watcher->result(); setEnabled(true);
        if (!out.error && out.verified) {
            presentEdit(out.readback);
            status_->setText(status_->text() + (out.unchanged ? " Requested offset already present; no settings write performed."
                : " Full point readback verified; domain configuration and override state unchanged."));
        } else {
            invalidateEdit(); table_->setRowCount(0);
            status_->setText(QString("Failed (%1). ").arg(out.error ? out.error : -EIO) + (out.writeAttempted
                ? "A settings write was attempted; the result is not verified. No retry or rollback was performed."
                : "No settings write was attempted. Prepare the point again."));
        }
        watcher->deleteLater();
    });
    const auto access = access_; const auto cancelled = cancelled_;
    watcher->setFuture(QtConcurrent::run([access, cancelled, old, value] {
        IntelVfUpdate out;
        const int error = access->transaction([&](HardwareSession &s) { out = applyIntelVfOffset(s, old, value); return out.error; }, 5000, cancelled.get());
        if (error) out.error = error;
        return out;
    }));
}
