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
    src/core/RtfReader.cpp
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
    src/core/RtfWriter.cpp
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
    if(_pb_name MATCHES "\\.rtf\\.json$")
        continue()
    endif()
    if(_pb_name MATCHES "^wave19-pdf-")
        continue()
    endif()
    # Export/import playbooks under qa/wave20-* are registered by the epub and
    # rtf loops below, which need a sibling expectation file. Playing them here
    # as well would run each one twice.
    if(_pb_name MATCHES "^wave20-")
        continue()
    endif()
    if(_pb_name MATCHES "^wave24-")
        continue()
    endif()
    # Wave 25 PDF playbooks (D20-D23) are played by pdf_check_selftest
    # (qa/check_pdf.py run_wave25_suite), which writes the PDF to a private
    # temp path. Played here they would write to build/linux, which a
    # linux-debug or Windows build does not have.
    if(_pb_name MATCHES "^wave25-d2[0-3]-")
        continue()
    endif()
    add_test(NAME "playbook_${_pb_name}" COMMAND aioffice_cli play "${_pb}")
    set_tests_properties("playbook_${_pb_name}" PROPERTIES
        LABELS "playbook"
        WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
    )
endforeach()

# wave23 I/O regressions prepare /dev/full links and hostile fixtures themselves.
# They must not depend on qa/audit/setup.sh or a hand-made /tmp directory.
set(_wave23_io_fixture "${CMAKE_BINARY_DIR}/wave23-io-fixture")
add_test(NAME wave23_io_setup
    COMMAND bash "${CMAKE_SOURCE_DIR}/qa/wave23-io-setup.sh" "${_wave23_io_fixture}")
set_tests_properties(wave23_io_setup PROPERTIES
    FIXTURES_SETUP wave23_io
    LABELS "playbook"
    WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
    TIMEOUT 60
)
file(GLOB _wave23_io_playbooks CONFIGURE_DEPENDS "${CMAKE_SOURCE_DIR}/qa/wave23-io-*.json")
foreach(_pb IN LISTS _wave23_io_playbooks)
    get_filename_component(_pb_name "${_pb}" NAME)
    set_tests_properties("playbook_${_pb_name}" PROPERTIES
        FIXTURES_REQUIRED wave23_io
        WORKING_DIRECTORY "${_wave23_io_fixture}"
        TIMEOUT 60
    )
endforeach()
# Real inflated bytes, not the central-directory size. Caps match
# ZIP_ENTRY_CAP / ZIP_TOTAL_CAP in qa/wave23-io-fixtures.py and stay below
# the built-in 256 MiB / 1 GiB ceilings.
set_tests_properties("playbook_wave23-io-zip-underdeclared.json" PROPERTIES
    ENVIRONMENT "AIOFFICE_ZIP_ENTRY_CAP=200000;AIOFFICE_ZIP_TOTAL_CAP=300000"
)
set_tests_properties("playbook_wave23-io-field-layout.json" PROPERTIES
    ENVIRONMENT "AIOFFICE_COUNT_LAYOUT=1"
)
# QPdfWriter blocks on a fifo. Fail fast if the regular-file check regresses.
set_tests_properties("playbook_wave23-io-export-pdf-target.json" PROPERTIES
    TIMEOUT 20
)
add_test(NAME tools_catalog COMMAND aioffice_cli tools)
set_tests_properties(tools_catalog PROPERTIES LABELS "playbook" WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")
add_test(NAME play_classify COMMAND "${CMAKE_COMMAND}" -E env AIOFFICE_PLAY_CLASSIFY_CHECK=1 AIOFFICE_HEADLESS=1 QT_QPA_PLATFORM=offscreen
    "${AIOFFICE_CLI}")
set_tests_properties(play_classify PROPERTIES LABELS "playbook" WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")

# Roundtrip tests run Python; require it at configure time so a missing interpreter
# fails loudly here instead of as "no such file or directory" inside ctest (CI container).
find_package(Python3 REQUIRED COMPONENTS Interpreter)
add_test(NAME wave24_docx_export COMMAND "${CMAKE_COMMAND}" -E env AIOFFICE_HEADLESS=1 QT_QPA_PLATFORM=offscreen
    "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/qa/check_wave24_docx_export.py" --cli "${AIOFFICE_CLI}")
set_tests_properties(wave24_docx_export PROPERTIES
    LABELS "playbook"
    TIMEOUT 180
    WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
)
add_test(NAME wave24_docx_export_selftest COMMAND "${Python3_EXECUTABLE}"
    "${CMAKE_SOURCE_DIR}/qa/check_wave24_docx_export.py" --self-test)
set_tests_properties(wave24_docx_export_selftest PROPERTIES
    LABELS "playbook"
    WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
)
add_test(NAME wave23_io_ids
    COMMAND "${CMAKE_COMMAND}" -E env AIOFFICE_HEADLESS=1 QT_QPA_PLATFORM=offscreen
        "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/qa/check_wave23_io_ids.py"
        "${AIOFFICE_CLI}" "${_wave23_io_fixture}")
set_tests_properties(wave23_io_ids PROPERTIES
    FIXTURES_REQUIRED wave23_io
    LABELS "playbook"
    WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
    TIMEOUT 120
)
add_test(NAME wave23_io_hostile
    COMMAND "${CMAKE_COMMAND}" -E env AIOFFICE_HEADLESS=1 QT_QPA_PLATFORM=offscreen
        "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/qa/check_wave23_io_hostile.py"
        "${AIOFFICE_CLI}")
set_tests_properties(wave23_io_hostile PROPERTIES
    LABELS "playbook"
    WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
    TIMEOUT 60
)
# D17: page-preview PNGs must decode with zbarimg. The playbook above is the
# capacity expect; this checker is the byte-for-byte judge.
add_test(NAME wave24_qr
    COMMAND "${CMAKE_COMMAND}" -E env AIOFFICE_HEADLESS=1 QT_QPA_PLATFORM=offscreen
        "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/qa/check_qr.py"
        --cli "${AIOFFICE_CLI}"
        --playbook "${CMAKE_SOURCE_DIR}/qa/wave24-qr.json")
set_tests_properties(wave24_qr PROPERTIES
    LABELS "playbook"
    WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
    TIMEOUT 300
)
set(_AIO_PYTHON "${CMAKE_SOURCE_DIR}/qa/check_roundtrip.py")
add_test(NAME play_expect_shape COMMAND "${CMAKE_COMMAND}" -E env AIOFFICE_HEADLESS=1 QT_QPA_PLATFORM=offscreen
    "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/qa/check_play_expect.py" "${AIOFFICE_CLI}")
set_tests_properties(play_expect_shape PROPERTIES LABELS "playbook" WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")
add_test(NAME mcp_doc_export_prose COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/qa/check_mcp_prose.py")
set_tests_properties(mcp_doc_export_prose PROPERTIES LABELS "playbook" WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")
add_test(NAME mcp_stdio COMMAND "${CMAKE_COMMAND}" -E env AIOFFICE_HEADLESS=1 QT_QPA_PLATFORM=offscreen
    "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/qa/check_mcp_stdio.py" "${AIOFFICE_CLI}")
set_tests_properties(mcp_stdio PROPERTIES LABELS "playbook" TIMEOUT 60 WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")
add_test(NAME wave23_ipc COMMAND "${CMAKE_COMMAND}" -E env AIOFFICE_HEADLESS=1 QT_QPA_PLATFORM=offscreen
    "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/qa/check_wave23_ipc.py" "${AIOFFICE_CLI}")
set_tests_properties(wave23_ipc PROPERTIES LABELS "playbook" TIMEOUT 90 WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")
add_test(NAME wave23_socket COMMAND "${CMAKE_COMMAND}" -E env AIOFFICE_HEADLESS=1 QT_QPA_PLATFORM=offscreen
    "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/qa/check_wave23_socket.py" "${AIOFFICE_CLI}")
set_tests_properties(wave23_socket PROPERTIES LABELS "playbook" TIMEOUT 60 WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")
add_test(NAME wave23_lok COMMAND "${CMAKE_COMMAND}" -E env AIOFFICE_HEADLESS=1 QT_QPA_PLATFORM=offscreen
    "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/qa/check_wave23_lok.py" "${AIOFFICE_CLI}")
set_tests_properties(wave23_lok PROPERTIES LABELS "playbook" TIMEOUT 60 WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")
add_test(NAME wave23_cli COMMAND "${CMAKE_COMMAND}" -E env AIOFFICE_HEADLESS=1 QT_QPA_PLATFORM=offscreen
    "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/qa/check_wave23_cli.py" "${AIOFFICE_CLI}")
set_tests_properties(wave23_cli PROPERTIES LABELS "playbook" TIMEOUT 60 WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")
add_test(NAME pr_merge_gate_selftest COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/scripts/pr-merge-gate.py" --self-test)
set_tests_properties(pr_merge_gate_selftest PROPERTIES LABELS "playbook" WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")
add_test(NAME usage_guard_selftest COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/scripts/usage-guard.py" --self-test)
set_tests_properties(usage_guard_selftest PROPERTIES LABELS "playbook" WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")
add_test(NAME fleet_verify_merged_selftest COMMAND bash "${CMAKE_SOURCE_DIR}/scripts/fleet-verify-merged.sh" --self-test)
set_tests_properties(fleet_verify_merged_selftest PROPERTIES LABELS "playbook" WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")
add_test(NAME roundtrip_compare_selftest COMMAND "${Python3_EXECUTABLE}" "${_AIO_PYTHON}" --self-test)
set_tests_properties(roundtrip_compare_selftest PROPERTIES LABELS "playbook" WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")
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
# A-qa-1: re-enabled for wave 24 (export/reimport payload fidelity — T1–T3); included in default qa-all via labels roundtrip and wave24.
add_test(NAME roundtrip_foreign_odt_wave17 COMMAND "${CMAKE_COMMAND}" -E env AIOFFICE_HEADLESS=1 QT_QPA_PLATFORM=offscreen
    "${Python3_EXECUTABLE}" "${_AIO_PYTHON}" --cli "${AIOFFICE_CLI}" --foreign "${CMAKE_SOURCE_DIR}/qa/wave17-odt-revisions.aio.json" odt)
set_tests_properties(roundtrip_foreign_odt_wave17 PROPERTIES
    LABELS "roundtrip;wave24"
    WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
)
add_test(NAME roundtrip_foreign_docx_wave17 COMMAND "${CMAKE_COMMAND}" -E env AIOFFICE_HEADLESS=1 QT_QPA_PLATFORM=offscreen
    "${Python3_EXECUTABLE}" "${_AIO_PYTHON}" --cli "${AIOFFICE_CLI}" --foreign "${CMAKE_SOURCE_DIR}/qa/wave17-docx-revisions.aio.json" docx)
set_tests_properties(roundtrip_foreign_docx_wave17 PROPERTIES
    LABELS "roundtrip;wave24"
    WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
)

add_test(NAME wave24_qr_decode COMMAND "${CMAKE_COMMAND}" -E env AIOFFICE_HEADLESS=1 QT_QPA_PLATFORM=offscreen
    "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/qa/check_qr.py" --cli "${AIOFFICE_CLI}")
set_tests_properties(wave24_qr_decode PROPERTIES
    LABELS "wave24"
    TIMEOUT 180
    WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
)
add_test(NAME wave24_qr_selftest COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/qa/check_qr.py" --self-test)
set_tests_properties(wave24_qr_selftest PROPERTIES LABELS "playbook" WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")

add_test(NAME wave24_checker_selftest COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/qa/check_wave24.py" --self-test)
set_tests_properties(wave24_checker_selftest PROPERTIES LABELS "playbook" WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")

file(GLOB _wave24_fixture_expectations CONFIGURE_DEPENDS "${CMAKE_SOURCE_DIR}/qa/fixtures/wave24-*.wave24.json")
foreach(_w24exp IN LISTS _wave24_fixture_expectations)
    get_filename_component(_w24stem "${_w24exp}" NAME_WE)
    string(REGEX REPLACE "\\.wave24$" "" _w24base "${_w24stem}")
    set(_w24fixture "")
    if(EXISTS "${CMAKE_SOURCE_DIR}/qa/fixtures/${_w24base}.docx")
        set(_w24fixture "${CMAKE_SOURCE_DIR}/qa/fixtures/${_w24base}.docx")
    elseif(EXISTS "${CMAKE_SOURCE_DIR}/qa/fixtures/${_w24base}.odt")
        set(_w24fixture "${CMAKE_SOURCE_DIR}/qa/fixtures/${_w24base}.odt")
    endif()
    if(_w24fixture STREQUAL "")
        continue()
    endif()
    add_test(NAME "wave24_${_w24base}" COMMAND "${CMAKE_COMMAND}" -E env AIOFFICE_HEADLESS=1 QT_QPA_PLATFORM=offscreen
        "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/qa/check_wave24.py"
        --cli "${AIOFFICE_CLI}" --fixture "${_w24fixture}")
    set_tests_properties("wave24_${_w24base}" PROPERTIES
        LABELS "wave24"
        TIMEOUT 180
        WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
    )
endforeach()

file(GLOB _wave24_playbooks CONFIGURE_DEPENDS "${CMAKE_SOURCE_DIR}/qa/wave24-*.json")
foreach(_w24pb IN LISTS _wave24_playbooks)
    get_filename_component(_w24pb_name "${_w24pb}" NAME)
    if(_w24pb_name MATCHES "\\.wave24\\.json$")
        continue()
    endif()
    get_filename_component(_w24pb_stem "${_w24pb}" NAME_WE)
    if(NOT EXISTS "${CMAKE_SOURCE_DIR}/qa/${_w24pb_stem}.wave24.json")
        continue()
    endif()
    add_test(NAME "wave24_play_${_w24pb_stem}" COMMAND "${CMAKE_COMMAND}" -E env AIOFFICE_HEADLESS=1 QT_QPA_PLATFORM=offscreen
        "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/qa/check_wave24.py"
        --cli "${AIOFFICE_CLI}" --playbook "${_w24pb}")
    set_tests_properties("wave24_play_${_w24pb_stem}" PROPERTIES
        LABELS "wave24"
        TIMEOUT 180
        WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
    )
endforeach()

# Per-decision functional playbooks (D1-D16): plain `aioffice_cli play` scripts with
# inline `expect`, one file per decision, asserting the real tool-visible behaviour
# (document_get/page_preview/round trip through document_save+document_open) rather
# than the check_wave24.py body/export judge above. Each exercises a fixture built by
# qa/fixtures/make_wave24_docx.py or qa/fixtures/gen_wave24_odt.py and fails without
# its decision's fix. D17 (QR) is covered separately by wave24_qr/wave24_qr_decode.
set(_wave24_decision_playbooks
    wave24-docx-d1-runs.json
    wave24-docx-d2-image.json
    wave24-docx-d3-page.json
    wave24-docx-d4-tables.json
    wave24-docx-d4-tables-odt.json
    wave24-docx-d5-caption.json
    wave24-docx-d6-core.json
    wave24-docx-d7-del-field.json
    wave24-docx-d8-lists.json
    wave24-docx-d10-revisions.json
    wave24-docx-d10-revision-ids.json
    wave24-qr-validate-block.json
    wave24-odt-d3-page.json
    wave24-odt-d4-table.json
    wave24-odt-d9-escape.json
    wave24-odt-d12-notes.json
    wave24-odt-d13-font.json
    wave24-odt-d14-lists.json
    wave24-odt-d15-figures.json
    wave24-odt-d16-bookmarks.json
)
foreach(_w24d IN LISTS _wave24_decision_playbooks)
    get_filename_component(_w24d_stem "${_w24d}" NAME_WE)
    add_test(NAME "wave24_decision_${_w24d_stem}" COMMAND aioffice_cli play "${CMAKE_SOURCE_DIR}/qa/${_w24d}")
    set_tests_properties("wave24_decision_${_w24d_stem}" PROPERTIES
        LABELS "wave24"
        TIMEOUT 180
        WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
    )
endforeach()

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
add_test(NAME pdf_check_selftest COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/qa/check_pdf.py" --self-test --cli "${AIOFFICE_CLI}")
set_tests_properties(pdf_check_selftest PROPERTIES LABELS "playbook" WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")

# Wave 26 paint judges. png_probe.py is stdlib-only and does not need the CLI.
add_test(NAME wave26_png_probe COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/qa/png_probe.py" --self-test)
set_tests_properties(wave26_png_probe PROPERTIES
    LABELS "wave26"
    WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
    TIMEOUT 30
)
foreach(_w26_check d4 d5 d6)
    add_test(NAME "wave26_${_w26_check}" COMMAND "${CMAKE_COMMAND}" -E env AIOFFICE_HEADLESS=1 QT_QPA_PLATFORM=offscreen
        "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/qa/wave26-paint.py"
        --check "${_w26_check}" --cli "${AIOFFICE_CLI}")
    set_tests_properties("wave26_${_w26_check}" PROPERTIES
        LABELS "wave26"
        WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
        TIMEOUT 180
    )
endforeach()

if(EXISTS "${CMAKE_SOURCE_DIR}/qa/wave20-rtf-out.json"
   AND EXISTS "${CMAKE_SOURCE_DIR}/qa/wave20-rtf-out.rtf.json")
    add_test(NAME rtf_wave20_rtf_out COMMAND "${CMAKE_COMMAND}" -E env AIOFFICE_HEADLESS=1 QT_QPA_PLATFORM=offscreen
        "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/qa/check_wave20_rtf_out.py"
        --cli "${AIOFFICE_CLI}" --playbook "${CMAKE_SOURCE_DIR}/qa/wave20-rtf-out.json")
    set_tests_properties(rtf_wave20_rtf_out PROPERTIES
        LABELS "playbook"
        WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
    )
endif()
add_test(NAME rtf_wave20_rtf_out_selftest COMMAND "${Python3_EXECUTABLE}"
    "${CMAKE_SOURCE_DIR}/qa/check_wave20_rtf_out.py" --self-test)
set_tests_properties(rtf_wave20_rtf_out_selftest PROPERTIES
    LABELS "playbook"
    WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
)
# wave20-epub.json has no hyphen suffix, so wave20-epub-*.json does not match it.
file(GLOB _aio_epub_playbooks CONFIGURE_DEPENDS
    "${CMAKE_SOURCE_DIR}/qa/wave20-epub-*.json"
    "${CMAKE_SOURCE_DIR}/qa/wave20-epub.json")
foreach(_epub_pb IN LISTS _aio_epub_playbooks)
    get_filename_component(_epub_name "${_epub_pb}" NAME)
    if(_epub_name MATCHES "\\.epub\\.json$")
        continue()
    endif()
    get_filename_component(_epub_stem "${_epub_pb}" NAME_WE)
    if(NOT EXISTS "${CMAKE_SOURCE_DIR}/qa/${_epub_stem}.epub.json")
        continue()
    endif()
    add_test(NAME "epub_${_epub_name}" COMMAND "${CMAKE_COMMAND}" -E env AIOFFICE_HEADLESS=1 QT_QPA_PLATFORM=offscreen
        "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/qa/check_epub.py"
        --cli "${AIOFFICE_CLI}" --playbook "${_epub_pb}")
    set_tests_properties("epub_${_epub_name}" PROPERTIES
        LABELS "epub"
        WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
    )
endforeach()
add_test(NAME epub_check_selftest COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/qa/check_epub.py" --self-test --cli "${AIOFFICE_CLI}")
set_tests_properties(epub_check_selftest PROPERTIES LABELS "playbook" WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")

file(GLOB _aio_rtf_playbooks CONFIGURE_DEPENDS "${CMAKE_SOURCE_DIR}/qa/wave20-rtf-*.json")
foreach(_rtf_pb IN LISTS _aio_rtf_playbooks)
    get_filename_component(_rtf_name "${_rtf_pb}" NAME)
    if(_rtf_name MATCHES "\\.rtf\\.json$")
        continue()
    endif()
    get_filename_component(_rtf_stem "${_rtf_pb}" NAME_WE)
    if(NOT EXISTS "${CMAKE_SOURCE_DIR}/qa/${_rtf_stem}.rtf.json")
        continue()
    endif()
    add_test(NAME "rtf_${_rtf_name}" COMMAND "${CMAKE_COMMAND}" -E env AIOFFICE_HEADLESS=1 QT_QPA_PLATFORM=offscreen
        "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/qa/check_rtf.py"
        --cli "${AIOFFICE_CLI}" --playbook "${_rtf_pb}")
    set_tests_properties("rtf_${_rtf_name}" PROPERTIES
        LABELS "rtf"
        WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
    )
endforeach()
file(GLOB _aio_rtf_fixtures CONFIGURE_DEPENDS
    "${CMAKE_SOURCE_DIR}/qa/fixtures/wave20-rtf-*-foreign.rtf"
    "${CMAKE_SOURCE_DIR}/qa/fixtures/wave20-rtf-hostile-*.rtf")
foreach(_rtf_fx IN LISTS _aio_rtf_fixtures)
    get_filename_component(_rtf_fx_name "${_rtf_fx}" NAME)
    if(NOT EXISTS "${CMAKE_SOURCE_DIR}/qa/fixtures/${_rtf_fx_name}.json")
        continue()
    endif()
    add_test(NAME "rtf_fixture_${_rtf_fx_name}" COMMAND "${CMAKE_COMMAND}" -E env AIOFFICE_HEADLESS=1 QT_QPA_PLATFORM=offscreen
        "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/qa/check_rtf.py"
        --cli "${AIOFFICE_CLI}" --fixture "${_rtf_fx}")
    set_tests_properties("rtf_fixture_${_rtf_fx_name}" PROPERTIES
        LABELS "rtf"
        WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
    )
endforeach()
add_test(NAME rtf_check_selftest COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/qa/check_rtf.py" --self-test --cli "${AIOFFICE_CLI}")
set_tests_properties(rtf_check_selftest PROPERTIES LABELS "playbook" WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")

add_test(NAME rtf_footnote_scale COMMAND "${CMAKE_COMMAND}" -E env AIOFFICE_HEADLESS=1 QT_QPA_PLATFORM=offscreen
    "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/qa/check_rtf.py"
    --cli "${AIOFFICE_CLI}" --footnote-scale 8000 --footnote-max-seconds 8)
set_tests_properties(rtf_footnote_scale PROPERTIES
    LABELS "rtf"
    TIMEOUT 120
    WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
)
add_test(NAME rtf_footnote_flush COMMAND "${CMAKE_COMMAND}" -E env AIOFFICE_HEADLESS=1 QT_QPA_PLATFORM=offscreen
    "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/qa/check_rtf.py"
    --cli "${AIOFFICE_CLI}" --footnote-flush 100000 --footnote-table 20000 --footnote-max-seconds 8)
set_tests_properties(rtf_footnote_flush PROPERTIES
    LABELS "rtf"
    TIMEOUT 90
    WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
)

# Wave 26 D1–D3. The PNG helper is qa/png_probe.py (stdlib only). D3's own
# limit is 10s; the ctest budget covers the other judges plus that timeout.
add_test(NAME wave26_text COMMAND "${CMAKE_COMMAND}" -E env AIOFFICE_HEADLESS=1 QT_QPA_PLATFORM=offscreen
    "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/qa/wave26-text.py" --cli "${AIOFFICE_CLI}")
set_tests_properties(wave26_text PROPERTIES
    LABELS "playbook"
    TIMEOUT 180
    WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
)
