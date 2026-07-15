#include "main.hpp"

#include "mh3u_sv.hpp"
#include "widget/qinventory.hpp"
#include "widget/qpouch.hpp"
#include "widget/qchest.hpp"
#include "widget/qbox.hpp"

#include <QApplication>
#include <QFont>
#include <QStyleFactory>

static void applyApplicationStyle(QApplication &app)
{
    app.setStyle(QStyleFactory::create("Fusion"));

    QFont font = app.font();
    font.setPointSize(10);
    app.setFont(font);

    app.setStyleSheet(
        "QWidget {"
        "  background: #f6f7f9;"
        "  color: #1f2933;"
        "}"
        "QPushButton {"
        "  background: #ffffff;"
        "  border: 1px solid #c8ced8;"
        "  border-radius: 4px;"
        "  padding: 5px 10px;"
        "  min-height: 20px;"
        "}"
        "QPushButton:hover {"
        "  background: #eef4ff;"
        "  border-color: #8fb4e8;"
        "}"
        "QPushButton:pressed {"
        "  background: #dceafe;"
        "}"
        "QPushButton:disabled {"
        "  color: #9299a3;"
        "  background: #edf0f3;"
        "  border-color: #d8dde4;"
        "}"
        "QLineEdit, QSpinBox, QComboBox {"
        "  background: #ffffff;"
        "  border: 1px solid #c8ced8;"
        "  border-radius: 3px;"
        "  padding: 3px 6px;"
        "  min-height: 20px;"
        "}"
        "QComboBox::drop-down {"
        "  border: 0;"
        "  width: 22px;"
        "}"
        "QTableWidget, QTableView {"
        "  background: #ffffff;"
        "  alternate-background-color: #f1f3f6;"
        "  gridline-color: #d4d9e1;"
        "  selection-background-color: #2f7fd3;"
        "  selection-color: #ffffff;"
        "}"
        "QHeaderView::section {"
        "  background: #e8ecf1;"
        "  color: #1f2933;"
        "  border: 0;"
        "  border-right: 1px solid #c9cfd8;"
        "  border-bottom: 1px solid #c9cfd8;"
        "  padding: 5px 6px;"
        "  font-weight: 600;"
        "}"
        "QScrollBar:vertical {"
        "  background: #edf0f3;"
        "  width: 13px;"
        "  margin: 0;"
        "}"
        "QScrollBar::handle:vertical {"
        "  background: #b5beca;"
        "  min-height: 24px;"
        "  border-radius: 6px;"
        "}"
        "QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical {"
        "  height: 0;"
        "}"
        "QGroupBox {"
        "  border: 1px solid #d6dbe3;"
        "  border-radius: 4px;"
        "  margin-top: 10px;"
        "  padding-top: 8px;"
        "}"
        "QGroupBox::title {"
        "  subcontrol-origin: margin;"
        "  left: 8px;"
        "  padding: 0 4px;"
        "}"
    );
}

int main(int argc, char *argv[])
{
    QApplication a(argc, argv);
    applyApplicationStyle(a);

    MH3U_SV w;
    w.show();

    return a.exec();
}
