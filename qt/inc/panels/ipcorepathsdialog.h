#pragma once

#include <QDialog>
#include <QStringList>

class QListWidget;
class QPushButton;

class IpcorePathsDialog : public QDialog {
    Q_OBJECT

public:
    explicit IpcorePathsDialog(QWidget* parent = nullptr);

    void setPaths(const QStringList& paths);
    QStringList paths() const;
    void setDiagnostics(const QStringList& diagnostics);

private slots:
    void addPath();
    void removeSelectedPath();

private:
    QListWidget* m_paths = nullptr;
    QListWidget* m_diagnostics = nullptr;
    QPushButton* m_removeButton = nullptr;
};
