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
#include "drill_model.h"
#include "gc_form.h"
#include "gc_plugin.h"

#include <compare>
#include <deque>

namespace Ui {
class DrillForm;
}

class QCheckBox;
class QMenu;
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

// Ключ УП за показ формы: файл сверловки, обработка и инструмент. Строки
// одного ToolKey в разных режимах (сверловка, карман и профиль -- каждый
// попутно и встречно) -- РАЗНЫЕ УП и заменять друг друга не должны. У
// сверловки пазы и отверстия -- тоже разные УП: пазы фрезеруются вдоль путей,
// точки сверлятся. И УП разных файлов (PTH и NPTH одним сверлом) -- разные:
// сменив файл в форме, Create не должен заменить УП прежнего.
struct ProgramKey {
    int32_t srcFileId{-1}; // -1 -- окружности-фигуры, а не файл
    Mode mode{};
    bool slot{};
    ToolKey tool{};
    auto operator<=>(const ProgramKey&) const = default;
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
    // УП, пересчитанные за текущий показ формы: id проекта по ProgramKey, чтобы
    // повторный Create ЗАМЕНЯЛ их, а не плодил дубли. Ключ -- с режимом: пазы,
    // точки сверловки, карман и профиль считаются НЕЗАВИСИМО и за один клик
    // могут дать по УП на один ToolKey каждый; смешивать их id нельзя. И с
    // файлом сверловки: прежде ключом был один инструмент, и Create по NPTH
    // заменял УП PTH, сделанную тем же сверлом минутой раньше.
    // Сбрасываются в showEvent() -- после закрытия/переоткрытия формы снова
    // add-new; правка УП из дерева (editFile) кладёт сюда её саму, и «Обновить»
    // заменяет её.
    std::map<ProgramKey, int32_t> createdFileIds_;
    // Профиль и карман по отверстиям -- УП чужих File, без srcFileId: их id
    // (с именем программы) по файлу сверловки, чтобы drillGroup() нашёл группу
    // этого файла и после переименования. В showEvent() не сбрасывается --
    // группа живёт дольше показа формы.
    std::map<int32_t, std::vector<std::pair<int32_t, QString>>> srcPrograms_;
    QString drillGroup(const AbstractFile* srcFile) const;

    void initToolTable();
    // Операция УП для инструмента строки: сверловка -- только у строки в
    // режиме сверловки и не у паза (пазы фрезеруются вдоль, drilling.js --
    // millingProfilePath), лазер и резьба -- по типу, остальное -- фрезеровка.
    Tool::Operation operation(const Tool& tool, Mode mode, bool slot) const;
    // Инструмент базы под операцию строки и материал проекта (наборы режимов).
    Tool resolved(const Tool& tool, Mode mode, bool slot) const;
    // Действующий инструмент строки для УП: правка -- как есть, инструмент
    // базы -- разрешённый (resolved).
    Tool rowTool(const Row& row) const;
    // Кнопки правки инструмента в столбце Edit -- по одной на строку модели.
    void initEditButtons();
    // Выпадающие списки режима в столбце Mode -- по одному на строку модели.
    void initModeBoxes();
    // Текущий режим и доступные пункты списка строки -- по модели: чего
    // инструмент строки не умеет, то серым (сверло -- фрезеровку).
    void updateModeBox(int row);
    // Пункты режимов в контекстное меню для строк rows: пункт доступен, если
    // его умеет инструмент хоть одной из них, отмечен, если в нём все.
    void fillModeMenu(QMenu* menu, const std::vector<int>& rows);
    // Режим строкам, чей инструмент его умеет; прочие остаются как были.
    // Правку инструмента «только для этой УП», сделанную под операцию прежнего
    // режима, новая операция сбрасывает (см. .cpp).
    void setRowsMode(const std::vector<int>& rows, Mode mode);
    // Инструмент из базы строкам: строка в режиме, которого инструмент не умеет,
    // его не получает -- кроме сверла: оно переводит строку на сверловку.
    // Не получившие -- одним сообщением.
    void setRowsTool(const std::vector<int>& rows, const Tool& tool);
    // Типы инструментов для выбора из базы сразу нескольким строкам: всё, что
    // принимает хоть одна из них.
    std::vector<Tool::Type> toolTypesOf(const std::vector<int>& rows) const;
    // Строки выделения, без повторов, по порядку.
    std::vector<int> selectedRows() const;
    // Правка инструмента строки только для этой УП (в базу не пишется). Та же
    // правка уходит и остальным выделенным строкам с тем же инструментом базы и
    // той же операцией (режимом), если сама строка среди выделенных.
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
    // асинхронно; при этом за один клик бывает НЕСКОЛЬКО независимых прогонов --
    // по одному на (режим, ToolKey). Подряд их не запустить: смена Creator'а
    // (setCreator -> stopWorker) не только ждёт идущий расчёт, но и ОТМЕНЯЕТ
    // его -- jthread при присваивании просит остановиться, и все прогоны,
    // кроме последнего, приходили пустыми («инструмент не помещается»).
    // Поэтому computePaths() только складывает прогоны сюда, со всеми их
    // параметрами, и запускает первый; каждый следующий стартует из
    // fileHandler(), когда пришёл файл предыдущего. Голова очереди -- всегда
    // прогон, который сейчас считается.
    struct PendingRun {
        std::vector<RowRef> rows;
        // Режим -- карман или профиль, попутно или встречно. Файл сверловки
        // (key.srcFileId) -- свой у прогона, а не выбранный в форме: пока
        // прогон ждёт очереди, выбрать могут другой, а УП ляжет в группу своего.
        ProgramKey key{};
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
    // Плагины с собственной независимой моделью "что сейчас правится" (своя
    // карта ProgramKey->fileId) не пользуются единым fileId/editMode_ базового
    // класса -- он бы не различил несколько программ, получившихся за один клик.
    bool autoEnterEditMode() const override { return false; }
    // QWidget interface
    void showEvent(QShowEvent* event) override {
        updateFiles();
        createdFileIds_.clear();
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
