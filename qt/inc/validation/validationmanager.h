// ValidationManager runs validators on demand and updates the log panel.
#ifndef VALIDATIONMANAGER_H
#define VALIDATIONMANAGER_H

#include <QObject>
#include <QString>

class Graph;
class LogPanel;
class IpCatalogService;
class ProjectStateService;
class ActiveWorkspaceController;
class ProjectExternalValidationRunner;
class ProjectValidationRunner;
namespace ipcraft::core {
struct ProjectDesign;
}

class ValidationManager : public QObject {
    Q_OBJECT

public:
    // Owns validator instances and targets a specific Graph/LogPanel pair.
    ValidationManager(Graph* graph,
                      ProjectStateService* projectStateService,
                      const IpCatalogService* catalogService,
                      const ActiveWorkspaceController* activeWorkspaceController,
                      LogPanel* logPanel,
                      const ipcraft::core::ProjectDesign* projectDesign = nullptr,
                      QObject* parent = nullptr);
    ~ValidationManager();

public slots:
    // Runs structural checks and IP-provided DRC, then publishes merged results to the log panel.
    void runValidation(const QString& projectPath = QString(), const QString& designName = QString());

private:
    Graph* m_graph;
    ProjectStateService* m_projectStateService;
    const IpCatalogService* m_catalogService;
    LogPanel* m_logPanel;
    const ipcraft::core::ProjectDesign* m_projectDesign;
    ProjectValidationRunner* m_projectValidationRunner;
    ProjectExternalValidationRunner* m_projectExternalValidationRunner;
};

#endif
