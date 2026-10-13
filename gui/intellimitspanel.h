// SPDX-License-Identifier: GPL-2.0-only
#pragma once
#include "access.h"
#include "core/intel_limits.h"
#include <QWidget>
class QLabel;
class QLineEdit;
class QComboBox;
class QPushButton;
class QTableWidget;
class IntelLimitsPanel : public QWidget {
    Q_OBJECT
public:
    explicit IntelLimitsPanel(std::shared_ptr<HardwareAccess> access, QWidget *parent=nullptr, unsigned cpu=0);
    ~IntelLimitsPanel() override;
private:
    void refresh();
    void clearHistory();
    void invalidate();
    void present(const octool::core::IntelLimitsSnapshot &snapshot);
    std::shared_ptr<HardwareAccess> access_;
    std::shared_ptr<std::atomic<bool>> cancelled_;
    octool::core::IntelLimitsSnapshot snapshot_;
    unsigned revision_=0;
    QLineEdit *cpu_;
    QComboBox *reason_;
    QPushButton *clear_;
    QTableWidget *table_;
    QLabel *status_, *raw_;
};
