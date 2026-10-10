// SPDX-License-Identifier: GPL-2.0-only
#include "registerpanel.h"
#include "pstates.h"
#include "platformpanels.h"
#include "umcpanel.h"
#include "intelocpanel.h"
#include "intelvfpanel.h"
#include "intel_oc_fixture.h"
#include "amd_curve_fixture.h"
#include "platform/linux_inventory.h"
#include "platform/linux_hwio.h"
#include "platform/linux_cpu.h"
#include "authorizationpanel.h"
#include "../../port/abi/octool_hwio_abi.h"
#include <QtTest>
#include <QComboBox>
#include <QClipboard>
#include <QLineEdit>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QTimer>
#include <QTableWidget>
#include <QTemporaryDir>
#include <QFile>
#include <QDir>
#include <atomic>
#include <cstring>
#include <cerrno>
#include <sched.h>
#include <vector>
#include <chrono>
#include <thread>

struct PstateReference {
    bool amd = true, hardwarePstate = true;
    uint32_t signature = 0x00b00f81; // Synthetic family 1Ah/model 08h/stepping 1.
    uint32_t extendedMaximum = 0x80000007;
    uint32_t failLeaf = UINT32_MAX;
    uint32_t failRegister = 0;
    unsigned expectedCpu = 3;
    std::atomic<int> calls{0}, writes{0}, wrongCpu{0};
    std::vector<uint32_t> msrs;
    static int submit(void *ctx, const void *request, uint64_t *mailbox, size_t words) {
        auto &state = *static_cast<PstateReference *>(ctx);
        const auto &r = *static_cast<const octool_hwio_req *>(request);
        ++state.calls;
        if (r.user_id != state.expectedCpu) ++state.wrongCpu;
        std::memset(mailbox, 0, words * sizeof(*mailbox)); mailbox[0] = 1;
        if (r.cmd == OCTOOL_OP_CPUID) {
            if (r.data0 == state.failLeaf) return -EACCES;
            uint32_t values[4]{};
            switch (r.data0) {
            case 0:
                values[0] = 1;
                std::memcpy(&values[1], state.amd ? "Auth" : "Genu", 4);
                std::memcpy(&values[3], state.amd ? "enti" : "ineI", 4);
                std::memcpy(&values[2], state.amd ? "cAMD" : "ntel", 4);
                break;
            case 1: values[0] = state.signature; break;
            case 0x80000000: values[0] = state.extendedMaximum; break;
            case 0x80000007: values[3] = state.hardwarePstate ? 0x80 : 0; break;
            case 0x80000026: {
                const unsigned sub=unsigned(r.data1);
                if(sub<4) { values[0]=sub==0 ? 1 : sub==3 ? 8 : 4; values[1]=sub==0 ? 2 : sub==3 ? 144 : 12;
                    values[2]=((sub+1)<<8)|sub; values[3]=0xaf; }
                break;
            }
            default: return -EINVAL;
            }
            for (int i = 0; i < 4; ++i) mailbox[1 + i] = values[i];
            return 0;
        }
        if (r.cmd != OCTOOL_OP_RD_MSR) { ++state.writes; return -EPERM; }
        state.msrs.push_back(uint32_t(r.data0));
        if (r.data0 == state.failRegister) return -EIO;
        switch (r.data0) {
        case 0xc0010061: mailbox[1] = 0x20; break;
        case 0xc0010064: mailbox[1] = 0x80000001000003e8ULL; break;
        case 0xc0010065: mailbox[1] = 0x00000000ffc001ccULL; break;
        case 0xc0010066: mailbox[1] = 0x800000000000000fULL; break;
        default: return -EINVAL;
        }
        return 0;
    }
    std::shared_ptr<HardwareAccess> access() {
        hwio_transport transport{this, submit, nullptr, "PStates test transport"};
        return std::make_shared<HardwareAccess>(octool::platform::makeLinuxHardwareBackend(hwio_open_transport(&transport)));
    }
};

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
        return std::make_shared<HardwareAccess>(octool::platform::makeLinuxHardwareBackend(hwio_open_transport(&transport)));
    }
};

class Regression : public QObject {
    Q_OBJECT
private slots:
    void platformPanelsDoNotAccessHardwareAtConstruction() {
        Reference reference; auto access=reference.access();
        IntelControlsPanel intel(access,nullptr,7); AmdTuningPanel amd(access,nullptr,7); MemoryBoardPanel inventory;
        UmcPanel umc(access,nullptr,7);
        IntelOcPanel oc(access,nullptr,7);
        IntelVfPanel vf(access,nullptr,7);
        QCOMPARE(reference.calls.load(),0);
        QCOMPARE(intel.findChild<QLineEdit *>("intelCpu")->text(),QString("7"));
        QCOMPARE(amd.findChild<QLineEdit *>("smuCpu")->text(),QString("7"));
        QCOMPARE(inventory.findChild<QTableWidget *>("inventoryTable")->rowCount(),0);
        QCOMPARE(umc.findChild<QTableWidget *>("umcTable")->rowCount(),0);
        QVERIFY(!oc.findChild<QPushButton *>("ocApply")->isEnabled());
        QVERIFY(!oc.findChild<QPushButton *>("ocApplyRatio")->isEnabled());
        QVERIFY(!oc.findChild<QPushButton *>("ocApplyVoltage")->isEnabled());
        QVERIFY(oc.findChild<QLineEdit *>("ocTarget")->text().isEmpty());
        QCOMPARE(oc.findChild<QComboBox *>("ocMode")->currentData().toInt(), -1);
        QCOMPARE(vf.findChild<QLineEdit *>("vfCpu")->text(),QString("7"));
        QCOMPARE(vf.findChild<QTableWidget *>("vfTable")->rowCount(),0);
        intel.findChild<QLineEdit *>("intelValue")->setText("125");
        intel.findChild<QPushButton *>("intelApply")->click();
        QCOMPARE(reference.calls.load(),0);
    }
    void intelVfReadsBothDomainsAndInvalidates() {
        auto *device = new IntelOcFixture;
        auto access = std::make_shared<HardwareAccess>(std::unique_ptr<octool::core::HardwareBackend>(device));
        IntelVfPanel panel(access, nullptr, 130); panel.show();
        auto *read = panel.findChild<QPushButton *>("vfRead");
        auto *table = panel.findChild<QTableWidget *>("vfTable");
        QCOMPARE(device->calls, 0u); read->click(); QTRY_VERIFY(panel.isEnabled());
        QCOMPARE(table->rowCount(), 15); QCOMPARE(table->item(0, 0)->text(), QString("1"));
        QCOMPARE(table->item(14, 3)->text(), hexValue(device->vfSettings[0][15], 4));
        const unsigned calls = device->calls;
        panel.findChild<QComboBox *>("vfDomain")->setCurrentIndex(1);
        QCOMPARE(table->rowCount(), 0); QCOMPARE(device->calls, calls);
        panel.findChild<QComboBox *>("vfPoint")->setCurrentIndex(15); read->click(); QTRY_VERIFY(panel.isEnabled());
        QCOMPARE(table->rowCount(), 1); QCOMPARE(table->item(0, 0)->text(), QString("15"));
        QCOMPARE(table->item(0, 3)->text(), hexValue(device->vfSettings[2][15], 4));
        QCOMPARE(device->mutationCount, 0u); QCOMPARE(device->wrongCpu, 0u);
        const unsigned before = device->calls; panel.findChild<QLineEdit *>("vfCpu")->setText("-1"); read->click();
        QCOMPARE(table->rowCount(), 0); QCOMPARE(device->calls, before);
        panel.findChild<QLineEdit *>("vfCpu")->setText("130"); device->model = 0x8f; device->clearTrace();
        read->click(); QTRY_VERIFY(panel.isEnabled()); QVERIFY(device->requests.empty()); QCOMPARE(table->rowCount(), 0);
        QVERIFY(panel.findChild<QLabel *>("vfStatus")->text().contains("Failed"));
    }
    void intelVfRejectedPointsStayBlankAndCopyIncludesTarget() {
        auto *device = new IntelOcFixture; device->failVfPoint = 8;
        auto access = std::make_shared<HardwareAccess>(std::unique_ptr<octool::core::HardwareBackend>(device));
        IntelVfPanel panel(access, nullptr, 130); panel.resize(1100, 640); panel.show();
        panel.findChild<QPushButton *>("vfRead")->click(); QTRY_VERIFY(panel.isEnabled());
        auto *table = panel.findChild<QTableWidget *>("vfTable"); QCOMPARE(table->rowCount(), 15);
        for (int col = 1; col <= 3; ++col) QVERIFY(table->item(7, col)->text().isEmpty());
        QVERIFY(table->item(7, 4)->text().contains("0xfe")); QVERIFY(!table->item(8, 3)->text().isEmpty());
        auto *status = panel.findChild<QLabel *>("vfStatus"); QVERIFY(status->text().contains("14 valid / 15 requested"));
        panel.findChild<QPushButton *>("vfCopy")->click();
        const auto copy = QApplication::clipboard()->text();
        QVERIFY(copy.contains("CPU 130 - Core") && copy.contains("Offset (mV)") && copy.contains("0xfe"));
        const QString capture = qEnvironmentVariable("OCTOOL_VF_TEST_SCREENSHOT");
        if (!capture.isEmpty()) QVERIFY(panel.grab().save(capture));
        QCOMPARE(device->mutationCount, 0u);
    }
    void intelVfReadFailureClearsPreviousValues() {
        auto *device = new IntelOcFixture;
        auto access = std::make_shared<HardwareAccess>(std::unique_ptr<octool::core::HardwareBackend>(device));
        IntelVfPanel panel(access, nullptr, 130); panel.show();
        auto *read = panel.findChild<QPushButton *>("vfRead");
        auto *table = panel.findChild<QTableWidget *>("vfTable");
        read->click(); QTRY_VERIFY(panel.isEnabled()); QCOMPARE(table->rowCount(), 15);
        device->clearTrace(); device->failAt = 5;
        read->click(); QTRY_VERIFY(panel.isEnabled()); QCOMPARE(table->rowCount(), 1);
        for (int col = 1; col <= 3; ++col) QVERIFY(table->item(0, col)->text().isEmpty());
        QVERIFY(panel.findChild<QLabel *>("vfStatus")->text().contains("Queries incomplete"));
        QCOMPARE(device->mutationCount, 0u);
    }
    void intelOcQueriesAndInvalidatesTargets() {
        auto *device = new IntelOcFixture;
        auto access = std::make_shared<HardwareAccess>(std::unique_ptr<octool::core::HardwareBackend>(device));
        IntelOcPanel panel(access, nullptr, 130); panel.show();
        auto *read = panel.findChild<QPushButton *>("ocRead");
        auto *apply = panel.findChild<QPushButton *>("ocApply");
        auto *table = panel.findChild<QTableWidget *>("ocTable");
        QCOMPARE(device->calls, 0u); read->click(); QTRY_VERIFY(panel.isEnabled());
        QCOMPARE(table->rowCount(), 6); QVERIFY(apply->isEnabled());
        QCOMPARE(table->item(4, 1)->text(), hexValue(device->settings[0], 4));
        QCOMPARE(device->mutationCount, 0u); QCOMPARE(device->wrongCpu, 0u);
        const unsigned before = device->calls;
        panel.findChild<QComboBox *>("ocDomain")->setCurrentIndex(1);
        QCOMPARE(table->rowCount(), 0); QVERIFY(!apply->isEnabled()); QCOMPARE(device->calls, before);
        read->click(); QTRY_VERIFY(panel.isEnabled()); QCOMPARE(table->item(4, 1)->text(), hexValue(device->settings[2], 4));
        const auto after = device->calls; panel.findChild<QLineEdit *>("ocCpu")->setText("-1"); read->click();
        QCOMPARE(device->calls, after); QCOMPARE(table->rowCount(), 0);
        panel.findChild<QLineEdit *>("ocCpu")->setText("130"); device->model = 0x8f; device->clearTrace();
        read->click(); QTRY_VERIFY(panel.isEnabled());
        QVERIFY(device->requests.empty()); QCOMPARE(table->rowCount(), 0); QVERIFY(!apply->isEnabled());
    }
    void intelOcCancelThenApplyPreservesOtherFields() {
        auto *device = new IntelOcFixture;
        auto access = std::make_shared<HardwareAccess>(std::unique_ptr<octool::core::HardwareBackend>(device));
        IntelOcPanel panel(access, nullptr, 130); panel.show();
        panel.findChild<QPushButton *>("ocRead")->click(); QTRY_VERIFY(panel.isEnabled());
        const auto previous = device->settings[0], other = device->settings[2]; const auto calls = device->calls;
        auto *apply = panel.findChild<QPushButton *>("ocApply"); panel.findChild<QLineEdit *>("ocOffset")->setText("-50");
        QTimer::singleShot(0, [] {
            auto *box = qobject_cast<QMessageBox *>(QApplication::activeModalWidget());
            if (box) box->button(QMessageBox::Cancel)->click();
        });
        apply->click(); QCOMPARE(device->calls, calls); QCOMPARE(device->mutationCount, 0u);
        QTimer::singleShot(0, [] {
            auto *box = qobject_cast<QMessageBox *>(QApplication::activeModalWidget());
            if (box) box->button(QMessageBox::Yes)->click();
        });
        apply->click(); QTRY_VERIFY(panel.isEnabled());
        QCOMPARE(device->mutationCount, 1u); QCOMPARE(device->settings[0] & 0x1fffffu, previous & 0x1fffffu);
        QCOMPARE(device->settings[2], other); QCOMPARE(device->wrongCpu, 0u);
        QCOMPARE(panel.findChild<QTableWidget *>("ocTable")->item(0, 1)->text(), QString("-49.8046875"));
        QVERIFY(panel.findChild<QLabel *>("ocStatus")->text().contains("read back"));
    }
    void intelOcLockedOrUnverifiedWritesNeverShowSuccess() {
        auto *device = new IntelOcFixture;
        auto access = std::make_shared<HardwareAccess>(std::unique_ptr<octool::core::HardwareBackend>(device));
        IntelOcPanel panel(access, nullptr, 130); panel.show();
        auto *read = panel.findChild<QPushButton *>("ocRead"); auto *apply = panel.findChild<QPushButton *>("ocApply");
        auto *status = panel.findChild<QLabel *>("ocStatus"); auto *table = panel.findChild<QTableWidget *>("ocTable");
        device->locked = true; read->click(); QTRY_VERIFY(panel.isEnabled()); QVERIFY(!apply->isEnabled());
        device->locked = false; read->click(); QTRY_VERIFY(panel.isEnabled()); QVERIFY(apply->isEnabled());
        device->discardChange = true;
        panel.findChild<QLineEdit *>("ocOffset")->setText("-50");
        QTimer::singleShot(0, [] {
            auto *box = qobject_cast<QMessageBox *>(QApplication::activeModalWidget());
            if (box) box->button(QMessageBox::Yes)->click();
        });
        apply->click(); QTRY_VERIFY(panel.isEnabled());
        QCOMPARE(device->mutationCount, 1u); QCOMPARE(table->rowCount(), 0); QVERIFY(!apply->isEnabled());
        QVERIFY(status->text().contains("not verified"));
        device->failCommand = 0x10; device->status = 3;
        read->click(); QTRY_VERIFY(panel.isEnabled());
        QVERIFY(status->text().contains("Firmware status: 0x03")); QCOMPARE(table->rowCount(), 0); QVERIFY(!apply->isEnabled());
    }
    void intelOcRatioValidatesCancelsAndPreservesVoltage() {
        auto *device = new IntelOcFixture;
        auto access = std::make_shared<HardwareAccess>(std::unique_ptr<octool::core::HardwareBackend>(device));
        IntelOcPanel panel(access, nullptr, 130); panel.show();
        panel.findChild<QComboBox *>("ocDomain")->setCurrentIndex(1);
        panel.findChild<QPushButton *>("ocRead")->click(); QTRY_VERIFY(panel.isEnabled());
        auto *apply = panel.findChild<QPushButton *>("ocApplyRatio");
        auto *value = panel.findChild<QLineEdit *>("ocRatio"); const auto calls = device->calls;
        for (const auto &invalid : {QString(), QString("-1"), QString("0"), QString("86"), QString("59.5"), QString("0x3b"), QString("4294967297")}) {
            value->setText(invalid); apply->click(); QCOMPARE(device->calls, calls);
        }
        value->setText("59"); const auto old = device->settings[2], other = device->settings[0];
        QTimer::singleShot(0, [] {
            auto *box = qobject_cast<QMessageBox *>(QApplication::activeModalWidget());
            if (box) box->button(QMessageBox::Cancel)->click();
        });
        apply->click(); QCOMPARE(device->calls, calls);
        QTimer::singleShot(0, [] {
            auto *box = qobject_cast<QMessageBox *>(QApplication::activeModalWidget());
            if (box) box->button(QMessageBox::Yes)->click();
        });
        apply->click(); QTRY_VERIFY(panel.isEnabled());
        QCOMPARE(device->settings[2], (old & 0xffffff00u) | 59u); QCOMPARE(device->settings[0], other);
        QCOMPARE(device->mutationCount, 1u); QCOMPARE(device->wrongCpu, 0u);
        QCOMPARE(panel.findChild<QTableWidget *>("ocTable")->item(3, 1)->text(), QString("59"));
        QVERIFY(panel.findChild<QLabel *>("ocStatus")->text().contains("Ratio accepted"));
        panel.findChild<QComboBox *>("ocDomain")->setCurrentIndex(0);
        QVERIFY(!apply->isEnabled()); QVERIFY(!panel.findChild<QPushButton *>("ocApply")->isEnabled());
        QVERIFY(value->text().isEmpty());
    }
    void intelOcRatioLocksAndUnverifiedResult() {
        auto *device = new IntelOcFixture;
        auto access = std::make_shared<HardwareAccess>(std::unique_ptr<octool::core::HardwareBackend>(device));
        IntelOcPanel panel(access, nullptr, 130); panel.show();
        auto *read = panel.findChild<QPushButton *>("ocRead"); auto *apply = panel.findChild<QPushButton *>("ocApplyRatio");
        device->locked = true; read->click(); QTRY_VERIFY(panel.isEnabled()); QVERIFY(!apply->isEnabled());
        device->locked = false; read->click(); QTRY_VERIFY(panel.isEnabled()); QVERIFY(apply->isEnabled());
        device->discardChange = true; panel.findChild<QLineEdit *>("ocRatio")->setText("59");
        QTimer::singleShot(0, [] {
            auto *box = qobject_cast<QMessageBox *>(QApplication::activeModalWidget());
            if (box) box->button(QMessageBox::Yes)->click();
        });
        apply->click(); QTRY_VERIFY(panel.isEnabled());
        QCOMPARE(device->mutationCount, 1u); QVERIFY(!apply->isEnabled());
        QCOMPARE(panel.findChild<QTableWidget *>("ocTable")->rowCount(), 0);
        QVERIFY(panel.findChild<QLabel *>("ocStatus")->text().contains("not verified"));
    }
    void intelOcVoltageValidatesCancelsAndPreservesDomains() {
        for (int domain : {0, 1}) for (int mode : {1, 2}) {
            auto *device = new IntelOcFixture;
            auto access = std::make_shared<HardwareAccess>(std::unique_ptr<octool::core::HardwareBackend>(device));
            IntelOcPanel panel(access, nullptr, 130); panel.show();
            panel.findChild<QComboBox *>("ocDomain")->setCurrentIndex(domain);
            panel.findChild<QPushButton *>("ocRead")->click(); QTRY_VERIFY(panel.isEnabled());
            auto *apply = panel.findChild<QPushButton *>("ocApplyVoltage");
            auto *value = panel.findChild<QLineEdit *>("ocTarget"); auto *choice = panel.findChild<QComboBox *>("ocMode");
            const auto calls = device->calls;
            value->setText("1234"); apply->click(); QCOMPARE(device->calls, calls); // No implicit mode.
            choice->setCurrentIndex(mode);
            for (const auto &invalid : {QString(), QString("-1"), QString("0"), QString("2001"), QString("1234.5"), QString("0x4d2"), QString("4294967295")}) {
                value->setText(invalid); apply->click(); QCOMPARE(device->calls, calls);
            }
            value->setText("1234");
            QTimer::singleShot(0, [] {
                auto *box = qobject_cast<QMessageBox *>(QApplication::activeModalWidget());
                if (box) box->button(QMessageBox::Cancel)->click();
            });
            apply->click(); QCOMPARE(device->calls, calls);
            const auto previous = device->settings[domain * 2], other = device->settings[2 - domain * 2];
            QTimer::singleShot(0, [] {
                auto *box = qobject_cast<QMessageBox *>(QApplication::activeModalWidget());
                if (box) box->button(QMessageBox::Yes)->click();
            });
            apply->click(); QTRY_VERIFY(panel.isEnabled());
            QCOMPARE(device->settings[domain * 2], (previous & 0xffe000ffu) | 0x4f000u | (mode == 2 ? 1u << 20 : 0));
            QCOMPARE(device->settings[2 - domain * 2], other); QCOMPARE(device->mutationCount, 1u); QCOMPARE(device->wrongCpu, 0u);
            auto *table = panel.findChild<QTableWidget *>("ocTable");
            QCOMPARE(table->item(1, 1)->text(), QString("1234.375"));
            QCOMPARE(table->item(2, 1)->text(), mode == 2 ? QString("Override") : QString("Adaptive"));
            QVERIFY(panel.findChild<QLabel *>("ocStatus")->text().contains("Target and mode accepted"));
            QVERIFY(value->text().isEmpty()); QCOMPARE(choice->currentData().toInt(), -1);
            value->setText("1500"); choice->setCurrentIndex(2);
            panel.findChild<QLineEdit *>("ocCpu")->setText("131");
            QVERIFY(value->text().isEmpty()); QCOMPARE(choice->currentData().toInt(), -1);
            QVERIFY(!apply->isEnabled()); QCOMPARE(table->rowCount(), 0);
        }
    }
    void intelOcVoltageLocksStaleAndUnverified() {
        auto *device = new IntelOcFixture;
        auto access = std::make_shared<HardwareAccess>(std::unique_ptr<octool::core::HardwareBackend>(device));
        IntelOcPanel panel(access, nullptr, 130); panel.show();
        auto *read = panel.findChild<QPushButton *>("ocRead"); auto *apply = panel.findChild<QPushButton *>("ocApplyVoltage");
        device->locked = true; read->click(); QTRY_VERIFY(panel.isEnabled()); QVERIFY(!apply->isEnabled());
        device->locked = false;
        for (bool stale : {true, false}) {
            read->click(); QTRY_VERIFY(panel.isEnabled()); QVERIFY(apply->isEnabled());
            if (stale) device->settings[0] ^= 1u << 31;
            else device->discardChange = true;
            panel.findChild<QLineEdit *>("ocTarget")->setText("1234");
            panel.findChild<QComboBox *>("ocMode")->setCurrentIndex(2);
            QTimer::singleShot(0, [] {
                auto *box = qobject_cast<QMessageBox *>(QApplication::activeModalWidget());
                if (box) box->button(QMessageBox::Yes)->click();
            });
            apply->click(); QTRY_VERIFY(panel.isEnabled());
            QCOMPARE(device->mutationCount, stale ? 0u : 1u); QVERIFY(!apply->isEnabled());
            QCOMPARE(panel.findChild<QTableWidget *>("ocTable")->rowCount(), 0);
            QVERIFY(panel.findChild<QLabel *>("ocStatus")->text().contains(stale ? "No settings-change command" : "not verified"));
        }
    }
    void intelOcVoltageSameEncodingDoesNotSubmitAgain() {
        auto *device = new IntelOcFixture; device->settings[0] = 0xf354f045;
        auto access = std::make_shared<HardwareAccess>(std::unique_ptr<octool::core::HardwareBackend>(device));
        IntelOcPanel panel(access, nullptr, 130); panel.show();
        panel.findChild<QPushButton *>("ocRead")->click(); QTRY_VERIFY(panel.isEnabled());
        panel.findChild<QLineEdit *>("ocTarget")->setText("1234");
        panel.findChild<QComboBox *>("ocMode")->setCurrentIndex(2);
        QTimer::singleShot(0, [] {
            auto *box = qobject_cast<QMessageBox *>(QApplication::activeModalWidget());
            if (box) box->button(QMessageBox::Yes)->click();
        });
        panel.findChild<QPushButton *>("ocApplyVoltage")->click(); QTRY_VERIFY(panel.isEnabled());
        QCOMPARE(device->mutationCount, 0u); QCOMPARE(device->settings[0], 0xf354f045u);
        QVERIFY(panel.findChild<QLabel *>("ocStatus")->text().contains("already present"));
    }
    void umcOfflineCaptureKeepsDistinctFieldsAndClearsInvalidInput() {
        Reference reference; UmcPanel panel(reference.access());
        const QByteArray bytes=R"({"format":"octool-amd-umc-v1","bank":0,"refresh_slot":0,"registers":[{"offset":516,"value":34},{"offset":4624,"value":258048}]})";
        QVERIFY(panel.loadCapture(bytes)); QCOMPARE(reference.calls.load(),0);
        auto *table=panel.findChild<QTableWidget *>("umcTable"); QCOMPARE(table->rowCount(),212);
        QStringList tcl;
        for(int row=0;row<table->rowCount();++row) if(table->item(row,1)->text()=="Tcl") tcl<<table->item(row,6)->text();
        QCOMPARE(tcl,QStringList({"34","63"}));
        QVERIFY(panel.findChild<QLabel *>("umcStatus")->text().contains("Offline"));
        QVERIFY(!panel.captureBytes().isEmpty());
        QVERIFY(panel.loadCapture(panel.captureBytes()));
        const QList<QByteArray> bad={"{}",bytes.left(60),QByteArray(65537,' '),
            QByteArray(bytes).replace("\"bank\":0","\"bank\":-1"),
            QByteArray(bytes).replace("\"bank\":0","\"bank\":0.5"),
            QByteArray(bytes).replace("\"value\":34","\"value\":4294967296"),
            QByteArray(bytes).replace("\"offset\":4624","\"offset\":516")};
        for(const auto &input:bad) {
            QVERIFY(!panel.loadCapture(input)); QCOMPARE(table->rowCount(),0);
            QVERIFY(panel.captureBytes().isEmpty()); QVERIFY(!panel.findChild<QPushButton *>("umcSave")->isEnabled());
        }
        QCOMPARE(reference.calls.load(),0);
    }
    void umcRejectsInvalidTargetAndWrongVendorWithoutPci() {
        PstateReference reference; reference.amd=false; UmcPanel panel(reference.access(),nullptr,3);
        panel.findChild<QLineEdit *>("umcBus")->setText("100");
        auto *read=panel.findChild<QPushButton *>("umcRead"); read->click(); QCOMPARE(reference.calls.load(),0);
        panel.findChild<QLineEdit *>("umcBus")->setText("0"); read->click(); QTRY_VERIFY(read->isEnabled());
        QCOMPARE(reference.writes.load(),0); QVERIFY(reference.msrs.empty());
        QCOMPARE(panel.findChild<QTableWidget *>("umcTable")->rowCount(),0);
        QVERIFY(panel.findChild<QLabel *>("umcStatus")->text().contains("Read failed"));
    }
    void amdTopologyShowsSparseIdsWithoutPreparingACommand() {
        PstateReference reference; reference.extendedMaximum=0x80000026;
        AmdTuningPanel panel(reference.access(),nullptr,3);
        auto *read=panel.findChild<QPushButton *>("smuTopology"); read->click(); QTRY_VERIFY(read->isEnabled());
        auto *table=panel.findChild<QTableWidget *>("smuTable"); QVERIFY(table);
        QCOMPARE(table->rowCount(),10);
        QCOMPARE(table->item(3,1)->text(),QString("10"));
        QCOMPARE(table->item(4,1)->text(),QString("7"));
        QCOMPARE(panel.findChild<QLineEdit *>("smuArg0")->text(),QString("0"));
        QCOMPARE(reference.writes.load(),0); QCOMPARE(reference.wrongCpu.load(),0); QVERIFY(reference.msrs.empty());
        reference.extendedMaximum=0x80000025; read->click(); QTRY_VERIFY(read->isEnabled());
        QCOMPARE(table->rowCount(),0);
        QVERIFY(panel.findChild<QLabel *>("smuStatus")->text().contains("unavailable"));
    }
    void unsupportedIntelProfileDoesNotReadOrWriteMsrs() {
        PstateReference reference; reference.amd=false; auto access=reference.access();
        IntelControlsPanel intel(access,nullptr,3); intel.show();
        auto *read=intel.findChild<QPushButton *>("intelRead"); read->click(); QTRY_VERIFY(read->isEnabled());
        QVERIFY(reference.msrs.empty()); QCOMPARE(reference.writes.load(),0);
        QCOMPARE(intel.findChild<QComboBox *>("intelField")->count(),0);
        QVERIFY(intel.findChild<QLabel *>("intelStatus")->text().contains("No verified RAPL profile"));
    }
    void smuProbeRejectsInvalidInputsBeforeAnyAccess() {
        Reference reference; AmdTuningPanel panel(reference.access());
        panel.findChild<QLineEdit *>("smuBus")->setText("100");
        panel.findChild<QPushButton *>("smuProbe")->click();
        QCOMPARE(reference.calls.load(),0);
        QVERIFY(panel.findChild<QLabel *>("smuStatus")->text().contains("Invalid"));
        panel.findChild<QLineEdit *>("smuCcd")->setText("3");
        panel.findChild<QLineEdit *>("smuCore")->setText("7");
        panel.findChild<QLineEdit *>("smuMhz")->setText("6000");
        panel.findChild<QPushButton *>("smuEncodeFrequency")->click();
        QCOMPARE(panel.findChild<QLineEdit *>("smuArg0")->text(),QString("30701770"));
        QCOMPARE(panel.findChild<QComboBox *>("smuCommand")->currentData().toInt(),0x27);
        QCOMPARE(reference.calls.load(),0);
        panel.findChild<QLineEdit *>("smuCcd")->setText("16");
        panel.findChild<QPushButton *>("smuEncodeFrequency")->click();
        QCOMPARE(panel.findChild<QLineEdit *>("smuArg0")->text(),QString("30701770"));
        QCOMPARE(reference.calls.load(),0);
    }
    void curveRejectsInvalidInputsWithoutHardware() {
        Reference reference; AmdTuningPanel panel(reference.access());
        auto *read = panel.findChild<QPushButton *>("curveRead");
        auto *ccd = panel.findChild<QLineEdit *>("curveCcd");
        auto *core = panel.findChild<QLineEdit *>("curveCore");
        read->click(); QCOMPARE(reference.calls.load(), 0);
        core->setText("7");
        for (const QString &value : {QString("-1"), QString("16"), QString("0x3"), QString("3.5"), QString("4294967296")}) {
            ccd->setText(value); read->click(); QCOMPARE(reference.calls.load(), 0);
        }
        ccd->setText("3"); core->setText("8"); read->click(); QCOMPARE(reference.calls.load(), 0);
        core->setText("7"); panel.findChild<QLineEdit *>("smuBus")->setText("01");
        read->click(); QCOMPARE(reference.calls.load(), 0);
        panel.findChild<QLineEdit *>("smuBus")->setText("00");
        panel.findChild<QComboBox *>("smuProfile")->setCurrentIndex(1);
        read->click(); QCOMPARE(reference.calls.load(), 0);
        QVERIFY(panel.findChild<QLabel *>("smuStatus")->text().contains("No query sent"));
    }
    void curveDisplaysRawAndInvalidatesChangedTarget() {
        auto *device = new AmdCurveFixture;
        auto access = std::make_shared<HardwareAccess>(std::unique_ptr<octool::core::HardwareBackend>(device));
        AmdTuningPanel panel(access, nullptr, 130); panel.show();
        panel.findChild<QLineEdit *>("curveCcd")->setText("3");
        panel.findChild<QLineEdit *>("curveCore")->setText("7");
        auto *read = panel.findChild<QPushButton *>("curveRead");
        auto *table = panel.findChild<QTableWidget *>("smuTable");
        QCOMPARE(device->calls, 0u); read->click(); QTRY_VERIFY(panel.isEnabled());
        QCOMPARE(table->rowCount(), 5); QCOMPARE(table->item(3, 1)->text(), hexValue(0xffffffe2, 4));
        QCOMPARE(table->item(4, 1)->text(), QString("-30"));
        QCOMPARE(device->submitted, 0x30700000u); QCOMPARE(device->commands, 1u); QCOMPARE(device->wrongCpu, 0u);
        QCOMPARE(panel.findChild<QLineEdit *>("smuArg0")->text(), QString("0"));
        QCOMPARE(panel.findChild<QLineEdit *>("smuCcd")->text(), QString("0"));
        panel.findChild<QLineEdit *>("curveCore")->setText("6"); QCOMPARE(table->rowCount(), 0);
        read->click(); QTRY_VERIFY(panel.isEnabled()); QCOMPARE(table->rowCount(), 5);
        panel.findChild<QLineEdit *>("smuCpu")->setText("1"); QCOMPARE(table->rowCount(), 0);
    }
    void curveNeverDisplaysFailedOrStaleResults() {
        auto *device = new AmdCurveFixture;
        auto access = std::make_shared<HardwareAccess>(std::unique_ptr<octool::core::HardwareBackend>(device));
        AmdTuningPanel panel(access, nullptr, 130);
        panel.findChild<QLineEdit *>("curveCcd")->setText("0");
        panel.findChild<QLineEdit *>("curveCore")->setText("0");
        auto *read = panel.findChild<QPushButton *>("curveRead");
        auto *table = panel.findChild<QTableWidget *>("smuTable");
        read->click(); QTRY_VERIFY(panel.isEnabled()); QCOMPARE(table->rowCount(), 5);
        device->completion = 0xfe; read->click(); QTRY_VERIFY(panel.isEnabled());
        QCOMPARE(table->rowCount(), 0); QCOMPARE(device->argumentReads, 1u);
        QVERIFY(panel.findChild<QLabel *>("smuStatus")->text().contains("unavailable"));
        device->pciId = 0x14d81022; read->click(); QTRY_VERIFY(panel.isEnabled());
        QCOMPARE(table->rowCount(), 0); QCOMPARE(device->commands, 2u);
    }
    void inventoryReadsUnitsErrorsAndBoundSpdFixtures() {
        QTemporaryDir root; QVERIFY(root.isValid());
        auto put=[&root](const QString &relative,const QByteArray &data) {
            const QString path=root.path()+relative; QVERIFY(QDir().mkpath(QFileInfo(path).path()));
            QFile f(path); QVERIFY(f.open(QIODevice::WriteOnly)); QCOMPARE(f.write(data),qint64(data.size()));
        };
        put("/class/dmi/id/board_name","Fixture board\n");
        put("/class/hwmon/hwmon0/name","Fixture sensor\n");
        put("/class/hwmon/hwmon0/temp1_input","-1250\n");
        put("/class/hwmon/hwmon0/temp1_label","CPU\n");
        put("/class/hwmon/hwmon0/temp1_fault","1\n");
        put("/class/hwmon/hwmon0/in1_input","invalid\n");
        put("/class/hwmon/hwmon0/power1_average","125000000\n");
        QByteArray spd(1024,0); spd[2]=0x12;
        put("/bus/i2c/drivers/spd5118/0-0050/eeprom",spd);
        const auto result=octool::platform::linuxInventory(root.path());
        QCOMPARE(result.spd.size(),1); QCOMPARE(result.spd[0].bytes,spd);
        bool temperature=false,power=false,invalid=false,board=false;
        for(const auto &r:result.rows) {
            if(r.name=="CPU") { temperature=true; QCOMPARE(r.value,QString("-1.25")); QCOMPARE(r.unit,QString("C")); QVERIFY(r.status.contains("fault")); }
            if(r.name=="power1 (average)") { power=true; QCOMPARE(r.value,QString("125")); QCOMPARE(r.unit,QString("W")); }
            if(r.name=="in1") { invalid=true; QVERIFY(r.value.isEmpty()); QVERIFY(r.status.contains("Invalid")); }
            if(r.name=="board_name") { board=true; QCOMPARE(r.value,QString("Fixture board")); }
        }
        QVERIFY(temperature && power && invalid && board);
    }
    void linuxCpuInformationAndAffinityAreExplicit() {
        using namespace octool::platform;
        QCOMPARE(QString::fromStdString(parseLinuxCpuModel("processor: 8\n model name \t:  Example CPU  \r\n")), QString("Example CPU"));
        QVERIFY(parseLinuxCpuModel("model names: Wrong\nmodel name: \n").empty());
        const auto original = systemInfo();
        QCOMPARE(original.affinityError, 0);
        QVERIFY(!original.allowedCpus.empty());
        const unsigned selected = original.allowedCpus.back();
        QCOMPARE(onLinuxCpu(selected, [selected] {
            const auto limited = systemInfo();
            return limited.affinityError == 0 && limited.allowedCpus == std::vector<unsigned>{selected} ? -EIO : -EINVAL;
        }), -EIO);
        QVERIFY(systemInfo().allowedCpus == original.allowedCpus);
        QCOMPARE(onLinuxCpu(UINT32_MAX, [] { return 0; }), -ERANGE);
        Reference reference; auto access = reference.access();
        RegisterPanel msr(Space::Msr, access, nullptr, selected);
        PstatesPanel pstates(access, nullptr, selected);
        QCOMPARE(msr.findChild<QLineEdit *>("cpu")->text(), QString::number(selected));
        QCOMPARE(pstates.findChild<QLineEdit *>("pstateCpu")->text(), QString::number(selected));
        QCOMPARE(reference.calls.load(), 0);
    }
    void authorizationIsExplicitAndFailureRetainsBackend() {
        std::atomic<int> attempts{0};
        auto access = std::make_shared<HardwareAccess>(octool::platform::makeLinuxHardwareBackend(nullptr),
            [&attempts](const std::atomic<bool> &) {
                ++attempts; octool::platform::BackendConnection result; result.error = -EACCES; return result;
            });
        AuthorizationPanel panel(access); panel.show();
        QCOMPARE(attempts.load(), 0);
        auto *authorize = panel.findChild<QPushButton *>("authorizeHardware");
        authorize->click(); QTRY_VERIFY(authorize->isEnabled());
        QCOMPARE(attempts.load(), 1);
        QVERIFY(panel.findChild<QLabel *>("authorizationStatus")->text().contains("failed"));
        QCOMPARE(access->execute(Request{}).error, -ENOMEM);
    }
    void authorizationCancellationAndSuccessfulReplacement() {
        std::atomic<bool> entered{false};
        auto access = std::make_shared<HardwareAccess>(octool::platform::makeLinuxHardwareBackend(nullptr),
            [&entered](const std::atomic<bool> &cancelled) {
                entered.store(true);
                for (unsigned i = 0; i < 2000 && !cancelled.load(); ++i)
                    std::this_thread::sleep_for(std::chrono::milliseconds(1));
                octool::platform::BackendConnection result;
                result.error = cancelled.load() ? -ECANCELED : -ETIMEDOUT; return result;
            });
        AuthorizationPanel panel(access); panel.show();
        auto *authorize = panel.findChild<QPushButton *>("authorizeHardware");
        authorize->click(); QTRY_VERIFY(entered.load());
        panel.findChild<QPushButton *>("cancelAuthorization")->click();
        QTRY_VERIFY(authorize->isEnabled());
        QVERIFY(panel.findChild<QLabel *>("authorizationStatus")->text().contains("cancelled"));
        QCOMPARE(access->execute(Request{}).error, -ENOMEM);
        Reference reference;
        HardwareAccess granted(octool::platform::makeLinuxHardwareBackend(nullptr),
            [&reference](const std::atomic<bool> &) {
                hwio_transport transport{&reference, Reference::submit, nullptr, "authorized test"};
                octool::platform::BackendConnection result;
                result.backend = octool::platform::makeLinuxHardwareBackend(hwio_open_transport(&transport));
                return result;
            });
        std::atomic<bool> cancelled{false};
        QCOMPARE(granted.authorize(cancelled), 0);
        QCOMPARE(reference.calls.load(), 0);
        QCOMPARE(granted.execute(Request{}).value, std::uint64_t(0xfedcba9876543210ULL));
    }
    void pstateFrequencyAndInvalidDefinitions() {
        // Literal specification vectors, not values generated by the decoder.
        QCOMPARE(decodeFamily1aPstate(0x80000001000003e8ULL).frequencyMHz, 5000u);
        QCOMPARE(decodeFamily1aPstate(0x80000000000001ccULL).frequencyMHz, 2300u);
        QCOMPARE(decodeFamily1aPstate(0x800000000000012cULL).frequencyMHz, 1500u);
        QCOMPARE(decodeFamily1aPstate(0x8000000000000010ULL).frequencyMHz, 80u);
        QCOMPARE(decodeFamily1aPstate(0x8000000000000fffULL).frequencyMHz, 20475u);
        QVERIFY(!decodeFamily1aPstate(0x800000000000000fULL).validFrequency);
        QVERIFY(!decodeFamily1aPstate(0x00000000000003e8ULL).validFrequency);
        QVERIFY(!decodeFamily1aPstate(0x00000000000003e8ULL).enabled);
    }
    void pstateCpuGuardsPreventMsrAccess() {
        for (int unsupported = 0; unsupported < 5; ++unsupported) {
            PstateReference reference;
            if (unsupported == 0) reference.amd = false;
            if (unsupported == 1) reference.signature = 0x00a00f11; // Family 19h.
            if (unsupported == 2) reference.hardwarePstate = false;
            if (unsupported == 3) reference.extendedMaximum = 0x80000006;
            if (unsupported == 4) reference.failLeaf = 1;
            auto access = reference.access();
            auto snapshot = readAmdPstates(*access, 3);
            QVERIFY(!snapshot.status.isEmpty());
            QVERIFY(reference.msrs.empty());
            QCOMPARE(reference.writes.load(), 0);
            QCOMPARE(reference.wrongCpu.load(), 0);
            for (const auto &row : snapshot.rows) QVERIFY(!row.read);
        }
    }
    void pstateLimitErrorsAndRawBitsSurvive() {
        PstateReference reference; auto access = reference.access();
        reference.failRegister = 0xc0010066;
        auto snapshot = readAmdPstates(*access, 3);
        QVERIFY(reference.msrs == std::vector<uint32_t>({0xc0010061, 0xc0010064, 0xc0010065, 0xc0010066}));
        QCOMPARE(snapshot.rows[0].raw, quint64(0x80000001000003e8ULL));
        QVERIFY(snapshot.rows[0].read); QVERIFY(snapshot.rows[1].read);
        QVERIFY(!snapshot.rows[2].read); QVERIFY(snapshot.rows[2].status.contains("FAILED"));
        for (int i = 3; i < 8; ++i) QVERIFY(!snapshot.rows[i].read);
        QCOMPARE(reference.writes.load(), 0); QCOMPARE(reference.wrongCpu.load(), 0);
        reference.msrs.clear(); reference.failRegister = 0xc0010061;
        snapshot = readAmdPstates(*access, 3);
        QCOMPARE(reference.msrs.size(), size_t(1));
        for (const auto &row : snapshot.rows) QVERIFY(!row.read);
    }
    void pstateWindowStartsIdleAndClearsStaleValues() {
        PstateReference reference; PstatesPanel panel(reference.access()); panel.show();
        QCOMPARE(reference.calls.load(), 0);
        auto *cpu = panel.findChild<QLineEdit *>("pstateCpu");
        auto *read = panel.findChild<QPushButton *>("pstateRead");
        auto *table = panel.findChild<QTableWidget *>("pstateTable");
        cpu->setText("3"); read->click(); QTRY_VERIFY(panel.isEnabled());
        QCOMPARE(table->item(0, 2)->text(), QString("0x80000001000003E8"));
        QCOMPARE(table->item(0, 4)->text(), QString("5000"));
        QCOMPARE(table->item(1, 3)->text(), QString("No"));
        QCOMPARE(table->item(1, 4)->text(), QString("—"));
        QCOMPARE(table->item(2, 4)->text(), QString("—"));
        cpu->setText("5"); QCOMPARE(table->item(0, 2)->text(), QString("—"));
        reference.expectedCpu = 5; reference.failRegister = 0xc0010061;
        read->click(); QTRY_VERIFY(panel.isEnabled());
        QCOMPARE(table->item(0, 4)->text(), QString("—"));
        QVERIFY(panel.findChild<QLabel *>("pstateStatus")->text().contains("FAILED"));
        QCOMPARE(reference.writes.load(), 0); QCOMPARE(reference.wrongCpu.load(), 0);
    }
    void directCpuidRestoresThreadAffinity() {
        cpu_set_t before, after;
        QCOMPARE(sched_getaffinity(0, sizeof(before), &before), 0);
        unsigned cpu = 0;
        while (cpu < CPU_SETSIZE && !CPU_ISSET(cpu, &before)) ++cpu;
        QVERIFY(cpu < CPU_SETSIZE);
        HardwareAccess access(octool::platform::makeLinuxHardwareBackend(
            hwio_open("/nonexistent-octool-regression-device")));
        QCOMPARE(access.cpuid(cpu, 0).error, 0);
        QCOMPARE(sched_getaffinity(0, sizeof(after), &after), 0);
        QVERIFY(CPU_EQUAL(&before, &after));
        QCOMPARE(access.cpuid(UINT32_MAX, 0).error, -ERANGE);
    }
    void failedBackendDoesNotReopenOrExposeCpuidData() {
        HardwareAccess unavailable(octool::platform::makeLinuxHardwareBackend(nullptr));
        QCOMPARE(unavailable.backend(Space::Msr), QString("none"));
        QCOMPARE(unavailable.execute(Request{}).error, -ENOMEM);
        QCOMPARE(unavailable.cpuid(0, 0).error, -ENOMEM);
        PstateReference reference; auto access = reference.access();
        reference.failLeaf = 1;
        const auto result = access->cpuid(3, 1);
        QCOMPARE(result.error, -EACCES);
        for (auto word : result.words) QCOMPARE(word, std::uint32_t(0));
    }
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
        r = Request{}; r.space = static_cast<Space>(99);
        QCOMPARE(access->execute(r).error, -EINVAL);
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
            if (box) box->button(QMessageBox::Cancel)->click();
        });
        write->click(); QCOMPARE(reference.calls.load(), 0);
        QTimer::singleShot(0, [] {
            auto *box = qobject_cast<QMessageBox *>(QApplication::activeModalWidget());
            if (box) box->button(QMessageBox::Yes)->click();
        });
        write->click(); QTRY_VERIFY(panel.isEnabled());
        QCOMPARE(reference.calls.load(), 1);
        QCOMPARE(reference.last.data1, uint64_t(0xfedcba9876543210ULL));
        QVERIFY(panel.findChild<QLineEdit *>("result")->text().isEmpty());
    }
    void lostWriteReplyReportsUncertainCompletion() {
        Reference reference; reference.error = -EPIPE;
        RegisterPanel panel(Space::Msr, reference.access()); panel.show();
        panel.findChild<QLineEdit *>("address")->setText("123");
        panel.findChild<QLineEdit *>("value")->setText("FEDCBA9876543210");
        QTimer::singleShot(0, [] {
            auto *box = qobject_cast<QMessageBox *>(QApplication::activeModalWidget());
            if (box) box->button(QMessageBox::Yes)->click();
        });
        panel.findChild<QPushButton *>("write")->click();
        QTRY_VERIFY(panel.isEnabled());
        QCOMPARE(reference.calls.load(), 1);
        const auto status = panel.findChild<QLabel *>("status")->text();
        QVERIFY(status.contains("FAILED") && status.contains("Write completion is unknown"));
        QVERIFY(panel.findChild<QLineEdit *>("result")->text().isEmpty());
    }
};
QTEST_MAIN(Regression)
#include "regression.moc"
