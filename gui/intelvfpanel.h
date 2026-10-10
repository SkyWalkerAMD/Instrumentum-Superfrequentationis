// SPDX-License-Identifier: GPL-2.0-only
#pragma once
#include "access.h"
#include "core/intel_oc.h"
#include <QWidget>
class QComboBox;
class QLabel;
class QLineEdit;
class QTableWidget;
class IntelVfPanel : public QWidget {
    Q_OBJECT
public:
    explicit IntelVfPanel(std::shared_ptr<HardwareAccess> access, QWidget *parent = nullptr, unsigned cpu = 0);
    ~IntelVfPanel() override;
private:
    void refresh();
    void present(const octool::core::IntelVfSnapshot &snapshot);
    std::shared_ptr<HardwareAccess> access_;
    std::shared_ptr<std::atomic<bool>> cancelled_;
    QLineEdit *cpu_;
    QComboBox *domain_, *point_;
    QTableWidget *table_;
    QLabel *status_;
};
