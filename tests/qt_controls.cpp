// La prueba incluye la interfaz real y envía eventos de Qt sin añadir controles
// remotos al programa instalado. Se ejecuta únicamente en el compositor privado.
#define main quicklook_cli_main
#include "../qt/main.cpp"
#undef main
#include <QSocketNotifier>
#include <QTest>
#include <unistd.h>

int main(int argc, char **argv)
{
    if (argc != 2) { return 1; }
    const QByteArray path(argv[1]);
    QApplication app(argc, argv);
    app.setApplicationName("quicklook");
    app.setDesktopFileName("org.quicklook.SoftBlur");
    auto *backend = ql_create(reinterpret_cast<const uint8_t *>(path.constData()), path.size());
    if (!backend) { return 1; }
    Preview preview(backend, false, true);
    preview.show();
    QSocketNotifier commands(STDIN_FILENO, QSocketNotifier::Read);
    QByteArray pending;
    QObject::connect(&commands, &QSocketNotifier::activated, &app, [&] {
        char input[256];
        const ssize_t count = ::read(STDIN_FILENO, input, sizeof(input));
        if (count <= 0) { app.quit(); return; }
        pending.append(input, count);
        while (pending.contains('\n')) {
            const int end = pending.indexOf('\n');
            const QByteArray command = pending.left(end);
            pending.remove(0, end + 1);
            if (command == "next" || command == "previous") {
                QTest::keyClick(&preview, command == "next" ? Qt::Key_Right : Qt::Key_Left, Qt::NoModifier, 0);
            } else if (command == "content-next" || command == "content-space") {
                QWidget *content = preview.findChild<QWidget *>("quicklook-pdf-pages");
                if (!content || !content->isVisible()) { content = preview.findChild<QPlainTextEdit *>(); }
                if (!content || !content->isVisible()) { app.exit(1); return; }
                content->setFocus();
                QTest::keyClick(content, command == "content-next" ? Qt::Key_Right : Qt::Key_Space, Qt::NoModifier, 0);
            } else if (command == "rapid") {
                for (int i = 0; i < 120; ++i) {
                    QTest::keyClick(&preview, Qt::Key_Right, Qt::NoModifier, 0);
                }
            } else if (command == "repeat-space") {
                QKeyEvent repeat(QEvent::KeyPress, Qt::Key_Space, Qt::NoModifier, " ", true, 1);
                QApplication::sendEvent(&preview, &repeat);
                QTextStream(stdout) << "repeat-space: ignorado\n";
            } else if (command == "space" || command == "escape") {
                QTest::keyClick(&preview, command == "space" ? Qt::Key_Space : Qt::Key_Escape, Qt::NoModifier, 0);
            } else if (command == "focus-away") {
                auto *other = new QWidget;
                other->setAttribute(Qt::WA_DeleteOnClose);
                other->setWindowTitle("quicklook-test-focus");
                other->show();
                other->activateWindow();
                QTimer::singleShot(500, &app, [&app, &preview] {
                    if (preview.isVisible()) { app.exit(1); }
                    else { app.quit(); }
                });
            }
        }
    });
    return app.exec();
}
