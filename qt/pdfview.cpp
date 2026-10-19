#include "localfile.h"
#include <QLabel>
#include <QPdfDocument>
#include <QPdfPageNavigator>
#include <QPdfView>
#include <QVBoxLayout>

class PdfPreview final : public QWidget
{
public:
    ~PdfPreview() override
    {
        // Desconectar mientras QPdfView conserva su implementación interna;
        // QPdfDocument::close emite señales durante su destrucción.
        m_view->setDocument(nullptr);
        delete m_document;
    }

    PdfPreview(const QByteArray &path, QWidget *parent) : QWidget(parent)
    {
        setProperty("quicklookState", 0u);
        auto *layout = new QVBoxLayout(this);
        layout->setContentsMargins(0, 0, 0, 0);
        m_view = new QPdfView(this);
        m_view->setObjectName("quicklook-pdf-pages");
        m_view->setFrameShape(QFrame::NoFrame);
        m_view->setPageMode(QPdfView::PageMode::MultiPage);
        m_view->setZoomMode(QPdfView::ZoomMode::FitToWidth);
        m_view->setFocusPolicy(Qt::StrongFocus);
        QPalette colors = palette();
        colors.setColor(QPalette::Dark, colors.color(QPalette::Window));
        m_view->setPalette(colors);
        layout->addWidget(m_view);
        m_label = new QLabel("Cargando PDF…", this);
        m_label->setTextFormat(Qt::PlainText);
        m_label->setWordWrap(true);
        layout->addWidget(m_label);
        m_document = new QPdfDocument(m_view);
        m_view->setDocument(m_document);
        auto *file = new QFile(m_document);
        const QString error = openPreviewFile(*file, path);
        if (!error.isEmpty()) { fail(error); return; }
        connect(m_document, &QPdfDocument::passwordRequired, this, [this] {
            fail("Este PDF necesita una contraseña. Ábrelo en tu lector de documentos.");
        });
        connect(m_document, &QPdfDocument::statusChanged, this, [this](QPdfDocument::Status status) {
            if (status == QPdfDocument::Status::Ready) {
                setProperty("quicklookPages", m_document->pageCount());
                setProperty("quicklookState", 1u);
                updatePageLabel();
            } else if (status == QPdfDocument::Status::Error) {
                fail(m_document->error() == QPdfDocument::Error::IncorrectPassword
                    ? "Este PDF necesita una contraseña. Ábrelo en tu lector de documentos."
                    : "No se pudo abrir el PDF: el archivo está dañado o usa un formato no compatible.");
            }
        });
        connect(m_view->pageNavigator(), &QPdfPageNavigator::currentPageChanged, this, [this] { updatePageLabel(); });
        // QPdfView renderiza las páginas visibles; no crea una imagen de todo
        // el documento ni renderiza todas las páginas por adelantado.
        m_document->load(file);
    }

private:
    void fail(const QString &error)
    {
        m_view->hide();
        m_label->setText(error);
        setProperty("quicklookError", error);
        setProperty("quicklookState", 2u);
    }

    void updatePageLabel()
    {
        if (m_document->status() != QPdfDocument::Status::Ready) { return; }
        m_label->setText(QString("Página %1 de %2 · Rueda o AvPág/RePág para desplazarte")
            .arg(m_view->pageNavigator()->currentPage() + 1).arg(m_document->pageCount()));
    }

    QPdfView *m_view;
    QPdfDocument *m_document = nullptr;
    QLabel *m_label;
};

extern "C" QWidget *ql_pdf_create(const QByteArray &path, QWidget *parent)
{
    return new PdfPreview(path, parent);
}
