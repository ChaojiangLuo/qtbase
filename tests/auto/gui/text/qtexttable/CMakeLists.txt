# Generated from qtexttable.pro.

#####################################################################
## tst_qtexttable Test:
#####################################################################

qt_add_test(tst_qtexttable
    SOURCES
        tst_qtexttable.cpp
    PUBLIC_LIBRARIES
        Qt::Gui
        Qt::GuiPrivate
)

## Scopes:
#####################################################################

qt_extend_target(tst_qtexttable CONDITION TARGET Qt::Widgets
    PUBLIC_LIBRARIES
        Qt::Widgets
)
