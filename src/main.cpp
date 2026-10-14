#include <QApplication>
#include "ui/main_window.h"

int main(int argc, char* argv[]) {
    QApplication app(argc, argv);
    app.setApplicationName("QTest");
    app.setApplicationVersion("1.0.0");

    qf::ui::MainWindow window;
    window.show();

    return app.exec();
}