#include "../qt/documentview.h"
#include <QTemporaryDir>
#include <QPainter>
#include <QPdfWriter>
#include <QTest>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QElapsedTimer>

// Conversor controlado: permite probar cancelación, fallos y caché sin imponer
// LibreOffice a quien ejecuta las pruebas de imágenes/texto/PDF/carpetas.
class ConverterFixture
{
public:
    QTemporaryDir directory;
    QByteArray originalPath = qgetenv("PATH");

    ConverterFixture()
    {
        const QString root = directory.path();
        QDir().mkpath(root + "/bin");
        QFile script(root + "/bin/libreoffice");
        if (!script.open(QIODevice::WriteOnly)) { return; }
        script.write(R"(#!/usr/bin/python3
from pathlib import Path
import json, shutil, subprocess, sys, time
root = Path(__file__).resolve().parent.parent
input = Path(sys.argv[-1])
out = Path(sys.argv[sys.argv.index('--outdir') + 1])
profile = next(a.split('=', 1)[1] for a in sys.argv if a.startswith('-env:UserInstallation='))
with (root / 'calls').open('a') as calls:
    calls.write(json.dumps({'input': input.name, 'profile': profile}) + '\n')
(root / 'settings').write_bytes((out / 'profile/user/registrymodifications.xcu').read_bytes())
content = input.read_bytes()
if content.startswith(b'slow'):
    child = subprocess.Popen([sys.executable, '-c', 'import time; time.sleep(30)'])
    (root / 'child').write_text(str(child.pid))
    time.sleep(30)
elif content.startswith(b'bad'):
    sys.exit(0)
else:
    shutil.copyfile(root / 'template.pdf', out / 'input.pdf')
)");
        script.close();
        script.setPermissions(QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner);
        {
            QPdfWriter writer(root + "/template.pdf");
            QPainter painter(&writer);
            painter.drawText(100, 200, "Office preview");
        }
        qputenv("PATH", QFile::encodeName(root + "/bin") + ':' + originalPath);
    }

    ~ConverterFixture() { qputenv("PATH", originalPath); }

    QString input(const QByteArray &bytes = "normal")
    {
        const QString path = directory.filePath("original ;$(literal).docx");
        QFile file(path);
        if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) { return {}; }
        file.write(bytes);
        return path;
    }

    int calls() const
    {
        QFile file(directory.filePath("calls"));
        return file.open(QIODevice::ReadOnly) ? file.readAll().count('\n') : 0;
    }
};

class Documents final : public QObject
{
    Q_OBJECT
private Q_SLOTS:
    void folderShowsOnlyDirectContents()
    {
        QTemporaryDir dir;
        QVERIFY(QDir().mkpath(dir.filePath("subfolder/deeper")));
        for (const QString &name : {QString("file.txt"), QString(".hidden"), QString("subfolder/inside.txt")}) {
            QFile file(dir.filePath(name));
            QVERIFY(file.open(QIODevice::WriteOnly));
            file.write("content");
        }
        QVERIFY(QFile::link(dir.path(), dir.filePath("loop")));
        DocumentView view(Qt::white, nullptr);
        view.resize(700, 500);
        view.show();
        view.open(dir.path(), 4);
        auto *tree = view.findChild<QTreeView *>("quicklook-folder");
        QVERIFY(tree);
        auto *model = qobject_cast<QFileSystemModel *>(tree->model());
        QVERIFY(model && model->isReadOnly());
        QSignalSpy loaded(model, &QFileSystemModel::directoryLoaded);
        QTRY_COMPARE(view.state(), 1u);
        QCOMPARE(view.entryCount(), 4);
        QCOMPARE(model->rowCount(tree->rootIndex()), 4);
        QVERIFY(!tree->itemsExpandable());
        QCOMPARE(tree->editTriggers(), QAbstractItemView::NoEditTriggers);
        const QModelIndex child = model->index(dir.filePath("subfolder"));
        QCOMPARE(model->rowCount(child), 0);
        QTest::mouseDClick(tree->viewport(), Qt::LeftButton, Qt::NoModifier, tree->visualRect(child).center());
        QTest::qWait(100);
        QVERIFY(!tree->isExpanded(child));
        QCOMPARE(model->rowCount(child), 0);
        for (const auto &signal : loaded) { QCOMPARE(signal.front().toString(), dir.path()); }
        view.reset();
        QVERIFY(!view.findChild<QTreeView *>());
    }

    void emptyFolderAndFolderErrors()
    {
        QTemporaryDir dir;
        DocumentView view(Qt::white, nullptr);
        view.open(dir.path(), 4);
        QTRY_COMPARE(view.state(), 1u);
        QCOMPARE(view.entryCount(), 0);
        view.open(dir.filePath("missing"), 4);
        QCOMPARE(view.state(), 2u);
        QVERIFY(!view.error().isEmpty());
    }

    void officeConversionCacheAndChanges()
    {
        ConverterFixture fixture;
        OfficeConverter converter;
        QSignalSpy ready(&converter, &OfficeConverter::converted);
        QSignalSpy failed(&converter, &OfficeConverter::failed);
        const QString path = fixture.input();
        converter.open(QFile::encodeName(path));
        QTRY_COMPARE(ready.count(), 1);
        QCOMPARE(failed.count(), 0);
        QCOMPARE(fixture.calls(), 1);
        const QString pdf = ready.front().front().toString();
        QVERIFY(QFileInfo::exists(pdf));
        QFile settings(fixture.directory.filePath("settings"));
        QVERIFY(settings.open(QIODevice::ReadOnly));
        const QByteArray registry = settings.readAll();
        QVERIFY(registry.contains("DisableMacrosExecution"));
        QVERIFY(registry.contains("<value>true</value>"));
        QVERIFY(registry.contains("<value>3</value>"));
        converter.open(QFile::encodeName(path));
        QCOMPARE(ready.count(), 2);
        QCOMPARE(fixture.calls(), 1);
        QCOMPARE(ready.back().front().toString(), pdf);
        QCOMPARE(fixture.input("changed document"), path);
        converter.open(QFile::encodeName(path));
        QTRY_COMPARE(ready.count(), 3);
        QCOMPARE(fixture.calls(), 2);
        QVERIFY(ready.back().front().toString() != pdf);
        QFile source(path);
        QVERIFY(source.open(QIODevice::ReadOnly));
        QCOMPARE(source.readAll(), QByteArray("changed document"));
    }

    void officeCancellationKillsDescendants()
    {
        ConverterFixture fixture;
        OfficeConverter converter;
        QSignalSpy ready(&converter, &OfficeConverter::converted);
        QSignalSpy failed(&converter, &OfficeConverter::failed);
        converter.open(QFile::encodeName(fixture.input("slow")));
        const QString childFile = fixture.directory.filePath("child");
        QTRY_VERIFY(QFileInfo::exists(childFile));
        QFile child(childFile);
        QVERIFY(child.open(QIODevice::ReadOnly));
        const qint64 pid = child.readAll().toLongLong();
        QVERIFY(pid > 0);
        converter.cancel();
        auto exited = [pid] {
            QFile status(QString("/proc/%1/stat").arg(pid));
            if (!status.open(QIODevice::ReadOnly)) { return true; }
            const auto data = status.readAll();
            return data.mid(data.lastIndexOf(')') + 2, 1) == "Z";
        };
        QTRY_VERIFY(exited());
        QTest::qWait(100);
        QCOMPARE(ready.count(), 0);
        QCOMPARE(failed.count(), 0);
    }

    void officeRejectsOversizedAndNonRegularFiles()
    {
        ConverterFixture fixture;
        OfficeConverter converter;
        QSignalSpy failed(&converter, &OfficeConverter::failed);
        const QString path = fixture.directory.filePath("large.docx");
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        QVERIFY(file.resize(OfficeConverter::FileLimit + 1));
        file.close();
        converter.open(QFile::encodeName(path));
        QCOMPARE(failed.count(), 1);
        QVERIFY(failed.back().front().toString().contains("64 MiB"));
        const QByteArray fifo = QFile::encodeName(fixture.directory.filePath("pipe.docx"));
        QCOMPARE(::mkfifo(fifo.constData(), 0600), 0);
        converter.open(fifo);
        QCOMPARE(failed.count(), 2);
        QCOMPARE(fixture.calls(), 0);
    }

    void officeFailuresAndTimeoutKeepOtherViewsUsable()
    {
        ConverterFixture fixture;
        OfficeConverter converter(nullptr, 500);
        QSignalSpy failed(&converter, &OfficeConverter::failed);
        converter.open(QFile::encodeName(fixture.input("bad")));
        QTRY_COMPARE(failed.count(), 1);
        converter.open(QFile::encodeName(fixture.input("slow")));
        QTRY_COMPARE(failed.count(), 2);
        QVERIFY(failed.back().front().toString().contains("tiempo límite"));
        qputenv("PATH", QFile::encodeName(fixture.directory.path()));
        converter.open(QFile::encodeName(fixture.input()));
        QCOMPARE(failed.count(), 3);
        QVERIFY(failed.back().front().toString().contains("LibreOffice"));
        DocumentView view(Qt::white, nullptr);
        view.open(fixture.directory.path(), 4);
        QTRY_COMPARE(view.state(), 1u);
    }

#ifdef QUICKLOOK_TEST_PDF
    void officeUsesPdfViewAndRecoversOnNavigation()
    {
        ConverterFixture fixture;
        DocumentView view(Qt::white, nullptr);
        view.open(fixture.input(), 3);
        QCOMPARE(view.state(), 0u);
        QTRY_COMPARE(view.state(), 1u);
        QCOMPARE(view.pageCount(), 1);
        QVERIFY(view.findChild<QWidget *>("quicklook-pdf-pages"));
        view.open(fixture.directory.path(), 4);
        QTRY_COMPARE(view.state(), 1u);
        QVERIFY(!view.findChild<QWidget *>("quicklook-pdf-pages"));
        QCOMPARE(fixture.input("slow"), fixture.directory.filePath("original ;$(literal).docx"));
        view.open(fixture.directory.filePath("original ;$(literal).docx"), 3);
        view.open(fixture.directory.path(), 4);
        QTRY_COMPARE(view.state(), 1u);
        QVERIFY(!view.findChild<QWidget *>("quicklook-pdf-pages"));
    }

    void officeRealFormats()
    {
        const QString fixtures = qEnvironmentVariable("QUICKLOOK_OFFICE_FIXTURES");
        if (fixtures.isEmpty()) { QSKIP("Prueba real opcional: configurar QUICKLOOK_OFFICE_FIXTURES con los seis formatos."); }
        QVERIFY(!QStandardPaths::findExecutable("libreoffice").isEmpty());
        DocumentView view(Qt::white, nullptr);
        view.resize(700, 500);
        view.show();
        for (const QString &name : {QString("text.odt"), QString("text.doc"), QString("text.docx"),
                 QString("sheet.ods"), QString("sheet.xls"), QString("sheet.xlsx")}) {
            const QString path = QDir(fixtures).filePath(name);
            QVERIFY2(QFileInfo::exists(path), qPrintable(path));
            QElapsedTimer elapsed;
            elapsed.start();
            view.open(path, 3);
            QTRY_COMPARE_WITH_TIMEOUT(view.state(), 1u, 65000);
            QCOMPARE(view.pageCount(), 1);
            QVERIFY(view.findChild<QWidget *>("quicklook-pdf-pages"));
            qInfo() << "Office real:" << name << elapsed.elapsed() << "ms";
        }
    }
#endif

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
