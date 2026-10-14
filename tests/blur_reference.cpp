// SPDX-License-Identifier: GPL-3.0-or-later
// Reference window for isolated KWin native blur comparisons.
#include <QApplication>
#include <QCommandLineParser>
#include <QImage>
#include <QPainter>
#include <QScreen>
#include <QTimer>
#include <QWidget>
#include <QWindow>
#include <KWindowEffects>

class Reference final : public QWidget {
public:
    Reference(QImage image, bool background, int stripeWidth)
        : m_image(std::move(image)), m_background(background), m_stripeWidth(stripeWidth) {
        setWindowFlags(Qt::Window | Qt::FramelessWindowHint);
        if (background) {
            setWindowFlag(Qt::WindowDoesNotAcceptFocus);
            setAttribute(Qt::WA_ShowWithoutActivating);
        } else {
            setAttribute(Qt::WA_TranslucentBackground);
            const QSize monitor = screen()->geometry().size();
            setFixedSize(qRound(monitor.width() * .90), qRound(monitor.height() * .90));
        }
    }
protected:
    void showEvent(QShowEvent *) override {
        if (!m_background) {
            for (int delay : {0, 300}) QTimer::singleShot(delay, this, [this] {
                KWindowEffects::enableBlurBehind(windowHandle(), true,
                    QRegion(rect()).subtracted(QRegion(imageRect())));
            });
        }
    }
    void paintEvent(QPaintEvent *) override {
        QPainter painter(this);
        if (m_background) {
            for (int x = 0; x < width(); x += m_stripeWidth)
                painter.fillRect(x, 0, m_stripeWidth, height(),
                    (x / m_stripeWidth) % 2 ? Qt::white : Qt::black);
        } else {
            painter.setCompositionMode(QPainter::CompositionMode_Source);
            painter.fillRect(rect(), QColor(255, 255, 255, 102));
            painter.setCompositionMode(QPainter::CompositionMode_SourceOver);
            painter.setRenderHint(QPainter::SmoothPixmapTransform);
            painter.drawImage(imageRect(), m_image);
        }
    }
private:
    QRect imageRect() const {
        const QSize size = m_image.size().scaled(
            QSize(qRound(width() * 85.0 / 90), qRound(height() * 85.0 / 90)), Qt::KeepAspectRatio);
        return QRect(QPoint((width() - size.width()) / 2, (height() - size.height()) / 2), size);
    }
    QImage m_image;
    bool m_background;
    int m_stripeWidth;
};

int main(int argc, char **argv) {
    QApplication app(argc, argv);
    QCommandLineParser parser;
    parser.addOption(QCommandLineOption("test-background"));
    parser.addOption(QCommandLineOption("test-stripe-width", "Stripe width", "px", "32"));
    parser.addPositionalArgument("image", "Reference image");
    parser.process(app);
    bool valid;
    const int stripe = parser.value("test-stripe-width").toInt(&valid);
    if (!valid || stripe < 4 || stripe > 128) return 1;
    const bool background = parser.isSet("test-background");
    QImage image;
    if (!background) {
        if (parser.positionalArguments().size() != 1) return 1;
        image.load(parser.positionalArguments().first());
        if (image.isNull()) return 1;
    }
    app.setDesktopFileName(background ? "org.quicklook.TestBackground" : "org.quicklook.Viewer");
    Reference window(std::move(image), background, stripe);
    if (background) window.showFullScreen(); else window.show();
    return app.exec();
}
