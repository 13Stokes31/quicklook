// Comprueba la ejecución de la misma KDesktopFileAction que utiliza Dolphin.
#include <QEventLoop>
#include <QGuiApplication>
#include <QTimer>
#include <KDesktopFile>
#include <KDesktopFileAction>
#include <KIO/ApplicationLauncherJob>
#include <KJob>
#include <cstdio>

int main(int argc, char **argv)
{
    QGuiApplication app(argc, argv);
    if (argc != 3) return 2;
    const KDesktopFile file(QString::fromLocal8Bit(argv[1]));
    if (file.actions().size() != 1) return 2;
    auto *job = new KIO::ApplicationLauncherJob(file.actions().first());
    job->setUiDelegate(nullptr);
    job->setUrls({QUrl::fromLocalFile(QString::fromLocal8Bit(argv[2]))});
    QEventLoop loop;
    int result = 2;
    QObject::connect(job, &KJob::result, &loop, [&](KJob *done) {
        result = done->error() ? 1 : 0;
        std::printf("error=%d mensaje=%s\n", done->error(), done->errorText().toUtf8().constData());
        loop.quit();
    });
    QTimer::singleShot(5000, &loop, &QEventLoop::quit);
    job->start();
    if (result == 2) loop.exec();
    return result;
}
