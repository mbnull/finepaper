#pragma once

#include <memory>

class IAppPlugin;

std::unique_ptr<IAppPlugin> createNoCPlugin();
