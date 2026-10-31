# A remote frontend that reuses the upstream desktop window form and resources.
# Deliberately does not link qbt_base or instantiate a local BitTorrent session.
cmake_minimum_required(VERSION 3.16)
project(qBittorrentRemote VERSION 5.2.4 LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC_SEARCH_PATHS "${CMAKE_CURRENT_SOURCE_DIR}/../gui" "${CMAKE_CURRENT_BINARY_DIR}")
# Reuse the actual upstream property form. Only its promoted, session-bound file
# controller is replaced by its shared presentation view and remote content model.
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/../gui/properties/propertieswidget.ui")
file(READ "${CMAKE_CURRENT_SOURCE_DIR}/../gui/properties/propertieswidget.ui" propertiesForm)
string(REPLACE "TorrentContentWidget" "TorrentContentView" propertiesForm "${propertiesForm}")
string(REPLACE "gui/torrentcontentwidget.h" "gui/propertylistviews.h" propertiesForm "${propertiesForm}")
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/remoteproperties.ui" "${propertiesForm}")
find_package(Qt6 6.6 REQUIRED COMPONENTS Core Network Widgets Svg)
add_library(qbt_remote STATIC client.cpp client.h torrentmodel.cpp torrentmodel.h window.cpp window.h properties.cpp properties.h sidebar.cpp sidebar.h
    ../gui/propertylistviews.cpp ../gui/propertylistviews.h
    ../gui/properties/peerlistsortmodel.cpp ../gui/properties/peerlistsortmodel.h
    ../gui/trackerlist/trackerlistsortmodel.cpp ../gui/trackerlist/trackerlistsortmodel.h
    ../gui/trackerlist/trackerlistitemdelegate.cpp ../gui/trackerlist/trackerlistitemdelegate.h
    ../gui/torrentcontentitemdelegate.cpp ../gui/torrentcontentitemdelegate.h
    ../base/utils/size.cpp
    ../gui/properties/speedwidget.cpp ../gui/properties/speedwidget.h
    ../gui/properties/speedplotview.cpp ../gui/properties/speedplotview.h
    ../gui/properties/piecesbar.cpp ../gui/properties/piecesbar.h
    ../gui/properties/downloadedpiecesbar.cpp ../gui/properties/downloadedpiecesbar.h
    ../gui/properties/pieceavailabilitybar.cpp ../gui/properties/pieceavailabilitybar.h
    ../gui/transferlistfilterswidgetitem.cpp ../gui/transferlistfilterswidgetitem.h
    ../gui/hidabletabwidget.cpp ../gui/hidabletabwidget.h
    ../gui/properties/proptabbar.cpp ../gui/properties/proptabbar.h
    ../gui/progressbarpainter.cpp ../gui/progressbarpainter.h
    "${CMAKE_CURRENT_BINARY_DIR}/remoteproperties.ui")
target_compile_definitions(qbt_remote PUBLIC QBT_REMOTE_CLIENT QBT_USE_QCOLLATOR=1)
target_compile_definitions(qbt_remote PUBLIC QT_USE_QSTRINGBUILDER QT_NO_CAST_FROM_ASCII QT_NO_CAST_TO_ASCII)
target_link_libraries(qbt_remote PUBLIC Qt6::Core Qt6::Network Qt6::Widgets)
target_include_directories(qbt_remote PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}" "${CMAKE_CURRENT_SOURCE_DIR}/..")
qt_add_executable(qbittorrent-remote main.cpp ../icons/icons.qrc)
target_link_libraries(qbittorrent-remote PRIVATE qbt_remote Qt6::Svg)
if(WIN32)
    set_target_properties(qbittorrent-remote PROPERTIES WIN32_EXECUTABLE ON)
endif()
option(REMOTE_TESTING "Build remote frontend tests" OFF)
if(REMOTE_TESTING)
    enable_testing()
    find_package(Qt6 REQUIRED COMPONENTS Test)
    add_executable(test_remote tests/test_remote.cpp ../icons/icons.qrc)
    target_link_libraries(test_remote PRIVATE qbt_remote Qt6::Test Qt6::Svg)
    add_test(NAME remote COMMAND test_remote -platform offscreen)
endif()
# Reuse the upstream Russian catalog when Qt LinguistTools is available.
find_package(Qt6 QUIET COMPONENTS LinguistTools)
if(TARGET Qt6::lrelease AND COMMAND qt_add_translation)
    set_source_files_properties("${CMAKE_CURRENT_SOURCE_DIR}/../lang/qbittorrent_ru.ts"
        PROPERTIES OUTPUT_LOCATION "${CMAKE_CURRENT_BINARY_DIR}")
    qt_add_translation(REMOTE_QM "${CMAKE_CURRENT_SOURCE_DIR}/../lang/qbittorrent_ru.ts")
    file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/remote-lang.qrc"
        "<RCC><qresource prefix=\"/translations\"><file alias=\"qbittorrent_ru.qm\">${CMAKE_CURRENT_BINARY_DIR}/qbittorrent_ru.qm</file></qresource></RCC>")
    target_sources(qbittorrent-remote PRIVATE ${REMOTE_QM} "${CMAKE_CURRENT_BINARY_DIR}/remote-lang.qrc")
    if(REMOTE_TESTING)
        target_sources(test_remote PRIVATE ${REMOTE_QM} "${CMAKE_CURRENT_BINARY_DIR}/remote-lang.qrc")
        target_compile_definitions(test_remote PRIVATE REMOTE_HAS_TRANSLATIONS)
    endif()
endif()
install(TARGETS qbittorrent-remote RUNTIME DESTINATION bin)
