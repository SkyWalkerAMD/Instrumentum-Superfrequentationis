// SPDX-License-Identifier: GPL-2.0-only
#pragma once
#include "access.h"
#include "core/intel_tvb.h"
#include <QWidget>
class QComboBox;
class QLabel;
class QLineEdit;
class QTableWidget;
class IntelTvbTablePanel : public QWidget {
    Q_OBJECT
public:
    explicit IntelTvbTablePanel(std::shared_ptr<HardwareAccess> access, QWidget *parent = nullptr, unsigned cpu = 0);
    ~IntelTvbTablePanel() override;
private:
    void invalidate();
    void refresh();
    void present(const octool::core::IntelTvbTableSnapshot &snapshot);
    std::shared_ptr<HardwareAccess> access_;
    std::shared_ptr<std::atomic<bool>> cancelled_;
    unsigned revision_ = 0;
    QLineEdit *cpu_;
    QComboBox *entry_;
    QTableWidget *table_;
    QLabel *status_;
};
