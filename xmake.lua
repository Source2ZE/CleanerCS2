set_runtimes("MT")
add_rules("mode.debug", "mode.release")
add_requires("re2")

includes("@builtin/xpack")

local SDK_PATH = os.getenv("HL2SDKCS2")
local MM_PATH = os.getenv("MMSOURCE112")

target("CleanerCS2-Xmake")
    set_kind("shared")
    add_files("src/*.cpp")
    add_packages("re2")
    set_symbols("hidden")

    add_files({
        SDK_PATH.."/tier1/convar.cpp",
        SDK_PATH.."/public/tier0/memoverride.cpp",
    })

    if is_plat("windows") then
        add_links({
            SDK_PATH.."/lib/public/win64/tier0.lib",
            SDK_PATH.."/lib/public/win64/tier1.lib",
            SDK_PATH.."/lib/public/win64/interfaces.lib",
            SDK_PATH.."/lib/public/win64/mathlib.lib",
        })
    else
        add_links({
            SDK_PATH.."/lib/linux64/libtier0.so",
            SDK_PATH.."/lib/linux64/tier1.a",
            SDK_PATH.."/lib/linux64/interfaces.a",
            SDK_PATH.."/lib/linux64/mathlib.a",
        })
    end

    add_linkdirs({
        "vendor/funchook/lib/Release",
    })

    add_links({
        "funchook",
        "distorm",
    })

    if is_plat("windows") then
        add_links("psapi");
        add_files("src/utils/plat_win.cpp");
    else
        add_files("src/utils/plat_unix.cpp");
    end

    add_includedirs({
        "src",
        "vendor/funchook/include",
        -- sdk
        SDK_PATH,
        SDK_PATH.."/thirdparty/protobuf-3.21.8/src",
        SDK_PATH.."/common",
        SDK_PATH.."/game/shared",
        SDK_PATH.."/game/server",
        SDK_PATH.."/public",
        SDK_PATH.."/public/engine",
        SDK_PATH.."/public/mathlib",
        SDK_PATH.."/public/tier0",
        SDK_PATH.."/public/tier1",
        SDK_PATH.."/public/entity2",
        SDK_PATH.."/public/game/server",
        -- metamod
        MM_PATH.."/core",
        MM_PATH.."/core/sourcehook",
    })

    if(is_plat("windows")) then
        add_defines({
            "COMPILER_MSVC",
            "COMPILER_MSVC64",
            "PLATFORM_64BITS",
            "WIN32",
            "WINDOWS",
            "CRT_SECURE_NO_WARNINGS",
            "CRT_SECURE_NO_DEPRECATE",
            "CRT_NONSTDC_NO_DEPRECATE",
            "_MBCS",
            "META_IS_SOURCE2"
        })
    else
        add_defines({
            "_LINUX",
            "LINUX",
            "POSIX",
            "GNUC",
            "COMPILER_GCC",
            "PLATFORM_64BITS",
            "META_IS_SOURCE2",
            "_GLIBCXX_USE_CXX11_ABI=1",
            "stricmp=strcasecmp",
            "_stricmp=strcasecmp",
            "_snprintf=snprintf",
            "_vsnprintf=vsnprintf"
        })
    end
    set_languages("cxx17")

xpack("CleanerCS2")
    set_formats("zip")
    add_installfiles("build/windows/x64/release/CleanerCS2-Xmake.dll", {filename = "cleanercs2.dll", prefixdir = "cleanercs2"})
    add_installfiles("build/linux/x86_64/release/libCleanerCS2-Xmake.so", {filename = "cleanercs2.so", prefixdir = "cleanercs2"})
    add_installfiles("package/config.cfg", {prefixdir = "cleanercs2"})
    add_installfiles("package/cleanercs2.vdf", {prefixdir = "metamod"})