add_rules("mode.debug", "mode.release")

includes("deps/packages.lua")
add_requires("nodeeditor")

target("qt")
    add_rules("qt.widgetapp")

    add_packages("nodeeditor")
    add_headerfiles("inc/*.h")
    add_files("src/*.cpp")
    add_files("src/ui/mainwindow.ui")
    -- add files with Q_OBJECT meta (only for qt.moc)
    add_files("inc/mainwindow.h")

    add_includedirs("inc")

