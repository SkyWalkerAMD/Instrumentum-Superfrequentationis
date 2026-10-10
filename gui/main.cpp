// SPDX-License-Identifier: GPL-2.0-only
#include "registerpanel.h"
#include "pstates.h"
#include "platformpanels.h"
#include "authorizationpanel.h"
#include "platform/system_info.h"
#include <QApplication>
#include <QCoreApplication>
#include <QJsonArray>
#include "umcpanel.h"
#include <QJsonDocument>
#include <QJsonObject>
#include <QFormLayout>
#include <QLabel>
#include <QMainWindow>
#include <QPixmap>
#include <QSysInfo>
#include <QTabWidget>
#include <QTimer>
#include <QVBoxLayout>
#include <cstring>
#include <iostream>

static int diagnostics(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    const auto info = octool::platform::systemInfo();
    QJsonArray allowed;
    for (auto cpu : info.allowedCpus) allowed.append(int(cpu));
    const QJsonObject report{{"version", OCTOOL_VERSION}, {"os", QSysInfo::prettyProductName()},
        {"kernel", QSysInfo::kernelVersion()}, {"architecture", QSysInfo::currentCpuArchitecture()},
        {"cpu_model", QString::fromStdString(info.cpuModel)}, {"online_cpus", double(info.onlineCpus)},
        {"allowed_cpus", allowed}, {"affinity_error", info.affinityError},
        {"module_loaded", info.moduleLoaded}, {"helper_installed", info.helperInstalled},
        {"register_access_tested", false}};
    std::cout << QJsonDocument(report).toJson().constData();
    return info.affinityError ? 1 : 0;
}

int main(int argc, char **argv)
{
    if (argc == 2 && std::strcmp(argv[1], "--diagnose") == 0) return diagnostics(argc, argv);
    QApplication app(argc, argv);
    app.setApplicationName("octool");
    app.setOrganizationName("OCTool");
    app.setApplicationVersion(OCTOOL_VERSION);
    auto access = std::make_shared<HardwareAccess>();
    const auto info = octool::platform::systemInfo();
    const unsigned initialCpu = info.allowedCpus.empty() ? 0 : info.allowedCpus.front();
    QMainWindow window;
    window.setObjectName("MainWindow");
    window.setWindowTitle("OCTool — Platform controls and hardware information");
    window.resize(1100, 760);
    auto *tabs = new QTabWidget(&window);
    auto *overview = new QWidget(tabs);
    auto *layout = new QVBoxLayout(overview);
    auto *heading = new QLabel("Instrumentum Superfrequentationis", overview);
    QFont title = heading->font(); title.setPointSize(18); title.setBold(true); heading->setFont(title);
    layout->addWidget(heading);
    auto *form = new QFormLayout;
    auto row = [form, overview](const QString &label, const QString &value) {
        auto *text = new QLabel(value, overview); text->setWordWrap(true);
        text->setTextInteractionFlags(Qt::TextSelectableByMouse); form->addRow(label, text);
    };
    row("CPU", info.cpuModel.empty() ? "Unavailable" : QString::fromStdString(info.cpuModel));
    row("Online logical CPUs", info.onlineCpus > 0 ? QString::number(info.onlineCpus) : "Unavailable");
    row("CPUs available to this process", info.affinityError ? "Unavailable" : QString::number(info.allowedCpus.size()));
    row("Operating system", QSysInfo::prettyProductName());
    row("Kernel", QSysInfo::kernelVersion());
    row("Architecture", QSysInfo::currentCpuArchitecture());
    row("OCTool", app.applicationVersion());
    layout->addLayout(form);
    layout->addWidget(new AuthorizationPanel(access, overview));
    auto *scope = new QLabel("Raw MSR / MMIO / PCI, AMD P-state definitions, Intel power / HWP controls, "
        "AMD BIOS mailbox commands, and memory / motherboard inventory. "
        "Each page states the recovered scope; board-specific tuning and full original feature parity remain in progress.", overview);
    scope->setWordWrap(true); layout->addWidget(scope);
    layout->addStretch();
    tabs->addTab(overview, "Information");
    tabs->addTab(new RegisterPanel(Space::Msr, access, tabs, initialCpu), "MSR");
    tabs->addTab(new RegisterPanel(Space::Memory, access, tabs), "MMIO");
    tabs->addTab(new RegisterPanel(Space::Pci, access, tabs), "PCI");
    tabs->addTab(new PstatesPanel(access, tabs, initialCpu), "AMD PStates");
    tabs->addTab(new IntelControlsPanel(access, tabs, initialCpu), "Intel Controls");
    tabs->addTab(new AmdTuningPanel(access, tabs, initialCpu), "AMD tuning");
    tabs->addTab(new MemoryBoardPanel(tabs), "Memory / Motherboard");
    tabs->addTab(new UmcPanel(access,tabs,initialCpu), "AMD UMC");
    window.setCentralWidget(tabs);
    window.show();
    // CI requests snapshots of these real windows for visual inspection.
    // Normal launches have no screenshot path and create no image files.
    const QString capture = qEnvironmentVariable("OCTOOL_SMOKE_SCREENSHOT");
    if (!capture.isEmpty()) QTimer::singleShot(1000, &window, [&window, tabs, capture] {
        for (int page = 0; page < tabs->count(); ++page) {
            tabs->setCurrentIndex(page);
            window.grab().save(capture + QString("-%1.png").arg(page));
        }
        tabs->setCurrentIndex(0);
    });
    return app.exec();
}
