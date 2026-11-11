cmake_minimum_required(VERSION 3.24)
project(TankWars VERSION 1.1.1 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
option(TANKWARS_FETCH_SFML "Download SFML when not installed" ON)
option(TANKWARS_BUILD_TESTS "Build simulation and LAN integration tests" ON)

find_package(SFML 3 QUIET COMPONENTS Graphics Network)
if(NOT SFML_FOUND)
    if(NOT TANKWARS_FETCH_SFML)
        message(FATAL_ERROR "Install SFML 3 or enable TANKWARS_FETCH_SFML")
    endif()
    include(FetchContent)
    set(SFML_BUILD_AUDIO OFF CACHE BOOL "" FORCE)
    set(SFML_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
    set(SFML_BUILD_TEST_SUITE OFF CACHE BOOL "" FORCE)
    set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
    FetchContent_Declare(SFML
        URL https://github.com/SFML/SFML/archive/refs/tags/3.0.2.tar.gz
        URL_HASH SHA256=0034e05f95509e5d3fb81b1625713e06da7b068f210288ce3fd67106f8f46995
        DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
    FetchContent_MakeAvailable(SFML)
endif()

find_package(Threads REQUIRED)
add_library(tankwars_core
    src/Config.cpp src/Map.cpp src/Physics.cpp src/World.cpp src/GameSession.cpp)
target_include_directories(tankwars_core PUBLIC include)
add_library(tankwars_network src/Network.cpp)
target_link_libraries(tankwars_network PUBLIC tankwars_core SFML::Network Threads::Threads)
add_executable(tank_wars src/main.cpp src/Game.cpp src/Renderer.cpp)
target_link_libraries(tank_wars PRIVATE tankwars_network SFML::Graphics)
target_compile_definitions(tank_wars PRIVATE TANKWARS_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
set_property(TARGET tank_wars APPEND PROPERTY LINK_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/config/game.cfg"
    "${CMAKE_CURRENT_SOURCE_DIR}/assets/fonts/Rajdhani-SemiBold.ttf"
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake/Info.plist.in")
foreach(target tankwars_core tankwars_network tank_wars)
    if(MSVC)
        target_compile_options(${target} PRIVATE /W4 /permissive-)
    else()
        target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic)
    endif()
endforeach()
add_custom_command(TARGET tank_wars POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_CURRENT_SOURCE_DIR}/assets $<TARGET_FILE_DIR:tank_wars>/assets
    COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_CURRENT_SOURCE_DIR}/config $<TARGET_FILE_DIR:tank_wars>/config)
if(APPLE)
    configure_file(cmake/Info.plist.in ${CMAKE_CURRENT_BINARY_DIR}/Info.plist @ONLY)
    set(APP_DIR "${CMAKE_CURRENT_BINARY_DIR}/TankWars.app")
    add_custom_command(TARGET tank_wars POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory "${APP_DIR}/Contents/MacOS"
        COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:tank_wars> "${APP_DIR}/Contents/MacOS/tank_wars"
        COMMAND ${CMAKE_COMMAND} -E copy ${CMAKE_CURRENT_BINARY_DIR}/Info.plist "${APP_DIR}/Contents/Info.plist"
        COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_CURRENT_SOURCE_DIR}/assets "${APP_DIR}/Contents/Resources/assets"
        COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_CURRENT_SOURCE_DIR}/config "${APP_DIR}/Contents/Resources/config")
    install(DIRECTORY "${APP_DIR}" DESTINATION .)
endif()
install(TARGETS tank_wars RUNTIME DESTINATION .)
install(DIRECTORY assets config DESTINATION .)

if(TANKWARS_BUILD_TESTS)
    enable_testing()
    add_executable(simulation_tests tests/SimulationTests.cpp)
    target_link_libraries(simulation_tests PRIVATE tankwars_core)
    add_test(NAME simulation COMMAND simulation_tests)
    add_executable(mechanics_tests tests/MechanicsTests.cpp)
    target_link_libraries(mechanics_tests PRIVATE tankwars_core)
    add_test(NAME mechanics COMMAND mechanics_tests)
    add_executable(controls_tests tests/ControlsTests.cpp)
    target_link_libraries(controls_tests PRIVATE tankwars_core SFML::Window)
    add_test(NAME controls COMMAND controls_tests)
    add_executable(titan_tests tests/TitanTests.cpp)
    target_link_libraries(titan_tests PRIVATE tankwars_core)
    add_test(NAME titan COMMAND titan_tests)
    add_executable(compatibility_peer tests/CompatibilityPeer.cpp)
    target_link_libraries(compatibility_peer PRIVATE tankwars_network)
    target_compile_definitions(compatibility_peer PRIVATE TANKWARS_NATIVE_HEALTH=1)
    add_executable(stomp_network_tests tests/StompNetworkTests.cpp)
    target_link_libraries(stomp_network_tests PRIVATE tankwars_network)
    add_test(NAME stomp_network COMMAND stomp_network_tests)
    set_tests_properties(stomp_network PROPERTIES TIMEOUT 20)
    add_executable(network_tests tests/NetworkTests.cpp)
    target_link_libraries(network_tests PRIVATE tankwars_network)
    add_test(NAME network COMMAND network_tests)
    set_tests_properties(network PROPERTIES TIMEOUT 25)
endif()
