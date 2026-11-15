cmake_minimum_required(VERSION 3.16)

# Зависимости .cpp от заголовков — собственным сканером CMake, а не по выводу /showIncludes.
# Русский MSVC пишет "Примечание: включение файла:", и в NMake-сборке этот префикс до фильтра
# CMake доходит в другой кодировке — зависимости терялись, после правки .h зависимые .cpp
# не пересобирались (объекты с разным размером класса → вылеты).
set(CMAKE_DEPENDS_USE_COMPILER OFF)

project(krendtrademonitor LANGUAGES CXX)

# Qt 5.12 полностью поддерживает C++17; с C++20 его заголовки собираются плохо.
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_AUTOMOC ON)

# Путь к Qt можно переопределить: cmake -DCMAKE_PREFIX_PATH=...
if(NOT CMAKE_PREFIX_PATH)
    set(CMAKE_PREFIX_PATH "C:/prog/cpp/qt/forVS2019/5.12.12/msvc2017_64")
endif()

find_package(Qt5 5.12 REQUIRED COMPONENTS Widgets Network WebSockets)

# Общие флаги для всех целей проекта.
add_library(km_options INTERFACE)
target_compile_definitions(km_options INTERFACE
    QT_DEPRECATED_WARNINGS
    QT_DISABLE_DEPRECATED_BEFORE=0x050C00)
if(MSVC)
    target_compile_options(km_options INTERFACE /utf-8 /W3 /permissive- /Zc:__cplusplus
        "/FI${CMAKE_CURRENT_SOURCE_DIR}/cmake/qt5_msvc_compat.h")   # Qt 5.12 + новый MSVC STL
endif()

add_subdirectory(QtInterface)

add_executable(krendtrademonitor WIN32
    src/main.cpp
    src/appconfig.h
    src/appconfig.cpp
    src/botlink.h
    src/botlink.cpp
    src/mainwindow.h
    src/mainwindow.cpp
    src/market/candle.h
    src/market/candle.cpp
    src/market/candlechart.h
    src/market/candlechart.cpp
    src/market/symbolpicker.h
    src/market/symbolpicker.cpp
    src/market/marketview.h
    src/market/marketview.cpp
    src/bot/botview.h
    src/bot/botview.cpp
    src/serverlog/serverlogview.h
    src/serverlog/serverlogview.cpp
    src/advice/advicefile.h
    src/advice/advicefile.cpp
    src/advice/adviceview.h
    src/advice/adviceview.cpp
    src/advice/adviceeditor.h
    src/advice/adviceeditor.cpp
)
target_include_directories(krendtrademonitor PRIVATE src)
target_link_libraries(krendtrademonitor PRIVATE km_options QtInterface
    Qt5::Widgets Qt5::Network Qt5::WebSockets)
