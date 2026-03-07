#include <QApplication>

#include "app/AppController.h"
#include "common/Constants.h"

int main(int argc, char *argv[]) {
    QApplication app(argc, argv);
    QCoreApplication::setOrganizationName(QString::fromUtf8(glassnote::constants::kOrganizationName));
    QCoreApplication::setApplicationName(QString::fromUtf8(glassnote::constants::kApplicationName));

    auto *controller = new glassnote::AppController(&app);
    controller->initialize();

    return app.exec();
}
