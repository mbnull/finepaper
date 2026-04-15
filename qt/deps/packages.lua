package("nodeeditor")
set_homepage("https://github.com/paceholder/nodeeditor")
set_description("Qt Node Editor. Dataflow programming framework")
set_license("BSD-3")

set_urls("https://github.com/paceholder/nodeeditor/archive/refs/tags/$(version).tar.gz",
    "https://github.com/paceholder/nodeeditor.git")

add_versions("3.0.16", "453e6eb783379fee6edf9282283576eaa7d27349b8731e638926ccbd8331f7ef")

add_deps("cmake")

on_load(function(package)
    -- package:add("deps", "qt6gui", "qt6widgets", {
    --     debug = package:is_debug()
    -- })

    package:add("framworks", "Qt6Core", "Qt6Gui", "Qt6Widgets")
    if package:config("shared") then
        package:add("defines", "NODE_EDITOR_SHARED")
    else
        package:add("defines", "NODE_EDITOR_STATIC")
    end
end)

on_install("linux", function(package)
    -- local qt = package:dep("qt6widgets"):fetch().qtdir

    local configs = {"-DBUILD_EXAMPLES=OFF", "-DBUILD_TESTING=OFF"}
    table.insert(configs, "-DBUILD_SHARED_LIBS=" .. (package:config("shared") and "ON" or "OFF"))
    table.insert(configs, "-DCMAKE_BUILD_TYPE=" .. (package:debug() and "Debug" or "Release"))
    -- if qt then
        table.insert(configs, "-DUSE_QT6=on")
    -- end

    import("package.tools.cmake").install(package, configs)
end)

-- on_test(function(package)
--     local cxflags
--     if not package:is_plat("windows") then
--         cxflags = "-fPIC"
--     end
--     assert(package:check_cxxsnippets({
--         test = [[
--             void test() {

--             }
--         ]]
--     }, {
--         configs = {
--             languages = "c++20",
--             cxflags = cxflags
--         },
--         includedirs = {
--             "/usr/include/qt6"
--         },
--         includes = {"QtNodes/GraphicsView", "QtNodes/BasicGraphicsScene", "QtNodes/AbstractGraphModel"}
--     }))
-- end)
