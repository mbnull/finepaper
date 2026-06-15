#include "noc/nocplugin.h"

#include <iostream>
#include <stdexcept>

namespace {

void require(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

void testNoCPluginHeaderOwnsReturnedPlugin() {
    std::unique_ptr<IAppPlugin> plugin = createNoCPlugin();
    require(static_cast<bool>(plugin), "NoC plugin factory should return a plugin");
    plugin.reset();
}

} // namespace

int main() {
    try {
        testNoCPluginHeaderOwnsReturnedPlugin();
    } catch (const std::exception& error) {
        std::cerr << "nocplugin_header_test failed: " << error.what() << '\n';
        return 1;
    }
    std::cout << "nocplugin_header_test passed\n";
    return 0;
}
