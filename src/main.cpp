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
    return a.exec();
}
