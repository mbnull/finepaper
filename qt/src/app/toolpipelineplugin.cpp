#include "app/toolpipelineplugin.h"

#include "app/appcontext.h"

#include <stdexcept>

namespace {

class ToolPipelinePlugin final : public IAppPlugin {
public:
    QString id() const override {
        return QStringLiteral("finepaper.tool-pipeline");
    }

    void activate(AppContext& context) override {
        if (!context.toolPipelineService) {
            throw std::runtime_error(
                "ToolPipelineService is required before activating ToolPipelinePlugin.");
        }
    }
};

} // namespace

std::unique_ptr<IAppPlugin> createToolPipelinePlugin() {
    return std::make_unique<ToolPipelinePlugin>();
}
