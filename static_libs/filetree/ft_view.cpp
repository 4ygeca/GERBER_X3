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
#include "ft_view.h"

#include "ft_node.h"
#include "ft_sidedelegate.h"
#include "ft_textdelegate.h"
#include "ft_typedelegate.h"

#include "abstract_file.h"
#include "gc_plugin.h"
#include "gc_types.h"
#include "project.h"
#include "shapepluginin.h"

#include <QFileDialog>
#include <QFormLayout>
#include <QHeaderView>
#include <QLabel>
#include <QMenu>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPainter>
#include <QPushButton>
#include <QTimer>

#include <ranges>

namespace FileTree {

View::View(QWidget* parent)
    : QTreeView{parent} {
    setAlternatingRowColors(true);
    setAnimated(true);
    setUniformRowHeights(true);

    // Раскрыл или свернул пользователь (или reveal) -- запомнить в узле.
    // NOTE: expandAll() сигнала expanded по узлам не шлёт -- флаги разошлись бы с
    // видом, поэтому здесь раскрывают только через expand()/setExpanded().
    connect(this, &QTreeView::expanded, this, [this](const QModelIndex& index) {
        if(auto* node = static_cast<Node*>(index.internalPointer()); node && syncExpanded_)
            node->setExpanded(true);
    });
    connect(this, &QTreeView::collapsed, this, [this](const QModelIndex& index) {
        if(auto* node = static_cast<Node*>(index.internalPointer()); node && syncExpanded_)
            node->setExpanded(false);
    });

    App::fileTreeView.set(this);
}

View::~View() {
    App::fileTreeView.set(nullptr);
}

void View::reset() {
    QTreeView::reset();
    // Раскрытие вида пропало, а MainWindow::showEvent следом зовёт ещё и
    // expandAll(). Флаги узлов пережили и то и другое -- вернём их, когда
    // вызвавший отработает, а до тех пор вид их не трогает.
    syncExpanded_ = false;
    QTimer::singleShot(0, this, [this] {
        syncExpanded_ = true;
        if(model_) restoreExpanded({});
    });
}

void View::applyExpanded(const QModelIndex& index) {
    auto* node = static_cast<Node*>(index.internalPointer());
    if(!index.isValid() || !node) return;
    // Пустую папку -- тоже: группа заводится пустой и наполняется переносом
    // узлов, и раскрыть её потом было бы некому.
    if(node->childCount() || node->type_ == Type::Folder)
        setExpanded(index, node->isExpanded());
}

void View::restoreExpanded(const QModelIndex& index) {
    applyExpanded(index);
    for(int row{}, count = model_->rowCount(index); row < count; ++row)
        restoreExpanded(model_->index(row, 0, index));
}

void View::reveal(const QModelIndex& index) {
    if(!index.isValid()) return;
    // Раскрываем только цепочку предков -- свёрнутые пользователем соседние
    // группы не трогаем. Сам scrollTo() раскрывает предков лишь при
    // state() == NoState, поэтому явно.
    for(QModelIndex parent = index.parent(); parent.isValid(); parent = parent.parent())
        expand(parent);
    scrollTo(index);
}

void View::selectAndReveal(const QModelIndex& index) {
    if(!index.isValid()) return;
    reveal(index);
    selectionModel()->select(index, QItemSelectionModel::Rows | QItemSelectionModel::ClearAndSelect);
}

void View::updateIcons() {
    QModelIndex index = model_->index(0, 0, QModelIndex());
    int rowCount = static_cast<Node*>(index.internalPointer())->childCount();
    for(int r{}; r < rowCount; ++r)
        update(model_->index(r, 0, index));
}

void View::onSelectionChanged(const QItemSelection& selected, const QItemSelection& deselected) {
    if(!selected.indexes().isEmpty())
        for(auto& index: selected.indexes())
            model_->setData(index, true, Role::Select);
    if(!deselected.indexes().isEmpty())
        for(auto& index: deselected.indexes())
            model_->setData(index, false, Role::Select);
}

void View::hideOther() {
    // Видимой остаётся строка меню, соседние гаснут. Из вложенной папки (группа
    // УП, подпапка программы) -- и этажом выше: гаснут соседние группы и УП вне
    // групп, иначе «Hide other» у УП в группе прятал бы лишь соседей по группе.
    // Выше папки типа (строки корня) не поднимаемся -- там другие типы файлов.
    const QModelIndex target = menuIndex_.siblingAtColumn(0);
    for(QModelIndex keep = target; keep.isValid() && keep.parent().isValid(); keep = keep.parent()) {
        const QPersistentModelIndex parent = keep.parent();
        const int rowCount = model_->rowCount(parent);
        for(int row{}; row < rowCount; ++row) {
            const QModelIndex index = model_->index(row, 0, parent);
            auto* item = static_cast<Node*>(index.internalPointer());
            // Включается только сама строка меню: флажок папки -- видимость
            // всех её УП разом, и этажом выше он вернул бы погашенных соседей.
            if(row != keep.row())
                item->setData(index, Qt::Unchecked, Qt::CheckStateRole);
            else if(keep == target)
                item->setData(index, Qt::Checked, Qt::CheckStateRole);
        }
        QTimer::singleShot(100, this, [this, parent] {
            // Строки могли смениться, а папка -- исчезнуть: счёт -- заново.
            if(const int count = model_->rowCount(parent); parent.isValid() && count)
                emit model_->dataChanged(model_->index(0, 0, parent), model_->index(count - 1, 0, parent), {Qt::CheckStateRole});
        });
        if(static_cast<Node*>(parent.internalPointer())->type_ != Type::Folder) break; // строка внутри файла
    }
}

void View::closeFile() {
    model_->removeRow(menuIndex_.row(), menuIndex_.parent());
    // if (App::drillForm())
    // App::drillForm().on_pbClose_clicked();
}

void View::closeFiles() {
    model_->removeRows(0, childCount_, menuIndex_);
}

void View::closeAllFiles(uint32_t /*type*/) {
    // WARNING   model_->createIndex(0, 0, &model_->fileFolders[type]);
    model_->removeRows(0, childCount_, menuIndex_);
}

void View::setModel(QAbstractItemModel* model) {
    QTreeView::setModel(model_ = static_cast<Model*>(model));

    // Раскрытие строк -- как оставил пользователь (флаг узла, см. конструктор).
    // Прежде на каждую вставку, удаление и даже смену выделения звался
    // expandAll(), и выбор строки раскрывал все свёрнутые группы.
    //
    // Новые строки -- по их флагам (новый узел раскрыт), родитель -- по своему:
    // папка, заведённая пустой, при вставке в неё раскрывается, если не свёрнута.
    connect(model_, &Model::rowsInserted, this, [this](const QModelIndex& parent, int first, int last) {
        applyExpanded(parent);
        for(int row = first; row <= last; ++row)
            restoreExpanded(model_->index(row, 0, parent));
    });
    // Перенесённые строки вид помнит сам (раскрытие -- по QPersistentModelIndex),
    // а папке-приёмнику, заведённой пустой, нужен её флаг.
    connect(model_, &Model::rowsMoved, this,
        [this](const QModelIndex& /*source*/, int /*first*/, int /*last*/, const QModelIndex& destination, int /*row*/) {
            applyExpanded(destination);
        });
    connect(model_, &Model::updateActions, this, [this] { restoreExpanded({}); });
    connect(model_, &Model::select, this, &View::selectAndReveal);

    connect(selectionModel(), &QItemSelectionModel::selectionChanged, this, &View::onSelectionChanged);
    // Только F2. Умолчания (DoubleClicked, SelectedClicked) дрались бы с
    // mouseDoubleClickEvent: двойной клик по имени открывает форму плагина, а не
    // редактор, а одиночный по выделенной строке не должен делать ничего.
    setEditTriggers(QAbstractItemView::EditKeyPressed);

    header()->setStretchLastSection(false);
    header()->setSectionResizeMode(QHeaderView::Fixed);
    header()->setDefaultSectionSize(QFontMetrics(font()).size(Qt::TextSingleLine, u"123456789"_s).width()); // ~6 символов и ...
    header()->setSectionResizeMode(0, QHeaderView::Stretch);

    setItemDelegateForColumn(0, new TextDelegate{this});
    setItemDelegateForColumn(1, new SideDelegate{this});
    setItemDelegateForColumn(2, new TypeDelegate{this});

    setIconSize(QSize{24, 24});
}

void View::showExcellonDialog() { }

void View::contextMenuEvent(QContextMenuEvent* event) {
    menuIndex_ = indexAt(event->pos());
    qDebug() << menuIndex_;
    if(!menuIndex_.isValid())
        return;
    QMenu menu{this};
    childCount_ = static_cast<Node*>(menuIndex_.internalPointer())->childCount();
    if(menuIndex_.data(Role::NodeType).toInt() == Type::Folder && menuIndex_.parent().isValid()) {
        // Вложенная папка -- группа УП или подпапка программы: меню у неё своё.
        static_cast<Node*>(menuIndex_.internalPointer())->menu(menu, this);
    } else if(menuIndex_.data(Role::NodeType).toInt() == Type::Folder) {
        const uint32_t type = menuIndex_.data(Role::Id).value<uint32_t>(); // File Type
        const int nodeType = menuIndex_.data(Role::ContentType).toInt();
        if(App::filePlugins().contains(type))
            App::filePlugin(type)->createMainMenu(menu, this);
        else if(App::gCodePlugins().contains(type))
            App::gCodePlugin(type)->createMainMenu(menu, this);
        else if(nodeType == Type::AbstractShape)
            App::shapePlugins().begin()->second->createMainMenu(menu, this);
        else if(type == G_CODE)
            App::gCodePlugins().begin()->second->createMainMenu(menu, this);
    } else {
        reinterpret_cast<Node*>(menuIndex_.internalId())->menu(menu, this);
        if(auto selectedRows{selectionModel()->selectedRows().toVector()}; selectedRows.count() > 1) {
            menu.addSeparator();
            // TODO rename Action in future.
            menu.addAction(QIcon::fromTheme(u"edit-delete"_s), tr("Delete Selected"), [selectedRows, this]() mutable {
                r::sort(selectedRows, std::greater{}, &QModelIndex::row);
                for(auto&& index: selectedRows)
                    model_->removeRow(index.row(), index.parent());
            });
        }
        {
            auto selectedRows{selectionModel()->selectedRows().toVector()};
            if(selectedRows.empty())
                selectedRows.push_back(menuIndex_);
            auto file = App::project().file(selectedRows.front().data(FileTree::Id).toInt());
            if(file) {
                menu.addSeparator();
                menu.addAction(QIcon::fromTheme({}), tr("Transform"), [selectedRows, this]() mutable {
                    auto files = selectedRows
                        | v::transform(
                            [](auto&& index) { return App::project().file(index.data(FileTree::Id).toInt()); })
                        | v::filter(
                            [](auto&& file) { return file != nullptr; });
                    if(!r::empty(files))
                        TransformDialog({files.begin(), files.end()}, this).exec();
                });
            }
        }
    }

    if(!menu.isEmpty())
        menu.exec(viewport()->mapToGlobal(event->pos()));
}

// Редактор по одиночному клику здесь больше не открывается: он занимал место
// двойного клика по имени, а тот теперь открывает форму плагина. Переименование
// осталось на F2 (QAbstractItemView::EditKeyPressed, см. setModel), комбобоксы
// «Сторона» и «Тип» открываются двойным кликом ниже.
void View::mousePressEvent(QMouseEvent* event) { QTreeView::mousePressEvent(event); }

void View::mouseDoubleClickEvent(QMouseEvent* event) {
    menuIndex_ = indexAt(event->pos());
    if(!menuIndex_.isValid()) return QTreeView::mouseDoubleClickEvent(event);

    // Папка (группа УП, подпапка программы) сворачивается и раскрывается, как
    // в любом дереве. Прежде вложенная папка уходила ниже в hideOther() и гасила
    // соседние УП.
    if(static_cast<Node*>(menuIndex_.internalPointer())->type_ == Type::Folder)
        return QTreeView::mouseDoubleClickEvent(event);

    if(menuIndex_.column() != Column::NameColorVisible)
        return edit(menuIndex_), void(); // «Сторона» и «Тип» -- выпадающие списки

    // Файловый узел: его родитель -- папка типа, лежащая строкой в корне.
    if(menuIndex_.parent().row() > -1) {
        if(static_cast<Node*>(menuIndex_.internalPointer())->doubleClicked(this)) return;
        return hideOther();
    }

    QTreeView::mouseDoubleClickEvent(event);
}

} // namespace FileTree

#include "moc_ft_view.cpp"
