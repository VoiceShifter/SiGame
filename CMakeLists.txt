cmake_minimum_required(VERSION 3.16)

project(SiGame VERSION 0.1 LANGUAGES CXX)

include(CTest)

set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(QT NAMES Qt6 Qt5 REQUIRED COMPONENTS Widgets Network WebSockets Multimedia MultimediaWidgets)
find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Widgets Network WebSockets Multimedia MultimediaWidgets)

set(PROJECT_SOURCES
        main.cpp
)
set(Directory "Images")
file(COPY ${Directory} DESTINATION ${CMAKE_BINARY_DIR})

set(CMAKE_INCLUDE_CURRENT_DIR true)
if(${QT_VERSION_MAJOR} GREATER_EQUAL 6)
    qt_add_executable(SiGame
        MANUAL_FINALIZATION
        ${PROJECT_SOURCES}
        mainwindow.h mainwindow.cpp mainwindow.ui
        singleplayerscreen.h singleplayerscreen.cpp singleplayerscreen.ui
        gamescreen.h gamescreen.cpp gamescreen.ui
        gamecontent.h gamecontent.cpp
        gameconfig.h
        playerstate.h
        playeridentity.h playeridentity.cpp
        packmanifest.h packmanifest.cpp
        multiplayerprotocol.h multiplayerprotocol.cpp
        multiplayerconnection.h multiplayerconnection.cpp
        pingworker.h pingworker.cpp
        gamesession.h gamesession.cpp
        multiplayerhost.h multiplayerhost.cpp
        multiplayerclient.h multiplayerclient.cpp
        multiplayerhostscreen.h multiplayerhostscreen.cpp
        multiplayerjoinscreen.h multiplayerjoinscreen.cpp

    )
# Define target properties for Android with Qt 6 as:
#    set_property(TARGET SiGame APPEND PROPERTY QT_ANDROID_PACKAGE_SOURCE_DIR
#                 ${CMAKE_CURRENT_SOURCE_DIR}/android)
# For more information, see https://doc.qt.io/qt-6/qt-add-executable.html#target-creation
else()
    if(ANDROID)
        add_library(SiGame SHARED
            ${PROJECT_SOURCES}
            mainwindow.h mainwindow.cpp mainwindow.ui
            singleplayerscreen.h singleplayerscreen.cpp singleplayerscreen.ui
            gamescreen.h gamescreen.cpp gamescreen.ui
            gamecontent.h gamecontent.cpp
            gameconfig.h playerstate.h
            playeridentity.h playeridentity.cpp
            packmanifest.h packmanifest.cpp
            multiplayerprotocol.h multiplayerprotocol.cpp
            multiplayerconnection.h multiplayerconnection.cpp
            pingworker.h pingworker.cpp
            gamesession.h gamesession.cpp
            multiplayerhost.h multiplayerhost.cpp
            multiplayerclient.h multiplayerclient.cpp
            multiplayerhostscreen.h multiplayerhostscreen.cpp
            multiplayerjoinscreen.h multiplayerjoinscreen.cpp
        )
# Define properties for Android with Qt 5 after find_package() calls as:
#    set(ANDROID_PACKAGE_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/android")
    else()
        add_executable(SiGame
            ${PROJECT_SOURCES}
            mainwindow.h mainwindow.cpp mainwindow.ui
            singleplayerscreen.h singleplayerscreen.cpp singleplayerscreen.ui
            gamescreen.h gamescreen.cpp gamescreen.ui
            gamecontent.h gamecontent.cpp
            gameconfig.h playerstate.h
            playeridentity.h playeridentity.cpp
            packmanifest.h packmanifest.cpp
            multiplayerprotocol.h multiplayerprotocol.cpp
            multiplayerconnection.h multiplayerconnection.cpp
            pingworker.h pingworker.cpp
            gamesession.h gamesession.cpp
            multiplayerhost.h multiplayerhost.cpp
            multiplayerclient.h multiplayerclient.cpp
            multiplayerhostscreen.h multiplayerhostscreen.cpp
            multiplayerjoinscreen.h multiplayerjoinscreen.cpp
        )
    endif()
endif()

target_sources(SiGame PRIVATE
    resources.qrc
    wasmpackimporter.h wasmpackimporter.cpp
    packvalidator.h packvalidator.cpp
    packvalidatorscreen.h packvalidatorscreen.cpp
)
if(EMSCRIPTEN)
    target_sources(SiGame PRIVATE wasm_resources.qrc)
    configure_file(Fonts/NotoColorEmoji.LICENSE.txt
        ${CMAKE_CURRENT_BINARY_DIR}/NotoColorEmoji.LICENSE.txt COPYONLY)
endif()

target_link_libraries(SiGame PRIVATE Qt${QT_VERSION_MAJOR}::Widgets
    Qt${QT_VERSION_MAJOR}::Network Qt${QT_VERSION_MAJOR}::WebSockets
    Qt${QT_VERSION_MAJOR}::Multimedia Qt${QT_VERSION_MAJOR}::MultimediaWidgets)

if(NOT EMSCRIPTEN AND NOT ANDROID AND NOT IOS)
    if(${QT_VERSION_MAJOR} GREATER_EQUAL 6)
        qt_add_executable(SiGameBridge
            MANUAL_FINALIZATION
            networkbridgemain.cpp
            networkbridgeserver.h networkbridgeserver.cpp
            multiplayerconnection.h multiplayerconnection.cpp
            multiplayerprotocol.h multiplayerprotocol.cpp
            playerstate.h
        )
    else()
        add_executable(SiGameBridge
            networkbridgemain.cpp
            networkbridgeserver.h networkbridgeserver.cpp
            multiplayerconnection.h multiplayerconnection.cpp
            multiplayerprotocol.h multiplayerprotocol.cpp
            playerstate.h
        )
    endif()
    target_link_libraries(SiGameBridge PRIVATE Qt${QT_VERSION_MAJOR}::Network
        Qt${QT_VERSION_MAJOR}::WebSockets)
endif()

# Qt for iOS sets MACOSX_BUNDLE_GUI_IDENTIFIER automatically since Qt 6.1.
# If you are developing for iOS or macOS you should consider setting an
# explicit, fixed bundle identifier manually though.
if(${QT_VERSION} VERSION_LESS 6.1.0)
  set(BUNDLE_ID_OPTION MACOSX_BUNDLE_GUI_IDENTIFIER com.example.SiGame)
endif()
set_target_properties(SiGame PROPERTIES
    ${BUNDLE_ID_OPTION}
    MACOSX_BUNDLE_BUNDLE_VERSION ${PROJECT_VERSION}
    MACOSX_BUNDLE_SHORT_VERSION_STRING ${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR}
    MACOSX_BUNDLE TRUE
    WIN32_EXECUTABLE TRUE
)

include(GNUInstallDirs)
set(SIGAME_INSTALL_TARGETS SiGame)
if(TARGET SiGameBridge)
    list(APPEND SIGAME_INSTALL_TARGETS SiGameBridge)
endif()
install(TARGETS ${SIGAME_INSTALL_TARGETS}
    BUNDLE DESTINATION .
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

if(BUILD_TESTING AND NOT EMSCRIPTEN AND NOT ANDROID AND NOT IOS)
    find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Test)
    add_executable(packvalidator_tests
        tests/test_packvalidator.cpp
        packvalidator.h packvalidator.cpp
        gamecontent.h gamecontent.cpp
    )
    target_link_libraries(packvalidator_tests PRIVATE
        Qt${QT_VERSION_MAJOR}::Test)
    add_test(NAME packvalidator_tests COMMAND packvalidator_tests)
endif()

if(QT_VERSION_MAJOR EQUAL 6)
    qt_finalize_executable(SiGame)
    if(TARGET SiGameBridge)
        qt_finalize_executable(SiGameBridge)
    endif()
endif()
