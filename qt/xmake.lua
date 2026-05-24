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
    add_files("inc/**/ipcatalogpanel.h")
    add_files("inc/**/ipcorepathsdialog.h")
    add_files("inc/**/projectstateservice.h")
    add_files("inc/**/projectipservice.h")
    add_files("inc/**/activeworkspacecontroller.h")
    add_files("inc/**/validationmanager.h")

    add_includedirs("inc")

local function add_qt_test_target(name, source_files, extra_files, qt_rule)
    target(name)
        add_rules(qt_rule or "qt.console")
        set_kind("binary")
        set_group("test")
        set_default(false)
        set_languages("c++23")

        add_includedirs("inc")
        add_files(source_files)
        add_files("src/app/appsettings.cpp")
        add_files("src/ipcraft/ipcraftconnectionvalidator.cpp")
        add_files("src/ipcraft/ipcraftmanifest.cpp")
        add_files("src/ipcraft/ipcraftmanifestreader.cpp")
        add_files("src/ipcraft/packagespec.cpp")
        add_files("src/ipcraft/diagnostics.cpp")
        add_files("src/ipcraft/jsonhelpers.cpp")
        add_files("src/ipcraft/ipcraftregistry.cpp")
        if extra_files then
            add_files(extra_files)
        end
        add_tests("default", {
            trim_output = true,
            pass_outputs = name .. " passed"
        })
end

add_qt_test_target("graph_test", "test/graph_test.cpp", {
    "src/commands/addconnectioncommand.cpp",
    "src/commands/command.cpp",
    "src/commands/commandmanager.cpp",
    "src/commands/removeconnectioncommand.cpp",
    "src/commands/removemodulecommand.cpp",
    "src/connection/connectionruleservice.cpp",
    "src/**/graph.cpp",
    "src/**/module.cpp",
    "src/**/connection.cpp",
    "src/**/port.cpp",
    "src/**/parameter.cpp",
    "src/**/moduleregistry.cpp",
    "src/**/moduleprovider.cpp",
    "src/**/ipcoreruntimeregistry.cpp",
    "inc/**/connectionruleservice.h",
    "inc/**/addconnectioncommand.h",
    "inc/**/commandmanager.h",
    "inc/**/removeconnectioncommand.h",
    "inc/**/removemodulecommand.h",
    "inc/**/graph.h",
    "inc/**/module.h",
    "inc/**/ipcoreruntimeregistry.h",
    "inc/**/ipcoreruntimedescriptor.h"
})

add_qt_test_target("connectionruleservice_test", "test/connectionruleservice_test.cpp", {
    "src/connection/connectionruleservice.cpp",
    "src/**/graph.cpp",
    "src/**/module.cpp",
    "src/**/connection.cpp",
    "src/**/port.cpp",
    "src/**/parameter.cpp",
    "src/**/moduleregistry.cpp",
    "src/**/moduleprovider.cpp",
    "src/**/ipcoreruntimeregistry.cpp",
    "inc/**/connectionruleservice.h",
    "inc/**/graph.h",
    "inc/**/module.h",
    "inc/**/ipcoreruntimeregistry.h",
    "inc/**/ipcoreruntimedescriptor.h"
})

add_qt_test_target("ipcoregraphexporter_test", "test/ipcoregraphexporter_test.cpp", {
    "src/ipcore/ipcoregraphexporter.cpp",
    "src/ipcore/ipcatalogservice.cpp",
    "src/**/graph.cpp",
    "src/**/module.cpp",
    "src/**/connection.cpp",
    "src/**/port.cpp",
    "src/**/parameter.cpp",
    "src/**/moduleregistry.cpp",
    "src/**/moduleprovider.cpp",
    "src/**/ipcoreruntimeregistry.cpp",
    "inc/ipcore/ipcoregraphexporter.h",
    "inc/ipcore/ipcatalogservice.h",
    "inc/**/graph.h",
    "inc/**/module.h",
    "inc/**/ipcoreruntimeregistry.h",
    "inc/**/ipcoreruntimedescriptor.h"
})

add_qt_test_target("commandmanager_test", "test/commandmanager_test.cpp", {
    "src/**/command.cpp",
    "src/commands/compositecommand.cpp",
    "src/**/commandmanager.cpp"
})

add_qt_test_target("arrangecommand_test", "test/arrangecommand_test.cpp", {
    "src/commands/arrangecommand.cpp",
    "src/**/command.cpp",
    "src/**/graph.cpp",
    "src/**/module.cpp",
    "src/**/connection.cpp",
    "src/**/port.cpp",
    "src/**/parameter.cpp",
    "src/**/moduleregistry.cpp",
    "src/**/moduleprovider.cpp",
    "src/**/ipcoreruntimeregistry.cpp",
    "inc/**/arrangecommand.h",
    "inc/**/graph.h",
    "inc/**/module.h",
    "inc/**/ipcoreruntimeregistry.h",
    "inc/**/ipcoreruntimedescriptor.h"
})

add_qt_test_target("validation_test", "test/validation_test.cpp", {
    "src/connection/connectionruleservice.cpp",
    "src/ipcraft/ipcraftbuiltinvalidator.cpp",
    "src/ipcraft/ipxactconnectionchecker.cpp",
    "src/ipcraft/compositionmodel.cpp",
    "src/ipcore/ipcoregraphexporter.cpp",
    "src/ipcore/ipcatalogservice.cpp",
    "src/**/drcrunner.cpp",
    "src/**/projectvalidationrunner.cpp",
    "src/**/validationmanager.cpp",
    "src/**/logpanel.cpp",
    "src/app/generationartifacts.cpp",
    "src/project/projectstateservice.cpp",
    "src/**/ipcorecommandrunner.cpp",
    "src/**/graphprojectserializer.cpp",
    "src/**/projectwriter.cpp",
    "src/**/validator.cpp",
    "src/**/validationresult.cpp",
    "src/**/graph.cpp",
    "src/**/module.cpp",
    "src/**/connection.cpp",
    "src/**/port.cpp",
    "src/**/parameter.cpp",
    "src/**/moduleregistry.cpp",
    "src/**/moduleprovider.cpp",
    "src/**/ipcoreruntimeregistry.cpp",
    "inc/**/connectionruleservice.h",
    "inc/**/graph.h",
    "inc/**/module.h",
    "inc/**/drcrunner.h",
    "inc/**/projectvalidationrunner.h",
    "inc/**/validationmanager.h",
    "inc/**/logpanel.h",
    "inc/ipcore/ipcoregraphexporter.h",
    "inc/ipcore/ipcatalogservice.h",
    "inc/app/generationartifacts.h",
    "inc/project/projectstateservice.h",
    "inc/**/ipcorecommandrunner.h",
    "inc/**/graphprojectserializer.h",
    "inc/**/projectdocument.h",
    "inc/**/projectwriter.h",
    "inc/**/ipinstancestate.h",
    "inc/**/ipcoreruntimeregistry.h",
    "inc/**/ipcoreruntimedescriptor.h"
}, "qt.widgetapp")

add_qt_test_target("uiscale_test", "test/uiscale_test.cpp", {
    "src/**/uiscale.cpp",
    "inc/**/uiscale.h"
})

add_qt_test_target("logformat_test", "test/logformat_test.cpp", {
    "src/**/logformat.cpp",
    "inc/**/logformat.h"
})

add_qt_test_target("appsettings_test", "test/appsettings_test.cpp", {
    "inc/app/appsettings.h"
})

add_qt_test_target("startupflow_test", "test/startupflow_test.cpp", {
    "src/app/startupflow.cpp",
    "inc/app/startupflow.h",
    "inc/app/projectlauncher.h"
})

add_qt_test_target("projectdocument_test", "test/projectdocument_test.cpp", {
    "src/connection/connectionruleservice.cpp",
    "src/ipcraft/compositionmodel.cpp",
    "src/ipcore/ipcoregraphexporter.cpp",
    "src/ipcore/ipcatalogservice.cpp",
    "src/**/projectreader.cpp",
    "src/**/projectwriter.cpp",
    "src/**/projectstateservice.cpp",
    "src/**/graphprojectserializer.cpp",
    "src/app/generationartifacts.cpp",
    "src/**/graph.cpp",
    "src/**/module.cpp",
    "src/**/connection.cpp",
    "src/**/port.cpp",
    "src/**/parameter.cpp",
    "src/**/moduleregistry.cpp",
    "src/**/moduleprovider.cpp",
    "src/**/ipcoreruntimeregistry.cpp",
    "inc/**/connectionruleservice.h",
    "inc/ipcraft/compositionmodel.h",
    "inc/**/projectdocument.h",
    "inc/**/ipinstancestate.h",
    "inc/**/projectstateservice.h",
    "inc/**/projectreader.h",
    "inc/**/projectwriter.h",
    "inc/**/graphprojectserializer.h",
    "inc/app/generationartifacts.h",
    "inc/ipcore/ipcoregraphexporter.h",
    "inc/ipcore/ipcatalogservice.h",
    "inc/**/graph.h",
    "inc/**/module.h",
    "inc/**/ipcoreruntimeregistry.h",
    "inc/**/ipcoreruntimedescriptor.h"
})

add_qt_test_target("projectgenerationrunner_test", "test/projectgenerationrunner_test.cpp", {
    "src/app/projectgenerationrunner.cpp",
    "src/connection/connectionruleservice.cpp",
    "src/ipcraft/flowrunner.cpp",
    "src/ipcraft/artifactmodel.cpp",
    "src/ipcraft/emitter.cpp",
    "src/ipcraft/configschema.cpp",
    "src/ipcraft/value.cpp",
    "src/ipcraft/compositionmodel.cpp",
    "src/ipcraft/ipcraftbuiltinvalidator.cpp",
    "src/ipcraft/ipxactconnectionchecker.cpp",
    "src/ipcore/ipcatalogservice.cpp",
    "src/ipcore/ipcorecommandrunner.cpp",
    "src/ipcore/ipcoregraphexporter.cpp",
    "src/project/projectreader.cpp",
    "src/project/projectwriter.cpp",
    "src/project/projectstateservice.cpp",
    "src/project/graphprojectserializer.cpp",
    "src/app/generationartifacts.cpp",
    "src/validation/validationresult.cpp",
    "src/**/graph.cpp",
    "src/**/module.cpp",
    "src/**/connection.cpp",
    "src/**/port.cpp",
    "src/**/parameter.cpp",
    "src/**/moduleregistry.cpp",
    "src/**/moduleprovider.cpp",
    "src/**/ipcoreruntimeregistry.cpp",
    "inc/app/projectgenerationrunner.h",
    "inc/app/generationartifacts.h",
    "inc/ipcraft/flowrunner.h",
    "inc/ipcraft/artifactmodel.h",
    "inc/ipcraft/emitter.h",
    "inc/connection/connectionruleservice.h",
    "inc/ipcore/ipcatalogservice.h",
    "inc/ipcore/ipcorecommandrunner.h",
    "inc/ipcore/ipcoregraphexporter.h",
    "inc/project/projectreader.h",
    "inc/project/projectwriter.h",
    "inc/project/projectstateservice.h",
    "inc/project/graphprojectserializer.h",
    "inc/project/ipinstancestate.h",
    "inc/**/graph.h",
    "inc/**/module.h",
    "inc/**/ipcoreruntimeregistry.h",
    "inc/**/ipcoreruntimedescriptor.h"
})

add_qt_test_target("ipcatalogservice_test", "test/ipcatalogservice_test.cpp", {
    "src/ipcore/ipcatalogservice.cpp",
    "src/ipcore/ipcorecommandrunner.cpp",
    "src/modules/moduleregistry.cpp",
    "src/modules/moduleprovider.cpp",
    "src/ipcore/ipcoreruntimeregistry.cpp",
    "src/graph/parameter.cpp",
    "src/graph/port.cpp",
    "inc/ipcore/ipcatalogservice.h",
    "inc/ipcore/ipcorecommandrunner.h",
    "inc/**/moduleregistry.h",
    "inc/**/ipcoreruntimedescriptor.h"
})

add_qt_test_target("ipcraftmanifest_test", "test/ipcraftmanifest_test.cpp", {
    "inc/ipcraft/*.h"
})

target("ipcraft_diagnostics_test")
    add_rules("qt.console")
    set_kind("binary")
    set_group("test")
    set_default(false)
    set_languages("c++23")

    add_includedirs("inc")
    add_files("test/ipcraft_diagnostics_test.cpp")
    add_files("src/ipcraft/diagnostics.cpp")
    add_files("src/ipcraft/jsonhelpers.cpp")
    add_files("inc/ipcraft/diagnostics.h")
    add_files("inc/ipcraft/jsonhelpers.h")
    add_files("inc/ipcraft/schemaids.h")
    add_tests("default", {
        trim_output = true,
        pass_outputs = "ipcraft_diagnostics_test passed"
    })

target("ipcraft_project_model_test")
    add_rules("qt.console")
    set_kind("binary")
    set_group("test")
    set_default(false)
    set_languages("c++23")

    add_includedirs("inc")
    add_files("test/ipcraft_project_model_test.cpp")
    add_files("src/project/projectreader.cpp")
    add_files("src/project/projectwriter.cpp")
    add_files("src/ipcraft/compositionmodel.cpp")
    add_files("src/ipcraft/diagnostics.cpp")
    add_files("src/ipcraft/jsonhelpers.cpp")
    add_files("inc/ipcraft/compositionmodel.h")
    add_files("inc/ipcraft/packagespec.h")
    add_files("inc/project/projectdocument.h")
    add_files("inc/project/ipinstancestate.h")
    add_files("inc/project/projectreader.h")
    add_files("inc/project/projectwriter.h")
    add_files("inc/ipcraft/diagnostics.h")
    add_files("inc/ipcraft/jsonhelpers.h")
    add_files("inc/ipcraft/schemaids.h")
    add_tests("default", {
        trim_output = true,
        pass_outputs = "ipcraft_project_model_test passed"
    })

target("ipcraft_package_spec_test")
    add_rules("qt.console")
    set_kind("binary")
    set_group("test")
    set_default(false)
    set_languages("c++23")

    add_includedirs("inc")
    add_files("test/ipcraft_package_spec_test.cpp")
    add_files("src/ipcraft/packagespec.cpp")
    add_files("src/ipcraft/diagnostics.cpp")
    add_files("src/ipcraft/jsonhelpers.cpp")
    add_files("inc/ipcraft/packagespec.h")
    add_files("inc/ipcraft/diagnostics.h")
    add_files("inc/ipcraft/jsonhelpers.h")
    add_files("inc/ipcraft/schemaids.h")
    add_tests("default", {
        trim_output = true,
        pass_outputs = "ipcraft_package_spec_test passed"
    })

target("ipcraft_config_validation_test")
    add_rules("qt.console")
    set_kind("binary")
    set_group("test")
    set_default(false)
    set_languages("c++23")

    add_includedirs("inc")
    add_files("test/ipcraft_config_validation_test.cpp")
    add_files("src/ipcraft/configschema.cpp")
    add_files("src/ipcraft/value.cpp")
    add_files("src/ipcraft/diagnostics.cpp")
    add_files("src/ipcraft/jsonhelpers.cpp")
    add_files("inc/ipcraft/configschema.h")
    add_files("inc/ipcraft/value.h")
    add_files("inc/ipcraft/diagnostics.h")
    add_files("inc/ipcraft/jsonhelpers.h")
    add_files("inc/ipcraft/schemaids.h")
    add_tests("default", {
        trim_output = true,
        pass_outputs = "ipcraft_config_validation_test passed"
    })

target("ipcraft_composition_test")
    add_rules("qt.console")
    set_kind("binary")
    set_group("test")
    set_default(false)
    set_languages("c++23")

    add_includedirs("inc")
    add_files("test/ipcraft_composition_test.cpp")
    add_files("src/ipcraft/compositionmodel.cpp")
    add_files("src/ipcraft/layoutmodel.cpp")
    add_files("src/ipcraft/diagnostics.cpp")
    add_files("src/ipcraft/jsonhelpers.cpp")
    add_files("inc/ipcraft/compositionmodel.h")
    add_files("inc/ipcraft/layoutmodel.h")
    add_files("inc/ipcraft/packagespec.h")
    add_files("inc/ipcraft/diagnostics.h")
    add_files("inc/ipcraft/jsonhelpers.h")
    add_files("inc/ipcraft/schemaids.h")
    add_tests("default", {
        trim_output = true,
        pass_outputs = "ipcraft_composition_test passed"
    })

target("ipcraft_emitter_test")
    add_rules("qt.console")
    set_kind("binary")
    set_group("test")
    set_default(false)
    set_languages("c++23")

    add_includedirs("inc")
    add_files("test/ipcraft_emitter_test.cpp")
    add_files("src/ipcraft/emitter.cpp")
    add_files("src/ipcraft/configschema.cpp")
    add_files("src/ipcraft/value.cpp")
    add_files("src/ipcraft/compositionmodel.cpp")
    add_files("src/ipcraft/diagnostics.cpp")
    add_files("src/ipcraft/jsonhelpers.cpp")
    add_files("inc/ipcraft/emitter.h")
    add_files("inc/ipcraft/configschema.h")
    add_files("inc/ipcraft/value.h")
    add_files("inc/ipcraft/compositionmodel.h")
    add_files("inc/ipcraft/packagespec.h")
    add_files("inc/ipcraft/diagnostics.h")
    add_files("inc/ipcraft/jsonhelpers.h")
    add_files("inc/ipcraft/schemaids.h")
    add_tests("default", {
        trim_output = true,
        pass_outputs = "ipcraft_emitter_test passed"
    })

target("ipcraft_artifact_test")
    add_rules("qt.console")
    set_kind("binary")
    set_group("test")
    set_default(false)
    set_languages("c++23")

    add_includedirs("inc")
    add_files("test/ipcraft_artifact_test.cpp")
    add_files("src/ipcraft/artifactmodel.cpp")
    add_files("src/ipcraft/diagnostics.cpp")
    add_files("src/ipcraft/jsonhelpers.cpp")
    add_files("inc/ipcraft/artifactmodel.h")
    add_files("inc/ipcraft/packagespec.h")
    add_files("inc/ipcraft/diagnostics.h")
    add_files("inc/ipcraft/jsonhelpers.h")
    add_files("inc/ipcraft/schemaids.h")
    add_tests("default", {
        trim_output = true,
        pass_outputs = "ipcraft_artifact_test passed"
    })

target("ipcraft_flowrunner_test")
    add_rules("qt.console")
    set_kind("binary")
    set_group("test")
    set_default(false)
    set_languages("c++23")

    add_includedirs("inc")
    add_files("test/ipcraft_flowrunner_test.cpp")
    add_files("src/ipcraft/flowrunner.cpp")
    add_files("src/ipcraft/artifactmodel.cpp")
    add_files("src/ipcraft/emitter.cpp")
    add_files("src/ipcraft/configschema.cpp")
    add_files("src/ipcraft/value.cpp")
    add_files("src/ipcraft/compositionmodel.cpp")
    add_files("src/ipcraft/diagnostics.cpp")
    add_files("src/ipcraft/jsonhelpers.cpp")
    add_files("src/ipcraft/ipcraftmanifest.cpp")
    add_files("src/ipcraft/ipcraftmanifestreader.cpp")
    add_files("src/ipcraft/ipcraftregistry.cpp")
    add_files("src/ipcraft/packagespec.cpp")
    add_files("src/app/appsettings.cpp")
    add_files("src/validation/projectvalidationrunner.cpp")
    add_files("src/validation/validationresult.cpp")
    add_files("src/ipcraft/ipcraftbuiltinvalidator.cpp")
    add_files("src/ipcraft/ipcraftconnectionvalidator.cpp")
    add_files("src/ipcraft/ipxactconnectionchecker.cpp")
    add_files("src/validation/validator.cpp")
    add_files("src/modules/moduleregistry.cpp")
    add_files("src/modules/moduleprovider.cpp")
    add_files("src/ipcore/ipcoreruntimeregistry.cpp")
    add_files("src/graph/graph.cpp")
    add_files("src/graph/module.cpp")
    add_files("src/graph/connection.cpp")
    add_files("src/graph/port.cpp")
    add_files("src/graph/parameter.cpp")
    add_files("inc/ipcraft/flowrunner.h")
    add_files("inc/ipcraft/artifactmodel.h")
    add_files("inc/ipcraft/emitter.h")
    add_files("inc/ipcraft/configschema.h")
    add_files("inc/ipcraft/value.h")
    add_files("inc/ipcraft/compositionmodel.h")
    add_files("inc/ipcraft/packagespec.h")
    add_files("inc/ipcraft/diagnostics.h")
    add_files("inc/ipcraft/jsonhelpers.h")
    add_files("inc/ipcraft/schemaids.h")
    add_files("inc/validation/projectvalidationrunner.h")
    add_files("inc/graph/graph.h")
    add_files("inc/graph/module.h")
    add_tests("default", {
        trim_output = true,
        pass_outputs = "ipcraft_flowrunner_test passed"
    })

target("ipcraft-cli")
    add_rules("qt.console")
    set_kind("binary")
    set_default(false)
    set_languages("c++23")

    add_includedirs("inc")
    add_files("cli/ipcraft_cli_main.cpp")
    add_files("src/cli/cliresult.cpp")
    add_files("src/project/projectreader.cpp")
    add_files("src/project/projectwriter.cpp")
    add_files("src/ipcraft/packagespec.cpp")
    add_files("src/ipcraft/configschema.cpp")
    add_files("src/ipcraft/value.cpp")
    add_files("src/ipcraft/compositionmodel.cpp")
    add_files("src/ipcraft/diagnostics.cpp")
    add_files("src/ipcraft/jsonhelpers.cpp")
    add_files("src/ipcraft/migration.cpp")
    add_files("src/ipcraft/emitter.cpp")
    add_files("src/ipcraft/artifactmodel.cpp")
    add_files("src/ipcraft/flowrunner.cpp")
    add_files("inc/cli/cliresult.h")
    add_files("inc/project/projectreader.h")
    add_files("inc/project/projectwriter.h")
    add_files("inc/project/projectdocument.h")
    add_files("inc/project/ipinstancestate.h")
    add_files("inc/ipcraft/packagespec.h")
    add_files("inc/ipcraft/configschema.h")
    add_files("inc/ipcraft/value.h")
    add_files("inc/ipcraft/compositionmodel.h")
    add_files("inc/ipcraft/diagnostics.h")
    add_files("inc/ipcraft/jsonhelpers.h")
    add_files("inc/ipcraft/migration.h")
    add_files("inc/ipcraft/emitter.h")
    add_files("inc/ipcraft/artifactmodel.h")
    add_files("inc/ipcraft/flowrunner.h")
    add_files("inc/ipcraft/schemaids.h")

target("ipcraft_cli_contract_test")
    add_rules("qt.console")
    set_kind("binary")
    set_group("test")
    set_default(false)
    set_languages("c++23")

    add_deps("ipcraft-cli")
    add_includedirs("inc")
    add_files("test/ipcraft_cli_contract_test.cpp")
    add_files("inc/ipcraft/schemaids.h")
    add_tests("default", {
        trim_output = true,
        pass_outputs = "ipcraft_cli_contract_test passed"
    })

target("ipcraft_migration_test")
    add_rules("qt.console")
    set_kind("binary")
    set_group("test")
    set_default(false)
    set_languages("c++23")

    add_deps("ipcraft-cli")
    add_includedirs("inc")
    add_files("test/ipcraft_migration_test.cpp")
    add_files("src/ipcraft/migration.cpp")
    add_files("src/project/projectreader.cpp")
    add_files("src/project/projectwriter.cpp")
    add_files("src/ipcraft/compositionmodel.cpp")
    add_files("src/ipcraft/diagnostics.cpp")
    add_files("src/ipcraft/jsonhelpers.cpp")
    add_files("inc/ipcraft/migration.h")
    add_tests("default", {
        trim_output = true,
        pass_outputs = "ipcraft_migration_test passed"
    })

target("ipcraft_contract_examples_test")
    add_rules("qt.console")
    set_kind("binary")
    set_group("test")
    set_default(false)
    set_languages("c++23")

    add_includedirs("inc")
    add_files("test/ipcraft_contract_examples_test.cpp")
    add_files("src/ipcraft/packagespec.cpp")
    add_files("src/project/projectreader.cpp")
    add_files("src/ipcraft/configschema.cpp")
    add_files("src/ipcraft/value.cpp")
    add_files("src/ipcraft/compositionmodel.cpp")
    add_files("src/ipcraft/diagnostics.cpp")
    add_files("src/ipcraft/jsonhelpers.cpp")
    add_tests("default", {
        trim_output = true,
        pass_outputs = "ipcraft_contract_examples_test passed"
    })

add_qt_test_target("ipcraft_phase_review_test", "test/ipcraft_phase_review_test.cpp", {
    "src/ipcore/ipcatalogservice.cpp",
    "src/ipcore/ipcorecommandrunner.cpp",
    "src/modules/moduleregistry.cpp",
    "src/modules/moduleprovider.cpp",
    "src/ipcore/ipcoreruntimeregistry.cpp",
    "src/graph/parameter.cpp",
    "src/graph/port.cpp",
    "inc/ipcore/ipcatalogservice.h",
    "inc/ipcore/ipcorecommandrunner.h",
    "inc/**/moduleregistry.h",
    "inc/**/moduleprovider.h",
    "inc/**/ipcoreruntimedescriptor.h"
})

add_qt_test_target("ipxactconnectionchecker_test", "test/ipxactconnectionchecker_test.cpp", {
    "src/ipcraft/ipxactconnectionchecker.cpp",
    "inc/ipcraft/ipxactconnectionchecker.h"
})

add_qt_test_target("projectipservice_test", "test/projectipservice_test.cpp", {
    "src/project/projectipservice.cpp",
    "src/project/projectstateservice.cpp",
    "src/ipcore/ipcatalogservice.cpp",
    "src/workspace/activeworkspacecontroller.cpp",
    "src/modules/moduleregistry.cpp",
    "src/modules/moduleprovider.cpp",
    "src/ipcore/ipcoreruntimeregistry.cpp",
    "src/graph/parameter.cpp",
    "src/graph/port.cpp",
    "inc/project/projectipservice.h",
    "inc/project/projectstateservice.h",
    "inc/ipcore/ipcatalogservice.h",
    "inc/workspace/activeworkspacecontroller.h",
    "inc/**/moduleregistry.h",
    "inc/**/ipcoreruntimedescriptor.h"
})

target("removeipinstancecommand_test")
    add_rules("qt.console")
    set_kind("binary")
    set_group("test")
    set_default(false)
    set_languages("c++23")

    add_includedirs("inc")
    add_files("test/removeipinstancecommand_test.cpp")
    add_files("src/commands/command.cpp")
    add_files("src/commands/commandmanager.cpp")
    add_files("src/commands/addipinstancecommand.cpp")
    add_files("src/commands/removeipinstancecommand.cpp")
    add_files("src/graph/graph.cpp")
    add_files("src/graph/module.cpp")
    add_files("src/graph/connection.cpp")
    add_files("src/graph/port.cpp")
    add_files("src/graph/parameter.cpp")
    add_files("src/project/projectipservice.cpp")
    add_files("src/project/projectstateservice.cpp")
    add_files("inc/commands/commandmanager.h")
    add_files("inc/commands/addipinstancecommand.h")
    add_files("inc/commands/removeipinstancecommand.h")
    add_files("inc/graph/graph.h")
    add_files("inc/graph/module.h")
    add_files("inc/project/projectipservice.h")
    add_files("inc/project/projectstateservice.h")
    add_tests("default", {
        trim_output = true,
        pass_outputs = "removeipinstancecommand_test passed"
    })

target("logpanel_test")
    add_rules("qt.widgetapp")
    set_kind("binary")
    set_group("test")
    set_default(false)
    set_languages("c++23")

    add_includedirs("inc")
    add_files("test/logpanel_test.cpp")
    add_files("src/graph/connection.cpp")
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
    add_files("src/app/appsettings.cpp")
    add_files("src/panels/propertypanel.cpp")
    add_files("src/widgets/collapsiblesection.cpp")
    add_files("src/project/projectstateservice.cpp")
    add_files("src/project/runtimeipinstanceparameteradapter.cpp")
    add_files("src/commands/setconnectionclasscommand.cpp")
    add_files("src/commands/setipinstanceparametercommand.cpp")
    add_files("src/commands/setparametercommand.cpp")
    add_files("src/commands/command.cpp")
    add_files("src/commands/commandmanager.cpp")
    add_files("src/graph/graph.cpp")
    add_files("src/graph/module.cpp")
    add_files("src/graph/connection.cpp")
    add_files("src/graph/port.cpp")
    add_files("src/graph/parameter.cpp")
    add_files("src/modules/moduleregistry.cpp")
    add_files("src/modules/moduleprovider.cpp")
    add_files("src/ipcraft/ipcraftconnectionvalidator.cpp")
    add_files("src/ipcraft/ipcraftmanifest.cpp")
    add_files("src/ipcraft/ipcraftmanifestreader.cpp")
    add_files("src/ipcraft/packagespec.cpp")
    add_files("src/ipcraft/diagnostics.cpp")
    add_files("src/ipcraft/jsonhelpers.cpp")
    add_files("src/ipcraft/ipcraftregistry.cpp")
    add_files("src/ipcore/ipcoreruntimeregistry.cpp")
    add_files("inc/**/propertypanel.h")
    add_files("inc/**/graph.h")
    add_files("inc/**/module.h")
    add_files("inc/**/projectstateservice.h")
    add_files("inc/project/ipinstanceparameteradapter.h")
    add_files("inc/**/setconnectionclasscommand.h")
    add_files("inc/**/setipinstanceparametercommand.h")
    add_files("inc/**/projectdocument.h")
    add_files("inc/**/ipinstancestate.h")
    add_files("inc/**/ipcoreruntimeregistry.h")
    add_files("inc/**/ipcoreruntimedescriptor.h")
    add_tests("default", {
        trim_output = true,
        pass_outputs = "propertypanel_test passed"
    })

target("ipcatalogpanel_test")
    add_rules("qt.widgetapp")
    set_kind("binary")
    set_group("test")
    set_default(false)
    set_languages("c++23")

    add_packages("nodeeditor")
    add_includedirs("inc")
    add_files("test/ipcatalogpanel_test.cpp")
    add_files("src/app/appsettings.cpp")
    add_files("src/app/mainwindow.cpp")
    add_files("src/app/generationartifacts.cpp")
    add_files("src/app/logformat.cpp")
    add_files("src/app/projectgenerationrunner.cpp")
    add_files("src/app/projectlauncher.cpp")
    add_files("src/panels/*.cpp")
    add_files("src/widgets/*.cpp")
    add_files("src/ipcraft/artifactmodel.cpp")
    add_files("src/ipcraft/compositionmodel.cpp")
    add_files("src/ipcraft/configschema.cpp")
    add_files("src/ipcraft/emitter.cpp")
    add_files("src/ipcraft/flowrunner.cpp")
    add_files("src/ipcraft/ipcraftbuiltinvalidator.cpp")
    add_files("src/ipcraft/ipxactconnectionchecker.cpp")
    add_files("src/ipcraft/ipcraftconnectionvalidator.cpp")
    add_files("src/ipcraft/ipcraftmanifest.cpp")
    add_files("src/ipcraft/ipcraftmanifestreader.cpp")
    add_files("src/ipcraft/packagespec.cpp")
    add_files("src/ipcraft/value.cpp")
    add_files("src/ipcraft/diagnostics.cpp")
    add_files("src/ipcraft/jsonhelpers.cpp")
    add_files("src/ipcraft/ipcraftregistry.cpp")
    add_files("src/ipcore/*.cpp")
    add_files("src/workspace/*.cpp")
    add_files("src/project/*.cpp")
    add_files("src/validation/*.cpp")
    add_files("src/topology/*.cpp")
    add_files("src/connection/*.cpp")
    add_files("src/commands/*.cpp")
    add_files("src/graph/*.cpp")
    add_files("src/modules/*.cpp")
    add_files("src/nodeeditor/*.cpp")
    add_files("src/nodeeditor/events/*.cpp")
    add_files("inc/**/mainwindow.h")
    add_files("inc/**/graph.h")
    add_files("inc/**/module.h")
    add_files("inc/**/logpanel.h")
    add_files("inc/**/nodeeditorwidget.h")
    add_files("inc/**/graphnodemodel.h")
    add_files("inc/**/propertypanel.h")
    add_files("inc/**/ipcatalogpanel.h")
    add_files("inc/**/ipcorepathsdialog.h")
    add_files("inc/**/projectstateservice.h")
    add_files("inc/**/projectipservice.h")
    add_files("inc/**/activeworkspacecontroller.h")
    add_files("inc/**/validationmanager.h")
    add_tests("default", {
        trim_output = true,
        pass_outputs = "ipcatalogpanel_test passed"
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
    add_files("src/app/appsettings.cpp")
    add_files("src/connection/connectionruleservice.cpp")
    add_files("src/commands/addconnectioncommand.cpp")
    add_files("src/commands/addmodulecommand.cpp")
    add_files("src/commands/arrangecommand.cpp")
    add_files("src/commands/command.cpp")
    add_files("src/commands/commandmanager.cpp")
    add_files("src/commands/removeconnectioncommand.cpp")
    add_files("src/commands/removemodulecommand.cpp")
    add_files("src/commands/setparametercommand.cpp")
    add_files("src/graph/graph.cpp")
    add_files("src/graph/connection.cpp")
    add_files("src/**/module.cpp")
    add_files("src/**/parameter.cpp")
    add_files("src/**/port.cpp")
    add_files("src/ipcraft/ipcraftconnectionvalidator.cpp")
    add_files("src/ipcraft/ipcraftmanifest.cpp")
    add_files("src/ipcraft/ipcraftmanifestreader.cpp")
    add_files("src/ipcraft/packagespec.cpp")
    add_files("src/ipcraft/diagnostics.cpp")
    add_files("src/ipcraft/jsonhelpers.cpp")
    add_files("src/ipcraft/ipcraftregistry.cpp")
    add_files("src/ipcore/ipcatalogservice.cpp")
    add_files("src/project/projectipservice.cpp")
    add_files("src/project/projectstateservice.cpp")
    add_files("src/workspace/activeworkspacecontroller.cpp")
    add_files("src/**/moduleregistry.cpp")
    add_files("src/**/moduleprovider.cpp")
    add_files("src/**/ipcoreruntimeregistry.cpp")
    add_files("src/nodeeditor/animatedgraphicsview.cpp")
    add_files("src/nodeeditor/events/nodeeditorwidget_events.cpp")
    add_files("src/nodeeditor/graphnodepainter.cpp")
    add_files("src/nodeeditor/graphnodemodel.cpp")
    add_files("src/nodeeditor/graphnodegeometry.cpp")
    add_files("src/nodeeditor/nodeeditorentityfactory.cpp")
    add_files("src/nodeeditor/nodeeditorwidget.cpp")
    add_files("src/nodeeditor/straightconnectionpainter.cpp")
    add_files("inc/**/connectionruleservice.h")
    add_files("inc/**/graph.h")
    add_files("inc/**/module.h")
    add_files("inc/**/modulelabels.h")
    add_files("inc/**/moduleregistry.h")
    add_files("inc/**/nodeeditorwidget.h")
    add_files("inc/**/ipcatalogservice.h")
    add_files("inc/**/projectipservice.h")
    add_files("inc/**/projectstateservice.h")
    add_files("inc/**/activeworkspacecontroller.h")
    add_files("inc/**/ipinstancestate.h")
    add_files("inc/**/ipcoreruntimeregistry.h")
    add_files("inc/**/ipcoreruntimedescriptor.h")
    add_files("inc/**/graphnodemodel.h")
    add_tests("default", {
        trim_output = true,
        pass_outputs = "nodeeditor_geometry_test passed"
    })

add_qt_test_target("ipcoreruntime_test", "test/ipcoreruntime_test.cpp", {
    "src/**/ipcorecommandrunner.cpp",
    "src/project/runtimeipinstanceparameteradapter.cpp",
    "src/**/graph.cpp",
    "src/**/module.cpp",
    "src/**/connection.cpp",
    "src/**/ipcoreruntimeregistry.cpp",
    "src/**/ipcoreruntimediagnostics.cpp",
    "src/**/moduleprovider.cpp",
    "src/**/moduleregistry.cpp",
    "src/**/parameter.cpp",
    "src/**/port.cpp",
    "inc/**/ipcorecommandrunner.h",
    "inc/project/ipinstanceparameteradapter.h",
    "inc/**/graph.h",
    "inc/**/module.h",
    "inc/**/ipcoreruntimeregistry.h",
    "inc/**/ipcoreruntimedescriptor.h"
})

add_qt_test_target("topology_preset_test", "test/topology_preset_test.cpp", {
    "src/commands/command.cpp",
    "src/commands/commandmanager.cpp",
    "src/commands/topologypresetcommand.cpp",
    "src/connection/connectionruleservice.cpp",
    "src/**/topologypresetbuilder.cpp",
    "src/**/graph.cpp",
    "src/**/module.cpp",
    "src/**/connection.cpp",
    "src/**/port.cpp",
    "src/**/parameter.cpp",
    "src/**/moduleregistry.cpp",
    "src/**/moduleprovider.cpp",
    "src/**/ipcoreruntimeregistry.cpp",
    "inc/**/connectionruleservice.h",
    "inc/**/commandmanager.h",
    "inc/**/topologypresetcommand.h",
    "inc/**/topologypresetbuilder.h",
    "inc/**/graph.h",
    "inc/**/module.h",
    "inc/**/ipcoreruntimedescriptor.h"
})

add_qt_test_target("v1architecturegate_test", "test/v1architecturegate_test.cpp", {
    "src/app/generationartifacts.cpp",
    "src/app/projectgenerationrunner.cpp",
    "src/connection/connectionruleservice.cpp",
    "src/ipcraft/artifactmodel.cpp",
    "src/ipcraft/compositionmodel.cpp",
    "src/ipcraft/configschema.cpp",
    "src/ipcraft/emitter.cpp",
    "src/ipcraft/flowrunner.cpp",
    "src/ipcraft/ipcraftbuiltinvalidator.cpp",
    "src/ipcraft/ipxactconnectionchecker.cpp",
    "src/ipcraft/value.cpp",
    "src/ipcore/ipcatalogservice.cpp",
    "src/ipcore/ipcoregraphexporter.cpp",
    "src/ipcore/ipcorecommandrunner.cpp",
    "src/ipcore/ipcoreruntimeregistry.cpp",
    "src/project/graphprojectserializer.cpp",
    "src/project/projectipservice.cpp",
    "src/project/projectreader.cpp",
    "src/project/projectstateservice.cpp",
    "src/project/projectwriter.cpp",
    "src/topology/topologypresetbuilder.cpp",
    "src/validation/drcrunner.cpp",
    "src/validation/projectvalidationrunner.cpp",
    "src/validation/validator.cpp",
    "src/validation/validationresult.cpp",
    "src/workspace/activeworkspacecontroller.cpp",
    "src/**/graph.cpp",
    "src/**/module.cpp",
    "src/**/connection.cpp",
    "src/**/port.cpp",
    "src/**/parameter.cpp",
    "src/**/moduleregistry.cpp",
    "src/**/moduleprovider.cpp",
    "inc/app/generationartifacts.h",
    "inc/app/projectgenerationrunner.h",
    "inc/connection/connectionruleservice.h",
    "inc/graph/graph.h",
    "inc/graph/module.h",
    "inc/ipcraft/artifactmodel.h",
    "inc/ipcraft/compositionmodel.h",
    "inc/ipcraft/configschema.h",
    "inc/ipcraft/emitter.h",
    "inc/ipcraft/flowrunner.h",
    "inc/ipcore/ipcatalogservice.h",
    "inc/ipcore/ipcoregraphexporter.h",
    "inc/ipcore/ipcorecommandrunner.h",
    "inc/ipcore/ipcoreruntimeregistry.h",
    "inc/project/graphprojectserializer.h",
    "inc/project/projectipservice.h",
    "inc/project/projectreader.h",
    "inc/project/projectstateservice.h",
    "inc/project/projectwriter.h",
    "inc/topology/topologypresetbuilder.h",
    "inc/validation/drcrunner.h",
    "inc/validation/projectvalidationrunner.h",
    "inc/validation/validator.h",
    "inc/validation/validationresult.h",
    "inc/workspace/activeworkspacecontroller.h"
})
