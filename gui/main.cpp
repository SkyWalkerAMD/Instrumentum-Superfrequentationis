// SPDX-License-Identifier: GPL-2.0-only
#include "registerpanel.h"
#include <QApplication>
#include <QFile>
#include <QFormLayout>
#include <QLabel>
#include <QMainWindow>
#include <QPixmap>
#include <QSysInfo>
#include <QTabWidget>
#include <QTimer>
#include <QVBoxLayout>
#include <unistd.h>

static QString cpuModel()
{
    QFile file("/proc/cpuinfo");
    if (file.open(QIODevice::ReadOnly)) {
        const auto lines = file.readAll().split('\n');
        for (const auto &line : lines) {
            if (line.startsWith("model name") && line.contains(':'))
                return QString::fromUtf8(line.mid(line.indexOf(':') + 1)).trimmed();
        }
    }
    return "Unavailable";
}

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    app.setApplicationName("octool");
    app.setOrganizationName("OCTool");
    app.setApplicationVersion(OCTOOL_VERSION);
    auto access = std::make_shared<HardwareAccess>();
    QMainWindow window;
    window.setObjectName("MainWindow");
    window.setWindowTitle("OCTool — Basic information and raw registers");
    window.resize(940, 700);
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
    row("CPU", cpuModel());
    const long cpuCount = sysconf(_SC_NPROCESSORS_ONLN);
    row("Online logical CPUs", cpuCount > 0 ? QString::number(cpuCount) : "Unavailable");
    row("Operating system", QSysInfo::prettyProductName());
    row("Kernel", QSysInfo::kernelVersion());
    row("Architecture", QSysInfo::currentCpuArchitecture());
    row("OCTool", app.applicationVersion());
    const QString msr = access->backend(Space::Msr);
    row("Hardware access", msr == "module" ? "Connected to /dev/mydev" :
        "Module unavailable. Explicit register operations may use direct OS access if permitted.");
    layout->addLayout(form);
    auto *scope = new QLabel("First reconstruction stage: basic information and raw MSR / MMIO / PCI access. "
        "Platform monitoring and overclocking panels are not restored yet.", overview);
    scope->setWordWrap(true); layout->addWidget(scope);
    layout->addStretch();
    tabs->addTab(overview, "Information");
    tabs->addTab(new RegisterPanel(Space::Msr, access, tabs), "MSR");
    tabs->addTab(new RegisterPanel(Space::Memory, access, tabs), "MMIO");
    tabs->addTab(new RegisterPanel(Space::Pci, access, tabs), "PCI");
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
