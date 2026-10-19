#include "officeconverter.h"

#include "localfile.h"

#include <QtConcurrentRun>
#include <QDir>
#include <QFileInfo>
#include <QFutureWatcher>
#include <QProcess>
#include <QProcessEnvironment>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTimer>
#include <QUrl>
#include <atomic>
#include <csignal>
#include <memory>
struct OfficeConverter::Job {
    QTemporaryDir directory{QDir::tempPath() + "/quicklook-office-XXXXXX"};
    std::atomic_bool cancelled{false};
    QByteArray source, stamp;
    QString input;
    QString pdf() const { return directory.filePath("input.pdf"); }
};

OfficeConverter::OfficeConverter(QObject *parent, int timeoutMs)
    : QObject(parent), m_timeoutMs(timeoutMs) {}

OfficeConverter::~OfficeConverter() { cancel(); }

void OfficeConverter::cancel()
{
    if (m_timeout) {
        m_timeout->stop();
        m_timeout->deleteLater();
        m_timeout = nullptr;
    }
    if (m_active) { m_active->cancelled = true; }
    m_active.reset();
    if (m_process) {
        m_process->disconnect(this);
        const qint64 pid = m_process->processId();
        if (pid > 0) { ::kill(-pid, SIGKILL); }
        if (m_process->state() != QProcess::NotRunning) { m_process->kill(); }
        else { m_process->deleteLater(); }
        m_process = nullptr;
    }
}

void OfficeConverter::open(const QByteArray &path)
{
    cancel();
    QString program = QStandardPaths::findExecutable("libreoffice");
    if (program.isEmpty()) { program = QStandardPaths::findExecutable("soffice"); }
    if (program.isEmpty()) {
        Q_EMIT failed("Para estos documentos se necesita LibreOffice (libreoffice-fresh o libreoffice-still en Arch). Imágenes, texto, PDF y carpetas siguen disponibles.");
        return;
    }
    const QString format = QFileInfo(QFile::decodeName(path)).suffix().toLower();
    if (!QStringList{"odt", "doc", "docx", "ods", "xls", "xlsx"}.contains(format)) {
        Q_EMIT failed("Este formato de oficina no tiene vista previa.");
        return;
    }
    QFile file;
    const QString error = openPreviewFile(file, path);
    if (!error.isEmpty()) { Q_EMIT failed(error); return; }
    if (file.size() > FileLimit) {
        Q_EMIT failed("El documento supera el límite de 64 MiB para la vista previa.");
        return;
    }
    const QByteArray stamp = sourceStamp(file);
    if (stamp.isEmpty()) { Q_EMIT failed("No se pudo comprobar el documento."); return; }
    if (m_cached && m_cached->source == path && m_cached->stamp == stamp
        && QFileInfo::exists(m_cached->pdf())) {
        Q_EMIT converted(m_cached->pdf());
        return;
    }
    auto job = std::make_shared<Job>();
    if (!job->directory.isValid()) {
        Q_EMIT failed("No se pudo crear la carpeta temporal para la vista previa.");
        return;
    }
    job->source = path;
    job->stamp = stamp;
    job->input = job->directory.filePath("input." + format);
    m_active = job;
    auto *watcher = new QFutureWatcher<QString>(this);
    connect(watcher, &QFutureWatcher<QString>::finished, this, [this, watcher, job, program] {
        const QString error = watcher->result();
        watcher->deleteLater();
        if (m_active != job) { return; }
        if (!error.isEmpty()) { fail(error); return; }
        startProcess(job, program);
    });
    watcher->setFuture(QtConcurrent::run([job] { return prepare(job); }));
    m_timeout = new QTimer(this);
    m_timeout->setSingleShot(true);
    connect(m_timeout, &QTimer::timeout, this, [this, job] {
        if (m_active == job) { fail("LibreOffice ha superado el tiempo límite de la vista previa."); }
    });
    m_timeout->start(m_timeoutMs);
}

QByteArray OfficeConverter::sourceStamp(const QFile &file)
{
    struct stat status{};
    if (::fstat(file.handle(), &status) != 0) { return {}; }
    return QByteArray::number(status.st_dev) + ':' + QByteArray::number(status.st_ino)
        + ':' + QByteArray::number(status.st_size)
        + ':' + QByteArray::number(status.st_mtim.tv_sec) + ':' + QByteArray::number(status.st_mtim.tv_nsec)
        + ':' + QByteArray::number(status.st_ctim.tv_sec) + ':' + QByteArray::number(status.st_ctim.tv_nsec);
}

QString OfficeConverter::prepare(const std::shared_ptr<Job> &job)
{
    if (job->cancelled) { return {}; }
    QFile source;
    const QString error = openPreviewFile(source, job->source);
    if (!error.isEmpty()) { return error; }
    if (sourceStamp(source) != job->stamp) { return "El documento ha cambiado. Vuelve a abrir su vista previa."; }
    QFile copy(job->input);
    if (!copy.open(QIODevice::WriteOnly | QIODevice::NewOnly)) { return copy.errorString(); }
    qint64 copied = 0;
    while (!job->cancelled) {
        const QByteArray bytes = source.read(128 * 1024);
        if (source.error() != QFileDevice::NoError) { return source.errorString(); }
        if (bytes.isEmpty()) { break; }
        copied += bytes.size();
        if (copied > FileLimit) { return "El documento supera el límite de 64 MiB para la vista previa."; }
        if (copy.write(bytes) != bytes.size()) { return copy.errorString(); }
    }
    if (job->cancelled) { return {}; }
    if (sourceStamp(source) != job->stamp) { return "El documento ha cambiado. Vuelve a abrir su vista previa."; }
    const QString profile = job->directory.filePath("profile/user");
    if (!QDir().mkpath(profile)) { return "No se pudo preparar el perfil temporal de LibreOffice."; }
    QFile registry(profile + "/registrymodifications.xcu");
    if (!registry.open(QIODevice::WriteOnly | QIODevice::NewOnly)) { return registry.errorString(); }
    const QByteArray settings = R"(<?xml version="1.0" encoding="UTF-8"?>
<oor:items xmlns:oor="http://openoffice.org/2001/registry">
<item oor:path="/org.openoffice.Office.Common/Security/Scripting">
 <prop oor:name="DisableMacrosExecution" oor:op="fuse"><value>true</value></prop>
 <prop oor:name="MacroSecurityLevel" oor:op="fuse"><value>3</value></prop>
</item>
<item oor:path="/org.openoffice.Office.Writer/Content/Update">
 <prop oor:name="Link" oor:op="fuse"><value>2</value></prop>
</item>
<item oor:path="/org.openoffice.Office.Calc/Content/Update">
 <prop oor:name="Link" oor:op="fuse"><value>2</value></prop>
</item>
</oor:items>)";
    if (registry.write(settings) != settings.size()) { return registry.errorString(); }
    return {};
}

void OfficeConverter::startProcess(const std::shared_ptr<Job> &job, const QString &program)
{
    auto *process = new QProcess(this);
    m_process = process;
    process->setUnixProcessParameters(QProcess::UnixProcessFlag::CreateNewSession);
    auto environment = QProcessEnvironment::systemEnvironment();
    environment.insert("SAL_USE_VCLPLUGIN", "svp");
    process->setProcessEnvironment(environment);
    process->setWorkingDirectory(job->directory.path());
    process->setStandardOutputFile(QProcess::nullDevice());
    process->setStandardErrorFile(QProcess::nullDevice());
    // Mantener el directorio vivo hasta que haya terminado todo el proceso,
    // también si la selección cambia y se desconectan los avisos de la UI.
    connect(process, &QProcess::finished, process, [process, job] { process->deleteLater(); });
    connect(process, &QProcess::finished, this, [this, job](int exitCode, QProcess::ExitStatus status) {
        if (m_active != job) { return; }
        m_process = nullptr;
        if (status != QProcess::NormalExit || exitCode != 0) {
            fail("LibreOffice no pudo convertir el documento. Ábrelo en su aplicación.");
            return;
        }
        QFile pdf(job->pdf());
        if (!pdf.open(QIODevice::ReadOnly) || pdf.size() > FileLimit || !pdf.read(5).startsWith("%PDF-")) {
            fail("No se pudo generar la vista previa. El documento puede estar dañado o protegido con contraseña.");
            return;
        }
        m_active.reset();
        m_timeout->stop();
        m_timeout->deleteLater();
        m_timeout = nullptr;
        m_cached = job;
        Q_EMIT converted(job->pdf());
    });
    connect(process, &QProcess::errorOccurred, this, [this, job](QProcess::ProcessError error) {
        if (m_active == job && error == QProcess::FailedToStart) {
            fail("No se pudo iniciar LibreOffice para generar la vista previa.");
        }
    });
    const QString profile = QUrl::fromLocalFile(job->directory.filePath("profile")).toString(QUrl::FullyEncoded);
    process->start(program, {"-env:UserInstallation=" + profile, "--headless", "--norestore",
        "--convert-to", "pdf", "--outdir", job->directory.path(), job->input});
}

void OfficeConverter::fail(const QString &message) { cancel(); Q_EMIT failed(message); }
