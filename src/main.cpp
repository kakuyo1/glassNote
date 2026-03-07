#include <QApplication>

#include "app/AppController.h"
#include "common/Constants.h"

#ifndef GLASSNOTE_APP_VERSION
#define GLASSNOTE_APP_VERSION "0.1.0"
#endif

int main(int argc, char *argv[]) {
    QApplication app(argc, argv);
    QCoreApplication::setOrganizationName(QString::fromUtf8(glassnote::constants::kOrganizationName));
    QCoreApplication::setApplicationName(QString::fromUtf8(glassnote::constants::kApplicationName));
    QCoreApplication::setApplicationVersion(QStringLiteral(GLASSNOTE_APP_VERSION));

    auto *controller = new glassnote::AppController(&app);
    controller->initialize();

    return app.exec();
}
