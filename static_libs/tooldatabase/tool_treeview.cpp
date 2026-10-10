// This is an open source non-commercial project. Dear PVS-Studio, please check it.
// PVS-Studio Static Code Analyzer for C, C++, C#, and Java: https://pvs-studio.com
/********************************************************************************
 * Author    :  Damir Bakiev                                                    *
 * Version   :  na                                                              *
 * Date      :  XXXXX XX, 2025                                                  *
 * Website   :  na                                                              *
 * Copyright :  Damir Bakiev 2016-2026                                          *
 * License   :                                                                  *
 * Use, modification & distribution is subject to Boost Software License Ver 1. *
 * http://www.boost.org/LICENSE_1_0.txt                                         *
 ********************************************************************************/
#include "tool_treeview.h"
#include "tool_item.h"

#include <QDropEvent>
#include <QFile>
#include <QHeaderView>
#include <QMessageBox>
#include <QPainter>
#include <QPushButton>

ToolTreeView::ToolTreeView(QWidget* parent)
    : QTreeView{parent} {
    setDragDropMode(QAbstractItemView::InternalMove);
    setDefaultDropAction(Qt::MoveAction);
    setAlternatingRowColors(true);
    setAnimated(true);

    model_ = new ToolModel{this};
    setModel(model_); // NOTE V1053. Calling the 'foo' virtual function in the constructor/destructor may lead to unexpected result at runtime.
    connect(selectionModel(), &QItemSelectionModel::selectionChanged, this, &ToolTreeView::updateActions);

    // Раскрытие групп -- как оставил пользователь (флаг ToolItem, хранится в tools.json),
    // дальше его меняет только он сам: выбор строки больше ничего не раскрывает.
    restoreExpanded({});
    // NOTE: expandAll() сигнала expanded по узлам не шлёт -- флаги разойдутся с видом,
    // раскрывать только через expand()/setExpanded()/expandRecursively().
    connect(this, &QTreeView::expanded, this, [](const QModelIndex& index) {
        if(auto* item = static_cast<ToolItem*>(index.internalPointer()); item)
            item->setExpanded(true);
    });
    connect(this, &QTreeView::collapsed, this, [](const QModelIndex& index) {
        if(auto* item = static_cast<ToolItem*>(index.internalPointer()); item)
            item->setExpanded(false);
    });
    // Группы теперь бывают свёрнуты -- при перетаскивании раскрываем по наведению.
    setAutoExpandDelay(600);

    header()->setSectionResizeMode(QHeaderView::ResizeToContents);
    header()->setSectionResizeMode(1, QHeaderView::Stretch);
    setMinimumWidth(400);
    setIconSize(QSize(24, 24));
}

void ToolTreeView::newGroup() {
    QModelIndex index = selectionModel()->currentIndex();
    if(index.data(Qt::UserRole).toInt())
        index = index.parent();
    if(!model_->insertRows(0, 1, index))
        return;
    index = model_->index(0, 0, index);
    model_->setData(index, tr("New Group"), Qt::EditRole);
    selectAndReveal(index);
}

void ToolTreeView::newTool() {
    QModelIndex index = selectionModel()->currentIndex();
    if(index.data(Qt::UserRole).toInt())
        index = index.parent();
    if(!model_->insertRows(index.data(Qt::UserRole + 1).toInt(), 1, index))
        return;

    ToolItem* item = nullptr;

    if(!index.isValid())
        item = static_cast<ToolItem*>(model_->index(0, 0, index).internalPointer());
    else
        item = static_cast<ToolItem*>(index.internalPointer())->lastChild();

    if(item) {
        item->setIsTool();
        item->tool().setName(tr("New Tool ") + QString::number(+item->toolId()));
        index = model_->createIndex(item->row(), 0, item);
        selectAndReveal(index);
    }
}

void ToolTreeView::deleteItem() {
    if(QMessageBox::question(this, tr("Warning"), tr("Are you sure you want to delete the item and all content?"), QMessageBox::Yes, QMessageBox::No) == QMessageBox::No)
        return;
    QModelIndex index = selectionModel()->currentIndex();
    if(model_->removeRows(index.row(), 1, index.parent()))
        updateActions();
}

void ToolTreeView::copyTool() {
    QModelIndex index = selectionModel()->currentIndex();
    ToolItem* itemSrc = static_cast<ToolItem*>(index.internalPointer());
    if(!itemSrc || !itemSrc->isTool()) // окно только открыто -- выбора нет
        return;
    if(!model_->insertRows(index.row() + 1, 1, index.parent()))
        return;

    index = index.sibling(index.row() + 1, 0);

    ToolItem* itemDst = static_cast<ToolItem*>(index.internalPointer());
    itemDst->setIsTool();
    const Tool::ID newId = itemDst->toolId();
    itemDst->tool() = itemSrc->tool();
    // Присваивание Tool целиком тащит и id источника -- вернуть только что
    // выданный свежий id, иначе копия дублирует id оригинала.
    itemDst->tool().setId(newId);
    selectAndReveal(index);
}

void ToolTreeView::updateActions() {
    QModelIndex index = selectionModel()->currentIndex();
    ToolItem* item = static_cast<ToolItem*>(index.internalPointer());
    buttons_[Delete]->setEnabled(!selectionModel()->selection().isEmpty());
    if(item) {
        buttons_[Copy]->setEnabled(item->isTool());
        emit itemSelected(item);
    } else
        buttons_[Copy]->setEnabled(false);
}

void ToolTreeView::setButtons(const std::vector<QPushButton*>& buttons) {
    buttons_ = buttons;
    connect(buttons_[Copy], &QPushButton::clicked, this, &ToolTreeView::copyTool);
    connect(buttons_[Delete], &QPushButton::clicked, this, &ToolTreeView::deleteItem);
    connect(buttons_[New], &QPushButton::clicked, this, &ToolTreeView::newTool);
    connect(buttons_[NewGroup], &QPushButton::clicked, this, &ToolTreeView::newGroup);
    updateActions();
}

void ToolTreeView::updateItem() {
    resizeColumnToContents(0);
    for(QModelIndex index: selectionModel()->selection().indexes())
        update(index);
}

bool ToolTreeView::selectTool(Tool::ID id) {
    if(id <= Tool::ID::Null) // Null == Folder -- такой id у любой группы
        return false;
    const QModelIndex index = findTool({}, id);
    selectAndReveal(index); // -> selectionChanged -> updateActions -> itemSelected
    return index.isValid();
}

void ToolTreeView::selectAndReveal(const QModelIndex& index) {
    if(!index.isValid())
        return;
    // Раскрываем только цепочку предков -- свёрнутые пользователем соседние группы не трогаем.
    // Сам scrollTo() раскрывает предков лишь при state() == NoState, поэтому явно.
    for(QModelIndex parent = index.parent(); parent.isValid(); parent = parent.parent())
        expand(parent);
    selectionModel()->setCurrentIndex(index, QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
    scrollTo(index);
}

QModelIndex ToolTreeView::findTool(const QModelIndex& parent, Tool::ID id) const {
    for(int row{}, count = model_->rowCount(parent); row < count; ++row) {
        const QModelIndex index = model_->index(row, 0, parent);
        if(auto* item = static_cast<const ToolItem*>(index.internalPointer()); item && item->toolId() == id)
            return index;
        if(QModelIndex found = findTool(index, id); found.isValid())
            return found;
    }
    return {};
}

void ToolTreeView::restoreExpanded(const QModelIndex& index) {
    if(index.isValid()) {
        auto* item = static_cast<ToolItem*>(index.internalPointer());
        if(!item || item->isTool())
            return;
        setExpanded(index, item->isExpanded());
    }
    for(int row{}, count = model_->rowCount(index); row < count; ++row)
        restoreExpanded(model_->index(row, 0, index));
}

void ToolTreeView::dropEvent(QDropEvent* event) {
    // ToolModel::dropMimeData: insertRows() (rowsInserted с ЗАГЛУШКОЙ), затем setChild()
    // подменяет заглушку копией и удаляет её. Поэтому здесь запоминаем только родителя и строку,
    // а индекс берём заново ПОСЛЕ базового dropEvent -- раскрыть индекс заглушки нельзя:
    // в expandedIndexes остался бы висячий internalPointer (use-after-free при следующем rowCount).
    QPersistentModelIndex parent;
    int row = -1;
    const auto connection = connect(model_, &QAbstractItemModel::rowsInserted, this,
        [&parent, &row](const QModelIndex& insertParent, int first, int /*last*/) {
            if(row < 0) {
                parent = insertParent;
                row = first;
            }
        });
    QTreeView::dropEvent(event);
    disconnect(connection);
    if(row < 0)
        return;
    // Источник удалится уже после выхода отсюда (QAbstractItemView::startDrag -> clearOrRemove),
    // QPersistentModelIndex в expandedIndexes сам сдвинет строки. По той же причине копию
    // здесь не выделять: clearOrRemove удаляет ВЫДЕЛЕННЫЕ строки -- ушла бы она, а не источник.
    if(parent.isValid())
        expand(parent); // группа-приёмник -- брошенное видно
    if(const QModelIndex dropped = model_->index(row, 0, parent); dropped.isValid())
        restoreExpanded(dropped); // перенесённая группа раскрыта, как была (флаги уехали с копией)
}

#include "moc_tool_treeview.cpp"
