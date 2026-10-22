#include <QApplication>
#include <QTranslator>
#include <QLibraryInfo>
#include <QDir>
#include "ui/main_window.h"

int main(int argc, char* argv[]) {
    QApplication app(argc, argv);
    app.setApplicationName("DefectX");
    app.setApplicationVersion("1.0.0");

    static QTranslator appTranslator;
    static QTranslator qtTranslator;

    QString langPath = QApplication::applicationDirPath() + "/lang";
    if (appTranslator.load("qtest_zh_CN", langPath)) {
        app.installTranslator(&appTranslator);
    }
    if (qtTranslator.load("qtbase_zh_CN", QLibraryInfo::path(QLibraryInfo::TranslationsPath))) {
        app.installTranslator(&qtTranslator);
    }

    qf::ui::MainWindow window;
    window.show();

    return app.exec();
}