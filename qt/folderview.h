#pragma once

#include <QDir>
#include <QFileSystemModel>
#include <QHeaderView>
#include <QLabel>
#include <QTreeView>
#include <QVBoxLayout>

// QFileSystemModel enumera en su hilo de trabajo. La vista nunca expande ni
// solicita el contenido de las subcarpetas, ni calcula tamaños recursivos.
class FolderView final : public QWidget
{
public:
    explicit FolderView(const QString &path, QWidget *parent) : QWidget(parent)
    {
        setProperty("quicklookState", 0u);
        auto *layout = new QVBoxLayout(this);
        layout->setContentsMargins(0, 0, 0, 0);
        m_label = new QLabel("Cargando contenido de la carpeta…", this);
        m_label->setTextFormat(Qt::PlainText);
        layout->addWidget(m_label);
        m_model = new QFileSystemModel(this);
        m_model->setOptions(QFileSystemModel::DontWatchForChanges
            | QFileSystemModel::DontResolveSymlinks | QFileSystemModel::DontUseCustomDirectoryIcons);
        m_model->setReadOnly(true);
        m_model->setFilter(QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden | QDir::System);
        m_tree = new QTreeView(this);
        m_tree->setObjectName("quicklook-folder");
        m_tree->setModel(m_model);
        m_tree->setFrameShape(QFrame::NoFrame);
        m_tree->setRootIsDecorated(false);
        m_tree->setItemsExpandable(false);
        m_tree->setExpandsOnDoubleClick(false);
        m_tree->setEditTriggers(QAbstractItemView::NoEditTriggers);
        m_tree->setDragEnabled(false);
        m_tree->setAcceptDrops(false);
        m_tree->setUniformRowHeights(true);
        m_tree->setSortingEnabled(true);
        m_tree->sortByColumn(0, Qt::AscendingOrder);
        m_tree->header()->setSectionResizeMode(0, QHeaderView::Stretch);
        for (int column = 1; column < 4; ++column) {
            m_tree->header()->setSectionResizeMode(column, QHeaderView::ResizeToContents);
        }
        layout->addWidget(m_tree);
        connect(m_model, &QFileSystemModel::directoryLoaded, this, [this, path](const QString &loaded) {
            if (loaded != path) { return; }
            const int count = m_model->rowCount(m_tree->rootIndex());
            m_label->setText(QString("%1 elementos · Contenido directo, incluidos ocultos").arg(count));
            setProperty("quicklookEntries", count);
            setProperty("quicklookState", 1u);
        });
        m_tree->setRootIndex(m_model->setRootPath(path));
    }

private:
    QFileSystemModel *m_model;
    QTreeView *m_tree;
    QLabel *m_label;
};
