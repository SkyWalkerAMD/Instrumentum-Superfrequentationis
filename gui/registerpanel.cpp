// SPDX-License-Identifier: GPL-2.0-only
#include "registerpanel.h"
#include <QComboBox>
#include <QDateTime>
#include <QFormLayout>
#include <QFutureWatcher>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QVBoxLayout>
#include <QtConcurrent/QtConcurrentRun>
#include <cstring>
#include <limits>
#include <cerrno>

QLineEdit *RegisterPanel::field(const QString &name, const QString &initial)
{
    auto *edit = new QLineEdit(initial, this);
    edit->setObjectName(name);
    edit->setClearButtonEnabled(true);
    fields_[name] = edit;
    return edit;
}

RegisterPanel::RegisterPanel(Space space, std::shared_ptr<HardwareAccess> access, QWidget *parent, unsigned initialCpu)
    : QWidget(parent), space_(space), access_(std::move(access))
{
    setObjectName(space == Space::Msr ? "rw_msr" : space == Space::Memory ? "rw_memory" : "rw_pci");
    auto *layout = new QVBoxLayout(this);
    auto *description = new QLabel(space == Space::Pci ?
        "Raw PCI configuration · domain 0000 · first 256 bytes" :
        space == Space::Msr ? "Raw MSR access · one logical CPU per operation" :
        "Raw physical MMIO access · explicit address and width", this);
    description->setWordWrap(true);
    layout->addWidget(description);
    auto *form = new QFormLayout;
    if (space == Space::Msr) form->addRow("Logical CPU (decimal)", field("cpu", QString::number(initialCpu)));
    if (space == Space::Pci) {
        form->addRow("Bus (hex)", field("bus"));
        form->addRow("Device (hex)", field("device"));
        form->addRow("Function (hex)", field("function"));
    }
    form->addRow(space == Space::Msr ? "MSR index (hex)" :
                 space == Space::Memory ? "Physical address (hex)" : "Offset (hex)", field("address"));
    width_ = new QComboBox(this);
    width_->setObjectName("width");
    for (int bytes : {1, 2, 4, 8}) {
        if (space == Space::Msr && bytes != 8) continue;
        if (space == Space::Pci && bytes == 8) continue;
        width_->addItem(QString::number(bytes * 8) + " bits", bytes);
    }
    form->addRow("Width", width_);
    form->addRow("Write value (hex)", field("value"));
    result_ = new QLineEdit(this);
    result_->setObjectName("result");
    result_->setReadOnly(true);
    form->addRow("Read result (hex)", result_);
    layout->addLayout(form);
    auto *buttons = new QHBoxLayout;
    auto *read = new QPushButton("Read once", this);
    auto *write = new QPushButton("Write…", this);
    read->setObjectName("read"); write->setObjectName("write");
    buttons->addWidget(read); buttons->addWidget(write); buttons->addStretch();
    layout->addLayout(buttons);
    status_ = new QLabel("Ready. No register access has been performed.", this);
    status_->setObjectName("status"); status_->setWordWrap(true);
    layout->addWidget(status_);
    history_ = new QPlainTextEdit(this);
    history_->setObjectName("history"); history_->setReadOnly(true);
    history_->setMaximumBlockCount(500);
    layout->addWidget(history_, 1);
    connect(read, &QPushButton::clicked, this, [this] { submit(false); });
    connect(write, &QPushButton::clicked, this, [this] { submit(true); });
    for (auto *edit : fields_) connect(edit, &QLineEdit::textChanged, result_, &QLineEdit::clear);
    connect(width_, QOverload<int>::of(&QComboBox::currentIndexChanged), result_, &QLineEdit::clear);
}

void RegisterPanel::submit(bool write)
{
    Request request;
    request.space = space_; request.write = write; request.width = width_->currentData().toInt();
    auto number = [this](const QString &name, int base, quint64 maximum, quint64 &out) {
        if (parseNumber(fields_[name]->text(), base, maximum, out)) return true;
        status_->setText("Invalid " + name + ": enter an unsigned " +
                         (base == 16 ? "hexadecimal" : "decimal") + " value within range.");
        fields_[name]->setFocus();
        return false;
    };
    quint64 n = 0;
    if (!number("address", 16, std::numeric_limits<quint64>::max(), n)) return;
    request.address = n;
    if (space_ == Space::Msr) {
        if (!number("cpu", 10, UINT32_MAX, n)) return;
        request.cpu = unsigned(n);
    }
    if (space_ == Space::Pci) {
        if (!number("bus", 16, 255, n)) return;
        request.bus = unsigned(n);
        if (!number("device", 16, 31, n)) return;
        request.device = unsigned(n);
        if (!number("function", 16, 7, n)) return;
        request.function = unsigned(n);
    }
    if (write) {
        if (!number("value", 16, std::numeric_limits<quint64>::max(), n)) return;
        request.value = n;
    }
    QString invalid = validate(request);
    if (!invalid.isEmpty()) { status_->setText(invalid); return; }
    if (write && QMessageBox::question(this, "Confirm register write",
            QString("Write %1 (%2 bits) to %3?\nNo automatic readback will be issued.")
                .arg(hexValue(request.value, request.width)).arg(request.width * 8).arg(targetText(request)),
            QMessageBox::Yes | QMessageBox::Cancel, QMessageBox::Cancel) != QMessageBox::Yes) return;
    result_->clear();
    status_->setText("Working…");
    setEnabled(false);
    auto *watcher = new QFutureWatcher<Reply>(this);
    connect(watcher, &QFutureWatcher<Reply>::finished, this, [this, watcher, request] {
        Reply reply = watcher->result();
        setEnabled(true);
        QString message = targetText(request) + ": ";
        if (reply.error) {
            message += QString("FAILED (%1): %2").arg(reply.error)
                .arg(QString::fromLocal8Bit(std::strerror(-reply.error)));
            if (request.write && (reply.error == -ETIMEDOUT || reply.error == -EPIPE ||
                reply.error == -EPROTO || reply.error == -ENOTCONN))
                message += ". Write completion is unknown; inspect the hardware state before retrying.";
        } else if (request.write) {
            message += "write submitted " + hexValue(request.value, request.width) + " (not read back)";
        } else {
            result_->setText(hexValue(reply.value, request.width));
            message += result_->text();
        }
        status_->setText(message);
        history_->appendPlainText(QDateTime::currentDateTime().toString(Qt::ISODate) + "  " + message);
        watcher->deleteLater();
    });
    auto access = access_;
    watcher->setFuture(QtConcurrent::run([access, request] { return access->execute(request); }));
}
