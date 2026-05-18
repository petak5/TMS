#include <QApplication>
#include <QDir>
#include <unistd.h>
#include "tmoguiwindow.h"

int main(int argc, char **argv)
{
    Q_INIT_RESOURCE(icons);

    QApplication app(argc, argv);

    QDir hdrTmp(QDir::tempPath() + "/tms_hdr");
    if (hdrTmp.exists())
        for (const auto& f : hdrTmp.entryList(QDir::Files))
            hdrTmp.remove(f);
    app.setAttribute(Qt::AA_DontShowIconsInMenus, false);

    TMOGUIWindow w;
    w.show();

    if (argc >= 2)
        w.openFile(argv[1]);

    int ret = app.exec();
    _exit(ret);
}
