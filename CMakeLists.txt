cmake_minimum_required(VERSION 3.21)

project(glassNote VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(Qt6 REQUIRED COMPONENTS Widgets Test Network)

qt_add_executable(glassNote
    src/main.cpp
    src/app/AppController.cpp
    src/app/AppController.h
    src/app/AppStateWorkflow.cpp
    src/app/AppStateWorkflow.h
    src/model/AppState.h
    src/model/NoteItem.h
    src/animation/AnimationCoordinator.cpp
    src/animation/AnimationCoordinator.h
    src/common/Constants.h
    src/platform/windows/WindowsBlurHelper.cpp
    src/platform/windows/WindowsBlurHelper.h
    src/storage/AutoSaveCoordinator.cpp
    src/storage/AutoSaveCoordinator.h
    src/storage/JsonStorageService.cpp
    src/storage/JsonStorageService.h
    src/theme/ThemeHelper.cpp
    src/theme/ThemeHelper.h
    src/ui/MainWindow.cpp
    src/ui/MainWindow.h
    src/ui/NoteCardWidget.cpp
    src/ui/NoteCardWidget.h
    src/ui/NotesBoardWidget.cpp
    src/ui/NotesBoardWidget.h
)

target_link_libraries(glassNote PRIVATE Qt6::Widgets Qt6::Network)
target_compile_definitions(glassNote PRIVATE GLASSNOTE_APP_VERSION="${PROJECT_VERSION}")

target_include_directories(glassNote PRIVATE src)

include(GNUInstallDirs)

install(TARGETS glassNote
    RUNTIME DESTINATION .
    BUNDLE DESTINATION .
)

install(FILES README.md DESTINATION .)

if(WIN32)
    if(CMAKE_SIZEOF_VOID_P EQUAL 8)
        set(GLASSNOTE_PACKAGE_ARCH "win64")
    else()
        set(GLASSNOTE_PACKAGE_ARCH "win32")
    endif()
else()
    set(GLASSNOTE_PACKAGE_ARCH "${CMAKE_SYSTEM_NAME}")
endif()

set(CPACK_PACKAGE_NAME "glassNote")
set(CPACK_PACKAGE_VENDOR "glassNote")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "A lightweight transparent sticky notes desktop app")
set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
set(CPACK_PACKAGE_INSTALL_DIRECTORY "glassNote")
set(CPACK_PACKAGE_FILE_NAME "glassNote-${PROJECT_VERSION}-${GLASSNOTE_PACKAGE_ARCH}-portable")
set(CPACK_MONOLITHIC_INSTALL ON)
set(CPACK_GENERATOR "ZIP")

include(CPack)

if(WIN32)
    set_target_properties(glassNote PROPERTIES WIN32_EXECUTABLE TRUE)
    target_link_libraries(glassNote PRIVATE dwmapi)
endif()

include(CTest)

if(BUILD_TESTING)
    set(GLASSNOTE_TEST_COMMON_SOURCES
        src/app/AppStateWorkflow.cpp
        src/app/AppStateWorkflow.h
        src/storage/JsonStorageService.cpp
        src/storage/JsonStorageService.h
    )

    qt_add_executable(glassNoteUnitTests
        tests/unit/test_serialization_and_workflow.cpp
        ${GLASSNOTE_TEST_COMMON_SOURCES}
    )
    target_link_libraries(glassNoteUnitTests PRIVATE Qt6::Test Qt6::Widgets)
    target_include_directories(glassNoteUnitTests PRIVATE src)
    add_test(NAME glassnote_unit_tests COMMAND glassNoteUnitTests)

    qt_add_executable(glassNoteIntegrationTests
        tests/integration/test_load_edit_save_reload.cpp
        ${GLASSNOTE_TEST_COMMON_SOURCES}
    )
    target_link_libraries(glassNoteIntegrationTests PRIVATE Qt6::Test Qt6::Widgets)
    target_include_directories(glassNoteIntegrationTests PRIVATE src)
    add_test(NAME glassnote_integration_tests COMMAND glassNoteIntegrationTests)

    qt_add_executable(glassNotePerfTests
        tests/perf/test_many_notes.cpp
        ${GLASSNOTE_TEST_COMMON_SOURCES}
    )
    target_link_libraries(glassNotePerfTests PRIVATE Qt6::Test Qt6::Widgets)
    target_include_directories(glassNotePerfTests PRIVATE src)
    add_test(NAME glassnote_perf_tests COMMAND glassNotePerfTests)
endif()
