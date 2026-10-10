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
#include <QDebug>
#include <QPainter>

namespace Drilling {

Model::Model(size_t rowCount, QObject* parent)
    : QAbstractTableModel{parent} {
    data_.resize(rowCount);
}

void Model::setToolId(int row, Tool::ID id, ToolOverride toolOverride) {
    if(data_[row].toolId != id)
        data_[row].useForCalc = id > Tool::ID::Null;
    data_[row].toolId = id;
    // Правка -- вместе с id и ДО updateTool(): превью берёт действующий
    // инструмент, а инструмента восстановленной УП в базе может уже не быть.
    data_[row].toolOverride = id > Tool::ID::Null ? std::move(toolOverride) : ToolOverride{};
    for(auto item: data_[row].items) {
        // Флаг должен быть выставлен ДО updateTool(), т.к. тот в конце вызывает
        // changeColor(), которая читает ItemIsSelectable как признак "Used" —
        // иначе цвет пересчитывается по ещё не обновлённому (старому) флагу.
        item->setFlag(QGraphicsItem::ItemIsSelectable, id > Tool::ID::Null);
        item->updateTool();
    }
    emit set(row, id > Tool::ID::Null);
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

void Model::setCreate(int row, bool create) {
    if(data_[row].toolId == Tool::ID::Null)
        return;
    data_[row].useForCalc = create;
    for(auto item: data_[row].items) {
        item->setFlag(QGraphicsItem::ItemIsSelectable, create);
        item->changeColor();
    }
    emit dataChanged(createIndex(row, 0), createIndex(row, ColumnCount - 1));
    emit headerDataChanged(Qt::Vertical, row, row);
}

void Model::setCreate(bool create) {
    for(int row{}; row < rowCount(); ++row)
        data_[row].useForCalc = create && data_[row].toolId != Tool::ID::Null;
    emit dataChanged(createIndex(0, 0), createIndex(rowCount() - 1, ColumnCount - 1));
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
            case Qt::ToolTipRole:
                if(edited) {
                    QString tip = tr("Edited for this toolpath only (the tool database is not changed)");
                    if(!effective.note().isEmpty())
                        tip += u"\n\n"_s + effective.note();
                    return tip;
                }
                break;
            case Qt::UserRole: return +data_[row].toolId;
            default          : break;
            }
        }
    }
    // Edit: в ячейке живёт кнопка формы (index-виджет), данных у неё нет.
    return {};
}

QVariant Model::headerData(int section, Qt::Orientation orientation, int role) const {
    switch(role) {
    case Qt::DisplayRole:
        if(orientation == Qt::Horizontal) {
            switch(section) {
            case Name: return {tr("Aperture") + u" / "_s + tr("Tool")};
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
    // Edit не выделяется: selectedIndexes() формы по-прежнему даёт ровно
    // один индекс (столбец Tool) на строку.
    if(index.column() == Edit)
        return Qt::ItemIsEnabled;
    return Qt::ItemIsSelectable | Qt::ItemIsEnabled;
}

Row::~Row() { qDeleteAll(items); }

const Tool& Row::tool() const { return toolOverride ? *toolOverride : App::toolHolder().tool(toolId); }

} // namespace Drilling

#include "moc_drill_model.cpp"
