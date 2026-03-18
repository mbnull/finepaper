#include "validationmanager.h"
#include "graph.h"
#include "logpanel.h"
#include "validator.h"
#include "drcrunner.h"

ValidationManager::ValidationManager(Graph* graph, LogPanel* logPanel, QObject* parent)
    : QObject(parent), m_graph(graph), m_logPanel(logPanel),
      m_validator(new BasicValidator()), m_drcRunner(new DRCRunner()) {

    connect(m_graph, &Graph::moduleAdded, this, &ValidationManager::runValidation);
    connect(m_graph, &Graph::moduleRemoved, this, &ValidationManager::runValidation);
    connect(m_graph, &Graph::connectionAdded, this, &ValidationManager::runValidation);
    connect(m_graph, &Graph::connectionRemoved, this, &ValidationManager::runValidation);
}

ValidationManager::~ValidationManager() {
    delete m_validator;
    delete m_drcRunner;
}

void ValidationManager::runValidation() {
    QList<ValidationResult> results = m_validator->validate(m_graph);
    results.append(m_drcRunner->validate(m_graph));
    m_logPanel->setResults(results);
}
