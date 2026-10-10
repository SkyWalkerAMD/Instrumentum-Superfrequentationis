// SPDX-License-Identifier: GPL-2.0-only
#pragma once
#include "access.h"
#include "core/intel_uncore.h"
#include <QWidget>
class QLabel;
class QLineEdit;
class QPushButton;
class QTableWidget;
class IntelUncorePanel : public QWidget {
    Q_OBJECT
public:
    explicit IntelUncorePanel(std::shared_ptr<HardwareAccess> access, QWidget *parent = nullptr, unsigned cpu = 0);
    ~IntelUncorePanel() override;
private:
    void refresh();
    void apply();
    void invalidate();
    void present(const octool::core::IntelUncoreSnapshot &snapshot);
    std::shared_ptr<HardwareAccess> access_;
    std::shared_ptr<std::atomic<bool>> cancelled_;
    octool::core::IntelUncoreSnapshot snapshot_;
    QLineEdit *cpu_, *minimum_, *maximum_;
    QPushButton *apply_;
    QTableWidget *table_;
    QLabel *status_, *raw_;
};
