// SPDX-License-Identifier: GPL-2.0-only
#pragma once
#include "access.h"
#include "core/intel_oc.h"
#include <QWidget>

class QComboBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QTableWidget;
class IntelOcPanel : public QWidget {
    Q_OBJECT
public:
    explicit IntelOcPanel(std::shared_ptr<HardwareAccess> access, QWidget *parent = nullptr, unsigned cpu = 0);
    ~IntelOcPanel() override;
private:
    void invalidate();
    void refresh();
    void apply();
    void present(const octool::core::IntelOcSnapshot &snapshot);
    std::shared_ptr<HardwareAccess> access_;
    std::shared_ptr<std::atomic<bool>> cancelled_;
    octool::core::IntelOcSnapshot snapshot_;
    QLineEdit *cpu_, *value_;
    QComboBox *domain_;
    QPushButton *apply_;
    QTableWidget *table_;
    QLabel *status_;
};
