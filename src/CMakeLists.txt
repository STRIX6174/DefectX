cmake_minimum_required(VERSION 3.16)
project(QTest VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

find_package(OpenCV REQUIRED COMPONENTS core imgproc imgcodecs)
find_package(Qt6 REQUIRED COMPONENTS Widgets Core Gui)
find_package(SQLite3 REQUIRED)
find_package(Threads REQUIRED)

# ---- Core Model Library (header-only) ----
add_library(qtest_model INTERFACE)
target_include_directories(qtest_model INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}/model
    ${CMAKE_CURRENT_SOURCE_DIR}
)

# ---- Algorithm Library (no Qt) ----
add_library(qtest_algorithm STATIC
    algorithm/preprocess_operators.cpp
    algorithm/defect_algorithms.cpp
    algorithm/result_fusion.cpp
)
target_include_directories(qtest_algorithm PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/..
)
target_include_directories(qtest_algorithm PUBLIC
    ${OpenCV_INCLUDE_DIRS}
)
target_link_libraries(qtest_algorithm PUBLIC
    qtest_model
    ${OpenCV_LIBS}
)

# ---- Infrastructure Library (no Qt/OpenCV, model + SQLite + sockets) ----
add_library(qtest_infrastructure STATIC
    infrastructure/ground_truth.cpp
    infrastructure/metric_calculator.cpp
    infrastructure/database.cpp
    infrastructure/result_store.cpp
    infrastructure/comm/modbus_common.cpp
    infrastructure/comm/modbus_tcp_transmitter.cpp
    infrastructure/comm/modbus_rtu_transmitter.cpp
)
target_include_directories(qtest_infrastructure PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
)
target_link_libraries(qtest_infrastructure PUBLIC
    qtest_model
    SQLite3::SQLite3
)
if(WIN32)
    target_link_libraries(qtest_infrastructure PUBLIC ws2_32)
endif()

# ---- Application Library (no Qt) ----
add_library(qtest_application STATIC
    application/image_loader.cpp
    application/config_manager.cpp
    application/log_manager.cpp
    application/result_aggregator.cpp
    application/pipeline_runner.cpp
)
target_include_directories(qtest_application PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/..
)
target_include_directories(qtest_application PUBLIC
    ${OpenCV_INCLUDE_DIRS}
)
target_link_libraries(qtest_application PUBLIC
    qtest_model
    qtest_algorithm
    qtest_infrastructure
    ${OpenCV_LIBS}
    Threads::Threads
)

# ---- UI Library (with Qt) ----
add_library(qtest_ui STATIC
    ui/run_bridge.cpp
    ui/main_window.cpp
    ui/p5_widgets.cpp
    ui/comm_panel.cpp
    ui/history_panel.cpp
    ui/config_panel.cpp
)
target_include_directories(qtest_ui PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/..
)
target_include_directories(qtest_ui PUBLIC
    ${OpenCV_INCLUDE_DIRS}
)
target_link_libraries(qtest_ui PUBLIC
    qtest_model
    qtest_algorithm
    qtest_application
    qtest_infrastructure
    Qt6::Widgets
    Qt6::Core
    Qt6::Gui
    ${OpenCV_LIBS}
)

# ---- Main Executable ----
add_executable(DefectX
    main.cpp
)
target_link_libraries(DefectX PRIVATE
    qtest_ui
    qtest_application
    qtest_algorithm
    qtest_model
    Qt6::Widgets
)