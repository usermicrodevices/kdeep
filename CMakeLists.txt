cmake_minimum_required(VERSION 3.16)
project(kdeep VERSION 1.0)

find_package(ECM REQUIRED NO_MODULE)
set(CMAKE_MODULE_PATH ${ECM_MODULE_PATH})

include(KDEInstallDirs)
include(KDECMakeSettings)

find_package(Qt6 REQUIRED COMPONENTS Widgets Network)

set(KF6_DEP_VERSION "6.0")
find_package(KF6 ${KF6_DEP_VERSION} REQUIRED COMPONENTS
    CoreAddons
    I18n
    TextEditor
    Config
)

set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib")

kcoreaddons_add_plugin(kdeep
    INSTALL_NAMESPACE "kf6/ktexteditor"
)

set_target_properties(kdeep PROPERTIES
    KPLUGIN_JSON "${CMAKE_CURRENT_SOURCE_DIR}/src/kdeep.json"
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
)

add_library(picolm STATIC
    thirdparty/picolm/picolm/model.c
    thirdparty/picolm/picolm/tensor.c
    thirdparty/picolm/picolm/quant.c
    thirdparty/picolm/picolm/tokenizer.c
    thirdparty/picolm/picolm/sampler.c
    thirdparty/picolm/picolm/grammar.c
)

target_include_directories(picolm PUBLIC thirdparty/picolm/picolm)

target_compile_options(picolm PRIVATE
    $<$<COMPILE_LANGUAGE:C>:-std=c11>
    $<$<COMPILE_LANGUAGE:C>:-fPIC>
    -O2
    -pthread
)

target_compile_definitions(picolm PRIVATE
    $<$<COMPILE_LANGUAGE:C>:_GNU_SOURCE>
)

target_link_libraries(picolm PRIVATE m pthread)

include_directories(
    include
    thirdparty/picolm/picolm
)

target_sources(kdeep
    PRIVATE
    src/plugin.cpp
    src/networkmanager.cpp
    src/kdeepconfig.cpp
    src/opencodemanager.cpp
    include/kdeepconfig.hpp
    include/networkmanager.hpp
    include/opencodemanager.hpp
    include/plugin.hpp
)

target_compile_definitions(kdeep PRIVATE TRANSLATION_DOMAIN="kdeep")

target_link_libraries(kdeep
    PRIVATE
    picolm
    KF6::CoreAddons
    KF6::I18n
    KF6::TextEditor
    KF6::ConfigCore
    Qt6::Widgets
    Qt6::Network
)

install(TARGETS kdeep DESTINATION ${KDE_INSTALL_PLUGINDIR})
