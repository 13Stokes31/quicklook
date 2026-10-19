#pragma once

#include <QFile>
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

// Conserva nombres Unix no UTF-8 y evita bloquear al abrir una FIFO que haya
// sustituido al archivo entre el listado y la selección. Vacío significa éxito.
inline QString openPreviewFile(QFile &file, const QByteArray &path)
{
    const int descriptor = ::open(path.constData(), O_RDONLY | O_CLOEXEC | O_NONBLOCK);
    if (descriptor < 0) { return QString::fromLocal8Bit(std::strerror(errno)); }
    struct stat status{};
    if (::fstat(descriptor, &status) != 0 || !S_ISREG(status.st_mode)) {
        ::close(descriptor);
        return "La vista previa requiere un archivo normal.";
    }
    if (!file.open(descriptor, QIODevice::ReadOnly, QFileDevice::AutoCloseHandle)) {
        ::close(descriptor);
        return file.errorString();
    }
    return {};
}
