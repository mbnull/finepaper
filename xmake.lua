set_project("finepaper")
set_version("0.1.0")
set_languages("c++23")

add_rules("mode.debug", "mode.release")

target("finepaper-application")
    add_rules("qt.static")
    set_kind("static")
    add_headerfiles("src/**.h")
    add_files("src/noc/*.cpp")
    add_files("src/package/*.cpp")
    add_files("src/storage/*.cpp")
    add_files("src/execution/*.cpp")
    add_files("src/application/*.cpp")
    add_includedirs("src", {public = true})

target("finepaper")
    add_rules("qt.console")
    set_kind("binary")
    add_deps("finepaper-application")
    add_files("src/cli/main.cpp")
    add_includedirs("src")

target("finepaper-gui")
    add_rules("qt.widgetapp")
    set_kind("binary")
    add_deps("finepaper-application")
    add_files("src/gui/*.cpp")
    add_includedirs("src")

target("finepaper-tests")
    add_rules("qt.console")
    set_kind("binary")
    set_group("test")
    set_default(false)
    add_deps("finepaper-application")
    add_files("tests/*.cpp")
    add_includedirs("src")
    add_defines('FINEPAPER_SOURCE_DIR="' .. path.unix(os.projectdir()) .. '"')
    add_tests("default", {
        trim_output = true,
        pass_outputs = "finepaper-tests passed"
    })
