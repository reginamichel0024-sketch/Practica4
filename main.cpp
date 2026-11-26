#include "mainwindow.h"

#include <QApplication>

int main(int argc, char *argv[])
{
    QApplication a(argc, argv);
    MainWindow w;
#ifdef Q_OS_ANDROID
    w.showMaximized();   // P4: en el telefono ocupa toda la pantalla
#else
    w.resize(420, 820);  // P4: en la computadora se ve del tamaño de un telefono
    w.show();
#endif
    return QApplication::exec();
}
