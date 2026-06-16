// Handles topology preset workspace interactions outside MainWindow.
#pragma once

#include "app/plugininteractionregistry.h"

class ActiveWorkspaceController;
class CommandManager;
class EditorMutationTarget;
class Graph;
class QWidget;

class TopologyPresetInteractionHandler {
public:
    TopologyPresetInteractionHandler(QWidget* hostWidget,
                                     Graph* graph,
                                     CommandManager* commandManager,
                                     ActiveWorkspaceController* workspaceController,
                                     EditorMutationTarget* editorMutationTarget);

    bool registerHandlers(PluginInteractionRegistry& interactions) const;

private:
    PluginInteractionResult handleInteraction(const PluginInteractionDescriptor& interaction,
                                              const PluginInteractionContext& context) const;

    QWidget* m_hostWidget = nullptr;
    Graph* m_graph = nullptr;
    CommandManager* m_commandManager = nullptr;
    ActiveWorkspaceController* m_workspaceController = nullptr;
    EditorMutationTarget* m_editorMutationTarget = nullptr;
};
