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
#include <array>
#include <optional>
#include <vector>

namespace Drilling {

namespace Gi {
class Preview;
}

// Правка инструмента только для этой УП. Псевдоним -- ради Model: там имя Tool
// занято столбцом.
using ToolOverride = std::optional<Tool>;

// Обработка строки (типа отверстий): сверловка либо фрезеровка карманом или
// профилем. Направление фрезеровки -- часть режима, у сверловки его нет.
// Профиль отверстий -- всегда внутри: снаружи и по контуру отверстие не
// обрабатывают, а сторона отдельным выбором только мешала. Порядок -- порядок
// пунктов меню режима.
enum class Mode : int {
    Drilling,
    PocketClimb,
    PocketConventional,
    ProfileClimb,
    ProfileConventional,
};

inline constexpr std::array modes{Mode::Drilling, Mode::PocketClimb, Mode::PocketConventional, Mode::ProfileClimb, Mode::ProfileConventional};

constexpr bool isPocket(Mode mode) { return mode == Mode::PocketClimb || mode == Mode::PocketConventional; }
constexpr bool isConventional(Mode mode) { return mode == Mode::PocketConventional || mode == Mode::ProfileConventional; }

// Умеет ли инструмент этого типа обработку в этом режиме. Сверло не фрезерует
// (сменив фрезу на сверло, нельзя оставить строку фрезероваться), лазер не
// сверлит; фреза и гравёр -- любой режим (гравёр в сверловке накернивает центры).
constexpr bool canProcess(Tool::Type type, Mode mode) {
    switch(type) {
    case Tool::Drill: return mode == Mode::Drilling;
    case Tool::Laser: return mode != Mode::Drilling;
    default         : return true;
    }
}

// Какие инструменты строка принимает из базы. Паз -- только концевую фрезу: его
// фрезеруют и при сверловке (вдоль пути). Сверловка отверстий -- сверло или
// фреза (врезание). Карман и профиль -- то же, что их собственные формы:
// фреза, гравёр, лазер, -- и сверло: выбранное, оно переводит строку на
// сверловку (Model::setToolId). Без него сверлить отверстия, которые решили
// было фрезеровать, можно было только в два шага: сперва режим, потом сверло.
std::vector<Tool::Type> toolTypes(Mode mode, bool slot);

// Подходит ли инструмент этого типа строке, когда его выбирают из базы сразу
// нескольким строкам: паз -- только концевая фреза, отверстиям -- то, что
// умеет их режим, и сверло всегда (переведёт строку на сверловку). Всё, что
// предлагает toolTypes(), подходит.
constexpr bool toolFits(Tool::Type type, Mode mode, bool slot) {
    if(slot) return type == Tool::EndMill;
    return type == Tool::Drill || canProcess(type, mode);
}

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
    // Режим строки (столбец Mode). Сверло -- только сверловка: это держит
    // Model -- setToolId() переводит строку со сверлом на сверловку, setMode()
    // не ставит режима, которого инструмент строки не умеет (canProcess).
    Mode mode{Mode::Drilling};
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
    // Столбцы. Внутри Model имена Tool и Mode -- столбцы, а не типы: типы здесь
    // пишутся как ::Tool и Drilling::Mode (Tool::ID работает -- перед :: ищутся
    // только типы). Mode и Edit -- места под виджеты формы (index-виджеты):
    // список режима строки и кнопку правки её инструмента.
    enum {
        Name,
        Mode,
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

    Drilling::Mode mode(int row) const { return data_[row].mode; }
    // Режим, который инструмент строки умеет (canProcess); без инструмента -- любой.
    bool modeAllowed(int row, Drilling::Mode mode) const;
    // false -- инструмент строки этот режим не умеет, строка осталась как была.
    // Новый режим взводит галку строки, как и новый инструмент: её УП теперь другая.
    bool setMode(int row, Drilling::Mode mode);

    void setCreate(bool create);
    void setCreate(int row, bool create);
    // Новый выбор из базы правку сбрасывает. toolOverride -- для восстановления
    // УП на правку: ставится ДО updateTool() превью, которое её и читает.
    // Сверло в режиме фрезеровки переводит строку на сверловку.
    void setToolId(int row, Tool::ID id, ToolOverride toolOverride = {});
    // Правка инструмента только для этой УП; nullopt -- снова инструмент из базы.
    void setToolOverride(int row, ToolOverride toolOverride);
    // void setType(int type_) { type = type_; }

    static QString modeName(Drilling::Mode mode);
    static QIcon modeIcon(Drilling::Mode mode);

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
