// SPDX-License-Identifier: GPL-2.0-only
#include "authorizationpanel.h"
#include <QFutureWatcher>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>
#include <QtConcurrent>
#include <cerrno>
#include <cstring>

AuthorizationPanel::AuthorizationPanel(std::shared_ptr<HardwareAccess> access, QWidget *parent)
    : QWidget(parent), access_(std::move(access))
{
    auto *layout = new QVBoxLayout(this);
    status_ = new QLabel("Hardware authorization has not been requested. Basic information is available.", this);
    status_->setObjectName("authorizationStatus"); status_->setWordWrap(true);
    layout->addWidget(status_);
    auto *buttons = new QHBoxLayout;
    authorize_ = new QPushButton("Authorize hardware access…", this);
    authorize_->setObjectName("authorizeHardware");
    cancel_ = new QPushButton("Cancel", this); cancel_->setObjectName("cancelAuthorization");
    cancel_->setEnabled(false);
    buttons->addWidget(authorize_); buttons->addWidget(cancel_); buttons->addStretch();
    layout->addLayout(buttons);
    connect(authorize_, &QPushButton::clicked, this, [this] { start(); });
    connect(cancel_, &QPushButton::clicked, this, [this] {
        if (cancelled_) cancelled_->store(true);
        cancel_->setEnabled(false); status_->setText("Cancelling authorization…");
    });
}

AuthorizationPanel::~AuthorizationPanel()
{
    if (cancelled_) cancelled_->store(true);
}

void AuthorizationPanel::start()
{
    cancelled_ = std::make_shared<std::atomic<bool>>(false);
    authorize_->setEnabled(false); cancel_->setEnabled(true);
    status_->setText("Waiting for administrator authentication…");
    auto *watcher = new QFutureWatcher<int>(this);
    connect(watcher, &QFutureWatcher<int>::finished, this, [this, watcher] {
        const int error = watcher->result();
        authorize_->setEnabled(true); cancel_->setEnabled(false);
        if (!error) status_->setText("Hardware access authorized for this application session.");
        else if (error == -ECANCELED) status_->setText("Authorization cancelled. The previous connection is unchanged.");
        else status_->setText(QString("Hardware authorization failed (%1): %2. "
            "Check the helper/polkit installation and your desktop authentication agent.")
            .arg(error).arg(QString::fromLocal8Bit(std::strerror(-error))));
        watcher->deleteLater();
    });
    const auto access = access_;
    const auto cancelled = cancelled_;
    watcher->setFuture(QtConcurrent::run([access, cancelled] { return access->authorize(*cancelled); }));
}
