// SPDX-License-Identifier: GPL-2.0-only
#pragma once
#include "access.h"
#include <QWidget>
class QLabel;
class QPushButton;

class AuthorizationPanel final : public QWidget {
public:
    explicit AuthorizationPanel(std::shared_ptr<HardwareAccess> access, QWidget *parent = nullptr);
    ~AuthorizationPanel() override;
private:
    void start();
    std::shared_ptr<HardwareAccess> access_;
    std::shared_ptr<std::atomic<bool>> cancelled_;
    QLabel *status_;
    QPushButton *authorize_, *cancel_;
};
