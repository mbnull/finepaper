#include "app/staticplugincatalog.h"

#include "app/pluginhost.h"
#include "app/toolpipelineplugin.h"
#include "package/packageplugin.h"
#include "project/projectplugin.h"

void registerStaticPlugins(PluginHost& host) {
    host.registerPlugin(createProjectPlugin());
    host.registerPlugin(createPackagePlugin());
    host.registerPlugin(createToolPipelinePlugin());
}
