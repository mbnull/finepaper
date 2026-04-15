-- xmake build configuration for the Qt editor app and its test binaries.
add_rules("mode.debug", "mode.release")

includes("deps/packages.lua")
add_requires("nodeeditor")

target("qt")
    add_rules("qt.widgetapp")
    set_languages("c++20")

    add_packages("nodeeditor")
    add_headerfiles("inc/*.h")
    add_headerfiles("inc/commands/*.h")
    add_files("src/*.cpp")
    add_files("src/nodeeditor/*.cpp")
    add_files("src/nodeeditor/events/*.cpp")
    add_files("src/commands/*.cpp")
    -- add files with Q_OBJECT meta (only for qt.moc)
    add_files("inc/mainwindow.h")
    add_files("inc/graph.h")
    add_files("inc/module.h")
    add_files("inc/logpanel.h")
    add_files("inc/nodeeditorwidget.h")
    add_files("inc/graphnodemodel.h")
    add_files("inc/propertypanel.h")
    add_files("inc/palette.h")
    add_files("inc/validationmanager.h")

    add_includedirs("inc")

local function add_qt_test_target(name, source_files, extra_files)
    target(name)
        add_rules("qt.console")
        set_kind("binary")
        set_group("test")
        set_default(false)
        set_languages("c++20")

        add_includedirs("inc")
        add_files(source_files)
        if extra_files then
            add_files(extra_files)
        end
        add_tests("default", {
            trim_output = true,
            pass_outputs = name .. " passed"
        })
end

add_qt_test_target("graph_test", "test/graph_test.cpp", {
    "src/graph.cpp",
    "src/module.cpp",
    "src/connection.cpp",
    "src/port.cpp",
    "src/parameter.cpp",
    "src/frameworkpaths.cpp",
    "src/moduleregistry.cpp",
    "src/moduleprovider.cpp",
    "inc/graph.h",
    "inc/module.h"
})

add_qt_test_target("commandmanager_test", "test/commandmanager_test.cpp", {
    "src/command.cpp",
    "src/commandmanager.cpp"
})
