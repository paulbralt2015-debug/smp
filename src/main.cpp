#include <QApplication>
#include <QStringList>

#include "MainWindow.h"

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);

    QApplication::setApplicationName("Cinema Surround Player");
    QApplication::setOrganizationName("CinemaSurround");
    QApplication::setApplicationVersion("1.1");

    MainWindow window;
    window.show();

    // Permet « Ouvrir avec… » et la ligne de commande : player film.mkv
    const QStringList args = QApplication::arguments();
    if (args.size() > 1)
        window.openPath(args.at(1));

    return app.exec();
}
