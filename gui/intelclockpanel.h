// SPDX-License-Identifier: GPL-2.0-only
#pragma once
#include "access.h"
#include "core/intel_clock.h"
#include <QByteArray>
#include <QWidget>
class QLabel;
class QLineEdit;
class QPushButton;
class QTableWidget;

class IntelClockPanel : public QWidget {
public:
    explicit IntelClockPanel(std::shared_ptr<HardwareAccess> access, QWidget *parent = nullptr, unsigned cpu = 0);
    ~IntelClockPanel() override;
    bool loadCapture(const QByteArray &bytes);
    QByteArray captureBytes() const;
private:
    void clear();
    void present(const octool::core::IntelClockSnapshot &snapshot);
    void read();
    std::shared_ptr<HardwareAccess> access_;
    std::shared_ptr<std::atomic<bool>> cancelled_;
    QLineEdit *cpu_;
    QTableWidget *table_, *raw_;
    QLabel *status_;
    QPushButton *copy_, *save_;
    QByteArray capture_;
    bool busy_ = false;
    unsigned generation_ = 0;
};
