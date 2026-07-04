cmake_minimum_required(VERSION 3.20)

project(vion VERSION 0.4.0 LANGUAGES CXX)

include(GNUInstallDirs)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(vion
    src/main.cpp
    src/lexer/Lexer.cpp
    src/parser/Parser.cpp
    src/runtime/GC.cpp
    src/vm/Chunk.cpp
    src/vm/VM.cpp
    src/compiler/Compiler.cpp
)

target_include_directories(vion PRIVATE src)

# Increase stack size to 8 MB on Windows so recursive Vion programs don't hit
# the OS limit before our callDepth guard fires.
if(WIN32)
    target_link_options(vion PRIVATE "/STACK:8388608")  # 8 MB
endif()

# Treat warnings as warnings (not errors) in release builds
if(MSVC)
    target_compile_options(vion PRIVATE /W3)
endif()

enable_testing()

add_test(
    NAME vion-integration
    COMMAND powershell -ExecutionPolicy Bypass -File "${CMAKE_SOURCE_DIR}/tests/run-tests.ps1" -ExePath "$<TARGET_FILE:vion>"
)

add_custom_target(check
    COMMAND powershell -ExecutionPolicy Bypass -File "${CMAKE_SOURCE_DIR}/tests/run-tests.ps1" -ExePath "$<TARGET_FILE:vion>"
    DEPENDS vion
    WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
)

install(TARGETS vion
    RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}"
)

install(FILES
    LICENSE
    README.md
    INSTALL.md
    LANGUAGE_SPEC.md
    ROADMAP.md
    scripts/uninstall-windows.ps1
    DESTINATION "${CMAKE_INSTALL_DOCDIR}"
)

install(DIRECTORY examples/
    DESTINATION "${CMAKE_INSTALL_DATADIR}/vion/examples"
    FILES_MATCHING PATTERN "*.vion"
)

install(DIRECTORY tools/vscode-vion/
    DESTINATION "${CMAKE_INSTALL_DATADIR}/vion/tools/vscode-vion"
)

set(CPACK_PACKAGE_NAME "Vion")
set(CPACK_PACKAGE_VENDOR "Vionex")
set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Vion programming language CLI")
set(CPACK_PACKAGE_FILE_NAME "vion-${PROJECT_VERSION}-${CMAKE_SYSTEM_NAME}")
set(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_SOURCE_DIR}/LICENSE")
set(CPACK_GENERATOR "ZIP")

include(CPack)
