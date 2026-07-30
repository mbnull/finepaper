#pragma once

#include "gui/domain_property_form.h"
#include "noc/model.h"
#include "package/package.h"

#include <QVector>
#include <QDialog>

#include <functional>
#include <optional>

class QComboBox;
class QDialogButtonBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QTimer;

namespace finepaper {

using DomainCandidateValidator =
    std::function<QVector<Diagnostic>(const DomainDefinition&)>;

class DomainInstanceDialog final : public QDialog {
public:
    DomainInstanceDialog(QVector<DomainTypeDefinition> types,
                         QVector<DomainDefinition> draftDomains,
                         std::optional<DomainDefinition> existing,
                         QString preferredType,
                         DomainCandidateValidator validator,
                         QWidget* parent = nullptr);

    [[nodiscard]] DomainDefinition candidate() const;
    [[nodiscard]] QStringList localErrors() const;
    [[nodiscard]] const QVector<Diagnostic>& validationDiagnostics() const {
        return m_validationDiagnostics;
    }

protected:
    void accept() override;

private:
    [[nodiscard]] const DomainTypeDefinition* selectedType() const;
    [[nodiscard]] QString selectedTypeId() const;
    void rebuildPropertyForm(PropertyInitialization initialization);
    void scheduleValidation();
    void updateValidation(bool runAuthoritativeValidator = true);
    void updateTypeDescription();

    QVector<DomainTypeDefinition> m_types;
    QVector<DomainDefinition> m_draftDomains;
    std::optional<DomainDefinition> m_existing;
    DomainCandidateValidator m_validator;
    QVector<Diagnostic> m_validationDiagnostics;
    bool m_updating = false;

    QComboBox* m_typeSelector = nullptr;
    QLineEdit* m_typeDisplay = nullptr;
    QLineEdit* m_idEditor = nullptr;
    QLineEdit* m_nameEditor = nullptr;
    QLabel* m_typeDescription = nullptr;
    DomainPropertyForm* m_propertyForm = nullptr;
    QLabel* m_diagnostics = nullptr;
    QDialogButtonBox* m_buttons = nullptr;
    QPushButton* m_okButton = nullptr;
    QTimer* m_validationTimer = nullptr;
};

} // namespace finepaper
