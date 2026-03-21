cmake_minimum_required(VERSION 3.16)
project(kdeepseek VERSION 1.0)

find_package(ECM REQUIRED NO_MODULE)
set(CMAKE_MODULE_PATH ${ECM_MODULE_PATH})

include(KDEInstallDirs)
include(KDECMakeSettings)

# Find Qt6
find_package(Qt6 REQUIRED COMPONENTS Widgets Network)

# Find KF6 components
set(KF6_DEP_VERSION "6.0")
find_package(KF6 ${KF6_DEP_VERSION} REQUIRED COMPONENTS
    CoreAddons
    I18n
    TextEditor
    Config
)

# Define the plugin
kcoreaddons_add_plugin(kdeepseek
    INSTALL_NAMESPACE "kf6/ktexteditor"
)

# Force build outputs to stay in the build directory
set_target_properties(kdeepseek PROPERTIES
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
)

include_directories(
    include
)

target_sources(kdeepseek
    PRIVATE
    src/plugin.cpp
    src/networkmanager.cpp
    src/deepseekconfigpage.cpp
)

target_compile_definitions(kdeepseek PRIVATE TRANSLATION_DOMAIN="kdeepseek")

target_link_libraries(kdeepseek
    PRIVATE
    KF6::CoreAddons
    KF6::I18n
    KF6::TextEditor
    KF6::ConfigCore
    Qt6::Widgets
    Qt6::Network
)

# Install the plugin (this step is handled manually by your build script)
install(TARGETS kdeepseek DESTINATION ${KDE_INSTALL_PLUGINDIR})
