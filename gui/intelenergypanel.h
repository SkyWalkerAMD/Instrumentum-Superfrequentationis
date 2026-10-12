// SPDX-License-Identifier: GPL-2.0-only
#pragma once
#include "access.h"
#include "core/intel_energy.h"
#include <QWidget>
class QComboBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QTableWidget;
class IntelEnergyPanel : public QWidget {
    Q_OBJECT
public:
    explicit IntelEnergyPanel(std::shared_ptr<HardwareAccess> access, QWidget *parent = nullptr, unsigned cpu = 0);
    ~IntelEnergyPanel() override;
private:
    void refresh();
    void apply();
    void invalidate();
    void selectCurrent();
    void present(const octool::core::IntelEnergySnapshot &snapshot);
    std::shared_ptr<HardwareAccess> access_;
    std::shared_ptr<std::atomic<bool>> cancelled_;
    octool::core::IntelEnergySnapshot snapshot_;
    unsigned revision_ = 0;
    QLineEdit *cpu_;
    QComboBox *field_, *value_;
    QPushButton *apply_;
    QTableWidget *table_;
    QLabel *status_, *raw_;
};
