// Handles topology preset workspace interactions outside MainWindow.
#pragma once

#include "app/plugininteractionregistry.h"

class ActiveWorkspaceController;
class DesignEditingService;
class QWidget;

class TopologyPresetInteractionHandler {
public:
    TopologyPresetInteractionHandler(QWidget* hostWidget,
                                     DesignEditingService* designEditingService,
                                     ActiveWorkspaceController* workspaceController);

    bool registerHandlers(PluginInteractionRegistry& interactions) const;

private:
    PluginInteractionResult handleInteraction(const PluginInteractionDescriptor& interaction,
                                              const PluginInteractionContext& context) const;

    QWidget* m_hostWidget = nullptr;
    DesignEditingService* m_designEditingService = nullptr;
    ActiveWorkspaceController* m_workspaceController = nullptr;
};
