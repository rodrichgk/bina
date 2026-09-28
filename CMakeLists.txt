cmake_minimum_required(VERSION 3.5)

project(Music_App VERSION 0.1 LANGUAGES CXX)

set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# -------------------------------
# Qt setup
# -------------------------------
find_package(QT NAMES Qt6 Qt5 REQUIRED COMPONENTS Widgets Multimedia Concurrent)
find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Widgets Multimedia Concurrent)

# -------------------------------
# FFmpeg
# -------------------------------
# Point FFMPEG_ROOT at an FFmpeg dev install (the folder containing include/ and lib/):
#   cmake -DFFMPEG_ROOT=C:/ffmpeg ..      or set the FFMPEG_ROOT environment variable.
# Without it, standard locations are searched, then pkg-config.
set(FFMPEG_ROOT "$ENV{FFMPEG_ROOT}" CACHE PATH "FFmpeg install prefix (contains include/ and lib/)")
set(FFMPEG_COMPONENTS avformat avcodec avutil swresample)

set(_ffmpeg_hints)
if(FFMPEG_ROOT)
    list(APPEND _ffmpeg_hints "${FFMPEG_ROOT}")
endif()
if(WIN32)
    list(APPEND _ffmpeg_hints "C:/ffmpeg") # Common manual install location
endif()

find_path(FFMPEG_INCLUDE_DIR libavformat/avformat.h HINTS ${_ffmpeg_hints} PATH_SUFFIXES include)
set(FFMPEG_LIBRARIES)
set(_ffmpeg_found ${FFMPEG_INCLUDE_DIR})
foreach(_lib ${FFMPEG_COMPONENTS})
    find_library(FFMPEG_${_lib}_LIBRARY NAMES ${_lib} HINTS ${_ffmpeg_hints} PATH_SUFFIXES lib)
    if(FFMPEG_${_lib}_LIBRARY)
        list(APPEND FFMPEG_LIBRARIES ${FFMPEG_${_lib}_LIBRARY})
    else()
        set(_ffmpeg_found FALSE)
    endif()
endforeach()

if(NOT _ffmpeg_found)
    find_package(PkgConfig QUIET)
    if(PKG_CONFIG_FOUND)
        pkg_check_modules(FFMPEG_PC QUIET IMPORTED_TARGET libavformat libavcodec libavutil libswresample)
    endif()
    if(FFMPEG_PC_FOUND)
        set(FFMPEG_LIBRARIES PkgConfig::FFMPEG_PC)
        set(FFMPEG_INCLUDE_DIR "")
        set(_ffmpeg_found TRUE)
    endif()
endif()

if(NOT _ffmpeg_found)
    message(FATAL_ERROR
        "FFmpeg development files not found. Install FFmpeg (headers + import libraries) "
        "and configure with -DFFMPEG_ROOT=<path>, or set the FFMPEG_ROOT environment variable.")
endif()
message(STATUS "FFmpeg: ${FFMPEG_LIBRARIES}")

# -------------------------------
# Sources
# -------------------------------
set(PROJECT_SOURCES
    src/main.cpp
    src/mainwindow.cpp
    src/mainwindow.h
    src/mainwindow.ui
    src/appconfig.cpp
    src/appconfig.h
    src/theme.cpp
    src/theme.h
    src/resources/fonts.qrc
    src/resources/brand.qrc
    src/core/projectmodel.cpp
    src/core/projectmodel.h
    src/core/timescale.h
    src/core/audiobuffer.h
    src/core/instrumentsettings.h
    src/audio/audiodecoder.cpp
    src/audio/audiodecoder.h
    src/audio/effects.cpp
    src/audio/effects.h
    src/pianoroll/pianoroll.cpp
    src/pianoroll/pianoroll.h
    src/widgets/colorswatchpicker.cpp
    src/widgets/colorswatchpicker.h
    src/widgets/tempofield.cpp
    src/widgets/tempofield.h
    src/widgets/motion.cpp
    src/widgets/brandmark.cpp
    src/widgets/brandmark.h
    src/widgets/motion.h
    src/audio/audioerror.h
    src/audio/audioengine.cpp
    src/audio/audioengine.h
    src/audio/audioiodevice.cpp
    src/audio/audioiodevice.h
    src/audio/audioimportdialog.cpp
    src/audio/audioimportdialog.h
    src/transportdock.cpp
    src/transportdock.h
    src/timeline/timelinewidget.cpp
    src/timeline/timelinewidget.h
    src/timeline/clipregion.cpp
    src/timeline/musicalgrid.h
    src/timeline/timelineruler.cpp
    src/timeline/timelineruler.h
    src/timeline/timelineview.cpp
    src/timeline/timelineview.h
    src/timeline/clipregion.h
    src/timeline/tracklane.cpp
    src/timeline/tracklane.h
    src/timeline/TimelineIndicator.cpp
    src/timeline/TimelineIndicator.h
    src/timeline/trackheaderwidget.cpp
    src/timeline/trackheaderwidget.h
    src/timeline/tracksettingsdialog.cpp
    src/timeline/tracksettingsdialog.h
)

# Instruments: every .cpp in the folder registers itself, so new ones need no CMake edit
file(GLOB INSTRUMENT_SOURCES CONFIGURE_DEPENDS
    src/audio/instruments/*.cpp
    src/audio/instruments/*.h
)
list(APPEND PROJECT_SOURCES ${INSTRUMENT_SOURCES})

# Windows exe icon (the Bina mark; regenerate with tools/make_icon.py)
if(WIN32)
    list(APPEND PROJECT_SOURCES src/resources/bina.rc)
endif()

# -------------------------------
# Target
# -------------------------------
if(${QT_VERSION_MAJOR} GREATER_EQUAL 6)
    qt_add_executable(Music_App
        MANUAL_FINALIZATION
        ${PROJECT_SOURCES}
    )
else()
    add_executable(Music_App ${PROJECT_SOURCES})
endif()

# -------------------------------
# Linking
# -------------------------------
target_link_libraries(Music_App PRIVATE
    Qt${QT_VERSION_MAJOR}::Widgets
    Qt${QT_VERSION_MAJOR}::Multimedia
    Qt${QT_VERSION_MAJOR}::Concurrent
)

if(FFMPEG_INCLUDE_DIR)
    target_include_directories(Music_App PRIVATE ${FFMPEG_INCLUDE_DIR})
endif()
target_link_libraries(Music_App PRIVATE ${FFMPEG_LIBRARIES})
target_compile_definitions(Music_App PRIVATE HAVE_FFMPEG=1)


# -------------------------------
# Bundle / install
# -------------------------------
if(${QT_VERSION} VERSION_LESS 6.1.0)
  set(BUNDLE_ID_OPTION MACOSX_BUNDLE_GUI_IDENTIFIER com.example.Music_App)
endif()
set_target_properties(Music_App PROPERTIES
    OUTPUT_NAME Bina
    ${BUNDLE_ID_OPTION}
    MACOSX_BUNDLE_BUNDLE_VERSION ${PROJECT_VERSION}
    MACOSX_BUNDLE_SHORT_VERSION_STRING ${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR}
    MACOSX_BUNDLE TRUE
    WIN32_EXECUTABLE FALSE
)

include(GNUInstallDirs)
install(TARGETS Music_App
    BUNDLE DESTINATION .
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

if(QT_VERSION_MAJOR EQUAL 6)
    qt_finalize_executable(Music_App)
endif()

# -------------------------------
# Instrument test harness (not built by default): ninja instrument_render
# -------------------------------
add_executable(instrument_render EXCLUDE_FROM_ALL tools/instrument_render.cpp ${INSTRUMENT_SOURCES})
target_link_libraries(instrument_render PRIVATE Qt${QT_VERSION_MAJOR}::Core)
