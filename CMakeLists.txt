cmake_minimum_required(VERSION 3.21)

project(HubSightAdminSdk
    VERSION 0.2.0
    DESCRIPTION "HubSight Admin SDK for Qt/C++"
    LANGUAGES CXX
)

# Keep a compilation database available for clangd-based editors such as Zed.
# This is local/tooling metadata and does not change the SDK build outputs.
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

include(GNUInstallDirs)
include(CTest)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)

option(HUBSIGHT_ADMIN_BUILD_TESTS "Build HubSight Admin SDK tests" ${BUILD_TESTING})
option(HUBSIGHT_ADMIN_BUILD_INTEGRATION_TESTS
       "Build opt-in tests against a configured HubSight Admin backend" OFF)
option(HUBSIGHT_ADMIN_ENABLE_HSCFG_IMPORT
       "Enable native .hscfg Admin importer when dependencies are available" ON)
option(HUBSIGHT_ADMIN_ENABLE_DESKTOP_SECURE_STORAGE
       "Enable Windows/macOS/Linux desktop credential-vault integration" ON)

find_package(Qt6 6.6 REQUIRED COMPONENTS Core Network WebSockets)

set(HUBSIGHT_ADMIN_MACOS_KEYCHAIN_AVAILABLE OFF)
set(HUBSIGHT_ADMIN_LIBSECRET_AVAILABLE OFF)
set(HUBSIGHT_COREFOUNDATION_FRAMEWORK "")
if(HUBSIGHT_ADMIN_ENABLE_DESKTOP_SECURE_STORAGE)
    if(CMAKE_SYSTEM_NAME STREQUAL "Darwin")
        find_library(HUBSIGHT_SECURITY_FRAMEWORK Security)
        find_library(HUBSIGHT_COREFOUNDATION_FRAMEWORK CoreFoundation)
        if(HUBSIGHT_SECURITY_FRAMEWORK AND HUBSIGHT_COREFOUNDATION_FRAMEWORK)
            set(HUBSIGHT_ADMIN_MACOS_KEYCHAIN_AVAILABLE ON)
        endif()
    elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
        find_package(PkgConfig QUIET)
        if(PkgConfig_FOUND)
            pkg_check_modules(HUBSIGHT_LIBSECRET QUIET IMPORTED_TARGET libsecret-1)
            if(HUBSIGHT_LIBSECRET_FOUND)
                set(HUBSIGHT_ADMIN_LIBSECRET_AVAILABLE ON)
            endif()
        endif()
    endif()
endif()

set(HUBSIGHT_ADMIN_HSCFG_AVAILABLE OFF)
if(HUBSIGHT_ADMIN_ENABLE_HSCFG_IMPORT)
    find_package(OpenSSL QUIET COMPONENTS Crypto)
    find_package(PkgConfig QUIET)
    if(OpenSSL_FOUND AND PkgConfig_FOUND)
        pkg_check_modules(HUBSIGHT_ARGON2 QUIET IMPORTED_TARGET libargon2)
        pkg_check_modules(HUBSIGHT_LIBZIP QUIET IMPORTED_TARGET libzip)
        pkg_check_modules(HUBSIGHT_LIBYAML QUIET IMPORTED_TARGET yaml-0.1)
        if(HUBSIGHT_ARGON2_FOUND AND HUBSIGHT_LIBZIP_FOUND AND
           HUBSIGHT_LIBYAML_FOUND)
            set(HUBSIGHT_ADMIN_HSCFG_AVAILABLE ON)
        endif()
    endif()
    if(NOT HUBSIGHT_ADMIN_HSCFG_AVAILABLE)
        message(WARNING
            "HSCFG importer disabled: install OpenSSL Crypto, libargon2, "
            "libzip, and libyaml, or set HUBSIGHT_ADMIN_ENABLE_HSCFG_IMPORT=OFF.")
    endif()
endif()

set(HUBSIGHT_PREFERENCES_PUBLIC_HEADERS
    include/hubsight/preferences.h
    include/hubsight/preferences/preferences_export.h
    include/hubsight/preferences/preference_store.h
)

set(HUBSIGHT_PREFERENCES_SOURCES
    src/preferences/preference_store.cpp
)

set(HUBSIGHT_ADMIN_PUBLIC_HEADERS
    include/hubsight/admin/admin_application_client.h
    include/hubsight/admin/admin_client.h
    include/hubsight/admin/admin_endpoint_catalog.h
    include/hubsight/admin/admin_endpoint_client.h
    include/hubsight/admin/admin_export.h
    include/hubsight/admin/admin_types.h
    include/hubsight/admin/auth_manager.h
    include/hubsight/admin/hubsight_admin.h
    include/hubsight/admin/secure_storage.h
    include/hubsight/admin/desktop_secure_storage.h
    include/hubsight/admin/sdk_diagnostics.h
    include/hubsight/admin/config/hscfg_types.h
    include/hubsight/admin/config/hscfg_importer.h
    include/hubsight/admin/resources/account_types.h
    include/hubsight/admin/resources/account_client.h
    include/hubsight/admin/resources/camera_client.h
    include/hubsight/admin/resources/camera_management_types.h
    include/hubsight/admin/resources/camera_management_client.h
    include/hubsight/admin/resources/identity_types.h
    include/hubsight/admin/resources/identity_client.h
    include/hubsight/admin/resources/integration_types.h
    include/hubsight/admin/resources/integration_client.h
    include/hubsight/admin/resources/member_types.h
    include/hubsight/admin/resources/member_client.h
    include/hubsight/admin/resources/system_client.h
    include/hubsight/admin/resources/system_operations_types.h
    include/hubsight/admin/resources/system_operations_client.h
    include/hubsight/admin/resources/live_types.h
    include/hubsight/admin/resources/live_client.h
    include/hubsight/admin/resources/archive_types.h
    include/hubsight/admin/resources/archive_client.h
    include/hubsight/admin/resources/notification_types.h
    include/hubsight/admin/resources/notification_client.h
    include/hubsight/admin/realtime/socket_io_client.h
    include/hubsight/admin/realtime/socket_io_realtime_client.h
    include/hubsight/admin/realtime/relay_client.h
    include/hubsight/admin/realtime/relay_realtime_client.h
    include/hubsight/admin/webrtc/webrtc_types.h
    include/hubsight/admin/webrtc/webrtc_peer_connection.h
    include/hubsight/admin/webrtc/webrtc_client.h
)

set(HUBSIGHT_ADMIN_SOURCES
    src/admin_application_client.cpp
    src/admin_client.cpp
    src/admin_endpoint_catalog.cpp
    src/admin_endpoint_client.cpp
    src/admin_transport.cpp
    src/admin_transport.h
    src/admin_types.cpp
    src/auth_manager.cpp
    src/resource_helpers.h
    src/secure_storage.cpp
    src/desktop_secure_storage.cpp
    src/sdk_diagnostics.cpp
    src/config/hscfg_types.cpp
    src/config/hscfg_importer.cpp
    src/resources/account_types.cpp
    src/resources/account_client.cpp
    src/resources/camera_client.cpp
    src/resources/camera_management_types.cpp
    src/resources/camera_management_client.cpp
    src/resources/identity_types.cpp
    src/resources/identity_client.cpp
    src/resources/integration_types.cpp
    src/resources/integration_client.cpp
    src/resources/member_types.cpp
    src/resources/member_client.cpp
    src/resources/system_client.cpp
    src/resources/system_operations_types.cpp
    src/resources/system_operations_client.cpp
    src/resources/live_types.cpp
    src/resources/live_client.cpp
    src/resources/archive_types.cpp
    src/resources/archive_client.cpp
    src/resources/notification_types.cpp
    src/resources/notification_client.cpp
    src/realtime/socket_io_client.cpp
    src/realtime/socket_io_realtime_client.cpp
    src/realtime/relay_client.cpp
    src/realtime/relay_realtime_client.cpp
    src/webrtc/webrtc_types.cpp
    src/webrtc/webrtc_peer_connection.cpp
    src/webrtc/webrtc_client.cpp
)

add_library(hubsight-preferences SHARED
    ${HUBSIGHT_PREFERENCES_PUBLIC_HEADERS}
    ${HUBSIGHT_PREFERENCES_SOURCES}
)

add_library(HubSight::Preferences ALIAS hubsight-preferences)

target_compile_features(hubsight-preferences PUBLIC cxx_std_20)
target_compile_definitions(hubsight-preferences PRIVATE HUBSIGHT_PREFERENCES_LIBRARY)
target_link_libraries(hubsight-preferences PUBLIC Qt6::Core)
target_include_directories(hubsight-preferences
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)
set_target_properties(hubsight-preferences PROPERTIES
    VERSION ${PROJECT_VERSION}
    SOVERSION ${PROJECT_VERSION_MAJOR}
    EXPORT_NAME Preferences
)

add_library(hubsight-admin-sdk SHARED
    ${HUBSIGHT_ADMIN_PUBLIC_HEADERS}
    ${HUBSIGHT_ADMIN_SOURCES}
)

add_library(HubSight::AdminSdk ALIAS hubsight-admin-sdk)

target_compile_features(hubsight-admin-sdk PUBLIC cxx_std_20)
target_compile_definitions(hubsight-admin-sdk PRIVATE HUBSIGHT_ADMIN_LIBRARY)
if(HUBSIGHT_ADMIN_HSCFG_AVAILABLE)
    target_compile_definitions(hubsight-admin-sdk
        PRIVATE HUBSIGHT_ADMIN_HSCFG_AVAILABLE)
endif()
target_link_libraries(hubsight-admin-sdk PUBLIC
    Qt6::Core
    Qt6::Network
    Qt6::WebSockets
)
if(HUBSIGHT_ADMIN_HSCFG_AVAILABLE)
    target_link_libraries(hubsight-admin-sdk PRIVATE
        OpenSSL::Crypto
        PkgConfig::HUBSIGHT_ARGON2
        PkgConfig::HUBSIGHT_LIBZIP
        PkgConfig::HUBSIGHT_LIBYAML
    )
endif()
if(WIN32 AND HUBSIGHT_ADMIN_ENABLE_DESKTOP_SECURE_STORAGE)
    target_link_libraries(hubsight-admin-sdk PRIVATE Advapi32)
    target_compile_definitions(hubsight-admin-sdk
        PRIVATE HUBSIGHT_ADMIN_WINDOWS_CREDENTIALS_AVAILABLE)
endif()
if(HUBSIGHT_ADMIN_MACOS_KEYCHAIN_AVAILABLE)
    target_link_libraries(hubsight-admin-sdk PRIVATE
        ${HUBSIGHT_SECURITY_FRAMEWORK}
        ${HUBSIGHT_COREFOUNDATION_FRAMEWORK}
    )
    target_compile_definitions(hubsight-admin-sdk
        PRIVATE HUBSIGHT_ADMIN_MACOS_KEYCHAIN_AVAILABLE)
endif()
if(HUBSIGHT_ADMIN_LIBSECRET_AVAILABLE)
    target_link_libraries(hubsight-admin-sdk PRIVATE PkgConfig::HUBSIGHT_LIBSECRET)
    target_compile_definitions(hubsight-admin-sdk
        PRIVATE HUBSIGHT_ADMIN_LIBSECRET_AVAILABLE)
endif()
target_include_directories(hubsight-admin-sdk
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)

set_target_properties(hubsight-admin-sdk PROPERTIES
    VERSION ${PROJECT_VERSION}
    SOVERSION ${PROJECT_VERSION_MAJOR}
    EXPORT_NAME AdminSdk
)

install(TARGETS hubsight-preferences
    EXPORT HubSightPreferencesTargets
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)
install(TARGETS hubsight-admin-sdk
    EXPORT HubSightAdminSdkTargets
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)
install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})

include(CMakePackageConfigHelpers)
configure_package_config_file(
    cmake/HubSightAdminSdkConfig.cmake.in
    ${CMAKE_CURRENT_BINARY_DIR}/HubSightAdminSdkConfig.cmake
    INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/HubSightAdminSdk
)
write_basic_package_version_file(
    ${CMAKE_CURRENT_BINARY_DIR}/HubSightAdminSdkConfigVersion.cmake
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY SameMajorVersion
)
install(FILES
    ${CMAKE_CURRENT_BINARY_DIR}/HubSightAdminSdkConfig.cmake
    ${CMAKE_CURRENT_BINARY_DIR}/HubSightAdminSdkConfigVersion.cmake
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/HubSightAdminSdk
)

configure_package_config_file(
    cmake/HubSightPreferencesConfig.cmake.in
    ${CMAKE_CURRENT_BINARY_DIR}/HubSightPreferencesConfig.cmake
    INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/HubSightPreferences
)
write_basic_package_version_file(
    ${CMAKE_CURRENT_BINARY_DIR}/HubSightPreferencesConfigVersion.cmake
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY SameMajorVersion
)
install(FILES
    ${CMAKE_CURRENT_BINARY_DIR}/HubSightPreferencesConfig.cmake
    ${CMAKE_CURRENT_BINARY_DIR}/HubSightPreferencesConfigVersion.cmake
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/HubSightPreferences
)
install(EXPORT HubSightAdminSdkTargets
    FILE HubSightAdminSdkTargets.cmake
    NAMESPACE HubSight::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/HubSightAdminSdk
)
install(EXPORT HubSightPreferencesTargets
    FILE HubSightPreferencesTargets.cmake
    NAMESPACE HubSight::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/HubSightPreferences
)

if(HUBSIGHT_ADMIN_BUILD_TESTS OR HUBSIGHT_ADMIN_BUILD_INTEGRATION_TESTS)
    find_package(Qt6 6.6 REQUIRED COMPONENTS Test)
endif()

if(HUBSIGHT_ADMIN_BUILD_TESTS)
    add_executable(hubsight-admin-sdk-tests
        tests/test_admin_sdk.cpp
    )
    target_link_libraries(hubsight-admin-sdk-tests PRIVATE
        HubSight::AdminSdk
        Qt6::Test
        Qt6::Network
        Qt6::WebSockets
    )
    add_test(NAME hubsight-admin-sdk-tests COMMAND hubsight-admin-sdk-tests)

    add_executable(hubsight-preferences-tests
        tests/preferences/test_preference_store.cpp
    )
    target_link_libraries(hubsight-preferences-tests PRIVATE
        HubSight::Preferences
        Qt6::Test
    )
    add_test(NAME hubsight-preferences-tests
             COMMAND hubsight-preferences-tests)
endif()

if(HUBSIGHT_ADMIN_BUILD_INTEGRATION_TESTS)
    add_executable(hubsight-admin-sdk-integration-tests
        tests/integration/test_admin_backend.cpp
    )
    target_link_libraries(hubsight-admin-sdk-integration-tests PRIVATE
        HubSight::AdminSdk
        Qt6::Test
        Qt6::Network
        Qt6::WebSockets
    )
    add_test(NAME hubsight-admin-sdk-integration-tests
             COMMAND hubsight-admin-sdk-integration-tests)
endif()
