// SPDX-License-Identifier: GPL-2.0-only
#include "registerpanel.h"
#include "pstates.h"
#include "platformpanels.h"
#include "umcpanel.h"
#include "intelocpanel.h"
#include "intelvfpanel.h"
#include "intelturbopanel.h"
#include "intel_oc_fixture.h"
#include "intel_controls_fixture.h"
#include "intel_turbo_fixture.h"
#include "inteluncorepanel.h"
#include "intel_uncore_fixture.h"
#include "intelmemorypanel.h"
#include "intel_memory_fixture.h"
#include "spd_fixture.h"
#include "amd_curve_fixture.h"
#include "amdpmpanel.h"
#include "amd_pm_fixture.h"
#include "platform/linux_inventory.h"
#include "platform/linux_hwio.h"
#include "platform/linux_cpu.h"
#include "authorizationpanel.h"
#include "../../port/abi/octool_hwio_abi.h"
#include <QtTest>
#include <QComboBox>
#include <QCheckBox>
#include <QClipboard>
#include <QLineEdit>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QTimer>
#include <QTableWidget>
#include <QHeaderView>
#include <QTemporaryDir>
#include <QFile>
#include <QDir>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
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

static bool savePmFile(const QString &path, const std::vector<std::uint8_t> &bytes) {
    QFile file(path);
    return file.open(QIODevice::WriteOnly) && file.write(reinterpret_cast<const char *>(bytes.data()), qint64(bytes.size())) == qint64(bytes.size());
}
static bool savePmJson(const QString &path, const std::string &json) {
    return savePmFile(path, std::vector<std::uint8_t>(json.begin(), json.end()));
}
static octool::core::AmdPmReader pmReader(const std::shared_ptr<pm_fixture::Driver> &driver) {
    return [driver](const std::string &name, std::vector<std::uint8_t> &bytes, std::size_t maximum) { return driver->read(name, bytes, maximum); };
}

static std::shared_ptr<HardwareAccess> intelMemoryAccess(const std::shared_ptr<intel_memory_fixture::Device> &device) {
    struct Adapter : octool::core::HardwareBackend {
        std::shared_ptr<intel_memory_fixture::Device> d;
        explicit Adapter(std::shared_ptr<intel_memory_fixture::Device> value) : d(std::move(value)) {}
        Reply execute(const Request &r) override { return d->execute(r); }
        CpuIdReply cpuid(unsigned cpu,std::uint32_t leaf,std::uint32_t sub) override { return d->cpuid(cpu,leaf,sub); }
        octool::core::Backend backend(Space s) const override { return d->backend(s); }
    };
    return std::make_shared<HardwareAccess>(std::unique_ptr<octool::core::HardwareBackend>(new Adapter(device)));
}

class Regression : public QObject {
    Q_OBJECT
private slots:
    void intelMemoryOfflineSnapshot() {
        using namespace octool::core;
        Reference reference; IntelMemoryPanel panel(reference.access(),nullptr,130); panel.resize(1250,1100); panel.show();
        auto snapshot = decodeIntelMemory({},intel_memory_fixture::registers());
        const auto bytes = QByteArray::fromStdString(intelMemoryJson(snapshot)); QVERIFY(panel.loadCapture(bytes));
        auto *table = panel.findChild<QTableWidget *>("imcTable"); QCOMPARE(table->rowCount(),30);
        QCOMPARE(table->item(0,0)->text(),QString("tCL")); QCOMPARE(table->item(0,1)->text(),QString("36"));
        QCOMPARE(table->item(5,0)->text(),QString("tWRPRE")); QCOMPARE(table->item(5,1)->text(),QString("80"));
        QCOMPARE(table->item(7,1)->text(),QString("Not used")); QCOMPARE(table->item(13,1)->text(),QString("24000"));
        QCOMPARE(table->item(22,1)->text(),QString("70")); QCOMPARE(table->item(25,1)->text(),QString("10"));
        panel.findChild<QPushButton *>("imcCopy")->click();
        const auto copied = QApplication::clipboard()->text().toUtf8(); QVERIFY(parseIntelMemoryCapture(copied.toStdString()).valid);
        QVERIFY(panel.loadCapture("{\"schema_version\":1,\"command\":\"intel-memory-decode\",\"ok\":true,\"error\":0,\"data\":{\"memory\":"+copied+"}}"));
        QCOMPARE(panel.captureBytes(),copied); QCOMPARE(reference.calls.load(),0);
        auto *status = panel.findChild<QLabel *>("imcStatus"); QVERIFY(status->text().contains("Offline"));
        QTRY_VERIFY(status->height() >= status->heightForWidth(status->width()));
        // Large system fonts can increase the minimum row height. Verify that
        // the normal viewport can reach the last row, then size the reference
        // image from the actual geometry instead of assuming 25 px rows.
        table->scrollToItem(table->item(29,1));
        QTRY_VERIFY(table->viewport()->rect().contains(table->visualItemRect(table->item(29,1))));
        table->scrollToTop(); QCoreApplication::processEvents();
        panel.resize(panel.width(),panel.height()+qMax(0,table->verticalHeader()->length()-table->viewport()->height())+8);
        QTRY_VERIFY(table->viewport()->rect().contains(table->visualItemRect(table->item(29,1))));
        const auto shot = qEnvironmentVariable("OCTOOL_INTEL_MEMORY_TEST_SCREENSHOT"); if (!shot.isEmpty()) QVERIFY(panel.grab().save(shot));
        auto v = intel_memory_fixture::registers(0); v[3].raw |= UINT64_C(1)<<30;
        QVERIFY(panel.loadCapture(QByteArray::fromStdString(intelMemoryJson(decodeIntelMemory({},v)))));
        QCOMPARE(table->item(7,1)->text(),QString("4")); QCOMPARE(table->item(11,1)->text(),QString("Not used"));
        QCOMPARE(table->item(0,2)->text(),QString("Encoded")); QVERIFY(status->text().contains("overrides"));
    }
    void intelMemoryInvalidCaptureClearsOldValues() {
        using namespace octool::core;
        Reference reference; IntelMemoryPanel panel(reference.access());
        const auto good = QByteArray::fromStdString(intelMemoryJson(decodeIntelMemory({},intel_memory_fixture::registers())));
        auto *table = panel.findChild<QTableWidget *>("imcTable");
        for (const auto &bad : QList<QByteArray>{"{}",good+"x",good.left(good.size()-1),QByteArray(65537,' '),"{\"schema_version\":1,\"ok\":false}"}) {
            QVERIFY(panel.loadCapture(good)); QVERIFY(!panel.loadCapture(bad)); QCOMPARE(table->rowCount(),0);
            QVERIFY(panel.captureBytes().isEmpty()); QVERIFY(!panel.findChild<QPushButton *>("imcCopy")->isEnabled());
            QVERIFY(!panel.findChild<QPushButton *>("imcSave")->isEnabled());
        }
        for (unsigned target = 0; target < 3; ++target) {
            QVERIFY(panel.loadCapture(good));
            if (!target) panel.findChild<QLineEdit *>("imcCpu")->setText("131");
            else panel.findChild<QComboBox *>(target == 1 ? "imcController" : "imcChannel")->setCurrentIndex(1);
            QCOMPARE(table->rowCount(),0); QVERIFY(panel.captureBytes().isEmpty());
        }
        QCOMPARE(reference.calls.load(),0);
    }
    void intelMemoryLiveReadsAndFailures() {
        auto device = std::make_shared<intel_memory_fixture::Device>();
        IntelMemoryPanel panel(intelMemoryAccess(device),nullptr,130); panel.show();
        auto *read = panel.findChild<QPushButton *>("imcRead"); auto *table = panel.findChild<QTableWidget *>("imcTable");
        for (int failure : {0,5,6,7,8,11,17,22,38}) {
            *device = intel_memory_fixture::Device(); read->click(); QTRY_VERIFY(read->isEnabled());
            QCOMPARE(table->rowCount(),30); QVERIFY(panel.findChild<QPushButton *>("imcCopy")->isEnabled());
            QVERIFY(!octool::core::parseIntelMemoryCapture(panel.captureBytes().toStdString()).error);
            *device = intel_memory_fixture::Device(); device->failRead = failure;
            read->click(); QCOMPARE(table->rowCount(),0); QTRY_VERIFY(read->isEnabled());
            QCOMPARE(table->rowCount(),0); QVERIFY(panel.captureBytes().isEmpty()); QCOMPARE(device->writes,0u);
            QVERIFY(panel.findChild<QLabel *>("imcStatus")->text().contains("Read failed"));
        }
    }
    void intelMemoryTargetAndCpuGates() {
        auto device = std::make_shared<intel_memory_fixture::Device>(); IntelMemoryPanel panel(intelMemoryAccess(device),nullptr,130);
        auto *read = panel.findChild<QPushButton *>("imcRead");
        panel.findChild<QLineEdit *>("imcCpu")->setText("not a CPU"); read->click(); QCOMPARE(device->cpuCalls,0u);
        panel.findChild<QLineEdit *>("imcCpu")->setText("130"); device->model = 0xba;
        read->click(); QTRY_VERIFY(read->isEnabled()); QVERIFY(device->requests.empty());
        *device = intel_memory_fixture::Device(); device->channel = 1; device->memory[0xd800] = 0;
        panel.findChild<QComboBox *>("imcChannel")->setCurrentIndex(1); read->click(); QTRY_VERIFY(read->isEnabled());
        QCOMPARE(device->reads,6u); QCOMPARE(panel.findChild<QTableWidget *>("imcTable")->rowCount(),0);
        *device = intel_memory_fixture::Device(); device->memory[0xd80c] = 0x10001800; device->memory[0xe454] = 15;
        panel.findChild<QComboBox *>("imcChannel")->setCurrentIndex(0); read->click(); QTRY_VERIFY(read->isEnabled());
        QCOMPARE(device->reads,7u); QCOMPARE(panel.findChild<QTableWidget *>("imcTable")->rowCount(),0);
    }
    void intelMemoryReadLifetimeAndTargetChange() {
        struct Gate { std::atomic<bool> entered{false}, release{false}; };
        for (bool close : {false,true}) {
            auto gate = std::make_shared<Gate>(); auto device = std::make_shared<intel_memory_fixture::Device>();
            device->afterRequest = [gate](const Request &) {
                if (gate->entered.exchange(true)) return;
                const auto deadline = std::chrono::steady_clock::now()+std::chrono::seconds(3);
                while (!gate->release && std::chrono::steady_clock::now()<deadline) std::this_thread::sleep_for(std::chrono::milliseconds(1));
            };
            std::weak_ptr<intel_memory_fixture::Device> alive = device;
            auto *panel = new IntelMemoryPanel(intelMemoryAccess(device),nullptr,130); device.reset();
            panel->findChild<QPushButton *>("imcRead")->click(); QTRY_VERIFY(gate->entered.load());
            QVERIFY(!panel->loadCapture("{}"));
            if (close) { delete panel; QVERIFY(!alive.expired()); gate->release = true; QTRY_VERIFY(alive.expired()); }
            else {
                panel->findChild<QComboBox *>("imcChannel")->setCurrentIndex(1); gate->release = true;
                QTRY_VERIFY(panel->findChild<QPushButton *>("imcRead")->isEnabled());
                QCOMPARE(panel->findChild<QTableWidget *>("imcTable")->rowCount(),0);
                QVERIFY(panel->findChild<QLabel *>("imcStatus")->text().contains("discarded")); delete panel;
            }
        }
    }

    void pmOfflineRoundtripAndUnits() {
        using namespace octool::core;
        QTemporaryDir directory; QVERIFY(directory.isValid());
        const auto raw = directory.filePath("synthetic-pm.bin"), json = directory.filePath(QString::fromUtf8("synthetic-PM-快照.json"));
        auto driver = std::make_shared<pm_fixture::Driver>(); AmdPmPanel panel(nullptr, pmReader(driver));
        panel.resize(1300, 1040); panel.show();
        auto *table = panel.findChild<QTableWidget *>("pmTable");
        auto *copy = panel.findChild<QPushButton *>("pmCopy");
        panel.findChild<QCheckBox *>("pmScale")->setChecked(true);
        panel.findChild<QLineEdit *>("pmBclk")->setText("102.5");
        for (const auto &layout : amdPmLayouts()) {
            QVERIFY(savePmFile(raw, pm_fixture::bytes(layout.bytes))); QVERIFY(panel.loadRawFile(raw, layout.version));
            QCOMPARE(table->rowCount(), 24); QCOMPARE(table->item(0, 1)->text(), QString("2000"));
            QCOMPARE(table->item(0, 3)->text(), QString("2050")); QCOMPARE(table->item(0, 4)->text(), QString("0x44fa0000"));
            QCOMPARE(table->item(3, 1)->text(), QString("1.0625")); QCOMPARE(table->item(3, 2)->text(), QString("V"));
            QCOMPARE(table->item(10, 1)->text(), QString("100")); QCOMPARE(table->item(10, 2)->text(), QString("A"));
            QCOMPARE(table->item(11, 1)->text(), QString("70")); QCOMPARE(table->item(11, 2)->text(), QString("C"));
            QVERIFY(copy->isEnabled()); copy->click();
            const auto captured = QApplication::clipboard()->text().toStdString();
            const auto decoded = parseAmdPmCapture(captured); QVERIFY(!decoded.error);
            QCOMPARE(qulonglong(decoded.raw.size()), qulonglong(layout.bytes));
            QVERIFY(savePmJson(json, captured)); QVERIFY(panel.loadSnapshotFile(json));
            QVERIFY(savePmJson(json, "{\"schema_version\":1,\"command\":\"amd-pm-decode\",\"data\":{\"pm\":" + captured + "}}"));
            QVERIFY(panel.loadSnapshotFile(json)); QCOMPARE(table->item(0, 3)->text(), QString("2050"));
        }
        auto captured = QApplication::clipboard()->text().toUtf8();
        captured.replace("\"stored_value\":2000", "\"stored_value\":99999");
        QVERIFY(savePmJson(json, captured.toStdString())); QVERIFY(panel.loadSnapshotFile(json));
        QCOMPARE(table->item(0, 1)->text(), QString("2000")); QVERIFY(driver->calls.empty());
        auto *status = panel.findChild<QLabel *>("pmStatus");
        QTRY_VERIFY(status->height() >= status->heightForWidth(status->width()));
        QVERIFY(status->geometry().bottom() < panel.height());
        const auto shot = qEnvironmentVariable("OCTOOL_PM_TEST_SCREENSHOT");
        if (!shot.isEmpty()) QVERIFY(panel.grab().save(shot));
    }
    void pmPartialErrorsAndFailedImportClearOldData() {
        QTemporaryDir directory; QVERIFY(directory.isValid()); const auto raw = directory.filePath("synthetic-nonfinite.bin");
        auto driver = std::make_shared<pm_fixture::Driver>(); AmdPmPanel panel(nullptr, pmReader(driver));
        panel.resize(1300, 1040); panel.show();
        auto *table = panel.findChild<QTableWidget *>("pmTable"); auto *copy = panel.findChild<QPushButton *>("pmCopy");
        auto bytes = pm_fixture::bytes(); pm_fixture::put(bytes, 0x20c, 0x7f800001);
        QVERIFY(savePmFile(raw, bytes)); QVERIFY(!panel.loadRawFile(raw, 0x730204));
        QCOMPARE(table->rowCount(), 24); QCOMPARE(table->item(0, 1)->text(), QString("Unavailable"));
        QCOMPARE(table->item(0, 4)->text(), QString("0x7f800001")); QCOMPARE(table->item(1, 1)->text(), QString("1800"));
        QVERIFY(copy->isEnabled()); copy->click();
        const auto json = directory.filePath("synthetic-partial.json");
        QVERIFY(savePmJson(json, QApplication::clipboard()->text().toStdString())); QVERIFY(!panel.loadSnapshotFile(json));
        QCOMPARE(table->rowCount(), 24); QVERIFY(copy->isEnabled());
        auto *status = panel.findChild<QLabel *>("pmStatus");
        QTRY_VERIFY(status->height() >= status->heightForWidth(status->width()));
        QVERIFY(status->geometry().bottom() < panel.height());
        const auto shot = qEnvironmentVariable("OCTOOL_PM_ERROR_TEST_SCREENSHOT");
        if (!shot.isEmpty()) QVERIFY(panel.grab().save(shot));
        for (unsigned kind = 0; kind < 5; ++kind) {
            QVERIFY(savePmFile(raw, pm_fixture::bytes())); QVERIFY(panel.loadRawFile(raw, 0x730204));
            if (kind == 0) QVERIFY(!panel.loadSnapshotFile(directory.filePath("missing")));
            if (kind == 1) { QVERIFY(savePmJson(json, "{}")); QVERIFY(!panel.loadSnapshotFile(json)); }
            if (kind == 2) { QVERIFY(savePmFile(raw, std::vector<std::uint8_t>(2843))); QVERIFY(!panel.loadRawFile(raw, 0x730204)); }
            if (kind == 3) { QVERIFY(savePmFile(raw, std::vector<std::uint8_t>(2845))); QVERIFY(!panel.loadRawFile(raw, 0x730204)); }
            if (kind == 4) QVERIFY(!panel.loadRawFile(raw, 0x730205));
            QCOMPARE(table->rowCount(), 0); QVERIFY(!copy->isEnabled());
        }
        QVERIFY(driver->calls.empty());
    }
    void pmValidationAndInputChangesDoNotReadDriver() {
        auto driver = std::make_shared<pm_fixture::Driver>(); AmdPmPanel panel(nullptr, pmReader(driver));
        auto *table = panel.findChild<QTableWidget *>("pmTable"); auto *copy = panel.findChild<QPushButton *>("pmCopy");
        auto *bclk = panel.findChild<QLineEdit *>("pmBclk"); auto *scale = panel.findChild<QCheckBox *>("pmScale");
        QCOMPARE(table->rowCount(), 0); QVERIFY(!copy->isEnabled()); QVERIFY(driver->calls.empty());
        QVERIFY(table->columnWidth(3) >= table->fontMetrics().boundingRect(table->horizontalHeaderItem(3)->text()).width());
        scale->setChecked(true);
        for (const auto &text : QStringList{"", "0", "-1", "nan", "inf", "1e999", "100 ", "100MHz", "1,00"}) {
            bclk->setText(text); panel.findChild<QPushButton *>("pmRead")->click();
            QVERIFY(driver->calls.empty()); QCOMPARE(table->rowCount(), 0); QVERIFY(!copy->isEnabled());
        }
        QTemporaryDir directory; QVERIFY(directory.isValid()); const auto raw = directory.filePath("synthetic.bin");
        QVERIFY(savePmFile(raw, pm_fixture::bytes())); bclk->setText("100");
        QVERIFY(panel.loadRawFile(raw, 0x730204)); bclk->setText("101"); QCOMPARE(table->rowCount(), 0);
        QVERIFY(panel.loadRawFile(raw, 0x730204)); scale->setChecked(false); QCOMPARE(table->rowCount(), 0);
        QVERIFY(panel.loadRawFile(raw, 0x730204)); panel.findChild<QComboBox *>("pmVersion")->setCurrentIndex(2);
        QCOMPARE(table->rowCount(), 0); QVERIFY(!copy->isEnabled()); QVERIFY(driver->calls.empty());
    }
    void pmDriverReadAndFailurePresentation() {
        auto driver = std::make_shared<pm_fixture::Driver>(); AmdPmPanel panel(nullptr, pmReader(driver)); panel.show();
        auto *read = panel.findChild<QPushButton *>("pmRead"); auto *table = panel.findChild<QTableWidget *>("pmTable");
        auto *copy = panel.findChild<QPushButton *>("pmCopy");
        for (unsigned stage = 0; stage < 7; ++stage) {
            *driver = pm_fixture::Driver(); read->click(); QTRY_VERIFY(read->isEnabled());
            QCOMPARE(table->rowCount(), 24); QVERIFY(copy->isEnabled());
            *driver = pm_fixture::Driver();
            if (stage < 5) driver->failCall = stage + 1;
            else if (stage == 5) driver->mutateAfter = 4;
            else driver->files["pm_table_version"] = pm_fixture::le(0x730205, 4);
            read->click(); QCOMPARE(table->rowCount(), 0); QVERIFY(!copy->isEnabled());
            QTRY_VERIFY(read->isEnabled()); QCOMPARE(table->rowCount(), 0); QVERIFY(!copy->isEnabled());
            QCOMPARE(driver->calls.size(), std::size_t(stage < 5 ? stage + 1 : stage == 5 ? 5 : 2));
            const auto status = panel.findChild<QLabel *>("pmStatus")->text();
            QVERIFY(status.contains(stage < 5 ? "Permission denied" : stage == 5 ? "discarded" : "Unsupported"));
        }
    }
    void pmClosingPanelWhileReaderRuns() {
        struct State { std::atomic<bool> entered{false}, release{false}; };
        auto state = std::make_shared<State>(); auto driver = std::make_shared<pm_fixture::Driver>();
        std::weak_ptr<pm_fixture::Driver> alive = driver;
        auto *panel = new AmdPmPanel(nullptr, [driver, state](const std::string &name, std::vector<std::uint8_t> &bytes, std::size_t maximum) {
            state->entered = true;
            const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
            while (!state->release && std::chrono::steady_clock::now() < deadline) std::this_thread::sleep_for(std::chrono::milliseconds(1));
            return driver->read(name, bytes, maximum);
        });
        driver.reset(); panel->findChild<QPushButton *>("pmRead")->click();
        QTRY_VERIFY(state->entered.load()); QVERIFY(!panel->loadSnapshotFile("unused-while-busy"));
        delete panel; QVERIFY(!alive.expired()); state->release = true;
        QTRY_VERIFY(alive.expired());
    }
    void platformPanelsDoNotAccessHardwareAtConstruction() {
        Reference reference; auto access=reference.access();
        IntelControlsPanel intel(access,nullptr,7); AmdTuningPanel amd(access,nullptr,7); MemoryBoardPanel inventory;
        UmcPanel umc(access,nullptr,7);
        IntelOcPanel oc(access,nullptr,7);
        IntelVfPanel vf(access,nullptr,7);
        IntelTurboPanel turbo(access,nullptr,7);
        IntelUncorePanel uncore(access,nullptr,7);
        IntelMemoryPanel imc(access,nullptr,7);
        QCOMPARE(imc.findChild<QTableWidget *>("imcTable")->rowCount(),0);
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
        QCOMPARE(turbo.findChild<QLineEdit *>("turboCpu")->text(),QString("7"));
        QVERIFY(!turbo.findChild<QPushButton *>("turboApply")->isEnabled());
        QCOMPARE(uncore.findChild<QLineEdit *>("uncoreCpu")->text(),QString("7"));
        QVERIFY(!uncore.findChild<QPushButton *>("uncoreApply")->isEnabled());
        intel.findChild<QLineEdit *>("intelValue")->setText("125");
        intel.findChild<QPushButton *>("intelApply")->click();
        QCOMPARE(reference.calls.load(),0);
    }
    void intelUncoreReadAndTargetChange() {
        auto *device = new IntelUncoreFixture;
        auto access = std::make_shared<HardwareAccess>(std::unique_ptr<octool::core::HardwareBackend>(device));
        IntelUncorePanel panel(access, nullptr, 130); panel.show();
        auto *read = panel.findChild<QPushButton *>("uncoreRead"); auto *table = panel.findChild<QTableWidget *>("uncoreTable");
        auto *apply = panel.findChild<QPushButton *>("uncoreApply");
        read->click(); QTRY_VERIFY(panel.isEnabled());
        QCOMPARE(table->rowCount(), 2); QCOMPARE(table->item(0, 1)->text(), QString("8"));
        QCOMPARE(table->item(1, 1)->text(), QString("45")); QVERIFY(apply->isEnabled());
        panel.findChild<QPushButton *>("uncoreCopy")->click();
        QVERIFY(QApplication::clipboard()->text().contains(hexValue(device->regs[0x620], 8)));
        const auto calls = device->cpuCalls;
        panel.findChild<QLineEdit *>("uncoreCpu")->setText("-1"); read->click();
        QCOMPARE(device->cpuCalls, calls); QCOMPARE(table->rowCount(), 0); QVERIFY(!apply->isEnabled());
        panel.findChild<QLineEdit *>("uncoreCpu")->setText("130"); device->hypervisor = true;
        const auto reads = device->reads; read->click(); QTRY_VERIFY(panel.isEnabled());
        QCOMPARE(device->reads, reads); QCOMPARE(table->rowCount(), 0); QVERIFY(!apply->isEnabled());
        device->hypervisor = false; device->model = 0x8f; device->regs[0x620] = 0;
        read->click(); QTRY_VERIFY(panel.isEnabled()); QCOMPARE(table->rowCount(), 2); QVERIFY(!apply->isEnabled());
        QCOMPARE(device->writes, 0u); QCOMPARE(device->wrongCpu, 0u);
    }
    void intelUncoreValidateCancelApplyAndNoop() {
        auto *device = new IntelUncoreFixture;
        auto access = std::make_shared<HardwareAccess>(std::unique_ptr<octool::core::HardwareBackend>(device));
        IntelUncorePanel panel(access, nullptr, 130); panel.resize(1100, 640); panel.show();
        panel.findChild<QPushButton *>("uncoreRead")->click(); QTRY_VERIFY(panel.isEnabled());
        auto *minimum = panel.findChild<QLineEdit *>("uncoreMinimum"); auto *maximum = panel.findChild<QLineEdit *>("uncoreMaximum");
        auto *apply = panel.findChild<QPushButton *>("uncoreApply"); auto *status = panel.findChild<QLabel *>("uncoreStatus");
        const auto calls = device->cpuCalls;
        for (const QString &value : {QString("0"), QString("128"), QString("46"), QString("8.5")}) {
            minimum->setText(value); apply->click(); QCOMPARE(device->cpuCalls, calls);
        }
        minimum->setText("12"); maximum->setText("42");
        QTimer::singleShot(0, [] { auto *box = qobject_cast<QMessageBox *>(QApplication::activeModalWidget()); if (box) box->button(QMessageBox::Cancel)->click(); });
        apply->click(); QCOMPARE(device->cpuCalls, calls); QCOMPARE(device->writes, 0u);
        const auto original = device->regs[0x620];
        QTimer::singleShot(0, [] { auto *box = qobject_cast<QMessageBox *>(QApplication::activeModalWidget()); if (box) box->button(QMessageBox::Yes)->click(); });
        apply->click(); QTRY_VERIFY(panel.isEnabled());
        QCOMPARE(device->writes, 1u); QCOMPARE(device->wrongCpu, 0u);
        QCOMPARE(device->regs[0x620] & ~UINT64_C(0x7f7f), original & ~UINT64_C(0x7f7f));
        QCOMPARE(minimum->text(), QString("12")); QCOMPARE(maximum->text(), QString("42"));
        QVERIFY(status->text().contains("readback verified"));
        QTRY_VERIFY(status->height() >= status->heightForWidth(status->width()));
        QVERIFY(status->geometry().bottom() < panel.height());
        const auto screenshot = qEnvironmentVariable("OCTOOL_UNCORE_TEST_SCREENSHOT");
        if (!screenshot.isEmpty()) QVERIFY(panel.grab().save(screenshot));
        QTimer::singleShot(0, [] { auto *box = qobject_cast<QMessageBox *>(QApplication::activeModalWidget()); if (box) box->button(QMessageBox::Yes)->click(); });
        apply->click(); QTRY_VERIFY(panel.isEnabled());
        QCOMPARE(device->writes, 1u); QVERIFY(status->text().contains("no write performed"));
    }
    void intelUncoreStaleAndFailedWritesClearSnapshot() {
        for (unsigned mode = 0; mode < 3; ++mode) {
            auto *device = new IntelUncoreFixture;
            auto access = std::make_shared<HardwareAccess>(std::unique_ptr<octool::core::HardwareBackend>(device));
            IntelUncorePanel panel(access, nullptr, 130); panel.show();
            panel.findChild<QPushButton *>("uncoreRead")->click(); QTRY_VERIFY(panel.isEnabled());
            panel.findChild<QLineEdit *>("uncoreMinimum")->setText("12");
            panel.findChild<QLineEdit *>("uncoreMaximum")->setText("42");
            if (mode == 0) device->regs[0x620] ^= UINT64_C(1) << 63;
            if (mode == 1) device->discardChange = true;
            if (mode == 2) device->failWrite = 0;
            QTimer::singleShot(0, [] { auto *box = qobject_cast<QMessageBox *>(QApplication::activeModalWidget()); if (box) box->button(QMessageBox::Yes)->click(); });
            panel.findChild<QPushButton *>("uncoreApply")->click(); QTRY_VERIFY(panel.isEnabled());
            QCOMPARE(device->writes, mode == 0 ? 0u : 1u);
            QCOMPARE(panel.findChild<QTableWidget *>("uncoreTable")->rowCount(), 0);
            QVERIFY(!panel.findChild<QPushButton *>("uncoreApply")->isEnabled());
            QVERIFY(panel.findChild<QLabel *>("uncoreRaw")->text().isEmpty());
            QVERIFY(panel.findChild<QLineEdit *>("uncoreMinimum")->text().isEmpty());
            QVERIFY(panel.findChild<QLabel *>("uncoreStatus")->text().contains(mode == 0 ? "No settings write" : "not verified"));
        }
    }
    void intelTurboReadsGroupsAndInvalidatesTargets() {
        auto *device = new IntelTurboFixture;
        auto access = std::make_shared<HardwareAccess>(std::unique_ptr<octool::core::HardwareBackend>(device));
        IntelTurboPanel panel(access, nullptr, 130); panel.show();
        auto *read = panel.findChild<QPushButton *>("turboRead"); auto *table = panel.findChild<QTableWidget *>("turboTable");
        auto *apply = panel.findChild<QPushButton *>("turboApply");
        QCOMPARE(device->reads, 0u); read->click(); QTRY_VERIFY(panel.isEnabled());
        QCOMPARE(table->rowCount(), 8); QCOMPARE(table->item(7, 1)->text(), QString("8"));
        QCOMPARE(table->item(0, 2)->text(), QString("60")); QVERIFY(apply->isEnabled());
        panel.findChild<QPushButton *>("turboCopy")->click();
        QVERIFY(QApplication::clipboard()->text().contains("Active-core threshold"));
        QVERIFY(QApplication::clipboard()->text().contains(hexValue(device->regs[0x1ad], 8)));
        const auto reads = device->reads;
        panel.findChild<QComboBox *>("turboKind")->setCurrentIndex(1);
        QCOMPARE(table->rowCount(), 0); QVERIFY(!apply->isEnabled()); QCOMPARE(device->reads, reads);
        read->click(); QTRY_VERIFY(panel.isEnabled());
        QCOMPARE(table->item(7, 1)->text(), QString("16")); QCOMPARE(table->item(0, 2)->text(), QString("44"));
        QCOMPARE(device->writes, 0u); QCOMPARE(device->wrongCpu, 0u);
        const auto calls = device->cpuCalls; panel.findChild<QLineEdit *>("turboCpu")->setText("-1"); read->click();
        QCOMPARE(device->cpuCalls, calls); QCOMPARE(table->rowCount(), 0); QVERIFY(!apply->isEnabled());
    }
    void intelTurboCancelApplyAndSkipUnchanged() {
        auto *device = new IntelTurboFixture;
        auto access = std::make_shared<HardwareAccess>(std::unique_ptr<octool::core::HardwareBackend>(device));
        IntelTurboPanel panel(access, nullptr, 130); panel.resize(1100, 640); panel.show();
        panel.findChild<QPushButton *>("turboRead")->click(); QTRY_VERIFY(panel.isEnabled());
        auto *group = panel.findChild<QComboBox *>("turboGroup"); auto *value = panel.findChild<QLineEdit *>("turboRatio");
        auto *apply = panel.findChild<QPushButton *>("turboApply"); auto *status = panel.findChild<QLabel *>("turboStatus");
        group->setCurrentIndex(3); value->setText("60"); const auto reads = device->reads;
        apply->click(); QCOMPARE(device->reads, reads); QCOMPARE(device->writes, 0u);
        value->setText("56");
        QTimer::singleShot(0, [] { auto *box = qobject_cast<QMessageBox *>(QApplication::activeModalWidget()); if (box) box->button(QMessageBox::Cancel)->click(); });
        apply->click(); QCOMPARE(device->reads, reads); QCOMPARE(device->writes, 0u);
        const auto previous = device->regs;
        QTimer::singleShot(0, [] { auto *box = qobject_cast<QMessageBox *>(QApplication::activeModalWidget()); if (box) box->button(QMessageBox::Yes)->click(); });
        apply->click(); QTRY_VERIFY(panel.isEnabled());
        QCOMPARE(device->writes, 1u); QCOMPARE(device->wrongCpu, 0u);
        QCOMPARE(device->regs[0x1ae], previous.at(0x1ae)); QCOMPARE(device->regs[0x650], previous.at(0x650));
        QVERIFY(value->text().isEmpty()); QVERIFY(status->text().contains("readback verified"));
        QCOMPARE(panel.findChild<QTableWidget *>("turboTable")->item(3, 2)->text(), QString("56"));
        const auto screenshot = qEnvironmentVariable("OCTOOL_TURBO_TEST_SCREENSHOT");
        if (!screenshot.isEmpty()) QVERIFY(panel.grab().save(screenshot));
        group->setCurrentIndex(3); value->setText("56");
        QTimer::singleShot(0, [] { auto *box = qobject_cast<QMessageBox *>(QApplication::activeModalWidget()); if (box) box->button(QMessageBox::Yes)->click(); });
        apply->click(); QTRY_VERIFY(panel.isEnabled());
        QCOMPARE(device->writes, 1u); QVERIFY(status->text().contains("no write performed")); QVERIFY(value->text().isEmpty());
    }
    void intelTurboPermissionsAndUnverifiedResults() {
        auto *device = new IntelTurboFixture;
        auto access = std::make_shared<HardwareAccess>(std::unique_ptr<octool::core::HardwareBackend>(device));
        IntelTurboPanel panel(access, nullptr, 130); panel.show();
        auto *read = panel.findChild<QPushButton *>("turboRead"); auto *apply = panel.findChild<QPushButton *>("turboApply");
        auto *table = panel.findChild<QTableWidget *>("turboTable"); auto *status = panel.findChild<QLabel *>("turboStatus");
        device->regs[0x194] |= UINT64_C(1) << 20; read->click(); QTRY_VERIFY(panel.isEnabled());
        QCOMPARE(table->rowCount(), 8); QVERIFY(!apply->isEnabled());
        device->regs[0x194] &= ~(UINT64_C(1) << 20); const auto counts = device->regs[0x1ae]; device->regs[0x1ae] = 0;
        read->click(); QTRY_VERIFY(panel.isEnabled()); QCOMPARE(table->rowCount(), 8); QVERIFY(!apply->isEnabled());
        device->regs[0x1ae] = counts; read->click(); QTRY_VERIFY(panel.isEnabled()); QVERIFY(apply->isEnabled());
        device->discardChange = true; panel.findChild<QComboBox *>("turboGroup")->setCurrentIndex(3);
        panel.findChild<QLineEdit *>("turboRatio")->setText("56");
        QTimer::singleShot(0, [] { auto *box = qobject_cast<QMessageBox *>(QApplication::activeModalWidget()); if (box) box->button(QMessageBox::Yes)->click(); });
        apply->click(); QTRY_VERIFY(panel.isEnabled()); QCOMPARE(device->writes, 1u);
        QCOMPARE(table->rowCount(), 0); QVERIFY(!apply->isEnabled()); QVERIFY(status->text().contains("not verified"));
        QVERIFY(panel.findChild<QLabel *>("turboRaw")->text().isEmpty());
        device->failRead = int(device->reads); read->click(); QTRY_VERIFY(panel.isEnabled());
        QCOMPARE(table->rowCount(), 0); QVERIFY(!apply->isEnabled()); QVERIFY(status->text().contains("No settings displayed"));
    }
    void intelTurboStaleCountsPreventWrites() {
        auto *device = new IntelTurboFixture;
        auto access = std::make_shared<HardwareAccess>(std::unique_ptr<octool::core::HardwareBackend>(device));
        IntelTurboPanel panel(access, nullptr, 130); panel.show();
        panel.findChild<QPushButton *>("turboRead")->click(); QTRY_VERIFY(panel.isEnabled());
        panel.findChild<QComboBox *>("turboGroup")->setCurrentIndex(3);
        panel.findChild<QLineEdit *>("turboRatio")->setText("56"); device->regs[0x1ae] ^= UINT64_C(1) << 56;
        QTimer::singleShot(0, [] { auto *box = qobject_cast<QMessageBox *>(QApplication::activeModalWidget()); if (box) box->button(QMessageBox::Yes)->click(); });
        panel.findChild<QPushButton *>("turboApply")->click(); QTRY_VERIFY(panel.isEnabled());
        QCOMPARE(device->writes, 0u); QCOMPARE(panel.findChild<QTableWidget *>("turboTable")->rowCount(), 0);
        QVERIFY(panel.findChild<QLabel *>("turboStatus")->text().contains("No settings write"));
    }
    void intelTurboThresholdEditing() {
        auto *device = new IntelTurboFixture;
        auto access = std::make_shared<HardwareAccess>(std::unique_ptr<octool::core::HardwareBackend>(device));
        IntelTurboPanel panel(access,nullptr,130); panel.resize(1240,700); panel.show();
        auto *kind = panel.findChild<QComboBox *>("turboKind"), *field = panel.findChild<QComboBox *>("turboField"), *group = panel.findChild<QComboBox *>("turboGroup");
        auto *value = panel.findChild<QLineEdit *>("turboRatio"); auto *apply = panel.findChild<QPushButton *>("turboApply");
        auto *status = panel.findChild<QLabel *>("turboStatus");
        kind->setCurrentIndex(1); panel.findChild<QPushButton *>("turboRead")->click(); QTRY_VERIFY(panel.isEnabled());
        QVERIFY(value->isEnabled());
        group->setCurrentIndex(3); value->setText("40"); field->setCurrentIndex(1); QVERIFY(value->text().isEmpty());
        group->setCurrentIndex(7); QVERIFY(!apply->isEnabled()); QVERIFY(!value->isEnabled()); QVERIFY(status->text().contains("last active"));
        field->setCurrentIndex(0); QVERIFY(apply->isEnabled()); field->setCurrentIndex(1); group->setCurrentIndex(3);
        const auto reads = device->reads; value->setText("10"); apply->click(); QCOMPARE(reads,device->reads); QCOMPARE(device->writes,0u);
        value->setText("9");
        QTimer::singleShot(0, [] { auto *box = qobject_cast<QMessageBox *>(QApplication::activeModalWidget()); if (box) box->button(QMessageBox::Cancel)->click(); });
        apply->click(); QCOMPARE(reads,device->reads); QCOMPARE(device->writes,0u);
        const auto before = device->regs;
        QTimer::singleShot(0, [] { auto *box = qobject_cast<QMessageBox *>(QApplication::activeModalWidget()); if (box) box->button(QMessageBox::Yes)->click(); });
        apply->click(); QTRY_VERIFY(panel.isEnabled()); QCOMPARE(device->writes,1u);
        QCOMPARE(device->regs[0x651],UINT64_C(0x100e0c0a09060402)); QCOMPARE(device->regs[0x650],before.at(0x650));
        QCOMPARE(device->regs[0x1ae],before.at(0x1ae)); QVERIFY(status->text().contains("all ratios")); QVERIFY(value->text().isEmpty());
        QVERIFY(value->isEnabled());
        auto *table = panel.findChild<QTableWidget *>("turboTable"); QCOMPARE(table->item(3,1)->text(),QString("9"));
        QCOMPARE(table->item(7,1)->text(),QString("16")); QVERIFY(table->item(7,3)->text().contains("fixed"));
        group->setCurrentIndex(3); value->setText("9");
        QTest::qWait(50);
        QVERIFY(status->height() >= status->heightForWidth(status->width()));
        QVERIFY(status->geometry().bottom() < panel.height());
        const auto shot = qEnvironmentVariable("OCTOOL_TURBO_THRESHOLD_TEST_SCREENSHOT"); if (!shot.isEmpty()) QVERIFY(panel.grab().save(shot));
        QTimer::singleShot(0, [] { auto *box = qobject_cast<QMessageBox *>(QApplication::activeModalWidget()); if (box) box->button(QMessageBox::Yes)->click(); });
        apply->click(); QTRY_VERIFY(panel.isEnabled()); QCOMPARE(device->writes,1u); QVERIFY(status->text().contains("no write performed"));
        kind->setCurrentIndex(0); QVERIFY(!apply->isEnabled()); QCOMPARE(table->rowCount(),0); QVERIFY(value->text().isEmpty());
    }
    void intelTurboThresholdFailuresClearSnapshot() {
        for (unsigned mode = 0; mode < 3; ++mode) {
            auto *device = new IntelTurboFixture;
            auto access = std::make_shared<HardwareAccess>(std::unique_ptr<octool::core::HardwareBackend>(device));
            IntelTurboPanel panel(access,nullptr,130); panel.show();
            panel.findChild<QComboBox *>("turboKind")->setCurrentIndex(1);
            panel.findChild<QPushButton *>("turboRead")->click(); QTRY_VERIFY(panel.isEnabled());
            panel.findChild<QComboBox *>("turboField")->setCurrentIndex(1);
            panel.findChild<QComboBox *>("turboGroup")->setCurrentIndex(3);
            panel.findChild<QLineEdit *>("turboRatio")->setText("9");
            if (mode == 0) device->regs[0x650] ^= UINT64_C(1)<<48;
            if (mode == 1) device->discardChange = true;
            if (mode == 2) device->afterRequest = [&](const octool::core::Request &r) { if (r.write) device->regs[0x650] ^= UINT64_C(1)<<48; };
            QTimer::singleShot(0, [] { auto *box = qobject_cast<QMessageBox *>(QApplication::activeModalWidget()); if (box) box->button(QMessageBox::Yes)->click(); });
            panel.findChild<QPushButton *>("turboApply")->click(); QTRY_VERIFY(panel.isEnabled());
            QCOMPARE(device->writes,mode == 0 ? 0u : 1u);
            QCOMPARE(panel.findChild<QTableWidget *>("turboTable")->rowCount(),0);
            QVERIFY(!panel.findChild<QPushButton *>("turboApply")->isEnabled());
            QVERIFY(panel.findChild<QLabel *>("turboRaw")->text().isEmpty());
            QVERIFY(panel.findChild<QLabel *>("turboStatus")->text().contains(mode == 0 ? "No settings write" : "not verified"));
        }
    }
    void intelVfEditPreparationAndTargetChanges() {
        auto *device = new IntelVfEditFixture;
        auto access = std::make_shared<HardwareAccess>(std::unique_ptr<octool::core::HardwareBackend>(device));
        IntelVfPanel panel(access, nullptr, 130); panel.show();
        auto *prepare = panel.findChild<QPushButton *>("vfPrepare");
        auto *apply = panel.findChild<QPushButton *>("vfApply");
        auto *table = panel.findChild<QTableWidget *>("vfTable");
        auto *offset = panel.findChild<QLineEdit *>("vfOffset");
        auto *context = panel.findChild<QLabel *>("vfContext");
        prepare->click(); QCOMPARE(device->calls, 0u); QVERIFY(!apply->isEnabled());
        panel.findChild<QComboBox *>("vfPoint")->setCurrentIndex(8);
        prepare->click(); QTRY_VERIFY(panel.isEnabled());
        QCOMPARE(table->rowCount(), 1); QVERIFY(apply->isEnabled()); QCOMPARE(device->mutationCount, 0u);
        panel.findChild<QPushButton *>("vfCopy")->click();
        const auto copy = QApplication::clipboard()->text();
        QVERIFY(copy.contains("CPU 130 - Core - point 8") && copy.contains("Control:"));
        offset->setText("-50"); const unsigned calls = device->calls;
        panel.findChild<QComboBox *>("vfDomain")->setCurrentIndex(1);
        QVERIFY(offset->text().isEmpty() && context->text().isEmpty());
        QCOMPARE(table->rowCount(), 0); QVERIFY(!apply->isEnabled()); QCOMPARE(device->calls, calls);
        device->control |= 8; prepare->click(); QTRY_VERIFY(panel.isEnabled());
        QVERIFY(apply->isEnabled()); QCOMPARE(table->item(0, 3)->text(), hexValue(device->vfSettings[2][8], 4));
        const unsigned before = device->calls;
        panel.findChild<QLineEdit *>("vfCpu")->setText("-1"); prepare->click();
        QCOMPARE(device->calls, before); QCOMPARE(table->rowCount(), 0); QVERIFY(!apply->isEnabled());
    }
    void intelVfEditInvalidCancelApplyAndSameValue() {
        auto *device = new IntelVfEditFixture;
        auto access = std::make_shared<HardwareAccess>(std::unique_ptr<octool::core::HardwareBackend>(device));
        IntelVfPanel panel(access, nullptr, 130); panel.resize(1100, 720); panel.show();
        panel.findChild<QComboBox *>("vfPoint")->setCurrentIndex(8);
        panel.findChild<QPushButton *>("vfPrepare")->click(); QTRY_VERIFY(panel.isEnabled());
        auto *apply = panel.findChild<QPushButton *>("vfApply");
        auto *offset = panel.findChild<QLineEdit *>("vfOffset");
        auto *status = panel.findChild<QLabel *>("vfStatus");
        const auto previous = device->vfSettings[0][8]; const unsigned calls = device->calls;
        for (const auto &value : {"nan", "1000", "-1000.01", "1,5"}) {
            offset->setText(value); apply->click(); QCOMPARE(device->calls, calls);
        }
        offset->setText("-50");
        QTimer::singleShot(0, [] { auto *box = qobject_cast<QMessageBox *>(QApplication::activeModalWidget()); if (box) box->button(QMessageBox::Cancel)->click(); });
        apply->click(); QCOMPARE(device->calls, calls); QCOMPARE(device->mutationCount, 0u);
        QTimer::singleShot(0, [] { auto *box = qobject_cast<QMessageBox *>(QApplication::activeModalWidget()); if (box) box->button(QMessageBox::Yes)->click(); });
        apply->click(); QTRY_VERIFY(panel.isEnabled()); QCOMPARE(device->mutationCount, 1u);
        QCOMPARE(device->vfSettings[0][8] & 0x1fffffu, previous & 0x1fffffu);
        QCOMPARE(panel.findChild<QTableWidget *>("vfTable")->item(0, 2)->text(), QString("-49.8046875"));
        QVERIFY(offset->text().isEmpty()); QVERIFY(status->text().contains("Full point readback verified"));
        const QString capture = qEnvironmentVariable("OCTOOL_VF_EDIT_TEST_SCREENSHOT");
        if (!capture.isEmpty()) QVERIFY(panel.grab().save(capture));
        offset->setText("-50");
        QTimer::singleShot(0, [] { auto *box = qobject_cast<QMessageBox *>(QApplication::activeModalWidget()); if (box) box->button(QMessageBox::Yes)->click(); });
        apply->click(); QTRY_VERIFY(panel.isEnabled()); QCOMPARE(device->mutationCount, 1u);
        QVERIFY(offset->text().isEmpty()); QVERIFY(status->text().contains("no settings write performed"));
        QCOMPARE(device->wrongCpu, 0u);
    }
    void intelVfEditPermissionsAndFailedReadback() {
        auto *device = new IntelVfEditFixture;
        auto access = std::make_shared<HardwareAccess>(std::unique_ptr<octool::core::HardwareBackend>(device));
        IntelVfPanel panel(access, nullptr, 130); panel.show();
        panel.findChild<QComboBox *>("vfPoint")->setCurrentIndex(8);
        auto *prepare = panel.findChild<QPushButton *>("vfPrepare");
        auto *apply = panel.findChild<QPushButton *>("vfApply");
        auto *context = panel.findChild<QLabel *>("vfContext");
        auto *table = panel.findChild<QTableWidget *>("vfTable");
        auto *status = panel.findChild<QLabel *>("vfStatus");
        for (unsigned mode = 0; mode < 3; ++mode) {
            device->locked = mode == 0; device->control = mode == 1 ? 8 : 0;
            device->settings[0] = mode == 2 ? 0x100032 : 50;
            prepare->click(); QTRY_VERIFY(panel.isEnabled());
            QCOMPARE(table->rowCount(), 1); QVERIFY(!apply->isEnabled());
            QVERIFY(context->text().contains(mode == 0 ? "locked" : mode == 1 ? "Per-core override" : "not default"));
        }
        device->settings[0] = 50; prepare->click(); QTRY_VERIFY(panel.isEnabled()); QVERIFY(apply->isEnabled());
        device->discardChange = true; panel.findChild<QLineEdit *>("vfOffset")->setText("-50");
        QTimer::singleShot(0, [] { auto *box = qobject_cast<QMessageBox *>(QApplication::activeModalWidget()); if (box) box->button(QMessageBox::Yes)->click(); });
        apply->click(); QTRY_VERIFY(panel.isEnabled()); QCOMPARE(device->mutationCount, 1u);
        QCOMPARE(table->rowCount(), 0); QVERIFY(!apply->isEnabled()); QVERIFY(context->text().isEmpty());
        QVERIFY(panel.findChild<QLineEdit *>("vfOffset")->text().isEmpty()); QVERIFY(status->text().contains("not verified"));
        device->failAt = int(device->calls); prepare->click(); QTRY_VERIFY(panel.isEnabled());
        QCOMPARE(table->rowCount(), 0); QVERIFY(!apply->isEnabled()); QVERIFY(status->text().contains("Preparation failed"));
    }
    void intelVfEditStaleContextAndQueriesClearPreparation() {
        auto *device = new IntelVfEditFixture;
        auto access = std::make_shared<HardwareAccess>(std::unique_ptr<octool::core::HardwareBackend>(device));
        IntelVfPanel panel(access, nullptr, 130); panel.show();
        panel.findChild<QComboBox *>("vfPoint")->setCurrentIndex(8);
        auto *prepare = panel.findChild<QPushButton *>("vfPrepare");
        auto *apply = panel.findChild<QPushButton *>("vfApply");
        prepare->click(); QTRY_VERIFY(panel.isEnabled()); QVERIFY(apply->isEnabled());
        device->control ^= 1; panel.findChild<QLineEdit *>("vfOffset")->setText("-50");
        QTimer::singleShot(0, [] { auto *box = qobject_cast<QMessageBox *>(QApplication::activeModalWidget()); if (box) box->button(QMessageBox::Yes)->click(); });
        apply->click(); QTRY_VERIFY(panel.isEnabled()); QCOMPARE(device->mutationCount, 0u); QVERIFY(!apply->isEnabled());
        QVERIFY(panel.findChild<QLabel *>("vfStatus")->text().contains("No settings write was attempted"));
        prepare->click(); QTRY_VERIFY(panel.isEnabled()); QVERIFY(apply->isEnabled());
        panel.findChild<QPushButton *>("vfRead")->click(); QTRY_VERIFY(panel.isEnabled());
        QCOMPARE(panel.findChild<QTableWidget *>("vfTable")->rowCount(), 1); QVERIFY(!apply->isEnabled());
        QVERIFY(panel.findChild<QLabel *>("vfContext")->text().isEmpty()); QCOMPARE(device->mutationCount, 0u);
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
    void umcImportsCliContractAndRecomputesFieldsWithoutHardware() {
        QFile input(QFINDTESTDATA("fixtures/cli-umc.json")); QVERIFY(input.open(QIODevice::ReadOnly));
        const auto report = QJsonDocument::fromJson(input.readAll()).object();
        Reference reference; UmcPanel panel(reference.access());
        QVERIFY(panel.loadCapture(QJsonDocument(report).toJson()));
        QCOMPARE(reference.calls.load(), 0);
        auto *table = panel.findChild<QTableWidget *>("umcTable"); QCOMPARE(table->rowCount(), 212);
        QStringList expected;
        for (int row = 0; row < table->rowCount(); ++row) {
            QVERIFY(table->item(row, 7)->text() != "Missing register");
            expected << table->item(row, 6)->text();
        }
        auto changed = report; auto data = changed.value("data").toObject();
        data["fields"] = QJsonArray{QJsonObject{{"id", 0}, {"encoded", -12345}}}; changed["data"] = data;
        QVERIFY(panel.loadCapture(QJsonDocument(changed).toJson()));
        for (int row = 0; row < table->rowCount(); ++row) QCOMPARE(table->item(row, 6)->text(), expected.at(row));
        const auto saved = QJsonDocument::fromJson(panel.captureBytes()).object();
        QCOMPARE(saved.value("format").toString(), QString("octool-amd-umc-v1"));
        QCOMPARE(saved.value("bank").toInt(), 22); QCOMPARE(saved.value("refresh_slot").toInt(), 15);
        QCOMPARE(saved.value("cpu").toInt(), 130); QCOMPARE(saved.value("bus").toInt(), 2);
        QVERIFY(panel.loadCapture(panel.captureBytes())); QCOMPARE(reference.calls.load(), 0);
        QVERIFY(panel.findChild<QLabel *>("umcStatus")->text().contains("unverified"));
    }
    void umcRejectsFailedOrMalformedCliReports() {
        QFile input(QFINDTESTDATA("fixtures/cli-umc.json")); QVERIFY(input.open(QIODevice::ReadOnly));
        const auto report = QJsonDocument::fromJson(input.readAll()).object();
        Reference reference; UmcPanel panel(reference.access());
        for (unsigned kind = 0; kind < 14; ++kind) {
            QVERIFY(panel.loadCapture(QJsonDocument(report).toJson()));
            auto bad = report; auto data = bad.value("data").toObject(); auto regs = data.value("registers").toArray();
            auto reg = regs.at(0).toObject();
            switch (kind) {
            case 0: bad["ok"] = false; break;
            case 1: bad["error"] = -5; break;
            case 2: bad["schema_version"] = 2; break;
            case 3: bad["command"] = "intel-read"; break;
            case 4: regs.removeLast(); break;
            case 5: regs[1] = regs[0]; break;
            case 6: reg["raw"] = "0x100000000"; regs[0] = reg; break;
            case 7: reg["raw"] = 305419896; regs[0] = reg; break;
            case 8: reg["raw"] = "0x+0000001"; regs[0] = reg; break;
            case 9: reg["offset"] = "0xffffffff"; regs[0] = reg; break;
            case 10: data["refresh_slot"] = 16; break;
            case 11: bad["ok"] = "true"; break;
            case 12: data["bank"] = 0.5; break;
            case 13: data["pci"] = QJsonObject{{"domain", 1}, {"bus", 2}, {"device", 3}, {"function", 1}}; break;
            }
            data["registers"] = regs; bad["data"] = data;
            QVERIFY(!panel.loadCapture(QJsonDocument(bad).toJson()));
            QCOMPARE(panel.findChild<QTableWidget *>("umcTable")->rowCount(), 0);
            QVERIFY(panel.captureBytes().isEmpty()); QVERIFY(!panel.findChild<QPushButton *>("umcSave")->isEnabled());
        }
        QCOMPARE(reference.calls.load(), 0);
    }
    void umcComparisonSharesDecoderAndPreservesSnapshot() {
        Reference reference; UmcPanel panel(reference.access()); panel.resize(1440,700); panel.show();
        const QByteArray before=R"({"format":"octool-amd-umc-v1","bank":0,"refresh_slot":0,"registers":[{"offset":516,"value":34},{"offset":4624,"value":258048}]})";
        const auto after=QByteArray(before).replace("\"value\":34","\"value\":35");
        QVERIFY(panel.loadCapture(before)); const auto saved=panel.captureBytes();
        QVERIFY(panel.compareCapture(after)); QCOMPARE(reference.calls.load(),0); QCOMPARE(panel.captureBytes(),saved);
        auto *table=panel.findChild<QTableWidget *>("umcTable");
        auto *only=panel.findChild<QCheckBox *>("umcDifferences"); QVERIFY(only->isEnabled() && only->isChecked());
        for(int row=0;row<table->rowCount();++row) {
            const auto id=table->item(row,2)->text().toUInt();
            if(id==32) {
                QCOMPARE(table->item(row,6)->text(),QString("34")); QCOMPARE(table->item(row,9)->text(),QString("35"));
                QCOMPARE(table->item(row,10)->text(),QString("Value changed")); QVERIFY(!table->isRowHidden(row));
            }
            if(id==203) QVERIFY(table->isRowHidden(row));
        }
        // Setting the wrapped comparison summary posts a layout request. Wait
        // for its full height before capturing, as the interactive event loop does.
        auto *status=panel.findChild<QLabel *>("umcStatus");
        QTRY_VERIFY(status->height()>=status->heightForWidth(status->width()));
        QVERIFY(status->geometry().bottom()<=panel.rect().bottom());
        const auto screenshot=qEnvironmentVariable("OCTOOL_UMC_DIFF_TEST_SCREENSHOT");
        if(!screenshot.isEmpty()) QVERIFY(panel.grab().save(screenshot));
        only->setChecked(false);
        for(int row=0;row<table->rowCount();++row) QVERIFY(!table->isRowHidden(row));
        panel.findChild<QPushButton *>("umcClearComparison")->click();
        QVERIFY(table->isColumnHidden(8)); QVERIFY(!only->isEnabled()); QCOMPARE(panel.captureBytes(),saved);
        QVERIFY(panel.compareCapture(before));
        panel.findChild<QComboBox *>("umcBank")->setCurrentIndex(1);
        QCOMPARE(table->rowCount(),0); QVERIFY(panel.captureBytes().isEmpty());
        QVERIFY(!panel.findChild<QPushButton *>("umcCompare")->isEnabled()); QCOMPARE(reference.calls.load(),0);
    }
    void umcComparisonFailureClearsOnlyComparison() {
        Reference reference; UmcPanel panel(reference.access());
        const QByteArray bytes=R"({"format":"octool-amd-umc-v1","bank":0,"refresh_slot":0,"registers":[{"offset":516,"value":34}]})";
        QVERIFY(!panel.compareCapture(bytes));
        QVERIFY(panel.loadCapture(bytes));
        for(const auto &bad:QList<QByteArray>{"{}",QByteArray(bytes).replace("\"bank\":0","\"bank\":1"),
            QByteArray(bytes).replace("\"refresh_slot\":0","\"refresh_slot\":1"),
            QByteArray(bytes).replace("\"bank\":0","\"bank\":0,\"bank\":1")}) {
            QVERIFY(panel.compareCapture(bytes)); QVERIFY(!panel.compareCapture(bad));
            QCOMPARE(panel.captureBytes(),bytes); QCOMPARE(panel.findChild<QTableWidget *>("umcTable")->rowCount(),212);
            QVERIFY(panel.findChild<QTableWidget *>("umcTable")->isColumnHidden(10));
            QVERIFY(panel.findChild<QPushButton *>("umcSave")->isEnabled());
            QVERIFY(!panel.findChild<QPushButton *>("umcClearComparison")->isEnabled());
        }
        QCOMPARE(reference.calls.load(),0);
    }
    void umcComparisonAcceptsCliDecodeAndMissingFields() {
        QFile input(QFINDTESTDATA("fixtures/cli-umc.json")); QVERIFY(input.open(QIODevice::ReadOnly));
        auto report=QJsonDocument::fromJson(input.readAll()).object();
        Reference reference; UmcPanel panel(reference.access()); QVERIFY(panel.loadCapture(QJsonDocument(report).toJson()));
        auto data=report.value("data").toObject(); auto regs=data.value("registers").toArray(); regs.removeLast();
        data["registers"]=regs; report["data"]=data; report["command"]="amd-umc-decode";
        QVERIFY(panel.compareCapture(QJsonDocument(report).toJson()));
        auto *table=panel.findChild<QTableWidget *>("umcTable"); bool missing=false;
        for(int row=0;row<table->rowCount();++row) if(table->item(row,10)->text()=="Missing compared") {
            missing=true; QCOMPARE(table->item(row,9)->text(),QString("—")); QVERIFY(!table->isRowHidden(row));
        }
        QVERIFY(missing); QVERIFY(panel.loadCapture(QJsonDocument(report).toJson()));
        QVERIFY(panel.loadCapture(panel.captureBytes())); QCOMPARE(reference.calls.load(),0);
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
    void intelPowerReadbackShowsConfiguredValueAndSkipsUnchanged() {
        auto *device = new IntelControlsFixture; device->expectedCpu = 130;
        auto access = std::make_shared<HardwareAccess>(std::unique_ptr<octool::core::HardwareBackend>(device));
        IntelControlsPanel panel(access, nullptr, 130); panel.resize(1100, 760); panel.show();
        panel.findChild<QPushButton *>("intelRead")->click(); QTRY_VERIFY(panel.isEnabled());
        auto *value = panel.findChild<QLineEdit *>("intelValue");
        auto *apply = panel.findChild<QPushButton *>("intelApply");
        auto *status = panel.findChild<QLabel *>("intelStatus");
        auto *table = panel.findChild<QTableWidget *>("intelTable");
        value->setText("100.12");
        const auto count = device->requests.size();
        QTimer::singleShot(0, [] { auto *box = qobject_cast<QMessageBox *>(QApplication::activeModalWidget()); if (box) box->button(QMessageBox::Cancel)->click(); });
        apply->click(); QCOMPARE(device->requests.size(), count); QCOMPARE(device->writes, 0u);
        QTimer::singleShot(0, [] { auto *box = qobject_cast<QMessageBox *>(QApplication::activeModalWidget()); if (box) box->button(QMessageBox::Yes)->click(); });
        apply->click(); QTRY_VERIFY(panel.isEnabled());
        QCOMPARE(device->writes, 1u); QCOMPARE(device->wrongCpu, 0u);
        QCOMPARE(table->item(0, 1)->text(), QString("100")); QVERIFY(value->text().isEmpty());
        QVERIFY(status->text().contains("readback verified") && status->text().contains("configured value 100"));
        const auto capture = qEnvironmentVariable("OCTOOL_CONTROL_TEST_SCREENSHOT");
        if (!capture.isEmpty()) QVERIFY(panel.grab().save(capture));
        value->setText("100.12");
        QTimer::singleShot(0, [] { auto *box = qobject_cast<QMessageBox *>(QApplication::activeModalWidget()); if (box) box->button(QMessageBox::Yes)->click(); });
        apply->click(); QTRY_VERIFY(panel.isEnabled());
        QCOMPARE(device->writes, 1u); QVERIFY(status->text().contains("Already set; no write needed"));
        panel.findChild<QLineEdit *>("intelCpu")->setText("131"); QCOMPARE(table->rowCount(), 0);
    }
    void intelHwpReadbackFailureClearsEditableSnapshot() {
        auto *device = new IntelControlsFixture; device->expectedCpu = 130;
        auto access = std::make_shared<HardwareAccess>(std::unique_ptr<octool::core::HardwareBackend>(device));
        IntelControlsPanel panel(access, nullptr, 130); panel.show();
        panel.findChild<QPushButton *>("intelRead")->click(); QTRY_VERIFY(panel.isEnabled());
        auto *field = panel.findChild<QComboBox *>("intelField");
        field->setCurrentIndex(field->findData(int(octool::core::IntelField::HwpEpp)));
        auto *value = panel.findChild<QLineEdit *>("intelValue"); value->setText("192");
        auto *apply = panel.findChild<QPushButton *>("intelApply");
        QTimer::singleShot(0, [] { auto *box = qobject_cast<QMessageBox *>(QApplication::activeModalWidget()); if (box) box->button(QMessageBox::Yes)->click(); });
        apply->click(); QTRY_VERIFY(panel.isEnabled());
        QCOMPARE(device->writes, 1u);
        QVERIFY(panel.findChild<QLabel *>("intelStatus")->text().contains("configured value 192"));
        device->discardChange = true;
        field->setCurrentIndex(field->findData(int(octool::core::IntelField::HwpEpp))); value->setText("64");
        QTimer::singleShot(0, [] { auto *box = qobject_cast<QMessageBox *>(QApplication::activeModalWidget()); if (box) box->button(QMessageBox::Yes)->click(); });
        apply->click(); QTRY_VERIFY(panel.isEnabled());
        QCOMPARE(device->writes, 2u); QCOMPARE(device->wrongCpu, 0u);
        QCOMPARE(field->count(), 0); QCOMPARE(panel.findChild<QTableWidget *>("intelTable")->rowCount(), 0);
        QVERIFY(value->text().isEmpty());
        QVERIFY(panel.findChild<QLabel *>("intelStatus")->text().contains("readback was not verified"));
        value->setText("192"); apply->click(); QCOMPARE(device->writes, 2u);
    }
    void intelActivityWindowUsesMicrosecondsAndChecksAutonomousMode() {
        auto *device = new IntelControlsFixture; device->expectedCpu = 130;
        auto access = std::make_shared<HardwareAccess>(std::unique_ptr<octool::core::HardwareBackend>(device));
        IntelControlsPanel panel(access, nullptr, 130); panel.show();
        auto *read = panel.findChild<QPushButton *>("intelRead");
        auto *field = panel.findChild<QComboBox *>("intelField");
        auto *value = panel.findChild<QLineEdit *>("intelValue");
        auto *apply = panel.findChild<QPushButton *>("intelApply");
        const int window = int(octool::core::IntelField::HwpActivityWindow);
        read->click(); QTRY_VERIFY(panel.isEnabled());
        QVERIFY(field->findData(window) >= 0); field->setCurrentIndex(field->findData(window));
        value->setText("15333");
        QTimer::singleShot(0, [] { auto *box = qobject_cast<QMessageBox *>(QApplication::activeModalWidget()); if (box) box->button(QMessageBox::Yes)->click(); });
        apply->click(); QTRY_VERIFY(panel.isEnabled()); QCOMPARE(device->writes, 1u);
        QVERIFY(panel.findChild<QLabel *>("intelStatus")->text().contains("configured value 15000"));
        field->setCurrentIndex(field->findData(window)); value->setText("0");
        QTimer::singleShot(0, [] { auto *box = qobject_cast<QMessageBox *>(QApplication::activeModalWidget()); if (box) box->button(QMessageBox::Yes)->click(); });
        apply->click(); QTRY_VERIFY(panel.isEnabled()); QCOMPARE(device->writes, 2u);
        QVERIFY(panel.findChild<QLabel *>("intelStatus")->text().contains("configured value 0"));
        device->regs[0x774] |= UINT64_C(32) << 16;
        read->click(); QTRY_VERIFY(panel.isEnabled()); QCOMPARE(field->findData(window), -1);
        device->regs[0x774] &= ~(UINT64_C(255) << 16); device->activityWindow = false;
        read->click(); QTRY_VERIFY(panel.isEnabled()); QCOMPARE(field->findData(window), -1); QCOMPARE(device->writes, 2u);
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
    void spdXmpOfflineValuesAndSectionErrors() {
        QTemporaryDir dir; QVERIFY(dir.isValid()); const auto path = dir.filePath("profiles.spd");
        auto bytes = octool::test::spdFixture();
        const auto save = [&] { QFile file(path); if (!file.open(QIODevice::WriteOnly)) return false;
            return file.write(reinterpret_cast<const char *>(bytes.data()), qint64(bytes.size())) == qint64(bytes.size()); };
        QVERIFY(save()); MemoryBoardPanel panel; panel.resize(1100,760); panel.show();
        auto *table = panel.findChild<QTableWidget *>("inventoryTable"); QVERIFY(table); QCOMPARE(table->rowCount(),0);
        QVERIFY(panel.loadSpdFile(path));
        const auto row = [&](const QString &group, const QString &name) {
            for (int i = 0; i < table->rowCount(); ++i)
                if (table->item(i,0)->text() == group && table->item(i,1)->text() == name) return i;
            return -1;
        };
        const int voltage = row("XMP 1","VDD"), refresh = row("XMP 1","tRFC1 minimum");
        QVERIFY(voltage >= 0 && refresh >= 0);
        QCOMPARE(table->item(voltage,2)->text(),QString("1250")); QCOMPARE(table->item(voltage,3)->text(),QString("mV"));
        QCOMPARE(table->item(refresh,2)->text(),QString("295")); QCOMPARE(table->item(refresh,3)->text(),QString("ns"));
        const int header = row("XMP","Header"); QVERIFY(header >= 0);
        QTRY_VERIFY(panel.findChild<QLabel *>("inventoryStatus")->isVisible());
        QTest::qWait(50); // Let the initial table layout settle before scrolling.
        table->scrollToItem(table->item(header,0),QAbstractItemView::PositionAtTop);
        QTRY_VERIFY(table->visualItemRect(table->item(header,0)).top() >= 0 && table->visualItemRect(table->item(header,0)).top() < 20);
        const auto screenshot = qEnvironmentVariable("OCTOOL_SPD_TEST_SCREENSHOT");
        if (!screenshot.isEmpty()) { QTest::qWait(50); QVERIFY(panel.grab().save(screenshot)); }
        bytes[720] ^= 1; QVERIFY(save()); QVERIFY(panel.loadSpdFile(path));
        QCOMPARE(row("XMP 1","VDD"),-1); QVERIFY(row("XMP 2","VDD") >= 0);
        QVERIFY(table->item(row("XMP 1","Profile name"),4)->text().contains("CRC mismatch"));
        QCOMPARE(table->item(row("SPD","Captured bytes"),4)->text(),QString("Base CRC valid"));
        bytes = octool::test::spdFixture(); bytes[643] = 3;
        std::memcpy(bytes.data()+832,"EXPO",4); octool::test::spdSeal(bytes,640,62);
        QVERIFY(save()); QVERIFY(panel.loadSpdFile(path));
        QVERIFY(row("EXPO","Header") >= 0); QCOMPARE(row("XMP 3","VDD"),-1);
        bytes.resize(512); QVERIFY(save()); QVERIFY(panel.loadSpdFile(path));
        QCOMPARE(row("XMP 1","VDD"),-1);
        QCOMPARE(table->item(row("XMP","Header"),4)->text(),QString("Header not captured"));
    }
    void spdXmpSecondaryTimingsAndCycleLimits() {
        QTemporaryDir dir; QVERIFY(dir.isValid()); const auto path = dir.filePath("xmp-secondary.spd");
        auto bytes = octool::test::spdFixture();
        const auto save = [&] { QFile file(path); if (!file.open(QIODevice::WriteOnly)) return false;
            return file.write(reinterpret_cast<const char *>(bytes.data()), qint64(bytes.size())) == qint64(bytes.size()); };
        QVERIFY(save()); MemoryBoardPanel panel; panel.resize(1200,900); panel.show();
        auto *table = panel.findChild<QTableWidget *>("inventoryTable"); QVERIFY(table); QCOMPARE(table->rowCount(),0);
        QVERIFY(panel.loadSpdFile(path));
        const auto row = [&](const QString &group, const QString &name) {
            for (int i = 0; i < table->rowCount(); ++i)
                if (table->item(i,0)->text() == group && table->item(i,1)->text() == name) return i;
            return -1;
        };
        const QStringList names = {"tRRD_L", "tCCD_L_WR", "tCCD_L_WR2", "tCCD_L_WTR", "tCCD_S_WTR", "tCCD_L", "tRTP", "tFAW"};
        const unsigned times[] = {5000,20000,10000,10000,2500,5000,7500,10000};
        const unsigned limits[] = {8,32,16,16,4,8,12,32};
        for (unsigned slot = 0; slot < 3; ++slot) for (int t = 0; t < names.size(); ++t) {
            const auto group = QString("XMP %1").arg(slot+1);
            const int time = row(group,names[t]+" minimum"), limit = row(group,names[t]+" cycle lower limit");
            QVERIFY(time >= 0 && limit >= 0);
            QCOMPARE(table->item(time,2)->text(),QString::number(times[t]+17*slot));
            QCOMPARE(table->item(time,3)->text(),QString("ps"));
            QCOMPARE(table->item(limit,2)->text(),QString::number(limits[t]+slot));
            QCOMPARE(table->item(limit,3)->text(),QString("nCK"));
        }
        QTest::qWait(50);
        table->scrollToItem(table->item(row("XMP 1","tRRD_L minimum"),0),QAbstractItemView::PositionAtTop);
        QTRY_VERIFY(table->visualItemRect(table->item(row("XMP 1","tRRD_L minimum"),0)).top() < 20);
        const auto screenshot = qEnvironmentVariable("OCTOOL_XMP_SECONDARY_TEST_SCREENSHOT");
        if (!screenshot.isEmpty()) { QTest::qWait(50); QVERIFY(panel.grab().save(screenshot)); }
        std::fill(bytes.begin()+735,bytes.begin()+759,std::uint8_t(0)); octool::test::spdSeal(bytes,704,62);
        QVERIFY(save()); QVERIFY(panel.loadSpdFile(path));
        QCOMPARE(table->item(row("XMP 1","tRRD_L minimum"),2)->text(),QString("0"));
        QCOMPARE(table->item(row("XMP 1","tFAW cycle lower limit"),2)->text(),QString("0"));
        bytes[736] ^= 1; QVERIFY(save()); QVERIFY(panel.loadSpdFile(path));
        QCOMPARE(row("XMP 1","tRRD_L minimum"),-1); QVERIFY(row("XMP 2","tRRD_L minimum") >= 0);
        bytes[643] = 6; octool::test::spdSeal(bytes,640,62); QVERIFY(save()); QVERIFY(panel.loadSpdFile(path));
        QCOMPARE(row("XMP 1","tFAW cycle lower limit"),-1); QVERIFY(row("XMP 3","tFAW cycle lower limit") >= 0);
        bytes.resize(512); QVERIFY(save()); QVERIFY(panel.loadSpdFile(path));
        QCOMPARE(row("XMP 2","tRRD_L minimum"),-1);
    }
    void spdCasListsAndCommandRateStates() {
        QTemporaryDir dir; QVERIFY(dir.isValid()); const auto path = dir.filePath("cas.spd");
        auto bytes = octool::test::spdFixture();
        const auto save = [&] { QFile file(path); if (!file.open(QIODevice::WriteOnly)) return false;
            return file.write(reinterpret_cast<const char *>(bytes.data()), qint64(bytes.size())) == qint64(bytes.size()); };
        QVERIFY(save()); MemoryBoardPanel panel; panel.resize(1200,900); panel.show();
        auto *table = panel.findChild<QTableWidget *>("inventoryTable"); QVERIFY(table); QCOMPARE(table->rowCount(),0);
        const auto row = [&](const QString &group, const QString &name) {
            for (int i = 0; i < table->rowCount(); ++i)
                if (table->item(i,0)->text() == group && table->item(i,1)->text() == name) return i;
            return -1;
        };
        QVERIFY(panel.loadSpdFile(path));
        QCOMPARE(table->item(row("SPD","Supported CAS latencies"),2)->text(),QString("20, 34, 36, 98"));
        for (unsigned slot = 0; slot < 3; ++slot) {
            const QString group = QString("XMP %1").arg(slot+1);
            QCOMPARE(table->item(row(group,"Supported CAS latencies"),2)->text(),QString("%1, %2, %3").arg(22+2*slot).arg(40+2*slot).arg(98-2*slot));
            QCOMPARE(table->item(row(group,"Supported CAS latencies"),3)->text(),QString("nCK"));
            QCOMPARE(table->item(row(group,"Command rate"),2)->text(),QString("%1N").arg(slot+1));
        }
        QTest::qWait(50); table->scrollToItem(table->item(row("SPD","CAS bitmap"),0),QAbstractItemView::PositionAtTop);
        const auto screenshot = qEnvironmentVariable("OCTOOL_CAS_TEST_SCREENSHOT");
        if (!screenshot.isEmpty()) { QTest::qWait(50); QVERIFY(panel.grab().save(screenshot)); }
        std::fill(bytes.begin()+711,bytes.begin()+716,std::uint8_t(255)); bytes[764] = 0xa4; octool::test::spdSeal(bytes,704,62);
        QVERIFY(save()); QVERIFY(panel.loadSpdFile(path));
        QStringList all;
        for (unsigned part = 0; part < 7; ++part) {
            const auto name = part ? QString("Supported CAS latencies (%1)").arg(part+1) : "Supported CAS latencies";
            const int r = row("XMP 1",name); QVERIFY(r >= 0);
            const auto chunk = table->item(r,2)->text().split(", "); QVERIFY(chunk.size() <= 6); all += chunk;
        }
        QCOMPARE(all.size(),40); for (int i = 0; i < 40; ++i) QCOMPARE(all[i],QString::number(20+2*i));
        QCOMPARE(table->item(row("XMP 1","Command rate"),2)->text(),QString("Unknown code 4"));
        QCOMPARE(table->item(row("XMP 1","Command rate byte"),2)->text(),QString("0xa4"));
        std::fill(bytes.begin()+711,bytes.begin()+716,std::uint8_t(0)); bytes[764] = 0; octool::test::spdSeal(bytes,704,62);
        QVERIFY(save()); QVERIFY(panel.loadSpdFile(path));
        QCOMPARE(table->item(row("XMP 1","Supported CAS latencies"),2)->text(),QString("None encoded"));
        QCOMPARE(row("XMP 1","Supported CAS latencies (2)"),-1);
        QCOMPARE(table->item(row("XMP 1","Command rate"),2)->text(),QString("Not specified"));
        bytes[711] ^= 1; QVERIFY(save()); QVERIFY(panel.loadSpdFile(path));
        QCOMPARE(row("XMP 1","CAS bitmap"),-1); QCOMPARE(row("XMP 1","Command rate"),-1);
        QVERIFY(row("XMP 2","Command rate") >= 0);
        bytes = octool::test::spdFixture(); bytes[1] = 0x20; octool::test::spdSeal(bytes,0,510);
        QVERIFY(save()); QVERIFY(panel.loadSpdFile(path));
        QCOMPARE(table->item(row("SPD","Supported CAS latencies"),2)->text(),QString("Not decoded"));
        bytes = octool::test::spdDdr4XmpFixture(); bytes[23] |= 0xc0; octool::test::spdSeal(bytes,0,126);
        QVERIFY(save()); QVERIFY(panel.loadSpdFile(path));
        QCOMPARE(table->item(row("SPD","Supported CAS latencies"),2)->text(),QString("23, 32, 46, 52"));
        QVERIFY(table->item(row("SPD","Supported CAS latencies"),4)->text().contains("Reserved bit set"));
        QCOMPARE(table->item(row("XMP 1","Supported CAS latencies"),2)->text(),QString("None encoded")); QCOMPARE(row("XMP 1","Command rate"),-1);
    }
    void spdXmp2RecommendationsAndCas() {
        QTemporaryDir dir; QVERIFY(dir.isValid()); const auto path = dir.filePath("xmp2-metadata.spd");
        auto bytes = octool::test::spdProfileGeometryFixture(false);
        const auto save = [&] { QFile file(path); if (!file.open(QIODevice::WriteOnly)) return false;
            return file.write(reinterpret_cast<const char *>(bytes.data()),qint64(bytes.size())) == qint64(bytes.size()); };
        MemoryBoardPanel panel; panel.resize(1280,1050); panel.show();
        auto *table = panel.findChild<QTableWidget *>("inventoryTable"); QVERIFY(table);
        const auto row = [&](const QString &group, const QString &name) {
            for (int i = 0; i < table->rowCount(); ++i)
                if (table->item(i,0)->text() == group && table->item(i,1)->text() == name) return i;
            return -1;
        };
        const auto value = [&](const QString &group, const QString &name, int col = 2) {
            const int i = row(group,name); return i < 0 ? QString() : table->item(i,col)->text();
        };
        QVERIFY(save()); QVERIFY(panel.loadSpdFile(path));
        QCOMPARE(value("XMP 1","Recommended DIMMs per channel"),QString("2"));
        QCOMPARE(value("XMP 2","Recommended DIMMs per channel"),QString("4"));
        QVERIFY(value("XMP 1","Recommended DIMMs per channel",4).contains("installed count not measured"));
        QCOMPARE(value("XMP 1","Supported CAS latencies"),QString("9, 11, 12, 14, 15, 17"));
        QCOMPARE(value("XMP 2","Supported CAS latencies"),QString("7, 24"));
        QCOMPARE(value("XMP 2","CAS encoding"),QString("xmp2-cl7-24"));
        QVERIFY(value("XMP 2","Supported CAS latencies",4).contains("Reserved bit set"));
        QCOMPARE(row("XMP 1","Command rate"),-1);
        QTest::qWait(50); table->scrollToItem(table->item(row("XMP 1","Stored profile"),0),QAbstractItemView::PositionAtTop);
        const auto shot = qEnvironmentVariable("OCTOOL_XMP2_METADATA_TEST_SCREENSHOT");
        if (!shot.isEmpty()) { QTest::qWait(50); QVERIFY(panel.grab().save(shot)); }
        bytes[388] = 1; QVERIFY(save()); QVERIFY(panel.loadSpdFile(path));
        QCOMPARE(row("XMP 1","Supported CAS latencies"),-1); QCOMPARE(value("XMP 1","Recommended DIMMs per channel"),QString("2"));
        QCOMPARE(value("XMP 2","Supported CAS latencies"),QString("7, 24"));
        bytes[386] &= ~1u; QVERIFY(save()); QVERIFY(panel.loadSpdFile(path));
        QCOMPARE(row("XMP 1","Recommended DIMMs per channel"),-1);
        bytes = octool::test::spdFixture(); QVERIFY(save()); QVERIFY(panel.loadSpdFile(path));
        QCOMPARE(row("XMP 2","Recommended DIMMs per channel"),-1);
        QCOMPARE(value("XMP 1","CAS encoding"),QString("ddr5-cl20-98"));
    }
    void spdStackedGeometryAndRevisionChanges() {
        QTemporaryDir dir; QVERIFY(dir.isValid()); const auto path = dir.filePath("geometry.spd");
        auto bytes = octool::test::spdProfileGeometryFixture(true);
        const auto save = [&] { QFile file(path); if (!file.open(QIODevice::WriteOnly)) return false;
            return file.write(reinterpret_cast<const char *>(bytes.data()),qint64(bytes.size())) == qint64(bytes.size()); };
        MemoryBoardPanel panel; panel.resize(1280,1050); panel.show();
        auto *table = panel.findChild<QTableWidget *>("inventoryTable"); QVERIFY(table);
        const auto row = [&](const QString &name) {
            for (int i = 0; i < table->rowCount(); ++i)
                if (table->item(i,0)->text() == "SPD" && table->item(i,1)->text() == name) return i;
            return -1;
        };
        const auto value = [&](const QString &name, int col = 2) {
            const int i = row(name); return i < 0 ? QString() : table->item(i,col)->text();
        };
        QVERIFY(save()); QVERIFY(panel.loadSpdFile(path));
        QCOMPARE(value("Module capacity (MiB)"),QString("393216"));
        QCOMPARE(value("SDRAM package"),QString("4H 3DS"));
        QCOMPARE(value("Package die count"),QString("4"));
        QCOMPARE(value("Subchannels per module"),QString("4"));
        QCOMPARE(value("SPD tRFC1 different logical rank minimum (ns)"),QString("321"));
        QCOMPARE(value("SPD tRFC2 different logical rank minimum (ns)"),QString("234"));
        QCOMPARE(value("SPD tRFCsb different logical rank minimum (ns)"),QString("123"));
        QTest::qWait(50); table->scrollToTop();
        const auto shot = qEnvironmentVariable("OCTOOL_GEOMETRY_TEST_SCREENSHOT");
        if (!shot.isEmpty()) { QTest::qWait(50); QVERIFY(panel.grab().save(shot)); }
        bytes[192] = 0x20; octool::test::spdSeal(bytes,0,510); QVERIFY(save()); QVERIFY(panel.loadSpdFile(path));
        QCOMPARE(row("Module capacity (MiB)"),-1); QCOMPARE(row("Subchannels per module"),-1);
        QCOMPARE(value("SPD tRFC1 different logical rank minimum (ns)"),QString("321"));
        bytes[1] = 0x20; octool::test::spdSeal(bytes,0,510); QVERIFY(save()); QVERIFY(panel.loadSpdFile(path));
        QCOMPARE(row("SPD tRFC1 different logical rank minimum (ns)"),-1);
        bytes = octool::test::spdProfileGeometryFixture(true); bytes[235] = 0x82;
        octool::test::spdSeal(bytes,0,510); QVERIFY(save()); QVERIFY(panel.loadSpdFile(path));
        QCOMPARE(row("Subchannels per module"),-1); QCOMPARE(row("Module capacity (MiB)"),-1);
        bytes = octool::test::spdProfileGeometryFixture(true); bytes.resize(236); QVERIFY(save()); QVERIFY(panel.loadSpdFile(path));
        QCOMPARE(value("Module capacity (MiB)",4),QString("CRC not verified"));
        bytes = octool::test::spdProfileGeometryFixture(true); bytes[48] ^= 1; QVERIFY(save()); QVERIFY(panel.loadSpdFile(path));
        QCOMPARE(row("Module capacity (MiB)"),-1); QCOMPARE(row("SPD tRFC1 different logical rank minimum (ns)"),-1);
    }
    void spdAsymmetricGeometry() {
        QTemporaryDir dir; QVERIFY(dir.isValid()); const auto path = dir.filePath("mixed-ranks.spd");
        auto bytes = octool::test::spdAsymmetricFixture();
        const auto save = [&] { QFile file(path); if (!file.open(QIODevice::WriteOnly)) return false;
            return file.write(reinterpret_cast<const char *>(bytes.data()),qint64(bytes.size())) == qint64(bytes.size()); };
        MemoryBoardPanel panel; panel.resize(1350,1050); panel.show();
        auto *table = panel.findChild<QTableWidget *>("inventoryTable");
        const auto value = [&](const QString &name, int col = 2) {
            for (int i = 0; i < table->rowCount(); ++i)
                if (table->item(i,0)->text() == "SPD" && table->item(i,1)->text() == name) return table->item(i,col)->text();
            return QString();
        };
        QVERIFY(save()); QVERIFY(panel.loadSpdFile(path));
        QCOMPARE(value("Rank organization"),QString("Asymmetric"));
        QCOMPARE(value("Second SDRAM density per die (Gbit)"),QString("16"));
        QCOMPARE(value("Second SDRAM device width (bits)"),QString("16"));
        QCOMPARE(value("Second SDRAM package"),QString("Monolithic"));
        QCOMPARE(value("First geometry capacity (MiB)"),QString("196608"));
        QCOMPARE(value("Second geometry capacity (MiB)"),QString("8192"));
        QCOMPARE(value("Module capacity (MiB)"),QString("204800"));
        QTest::qWait(50); table->scrollToTop();
        const auto shot = qEnvironmentVariable("OCTOOL_ASYMMETRIC_TEST_SCREENSHOT"); if (!shot.isEmpty()) { QTest::qWait(50); QVERIFY(panel.grab().save(shot)); }
        QTableWidgetItem *start = nullptr, *total = nullptr;
        for (int i = 0; i < table->rowCount(); ++i) {
            if (table->item(i,1)->text() == "Package die count") start = table->item(i,0);
            if (table->item(i,1)->text() == "Module capacity (MiB)") total = table->item(i,0);
        }
        QVERIFY(start); QVERIFY(total); table->scrollToItem(start,QAbstractItemView::PositionAtTop); QTest::qWait(50);
        QVERIFY(table->viewport()->rect().contains(table->visualItemRect(total)));
        const auto sumShot = qEnvironmentVariable("OCTOOL_ASYMMETRIC_CAPACITY_TEST_SCREENSHOT"); if (!sumShot.isEmpty()) QVERIFY(panel.grab().save(sumShot));
        bytes[8] = 0; octool::test::spdSeal(bytes,0,510); QVERIFY(save()); QVERIFY(panel.loadSpdFile(path));
        QVERIFY(value("Module capacity (MiB)").isEmpty()); QVERIFY(value("Second SDRAM density per die (Gbit)").isEmpty());
        QVERIFY(value("Module capacity status").contains("Not decoded")); QCOMPARE(value("Second SDRAM density (encoded)"),QString("0"));
        bytes = octool::test::spdAsymmetricFixture(); bytes[234] &= ~0x40; bytes[8] = 255; bytes[10] = 255;
        octool::test::spdSeal(bytes,0,510); QVERIFY(save()); QVERIFY(panel.loadSpdFile(path));
        QCOMPARE(value("Module capacity (MiB)"),QString("294912")); QVERIFY(value("Second SDRAM package").isEmpty());
        QVERIFY(value("Second geometry capacity (MiB)").isEmpty()); QVERIFY(value("Module capacity status").isEmpty());
    }
    void spdAsymmetricCaptureAndIntegrity() {
        QTemporaryDir dir; QVERIFY(dir.isValid()); const auto path = dir.filePath("mixed-ranks.spd");
        MemoryBoardPanel panel; auto *table = panel.findChild<QTableWidget *>("inventoryTable");
        for (unsigned mode = 0; mode < 5; ++mode) {
            auto bytes = octool::test::spdAsymmetricFixture();
            if (mode == 0) bytes.resize(236);
            if (mode == 1 || mode == 2) { bytes[mode == 1 ? 192 : 1] = 0x20; octool::test::spdSeal(bytes,0,510); }
            if (mode == 3) bytes[8] ^= 1;
            if (mode == 4) bytes.resize(235);
            QFile file(path); QVERIFY(file.open(QIODevice::WriteOnly)); QCOMPARE(file.write(reinterpret_cast<const char *>(bytes.data()),qint64(bytes.size())),qint64(bytes.size())); file.close();
            QVERIFY(panel.loadSpdFile(path)); bool capacity = false;
            for (int i = 0; i < table->rowCount(); ++i) if (table->item(i,0)->text() == "SPD" && table->item(i,1)->text() == "Module capacity (MiB)") {
                capacity = true; QCOMPARE(table->item(i,2)->text(),QString("204800")); QCOMPARE(table->item(i,4)->text(),QString("CRC not verified"));
            }
            QCOMPARE(capacity,mode == 0);
        }
    }
    void spdDdr4AsymmetricGeometry() {
        QTemporaryDir dir; QVERIFY(dir.isValid()); const auto path = dir.filePath("ddr4-mixed.spd");
        auto bytes = octool::test::spdDdr4AsymmetricFixture();
        const auto save = [&] { QFile file(path); if (!file.open(QIODevice::WriteOnly)) return false;
            return file.write(reinterpret_cast<const char *>(bytes.data()),qint64(bytes.size())) == qint64(bytes.size()); };
        MemoryBoardPanel panel; panel.resize(1350,1050); panel.show();
        auto *table = panel.findChild<QTableWidget *>("inventoryTable"); QVERIFY(table);
        const auto row = [&](const QString &name) {
            for (int i = 0; i < table->rowCount(); ++i)
                if (table->item(i,0)->text() == "SPD" && table->item(i,1)->text() == name) return i;
            return -1;
        };
        const auto value = [&](const QString &name) { const int r = row(name); return r < 0 ? QString() : table->item(r,2)->text(); };
        QVERIFY(save()); QVERIFY(panel.loadSpdFile(path));
        QCOMPARE(value("Second SDRAM density per die (Mbit)"),QString("12288"));
        QCOMPARE(value("First geometry package ranks"),QString("1")); QCOMPARE(value("Second geometry package ranks"),QString("2"));
        QCOMPARE(value("First geometry capacity (MiB)"),QString("16384"));
        QCOMPARE(value("Second geometry capacity (MiB)"),QString("24576")); QCOMPARE(value("Module capacity (MiB)"),QString("40960"));
        QCOMPARE(value("Second SDRAM package"),QString("Multi-load stack"));
        QTest::qWait(50); const int start = row("SDRAM package"), total = row("Module capacity (MiB)"); QVERIFY(start >= 0 && total >= 0);
        table->scrollToItem(table->item(start,0),QAbstractItemView::PositionAtTop); QTest::qWait(50);
        QVERIFY(table->viewport()->rect().contains(table->visualItemRect(table->item(total,0))));
        const auto shot = qEnvironmentVariable("OCTOOL_DDR4_ASYMMETRIC_TEST_SCREENSHOT"); if (!shot.isEmpty()) QVERIFY(panel.grab().save(shot));
        bytes[6] = 0x91; bytes[10] = 4; octool::test::spdSeal(bytes,0,126); QVERIFY(save()); QVERIFY(panel.loadSpdFile(path));
        QCOMPARE(value("First geometry package ranks"),QString("2")); QCOMPARE(value("Second geometry package ranks"),QString("1"));
        QCOMPARE(value("Module capacity (MiB)"),QString("45056"));
        bytes[6] = 0xf2; bytes[10] = 0xe6; bytes[12] = 0x49; octool::test::spdSeal(bytes,0,126); QVERIFY(save()); QVERIFY(panel.loadSpdFile(path));
        QCOMPARE(value("First geometry capacity rank factor"),QString("8")); QCOMPARE(value("Second geometry capacity rank factor"),QString("7"));
        QCOMPARE(value("Module capacity (MiB)"),QString("217088"));
        bytes = octool::test::spdAsymmetricFixture(); QVERIFY(save()); QVERIFY(panel.loadSpdFile(path));
        QCOMPARE(value("Module capacity (MiB)"),QString("204800")); QVERIFY(value("SDRAM density ratio (encoded)").isEmpty());
        QVERIFY(value("First geometry package ranks").isEmpty());
    }
    void spdDdr4AsymmetricFailureClearsCapacity() {
        QTemporaryDir dir; QVERIFY(dir.isValid()); const auto path = dir.filePath("ddr4-mixed.spd");
        MemoryBoardPanel panel; auto *table = panel.findChild<QTableWidget *>("inventoryTable"); QVERIFY(table);
        const auto value = [&](const QString &name) {
            for (int i = 0; i < table->rowCount(); ++i)
                if (table->item(i,0)->text() == "SPD" && table->item(i,1)->text() == name) return table->item(i,2)->text();
            return QString();
        };
        const auto save = [&](const std::vector<std::uint8_t> &bytes) { QFile file(path); if (!file.open(QIODevice::WriteOnly)) return false;
            return file.write(reinterpret_cast<const char *>(bytes.data()),qint64(bytes.size())) == qint64(bytes.size()); };
        for (unsigned mode = 0; mode < 7; ++mode) {
            auto bytes = octool::test::spdDdr4AsymmetricFixture(); QVERIFY(save(bytes)); QVERIFY(panel.loadSpdFile(path));
            QCOMPARE(value("Module capacity (MiB)"),QString("40960"));
            if (mode == 0) bytes[10] = 0x9d;
            if (mode == 1) bytes[12] = 0x41;
            if (mode == 2) bytes[1] = 0x20;
            if (mode == 3) { bytes[10] = 0xe6; bytes[12] = 0x49; }
            octool::test::spdSeal(bytes,0,126);
            if (mode == 4) bytes[10] ^= 1;
            if (mode == 5 || mode == 6) bytes.resize(mode == 5 ? 127 : 128);
            QVERIFY(save(bytes)); QVERIFY(panel.loadSpdFile(path));
            QCOMPARE(value("Module capacity (MiB)"),mode == 6 ? QString("40960") : QString());
            if (mode < 2 || mode == 3) QVERIFY(value("Module capacity status").contains("Not decoded"));
            if (mode < 6) QVERIFY(value("Second geometry capacity (MiB)").isEmpty());
        }
    }
    void spdBaseTimingValuesAndRevisionChanges() {
        QTemporaryDir dir; QVERIFY(dir.isValid()); const auto path = dir.filePath("base-timings.spd");
        auto bytes = octool::test::spdBaseTimingsFixture(true);
        const auto save = [&] { QFile file(path); if (!file.open(QIODevice::WriteOnly)) return false;
            return file.write(reinterpret_cast<const char *>(bytes.data()),qint64(bytes.size())) == qint64(bytes.size()); };
        MemoryBoardPanel panel; panel.resize(1280,1050); panel.show();
        auto *table = panel.findChild<QTableWidget *>("inventoryTable"); QVERIFY(table);
        const auto row = [&](const QString &name) {
            for (int i = 0; i < table->rowCount(); ++i)
                if (table->item(i,0)->text() == "SPD" && table->item(i,1)->text() == name) return i;
            return -1;
        };
        const QStringList names = {"tRRD_L", "tCCD_L", "tCCD_L_WR", "tCCD_L_WR2", "tFAW",
            "tCCD_L_WTR", "tCCD_S_WTR", "tRTP", "tCCD_M", "tCCD_M_WR", "tCCD_M_WTR"};
        QVERIFY(save()); QVERIFY(panel.loadSpdFile(path));
        for (int i = 0; i < names.size(); ++i) {
            const int ps = row("SPD "+names[i]+" minimum (ps)"), ck = row("SPD "+names[i]+" cycle lower limit (nCK)");
            QVERIFY(ps >= 0 && ck >= 0);
            QCOMPARE(table->item(ps,2)->text(),QString::number(1200+179*i));
            QCOMPARE(table->item(ck,2)->text(),QString::number(3+5*i));
            QCOMPARE(table->item(ps,4)->text(),QString("Base CRC valid"));
        }
        QTest::qWait(50); table->scrollToItem(table->item(row("SPD tRRD_L minimum (ps)"),0),QAbstractItemView::PositionAtTop);
        const auto ddr5Shot = qEnvironmentVariable("OCTOOL_SPD_BASE_TEST_SCREENSHOT");
        if (!ddr5Shot.isEmpty()) { QTest::qWait(50); QVERIFY(panel.grab().save(ddr5Shot)); }
        bytes[1] = 0x11; octool::test::spdSeal(bytes,0,510); QVERIFY(save()); QVERIFY(panel.loadSpdFile(path));
        QCOMPARE(row("SPD tCCD_M minimum (ps)"),-1); QVERIFY(row("SPD tRTP minimum (ps)") >= 0);
        bytes[1] = 0x20; octool::test::spdSeal(bytes,0,510); QVERIFY(save()); QVERIFY(panel.loadSpdFile(path));
        QCOMPARE(row("SPD tRTP minimum (ps)"),-1);
        bytes = octool::test::spdBaseTimingsFixture(true);
        std::fill(bytes.begin()+70,bytes.begin()+103,std::uint8_t(0)); octool::test::spdSeal(bytes,0,510);
        QVERIFY(save()); QVERIFY(panel.loadSpdFile(path));
        QCOMPARE(table->item(row("SPD tCCD_M_WTR minimum (ps)"),2)->text(),QString("0"));
        QCOMPARE(table->item(row("SPD tCCD_M_WTR cycle lower limit (nCK)"),2)->text(),QString("0"));
        bytes[70] ^= 1; QVERIFY(save()); QVERIFY(panel.loadSpdFile(path));
        QCOMPARE(row("SPD tRRD_L minimum (ps)"),-1); QCOMPARE(row("SPD tCCD_M_WTR minimum (ps)"),-1);
        bytes = octool::test::spdBaseTimingsFixture(false); QVERIFY(save()); QVERIFY(panel.loadSpdFile(path));
        QCOMPARE(table->item(row("SPD tWR minimum (ps)"),2)->text(),QString("36375"));
        QCOMPARE(table->item(row("SPD tWTR_S minimum (ps)"),2)->text(),QString("70500"));
        QCOMPARE(table->item(row("SPD tWTR_L minimum (ps)"),2)->text(),QString("138750"));
        QCOMPARE(row("SPD tCCD_M minimum (ps)"),-1);
        QTest::qWait(50); table->scrollToItem(table->item(row("SPD tWR minimum (ps)"),0),QAbstractItemView::PositionAtTop);
        const auto ddr4Shot = qEnvironmentVariable("OCTOOL_DDR4_BASE_TEST_SCREENSHOT");
        if (!ddr4Shot.isEmpty()) { QTest::qWait(50); QVERIFY(panel.grab().save(ddr4Shot)); }
        bytes[17] = 1; octool::test::spdSeal(bytes,0,126); QVERIFY(save()); QVERIFY(panel.loadSpdFile(path));
        QCOMPARE(row("SPD tWR minimum (ps)"),-1); QCOMPARE(row("SPD tWTR_S minimum (ps)"),-1);
        bytes.resize(127); QVERIFY(save()); QVERIFY(panel.loadSpdFile(path));
        QCOMPARE(row("SPD tWTR_L minimum (ps)"),-1);
    }
    void spdUserSlotsAndUnknownEnablement() {
        QTemporaryDir dir; QVERIFY(dir.isValid()); const auto path = dir.filePath("user-slots.spd");
        auto bytes = octool::test::spdUserFixture();
        const auto save = [&] { QFile file(path); if (!file.open(QIODevice::WriteOnly)) return false;
            return file.write(reinterpret_cast<const char *>(bytes.data()),qint64(bytes.size())) == qint64(bytes.size()); };
        MemoryBoardPanel panel; panel.resize(1100,900); panel.show();
        auto *table = panel.findChild<QTableWidget *>("inventoryTable"); QVERIFY(table);
        const auto row = [&](const QString &group, const QString &name) {
            for (int i = 0; i < table->rowCount(); ++i)
                if (table->item(i,0)->text() == group && table->item(i,1)->text() == name) return i;
            return -1;
        };
        QVERIFY(save()); QVERIFY(panel.loadSpdFile(path));
        for (unsigned i = 0; i < 2; ++i) {
            const auto group = QString("XMP User %1").arg(i+1);
            const int time = row(group,"tCK minimum"), enabled = row(group,"Stored profile"); QVERIFY(time >= 0 && enabled >= 0);
            QCOMPARE(table->item(time,2)->text(),QString::number(400+25*i));
            QCOMPARE(table->item(time,4)->text(),QString("CRC OK; enable unknown"));
            QCOMPARE(table->item(enabled,2)->text(),QString("Enablement unknown"));
        }
        const int start = row("XMP User 1","Storage"); QVERIFY(start >= 0);
        QTest::qWait(50); table->scrollToItem(table->item(start,0),QAbstractItemView::PositionAtTop);
        const auto shot = qEnvironmentVariable("OCTOOL_USER_SPD_TEST_SCREENSHOT");
        if (!shot.isEmpty()) { QTest::qWait(50); QVERIFY(panel.grab().save(shot)); }
        octool::test::spdZeroCrcUserSlot(bytes); QVERIFY(save()); QVERIFY(panel.loadSpdFile(path));
        QVERIFY(row("XMP User 1","tCK minimum") >= 0);
        QCOMPARE(table->item(row("XMP User 1","CRC stored / computed"),2)->text(),QString("0000 / 0000"));
        bytes[958] = 1; QVERIFY(save()); QVERIFY(panel.loadSpdFile(path));
        QCOMPARE(row("XMP User 1","tCK minimum"),-1); QVERIFY(row("XMP User 2","tCK minimum") >= 0);
        bytes = octool::test::spdUserFixture(); bytes.resize(950); QVERIFY(save()); QVERIFY(panel.loadSpdFile(path));
        QCOMPARE(row("XMP User 1","tCK minimum"),-1); QCOMPARE(row("XMP User 2","tCK minimum"),-1);
        QCOMPARE(table->item(row("XMP User 1","Storage"),2)->text(),QString("Incomplete capture"));
        bytes = octool::test::spdUserFixture(true); QVERIFY(save()); QVERIFY(panel.loadSpdFile(path));
        QCOMPARE(row("XMP User 1","tCK minimum"),-1); QVERIFY(row("XMP User 2","tCK minimum") >= 0);
        QCOMPARE(table->item(row("XMP User 1","Storage"),2)->text(),QString("Occupied by EXPO"));
        bytes[660] ^= 1; QVERIFY(save()); QVERIFY(panel.loadSpdFile(path));
        QCOMPARE(row("XMP User 2","tCK minimum"),-1);
    }
    void spdStoredExpoEnhancedTimings() {
        QTemporaryDir dir; QVERIFY(dir.isValid()); const auto path = dir.filePath("enhanced.spd");
        auto bytes = octool::test::spdExpoFixture();
        for (unsigned i = 0; i < 16; ++i) octool::test::spdWord(bytes,866+40*(i/8)+2*(i%8),1000+17*i);
        octool::test::spdSeal(bytes,832,126);
        const auto save = [&] { QFile file(path); if (!file.open(QIODevice::WriteOnly)) return false;
            return file.write(reinterpret_cast<const char *>(bytes.data()),qint64(bytes.size())) == qint64(bytes.size()); };
        MemoryBoardPanel panel; panel.resize(1100,900); panel.show();
        auto *table = panel.findChild<QTableWidget *>("inventoryTable"); QVERIFY(table);
        const auto row = [&](const QString &group, const QString &name) {
            for (int i = 0; i < table->rowCount(); ++i)
                if (table->item(i,0)->text() == group && table->item(i,1)->text() == name) return i;
            return -1;
        };
        QVERIFY(save()); QVERIFY(panel.loadSpdFile(path));
        for (unsigned i = 0; i < 2; ++i) {
            const int time = row(QString("EXPO %1").arg(i+1),"Stored enhanced tRRD_L minimum"); QVERIFY(time >= 0);
            QCOMPARE(table->item(time,2)->text(),QString::number(1000+136*i)); QCOMPARE(table->item(time,3)->text(),QString("ps"));
            QCOMPARE(table->item(time,4)->text(),QString("CRC OK; enable unknown"));
        }
        const int start = row("EXPO 1","Stored enhanced tRRD_L minimum");
        QTest::qWait(50); table->scrollToItem(table->item(start,0),QAbstractItemView::PositionAtTop);
        const auto shot = qEnvironmentVariable("OCTOOL_ENHANCED_SPD_TEST_SCREENSHOT");
        if (!shot.isEmpty()) { QTest::qWait(50); QVERIFY(panel.grab().save(shot)); }
        bytes[837] = 0x10; octool::test::spdSeal(bytes,832,126); QVERIFY(save()); QVERIFY(panel.loadSpdFile(path));
        QCOMPARE(row("EXPO 1","Stored enhanced tRRD_L minimum"),-1); QVERIFY(row("EXPO 2","Stored enhanced tRRD_L minimum") >= 0);
        bytes[866] ^= 1; QVERIFY(save()); QVERIFY(panel.loadSpdFile(path));
        QCOMPARE(row("EXPO 2","Stored enhanced tRRD_L minimum"),-1);
    }
    void spdExpoProfilesAndIndependentFailures() {
        QTemporaryDir dir; QVERIFY(dir.isValid()); const auto path = dir.filePath("expo.spd");
        auto bytes = octool::test::spdExpoFixture();
        const auto save = [&] { QFile file(path); if (!file.open(QIODevice::WriteOnly)) return false;
            return file.write(reinterpret_cast<const char *>(bytes.data()),qint64(bytes.size())) == qint64(bytes.size()); };
        MemoryBoardPanel panel; panel.resize(1100,900); panel.show();
        auto *table = panel.findChild<QTableWidget *>("inventoryTable"); QVERIFY(table);
        const auto row = [&](const QString &group, const QString &name) {
            for (int i = 0; i < table->rowCount(); ++i)
                if (table->item(i,0)->text() == group && table->item(i,1)->text() == name) return i;
            return -1;
        };
        QVERIFY(save()); QVERIFY(panel.loadSpdFile(path));
        for (unsigned i = 0; i < 2; ++i) {
            const auto group = QString("EXPO %1").arg(i+1);
            const int voltage = row(group,"VDD"), time = row(group,"tCK minimum"), refresh = row(group,"tRFC1 minimum");
            QVERIFY(voltage >= 0 && time >= 0 && refresh >= 0);
            QCOMPARE(table->item(voltage,2)->text(),QString::number(1250-50*i)); QCOMPARE(table->item(voltage,3)->text(),QString("mV"));
            QCOMPARE(table->item(time,2)->text(),QString::number(333+24*i)); QCOMPARE(table->item(time,3)->text(),QString("ps"));
            QCOMPARE(table->item(refresh,2)->text(),QString("295")); QCOMPARE(table->item(refresh,3)->text(),QString("ns"));
        }
        const int header = row("EXPO","Header"); QVERIFY(header >= 0);
        QTest::qWait(50); table->scrollToItem(table->item(header,0),QAbstractItemView::PositionAtTop);
        QTRY_VERIFY(table->visualItemRect(table->item(header,0)).top() >= 0 && table->visualItemRect(table->item(header,0)).top() < 20);
        const auto screenshot = qEnvironmentVariable("OCTOOL_EXPO_TEST_SCREENSHOT");
        if (!screenshot.isEmpty()) { QTest::qWait(50); QVERIFY(panel.grab().save(screenshot)); }
        bytes[852] ^= 1; QVERIFY(save()); QVERIFY(panel.loadSpdFile(path));
        QCOMPARE(row("EXPO 1","VDD"),-1); QCOMPARE(row("EXPO 2","VDD"),-1); QVERIFY(row("XMP 1","VDD") >= 0);
        QCOMPARE(table->item(row("EXPO","Header"),4)->text(),QString("Block CRC mismatch"));
        bytes = octool::test::spdExpoFixture(); bytes[660] ^= 1; QVERIFY(save()); QVERIFY(panel.loadSpdFile(path));
        QCOMPARE(row("XMP 1","VDD"),-1); QVERIFY(row("EXPO 1","VDD") >= 0);
        bytes = octool::test::spdExpoFixture(); bytes[837] = 0x10; octool::test::spdSeal(bytes,832,126);
        QVERIFY(save()); QVERIFY(panel.loadSpdFile(path));
        QCOMPARE(row("EXPO 1","VDD"),-1); QVERIFY(row("EXPO 2","VDD") >= 0);
        QCOMPARE(table->item(row("EXPO 1","Stored profile"),4)->text(),QString("Disabled"));
        bytes.resize(959); QVERIFY(save()); QVERIFY(panel.loadSpdFile(path));
        QCOMPARE(row("EXPO 2","VDD"),-1); QCOMPARE(table->item(row("EXPO","Header"),4)->text(),QString("Block truncated"));
    }
    void spdDdr4XmpProfilesAndUnverifiedIntegrity() {
        QTemporaryDir dir; QVERIFY(dir.isValid()); const auto path = dir.filePath("ddr4-xmp.spd");
        auto bytes = octool::test::spdDdr4XmpFixture();
        const auto save = [&] { QFile file(path); if (!file.open(QIODevice::WriteOnly)) return false;
            return file.write(reinterpret_cast<const char *>(bytes.data()),qint64(bytes.size())) == qint64(bytes.size()); };
        MemoryBoardPanel panel; panel.resize(1100,900); panel.show();
        auto *table = panel.findChild<QTableWidget *>("inventoryTable"); QVERIFY(table);
        const auto row = [&](const QString &group, const QString &name) {
            for (int i = 0; i < table->rowCount(); ++i)
                if (table->item(i,0)->text() == group && table->item(i,1)->text() == name) return i;
            return -1;
        };
        QVERIFY(save()); QVERIFY(panel.loadSpdFile(path));
        const auto decoded = octool::core::decodeSpd(bytes);
        for (const auto &p : decoded.xmp.profiles) {
            const auto group = QString("XMP %1").arg(p.index);
            for (const auto &v : p.values) {
                const int r = row(group,QString::fromStdString(v.name)); QVERIFY(r >= 0);
                QCOMPARE(table->item(r,2)->text(),QString::number(v.value));
                QCOMPARE(table->item(r,3)->text(),QString::fromStdString(v.unit));
                QCOMPARE(table->item(r,4)->text(),QString("No XMP checksum provided"));
            }
            QCOMPARE(row(group,"Profile name"),-1); QCOMPARE(row(group,"CRC stored / computed"),-1);
        }
        QCOMPARE(table->item(row("XMP 1","tRC minimum"),2)->text(),QString("51975"));
        QCOMPARE(table->item(row("XMP 2","tCK minimum"),2)->text(),QString("667"));
        QCOMPARE(row("EXPO","Header"),-1);
        const int header = row("XMP","Header"); QVERIFY(header >= 0);
        QCOMPARE(table->item(header,2)->text(),QString("Revision 0x20"));
        QCOMPARE(table->item(header,4)->text(),QString("No XMP checksum provided"));
        QTest::qWait(50); table->scrollToItem(table->item(header,0),QAbstractItemView::PositionAtTop);
        QTRY_VERIFY(table->visualItemRect(table->item(header,0)).top() >= 0 && table->visualItemRect(table->item(header,0)).top() < 20);
        const auto screenshot = qEnvironmentVariable("OCTOOL_DDR4_XMP_TEST_SCREENSHOT");
        if (!screenshot.isEmpty()) { QTest::qWait(50); QVERIFY(panel.grab().save(screenshot)); }
        bytes[388] = 1; QVERIFY(save()); QVERIFY(panel.loadSpdFile(path));
        QCOMPARE(row("XMP 1","VDD"),-1); QVERIFY(row("XMP 2","VDD") >= 0);
        QCOMPARE(table->item(row("XMP 1","Timebase byte"),4)->text(),QString("Unsupported timebase"));
        bytes[386] = 2; QVERIFY(save()); QVERIFY(panel.loadSpdFile(path));
        QCOMPARE(table->item(row("XMP 1","Stored profile"),4)->text(),QString("Disabled"));
        bytes = octool::test::spdDdr4XmpFixture(); bytes.resize(486); QVERIFY(save()); QVERIFY(panel.loadSpdFile(path));
        QVERIFY(row("XMP 1","VDD") >= 0); QCOMPARE(row("XMP 2","VDD"),-1);
        bytes.resize(392); QVERIFY(save()); QVERIFY(panel.loadSpdFile(path));
        QCOMPARE(row("XMP 1","VDD"),-1); QCOMPARE(table->item(row("XMP","Header"),4)->text(),QString("Header truncated"));
    }
    void spdFileFailuresClearPriorRows() {
        QTemporaryDir dir; QVERIFY(dir.isValid()); const auto path = dir.filePath("profiles.spd");
        auto bytes = octool::test::spdFixture(); QFile file(path); QVERIFY(file.open(QIODevice::WriteOnly));
        QCOMPARE(file.write(reinterpret_cast<const char *>(bytes.data()),qint64(bytes.size())),qint64(bytes.size())); file.close();
        MemoryBoardPanel panel; QVERIFY(panel.loadSpdFile(path));
        auto *table = panel.findChild<QTableWidget *>("inventoryTable"); QVERIFY(table->rowCount() > 0);
        QVERIFY(!panel.loadSpdFile(dir.filePath("missing.spd"))); QCOMPARE(table->rowCount(),0);
        QVERIFY(panel.loadSpdFile(path)); QVERIFY(!panel.loadSpdFile(dir.path())); QCOMPARE(table->rowCount(),0);
        QVERIFY(file.open(QIODevice::WriteOnly)); QCOMPARE(file.write(QByteArray(4097,0)),qint64(4097)); file.close();
        QVERIFY(!panel.loadSpdFile(path)); QCOMPARE(table->rowCount(),0);
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
