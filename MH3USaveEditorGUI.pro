QT += widgets sql network

TEMPLATE = app
TARGET = MH3USaveEditorGUI

CONFIG += c++11

CORE_DIR = "$$PWD/app/MH3U Save Editor/MH3U Save Editor"
GUI_DIR = "$$PWD/app/MH3U Save Editor GUI/MH3U Save Editor GUI"

DESTDIR = $$PWD/bin
OBJECTS_DIR = $$PWD/build/obj
MOC_DIR = $$PWD/build/moc
RCC_DIR = $$PWD/build/rcc

INCLUDEPATH += $$GUI_DIR \
               $$GUI_DIR/widget \
               $$CORE_DIR

SOURCES += "$$CORE_DIR/mh3u_ds.cpp" \
           "$$CORE_DIR/game_data_repository.cpp" \
           "$$CORE_DIR/equipment_validator.cpp" \
           "$$CORE_DIR/loadout.cpp" \
           "$$CORE_DIR/mh3u_se.cpp" \
           "$$CORE_DIR/mh3u_transfer.cpp" \
           "$$GUI_DIR/main.cpp" \
           "$$GUI_DIR/mh3u_sv.cpp" \
           "$$GUI_DIR/widget.cpp" \
           "$$GUI_DIR/widget/qarmor.cpp" \
           "$$GUI_DIR/widget/qbox.cpp" \
           "$$GUI_DIR/widget/qcharacter.cpp" \
           "$$GUI_DIR/widget/qcharm.cpp" \
           "$$GUI_DIR/widget/qchest.cpp" \
           "$$GUI_DIR/widget/qcommunity.cpp" \
           "$$GUI_DIR/widget/qequipment.cpp" \
           "$$GUI_DIR/widget/qinventory.cpp" \
           "$$GUI_DIR/widget/qitem.cpp" \
           "$$GUI_DIR/widget/qloadout.cpp" \
           "$$GUI_DIR/widget/qoption.cpp" \
           "$$GUI_DIR/widget/qpouch.cpp" \
           "$$GUI_DIR/widget/qweapon.cpp"

HEADERS += "$$CORE_DIR/main.hpp" \
           "$$CORE_DIR/game_data_repository.hpp" \
           "$$CORE_DIR/equipment_validator.hpp" \
           "$$CORE_DIR/loadout.hpp" \
           "$$CORE_DIR/mh3u_ds.hpp" \
           "$$CORE_DIR/mh3u_se.hpp" \
           "$$CORE_DIR/mh3u_transfer.hpp" \
           "$$GUI_DIR/main.hpp" \
           "$$GUI_DIR/searchable_combobox.hpp" \
           "$$GUI_DIR/mh3u_sv.hpp" \
           "$$GUI_DIR/widget.hpp" \
           "$$GUI_DIR/widget/qarmor.hpp" \
           "$$GUI_DIR/widget/qbox.hpp" \
           "$$GUI_DIR/widget/qcharacter.hpp" \
           "$$GUI_DIR/widget/qcharm.hpp" \
           "$$GUI_DIR/widget/qchest.hpp" \
           "$$GUI_DIR/widget/qcommunity.hpp" \
           "$$GUI_DIR/widget/qequipment.hpp" \
           "$$GUI_DIR/widget/qinventory.hpp" \
           "$$GUI_DIR/widget/qitem.hpp" \
           "$$GUI_DIR/widget/qloadout.hpp" \
           "$$GUI_DIR/widget/qoption.hpp" \
           "$$GUI_DIR/widget/qpouch.hpp" \
           "$$GUI_DIR/widget/qweapon.hpp"
