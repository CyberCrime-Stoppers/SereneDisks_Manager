cmake_minimum_required(VERSION 3.21)

project(SereneDiskManager VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

find_package(Qt6 6.5 REQUIRED COMPONENTS Quick QuickControls2 DBus Concurrent)

qt_standard_project_setup()

# Opt into the newer Qt CMake policies:
#   QTP0001 — ':/qt/qml/' default resource prefix for QML modules.
#   QTP0004 — auto-generate qmldir files for each subdirectory that holds
#             QML files (pages/, components/) — required for reliable
#             composite-type resolution in compiled modules.
qt_policy(SET QTP0001 NEW)
qt_policy(SET QTP0004 NEW)

# ---------------------------------------------------------------------------
# Sources
# ---------------------------------------------------------------------------
set(SDM_SOURCES
    src/main.cpp
    src/backend/StorageBackend.h
    src/backend/UDisks2Backend.h
    src/backend/UDisks2Backend.cpp
    src/backend/Operations.h
    src/backend/Operations.cpp
    src/JournalStore.h
    src/JournalStore.cpp
)

qt_add_executable(SereneDiskManager ${SDM_SOURCES})

target_link_libraries(SereneDiskManager PRIVATE
    Qt6::Quick
    Qt6::QuickControls2
    Qt6::DBus
    Qt6::Concurrent
)

# ---------------------------------------------------------------------------
# QML module — everything reachable from one URI "SDM"
# ---------------------------------------------------------------------------
# Singleton-ness must be declared BEFORE qt_add_qml_module consumes the
# files: it emits "singleton Theme Theme.qml" into the generated qmldir.
# (pragma Singleton alone in the file does NOT register it.)
set_source_files_properties(qml/Theme.qml PROPERTIES
    QT_QML_SINGLETON_TYPE TRUE
)

qt_add_qml_module(SereneDiskManager
    URI SDM
    VERSION 1.0
    QML_FILES
        qml/Main.qml
        qml/Theme.qml
        qml/pages/DevicesPage.qml
        qml/pages/JournalPage.qml
        qml/components/DeviceCard.qml
        qml/components/StatusPill.qml
        qml/components/UsageBar.qml
        qml/components/PartitionMap.qml
        qml/components/OperationsPopup.qml
)

# ---------------------------------------------------------------------------
# libblockdev (M2+: filesystem & partition surgery)
# ---------------------------------------------------------------------------
# find_package(PkgConfig REQUIRED)
# pkg_check_modules(BLOCKDEV IMPORTED_TARGET
#     blockdev blockdev-fs blockdev-part blockdev-crypto blockdev-utils)
# target_link_libraries(SereneDiskManager PRIVATE PkgTarget::BLOCKDEV)

include(GNUInstallDirs)
install(TARGETS SereneDiskManager RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
