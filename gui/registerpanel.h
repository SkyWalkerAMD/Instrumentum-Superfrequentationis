// SPDX-License-Identifier: GPL-2.0-only
#pragma once
#include "access.h"
#include <QWidget>
#include <QMap>
class QLineEdit;
class QComboBox;
class QLabel;
class QPlainTextEdit;

class RegisterPanel : public QWidget {
public:
    RegisterPanel(Space space, std::shared_ptr<HardwareAccess> access, QWidget *parent = nullptr, unsigned initialCpu = 0);
private:
    QLineEdit *field(const QString &name, const QString &initial = {});
    void submit(bool write);
    Space space_;
    std::shared_ptr<HardwareAccess> access_;
    QMap<QString, QLineEdit *> fields_;
    QComboBox *width_;
    QLabel *status_;
    QLineEdit *result_;
    QPlainTextEdit *history_;
};
