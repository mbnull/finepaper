#pragma once

#include "application/design_creation.h"
#include "package/package.h"

#include <QDialog>
#include <QHash>
#include <QStringList>
#include <QVector>

class QComboBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QSpinBox;

namespace finepaper {

// A small, immutable projection of Package metadata used by design creation.
// The dialog deliberately does not retain PackageDefinition and its compiled
// schemas or runtime metadata.
struct DesignCreationPackageOption final {
    PackageReference reference;
    QString name;
    TopologySpec defaultTopology;
    int minimumRows = 1;
    int maximumRows = 1;
    int minimumColumns = 1;
    int maximumColumns = 1;
    QStringList endpointTypes;
    QStringList domainTypes;
    qsizetype endpointTypeCount = 0;
    qsizetype domainTypeCount = 0;
    qsizetype elementPropertySetCount = 0;
    qsizetype designExtensionCount = 0;

    [[nodiscard]] QString key() const { return reference.key(); }
};

[[nodiscard]] DesignCreationPackageOption designCreationPackageOption(
    const PackageDefinition& package);
[[nodiscard]] QVector<DesignCreationPackageOption> designCreationPackageOptions(
    const QVector<PackageDefinition>& packages);

class NewDesignDialog final : public QDialog {
    Q_OBJECT
    Q_DISABLE_COPY_MOVE(NewDesignDialog)

public:
    NewDesignDialog(QVector<DesignCreationPackageOption> packages,
                    const QString& preferredPackageKey,
                    const QString& suggestedName,
                    QWidget* parent = nullptr);

    [[nodiscard]] DesignCreationRequest draft() const;
    [[nodiscard]] QString selectedPackageKey() const;
    void accept() override;

private:
    struct TopologyDraft final {
        int rows = 1;
        int columns = 1;
    };

    [[nodiscard]] const DesignCreationPackageOption* selectedPackage() const;
    void saveCurrentTopologyDraft();
    void updatePackageSelection();
    void updateAcceptState();

    QVector<DesignCreationPackageOption> m_packages;
    QHash<QString, TopologyDraft> m_topologyDrafts;
    QString m_currentPackageKey;
    bool m_updatingPackage = false;

    QComboBox* m_packageSelector = nullptr;
    QLineEdit* m_designName = nullptr;
    QLabel* m_topologyType = nullptr;
    QSpinBox* m_rows = nullptr;
    QSpinBox* m_columns = nullptr;
    QLabel* m_packageDetails = nullptr;
    QLabel* m_validation = nullptr;
    QPushButton* m_createButton = nullptr;
};

} // namespace finepaper
