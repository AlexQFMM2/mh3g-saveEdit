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

    QFont font(QStringLiteral("Microsoft YaHei UI"));
    font.setStyleHint(QFont::SansSerif);
    font.setPointSize(10);
    app.setFont(font);

    app.setStyleSheet(R"STYLE(
        QWidget {
            background: #f4f7fb;
            color: #182033;
        }
        QLabel, QCheckBox, QRadioButton {
            background: transparent;
        }
        QLabel#appTitle {
            color: #15213a;
            font-size: 24px;
            font-weight: 700;
        }
        QLabel#appSubtitle {
            color: #69758a;
            font-size: 12px;
        }
        QLabel#sectionTitle {
            color: #344158;
            font-size: 12px;
            font-weight: 600;
        }
        QLabel#statusLabel {
            color: #6e788a;
            background: #eef2f7;
            border: 1px solid #dce3ed;
            border-radius: 8px;
            padding: 9px 12px;
        }
        QLabel#statusLabel[loaded="true"] {
            color: #17643a;
            background: #eaf8f0;
            border-color: #bce6cd;
        }
        QFrame#contentCard {
            background: #ffffff;
            border: 1px solid #dfe6f0;
            border-radius: 12px;
        }
        QPushButton {
            color: #26344c;
            background: #ffffff;
            border: 1px solid #cfd8e6;
            border-radius: 8px;
            padding: 8px 14px;
            min-height: 24px;
            font-weight: 500;
        }
        QPushButton:hover {
            color: #1858a8;
            background: #f4f8ff;
            border-color: #7fa9e3;
        }
        QPushButton:pressed {
            background: #e8f1ff;
            border-color: #4f88d3;
        }
        QPushButton:disabled {
            color: #9aa4b4;
            background: #f0f3f7;
            border-color: #e0e5ec;
        }
        QPushButton#navigationButton {
            text-align: left;
            padding: 11px 15px;
            min-height: 28px;
            font-weight: 600;
        }
        QPushButton#primaryButton {
            color: #ffffff;
            background: #2769bd;
            border-color: #2769bd;
            font-weight: 700;
        }
        QPushButton#primaryButton:hover {
            background: #1f5ca9;
            border-color: #1f5ca9;
        }
        QPushButton#saveButton {
            color: #ffffff;
            background: #23845a;
            border-color: #23845a;
            font-weight: 700;
        }
        QPushButton#saveButton:hover {
            background: #1b714c;
            border-color: #1b714c;
        }
        QPushButton#saveButton:disabled {
            color: #9aa4b4;
            background: #f0f3f7;
            border-color: #e0e5ec;
        }
        QLineEdit, QSpinBox, QDoubleSpinBox, QComboBox, QTextEdit, QPlainTextEdit {
            color: #1e293b;
            background: #ffffff;
            border: 1px solid #cfd8e6;
            border-radius: 7px;
            padding: 5px 8px;
            min-height: 22px;
            selection-background-color: #2d70c7;
        }
        QLineEdit:focus, QSpinBox:focus, QDoubleSpinBox:focus, QComboBox:focus,
        QTextEdit:focus, QPlainTextEdit:focus {
            border: 1px solid #4f88d3;
        }
        QComboBox::drop-down {
            border: 0;
            width: 24px;
        }
        QTableWidget, QTableView {
            color: #1e293b;
            background: #ffffff;
            alternate-background-color: #f7f9fc;
            border: 1px solid #d8e0eb;
            border-radius: 8px;
            gridline-color: #e5eaf1;
            selection-background-color: #2d70c7;
            selection-color: #ffffff;
        }
        QHeaderView::section {
            color: #344158;
            background: #edf2f8;
            border: 0;
            border-right: 1px solid #d8e0eb;
            border-bottom: 1px solid #d8e0eb;
            padding: 7px 8px;
            font-weight: 600;
        }
        QGroupBox {
            background: #ffffff;
            border: 1px solid #d8e0eb;
            border-radius: 9px;
            margin-top: 12px;
            padding-top: 9px;
            font-weight: 600;
        }
        QGroupBox::title {
            subcontrol-origin: margin;
            left: 10px;
            padding: 0 5px;
            background: #f4f7fb;
        }
        QScrollBar:vertical {
            background: transparent;
            width: 12px;
            margin: 2px;
        }
        QScrollBar::handle:vertical {
            background: #b8c3d2;
            min-height: 28px;
            border-radius: 5px;
        }
        QScrollBar::handle:vertical:hover {
            background: #93a3b8;
        }
        QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical {
            height: 0;
        }
        QToolTip {
            color: #ffffff;
            background: #26344c;
            border: 0;
            padding: 5px 7px;
        }
    )STYLE");
}

int main(int argc, char *argv[])
{
    QCoreApplication::setAttribute(Qt::AA_EnableHighDpiScaling);
    QCoreApplication::setAttribute(Qt::AA_UseHighDpiPixmaps);
    QApplication a(argc, argv);
    applyApplicationStyle(a);

    MH3U_SV w;
    w.show();

    return a.exec();
}
