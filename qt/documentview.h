#pragma once

#include "localfile.h"
#include "folderview.h"
#include "officeconverter.h"

#include <algorithm>
#include <cstdint>
#include <memory>

#include <QFile>
#include <QFileInfo>
#include <QCoreApplication>
#include <QFontDatabase>
#include <QLabel>
#include <QLibrary>
#include <QPlainTextEdit>
#include <QStringDecoder>
#include <QVBoxLayout>

// Lectura acotada de texto. No interpreta HTML, Markdown ni ejecuta contenido.
class DocumentView final : public QWidget
{
public:
    static constexpr qint64 TextLimit = 512 * 1024;
    static constexpr int LineLimit = 10000;

    explicit DocumentView(const QColor &background, QWidget *parent) : QWidget(parent)
    {
        setAutoFillBackground(true);
        QPalette colors = palette();
        colors.setColor(QPalette::Window, background);
        colors.setColor(QPalette::Base, background);
        setPalette(colors);
        auto *layout = new QVBoxLayout(this);
        m_notice = new QLabel(this);
        m_notice->setTextFormat(Qt::PlainText);
        m_notice->setWordWrap(true);
        layout->addWidget(m_notice);
        m_text = new QPlainTextEdit(this);
        m_text->setObjectName("quicklook-text");
        m_text->setReadOnly(true);
        m_text->setFrameShape(QFrame::NoFrame);
        m_text->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
        m_text->setLineWrapMode(QPlainTextEdit::NoWrap);
        layout->addWidget(m_text);
        m_office = new OfficeConverter(this);
        connect(m_office, &OfficeConverter::converted, this, [this](const QString &pdf) {
            m_notice->hide();
            openPdf(QFile::encodeName(pdf));
            setProperty("quicklookState", state());
        });
        connect(m_office, &OfficeConverter::failed, this, [this](const QString &error) { fail(error); });
    }

    void open(const QString &path, uint32_t kind) { open(QFile::encodeName(path), kind); }

    void open(const QByteArray &path, uint32_t kind)
    {
        if (path == m_path && kind == m_kind) { return; }
        m_path = path;
        m_kind = kind;
        m_state = 1;
        m_truncated = false;
        m_office->cancel();
        m_text->clear();
        m_text->hide();
        delete m_pdf;
        m_pdf = nullptr;
        delete m_folder;
        m_folder = nullptr;
        m_notice->clear();
        m_notice->hide();
        m_notice->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
        if (kind == 5) {
            m_notice->setAlignment(Qt::AlignCenter);
            m_notice->setText("Formato no compatible");
            m_notice->show();
            return;
        }
        if (kind == 2) {
            openPdf(path);
            return;
        }
        if (kind == 3) {
            if (!pdfFactory()) { pdfUnavailable(); return; }
            m_state = 0;
            m_notice->setText("Generando vista previa con LibreOffice…");
            m_notice->show();
            m_office->open(path);
            return;
        }
        if (kind == 4) {
            const int descriptor = ::open(path.constData(), O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NONBLOCK);
            if (descriptor < 0) { fail(QString::fromLocal8Bit(std::strerror(errno))); return; }
            ::close(descriptor);
            const QString directory = QFile::decodeName(path);
            if (QFile::encodeName(directory) != path) {
                fail("La vista previa de carpetas requiere un nombre compatible con UTF-8.");
                return;
            }
            m_folder = new FolderView(directory, this);
            layout()->addWidget(m_folder);
            m_folder->show();
            return;
        }
        QFile file;
        const QString error = openPreviewFile(file, path);
        if (!error.isEmpty()) { fail(error); return; }
        QByteArray bytes = file.read(TextLimit + 1);
        if (file.error() != QFileDevice::NoError) { fail(file.errorString()); return; }
        if (std::any_of(bytes.cbegin(), bytes.cend(), [](unsigned char byte) {
            return byte < 32 && byte != '\t' && byte != '\r' && byte != '\n' && byte != 12;
        })) { fail("El archivo contiene datos binarios; no es texto compatible."); return; }
        m_truncated = bytes.size() > TextLimit;
        bytes.truncate(qMin(qint64(bytes.size()), TextLimit));
        if (m_truncated) {
            const auto newline = bytes.lastIndexOf('\n');
            if (newline > 0) { bytes.truncate(newline + 1); }
        }
        qsizetype end = -1;
        for (int line = 0; line < LineLimit; ++line) {
            end = bytes.indexOf('\n', end + 1);
            if (end < 0) { break; }
        }
        if (end >= 0 && end + 1 < bytes.size()) {
            bytes.truncate(end + 1);
            m_truncated = true;
        }
        // El decodificador con estado conserva fuera del texto una secuencia
        // UTF-8 incompleta al alcanzar el límite, sin añadir un carácter roto.
        QStringDecoder decoder(QStringDecoder::Utf8, m_truncated ? QStringConverter::Flag::Default : QStringConverter::Flag::Stateless);
        QString text = decoder.decode(bytes);
        if (decoder.hasError()) { text = QString::fromLatin1(bytes); }
        m_text->setPlainText(text);
        m_text->show();
        if (m_truncated) {
            m_notice->setText("Vista previa limitada a 512 KiB o 10 000 líneas. Abre el archivo para leerlo completo.");
            m_notice->show();
        }
    }

    void reset()
    {
        m_office->cancel();
        m_path.clear();
        m_text->clear();
        delete m_pdf;
        m_pdf = nullptr;
        delete m_folder;
        m_folder = nullptr;
        hide();
    }

    uint32_t state() const {
        if (m_folder) { return m_folder->property("quicklookState").toUInt(); }
        return m_pdf ? m_pdf->property("quicklookState").toUInt() : m_state;
    }
    bool truncated() const { return m_truncated; }
    QString error() const { return m_pdf ? m_pdf->property("quicklookError").toString() : m_state == 2 ? m_notice->text() : QString(); }
    int pageCount() const { return m_pdf ? m_pdf->property("quicklookPages").toInt() : 0; }
    int entryCount() const { return m_folder ? m_folder->property("quicklookEntries").toInt() : 0; }

private:
    using Factory = QWidget *(*)(const QByteArray &, QWidget *);

    Factory pdfFactory()
    {
        if (!m_pdfLibrary || !m_pdfLibrary->isLoaded()) {
            // El menú puede invocar ~/.local/bin/quicklook como enlace a /usr/bin.
            // Buscar respecto al ejecutable real, no al directorio del enlace.
            const QString executableDirectory = QFileInfo(QCoreApplication::applicationFilePath()).canonicalPath();
            for (const QString &directory : {executableDirectory,
                     executableDirectory + "/../lib/quicklook"}) {
                m_pdfLibrary = std::make_unique<QLibrary>(directory + "/quicklook-pdf.so");
                // Qt registra tipos del módulo: conservar el código cargado
                // hasta que termine el proceso, aunque se cambie de archivo.
                m_pdfLibrary->setLoadHints(QLibrary::PreventUnloadHint);
                if (m_pdfLibrary->load()) { break; }
            }
        }
        return reinterpret_cast<Factory>(m_pdfLibrary->resolve("ql_pdf_create"));
    }

    void pdfUnavailable()
    {
        fail("PDF no disponible. Se necesita el módulo PDF de Quick Look y Qt PDF (qt6-webengine en Arch). Imágenes, texto y carpetas siguen disponibles.");
    }

    void openPdf(const QByteArray &path)
    {
        const auto create = pdfFactory();
        if (!create) { pdfUnavailable(); return; }
        m_state = 1;
        m_pdf = create(path, this);
        layout()->addWidget(m_pdf);
        m_pdf->show();
    }

    void fail(const QString &message)
    {
        m_state = 2;
        m_notice->setText(message);
        m_notice->show();
        setProperty("quicklookState", m_state);
    }

    QLabel *m_notice;
    QPlainTextEdit *m_text;
    QWidget *m_pdf = nullptr;
    FolderView *m_folder = nullptr;
    OfficeConverter *m_office;
    std::unique_ptr<QLibrary> m_pdfLibrary;
    QByteArray m_path;
    uint32_t m_kind = 0, m_state = 1;
    bool m_truncated = false;
};
