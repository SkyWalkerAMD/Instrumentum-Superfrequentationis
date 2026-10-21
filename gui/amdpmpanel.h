// SPDX-License-Identifier: GPL-2.0-only
#pragma once
#include "core/amd_pm.h"
#include <QWidget>
class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QTableWidget;
class AmdPmPanel : public QWidget {
public:
    explicit AmdPmPanel(QWidget *parent = nullptr, octool::core::AmdPmReader reader = octool::core::AmdPmReader());
    bool loadSnapshotFile(const QString &path);
    bool loadRawFile(const QString &path, std::uint32_t version);
private:
    void readDriver();
    void clear();
    void showFields();
    void auditLayout();
    bool options(octool::core::AmdPmOptions &out);
    void present(const octool::core::AmdPmSnapshot &snapshot, const QString &source);
    octool::core::AmdPmReader reader_;
    octool::core::AmdPmSnapshot snapshot_;
    QComboBox *version_, *view_;
    QCheckBox *scale_;
    QLineEdit *bclk_, *layoutCcds_;
    QWidget *controls_;
    QPushButton *read_, *copy_, *copyAudit_;
    std::string layoutAuditJson_;
    QTableWidget *table_;
    QLabel *metadata_, *status_;
    bool busy_ = false;
};
