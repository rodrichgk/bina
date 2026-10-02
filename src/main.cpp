#include "mainwindow.h"
#include "theme.h"

#include <QApplication>
#include <QDir>
#include <QFile>

void myMessageOutput(QtMsgType type, const QMessageLogContext &context, const QString &msg)
{
    QByteArray localMsg = msg.toLocal8Bit();
    FILE *f = fopen("debug_log.txt", "a");
    if (f) {
        fprintf(f, "%s\n", localMsg.constData());
        fclose(f);
    }
}

int main(int argc, char *argv[])
{
    qInstallMessageHandler(myMessageOutput);
    QApplication a(argc, argv);
    
    QApplication::setApplicationName("Bina");
    QApplication::setApplicationDisplayName("Bina");
    QApplication::setOrganizationName("Butu");
    Theme::apply(a);
    QApplication::setWindowIcon(Theme::appIcon());

    MainWindow w;
    w.show();
    // "Bina.exe song.bina" opens the project
    const QStringList args = QApplication::arguments();
    if (args.size() > 1 && args.at(1).endsWith(".bina", Qt::CaseInsensitive)) {
        w.openProject(args.at(1));
    }
    return a.exec();
}
