add_rules("mode.debug", "mode.release")

includes("deps/packages.lua")
add_requires("nodeeditor")

target("qt")
    add_rules("qt.widgetapp")

    add_packages("nodeeditor")
    add_headerfiles("inc/*.h")
    add_headerfiles("inc/commands/*.h")
    add_files("src/*.cpp")
    add_files("src/commands/*.cpp")
    add_files("src/ui/mainwindow.ui")
    -- add files with Q_OBJECT meta (only for qt.moc)
    add_files("inc/mainwindow.h")
    add_files("inc/graph.h")
    add_files("inc/module.h")
    add_files("inc/logpanel.h")
    add_files("inc/nodeeditorwidget.h")
    add_files("inc/graphnodemodel.h")
    add_files("inc/propertypanel.h")
    add_files("inc/palette.h")

    add_includedirs("inc")

