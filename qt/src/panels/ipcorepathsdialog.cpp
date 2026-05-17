#include "panels/ipcorepathsdialog.h"

#include <QAbstractItemView>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QSet>
#include <QVBoxLayout>

namespace {

QString normalizedPath(const QString& path) {
    const QString trimmed = path.trimmed();
    return trimmed.isEmpty() ? QString() : QFileInfo(trimmed).absoluteFilePath();
}

QStringList uniquePaths(const QStringList& paths) {
    QStringList result;
    QSet<QString> seen;
    for (const QString& path : paths) {
        const QString normalized = normalizedPath(path);
        if (normalized.isEmpty() || seen.contains(normalized)) {
            continue;
        }
        seen.insert(normalized);
        result.append(normalized);
    }
    return result;
}

} // namespace

IpcorePathsDialog::IpcorePathsDialog(QWidget* parent)
    : QDialog(parent) {
    setObjectName(QStringLiteral("ipcorePathsDialog"));
    setWindowTitle(QStringLiteral("IP Core Packages"));
    resize(640, 420);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(12, 12, 12, 12);
    layout->setSpacing(8);

    auto* pathsLabel = new QLabel(QStringLiteral("Package roots"), this);
    layout->addWidget(pathsLabel);

    m_paths = new QListWidget(this);
    m_paths->setObjectName(QStringLiteral("ipcorePathsList"));
    m_paths->setSelectionMode(QAbstractItemView::ExtendedSelection);
    layout->addWidget(m_paths, 1);

    auto* pathButtons = new QHBoxLayout();
    auto* addButton = new QPushButton(QStringLiteral("Add..."), this);
    addButton->setObjectName(QStringLiteral("ipcorePathsAddButton"));
    m_removeButton = new QPushButton(QStringLiteral("Remove"), this);
    m_removeButton->setObjectName(QStringLiteral("ipcorePathsRemoveButton"));
    m_removeButton->setEnabled(false);
    pathButtons->addWidget(addButton);
    pathButtons->addWidget(m_removeButton);
    pathButtons->addStretch(1);
    layout->addLayout(pathButtons);

    auto* diagnosticsLabel = new QLabel(QStringLiteral("Diagnostics"), this);
    layout->addWidget(diagnosticsLabel);

    m_diagnostics = new QListWidget(this);
    m_diagnostics->setObjectName(QStringLiteral("ipcorePathsDiagnostics"));
    m_diagnostics->setSelectionMode(QAbstractItemView::NoSelection);
    m_diagnostics->setMinimumHeight(90);
    layout->addWidget(m_diagnostics);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    layout->addWidget(buttons);

    connect(addButton, &QPushButton::clicked, this, &IpcorePathsDialog::addPath);
    connect(m_removeButton, &QPushButton::clicked, this, &IpcorePathsDialog::removeSelectedPath);
    connect(m_paths, &QListWidget::itemSelectionChanged, this, [this] {
        if (m_removeButton) {
            m_removeButton->setEnabled(m_paths && !m_paths->selectedItems().isEmpty());
        }
    });
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
}

void IpcorePathsDialog::setPaths(const QStringList& paths) {
    if (!m_paths) {
        return;
    }

    m_paths->clear();
    for (const QString& path : uniquePaths(paths)) {
        m_paths->addItem(path);
    }
    if (m_removeButton) {
        m_removeButton->setEnabled(false);
    }
}

QStringList IpcorePathsDialog::paths() const {
    QStringList result;
    if (!m_paths) {
        return result;
    }

    for (int row = 0; row < m_paths->count(); ++row) {
        const QListWidgetItem* item = m_paths->item(row);
        if (item) {
            result.append(item->text());
        }
    }
    return uniquePaths(result);
}

void IpcorePathsDialog::setDiagnostics(const QStringList& diagnostics) {
    if (!m_diagnostics) {
        return;
    }

    m_diagnostics->clear();
    for (const QString& diagnostic : diagnostics) {
        const QString trimmed = diagnostic.trimmed();
        if (!trimmed.isEmpty()) {
            m_diagnostics->addItem(trimmed);
        }
    }
}

void IpcorePathsDialog::addPath() {
    const QString startPath = paths().isEmpty() ? QDir::homePath() : paths().last();
    const QString selectedPath =
        QFileDialog::getExistingDirectory(this,
                                          QStringLiteral("Add IP Core Package Root"),
                                          startPath);
    if (selectedPath.trimmed().isEmpty()) {
        return;
    }

    QStringList nextPaths = paths();
    nextPaths.append(selectedPath);
    setPaths(nextPaths);
}

void IpcorePathsDialog::removeSelectedPath() {
    if (!m_paths) {
        return;
    }

    qDeleteAll(m_paths->selectedItems());
    if (m_removeButton) {
        m_removeButton->setEnabled(false);
    }
}
