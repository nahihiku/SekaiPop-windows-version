cmake_minimum_required(VERSION 3.21)
project(SekaiPopShell VERSION 1.0.0 LANGUAGES CXX)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

option(SEKAIPOP_CONSOLE "Show debug console window alongside GUI on Windows" OFF)

if(NOT WIN32)
    message(FATAL_ERROR "SekaiPopShell Windows host only targets Windows (Win32 + Qt 6).")
endif()

find_package(Qt6 REQUIRED COMPONENTS
    Core
    Gui
    Network
    Quick
    Qml
    QuickControls2
    Svg
    Widgets
)
find_package(Qt6 QUIET COMPONENTS ShaderTools)

qt_standard_project_setup(REQUIRES 6.5)

set(SOURCES
    src/main.cpp
    src/quickshell/QsTypes.h
    src/quickshell/QsTypes.cpp
    src/quickshell/QsWindowing.h
    src/quickshell/QsWindowing.cpp
    src/quickshell/QsIo.h
    src/quickshell/QsIo.cpp
    src/quickshell/QsServices.h
    src/quickshell/QsServices.cpp
    src/quickshell/QsEngineSetup.h
    src/quickshell/QsEngineSetup.cpp
    src/win32/Win32Utils.h
    src/win32/Win32Utils.cpp
    src/win32/WinIconProvider.h
    src/win32/WinIconProvider.cpp
    src/win32/WallpaperHost.h
    src/win32/WallpaperHost.cpp
    src/win32/AppBarManager.h
    src/win32/AppBarManager.cpp
    src/win32/WindowTracker.h
    src/win32/WindowTracker.cpp
    src/win32/HotkeyManager.h
    src/win32/HotkeyManager.cpp
    src/win32/ScreenshotOverlay.h
    src/win32/ScreenshotOverlay.cpp
    src/services/SettingsService.h
    src/services/SettingsService.cpp
    src/services/ThemeService.h
    src/services/ThemeService.cpp
    src/services/MatugenEngine.h
    src/services/MatugenEngine.cpp
    src/services/WallpaperService.h
    src/services/WallpaperService.cpp
    src/services/VideoWallpaperItem.h
    src/services/VideoWallpaperItem.cpp
    src/services/BarService.h
    src/services/BarService.cpp
    src/services/ShellScale.h
    src/services/ShellScale.cpp
    src/services/AppSource.h
    src/services/AppSource.cpp
    src/services/AudioSpectrumSource.h
    src/services/AudioSpectrumSource.cpp
    src/services/MediaSource.h
    src/services/MediaSource.cpp
    src/services/NotificationSource.h
    src/services/NotificationSource.cpp
    src/services/OsdSource.h
    src/services/OsdSource.cpp
    src/services/PinnedAppsService.h
    src/services/PinnedAppsService.cpp
    src/services/ResourceSource.h
    src/services/ResourceSource.cpp
    src/services/BatterySource.h
    src/services/BatterySource.cpp
    src/services/QuickSource.h
    src/services/QuickSource.cpp
    src/services/StatusIconsService.h
    src/services/StatusIconsService.cpp
    src/services/ShellController.h
    src/services/ShellController.cpp
    src/services/TodoService.h
    src/services/TodoService.cpp
    src/services/WeatherSource.h
    src/services/WeatherSource.cpp
    resources/SekaiPopShell.rc
)

if(SEKAIPOP_CONSOLE)
    add_executable(SekaiPopShell ${SOURCES})
else()
    add_executable(SekaiPopShell WIN32 ${SOURCES})
endif()

target_include_directories(SekaiPopShell PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_compile_definitions(SekaiPopShell PRIVATE
    UNICODE
    _UNICODE
    WIN32_LEAN_AND_MEAN
    NOMINMAX
    QT_NO_KEYWORDS
)

target_link_libraries(SekaiPopShell PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::GuiPrivate
    Qt6::Network
    Qt6::Quick
    Qt6::QuickPrivate
    Qt6::Qml
    Qt6::QuickControls2
    Qt6::Svg
    Qt6::Widgets
    user32
    shell32
    ole32
    oleaut32
    shlwapi
    dwmapi
    wlanapi
    iphlpapi
    powrprof
    gdi32
    dxva2
    bthprops
    uuid
    version
    winmm
    runtimeobject
    mfplat
    mfreadwrite
    mfuuid
    d3d11
)

# Compile all SekaiPop shaders with Qt6::ShaderTools so Direct3D 11 / Vulkan / OpenGL
# shader blobs match the active Qt 6 version.
set(SHADER_STEMS
    fade
    sekai_ring
    sekai_iris
    sekai_star
    sekai_blocks
    sekai_flash
    fade_lock
    sekai_ring_lock
    sekai_iris_lock
    sekai_star_lock
    sekai_blocks_lock
    sekai_flash_lock
    welcome_warp
)

set(FRAG_ONLY_STEMS
    welcome_dots
    welcome_halo
)

set(VIZ_SHADER_STEMS
    tide
    ribbons
    pulse
    scope
)

set(VIZ_PREBUILT_QSB
    aurora.frag.qsb
    dots.frag.qsb
    mirror.frag.qsb
    ring.frag.qsb
)

set(SHADER_OUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/shaders")
file(MAKE_DIRECTORY "${SHADER_OUT_DIR}/visualizer")

if(TARGET Qt6::qsb)
    set(GENERATED_QSB_FILES "")
    foreach(stem IN LISTS SHADER_STEMS)
        set(vert_in "${CMAKE_CURRENT_SOURCE_DIR}/sekaipop/shaders/${stem}.vert")
        set(frag_in "${CMAKE_CURRENT_SOURCE_DIR}/sekaipop/shaders/${stem}.frag")
        set(vert_out "${SHADER_OUT_DIR}/${stem}.vert.qsb")
        set(frag_out "${SHADER_OUT_DIR}/${stem}.frag.qsb")
        add_custom_command(
            OUTPUT "${vert_out}"
            COMMAND Qt6::qsb --qt6 -o "${vert_out}" "${vert_in}"
            DEPENDS "${vert_in}"
            COMMENT "Baking shader ${stem}.vert.qsb"
            VERBATIM
        )
        add_custom_command(
            OUTPUT "${frag_out}"
            COMMAND Qt6::qsb --qt6 -o "${frag_out}" "${frag_in}"
            DEPENDS "${frag_in}"
            COMMENT "Baking shader ${stem}.frag.qsb"
            VERBATIM
        )
        list(APPEND GENERATED_QSB_FILES "${vert_out}" "${frag_out}")
    endforeach()
    foreach(fstem IN LISTS FRAG_ONLY_STEMS)
        set(frag_in "${CMAKE_CURRENT_SOURCE_DIR}/sekaipop/shaders/${fstem}.frag")
        set(frag_out "${SHADER_OUT_DIR}/${fstem}.frag.qsb")
        add_custom_command(
            OUTPUT "${frag_out}"
            COMMAND Qt6::qsb --qt6 -o "${frag_out}" "${frag_in}"
            DEPENDS "${frag_in}"
            COMMENT "Baking shader ${fstem}.frag.qsb"
            VERBATIM
        )
        list(APPEND GENERATED_QSB_FILES "${frag_out}")
    endforeach()
    foreach(vstem IN LISTS VIZ_SHADER_STEMS)
        set(vfrag_in "${CMAKE_CURRENT_SOURCE_DIR}/sekaipop/shaders/visualizer/${vstem}.frag")
        set(vfrag_out "${SHADER_OUT_DIR}/visualizer/${vstem}.frag.qsb")
        add_custom_command(
            OUTPUT "${vfrag_out}"
            COMMAND Qt6::qsb --glsl "100 es,120,150,300 es" --hlsl 50 --msl 12 -o "${vfrag_out}" "${vfrag_in}"
            DEPENDS "${vfrag_in}"
            COMMENT "Baking visualizer shader ${vstem}.frag.qsb"
            VERBATIM
        )
        list(APPEND GENERATED_QSB_FILES "${vfrag_out}")
    endforeach()
    add_custom_target(SekaiPopShaders ALL DEPENDS ${GENERATED_QSB_FILES})
    add_dependencies(SekaiPopShell SekaiPopShaders)
endif()

foreach(vqsb IN LISTS VIZ_PREBUILT_QSB)
    add_custom_command(TARGET SekaiPopShell POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_CURRENT_SOURCE_DIR}/sekaipop/shaders/visualizer/${vqsb}"
            "${SHADER_OUT_DIR}/visualizer/${vqsb}"
    )
endforeach()

# Stage runtime QML tree and visual assets once (stamp-guarded so incremental builds stay fast).
set(ASSETS_STAMP "${CMAKE_CURRENT_BINARY_DIR}/.assets_staged_v1")
add_custom_command(
    OUTPUT "${ASSETS_STAMP}"
    COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:SekaiPopShell>/qml"
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_CURRENT_SOURCE_DIR}/qml"
        "$<TARGET_FILE_DIR:SekaiPopShell>/qml"
    COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:SekaiPopShell>/assets/icons"
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_CURRENT_SOURCE_DIR}/sekaipop/assets/icons"
        "$<TARGET_FILE_DIR:SekaiPopShell>/assets/icons"
    COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:SekaiPopShell>/assets/images"
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_CURRENT_SOURCE_DIR}/sekaipop/assets/images"
        "$<TARGET_FILE_DIR:SekaiPopShell>/assets/images"
    COMMAND ${CMAKE_COMMAND} -E touch "${ASSETS_STAMP}"
    COMMENT "Staging SekaiPop runtime assets (one-time)"
)
add_custom_target(SekaiPopRuntimeAssets ALL DEPENDS "${ASSETS_STAMP}")
add_dependencies(SekaiPopShell SekaiPopRuntimeAssets)

# Copy libc++.dll / libunwind.dll from Qt's bin dir when building with LLVM-MinGW.
get_target_property(QT_QMAKE_BIN Qt6::qmake IMPORTED_LOCATION)
if(QT_QMAKE_BIN)
    get_filename_component(QT_BIN_DIR "${QT_QMAKE_BIN}" DIRECTORY)
    foreach(rt_dll IN ITEMS libc++.dll libunwind.dll)
        if(EXISTS "${QT_BIN_DIR}/${rt_dll}")
            add_custom_command(TARGET SekaiPopShell POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    "${QT_BIN_DIR}/${rt_dll}"
                    "$<TARGET_FILE_DIR:SekaiPopShell>/${rt_dll}"
            )
        endif()
    endforeach()
endif()

if(TARGET Qt6::qsb)
    add_custom_command(TARGET SekaiPopShell POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:SekaiPopShell>/shaders"
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${SHADER_OUT_DIR}"
            "$<TARGET_FILE_DIR:SekaiPopShell>/shaders"
    )
endif()

if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/ukikura-lyrics/index.js")
    add_custom_command(TARGET SekaiPopShell POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:SekaiPopShell>/ukikura-lyrics"
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${CMAKE_CURRENT_SOURCE_DIR}/ukikura-lyrics"
            "$<TARGET_FILE_DIR:SekaiPopShell>/ukikura-lyrics"
    )
endif()

# ============================================================================
# Native Zero-Dependency Windows Shell Host, Control Center, and Setup Stub
# ============================================================================
add_executable(SekaiPopBoot WIN32
    src/boot/SekaiPopBoot.cpp
    resources/SekaiPopBoot.rc
)
target_link_libraries(SekaiPopBoot PRIVATE
    user32 shell32 ole32 advapi32 shlwapi uuid
)
if(MINGW)
    target_link_options(SekaiPopBoot PRIVATE -static -municode)
endif()

add_executable(SekaiPopManager WIN32
    src/installer/SekaiPopManager.cpp
    resources/SekaiPopManager.rc
)
target_include_directories(SekaiPopManager PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src/installer
)
target_link_libraries(SekaiPopManager PRIVATE
    user32 gdi32 shell32 ole32 advapi32 comctl32 shlwapi uuid
)
if(MINGW)
    target_link_options(SekaiPopManager PRIVATE -static -municode)
endif()

add_executable(SekaiPopSetupStub WIN32
    src/installer/SekaiPopSetup.cpp
    resources/SekaiPopSetup.rc
)
target_include_directories(SekaiPopSetupStub PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src/installer
)
target_link_libraries(SekaiPopSetupStub PRIVATE
    user32 gdi32 shell32 ole32 advapi32 comctl32 setupapi shlwapi uuid
)
if(MINGW)
    target_link_options(SekaiPopSetupStub PRIVATE -static -municode)
endif()
