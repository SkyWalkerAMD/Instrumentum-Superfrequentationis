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
class IntelVfPanel : public QWidget {
    Q_OBJECT
public:
    explicit IntelVfPanel(std::shared_ptr<HardwareAccess> access, QWidget *parent = nullptr, unsigned cpu = 0);
    ~IntelVfPanel() override;
private:
    void refresh();
    void prepare();
    void apply();
    void invalidateEdit();
    void present(const octool::core::IntelVfSnapshot &snapshot);
    void presentEdit(const octool::core::IntelVfEditSnapshot &snapshot);
    std::shared_ptr<HardwareAccess> access_;
    std::shared_ptr<std::atomic<bool>> cancelled_;
    octool::core::IntelVfEditSnapshot editSnapshot_;
    QLineEdit *cpu_, *offset_;
    QComboBox *domain_, *point_;
    QTableWidget *table_;
    QLabel *status_, *context_;
    QPushButton *apply_;
};
