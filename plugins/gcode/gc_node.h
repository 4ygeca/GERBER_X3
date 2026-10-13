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
#pragma once

#include "ft_node.h"

#include <QString>
#include <vector>

class AbstractFile;

namespace GCode {

class File;

class Node : public FileTree::Node {
    friend class ::AbstractFile;
    AbstractFile* file;

public:
    explicit Node(AbstractFile* file);
    ~Node() override;

    // Узел переживает замену УП на месте: объект файла меняется, а строка в
    // дереве остаётся той же. Без перепривязки внутри остался бы указатель на
    // уже удалённый файл.
    void setFile(AbstractFile* newFile) { file = newFile; }
    // УП узла: узлы этого класса заводит только GCode::File::node().
    File* gcodeFile() const;

    // FileTree::Node interface
    bool setData(const QModelIndex& index, const QVariant& value, int role) override;
    Qt::ItemFlags flags(const QModelIndex& index) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    void menu(QMenu& menu, FileTree::View* tv) override;
    bool doubleClicked(FileTree::View* tv) override;
    virtual int32_t id() const override;
};

// Папка в дереве УП. Двух родов:
//   * группа -- папка прямо в «GCode»; в ней лежат УП с File::group() == имени;
//   * программа -- подпапка многофайловой УП (у многоинструментного плагина
//     один запуск даёт по файлу на инструмент) с именем программы, в группе
//     либо прямо в «GCode».
// Обе заводит и убирает раскладка (layoutTree): папок без УП не бывает.
// Флажок папки -- видимость всех УП под ней разом.
class GroupNode final : public FileTree::Node {
public:
    enum Kind {
        Group,
        Program,
    };

    GroupNode(Kind kind, const QString& name);
    ~GroupNode() override;

    Kind kind() const { return kind_; }
    const QString& name() const { return name_; }
    void setName(const QString& name) { name_ = name; }

    // FileTree::Node interface
    QVariant data(const QModelIndex& index, int role) const override;
    bool setData(const QModelIndex& index, const QVariant& value, int role) override;
    Qt::ItemFlags flags(const QModelIndex& index) const override;
    void menu(QMenu& menu, FileTree::View* tv) override;

private:
    const Kind kind_;
    QString name_;
};

// Группа в проекте: порядок групп в дереве и их раскрытие. Кто в какой группе,
// хранит каждая УП сама (File::group), здесь -- только то, чего в УП нет.
struct GroupState {
    QString name;
    bool expanded{true};
};

// Разложить узлы УП в «GCode» по группам и программам. Правило одно и при
// расчёте, и при открытии проекта: УП с непустым File::group() -- в папку
// группы этого имени; внутри неё (или прямо в «GCode») многофайловая
// программа -- в подпапку своего имени, одиночная -- сама по себе. Ключ
// программы -- имя ВНУТРИ группы: одноимённые УП разных групп (Drilling зовёт
// их по инструменту) -- разные программы. Недостающие папки заводятся,
// опустевшие убираются, узлы не пересоздаются: с ними остаются выделение и
// раскрытие. states -- группы из проекта: заводятся первыми, в своём порядке
// и со своим раскрытием.
void layoutTree(const std::vector<GroupState>& states = {});
// Разложить и показать УП id: раскрыть ровно её предков и выделить.
void layoutAndReveal(int32_t id);
// Группы дерева по порядку -- для проекта (Project::save).
std::vector<GroupState> groupStates();

} // namespace GCode
