#pragma once

#include "app/pluginhost.h"

#include <memory>

std::unique_ptr<IAppPlugin> createToolPipelinePlugin();
