# QtInterface — база для виджетов-компонентов монитора (наследник идей QtShell).
add_library(QtInterface STATIC
    QtInterface/core/basewidget.h
    QtInterface/core/basewidget.cpp
    QtInterface/core/logger.h
    QtInterface/core/logger.cpp
    QtInterface/style/theme.h
    QtInterface/style/theme.cpp
    QtInterface/models/tablecolumn.h
    QtInterface/models/basetablemodel.h
    QtInterface/delegates/cellcomponent.h
    QtInterface/delegates/delegate.h
    QtInterface/delegates/delegate.cpp
    QtInterface/delegates/cellcomponents.h
    QtInterface/delegates/cellcomponents.cpp
    QtInterface/components/tabwidget.h
    QtInterface/components/tabwidget.cpp
    QtInterface/components/baseitemviewwidget.h
    QtInterface/components/baseitemviewwidget.cpp
    QtInterface/components/tablewidget.h
    QtInterface/components/tablewidget.cpp
    QtInterface/components/treewidget.h
    QtInterface/components/treewidget.cpp
    QtInterface/components/scrollwidget.h
    QtInterface/components/scrollwidget.cpp
    QtInterface/components/logwidget.h
    QtInterface/components/logwidget.cpp
    QtInterface/net/apiclient.h
    QtInterface/net/apiclient.cpp
    QtInterface/net/poller.h
    QtInterface/net/poller.cpp
    QtInterface/net/streamclient.h
    QtInterface/net/streamclient.cpp
)
target_include_directories(QtInterface PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(QtInterface PUBLIC km_options Qt5::Widgets Qt5::Network Qt5::WebSockets)
