#include "theme.h"

#include <QApplication>
#include <QStyleFactory>

namespace QtInterface {

Theme& Theme::Instance()
{
    static Theme instance;
    return instance;
}

Theme::Theme()
    : app(nullptr)
{
}

void Theme::ApplyToApp(QApplication& app_)
{
    app = &app_;
    app->setStyle(QStyleFactory::create("Fusion"));
    app->setPalette(GetPalette());
    app->setStyleSheet(GetStyleSheet());
    emit Changed();
}

void Theme::SetColors(const ThemeColors& colors_)
{
    colors = colors_;
    if(app) ApplyToApp(*app);
    else    emit Changed();
}

const ThemeColors& Theme::GetColors() const { return colors; }

QPalette Theme::GetPalette() const
{
    QPalette p;
    p.setColor(QPalette::Window,          colors.background);
    p.setColor(QPalette::WindowText,      colors.text);
    p.setColor(QPalette::Base,            colors.base);
    p.setColor(QPalette::AlternateBase,   colors.alternateBase);
    p.setColor(QPalette::Text,            colors.text);
    p.setColor(QPalette::Button,          colors.background);
    p.setColor(QPalette::ButtonText,      colors.text);
    p.setColor(QPalette::Highlight,       colors.selection);
    p.setColor(QPalette::HighlightedText, colors.selectionText);
    p.setColor(QPalette::ToolTipBase,     colors.base);
    p.setColor(QPalette::ToolTipText,     colors.text);
    p.setColor(QPalette::PlaceholderText, colors.mutedText);
    p.setColor(QPalette::Disabled, QPalette::Text,       colors.mutedText);
    p.setColor(QPalette::Disabled, QPalette::WindowText, colors.mutedText);
    p.setColor(QPalette::Disabled, QPalette::ButtonText, colors.mutedText);
    return p;
}

QString Theme::GetStyleSheet() const
{
    return QString(
        "QTabWidget::pane { border: 1px solid %1; top: -1px; }"
        "QTabBar::tab { background: %2; border: 1px solid %1; padding: 5px 14px; margin-right: 2px; }"
        "QTabBar::tab:selected { background: %3; border-bottom: 2px solid %4; }"
        "QHeaderView::section { background: %2; border: none; border-right: 1px solid %1;"
        "  border-bottom: 1px solid %1; padding: 4px 6px; font-weight: bold; }"
        "QTableView, QTreeView { gridline-color: %1; border: 1px solid %1; }"
        "QProgressBar { border: 1px solid %1; border-radius: 3px; background: %3; text-align: center; }"
        "QProgressBar::chunk { background: %4; border-radius: 2px; }"
        "QLineEdit { border: 1px solid %1; border-radius: 3px; padding: 3px 5px; background: %3; }"
        "QStatusBar { border-top: 1px solid %1; }"
    ).arg(colors.border.name(), colors.background.name(), colors.base.name(), colors.accent.name());
}

} // QtInterface
