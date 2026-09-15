cmake_minimum_required(VERSION 3.20)
project(PixelForge VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

# ---------------- Rust engine (cdylib) ----------------
set(ENGINE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../engine)
if(NOT CARGO_EXECUTABLE)
  find_program(CARGO_EXECUTABLE cargo)
  if(NOT CARGO_EXECUTABLE)
    # fall back to rustup user-local path
    set(CARGO_EXECUTABLE "$ENV{HOME}/.cargo/bin/cargo")
  endif()
endif()

set(RUST_TARGET_DIR ${CMAKE_CURRENT_BINARY_DIR}/rust-target)

if(WIN32)
  set(ENGINE_LIB_NAME pfengine)
  set(ENGINE_DLL ${RUST_TARGET_DIR}/release/${ENGINE_LIB_NAME}.dll)
  set(ENGINE_IMPLIB ${RUST_TARGET_DIR}/release/${ENGINE_LIB_NAME}.dll.lib)
else()
  set(ENGINE_LIB_NAME pfengine)
  set(ENGINE_LIB ${RUST_TARGET_DIR}/release/lib${ENGINE_LIB_NAME}.so)
endif()

# OUTPUT-based custom command: Ninja/MSBuild get a real rule to build the engine lib
if(WIN32)
  add_custom_command(
    OUTPUT ${ENGINE_DLL} ${ENGINE_IMPLIB}
    COMMAND ${CARGO_EXECUTABLE} build --release --manifest-path ${ENGINE_DIR}/Cargo.toml --target-dir ${RUST_TARGET_DIR}
    WORKING_DIRECTORY ${ENGINE_DIR}
    VERBATIM
    COMMENT "Building Rust engine (release)...")
else()
  add_custom_command(
    OUTPUT ${ENGINE_LIB}
    COMMAND ${CARGO_EXECUTABLE} build --release --manifest-path ${ENGINE_DIR}/Cargo.toml --target-dir ${RUST_TARGET_DIR}
    WORKING_DIRECTORY ${ENGINE_DIR}
    VERBATIM
    COMMENT "Building Rust engine (release)...")
endif()
add_custom_target(pfengine_build ALL DEPENDS ${ENGINE_LIB} ${ENGINE_DLL} ${ENGINE_IMPLIB})

# ---------------- Qt ----------------
find_package(Qt6 REQUIRED COMPONENTS Widgets)

set(APP_SOURCES
  src/main.cpp
  src/Engine.cpp
  src/Theme.cpp
  src/Icons.cpp
  src/MainWindow.cpp
  src/CanvasView.cpp
  src/EditorTab.cpp
  src/panels/LayersPanel.cpp
  src/panels/LayerItemDelegate.cpp
  src/panels/ColorPanel.cpp
  src/panels/BrushPanel.cpp
  src/panels/HistoryPanel.cpp
  src/dialogs/Dialogs.cpp
  src/dialogs/CurvesWidget.cpp
)

qt_add_executable(PixelForge ${APP_SOURCES})
add_dependencies(PixelForge pfengine_build)

target_include_directories(PixelForge PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)

target_link_libraries(PixelForge PRIVATE Qt6::Widgets)

# EGL needed by Qt6Gui at link time on Linux
find_library(EGL_LIBRARY NAMES EGL)
if(NOT EGL_LIBRARY AND EXISTS "$ENV{HOME}/usr-local-root/usr/lib/x86_64-linux-gnu/libEGL.so")
  set(EGL_LIBRARY "$ENV{HOME}/usr-local-root/usr/lib/x86_64-linux-gnu/libEGL.so")
endif()
if(EGL_LIBRARY)
  target_link_libraries(PixelForge PRIVATE ${EGL_LIBRARY})
endif()

if(WIN32)
  target_link_libraries(PixelForge PRIVATE ${ENGINE_IMPLIB})
  add_custom_command(TARGET PixelForge POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different ${ENGINE_DLL} $<TARGET_FILE_DIR:PixelForge>
    COMMENT "Copying pfengine.dll")
else()
  # copy .so next to the executable and link it with $ORIGIN rpath
  add_custom_command(TARGET PixelForge POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different ${ENGINE_LIB} $<TARGET_FILE_DIR:PixelForge>
    COMMENT "Copying libpfengine.so")
  target_link_libraries(PixelForge PRIVATE ${RUST_TARGET_DIR}/release/lib${ENGINE_LIB_NAME}.so)
  set_target_properties(PixelForge PROPERTIES BUILD_RPATH "$ORIGIN" INSTALL_RPATH "$ORIGIN")
endif()

install(TARGETS PixelForge BUNDLE DESTINATION . RUNTIME DESTINATION .)
