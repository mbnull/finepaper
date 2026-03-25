// ArrangeCommand arranges the graph into a mesh-style  layout
// and stores enough state to undo the arrangement in one step.
#pragma once

#include "command.h"
#include "parameter.h"
#include <QHash>
#include <QString>

class Graph;
class Module;

class ArrangeCommand : public Command {
public:
    explicit ArrangeCommand(Graph* graph);

    void execute() override;
    void undo() override;

private:
    struct ParameterSnapshot {
        bool existed = false;
        Parameter::Value value{};
    };

    struct ModuleSnapshot {
        ParameterSnapshot x;
        ParameterSnapshot y;
        ParameterSnapshot collapsed;
    };

    struct ModulePlacement {
        int x = 0;
        int y = 0;
        bool setCollapsed = false;
        bool collapsed = false;
    };

    void initializeSnapshots();
    void captureCurrentState(QHash<QString, ModuleSnapshot>& snapshots) const;
    void applyState(const QHash<QString, ModuleSnapshot>& snapshots) const;
    QHash<QString, ModulePlacement> buildPlacements() const;

    Graph* m_graph;
    bool m_initialized = false;
    QHash<QString, ModuleSnapshot> m_before;
    QHash<QString, ModuleSnapshot> m_after;
};
