// SPDX-License-Identifier: GPL-2.0-only
#pragma once
#include "access.h"
#include "core/umc_capture.h"
#include <QByteArray>
#include <QWidget>
class QLineEdit;
class QComboBox;
class QTableWidget;
class QLabel;
class QPushButton;
class QCheckBox;

class UmcPanel : public QWidget {
public:
    UmcPanel(std::shared_ptr<HardwareAccess> access, QWidget *parent=nullptr, unsigned cpu=0);
    ~UmcPanel() override;
    // Offline path shared by the file picker and regression tests; never IO.
    bool loadCapture(const QByteArray &bytes);
    bool compareCapture(const QByteArray &bytes);
    QByteArray captureBytes() const;
private:
    void read();
    void clear();
    void clearComparison();
    void filterComparison();
    void present(const octool::core::UmcDecode &decoded);
    std::shared_ptr<HardwareAccess> access_;
    std::shared_ptr<std::atomic<bool>> cancelled_;
    QLineEdit *cpu_, *bus_, *device_, *function_;
    QComboBox *bank_, *slot_;
    QTableWidget *table_;
    QLabel *status_;
    QPushButton *save_, *compare_, *resetComparison_;
    QCheckBox *differences_;
    QByteArray capture_;
    octool::core::UmcCapture current_;
    octool::core::UmcComparison comparison_;
};
