// SPDX-License-Identifier: GPL-2.0-only
#pragma once
#include "access.h"
#include "core/intel_iccmax.h"
#include <QWidget>
class QLabel;
class QDoubleSpinBox;
class QLineEdit;
class QPushButton;
class QTableWidget;
class IntelIccMaxPanel : public QWidget {
    Q_OBJECT
public:
    explicit IntelIccMaxPanel(std::shared_ptr<HardwareAccess> access, QWidget *parent=nullptr, unsigned cpu=0);
    ~IntelIccMaxPanel() override;
private:
    void refresh();
    void apply();
    void invalidate();
    void present(const octool::core::IntelIccMaxSnapshot &snapshot);
    std::shared_ptr<HardwareAccess> access_;
    std::shared_ptr<std::atomic<bool>> cancelled_;
    octool::core::IntelIccMaxSnapshot snapshot_;
    unsigned revision_=0;
    QLineEdit *cpu_;
    QDoubleSpinBox *value_;
    QPushButton *apply_;
    QTableWidget *table_;
    QLabel *status_, *raw_;
};
