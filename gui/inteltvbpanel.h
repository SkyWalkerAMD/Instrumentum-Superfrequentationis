// SPDX-License-Identifier: GPL-2.0-only
#pragma once
#include "access.h"
#include "core/intel_tvb.h"
#include <QWidget>
class QComboBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QTableWidget;
class IntelTvbPanel : public QWidget {
    Q_OBJECT
public:
    explicit IntelTvbPanel(std::shared_ptr<HardwareAccess> access, QWidget *parent = nullptr, unsigned cpu = 0);
    ~IntelTvbPanel() override;
private:
    void refresh();
    void apply();
    void invalidate();
    void selectCurrent();
    void present(const octool::core::IntelTvbSnapshot &snapshot);
    std::shared_ptr<HardwareAccess> access_;
    std::shared_ptr<std::atomic<bool>> cancelled_;
    octool::core::IntelTvbSnapshot snapshot_;
    unsigned revision_ = 0;
    QLineEdit *cpu_;
    QComboBox *field_, *value_;
    QPushButton *apply_;
    QTableWidget *table_;
    QLabel *status_, *raw_;
};
