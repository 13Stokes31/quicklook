#include <QCoreApplication>
#include <QDir>
#include <QImage>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTextStream>
#include <cmath>
#include <stdexcept>

static void require(bool value, const char *message)
{
    if (!value) { throw std::runtime_error(message); }
}

static double deviation(const QImage &image)
{
    double sum = 0, squares = 0;
    int count = 0;
    for (int y = image.height() * 3 / 8; y < image.height() * 5 / 8; ++y) {
        for (int x = image.width() / 10; x < image.width() * 275 / 1000; ++x) {
            const double value = image.pixelColor(x, y).red();
            sum += value; squares += value * value; ++count;
        }
    }
    return std::sqrt(std::max(0.0, squares / count - std::pow(sum / count, 2)));
}

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    if (argc != 2) { return 1; }
    try {
        const QDir directory(QString::fromLocal8Bit(argv[1]));
        auto load = [&directory](const char *name) {
            QImage image(directory.filePath(QString::fromLatin1(name) + ".png"));
            require(!image.isNull(), "no se pudo leer la captura");
            return image;
        };
        const QImage reference = load("nativo-3");
        const QImage background = load("background");
        for (const char *name : {"qt-inicial", "qt-nativo", "qt-sin-efecto"}) {
            const QImage rounded = load(name);
            const QPoint corner(qRound(rounded.width() * 0.05) + 1, qRound(rounded.height() * 0.05) + 1);
            require(rounded.pixelColor(corner) == background.pixelColor(corner), "la esquina del marco no es transparente");
        }
        double largest = 0;
        for (const char *name : {"qt-inicial", "qt-rapida", "qt-recargado"}) {
            const QImage image = load(name);
            require(image.size() == reference.size(), "tamaño de captura distinto");
            const QColor center = image.pixelColor(image.width() / 2, image.height() / 2);
            require(center.green() > 250 && center.red() < 5 && center.blue() < 5, "imagen verde incorrecta o translúcida");
            double difference = 0;
            int count = 0;
            for (int y = image.height() * 3 / 8; y < image.height() * 5 / 8; ++y) {
                for (int x = image.width() / 10; x < image.width() * 275 / 1000; ++x) {
                    difference += std::abs(image.pixelColor(x, y).red() - reference.pixelColor(x, y).red());
                    ++count;
                }
            }
            largest = std::max(largest, difference / count);
            require(difference / count < 1.0, "el marco Qt no reproduce el blur suave 3");
        }
        const QImage native = load("nativo-15");
        require(deviation(native) < deviation(reference) / 3, "el control fuerte no difiere del suave");
        double nativeDifference = 0;
        for (const char *name : {"qt-nativo", "qt-nativo-recargado"}) {
            const QImage fallback = load(name);
            require(fallback.size() == native.size(), "tamaño del respaldo nativo distinto");
            require(fallback.pixelColor(fallback.width() / 2, fallback.height() / 2) == QColor(0, 255, 0),
                "la imagen no permanece opaca con blur nativo");
            double difference = 0;
            int count = 0;
            for (int y = fallback.height() * 3 / 8; y < fallback.height() * 5 / 8; ++y) {
                for (int x = fallback.width() / 10; x < fallback.width() * 275 / 1000; ++x) {
                    difference += std::abs(fallback.pixelColor(x, y).red() - native.pixelColor(x, y).red());
                    ++count;
                }
            }
            nativeDifference = std::max(nativeDifference, difference / count);
            require(difference / count < 1.0, "el respaldo no reproduce el blur nativo de KDE");
        }
        const QImage blue = load("qt-horizontal");
        require(blue.pixelColor(blue.width() / 2, blue.height() / 2) == QColor(0, 0, 255), "navegación a azul incorrecta");
        const QImage alpha = load("qt-alpha");
        const QColor transparent = alpha.pixelColor(alpha.width() / 2, alpha.height() / 2);
        require(transparent.red() == 255 && std::abs(transparent.green() - 127) <= 1
            && std::abs(transparent.blue() - 127) <= 1, "el alpha de la imagen no se compone sobre fondo opaco");
        const QImage nativeAlpha = load("qt-nativo-alpha");
        require(nativeAlpha.pixelColor(nativeAlpha.width() / 2, nativeAlpha.height() / 2) == transparent,
            "el alpha de la imagen no permanece opaco con blur nativo");
        const QImage error = load("qt-error");
        require(error.pixelColor(error.width() / 2, error.height() / 2) == QColor(0xe5, 0x73, 0x73), "el error conserva una imagen anterior");
        const QImage solid = load("qt-sin-efecto");
        require(solid.pixelColor(solid.width() / 5, solid.height() / 2) == QColor(Qt::white), "el fondo de respaldo no es opaco");
        QTextStream(stdout) << QJsonDocument(QJsonObject{{"max_mean_pixel_difference_from_native3", largest},
            {"max_mean_pixel_difference_from_native_fallback", nativeDifference},
            {"soft_deviation", deviation(reference)}, {"strong_deviation", deviation(load("nativo-15"))}})
            .toJson(QJsonDocument::Compact) << '\n';
    } catch (const std::exception &error) {
        QTextStream(stderr) << error.what() << '\n'; return 1;
    }
    return 0;
}
