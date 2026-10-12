// SPDX-License-Identifier: GPL-2.0-only
#pragma once
#include "access.h"
#include "core/intel_tvb.h"
#include <QWidget>
class QComboBox;
class QLabel;
class QLineEdit;
class QTableWidget;
class QPushButton;
class IntelTvbTablePanel : public QWidget {
    Q_OBJECT
public:
    explicit IntelTvbTablePanel(std::shared_ptr<HardwareAccess> access, QWidget *parent = nullptr, unsigned cpu = 0);
    ~IntelTvbTablePanel() override;
private:
    void invalidate();
    void refresh();
    void present(const octool::core::IntelTvbTableSnapshot &snapshot);
    void prepare();
    void presentPrepared(const octool::core::IntelTvbTableEditSnapshot &snapshot);
    void selectCurrent();
    void apply();
    std::shared_ptr<HardwareAccess> access_;
    std::shared_ptr<std::atomic<bool>> cancelled_;
    unsigned revision_ = 0;
    octool::core::IntelTvbTableEditSnapshot snapshot_;
    QLineEdit *cpu_;
    QComboBox *entry_;
    QTableWidget *table_;
    QLabel *status_;
    QComboBox *field_;
    QLineEdit *value_;
    QPushButton *prepare_, *apply_;
};
