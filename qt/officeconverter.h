#pragma once

#include <QObject>
#include <QPointer>
#include <memory>

class QFile;
class QProcess;
class QTimer;

// Un único trabajo activo y un resultado reutilizable por ventana. Los
// originales se copian en segundo plano; LibreOffice usa un perfil privado.
class OfficeConverter final : public QObject
{
    Q_OBJECT
public:
    static constexpr qint64 FileLimit = 64 * 1024 * 1024;
    explicit OfficeConverter(QObject *parent = nullptr, int timeoutMs = 60000);
    ~OfficeConverter() override;
    void cancel();
    void open(const QByteArray &path);

Q_SIGNALS:
    void converted(const QString &pdf);
    void failed(const QString &error);

private:
    struct Job;
    static QByteArray sourceStamp(const QFile &file);
    static QString prepare(const std::shared_ptr<Job> &job);
    void startProcess(const std::shared_ptr<Job> &job, const QString &program);
    void fail(const QString &message);

    int m_timeoutMs;
    std::shared_ptr<Job> m_active, m_cached;
    QPointer<QProcess> m_process;
    QTimer *m_timeout = nullptr;
};
