-- xmake build configuration for the Qt editor app and its test binaries.
add_rules("mode.debug", "mode.release")

includes("deps/packages.lua")
add_requires("nodeeditor")

target("qt")
    add_rules("qt.widgetapp")
    set_languages("c++23")

    add_packages("nodeeditor")
    add_headerfiles("inc/**.h")
    add_headerfiles("inc/commands/*.h")
    add_files("src/**.cpp")
    add_files("src/nodeeditor/*.cpp")
    add_files("src/nodeeditor/events/*.cpp")
    add_files("src/commands/*.cpp")
    -- add files with Q_OBJECT meta (only for qt.moc)
    add_files("inc/**/mainwindow.h")
    add_files("inc/**/graph.h")
    add_files("inc/**/module.h")
    add_files("inc/**/logpanel.h")
    add_files("inc/**/nodeeditorwidget.h")
    add_files("inc/**/graphnodemodel.h")
    add_files("inc/**/propertypanel.h")
    add_files("inc/**/palette.h")
    add_files("inc/**/validationmanager.h")

    add_includedirs("inc")

local function add_qt_test_target(name, source_files, extra_files)
    target(name)
        add_rules("qt.console")
        set_kind("binary")
        set_group("test")
        set_default(false)
        set_languages("c++23")

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
    "src/**/graph.cpp",
    "src/**/module.cpp",
    "src/**/connection.cpp",
    "src/**/port.cpp",
    "src/**/parameter.cpp",
    "src/**/frameworkpaths.cpp",
    "src/**/moduleregistry.cpp",
    "src/**/moduleprovider.cpp",
    "src/**/pluginregistry.cpp",
    "inc/**/graph.h",
    "inc/**/module.h",
    "inc/**/pluginregistry.h",
    "inc/**/plugindescriptor.h"
})

add_qt_test_target("commandmanager_test", "test/commandmanager_test.cpp", {
    "src/**/command.cpp",
    "src/**/commandmanager.cpp"
})

add_qt_test_target("validation_test", "test/validation_test.cpp", {
    "src/**/drcrunner.cpp",
    "src/**/generatorrunner.cpp",
    "src/**/validator.cpp",
    "src/**/validationresult.cpp",
    "src/**/graph.cpp",
    "src/**/module.cpp",
    "src/**/connection.cpp",
    "src/**/port.cpp",
    "src/**/parameter.cpp",
    "src/**/frameworkpaths.cpp",
    "src/**/moduleregistry.cpp",
    "src/**/moduleprovider.cpp",
    "src/**/pluginregistry.cpp",
    "inc/**/graph.h",
    "inc/**/module.h",
    "inc/**/drcrunner.h",
    "inc/**/generatorrunner.h",
    "inc/**/pluginregistry.h",
    "inc/**/plugindescriptor.h"
})

add_qt_test_target("uiscale_test", "test/uiscale_test.cpp", {
    "src/**/uiscale.cpp",
    "inc/**/uiscale.h"
})

add_qt_test_target("logformat_test", "test/logformat_test.cpp", {
    "src/**/logformat.cpp",
    "inc/**/logformat.h"
})

add_qt_test_target("projectdocument_test", "test/projectdocument_test.cpp", {
    "src/**/projectreader.cpp",
    "src/**/projectwriter.cpp",
    "src/**/graphprojectserializer.cpp",
    "src/app/generationartifacts.cpp",
    "src/**/graph.cpp",
    "src/**/module.cpp",
    "src/**/connection.cpp",
    "src/**/port.cpp",
    "src/**/parameter.cpp",
    "src/**/frameworkpaths.cpp",
    "src/**/moduleregistry.cpp",
    "src/**/moduleprovider.cpp",
    "src/**/pluginregistry.cpp",
    "inc/**/projectdocument.h",
    "inc/**/projectreader.h",
    "inc/**/projectwriter.h",
    "inc/**/graphprojectserializer.h",
    "inc/app/generationartifacts.h",
    "inc/**/graph.h",
    "inc/**/module.h",
    "inc/**/pluginregistry.h",
    "inc/**/plugindescriptor.h"
})

target("logpanel_test")
    add_rules("qt.widgetapp")
    set_kind("binary")
    set_group("test")
    set_default(false)
    set_languages("c++23")

    add_includedirs("inc")
    add_files("test/logpanel_test.cpp")
    add_files("src/**/logpanel.cpp")
    add_files("src/**/validationresult.cpp")
    add_files("inc/**/logpanel.h")
    add_tests("default", {
        trim_output = true,
        pass_outputs = "logpanel_test passed"
    })

target("propertypanel_test")
    add_rules("qt.widgetapp")
    set_kind("binary")
    set_group("test")
    set_default(false)
    set_languages("c++23")

    add_includedirs("inc")
    add_files("test/propertypanel_test.cpp")
    add_files("src/panels/propertypanel.cpp")
    add_files("src/commands/setipinstanceparametercommand.cpp")
    add_files("src/commands/setparametercommand.cpp")
    add_files("src/commands/command.cpp")
    add_files("src/commands/commandmanager.cpp")
    add_files("src/graph/graph.cpp")
    add_files("src/graph/module.cpp")
    add_files("src/graph/connection.cpp")
    add_files("src/graph/port.cpp")
    add_files("src/graph/parameter.cpp")
    add_files("src/common/frameworkpaths.cpp")
    add_files("src/modules/moduleregistry.cpp")
    add_files("src/modules/moduleprovider.cpp")
    add_files("src/plugins/pluginregistry.cpp")
    add_files("inc/**/propertypanel.h")
    add_files("inc/**/graph.h")
    add_files("inc/**/module.h")
    add_files("inc/**/pluginregistry.h")
    add_files("inc/**/plugindescriptor.h")
    add_tests("default", {
        trim_output = true,
        pass_outputs = "propertypanel_test passed"
    })

target("nodeeditor_geometry_test")
    add_rules("qt.widgetapp")
    set_kind("binary")
    set_group("test")
    set_default(false)
    set_languages("c++23")

    add_packages("nodeeditor")
    add_includedirs("inc")
    add_files("test/nodeeditor_geometry_test.cpp")
    add_files("src/**/module.cpp")
    add_files("src/**/parameter.cpp")
    add_files("src/**/port.cpp")
    add_files("src/**/moduleregistry.cpp")
    add_files("src/**/moduleprovider.cpp")
    add_files("src/**/pluginregistry.cpp")
    add_files("src/**/frameworkpaths.cpp")
    add_files("src/nodeeditor/graphnodemodel.cpp")
    add_files("src/nodeeditor/graphnodegeometry.cpp")
    add_files("inc/**/module.h")
    add_files("inc/**/modulelabels.h")
    add_files("inc/**/moduleregistry.h")
    add_files("inc/**/pluginregistry.h")
    add_files("inc/**/plugindescriptor.h")
    add_files("inc/**/graphnodemodel.h")
    add_tests("default", {
        trim_output = true,
        pass_outputs = "nodeeditor_geometry_test passed"
    })

add_qt_test_target("plugin_test", "test/plugin_test.cpp", {
    "src/**/generatorrunner.cpp",
    "src/**/graph.cpp",
    "src/**/module.cpp",
    "src/**/connection.cpp",
    "src/**/pluginregistry.cpp",
    "src/**/startupdiagnostics.cpp",
    "src/**/moduleprovider.cpp",
    "src/**/moduleregistry.cpp",
    "src/**/frameworkpaths.cpp",
    "src/**/parameter.cpp",
    "src/**/port.cpp",
    "inc/**/generatorrunner.h",
    "inc/**/graph.h",
    "inc/**/module.h",
    "inc/**/pluginregistry.h",
    "inc/**/plugindescriptor.h"
})

add_qt_test_target("topology_preset_test", "test/topology_preset_test.cpp", {
    "src/**/topologypresetbuilder.cpp",
    "src/**/graph.cpp",
    "src/**/module.cpp",
    "src/**/connection.cpp",
    "src/**/port.cpp",
    "src/**/parameter.cpp",
    "src/**/moduleregistry.cpp",
    "src/**/moduleprovider.cpp",
    "src/**/pluginregistry.cpp",
    "src/**/frameworkpaths.cpp",
    "inc/**/topologypresetbuilder.h",
    "inc/**/graph.h",
    "inc/**/module.h",
    "inc/**/plugindescriptor.h"
})
