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
#include "gc_node.h"
#include "abstract_file.h"
#include "gc_file.h"
#include "gc_programdialog.h"

#include "app.h"
#include "ft_model.h"
#include "project.h"

#include <QFileInfo>
#include <QIcon>
#include <QInputDialog>
#include <QMenu>
#include <set>

#include "ft_view.h"

namespace GCode {

namespace {

// Папка «GCode»; nullptr, пока в проекте нет ни одной УП.
FileTree::Node* programRoot() { return App::fileModel().folder(G_CODE); }

// Лежит ли узел под «GCode»: выделение захватывает и прочие файлы, а
// отладочные УП живут в своей папке.
bool inProgramTree(const FileTree::Node* node) {
    const FileTree::Node* root = programRoot();
    for(; root && node; node = node->parent())
        if(node == root) return true;
    return false;
}

// Узлы УП под folder -- в порядке дерева.
void collectProgramNodes(const FileTree::Node* folder, std::vector<Node*>& nodes) {
    for(int row{}; row < folder->childCount(); ++row) {
        FileTree::Node* child = folder->child(row);
        if(auto* node = dynamic_cast<Node*>(child))
            nodes.push_back(node);
        else if(dynamic_cast<GroupNode*>(child))
            collectProgramNodes(child, nodes);
    }
}

// Папка рода kind с именем name среди детей parent.
GroupNode* findGroupNode(const FileTree::Node* parent, GroupNode::Kind kind, const QString& name) {
    for(int row{}; row < parent->childCount(); ++row)
        if(auto* folder = dynamic_cast<GroupNode*>(parent->child(row));
            folder && folder->kind() == kind && folder->name() == name)
            return folder;
    return nullptr;
}

// Папки групп -- в порядке дерева.
std::vector<GroupNode*> programGroups() {
    std::vector<GroupNode*> groups;
    if(FileTree::Node* root = programRoot())
        for(int row{}; row < root->childCount(); ++row)
            if(auto* folder = dynamic_cast<GroupNode*>(root->child(row)); folder && folder->kind() == GroupNode::Group)
                groups.push_back(folder);
    return groups;
}

File* programFile(int32_t id) { return dynamic_cast<File*>(App::project().file(id)); }

// УП группы -- по полю File::group(), а не по папке.
std::vector<int32_t> groupProgramIds(const QString& group) {
    std::vector<int32_t> ids;
    if(FileTree::Node* root = programRoot()) {
        std::vector<Node*> nodes;
        collectProgramNodes(root, nodes);
        for(Node* node: nodes)
            if(node->gcodeFile()->group() == group) ids.push_back(node->id());
    }
    return ids;
}

// Перенести УП в группу; пустое имя -- из группы прямо в «GCode». Берёт id,
// а не указатели: пока открыт диалог имени, успевает прийти расчёт, и
// заменённая на месте УП -- уже другой объект под тем же id.
void moveProgramsToGroup(const std::vector<int32_t>& ids, const QString& group) {
    bool changed{};
    for(int32_t id: ids)
        if(File* file = programFile(id); file && file->group() != group) {
            file->setGroup(group);
            changed = true;
        }
    if(!changed) return;
    layoutTree();
    App::project().setChanged();
    // Перенесённые УП -- показать: раскрыть ровно их папки. Выделение едет
    // вместе со строками, его не трогаем.
    if(auto* view = App::fileTreeView.ptr())
        for(int32_t id: ids)
            if(File* file = programFile(id)) view->reveal(file->node()->index());
}

void renameProgramGroup(const QString& from, const QString& to) {
    FileTree::Node* root = programRoot();
    if(!root) return;
    // Папку -- переименовать на месте: с ней остаются её строка и раскрытие.
    // Если группа с новым именем уже есть, раскладка сольёт обе в неё.
    if(auto* folder = findGroupNode(root, GroupNode::Group, from);
        folder && !findGroupNode(root, GroupNode::Group, to)) {
        folder->setName(to);
        App::fileModel().updateNode(folder);
    }
    moveProgramsToGroup(groupProgramIds(from), to);
}

// Имя для новой группы: имя общего исходного файла выбранных УП, иначе
// «Группа N». Занятое -- с номером: «Group Selected» заводит НОВУЮ группу.
QString newProgramGroupName(const std::vector<int32_t>& ids) {
    std::set<int32_t> sources;
    for(int32_t id: ids) {
        const File* file = programFile(id);
        if(!file) continue;
        const auto& params = file->params().params;
        const auto it = params.find(Params::GrItems);
        const auto* items = it != params.end() ? std::get_if<UsedItems>(&static_cast<const V&>(it->second)) : nullptr;
        if(!items || items->empty()) {
            sources.insert(-1); // источник неизвестен (сверловка Drilling) -- общего нет
            continue;
        }
        for(const auto& entry: *items) // ключ -- пара {id файла, тип элементов}
            if(!entry.first.empty()) sources.insert(entry.first.front());
    }

    std::vector<QString> taken;
    for(const GroupNode* group: programGroups())
        taken.push_back(group->name());
    QString base;
    if(sources.size() == 1 && *sources.begin() > -1)
        if(const AbstractFile* source = App::project().file(*sources.begin()))
            base = source->shortName();
    if(base.isEmpty()) {
        int number{};
        do base = QObject::tr("Group %1").arg(++number);
        while(r::contains(taken, base));
        return base;
    }
    QString name = base;
    for(int number = 2; r::contains(taken, name); ++number)
        name = u"%1 (%2)"_s.arg(base, QString::number(number));
    return name;
}

// УП под действием меню: выделенные строки, а если щёлкнули мимо выделения --
// одна та, по которой щёлкнули. Файл многофайловой программы тянет её всю:
// программа -- одна строка дерева, разносить её по группам нельзя.
std::vector<int32_t> menuProgramIds(FileTree::View* tv, FileTree::Node* clicked) {
    std::vector<FileTree::Node*> picked;
    for(const QModelIndex& index: tv->selectionModel()->selectedRows())
        picked.push_back(static_cast<FileTree::Node*>(index.internalPointer()));
    if(!r::contains(picked, clicked)) picked = {clicked};

    std::vector<int32_t> ids;
    for(FileTree::Node* node: picked) {
        if(!node || !inProgramTree(node)) continue;
        if(auto* program = dynamic_cast<GroupNode*>(node->parent()); program && program->kind() == GroupNode::Program)
            node = program;
        std::vector<Node*> nodes;
        if(auto* gcNode = dynamic_cast<Node*>(node))
            nodes.push_back(gcNode);
        else
            collectProgramNodes(node, nodes);
        for(Node* gcNode: nodes)
            if(!r::contains(ids, gcNode->id())) ids.push_back(gcNode->id());
    }
    return ids;
}

// Пункты групп в меню УП и подпапки программы.
void addGroupActions(QMenu& menu, FileTree::View* tv, FileTree::Node* clicked) {
    const std::vector<int32_t> ids = menuProgramIds(tv, clicked);
    if(ids.empty()) return;

    std::set<QString> ownGroups; // где лежат выбранные УП; пустое имя -- вне групп
    for(int32_t id: ids)
        if(const File* file = programFile(id)) ownGroups.insert(file->group());

    auto newGroup = [tv, ids] {
        bool ok{};
        const QString name = QInputDialog::getText(tv, QObject::tr("New Group"), QObject::tr("Group name:"),
            QLineEdit::Normal, newProgramGroupName(ids), &ok)
                                 .trimmed();
        if(ok && !name.isEmpty()) moveProgramsToGroup(ids, name);
    };

    menu.addSeparator();
    menu.addAction(QIcon::fromTheme(u"folder"_s), QObject::tr("&Group Selected..."), newGroup);
    QMenu* moveMenu = menu.addMenu(QObject::tr("&Move to Group"));
    const std::vector<GroupNode*> groups = programGroups();
    for(const GroupNode* group: groups) {
        const QString name = group->name();
        QString text = name;
        text.replace(u'&', u"&&"_s); // '&' в имени -- не мнемоника
        QAction* action = moveMenu->addAction(QIcon::fromTheme(u"folder"_s), text, [ids, name] { moveProgramsToGroup(ids, name); });
        // Все выбранные уже в этой группе -- переносить некуда.
        action->setEnabled(ownGroups.size() != 1 || *ownGroups.begin() != name);
    }
    if(!groups.empty()) moveMenu->addSeparator();
    moveMenu->addAction(QObject::tr("&New Group..."), newGroup);
    if(r::any_of(ownGroups, [](const QString& group) { return !group.isEmpty(); }))
        menu.addAction(QObject::tr("&Remove from Group"), [ids] { moveProgramsToGroup(ids, {}); });
}

} // namespace

Node::Node(AbstractFile* file)
    : FileTree::Node(FileTree::File)
    , file(file) {
}

Node::~Node() {
    // Файла больше нет -- смотреть в окне просмотра нечего.
    Dialog::programClosed(file->id());
    App::project().deleteFile(file->id());
}

bool Node::setData(const QModelIndex& index, const QVariant& value, int role) {

    switch(index.column()) {
    case FileTree::Column::NameColorVisible:
        switch(role) {
        case Qt::CheckStateRole:
            file->itemGroup()->setVisible(value.value<Qt::CheckState>() == Qt::Checked);
            // Флажок папок над узлом (подпапка программы, группа) считается по
            // их УП -- он тоже устарел.
            for(FileTree::Node* folder = parent(); dynamic_cast<GroupNode*>(folder); folder = folder->parent())
                App::fileModel().updateNode(folder);
            return true;
        case Qt::EditRole: {
            // Переименование по F2 меняет имя ПРОГРАММЫ, а отображаемое имя
            // пересобирается из него так же, как это делает Form::fileHandler.
            // Иначе programName_ разъехался бы с меткой в дереве, и поиск
            // коллизий начал бы срабатывать не на то.
            auto* gcFile = static_cast<File*>(file);
            gcFile->setProgramName(value.toString());
            // Params::tool() индексирует tools без проверки: у УП без инструмента
            // (отладочные файлы) суффикса просто не будет.
            const auto& params = gcFile->params();
            gcFile->setFileName(params.tools.empty()
                    ? gcFile->programName()
                    : gcFile->programName() + u'_' + params.tool().nameEnc());
            return true;
        }
        default:;
        }
        [[fallthrough]]; // роль FileTree::Select обрабатывается ниже, в default
    case FileTree::Column::Side:
        switch(role) {
        case Qt::EditRole: {
            const auto newSide = static_cast<Side>(value.toBool());
            if(newSide == file->side()) return true; // холостой выбор той же стороны

            auto* gcFile = static_cast<File*>(file);
            // Текст УП стороной и задан: снизу он зеркальный (см.
            // File::mirrorAndOffsetCurves), сверху идёт как есть. setSide
            // помечает его устаревшим -- пересоберёт первый, кто прочтёт
            // (сохранение, «Показать исходник»), а открытое окно просмотра --
            // сразу: иначе оно показывало бы чужую сторону. Сама геометрия на
            // сцене не меняется: зеркалит вывод, а не чертёж.
            gcFile->setSide(newSide);
            refreshGCodeViewers({gcFile});
            // Подпись в дереве несёт сторону суффиксом (_TS/_BS), и её строка
            // тоже устарела.
            App::fileModel().updateNode(this);
            return true;
        }
        default:;
        }
        [[fallthrough]];
    default:
        if(role == FileTree::Select) {
            file->itemGroup()->setZValue((value.toBool() ? +(file->id() + 1) : -(file->id() + 1)) * 1000);
            return true;
        }
        return false;
    }
}

QVariant Node::data(const QModelIndex& index, int role) const {
    switch(index.column()) {
    case FileTree::Column::NameColorVisible:
        switch(role) {
        case Qt::DisplayRole:
            // if (file->shortName().endsWith(App::gcSettings().fileExtension()))
            // return file->shortName();
            // else
            return {
                file->shortName() + QStringList{u"_TS"_s, u"_BS"_s}
                  [file->side()]
            };
        case Qt::EditRole      : return file->shortName();
        case Qt::ToolTipRole   : return {file->shortName() + u'\n' + file->name()};
        case Qt::CheckStateRole: return file->itemGroup()->isVisible() ? Qt::Checked : Qt::Unchecked;
        case Qt::DecorationRole: return file->icon();
        case FileTree::Id      : return id();
        default                : return {};
        }
    case FileTree::Column::Side:
        switch(role) {
        case Qt::DisplayRole:
        case Qt::ToolTipRole: return sideStrList[file->side()];
        case Qt::EditRole   : return static_cast<bool>(file->side());
        default             : return {};
        }
    default: return {};
    }
}

Qt::ItemFlags Node::flags(const QModelIndex& index) const {
    Qt::ItemFlags itemFlag = Qt::ItemIsEnabled | Qt::ItemNeverHasChildren | Qt::ItemIsSelectable /*| Qt::ItemIsDragEnabled*/;
    switch(index.column()) {
    case FileTree::Column::NameColorVisible:
        // if (file->shortName().endsWith(App::gcSettings().fileExtension()))
        // return itemFlag | Qt::ItemIsUserCheckable;
        return itemFlag | Qt::ItemIsUserCheckable | Qt::ItemIsEditable;
    case FileTree::Column::Side: {
        // if (file->shortName().endsWith(App::gcSettings().fileExtension()))
        // return itemFlag;
        return itemFlag | Qt::ItemIsEditable;
    }
    default: return itemFlag;
    }
}

void Node::menu(QMenu& menu, FileTree::View* tv) {
    menu.addAction(QIcon::fromTheme(u"document-save"_s), QObject::tr("&Save Toolpath"), [tv, this] {
        emit tv->saveGCodeFile(id());
    });
    menu.addSeparator();
    menu.addAction(QIcon::fromTheme(u"hint"_s), QObject::tr("&Hide other"),
        tv, &FileTree::View::hideOther);
    // Реестр окон держит сам Dialog: одно окно на файл, оно же обновляется при
    // перегенерации и закрывается при удалении.
    menu.addAction(QIcon(), QObject::tr("&Show source"), [tv, this] {
        // Текст -- актуальный: после правок параметров он мог остаться
        // помеченным устаревшим (пересчёт ленивый).
        ensureGCodeLines({gcodeFile()});
        Dialog::showFor(id(), file->lines2(), file->name(), tv);
    });
    addGroupActions(menu, tv, this);
    menu.addSeparator();
    menu.addAction(QIcon::fromTheme(u"edit-delete"_s), QObject::tr("&Delete Toolpath"), tv, &FileTree::View::closeFile);
}

bool Node::doubleClicked(FileTree::View* tv) {
    emit tv->editGCodeFile(id());
    return true;
}

int Node::id() const { return file->id(); }

File* Node::gcodeFile() const { return static_cast<File*>(file); }

///////////////////////////////////////////////////////////////
/// \brief GroupNode -- папка группы или многофайловой программы
///
GroupNode::GroupNode(Kind kind, const QString& name)
    : FileTree::Node{FileTree::Folder}
    , kind_{kind}
    , name_{name} { }

GroupNode::~GroupNode() { }

QVariant GroupNode::data(const QModelIndex& index, int role) const {
    if(index.column() == FileTree::Column::NameColorVisible) {
        switch(role) {
        case Qt::DisplayRole:
        case Qt::ToolTipRole   : return name_;
        case Qt::DecorationRole: return QIcon::fromTheme(u"folder"_s);
        case Qt::CheckStateRole: {
            // Здесь, в члене GroupNode, голое Node -- база FileTree::Node.
            std::vector<GCode::Node*> nodes;
            collectProgramNodes(this, nodes);
            const auto visible = r::count_if(nodes, [](const GCode::Node* node) { return node->gcodeFile()->itemGroup()->isVisible(); });
            if(!visible) return Qt::Unchecked;
            return std::cmp_equal(visible, nodes.size()) ? Qt::Checked : Qt::PartiallyChecked;
        }
        default: break;
        }
    }
    switch(role) {
    case FileTree::Id         : return id();
    case FileTree::NodeType   : return FileTree::Folder;
    case FileTree::ContentType: return childs.size() ? childs.front()->type_ : FileTree::Null;
    default                   : return {};
    }
}

bool GroupNode::setData(const QModelIndex& index, const QVariant& value, int role) {
    // Флажок папки -- видимость всех её УП. Через него же «Hide other»
    // гасит соседние группы целиком.
    if(index.column() != FileTree::Column::NameColorVisible || role != Qt::CheckStateRole)
        return false;
    std::vector<GCode::Node*> nodes;
    collectProgramNodes(this, nodes);
    for(GCode::Node* node: nodes) {
        node->setData(node->index(), value, Qt::CheckStateRole);
        App::fileModel().updateNode(node);
    }
    return true;
}

Qt::ItemFlags GroupNode::flags(const QModelIndex& index) const {
    // Папка не выделяется, как и папки типов: «Delete Selected» по выделенной
    // папке снёс бы все УП в ней.
    if(index.column() == FileTree::Column::NameColorVisible)
        return Qt::ItemIsEnabled | Qt::ItemIsUserCheckable;
    return Qt::ItemIsEnabled;
}

void GroupNode::menu(QMenu& menu, FileTree::View* tv) {
    // Подпапка программы -- та же УП, только многофайловая: и пункты те же.
    if(kind_ == Program) return addGroupActions(menu, tv, this);

    // Имя -- копией: «Ungroup» папку убирает, а лямбды меню её переживают.
    const QString group = name_;
    menu.addAction(QIcon::fromTheme(u"document-edit"_s), QObject::tr("&Rename Group..."), [tv, group] {
        bool ok{};
        const QString name = QInputDialog::getText(tv, QObject::tr("Rename Group"), QObject::tr("Group name:"),
            QLineEdit::Normal, group, &ok)
                                 .trimmed();
        if(ok && !name.isEmpty() && name != group) renameProgramGroup(group, name);
    });
    // Распустить: УП -- прямо в «GCode», пустая папка уходит сама.
    menu.addAction(QObject::tr("&Ungroup"), [group] {
        moveProgramsToGroup(groupProgramIds(group), {});
    });
}

///////////////////////////////////////////////////////////////
/// Раскладка дерева УП
///
void layoutTree(const std::vector<GroupState>& states) {
    auto& model = App::fileModel();
    FileTree::Node* root = model.folder(G_CODE);
    if(!root) return;

    // Группы проекта -- первыми, в его порядке и с его раскрытием. Группа, от
    // которой не осталось ни одной УП, уйдёт с подчисткой в конце.
    for(const GroupState& state: states) {
        if(state.name.isEmpty() || findGroupNode(root, GroupNode::Group, state.name)) continue;
        auto* folder = new GroupNode{GroupNode::Group, state.name};
        folder->setExpanded(state.expanded);
        model.appendChild(root, folder);
    }

    std::vector<Node*> nodes;
    collectProgramNodes(root, nodes);

    // Группа -> программа -> узлы, в порядке дерева.
    struct ProgramEntry {
        QString name;
        std::vector<Node*> nodes;
    };
    struct GroupEntry {
        QString name;
        std::vector<ProgramEntry> programs;
    };
    std::vector<GroupEntry> groups;
    for(Node* node: nodes) {
        const File* file = node->gcodeFile();
        auto group = r::find(groups, file->group(), &GroupEntry::name);
        if(group == groups.end())
            group = groups.insert(groups.end(), GroupEntry{file->group(), {}});
        // Безымянная УП (сверловка Drilling, старые проекты) -- программа сама по себе.
        auto program = file->programName().isEmpty()
            ? group->programs.end()
            : r::find(group->programs, file->programName(), &ProgramEntry::name);
        if(program == group->programs.end())
            program = group->programs.insert(group->programs.end(), ProgramEntry{file->programName(), {}});
        program->nodes.push_back(node);
    }

    for(auto& group: groups) {
        FileTree::Node* container = root;
        if(!group.name.isEmpty()) {
            container = findGroupNode(root, GroupNode::Group, group.name);
            if(!container) {
                container = new GroupNode{GroupNode::Group, group.name};
                model.appendChild(root, container);
            }
        }
        for(auto& program: group.programs) {
            if(program.nodes.size() < 2) {
                if(Node* node = program.nodes.front(); node->parent() != container)
                    model.moveNode(node, container);
                continue;
            }
            // Подпапка, где лежит ровно эта программа, едет целиком: с ней
            // остаются её строка и раскрытие.
            auto* folder = dynamic_cast<GroupNode*>(program.nodes.front()->parent());
            const bool own = folder && folder->kind() == GroupNode::Program && folder->name() == program.name
                && std::cmp_equal(folder->childCount(), program.nodes.size())
                && r::all_of(program.nodes, [folder](const Node* node) { return node->parent() == folder; });
            if(!own)
                folder = findGroupNode(container, GroupNode::Program, program.name);
            if(!folder) {
                folder = new GroupNode{GroupNode::Program, program.name};
                model.appendChild(container, folder);
            } else if(folder->parent() != container)
                model.moveNode(folder, container);
            for(Node* node: program.nodes)
                if(node->parent() != folder) model.moveNode(node, folder);
        }
    }

    // Опустевшие папки: программа сократилась до одного файла, группу покинули.
    model.pruneEmptyFolders();
}

void layoutAndReveal(int32_t id) {
    layoutTree();
    if(File* file = programFile(id))
        App::fileModel().revealNode(file->node());
}

std::vector<GroupState> groupStates() {
    std::vector<GroupState> states;
    for(const GroupNode* group: programGroups())
        states.push_back({group->name(), group->isExpanded()});
    return states;
}

} // namespace GCode
