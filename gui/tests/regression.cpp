// SPDX-License-Identifier: GPL-2.0-only
#include "registerpanel.h"
#include "../../port/abi/octool_hwio_abi.h"
#include <QtTest>
#include <QComboBox>
#include <QLineEdit>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QTimer>
#include <atomic>
#include <cstring>
#include <cerrno>

struct Reference {
    std::atomic<int> calls{0};
    octool_hwio_req last{};
    int error = 0;
    static int submit(void *ctx, const void *request, uint64_t *mailbox, size_t words) {
        auto *r = static_cast<Reference *>(ctx);
        std::memcpy(&r->last, request, sizeof(r->last));
        std::memset(mailbox, 0, words * sizeof(*mailbox));
        mailbox[0] = 1; mailbox[1] = 0xfedcba9876543210ULL;
        ++r->calls;
        return r->error;
    }
    std::shared_ptr<HardwareAccess> access() {
        hwio_transport transport{this, submit, nullptr, "GUI test transport"};
        return std::make_shared<HardwareAccess>(hwio_open_transport(&transport));
    }
};

class Regression : public QObject {
    Q_OBJECT
private slots:
    void parsesWithoutTruncation() {
        quint64 value = 0;
        QVERIFY(parseNumber("0xFEDCBA9876543210", 16, UINT64_MAX, value));
        QCOMPARE(value, quint64(0xfedcba9876543210ULL));
        for (const QString &bad : {"", "-1", "+1", "1.5", "10000000000000000", "0x", "0x1 2"})
            QVERIFY2(!parseNumber(bad, 16, UINT64_MAX, value), qPrintable(bad));
        QVERIFY(!parseNumber("100", 16, 255, value));
        QVERIFY(!parseNumber("0x10", 10, 100, value));
    }
    void invalidRequestsNeverReachTransport() {
        Reference reference; auto access = reference.access();
        Request r; r.space = Space::Pci; r.width = 4; r.address = 0x100;
        QCOMPARE(access->execute(r).error, -EINVAL);
        r.address = 2; QCOMPARE(access->execute(r).error, -EINVAL);
        r.address = 0; r.device = 32; QCOMPARE(access->execute(r).error, -EINVAL);
        r = Request{}; r.space = Space::Memory; r.width = 1; r.write = true; r.value = 256;
        QCOMPARE(access->execute(r).error, -EINVAL);
        r.width = 8; r.address = UINT64_MAX; QCOMPARE(access->execute(r).error, -EINVAL);
        QCOMPARE(reference.calls.load(), 0);
    }
    void familiesEncodeActualRequests() {
        Reference reference; auto access = reference.access();
        Request r; r.cpu = 3; r.address = 0x123; r.write = true; r.value = 0x8877665544332211ULL;
        QCOMPARE(access->execute(r).error, 0);
        QCOMPARE(reference.last.cmd, uint64_t(0x21)); QCOMPARE(reference.last.user_id, uint64_t(3));
        QCOMPARE(reference.last.data1, uint64_t(r.value));
        r.space = Space::Memory; r.address = 0x8000;
        for (int width : {1, 2, 4, 8}) {
            r.width = width; r.value = 0x12;
            QCOMPARE(access->execute(r).error, 0);
            QCOMPARE(reference.last.cmd, uint64_t(width == 1 ? 0x11 : width == 2 ? 0x0f : width == 4 ? 0x0d : 0x0b));
            QCOMPARE(reference.last.data0, uint64_t(0x8000));
        }
        r.space = Space::Pci; r.width = 4; r.address = 0x40; r.bus = 0x23; r.device = 7; r.function = 2;
        QCOMPARE(access->execute(r).error, 0);
        QCOMPARE(reference.last.cmd, uint64_t(0x41)); QCOMPARE(reference.last.data0, uint64_t(0x23));
        QCOMPARE(reference.last.data1, uint64_t(7)); QCOMPARE(reference.last.data2, uint64_t(2));
        QCOMPARE(reference.last.data3, uint64_t(0x40)); QCOMPARE(reference.last.data4, uint64_t(4));
    }
    void windowReadAndErrorDoNotFabricateValues() {
        Reference reference; RegisterPanel panel(Space::Msr, reference.access());
        panel.show();
        QCOMPARE(reference.calls.load(), 0);
        auto *address = panel.findChild<QLineEdit *>("address");
        auto *result = panel.findChild<QLineEdit *>("result");
        auto *read = panel.findChild<QPushButton *>("read");
        QVERIFY(address && result && read);
        address->setText("123"); panel.findChild<QLineEdit *>("cpu")->setText("3");
        read->click();
        QTRY_VERIFY(panel.isEnabled());
        QCOMPARE(result->text(), QString("0xFEDCBA9876543210"));
        QCOMPARE(reference.last.user_id, uint64_t(3));
        address->setText("124"); QVERIFY(result->text().isEmpty());
        reference.error = -EACCES; read->click();
        QTRY_VERIFY(panel.isEnabled());
        QVERIFY(result->text().isEmpty());
        QVERIFY(panel.findChild<QLabel *>("status")->text().contains("FAILED"));
    }
    void writeCancellationAndConfirmation() {
        Reference reference; RegisterPanel panel(Space::Msr, reference.access()); panel.show();
        panel.findChild<QLineEdit *>("address")->setText("123");
        panel.findChild<QLineEdit *>("value")->setText("FEDCBA9876543210");
        auto *write = panel.findChild<QPushButton *>("write");
        QTimer::singleShot(0, [] {
            auto *box = qobject_cast<QMessageBox *>(QApplication::activeModalWidget());
            if (box) box->done(QMessageBox::Cancel);
        });
        write->click(); QCOMPARE(reference.calls.load(), 0);
        QTimer::singleShot(0, [] {
            auto *box = qobject_cast<QMessageBox *>(QApplication::activeModalWidget());
            if (box) box->done(QMessageBox::Yes);
        });
        write->click(); QTRY_VERIFY(panel.isEnabled());
        QCOMPARE(reference.calls.load(), 1);
        QCOMPARE(reference.last.data1, uint64_t(0xfedcba9876543210ULL));
        QVERIFY(panel.findChild<QLineEdit *>("result")->text().isEmpty());
    }
};
QTEST_MAIN(Regression)
#include "regression.moc"
