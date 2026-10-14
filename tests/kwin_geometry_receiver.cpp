// Recibe únicamente la geometría de las ventanas del KWin privado de pruebas.
#include <QCoreApplication>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusVirtualObject>
#include <QFile>

class Receiver : public QDBusVirtualObject {
    QString m_path;
public:
    explicit Receiver(const QString &path) : m_path(path) {}
    QString introspect(const QString &) const override {
        return QStringLiteral("<interface name='org.quicklook.Geometry'><method name='report'><arg type='s' direction='in'/></method></interface>");
    }
    bool handleMessage(const QDBusMessage &message, const QDBusConnection &connection) override {
        if (message.interface() != QStringLiteral("org.quicklook.Geometry") || message.member() != QStringLiteral("report")) return false;
        QFile log(m_path);
        if (!log.open(QIODevice::WriteOnly | QIODevice::Append)) return false;
        log.write(message.arguments().first().toString().toUtf8() + '\n');
        connection.send(message.createReply());
        return true;
    }
};

int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    if (argc != 2) return 2;
    Receiver receiver(QString::fromLocal8Bit(argv[1]));
    auto connection = QDBusConnection::sessionBus();
    if (!connection.registerService(QStringLiteral("org.quicklook.Geometry")) ||
        !connection.registerVirtualObject(QStringLiteral("/Geometry"), &receiver)) return 2;
    return app.exec();
}
