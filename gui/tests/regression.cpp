// SPDX-License-Identifier: GPL-2.0-only
#include "registerpanel.h"
#include "pstates.h"
#include "pstates_electrical_fixture.h"
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
#include "intelenergypanel.h"
#include "intelavxpanel.h"
#include "intelbclkvoltagepanel.h"
#include "inteliccmaxpanel.h"
#include "intel_iccmax_fixture.h"
#include <QDoubleSpinBox>
#include "intel_avx_fixture.h"
#include "intel_bclk_voltage_fixture.h"
#include "intelthermalpanel.h"
#include "intellimitspanel.h"
#include "intel_limits_fixture.h"
#include "intel_thermal_fixture.h"
#include "intel_energy_fixture.h"
#include "intelmemorypanel.h"
#include "intel_memory_fixture.h"
#include "intelclockpanel.h"
#include "intel_clock_fixture.h"
#include "inteltvbpanel.h"
#include "intel_tvb_fixture.h"
#include "inteltvbtablepanel.h"
#include "intel_tvb_table_fixture.h"
#include "spd_fixture.h"
#include "amd_curve_fixture.h"
#include "amd_pbo_fixture.h"
#include "amdpmpanel.h"
#include "amd_pm_fixture.h"
#include "amd_pm_native_fixture.h"
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
#include <array>
#include <cmath>
#include <cstring>
#include <cerrno>
#include <sched.h>
#include <vector>
#include <chrono>
#include <thread>

struct PstateReference {
    bool amd = true, hardwarePstate = true, hypervisor = false;
    uint32_t signature = 0x00b00f81; // Synthetic family 1Ah/model 08h/stepping 1.
    uint32_t extendedMaximum = 0x80000007;
    uint32_t failLeaf = UINT32_MAX;
    uint32_t failRegister = 0;
    uint64_t limit = 0x20;
    std::array<uint64_t,8> definitions{{0x80000001000003e8ULL,0x00000000ffc001ccULL,0x800000000000000fULL}};
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
            case 1: values[0] = state.signature; values[2] = state.hypervisor ? UINT32_C(1) << 31 : 0; break;
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
        if (r.data0 == 0xc0010061) mailbox[1] = state.limit;
        else if (r.data0 >= 0xc0010064 && r.data0 <= 0xc001006b) mailbox[1] = state.definitions[size_t(r.data0-0xc0010064)];
        else return -EINVAL;
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
    void intelClockOfflineSnapshot() {
        using namespace octool::core;
        Reference reference; IntelClockPanel panel(reference.access(),nullptr,130); panel.resize(1200,850); panel.show();
        const auto snapshot = decodeIntelClock(intel_clock_fixture::registers());
        const auto bytes = QByteArray::fromStdString(intelClockJson(snapshot)); QVERIFY(panel.loadCapture(bytes));
        auto *table = panel.findChild<QTableWidget *>("clockTable"); auto *raw = panel.findChild<QTableWidget *>("clockRaw");
        QCOMPARE(table->rowCount(),8); QCOMPARE(raw->rowCount(),3);
        QCOMPARE(table->item(0,1)->text(),QString("101250")); QCOMPARE(table->item(0,2)->text(),QString("kHz"));
        QCOMPARE(table->item(1,1)->text(),QString("2")); QCOMPARE(table->item(2,1)->text(),QString("100"));
        QCOMPARE(table->item(3,1)->text(),QString("32")); QCOMPARE(table->item(4,1)->text(),QString("No"));
        QCOMPARE(table->item(5,1)->text(),QString("1150")); QCOMPARE(table->item(6,1)->text(),QString("9"));
        QCOMPARE(table->item(7,1)->text(),QString("Unavailable")); QCOMPARE(raw->item(2,2)->text(),QString("0x0000000000018B82"));
        panel.findChild<QPushButton *>("clockCopy")->click(); const auto copied = QApplication::clipboard()->text().toUtf8();
        QCOMPARE(copied,bytes); QVERIFY(parseIntelClockCapture(copied.toStdString()).valid);
        QVERIFY(panel.loadCapture("{\"schema_version\":1,\"command\":\"intel-clock-read\",\"ok\":true,\"error\":0,\"data\":{\"clock\":"+copied+"}}"));
        QCOMPARE(panel.captureBytes(),copied); QCOMPARE(reference.calls.load(),0);
        auto *status = panel.findChild<QLabel *>("clockStatus"); QVERIFY(status->text().contains("Offline"));
        QTRY_VERIFY(status->height() >= status->heightForWidth(status->width()));
        QTRY_VERIFY(table->viewport()->rect().contains(table->visualItemRect(table->item(7,1))));
        QTRY_VERIFY(raw->viewport()->rect().contains(raw->visualItemRect(raw->item(2,2))));
        const auto shot = qEnvironmentVariable("OCTOOL_INTEL_CLOCK_TEST_SCREENSHOT"); if (!shot.isEmpty()) QVERIFY(panel.grab().save(shot));
    }
    void intelClockPartialAndInvalidCapture() {
        using namespace octool::core;
        Reference reference; IntelClockPanel panel(reference.access()); auto *table = panel.findChild<QTableWidget *>("clockTable");
        const auto good = QByteArray::fromStdString(intelClockJson(decodeIntelClock(intel_clock_fixture::registers())));
        auto registers = intel_clock_fixture::registers(); registers[2].raw = UINT64_C(0x8000000000018b82); registers[1].raw |= 1u<<14;
        QVERIFY(panel.loadCapture(QByteArray::fromStdString(intelClockJson(decodeIntelClock(registers)))));
        for (int i = 0; i < 8; ++i) QCOMPARE(table->item(i,1)->text(),QString("Unavailable"));
        QCOMPARE(panel.findChild<QTableWidget *>("clockRaw")->item(2,2)->text(),QString("0x8000000000018B82"));
        registers[1].raw = 0; registers[2].raw = 100000;
        QVERIFY(panel.loadCapture(QByteArray::fromStdString(intelClockJson(decodeIntelClock(registers)))));
        QCOMPARE(table->item(4,1)->text(),QString("Yes")); QCOMPARE(table->item(2,1)->text(),QString("133.33"));
        for (const auto &bad : QList<QByteArray>{"{}",good+"x",good.left(good.size()-1),QByteArray(65537,' '),"{\"schema_version\":1,\"ok\":false}"}) {
            QVERIFY(panel.loadCapture(good)); QVERIFY(!panel.loadCapture(bad)); QCOMPARE(table->rowCount(),0);
            QCOMPARE(panel.findChild<QTableWidget *>("clockRaw")->rowCount(),0); QVERIFY(panel.captureBytes().isEmpty());
            QVERIFY(!panel.findChild<QPushButton *>("clockCopy")->isEnabled()); QVERIFY(!panel.findChild<QPushButton *>("clockSave")->isEnabled());
        }
        QCOMPARE(reference.calls.load(),0);
    }
    void intelClockLiveReadsAndFailures() {
        auto device = std::make_shared<intel_clock_fixture::Device>(); IntelClockPanel panel(intelMemoryAccess(device),nullptr,130); panel.show();
        auto *read = panel.findChild<QPushButton *>("clockRead"); auto *table = panel.findChild<QTableWidget *>("clockTable");
        for (int failure : {0,2,4,5,6,7,8,13,14,15,16,20}) {
            *device = intel_clock_fixture::Device(); read->click(); QTRY_VERIFY(read->isEnabled());
            QCOMPARE(table->rowCount(),8); QCOMPARE(device->reads,21u);
            QVERIFY(octool::core::parseIntelClockCapture(panel.captureBytes().toStdString()).valid);
            *device = intel_clock_fixture::Device(); device->failRead = failure;
            read->click(); QCOMPARE(table->rowCount(),0); QTRY_VERIFY(read->isEnabled());
            QCOMPARE(table->rowCount(),0); QCOMPARE(panel.findChild<QTableWidget *>("clockRaw")->rowCount(),0);
            QVERIFY(panel.captureBytes().isEmpty()); QCOMPARE(device->writes,0u);
        }
    }
    void intelClockCpuAndBusyGates() {
        auto device = std::make_shared<intel_clock_fixture::Device>(); IntelClockPanel panel(intelMemoryAccess(device),nullptr,130);
        auto *read = panel.findChild<QPushButton *>("clockRead"); auto *cpu = panel.findChild<QLineEdit *>("clockCpu");
        cpu->setText("not a CPU"); read->click(); QCOMPARE(device->cpuCalls,0u);
        cpu->setText("130"); device->model = 0xba; read->click(); QTRY_VERIFY(read->isEnabled()); QVERIFY(device->requests.empty());
        *device = intel_clock_fixture::Device(); device->clock[0x5e00] |= 1u<<31;
        read->click(); QTRY_VERIFY(read->isEnabled()); QCOMPARE(device->reads,6u); QVERIFY(panel.captureBytes().isEmpty());
        *device = intel_clock_fixture::Device(); read->click(); QTRY_VERIFY(read->isEnabled()); QVERIFY(!panel.captureBytes().isEmpty());
        cpu->setText("131"); QVERIFY(panel.captureBytes().isEmpty()); QCOMPARE(panel.findChild<QTableWidget *>("clockTable")->rowCount(),0);
    }
    void intelClockReadLifetimeAndTargetChange() {
        struct Gate { std::atomic<bool> entered{false}, release{false}; };
        for (bool close : {false,true}) {
            auto gate = std::make_shared<Gate>(); auto device = std::make_shared<intel_clock_fixture::Device>();
            device->afterRequest = [gate](const Request &) {
                if (gate->entered.exchange(true)) return;
                const auto deadline = std::chrono::steady_clock::now()+std::chrono::seconds(3);
                while (!gate->release && std::chrono::steady_clock::now()<deadline) std::this_thread::sleep_for(std::chrono::milliseconds(1));
            };
            std::weak_ptr<intel_clock_fixture::Device> alive = device;
            auto *panel = new IntelClockPanel(intelMemoryAccess(device),nullptr,130); device.reset();
            panel->findChild<QPushButton *>("clockRead")->click(); QTRY_VERIFY(gate->entered.load()); QVERIFY(!panel->loadCapture("{}"));
            if (close) { delete panel; QVERIFY(!alive.expired()); gate->release = true; QTRY_VERIFY(alive.expired()); }
            else {
                panel->findChild<QLineEdit *>("clockCpu")->setText("131"); gate->release = true;
                QTRY_VERIFY(panel->findChild<QPushButton *>("clockRead")->isEnabled());
                QCOMPARE(panel->findChild<QTableWidget *>("clockTable")->rowCount(),0); QVERIFY(panel->captureBytes().isEmpty());
                QVERIFY(panel->findChild<QLabel *>("clockStatus")->text().contains("discarded")); delete panel;
            }
        }
    }
    void intelMemoryOfflineSnapshot() {
        using namespace octool::core;
        Reference reference; IntelMemoryPanel panel(reference.access(),nullptr,130); panel.resize(1250,1100); panel.show();
        auto snapshot = decodeIntelMemory({},intel_memory_fixture::registers(1,2),2);
        const auto bytes = QByteArray::fromStdString(intelMemoryJson(snapshot)); QVERIFY(panel.loadCapture(bytes));
        auto *table = panel.findChild<QTableWidget *>("imcTable"); QCOMPARE(table->rowCount(),43);
        QCOMPARE(table->item(0,0)->text(),QString("tCL")); QCOMPARE(table->item(0,1)->text(),QString("36"));
        QCOMPARE(table->item(5,0)->text(),QString("tWRPRE")); QCOMPARE(table->item(5,1)->text(),QString("80"));
        QCOMPARE(table->item(7,1)->text(),QString("Not used")); QCOMPARE(table->item(13,1)->text(),QString("24000"));
        QCOMPARE(table->item(22,1)->text(),QString("70")); QCOMPARE(table->item(25,1)->text(),QString("10"));
        QCOMPARE(table->item(30,0)->text(),QString("RTL_Rank0")); QCOMPARE(table->item(30,1)->text(),QString("40"));
        QCOMPARE(table->item(30,2)->text(),QString("QCLK")); QCOMPARE(table->item(32,1)->text(),QString("Rank absent"));
        QCOMPARE(table->item(35,1)->text(),QString("9")); QCOMPARE(table->item(35,2)->text(),QString("MC DCLK"));
        QCOMPARE(table->item(37,1)->text(),QString("101")); QCOMPARE(table->item(41,1)->text(),QString("8"));
        QCOMPARE(table->item(42,1)->text(),QString("Not used"));
        panel.findChild<QPushButton *>("imcCopy")->click();
        const auto copied = QApplication::clipboard()->text().toUtf8(); QVERIFY(parseIntelMemoryCapture(copied.toStdString()).valid);
        QVERIFY(panel.loadCapture("{\"schema_version\":1,\"command\":\"intel-memory-decode\",\"ok\":true,\"error\":0,\"data\":{\"memory\":"+copied+"}}"));
        QCOMPARE(panel.captureBytes(),copied); QCOMPARE(reference.calls.load(),0);
        auto *status = panel.findChild<QLabel *>("imcStatus"); QVERIFY(status->text().contains("Offline"));
        QTRY_VERIFY(status->height() >= status->heightForWidth(status->width()));
        // Large system fonts can increase the minimum row height. Verify that
        // the normal viewport can reach the last row, then size the reference
        // image from the actual geometry instead of assuming 25 px rows.
        table->scrollToItem(table->item(42,1));
        QTRY_VERIFY(table->viewport()->rect().contains(table->visualItemRect(table->item(42,1))));
        table->scrollToTop(); QCoreApplication::processEvents();
        panel.resize(panel.width(),panel.height()+qMax(0,table->verticalHeader()->length()-table->viewport()->height())+8);
        QTRY_VERIFY(table->viewport()->rect().contains(table->visualItemRect(table->item(42,1))));
        const auto shot = qEnvironmentVariable("OCTOOL_INTEL_MEMORY_TEST_SCREENSHOT"); if (!shot.isEmpty()) QVERIFY(panel.grab().save(shot));
        auto v = intel_memory_fixture::registers(0); v[3].raw |= UINT64_C(1)<<30;
        QVERIFY(panel.loadCapture(QByteArray::fromStdString(intelMemoryJson(decodeIntelMemory({},v)))));
        QCOMPARE(table->item(7,1)->text(),QString("4")); QCOMPARE(table->item(11,1)->text(),QString("Not used"));
        QCOMPARE(table->item(0,2)->text(),QString("Encoded")); QVERIFY(status->text().contains("overrides"));
        QCOMPARE(table->item(30,2)->text(),QString("QCLK")); QCOMPARE(table->item(35,2)->text(),QString("MC DCLK"));
        QCOMPARE(table->item(39,1)->text(),QString("30")); QCOMPARE(table->item(42,1)->text(),QString("5"));
    }
    void intelMemoryLegacyVersionSwitch() {
        using namespace octool::core;
        Reference reference; IntelMemoryPanel panel(reference.access());
        auto *table = panel.findChild<QTableWidget *>("imcTable");
        const auto latest = QByteArray::fromStdString(intelMemoryJson(decodeIntelMemory({},intel_memory_fixture::registers())));
        const auto legacy = QByteArray::fromStdString(intelMemoryJson(decodeIntelMemory({},intel_memory_fixture::registers(0,1),1)));
        QVERIFY(panel.loadCapture(latest)); QCOMPARE(table->rowCount(),83);
        QVERIFY(panel.loadCapture(legacy)); QCOMPARE(table->rowCount(),30); QCOMPARE(panel.captureBytes(),legacy);
        QVERIFY(panel.findChild<QLabel *>("imcStatus")->text().contains("Legacy v1"));
        QCOMPARE(parseIntelMemoryCapture(panel.captureBytes().toStdString()).version,1u);
        const auto v2 = QByteArray::fromStdString(intelMemoryJson(decodeIntelMemory({},intel_memory_fixture::registers(1,2),2)));
        QVERIFY(panel.loadCapture(v2)); QCOMPARE(table->rowCount(),43); QCOMPARE(panel.captureBytes(),v2);
        QVERIFY(panel.findChild<QLabel *>("imcStatus")->text().contains("were not captured"));
        QVERIFY(panel.loadCapture(latest)); QCOMPARE(table->rowCount(),83);
        auto bad = legacy; bad.replace("memory-v1","memory-v2");
        QVERIFY(!panel.loadCapture(bad)); QCOMPARE(table->rowCount(),0); QVERIFY(panel.captureBytes().isEmpty());
        QCOMPARE(reference.calls.load(),0);
    }
    void intelMemoryDdrioSnapshot() {
        using namespace octool::core;
        Reference reference; IntelMemoryPanel panel(reference.access()); panel.resize(1450,1000); panel.show();
        const auto good = QByteArray::fromStdString(intelMemoryJson(decodeIntelMemory({},intel_memory_fixture::registers(1,4),4)));
        QVERIFY(panel.loadCapture(good));
        auto *table = panel.findChild<QTableWidget *>("imcTable"); QCOMPARE(table->rowCount(),68);
        for (int row = 57; row < 68; ++row) QCOMPARE(table->item(row,1)->text(),QString::number(intel_memory_fixture::expected(unsigned(row))));
        QCOMPARE(table->item(57,0)->text(),QString("DDRIO_self_refresh_enable"));
        QCOMPARE(table->item(61,2)->text(),QString("DCLK")); QCOMPARE(table->item(62,2)->text(),QString("MC DCLK"));
        QCOMPARE(table->item(63,2)->text(),QString("8*tCK")); QCOMPARE(table->item(65,2)->text(),QString("tCK"));
        QCOMPARE(table->item(66,2)->text(),QString("Encoded"));
        QVERIFY(panel.findChild<QLabel *>("imcContext")->text().contains("snapshot v4"));
        auto *status = panel.findChild<QLabel *>("imcStatus");
        QVERIFY(status->text().contains("do not report the current power state"));
        // Loading after show() invalidates the label's wrapped-height hint.
        // Let Qt complete the layout before capturing the whole status text.
        QTRY_VERIFY(status->height() >= status->heightForWidth(status->width()));
        table->scrollToBottom();
        QTRY_VERIFY(table->viewport()->rect().contains(table->visualItemRect(table->item(57,1))));
        QTRY_VERIFY(table->viewport()->rect().contains(table->visualItemRect(table->item(67,1))));
        const auto shot = qEnvironmentVariable("OCTOOL_INTEL_DDRIO_TEST_SCREENSHOT"); if (!shot.isEmpty()) QVERIFY(panel.grab().save(shot));
        panel.findChild<QPushButton *>("imcCopy")->click(); QCOMPARE(QApplication::clipboard()->text().toUtf8(),good);
        auto forged = good; forged.replace("\"encoded\":21","\"encoded\":99"); forged.replace("8*tCK","ns");
        QVERIFY(panel.loadCapture(forged)); QCOMPARE(panel.captureBytes(),good);
        auto v3 = QByteArray::fromStdString(intelMemoryJson(decodeIntelMemory({},intel_memory_fixture::registers(1,3),3)));
        QVERIFY(panel.loadCapture(v3)); QCOMPARE(table->rowCount(),57);
        QVERIFY(panel.findChild<QLabel *>("imcStatus")->text().contains("DDRIO low-power fields were not captured"));
        QCOMPARE(panel.captureBytes(),v3); QCOMPARE(reference.calls.load(),0);
    }
    void intelMemoryDdrioZerosAndOverrides() {
        using namespace octool::core;
        Reference reference; IntelMemoryPanel panel(reference.access());
        auto *table = panel.findChild<QTableWidget *>("imcTable");
        for (unsigned type = 0; type < 2; ++type) {
            auto regs = intel_memory_fixture::registers(type,4); regs[3].raw |= UINT64_C(1)<<16;
            QVERIFY(panel.loadCapture(QByteArray::fromStdString(intelMemoryJson(decodeIntelMemory({},regs,4)))));
            QCOMPARE(table->item(63,2)->text(),QString("Encoded")); QCOMPARE(table->item(65,2)->text(),QString("Encoded"));
            QCOMPARE(table->item(61,2)->text(),QString("DCLK")); QCOMPARE(table->item(62,2)->text(),QString("MC DCLK"));
            regs[18].raw = 0;
            QVERIFY(panel.loadCapture(QByteArray::fromStdString(intelMemoryJson(decodeIntelMemory({},regs,4)))));
            for (int row = 57; row < 68; ++row) QCOMPARE(table->item(row,1)->text(),QString("0"));
            auto bad = panel.captureBytes(); bad.replace("memory-v4","memory-v3");
            QVERIFY(!panel.loadCapture(bad)); QCOMPARE(table->rowCount(),0); QVERIFY(panel.captureBytes().isEmpty());
        }
        QCOMPARE(reference.calls.load(),0);
    }
    void intelMemoryOdtSnapshot() {
        using namespace octool::core;
        Reference reference; IntelMemoryPanel panel(reference.access()); panel.resize(1500,1060); panel.show();
        auto *table = panel.findChild<QTableWidget *>("imcTable");
        const char *masks[] = {"0x02","0x01","0x08","0x04","0x0E","0x0D","0x0B","0x07"};
        for (unsigned type = 0; type < 2; ++type) {
            auto regs = intel_memory_fixture::registers(type); regs[2].raw = 15;
            const auto good = QByteArray::fromStdString(intelMemoryJson(decodeIntelMemory({},regs)));
            QVERIFY(panel.loadCapture(good)); QCOMPARE(table->rowCount(),83);
            for (int i = 0; i < 8; ++i) {
                QCOMPARE(table->item(68+i,1)->text(),QString::fromLatin1(masks[i]));
                QCOMPARE(table->item(68+i,2)->text(),QString("ODT pin mask"));
            }
            QCOMPARE(table->item(68,0)->text(),QString("ODT_read_rank0_mask"));
            QCOMPARE(table->item(75,0)->text(),QString("ODT_write_rank3_mask"));
            QCOMPARE(table->item(76,1)->text(),QString("45")); QCOMPARE(table->item(76,2)->text(),QString("DCLK"));
            QCOMPARE(table->item(77,1)->text(),QString("13")); QCOMPARE(table->item(77,2)->text(),QString("DCLK"));
            QCOMPARE(table->item(78,1)->text(),QString("1")); QCOMPARE(table->item(78,2)->text(),QString("flag"));
            QVERIFY(panel.findChild<QLabel *>("imcContext")->text().contains("snapshot v6"));
            auto *status = panel.findChild<QLabel *>("imcStatus");
            QVERIFY(status->text().contains("matrix activation is unverified"));
            QVERIFY(status->text().contains("No effective tCWL or IOL is derived"));
            QTRY_VERIFY(status->height() >= status->heightForWidth(status->width()));
            table->scrollToBottom();
            QTRY_VERIFY(table->viewport()->rect().contains(table->visualItemRect(table->item(68,1))));
            QTRY_VERIFY(table->viewport()->rect().contains(table->visualItemRect(table->item(78,1))));
            const auto shot = qEnvironmentVariable(type ? "OCTOOL_INTEL_ODT_DDR5_TEST_SCREENSHOT" : "OCTOOL_INTEL_ODT_DDR4_TEST_SCREENSHOT");
            if (!shot.isEmpty()) QVERIFY(panel.grab().save(shot));
            panel.findChild<QPushButton *>("imcCopy")->click(); QCOMPARE(QApplication::clipboard()->text().toUtf8(),good);
            auto forged = good; forged.replace("\"encoded\":45","\"encoded\":99"); forged.replace("odt_pin_mask","ohms");
            QVERIFY(panel.loadCapture(forged)); QCOMPARE(panel.captureBytes(),good);
            QVERIFY(panel.loadCapture("{\"schema_version\":1,\"command\":\"intel-memory-read\",\"ok\":true,\"error\":0,\"data\":{\"memory\":"+good+"}}"));
            QCOMPARE(panel.captureBytes(),good);
        }
        QCOMPARE(reference.calls.load(),0);
    }
    void intelMemoryOdtRanksAndLegacySwitch() {
        using namespace octool::core;
        Reference reference; IntelMemoryPanel panel(reference.access()); auto *table = panel.findChild<QTableWidget *>("imcTable");
        for (unsigned type = 0; type < 2; ++type) for (unsigned ranks = 1; ranks < 16; ++ranks) {
            auto regs = intel_memory_fixture::registers(type); regs[2].raw = ranks;
            regs[3].raw |= (UINT64_C(1)<<30)|(UINT64_C(1)<<16); regs[19].raw = regs[20].raw = 0;
            QVERIFY(panel.loadCapture(QByteArray::fromStdString(intelMemoryJson(decodeIntelMemory({},regs)))));
            for (int i = 0; i < 8; ++i) {
                const bool present = (ranks & (1u<<(i%4))) != 0;
                QCOMPARE(table->item(68+i,1)->text(),QString(present ? "0x00" : "Rank absent"));
                QCOMPARE(table->item(68+i,2)->text(),QString(present ? "ODT pin mask" : "—"));
            }
            for (int i = 76; i < 79; ++i) QCOMPARE(table->item(i,1)->text(),QString("0"));
            QCOMPARE(table->item(76,2)->text(),QString("DCLK")); QCOMPARE(table->item(77,2)->text(),QString("DCLK"));
            QCOMPARE(table->item(78,2)->text(),QString("flag")); QCOMPARE(table->item(4,2)->text(),QString("Encoded"));
        }
        const auto latest = panel.captureBytes();
        for (unsigned version = 1; version < 5; ++version) {
            const auto old = QByteArray::fromStdString(intelMemoryJson(decodeIntelMemory({},intel_memory_fixture::registers(1,version),version)));
            QVERIFY(panel.loadCapture(old)); QCOMPARE(table->rowCount(),version == 1 ? 30 : version == 2 ? 43 : version == 3 ? 57 : 68);
            QCOMPARE(panel.captureBytes(),old);
            QVERIFY(panel.findChild<QLabel *>("imcStatus")->text().contains("ODT matrix and write-data delays were not captured"));
            QVERIFY(panel.loadCapture(latest)); QCOMPARE(table->rowCount(),83); QCOMPARE(panel.captureBytes(),latest);
        }
        auto bad = latest; bad.replace("memory-v6","memory-v4");
        QVERIFY(!panel.loadCapture(bad)); QCOMPARE(table->rowCount(),0); QVERIFY(panel.captureBytes().isEmpty());
        QVERIFY(!panel.findChild<QPushButton *>("imcCopy")->isEnabled()); QVERIFY(!panel.findChild<QPushButton *>("imcSave")->isEnabled());
        QCOMPARE(reference.calls.load(),0);
    }
    void intelMemoryOdtLiveCoherency() {
        auto device = std::make_shared<intel_memory_fixture::Device>();
        IntelMemoryPanel panel(intelMemoryAccess(device),nullptr,130); panel.show();
        auto *read = panel.findChild<QPushButton *>("imcRead"); auto *table = panel.findChild<QTableWidget *>("imcTable");
        for (unsigned offset : {0xe080u,0xe478u}) {
            *device = intel_memory_fixture::Device(); read->click(); QTRY_VERIFY(read->isEnabled());
            QCOMPARE(device->reads,59u); QCOMPARE(table->rowCount(),83); QVERIFY(!panel.captureBytes().isEmpty());
            *device = intel_memory_fixture::Device();
            const auto borrowed = device.get();
            device->afterRequest = [borrowed,offset](const Request &) { if (borrowed->reads == 26) borrowed->memory[offset] ^= 1; };
            read->click(); QCOMPARE(table->rowCount(),0); QTRY_VERIFY(read->isEnabled());
            QCOMPARE(table->rowCount(),0); QVERIFY(panel.captureBytes().isEmpty()); QCOMPARE(device->writes,0u);
            QVERIFY(!panel.findChild<QPushButton *>("imcCopy")->isEnabled()); QVERIFY(!panel.findChild<QPushButton *>("imcSave")->isEnabled());
            device->afterRequest = {};
        }
    }
    void intelMemoryTemperatureSnapshot() {
        using namespace octool::core;
        Reference reference; IntelMemoryPanel panel(reference.access()); panel.resize(1550,1060); panel.show();
        auto regs = intel_memory_fixture::registers(0); regs[2].raw = 15;
        const auto good = QByteArray::fromStdString(intelMemoryJson(decodeIntelMemory({},regs)));
        QVERIFY(panel.loadCapture(good)); auto *table = panel.findChild<QTableWidget *>("imcTable"); QCOMPARE(table->rowCount(),83);
        const char *expected[] = {"Cold (<45 C) / 1x refresh required","Normal (45-85 C) / 1x refresh required","Hot (>85 C) / 2x refresh required","Reserved code"};
        for (int rank = 0; rank < 4; ++rank) {
            QCOMPARE(table->item(79+rank,0)->text(),QString("DDR4_temperature_rank%1").arg(rank));
            QCOMPARE(table->item(79+rank,1)->text(),QString::fromLatin1(expected[rank]));
            QCOMPARE(table->item(79+rank,2)->text(),QString(rank == 3 ? "—" : "Stored category"));
            QCOMPARE(table->item(79+rank,3)->text(),QString("0x0000E428"));
        }
        auto *status = panel.findChild<QLabel *>("imcStatus"); QVERIFY(status->text().contains("freshness is unverified"));
        QVERIFY(status->text().contains("do not report the active rate"));
        QTRY_VERIFY(status->height() >= status->heightForWidth(status->width()));
        table->scrollToBottom();
        QTRY_VERIFY(table->viewport()->rect().contains(table->visualItemRect(table->item(79,1))));
        QTRY_VERIFY(table->viewport()->rect().contains(table->visualItemRect(table->item(82,5))));
        const auto shot = qEnvironmentVariable("OCTOOL_INTEL_MEMORY_TEMPERATURE_TEST_SCREENSHOT");
        if (!shot.isEmpty()) QVERIFY(panel.grab().save(shot));
        panel.findChild<QPushButton *>("imcCopy")->click(); QCOMPARE(QApplication::clipboard()->text().toUtf8(),good);
        auto forged = good; forged.replace("\"temperature_class\":\"hot\"","\"temperature_class\":\"cold\"");
        forged.replace("\"required_refresh_multiplier\":2","\"required_refresh_multiplier\":1");
        forged.replace("\"temperature_freshness_verified\":false","\"temperature_freshness_verified\":true");
        QVERIFY(panel.loadCapture(forged)); QCOMPARE(panel.captureBytes(),good);
        QVERIFY(panel.loadCapture("{\"schema_version\":1,\"command\":\"intel-memory-read\",\"ok\":true,\"error\":0,\"data\":{\"memory\":"+good+"}}"));
        QCOMPARE(panel.captureBytes(),good); QCOMPARE(reference.calls.load(),0);
    }
    void intelMemoryTemperaturePresenceAndLegacy() {
        using namespace octool::core;
        Reference reference; IntelMemoryPanel panel(reference.access()); auto *table = panel.findChild<QTableWidget *>("imcTable");
        for (unsigned type = 0; type < 2; ++type) for (unsigned ranks = 1; ranks < 16; ++ranks) {
            auto regs = intel_memory_fixture::registers(type); regs[2].raw = ranks; regs[21].raw = 0;
            regs[3].raw |= (UINT64_C(1)<<30)|(UINT64_C(1)<<16);
            QVERIFY(panel.loadCapture(QByteArray::fromStdString(intelMemoryJson(decodeIntelMemory({},regs)))));
            for (unsigned rank = 0; rank < 4; ++rank) {
                const auto expected = type ? "Not used" : !(ranks&(1u<<rank)) ? "Rank absent" : "Cold (<45 C) / 1x refresh required";
                QCOMPARE(table->item(int(79+rank),1)->text(),QString::fromLatin1(expected));
            }
        }
        const auto latest = panel.captureBytes();
        for (unsigned version = 1; version < 6; ++version) {
            const auto old = QByteArray::fromStdString(intelMemoryJson(decodeIntelMemory({},intel_memory_fixture::registers(0,version),version)));
            QVERIFY(panel.loadCapture(old)); QCOMPARE(table->rowCount(),version == 1 ? 30 : version == 2 ? 43 : version == 3 ? 57 : version == 4 ? 68 : 79);
            QVERIFY(panel.findChild<QLabel *>("imcStatus")->text().contains("DDR4 temperature categories were not captured"));
            QCOMPARE(panel.captureBytes(),old); QVERIFY(panel.loadCapture(latest)); QCOMPARE(table->rowCount(),83);
        }
        QCOMPARE(reference.calls.load(),0);
    }
    void intelMemoryTemperatureLiveClearsFailures() {
        auto device = std::make_shared<intel_memory_fixture::Device>();
        IntelMemoryPanel panel(intelMemoryAccess(device),nullptr,130); panel.resize(1550,1060); panel.show();
        auto *read = panel.findChild<QPushButton *>("imcRead"); auto *table = panel.findChild<QTableWidget *>("imcTable");
        for (unsigned scenario = 0; scenario < 4; ++scenario) {
            *device = intel_memory_fixture::Device(); device->memory[0xd800] = 0;
            read->click(); QTRY_VERIFY(read->isEnabled()); QCOMPARE(device->reads,59u); QCOMPARE(table->rowCount(),83);
            *device = intel_memory_fixture::Device(); device->memory[0xd800] = 0;
            if (scenario < 2) device->failRead = scenario ? 53 : 26;
            if (scenario == 2) {
                const auto borrowed = device.get();
                device->afterRequest = [borrowed](const Request &) { if (borrowed->reads == 27) borrowed->memory[0xe428] ^= 1; };
            }
            if (scenario == 3) device->memory[0xe428] = UINT32_MAX;
            read->click(); QCOMPARE(table->rowCount(),0); QTRY_VERIFY(read->isEnabled());
            QCOMPARE(table->rowCount(),0); QVERIFY(panel.captureBytes().isEmpty()); QCOMPARE(device->writes,0u);
            QVERIFY(!panel.findChild<QPushButton *>("imcCopy")->isEnabled()); QVERIFY(!panel.findChild<QPushButton *>("imcSave")->isEnabled());
            QVERIFY(panel.findChild<QLabel *>("imcStatus")->text().contains("Read failed"));
            device->afterRequest = {};
        }
        const auto shot = qEnvironmentVariable("OCTOOL_INTEL_MEMORY_TEMPERATURE_ERROR_TEST_SCREENSHOT");
        if (!shot.isEmpty()) QVERIFY(panel.grab().save(shot));
    }
    void intelMemoryRefreshSnapshot() {
        using namespace octool::core;
        Reference reference; IntelMemoryPanel panel(reference.access()); panel.resize(1350,960); panel.show();
        const auto good = QByteArray::fromStdString(intelMemoryJson(decodeIntelMemory({},intel_memory_fixture::registers(1,3),3)));
        QVERIFY(panel.loadCapture(good));
        auto *table = panel.findChild<QTableWidget *>("imcTable"); QCOMPARE(table->rowCount(),57);
        QCOMPARE(table->item(43,0)->text(),QString("tREFIx9")); QCOMPARE(table->item(43,1)->text(),QString("66"));
        QCOMPARE(table->item(43,2)->text(),QString("1024 DCLK"));
        QCOMPARE(table->item(44,1)->text(),QString("6")); QCOMPARE(table->item(44,2)->text(),QString("MC DCLK"));
        QCOMPARE(table->item(47,1)->text(),QString("0")); QCOMPARE(table->item(47,2)->text(),QString("flag"));
        QCOMPARE(table->item(51,1)->text(),QString("683")); QCOMPARE(table->item(51,2)->text(),QString("Encoded"));
        QCOMPARE(table->item(52,1)->text(),QString("1110")); QCOMPARE(table->item(52,2)->text(),QString("2*tCK"));
        QCOMPARE(table->item(53,1)->text(),QString("Not used")); QCOMPARE(table->item(54,1)->text(),QString("293"));
        QCOMPARE(table->item(55,1)->text(),QString("Not used")); QCOMPARE(table->item(56,1)->text(),QString("41"));
        QCOMPARE(table->item(56,2)->text(),QString("Encoded"));
        QVERIFY(panel.findChild<QLabel *>("imcContext")->text().contains("snapshot v3"));
        auto *status = panel.findChild<QLabel *>("imcStatus");
        QTRY_VERIFY(status->height() >= status->heightForWidth(status->width()));
        table->scrollToBottom();
        QTRY_VERIFY(table->viewport()->rect().contains(table->visualItemRect(table->item(43,1))));
        QTRY_VERIFY(table->viewport()->rect().contains(table->visualItemRect(table->item(56,1))));
        const auto shot = qEnvironmentVariable("OCTOOL_INTEL_REFRESH_TEST_SCREENSHOT"); if (!shot.isEmpty()) QVERIFY(panel.grab().save(shot));
        panel.findChild<QPushButton *>("imcCopy")->click(); QCOMPARE(QApplication::clipboard()->text().toUtf8(),good);
        auto forged = good; forged.replace("\"encoded\":66","\"encoded\":99"); forged.replace("1024_DCLK","ns");
        QVERIFY(panel.loadCapture(forged)); QCOMPARE(table->item(43,1)->text(),QString("66"));
        QCOMPARE(table->item(43,2)->text(),QString("1024 DCLK")); QCOMPARE(panel.captureBytes(),good);
        QCOMPARE(reference.calls.load(),0);
    }
    void intelMemoryRefreshAliasesAndZero() {
        using namespace octool::core;
        Reference reference; IntelMemoryPanel panel(reference.access());
        auto *table = panel.findChild<QTableWidget *>("imcTable");
        for (unsigned type = 0; type < 2; ++type) {
            auto regs = intel_memory_fixture::registers(type,3); regs[3].raw |= UINT64_C(1)<<16;
            QVERIFY(panel.loadCapture(QByteArray::fromStdString(intelMemoryJson(decodeIntelMemory({},regs,3)))));
            QCOMPARE(table->item(52,2)->text(),QString(type ? "Encoded" : "—"));
            QCOMPARE(table->item(type ? 54 : 53,1)->text(),QString("293"));
            QCOMPARE(table->item(type ? 54 : 53,2)->text(),QString("Encoded"));
            QCOMPARE(table->item(type ? 56 : 55,1)->text(),QString("41"));
            QCOMPARE(table->item(44,2)->text(),QString("MC DCLK"));
            for (unsigned i = 14; i < 18; ++i) regs[i].raw = 0;
            QVERIFY(panel.loadCapture(QByteArray::fromStdString(intelMemoryJson(decodeIntelMemory({},regs,3)))));
            for (int row : {43,44,45,46,47,48,49,50,51}) QCOMPARE(table->item(row,1)->text(),QString("0"));
            QCOMPARE(table->item(type ? 54 : 53,1)->text(),QString("0"));
            QCOMPARE(table->item(type ? 56 : 55,1)->text(),QString("0"));
            auto bad = panel.captureBytes(); bad.replace("memory-v3","memory-v2");
            QVERIFY(!panel.loadCapture(bad)); QCOMPARE(table->rowCount(),0); QVERIFY(panel.captureBytes().isEmpty());
        }
        QCOMPARE(reference.calls.load(),0);
    }
    void intelMemoryRankMaskAndZeroTiming() {
        using namespace octool::core;
        Reference reference; IntelMemoryPanel panel(reference.access());
        auto registers = intel_memory_fixture::registers(0);
        registers[2].raw = 8; registers[12].raw = UINT64_C(0xffffffc034000000);
        registers[3].raw &= ~(UINT64_C(31)<<56); // tCPDED=0 is valid.
        const auto s = decodeIntelMemory({},registers); QVERIFY(s.valid);
        QVERIFY(panel.loadCapture(QByteArray::fromStdString(intelMemoryJson(s))));
        auto *table = panel.findChild<QTableWidget *>("imcTable");
        for (int i = 30; i < 33; ++i) { QCOMPARE(table->item(i,1)->text(),QString("Rank absent")); QCOMPARE(table->item(i,2)->text(),QString("—")); }
        QCOMPARE(table->item(33,1)->text(),QString("52")); QCOMPARE(table->item(33,2)->text(),QString("QCLK"));
        QCOMPARE(table->item(42,1)->text(),QString("0")); QCOMPARE(table->item(42,2)->text(),QString("tCK"));
        const auto copied = parseIntelMemoryCapture(panel.captureBytes().toStdString());
        QVERIFY(copied.valid); QCOMPARE(copied.rankMask,8u); QCOMPARE(copied.values[42].encoded,0u);
        QCOMPARE(reference.calls.load(),0);
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
        for (int failure : {0,5,6,7,8,11,17,18,19,20,21,22,23,24,25,26,36,37,42,43,44,45,46,47,48,49,50,51,52,56}) {
            *device = intel_memory_fixture::Device(); read->click(); QTRY_VERIFY(read->isEnabled());
            QCOMPARE(table->rowCount(),83); QVERIFY(panel.findChild<QPushButton *>("imcCopy")->isEnabled());
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

    void pmNativeRoundtripAndPartialFields() {
        using namespace octool::core;
        for (bool partial : {false,true}) {
            auto *device = new AmdPmNativeFixture;
            if (partial) pm_fixture::put(device->bytes,0x20c,0x7f800001);
            auto access = std::make_shared<HardwareAccess>(std::unique_ptr<HardwareBackend>(device));
            AmdPmPanel panel(nullptr,{},access,130); panel.resize(1250,1000); panel.show();
            auto *read = panel.findChild<QPushButton *>("pmNativeRead");
            auto *table = panel.findChild<QTableWidget *>("pmTable");
            QCOMPARE(device->calls,0u); read->click(); QTRY_VERIFY(read->isEnabled());
            QCOMPARE(table->rowCount(),24); QCOMPARE(device->memoryReads,711u); QCOMPARE(device->wrongCpu,0u);
            QCOMPARE(table->item(0,1)->text(),partial ? QString("Unavailable") : QString("2000"));
            panel.findChild<QPushButton *>("pmCopy")->click();
            const auto json = QApplication::clipboard()->text().toUtf8();
            const auto root = QJsonDocument::fromJson(json).object(); const auto data = root["data"].toObject();
            QVERIFY(data["native"].toObject()["metadata_rechecked"].toBool());
            const auto capture = parseAmdPmCapture(json.toStdString()); QCOMPARE(capture.error,partial ? -EDOM : 0);
            QVERIFY(capture.raw == device->bytes);
            QTemporaryDir dir; QFile file(dir.path()+"/native.json"); QVERIFY(file.open(QIODevice::WriteOnly)); file.write(json); file.close();
            AmdPmPanel imported; QCOMPARE(imported.loadSnapshotFile(file.fileName()),!partial);
            QCOMPARE(imported.findChild<QTableWidget *>("pmTable")->rowCount(),24);
            panel.findChild<QComboBox *>("pmView")->setCurrentIndex(9); QCOMPARE(table->rowCount(),36);
            panel.findChild<QComboBox *>("pmView")->setCurrentIndex(0); QCOMPARE(device->memoryReads,711u);
            if (!partial) {
                QApplication::processEvents(); const auto shot = qEnvironmentVariable("OCTOOL_PM_NATIVE_TEST_SCREENSHOT");
                if (!shot.isEmpty()) QVERIFY(panel.grab().save(shot));
            }
        }
    }
    void pmNativeFailureDiscardsOldCapture() {
        using namespace octool::core;
        auto *device = new AmdPmNativeFixture; device->versionAfter = 0x730404;
        auto access = std::make_shared<HardwareAccess>(std::unique_ptr<HardwareBackend>(device));
        AmdPmPanel panel(nullptr,{},access,130); panel.resize(1250,1000); panel.show();
        QTemporaryDir dir; QFile file(dir.path()+"/old.json"); QVERIFY(file.open(QIODevice::WriteOnly));
        file.write(QByteArray::fromStdString(amdPmJson(decodeAmdPm(device->bytes,0x730204)))); file.close();
        QVERIFY(panel.loadSnapshotFile(file.fileName())); QCOMPARE(panel.findChild<QTableWidget *>("pmTable")->rowCount(),24);
        auto *read = panel.findChild<QPushButton *>("pmNativeRead"); read->click();
        QCOMPARE(panel.findChild<QTableWidget *>("pmTable")->rowCount(),0); QTRY_VERIFY(read->isEnabled());
        QCOMPARE(panel.findChild<QTableWidget *>("pmTable")->rowCount(),0);
        QVERIFY(panel.findChild<QLabel *>("pmStatus")->text().contains("recheck"));
        panel.findChild<QPushButton *>("pmCopy")->click();
        const auto data = QJsonDocument::fromJson(QApplication::clipboard()->text().toUtf8()).object()["data"].toObject();
        QCOMPARE(data["native"].toObject()["bytes_read"].toInt(),2844);
        QVERIFY(!data["native"].toObject()["metadata_rechecked"].toBool());
        QVERIFY(data["pm"].toObject()["raw_hex"].toString().isEmpty());
        QApplication::processEvents(); const auto shot = qEnvironmentVariable("OCTOOL_PM_NATIVE_FAILURE_TEST_SCREENSHOT");
        if (!shot.isEmpty()) QVERIFY(panel.grab().save(shot));
    }
    void pmNativeInputChangesAndClose() {
        using namespace octool::core;
        auto *device = new AmdPmNativeFixture;
        auto access = std::make_shared<HardwareAccess>(std::unique_ptr<HardwareBackend>(device));
        AmdPmPanel panel(nullptr,{},access,130); panel.show();
        auto *read = panel.findChild<QPushButton *>("pmNativeRead"); read->click();
        auto *cpu = panel.findChild<QLineEdit *>("pmCpu"); cpu->setText("131"); cpu->setText("130");
        QTRY_VERIFY(read->isEnabled()); QCOMPARE(panel.findChild<QTableWidget *>("pmTable")->rowCount(),0);
        QVERIFY(panel.findChild<QLabel *>("pmStatus")->text().contains("discarded"));
        QVERIFY(!panel.findChild<QPushButton *>("pmCopy")->isEnabled());
        auto owner = std::make_shared<HardwareAccess>(std::unique_ptr<HardwareBackend>(new AmdPmNativeFixture));
        std::weak_ptr<HardwareAccess> lifetime = owner;
        auto *closing = new AmdPmPanel(nullptr,{},owner,130); owner.reset();
        closing->findChild<QPushButton *>("pmNativeRead")->click(); delete closing;
        QTRY_VERIFY(lifetime.expired());
    }
    void pmNativeInvalidInputsBeforeAccess() {
        using namespace octool::core;
        auto *device = new AmdPmNativeFixture;
        auto access = std::make_shared<HardwareAccess>(std::unique_ptr<HardwareBackend>(device));
        AmdPmPanel panel(nullptr,{},access,130);
        for (const char *value : {"","-1","1048576","bad"}) {
            panel.findChild<QLineEdit *>("pmCpu")->setText(value);
            panel.findChild<QPushButton *>("pmNativeRead")->click(); QCOMPARE(device->calls,0u);
        }
        panel.findChild<QLineEdit *>("pmCpu")->setText("130");
        panel.findChild<QCheckBox *>("pmScale")->setChecked(true); panel.findChild<QLineEdit *>("pmBclk")->setText("0");
        panel.findChild<QPushButton *>("pmNativeRead")->click(); QCOMPARE(device->calls,0u);
        AmdPmPanel offline; QVERIFY(!offline.findChild<QPushButton *>("pmNativeRead")->isEnabled());
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
    void pmOriginalLimitMonitorRoundtrip() {
        using namespace octool::core;
        QTemporaryDir directory; QVERIFY(directory.isValid());
        const auto raw=directory.filePath("limits.bin"), json=directory.filePath("limits.json");
        auto driver=std::make_shared<pm_fixture::Driver>(); AmdPmPanel panel(nullptr,pmReader(driver));
        panel.resize(1400,1060); panel.show();
        auto *view=panel.findChild<QComboBox *>("pmView"); auto *table=panel.findChild<QTableWidget *>("pmTable");
        auto *copy=panel.findChild<QPushButton *>("pmCopy");
        view->setCurrentIndex(1);
        for (const auto &layout : amdPmLayouts()) {
            QVERIFY(savePmFile(raw,pm_fixture::bytes(layout.bytes))); QVERIFY(panel.loadRawFile(raw,layout.version));
            QCOMPARE(table->rowCount(),24); QCOMPARE(table->item(0,0)->text(),QString("Fast PPT Limit"));
            QCOMPARE(table->item(0,1)->text(),QString("400.5")); QCOMPARE(table->item(0,2)->text(),QString("Unverified"));
            QCOMPARE(table->item(21,1)->text(),QString("220.25")); QCOMPARE(table->item(21,5)->text(),QString("0x000001dc"));
            for (int row=0; row<24; ++row) {
                QCOMPARE(table->item(row,3)->text(),QString::fromUtf8("—"));
                QCOMPARE(table->item(row,6)->text(),QString("Units unverified"));
            }
            copy->click(); const auto captured=QApplication::clipboard()->text();
            QCOMPARE(parseAmdPmCapture(captured.toStdString()).originalLimits.size(),std::size_t(24));
            QVERIFY(savePmJson(json,"{\"schema_version\":1,\"command\":\"amd-pm-read\",\"data\":{\"pm\":"+captured.toStdString()+"}}"));
            QVERIFY(panel.loadSnapshotFile(json)); QCOMPARE(table->item(0,1)->text(),QString("400.5"));
            view->setCurrentIndex(0); QCOMPARE(table->item(0,1)->text(),QString("2000"));
            view->setCurrentIndex(1); copy->click(); QCOMPARE(QApplication::clipboard()->text(),captured);
        }
        auto forged=QApplication::clipboard()->text();
        forged.replace("\"stored_value\":400.5","\"stored_value\":999");
        forged.replace("\"unit\":\"unverified\"","\"unit\":\"W\"");
        forged.replace("\"original_limits_units_verified\":false","\"original_limits_units_verified\":true");
        QVERIFY(savePmJson(json,forged.toStdString())); QVERIFY(panel.loadSnapshotFile(json));
        QCOMPARE(table->item(0,1)->text(),QString("400.5")); QCOMPARE(table->item(0,2)->text(),QString("Unverified"));
        auto *status=panel.findChild<QLabel *>("pmStatus");
        QTRY_VERIFY(status->height()>=status->heightForWidth(status->width()));
        QVERIFY(status->geometry().bottom()<panel.height()); QVERIFY(driver->calls.empty());
        const auto shot=qEnvironmentVariable("OCTOOL_PM_LIMITS_TEST_SCREENSHOT");
        if (!shot.isEmpty()) QVERIFY(panel.grab().save(shot));
    }
    void pmOriginalLimitMonitorInvalidation() {
        QTemporaryDir directory; QVERIFY(directory.isValid()); const auto raw=directory.filePath("limits.bin");
        QVERIFY(savePmFile(raw,pm_fixture::bytes()));
        auto driver=std::make_shared<pm_fixture::Driver>(); AmdPmPanel panel(nullptr,pmReader(driver));
        auto *view=panel.findChild<QComboBox *>("pmView"); auto *table=panel.findChild<QTableWidget *>("pmTable");
        auto *copy=panel.findChild<QPushButton *>("pmCopy"); view->setCurrentIndex(1);
        for (unsigned mode=0; mode<3; ++mode) {
            QVERIFY(panel.loadRawFile(raw,0x730204)); QVERIFY(copy->isEnabled());
            if (mode==0) QVERIFY(!panel.loadSnapshotFile(directory.filePath("missing")));
            if (mode==1) panel.findChild<QLineEdit *>("pmBclk")->setText("102.5");
            if (mode==2) panel.findChild<QComboBox *>("pmVersion")->setCurrentIndex(2);
            QCOMPARE(table->rowCount(),0); QVERIFY(!copy->isEnabled());
            view->setCurrentIndex(0); QCOMPARE(table->rowCount(),0);
            view->setCurrentIndex(1); QCOMPARE(table->rowCount(),0);
        }
        QVERIFY(driver->calls.empty());
    }
    void pmOriginalLimitDriverErrors() {
        auto driver=std::make_shared<pm_fixture::Driver>(); AmdPmPanel panel(nullptr,pmReader(driver));
        panel.resize(1400,1060); panel.show();
        auto *view=panel.findChild<QComboBox *>("pmView"); view->setCurrentIndex(1);
        auto *table=panel.findChild<QTableWidget *>("pmTable"); auto *read=panel.findChild<QPushButton *>("pmRead");
        auto *copy=panel.findChild<QPushButton *>("pmCopy");
        pm_fixture::put(driver->files["pm_table"],0,0x7f800001);
        read->click(); QTRY_VERIFY(read->isEnabled());
        QCOMPARE(table->rowCount(),24); QCOMPARE(table->item(0,1)->text(),QString("Unavailable"));
        QCOMPARE(table->item(1,1)->text(),QString("350.25")); QCOMPARE(table->item(0,4)->text(),QString("0x7f800001"));
        QVERIFY(copy->isEnabled()); copy->click();
        const auto decoded=octool::core::parseAmdPmCapture(QApplication::clipboard()->text().toStdString());
        QCOMPARE(decoded.error,-EDOM); QCOMPARE(decoded.originalLimits[0].error,-EDOM);
        view->setCurrentIndex(0); QCOMPARE(table->item(0,1)->text(),QString("2000"));
        view->setCurrentIndex(1);
        auto *status=panel.findChild<QLabel *>("pmStatus");
        QTRY_VERIFY(status->height()>=status->heightForWidth(status->width()));
        QVERIFY(status->geometry().bottom()<panel.height());
        const auto shot=qEnvironmentVariable("OCTOOL_PM_LIMITS_ERROR_TEST_SCREENSHOT");
        if (!shot.isEmpty()) QVERIFY(panel.grab().save(shot));
        *driver=pm_fixture::Driver(); driver->mutateAfter=4;
        read->click(); QTRY_VERIFY(read->isEnabled()); QCOMPARE(table->rowCount(),0); QVERIFY(!copy->isEnabled());
        view->setCurrentIndex(0); QCOMPARE(table->rowCount(),0);
        QVERIFY(status->text().contains("discarded"));
    }
    void pmOriginalPackageRoundtrip() {
        using namespace octool::core;
        QTemporaryDir directory; QVERIFY(directory.isValid());
        const auto raw=directory.filePath("package.bin"), json=directory.filePath("package.json");
        auto driver=std::make_shared<pm_fixture::Driver>(); AmdPmPanel panel(nullptr,pmReader(driver));
        panel.resize(1150,650); panel.show();
        auto *view=panel.findChild<QComboBox *>("pmView"); auto *table=panel.findChild<QTableWidget *>("pmTable");
        auto *copy=panel.findChild<QPushButton *>("pmCopy"); auto *status=panel.findChild<QLabel *>("pmStatus");
        QCOMPARE(view->count(),12);
        const int counts[]={7,8,4,10,24}; const char *names[]={"CPU Power","PPT_WC","VDDCR_CPU_DYN_POWER_WC","FIT PID0","PPT0"};
        const char *values[]={"10","11.75","13.75","14.75","17.25"};
        for (const auto &layout : amdPmLayouts()) {
            QVERIFY(savePmFile(raw,pm_fixture::bytes(layout.bytes))); QVERIFY(panel.loadRawFile(raw,layout.version));
            for (int group=0; group<5; ++group) {
                view->setCurrentIndex(group+2); QCOMPARE(table->rowCount(),counts[group]);
                QCOMPARE(table->item(0,0)->text(),QString(names[group])); QCOMPARE(table->item(0,1)->text(),QString(values[group]));
                for (int row=0; row<table->rowCount(); ++row) {
                    QCOMPARE(table->item(row,2)->text(),QString("Unverified"));
                    QCOMPARE(table->item(row,3)->text(),QString::fromUtf8("—"));
                }
            }
            copy->click(); const auto captured=QApplication::clipboard()->text();
            QCOMPARE(parseAmdPmCapture(captured.toStdString()).originalPackage.size(),std::size_t(53));
            QVERIFY(savePmJson(json,"{\"schema_version\":1,\"command\":\"amd-pm-decode\",\"data\":{\"pm\":"+captured.toStdString()+"}}"));
            QVERIFY(panel.loadSnapshotFile(json)); copy->click(); QCOMPARE(QApplication::clipboard()->text(),captured);
        }
        auto forged=QApplication::clipboard()->text();
        forged.replace("\"name\":\"CPU Power\"","\"name\":\"Forged\"");
        forged.replace("\"original_package_units_verified\":false","\"original_package_units_verified\":true");
        forged.replace("\"group\":\"power\"","\"group\":\"physical_cpu_0\"");
        QVERIFY(savePmJson(json,forged.toStdString())); QVERIFY(panel.loadSnapshotFile(json));
        view->setCurrentIndex(2); QCOMPARE(table->item(0,0)->text(),QString("CPU Power"));
        QTRY_VERIFY(status->height()>=status->heightForWidth(status->width()));
        QVERIFY(status->geometry().bottom()<panel.height());
        auto shot=qEnvironmentVariable("OCTOOL_PM_PACKAGE_TEST_SCREENSHOT");
        if (!shot.isEmpty()) QVERIFY(panel.grab().save(shot));
        view->setCurrentIndex(5); QCOMPARE(table->rowCount(),10);
        QTRY_VERIFY(table->columnWidth(0)>=table->fontMetrics().boundingRect("LATCHUP_VOLTAGE_1").width());
        shot=qEnvironmentVariable("OCTOOL_PM_POLICY_TEST_SCREENSHOT");
        if (!shot.isEmpty()) QVERIFY(panel.grab().save(shot));
        QVERIFY(driver->calls.empty());
    }
    void pmOriginalPackageDriverErrors() {
        using namespace octool::core;
        auto driver=std::make_shared<pm_fixture::Driver>(); AmdPmPanel panel(nullptr,pmReader(driver));
        panel.resize(1150,650); panel.show();
        auto *view=panel.findChild<QComboBox *>("pmView"); auto *table=panel.findChild<QTableWidget *>("pmTable");
        auto *read=panel.findChild<QPushButton *>("pmRead"); auto *copy=panel.findChild<QPushButton *>("pmCopy");
        auto *status=panel.findChild<QLabel *>("pmStatus"); view->setCurrentIndex(2);
        pm_fixture::put(driver->files["pm_table"],0x60,0x7f800001);
        read->click(); QTRY_VERIFY(read->isEnabled()); QCOMPARE(table->rowCount(),7);
        QCOMPARE(table->item(0,1)->text(),QString("Unavailable")); QCOMPARE(table->item(1,1)->text(),QString("10.25"));
        QCOMPARE(table->item(0,4)->text(),QString("0x7f800001"));
        QVERIFY(copy->isEnabled()); copy->click();
        const auto decoded=parseAmdPmCapture(QApplication::clipboard()->text().toStdString());
        QCOMPARE(decoded.error,-EDOM); QCOMPARE(decoded.originalPackage[0].error,-EDOM);
        QCOMPARE(decoded.originalLimits[0].value,400.5); QCOMPARE(decoded.fields[0].value,2000.0);
        QTRY_VERIFY(status->height()>=status->heightForWidth(status->width()));
        QVERIFY(status->geometry().bottom()<panel.height());
        const auto shot=qEnvironmentVariable("OCTOOL_PM_PACKAGE_ERROR_TEST_SCREENSHOT");
        if (!shot.isEmpty()) QVERIFY(panel.grab().save(shot));
        *driver=pm_fixture::Driver(); driver->mutateAfter=4;
        read->click(); QTRY_VERIFY(read->isEnabled()); QVERIFY(!copy->isEnabled());
        for (int i=0; i<7; ++i) { view->setCurrentIndex(i); QCOMPARE(table->rowCount(),0); }
    }
    void pmOriginalPackageInvalidation() {
        QTemporaryDir directory; QVERIFY(directory.isValid()); const auto raw=directory.filePath("package.bin");
        QVERIFY(savePmFile(raw,pm_fixture::bytes()));
        auto driver=std::make_shared<pm_fixture::Driver>(); AmdPmPanel panel(nullptr,pmReader(driver));
        auto *view=panel.findChild<QComboBox *>("pmView"); auto *table=panel.findChild<QTableWidget *>("pmTable");
        auto *copy=panel.findChild<QPushButton *>("pmCopy");
        for (int group=2; group<7; ++group) {
            view->setCurrentIndex(group); QVERIFY(panel.loadRawFile(raw,0x730204)); QVERIFY(copy->isEnabled());
            QVERIFY(!panel.loadSnapshotFile(directory.filePath("missing"))); QVERIFY(!copy->isEnabled());
            for (int i=0; i<7; ++i) { view->setCurrentIndex(i); QCOMPARE(table->rowCount(),0); }
            QVERIFY(panel.loadRawFile(raw,0x730204));
            panel.findChild<QLineEdit *>("pmBclk")->setText(QString::number(100+group));
            QVERIFY(!copy->isEnabled());
            for (int i=0; i<7; ++i) { view->setCurrentIndex(i); QCOMPARE(table->rowCount(),0); }
        }
        QVERIFY(driver->calls.empty());
    }
    void pmOriginalFabricRoundtrip() {
        using namespace octool::core;
        QTemporaryDir directory; QVERIFY(directory.isValid());
        const auto raw=directory.filePath("fabric.bin"), json=directory.filePath("fabric.json");
        auto driver=std::make_shared<pm_fixture::Driver>(); AmdPmPanel panel(nullptr,pmReader(driver));
        panel.resize(1150,650); panel.show();
        auto *view=panel.findChild<QComboBox *>("pmView"); auto *table=panel.findChild<QTableWidget *>("pmTable");
        auto *copy=panel.findChild<QPushButton *>("pmCopy"); auto *status=panel.findChild<QLabel *>("pmStatus");
        QCOMPARE(view->count(),12);
        for (const auto &layout : amdPmLayouts()) {
            QVERIFY(savePmFile(raw,pm_fixture::bytes(layout.bytes))); QVERIFY(panel.loadRawFile(raw,layout.version));
            view->setCurrentIndex(7); QCOMPARE(table->rowCount(),9);
            QCOMPARE(table->item(0,0)->text(),QString("FCLK_PCIE_UP_THRESHOLD"));
            QCOMPARE(table->item(8,0)->text(),QString("FCLK_CXL_DOWN_THRESHOLD"));
            QCOMPARE(table->item(0,1)->text(),QString("20"));
            view->setCurrentIndex(8); QCOMPARE(table->rowCount(),18);
            QCOMPARE(table->item(0,0)->text(),QString("SMNCLK"));
            QCOMPARE(table->item(17,0)->text(),QString("MPDMA_PM_CLK_EFF"));
            QCOMPARE(table->item(17,1)->text(),QString("33"));
            for (int row=0; row<table->rowCount(); ++row) {
                QCOMPARE(table->item(row,2)->text(),QString("Unverified"));
                QCOMPARE(table->item(row,3)->text(),QString::fromUtf8("—"));
            }
            copy->click(); const auto captured=QApplication::clipboard()->text();
            QCOMPARE(parseAmdPmCapture(captured.toStdString()).originalFabric.size(),std::size_t(27));
            QVERIFY(savePmJson(json,"{\"schema_version\":1,\"command\":\"amd-pm-decode\",\"data\":{\"pm\":"+captured.toStdString()+"}}"));
            QVERIFY(panel.loadSnapshotFile(json)); copy->click(); QCOMPARE(QApplication::clipboard()->text(),captured);
        }
        auto capture=QApplication::clipboard()->text();
        auto forged=capture;
        forged.replace("\"name\":\"SMNCLK\"","\"name\":\"Forged MHz\"");
        forged.replace("\"original_fabric_units_verified\":false","\"original_fabric_units_verified\":true");
        QVERIFY(savePmJson(json,forged.toStdString())); QVERIFY(panel.loadSnapshotFile(json));
        QCOMPARE(table->item(0,0)->text(),QString("SMNCLK"));
        const int extension=capture.indexOf(",\"original_fabric_units_verified\"");
        QVERIFY(extension>0); QVERIFY(savePmJson(json,(capture.left(extension)+"}").toStdString()));
        QVERIFY(panel.loadSnapshotFile(json)); copy->click(); QCOMPARE(QApplication::clipboard()->text(),capture);
        view->setCurrentIndex(7);
        QTRY_VERIFY(table->columnWidth(0)>=table->fontMetrics().boundingRect("FCLK_PCIE_DOWN_THRESHOLD").width());
        QTRY_VERIFY(status->height()>=status->heightForWidth(status->width()));
        QVERIFY(status->geometry().bottom()<panel.height());
        auto shot=qEnvironmentVariable("OCTOOL_PM_LINK_TEST_SCREENSHOT");
        if (!shot.isEmpty()) QVERIFY(panel.grab().save(shot));
        // Leave room for the native-capture controls while still checking that
        // this exported screenshot shows every internal-clock row.
        view->setCurrentIndex(8); panel.resize(1150,1020);
        QTRY_VERIFY(table->viewport()->height()>table->rowCount()*table->rowHeight(0));
        QTRY_VERIFY(status->geometry().bottom()<panel.height());
        shot=qEnvironmentVariable("OCTOOL_PM_CLOCK_TEST_SCREENSHOT");
        if (!shot.isEmpty()) QVERIFY(panel.grab().save(shot));
        QVERIFY(driver->calls.empty());
    }
    void pmOriginalFabricDriverErrors() {
        using namespace octool::core;
        auto driver=std::make_shared<pm_fixture::Driver>(); AmdPmPanel panel(nullptr,pmReader(driver));
        panel.resize(1150,940); panel.show();
        auto *view=panel.findChild<QComboBox *>("pmView"); view->setCurrentIndex(8);
        auto *table=panel.findChild<QTableWidget *>("pmTable");
        auto *read=panel.findChild<QPushButton *>("pmRead"); auto *copy=panel.findChild<QPushButton *>("pmCopy");
        pm_fixture::put(driver->files["pm_table"],0x24c,0x7f800001);
        read->click(); QTRY_VERIFY(read->isEnabled());
        QCOMPARE(table->rowCount(),18); QCOMPARE(table->item(0,1)->text(),QString("Unavailable"));
        QCOMPARE(table->item(0,4)->text(),QString("0x7f800001")); QCOMPARE(table->item(1,1)->text(),QString("25"));
        QVERIFY(copy->isEnabled()); copy->click();
        const auto s=parseAmdPmCapture(QApplication::clipboard()->text().toStdString());
        QCOMPARE(s.originalFabric[9].error,-EDOM); QCOMPARE(s.originalFabric[0].value,20.0);
        const auto shot=qEnvironmentVariable("OCTOOL_PM_FABRIC_ERROR_TEST_SCREENSHOT");
        if (!shot.isEmpty()) QVERIFY(panel.grab().save(shot));
        driver->calls.clear(); driver->failCall=4;
        read->click(); QTRY_VERIFY(read->isEnabled()); QVERIFY(!copy->isEnabled());
        for (int i=0; i<9; ++i) { view->setCurrentIndex(i); QCOMPARE(table->rowCount(),0); }
    }
    void pmOriginalFabricInvalidation() {
        QTemporaryDir directory; QVERIFY(directory.isValid()); const auto raw=directory.filePath("fabric.bin");
        QVERIFY(savePmFile(raw,pm_fixture::bytes()));
        auto driver=std::make_shared<pm_fixture::Driver>(); AmdPmPanel panel(nullptr,pmReader(driver));
        auto *view=panel.findChild<QComboBox *>("pmView"); auto *table=panel.findChild<QTableWidget *>("pmTable");
        auto *copy=panel.findChild<QPushButton *>("pmCopy");
        for (int group=7; group<9; ++group) {
            view->setCurrentIndex(group); QVERIFY(panel.loadRawFile(raw,0x730204)); QVERIFY(copy->isEnabled());
            QVERIFY(!panel.loadRawFile(raw,0x730205)); QVERIFY(!copy->isEnabled()); QCOMPARE(table->rowCount(),0);
            QVERIFY(panel.loadRawFile(raw,0x730204));
            QVERIFY(!panel.loadSnapshotFile(directory.filePath("missing"))); QVERIFY(!copy->isEnabled());
            for (int i=0; i<9; ++i) { view->setCurrentIndex(i); QCOMPARE(table->rowCount(),0); }
            QVERIFY(panel.loadRawFile(raw,0x730204));
            panel.findChild<QLineEdit *>("pmBclk")->setText(QString::number(100+group));
            QVERIFY(!copy->isEnabled());
            for (int i=0; i<9; ++i) { view->setCurrentIndex(i); QCOMPARE(table->rowCount(),0); }
        }
        QVERIFY(driver->calls.empty());
    }
    void pmOriginalArraysRoundtrip() {
        using namespace octool::core;
        QTemporaryDir directory; QVERIFY(directory.isValid());
        const auto raw=directory.filePath("arrays.bin"), json=directory.filePath("arrays.json");
        auto driver=std::make_shared<pm_fixture::Driver>(); AmdPmPanel panel(nullptr,pmReader(driver));
        panel.resize(1150,940); panel.show();
        auto *view=panel.findChild<QComboBox *>("pmView"); auto *table=panel.findChild<QTableWidget *>("pmTable");
        auto *copy=panel.findChild<QPushButton *>("pmCopy"); auto *status=panel.findChild<QLabel *>("pmStatus");
        QCOMPARE(view->count(),12);
        const int counts[]={36,48,8}, first[]={0,36,84};
        for (const auto &layout : amdPmLayouts()) {
            QVERIFY(savePmFile(raw,pm_fixture::bytes(layout.bytes))); QVERIFY(panel.loadRawFile(raw,layout.version));
            for (int group=0; group<3; ++group) {
                view->setCurrentIndex(9+group); QCOMPARE(table->rowCount(),counts[group]);
                for (int row=0; row<counts[group]; ++row) {
                    QCOMPARE(table->item(row,1)->text(),QString::number(64+first[group]+row));
                    QCOMPARE(table->item(row,2)->text(),QString("Unverified"));
                    QCOMPARE(table->item(row,3)->text(),QString::fromUtf8("—"));
                }
            }
            QCOMPARE(table->item(0,0)->text(),QString("PCIE_ALLOWED"));
            QCOMPARE(table->item(7,0)->text(),QString("PCIE_IOHC_BANDWIDTH[4] [slot 3]"));
            copy->click(); const auto captured=QApplication::clipboard()->text();
            QCOMPARE(parseAmdPmCapture(captured.toStdString()).originalArrays.size(),std::size_t(92));
            QVERIFY(savePmJson(json,"{\"schema_version\":1,\"command\":\"amd-pm-decode\",\"data\":{\"pm\":"+captured.toStdString()+"}}"));
            QVERIFY(panel.loadSnapshotFile(json)); copy->click(); QCOMPARE(QApplication::clipboard()->text(),captured);
        }
        const auto capture=QApplication::clipboard()->text();
        auto forged=capture;
        forged.replace("\"name\":\"DIMM_POWER\"","\"name\":\"Forged module power\"");
        forged.replace("\"slot\":0,\"slot_count\":12","\"slot\":999,\"slot_count\":1000");
        forged.replace("\"original_arrays_targets_verified\":false","\"original_arrays_targets_verified\":true");
        QVERIFY(savePmJson(json,forged.toStdString())); QVERIFY(panel.loadSnapshotFile(json));
        copy->click(); QCOMPARE(QApplication::clipboard()->text(),capture);
        const int extension=capture.indexOf(",\"original_arrays_units_verified\"");
        QVERIFY(extension>0); QVERIFY(savePmJson(json,(capture.left(extension)+"}").toStdString()));
        QVERIFY(panel.loadSnapshotFile(json)); copy->click(); QCOMPARE(QApplication::clipboard()->text(),capture);
        view->setCurrentIndex(9);
        QCOMPARE(table->item(35,0)->text(),QString("GMI_QUARTER_WIDTH [slot 11]"));
        QTRY_VERIFY(table->columnWidth(0)>=table->fontMetrics().boundingRect(table->item(35,0)->text()).width());
        QTRY_VERIFY(status->height()>=status->heightForWidth(status->width()));
        QVERIFY(status->geometry().bottom()<panel.height());
        auto shot=qEnvironmentVariable("OCTOOL_PM_GMI_TEST_SCREENSHOT");
        if (!shot.isEmpty()) QVERIFY(panel.grab().save(shot));
        view->setCurrentIndex(10);
        QCOMPARE(table->item(0,0)->text(),QString("DIMM_POWER [slot 0]"));
        QCOMPARE(table->item(47,0)->text(),QString("DIMM_TEMP [slot 23]"));
        table->scrollToItem(table->item(47,0));
        shot=qEnvironmentVariable("OCTOOL_PM_DIMM_TEST_SCREENSHOT");
        if (!shot.isEmpty()) QVERIFY(panel.grab().save(shot));
        view->setCurrentIndex(11);
        shot=qEnvironmentVariable("OCTOOL_PM_PCIE_TEST_SCREENSHOT");
        if (!shot.isEmpty()) QVERIFY(panel.grab().save(shot));
        QVERIFY(driver->calls.empty());
    }
    void pmOriginalArraysDriverErrors() {
        using namespace octool::core;
        auto driver=std::make_shared<pm_fixture::Driver>(); AmdPmPanel panel(nullptr,pmReader(driver));
        panel.resize(1150,940); panel.show();
        auto *view=panel.findChild<QComboBox *>("pmView"); view->setCurrentIndex(10);
        auto *table=panel.findChild<QTableWidget *>("pmTable");
        auto *read=panel.findChild<QPushButton *>("pmRead"); auto *copy=panel.findChild<QPushButton *>("pmCopy");
        pm_fixture::put(driver->files["pm_table"],0x384,0x7f800001);
        read->click(); QTRY_VERIFY(read->isEnabled());
        QCOMPARE(table->rowCount(),48); QCOMPARE(table->item(24,1)->text(),QString("Unavailable"));
        QCOMPARE(table->item(24,4)->text(),QString("0x7f800001")); QCOMPARE(table->item(25,1)->text(),QString("125"));
        QVERIFY(copy->isEnabled()); copy->click();
        const auto s=parseAmdPmCapture(QApplication::clipboard()->text().toStdString());
        QCOMPARE(s.originalArrays[60].error,-EDOM); QCOMPARE(s.originalArrays[59].value,123.0);
        table->scrollToItem(table->item(24,0));
        const auto shot=qEnvironmentVariable("OCTOOL_PM_ARRAY_ERROR_TEST_SCREENSHOT");
        if (!shot.isEmpty()) QVERIFY(panel.grab().save(shot));
        driver->calls.clear(); driver->mutateAfter=4;
        read->click(); QTRY_VERIFY(read->isEnabled()); QVERIFY(!copy->isEnabled());
        for (int i=0; i<12; ++i) { view->setCurrentIndex(i); QCOMPARE(table->rowCount(),0); }
    }
    void pmOriginalArraysInvalidation() {
        QTemporaryDir directory; QVERIFY(directory.isValid()); const auto raw=directory.filePath("arrays.bin");
        QVERIFY(savePmFile(raw,pm_fixture::bytes()));
        auto driver=std::make_shared<pm_fixture::Driver>(); AmdPmPanel panel(nullptr,pmReader(driver));
        auto *view=panel.findChild<QComboBox *>("pmView"); auto *table=panel.findChild<QTableWidget *>("pmTable");
        auto *copy=panel.findChild<QPushButton *>("pmCopy");
        for (int group=9; group<12; ++group) {
            view->setCurrentIndex(group); QVERIFY(panel.loadRawFile(raw,0x730204)); QVERIFY(copy->isEnabled());
            QVERIFY(!panel.loadRawFile(raw,0x730205)); QVERIFY(!copy->isEnabled());
            for (int i=0; i<12; ++i) { view->setCurrentIndex(i); QCOMPARE(table->rowCount(),0); }
            QVERIFY(panel.loadRawFile(raw,0x730204));
            QVERIFY(!panel.loadSnapshotFile(directory.filePath("missing"))); QVERIFY(!copy->isEnabled());
            for (int i=0; i<12; ++i) { view->setCurrentIndex(i); QCOMPARE(table->rowCount(),0); }
            QVERIFY(panel.loadRawFile(raw,0x730204));
            panel.findChild<QLineEdit *>("pmBclk")->setText(QString::number(100+group));
            QVERIFY(!copy->isEnabled());
            for (int i=0; i<12; ++i) { view->setCurrentIndex(i); QCOMPARE(table->rowCount(),0); }
        }
        QVERIFY(driver->calls.empty());
    }
    void pmExpandedCaptureBounds() {
        using namespace octool::core;
        QTemporaryDir directory; QVERIFY(directory.isValid()); const auto json=directory.filePath("largest.json");
        auto driver=std::make_shared<pm_fixture::Driver>(); AmdPmPanel panel(nullptr,pmReader(driver));
        auto bytes=pm_fixture::bytes(11636);
        for (unsigned offset=0; offset<bytes.size(); offset+=4) pm_fixture::put(bytes,offset,0xff7fffff);
        AmdPmOptions o; o.hasBclk=true; o.bclkMHz=123.25;
        const auto capture=amdPmJson(decodeAmdPm(bytes,0x731004,o));
        const auto envelope=std::string("{\"schema_version\":1,\"command\":\"amd-pm-decode\",\"data\":{\"pm\":")+capture+"}}";
        QVERIFY(envelope.size()>65536 && envelope.size()<amdPmCaptureMaximumBytes);
        const auto padded=envelope+std::string(amdPmCaptureMaximumBytes-envelope.size(),' ');
        QVERIFY(savePmJson(json,padded)); QVERIFY(panel.loadSnapshotFile(json));
        auto *view=panel.findChild<QComboBox *>("pmView"); auto *table=panel.findChild<QTableWidget *>("pmTable");
        auto *copy=panel.findChild<QPushButton *>("pmCopy"); copy->click();
        QCOMPARE(QApplication::clipboard()->text().toStdString(),capture);
        view->setCurrentIndex(10); QCOMPARE(table->rowCount(),48);
        QVERIFY(savePmJson(json,padded+" ")); QVERIFY(!panel.loadSnapshotFile(json)); QVERIFY(!copy->isEnabled());
        for (int i=0; i<12; ++i) { view->setCurrentIndex(i); QCOMPARE(table->rowCount(),0); }
        QVERIFY(driver->calls.empty());
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
        IntelClockPanel clock(access,nullptr,7);
        QCOMPARE(clock.findChild<QLineEdit *>("clockCpu")->text(),QString("7"));
        QCOMPARE(clock.findChild<QTableWidget *>("clockTable")->rowCount(),0);
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
    void intelTvbTableReadAndCopy() {
        auto *device = new IntelTvbTableFixture;
        IntelTvbTablePanel panel(std::make_shared<HardwareAccess>(std::unique_ptr<octool::core::HardwareBackend>(device)),nullptr,130);
        panel.resize(1180,600); panel.show(); QCOMPARE(device->calls,0u);
        panel.findChild<QPushButton *>("tvbTableRead")->click(); QTRY_VERIFY(panel.isEnabled());
        auto *table = panel.findChild<QTableWidget *>("tvbThresholdTable"); QCOMPARE(table->rowCount(),8);
        QCOMPARE(table->item(0,1)->text(),QString("70")); QCOMPARE(table->item(0,2)->text(),QString("1"));
        QCOMPARE(table->item(0,3)->text(),QString("100")); QCOMPARE(table->item(0,4)->text(),QString("2"));
        QCOMPARE(table->item(0,5)->text(),QString("0x02644601"));
        auto *status = panel.findChild<QLabel *>("tvbTableStatus"); QVERIFY(status->text().contains("8 of 8"));
        panel.findChild<QPushButton *>("tvbTableCopy")->click();
        QVERIFY(QApplication::clipboard()->text().contains("CPU 130"));
        QVERIFY(QApplication::clipboard()->text().contains("1\t70\t1\t100\t2\t0x02644601\tRead"));
        QTRY_VERIFY(status->height() >= status->heightForWidth(status->width()));
        QTRY_VERIFY(table->columnWidth(5) >= table->fontMetrics().horizontalAdvance("0x02644601"));
        const auto shot = qEnvironmentVariable("OCTOOL_INTEL_TVB_TABLE_TEST_SCREENSHOT"); if (!shot.isEmpty()) QVERIFY(panel.grab().save(shot));
        QCOMPARE(device->mutations,0u); QCOMPARE(device->wrongCpu,0u);
    }
    void intelTvbTableSelectionAndPartialFailure() {
        auto *device = new IntelTvbTableFixture;
        IntelTvbTablePanel panel(std::make_shared<HardwareAccess>(std::unique_ptr<octool::core::HardwareBackend>(device)),nullptr,130); panel.show();
        auto *read = panel.findChild<QPushButton *>("tvbTableRead"); auto *table = panel.findChild<QTableWidget *>("tvbThresholdTable");
        auto *entry = panel.findChild<QComboBox *>("tvbTableEntry");
        entry->setCurrentIndex(8); read->click(); QTRY_VERIFY(panel.isEnabled());
        QCOMPARE(table->rowCount(),1); QCOMPARE(table->item(0,0)->text(),QString("8"));
        entry->setCurrentIndex(0); QCOMPARE(table->rowCount(),0);
        device->statuses[1] = 128; read->click(); QTRY_VERIFY(panel.isEnabled()); QCOMPARE(table->rowCount(),8);
        QCOMPARE(table->item(1,1)->text(),QString("—")); QCOMPARE(table->item(1,5)->text(),QString("—"));
        QCOMPARE(table->item(1,6)->text(),QString("Firmware status 128")); QCOMPARE(table->item(7,6)->text(),QString("Read"));
        QVERIFY(panel.findChild<QLabel *>("tvbTableStatus")->text().contains("7 of 8"));
        device->clearTrace(); device->busyEntry = 4; read->click(); QTRY_VERIFY(panel.isEnabled());
        QCOMPARE(table->rowCount(),4); QCOMPARE(table->item(3,5)->text(),QString("—"));
        QVERIFY(panel.findChild<QLabel *>("tvbTableStatus")->text().contains("Scan stopped"));
        QCOMPARE(device->mutations,0u);
    }
    void intelTvbTableInvalidTargetsAndGates() {
        auto *device = new IntelTvbTableFixture;
        IntelTvbTablePanel panel(std::make_shared<HardwareAccess>(std::unique_ptr<octool::core::HardwareBackend>(device)),nullptr,130); panel.show();
        auto *read = panel.findChild<QPushButton *>("tvbTableRead"); auto *cpu = panel.findChild<QLineEdit *>("tvbTableCpu");
        auto *table = panel.findChild<QTableWidget *>("tvbThresholdTable");
        read->click(); QTRY_VERIFY(panel.isEnabled()); QCOMPARE(table->rowCount(),8);
        cpu->setText("-1"); QCOMPARE(table->rowCount(),0); const auto calls = device->calls;
        read->click(); QCOMPARE(device->calls,calls);
        cpu->setText("130"); device->hypervisor = true; device->clearTrace();
        read->click(); QTRY_VERIFY(panel.isEnabled()); QCOMPARE(table->rowCount(),0); QVERIFY(device->requests.empty());
        panel.findChild<QPushButton *>("tvbTableCopy")->click(); QVERIFY(!QApplication::clipboard()->text().contains("0x02644601"));
    }
    void intelTvbTableLifetimeAndStaleResults() {
        struct Gate { std::atomic<bool> entered{false}, release{false}, destroyed{false}; };
        struct Device : IntelTvbTableFixture {
            std::shared_ptr<Gate> gate;
            explicit Device(std::shared_ptr<Gate> g) : gate(g) {}
            ~Device() override { gate->destroyed = true; }
        };
        for (unsigned mode = 0; mode < 3; ++mode) {
            auto gate = std::make_shared<Gate>(); auto *device = new Device(gate);
            device->afterRequest = [gate] {
                if (gate->entered.exchange(true)) return;
                const auto deadline = std::chrono::steady_clock::now()+std::chrono::seconds(3);
                while (!gate->release && std::chrono::steady_clock::now()<deadline) std::this_thread::sleep_for(std::chrono::milliseconds(1));
            };
            auto *panel = new IntelTvbTablePanel(std::make_shared<HardwareAccess>(std::unique_ptr<octool::core::HardwareBackend>(device)),nullptr,130);
            panel->findChild<QPushButton *>("tvbTableRead")->click(); QTRY_VERIFY(gate->entered.load());
            if (mode == 0) { delete panel; QVERIFY(!gate->destroyed.load()); gate->release = true; QTRY_VERIFY(gate->destroyed.load()); }
            else {
                if (mode == 1) panel->findChild<QLineEdit *>("tvbTableCpu")->setText("131");
                else panel->findChild<QComboBox *>("tvbTableEntry")->setCurrentIndex(1);
                gate->release = true; QTRY_VERIFY(panel->isEnabled());
                QCOMPARE(panel->findChild<QTableWidget *>("tvbThresholdTable")->rowCount(),0);
                QVERIFY(panel->findChild<QLabel *>("tvbTableStatus")->text().contains("discarded")); delete panel;
            }
        }
    }
    void intelTvbTablePrepareAndBounds() {
        auto *device = new IntelTvbTableFixture;
        IntelTvbTablePanel panel(std::make_shared<HardwareAccess>(std::unique_ptr<octool::core::HardwareBackend>(device)),nullptr,130); panel.show();
        auto *entry = panel.findChild<QComboBox *>("tvbTableEntry"); auto *prepare = panel.findChild<QPushButton *>("tvbTablePrepare");
        auto *apply = panel.findChild<QPushButton *>("tvbTableApply"); auto *value = panel.findChild<QLineEdit *>("tvbTableValue");
        auto *status = panel.findChild<QLabel *>("tvbTableStatus");
        QVERIFY(!prepare->isEnabled()); QVERIFY(!apply->isEnabled()); QCOMPARE(device->calls,0u);
        entry->setCurrentIndex(1); QVERIFY(prepare->isEnabled()); prepare->click(); QTRY_VERIFY(panel.isEnabled());
        QVERIFY(apply->isEnabled()); QCOMPARE(value->text(),QString("70"));
        entry->setCurrentIndex(2); QVERIFY(!apply->isEnabled()); QVERIFY(value->text().isEmpty());
        prepare->click(); QTRY_VERIFY(panel.isEnabled()); QVERIFY(!apply->isEnabled());
        QVERIFY(status->text().contains("outside the supported editing profile"));
        QCOMPARE(panel.findChild<QTableWidget *>("tvbThresholdTable")->item(0,3)->text(),QString("105"));
        entry->setCurrentIndex(1); device->locked = true; prepare->click(); QTRY_VERIFY(panel.isEnabled());
        QVERIFY(!apply->isEnabled()); QVERIFY(status->text().contains("OC locked"));
        device->failCommand = 0x18; device->status = 128; prepare->click(); QTRY_VERIFY(panel.isEnabled());
        QVERIFY(!apply->isEnabled()); QVERIFY(value->text().isEmpty()); QVERIFY(status->text().contains("Control status: 128"));
        QCOMPARE(panel.findChild<QTableWidget *>("tvbThresholdTable")->rowCount(),0);
        const auto calls = device->calls; panel.findChild<QLineEdit *>("tvbTableCpu")->setText("-1"); prepare->click();
        QCOMPARE(device->calls,calls); QCOMPARE(device->tableSettingsAttempts(),0u);
    }
    void intelTvbTableEditFieldsCancelAndNoop() {
        auto *device = new IntelTvbTableFixture;
        IntelTvbTablePanel panel(std::make_shared<HardwareAccess>(std::unique_ptr<octool::core::HardwareBackend>(device)),nullptr,130);
        panel.resize(1180,600); panel.show(); panel.findChild<QComboBox *>("tvbTableEntry")->setCurrentIndex(1);
        panel.findChild<QPushButton *>("tvbTablePrepare")->click(); QTRY_VERIFY(panel.isEnabled());
        auto *field = panel.findChild<QComboBox *>("tvbTableField"); auto *value = panel.findChild<QLineEdit *>("tvbTableValue");
        auto *apply = panel.findChild<QPushButton *>("tvbTableApply"); auto *status = panel.findChild<QLabel *>("tvbTableStatus");
        value->setText("80"); const auto calls = device->calls;
        QTimer::singleShot(0, [] { auto *box = qobject_cast<QMessageBox *>(QApplication::activeModalWidget()); if (box) box->button(QMessageBox::Cancel)->click(); });
        apply->click(); QCOMPARE(device->calls,calls);
        const char *values[] = {"80","0","90","10"};
        const std::uint32_t expected[] = {0x02645001,0x02645000,0x025a5000,0x0a5a5000};
        for (int index = 0; index < 4; ++index) {
            field->setCurrentIndex(index); value->setText(values[index]);
            QTimer::singleShot(0, [] { auto *box = qobject_cast<QMessageBox *>(QApplication::activeModalWidget()); if (box) box->button(QMessageBox::Yes)->click(); });
            apply->click(); QTRY_VERIFY(panel.isEnabled()); QCOMPARE(device->entries[0],expected[index]);
            QCOMPARE(device->tableSettingsAttempts(),unsigned(index+1)); QVERIFY(status->text().contains("readback verified"));
        }
        QTimer::singleShot(0, [] { auto *box = qobject_cast<QMessageBox *>(QApplication::activeModalWidget()); if (box) box->button(QMessageBox::Yes)->click(); });
        apply->click(); QTRY_VERIFY(panel.isEnabled()); QCOMPARE(device->tableSettingsAttempts(),4u);
        QVERIFY(status->text().contains("no settings write performed"));
        panel.findChild<QPushButton *>("tvbTableCopy")->click();
        QVERIFY(QApplication::clipboard()->text().contains("1\t80\t0\t90\t10\t0x0A5A5000\tRead"));
        QTRY_VERIFY(status->height() >= status->heightForWidth(status->width()));
        const auto shot = qEnvironmentVariable("OCTOOL_INTEL_TVB_TABLE_EDIT_TEST_SCREENSHOT"); if (!shot.isEmpty()) QVERIFY(panel.grab().save(shot));
        QCOMPARE(device->wrongCpu,0u); QCOMPARE(device->settingsAttempts(),0u);
    }
    void intelTvbTableEditInvalidValues() {
        auto *device = new IntelTvbTableFixture;
        IntelTvbTablePanel panel(std::make_shared<HardwareAccess>(std::unique_ptr<octool::core::HardwareBackend>(device)),nullptr,130); panel.show();
        panel.findChild<QComboBox *>("tvbTableEntry")->setCurrentIndex(1);
        panel.findChild<QPushButton *>("tvbTablePrepare")->click(); QTRY_VERIFY(panel.isEnabled());
        const auto calls = device->calls;
        for (int field = 0; field < 4; ++field) {
            panel.findChild<QComboBox *>("tvbTableField")->setCurrentIndex(field);
            for (const char *value : {"-1","1.5","","4294967296",field % 2 == 0 ? "101" : "11"}) {
                panel.findChild<QLineEdit *>("tvbTableValue")->setText(value);
                panel.findChild<QPushButton *>("tvbTableApply")->click(); QCOMPARE(device->calls,calls);
                QVERIFY(panel.findChild<QLabel *>("tvbTableStatus")->text().contains("Enter an integer"));
            }
        }
    }
    void intelTvbTableEditStaleAndFailures() {
        for (unsigned mode = 0; mode < 6; ++mode) {
            auto *device = new IntelTvbTableFixture;
            IntelTvbTablePanel panel(std::make_shared<HardwareAccess>(std::unique_ptr<octool::core::HardwareBackend>(device)),nullptr,130); panel.show();
            panel.findChild<QComboBox *>("tvbTableEntry")->setCurrentIndex(1);
            panel.findChild<QPushButton *>("tvbTablePrepare")->click(); QTRY_VERIFY(panel.isEnabled());
            panel.findChild<QLineEdit *>("tvbTableValue")->setText("80");
            if (mode == 0) device->entries[0] ^= 1;
            if (mode == 1) device->data ^= 4;
            if (mode == 2) device->locked = true;
            if (mode == 3) device->discardChange = true;
            if (mode == 4) { device->failCommand = 0x25; device->status = 128; }
            if (mode == 5) device->statuses[0] = 254;
            QTimer::singleShot(0, [] { auto *box = qobject_cast<QMessageBox *>(QApplication::activeModalWidget()); if (box) box->button(QMessageBox::Yes)->click(); });
            panel.findChild<QPushButton *>("tvbTableApply")->click(); QTRY_VERIFY(panel.isEnabled());
            const bool attempted = mode == 3 || mode == 4;
            QCOMPARE(device->tableSettingsAttempts(),attempted ? 1u : 0u);
            QCOMPARE(panel.findChild<QTableWidget *>("tvbThresholdTable")->rowCount(),0);
            QVERIFY(panel.findChild<QLineEdit *>("tvbTableValue")->text().isEmpty());
            QVERIFY(!panel.findChild<QPushButton *>("tvbTableApply")->isEnabled());
            QVERIFY(panel.findChild<QLabel *>("tvbTableStatus")->text().contains(attempted ? "not verified" : "No settings write"));
        }
    }
    void intelTvbTableEditSelectionDuringConfirmation() {
        for (bool changeCpu : {false,true}) {
            auto *device = new IntelTvbTableFixture;
            IntelTvbTablePanel panel(std::make_shared<HardwareAccess>(std::unique_ptr<octool::core::HardwareBackend>(device)),nullptr,130); panel.show();
            panel.findChild<QComboBox *>("tvbTableEntry")->setCurrentIndex(1);
            panel.findChild<QPushButton *>("tvbTablePrepare")->click(); QTRY_VERIFY(panel.isEnabled());
            panel.findChild<QLineEdit *>("tvbTableValue")->setText("80"); const auto calls = device->calls;
            QTimer::singleShot(0, [&panel,changeCpu] {
                if (changeCpu) panel.findChild<QLineEdit *>("tvbTableCpu")->setText("131");
                else panel.findChild<QComboBox *>("tvbTableEntry")->setCurrentIndex(2);
                auto *box = qobject_cast<QMessageBox *>(QApplication::activeModalWidget()); if (box) box->button(QMessageBox::Yes)->click();
            });
            panel.findChild<QPushButton *>("tvbTableApply")->click(); QCOMPARE(device->calls,calls);
            QVERIFY(!panel.findChild<QPushButton *>("tvbTableApply")->isEnabled());
            QVERIFY(panel.findChild<QLabel *>("tvbTableStatus")->text().contains("Selection changed"));
        }
    }
    void intelTvbTableEditLifetimeAndStaleResults() {
        struct Gate { std::atomic<bool> entered{false}, release{false}, destroyed{false}; };
        struct Device : IntelTvbTableFixture {
            std::shared_ptr<Gate> gate;
            explicit Device(std::shared_ptr<Gate> g) : gate(g) {}
            ~Device() override { gate->destroyed = true; }
        };
        for (bool writing : {false,true}) for (unsigned mode = 0; mode < 3; ++mode) {
            auto gate = std::make_shared<Gate>(); auto *device = new Device(gate);
            auto *panel = new IntelTvbTablePanel(std::make_shared<HardwareAccess>(std::unique_ptr<octool::core::HardwareBackend>(device)),nullptr,130);
            panel->findChild<QComboBox *>("tvbTableEntry")->setCurrentIndex(1);
            if (writing) {
                panel->findChild<QPushButton *>("tvbTablePrepare")->click(); QTRY_VERIFY(panel->isEnabled());
                panel->findChild<QLineEdit *>("tvbTableValue")->setText("80");
            }
            device->afterRequest = [gate] {
                if (gate->entered.exchange(true)) return;
                const auto deadline = std::chrono::steady_clock::now()+std::chrono::seconds(3);
                while (!gate->release && std::chrono::steady_clock::now()<deadline) std::this_thread::sleep_for(std::chrono::milliseconds(1));
            };
            if (writing) {
                QTimer::singleShot(0, [] { auto *box = qobject_cast<QMessageBox *>(QApplication::activeModalWidget()); if (box) box->button(QMessageBox::Yes)->click(); });
                panel->findChild<QPushButton *>("tvbTableApply")->click();
            } else panel->findChild<QPushButton *>("tvbTablePrepare")->click();
            QTRY_VERIFY(gate->entered.load());
            if (mode == 0) { delete panel; QVERIFY(!gate->destroyed.load()); gate->release = true; QTRY_VERIFY(gate->destroyed.load()); }
            else {
                if (mode == 1) panel->findChild<QLineEdit *>("tvbTableCpu")->setText("131");
                else panel->findChild<QComboBox *>("tvbTableEntry")->setCurrentIndex(2);
                gate->release = true; QTRY_VERIFY(panel->isEnabled());
                QCOMPARE(panel->findChild<QTableWidget *>("tvbThresholdTable")->rowCount(),0);
                QVERIFY(!panel->findChild<QPushButton *>("tvbTableApply")->isEnabled());
                QVERIFY(panel->findChild<QLabel *>("tvbTableStatus")->text().contains("discarded"));
                QCOMPARE(device->tableSettingsAttempts(),writing ? 1u : 0u); QCOMPARE(device->wrongCpu,0u); delete panel;
            }
        }
    }
    void intelTvbReadCopyAndTargetChange() {
        auto *device = new IntelTvbFixture;
        auto access = std::make_shared<HardwareAccess>(std::unique_ptr<octool::core::HardwareBackend>(device));
        IntelTvbPanel panel(access, nullptr, 130); panel.resize(1100, 640); panel.show();
        auto *read = panel.findChild<QPushButton *>("tvbRead"); auto *table = panel.findChild<QTableWidget *>("tvbTable");
        auto *field = panel.findChild<QComboBox *>("tvbField"); auto *value = panel.findChild<QComboBox *>("tvbValue");
        QVERIFY(!panel.findChild<QPushButton *>("tvbApply")->isEnabled()); QCOMPARE(value->currentIndex(), -1);
        for (unsigned bits : {0u, 2u, 4u, 6u, 8u, 10u, 12u, 14u}) {
            device->data = 0xa5c32110 | bits; read->click(); QTRY_VERIFY(panel.isEnabled()); QCOMPARE(table->rowCount(), 3);
            QCOMPARE(table->item(0,1)->text(), bits & 4 ? QString("Disabled") : QString("Enabled"));
            QCOMPARE(table->item(1,1)->text(), bits & 8 ? QString("Disabled") : QString("Enabled"));
            QCOMPARE(table->item(2,0)->text(), QString("Ring DownBin"));
            QCOMPARE(table->item(2,1)->text(), bits & 2 ? QString("Disabled") : QString("Enabled"));
            field->setCurrentIndex(2); QCOMPARE(value->currentIndex(), bits & 2 ? 0 : 1);
            field->setCurrentIndex(1); QCOMPARE(value->currentIndex(), bits & 8 ? 0 : 1);
            field->setCurrentIndex(0); QCOMPARE(value->currentIndex(), bits & 4 ? 0 : 1);
        }
        panel.findChild<QPushButton *>("tvbCopy")->click();
        QVERIFY(QApplication::clipboard()->text().contains("CPU 130")); QVERIFY(QApplication::clipboard()->text().contains("Voltage optimization"));
        QVERIFY(QApplication::clipboard()->text().contains("Ring DownBin"));
        auto *raw = panel.findChild<QLabel *>("tvbRaw");
        QVERIFY(raw->text().contains(hexValue(device->data, 4)));
        // The initially empty label needs Qt's queued layout pass before capture.
        QTRY_VERIFY(raw->height() >= raw->heightForWidth(raw->width()));
        auto *status = panel.findChild<QLabel *>("tvbStatus");
        QTRY_VERIFY(status->height() >= status->heightForWidth(status->width()));
        const auto shot = qEnvironmentVariable("OCTOOL_INTEL_TVB_TEST_SCREENSHOT"); if (!shot.isEmpty()) QVERIFY(panel.grab().save(shot));
        panel.findChild<QLineEdit *>("tvbCpu")->setText("-1");
        QCOMPARE(table->rowCount(), 0); QVERIFY(!panel.findChild<QPushButton *>("tvbApply")->isEnabled());
        const auto calls = device->calls; read->click(); QCOMPARE(device->calls, calls);
        QVERIFY(panel.findChild<QLabel *>("tvbRaw")->text().isEmpty());
        QCOMPARE(device->settingsAttempts(), 0u); QCOMPARE(device->wrongCpu, 0u);
    }
    void intelTvbCancelApplyAndNoop() {
        auto *device = new IntelTvbFixture;
        auto access = std::make_shared<HardwareAccess>(std::unique_ptr<octool::core::HardwareBackend>(device));
        IntelTvbPanel panel(access, nullptr, 130); panel.show();
        panel.findChild<QPushButton *>("tvbRead")->click(); QTRY_VERIFY(panel.isEnabled());
        auto *value = panel.findChild<QComboBox *>("tvbValue"); auto *field = panel.findChild<QComboBox *>("tvbField");
        auto *apply = panel.findChild<QPushButton *>("tvbApply"); value->setCurrentIndex(0);
        const auto calls = device->calls;
        QTimer::singleShot(0, [] { auto *box = qobject_cast<QMessageBox *>(QApplication::activeModalWidget()); if (box) box->button(QMessageBox::Cancel)->click(); });
        apply->click(); QCOMPARE(device->calls, calls); QCOMPARE(device->settingsAttempts(), 0u);
        for (int index = 0; index < 3; ++index) {
            field->setCurrentIndex(index); value->setCurrentIndex(0);
            QTimer::singleShot(0, [] { auto *box = qobject_cast<QMessageBox *>(QApplication::activeModalWidget()); if (box) box->button(QMessageBox::Yes)->click(); });
            apply->click(); QTRY_VERIFY(panel.isEnabled());
            QCOMPARE(device->settingsAttempts(), unsigned(index+1));
            QCOMPARE(device->data, index == 0 ? UINT32_C(0xa5c32114) : index == 1 ? UINT32_C(0xa5c3211c) : UINT32_C(0xa5c3211e));
            QVERIFY(panel.findChild<QLabel *>("tvbStatus")->text().contains("readback verified"));
        }
        QTimer::singleShot(0, [] { auto *box = qobject_cast<QMessageBox *>(QApplication::activeModalWidget()); if (box) box->button(QMessageBox::Yes)->click(); });
        apply->click(); QTRY_VERIFY(panel.isEnabled()); QCOMPARE(device->settingsAttempts(), 3u);
        QVERIFY(panel.findChild<QLabel *>("tvbStatus")->text().contains("no settings write performed"));
        QCOMPARE(device->wrongCpu, 0u);
    }
    void intelRingDownBinEnableAndConfirmationTargetChange() {
        auto *device = new IntelTvbFixture; device->data = UINT32_C(0xa5c3211e);
        auto access = std::make_shared<HardwareAccess>(std::unique_ptr<octool::core::HardwareBackend>(device));
        IntelTvbPanel panel(access, nullptr, 130); panel.show();
        auto *read = panel.findChild<QPushButton *>("tvbRead"); auto *apply = panel.findChild<QPushButton *>("tvbApply");
        auto *value = panel.findChild<QComboBox *>("tvbValue");
        auto *cpu = panel.findChild<QLineEdit *>("tvbCpu");
        read->click(); QTRY_VERIFY(panel.isEnabled());
        panel.findChild<QComboBox *>("tvbField")->setCurrentIndex(2);
        QCOMPARE(value->currentIndex(), 0); value->setCurrentIndex(1);
        bool described = false;
        QTimer::singleShot(0, [&] {
            auto *box = qobject_cast<QMessageBox *>(QApplication::activeModalWidget());
            if (!box) return;
            described = box->text().contains("Ring DownBin: Disabled → Enabled") && box->text().contains("CPU 130");
            box->button(QMessageBox::Yes)->click();
        });
        apply->click(); QTRY_VERIFY(panel.isEnabled()); QVERIFY(described);
        QCOMPARE(device->data, UINT32_C(0xa5c3211c)); QCOMPARE(device->settingsAttempts(), 1u);
        QCOMPARE(value->currentIndex(), 1);
        QCOMPARE(panel.findChild<QTableWidget *>("tvbTable")->item(2,1)->text(), QString("Enabled"));
        const auto calls = device->calls;
        value->setCurrentIndex(0);
        QTimer::singleShot(0, [&] {
            auto *box = qobject_cast<QMessageBox *>(QApplication::activeModalWidget());
            if (!box) return;
            cpu->setText("131"); box->button(QMessageBox::Yes)->click();
        });
        apply->click(); QCOMPARE(device->calls, calls); QCOMPARE(device->settingsAttempts(), 1u);
        QCOMPARE(panel.findChild<QTableWidget *>("tvbTable")->rowCount(), 0);
        QVERIFY(!apply->isEnabled()); QCOMPARE(device->wrongCpu, 0u);
    }
    void intelTvbLockAndFailedReads() {
        auto *device = new IntelTvbFixture;
        auto access = std::make_shared<HardwareAccess>(std::unique_ptr<octool::core::HardwareBackend>(device));
        IntelTvbPanel panel(access, nullptr, 130); panel.show(); auto *read = panel.findChild<QPushButton *>("tvbRead");
        read->click(); QTRY_VERIFY(panel.isEnabled()); device->locked = true;
        read->click(); QTRY_VERIFY(panel.isEnabled()); QCOMPARE(panel.findChild<QTableWidget *>("tvbTable")->rowCount(), 3);
        QVERIFY(!panel.findChild<QPushButton *>("tvbApply")->isEnabled());
        device->failCommand = 0x18; device->status = 5;
        read->click(); QTRY_VERIFY(panel.isEnabled()); QCOMPARE(panel.findChild<QTableWidget *>("tvbTable")->rowCount(), 0);
        QVERIFY(panel.findChild<QLabel *>("tvbStatus")->text().contains("Firmware status: 5"));
        QVERIFY(panel.findChild<QLabel *>("tvbRaw")->text().isEmpty()); QCOMPARE(panel.findChild<QComboBox *>("tvbValue")->currentIndex(), -1);
        device->hypervisor = true; device->clearTrace(); read->click(); QTRY_VERIFY(panel.isEnabled());
        QVERIFY(device->requests.empty());
    }
    void intelTvbStaleAndFailedWrites() {
        for (int field : {0, 2}) for (unsigned mode = 0; mode < 4; ++mode) {
            auto *device = new IntelTvbFixture;
            auto access = std::make_shared<HardwareAccess>(std::unique_ptr<octool::core::HardwareBackend>(device));
            IntelTvbPanel panel(access, nullptr, 130); panel.show();
            panel.findChild<QPushButton *>("tvbRead")->click(); QTRY_VERIFY(panel.isEnabled());
            panel.findChild<QComboBox *>("tvbField")->setCurrentIndex(field);
            panel.findChild<QComboBox *>("tvbValue")->setCurrentIndex(0);
            if (mode == 0) device->data ^= UINT32_C(1) << 31;
            if (mode == 1) device->discardChange = true;
            if (mode == 2) { device->failCommand = 0x19; device->status = 5; }
            if (mode == 3) device->locked = true;
            QTimer::singleShot(0, [] { auto *box = qobject_cast<QMessageBox *>(QApplication::activeModalWidget()); if (box) box->button(QMessageBox::Yes)->click(); });
            panel.findChild<QPushButton *>("tvbApply")->click(); QTRY_VERIFY(panel.isEnabled());
            QCOMPARE(device->settingsAttempts(), mode == 1 || mode == 2 ? 1u : 0u);
            QCOMPARE(panel.findChild<QTableWidget *>("tvbTable")->rowCount(), 0);
            QVERIFY(!panel.findChild<QPushButton *>("tvbApply")->isEnabled());
            QVERIFY(panel.findChild<QLabel *>("tvbRaw")->text().isEmpty());
            QCOMPARE(panel.findChild<QComboBox *>("tvbValue")->currentIndex(), -1);
            QVERIFY(panel.findChild<QLabel *>("tvbStatus")->text().contains(mode == 0 || mode == 3 ? "No settings write" : "not verified"));
        }
    }
    void intelTvbReadLifetimeAndTargetChange() {
        struct Gate { std::atomic<bool> entered{false}, release{false}, destroyed{false}; };
        struct Device : IntelTvbFixture {
            std::shared_ptr<Gate> gate;
            explicit Device(std::shared_ptr<Gate> g) : gate(g) {}
            ~Device() override { gate->destroyed = true; }
        };
        for (bool close : {false,true}) {
            auto gate = std::make_shared<Gate>(); auto *device = new Device(gate);
            device->afterRequest = [gate] {
                if (gate->entered.exchange(true)) return;
                const auto deadline = std::chrono::steady_clock::now()+std::chrono::seconds(3);
                while (!gate->release && std::chrono::steady_clock::now()<deadline) std::this_thread::sleep_for(std::chrono::milliseconds(1));
            };
            auto *panel = new IntelTvbPanel(std::make_shared<HardwareAccess>(std::unique_ptr<octool::core::HardwareBackend>(device)),nullptr,130);
            panel->findChild<QPushButton *>("tvbRead")->click(); QTRY_VERIFY(gate->entered.load());
            if (close) { delete panel; QVERIFY(!gate->destroyed.load()); gate->release = true; QTRY_VERIFY(gate->destroyed.load()); }
            else {
                panel->findChild<QLineEdit *>("tvbCpu")->setText("131"); gate->release = true;
                QTRY_VERIFY(panel->isEnabled()); QCOMPARE(panel->findChild<QTableWidget *>("tvbTable")->rowCount(),0);
                QVERIFY(panel->findChild<QLabel *>("tvbStatus")->text().contains("discarded")); delete panel;
            }
        }
    }
    void intelLimitsReadCopyAndTargetChange() {
        auto *device=new IntelLimitsFixture;
        IntelLimitsPanel panel(std::make_shared<HardwareAccess>(std::unique_ptr<octool::core::HardwareBackend>(device)),nullptr,130); panel.show();
        auto *read=panel.findChild<QPushButton *>("limitsRead"); auto *table=panel.findChild<QTableWidget *>("limitsTable");
        auto *clear=panel.findChild<QPushButton *>("limitsClear");
        read->click(); QTRY_VERIFY(panel.isEnabled()); QCOMPARE(table->rowCount(),11); QVERIFY(clear->isEnabled());
        QCOMPARE(table->item(0,1)->text(),QString("Inactive")); QCOMPARE(table->item(0,2)->text(),QString("Recorded"));
        panel.findChild<QPushButton *>("limitsCopy")->click();
        QVERIFY(QApplication::clipboard()->text().contains(hexValue(device->regs[0x64f],8)));
        QVERIFY(QApplication::clipboard()->text().contains("External PROCHOT"));
        const auto calls=device->cpuCalls;
        panel.findChild<QLineEdit *>("limitsCpu")->setText("-1"); read->click();
        QCOMPARE(device->cpuCalls,calls); QCOMPARE(table->rowCount(),0); QVERIFY(!clear->isEnabled());
        panel.findChild<QLineEdit *>("limitsCpu")->setText("130"); device->failCpuid=int(device->cpuCalls);
        read->click(); QTRY_VERIFY(panel.isEnabled()); QCOMPARE(table->rowCount(),0);
        QVERIFY(panel.findChild<QLabel *>("limitsRaw")->text().isEmpty()); QCOMPARE(device->writes,0u); QCOMPARE(device->wrongCpu,0u);
    }
    void intelLimitsCancelClearNoopAndNewEvents() {
        auto *device=new IntelLimitsFixture;
        IntelLimitsPanel panel(std::make_shared<HardwareAccess>(std::unique_ptr<octool::core::HardwareBackend>(device)),nullptr,130);
        panel.resize(1100,760); panel.show(); panel.findChild<QPushButton *>("limitsRead")->click(); QTRY_VERIFY(panel.isEnabled());
        auto *reason=panel.findChild<QComboBox *>("limitsReason"); reason->setCurrentIndex(reason->findData("prochot"));
        auto *clear=panel.findChild<QPushButton *>("limitsClear"); auto *status=panel.findChild<QLabel *>("limitsStatus");
        const auto calls=device->cpuCalls;
        QTimer::singleShot(0,[] { auto *box=qobject_cast<QMessageBox *>(QApplication::activeModalWidget()); if (box) box->button(QMessageBox::Cancel)->click(); });
        clear->click(); QCOMPARE(device->cpuCalls,calls); QCOMPARE(device->writes,0u);
        const auto original=device->regs[0x64f]; device->newEvents=UINT64_C(1)<<26; bool described=false;
        QTimer::singleShot(0,[&] { auto *box=qobject_cast<QMessageBox *>(QApplication::activeModalWidget()); if (box) {
            described=box->text().contains("CPU 130") && box->text().contains("External PROCHOT"); box->button(QMessageBox::Yes)->click();
        } });
        clear->click(); QTRY_VERIFY(panel.isEnabled()); QVERIFY(described);
        QCOMPARE(device->regs[0x64f],(original & ~(UINT64_C(1)<<16)) | device->newEvents); QCOMPARE(device->writes,1u);
        QVERIFY(status->text().contains("clear in readback"));
        QTRY_VERIFY(status->height()>=status->heightForWidth(status->width())); QVERIFY(status->geometry().bottom()<panel.height());
        const auto screenshot=qEnvironmentVariable("OCTOOL_INTEL_LIMITS_TEST_SCREENSHOT"); if (!screenshot.isEmpty()) QVERIFY(panel.grab().save(screenshot));
        QTimer::singleShot(0,[] { auto *box=qobject_cast<QMessageBox *>(QApplication::activeModalWidget()); if (box) box->button(QMessageBox::Yes)->click(); });
        clear->click(); QTRY_VERIFY(panel.isEnabled()); QCOMPARE(device->writes,1u);
        QVERIFY(status->text().contains("no write performed")); QCOMPARE(device->wrongCpu,0u);
    }
    void intelLimitsConfirmationSelectionChange() {
        for (bool cpu : {false,true}) {
            auto *device=new IntelLimitsFixture;
            IntelLimitsPanel panel(std::make_shared<HardwareAccess>(std::unique_ptr<octool::core::HardwareBackend>(device)),nullptr,130); panel.show();
            panel.findChild<QPushButton *>("limitsRead")->click(); QTRY_VERIFY(panel.isEnabled()); const auto calls=device->cpuCalls;
            QTimer::singleShot(0,[&] { auto *box=qobject_cast<QMessageBox *>(QApplication::activeModalWidget()); if (box) {
                if (cpu) panel.findChild<QLineEdit *>("limitsCpu")->setText("131");
                else panel.findChild<QComboBox *>("limitsReason")->setCurrentIndex(1);
                box->button(QMessageBox::Yes)->click();
            } });
            panel.findChild<QPushButton *>("limitsClear")->click(); QCOMPARE(device->cpuCalls,calls); QCOMPARE(device->writes,0u);
            QVERIFY(panel.findChild<QLabel *>("limitsStatus")->text().contains("Selection changed"));
        }
    }
    void intelLimitsStaleFailedAndReasserted() {
        for (unsigned mode=0;mode<5;++mode) {
            auto *device=new IntelLimitsFixture;
            IntelLimitsPanel panel(std::make_shared<HardwareAccess>(std::unique_ptr<octool::core::HardwareBackend>(device)),nullptr,130); panel.show();
            panel.findChild<QPushButton *>("limitsRead")->click(); QTRY_VERIFY(panel.isEnabled());
            if (mode==0) device->regs[0x64f]^=UINT64_C(1)<<63;
            if (mode==1) device->discardChange=true;
            if (mode==2) device->failWrite=0;
            if (mode==3) device->afterRequest=[device](const octool::core::Request &r) { if (r.write) device->regs[0x64f]^=UINT64_C(1)<<63; };
            if (mode==4) device->reassert=UINT64_C(1)<<16;
            QTimer::singleShot(0,[] { auto *box=qobject_cast<QMessageBox *>(QApplication::activeModalWidget()); if (box) box->button(QMessageBox::Yes)->click(); });
            panel.findChild<QPushButton *>("limitsClear")->click(); QTRY_VERIFY(panel.isEnabled());
            QCOMPARE(device->writes,mode==0?0u:1u);
            const bool recorded=mode==1 || mode==4;
            QCOMPARE(panel.findChild<QTableWidget *>("limitsTable")->rowCount(),recorded?11:0);
            QCOMPARE(panel.findChild<QPushButton *>("limitsClear")->isEnabled(),recorded);
            QVERIFY(panel.findChild<QLabel *>("limitsStatus")->text().contains(recorded?"still set":mode==0?"No history clear":"not verified"));
        }
    }
    void intelLimitsLifetimeAndStaleResults() {
        struct Gate { std::atomic<bool> entered{false}, release{false}, destroyed{false}; };
        struct Device : IntelLimitsFixture {
            std::shared_ptr<Gate> gate;
            explicit Device(std::shared_ptr<Gate> g):gate(g) {}
            ~Device() override { gate->destroyed=true; }
        };
        for (bool writing : {false,true}) for (bool close : {false,true}) {
            auto gate=std::make_shared<Gate>(); auto *device=new Device(gate);
            auto *panel=new IntelLimitsPanel(std::make_shared<HardwareAccess>(std::unique_ptr<octool::core::HardwareBackend>(device)),nullptr,130);
            if (writing) { panel->findChild<QPushButton *>("limitsRead")->click(); QTRY_VERIFY(panel->isEnabled()); }
            device->afterRequest=[gate,writing](const octool::core::Request &r) {
                if ((writing && !r.write) || gate->entered.exchange(true)) return;
                const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(3);
                while (!gate->release && std::chrono::steady_clock::now()<deadline) std::this_thread::sleep_for(std::chrono::milliseconds(1));
            };
            if (writing) {
                QTimer::singleShot(0,[] { auto *box=qobject_cast<QMessageBox *>(QApplication::activeModalWidget()); if (box) box->button(QMessageBox::Yes)->click(); });
                panel->findChild<QPushButton *>("limitsClear")->click();
            } else panel->findChild<QPushButton *>("limitsRead")->click();
            QTRY_VERIFY(gate->entered.load());
            if (close) { delete panel; QVERIFY(!gate->destroyed.load()); gate->release=true; QTRY_VERIFY(gate->destroyed.load()); }
            else {
                panel->findChild<QLineEdit *>("limitsCpu")->setText("131"); gate->release=true;
                QTRY_VERIFY(panel->isEnabled()); QCOMPARE(panel->findChild<QTableWidget *>("limitsTable")->rowCount(),0);
                QVERIFY(panel->findChild<QLabel *>("limitsStatus")->text().contains("discarded"));
                QCOMPARE(device->writes,writing?1u:0u); QCOMPARE(device->wrongCpu,0u); delete panel;
            }
        }
    }
    void intelThermalReadCopyAndTargetChange() {
        auto *device=new IntelThermalFixture;
        IntelThermalPanel panel(std::make_shared<HardwareAccess>(std::unique_ptr<octool::core::HardwareBackend>(device)),nullptr,130); panel.show();
        auto *read=panel.findChild<QPushButton *>("thermalRead"); auto *table=panel.findChild<QTableWidget *>("thermalTable");
        auto *value=panel.findChild<QLineEdit *>("thermalValue"); auto *apply=panel.findChild<QPushButton *>("thermalApply");
        read->click(); QTRY_VERIFY(panel.isEnabled()); QCOMPARE(table->rowCount(),3);
        QCOMPARE(value->text(),QString::number((device->regs[0x1a2]>>24)&63)); QVERIFY(apply->isEnabled());
        panel.findChild<QPushButton *>("thermalCopy")->click();
        QVERIFY(QApplication::clipboard()->text().contains(hexValue(device->regs[0x1a2],8)));
        QVERIFY(QApplication::clipboard()->text().contains("TCC offset below TjMax"));
        const auto calls=device->cpuCalls;
        panel.findChild<QLineEdit *>("thermalCpu")->setText("-1"); read->click();
        QCOMPARE(device->cpuCalls,calls); QCOMPARE(table->rowCount(),0); QVERIFY(value->text().isEmpty()); QVERIFY(!apply->isEnabled());
        panel.findChild<QLineEdit *>("thermalCpu")->setText("130"); device->regs[0x1a2]|=UINT64_C(1)<<31;
        read->click(); QTRY_VERIFY(panel.isEnabled()); QCOMPARE(table->rowCount(),3); QVERIFY(!apply->isEnabled());
        device->regs[0x1a2]&=~(UINT64_C(1)<<31); device->failCpuid=int(device->cpuCalls);
        read->click(); QTRY_VERIFY(panel.isEnabled()); QCOMPARE(table->rowCount(),0); QVERIFY(value->text().isEmpty());
        QVERIFY(panel.findChild<QLabel *>("thermalRaw")->text().isEmpty()); QCOMPARE(device->writes,0u); QCOMPARE(device->wrongCpu,0u);
    }
    void intelThermalValidateCancelApplyAndNoop() {
        auto *device=new IntelThermalFixture;
        IntelThermalPanel panel(std::make_shared<HardwareAccess>(std::unique_ptr<octool::core::HardwareBackend>(device)),nullptr,130);
        panel.resize(1100,640); panel.show(); panel.findChild<QPushButton *>("thermalRead")->click(); QTRY_VERIFY(panel.isEnabled());
        auto *value=panel.findChild<QLineEdit *>("thermalValue"); auto *apply=panel.findChild<QPushButton *>("thermalApply");
        auto *status=panel.findChild<QLabel *>("thermalStatus"); const auto calls=device->cpuCalls;
        for (const QString &invalid : {QString(""),QString("-1"),QString("64"),QString("7.5")}) {
            value->setText(invalid); apply->click(); QCOMPARE(device->cpuCalls,calls);
        }
        value->setText("7");
        QTimer::singleShot(0,[] { auto *box=qobject_cast<QMessageBox *>(QApplication::activeModalWidget()); if (box) box->button(QMessageBox::Cancel)->click(); });
        apply->click(); QCOMPARE(device->cpuCalls,calls); QCOMPARE(device->writes,0u);
        const auto original=device->regs[0x1a2]; bool described=false;
        QTimer::singleShot(0,[&] { auto *box=qobject_cast<QMessageBox *>(QApplication::activeModalWidget()); if (box) {
            described=box->text().contains("CPU 130") && box->text().contains("7 C"); box->button(QMessageBox::Yes)->click();
        } });
        apply->click(); QTRY_VERIFY(panel.isEnabled()); QVERIFY(described);
        QCOMPARE(device->regs[0x1a2],(original & ~(UINT64_C(0x3f)<<24))|(UINT64_C(7)<<24)); QCOMPARE(device->writes,1u);
        QCOMPARE(value->text(),QString("7")); QVERIFY(status->text().contains("readback verified"));
        QTRY_VERIFY(status->height()>=status->heightForWidth(status->width())); QVERIFY(status->geometry().bottom()<panel.height());
        const auto screenshot=qEnvironmentVariable("OCTOOL_INTEL_THERMAL_TEST_SCREENSHOT"); if (!screenshot.isEmpty()) QVERIFY(panel.grab().save(screenshot));
        QTimer::singleShot(0,[] { auto *box=qobject_cast<QMessageBox *>(QApplication::activeModalWidget()); if (box) box->button(QMessageBox::Yes)->click(); });
        apply->click(); QTRY_VERIFY(panel.isEnabled()); QCOMPARE(device->writes,1u);
        QVERIFY(status->text().contains("no settings write performed")); QCOMPARE(device->wrongCpu,0u);
    }
    void intelThermalConfirmationTargetChange() {
        auto *device=new IntelThermalFixture;
        IntelThermalPanel panel(std::make_shared<HardwareAccess>(std::unique_ptr<octool::core::HardwareBackend>(device)),nullptr,130); panel.show();
        panel.findChild<QPushButton *>("thermalRead")->click(); QTRY_VERIFY(panel.isEnabled());
        panel.findChild<QLineEdit *>("thermalValue")->setText("7"); const auto calls=device->cpuCalls;
        QTimer::singleShot(0,[&] { auto *box=qobject_cast<QMessageBox *>(QApplication::activeModalWidget()); if (box) {
            panel.findChild<QLineEdit *>("thermalCpu")->setText("131"); box->button(QMessageBox::Yes)->click();
        } });
        panel.findChild<QPushButton *>("thermalApply")->click(); QCOMPARE(device->cpuCalls,calls); QCOMPARE(device->writes,0u);
        QVERIFY(!panel.findChild<QPushButton *>("thermalApply")->isEnabled()); QCOMPARE(panel.findChild<QTableWidget *>("thermalTable")->rowCount(),0);
    }
    void intelThermalStaleAndFailedWrites() {
        for (unsigned mode=0;mode<4;++mode) {
            auto *device=new IntelThermalFixture;
            IntelThermalPanel panel(std::make_shared<HardwareAccess>(std::unique_ptr<octool::core::HardwareBackend>(device)),nullptr,130); panel.show();
            panel.findChild<QPushButton *>("thermalRead")->click(); QTRY_VERIFY(panel.isEnabled());
            panel.findChild<QLineEdit *>("thermalValue")->setText("7");
            if (mode==0) device->regs[0x1a2] ^= UINT64_C(1)<<63;
            if (mode==1) device->discardChange=true;
            if (mode==2) { device->failWrite=0; }
            if (mode==3) device->afterRequest=[device](const octool::core::Request &r) { if (r.write) device->regs[0x1a2] ^= 1; };
            QTimer::singleShot(0,[] { auto *box=qobject_cast<QMessageBox *>(QApplication::activeModalWidget()); if (box) box->button(QMessageBox::Yes)->click(); });
            panel.findChild<QPushButton *>("thermalApply")->click(); QTRY_VERIFY(panel.isEnabled());
            QCOMPARE(device->writes,mode==0?0u:1u); QCOMPARE(panel.findChild<QTableWidget *>("thermalTable")->rowCount(),0);
            QVERIFY(!panel.findChild<QPushButton *>("thermalApply")->isEnabled()); QVERIFY(panel.findChild<QLabel *>("thermalRaw")->text().isEmpty());
            QVERIFY(panel.findChild<QLineEdit *>("thermalValue")->text().isEmpty());
            QVERIFY(panel.findChild<QLabel *>("thermalStatus")->text().contains(mode==0?"No settings write":"not verified"));
        }
    }
    void intelThermalLifetimeAndStaleResults() {
        struct Gate { std::atomic<bool> entered{false}, release{false}, destroyed{false}; };
        struct Device : IntelThermalFixture {
            std::shared_ptr<Gate> gate;
            explicit Device(std::shared_ptr<Gate> g) : gate(g) {}
            ~Device() override { gate->destroyed=true; }
        };
        for (bool writing : {false,true}) for (bool close : {false,true}) {
            auto gate=std::make_shared<Gate>(); auto *device=new Device(gate);
            auto *panel=new IntelThermalPanel(std::make_shared<HardwareAccess>(std::unique_ptr<octool::core::HardwareBackend>(device)),nullptr,130);
            if (writing) { panel->findChild<QPushButton *>("thermalRead")->click(); QTRY_VERIFY(panel->isEnabled()); }
            device->afterRequest=[gate,writing](const octool::core::Request &r) {
                if ((writing && !r.write) || gate->entered.exchange(true)) return;
                const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(3);
                while (!gate->release && std::chrono::steady_clock::now()<deadline) std::this_thread::sleep_for(std::chrono::milliseconds(1));
            };
            if (writing) {
                panel->findChild<QLineEdit *>("thermalValue")->setText("7");
                QTimer::singleShot(0,[] { auto *box=qobject_cast<QMessageBox *>(QApplication::activeModalWidget()); if (box) box->button(QMessageBox::Yes)->click(); });
                panel->findChild<QPushButton *>("thermalApply")->click();
            } else panel->findChild<QPushButton *>("thermalRead")->click();
            QTRY_VERIFY(gate->entered.load());
            if (close) { delete panel; QVERIFY(!gate->destroyed.load()); gate->release=true; QTRY_VERIFY(gate->destroyed.load()); }
            else {
                panel->findChild<QLineEdit *>("thermalCpu")->setText("131"); gate->release=true;
                QTRY_VERIFY(panel->isEnabled()); QCOMPARE(panel->findChild<QTableWidget *>("thermalTable")->rowCount(),0);
                QVERIFY(panel->findChild<QLabel *>("thermalStatus")->text().contains("discarded"));
                QCOMPARE(device->writes,writing?1u:0u); QCOMPARE(device->wrongCpu,0u); delete panel;
            }
        }
    }
    void intelAvxReadCopyAndTargetChange() {
        auto *device=new IntelAvxFixture;
        IntelAvxPanel panel(std::make_shared<HardwareAccess>(std::unique_ptr<octool::core::HardwareBackend>(device)),nullptr,130); panel.show();
        auto *read=panel.findChild<QPushButton *>("avxRead"); auto *table=panel.findChild<QTableWidget *>("avxTable");
        auto *value=panel.findChild<QLineEdit *>("avxValue"); auto *apply=panel.findChild<QPushButton *>("avxApply");
        read->click(); QTRY_VERIFY(panel.isEnabled()); QCOMPARE(table->rowCount(),1);
        QCOMPARE(value->text(),QString::number((device->data>>5)&31)); QVERIFY(apply->isEnabled());
        panel.findChild<QPushButton *>("avxCopy")->click();
        QVERIFY(QApplication::clipboard()->text().contains(hexValue(device->data,4)));
        QVERIFY(QApplication::clipboard()->text().contains("AVX2 negative ratio offset"));
        const auto calls=device->calls;
        panel.findChild<QLineEdit *>("avxCpu")->setText("-1"); read->click();
        QCOMPARE(device->calls,calls); QCOMPARE(table->rowCount(),0); QVERIFY(value->text().isEmpty()); QVERIFY(!apply->isEnabled());
        panel.findChild<QLineEdit *>("avxCpu")->setText("130"); device->locked=true;
        read->click(); QTRY_VERIFY(panel.isEnabled()); QCOMPARE(table->rowCount(),1); QVERIFY(!apply->isEnabled());
        device->locked=false; device->failAt=int(device->calls);
        read->click(); QTRY_VERIFY(panel.isEnabled()); QCOMPARE(table->rowCount(),0); QVERIFY(value->text().isEmpty());
        QVERIFY(panel.findChild<QLabel *>("avxRaw")->text().isEmpty()); QCOMPARE(device->settingsAttempts(),0u); QCOMPARE(device->wrongCpu,0u);
    }
    void intelAvxValidateCancelApplyAndNoop() {
        auto *device=new IntelAvxFixture;
        IntelAvxPanel panel(std::make_shared<HardwareAccess>(std::unique_ptr<octool::core::HardwareBackend>(device)),nullptr,130);
        panel.resize(1100,640); panel.show(); panel.findChild<QPushButton *>("avxRead")->click(); QTRY_VERIFY(panel.isEnabled());
        auto *value=panel.findChild<QLineEdit *>("avxValue"); auto *apply=panel.findChild<QPushButton *>("avxApply");
        auto *status=panel.findChild<QLabel *>("avxStatus"); const auto calls=device->calls;
        for (const QString &invalid : {QString(""),QString("-1"),QString("32"),QString("7.5")}) {
            value->setText(invalid); apply->click(); QCOMPARE(device->calls,calls);
        }
        value->setText("7");
        QTimer::singleShot(0,[] { auto *box=qobject_cast<QMessageBox *>(QApplication::activeModalWidget()); if (box) box->button(QMessageBox::Cancel)->click(); });
        apply->click(); QCOMPARE(device->calls,calls); QCOMPARE(device->settingsAttempts(),0u);
        const auto original=device->data; bool described=false;
        QTimer::singleShot(0,[&] { auto *box=qobject_cast<QMessageBox *>(QApplication::activeModalWidget()); if (box) {
            described=box->text().contains("CPU 130") && box->text().contains("7 bins"); box->button(QMessageBox::Yes)->click();
        } });
        apply->click(); QTRY_VERIFY(panel.isEnabled()); QVERIFY(described);
        QCOMPARE(device->data,(original & ~0x3e0u)|(7u<<5)); QCOMPARE(device->settingsAttempts(),1u);
        QCOMPARE(value->text(),QString("7")); QVERIFY(status->text().contains("readback verified"));
        QTRY_VERIFY(status->height()>=status->heightForWidth(status->width())); QVERIFY(status->geometry().bottom()<panel.height());
        const auto screenshot=qEnvironmentVariable("OCTOOL_INTEL_AVX_TEST_SCREENSHOT"); if (!screenshot.isEmpty()) QVERIFY(panel.grab().save(screenshot));
        QTimer::singleShot(0,[] { auto *box=qobject_cast<QMessageBox *>(QApplication::activeModalWidget()); if (box) box->button(QMessageBox::Yes)->click(); });
        apply->click(); QTRY_VERIFY(panel.isEnabled()); QCOMPARE(device->settingsAttempts(),1u);
        QVERIFY(status->text().contains("no settings write performed")); QCOMPARE(device->wrongCpu,0u);
    }
    void intelAvxConfirmationTargetChange() {
        auto *device=new IntelAvxFixture;
        IntelAvxPanel panel(std::make_shared<HardwareAccess>(std::unique_ptr<octool::core::HardwareBackend>(device)),nullptr,130); panel.show();
        panel.findChild<QPushButton *>("avxRead")->click(); QTRY_VERIFY(panel.isEnabled());
        panel.findChild<QLineEdit *>("avxValue")->setText("7"); const auto calls=device->calls;
        QTimer::singleShot(0,[&] { auto *box=qobject_cast<QMessageBox *>(QApplication::activeModalWidget()); if (box) {
            panel.findChild<QLineEdit *>("avxCpu")->setText("131"); box->button(QMessageBox::Yes)->click();
        } });
        panel.findChild<QPushButton *>("avxApply")->click(); QCOMPARE(device->calls,calls); QCOMPARE(device->settingsAttempts(),0u);
        QVERIFY(!panel.findChild<QPushButton *>("avxApply")->isEnabled()); QCOMPARE(panel.findChild<QTableWidget *>("avxTable")->rowCount(),0);
    }
    void intelAvxStaleAndFailedWrites() {
        for (unsigned mode=0;mode<4;++mode) {
            auto *device=new IntelAvxFixture;
            IntelAvxPanel panel(std::make_shared<HardwareAccess>(std::unique_ptr<octool::core::HardwareBackend>(device)),nullptr,130); panel.show();
            panel.findChild<QPushButton *>("avxRead")->click(); QTRY_VERIFY(panel.isEnabled());
            panel.findChild<QLineEdit *>("avxValue")->setText("7");
            if (mode==0) device->data ^= 1u<<31;
            if (mode==1) device->discardChange=true;
            if (mode==2) { device->failCommand=0x1b; device->status=255; }
            if (mode==3) device->afterRequest=[device] { const auto &r=device->requests.back(); if (r.write && ((r.value>>32)&255)==0x1b) device->data ^= 1; };
            QTimer::singleShot(0,[] { auto *box=qobject_cast<QMessageBox *>(QApplication::activeModalWidget()); if (box) box->button(QMessageBox::Yes)->click(); });
            panel.findChild<QPushButton *>("avxApply")->click(); QTRY_VERIFY(panel.isEnabled());
            QCOMPARE(device->settingsAttempts(),mode==0?0u:1u); QCOMPARE(panel.findChild<QTableWidget *>("avxTable")->rowCount(),0);
            QVERIFY(!panel.findChild<QPushButton *>("avxApply")->isEnabled()); QVERIFY(panel.findChild<QLabel *>("avxRaw")->text().isEmpty());
            QVERIFY(panel.findChild<QLineEdit *>("avxValue")->text().isEmpty());
            QVERIFY(panel.findChild<QLabel *>("avxStatus")->text().contains(mode==0?"No settings write":"not verified"));
        }
    }
    void intelAvxLifetimeAndStaleResults() {
        struct Gate { std::atomic<bool> entered{false}, release{false}, destroyed{false}; };
        struct Device : IntelAvxFixture {
            std::shared_ptr<Gate> gate;
            explicit Device(std::shared_ptr<Gate> g) : gate(g) {}
            ~Device() override { gate->destroyed=true; }
        };
        for (bool writing : {false,true}) for (bool close : {false,true}) {
            auto gate=std::make_shared<Gate>(); auto *device=new Device(gate);
            auto *panel=new IntelAvxPanel(std::make_shared<HardwareAccess>(std::unique_ptr<octool::core::HardwareBackend>(device)),nullptr,130);
            if (writing) { panel->findChild<QPushButton *>("avxRead")->click(); QTRY_VERIFY(panel->isEnabled()); }
            device->afterRequest=[gate,writing,device] {
                const auto &r=device->requests.back();
                if ((writing && (!r.write || ((r.value>>32)&255)!=0x1b)) || gate->entered.exchange(true)) return;
                const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(3);
                while (!gate->release && std::chrono::steady_clock::now()<deadline) std::this_thread::sleep_for(std::chrono::milliseconds(1));
            };
            if (writing) {
                panel->findChild<QLineEdit *>("avxValue")->setText("7");
                QTimer::singleShot(0,[] { auto *box=qobject_cast<QMessageBox *>(QApplication::activeModalWidget()); if (box) box->button(QMessageBox::Yes)->click(); });
                panel->findChild<QPushButton *>("avxApply")->click();
            } else panel->findChild<QPushButton *>("avxRead")->click();
            QTRY_VERIFY(gate->entered.load());
            if (close) { delete panel; QVERIFY(!gate->destroyed.load()); gate->release=true; QTRY_VERIFY(gate->destroyed.load()); }
            else {
                panel->findChild<QLineEdit *>("avxCpu")->setText("131"); gate->release=true;
                QTRY_VERIFY(panel->isEnabled()); QCOMPARE(panel->findChild<QTableWidget *>("avxTable")->rowCount(),0);
                QVERIFY(panel->findChild<QLabel *>("avxStatus")->text().contains("discarded"));
                QCOMPARE(device->settingsAttempts(),writing?1u:0u); QCOMPARE(device->wrongCpu,0u); delete panel;
            }
        }
    }
    void intelIccMaxLifetimeAndStaleResults() {
        struct Gate { std::atomic<bool> entered{false}, release{false}, destroyed{false}; };
        struct Device : IntelIccMaxFixture {
            std::shared_ptr<Gate> gate;
            explicit Device(std::shared_ptr<Gate> g) : gate(g) {}
            ~Device() override { gate->destroyed=true; }
        };
        for (bool writing : {false,true}) for (bool close : {false,true}) {
            auto gate=std::make_shared<Gate>(); auto *device=new Device(gate);
            auto *panel=new IntelIccMaxPanel(std::make_shared<HardwareAccess>(std::unique_ptr<octool::core::HardwareBackend>(device)),nullptr,130);
            if (writing) { panel->findChild<QPushButton *>("iccMaxRead")->click(); QTRY_VERIFY(panel->isEnabled()); }
            device->afterRequest=[gate,writing,device] {
                const auto &r=device->requests.back();
                if ((writing && (!r.write || ((r.value>>32)&255)!=0x17)) || gate->entered.exchange(true)) return;
                const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(3);
                while (!gate->release && std::chrono::steady_clock::now()<deadline) std::this_thread::sleep_for(std::chrono::milliseconds(1));
            };
            if (writing) {
                panel->findChild<QDoubleSpinBox *>("iccMaxValue")->setValue(125.25);
                QTimer::singleShot(0,[] { auto *box=qobject_cast<QMessageBox *>(QApplication::activeModalWidget()); if (box) box->button(QMessageBox::Yes)->click(); });
                panel->findChild<QPushButton *>("iccMaxApply")->click();
            } else panel->findChild<QPushButton *>("iccMaxRead")->click();
            QTRY_VERIFY(gate->entered.load());
            if (close) { delete panel; QVERIFY(!gate->destroyed.load()); gate->release=true; QTRY_VERIFY(gate->destroyed.load()); }
            else {
                panel->findChild<QLineEdit *>("iccMaxCpu")->setText("131"); gate->release=true;
                QTRY_VERIFY(panel->isEnabled()); QCOMPARE(panel->findChild<QTableWidget *>("iccMaxTable")->rowCount(),0);
                QVERIFY(panel->findChild<QLabel *>("iccMaxStatus")->text().contains("discarded"));
                QCOMPARE(device->settingsAttempts(),writing?1u:0u); QCOMPARE(device->wrongCpu,0u); delete panel;
            }
        }
    }

    void intelIccMaxReadCopyAndTargetChange() {
        auto *d=new IntelIccMaxFixture;
        IntelIccMaxPanel panel(std::make_shared<HardwareAccess>(std::unique_ptr<octool::core::HardwareBackend>(d)),nullptr,130); panel.show();
        auto *read=panel.findChild<QPushButton *>("iccMaxRead"); auto *table=panel.findChild<QTableWidget *>("iccMaxTable");
        QCOMPARE(d->calls,0u); read->click(); QTRY_VERIFY(panel.isEnabled()); QCOMPARE(table->rowCount(),3);
        QCOMPARE(table->item(0,1)->text(),QString("100.00 A")); QCOMPARE(table->item(2,1)->text(),QString("6"));
        panel.findChild<QPushButton *>("iccMaxCopy")->click(); QVERIFY(QApplication::clipboard()->text().contains("shared IA rail"));
        QVERIFY(QApplication::clipboard()->text().contains("100.00 A"));
        panel.findChild<QLineEdit *>("iccMaxCpu")->setText("131"); QCOMPARE(table->rowCount(),0);
        QVERIFY(!panel.findChild<QPushButton *>("iccMaxApply")->isEnabled());
        QVERIFY(panel.findChild<QLabel *>("iccMaxRaw")->text().isEmpty()); QCOMPARE(d->settingsAttempts(),0u); QCOMPARE(d->wrongCpu,0u);
    }
    void intelIccMaxApplyCancelValidationAndNoop() {
        auto *d=new IntelIccMaxFixture;
        IntelIccMaxPanel panel(std::make_shared<HardwareAccess>(std::unique_ptr<octool::core::HardwareBackend>(d)),nullptr,130);
        panel.resize(1150,650); panel.show(); panel.findChild<QPushButton *>("iccMaxRead")->click(); QTRY_VERIFY(panel.isEnabled());
        auto *value=panel.findChild<QDoubleSpinBox *>("iccMaxValue"); auto *apply=panel.findChild<QPushButton *>("iccMaxApply");
        auto *status=panel.findChild<QLabel *>("iccMaxStatus"); const auto calls=d->calls;
        value->setValue(100.1); apply->click(); QCOMPARE(d->calls,calls); QVERIFY(status->text().contains("0.25 A"));
        value->setValue(125.25);
        QTimer::singleShot(0,[] { auto *box=qobject_cast<QMessageBox *>(QApplication::activeModalWidget()); if (box) box->button(QMessageBox::Cancel)->click(); });
        apply->click(); QCOMPARE(d->calls,calls);
        bool described=false;
        QTimer::singleShot(0,[&] { auto *box=qobject_cast<QMessageBox *>(QApplication::activeModalWidget()); if (box) {
            described=box->text().contains("shared IA rail") && box->text().contains("100.00 A → 125.25 A"); box->button(QMessageBox::Yes)->click(); } });
        apply->click(); QTRY_VERIFY(panel.isEnabled()); QVERIFY(described); QCOMPARE(d->settingsAttempts(),1u);
        QCOMPARE(value->value(),125.25); QCOMPARE(d->data,0x25c321f5u); QVERIFY(status->text().contains("readback verified"));
        QTRY_VERIFY(status->height()>=status->heightForWidth(status->width())); QVERIFY(status->geometry().bottom()<panel.height());
        const auto screenshot=qEnvironmentVariable("OCTOOL_INTEL_ICCMAX_TEST_SCREENSHOT"); if (!screenshot.isEmpty()) QVERIFY(panel.grab().save(screenshot));
        QTimer::singleShot(0,[] { auto *box=qobject_cast<QMessageBox *>(QApplication::activeModalWidget()); if (box) box->button(QMessageBox::Yes)->click(); });
        apply->click(); QTRY_VERIFY(panel.isEnabled()); QCOMPARE(d->settingsAttempts(),1u); QVERIFY(status->text().contains("already present"));
    }
    void intelIccMaxReadOnlyAndErrors() {
        auto *d=new IntelIccMaxFixture;
        IntelIccMaxPanel panel(std::make_shared<HardwareAccess>(std::unique_ptr<octool::core::HardwareBackend>(d)),nullptr,130);
        panel.resize(1150,650); panel.show(); auto *read=panel.findChild<QPushButton *>("iccMaxRead");
        auto *apply=panel.findChild<QPushButton *>("iccMaxApply"); auto *table=panel.findChild<QTableWidget *>("iccMaxTable");
        auto *raw=panel.findChild<QLabel *>("iccMaxRaw"); auto *status=panel.findChild<QLabel *>("iccMaxStatus");
        for (unsigned mode=0;mode<6;++mode) {
            d->locked=mode==0; d->data=mode==1?0xa5c32190u:mode==2?0u:0x25c32190u;
            d->topology=mode==3?0x0a061600u:0x0a060600u; d->model=mode==4?0xb7:0x97; d->failAt=mode==5?int(d->calls):-1;
            read->click(); QTRY_VERIFY(panel.isEnabled()); QVERIFY(!apply->isEnabled()); QCOMPARE(table->rowCount(),mode<3?3:0);
            // The worker finishing does not imply Qt has applied the labels'
            // new height-for-width layout. Verify it before capturing pixels.
            if (mode<3) {
                QVERIFY(!raw->text().isEmpty()); QVERIFY(raw->heightForWidth(raw->width())>0);
                QTRY_VERIFY(raw->height()>=raw->heightForWidth(raw->width()));
                QVERIFY(raw->geometry().bottom()<panel.height());
            }
            QTRY_VERIFY(status->height()>=status->heightForWidth(status->width()));
            QVERIFY(status->geometry().bottom()<panel.height());
            if (mode==1) { QCOMPARE(table->item(1,1)->text(),QString("Set / unverified"));
                const auto shot=qEnvironmentVariable("OCTOOL_INTEL_ICCMAX_READONLY_SCREENSHOT"); if (!shot.isEmpty()) QVERIFY(panel.grab().save(shot)); }
            if (mode==5) { QVERIFY(panel.findChild<QLabel *>("iccMaxRaw")->text().isEmpty());
                const auto shot=qEnvironmentVariable("OCTOOL_INTEL_ICCMAX_ERROR_SCREENSHOT"); if (!shot.isEmpty()) QVERIFY(panel.grab().save(shot)); }
        }
        QCOMPARE(d->settingsAttempts(),0u);
    }
    void intelIccMaxConfirmationTargetChange() {
        auto *d=new IntelIccMaxFixture;
        IntelIccMaxPanel panel(std::make_shared<HardwareAccess>(std::unique_ptr<octool::core::HardwareBackend>(d)),nullptr,130); panel.show();
        panel.findChild<QPushButton *>("iccMaxRead")->click(); QTRY_VERIFY(panel.isEnabled()); const auto calls=d->calls;
        panel.findChild<QDoubleSpinBox *>("iccMaxValue")->setValue(125.25);
        QTimer::singleShot(0,[&] { auto *box=qobject_cast<QMessageBox *>(QApplication::activeModalWidget()); if (box) {
            panel.findChild<QLineEdit *>("iccMaxCpu")->setText("131"); box->button(QMessageBox::Yes)->click(); } });
        panel.findChild<QPushButton *>("iccMaxApply")->click(); QCOMPARE(d->calls,calls); QCOMPARE(d->settingsAttempts(),0u);
    }
    void intelIccMaxWriteFailureAndStale() {
        for (unsigned mode=0;mode<3;++mode) {
            auto *d=new IntelIccMaxFixture;
            IntelIccMaxPanel panel(std::make_shared<HardwareAccess>(std::unique_ptr<octool::core::HardwareBackend>(d)),nullptr,130); panel.show();
            panel.findChild<QPushButton *>("iccMaxRead")->click(); QTRY_VERIFY(panel.isEnabled());
            panel.findChild<QDoubleSpinBox *>("iccMaxValue")->setValue(125.25);
            if (mode==0) d->topology^=1;
            if (mode==1) d->discardChange=true;
            if (mode==2) { d->failCommand=0x17; d->status=255; }
            QTimer::singleShot(0,[] { auto *box=qobject_cast<QMessageBox *>(QApplication::activeModalWidget()); if (box) box->button(QMessageBox::Yes)->click(); });
            panel.findChild<QPushButton *>("iccMaxApply")->click(); QTRY_VERIFY(panel.isEnabled());
            QCOMPARE(panel.findChild<QTableWidget *>("iccMaxTable")->rowCount(),0);
            QVERIFY(!panel.findChild<QPushButton *>("iccMaxApply")->isEnabled()); QCOMPARE(d->settingsAttempts(),unsigned(mode!=0));
            QVERIFY(panel.findChild<QLabel *>("iccMaxStatus")->text().contains(mode?"not verified":"No settings write"));
        }
    }
    void intelBclkVoltageReadCopyAndTargetChange() {
        auto *device=new IntelBclkVoltageFixture;
        IntelBclkVoltagePanel panel(std::make_shared<HardwareAccess>(std::unique_ptr<octool::core::HardwareBackend>(device)),nullptr,130); panel.show();
        auto *read=panel.findChild<QPushButton *>("bclkVoltageRead"); auto *table=panel.findChild<QTableWidget *>("bclkVoltageTable");
        auto *value=panel.findChild<QComboBox *>("bclkVoltageValue"); auto *apply=panel.findChild<QPushButton *>("bclkVoltageApply");
        read->click(); QTRY_VERIFY(panel.isEnabled()); QCOMPARE(table->rowCount(),1);
        QCOMPARE(value->currentIndex(),int((device->data>>2)&1)); QVERIFY(apply->isEnabled());
        panel.findChild<QPushButton *>("bclkVoltageCopy")->click();
        QVERIFY(QApplication::clipboard()->text().contains(hexValue(device->data,4)));
        QVERIFY(QApplication::clipboard()->text().contains("BCLK adaptive voltage"));
        const auto calls=device->calls;
        panel.findChild<QLineEdit *>("bclkVoltageCpu")->setText("-1"); read->click();
        QCOMPARE(device->calls,calls); QCOMPARE(table->rowCount(),0); QCOMPARE(value->currentIndex(),-1); QVERIFY(!apply->isEnabled());
        panel.findChild<QLineEdit *>("bclkVoltageCpu")->setText("130"); device->locked=true;
        read->click(); QTRY_VERIFY(panel.isEnabled()); QCOMPARE(table->rowCount(),1); QVERIFY(!apply->isEnabled());
        device->locked=false; device->failAt=int(device->calls);
        read->click(); QTRY_VERIFY(panel.isEnabled()); QCOMPARE(table->rowCount(),0); QCOMPARE(value->currentIndex(),-1);
        QVERIFY(panel.findChild<QLabel *>("bclkVoltageRaw")->text().isEmpty()); QCOMPARE(device->settingsAttempts(),0u); QCOMPARE(device->wrongCpu,0u);
    }
    void intelBclkVoltageValidateCancelApplyAndNoop() {
        auto *device=new IntelBclkVoltageFixture;
        IntelBclkVoltagePanel panel(std::make_shared<HardwareAccess>(std::unique_ptr<octool::core::HardwareBackend>(device)),nullptr,130);
        panel.resize(1100,640); panel.show(); panel.findChild<QPushButton *>("bclkVoltageRead")->click(); QTRY_VERIFY(panel.isEnabled());
        auto *value=panel.findChild<QComboBox *>("bclkVoltageValue"); auto *apply=panel.findChild<QPushButton *>("bclkVoltageApply");
        auto *status=panel.findChild<QLabel *>("bclkVoltageStatus"); const auto calls=device->calls;
        value->setCurrentIndex(-1); apply->click(); QCOMPARE(device->calls,calls);
        value->setCurrentIndex(1);
        QTimer::singleShot(0,[] { auto *box=qobject_cast<QMessageBox *>(QApplication::activeModalWidget()); if (box) box->button(QMessageBox::Cancel)->click(); });
        apply->click(); QCOMPARE(device->calls,calls); QCOMPARE(device->settingsAttempts(),0u);
        const auto original=device->data; bool described=false;
        QTimer::singleShot(0,[&] { auto *box=qobject_cast<QMessageBox *>(QApplication::activeModalWidget()); if (box) {
            described=box->text().contains("CPU 130") && box->text().contains("Disabled → Enabled"); box->button(QMessageBox::Yes)->click();
        } });
        apply->click(); QTRY_VERIFY(panel.isEnabled()); QVERIFY(described);
        QCOMPARE(device->data,original | 4u); QCOMPARE(device->settingsAttempts(),1u);
        QCOMPARE(value->currentIndex(),1); QVERIFY(status->text().contains("readback verified"));
        QTRY_VERIFY(status->height()>=status->heightForWidth(status->width())); QVERIFY(status->geometry().bottom()<panel.height());
        const auto screenshot=qEnvironmentVariable("OCTOOL_INTEL_BCLK_VOLTAGE_TEST_SCREENSHOT"); if (!screenshot.isEmpty()) QVERIFY(panel.grab().save(screenshot));
        QTimer::singleShot(0,[] { auto *box=qobject_cast<QMessageBox *>(QApplication::activeModalWidget()); if (box) box->button(QMessageBox::Yes)->click(); });
        apply->click(); QTRY_VERIFY(panel.isEnabled()); QCOMPARE(device->settingsAttempts(),1u);
        QVERIFY(status->text().contains("no settings write performed")); QCOMPARE(device->wrongCpu,0u);
    }
    void intelBclkVoltageConfirmationTargetChange() {
        auto *device=new IntelBclkVoltageFixture;
        IntelBclkVoltagePanel panel(std::make_shared<HardwareAccess>(std::unique_ptr<octool::core::HardwareBackend>(device)),nullptr,130); panel.show();
        panel.findChild<QPushButton *>("bclkVoltageRead")->click(); QTRY_VERIFY(panel.isEnabled());
        panel.findChild<QComboBox *>("bclkVoltageValue")->setCurrentIndex(1); const auto calls=device->calls;
        QTimer::singleShot(0,[&] { auto *box=qobject_cast<QMessageBox *>(QApplication::activeModalWidget()); if (box) {
            panel.findChild<QLineEdit *>("bclkVoltageCpu")->setText("131"); box->button(QMessageBox::Yes)->click();
        } });
        panel.findChild<QPushButton *>("bclkVoltageApply")->click(); QCOMPARE(device->calls,calls); QCOMPARE(device->settingsAttempts(),0u);
        QVERIFY(!panel.findChild<QPushButton *>("bclkVoltageApply")->isEnabled()); QCOMPARE(panel.findChild<QTableWidget *>("bclkVoltageTable")->rowCount(),0);
    }
    void intelBclkVoltageStaleAndFailedWrites() {
        for (unsigned mode=0;mode<4;++mode) {
            auto *device=new IntelBclkVoltageFixture;
            IntelBclkVoltagePanel panel(std::make_shared<HardwareAccess>(std::unique_ptr<octool::core::HardwareBackend>(device)),nullptr,130); panel.show();
            panel.findChild<QPushButton *>("bclkVoltageRead")->click(); QTRY_VERIFY(panel.isEnabled());
            panel.findChild<QComboBox *>("bclkVoltageValue")->setCurrentIndex(1);
            if (mode==0) device->data ^= 1u<<31;
            if (mode==1) device->discardChange=true;
            if (mode==2) { device->failCommand=0x15; device->status=255; }
            if (mode==3) device->afterRequest=[device] { const auto &r=device->requests.back(); if (r.write && ((r.value>>32)&255)==0x15) device->data ^= 1; };
            QTimer::singleShot(0,[] { auto *box=qobject_cast<QMessageBox *>(QApplication::activeModalWidget()); if (box) box->button(QMessageBox::Yes)->click(); });
            panel.findChild<QPushButton *>("bclkVoltageApply")->click(); QTRY_VERIFY(panel.isEnabled());
            QCOMPARE(device->settingsAttempts(),mode==0?0u:1u); QCOMPARE(panel.findChild<QTableWidget *>("bclkVoltageTable")->rowCount(),0);
            QVERIFY(!panel.findChild<QPushButton *>("bclkVoltageApply")->isEnabled()); QVERIFY(panel.findChild<QLabel *>("bclkVoltageRaw")->text().isEmpty());
            QCOMPARE(panel.findChild<QComboBox *>("bclkVoltageValue")->currentIndex(),-1);
            QVERIFY(panel.findChild<QLabel *>("bclkVoltageStatus")->text().contains(mode==0?"No settings write":"not verified"));
        }
    }
    void intelBclkVoltageLifetimeAndStaleResults() {
        struct Gate { std::atomic<bool> entered{false}, release{false}, destroyed{false}; };
        struct Device : IntelBclkVoltageFixture {
            std::shared_ptr<Gate> gate;
            explicit Device(std::shared_ptr<Gate> g) : gate(g) {}
            ~Device() override { gate->destroyed=true; }
        };
        for (bool writing : {false,true}) for (bool close : {false,true}) {
            auto gate=std::make_shared<Gate>(); auto *device=new Device(gate);
            auto *panel=new IntelBclkVoltagePanel(std::make_shared<HardwareAccess>(std::unique_ptr<octool::core::HardwareBackend>(device)),nullptr,130);
            if (writing) { panel->findChild<QPushButton *>("bclkVoltageRead")->click(); QTRY_VERIFY(panel->isEnabled()); }
            device->afterRequest=[gate,writing,device] {
                const auto &r=device->requests.back();
                if ((writing && (!r.write || ((r.value>>32)&255)!=0x15)) || gate->entered.exchange(true)) return;
                const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(3);
                while (!gate->release && std::chrono::steady_clock::now()<deadline) std::this_thread::sleep_for(std::chrono::milliseconds(1));
            };
            if (writing) {
                panel->findChild<QComboBox *>("bclkVoltageValue")->setCurrentIndex(1);
                QTimer::singleShot(0,[] { auto *box=qobject_cast<QMessageBox *>(QApplication::activeModalWidget()); if (box) box->button(QMessageBox::Yes)->click(); });
                panel->findChild<QPushButton *>("bclkVoltageApply")->click();
            } else panel->findChild<QPushButton *>("bclkVoltageRead")->click();
            QTRY_VERIFY(gate->entered.load());
            if (close) { delete panel; QVERIFY(!gate->destroyed.load()); gate->release=true; QTRY_VERIFY(gate->destroyed.load()); }
            else {
                panel->findChild<QLineEdit *>("bclkVoltageCpu")->setText("131"); gate->release=true;
                QTRY_VERIFY(panel->isEnabled()); QCOMPARE(panel->findChild<QTableWidget *>("bclkVoltageTable")->rowCount(),0);
                QVERIFY(panel->findChild<QLabel *>("bclkVoltageStatus")->text().contains("discarded"));
                QCOMPARE(device->settingsAttempts(),writing?1u:0u); QCOMPARE(device->wrongCpu,0u); delete panel;
            }
        }
    }
    void intelEnergyReadCopyAndTargetChange() {
        auto *device = new IntelEnergyFixture;
        auto access = std::make_shared<HardwareAccess>(std::unique_ptr<octool::core::HardwareBackend>(device));
        IntelEnergyPanel panel(access, nullptr, 130); panel.resize(1100, 640); panel.show();
        auto *read = panel.findChild<QPushButton *>("energyRead"); auto *table = panel.findChild<QTableWidget *>("energyTable");
        auto *field = panel.findChild<QComboBox *>("energyField"); auto *value = panel.findChild<QComboBox *>("energyValue");
        QVERIFY(!panel.findChild<QPushButton *>("energyApply")->isEnabled()); QCOMPARE(value->currentIndex(), -1);
        QCOMPARE(device->cpuCalls, 0u); QCOMPARE(device->reads, 0u);
        for (unsigned bits = 0; bits < 4; ++bits) {
            device->regs[0x1fc] = UINT64_C(0xa5c3211098600243) | std::uint64_t(bits) << 19;
            read->click(); QTRY_VERIFY(panel.isEnabled()); QCOMPARE(table->rowCount(), 2);
            for (int index = 0; index < 2; ++index) {
                const bool enabled = !(bits & (1u << index));
                QCOMPARE(table->item(index,1)->text(), enabled ? QString("Enabled") : QString("Disabled"));
                field->setCurrentIndex(index); QCOMPARE(value->currentIndex(), enabled ? 1 : 0);
            }
        }
        panel.findChild<QPushButton *>("energyCopy")->click();
        const auto copy = QApplication::clipboard()->text();
        QVERIFY(copy.contains("CPU 130") && copy.contains("Energy Efficient Turbo") && copy.contains("Race to Halt"));
        QVERIFY(copy.contains(hexValue(device->regs[0x1fc], 8)));
        auto *raw = panel.findChild<QLabel *>("energyRaw"); auto *status = panel.findChild<QLabel *>("energyStatus");
        QTRY_VERIFY(raw->height() >= raw->heightForWidth(raw->width()));
        QTRY_VERIFY(status->height() >= status->heightForWidth(status->width()));
        const auto shot = qEnvironmentVariable("OCTOOL_INTEL_ENERGY_TEST_SCREENSHOT"); if (!shot.isEmpty()) QVERIFY(panel.grab().save(shot));
        const auto calls = device->cpuCalls;
        panel.findChild<QLineEdit *>("energyCpu")->setText("-1"); read->click();
        QCOMPARE(device->cpuCalls, calls); QCOMPARE(table->rowCount(), 0); QVERIFY(raw->text().isEmpty());
        QVERIFY(!value->isEnabled()); QCOMPARE(value->currentIndex(), -1); QCOMPARE(device->writes, 0u); QCOMPARE(device->wrongCpu, 0u);
    }
    void intelEnergyCancelApplyAndNoop() {
        auto *device = new IntelEnergyFixture;
        IntelEnergyPanel panel(std::make_shared<HardwareAccess>(std::unique_ptr<octool::core::HardwareBackend>(device)),nullptr,130); panel.show();
        panel.findChild<QPushButton *>("energyRead")->click(); QTRY_VERIFY(panel.isEnabled());
        auto *field = panel.findChild<QComboBox *>("energyField"); auto *value = panel.findChild<QComboBox *>("energyValue");
        auto *apply = panel.findChild<QPushButton *>("energyApply"); value->setCurrentIndex(0);
        const auto calls = device->cpuCalls;
        QTimer::singleShot(0, [] { auto *box = qobject_cast<QMessageBox *>(QApplication::activeModalWidget()); if (box) box->button(QMessageBox::Cancel)->click(); });
        apply->click(); QCOMPARE(device->cpuCalls, calls); QCOMPARE(device->writes, 0u);
        unsigned writes = 0;
        for (int index = 0; index < 2; ++index) for (int enabled : {0, 1}) {
            field->setCurrentIndex(index); value->setCurrentIndex(enabled);
            const auto before = device->regs[0x1fc]; const auto mask = UINT64_C(1) << (19+index);
            bool described = false;
            QTimer::singleShot(0, [&] { auto *box = qobject_cast<QMessageBox *>(QApplication::activeModalWidget()); if (box) {
                described = box->text().contains(field->currentText()) && box->text().contains("CPU 130"); box->button(QMessageBox::Yes)->click();
            } });
            apply->click(); QTRY_VERIFY(panel.isEnabled()); QVERIFY(described);
            QCOMPARE(device->writes, ++writes); QCOMPARE(device->regs[0x1fc], enabled ? before & ~mask : before | mask);
            QVERIFY(panel.findChild<QLabel *>("energyStatus")->text().contains("readback verified"));
            QTimer::singleShot(0, [] { auto *box = qobject_cast<QMessageBox *>(QApplication::activeModalWidget()); if (box) box->button(QMessageBox::Yes)->click(); });
            apply->click(); QTRY_VERIFY(panel.isEnabled()); QCOMPARE(device->writes, writes);
            QVERIFY(panel.findChild<QLabel *>("energyStatus")->text().contains("no settings write performed"));
        }
        QCOMPARE(device->wrongCpu, 0u);
    }
    void intelEnergyConfirmationTargetChange() {
        auto *device = new IntelEnergyFixture;
        IntelEnergyPanel panel(std::make_shared<HardwareAccess>(std::unique_ptr<octool::core::HardwareBackend>(device)),nullptr,130); panel.show();
        panel.findChild<QPushButton *>("energyRead")->click(); QTRY_VERIFY(panel.isEnabled());
        panel.findChild<QComboBox *>("energyValue")->setCurrentIndex(0);
        const auto calls = device->cpuCalls;
        QTimer::singleShot(0, [&] { auto *box = qobject_cast<QMessageBox *>(QApplication::activeModalWidget()); if (box) {
            panel.findChild<QLineEdit *>("energyCpu")->setText("131"); box->button(QMessageBox::Yes)->click();
        } });
        panel.findChild<QPushButton *>("energyApply")->click(); QCOMPARE(device->cpuCalls, calls); QCOMPARE(device->writes, 0u);
        QVERIFY(!panel.findChild<QPushButton *>("energyApply")->isEnabled()); QCOMPARE(panel.findChild<QTableWidget *>("energyTable")->rowCount(), 0);
    }
    void intelEnergyFailedReadClearsSnapshot() {
        auto *device = new IntelEnergyFixture;
        IntelEnergyPanel panel(std::make_shared<HardwareAccess>(std::unique_ptr<octool::core::HardwareBackend>(device)),nullptr,130); panel.show();
        auto *read = panel.findChild<QPushButton *>("energyRead"); read->click(); QTRY_VERIFY(panel.isEnabled());
        device->failRead = int(device->reads); read->click(); QTRY_VERIFY(panel.isEnabled());
        QCOMPARE(panel.findChild<QTableWidget *>("energyTable")->rowCount(), 0);
        QVERIFY(!panel.findChild<QPushButton *>("energyApply")->isEnabled());
        QVERIFY(panel.findChild<QLabel *>("energyRaw")->text().isEmpty());
        QCOMPARE(panel.findChild<QComboBox *>("energyValue")->currentIndex(), -1);
        device->hypervisor = true; const auto reads = device->reads; read->click(); QTRY_VERIFY(panel.isEnabled());
        QCOMPARE(device->reads, reads); QCOMPARE(device->writes, 0u);
    }
    void intelEnergyStaleAndFailedWrites() {
        for (int field = 0; field < 2; ++field) for (unsigned mode = 0; mode < 4; ++mode) {
            auto *device = new IntelEnergyFixture;
            IntelEnergyPanel panel(std::make_shared<HardwareAccess>(std::unique_ptr<octool::core::HardwareBackend>(device)),nullptr,130); panel.show();
            panel.findChild<QPushButton *>("energyRead")->click(); QTRY_VERIFY(panel.isEnabled());
            panel.findChild<QComboBox *>("energyField")->setCurrentIndex(field); panel.findChild<QComboBox *>("energyValue")->setCurrentIndex(0);
            if (mode == 0) device->regs[0x1fc] ^= UINT64_C(1) << 63;
            if (mode == 1) device->discardChange = true;
            if (mode == 2) device->failWrite = 0;
            if (mode == 3) device->afterRequest = [device](const octool::core::Request &r) { if (r.write) device->regs[0x1fc] ^= 1; };
            QTimer::singleShot(0, [] { auto *box = qobject_cast<QMessageBox *>(QApplication::activeModalWidget()); if (box) box->button(QMessageBox::Yes)->click(); });
            panel.findChild<QPushButton *>("energyApply")->click(); QTRY_VERIFY(panel.isEnabled());
            QCOMPARE(device->writes, mode == 0 ? 0u : 1u); QCOMPARE(panel.findChild<QTableWidget *>("energyTable")->rowCount(), 0);
            QVERIFY(!panel.findChild<QPushButton *>("energyApply")->isEnabled()); QVERIFY(panel.findChild<QLabel *>("energyRaw")->text().isEmpty());
            QCOMPARE(panel.findChild<QComboBox *>("energyValue")->currentIndex(), -1);
            QVERIFY(panel.findChild<QLabel *>("energyStatus")->text().contains(mode == 0 ? "No settings write" : "not verified"));
        }
    }
    void intelEnergyLifetimeAndStaleResults() {
        struct Gate { std::atomic<bool> entered{false}, release{false}, destroyed{false}; };
        struct Device : IntelEnergyFixture {
            std::shared_ptr<Gate> gate;
            explicit Device(std::shared_ptr<Gate> g) : gate(g) {}
            ~Device() override { gate->destroyed = true; }
        };
        for (bool writing : {false,true}) for (bool close : {false,true}) {
            auto gate = std::make_shared<Gate>(); auto *device = new Device(gate);
            auto *panel = new IntelEnergyPanel(std::make_shared<HardwareAccess>(std::unique_ptr<octool::core::HardwareBackend>(device)),nullptr,130);
            if (writing) { panel->findChild<QPushButton *>("energyRead")->click(); QTRY_VERIFY(panel->isEnabled()); }
            device->afterRequest = [gate,writing](const octool::core::Request &r) {
                if ((writing && !r.write) || gate->entered.exchange(true)) return;
                const auto deadline = std::chrono::steady_clock::now()+std::chrono::seconds(3);
                while (!gate->release && std::chrono::steady_clock::now()<deadline) std::this_thread::sleep_for(std::chrono::milliseconds(1));
            };
            if (writing) {
                panel->findChild<QComboBox *>("energyValue")->setCurrentIndex(0);
                QTimer::singleShot(0, [] { auto *box = qobject_cast<QMessageBox *>(QApplication::activeModalWidget()); if (box) box->button(QMessageBox::Yes)->click(); });
                panel->findChild<QPushButton *>("energyApply")->click();
            } else panel->findChild<QPushButton *>("energyRead")->click();
            QTRY_VERIFY(gate->entered.load());
            if (close) { delete panel; QVERIFY(!gate->destroyed.load()); gate->release = true; QTRY_VERIFY(gate->destroyed.load()); }
            else {
                panel->findChild<QLineEdit *>("energyCpu")->setText("131"); gate->release = true;
                QTRY_VERIFY(panel->isEnabled()); QCOMPARE(panel->findChild<QTableWidget *>("energyTable")->rowCount(),0);
                QVERIFY(panel->findChild<QLabel *>("energyStatus")->text().contains("discarded"));
                QCOMPARE(device->writes, writing ? 1u : 0u); QCOMPARE(device->wrongCpu,0u); delete panel;
            }
        }
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
    void intelPackageThermalEventsAndCopy() {
        auto *device=new IntelPackageThermalFixture;
        IntelControlsPanel panel(std::make_shared<HardwareAccess>(std::unique_ptr<octool::core::HardwareBackend>(device)),nullptr,130);
        panel.resize(1180,1100); panel.show();
        auto *table=panel.findChild<QTableWidget *>("intelTable");
        for (unsigned model : {0xb7,0x8f,0xcf}) {
            device->model=model; device->regs[0x1b1]=UINT64_C(0x8765432180143009);
            panel.findChild<QPushButton *>("intelRead")->click(); QTRY_VERIFY(panel.isEnabled());
            unsigned events=0;
            for (int row=0;row<table->rowCount();++row) {
                if (table->item(row,3)->text()!=hexValue(0x1b1,4) || table->item(row,2)->text()!=QString("0 / 1")) continue;
                const auto expected=events==0?"Active":events==3?"Recorded":events%2?"Not recorded":"Inactive";
                QCOMPARE(table->item(row,1)->text(),QString::fromLatin1(expected));
                QCOMPARE(table->item(row,5)->text(),QString("Read only / locked")); ++events;
            }
            QCOMPARE(events,12u); QCOMPARE(panel.findChild<QComboBox *>("intelField")->count(),model==0xb7?9:8);
            QCOMPARE(device->writes,0u); QCOMPARE(device->wrongCpu,0u);
        }
        for (auto *button:panel.findChildren<QPushButton *>()) if (button->text()==QString("Copy snapshot")) button->click();
        QVERIFY(QApplication::clipboard()->text().contains("Package PROCHOT event history\tRecorded"));
        QVERIFY(QApplication::clipboard()->text().contains("Package power limit active\tInactive"));
        QTest::qWait(20);
        const auto screenshot=qEnvironmentVariable("OCTOOL_INTEL_PACKAGE_EVENTS_SCREENSHOT");
        if (!screenshot.isEmpty()) QVERIFY(panel.grab().save(screenshot));
    }
    void intelPackageThermalErrorsStayIndependentOfTemperature() {
        auto *device=new IntelPackageThermalFixture; device->regs[0x1b1]|=0xfff;
        IntelControlsPanel panel(std::make_shared<HardwareAccess>(std::unique_ptr<octool::core::HardwareBackend>(device)),nullptr,130);
        auto *table=panel.findChild<QTableWidget *>("intelTable");
        for (bool statusFailed : {false,true}) {
            device->failRead=int(device->reads+(statusFailed?4:2));
            panel.findChild<QPushButton *>("intelRead")->click(); QTRY_VERIFY(panel.isEnabled());
            unsigned events=0;
            for (int row=0;row<table->rowCount();++row) {
                if (table->item(row,3)->text()!=hexValue(0x1b1,4) || table->item(row,2)->text()!=QString("0 / 1")) continue;
                QCOMPARE(table->item(row,1)->text(),statusFailed?QString("—"):events%2?QString("Recorded"):QString("Active"));
                if (statusFailed) QCOMPARE(table->item(row,4)->text(),QString("—"));
                ++events;
            }
            QCOMPARE(events,12u); QCOMPARE(device->writes,0u);
        }
    }
    void intelEnergyCounterUnitsCapabilitiesAndReadOnlyAccess() {
        auto *device=new IntelControlsFixture; device->expectedCpu=130;
        IntelControlsPanel panel(std::make_shared<HardwareAccess>(std::unique_ptr<octool::core::HardwareBackend>(device)),nullptr,130);
        panel.resize(1180,1100); panel.show();
        auto *table=panel.findChild<QTableWidget *>("intelTable"); auto *read=panel.findChild<QPushButton *>("intelRead");
        auto find=[&](const QString &name) { for (int i=0;i<table->rowCount();++i) if (table->item(i,0)->text()==name) return i; return -1; };
        for (unsigned model : {0xb7,0x8f,0xcf}) {
            device->model=model; read->click(); QTRY_VERIFY(panel.isEnabled());
            const auto package=find("Package energy counter"); QVERIFY(package>=0);
            QCOMPARE(table->item(package,1)->text(),QString("4")); QCOMPARE(table->item(package,2)->text(),QString("J"));
            QCOMPARE(table->item(package,5)->text(),QString("Read only / locked"));
            const auto domain=find(model==0xb7?"CPU cores energy counter (PP0)":"DRAM energy counter (BIOS mode 0)"); QVERIFY(domain>=0);
            QCOMPARE(table->item(domain,1)->text(),model==0xb7?QString("1"):QString("0.5"));
            QCOMPARE(table->item(find("HWP guaranteed capability"),1)->text(),QString("48"));
            QCOMPARE(table->item(find("HWP most efficient capability"),1)->text(),QString("32"));
            QCOMPARE(panel.findChild<QComboBox *>("intelField")->count(),model==0xb7?14:13); QCOMPARE(device->writes,0u);
            for (auto *button:panel.findChildren<QPushButton *>()) if (button->text()=="Copy snapshot") button->click();
            QVERIFY(QApplication::clipboard()->text().contains("Package energy counter"));
        }
        const auto capture=qEnvironmentVariable("OCTOOL_INTEL_TELEMETRY_TEST_SCREENSHOT"); if (!capture.isEmpty()) QVERIFY(panel.grab().save(capture));
        panel.findChild<QLineEdit *>("intelCpu")->setText("131"); QCOMPARE(table->rowCount(),0); QCOMPARE(device->wrongCpu,0u);
    }
    void intelEnergyCounterFailureClearsValuesAndPreservesIndependentFields() {
        auto *device=new IntelControlsFixture; device->expectedCpu=130;
        IntelControlsPanel panel(std::make_shared<HardwareAccess>(std::unique_ptr<octool::core::HardwareBackend>(device)),nullptr,130); panel.show();
        auto *read=panel.findChild<QPushButton *>("intelRead"); auto *table=panel.findChild<QTableWidget *>("intelTable");
        read->click(); QTRY_VERIFY(panel.isEnabled());
        device->failRead=int(device->reads+6); read->click(); QTRY_VERIFY(panel.isEnabled());
        for (int i=0;i<table->rowCount();++i) if (table->item(i,0)->text()=="Package energy counter") {
            QCOMPARE(table->item(i,1)->text(),QString("—")); QCOMPARE(table->item(i,4)->text(),QString("—"));
            QVERIFY(table->item(i,5)->text().startsWith("Failed"));
        }
        device->failRead=-1; device->virtualized=true; read->click(); QTRY_VERIFY(panel.isEnabled());
        for (int i=0;i<table->rowCount();++i) if (table->item(i,0)->text().contains("energy counter")) QCOMPARE(table->item(i,1)->text(),QString("—"));
        QCOMPARE(device->writes,0u); QCOMPARE(device->wrongCpu,0u);
    }

    void intelHwpRequestsAndPeciPresentation() {
        auto *device=new IntelControlsFixture; device->expectedCpu=130; device->model=0xff; device->enableHwpContext();
        IntelControlsPanel panel(std::make_shared<HardwareAccess>(std::unique_ptr<octool::core::HardwareBackend>(device)),nullptr,130);
        panel.resize(1240,1550); panel.show();
        auto *table=panel.findChild<QTableWidget *>("intelTable");
        panel.findChild<QPushButton *>("intelRead")->click(); QTRY_VERIFY(panel.isEnabled());
        QCOMPARE(table->rowCount(),36);
        auto find=[&](const QString &name) { for (int i=0;i<table->rowCount();++i) if (table->item(i,0)->text()==name) return i; return -1; };
        for (const auto &pair : {qMakePair(QString("HWP package minimum"),QString("16")),
            qMakePair(QString("HWP package activity window (0 = automatic)"),QString("20000")),
            qMakePair(QString("HWP PECI energy preference"),QString("195")), qMakePair(QString("HWP PECI maximum override"),QString("1"))}) {
            const int row=find(pair.first); QVERIFY(row>=0); QCOMPARE(table->item(row,1)->text(),pair.second);
            QCOMPARE(table->item(row,5)->text(),pair.first.startsWith("HWP package")?QString("Editable"):QString("Read only / locked"));
        }
        QCOMPARE(panel.findChild<QComboBox *>("intelField")->count(),6);
        for (auto *button:panel.findChildren<QPushButton *>()) if (button->text()==QString("Copy snapshot")) button->click();
        QVERIFY(QApplication::clipboard()->text().contains("HWP PECI maximum override\t1"));
        QVERIFY(QApplication::clipboard()->text().contains("HWP package activity window (0 = automatic)\t20000\tus"));
        auto *status=panel.findChild<QLabel *>("intelStatus");
        QTRY_VERIFY(status->height()>=status->heightForWidth(status->width()));
        const auto capture=qEnvironmentVariable("OCTOOL_HWP_REQUESTS_TEST_SCREENSHOT"); if (!capture.isEmpty()) QVERIFY(panel.grab().save(capture));
        panel.findChild<QLineEdit *>("intelCpu")->setText("131"); QCOMPARE(table->rowCount(),0);
        QCOMPARE(device->writes,0u); QCOMPARE(device->wrongCpu,0u);
    }
    void intelHwpPackageEditsAndNoop() {
        using octool::core::IntelField;
        const IntelField fields[]={IntelField::HwpPackageMin,IntelField::HwpPackageMax,IntelField::HwpPackageDesired,IntelField::HwpPackageEpp,IntelField::HwpPackageActivityWindow};
        const char *values[]={"24","48","32","192","15333"};
        const unsigned shifts[]={0,8,16,24,32}, codes[]={24,48,32,192,399};
        for (unsigned i=0;i<5;++i) {
            auto *device=new IntelControlsFixture; device->model=0xff; device->expectedCpu=130; device->enableHwpContext(); device->regs[0x775]=0;
            IntelControlsPanel panel(std::make_shared<HardwareAccess>(std::unique_ptr<octool::core::HardwareBackend>(device)),nullptr,130);
            panel.resize(1240,1600); panel.show();
            auto *field=panel.findChild<QComboBox *>("intelField"); auto *value=panel.findChild<QLineEdit *>("intelValue");
            auto *apply=panel.findChild<QPushButton *>("intelApply"); auto *status=panel.findChild<QLabel *>("intelStatus");
            panel.findChild<QPushButton *>("intelRead")->click(); QTRY_VERIFY(panel.isEnabled());
            const auto before=device->regs;
            for (bool noop:{false,true}) {
                QVERIFY(field->findData(int(fields[i]))>=0); field->setCurrentIndex(field->findData(int(fields[i]))); value->setText(values[i]);
                bool described=false;
                QTimer::singleShot(0,[&] { auto *box=qobject_cast<QMessageBox *>(QApplication::activeModalWidget()); if (!box) return;
                    described=box->text().contains("shared package hint") && box->text().contains("other logical CPUs") && box->text().contains("CPU 130");
                    box->button(QMessageBox::Yes)->click(); });
                apply->click(); QTRY_VERIFY(panel.isEnabled()); QVERIFY(described);
                const auto mask=std::uint64_t(i==4?1023:255)<<shifts[i];
                QCOMPARE(device->regs[0x772],(before.at(0x772)&~mask)|(std::uint64_t(codes[i])<<shifts[i]));
                QCOMPARE(device->writes,1u); QCOMPARE(device->wrongCpu,0u);
                QVERIFY(status->text().contains(noop?"Already set":"Applied."));
                for (const auto &r:before) if (r.first!=0x772) QCOMPARE(device->regs.at(r.first),r.second);
            }
            if (i==4) {
                field->setCurrentIndex(field->findData(int(fields[i])));
                QTRY_VERIFY(status->height()>=status->heightForWidth(status->width()));
                const auto capture=qEnvironmentVariable("OCTOOL_HWP_PACKAGE_TEST_SCREENSHOT"); if (!capture.isEmpty()) QVERIFY(panel.grab().save(capture));
            }
        }
    }
    void intelHwpPackageCancelAndChangedCpu() {
        auto *device=new IntelControlsFixture; device->model=0xff; device->enableHwpContext(); device->regs[0x775]=0;
        IntelControlsPanel panel(std::make_shared<HardwareAccess>(std::unique_ptr<octool::core::HardwareBackend>(device)),nullptr,130); panel.show();
        panel.findChild<QPushButton *>("intelRead")->click(); QTRY_VERIFY(panel.isEnabled());
        auto *field=panel.findChild<QComboBox *>("intelField"); field->setCurrentIndex(field->findData(int(octool::core::IntelField::HwpPackageEpp)));
        panel.findChild<QLineEdit *>("intelValue")->setText("192"); const auto reads=device->reads;
        QTimer::singleShot(0,[] { auto *box=qobject_cast<QMessageBox *>(QApplication::activeModalWidget()); if (box) box->button(QMessageBox::Cancel)->click(); });
        panel.findChild<QPushButton *>("intelApply")->click(); QCOMPARE(device->reads,reads); QCOMPARE(device->writes,0u);
        QTimer::singleShot(0,[&] { auto *box=qobject_cast<QMessageBox *>(QApplication::activeModalWidget()); if (!box) return;
            panel.findChild<QLineEdit *>("intelCpu")->setText("131"); box->button(QMessageBox::Yes)->click(); });
        panel.findChild<QPushButton *>("intelApply")->click(); QCOMPARE(device->reads,reads); QCOMPARE(device->writes,0u);
        QCOMPARE(field->count(),0); QVERIFY(panel.findChild<QLabel *>("intelStatus")->text().contains("CPU changed"));
    }
    void intelHwpPackageFailuresClearState() {
        for (unsigned mode=0;mode<3;++mode) {
            auto *device=new IntelControlsFixture; device->model=0xff; device->enableHwpContext(); device->regs[0x775]=0;
            IntelControlsPanel panel(std::make_shared<HardwareAccess>(std::unique_ptr<octool::core::HardwareBackend>(device)),nullptr,130); panel.show();
            panel.findChild<QPushButton *>("intelRead")->click(); QTRY_VERIFY(panel.isEnabled());
            auto *field=panel.findChild<QComboBox *>("intelField"); field->setCurrentIndex(field->findData(int(octool::core::IntelField::HwpPackageEpp)));
            panel.findChild<QLineEdit *>("intelValue")->setText("192");
            if (mode==0) device->regs[0x775]=UINT64_C(1)<<60;
            if (mode==1) device->discardChange=true;
            if (mode==2) device->afterRequest=[&](const octool::core::Request &r) { if (r.write) device->regs[0x774]^=1; };
            QTimer::singleShot(0,[] { auto *box=qobject_cast<QMessageBox *>(QApplication::activeModalWidget()); if (box) box->button(QMessageBox::Yes)->click(); });
            panel.findChild<QPushButton *>("intelApply")->click(); QTRY_VERIFY(panel.isEnabled());
            QCOMPARE(device->writes,mode==0?0u:1u); QCOMPARE(field->count(),0); QCOMPARE(panel.findChild<QTableWidget *>("intelTable")->rowCount(),0);
            QVERIFY(panel.findChild<QLabel *>("intelStatus")->text().contains(mode==0?"No write was attempted":"readback was not verified"));
        }
    }
    void intelHwpRequestsFailureAndCapabilityClearsOldValues() {
        auto *device=new IntelControlsFixture; device->expectedCpu=130; device->enableHwpContext();
        IntelControlsPanel panel(std::make_shared<HardwareAccess>(std::unique_ptr<octool::core::HardwareBackend>(device)),nullptr,130);
        auto *table=panel.findChild<QTableWidget *>("intelTable"); auto *read=panel.findChild<QPushButton *>("intelRead");
        read->click(); QTRY_VERIFY(panel.isEnabled());
        device->failRead=int(device->reads+6); read->click(); QTRY_VERIFY(panel.isEnabled());
        unsigned failed=0;
        for (int row=0;row<table->rowCount();++row) if (table->item(row,3)->text()==hexValue(0x775,4)) {
            ++failed; QCOMPARE(table->item(row,1)->text(),QString("—")); QCOMPARE(table->item(row,4)->text(),QString("—"));
            QVERIFY(table->item(row,5)->text().startsWith("Failed"));
        }
        QCOMPARE(failed,6u); QCOMPARE(panel.findChild<QComboBox *>("intelField")->count(),9);
        device->failRead=-1; device->peci=false; device->packageRequest=false; device->control=false; device->notifications=false;
        read->click(); QTRY_VERIFY(panel.isEnabled());
        for (int row=0;row<table->rowCount();++row) for (unsigned msr : {0x772,0x773,0x775,0x776})
            QVERIFY(table->item(row,3)->text()!=hexValue(msr,4));
        QCOMPARE(panel.findChild<QComboBox *>("intelField")->count(),14);
        QCOMPARE(device->writes,0u); QCOMPARE(device->wrongCpu,0u);
    }

    void intelHwpHistoryReadOnlyAndCopy() {
        auto *device=new IntelControlsFixture; device->expectedCpu=130;
        IntelControlsPanel panel(std::make_shared<HardwareAccess>(std::unique_ptr<octool::core::HardwareBackend>(device)),nullptr,130);
        panel.resize(1180,1100); panel.show();
        auto *table=panel.findChild<QTableWidget *>("intelTable");
        for (std::uint64_t raw : {UINT64_C(0),UINT64_C(1),UINT64_C(4),UINT64_MAX}) {
            device->regs[0x777]=raw;
            panel.findChild<QPushButton *>("intelRead")->click(); QTRY_VERIFY(panel.isEnabled());
            unsigned rows=0;
            for (int row=0;row<table->rowCount();++row) if (table->item(row,3)->text()==hexValue(0x777,4)) {
                QCOMPARE(table->item(row,1)->text(),QString((raw&(rows?4:1))?"Recorded":"Not recorded"));
                QCOMPARE(table->item(row,4)->text(),hexValue(raw,8));
                QCOMPARE(table->item(row,5)->text(),QString("Read only / locked")); ++rows;
            }
            QCOMPARE(rows,2u); QCOMPARE(panel.findChild<QComboBox *>("intelField")->count(),14);
        }
        for (auto *button:panel.findChildren<QPushButton *>()) if (button->text()==QString("Copy snapshot")) button->click();
        QVERIFY(QApplication::clipboard()->text().contains("HWP guaranteed performance change history\tRecorded"));
        QVERIFY(QApplication::clipboard()->text().contains("HWP excursion to minimum history\tRecorded"));
        panel.findChild<QLineEdit *>("intelCpu")->setText("131"); QCOMPARE(table->rowCount(),0);
        QCOMPARE(device->writes,0u); QCOMPARE(device->wrongCpu,0u);
    }
    void intelHwpHistoryReadFailureAndUnsupported() {
        auto *device=new IntelControlsFixture; device->expectedCpu=130;
        IntelControlsPanel panel(std::make_shared<HardwareAccess>(std::unique_ptr<octool::core::HardwareBackend>(device)),nullptr,130);
        auto *read=panel.findChild<QPushButton *>("intelRead"); auto *table=panel.findChild<QTableWidget *>("intelTable");
        read->click(); QTRY_VERIFY(panel.isEnabled());
        device->failRead=int(device->reads+8); read->click(); QTRY_VERIFY(panel.isEnabled());
        unsigned rows=0;
        for (int row=0;row<table->rowCount();++row) if (table->item(row,3)->text()==hexValue(0x777,4)) {
            QCOMPARE(table->item(row,1)->text(),QString("—")); QCOMPARE(table->item(row,4)->text(),QString("—"));
            QVERIFY(table->item(row,5)->text().startsWith("Failed")); ++rows;
        }
        QCOMPARE(rows,2u);
        device->failRead=-1; device->hwp=false; read->click(); QTRY_VERIFY(panel.isEnabled());
        for (int row=0;row<table->rowCount();++row) QVERIFY(table->item(row,3)->text()!=hexValue(0x777,4));
        QCOMPARE(device->writes,0u); QCOMPARE(device->wrongCpu,0u);
    }
    void intelNonTurboRatioIsReadOnlyAndErrorsAreUnavailable() {
        auto *device=new IntelControlsFixture; device->expectedCpu=130;
        IntelControlsPanel panel(std::make_shared<HardwareAccess>(std::unique_ptr<octool::core::HardwareBackend>(device)),nullptr,130); panel.show();
        auto *read=panel.findChild<QPushButton *>("intelRead"); auto *table=panel.findChild<QTableWidget *>("intelTable");
        auto *field=panel.findChild<QComboBox *>("intelField");
        read->click(); QTRY_VERIFY(panel.isEnabled()); const int row=table->rowCount()-1;
        QCOMPARE(table->item(row,0)->text(),QString("Maximum non-turbo ratio"));
        QCOMPARE(table->item(row,1)->text(),QString("42")); QCOMPARE(table->item(row,2)->text(),QString("ratio"));
        QCOMPARE(table->item(row,5)->text(),QString("Read only / locked"));
        QCOMPARE(field->count(),14);
        device->failRead=int(device->reads+5); read->click(); QTRY_VERIFY(panel.isEnabled());
        QCOMPARE(table->item(row,1)->text(),QString("—")); QCOMPARE(table->item(row,4)->text(),QString("—"));
        QVERIFY(table->item(row,5)->text().contains("Failed")); QCOMPARE(device->writes,0u); QCOMPARE(device->wrongCpu,0u);
        // An identity/feature failure must not leave partially read settings editable.
        device->failCpuid=int(device->cpuCalls+2); read->click(); QTRY_VERIFY(panel.isEnabled());
        QCOMPARE(field->count(),0);
    }
    void intelControlsConfirmationTargetChange() {
        auto *device=new IntelControlsFixture; device->expectedCpu=130;
        IntelControlsPanel panel(std::make_shared<HardwareAccess>(std::unique_ptr<octool::core::HardwareBackend>(device)),nullptr,130); panel.show();
        panel.findChild<QPushButton *>("intelRead")->click(); QTRY_VERIFY(panel.isEnabled());
        panel.findChild<QLineEdit *>("intelValue")->setText("100"); const auto calls=device->cpuCalls;
        QTimer::singleShot(0,[&] { auto *box=qobject_cast<QMessageBox *>(QApplication::activeModalWidget()); if (box) {
            panel.findChild<QLineEdit *>("intelCpu")->setText("131"); box->button(QMessageBox::Yes)->click();
        } });
        panel.findChild<QPushButton *>("intelApply")->click(); QCOMPARE(device->cpuCalls,calls); QCOMPARE(device->writes,0u);
        QCOMPARE(panel.findChild<QComboBox *>("intelField")->count(),0);
    }
    void intelControlsLifetimeAndStaleResults() {
        struct Gate { std::atomic<bool> entered{false}, release{false}, destroyed{false}; };
        struct Device : IntelControlsFixture {
            std::shared_ptr<Gate> gate;
            explicit Device(std::shared_ptr<Gate> g):gate(g) { expectedCpu=130; }
            ~Device() override { gate->destroyed=true; }
        };
        for (bool writing : {false,true}) for (bool close : {false,true}) {
            auto gate=std::make_shared<Gate>(); auto *device=new Device(gate);
            auto *panel=new IntelControlsPanel(std::make_shared<HardwareAccess>(std::unique_ptr<octool::core::HardwareBackend>(device)),nullptr,130);
            if (writing) { panel->findChild<QPushButton *>("intelRead")->click(); QTRY_VERIFY(panel->isEnabled()); }
            device->afterRequest=[gate,writing](const octool::core::Request &r) {
                if ((writing && !r.write) || gate->entered.exchange(true)) return;
                const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(3);
                while (!gate->release && std::chrono::steady_clock::now()<deadline) std::this_thread::sleep_for(std::chrono::milliseconds(1));
            };
            if (writing) {
                panel->findChild<QLineEdit *>("intelValue")->setText("100");
                QTimer::singleShot(0,[] { auto *box=qobject_cast<QMessageBox *>(QApplication::activeModalWidget()); if (box) box->button(QMessageBox::Yes)->click(); });
                panel->findChild<QPushButton *>("intelApply")->click();
            } else panel->findChild<QPushButton *>("intelRead")->click();
            QTRY_VERIFY(gate->entered.load());
            if (close) { delete panel; QVERIFY(!gate->destroyed.load()); gate->release=true; QTRY_VERIFY(gate->destroyed.load()); }
            else {
                panel->findChild<QLineEdit *>("intelCpu")->setText("131"); gate->release=true;
                QTRY_VERIFY(panel->isEnabled()); QCOMPARE(panel->findChild<QTableWidget *>("intelTable")->rowCount(),0);
                QCOMPARE(panel->findChild<QComboBox *>("intelField")->count(),0);
                QVERIFY(panel->findChild<QLabel *>("intelStatus")->text().contains("discarded"));
                QCOMPARE(device->writes,writing?1u:0u); QCOMPARE(device->wrongCpu,0u); delete panel;
            }
        }
    }
    void intelPl4ReadbackConfirmationAndNoop() {
        auto *device=new IntelControlsFixture; device->expectedCpu=130;
        IntelControlsPanel panel(std::make_shared<HardwareAccess>(std::unique_ptr<octool::core::HardwareBackend>(device)),nullptr,130);
        panel.resize(1360,1180); panel.show();
        auto *field=panel.findChild<QComboBox *>("intelField"); auto *value=panel.findChild<QLineEdit *>("intelValue");
        auto *apply=panel.findChild<QPushButton *>("intelApply"); auto *status=panel.findChild<QLabel *>("intelStatus");
        panel.findChild<QPushButton *>("intelRead")->click(); QTRY_VERIFY(panel.isEnabled());
        const int pl4=int(octool::core::IntelField::Pl4); QVERIFY(field->findData(pl4)>=0);
        field->setCurrentIndex(field->findData(pl4)); value->setText("100.12"); const auto reads=device->reads;
        QTimer::singleShot(0,[] { auto *box=qobject_cast<QMessageBox *>(QApplication::activeModalWidget()); if (box) box->button(QMessageBox::Cancel)->click(); });
        apply->click(); QCOMPARE(device->reads,reads); QCOMPARE(device->writes,0u);
        for (bool noop : {false,true}) {
            field->setCurrentIndex(field->findData(pl4)); value->setText("100.12");
            QTimer::singleShot(0,[] { auto *box=qobject_cast<QMessageBox *>(QApplication::activeModalWidget()); if (box) box->button(QMessageBox::Yes)->click(); });
            apply->click(); QTRY_VERIFY(panel.isEnabled());
            QCOMPARE(device->writes,1u); QCOMPARE(device->regs[0x601],UINT64_C(0x8765432140006320));
            QVERIFY(status->text().contains(noop?"Already set":"configured value 100"));
        }
        auto *table=panel.findChild<QTableWidget *>("intelTable"); int row=-1;
        for (int i=0;i<table->rowCount();++i) if (table->item(i,0)->text()=="Package PL4 (peak power)") row=i;
        QVERIFY(row>=0); QCOMPARE(table->item(row,1)->text(),QString("100")); QCOMPARE(table->item(row,2)->text(),QString("W"));
        table->scrollToItem(table->item(row,0)); field->setCurrentIndex(field->findData(pl4));
        for (auto *button:panel.findChildren<QPushButton *>()) if (button->text()=="Copy snapshot") button->click();
        QVERIFY(QApplication::clipboard()->text().contains("Package PL4 (peak power)\t100\tW"));
        QTest::qWait(20); const auto capture=qEnvironmentVariable("OCTOOL_INTEL_PL4_TEST_SCREENSHOT"); if (!capture.isEmpty()) QVERIFY(panel.grab().save(capture));
        panel.findChild<QLineEdit *>("intelCpu")->setText("131"); QCOMPARE(table->rowCount(),0); QCOMPARE(device->wrongCpu,0u);
    }
    void intelPl4LockFailureAndUnsupported() {
        auto *device=new IntelControlsFixture; device->expectedCpu=130;
        IntelControlsPanel panel(std::make_shared<HardwareAccess>(std::unique_ptr<octool::core::HardwareBackend>(device)),nullptr,130);
        auto *read=panel.findChild<QPushButton *>("intelRead"); auto *table=panel.findChild<QTableWidget *>("intelTable");
        for (unsigned mode=0;mode<3;++mode) {
            device->regs[0x601]=mode==0?UINT64_C(0x87654321c0006800):UINT64_C(0x8765432140006800);
            device->failRead=mode==1?int(device->reads+9):-1; device->model=mode==2?0x8f:0xb7;
            read->click(); QTRY_VERIFY(panel.isEnabled()); unsigned rows=0;
            for (int row=0;row<table->rowCount();++row) if (table->item(row,0)->text()=="Package PL4 (peak power)") {
                ++rows; QCOMPARE(table->item(row,1)->text(),mode==0?QString("256"):QString("—"));
                if (mode==1) QCOMPARE(table->item(row,4)->text(),QString("—"));
            }
            QCOMPARE(rows,mode==2?0u:1u);
            QCOMPARE(panel.findChild<QComboBox *>("intelField")->findData(int(octool::core::IntelField::Pl4)),-1);
        }
        QCOMPARE(device->writes,0u); QCOMPARE(device->wrongCpu,0u);
    }
    void intelPl4FailedWriteVerificationClearsState() {
        auto *device=new IntelControlsFixture; device->expectedCpu=130; device->discardChange=true;
        IntelControlsPanel panel(std::make_shared<HardwareAccess>(std::unique_ptr<octool::core::HardwareBackend>(device)),nullptr,130); panel.show();
        panel.findChild<QPushButton *>("intelRead")->click(); QTRY_VERIFY(panel.isEnabled());
        auto *field=panel.findChild<QComboBox *>("intelField"); field->setCurrentIndex(field->findData(int(octool::core::IntelField::Pl4)));
        panel.findChild<QLineEdit *>("intelValue")->setText("100");
        QTimer::singleShot(0,[] { auto *box=qobject_cast<QMessageBox *>(QApplication::activeModalWidget()); if (box) box->button(QMessageBox::Yes)->click(); });
        panel.findChild<QPushButton *>("intelApply")->click(); QTRY_VERIFY(panel.isEnabled());
        QCOMPARE(device->writes,1u); QCOMPARE(field->count(),0); QCOMPARE(panel.findChild<QTableWidget *>("intelTable")->rowCount(),0);
        QVERIFY(panel.findChild<QLabel *>("intelStatus")->text().contains("readback was not verified"));
        QCOMPARE(device->wrongCpu,0u);
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
    void curveSelectionWritesAndNoops() {
        auto *device = new AmdCurveFixture; device->writable = true;
        AmdTuningPanel panel(std::make_shared<HardwareAccess>(std::unique_ptr<octool::core::HardwareBackend>(device)), nullptr, 130);
        panel.resize(1100,900); panel.show();
        panel.findChild<QLineEdit *>("curveCcd")->setText("3");
        panel.findChild<QLineEdit *>("curveCore")->setText("0,2,7");
        panel.findChild<QLineEdit *>("curveMargin")->setText("-25");
        auto *read = panel.findChild<QPushButton *>("curveRead"), *apply = panel.findChild<QPushButton *>("curveApply");
        auto *table = panel.findChild<QTableWidget *>("smuTable"); auto *status = panel.findChild<QLabel *>("smuStatus");
        apply->click(); QCOMPARE(device->calls, 0u); QVERIFY(status->text().contains("Read selected"));
        read->click(); QTRY_VERIFY(panel.isEnabled()); QCOMPARE(table->rowCount(), 4);
        auto shot = qEnvironmentVariable("OCTOOL_CURVE_SELECTION_TEST_SCREENSHOT"); if (!shot.isEmpty()) QVERIFY(panel.grab().save(shot));
        const unsigned calls = device->calls;
        QTimer::singleShot(0, [] { auto *box = qobject_cast<QMessageBox *>(QApplication::activeModalWidget()); if (box) box->button(QMessageBox::Cancel)->click(); });
        apply->click(); QCOMPARE(device->calls, calls);
        bool described = false;
        QTimer::singleShot(0, [&] { auto *box = qobject_cast<QMessageBox *>(QApplication::activeModalWidget()); if (box) {
            described = box->text().contains("CCD 3") && box->text().contains("cores 0,2,7") && box->text().contains("-25 (not mV)");
            box->button(QMessageBox::Yes)->click();
        }});
        apply->click(); QTRY_VERIFY(panel.isEnabled()); QVERIFY(described); QCOMPARE(device->settings, 3u);
        QVERIFY(status->text().contains("verified by firmware readback")); QCOMPARE(table->rowCount(), 10);
        QCOMPARE(table->item(1,1)->text(), QString("Readback matched")); QCOMPARE(table->item(3,1)->text(), hexValue(std::uint32_t(-25),4));
        shot = qEnvironmentVariable("OCTOOL_CURVE_WRITE_TEST_SCREENSHOT"); if (!shot.isEmpty()) QVERIFY(panel.grab().save(shot));
        apply->click(); QVERIFY(status->text().contains("Read selected")); QCOMPARE(device->settings, 3u);
        read->click(); QTRY_VERIFY(panel.isEnabled());
        QTimer::singleShot(0, [] { auto *box = qobject_cast<QMessageBox *>(QApplication::activeModalWidget()); if (box) box->button(QMessageBox::Yes)->click(); });
        apply->click(); QTRY_VERIFY(panel.isEnabled()); QCOMPARE(device->settings, 3u);
        QCOMPARE(table->item(1,1)->text(), QString("Unchanged; rechecked"));
    }
    void pboScalarReadApplyAndNoop() {
        auto *device = new AmdPboFixture;
        AmdTuningPanel panel(std::make_shared<HardwareAccess>(std::unique_ptr<octool::core::HardwareBackend>(device)), nullptr, 130);
        panel.resize(1100,900); panel.show();
        auto *read = panel.findChild<QPushButton *>("pboScalarRead"), *apply = panel.findChild<QPushButton *>("pboScalarApply");
        auto *value = panel.findChild<QLineEdit *>("pboScalar"); value->setText("5");
        auto *result = panel.findChild<QTableWidget *>("smuTable"); auto *status = panel.findChild<QLabel *>("smuStatus");
        apply->click(); QCOMPARE(device->calls,0u); QVERIFY(status->text().contains("Read PBO scalar"));
        read->click(); QTRY_VERIFY(panel.isEnabled()); QCOMPARE(result->rowCount(),3); QCOMPARE(result->item(1,1)->text(),QString("1"));
        auto shot = qEnvironmentVariable("OCTOOL_PBO_SCALAR_READ_TEST_SCREENSHOT"); if (!shot.isEmpty()) QVERIFY(panel.grab().save(shot));
        const auto calls = device->calls;
        QTimer::singleShot(0, [] { auto *box=qobject_cast<QMessageBox *>(QApplication::activeModalWidget()); if(box) box->button(QMessageBox::Cancel)->click(); });
        apply->click(); QCOMPARE(device->calls,calls);
        bool described = false;
        QTimer::singleShot(0, [&] { auto *box=qobject_cast<QMessageBox *>(QApplication::activeModalWidget()); if(box) {
            described=box->text().contains("CPU 130") && box->text().contains("from 1 to 5"); box->button(QMessageBox::Yes)->click(); }});
        apply->click(); QTRY_VERIFY(panel.isEnabled()); QVERIFY(described); QCOMPARE(device->settings,1u);
        QCOMPARE(result->rowCount(),5); QCOMPARE(result->item(2,1)->text(),QString("5")); QCOMPARE(result->item(4,1)->text(),QString("Matched"));
        QVERIFY(status->text().contains("firmware readback matched"));
        shot=qEnvironmentVariable("OCTOOL_PBO_SCALAR_WRITE_TEST_SCREENSHOT"); if(!shot.isEmpty()) QVERIFY(panel.grab().save(shot));
        apply->click(); QCOMPARE(device->settings,1u); QVERIFY(status->text().contains("Read PBO scalar"));
        read->click(); QTRY_VERIFY(panel.isEnabled());
        QTimer::singleShot(0, [] { auto *box=qobject_cast<QMessageBox *>(QApplication::activeModalWidget()); if(box) box->button(QMessageBox::Yes)->click(); });
        apply->click(); QTRY_VERIFY(panel.isEnabled()); QCOMPARE(device->settings,1u);
        QCOMPARE(result->item(3,1)->text(),QString("Unchanged; no setting sent"));
    }
    void pboScalarInvalidInputAndConfirmationTargetChange() {
        auto *device = new AmdPboFixture;
        AmdTuningPanel panel(std::make_shared<HardwareAccess>(std::unique_ptr<octool::core::HardwareBackend>(device)), nullptr, 130);
        auto *read=panel.findChild<QPushButton *>("pboScalarRead"), *apply=panel.findChild<QPushButton *>("pboScalarApply");
        auto *value=panel.findChild<QLineEdit *>("pboScalar");
        panel.findChild<QLineEdit *>("smuBus")->setText("01"); read->click(); QCOMPARE(device->calls,0u);
        panel.findChild<QLineEdit *>("smuBus")->setText("00");
        read->click(); QTRY_VERIFY(panel.isEnabled()); const auto calls=device->calls;
        for (const QString text : {QString(""),QString("0"),QString("11"),QString("-1"),QString("1.5"),QString("1e0")}) {
            value->setText(text); apply->click(); QCOMPARE(device->calls,calls);
        }
        value->setText("5");
        QTimer::singleShot(0, [&] { auto *box=qobject_cast<QMessageBox *>(QApplication::activeModalWidget()); if(box) {
            panel.findChild<QLineEdit *>("smuCpu")->setText("131"); box->button(QMessageBox::Yes)->click(); }});
        apply->click(); QCOMPARE(device->calls,calls); QCOMPARE(device->settings,0u);
        QVERIFY(panel.findChild<QLabel *>("smuStatus")->text().contains("Inputs changed"));
        QCOMPARE(panel.findChild<QTableWidget *>("smuTable")->rowCount(),0);
    }
    void pboScalarConflictAndUnverifiedWrite() {
        auto *device = new AmdPboFixture;
        AmdTuningPanel panel(std::make_shared<HardwareAccess>(std::unique_ptr<octool::core::HardwareBackend>(device)), nullptr, 130);
        panel.resize(1100,900); panel.show();
        auto *read=panel.findChild<QPushButton *>("pboScalarRead"), *apply=panel.findChild<QPushButton *>("pboScalarApply");
        panel.findChild<QLineEdit *>("pboScalar")->setText("5");
        read->click(); QTRY_VERIFY(panel.isEnabled()); device->scalarRaw=0x40000000;
        QTimer::singleShot(0, [] { auto *box=qobject_cast<QMessageBox *>(QApplication::activeModalWidget()); if(box) box->button(QMessageBox::Yes)->click(); });
        apply->click(); QTRY_VERIFY(panel.isEnabled()); QCOMPARE(device->settings,0u);
        auto *status=panel.findChild<QLabel *>("smuStatus"); QVERIFY(status->text().contains("Stopped at compare"));
        read->click(); QTRY_VERIFY(panel.isEnabled()); device->discardChange=true;
        QTimer::singleShot(0, [] { auto *box=qobject_cast<QMessageBox *>(QApplication::activeModalWidget()); if(box) box->button(QMessageBox::Yes)->click(); });
        apply->click(); QTRY_VERIFY(panel.isEnabled()); QCOMPARE(device->settings,1u);
        auto *result=panel.findChild<QTableWidget *>("smuTable");
        QCOMPARE(result->item(3,1)->text(),QString("Firmware accepted")); QCOMPARE(result->item(4,1)->text(),QString("Not verified"));
        QVERIFY(status->text().contains("Stopped at verify")); QVERIFY(status->text().contains("may have changed"));
        const auto shot=qEnvironmentVariable("OCTOOL_PBO_SCALAR_FAILURE_TEST_SCREENSHOT"); if(!shot.isEmpty()) QVERIFY(panel.grab().save(shot));
    }
    void pboScalarDropsStaleRead() {
        auto *device = new AmdPboFixture;
        AmdTuningPanel panel(std::make_shared<HardwareAccess>(std::unique_ptr<octool::core::HardwareBackend>(device)), nullptr, 130);
        panel.findChild<QPushButton *>("pboScalarRead")->click();
        panel.findChild<QLineEdit *>("smuCpu")->setText("131");
        QTRY_VERIFY(panel.isEnabled()); QCOMPARE(panel.findChild<QTableWidget *>("smuTable")->rowCount(),0);
        QVERIFY(panel.findChild<QLabel *>("smuStatus")->text().contains("Inputs changed while reading"));
        const auto calls=device->calls;
        panel.findChild<QLineEdit *>("pboScalar")->setText("5"); panel.findChild<QPushButton *>("pboScalarApply")->click();
        QCOMPARE(device->calls,calls); QCOMPARE(device->settings,0u);
    }
    void pboFmaxAcknowledgementOnly() {
        auto *device = new AmdPboFixture; device->discardChange = true;
        AmdTuningPanel panel(std::make_shared<HardwareAccess>(std::unique_ptr<octool::core::HardwareBackend>(device)), nullptr, 130);
        panel.resize(1150,1000); panel.show();
        QApplication::processEvents(); // Settle PBO/PM fixture layout.
        auto *apply = panel.findChild<QPushButton *>("pboFmaxApply");
        auto *input = panel.findChild<QLineEdit *>("pboFmax"); input->setText("5000");
        apply->click(); QCOMPARE(device->calls,0u);
        panel.findChild<QPushButton *>("pboCapsRead")->click(); QTRY_VERIFY(panel.isEnabled());
        QCOMPARE(panel.findChild<QTableWidget *>("smuTable")->item(3,1)->text(),QString("Unavailable: no verified readback"));
        QTimer::singleShot(0, [] { auto *box=qobject_cast<QMessageBox *>(QApplication::activeModalWidget()); if(box) box->button(QMessageBox::Yes)->click(); });
        apply->click(); QTRY_VERIFY(panel.isEnabled());
        QCOMPARE(device->settings,1u); QCOMPARE(device->fmaxMHz,0u);
        auto *table = panel.findChild<QTableWidget *>("smuTable");
        QCOMPARE(table->item(1,1)->text(),QString("Firmware accepted"));
        QCOMPARE(table->item(3,1)->text(),QString("Unavailable / not verified"));
        QVERIFY(panel.findChild<QLabel *>("smuStatus")->text().contains("remain unverified"));
        const auto calls = device->calls; apply->click(); QCOMPARE(device->calls,calls);
        QApplication::processEvents(); // Settle PBO/PM fixture layout.
        const auto shot=qEnvironmentVariable("OCTOOL_PBO_FMAX_TEST_SCREENSHOT"); if(!shot.isEmpty()) QVERIFY(panel.grab().save(shot));
    }
    void pboFmaxRejectsChangedAndInvalidInputs() {
        auto *device = new AmdPboFixture;
        AmdTuningPanel panel(std::make_shared<HardwareAccess>(std::unique_ptr<octool::core::HardwareBackend>(device)), nullptr, 130);
        auto *read=panel.findChild<QPushButton *>("pboCapsRead"), *apply=panel.findChild<QPushButton *>("pboFmaxApply");
        auto *input=panel.findChild<QLineEdit *>("pboFmax");
        read->click(); QTRY_VERIFY(panel.isEnabled()); const auto calls=device->calls;
        for (const QString text : {QString(""),QString("0"),QString("1048576"),QString("-1"),QString("5000.5")}) {
            input->setText(text); apply->click(); QCOMPARE(device->calls,calls);
        }
        input->setText("5000");
        QTimer::singleShot(0, [&] { auto *box=qobject_cast<QMessageBox *>(QApplication::activeModalWidget()); if(box) { input->setText("5100"); box->button(QMessageBox::Yes)->click(); }});
        apply->click(); QCOMPARE(device->calls,calls); QCOMPARE(device->settings,0u);
        read->click(); QTRY_VERIFY(panel.isEnabled()); device->capsRaw=5;
        QTimer::singleShot(0, [] { auto *box=qobject_cast<QMessageBox *>(QApplication::activeModalWidget()); if(box) box->button(QMessageBox::Yes)->click(); });
        apply->click(); QTRY_VERIFY(panel.isEnabled()); QCOMPARE(device->settings,0u);
        QVERIFY(panel.findChild<QLabel *>("smuStatus")->text().contains("No setting sent"));
        read->click(); panel.findChild<QLineEdit *>("smuCpu")->setText("131"); QTRY_VERIFY(panel.isEnabled());
        QCOMPARE(panel.findChild<QTableWidget *>("smuTable")->rowCount(),0);
    }
    void firmwareLn2Readback() {
        auto *d=new AmdPboFixture; d->allowStatusQueries=true;
        AmdTuningPanel panel(std::make_shared<HardwareAccess>(std::unique_ptr<octool::core::HardwareBackend>(d)),nullptr,130);
        panel.resize(1400,1250); panel.show(); QApplication::processEvents();
        auto *read=panel.findChild<QPushButton *>("pboQueryRead");
        QVERIFY(!panel.findChild<QLineEdit *>("pboQueryIndex")->isEnabled());
        for (unsigned n : {0u,1u}) {
            d->ln2Raw=n; read->click(); QTRY_VERIFY(panel.isEnabled());
            auto *table=panel.findChild<QTableWidget *>("smuTable");
            QCOMPARE(table->rowCount(),6); QCOMPARE(table->item(3,1)->text(),n?QString("Enabled"):QString("Disabled"));
            QCOMPARE(d->messages.back(),std::uint32_t(0xa6)); QCOMPARE(d->settings,0u);
        }
        QApplication::processEvents(); const auto shot=qEnvironmentVariable("OCTOOL_PBO_LN2_TEST_SCREENSHOT"); if(!shot.isEmpty()) QVERIFY(panel.grab().save(shot));
    }
    void firmwarePerformanceRawReadback() {
        auto *d=new AmdPboFixture; d->allowStatusQueries=true; d->performanceRaw=0xdeadbeef;
        AmdTuningPanel panel(std::make_shared<HardwareAccess>(std::unique_ptr<octool::core::HardwareBackend>(d)),nullptr,130);
        panel.resize(1400,1250); panel.show(); QApplication::processEvents();
        panel.findChild<QComboBox *>("pboQuery")->setCurrentIndex(1);
        auto *index=panel.findChild<QLineEdit *>("pboQueryIndex"); QVERIFY(index->isEnabled()); index->setText("127");
        panel.findChild<QPushButton *>("pboQueryRead")->click(); QTRY_VERIFY(panel.isEnabled());
        auto *table=panel.findChild<QTableWidget *>("smuTable");
        QCOMPARE(table->item(1,1)->text(),QString("127")); QCOMPARE(table->item(2,1)->text(),QString("0xDEADBEEF"));
        QVERIFY(table->item(3,1)->text().contains("Opaque")); QCOMPARE(table->item(4,1)->text(),QString("Not verified"));
        QCOMPARE(d->commandArgs.back()[0],std::uint32_t(127)); QCOMPARE(d->messages.back(),std::uint32_t(0x5c)); QCOMPARE(d->settings,0u);
        QApplication::processEvents(); const auto shot=qEnvironmentVariable("OCTOOL_PBO_PERFORMANCE_TEST_SCREENSHOT"); if(!shot.isEmpty()) QVERIFY(panel.grab().save(shot));
    }
    void firmwareQueryRejectsInvalidAndFailedReads() {
        auto *d=new AmdPboFixture; d->allowStatusQueries=true;
        AmdTuningPanel panel(std::make_shared<HardwareAccess>(std::unique_ptr<octool::core::HardwareBackend>(d)),nullptr,130);
        panel.resize(1400,1250); panel.show(); QApplication::processEvents();
        auto *kind=panel.findChild<QComboBox *>("pboQuery"); auto *index=panel.findChild<QLineEdit *>("pboQueryIndex");
        auto *read=panel.findChild<QPushButton *>("pboQueryRead"); kind->setCurrentIndex(1);
        for (const QString value : {QString(""),QString("-1"),QString("128"),QString("1.5")}) {
            index->setText(value); read->click(); QCOMPARE(d->calls,0u);
        }
        kind->setCurrentIndex(0); d->completion=0xfe;
        read->click(); QTRY_VERIFY(panel.isEnabled());
        auto *table=panel.findChild<QTableWidget *>("smuTable");
        QCOMPARE(table->item(2,1)->text(),QString("Unavailable")); QCOMPARE(table->item(3,1)->text(),QString("Unknown / not decoded"));
        d->completion=1; d->ln2Raw=2; read->click(); QTRY_VERIFY(panel.isEnabled());
        QCOMPARE(table->item(2,1)->text(),QString("0x00000002")); QCOMPARE(table->item(3,1)->text(),QString("Unknown / not decoded"));
        QCOMPARE(d->settings,0u); QVERIFY(panel.findChild<QLabel *>("smuStatus")->text().contains("No decoded state"));
        QApplication::processEvents(); const auto shot=qEnvironmentVariable("OCTOOL_PBO_QUERY_FAILURE_TEST_SCREENSHOT"); if(!shot.isEmpty()) QVERIFY(panel.grab().save(shot));
    }
    void firmwareQueryDropsChangedInputs() {
        auto *d=new AmdPboFixture; d->allowStatusQueries=true;
        AmdTuningPanel panel(std::make_shared<HardwareAccess>(std::unique_ptr<octool::core::HardwareBackend>(d)),nullptr,130);
        auto *kind=panel.findChild<QComboBox *>("pboQuery"); auto *read=panel.findChild<QPushButton *>("pboQueryRead");
        auto *index=panel.findChild<QLineEdit *>("pboQueryIndex");
        read->click(); kind->setCurrentIndex(1); QTRY_VERIFY(panel.isEnabled());
        QCOMPARE(panel.findChild<QTableWidget *>("smuTable")->rowCount(),0);
        QVERIFY(panel.findChild<QLabel *>("smuStatus")->text().contains("Inputs changed"));
        index->setText("1"); read->click(); index->setText("2"); QTRY_VERIFY(panel.isEnabled());
        QCOMPARE(panel.findChild<QTableWidget *>("smuTable")->rowCount(),0);
        read->click(); panel.findChild<QLineEdit *>("smuCpu")->setText("131"); QTRY_VERIFY(panel.isEnabled());
        QCOMPARE(panel.findChild<QTableWidget *>("smuTable")->rowCount(),0); QCOMPARE(d->settings,0u);
    }
    void allCoreCurveAcknowledgementOnly() {
        auto *device = new AmdPboFixture; device->capsRaw=6; device->allowAllCoreCurve=true; device->discardChange=true;
        AmdTuningPanel panel(std::make_shared<HardwareAccess>(std::unique_ptr<octool::core::HardwareBackend>(device)),nullptr,130);
        panel.resize(1200,1200); panel.show(); QApplication::processEvents();
        auto *read=panel.findChild<QPushButton *>("allCurveCapsRead"), *apply=panel.findChild<QPushButton *>("allCurveApply");
        auto *input=panel.findChild<QLineEdit *>("allCurveMargin"); input->setText("-25");
        apply->click(); QCOMPARE(device->calls,0u);
        for (int margin : {-25,0,50}) {
            input->setText(QString::number(margin)); read->click(); QTRY_VERIFY(panel.isEnabled());
            QTimer::singleShot(0, [] { auto *box=qobject_cast<QMessageBox *>(QApplication::activeModalWidget()); if(box) box->button(QMessageBox::Yes)->click(); });
            apply->click(); QTRY_VERIFY(panel.isEnabled());
            QCOMPARE(device->messages.back(),std::uint32_t(7)); QCOMPARE(device->commandArgs.back()[0],unsigned(margin < 0 ? 65536+margin : margin));
            QCOMPARE(device->allCurveArgument,UINT32_MAX);
            auto *table=panel.findChild<QTableWidget *>("smuTable");
            QCOMPARE(table->item(2,1)->text(),QString("Firmware accepted"));
            QCOMPARE(table->item(4,1)->text(),QString("Unavailable / not verified"));
            QVERIFY(panel.findChild<QLabel *>("smuStatus")->text().contains("Per-core values remain unverified"));
            if (margin == -25) { QApplication::processEvents(); const auto shot=qEnvironmentVariable("OCTOOL_CURVE_ALL_TEST_SCREENSHOT"); if(!shot.isEmpty()) QVERIFY(panel.grab().save(shot)); }
            const auto calls=device->calls; apply->click(); QCOMPARE(device->calls,calls);
        }
        QCOMPARE(device->settings,3u); QCOMPARE(device->wrongCpu,0u);
    }
    void allCoreCurveRejectsInvalidAndChangedInputs() {
        auto *device=new AmdPboFixture; device->capsRaw=6; device->allowAllCoreCurve=true;
        AmdTuningPanel panel(std::make_shared<HardwareAccess>(std::unique_ptr<octool::core::HardwareBackend>(device)),nullptr,130);
        auto *read=panel.findChild<QPushButton *>("allCurveCapsRead"), *apply=panel.findChild<QPushButton *>("allCurveApply");
        auto *input=panel.findChild<QLineEdit *>("allCurveMargin");
        read->click(); QTRY_VERIFY(panel.isEnabled()); const auto calls=device->calls;
        for (const QString text : {QString(""),QString("-51"),QString("51"),QString("-1.5"),QString("0x10")}) {
            input->setText(text); apply->click(); QCOMPARE(device->calls,calls);
        }
        for (unsigned mode=0; mode<3; ++mode) {
            panel.findChild<QLineEdit *>("smuCpu")->setText("130"); input->setText("-25");
            read->click(); QTRY_VERIFY(panel.isEnabled()); const auto previous=device->calls;
            QTimer::singleShot(0, [&] {
                auto *box=qobject_cast<QMessageBox *>(QApplication::activeModalWidget()); if(!box) return;
                if(mode==0) input->setText("-24");
                if(mode==1) panel.findChild<QLineEdit *>("smuCpu")->setText("131");
                box->button(mode==2?QMessageBox::Cancel:QMessageBox::Yes)->click();
            });
            apply->click(); QCOMPARE(device->calls,previous); QCOMPARE(device->settings,0u);
        }
        device->capsRaw=7; read->click(); QTRY_VERIFY(panel.isEnabled()); const auto unavailable=device->calls;
        apply->click(); QCOMPARE(device->calls,unavailable);
    }
    void allCoreCurveRejectsStaleCapsAndFirmware() {
        auto *device=new AmdPboFixture; device->capsRaw=6; device->allowAllCoreCurve=true;
        AmdTuningPanel panel(std::make_shared<HardwareAccess>(std::unique_ptr<octool::core::HardwareBackend>(device)),nullptr,130);
        panel.resize(1200,1200); panel.show(); QApplication::processEvents();
        auto *read=panel.findChild<QPushButton *>("allCurveCapsRead"), *apply=panel.findChild<QPushButton *>("allCurveApply");
        panel.findChild<QLineEdit *>("allCurveMargin")->setText("-25");
        read->click(); QTRY_VERIFY(panel.isEnabled()); device->capsRaw=4;
        QTimer::singleShot(0, [] { auto *box=qobject_cast<QMessageBox *>(QApplication::activeModalWidget()); if(box) box->button(QMessageBox::Yes)->click(); });
        apply->click(); QTRY_VERIFY(panel.isEnabled()); QCOMPARE(device->settings,0u);
        QVERIFY(panel.findChild<QLabel *>("smuStatus")->text().contains("No setting sent"));
        read->click(); QTRY_VERIFY(panel.isEnabled()); device->writeCompletion=0xfd;
        QTimer::singleShot(0, [] { auto *box=qobject_cast<QMessageBox *>(QApplication::activeModalWidget()); if(box) box->button(QMessageBox::Yes)->click(); });
        apply->click(); QTRY_VERIFY(panel.isEnabled()); QCOMPARE(device->settings,1u);
        QCOMPARE(panel.findChild<QTableWidget *>("smuTable")->item(2,1)->text(),QString("Attempted; not confirmed"));
        QVERIFY(panel.findChild<QLabel *>("smuStatus")->text().contains("may have changed"));
        QApplication::processEvents(); const auto shot=qEnvironmentVariable("OCTOOL_CURVE_ALL_FAILURE_TEST_SCREENSHOT"); if(!shot.isEmpty()) QVERIFY(panel.grab().save(shot));
    }
    void allCoreCurveDropsStaleCompletion() {
        auto *device=new AmdPboFixture; device->capsRaw=6; device->allowAllCoreCurve=true;
        AmdTuningPanel panel(std::make_shared<HardwareAccess>(std::unique_ptr<octool::core::HardwareBackend>(device)),nullptr,130);
        auto *read=panel.findChild<QPushButton *>("allCurveCapsRead"), *apply=panel.findChild<QPushButton *>("allCurveApply");
        panel.findChild<QLineEdit *>("allCurveMargin")->setText("-25");
        read->click(); QTRY_VERIFY(panel.isEnabled());
        QTimer::singleShot(0, [] { auto *box=qobject_cast<QMessageBox *>(QApplication::activeModalWidget()); if(box) box->button(QMessageBox::Yes)->click(); });
        apply->click(); panel.findChild<QLineEdit *>("smuCpu")->setText("131"); QTRY_VERIFY(panel.isEnabled());
        QCOMPARE(panel.findChild<QTableWidget *>("smuTable")->rowCount(),0);
        QVERIFY(panel.findChild<QLabel *>("smuStatus")->text().contains("result belongs to CPU 130"));
        const auto calls=device->calls; apply->click(); QCOMPARE(device->calls,calls);
    }
    void pboLimitsAcknowledgementOnly() {
        auto *device = new AmdPboFixture; device->capsRaw = 6; device->discardChange = true;
        AmdTuningPanel panel(std::make_shared<HardwareAccess>(std::unique_ptr<octool::core::HardwareBackend>(device)), nullptr, 130);
        panel.resize(1200,1100); panel.show(); QApplication::processEvents();
        auto *apply = panel.findChild<QPushButton *>("pboLimitApply");
        auto *kind = panel.findChild<QComboBox *>("pboLimit");
        auto *input = panel.findChild<QLineEdit *>("pboLimitValue");
        apply->click(); QCOMPARE(device->calls,0u);
        const unsigned values[] = {350,280,500,90};
        for (unsigned i = 0; i < 4; ++i) {
            kind->setCurrentIndex(int(i)); input->setText(QString::number(values[i]));
            panel.findChild<QPushButton *>("pboCapsRead")->click(); QTRY_VERIFY(panel.isEnabled());
            QCOMPARE(panel.findChild<QTableWidget *>("smuTable")->item(4,1)->text(),QString("Yes"));
            QTimer::singleShot(0, [] { auto *box=qobject_cast<QMessageBox *>(QApplication::activeModalWidget()); if(box) box->button(QMessageBox::Yes)->click(); });
            apply->click(); QTRY_VERIFY(panel.isEnabled());
            QCOMPARE(device->settings,i+1); QCOMPARE(device->messages.back(),std::uint32_t(0x56+i));
            QCOMPARE(device->commandArgs.back()[0],i == 3 ? 90u : values[i]*1000);
            QCOMPARE(device->limitArguments[i],0u);
            auto *table = panel.findChild<QTableWidget *>("smuTable");
            QCOMPARE(table->item(1,1)->text(),QString("Firmware accepted"));
            QCOMPARE(table->item(3,1)->text(),QString("Unavailable / not verified"));
            QVERIFY(panel.findChild<QLabel *>("smuStatus")->text().contains("effective limit remains unverified"));
            if (i == 0 || i == 3) {
                QApplication::processEvents();
                const auto shot=qEnvironmentVariable(i == 0 ? "OCTOOL_PBO_LIMIT_PPT_TEST_SCREENSHOT" : "OCTOOL_PBO_LIMIT_TEMP_TEST_SCREENSHOT");
                if(!shot.isEmpty()) QVERIFY(panel.grab().save(shot));
            }
            const auto calls = device->calls; apply->click(); QCOMPARE(device->calls,calls);
        }
        QCOMPARE(device->wrongCpu,0u);
    }
    void pboLimitsRejectInvalidInputsAndCapabilities() {
        auto *device = new AmdPboFixture; device->capsRaw = 6;
        AmdTuningPanel panel(std::make_shared<HardwareAccess>(std::unique_ptr<octool::core::HardwareBackend>(device)), nullptr, 130);
        auto *read=panel.findChild<QPushButton *>("pboCapsRead"), *apply=panel.findChild<QPushButton *>("pboLimitApply");
        auto *input=panel.findChild<QLineEdit *>("pboLimitValue");
        read->click(); QTRY_VERIFY(panel.isEnabled()); const auto calls=device->calls;
        for (const QString text : {QString(""),QString("0"),QString("2001"),QString("-1"),QString("350.5"),QString("0x15e")}) {
            input->setText(text); apply->click(); QCOMPARE(device->calls,calls);
        }
        panel.findChild<QComboBox *>("pboLimit")->setCurrentIndex(3); input->setText("101"); apply->click(); QCOMPARE(device->calls,calls);
        device->capsRaw=4; read->click(); QTRY_VERIFY(panel.isEnabled()); const auto unavailable=device->calls;
        input->setText("90"); apply->click(); QCOMPARE(device->calls,unavailable); QCOMPARE(device->settings,0u);
        QVERIFY(panel.findChild<QLabel *>("smuStatus")->text().contains("power limits available"));
    }
    void pboLimitsRejectConfirmationChanges() {
        auto *device = new AmdPboFixture; device->capsRaw = 6;
        AmdTuningPanel panel(std::make_shared<HardwareAccess>(std::unique_ptr<octool::core::HardwareBackend>(device)), nullptr, 130);
        auto *read=panel.findChild<QPushButton *>("pboCapsRead"), *apply=panel.findChild<QPushButton *>("pboLimitApply");
        auto *input=panel.findChild<QLineEdit *>("pboLimitValue"); auto *kind=panel.findChild<QComboBox *>("pboLimit");
        for (unsigned mode = 0; mode < 4; ++mode) {
            panel.findChild<QLineEdit *>("smuCpu")->setText("130"); kind->setCurrentIndex(0); input->setText("350");
            read->click(); QTRY_VERIFY(panel.isEnabled()); const auto calls=device->calls;
            QTimer::singleShot(0, [&] {
                auto *box=qobject_cast<QMessageBox *>(QApplication::activeModalWidget()); if(!box) return;
                if (mode == 0) input->setText("351");
                if (mode == 1) { kind->setCurrentIndex(1); input->setText("350"); }
                if (mode == 2) panel.findChild<QLineEdit *>("smuCpu")->setText("131");
                box->button(mode == 3 ? QMessageBox::Cancel : QMessageBox::Yes)->click();
            });
            apply->click(); QCOMPARE(device->calls,calls); QCOMPARE(device->settings,0u);
        }
    }
    void pboLimitsRejectStaleCapabilitiesAndFirmware() {
        auto *device = new AmdPboFixture; device->capsRaw = 6;
        AmdTuningPanel panel(std::make_shared<HardwareAccess>(std::unique_ptr<octool::core::HardwareBackend>(device)), nullptr, 130);
        panel.resize(1200,1100); panel.show(); QApplication::processEvents();
        auto *read=panel.findChild<QPushButton *>("pboCapsRead"), *apply=panel.findChild<QPushButton *>("pboLimitApply");
        panel.findChild<QLineEdit *>("pboLimitValue")->setText("350");
        read->click(); QTRY_VERIFY(panel.isEnabled()); device->capsRaw=2;
        QTimer::singleShot(0, [] { auto *box=qobject_cast<QMessageBox *>(QApplication::activeModalWidget()); if(box) box->button(QMessageBox::Yes)->click(); });
        apply->click(); QTRY_VERIFY(panel.isEnabled()); QCOMPARE(device->settings,0u);
        QVERIFY(panel.findChild<QLabel *>("smuStatus")->text().contains("No setting sent"));
        read->click(); QTRY_VERIFY(panel.isEnabled()); device->writeCompletion=0xfd;
        QTimer::singleShot(0, [] { auto *box=qobject_cast<QMessageBox *>(QApplication::activeModalWidget()); if(box) box->button(QMessageBox::Yes)->click(); });
        apply->click(); QTRY_VERIFY(panel.isEnabled()); QCOMPARE(device->settings,1u);
        QCOMPARE(panel.findChild<QTableWidget *>("smuTable")->item(1,1)->text(),QString("Attempted; not confirmed"));
        QVERIFY(panel.findChild<QLabel *>("smuStatus")->text().contains("may have changed"));
        QApplication::processEvents();
        const auto shot=qEnvironmentVariable("OCTOOL_PBO_LIMIT_FAILURE_TEST_SCREENSHOT"); if(!shot.isEmpty()) QVERIFY(panel.grab().save(shot));
        read->click(); panel.findChild<QLineEdit *>("smuCpu")->setText("131"); QTRY_VERIFY(panel.isEnabled());
        QCOMPARE(panel.findChild<QTableWidget *>("smuTable")->rowCount(),0);
        const auto calls=device->calls; apply->click(); QCOMPARE(device->calls,calls);
    }
    void ocModeChecksReadback() {
        auto *device=new AmdPboFixture;device->allowManualOc=true;device->capsRaw=6;
        AmdTuningPanel panel(std::make_shared<HardwareAccess>(std::unique_ptr<octool::core::HardwareBackend>(device)),nullptr,130);
        panel.resize(1300,1200);panel.show();QApplication::processEvents();
        auto *read=panel.findChild<QPushButton *>("ocCapsRead"),*apply=panel.findChild<QPushButton *>("ocModeApply");
        auto *mode=panel.findChild<QComboBox *>("ocMode");auto *table=panel.findChild<QTableWidget *>("smuTable");
        apply->click();QCOMPARE(device->calls,0u);
        for(unsigned i=0;i<3;++i) {
            mode->setCurrentIndex(i==0?1:0);
            read->click();QTRY_VERIFY(panel.isEnabled());
            QTimer::singleShot(0,[]{auto *box=qobject_cast<QMessageBox *>(QApplication::activeModalWidget());if(box)box->button(QMessageBox::Yes)->click();});
            apply->click();QTRY_VERIFY(panel.isEnabled());
            QCOMPARE(device->capsRaw,i==0?7u:6u);QCOMPARE(device->settings,i==0?1u:2u);
            QCOMPARE(table->item(2,1)->text(),i==0?QString("Enabled"):QString("Disabled"));
            QCOMPARE(table->item(3,1)->text(),QString("Mode bit matches"));
            if(i==2)QCOMPARE(table->item(1,1)->text(),QString("Not sent; already requested mode"));
            if(i==0) {
                QApplication::processEvents();const auto shot=qEnvironmentVariable("OCTOOL_OC_MODE_TEST_SCREENSHOT");
                if(!shot.isEmpty())QVERIFY(panel.grab().save(shot));
            }
            const auto calls=device->calls;apply->click();QCOMPARE(device->calls,calls);
        }
        QCOMPARE(device->scalarRaw,0x3f800000u);QCOMPARE(device->ocAllMHz,0u);QCOMPARE(device->ocCoreArgument,0u);QCOMPARE(device->wrongCpu,0u);
    }
    void ocModeRejectsUnverifiedResult() {
        auto *device=new AmdPboFixture;device->allowManualOc=true;device->capsRaw=6;device->discardChange=true;
        AmdTuningPanel panel(std::make_shared<HardwareAccess>(std::unique_ptr<octool::core::HardwareBackend>(device)),nullptr,130);
        panel.resize(1300,1200);panel.show();QApplication::processEvents();
        panel.findChild<QComboBox *>("ocMode")->setCurrentIndex(1);
        panel.findChild<QPushButton *>("ocCapsRead")->click();QTRY_VERIFY(panel.isEnabled());
        QTimer::singleShot(0,[]{auto *box=qobject_cast<QMessageBox *>(QApplication::activeModalWidget());if(box)box->button(QMessageBox::Yes)->click();});
        panel.findChild<QPushButton *>("ocModeApply")->click();QTRY_VERIFY(panel.isEnabled());
        auto *table=panel.findChild<QTableWidget *>("smuTable");
        QCOMPARE(device->settings,1u);QCOMPARE(table->item(1,1)->text(),QString("Firmware accepted"));
        QCOMPARE(table->item(2,1)->text(),QString("Disabled"));QCOMPARE(table->item(3,1)->text(),QString("Not verified"));
        QVERIFY(panel.findChild<QLabel *>("smuStatus")->text().contains("may have changed"));
        QApplication::processEvents();const auto shot=qEnvironmentVariable("OCTOOL_OC_FAILURE_TEST_SCREENSHOT");
        if(!shot.isEmpty())QVERIFY(panel.grab().save(shot));
    }
    void ocFrequencyAcknowledgementOnly() {
        auto *device=new AmdPboFixture;device->allowManualOc=true;device->capsRaw=7;device->discardChange=true;
        AmdTuningPanel panel(std::make_shared<HardwareAccess>(std::unique_ptr<octool::core::HardwareBackend>(device)),nullptr,130);
        panel.resize(1300,1200);panel.show();QApplication::processEvents();
        auto *read=panel.findChild<QPushButton *>("ocCapsRead"),*apply=panel.findChild<QPushButton *>("ocFrequencyApply");
        auto *scope=panel.findChild<QComboBox *>("ocFrequencyScope");
        auto *ccd=panel.findChild<QLineEdit *>("ocCcd"),*core=panel.findChild<QLineEdit *>("ocCore"),*mhz=panel.findChild<QLineEdit *>("ocMHz");
        QVERIFY(ccd->text().isEmpty() && core->text().isEmpty() && mhz->text().isEmpty());
        apply->click();QCOMPARE(device->calls,0u);
        for(int all=0;all<2;++all) {
            scope->setCurrentIndex(all);mhz->setText("5000");
            if(!all){ccd->setText("15");core->setText("7");}
            else QVERIFY(!ccd->isEnabled() && !core->isEnabled() && ccd->text().isEmpty() && core->text().isEmpty());
            read->click();QTRY_VERIFY(panel.isEnabled());
            QTimer::singleShot(0,[]{auto *box=qobject_cast<QMessageBox *>(QApplication::activeModalWidget());if(box)box->button(QMessageBox::Yes)->click();});
            apply->click();QTRY_VERIFY(panel.isEnabled());
            QCOMPARE(device->messages.back(),all?0x5fu:0x60u);QCOMPARE(device->commandArgs.back()[0],all?5000u:0xf0701388u);
            auto *table=panel.findChild<QTableWidget *>("smuTable");
            QCOMPARE(table->item(1,1)->text(),QString("5000"));QCOMPARE(table->item(2,1)->text(),QString("Firmware accepted"));
            QCOMPARE(table->item(4,1)->text(),QString("Unavailable / not verified"));
            QVERIFY(panel.findChild<QLabel *>("smuStatus")->text().contains("remain unverified"));
            QApplication::processEvents();const auto shot=qEnvironmentVariable(all?"OCTOOL_OC_ALL_TEST_SCREENSHOT":"OCTOOL_OC_CORE_TEST_SCREENSHOT");
            if(!shot.isEmpty())QVERIFY(panel.grab().save(shot));
            const auto calls=device->calls;apply->click();QCOMPARE(device->calls,calls);
        }
        QCOMPARE(device->settings,2u);QCOMPARE(device->ocAllMHz,0u);QCOMPARE(device->ocCoreArgument,0u);QCOMPARE(device->capsRaw,7u);QCOMPARE(device->scalarRaw,0x3f800000u);
        scope->setCurrentIndex(0);QVERIFY(ccd->isEnabled() && core->isEnabled() && ccd->text().isEmpty() && core->text().isEmpty());
    }
    void ocSelectedCoresAcknowledgementOnly() {
        auto *device=new AmdPboFixture;device->allowManualOc=true;device->capsRaw=7;device->discardChange=true;
        AmdTuningPanel panel(std::make_shared<HardwareAccess>(std::unique_ptr<octool::core::HardwareBackend>(device)),nullptr,130);
        panel.resize(1400,1300);panel.show();QApplication::processEvents();
        auto *scope=panel.findChild<QComboBox *>("ocFrequencyScope");scope->setCurrentIndex(2);
        auto *cores=panel.findChild<QLineEdit *>("ocCore");
        QVERIFY(cores->text().isEmpty() && cores->placeholderText().contains("0,2,7"));
        panel.findChild<QLineEdit *>("ocCcd")->setText("15");panel.findChild<QLineEdit *>("ocMHz")->setText("5000");
        auto *apply=panel.findChild<QPushButton *>("ocFrequencyApply");auto *read=panel.findChild<QPushButton *>("ocCapsRead");
        for(const QString selected:{QString("0,2,7"),QString("0,1,2,3,4,5,6,7")}) {
            cores->setText(selected);read->click();QTRY_VERIFY(panel.isEnabled());
            QTimer::singleShot(0,[selected]{auto *box=qobject_cast<QMessageBox *>(QApplication::activeModalWidget());if(box){QVERIFY(box->text().contains(selected));box->button(QMessageBox::Yes)->click();}});
            apply->click();QTRY_VERIFY(panel.isEnabled());
            auto *table=panel.findChild<QTableWidget *>("smuTable");
            QCOMPARE(table->item(1,1)->text(),QString("5000"));QCOMPARE(table->item(4,1)->text(),QString("None"));
            QCOMPARE(table->item(5,0)->text(),QString("CCD 15 / core 0"));QVERIFY(table->item(5,1)->text().startsWith("Firmware accepted"));
            QCOMPARE(table->item(table->rowCount()-1,1)->text(),QString("Unavailable / not verified"));
            QVERIFY(panel.findChild<QLabel *>("smuStatus")->text().contains("remain unverified"));
            const auto calls=device->calls;apply->click();QCOMPARE(device->calls,calls);
            if(selected=="0,2,7") {
                QApplication::processEvents();const auto shot=qEnvironmentVariable("OCTOOL_OC_CORES_TEST_SCREENSHOT");
                if(!shot.isEmpty())QVERIFY(panel.grab().save(shot));
            }
        }
        QCOMPARE(device->settings,11u);QCOMPARE(device->ocCoreArgument,0u);QCOMPARE(device->ocAllMHz,0u);QCOMPARE(device->wrongCpu,0u);
        scope->setCurrentIndex(0);QVERIFY(cores->text().isEmpty() && cores->placeholderText()=="Core 0..7");
    }
    void ocSelectedCoresShowPartialFailure() {
        for(unsigned fault=0;fault<3;++fault) {
            auto *device=new AmdPboFixture;device->allowManualOc=true;device->capsRaw=7;
            AmdTuningPanel panel(std::make_shared<HardwareAccess>(std::unique_ptr<octool::core::HardwareBackend>(device)),nullptr,130);
            panel.resize(1400,1300);panel.show();QApplication::processEvents();
            panel.findChild<QComboBox *>("ocFrequencyScope")->setCurrentIndex(2);
            panel.findChild<QLineEdit *>("ocCcd")->setText("15");panel.findChild<QLineEdit *>("ocCore")->setText("0,2,7");panel.findChild<QLineEdit *>("ocMHz")->setText("5000");
            panel.findChild<QPushButton *>("ocCapsRead")->click();QTRY_VERIFY(panel.isEnabled());
            device->afterRequest=[&]{if(device->settings==1){if(fault==0)device->writeCompletion=0xfd;if(fault==1)device->capsRaw=0x80000007;if(fault==2)device->model=0x18;}};
            QTimer::singleShot(0,[]{auto *box=qobject_cast<QMessageBox *>(QApplication::activeModalWidget());if(box)box->button(QMessageBox::Yes)->click();});
            panel.findChild<QPushButton *>("ocFrequencyApply")->click();QTRY_VERIFY(panel.isEnabled());
            QCOMPARE(device->settings,fault==0?2u:1u);
            auto *table=panel.findChild<QTableWidget *>("smuTable");
            QCOMPARE(table->item(4,1)->text(),QString("2"));QVERIFY(table->item(5,1)->text().startsWith("Firmware accepted"));
            QVERIFY(table->item(6,1)->text().startsWith(fault==0?"Attempted; not confirmed":"Not sent"));
            QCOMPARE(table->item(7,0)->text(),QString("CCD 15 / core 7"));QCOMPARE(table->item(7,1)->text(),QString("Not sent"));
            QVERIFY(panel.findChild<QLabel *>("smuStatus")->text().contains("Partial changes may remain"));
            if(!fault) {
                QApplication::processEvents();const auto shot=qEnvironmentVariable("OCTOOL_OC_CORES_FAILURE_TEST_SCREENSHOT");
                if(!shot.isEmpty())QVERIFY(panel.grab().save(shot));
            }
        }
    }
    void ocSelectedCoresRejectInvalidAndChangedInputs() {
        auto *device=new AmdPboFixture;device->allowManualOc=true;device->capsRaw=7;
        AmdTuningPanel panel(std::make_shared<HardwareAccess>(std::unique_ptr<octool::core::HardwareBackend>(device)),nullptr,130);
        auto *scope=panel.findChild<QComboBox *>("ocFrequencyScope");scope->setCurrentIndex(2);
        auto *cores=panel.findChild<QLineEdit *>("ocCore"),*ccd=panel.findChild<QLineEdit *>("ocCcd");
        ccd->setText("15");panel.findChild<QLineEdit *>("ocMHz")->setText("5000");
        auto *read=panel.findChild<QPushButton *>("ocCapsRead"),*apply=panel.findChild<QPushButton *>("ocFrequencyApply");
        read->click();QTRY_VERIFY(panel.isEnabled());const auto calls=device->calls;
        for(const QString text:{QString(""),QString("0,0"),QString("2,0"),QString("0,8"),QString("0-7"),QString("0, 2"),QString("0,"),QString("0,,2")}) {
            cores->setText(text);apply->click();QCOMPARE(device->calls,calls);
        }
        for(unsigned mode=0;mode<4;++mode) {
            scope->setCurrentIndex(2);cores->setText("0,2,7");ccd->setText("15");
            read->click();QTRY_VERIFY(panel.isEnabled());const auto before=device->calls;
            QTimer::singleShot(0,[&]{auto *box=qobject_cast<QMessageBox *>(QApplication::activeModalWidget());if(!box)return;
                if(mode==0)cores->setText("0,7");if(mode==1)scope->setCurrentIndex(1);if(mode==2)ccd->setText("14");
                box->button(mode==3?QMessageBox::Cancel:QMessageBox::Yes)->click();});
            apply->click();QCOMPARE(device->calls,before);QCOMPARE(device->settings,0u);
        }
    }
    void ocSelectedCoresDiscardStaleTargetResult() {
        auto *device=new AmdPboFixture;device->allowManualOc=true;device->capsRaw=7;
        AmdTuningPanel panel(std::make_shared<HardwareAccess>(std::unique_ptr<octool::core::HardwareBackend>(device)),nullptr,130);
        panel.findChild<QComboBox *>("ocFrequencyScope")->setCurrentIndex(2);
        panel.findChild<QLineEdit *>("ocCcd")->setText("15");panel.findChild<QLineEdit *>("ocCore")->setText("0,2,7");panel.findChild<QLineEdit *>("ocMHz")->setText("5000");
        panel.findChild<QPushButton *>("ocCapsRead")->click();QTRY_VERIFY(panel.isEnabled());
        QTimer::singleShot(0,[]{auto *box=qobject_cast<QMessageBox *>(QApplication::activeModalWidget());if(box)box->button(QMessageBox::Yes)->click();});
        panel.findChild<QPushButton *>("ocFrequencyApply")->click();panel.findChild<QLineEdit *>("smuCpu")->setText("131");
        QTRY_VERIFY(panel.isEnabled());QCOMPARE(panel.findChild<QTableWidget *>("smuTable")->rowCount(),0);
        QVERIFY(panel.findChild<QLabel *>("smuStatus")->text().contains("result belongs to CPU 130"));
        QCOMPARE(device->settings,3u);QCOMPARE(device->wrongCpu,0u);
    }
    void ocRejectsInvalidAndChangedConfirmation() {
        auto *device=new AmdPboFixture;device->allowManualOc=true;device->capsRaw=7;
        AmdTuningPanel panel(std::make_shared<HardwareAccess>(std::unique_ptr<octool::core::HardwareBackend>(device)),nullptr,130);
        auto *read=panel.findChild<QPushButton *>("ocCapsRead"),*frequency=panel.findChild<QPushButton *>("ocFrequencyApply"),*modeApply=panel.findChild<QPushButton *>("ocModeApply");
        auto *scope=panel.findChild<QComboBox *>("ocFrequencyScope"),*mode=panel.findChild<QComboBox *>("ocMode");
        auto *ccd=panel.findChild<QLineEdit *>("ocCcd"),*core=panel.findChild<QLineEdit *>("ocCore"),*mhz=panel.findChild<QLineEdit *>("ocMHz"),*cpu=panel.findChild<QLineEdit *>("smuCpu");
        read->click();QTRY_VERIFY(panel.isEnabled());const auto calls=device->calls;
        for(const QString text:{QString(""),QString("0"),QString("-1"),QString("1048576"),QString("5000.5"),QString("0x1388")}) {
            ccd->setText("0");core->setText("0");mhz->setText(text);frequency->click();QCOMPARE(device->calls,calls);
        }
        mhz->setText("5000");ccd->setText("16");frequency->click();QCOMPARE(device->calls,calls);
        ccd->setText("0");core->setText("8");frequency->click();QCOMPARE(device->calls,calls);
        for(unsigned n=0;n<9;++n) {
            cpu->setText("130");scope->setCurrentIndex(0);ccd->setText("0");core->setText("0");mhz->setText("5000");mode->setCurrentIndex(0);
            read->click();QTRY_VERIFY(panel.isEnabled());const auto before=device->calls;
            QTimer::singleShot(0,[&]{
                auto *box=qobject_cast<QMessageBox *>(QApplication::activeModalWidget());if(!box)return;
                if(n==0)mhz->setText("5100");
                if(n==1)ccd->setText("1");
                if(n==2)core->setText("1");
                if(n==3)scope->setCurrentIndex(1);
                if(n==4 || n==7)cpu->setText("131");
                if(n==6)mode->setCurrentIndex(1);
                box->button(n==5 || n==8?QMessageBox::Cancel:QMessageBox::Yes)->click();
            });
            (n<6?frequency:modeApply)->click();QCOMPARE(device->calls,before);QCOMPARE(device->settings,0u);
        }
        device->capsRaw=6;cpu->setText("130");read->click();QTRY_VERIFY(panel.isEnabled());const auto disabled=device->calls;
        frequency->click();QCOMPARE(device->calls,disabled);QVERIFY(panel.findChild<QLabel *>("smuStatus")->text().contains("must already be enabled"));
    }
    void ocRejectsStaleCapabilitiesAndTargets() {
        for(bool frequency:{false,true})for(unsigned fault=0;fault<3;++fault) {
            auto *device=new AmdPboFixture;device->allowManualOc=true;device->capsRaw=frequency?7:6;
            AmdTuningPanel panel(std::make_shared<HardwareAccess>(std::unique_ptr<octool::core::HardwareBackend>(device)),nullptr,130);
            auto *read=panel.findChild<QPushButton *>("ocCapsRead"),*apply=panel.findChild<QPushButton *>(frequency?"ocFrequencyApply":"ocModeApply");
            panel.findChild<QComboBox *>("ocMode")->setCurrentIndex(1);
            panel.findChild<QLineEdit *>("ocCcd")->setText("0");panel.findChild<QLineEdit *>("ocCore")->setText("0");panel.findChild<QLineEdit *>("ocMHz")->setText("5000");
            read->click();QTRY_VERIFY(panel.isEnabled());
            if(fault==0)device->capsRaw^=0x80000000;
            if(fault==1)device->writeCompletion=0xfd;
            QTimer::singleShot(0,[]{auto *box=qobject_cast<QMessageBox *>(QApplication::activeModalWidget());if(box)box->button(QMessageBox::Yes)->click();});
            apply->click();
            if(fault==2)panel.findChild<QLineEdit *>("smuCpu")->setText("131");
            QTRY_VERIFY(panel.isEnabled());QCOMPARE(device->settings,fault==0?0u:1u);
            if(fault==2) {
                QCOMPARE(panel.findChild<QTableWidget *>("smuTable")->rowCount(),0);
                QVERIFY(panel.findChild<QLabel *>("smuStatus")->text().contains("result belongs to CPU 130"));
            }
            const auto calls=device->calls;apply->click();QCOMPARE(device->calls,calls);
        }
    }
    void pboRailsEncodingIsOffline() {
        auto *device = new AmdPboFixture;
        AmdTuningPanel panel(std::make_shared<HardwareAccess>(std::unique_ptr<octool::core::HardwareBackend>(device)), nullptr, 130);
        panel.resize(1150,1000); panel.show();
        QApplication::processEvents(); // Settle PBO/PM fixture layout.
        auto *a=panel.findChild<QLineEdit *>("pboRail0"), *b=panel.findChild<QLineEdit *>("pboRail1");
        a->setText("700"); b->setText("1250"); panel.findChild<QPushButton *>("pboRailsEncode")->click();
        QCOMPARE(device->calls,0u);
        QCOMPARE(panel.findChild<QTableWidget *>("smuTable")->item(0,1)->text(),hexValue(91u | (201u << 17),4));
        QVERIFY(panel.findChild<QLabel *>("smuStatus")->text().contains("sending is unavailable"));
        QApplication::processEvents(); // Settle PBO/PM fixture layout.
        const auto shot=qEnvironmentVariable("OCTOOL_PBO_RAILS_TEST_SCREENSHOT"); if(!shot.isEmpty()) QVERIFY(panel.grab().save(shot));
        a->setText("701"); panel.findChild<QPushButton *>("pboRailsEncode")->click();
        QCOMPARE(device->calls,0u); QVERIFY(panel.findChild<QLabel *>("smuStatus")->text().contains("Invalid rail encoding"));
    }
    void pmCoreCcdLayoutAudit() {
        QTemporaryDir directory; QVERIFY(directory.isValid()); const auto raw=directory.filePath("pm.bin");
        AmdPmPanel panel; panel.resize(1400,1000); panel.show();
        QApplication::processEvents(); // Settle PBO/PM fixture layout.
        auto *input=panel.findChild<QLineEdit *>("pmLayoutCcds");
        auto *audit=panel.findChild<QPushButton *>("pmLayoutAudit"), *copy=panel.findChild<QPushButton *>("pmLayoutCopy");
        auto *table=panel.findChild<QTableWidget *>("pmTable");
        QVERIFY(savePmFile(raw,pm_fixture::bytes(11636))); QVERIFY(panel.loadRawFile(raw,0x731004));
        audit->click(); QVERIFY(!copy->isEnabled());
        input->setText("16"); audit->click(); QCOMPARE(table->rowCount(),26); QVERIFY(copy->isEnabled());
        copy->click(); const auto json=QJsonDocument::fromJson(QApplication::clipboard()->text().toUtf8()).object();
        QCOMPARE(json.value("overlapping_words").toInt(),176); QVERIFY(!json.value("decoder_enabled").toBool());
        QCOMPARE(json.value("outside_words").toInt(),0);
        QApplication::processEvents(); // Settle PBO/PM fixture layout.
        const auto shot=qEnvironmentVariable("OCTOOL_PM_LAYOUT_TEST_SCREENSHOT"); if(!shot.isEmpty()) QVERIFY(panel.grab().save(shot));
        input->setText("6"); QVERIFY(!copy->isEnabled()); audit->click(); copy->click();
        QVERIFY(QJsonDocument::fromJson(QApplication::clipboard()->text().toUtf8()).object().value("constructor_fallback").toBool());
        panel.findChild<QComboBox *>("pmView")->setCurrentIndex(1); QVERIFY(!copy->isEnabled());
        QCOMPARE(table->horizontalHeaderItem(0)->text(),QString("Field"));
        QVERIFY(!panel.loadSnapshotFile(directory.filePath("missing"))); QVERIFY(!copy->isEnabled());
    }
    void pboScalarRejectsInvalidFirmwareValue() {
        auto *device = new AmdPboFixture; device->scalarRaw=0x7fc00000;
        AmdTuningPanel panel(std::make_shared<HardwareAccess>(std::unique_ptr<octool::core::HardwareBackend>(device)), nullptr, 130);
        panel.findChild<QPushButton *>("pboScalarRead")->click(); QTRY_VERIFY(panel.isEnabled());
        auto *result=panel.findChild<QTableWidget *>("smuTable");
        QCOMPARE(result->item(0,1)->text(),hexValue(0x7fc00000,4)); QCOMPARE(result->item(1,1)->text(),QString("Unavailable"));
        const auto calls=device->calls;
        panel.findChild<QLineEdit *>("pboScalar")->setText("5"); panel.findChild<QPushButton *>("pboScalarApply")->click();
        QCOMPARE(device->calls,calls); QCOMPARE(device->settings,0u);
    }
    void curvePartialUpdateReportsEachCore() {
        auto *device = new AmdCurveFixture; device->writable = true;
        AmdTuningPanel panel(std::make_shared<HardwareAccess>(std::unique_ptr<octool::core::HardwareBackend>(device)), nullptr, 130);
        panel.resize(1100,900); panel.show();
        panel.findChild<QLineEdit *>("curveCcd")->setText("3"); panel.findChild<QLineEdit *>("curveCore")->setText("0,2,7");
        panel.findChild<QLineEdit *>("curveMargin")->setText("-25");
        panel.findChild<QPushButton *>("curveRead")->click(); QTRY_VERIFY(panel.isEnabled());
        device->afterRequest = [&] { if (device->settings) device->writeCompletion = 0xfe; };
        QTimer::singleShot(0, [] { auto *box = qobject_cast<QMessageBox *>(QApplication::activeModalWidget()); if (box) box->button(QMessageBox::Yes)->click(); });
        panel.findChild<QPushButton *>("curveApply")->click(); QTRY_VERIFY(panel.isEnabled());
        auto *table = panel.findChild<QTableWidget *>("smuTable"); auto *status = panel.findChild<QLabel *>("smuStatus");
        QCOMPARE(device->settings, 2u); QVERIFY(status->text().contains("Stopped at write, core 2"));
        QCOMPARE(table->item(1,1)->text(), QString("Readback matched"));
        QCOMPARE(table->item(4,1)->text(), QString("Attempted; not confirmed"));
        QCOMPARE(table->item(6,1)->text(), QString("No setting command sent"));
        QVERIFY(status->text().contains("Earlier changes may remain"));
        const auto shot = qEnvironmentVariable("OCTOOL_CURVE_PARTIAL_TEST_SCREENSHOT"); if (!shot.isEmpty()) QVERIFY(panel.grab().save(shot));
    }
    void curveSetRejectsInvalidCountsAndConflicts() {
        auto *device = new AmdCurveFixture; device->writable = true;
        AmdTuningPanel panel(std::make_shared<HardwareAccess>(std::unique_ptr<octool::core::HardwareBackend>(device)), nullptr, 130);
        panel.findChild<QLineEdit *>("curveCcd")->setText("3"); panel.findChild<QLineEdit *>("curveCore")->setText("0");
        panel.findChild<QPushButton *>("curveRead")->click(); QTRY_VERIFY(panel.isEnabled()); const unsigned calls = device->calls;
        auto *margin = panel.findChild<QLineEdit *>("curveMargin"); auto *apply = panel.findChild<QPushButton *>("curveApply");
        for (const QString value : {QString(""),QString("-51"),QString("51"),QString("1.5"),QString("1e1")}) {
            margin->setText(value); apply->click(); QCOMPARE(device->calls, calls);
        }
        margin->setText("-25"); device->margins[24] = 0;
        QTimer::singleShot(0, [] { auto *box = qobject_cast<QMessageBox *>(QApplication::activeModalWidget()); if (box) box->button(QMessageBox::Yes)->click(); });
        apply->click(); QTRY_VERIFY(panel.isEnabled()); QCOMPARE(device->settings, 0u);
        QVERIFY(panel.findChild<QLabel *>("smuStatus")->text().contains("Stopped at preflight"));
        panel.findChild<QPushButton *>("curveRead")->click(); QTRY_VERIFY(panel.isEnabled()); device->discardChange = true;
        QTimer::singleShot(0, [] { auto *box = qobject_cast<QMessageBox *>(QApplication::activeModalWidget()); if (box) box->button(QMessageBox::Yes)->click(); });
        apply->click(); QTRY_VERIFY(panel.isEnabled()); QCOMPARE(device->settings, 1u);
        QVERIFY(panel.findChild<QLabel *>("smuStatus")->text().contains("Stopped at verify"));
        QCOMPARE(panel.findChild<QTableWidget *>("smuTable")->item(1,1)->text(), QString("Accepted; readback not confirmed"));
    }
    void curveTargetChangesInvalidateConfirmation() {
        auto *device = new AmdCurveFixture; device->writable = true;
        AmdTuningPanel panel(std::make_shared<HardwareAccess>(std::unique_ptr<octool::core::HardwareBackend>(device)), nullptr, 130);
        panel.findChild<QLineEdit *>("curveCcd")->setText("3"); panel.findChild<QLineEdit *>("curveCore")->setText("0,2");
        panel.findChild<QLineEdit *>("curveMargin")->setText("-25");
        panel.findChild<QPushButton *>("curveRead")->click(); QTRY_VERIFY(panel.isEnabled()); const unsigned calls = device->calls;
        QTimer::singleShot(0, [&] {
            panel.findChild<QLineEdit *>("curveCore")->setText("7");
            auto *box = qobject_cast<QMessageBox *>(QApplication::activeModalWidget()); if (box) box->button(QMessageBox::Yes)->click();
        });
        panel.findChild<QPushButton *>("curveApply")->click(); QCOMPARE(device->calls, calls); QCOMPARE(device->settings, 0u);
        QVERIFY(panel.findChild<QLabel *>("smuStatus")->text().contains("Inputs changed"));
        QCOMPARE(panel.findChild<QTableWidget *>("smuTable")->rowCount(), 0);
    }
    void curvePendingReadDoesNotPublishChangedTarget() {
        auto *device = new AmdCurveFixture;
        AmdTuningPanel panel(std::make_shared<HardwareAccess>(std::unique_ptr<octool::core::HardwareBackend>(device)), nullptr, 130);
        panel.findChild<QLineEdit *>("curveCcd")->setText("3"); panel.findChild<QLineEdit *>("curveCore")->setText("0");
        panel.findChild<QPushButton *>("curveRead")->click();
        panel.findChild<QLineEdit *>("curveCore")->setText("7");
        QTRY_VERIFY(panel.isEnabled()); QCOMPARE(panel.findChild<QTableWidget *>("smuTable")->rowCount(), 0);
        QVERIFY(panel.findChild<QLabel *>("smuStatus")->text().contains("Inputs changed"));
        const unsigned calls = device->calls; panel.findChild<QPushButton *>("curveApply")->click(); QCOMPARE(device->calls, calls);
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
        QCOMPARE(decodeFamily1aPstate(0x80000001000003e8ULL).frequencyMHz, 5000.0);
        QCOMPARE(decodeFamily1aPstate(0x80000000000001ccULL).frequencyMHz, 2300.0);
        QCOMPARE(decodeFamily1aPstate(0x800000000000012cULL).frequencyMHz, 1500.0);
        QCOMPARE(decodeFamily1aPstate(0x8000000000000010ULL).frequencyMHz, 80.0);
        QCOMPARE(decodeFamily1aPstate(0x8000000000000fffULL).frequencyMHz, 20475.0);
        QVERIFY(!decodeFamily1aPstate(0x800000000000000fULL).validFrequency);
        QVERIFY(!decodeFamily1aPstate(0x00000000000003e8ULL).validFrequency);
        QVERIFY(!decodeFamily1aPstate(0x00000000000003e8ULL).enabled);
    }
    void pstateCpuGuardsPreventMsrAccess() {
        for (int unsupported = 0; unsupported < 5; ++unsupported) {
            PstateReference reference;
            if (unsupported == 0) reference.amd = false;
            if (unsupported == 1) reference.signature = 0x00900f11; // Family 18h.
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
    void pstateLegacyFamiliesUseMatchingDecoder() {
        for (auto signature : {0x00800f11u,0x00a20f10u}) {
            PstateReference reference; reference.signature = signature; reference.limit = 0x71;
            const uint64_t base = UINT64_C(0xa5a5a5a9abcdc000);
            const unsigned bits[] = {0x08a0,0x0cab,0x09a5,0x08a0,0x00a0,0x080f,0x1ba0,0x2ea0};
            for (unsigned i=0;i<8;++i) reference.definitions[i] = (base | bits[i]) & (i==3 ? ~(UINT64_C(1)<<63) : UINT64_MAX);
            PstatesPanel panel(reference.access(),nullptr,3); panel.resize(1800,650); panel.show();
            auto *read=panel.findChild<QPushButton *>("pstateRead");
            auto *table=panel.findChild<QTableWidget *>("pstateTable");
            QCOMPARE(reference.calls.load(),0);
            read->click(); QTRY_VERIFY(panel.isEnabled());
            QCOMPARE(table->columnCount(),12);
            QCOMPARE(table->item(0,4)->text(),QString("4000"));
            QCOMPARE(table->item(1,4)->text(),QString("2850"));
            QVERIFY(std::abs(table->item(2,4)->text().toDouble()-3666.6666666666665)<1e-8);
            QCOMPARE(table->item(0,6)->text(),QString("55"));
            QCOMPARE(table->item(0,9)->text(),QString("160"));
            QCOMPARE(table->item(0,10)->text(),QString("8"));
            QCOMPARE(table->item(0,11)->text(),QString("1"));
            QCOMPARE(table->horizontalHeaderItem(6)->text(),QString("VID low bits 21:14"));
            QCOMPARE(table->item(4,5)->text(),QString("Divider off"));
            QCOMPARE(table->item(5,5)->text(),QString("Reserved frequency ID"));
            for (int i=3;i<8;++i) QCOMPARE(table->item(i,4)->text(),QString("—"));
            QVERIFY(panel.findChild<QLabel *>("pstateStatus")->text().contains("range P1-P7"));
            const auto capture=qEnvironmentVariable(signature==0x00800f11u ? "OCTOOL_PSTATES_17_TEST_SCREENSHOT" : "OCTOOL_PSTATES_19_TEST_SCREENSHOT");
            if (!capture.isEmpty()) QVERIFY(panel.grab().save(capture));
            // A different encoding on the next read must replace both data and headers.
            reference.signature=0x00b00f81; reference.limit=0;
            reference.definitions[0]=UINT64_C(0x80000001000003e8);
            read->click(); QTRY_VERIFY(panel.isEnabled());
            QCOMPARE(table->item(0,4)->text(),QString("5000"));
            QCOMPARE(table->item(0,6)->text(),QString("256"));
            QCOMPARE(table->item(0,10)->text(),QString("—"));
            QCOMPARE(table->horizontalHeaderItem(6)->text(),QString("VID bits 32,21:14"));
            for (int i=1;i<8;++i) QCOMPARE(table->item(i,2)->text(),QString("—"));
            QCOMPARE(reference.writes.load(),0); QCOMPARE(reference.wrongCpu.load(),0);
        }
    }
    void pstateDiscardsReadAfterCpuSelectionChanges() {
        PstateReference reference; PstatesPanel panel(reference.access(),nullptr,3); panel.show();
        auto *cpu=panel.findChild<QLineEdit *>("pstateCpu");
        auto *read=panel.findChild<QPushButton *>("pstateRead");
        auto *table=panel.findChild<QTableWidget *>("pstateTable");
        read->click(); cpu->setText("4"); cpu->setText("3");
        QTRY_VERIFY(panel.isEnabled());
        QVERIFY(panel.findChild<QLabel *>("pstateStatus")->text().contains("discarded"));
        for (int i=0;i<8;++i) QCOMPARE(table->item(i,2)->text(),QString("—"));
        read->click(); QTRY_VERIFY(panel.isEnabled());
        QCOMPARE(table->item(0,4)->text(),QString("5000"));
        QCOMPARE(reference.writes.load(),0); QCOMPARE(reference.wrongCpu.load(),0);
    }
    void pstateInconsistentLimitsAndPartialFailures() {
        PstateReference reference; PstatesPanel panel(reference.access(),nullptr,3); panel.show();
        auto *read=panel.findChild<QPushButton *>("pstateRead");
        auto *table=panel.findChild<QTableWidget *>("pstateTable");
        read->click(); QTRY_VERIFY(panel.isEnabled());
        QVERIFY(table->item(0,2)->text()!=QString("—"));
        reference.msrs.clear(); reference.limit=0x23;
        read->click(); QTRY_VERIFY(panel.isEnabled());
        QVERIFY(panel.findChild<QLabel *>("pstateStatus")->text().contains("inconsistent"));
        QVERIFY(reference.msrs==std::vector<uint32_t>({0xc0010061}));
        for (int i=0;i<8;++i) QCOMPARE(table->item(i,2)->text(),QString("—"));
        reference.limit=0x70; reference.failRegister=0xc0010065;
        read->click(); QTRY_VERIFY(panel.isEnabled());
        QCOMPARE(table->item(0,4)->text(),QString("5000"));
        QCOMPARE(table->item(1,2)->text(),QString("—"));
        QVERIFY(table->item(1,5)->text().contains("FAILED"));
        QCOMPARE(reference.writes.load(),0); QCOMPARE(reference.wrongCpu.load(),0);
    }
    void pstateElectricalViewsUseSharedDescriptors() {
        for (bool svi3 : {false,true}) {
            PstateReference reference; reference.signature = svi3 ? 0x008a0f01 : 0x00810f81; reference.limit = 0x70;
            for (unsigned i=0;i<8;++i) reference.definitions[i]=pstateElectricalFixture(svi3,i);
            PstatesPanel panel(reference.access(),nullptr,3); panel.resize(1550,650); panel.show();
            auto *view=panel.findChild<QComboBox *>("pstateView");
            auto *read=panel.findChild<QPushButton *>("pstateRead");
            auto *table=panel.findChild<QTableWidget *>("pstateTable");
            view->setCurrentIndex(1); QCOMPARE(reference.calls.load(),0);
            read->click(); QTRY_VERIFY(panel.isEnabled());
            QCOMPARE(table->columnCount(),8);
            QCOMPARE(table->item(1,2)->text(),QString(svi3 ? "250" : "1543.75"));
            QCOMPARE(table->item(2,1)->text(),QString(svi3 ? "256" : "80"));
            QCOMPARE(table->item(2,2)->text(),QString(svi3 ? "1525" : "1050"));
            QCOMPARE(table->item(2,4)->text(),QString("1.75"));
            QCOMPARE(table->item(0,3)->text(),QString(svi3 ? "voltage off" : "valid"));
            QCOMPARE(table->item(3,2)->text(),QString(svi3 ? "2800" : "6.25"));
            QCOMPARE(table->item(3,4)->text(),QString("0"));
            QCOMPARE(table->item(4,4)->text(),QString("—"));
            QCOMPARE(table->item(4,5)->text(),QString("reserved divisor"));
            for (int i=6;i<8;++i) {
                QCOMPARE(table->item(i,1)->text(),QString("—"));
                QCOMPARE(table->item(i,2)->text(),QString("—"));
                QCOMPARE(table->item(i,4)->text(),QString("—"));
            }
            QCOMPARE(table->item(6,3)->text(),QString("disabled"));
            QCOMPARE(table->item(7,3)->text(),QString("reserved bits"));
            const int calls=reference.calls.load();
            view->setCurrentIndex(0);
            QCOMPARE(table->columnCount(),12);
            QCOMPARE(table->item(2,6)->text(),QString(svi3 ? "0" : "80"));
            QCOMPARE(table->item(2,11)->text(),QString(svi3 ? "1" : "0"));
            view->setCurrentIndex(1); QCOMPARE(reference.calls.load(),calls);
            QCOMPARE(table->item(2,2)->text(),QString(svi3 ? "1525" : "1050"));
            for (auto *button : panel.findChildren<QPushButton *>()) if (button->text()=="Copy snapshot") button->click();
            QVERIFY(QApplication::clipboard()->text().contains("Expected core (A)"));
            QVERIFY(QApplication::clipboard()->text().contains("not live measurements"));
            const auto shot=qEnvironmentVariable(svi3 ? "OCTOOL_PSTATES_SVI3_TEST_SCREENSHOT" : "OCTOOL_PSTATES_SVI2_TEST_SCREENSHOT");
            if (!shot.isEmpty()) QVERIFY(panel.grab().save(shot));
            QCOMPARE(reference.writes.load(),0); QCOMPARE(reference.wrongCpu.load(),0);
        }
    }
    void pstateElectricalFailuresDoNotReuseValues() {
        PstateReference reference; reference.signature=0x00a50f01; reference.limit=0x70;
        for (unsigned i=0;i<8;++i) reference.definitions[i]=pstateElectricalFixture(false,i);
        PstatesPanel panel(reference.access(),nullptr,3); panel.resize(1550,650); panel.show();
        auto *view=panel.findChild<QComboBox *>("pstateView"); view->setCurrentIndex(1);
        auto *read=panel.findChild<QPushButton *>("pstateRead");
        auto *table=panel.findChild<QTableWidget *>("pstateTable");
        read->click(); QTRY_VERIFY(panel.isEnabled()); QCOMPARE(table->item(2,2)->text(),QString("1050"));
        reference.failRegister=0xc0010066;
        read->click(); QTRY_VERIFY(panel.isEnabled());
        for (int column : {1,2,3,4,5,7}) QCOMPARE(table->item(2,column)->text(),QString("—"));
        QVERIFY(table->item(2,6)->text().contains("FAILED"));
        QCOMPARE(table->item(1,2)->text(),QString("1543.75"));
        const auto shot=qEnvironmentVariable("OCTOOL_PSTATES_ELECTRICAL_ERROR_SCREENSHOT");
        if (!shot.isEmpty()) QVERIFY(panel.grab().save(shot));
        reference.failRegister=0; reference.hypervisor=true;
        read->click(); QTRY_VERIFY(panel.isEnabled());
        QCOMPARE(table->item(2,2)->text(),QString("—"));
        QCOMPARE(table->item(2,3)->text(),QString("virtualized"));
        reference.hypervisor=false; reference.signature=0x00b00f21;
        read->click(); QTRY_VERIFY(panel.isEnabled());
        QCOMPARE(table->item(2,2)->text(),QString("—"));
        QCOMPARE(table->item(2,3)->text(),QString("unsupported model"));
        reference.signature=0x008a0f01; reference.failRegister=0xc0010061;
        read->click(); QTRY_VERIFY(panel.isEnabled());
        for (int row=0;row<8;++row) for (int column : {1,2,3,4,5,7})
            QCOMPARE(table->item(row,column)->text(),QString("—"));
        view->setCurrentIndex(0);
        for (int row=0;row<8;++row) QCOMPARE(table->item(row,2)->text(),QString("—"));
        QCOMPARE(reference.writes.load(),0); QCOMPARE(reference.wrongCpu.load(),0);
    }
    void pstateElectricalSelectionDiscardsPendingRead() {
        PstateReference reference; reference.signature=0x00810f81;
        reference.definitions[0]=pstateElectricalFixture(false,2);
        PstatesPanel panel(reference.access(),nullptr,3); panel.show();
        auto *view=panel.findChild<QComboBox *>("pstateView"); view->setCurrentIndex(1);
        auto *cpu=panel.findChild<QLineEdit *>("pstateCpu");
        auto *read=panel.findChild<QPushButton *>("pstateRead");
        auto *table=panel.findChild<QTableWidget *>("pstateTable");
        read->click(); cpu->setText("4"); cpu->setText("3"); QTRY_VERIFY(panel.isEnabled());
        QVERIFY(panel.findChild<QLabel *>("pstateStatus")->text().contains("discarded"));
        QCOMPARE(table->item(0,2)->text(),QString("—"));
        view->setCurrentIndex(0); view->setCurrentIndex(1);
        QCOMPARE(table->item(0,2)->text(),QString("—"));
        read->click(); QTRY_VERIFY(panel.isEnabled()); QCOMPARE(table->item(0,2)->text(),QString("1050"));
        cpu->setText("4"); QCOMPARE(table->item(0,2)->text(),QString("—"));
        reference.expectedCpu=4; reference.failLeaf=1;
        read->click(); QTRY_VERIFY(panel.isEnabled());
        QCOMPARE(table->item(0,2)->text(),QString("—"));
        QCOMPARE(table->item(0,4)->text(),QString("—"));
        QCOMPARE(reference.writes.load(),0); QCOMPARE(reference.wrongCpu.load(),0);
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
