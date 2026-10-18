// SPDX-License-Identifier: GPL-2.0-only
#pragma once
#include "access.h"
#include "core/amd_pstates.h"
#include <QVector>
#include <QWidget>
class QLineEdit;
class QLabel;
class QTableWidget;
class QComboBox;

using PstateValue = octool::core::PstateValue;
PstateValue decodeFamily1aPstate(quint64 raw);
struct PstateRow {
    bool read = false;
    quint64 raw = 0;
    PstateValue decoded;
    octool::core::PstateElectricalValue electrical;
    QString status = "Not read";
};
struct PstateSnapshot {
    QString status;
    unsigned family = 0;
    QVector<PstateRow> rows = QVector<PstateRow>(8);
};
PstateSnapshot readAmdPstates(HardwareAccess &access, unsigned cpu);

class PstatesPanel : public QWidget {
public:
    explicit PstatesPanel(std::shared_ptr<HardwareAccess> access, QWidget *parent = nullptr, unsigned initialCpu = 0);
private:
    void refresh();
    void clearRows();
    void renderSnapshot();
    std::shared_ptr<HardwareAccess> access_;
    QLineEdit *cpu_;
    QLabel *status_;
    QTableWidget *table_;
    QComboBox *view_;
    PstateSnapshot snapshot_;
    bool hasSnapshot_ = false;
    quint64 revision_ = 0;
};
