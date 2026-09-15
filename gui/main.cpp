#include "MainWindow.h"

#include <QApplication>
#include <QCoreApplication>

int main(int argc, char *argv[])
{
    QApplication application(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("MGZ Compressor"));
    QCoreApplication::setApplicationVersion(QStringLiteral("1.1"));
    QCoreApplication::setOrganizationName(QStringLiteral("HuffmanTree"));

    MainWindow window;
    window.show();
    return application.exec();
}
