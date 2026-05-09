// ValidationManager runs validators on demand and updates the log panel.
#ifndef VALIDATIONMANAGER_H
#define VALIDATIONMANAGER_H

#include <QObject>

class Graph;
class LogPanel;
class BasicValidator;
class DRCRunner;
class IpCatalogService;
class ProjectStateService;
class ActiveWorkspaceController;

class ValidationManager : public QObject {
    Q_OBJECT

public:
    // Owns validator instances and targets a specific Graph/LogPanel pair.
    ValidationManager(Graph* graph,
                      ProjectStateService* projectStateService,
                      const IpCatalogService* catalogService,
                      const ActiveWorkspaceController* activeWorkspaceController,
                      LogPanel* logPanel,
                      QObject* parent = nullptr);
    ~ValidationManager();

public slots:
    // Runs framework checks and IP-provided DRC, then publishes merged results to the log panel.
    void runValidation();

private:
    Graph* m_graph;
    ProjectStateService* m_projectStateService;
    const IpCatalogService* m_catalogService;
    const ActiveWorkspaceController* m_activeWorkspaceController;
    LogPanel* m_logPanel;
    BasicValidator* m_validator;
    DRCRunner* m_drcRunner;
};

#endif
