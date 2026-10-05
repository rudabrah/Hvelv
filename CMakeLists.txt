cmake_minimum_required(VERSION 3.21)

project(Hvelv VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Qt6 6.8 REQUIRED COMPONENTS Core Gui Widgets Qml Quick QuickControls2 Concurrent Network Test)

qt_standard_project_setup(REQUIRES 6.8)

# --- Monocypher (vendret, se third_party/monocypher/VERSION.txt) ---
add_library(monocypher STATIC third_party/monocypher/monocypher.c)
target_include_directories(monocypher PUBLIC third_party/monocypher)

# --- Kjerne: ingen GUI, testbar for seg selv ---
add_library(hvelv_core STATIC
    src/core/crypto.h        src/core/crypto.cpp
    src/core/entry.h         src/core/entry.cpp
    src/core/entrytypes.h    src/core/entrytypes.cpp
    src/core/vaultdata.h     src/core/vaultdata.cpp
    src/core/importexport.h  src/core/importexport.cpp
    src/core/calendar.h      src/core/calendar.cpp
    src/core/health.h        src/core/health.cpp
    src/core/autotype.h      src/core/autotype.cpp
    src/core/vaultfile.h     src/core/vaultfile.cpp
    src/core/totp.h          src/core/totp.cpp
    src/core/passwordgenerator.h src/core/passwordgenerator.cpp
)
target_include_directories(hvelv_core PUBLIC src/core)
target_link_libraries(hvelv_core PUBLIC Qt6::Core PRIVATE monocypher)

# --- ZXing-cpp (vendret, se third_party/zxing-cpp/VERSION.txt): bare QR-leser ---
set(ZXING_READERS ON)
set(ZXING_WRITERS OFF)
set(ZXING_C_API OFF)
foreach(format 1D AZTEC DATAMATRIX MAXICODE PDF417)
    set(ZXING_ENABLE_${format} OFF CACHE BOOL "" FORCE)
endforeach()
set(ZXING_ENABLE_QRCODE ON CACHE BOOL "" FORCE)

option(HVELV_BUILD_APP "Bygg desktop-appen (av = bare kjerne og tester)" ON)

enable_testing()

if(HVELV_BUILD_APP)
    add_subdirectory(third_party/zxing-cpp/core EXCLUDE_FROM_ALL)
    set_target_properties(ZXing PROPERTIES AUTOMOC OFF AUTOUIC OFF AUTORCC OFF)

    # --- UI: QML-modulen "Hvelv" som statisk bibliotek, brukt av appen og UI-testen ---
    qt_add_library(hvelv_ui STATIC)
    qt_add_qml_module(hvelv_ui
        URI Hvelv
        VERSION 1.0
        SOURCES
            src/app/vaultcontroller.h  src/app/vaultcontroller.cpp
            src/app/entrymodel.h       src/app/entrymodel.cpp
            src/app/entryfiltermodel.h src/app/entryfiltermodel.cpp
            src/app/vaultcontroller_data.cpp
            src/app/vaultcontroller_transfer.cpp
            src/app/vaultcontroller_health.cpp
            src/app/netchecks.h        src/app/netchecks.cpp
            src/app/vaultcontroller_autotype.cpp
            src/app/globalhotkeys.h    src/app/globalhotkeys.cpp
            src/app/autotyper.h        src/app/autotyper.cpp
            src/app/qrscanner.h        src/app/qrscanner.cpp
            src/app/vaultcontroller_qr.cpp
            src/app/certificateinfo.h  src/app/certificateinfo.cpp
        QML_FILES
            qml/Main.qml
            qml/UnlockPage.qml
            qml/VaultPage.qml
            qml/EntryDetails.qml
            qml/EntryEditor.qml
            qml/PasswordGeneratorDialog.qml
            qml/CopyField.qml
            qml/SectionHeader.qml
            qml/ExpiryBadge.qml
            qml/TemplateDialog.qml
            qml/ContactEditDialog.qml
            qml/ContactsDialog.qml
            qml/ImportDialog.qml
            qml/ExportDialog.qml
            qml/HealthDialog.qml
            qml/AppDialog.qml
            qml/QuickSearchWindow.qml
    )
    target_include_directories(hvelv_ui PUBLIC src/app)
    target_link_libraries(hvelv_ui PUBLIC
        hvelv_core
        Qt6::Gui Qt6::Widgets Qt6::Qml Qt6::Quick Qt6::QuickControls2 Qt6::Concurrent Qt6::Network
    )
    target_link_libraries(hvelv_ui PRIVATE ZXing::ZXing)
    if(WIN32)
        target_link_libraries(hvelv_ui PUBLIC crypt32)
    endif()

    # --- Desktop-app ---
    # Ikonet (assets/) lages av tools/make_icon.cpp â€“ se mÃ¥let hvelv_make_icon nederst.
    qt_add_executable(hvelv WIN32 src/app/main.cpp assets/hvelv.rc)
    qt_add_resources(hvelv "app_icons"
        PREFIX "/"
        FILES
            assets/icon-16.png assets/icon-20.png assets/icon-24.png assets/icon-32.png
            assets/icon-40.png assets/icon-48.png assets/icon-64.png assets/icon-128.png
            assets/icon-256.png
    )
    target_compile_definitions(hvelv PRIVATE HVELV_VERSION="${PROJECT_VERSION}")
    target_link_libraries(hvelv PRIVATE hvelv_ui hvelv_uiplugin)
    # Egen mappe, sÃ¥ Qt-filene windeployqt kopierer ikke pÃ¥virker testprogrammene.
    set_target_properties(hvelv PROPERTIES
        OUTPUT_NAME Hvelv
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/app")

    # Kopier Qt-DLL-er, plugins og QML-moduler ved siden av exe-en, sÃ¥ den kan startes direkte.
    if(WIN32)
        add_custom_command(TARGET hvelv POST_BUILD
            COMMAND "${QT6_INSTALL_PREFIX}/${QT6_INSTALL_BINS}/windeployqt.exe"
                    --qmldir "${CMAKE_CURRENT_SOURCE_DIR}/qml"
                    --no-translations --no-compiler-runtime --no-system-d3d-compiler
                    "$<TARGET_FILE:hvelv>"
            COMMENT "windeployqt: kopierer Qt-avhengigheter"
            VERBATIM)
    endif()

    # --- UI-rÃ¸yktest (kjÃ¸rer offscreen, ingen ekte tastatur/mus) ---
    qt_add_executable(hvelv_ui_tests tests/tst_ui.cpp)
    target_link_libraries(hvelv_ui_tests PRIVATE hvelv_ui hvelv_uiplugin Qt6::Test)
    add_test(NAME hvelv_ui_tests COMMAND hvelv_ui_tests)
    set_tests_properties(hvelv_ui_tests PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QPA_FONTDIR=C:/Windows/Fonts")
endif()

# --- VerktÃ¸y: genererer assets/hvelv.ico og icon-*.png (kjÃ¸res manuelt ved designendring) ---
qt_add_executable(hvelv_make_icon tools/make_icon.cpp)
target_link_libraries(hvelv_make_icon PRIVATE Qt6::Gui)
set_target_properties(hvelv_make_icon PROPERTIES EXCLUDE_FROM_ALL ON)

# --- Kjernetester ---
qt_add_executable(hvelv_tests tests/tst_core.cpp)
target_link_libraries(hvelv_tests PRIVATE hvelv_core Qt6::Test)
add_test(NAME hvelv_tests COMMAND hvelv_tests)

qt_add_executable(hvelv_import_tests tests/tst_import.cpp)
target_link_libraries(hvelv_import_tests PRIVATE hvelv_core Qt6::Test)
add_test(NAME hvelv_import_tests COMMAND hvelv_import_tests)
