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

#include "drill_file.h"
#include "gc_form.h"
#include "gc_plugin.h"

#include <compare>
#include <deque>

namespace Ui {
class DrillForm;
}

class QCheckBox;
class ToolDatabase;

namespace Drilling {

// using PosOrPath = std::variant<const QPointF, const QPolygonF>;
// using Key = std::tuple<int, double, bool, QString>;
// struct Val {
// std::vector<PosOrPath> posOrPath;
// Paths64 draw;
// };
// using Preview = std::map<Key, Val>;

class Model;
class Header;
struct Row;

// Ключ программы: инструмент из базы плюс, если строка правлена только для
// этой УП, хэш правки (0 -- инструмент базы как есть). Строки одного toolId с
// правкой и без неё (или с разными правками) -- РАЗНЫЕ программы: у каждой
// свой инструмент, а значит и свои подачи, обороты и note для скриптов.
struct ToolKey {
    Tool::ID id{};
    size_t hash{};
    auto operator<=>(const ToolKey&) const = default;
};

class Form final : public GCode::Form {
    Q_OBJECT

public:
    explicit Form(GCode::Plugin* plugin);
    ~Form() override;

    void updateFiles();
    static bool canToShow();

private:
    Ui::DrillForm* ui;
    class Model* model = nullptr;
    class AbstractFile* file = nullptr;
    class Header* header;
    class QCheckBox* checkBox;
    GCType worckType = GCType::Drill;
    GCode::SideOfMilling side = GCode::Inner;
    // УП, пересчитанные за текущий показ формы: id проекта по ToolKey, чтобы
    // повторный Create ЗАМЕНЯЛ их, а не плодил дубли. Четыре карты, а не одна --
    // ветка пазов (worckType == Drill), ветка простых отверстий ("other", тот
    // же worckType) и асинхронные ветки Profile/Pocket по отверстиям считают
    // НЕЗАВИСИМЫЕ pathsMap и могут за один клик дать по файлу на один ключ
    // из каждой; смешивать их id в одном ключе нельзя.
    // Сбрасываются в showEvent() -- после закрытия/переоткрытия формы снова add-new.
    std::map<ToolKey, int32_t> createdSlotFileIds_;
    std::map<ToolKey, int32_t> createdDrillFileIds_;
    std::map<ToolKey, int32_t> createdProfileFileIds_;
    std::map<ToolKey, int32_t> createdPocketFileIds_;
    // Профиль и карман по отверстиям -- УП чужих File, без srcFileId: их id
    // (с именем программы) по файлу сверловки, чтобы drillGroup() нашёл группу
    // этого файла и после переименования. В showEvent() не сбрасывается --
    // группа живёт дольше показа формы.
    std::map<int32_t, std::vector<std::pair<int32_t, QString>>> srcPrograms_;
    QString drillGroup(const AbstractFile* srcFile) const;

    void initToolTable();
    // Операция УП для инструмента: сверловка -- только в режиме сверления и
    // не у паза (пазы фрезеруются вдоль, drilling.js -- millingProfilePath),
    // лазер и резьба -- по типу, остальное -- фрезеровка.
    Tool::Operation operation(const Tool& tool, bool slot) const;
    // Инструмент базы под операцию строки и материал проекта (наборы режимов).
    Tool resolved(const Tool& tool, bool slot) const;
    // Действующий инструмент строки для УП: правка -- как есть, инструмент
    // базы -- разрешённый (resolved).
    Tool rowTool(const Row& row) const;
    // Кнопки правки инструмента в столбце Edit -- по одной на строку модели.
    void initEditButtons();
    // Правка инструмента строки только для этой УП (в базу не пишется). Та же
    // правка уходит и остальным выделенным строкам с тем же инструментом базы,
    // если сама строка среди выделенных.
    void editRowTool(int row);
    // Открыть базу на инструменте строки (подсветить его), как «Выбрать» в
    // формах фрезеровки. Строка без инструмента -- база как есть.
    void selectRowTool(ToolDatabase& tdb, int row) const;
    // Строка, чей инструмент подсветить в базе при выборе для нескольких строк:
    // preferred (строка под курсором), если у неё есть инструмент, иначе первая
    // из indexes с инструментом; -1 -- такой нет.
    int toolRowOf(const QModelIndexList& indexes, int preferred = -1) const;

    void on_cbxFileCurrentIndexChanged();
    void on_doubleClicked(const QModelIndex& current);
    void on_selectionChanged(const QItemSelection& selected, const QItemSelection& deselected);
    void on_customContextMenuRequested(const QPoint& pos);
    void customContextMenuRequested(const QPoint& pos);

    void pickUpTool();
    void updateState();
    //////////
    void errorOccurred();

    QModelIndexList selectedIndexes() const;

    inline void zoomToSelected();

protected:
    // FormsUtil interface
    void computePaths() override;
    void updateName() override;
    // Файлы, созданные чужим Creator'ом (Profile/PocketOffset), тоже должны
    // унести с собой строки таблицы -- поэтому обработчик расширяется.
    void fileHandler(GCode::File* file) override;

    // Строки таблицы с данными индексами -- в том виде, в каком их надо будет
    // восстановить при правке.
    std::vector<RowRef> rowRefs(const std::vector<int>& indexes) const;
    // Снимает галку "Create" только у перечисленных строк -- зовётся ПОСЛЕ
    // подтверждения, что УП по ним реально получилась, не раньше.
    void uncheckRows(const std::vector<RowRef>& rows);
    // Профиль/карман по отверстиям считает чужой Creator и результат приходит
    // асинхронно; при этом за один клик бывает НЕСКОЛЬКО независимых по ToolKey
    // прогонов. Подряд их не запустить: смена Creator'а (setCreator ->
    // stopWorker) не только ждёт идущий расчёт, но и ОТМЕНЯЕТ его -- jthread
    // при присваивании просит остановиться, и все прогоны, кроме последнего,
    // приходили пустыми («инструмент не помещается»). Поэтому computePaths()
    // только складывает прогоны сюда, со всеми их параметрами, и запускает
    // первый; каждый следующий стартует из fileHandler(), когда пришёл файл
    // предыдущего. Голова очереди -- всегда прогон, который сейчас считается.
    struct PendingRun {
        std::vector<RowRef> rows;
        GCType worckType{};
        ToolKey key{};
        QString programName;
        std::vector<int32_t> replacedIds; // УП этого ключа из прошлого Create
        GCode::Params gcp;                // что считать: прогон ждёт своей очереди
        // Прогон отменённого клика, ещё не приславший файл, а за ним уже
        // встал новый Create: что он пришлёт -- не наше.
        bool discard{};
    };
    std::deque<PendingRun> pendingRuns_;
    // Запустить голову очереди.
    void startRun();
    // Плагины с собственной независимой моделью "что сейчас правится" (свои
    // карты ToolKey->fileId на каждую ветку) не пользуются единым fileId/
    // editMode_ базового класса -- он бы не различил несколько программ,
    // получившихся за один клик.
    bool autoEnterEditMode() const override { return false; }
    // QWidget interface
    void showEvent(QShowEvent* event) override {
        updateFiles();
        createdSlotFileIds_.clear();
        createdDrillFileIds_.clear();
        createdProfileFileIds_.clear();
        createdPocketFileIds_.clear();
        // Очередь прогонов не сбрасывается: форму закрывают и во время счёта
        // (расчёт досчитывает своё), и файл идущего прогона без своей записи
        // ушёл бы в проект под чужим именем, а оставшиеся прогоны -- не
        // запустились бы. Пуста она и так: голову снимает fileHandler().
        // Базовая, а не event->accept(): в GCode::Form::showEvent сбрасывается
        // режим правки, иначе форма молча перепишет УП из прошлого сеанса.
        GCode::Form::showEvent(event);
    }
    void hideEvent(QHideEvent* event) override;

public:
    void editFile(GCode::File* file) override;
};

class Plugin final : public GCode::Plugin {
    Q_OBJECT
    Q_PLUGIN_METADATA(IID GCodeInterface_iid FILE "description.json")
    Q_INTERFACES(GCode::Plugin)

    // GCode::Plugin interface
    Form form{this};

public:
    QAction* addAction(QMenu* menu, QToolBar* toolbar) override {
        auto action = GCode::Plugin ::addAction(menu, toolbar);
        action->setData(true);
        return action;
    }
    QIcon icon() const override { return QIcon::fromTheme(u"drill-path"_s); }
    QKeySequence keySequence() const override { return {u"Ctrl+Shift+D"_s}; }
    QWidget* createForm() override { return &form; }
    QString gcName() const override { return u"Drilling"_s; }
    bool canToShow() const override { return Form::canToShow(); }
    uint32_t type() const override { return DRILLING; }
    AbstractFile* loadFile(std::string_view json) const override { return Serial::load<File>(json); }
    std::string_view typeName() const override { return Serial::typeNameOf<File>(); }
};

} // namespace Drilling

#include "app.h"
