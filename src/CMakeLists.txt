cmake_minimum_required(VERSION 3.16)
project(QTest VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

find_package(OpenCV REQUIRED COMPONENTS core imgproc imgcodecs)
find_package(Qt6 REQUIRED COMPONENTS Widgets Core Gui LinguistTools)

# ---- Core Model Library (header-only) ----
add_library(qtest_model INTERFACE)
target_include_directories(qtest_model INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}/model
    ${CMAKE_CURRENT_SOURCE_DIR}
)

# ---- Algorithm Library (no Qt) ----
add_library(qtest_algorithm STATIC
    algorithm/qc_operator_cv.cpp
    algorithm/defect_operators_cv.cpp
    algorithm/decision_logic_industrial.cpp
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

# ---- Application Library (no Qt) ----
add_library(qtest_application STATIC
    application/image_loader.cpp
    application/ground_truth_parser.cpp
    application/config_manager.cpp
    application/log_manager.cpp
    application/result_aggregator.cpp
    application/result_marker.cpp
    application/report_generator.cpp
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
    ${OpenCV_LIBS}
)

# ---- UI Library (with Qt) ----
add_library(qtest_ui STATIC
    ui/run_bridge.cpp
    ui/main_window.cpp
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
    Qt6::Widgets
    Qt6::Core
    Qt6::Gui
    ${OpenCV_LIBS}
)

# ---- Main Executable ----
add_executable(qtest
    main.cpp
)
target_link_libraries(qtest PRIVATE
    qtest_ui
    qtest_application
    qtest_algorithm
    qtest_model
    Qt6::Widgets
)

qt6_add_translations(qtest
    TS_FILES
        ${CMAKE_CURRENT_SOURCE_DIR}/ui/lang/qtest_zh_CN.ts
    QM_FILES_OUTPUT_VARIABLE qm_files
)

add_custom_command(TARGET qtest POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:qtest>/lang"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different ${qm_files} "$<TARGET_FILE_DIR:qtest>/lang"
    COMMENT "Copying .qm translation files to output/lang/"
)