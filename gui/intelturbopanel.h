// SPDX-License-Identifier: GPL-2.0-only
#pragma once
#include "access.h"
#include "core/intel_turbo.h"
#include <QWidget>
class QComboBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QTableWidget;
class IntelTurboPanel : public QWidget {
    Q_OBJECT
public:
    explicit IntelTurboPanel(std::shared_ptr<HardwareAccess> access, QWidget *parent = nullptr, unsigned cpu = 0);
    ~IntelTurboPanel() override;
private:
    void refresh();
    void apply();
    void invalidate();
    void present(const octool::core::IntelTurboSnapshot &snapshot);
    std::shared_ptr<HardwareAccess> access_;
    std::shared_ptr<std::atomic<bool>> cancelled_;
    octool::core::IntelTurboSnapshot snapshot_;
    QLineEdit *cpu_, *ratio_;
    QComboBox *kind_, *group_;
    QPushButton *apply_;
    QTableWidget *table_;
    QLabel *status_, *raw_;
};
