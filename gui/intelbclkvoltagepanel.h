// SPDX-License-Identifier: GPL-2.0-only
#pragma once
#include "access.h"
#include "core/intel_bclk_voltage.h"
#include <QWidget>
class QLabel;
class QComboBox;
class QLineEdit;
class QPushButton;
class QTableWidget;
class IntelBclkVoltagePanel : public QWidget {
    Q_OBJECT
public:
    explicit IntelBclkVoltagePanel(std::shared_ptr<HardwareAccess> access, QWidget *parent=nullptr, unsigned cpu=0);
    ~IntelBclkVoltagePanel() override;
private:
    void refresh();
    void apply();
    void invalidate();
    void present(const octool::core::IntelBclkVoltageSnapshot &snapshot);
    std::shared_ptr<HardwareAccess> access_;
    std::shared_ptr<std::atomic<bool>> cancelled_;
    octool::core::IntelBclkVoltageSnapshot snapshot_;
    unsigned revision_=0;
    QLineEdit *cpu_;
    QComboBox *value_;
    QPushButton *apply_;
    QTableWidget *table_;
    QLabel *status_, *raw_;
};
