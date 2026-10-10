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
#pragma once

#include "tool.h"
#include <QAbstractTableModel>
#include <QIcon>
#include <optional>

namespace Drilling {

namespace Gi {
class Preview;
}

// Правка инструмента только для этой УП. Псевдоним -- ради Model: там имя Tool
// занято столбцом.
using ToolOverride = std::optional<Tool>;

struct Row {
    // Row(QString&& name = {},
    // QIcon&& icon = {},
    // int32_t id = {},
    // double diameter = {})
    // : icon{icon}
    // , name(name)
    // , diameter(diameter)
    // , rowId(id)
    // , toolId(-1) {
    // }
    ~Row();
    /*const*/ QIcon icon;
    /*const*/ QStringList name;
    /*const*/ double diameter;
    /*const*/ bool isSlot;
    bool useForCalc{};
    Tool::ID toolId{};
    // Инструмент, правленный только для этой УП (кнопка в столбце Edit): в
    // базу не пишется, id у него -- тот же toolId. Пусто -- берётся из базы.
    ToolOverride toolOverride;
    // Действующий инструмент строки: правка, иначе инструмент из базы. Только
    // для строк с toolId > Null -- ToolHolder::tool() это .at().
    const Tool& tool() const;
    std::vector<Gi::Preview*> items;
};

class Model : public QAbstractTableModel {
    Q_OBJECT

    std::vector<Row> data_;

signals:
    void set(int, bool);

public:
    // Столбцы. Внутри Model имя Tool -- столбец, а не класс: класс здесь
    // пишется как ::Tool (Tool::ID работает -- перед :: ищутся только типы).
    // Edit -- место под кнопку правки инструмента (index-виджет формы).
    enum {
        Name,
        Tool,
        Edit,
        ColumnCount
    };

    explicit Model(size_t rowCount, QObject* parent = nullptr);
    ~Model() override { qDebug(__FUNCTION__); }

    bool isSlot(int row) const { return data_[row].isSlot; }
    bool useForCalc(int row) const { return data_[row].useForCalc; }

    Tool::ID toolId(int row) const { return data_[row].toolId; }
    bool hasOverride(int row) const { return data_[row].toolOverride.has_value(); }
    // Действующий инструмент строки (см. Row::tool()).
    const ::Tool& tool(int row) const { return data_[row].tool(); }

    void setCreate(bool create);
    void setCreate(int row, bool create);
    // Новый выбор из базы правку сбрасывает. toolOverride -- для восстановления
    // УП на правку: ставится ДО updateTool() превью, которое её и читает.
    void setToolId(int row, Tool::ID id, ToolOverride toolOverride = {});
    // Правка инструмента только для этой УП; nullopt -- снова инструмент из базы.
    void setToolOverride(int row, ToolOverride toolOverride);
    // void setType(int type_) { type = type_; }

    // QAbstractItemModel interface
    int rowCount(const QModelIndex& parent = {}) const override;
    int columnCount(const QModelIndex& parent = {}) const override;
    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
    QVariant headerData(int section, Qt::Orientation orientation, int role) const override;
    Qt::ItemFlags flags(const QModelIndex& index) const override;

    std::vector<Row>& data() { return data_; }
    const std::vector<Row>& data() const { return data_; }
    auto begin() { return data_.begin(); }
    auto end() { return data_.end(); }
};

} // namespace Drilling
