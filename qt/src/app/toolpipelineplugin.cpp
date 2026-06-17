#include "app/toolpipelineplugin.h"

#include "app/appcontext.h"
#include "app/serviceregistry.h"
#include "app/toolpipelineservice.h"

#include <stdexcept>

namespace {

bool hasToolPipelineService(AppContext& context) {
    return context.services &&
           context.services->service<ToolPipelineService>(
               ServiceKey::fromLiteral("finepaper.tool-pipeline"));
}

class ToolPipelinePlugin final : public IAppPlugin {
public:
    QString id() const override {
        return QStringLiteral("finepaper.tool-pipeline");
    }

    void activate(AppContext& context) override {
        if (!hasToolPipelineService(context)) {
            throw std::runtime_error(
                "ToolPipelineService is required before activating ToolPipelinePlugin.");
        }
    }
};

} // namespace

std::unique_ptr<IAppPlugin> createToolPipelinePlugin() {
    return std::make_unique<ToolPipelinePlugin>();
}
