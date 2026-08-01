set_project("finepaper")
set_version("0.1.0")
set_languages("c++23")

add_rules("mode.debug", "mode.release")
includes("deps/packages.lua")
add_requires("nodeeditor 3.0.16")

target("finepaper-application")
    add_rules("qt.static")
    set_kind("static")
    add_headerfiles("src/noc/*.h")
    add_headerfiles("src/package/*.h")
    add_headerfiles("src/package/domain/*.h")
    add_headerfiles("src/storage/*.h")
    add_headerfiles("src/execution/*.h")
    add_headerfiles("src/application/*.h")
    add_headerfiles("src/application/package_catalog/*.h")
    add_headerfiles("src/schema/*.h")
    add_files("src/noc/*.cpp")
    add_files("src/package/*.cpp")
    add_files("src/storage/*.cpp")
    add_files("src/execution/*.cpp")
    add_files("src/application/*.cpp")
    add_files("src/application/package_catalog/*.cpp")
    add_files("src/schema/*.cpp")
    add_includedirs("src", {public = true})

target("finepaper")
    add_rules("qt.console")
    set_kind("binary")
    set_rundir(os.projectdir())
    add_deps("finepaper-application")
    add_files("src/cli/main.cpp")
    add_includedirs("src")

target("finepaper-gui")
    add_rules("qt.widgetapp")
    add_frameworks("QtConcurrent")
    set_kind("binary")
    set_rundir(os.projectdir())
    add_deps("finepaper-application")
    add_packages("nodeeditor")
    add_files("src/gui/*.cpp")
    add_files("src/features/attachment/*.cpp")
    add_files("src/features/design_extensions/*.cpp")
    add_files("src/features/domain/*.cpp")
    add_files("src/features/domain/presentation/*.cpp")
    add_files("src/features/operations/*.cpp")
    add_files("src/features/topology/*.cpp")
    add_files("src/ui/common/*.cpp")
    add_files("src/ui/components/*.cpp")
    add_files("src/ui/theme/*.cpp")
    add_files("src/ui/workbench/*.cpp")
    add_includedirs("src")

target("finepaper-tests")
    add_rules("qt.console")
    set_kind("binary")
    set_group("test")
    set_default(false)
    add_deps("finepaper-application")
    add_files("tests/core_tests.cpp")
    add_includedirs("src")
    add_defines('FINEPAPER_SOURCE_DIR="' .. path.unix(os.projectdir()) .. '"')
    add_tests("default", {
        trim_output = true
    })

target("finepaper-cli-tests")
    add_rules("qt.console")
    set_kind("binary")
    set_group("test")
    set_default(false)
    add_deps("finepaper")
    add_files("tests/cli_tests.cpp")
    add_defines('FINEPAPER_SOURCE_DIR="' .. path.unix(os.projectdir()) .. '"')
    add_tests("default", {
        trim_output = true
    })

target("finepaper-design-run-state-tests")
    add_rules("qt.console")
    set_kind("binary")
    set_group("test")
    set_default(false)
    add_files("tests/design_run_state_tests.cpp")
    add_files("src/features/operations/design_run_state.cpp")
    add_includedirs("src")
    add_tests("default", {
        trim_output = true
    })

target("finepaper-domain-model-tests")
    add_rules("qt.console")
    set_kind("binary")
    set_group("test")
    set_default(false)
    add_deps("finepaper-application")
    add_files("tests/domain_model_tests.cpp")
    add_includedirs("src")
    add_tests("default", {
        trim_output = true
    })

target("finepaper-json-version-tests")
    add_rules("qt.console")
    set_kind("binary")
    set_group("test")
    set_default(false)
    add_deps("finepaper-application")
    add_files("tests/json_version_tests.cpp")
    add_includedirs("src")
    add_tests("default", {
        trim_output = true
    })

target("finepaper-package-domain-tests")
    add_rules("qt.console")
    set_kind("binary")
    set_group("test")
    set_default(false)
    add_deps("finepaper-application")
    add_files("tests/package_domain_tests.cpp")
    add_includedirs("src")
    add_tests("default", {
        trim_output = true
    })

target("finepaper-package-catalog-tests")
    add_rules("qt.console")
    set_kind("binary")
    set_group("test")
    set_default(false)
    add_deps("finepaper-application")
    add_files("tests/package_catalog_tests.cpp")
    add_includedirs("src")
    add_defines('FINEPAPER_SOURCE_DIR="' .. path.unix(os.projectdir()) .. '"')
    add_tests("default", {
        trim_output = true
    })

target("finepaper-json-schema-tests")
    add_rules("qt.console")
    set_kind("binary")
    set_group("test")
    set_default(false)
    add_deps("finepaper-application")
    add_files("tests/json_schema_tests.cpp")
    add_includedirs("src")
    add_tests("default", {
        trim_output = true
    })

target("finepaper-application-design-extension-tests")
    add_rules("qt.console")
    set_kind("binary")
    set_group("test")
    set_default(false)
    add_deps("finepaper-application")
    add_files("tests/application_design_extension_tests.cpp")
    add_includedirs("src")
    add_tests("default", {
        trim_output = true
    })

target("finepaper-v3-runtime-tests")
    add_rules("qt.console")
    set_kind("binary")
    set_group("test")
    set_default(false)
    add_deps("finepaper-application")
    add_files("tests/v3_runtime_tests.cpp")
    add_includedirs("src")
    add_defines('FINEPAPER_SOURCE_DIR="' .. path.unix(os.projectdir()) .. '"')
    add_tests("default", {
        trim_output = true
    })

target("finepaper-application-domain-tests")
    add_rules("qt.console")
    set_kind("binary")
    set_group("test")
    set_default(false)
    add_deps("finepaper-application")
    add_files("tests/application_domain_tests.cpp")
    add_includedirs("src")
    add_tests("default", {
        trim_output = true
    })

target("finepaper-application-element-configuration-tests")
    add_rules("qt.console")
    set_kind("binary")
    set_group("test")
    set_default(false)
    add_deps("finepaper-application")
    add_files("tests/application_element_configuration_tests.cpp")
    add_includedirs("src")
    add_tests("default", {
        trim_output = true
    })

target("finepaper-application-endpoint-tests")
    add_rules("qt.console")
    set_kind("binary")
    set_group("test")
    set_default(false)
    add_deps("finepaper-application")
    add_files("tests/application_endpoint_tests.cpp")
    add_includedirs("src")
    add_tests("default", {
        trim_output = true
    })

target("finepaper-mesh-resize-plan-tests")
    add_rules("qt.console")
    set_kind("binary")
    set_group("test")
    set_default(false)
    add_deps("finepaper-application")
    add_files("tests/mesh_resize_plan_tests.cpp")
    add_includedirs("src")
    add_tests("default", {
        trim_output = true
    })

target("finepaper-domain-presentation-tests")
    add_rules("qt.console")
    add_frameworks("QtGui")
    set_kind("binary")
    set_group("test")
    set_default(false)
    add_deps("finepaper-application")
    add_files("tests/domain_presentation_tests.cpp")
    add_files("src/features/domain/domain_presentation.cpp")
    add_files("src/features/domain/presentation/domain_text.cpp")
    add_includedirs("src")
    add_tests("default", {
        trim_output = true
    })

target("finepaper-domain-manager-projection-tests")
    add_rules("qt.console")
    set_kind("binary")
    set_group("test")
    set_default(false)
    add_deps("finepaper-application")
    add_files("tests/domain_manager_projection_tests.cpp")
    add_files("src/features/domain/domain_manager_projection.cpp")
    add_includedirs("src")
    add_tests("default", {
        trim_output = true
    })

target("finepaper-domain-schema-editor-tests")
    add_rules("qt.widgetapp")
    set_kind("binary")
    set_group("test")
    set_default(false)
    add_deps("finepaper-application")
    add_files("tests/domain_schema_editor_tests.cpp")
    add_files("src/features/domain/domain_property_form.cpp")
    add_files("src/ui/common/schema_value_editor.cpp")
    add_includedirs("src")
    add_tests("default", {
        trim_output = true,
        runenvs = {QT_QPA_PLATFORM = "offscreen"}
    })

target("finepaper-element-configuration-panel-tests")
    add_rules("qt.widgetapp")
    set_kind("binary")
    set_group("test")
    set_default(false)
    add_deps("finepaper-application")
    add_files("tests/element_configuration_panel_tests.cpp")
    add_files("src/gui/element_configuration_panel.cpp")
    add_files("src/ui/common/schema_value_editor.cpp")
    add_includedirs("src")
    add_tests("default", {
        trim_output = true,
        runenvs = {QT_QPA_PLATFORM = "offscreen"}
    })

target("finepaper-domain-manager-panel-tests")
    add_rules("qt.widgetapp")
    set_kind("binary")
    set_group("test")
    set_default(false)
    add_deps("finepaper-application")
    add_files("tests/domain_manager_panel_tests.cpp")
    add_files("src/features/domain/domain_instance_dialog.cpp")
    add_files("src/features/domain/domain_manager_panel.cpp")
    add_files("src/features/domain/domain_manager_projection.cpp")
    add_files("src/features/domain/domain_presentation.cpp")
    add_files("src/features/domain/domain_property_form.cpp")
    add_files("src/features/domain/presentation/domain_text.cpp")
    add_files("src/ui/common/schema_value_editor.cpp")
    add_includedirs("src")
    add_tests("default", {
        trim_output = true,
        runenvs = {QT_QPA_PLATFORM = "offscreen"}
    })

target("finepaper-domain-configuration-dialog-tests")
    add_rules("qt.widgetapp")
    set_kind("binary")
    set_group("test")
    set_default(false)
    add_deps("finepaper-application")
    add_files("tests/domain_configuration_dialog_tests.cpp")
    add_files("src/features/domain/domain_configuration_dialog.cpp")
    add_files("src/features/domain/domain_configuration_workspace.cpp")
    add_files("src/features/domain/domain_property_form.cpp")
    add_files("src/features/domain/presentation/domain_text.cpp")
    add_files("src/ui/common/schema_value_editor.cpp")
    add_includedirs("src")
    add_tests("default", {
        trim_output = true,
        runenvs = {QT_QPA_PLATFORM = "offscreen"}
    })

target("finepaper-endpoint-domain-assignment-dialog-tests")
    add_rules("qt.widgetapp")
    set_kind("binary")
    set_group("test")
    set_default(false)
    add_deps("finepaper-application")
    add_files("tests/endpoint_domain_assignment_dialog_tests.cpp")
    add_files("src/features/domain/endpoint_domain_assignment_dialog.cpp")
    add_files("src/features/domain/presentation/domain_text.cpp")
    add_includedirs("src")
    add_tests("default", {
        trim_output = true,
        runenvs = {QT_QPA_PLATFORM = "offscreen"}
    })

target("finepaper-endpoint-configuration-panel-tests")
    add_rules("qt.widgetapp")
    set_kind("binary")
    set_group("test")
    set_default(false)
    add_deps("finepaper-application")
    add_files("tests/endpoint_configuration_panel_tests.cpp")
    add_files("src/gui/endpoint_configuration_panel.cpp")
    add_files("src/features/domain/endpoint_domain_assignment_dialog.cpp")
    add_files("src/features/domain/presentation/domain_text.cpp")
    add_files("src/gui/package_parameter_form.cpp")
    add_files("src/ui/common/schema_value_editor.cpp")
    add_includedirs("src")
    add_tests("default", {
        trim_output = true,
        runenvs = {QT_QPA_PLATFORM = "offscreen"}
    })

target("finepaper-mesh-resize-dialog-tests")
    add_rules("qt.widgetapp")
    set_kind("binary")
    set_group("test")
    set_default(false)
    add_deps("finepaper-application")
    add_files("tests/mesh_resize_dialog_tests.cpp")
    add_files("src/features/topology/mesh_resize_dialog.cpp")
    add_files("src/features/domain/presentation/domain_text.cpp")
    add_includedirs("src")
    add_tests("default", {
        trim_output = true,
        runenvs = {QT_QPA_PLATFORM = "offscreen"}
    })

target("finepaper-topology-workspace-store-tests")
    add_rules("qt.console")
    set_kind("binary")
    set_group("test")
    set_default(false)
    add_files("tests/topology_workspace_store_tests.cpp")
    add_files("src/features/topology/topology_workspace_store.cpp")
    add_includedirs("src")
    add_tests("default", {
        trim_output = true
    })

target("finepaper-endpoint-attachment-rules-tests")
    add_rules("qt.console")
    set_kind("binary")
    set_group("test")
    set_default(false)
    add_deps("finepaper-application")
    add_files("tests/endpoint_attachment_rules_tests.cpp")
    add_files("src/features/attachment/endpoint_attachment_rules.cpp")
    add_includedirs("src")
    add_tests("default", {
        trim_output = true
    })

target("finepaper-design-extensions-ui-tests")
    add_rules("qt.widgetapp")
    set_kind("binary")
    set_group("test")
    set_default(false)
    add_deps("finepaper-application")
    add_files("tests/design_extensions_ui_tests.cpp")
    add_files("src/features/design_extensions/design_extension_editor_dialog.cpp")
    add_files("src/features/design_extensions/design_extensions_workspace.cpp")
    add_includedirs("src")
    add_tests("default", {
        trim_output = true,
        runenvs = {QT_QPA_PLATFORM = "offscreen"}
    })

target("finepaper-gui-smoke")
    add_rules("qt.widgetapp")
    add_frameworks("QtConcurrent")
    set_kind("binary")
    set_group("test")
    set_default(false)
    add_deps("finepaper-application")
    add_packages("nodeeditor")
    add_files("tests/gui_smoke_test.cpp")
    add_files("src/gui/main_window.cpp")
    add_files("src/features/attachment/endpoint_attachment_rules.cpp")
    add_files("src/features/design_extensions/design_extension_editor_dialog.cpp")
    add_files("src/features/design_extensions/design_extensions_workspace.cpp")
    add_files("src/features/topology/animated_graphics_view.cpp")
    add_files("src/features/domain/domain_configuration_dialog.cpp")
    add_files("src/features/domain/domain_configuration_workspace.cpp")
    add_files("src/features/domain/domain_instance_dialog.cpp")
    add_files("src/features/domain/domain_manager_panel.cpp")
    add_files("src/features/domain/domain_manager_projection.cpp")
    add_files("src/features/domain/domain_presentation.cpp")
    add_files("src/features/domain/domain_property_form.cpp")
    add_files("src/features/domain/presentation/domain_text.cpp")
    add_files("src/features/operations/design_run_state.cpp")
    add_files("src/gui/element_configuration_panel.cpp")
    add_files("src/gui/endpoint_configuration_panel.cpp")
    add_files("src/features/domain/endpoint_domain_assignment_dialog.cpp")
    add_files("src/features/topology/mesh_resize_dialog.cpp")
    add_files("src/features/topology/noc_editor_style.cpp")
    add_files("src/features/topology/noc_node_editor.cpp")
    add_files("src/features/topology/topology_workspace_store.cpp")
    add_files("src/gui/package_parameter_form.cpp")
    add_files("src/ui/common/schema_value_editor.cpp")
    add_files("src/ui/components/*.cpp")
    add_files("src/ui/theme/*.cpp")
    add_files("src/ui/workbench/*.cpp")
    add_files("src/gui/workbench_view_registry.cpp")
    add_includedirs("src")
    add_defines('FINEPAPER_SOURCE_DIR="' .. path.unix(os.projectdir()) .. '"')
    add_tests("default", {
        trim_output = true,
        runenvs = {QT_QPA_PLATFORM = "offscreen"}
    })
    add_tests("compact-light", {
        trim_output = true,
        runenvs = {
            QT_QPA_PLATFORM = "offscreen",
            FINEPAPER_GUI_SMOKE_SIZE = "1280x720",
            FINEPAPER_GUI_SMOKE_THEME = "light"
        }
    })
    add_tests("compact-dark-scaled", {
        trim_output = true,
        runenvs = {
            QT_QPA_PLATFORM = "offscreen",
            QT_SCALE_FACTOR = "1.5",
            FINEPAPER_GUI_SMOKE_SIZE = "1280x720",
            FINEPAPER_GUI_SMOKE_THEME = "dark"
        }
    })
    add_tests("compact-large-font", {
        trim_output = true,
        runenvs = {
            QT_QPA_PLATFORM = "offscreen",
            FINEPAPER_GUI_SMOKE_SIZE = "1280x720",
            FINEPAPER_GUI_SMOKE_THEME = "light",
            FINEPAPER_GUI_SMOKE_FONT_SCALE = "1.5"
        }
    })

target("finepaper-workbench-theme-tests")
    add_rules("qt.widgetapp")
    set_kind("binary")
    set_group("test")
    set_default(false)
    add_files("tests/workbench_theme_tests.cpp")
    add_files("src/ui/components/empty_state.cpp")
    add_files("src/ui/theme/ui_tokens.cpp")
    add_files("src/ui/theme/workbench_style.cpp")
    add_includedirs("src")
    add_tests("default", {
        trim_output = true,
        runenvs = {QT_QPA_PLATFORM = "offscreen"}
    })

target("finepaper-inspector-workbench-tests")
    add_rules("qt.widgetapp")
    set_kind("binary")
    set_group("test")
    set_default(false)
    add_files("tests/inspector_workbench_tests.cpp")
    add_files("src/ui/workbench/inspector_design_settings.cpp")
    add_files("src/ui/workbench/inspector_summary_panel.cpp")
    add_files("src/ui/theme/ui_tokens.cpp")
    add_includedirs("src")
    add_tests("default", {
        trim_output = true,
        runenvs = {QT_QPA_PLATFORM = "offscreen"}
    })
