// SPDX-License-Identifier: GPL-2.0-only
#pragma once
#include "core/amd_pm.h"
#include "access.h"
#include <QWidget>
class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QTableWidget;
class AmdPmPanel : public QWidget {
public:
    explicit AmdPmPanel(QWidget *parent = nullptr, octool::core::AmdPmReader reader = octool::core::AmdPmReader(),
                        std::shared_ptr<HardwareAccess> access = {}, unsigned cpu = 0);
    ~AmdPmPanel() override;
    bool loadSnapshotFile(const QString &path);
    bool loadRawFile(const QString &path, std::uint32_t version);
private:
    void readDriver();
    void readNative();
    void clear();
    void showFields();
    void auditLayout();
    bool options(octool::core::AmdPmOptions &out);
    void present(const octool::core::AmdPmSnapshot &snapshot, const QString &source);
    octool::core::AmdPmReader reader_;
    octool::core::AmdPmSnapshot snapshot_;
    std::shared_ptr<HardwareAccess> access_;
    std::shared_ptr<std::atomic<bool>> cancelled_;
    unsigned revision_ = 0;
    std::string nativeCapture_;
    std::vector<unsigned> firmwareCcdIds_;
    QComboBox *version_, *view_, *nativePlatform_;
    QCheckBox *scale_, *mapCcds_;
    QLineEdit *bclk_, *layoutCcds_, *cpu_;
    QWidget *controls_;
    QPushButton *read_, *copy_, *copyAudit_;
    std::string layoutAuditJson_;
    QTableWidget *table_;
    QLabel *metadata_, *status_;
    bool busy_ = false;
};
