// ValidationManager runs validators on demand and updates the log panel.
#ifndef VALIDATIONMANAGER_H
#define VALIDATIONMANAGER_H

#include <QObject>

class Graph;
class LogPanel;
class BasicValidator;
class DRCRunner;
class ProjectStateService;

class ValidationManager : public QObject {
    Q_OBJECT

public:
    // Owns validator instances and targets a specific Graph/LogPanel pair.
    ValidationManager(Graph* graph,
                      ProjectStateService* projectStateService,
                      LogPanel* logPanel,
                      QObject* parent = nullptr);
    ~ValidationManager();

public slots:
    // Runs framework checks and IP-provided DRC, then publishes merged results to the log panel.
    void runValidation();

private:
    Graph* m_graph;
    ProjectStateService* m_projectStateService;
    LogPanel* m_logPanel;
    BasicValidator* m_validator;
    DRCRunner* m_drcRunner;
};

#endif
