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
 *******************************************************************************/
#include "drill_model.h"
#include "drill_form.h"
#include "drill_gi_preview.h"
#include "drill_header.h"
#include "tool_pch.h"

#include <QBitmap>
#include <QCoreApplication>
#include <QDebug>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPainter>
#include <climits>
#include <cmath>
#include <tuple>

namespace Drilling {

std::vector<Tool::Type> toolTypes(Mode mode, bool slot) {
    if(slot) return {Tool::EndMill};
    if(mode == Mode::Drilling) return {Tool::Drill, Tool::EndMill};
    return {Tool::Drill, Tool::EndMill, Tool::Engraver, Tool::Laser};
}

TreeOrder toolTreeOrder(const QJsonArray& tree) {
    // Узлы дерева -- обходом сверху вниз (ToolModel::exportTools): место узла
    // в массиве и есть его место в дереве. Папки мест не занимают.
    TreeOrder order;
    for(const QJsonValue& value: tree)
        if(const QJsonObject node = value.toObject(); node.value(u"tool"_s).toBool())
            order.try_emplace(static_cast<Tool::ID>(node.value(u"id"_s).toInt()), static_cast<int>(order.size()));
    return order;
}

TreeOrder toolTreeOrder() {
    // Тот же файл и тот же запасной путь, что у базы (ToolHolder::readTools).
    QFile file{App::settingsPath() + u"/tools.json"_s};
    if(!file.exists())
        file.setFileName(QCoreApplication::applicationDirPath() + u"/tools.json"_s);
    if(!file.open(QIODevice::ReadOnly))
        return {};
    return toolTreeOrder(QJsonDocument::fromJson(file.readAll()).object().value(u"tree"_s).toArray());
}

Tool::ID pickTool(Mode mode, bool slot, double diameter, double depth, const Tools& tools, const TreeOrder& order) {
    if(mode != Mode::Drilling || !(diameter > 0.0))
        return Tool::ID::Null;
    // Близкий -- как и прежде, в пределах 5% диаметра. Точный -- с запасом на
    // перевод из дюймов (0.0315" -- 0.8001 мм): размеры свёрл идут шагом не
    // мельче 0.01 мм, двух разных свёрл этот запас не спутает. (Не near/far:
    // на Windows это макросы windef.h.)
    constexpr double nearShare = 0.05;
    constexpr double exactDelta = 0.005;
    constexpr double eps = 1e-6;
    // Меньше -- лучше; сравниваются по порядку: тип (сверло раньше фрезы),
    // точный диаметр раньше близкого, порядок в дереве базы, id.
    using Rank = std::tuple<int, int, int, int32_t>;
    std::optional<Rank> best;
    Tool::ID picked{Tool::ID::Null};
    for(const auto& [id, tool]: tools) {
        int typeRank{};
        if(tool.type() == Tool::Drill && !slot)
            typeRank = 0;
        else if(tool.type() == Tool::EndMill)
            typeRank = 1;
        else
            continue;
        // Границы -- теми же выражениями, что и прежде: инструмент ровно на 5%
        // больше отверстия по-прежнему подходит.
        if(!(diameter * (1.0 - nearShare) <= tool.diameter() && tool.diameter() <= diameter * (1.0 + nearShare)))
            continue;
        // Достанет ли: рабочая часть должна быть не короче глубины резания --
        // глубины УП плюс, у сверла, конуса вершины (getDepths() идёт на всю
        // эту глубину). Фрезой с рабочей частью 2 мм отверстие глубиной 5 мм
        // не сделать. Сломанное сверло (длина 0.1) и неизмеренная длина (0)
        // отпадают по этому же правилу, без особых случаев.
        const double need = depth + (tool.type() == Tool::Drill ? tool.getDepth() : 0.0);
        if(tool.lenght() + eps < need)
            continue;
        const auto place = order.find(id);
        const Rank rank{typeRank, std::abs(tool.diameter() - diameter) <= exactDelta ? 0 : 1,
            place != order.end() ? place->second : INT_MAX, std::to_underlying(id)};
        if(!best || rank < *best)
            best = rank, picked = id;
    }
    return picked;
}

Model::Model(size_t rowCount, QObject* parent)
    : QAbstractTableModel{parent} {
    data_.resize(rowCount);
}

bool Model::modeAllowed(int row, Drilling::Mode mode) const {
    const Row& r = data_[row];
    if(r.toolId <= Tool::ID::Null)
        return true;
    if(r.toolOverride)
        return canProcess(r.toolOverride->type(), mode);
    // Не Row::tool(): это .at(), а инструмент могли удалить в окне базы --
    // тогда судить не по чему.
    const auto& tools = App::toolHolder().tools();
    const auto it = tools.find(r.toolId);
    return it == tools.end() || canProcess(it->second.type(), mode);
}

bool Model::setMode(int row, Drilling::Mode mode) {
    Row& r = data_[row];
    if(r.mode == mode) return true;
    if(!modeAllowed(row, mode)) return false;
    r.mode = mode;
    // Строка с инструментом -- снова в расчёт (галку взводит Header по
    // сигналу set): прежняя её УП была другой обработки.
    if(r.toolId > Tool::ID::Null) emit set(row, true);
    emit dataChanged(createIndex(row, 0), createIndex(row, ColumnCount - 1));
    return true;
}

void Model::setToolId(int row, Tool::ID id, ToolOverride toolOverride) {
    // Назначили инструмент -- строка сразу к расчёту. Сняли -- галка остаётся,
    // как стояла: она же подсветка отверстий, а УП без инструмента не строится.
    if(data_[row].toolId != id && id > Tool::ID::Null)
        data_[row].useForCalc = true;
    data_[row].toolId = id;
    // Правка -- вместе с id и ДО updateTool(): превью берёт действующий
    // инструмент, а инструмента восстановленной УП в базе может уже не быть.
    data_[row].toolOverride = id > Tool::ID::Null ? std::move(toolOverride) : ToolOverride{};
    // Сверло не фрезерует: строка с ним -- только сверловка. Иначе, сменив
    // фрезу на сверло, строка так и осталась бы карманом или профилем.
    if(id > Tool::ID::Null && data_[row].tool().type() == ::Tool::Drill)
        data_[row].mode = Drilling::Mode::Drilling;
    for(auto item: data_[row].items) {
        // Флаг должен быть выставлен ДО updateTool(), т.к. тот в конце вызывает
        // changeColor(), которая читает ItemIsSelectable как признак "Used" —
        // иначе цвет пересчитывается по ещё не обновлённому (старому) флагу.
        item->setFlag(QGraphicsItem::ItemIsSelectable, id > Tool::ID::Null);
        item->updateTool();
    }
    // Назначили инструмент -- галка ставится. Сняли -- галка остаётся как
    // была: она же подсветка отверстий (УП без инструмента не строится).
    emit set(row, id > Tool::ID::Null || data_[row].useForCalc);
    emit dataChanged(createIndex(row, 0), createIndex(row, ColumnCount - 1));
}

void Model::setToolOverride(int row, ToolOverride toolOverride) {
    if(data_[row].toolId == Tool::ID::Null)
        return;
    // Инструмента строки в базе уже нет (программа из проекта, другая база):
    // без копии строка смотрела бы на пропавший id, и .at() бросил бы.
    if(!toolOverride && !App::toolHolder().tools().contains(data_[row].toolId))
        return;
    data_[row].toolOverride = std::move(toolOverride);
    // Диаметр мог измениться -- перекрестье и контур инструмента на превью тоже.
    for(auto item: data_[row].items)
        item->updateTool();
    // Инструмент строки стал другим -- строку снова в расчёт, как и при новом
    // выборе из базы (галку взводит Header по сигналу set).
    emit set(row, true);
    emit dataChanged(createIndex(row, 0), createIndex(row, ColumnCount - 1));
}

void Model::setMaterial(const QString& material) {
    if(material_ == material) return;
    material_ = material;
    // Режимы инструментов базы теперь из другого набора -- подсказки устарели.
    if(rowCount()) emit dataChanged(createIndex(0, Tool), createIndex(rowCount() - 1, Tool), {Qt::ToolTipRole});
}

void Model::setCreate(int row, bool create) {
    // Галка ставится и строке без инструмента: ею подсвечивают отверстия на
    // плате, чтобы понять, что это за отверстия. УП строится только для строк
    // с инструментом (computePaths).
    data_[row].useForCalc = create;
    for(auto item: data_[row].items) {
        item->setFlag(QGraphicsItem::ItemIsSelectable, create);
        item->changeColor();
    }
    emit dataChanged(createIndex(row, 0), createIndex(row, ColumnCount - 1));
    emit headerDataChanged(Qt::Vertical, row, row);
}

void Model::setCreate(bool create) {
    // Всем строкам, и без инструмента тоже, -- с подсветкой их отверстий.
    for(int row{}; row < rowCount(); ++row)
        setCreate(row, create);
}

int Model::rowCount(const QModelIndex& /*parent*/) const { return static_cast<int>(data_.size()); }

int Model::columnCount(const QModelIndex& /*parent*/) const { return ColumnCount; }

QVariant Model::data(const QModelIndex& index, int role) const {
    int row = index.row();
    if(index.column() == Name) {
        switch(role) {
        case Qt::DisplayRole:
            if(data_[row].isSlot)
                return data_[row].name.back();
            else
                return data_[row].name.back();
        case Qt::DecorationRole: {
            if(data_[index.row()].toolId > Tool::ID::Null && data_[row].isSlot) {
                QImage image(data_[row].icon.pixmap(24, 24).toImage());
                for(int x{}; x < 24; ++x)
                    for(int y{}; y < 24; ++y)
                        image.setPixelColor(x, y, QColor(255, 0, 0, image.pixelColor(x, y).alpha()));
                return QIcon(QPixmap::fromImage(image));
            } else if(data_[index.row()].toolId > Tool::ID::Null) {
                return data_[row].icon;
            } else if(data_[row].isSlot) {
                QImage image(data_[row].icon.pixmap(24, 24).toImage());
                for(int x{}; x < 24; ++x)
                    for(int y{}; y < 24; ++y)
                        image.setPixelColor(x, y, QColor(255, 100, 100, image.pixelColor(x, y).alpha()));
                return QIcon(QPixmap::fromImage(image));
            } else {
                QImage image(data_[row].icon.pixmap(24, 24).toImage());
                for(int x{}; x < 24; ++x)
                    for(int y{}; y < 24; ++y)
                        image.setPixelColor(x, y, QColor(100, 100, 100, image.pixelColor(x, y).alpha()));
                return QIcon(QPixmap::fromImage(image));
            }
        }
        case Qt::UserRole: return row;
        default          : break;
        }
    } else if(index.column() == Tool) {
        if(data_[row].toolId == Tool::ID::Null)
            switch(role) {
            case Qt::DisplayRole      : return tr("Select Tool");
            case Qt::TextAlignmentRole: return Qt::AlignCenter;
            case Qt::UserRole         : return +data_[row].toolId;
            default                   : break;
            }
        else {
            // Действующий инструмент: правка "только для этой УП" помечается
            // звёздочкой и курсивом, чтобы её не спутать с инструментом базы.
            const bool edited = data_[row].toolOverride.has_value();
            const ::Tool& effective = data_[row].tool();
            switch(role) {
            case Qt::DisplayRole   : return edited ? u"%1 *"_s.arg(effective.name()) : effective.name();
            case Qt::DecorationRole: return effective.icon();
            case Qt::FontRole:
                if(edited) {
                    QFont font;
                    font.setItalic(true);
                    return QVariant::fromValue(font);
                }
                break;
            case Qt::ToolTipRole: {
                // Откуда у инструмента режимы -- как у выбора инструмента в
                // формах фрезеровки (ToolSelectorForm): правка, набор под
                // операцию строки и материал формы или свои значения.
                QString tip;
                if(edited) {
                    tip = tr("Edited for this toolpath only (the tool database is not changed)");
                } else {
                    const ::Tool::Operation operation = operationOf(effective.type(), data_[row].mode, data_[row].isSlot);
                    const ::Tool resolved = App::toolHolder().resolve(effective, operation, material_);
                    if(!resolved.material().isEmpty() || effective.preset(operation, {}))
                        tip = tr("Cutting data: %1").arg(App::toolHolder().presetName({.operation = operation, .material = resolved.material()}));
                    else
                        tip = tr("Cutting data: tool defaults");
                }
                if(!effective.note().isEmpty())
                    tip += u"\n\n"_s + effective.note();
                return tip;
            }
            case Qt::UserRole: return +data_[row].toolId;
            default          : break;
            }
        }
    } else if(index.column() == Mode && role == Qt::UserRole) {
        return static_cast<int>(data_[row].mode);
    }
    // Mode и Edit: в ячейке живёт виджет формы (index-виджет: список режима,
    // кнопка правки) -- текст под ним не рисуется, режим показывает сам список.
    return {};
}

QVariant Model::headerData(int section, Qt::Orientation orientation, int role) const {
    switch(role) {
    case Qt::DisplayRole:
        if(orientation == Qt::Horizontal) {
            switch(section) {
            case Name: return {tr("Aperture") + u" / "_s + tr("Tool")};
            case Mode: return tr("Mode");
            case Tool:;
                return tr("Tool");
            default: return {}; // Edit -- без заголовка, там только кнопки
            }
        }
        return data_[section].name.value(0);
    case Qt::SizeHintRole:
        if(orientation == Qt::Vertical)
            return QFontMetrics(QFont()).boundingRect(u"T999"_s).size() + QSize(Header::DelegateSize + 10, 1);
        return {};
    case Qt::TextAlignmentRole:
        if(orientation == Qt::Vertical)
            return static_cast<int>(Qt::AlignRight) | static_cast<int>(Qt::AlignVCenter);
        return Qt::AlignCenter;
    default: return {};
    }
}

Qt::ItemFlags Model::flags(const QModelIndex& index) const {
    if(index.column() == Name)
        return Qt::ItemIsEnabled | Qt::ItemIsUserCheckable;
    // Mode и Edit не выделяются: selectedIndexes() формы по-прежнему даёт
    // ровно один индекс (столбец Tool) на строку.
    if(index.column() == Mode || index.column() == Edit)
        return Qt::ItemIsEnabled;
    return Qt::ItemIsSelectable | Qt::ItemIsEnabled;
}

QString Model::modeName(Drilling::Mode mode) {
    switch(mode) {
    case Drilling::Mode::Drilling           : return tr("Drilling");
    case Drilling::Mode::PocketClimb        : return tr("Pocket · climb");
    case Drilling::Mode::PocketConventional : return tr("Pocket · conventional");
    case Drilling::Mode::ProfileClimb       : return tr("Profile · climb");
    case Drilling::Mode::ProfileConventional: return tr("Profile · conventional");
    }
    return {};
}

// Значки -- те же, что у самих УП в дереве: сверловка, карман, профиль.
QIcon Model::modeIcon(Drilling::Mode mode) {
    if(mode == Drilling::Mode::Drilling) return QIcon::fromTheme(u"drill-path"_s);
    return QIcon::fromTheme(isPocket(mode) ? u"pocket-path"_s : u"profile-path"_s);
}

Row::~Row() { qDeleteAll(items); }

const Tool& Row::tool() const { return toolOverride ? *toolOverride : App::toolHolder().tool(toolId); }

} // namespace Drilling

#include "moc_drill_model.cpp"
