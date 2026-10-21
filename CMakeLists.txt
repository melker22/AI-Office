cmake_minimum_required(VERSION 3.22)
project(AIOffice VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

find_program(CCACHE_PROGRAM ccache)
if(CCACHE_PROGRAM AND NOT CMAKE_CXX_COMPILER_LAUNCHER)
    set(CMAKE_CXX_COMPILER_LAUNCHER "${CCACHE_PROGRAM}")
endif()

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE RelWithDebInfo CACHE STRING "Build type" FORCE)
endif()

find_package(Qt6 6.5 REQUIRED COMPONENTS Core Gui Network Qml Quick QuickControls2)
find_package(ZLIB REQUIRED)
find_package(PkgConfig QUIET)
if(PkgConfig_FOUND)
    pkg_check_modules(HUNSPELL IMPORTED_TARGET hunspell)
endif()

qt_standard_project_setup(REQUIRES 6.5)

add_library(aioffice_core STATIC
    src/core/Document.cpp
    src/core/FieldExpr.cpp
    src/core/Session.cpp
    src/core/Odf.cpp
    src/core/Docx.cpp
    src/core/Zip.cpp
    src/core/Xhtml.cpp
    src/core/Epub.cpp
    src/core/Preview.cpp
    src/core/Xml.cpp
    src/core/Spell.cpp
    src/core/Qr.cpp
    src/core/Hyphenation.cpp
    src/core/hyphen/hyphen.qrc
    src/core/PdfPostProcess.cpp
    src/core/Csl.cpp
    src/lok/LokEngine.cpp
    src/mcp/ToolDispatch.cpp
    src/mcp/McpServer.cpp
    src/mcp/Ipc.cpp
    src/cli/Cli.cpp
)
target_include_directories(aioffice_core PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)
target_link_libraries(aioffice_core PUBLIC
    Qt6::Core
    Qt6::Gui
    Qt6::Network
    ZLIB::ZLIB
)
if(TARGET PkgConfig::HUNSPELL)
    target_link_libraries(aioffice_core PUBLIC PkgConfig::HUNSPELL)
    target_compile_definitions(aioffice_core PUBLIC AIOFFICE_HAS_HUNSPELL)
endif()
target_compile_definitions(aioffice_core PUBLIC
    AIOFFICE_VERSION="${PROJECT_VERSION}"
)

if(UNIX AND NOT APPLE)
    target_link_libraries(aioffice_core PUBLIC ${CMAKE_DL_LIBS})
endif()

# CLI/MCP binary with no QML — what CI compiles and what `aioffice play` needs.
# The GUI target below still exists; it is not the smoke-suite gate.
add_executable(aioffice_cli
    src/cli/HeadlessMain.cpp
)
target_link_libraries(aioffice_cli PRIVATE aioffice_core)
set_target_properties(aioffice_cli PROPERTIES
    OUTPUT_NAME aioffice-cli
    WIN32_EXECUTABLE FALSE
)

qt_add_executable(aioffice
    src/app/main.cpp
    src/app/AppController.cpp
    src/app/DocumentCanvas.cpp
)

set(_aio_qml
    Main.qml
    Theme.qml
    IconBtn.qml
    TitleBar.qml
    DocTabs.qml
    CommandBar.qml
    FindBar.qml
    Ruler.qml
    OutlineRail.qml
    EmptyWelcome.qml
    OpenPanel.qml
    SaveSheet.qml
    UnsavedSheet.qml
    LibreOfficeSheet.qml
    TableMiniBar.qml
    Sidebar.qml
    StatusBarBar.qml
    AiIntegratePanel.qml
    UiButton.qml
    UiField.qml
    UiCombo.qml
    UiSpin.qml
    UiTip.qml
)
foreach(_f IN LISTS _aio_qml)
    set_source_files_properties(src/app/qml/${_f} PROPERTIES QT_RESOURCE_ALIAS ${_f})
endforeach()
set_source_files_properties(src/app/qml/Theme.qml PROPERTIES QT_QML_SINGLETON_TYPE TRUE)

qt_add_qml_module(aioffice
    URI AIOffice
    VERSION 1.0
    RESOURCE_PREFIX /qt/qml
    QML_FILES
        src/app/qml/Main.qml
        src/app/qml/Theme.qml
        src/app/qml/IconBtn.qml
        src/app/qml/TitleBar.qml
        src/app/qml/DocTabs.qml
        src/app/qml/CommandBar.qml
        src/app/qml/FindBar.qml
        src/app/qml/Ruler.qml
        src/app/qml/OutlineRail.qml
        src/app/qml/EmptyWelcome.qml
        src/app/qml/OpenPanel.qml
        src/app/qml/SaveSheet.qml
        src/app/qml/UnsavedSheet.qml
        src/app/qml/LibreOfficeSheet.qml
        src/app/qml/TableMiniBar.qml
        src/app/qml/Sidebar.qml
        src/app/qml/StatusBarBar.qml
        src/app/qml/AiIntegratePanel.qml
        src/app/qml/UiButton.qml
        src/app/qml/UiField.qml
        src/app/qml/UiCombo.qml
        src/app/qml/UiSpin.qml
        src/app/qml/UiTip.qml
)

target_link_libraries(aioffice PRIVATE
    aioffice_core
    Qt6::Qml
    Qt6::Quick
    Qt6::QuickControls2
)

set_target_properties(aioffice PROPERTIES
    OUTPUT_NAME aioffice
    WIN32_EXECUTABLE FALSE
)

install(TARGETS aioffice RUNTIME DESTINATION bin)
install(FILES skills/SKILL.md skills/mcp.json DESTINATION share/ai-office)
install(FILES docs/mcp-tools.md DESTINATION share/ai-office/docs)

# QA: CTest playbooks (LABELS playbook) + foreign revision fixtures (LABELS roundtrip).
enable_testing()
set(AIOFFICE_CLI "$<TARGET_FILE:aioffice_cli>")
file(GLOB _aio_playbooks CONFIGURE_DEPENDS "${CMAKE_SOURCE_DIR}/qa/wave*.json")
foreach(_pb IN LISTS _aio_playbooks)
    get_filename_component(_pb_name "${_pb}" NAME)
    if(_pb_name STREQUAL "wave1-foundation.json")
        continue()
    endif()
    if(_pb_name MATCHES "\\.aio\\.json$")
        continue()
    endif()
    if(_pb_name MATCHES "^wave19-pdf-")
        continue()
    endif()
    add_test(NAME "playbook_${_pb_name}" COMMAND aioffice_cli play "${_pb}")
    set_tests_properties("playbook_${_pb_name}" PROPERTIES
        LABELS "playbook"
        WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
    )
endforeach()
add_test(NAME tools_catalog COMMAND aioffice_cli tools)
set_tests_properties(tools_catalog PROPERTIES LABELS "playbook" WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")
add_test(NAME play_classify COMMAND "${CMAKE_COMMAND}" -E env AIOFFICE_PLAY_CLASSIFY_CHECK=1 AIOFFICE_HEADLESS=1 QT_QPA_PLATFORM=offscreen
    "${AIOFFICE_CLI}")
set_tests_properties(play_classify PROPERTIES LABELS "playbook" WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")

# Roundtrip tests run Python; require it at configure time so a missing interpreter
# fails loudly here instead of as "no such file or directory" inside ctest (CI container).
find_package(Python3 REQUIRED COMPONENTS Interpreter)
set(_AIO_PYTHON "${CMAKE_SOURCE_DIR}/qa/check_roundtrip.py")
add_test(NAME play_expect_shape COMMAND "${CMAKE_COMMAND}" -E env AIOFFICE_HEADLESS=1 QT_QPA_PLATFORM=offscreen
    "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/qa/check_play_expect.py" "${AIOFFICE_CLI}")
set_tests_properties(play_expect_shape PROPERTIES LABELS "playbook" WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")
file(GLOB _aio_fixtures CONFIGURE_DEPENDS "${CMAKE_SOURCE_DIR}/qa/fixtures/*-foreign.docx" "${CMAKE_SOURCE_DIR}/qa/fixtures/*-foreign.odt")
foreach(_fx IN LISTS _aio_fixtures)
    get_filename_component(_fx_name "${_fx}" NAME)
    add_test(NAME "roundtrip_${_fx_name}" COMMAND "${CMAKE_COMMAND}" -E env AIOFFICE_HEADLESS=1 QT_QPA_PLATFORM=offscreen
        "${Python3_EXECUTABLE}" "${_AIO_PYTHON}" --cli "${AIOFFICE_CLI}" --fixture "${_fx}")
    set_tests_properties("roundtrip_${_fx_name}" PROPERTIES
        LABELS "roundtrip"
        WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
    )
endforeach()
add_test(NAME roundtrip_foreign_odt_wave17 COMMAND "${CMAKE_COMMAND}" -E env AIOFFICE_HEADLESS=1 QT_QPA_PLATFORM=offscreen
    "${Python3_EXECUTABLE}" "${_AIO_PYTHON}" --cli "${AIOFFICE_CLI}" --foreign "${CMAKE_SOURCE_DIR}/qa/wave17-odt-revisions.aio.json" odt)
set_tests_properties(roundtrip_foreign_odt_wave17 PROPERTIES LABELS "roundtrip" WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")
add_test(NAME roundtrip_foreign_docx_wave17 COMMAND "${CMAKE_COMMAND}" -E env AIOFFICE_HEADLESS=1 QT_QPA_PLATFORM=offscreen
    "${Python3_EXECUTABLE}" "${_AIO_PYTHON}" --cli "${AIOFFICE_CLI}" --foreign "${CMAKE_SOURCE_DIR}/qa/wave17-docx-revisions.aio.json" docx)
set_tests_properties(roundtrip_foreign_docx_wave17 PROPERTIES LABELS "roundtrip" WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")

file(GLOB _aio_pdf_playbooks CONFIGURE_DEPENDS "${CMAKE_SOURCE_DIR}/qa/wave19-pdf-*.json")
foreach(_pdf_pb IN LISTS _aio_pdf_playbooks)
    get_filename_component(_pdf_name "${_pdf_pb}" NAME)
    # The glob also matches expectation files (*.pdf.json) and the reading-order fixture
    # (*.aio.json). check_pdf.py only accepts a playbook that has a sibling .pdf.json.
    if(_pdf_name MATCHES "\\.pdf\\.json$" OR _pdf_name MATCHES "\\.aio\\.json$")
        continue()
    endif()
    get_filename_component(_pdf_stem "${_pdf_pb}" NAME_WE)
    if(NOT EXISTS "${CMAKE_SOURCE_DIR}/qa/${_pdf_stem}.pdf.json")
        continue()
    endif()
    add_test(NAME "pdf_${_pdf_name}" COMMAND "${CMAKE_COMMAND}" -E env AIOFFICE_HEADLESS=1 QT_QPA_PLATFORM=offscreen
        "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/qa/check_pdf.py"
        --cli "${AIOFFICE_CLI}" --playbook "${_pdf_pb}")
    set_tests_properties("pdf_${_pdf_name}" PROPERTIES
        LABELS "pdf"
        WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
    )
endforeach()
add_test(NAME pdf_check_selftest COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/qa/check_pdf.py" --self-test)
set_tests_properties(pdf_check_selftest PROPERTIES LABELS "playbook" WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")
