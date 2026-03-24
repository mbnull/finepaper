// ValidationManager runs validators on graph changes and updates log panel
#ifndef VALIDATIONMANAGER_H
#define VALIDATIONMANAGER_H

#include <QObject>

class Graph;
class LogPanel;
class BasicValidator;
class DRCRunner;

class ValidationManager : public QObject {
    Q_OBJECT

public:
    ValidationManager(Graph* graph, LogPanel* logPanel, QObject* parent = nullptr);
    ~ValidationManager();

private slots:
    void runValidation();

private:
    Graph* m_graph;
    LogPanel* m_logPanel;
    BasicValidator* m_validator;
    DRCRunner* m_drcRunner;
};

#endif
