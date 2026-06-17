// Handles topology preset workspace interactions outside MainWindow.
#pragma once

#include "app/plugininteractionregistry.h"

class ActiveWorkspaceController;
class CommandManager;
class QWidget;

class TopologyPresetInteractionHandler {
public:
    TopologyPresetInteractionHandler(QWidget* hostWidget,
                                     CommandManager* commandManager,
                                     ActiveWorkspaceController* workspaceController);

    bool registerHandlers(PluginInteractionRegistry& interactions) const;

private:
    PluginInteractionResult handleInteraction(const PluginInteractionDescriptor& interaction,
                                              const PluginInteractionContext& context) const;

    QWidget* m_hostWidget = nullptr;
    CommandManager* m_commandManager = nullptr;
    ActiveWorkspaceController* m_workspaceController = nullptr;
};
