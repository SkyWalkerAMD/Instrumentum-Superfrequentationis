// SPDX-License-Identifier: GPL-2.0-only
#pragma once
#include "access.h"
#include "core/intel_controls.h"
#include "core/amd_curve.h"
#include "core/amd_pbo.h"
#include <QWidget>
class QLabel;
class QLineEdit;
class QTableWidget;
class QComboBox;

class IntelControlsPanel : public QWidget {
public:
    IntelControlsPanel(std::shared_ptr<HardwareAccess> access, QWidget *parent = nullptr, unsigned cpu = 0);
    ~IntelControlsPanel() override;
private:
    void refresh();
    void apply();
    void present(const octool::core::IntelSnapshot &snapshot);
    void invalidate();
    std::shared_ptr<HardwareAccess> access_;
    std::shared_ptr<std::atomic<bool>> cancelled_;
    octool::core::IntelSnapshot snapshot_;
    unsigned revision_ = 0;
    QLineEdit *cpu_, *value_;
    QComboBox *field_;
    QTableWidget *table_;
    QLabel *status_;
};
class AmdTuningPanel : public QWidget {
public:
    AmdTuningPanel(std::shared_ptr<HardwareAccess> access, QWidget *parent = nullptr, unsigned cpu = 0);
    ~AmdTuningPanel() override;
private:
    void submit(bool probeOnly);
    void readTopology();
    void readCurve();
    void applyCurve();
    void applyAllCoreCurve();
    void invalidateCurve();
    void readPboScalar();
    void applyPboScalar();
    void readPboCapabilities();
    void readPboQuery();
    void readPowerAdjustment();
    void applyPowerAdjustment();
    void applyPboFmax();
    void applyPboLimit();
    void applyOcMode();
    void applyOcFrequency();
    std::shared_ptr<HardwareAccess> access_;
    std::shared_ptr<std::atomic<bool>> cancelled_;
    QLineEdit *cpu_, *bus_, *device_, *function_, *args_[6];
    QLineEdit *curveCcd_, *curveCore_, *curveMargin_, *allCurveMargin_;
    QLineEdit *pboScalar_, *pboFmax_, *pboLimitValue_;
    QComboBox *pboLimit_;
    QComboBox *pboQuery_;
    QLineEdit *pboQueryIndex_;
    QLineEdit *powerAdjustment_;
    octool::core::AmdPowerAdjustment powerSnapshot_;
    QComboBox *ocMode_, *ocFrequencyScope_;
    QLineEdit *ocCcd_, *ocCore_, *ocMHz_;
    octool::core::AmdPboScalar pboSnapshot_;
    octool::core::AmdPboCapabilities pboCaps_;
    octool::core::SmuTarget pboTarget_;
    octool::core::AmdCurveSnapshot curveSnapshot_;
    octool::core::SmuTarget curveTarget_;
    unsigned curveRevision_ = 0;
    QComboBox *profile_, *command_;
    QLabel *status_;
    QTableWidget *table_;
};
class MemoryBoardPanel : public QWidget {
public:
    explicit MemoryBoardPanel(QWidget *parent = nullptr);
    bool loadSpdFile(const QString &path);
private:
    void refresh();
    void showSpd(const QString &path, const QByteArray &bytes, const QString &error = QString());
    QTableWidget *table_;
    QLabel *status_;
};
