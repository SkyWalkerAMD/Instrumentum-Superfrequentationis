// SPDX-License-Identifier: GPL-2.0-only
#pragma once
#include "access.h"
#include "core/intel_thermal.h"
#include <QWidget>
class QLabel;
class QLineEdit;
class QPushButton;
class QTableWidget;
class IntelThermalPanel : public QWidget {
    Q_OBJECT
public:
    explicit IntelThermalPanel(std::shared_ptr<HardwareAccess> access, QWidget *parent=nullptr, unsigned cpu=0);
    ~IntelThermalPanel() override;
private:
    void refresh();
    void apply();
    void invalidate();
    void present(const octool::core::IntelThermalSnapshot &snapshot);
    std::shared_ptr<HardwareAccess> access_;
    std::shared_ptr<std::atomic<bool>> cancelled_;
    octool::core::IntelThermalSnapshot snapshot_;
    unsigned revision_=0;
    QLineEdit *cpu_, *value_;
    QPushButton *apply_;
    QTableWidget *table_;
    QLabel *status_, *raw_;
};
