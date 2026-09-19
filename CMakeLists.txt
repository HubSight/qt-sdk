cmake_minimum_required(VERSION 3.21)

project(HubSightAdminSdk
    VERSION 0.1.0
    DESCRIPTION "HubSight Admin SDK for Qt/C++"
    LANGUAGES CXX
)

include(GNUInstallDirs)
include(CTest)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)

option(HUBSIGHT_ADMIN_BUILD_TESTS "Build HubSight Admin SDK tests" ${BUILD_TESTING})

find_package(Qt6 6.6 REQUIRED COMPONENTS Core Network WebSockets)

set(HUBSIGHT_ADMIN_PUBLIC_HEADERS
    include/hubsight/admin/admin_client.h
    include/hubsight/admin/admin_endpoint_catalog.h
    include/hubsight/admin/admin_endpoint_client.h
    include/hubsight/admin/admin_export.h
    include/hubsight/admin/admin_types.h
    include/hubsight/admin/auth_manager.h
    include/hubsight/admin/hubsight_admin.h
    include/hubsight/admin/secure_storage.h
    include/hubsight/admin/resources/camera_client.h
    include/hubsight/admin/resources/system_client.h
    include/hubsight/admin/realtime/socket_io_client.h
    include/hubsight/admin/webrtc/webrtc_types.h
    include/hubsight/admin/webrtc/webrtc_peer_connection.h
    include/hubsight/admin/webrtc/webrtc_client.h
)

set(HUBSIGHT_ADMIN_SOURCES
    src/admin_client.cpp
    src/admin_endpoint_catalog.cpp
    src/admin_endpoint_client.cpp
    src/admin_transport.cpp
    src/admin_transport.h
    src/admin_types.cpp
    src/auth_manager.cpp
    src/resource_helpers.h
    src/secure_storage.cpp
    src/resources/camera_client.cpp
    src/resources/system_client.cpp
    src/realtime/socket_io_client.cpp
    src/webrtc/webrtc_types.cpp
    src/webrtc/webrtc_peer_connection.cpp
    src/webrtc/webrtc_client.cpp
)

add_library(hubsight-admin-sdk SHARED
    ${HUBSIGHT_ADMIN_PUBLIC_HEADERS}
    ${HUBSIGHT_ADMIN_SOURCES}
)

add_library(HubSight::AdminSdk ALIAS hubsight-admin-sdk)

target_compile_features(hubsight-admin-sdk PUBLIC cxx_std_20)
target_compile_definitions(hubsight-admin-sdk PRIVATE HUBSIGHT_ADMIN_LIBRARY)
target_link_libraries(hubsight-admin-sdk PUBLIC
    Qt6::Core
    Qt6::Network
    Qt6::WebSockets
)
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
install(EXPORT HubSightAdminSdkTargets
    FILE HubSightAdminSdkTargets.cmake
    NAMESPACE HubSight::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/HubSightAdminSdk
)

if(HUBSIGHT_ADMIN_BUILD_TESTS)
    find_package(Qt6 6.6 REQUIRED COMPONENTS Test)
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
endif()
