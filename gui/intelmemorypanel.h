// SPDX-License-Identifier: GPL-2.0-only
#pragma once
#include "access.h"
#include "core/intel_memory.h"
#include <QByteArray>
#include <QWidget>
class QComboBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QTableWidget;

class IntelMemoryPanel : public QWidget {
public:
    explicit IntelMemoryPanel(std::shared_ptr<HardwareAccess> access, QWidget *parent = nullptr, unsigned cpu = 0);
    ~IntelMemoryPanel() override;
    bool loadCapture(const QByteArray &bytes);
    QByteArray captureBytes() const;
private:
    void clear();
    void present(const octool::core::IntelMemorySnapshot &snapshot);
    void read();
    std::shared_ptr<HardwareAccess> access_;
    std::shared_ptr<std::atomic<bool>> cancelled_;
    QLineEdit *cpu_;
    QComboBox *controller_, *channel_;
    QTableWidget *table_;
    QLabel *context_, *status_;
    QPushButton *copy_, *save_;
    QByteArray capture_;
    bool busy_ = false;
    unsigned generation_ = 0;
};
