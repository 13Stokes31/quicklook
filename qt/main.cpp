#include "backend.h"
#include "documentview.h"
#include <QApplication>
#include <QDBusConnection>
#include <QDBusInterface>
#include <QDBusReply>
#include <QDynamicPropertyChangeEvent>
#include <QFile>
#include <QFileDialog>
#include <QImage>
#include <QJsonDocument>
#include <QJsonObject>
#include <QKeyEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPointer>
#include <QScreen>
#include <QTextStream>
#include <QTimer>
#include <QVariantMap>
#include <QWidget>
#include <QWindow>
#include <KWindowEffects>
#include <memory>

class BlurGeometry final : public QObject
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.quicklook.Blur")
public:
    explicit BlurGeometry(QObject *parent) : QObject(parent) {}
    void update(const QRectF &rect, double radius, bool enabled)
    {
        if (rect == m_rect && radius == m_radius && enabled == m_enabled) { return; }
        m_rect = rect;
        m_radius = radius;
        m_enabled = enabled;
        Q_EMIT geometryChanged(rect.x(), rect.y(), rect.width(), rect.height(), radius, enabled);
    }
public Q_SLOTS:
    QVariantMap imageGeometry() const
    {
        return {{"x", m_rect.x()}, {"y", m_rect.y()}, {"width", m_rect.width()},
                {"height", m_rect.height()}, {"radius", m_radius}, {"enabled", m_enabled}};
    }
Q_SIGNALS:
    void geometryChanged(double x, double y, double width, double height, double radius, bool enabled);
private:
    QRectF m_rect;
    double m_radius = 0;
    bool m_enabled = false;
};

class Preview final : public QWidget
{
    enum class BlurMode { Opaque, Native, Custom };
public:
    Preview(QuickLookBackend *backend, bool solid, bool report)
        : m_backend(backend, ql_destroy), m_geometry(this), m_solid(solid), m_report(report)
    {
        setWindowFlags(Qt::Window | Qt::FramelessWindowHint);
        setAttribute(Qt::WA_TranslucentBackground);
        m_background = QColor::fromRgb(ql_background(backend));
        m_filename = new QLabel(this);
        m_filename->setObjectName("quicklook-filename");
        m_filename->setTextFormat(Qt::PlainText);
        m_filename->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
        m_filename->setForegroundRole(QPalette::WindowText);
        qApp->installEventFilter(this);
        auto bus = QDBusConnection::sessionBus();
        m_service = QString("org.quicklook.Blur.p%1").arg(QCoreApplication::applicationPid());
        m_registered = bus.registerService(m_service)
            && bus.registerObject("/QuickLook", &m_geometry, QDBusConnection::ExportAllSlots | QDBusConnection::ExportAllSignals);
        connect(&m_poll, &QTimer::timeout, this, [this] {
            if (ql_poll(m_backend.get())) { synchronize(); }
            if (ql_state(m_backend.get()) != 0) { m_poll.stop(); }
        });
        m_poll.setInterval(16);
        connect(&m_availability, &QTimer::timeout, this, &Preview::checkEffect);
        m_availability.setInterval(1000);
        m_poll.start();
    }

    ~Preview() override
    {
        m_poll.stop();
        m_availability.stop();
        auto bus = QDBusConnection::sessionBus();
        bus.unregisterObject("/QuickLook");
        if (m_registered) { bus.unregisterService(m_service); }
    }

protected:
    bool eventFilter(QObject *object, QEvent *event) override
    {
        // Los lectores de documentos reciben foco para seleccionar/copiar y
        // desplazarse; los controles comunes siguen funcionando dentro de ellos.
        auto *widget = qobject_cast<QWidget *>(object);
        if (widget && m_document && (widget == m_document || m_document->isAncestorOf(widget)) && event->type() == QEvent::DynamicPropertyChange
            && static_cast<QDynamicPropertyChangeEvent *>(event)->propertyName() == "quicklookState") {
            QTimer::singleShot(0, this, &Preview::synchronize);
        }
        // Reclamar los controles antes de que el lector resuelva sus atajos,
        // tanto en la ventana nativa como en los widgets con foco.
        const bool ownTarget = object == windowHandle() || (widget && (widget == this || isAncestorOf(widget)));
        if (ownTarget && (event->type() == QEvent::KeyPress || event->type() == QEvent::ShortcutOverride)) {
            auto *key = static_cast<QKeyEvent *>(event);
            if (key->modifiers() == Qt::NoModifier && (key->key() == Qt::Key_Escape
                || key->key() == Qt::Key_Space || key->key() == Qt::Key_Left || key->key() == Qt::Key_Right)) {
                if (event->type() == QEvent::ShortcutOverride) { key->accept(); }
                else { keyPressEvent(key); }
                return true;
            }
        }
        return QWidget::eventFilter(object, event);
    }

    void showEvent(QShowEvent *) override
    {
        connect(windowHandle(), &QWindow::screenChanged, this, [this] { fitScreen(); });
        fitScreen();
        checkEffect();
        if (!m_solid) { m_availability.start(); }
        if (m_report) { QTimer::singleShot(100, this, &Preview::report); }
    }

    void changeEvent(QEvent *event) override
    {
        if (event->type() == QEvent::ActivationChange) {
            if (isActiveWindow()) { m_focused = true; }
            else if (m_focused) { close(); }
        }
        QWidget::changeEvent(event);
    }

    bool event(QEvent *event) override
    {
        if (event->type() == QEvent::DevicePixelRatioChange) {
            QTimer::singleShot(0, this, [this] { prepareImage(); update(); });
        }
        return QWidget::event(event);
    }

    void keyPressEvent(QKeyEvent *event) override
    {
        if (event->key() == Qt::Key_Escape || (event->key() == Qt::Key_Space && !event->isAutoRepeat())) {
            close();
        } else if (event->key() == Qt::Key_Left || event->key() == Qt::Key_Right) {
            if (ql_navigate(m_backend.get(), event->key() == Qt::Key_Right)) {
                synchronize();
                m_poll.start();
            }
        } else { QWidget::keyPressEvent(event); }
    }

    void resizeEvent(QResizeEvent *) override { prepareImage(); }

    void paintEvent(QPaintEvent *) override
    {
        QPainter painter(this);
        QColor color = m_background;
        color.setAlphaF(m_blurMode != BlurMode::Opaque ? 0.40 : 1.0);
        painter.setCompositionMode(QPainter::CompositionMode_Source);
        painter.fillRect(rect(), Qt::transparent);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.fillPath(m_framePath, color);
        painter.setCompositionMode(QPainter::CompositionMode_SourceOver);
        painter.setClipPath(m_framePath);
        if (m_pixels) {
            const QImage image(reinterpret_cast<const uchar *>(m_pixels), m_pixelSize.width(),
                m_pixelSize.height(), m_pixelSize.width() * 4, QImage::Format_RGB32);
            painter.drawImage(m_imageRect, image, m_sourceRect);
        } else if (ql_kind(m_backend.get()) == 0) {
            painter.setPen(Qt::NoPen);
            if (ql_state(m_backend.get()) == 2) {
                painter.setPen(QPen(QColor(0xe5, 0x73, 0x73), 5));
                const QPoint center = rect().center();
                painter.drawLine(center + QPoint(-20, -20), center + QPoint(20, 20));
                painter.drawLine(center + QPoint(-20, 20), center + QPoint(20, -20));
            } else {
                painter.setBrush(QColor(0xaa, 0xaa, 0xaa));
                for (int offset : {-12, 0, 12}) {
                    painter.drawEllipse(QPointF(width() / 2.0 + offset, height() / 2.0), 3, 3);
                }
            }
        }
    }

private:
    void checkEffect()
    {
        BlurMode mode = BlurMode::Opaque;
        if (!m_solid && m_registered && QGuiApplication::platformName() == "wayland") {
            QDBusInterface effect("org.kde.KWin", "/QuickLookSoftBlur", "org.quicklook.SoftBlurEffect");
            effect.setTimeout(250);
            const QDBusReply<bool> reply = effect.call("ready");
            const QDBusReply<int> protocol = effect.call("protocolVersion");
            if (reply.isValid() && reply.value() && protocol.isValid() && protocol.value() == 2) {
                mode = BlurMode::Custom;
            }
        }
        if (!m_solid && mode == BlurMode::Opaque
            && KWindowEffects::isEffectAvailable(KWindowEffects::BlurBehind)) {
            mode = BlurMode::Native;
        }
        if (mode != m_blurMode) {
            m_blurMode = mode;
            publishGeometry();
            update();
            if (m_report) { QTimer::singleShot(100, this, &Preview::report); }
        }
    }

    void fitScreen()
    {
        QScreen *monitor = screen();
        if (!monitor) { return; }
        if (m_screen != monitor) {
            if (m_screen) { disconnect(m_screen, nullptr, this, nullptr); }
            m_screen = monitor;
            connect(monitor, &QScreen::geometryChanged, this, [this] { fitScreen(); });
        }
        const QRect area = monitor->geometry();
        const QSize size(qMax(1, qRound(area.width() * 0.90)), qMax(1, qRound(area.height() * 0.90)));
        if (size != this->size()) {
            setFixedSize(size);
            move(area.center() - rect().center());
        }
        prepareImage();
    }

    void prepareImage()
    {
        if (m_frameSize != size()) {
            m_frameSize = size();
            m_cornerRadius = qMin(12.0, qMin(width(), height()) * 0.05);
            m_framePath = QPainterPath();
            m_framePath.addRoundedRect(QRectF(rect()), m_cornerRadius, m_cornerRadius);
            m_frameRegion = QRegion(m_framePath.toFillPolygon().toPolygon());
            setMask(m_frameRegion);
        }
        m_pixels = nullptr;
        m_imageRect = {};
        // El nombre ocupa el margen superior, fuera del área de contenido.
        // Usar siempre la ruta seleccionada, incluso durante la carga o un error.
        const int insetX = qRound(width() * (1.0 - 85.0 / 90.0) / 2.0);
        const int insetY = qRound(height() * (1.0 - 85.0 / 90.0) / 2.0);
        const QString path = QFile::decodeName(QByteArray(ql_path(m_backend.get())));
        const QString name = QFileInfo(path).fileName();
        const QString filename = name.isEmpty() ? path : name;
        m_filename->setGeometry(insetX, 0, width() - 2 * insetX, insetY);
        QFont font = this->font();
        if (QFontMetrics(font).height() > insetY - 2) {
            font.setPixelSize(qMax(1, insetY - 2));
        }
        m_filename->setFont(font);
        m_filename->setText(m_filename->fontMetrics().elidedText(filename, Qt::ElideMiddle, m_filename->width()));
        m_filename->setAccessibleName(filename);
        m_filename->setToolTip("<qt>" + filename.toHtmlEscaped() + "</qt>");
        if (ql_kind(m_backend.get()) != 0) {
            if (!m_document) { m_document = new DocumentView(m_background, this); }
            m_imageRect = rect().adjusted(insetX, insetY, -insetX, -insetY);
            m_document->setGeometry(m_imageRect.toAlignedRect());
            m_document->open(QByteArray(ql_path(m_backend.get())), ql_kind(m_backend.get()));
            m_document->show();
            setWindowTitle(QString::fromUtf8(ql_title(m_backend.get())));
            if (m_document->state() == 2) { setWindowTitle(windowTitle() + " — " + m_document->error()); }
            publishGeometry();
            return;
        }
        if (m_document) { m_document->reset(); }
        const double scale = devicePixelRatioF();
        const QSize surface(qMax(1, qRound(width() * scale)), qMax(1, qRound(height() * scale)));
        ImageRect source{};
        m_pixels = ql_render(m_backend.get(), surface.width(), surface.height(), &source);
        if (m_pixels) {
            m_pixelSize = QSize(source.width, source.height);
            m_sourceRect = QRect(0, 0, source.width, source.height);
            m_imageRect = QRectF(source.x / scale, source.y / scale, source.width / scale, source.height / scale);
        } else if (ql_state(m_backend.get()) == 0) {
            m_poll.start();
        }
        setWindowTitle(QString::fromUtf8(ql_title(m_backend.get())));
        publishGeometry();
    }

    void publishGeometry()
    {
        // Proporciones del cliente: Qt y KWin pueden usar escalas distintas.
        const QRectF normalized(m_imageRect.x() / width(), m_imageRect.y() / height(),
            m_imageRect.width() / width(), m_imageRect.height() / height());
        m_geometry.update(normalized, m_cornerRadius / qMin(width(), height()), m_blurMode == BlurMode::Custom);
        if (windowHandle()) {
            // Un solo proveedor a la vez; el blur nativo también excluye la imagen.
            KWindowEffects::enableBlurBehind(windowHandle(), m_blurMode == BlurMode::Native,
                m_frameRegion.subtracted(QRegion(m_imageRect.toAlignedRect())));
        }
    }

    void synchronize()
    {
        prepareImage();
        update();
        if (m_report) {
            QTimer::singleShot(100, this, &Preview::report);
        }
    }

    void report() const
    {
        const bool document = ql_kind(m_backend.get()) != 0;
        const QJsonObject report{{"title", windowTitle()}, {"state", int(document && m_document ? m_document->state() : ql_state(m_backend.get()))},
            {"kind", ql_kind(m_backend.get()) == 5 ? "unsupported" : ql_kind(m_backend.get()) == 4 ? "folder" : ql_kind(m_backend.get()) == 3 ? "office"
                : ql_kind(m_backend.get()) == 2 ? "pdf" : document ? "text" : "image"},
            {"text_truncated", document && m_document && m_document->truncated()},
            {"pdf_pages", document && m_document ? m_document->pageCount() : 0},
            {"folder_entries", document && m_document ? m_document->entryCount() : 0},
            {"filename", m_filename->accessibleName()}, {"filename_label", m_filename->text()},
            {"filename_x", m_filename->x()}, {"filename_y", m_filename->y()},
            {"filename_width", m_filename->width()}, {"filename_height", m_filename->height()},
            {"frame_width", width()}, {"frame_height", height()}, {"device_pixel_ratio", devicePixelRatioF()},
            {"screen_width", screen()->geometry().width()}, {"screen_height", screen()->geometry().height()},
            {"image_x", m_imageRect.x()}, {"image_y", m_imageRect.y()},
            {"image_width", m_imageRect.width()}, {"image_height", m_imageRect.height()},
            {"blur_enabled", m_blurMode != BlurMode::Opaque},
            {"blur_mode", m_blurMode == BlurMode::Custom ? "custom" : m_blurMode == BlurMode::Native ? "native" : "opaque"},
            {"corner_radius", m_cornerRadius},
            {"opacity", m_blurMode != BlurMode::Opaque ? 0.40 : 1.0}};
        QTextStream(stdout) << QJsonDocument(report).toJson(QJsonDocument::Compact) << '\n';
    }

    std::unique_ptr<QuickLookBackend, decltype(&ql_destroy)> m_backend;
    DocumentView *m_document = nullptr;
    QLabel *m_filename = nullptr;
    BlurGeometry m_geometry;
    QTimer m_poll, m_availability;
    QPointer<QScreen> m_screen;
    QColor m_background;
    QString m_service;
    QSize m_pixelSize;
    QRect m_sourceRect;
    QRectF m_imageRect;
    QSize m_frameSize;
    QPainterPath m_framePath;
    QRegion m_frameRegion;
    double m_cornerRadius = 0;
    const uint32_t *m_pixels = nullptr;
    BlurMode m_blurMode = BlurMode::Opaque;
    bool m_solid, m_report, m_registered = false, m_focused = false;
};

int main(int argc, char **argv)
{
    QByteArray path;
    bool solid = false, report = false, literal = false, choose = false;
    for (int i = 1; i < argc; ++i) {
        const QByteArray arg(argv[i]);
        if (!literal && arg == "--") { literal = true; continue; }
        if (!literal && arg == "--solid") { solid = true; continue; }
        if (!literal && arg == "--report") { report = true; continue; }
        if (!literal && arg == "--choose-image") { choose = true; continue; }
        if (!literal && (arg == "--help" || arg == "-h")) {
            QTextStream(stdout) << "uso: quicklook-qt [--solid] [--] archivo-o-carpeta\nFlechas: navegar. Escape/Espacio o perder el foco: cerrar.\n";
            return 0;
        }
        if (!path.isEmpty()) { QTextStream(stderr) << "Indica un único archivo.\n"; return 1; }
        path = arg;
    }
    if (path.isEmpty() && !choose) { QTextStream(stderr) << "Indica un archivo.\n"; return 1; }
    // Solapar la lectura/decodificación con la inicialización de Qt.
    std::unique_ptr<QuickLookBackend, decltype(&ql_destroy)> backend(nullptr, ql_destroy);
    if (!path.isEmpty()) {
        backend.reset(ql_create(reinterpret_cast<const uint8_t *>(path.constData()), path.size()));
        if (!backend) { return 1; }
    }
    QApplication app(argc, argv);
    app.setApplicationName("quicklook");
    app.setDesktopFileName("org.quicklook.SoftBlur");
    if (path.isEmpty()) {
        const QString selected = QFileDialog::getOpenFileName(nullptr, "Probar Quick Look",
            QDir::homePath(), "Archivos compatibles (*.png *.jpg *.jpeg *.webp *.bmp *.gif *.tif *.tiff *.txt *.md *.pdf);;Todos los archivos (*)");
        if (selected.isEmpty()) { return 0; }
        path = QFile::encodeName(selected);
    }
    if (!backend) {
        backend.reset(ql_create(reinterpret_cast<const uint8_t *>(path.constData()), path.size()));
        if (!backend) { return 1; }
    }
    Preview preview(backend.release(), solid, report);
    preview.show();
    return app.exec();
}
#include "main.moc"
