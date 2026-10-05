#include "MainWindow.h"

#include "BuildInfo.h"
#include "Logging.h"
#include "Theme.h"

#include <QApplication>
#include <QCoreApplication>
#include <QLoggingCategory>
#include <QPainter>
#include <QPainterPath>
#include <QPixmap>
#include <QPropertyAnimation>
#include <QSplashScreen>
#include <QTimer>
#include <QVersionNumber>
#include <QVulkanInstance>

int main(int argc, char* argv[])
{
    QApplication app(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("Velox Studio"));
    QCoreApplication::setOrganizationName(QStringLiteral("Velox Studio"));
    QCoreApplication::setApplicationVersion(velox::BuildInfo::versionString());
    velox::Logging::install();
    velox::ui::Theme::apply(app);

    QPixmap splashImage(480, 250);
    splashImage.fill(QColor(QStringLiteral("#202127")));
    {
        QPainter painter(&splashImage);
        painter.setRenderHint(QPainter::Antialiasing);
        QLinearGradient accent(0, 0, splashImage.width(), splashImage.height());
        accent.setColorAt(0.0, QColor(QStringLiteral("#477fe5")));
        accent.setColorAt(1.0, QColor(QStringLiteral("#79c6df")));
        QPen pen(accent, 8, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
        painter.setPen(pen);
        QPainterPath mark;
        mark.moveTo(213, 78);
        mark.lineTo(240, 128);
        mark.lineTo(267, 78);
        painter.drawPath(mark);
        painter.setPen(QColor(QStringLiteral("#f2f3f5")));
        QFont brandFont(QStringLiteral("Segoe UI"), 22, QFont::DemiBold);
        painter.setFont(brandFont);
        painter.drawText(QRect(30, 145, 420, 42), Qt::AlignCenter,
                         QStringLiteral("Velox Studio"));
        painter.setPen(QColor(QStringLiteral("#a6a8af")));
        painter.setFont(QFont(QStringLiteral("Segoe UI"), 10));
        painter.drawText(QRect(30, 190, 420, 24), Qt::AlignCenter,
                         QStringLiteral("Starting Vulkan workspace…"));
    }
    QSplashScreen splash(splashImage, Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint);
    splash.setWindowOpacity(0.0);
    splash.show();
    auto* fadeIn = new QPropertyAnimation(&splash, "windowOpacity", &splash);
    fadeIn->setDuration(240);
    fadeIn->setStartValue(0.0);
    fadeIn->setEndValue(1.0);
    fadeIn->start(QAbstractAnimation::DeleteWhenStopped);
    app.processEvents();

    QVulkanInstance vulkanInstance;
    vulkanInstance.setApiVersion(QVersionNumber(1, 1, 0));
    if (!vulkanInstance.create()) {
        qCritical() << "Could not initialize Vulkan. QVulkanInstance error:"
                    << vulkanInstance.errorCode();
        splash.close();
        velox::Logging::shutdown();
        return 1;
    }

    velox::ui::MainWindow window(&vulkanInstance);
    window.show();
    QTimer::singleShot(550, &splash, [&splash] {
        auto* fadeOut = new QPropertyAnimation(&splash, "windowOpacity", &splash);
        fadeOut->setDuration(260);
        fadeOut->setStartValue(splash.windowOpacity());
        fadeOut->setEndValue(0.0);
        QObject::connect(fadeOut, &QPropertyAnimation::finished, &splash, &QSplashScreen::close);
        fadeOut->start(QAbstractAnimation::DeleteWhenStopped);
    });
    const int exitCode = app.exec();
    velox::Logging::shutdown();
    return exitCode;
}
