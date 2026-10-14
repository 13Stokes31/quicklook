#include "backend.h"
#include <QApplication>
#include <QDBusConnection>
#include <QDBusInterface>
#include <QDBusReply>
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
    void showEvent(QShowEvent *) override
    {
        connect(windowHandle(), &QWindow::screenChanged, this, [this] { fitScreen(); });
        fitScreen();
        checkEffect();
        if (!m_solid) { m_availability.start(); }
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
        } else {
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
        const QJsonObject report{{"title", windowTitle()}, {"state", int(ql_state(m_backend.get()))},
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
            QTextStream(stdout) << "uso: quicklook-qt [--solid] [--] imagen\nFlechas: navegar. Escape/Espacio o perder el foco: cerrar.\n";
            return 0;
        }
        if (!path.isEmpty()) { QTextStream(stderr) << "Indica una única imagen.\n"; return 1; }
        path = arg;
    }
    if (path.isEmpty() && !choose) { QTextStream(stderr) << "Indica una imagen.\n"; return 1; }
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
            QDir::homePath(), "Imágenes (*.png *.jpg *.jpeg *.webp *.bmp *.gif *.tif *.tiff);;Todos los archivos (*)");
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
