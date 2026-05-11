// Project launcher dialog implementation.
#include "app/projectlauncher.h"

#include "app/appsettings.h"

#include <QDialog>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QListWidgetItem>
#include <QPushButton>
#include <QVBoxLayout>

namespace {

QString projectFileDialogSaveFilter() {
    return QStringLiteral("Finepaper Project (*.fpproj)");
}

QString projectFileDialogOpenFilter() {
    return QStringLiteral("Finepaper Project (*.fpproj);;All Files (*)");
}

QString pathWithProjectExtension(QString path) {
    return QFileInfo(path).suffix().isEmpty() ? path + QStringLiteral(".fpproj") : path;
}

} // namespace

ProjectLauncherDialog::ProjectLauncherDialog(QWidget* parent)
    : m_dialog(std::make_unique<QDialog>(parent)),
      m_appSettings(std::make_unique<AppSettings>()) {
    m_dialog->setWindowTitle(QStringLiteral("Open or Create Project"));
    m_dialog->setModal(true);
    m_dialog->resize(720, 420);

    auto* layout = new QVBoxLayout(m_dialog.get());
    layout->setContentsMargins(12, 12, 12, 12);
    layout->setSpacing(8);

    auto* actionRow = new QHBoxLayout;
    actionRow->setSpacing(8);
    auto* newButton = new QPushButton(QStringLiteral("New Project"), m_dialog.get());
    auto* openButton = new QPushButton(QStringLiteral("Open Project..."), m_dialog.get());
    auto* cancelButton = new QPushButton(QStringLiteral("Cancel"), m_dialog.get());
    actionRow->addWidget(newButton);
    actionRow->addWidget(openButton);
    actionRow->addStretch(1);
    actionRow->addWidget(cancelButton);
    layout->addLayout(actionRow);

    auto* recentLabel = new QLabel(QStringLiteral("Recent Projects"), m_dialog.get());
    layout->addWidget(recentLabel);

    m_recentProjects = new QListWidget(m_dialog.get());
    m_recentProjects->setObjectName(QStringLiteral("projectLauncherRecentProjects"));
    for (const QString& path : m_appSettings->recentProjects()) {
        auto* item = new QListWidgetItem(path, m_recentProjects);
        item->setToolTip(path);
        item->setData(Qt::UserRole, path);
    }
    layout->addWidget(m_recentProjects, 1);

    auto* recentRow = new QHBoxLayout;
    recentRow->setSpacing(8);
    auto* openSelectedButton = new QPushButton(QStringLiteral("Open Selected"), m_dialog.get());
    openSelectedButton->setEnabled(false);
    recentRow->addStretch(1);
    recentRow->addWidget(openSelectedButton);
    layout->addLayout(recentRow);

    QObject::connect(newButton, &QPushButton::clicked, m_dialog.get(), [this]() { chooseNewProject(); });
    QObject::connect(openButton, &QPushButton::clicked, m_dialog.get(), [this]() { chooseOpenProject(); });
    QObject::connect(cancelButton, &QPushButton::clicked, m_dialog.get(), [this]() { m_dialog->reject(); });
    QObject::connect(openSelectedButton,
                     &QPushButton::clicked,
                     m_dialog.get(),
                     [this]() { openSelectedRecentProject(); });
    QObject::connect(m_recentProjects,
                     &QListWidget::itemDoubleClicked,
                     m_dialog.get(),
                     [this](QListWidgetItem* item) { openRecentProject(item); });
    QObject::connect(m_recentProjects,
                     &QListWidget::itemActivated,
                     m_dialog.get(),
                     [this](QListWidgetItem* item) { openRecentProject(item); });
    QObject::connect(m_recentProjects,
                     &QListWidget::itemSelectionChanged,
                     m_dialog.get(),
                     [this, openSelectedButton]() {
                         openSelectedButton->setEnabled(
                             m_recentProjects && m_recentProjects->currentItem() != nullptr);
                     });
}

ProjectLauncherDialog::~ProjectLauncherDialog() = default;

int ProjectLauncherDialog::exec() {
    m_result = {};
    return m_dialog ? m_dialog->exec() : QDialog::Rejected;
}

ProjectLauncherResult ProjectLauncherDialog::result() const {
    return m_result;
}

void ProjectLauncherDialog::chooseNewProject() {
    const QString path = QFileDialog::getSaveFileName(m_dialog.get(),
                                                      QStringLiteral("Create Project"),
                                                      defaultDirectoryPath(),
                                                      projectFileDialogSaveFilter());
    if (path.isEmpty()) {
        return;
    }

    const QString projectPath = pathWithProjectExtension(path);
    if (m_appSettings) {
        m_appSettings->setLastDirectory(QFileInfo(projectPath).absolutePath());
    }
    m_result.action = ProjectLauncherResult::Action::NewProject;
    m_result.path = projectPath;
    m_dialog->accept();
}

void ProjectLauncherDialog::chooseOpenProject() {
    const QString path = QFileDialog::getOpenFileName(m_dialog.get(),
                                                      QStringLiteral("Open Project"),
                                                      defaultDirectoryPath(),
                                                      projectFileDialogOpenFilter());
    if (path.isEmpty()) {
        return;
    }

    if (m_appSettings) {
        m_appSettings->setLastDirectory(QFileInfo(path).absolutePath());
    }
    m_result.action = ProjectLauncherResult::Action::OpenProject;
    m_result.path = QFileInfo(path).absoluteFilePath();
    m_dialog->accept();
}

void ProjectLauncherDialog::openRecentProject(QListWidgetItem* item) {
    if (!item) {
        return;
    }

    const QString path = item->data(Qt::UserRole).toString();
    if (path.trimmed().isEmpty()) {
        return;
    }

    if (m_appSettings) {
        m_appSettings->setLastDirectory(QFileInfo(path).absolutePath());
    }
    m_result.action = ProjectLauncherResult::Action::OpenProject;
    m_result.path = QFileInfo(path).absoluteFilePath();
    m_dialog->accept();
}

void ProjectLauncherDialog::openSelectedRecentProject() {
    openRecentProject(m_recentProjects ? m_recentProjects->currentItem() : nullptr);
}

QString ProjectLauncherDialog::defaultDirectoryPath() const {
    const QString lastDirectory = m_appSettings ? m_appSettings->lastDirectory() : QString();
    return lastDirectory.isEmpty() ? QDir::currentPath() : lastDirectory;
}
