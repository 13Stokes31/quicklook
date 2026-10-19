#include "../qt/documentview.h"
#include <QTemporaryDir>
#include <QPainter>
#include <QPdfWriter>
#include <QTest>

class Documents final : public QObject
{
    Q_OBJECT
private Q_SLOTS:
    void textIsLiteralAndReadOnly()
    {
        QTemporaryDir dir;
        const QString path = dir.filePath("texto.txt");
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write("<img src=\"https://example.invalid/image\">\nTexto con acentos: café");
        file.close();
        DocumentView view(Qt::white, nullptr);
        view.open(path, 1);
        auto *text = view.findChild<QPlainTextEdit *>();
        QVERIFY(text && text->isReadOnly());
        QVERIFY(text->toPlainText().startsWith("<img src="));
        QVERIFY(text->toPlainText().endsWith(QString::fromUtf8("café")));
        QCOMPARE(view.state(), 1u);
        QVERIFY(!view.truncated());
        view.reset();
        QVERIFY(text->toPlainText().isEmpty());
    }

    void longFilesAndLinesAreBounded()
    {
        QTemporaryDir dir;
        DocumentView view(Qt::white, nullptr);
        const QString path = dir.filePath("grande.log");
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write(QByteArray(DocumentView::TextLimit, 'a') + "extra");
        file.close();
        view.open(path, 1);
        QVERIFY(view.truncated());
        QCOMPARE(view.findChild<QPlainTextEdit *>()->toPlainText().size(), DocumentView::TextLimit);
        view.reset();
        QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
        file.write(QByteArray(DocumentView::LineLimit * 2, '\n'));
        file.close();
        view.open(path, 1);
        QVERIFY(view.truncated());
        QCOMPARE(view.findChild<QPlainTextEdit *>()->toPlainText().size(), DocumentView::LineLimit);
    }

    void latin1AndReadErrors()
    {
        QTemporaryDir dir;
        DocumentView view(Qt::white, nullptr);
        const QString path = dir.filePath("latin1.txt");
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write("caf\xe9");
        file.close();
        view.open(path, 1);
        QCOMPARE(view.findChild<QPlainTextEdit *>()->toPlainText(), QString::fromUtf8("café"));
        view.open(dir.filePath("missing"), 1);
        QCOMPARE(view.state(), 2u);
        QVERIFY(!view.error().isEmpty());
        QVERIFY(view.findChild<QPlainTextEdit *>()->toPlainText().isEmpty());
        QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
        file.write(QByteArray("binary\0data", 11));
        file.close();
        view.open(path, 1);
        QCOMPARE(view.state(), 2u);
    }

    void nativeNamesAndNonRegularFiles()
    {
        QTemporaryDir dir;
        const QByteArray path = QFile::encodeName(dir.path()) + "/text-\xff.txt";
        const int descriptor = ::open(path.constData(), O_WRONLY | O_CREAT | O_EXCL, 0600);
        QVERIFY(descriptor >= 0);
        QFile file;
        QVERIFY(file.open(descriptor, QIODevice::WriteOnly, QFileDevice::AutoCloseHandle));
        file.write("native path");
        file.close();
        DocumentView view(Qt::white, nullptr);
        view.open(path, 1);
        QCOMPARE(view.state(), 1u);
        QCOMPARE(view.findChild<QPlainTextEdit *>()->toPlainText(), QString("native path"));
        QCOMPARE(::unlink(path.constData()), 0);
        const QByteArray fifo = QFile::encodeName(dir.filePath("pipe.txt"));
        QCOMPARE(::mkfifo(fifo.constData(), 0600), 0);
        view.open(fifo, 1);
        QCOMPARE(view.state(), 2u);
        QVERIFY(!view.error().isEmpty());
    }

#ifdef QUICKLOOK_TEST_PDF
    void pdfMultiplePagesAndRecovery()
    {
        QTemporaryDir dir;
        const QString pdf = dir.filePath("document.pdf");
        {
            QPdfWriter writer(pdf);
            QPainter painter(&writer);
            painter.drawText(100, 200, "First page");
            QVERIFY(writer.newPage());
            painter.drawText(100, 200, "Second page");
        }
        DocumentView view(Qt::white, nullptr);
        view.resize(600, 500);
        view.show();
        view.open(pdf, 2);
        QTRY_COMPARE(view.state(), 1u);
        QCOMPARE(view.pageCount(), 2);
        QVERIFY(view.findChild<QWidget *>("quicklook-pdf-pages"));
        const QString broken = dir.filePath("broken.pdf");
        QFile file(broken);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write("%PDF-broken");
        file.close();
        view.open(broken, 2);
        QTRY_COMPARE(view.state(), 2u);
        QVERIFY(!view.error().isEmpty());
        view.open(pdf, 2);
        QTRY_COMPARE(view.state(), 1u);
        QCOMPARE(view.pageCount(), 2);
        view.reset();
        QVERIFY(!view.findChild<QWidget *>("quicklook-pdf-pages"));
    }
#else
    void pdfUnavailableKeepsTextUsable()
    {
        QTemporaryDir dir;
        DocumentView view(Qt::white, nullptr);
        view.open(dir.filePath("document.pdf"), 2);
        QCOMPARE(view.state(), 2u);
        QVERIFY(view.error().contains("PDF"));
        const QString path = dir.filePath("notes.txt");
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write("Text still works");
        file.close();
        view.open(path, 1);
        QCOMPARE(view.state(), 1u);
        QCOMPARE(view.findChild<QPlainTextEdit *>()->toPlainText(), QString("Text still works"));
    }
#endif
};

QTEST_MAIN(Documents)
#include "check_documents.moc"
