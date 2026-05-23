#include "theme.h"

#include <QApplication>
#include <QPalette>

namespace mornox::ide {

void ApplyDarkTheme(QApplication& app) {
    QPalette palette;
    palette.setColor(QPalette::Window, QColor(12, 14, 17));
    palette.setColor(QPalette::WindowText, QColor(228, 233, 241));
    palette.setColor(QPalette::Base, QColor(10, 12, 15));
    palette.setColor(QPalette::AlternateBase, QColor(18, 21, 26));
    palette.setColor(QPalette::ToolTipBase, QColor(24, 28, 34));
    palette.setColor(QPalette::ToolTipText, QColor(228, 233, 241));
    palette.setColor(QPalette::Text, QColor(228, 233, 241));
    palette.setColor(QPalette::Button, QColor(28, 32, 38));
    palette.setColor(QPalette::ButtonText, QColor(228, 233, 241));
    palette.setColor(QPalette::BrightText, QColor(255, 93, 106));
    palette.setColor(QPalette::Highlight, QColor(45, 206, 198));
    palette.setColor(QPalette::HighlightedText, QColor(5, 13, 15));
    app.setPalette(palette);

    app.setStyleSheet(R"(
        QMainWindow, QWidget {
            background: #0c0e11;
            color: #e4e9f1;
            font-family: "Segoe UI", "Inter", sans-serif;
            font-size: 12px;
        }
        QMenuBar {
            background: #0b0d10;
            border-bottom: 1px solid #242a32;
            color: #aeb8c6;
            padding: 2px 6px;
        }
        QMenuBar::item:selected {
            background: #182026;
            color: #e8f8f7;
        }
        QMenu {
            background: #15191f;
            border: 1px solid #303844;
            color: #dce5ef;
        }
        QFrame#MissionBar {
            background: #11151a;
            border-bottom: 1px solid #27303a;
        }
        QFrame#WorkspaceRail {
            background: #0b0d10;
            border-right: 1px solid #252b34;
        }
        QFrame#InspectorPanel, QFrame#OperationLedger {
            background: #11151a;
            border: 1px solid #252d37;
        }
        QWidget#LeftContent, QWidget#TaskSurface {
            background: #0f1216;
        }
        QLabel#AppTitle {
            color: #f4f7fb;
            font-size: 16px;
            font-weight: 700;
        }
        QLabel#MissionEyebrow, QLabel#SectionEyebrow {
            color: #7d8998;
            font-size: 10px;
            font-weight: 700;
            letter-spacing: 0px;
        }
        QLabel#MissionPill {
            background: #102c2e;
            color: #bff7f2;
            border: 1px solid #2dcec6;
            border-radius: 6px;
            padding: 6px 10px;
            font-weight: 700;
        }
        QPushButton#MissionPill {
            background: #102c2e;
            color: #bff7f2;
            border: 1px solid #2dcec6;
            border-radius: 6px;
            padding: 7px 12px;
            font-weight: 700;
            text-align: left;
        }
        QPushButton#MissionPill:hover {
            background: #12383b;
            border-color: #39e1d8;
        }
        QLabel#SurfaceTitle {
            color: #f4f7fb;
            font-size: 18px;
            font-weight: 700;
        }
        QLabel#SurfaceSubtitle {
            color: #92a0b2;
        }
        QLabel#PanelTitle {
            color: #f0f4fb;
            font-size: 13px;
            font-weight: 700;
        }
        QLabel#PanelMeta {
            color: #8793a3;
            font-size: 11px;
        }
        QLabel#MetricLabel {
            background: #141922;
            border: 1px solid #27313d;
            border-radius: 6px;
            color: #d7e0ea;
            padding: 8px 10px;
        }
        QLabel#InlinePill {
            background: #10161d;
            border: 1px solid #27313d;
            border-radius: 5px;
            color: #91a0b0;
            padding: 3px 9px;
        }
        QFrame#StructuredItem {
            background: #10161d;
            border: 1px solid #27313d;
            border-radius: 7px;
        }
        QFrame#StructuredItem QWidget#StructuredTextColumn,
        QFrame#StructuredItem QLabel {
            background: transparent;
            border: 0;
        }
        QLabel#ItemRole {
            color: #f0f4fb;
            font-weight: 700;
        }
        QLabel#ItemDetail {
            color: #9aa6b7;
            font-size: 11px;
        }
        QLabel#ItemText {
            color: #cbd8e6;
            font-size: 11px;
        }
        QLabel#StatusBadgeOk, QLabel#StatusBadgeWarn, QLabel#StatusBadgeError, QLabel#StatusBadgeIdle {
            border-radius: 5px;
            padding: 1px 6px;
            font-size: 9px;
            font-weight: 700;
        }
        QLabel#StatusBadgeOk {
            background: #102c2e;
            border: 1px solid #2dcec6;
            color: #bff7f2;
        }
        QLabel#StatusBadgeWarn {
            background: #2c2414;
            border: 1px solid #856d31;
            color: #f0c56a;
        }
        QLabel#StatusBadgeError {
            background: #32181d;
            border: 1px solid #8d3945;
            color: #ff7680;
        }
        QLabel#StatusBadgeIdle {
            background: #151b23;
            border: 1px solid #303946;
            color: #9aa6b7;
        }
        QLabel#TestSummary, QLabel#SourceHeader {
            background: #141922;
            border: 1px solid #2a3542;
            border-radius: 7px;
            color: #dce5ef;
            padding: 10px 12px;
        }
        QLabel#FailureCard {
            background: #151820;
            border: 1px solid #8d3945;
            border-radius: 7px;
            color: #f2dce0;
            padding: 9px 11px;
        }
        QLabel#AnnotationCard {
            background: #111923;
            border: 1px solid #30475a;
            border-radius: 7px;
            color: #dce5ef;
            padding: 10px 11px;
        }
        QPlainTextEdit#OutputBlock {
            background: #0b0e12;
            border: 1px solid #27313d;
            border-radius: 7px;
            color: #d8e0e9;
            padding: 8px;
        }
        QPlainTextEdit#CodePreview {
            background: #080b0f;
            border: 1px solid #27313d;
            border-radius: 7px;
            color: #d8e0e9;
            padding: 8px;
        }
        QLabel#LedgerDetail {
            background: #10161d;
            border: 1px solid #27313d;
            border-radius: 7px;
            color: #d7e0ea;
            padding: 10px 12px;
        }
        QLineEdit {
            background: #080a0d;
            border: 1px solid #303946;
            border-radius: 7px;
            color: #eff5fb;
            padding: 8px 12px;
            selection-background-color: #2dcec6;
        }
        QLineEdit:focus {
            border-color: #2dcec6;
            background: #0a0d10;
        }
        QPushButton, QToolButton {
            background: #181d24;
            border: 1px solid #303946;
            border-radius: 6px;
            color: #dce5ef;
            padding: 7px 10px;
        }
        QPushButton#CommandButton {
            background: #151a21;
            border: 1px solid #303946;
            color: #dce5ef;
            padding: 7px 10px;
        }
        QPushButton#CommandButton:hover {
            background: #1b222b;
            border-color: #2dcec6;
        }
        QPushButton#WindowButton {
            background: transparent;
            border: 1px solid transparent;
            color: #8f9bac;
            padding: 5px;
        }
        QPushButton#WindowButton:hover {
            background: #1b222a;
            border-color: #303946;
            color: #f0f4fb;
        }
        QPushButton:hover, QToolButton:hover {
            background: #202731;
            border-color: #2dcec6;
        }
        QToolButton:checked {
            background: #102c2e;
            border-color: #2dcec6;
            color: #bff7f2;
        }
        QToolButton#RailButton {
            background: transparent;
            border: 1px solid transparent;
            border-radius: 8px;
            color: #7f8a99;
            padding: 5px;
        }
        QToolButton#RailButton:hover {
            background: #141a20;
            border-color: #27313d;
            color: #dce5ef;
        }
        QToolButton#RailButton:checked {
            background: #102c2e;
            border-color: #2dcec6;
            color: #bff7f2;
        }
        QTreeWidget, QListWidget, QTableWidget, QPlainTextEdit {
            background: #0b0e12;
            border: 1px solid #242c35;
            border-radius: 7px;
            color: #dde6ef;
            selection-background-color: #153f42;
            selection-color: #e9fbfa;
        }
        QListWidget::item, QTreeWidget::item {
            border-radius: 5px;
            padding: 5px 6px;
        }
        QListWidget::item:selected, QTreeWidget::item:selected {
            background: #153f42;
            color: #e9fbfa;
        }
        QTableWidget {
            gridline-color: #1e252d;
        }
        QTableWidget#LedgerTable {
            background: #0f141a;
            border: 1px solid #27313d;
            border-radius: 7px;
        }
        QTableWidget#LedgerTable::item {
            padding: 2px 6px;
        }
        QTableWidget#LedgerTable::item:selected {
            background: #173640;
            color: #e9fbfa;
        }
        QHeaderView::section {
            background: #121720;
            border: 0;
            border-bottom: 1px solid #252d37;
            color: #8f9bac;
            padding: 6px;
        }
        QTabWidget::pane {
            border: 1px solid #242c35;
            border-radius: 8px;
            background: #0a0c10;
            top: -1px;
        }
        QTabBar::tab {
            background: #10151b;
            border: 1px solid #242c35;
            border-radius: 7px;
            color: #9aa6b7;
            padding: 8px 13px;
            margin-right: 5px;
        }
        QTabBar::tab:selected {
            background: #102c2e;
            border-color: #2dcec6;
            color: #bff7f2;
        }
        QTabBar::tab:hover {
            border-color: #3d4956;
            color: #dce5ef;
        }
        QStatusBar {
            background: #0b0d10;
            color: #96a2b2;
            border-top: 1px solid #242c35;
        }
        QSplitter::handle {
            background: #171c22;
        }
        QScrollBar:vertical, QScrollBar:horizontal {
            background: #0b0e12;
            border: 0;
            width: 10px;
            height: 10px;
        }
        QScrollBar::handle {
            background: #303946;
            border-radius: 5px;
        }
        QScrollBar::handle:hover {
            background: #455160;
        }
        QScrollBar::add-line, QScrollBar::sub-line {
            width: 0;
            height: 0;
        }
    )");
}

}
