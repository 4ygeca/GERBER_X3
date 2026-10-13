/********************************************************************************
 * Author    :  Damir Bakiev                                                    *
 * Version   :  na                                                              *
 * Date      :  XXXXX XX, 2025                                                  *
 * Website   :  na                                                              *
 * Copyright :  Damir Bakiev 2016-2026                                          *
 * License:                                                                     *
 * Use, modification & distribution is subject to Boost Software License Ver 1. *
 * http://www.boost.org/LICENSE_1_0.txt                                         *
 ********************************************************************************/
#pragma once

#include "ggcore_export.h"

#include "ft_model.h"

#include <QTreeView>

namespace FileTree {

class GGCORE_EXPORT View : public QTreeView {
    Q_OBJECT
    friend class ::MainWindow;

public:
    explicit View(QWidget* parent = nullptr);
    ~View() override;
    void hideOther();
    void closeFile();
    void closeFiles();

    void closeAllFiles(uint32_t type);

    void setModel(QAbstractItemModel* model) override;
    // reset() забывает раскрытые строки; флаги узлов (Node::isExpanded) -- нет,
    // они и возвращаются.
    void reset() override;
    // Показать строку, не трогая выделения: раскрыть ровно её предков.
    void reveal(const QModelIndex& index);

signals:
    void saveGCodeFile(int32_t id);
    void saveGCodeFiles(); // NOTE unused
    void saveSelectedGCodeFiles();
    void editGCodeFile(int32_t id);

private:
    // Раскрыть или свернуть строку по флагу её узла; restoreExpanded -- с поддеревом.
    void applyExpanded(const QModelIndex& index);
    void restoreExpanded(const QModelIndex& index);
    // Выделить строку и показать её (reveal).
    void selectAndReveal(const QModelIndex& index);
    void updateIcons();
    Model* model_{};
    // Вид правит флаги узлов по своим expanded/collapsed. Между reset() и
    // возвратом флагов -- нет: expandAll() вслед за reset() их бы затёр.
    bool syncExpanded_{true};

    void onSelectionChanged(const QItemSelection& selected, const QItemSelection& deselected);
    int childCount_{};
    QModelIndex menuIndex_;
    void showExcellonDialog();

protected:
    void contextMenuEvent(QContextMenuEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;
};

} // namespace FileTree
