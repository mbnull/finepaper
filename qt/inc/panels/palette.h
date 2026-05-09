// Legacy module palette kept buildable until the IP catalog migration removes it.
#pragma once

#include <QWidget>
#include <QListWidget>

class Palette : public QWidget {
    Q_OBJECT

public:
    explicit Palette(QWidget* parent = nullptr);
    void setActivePluginId(const QString& pluginId);
    QString activePluginId() const { return m_activePluginId; }

private:
    void setupUI();
    void populateModuleTypes();

    QListWidget* m_listWidget;
    QString m_activePluginId;
};
