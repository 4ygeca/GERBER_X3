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
#include "drill_form.h"
#include "circle/shape.h"
#include "drill_gi_preview.h"
#include "drill_header.h"
#include "drill_model.h"
#include "gi_point.h"
#include "tool_database.h"
#include "tool_editdialog.h"
#include "ui_drillform.h"
#include <boost/graph/bellman_ford_shortest_paths.hpp>

// #include "file.h"
// #include "gi_point.h"
// #include "gi_preview.h"
#include "gc_node.h"
#include "graphicsview.h"
// #include "project.h"
// #include "settings.h"
// #include "tool_pch.h"

#include "pocketoffset/pocketoffset.h"
#include "profile/profile.h"

#include <QComboBox>
#include <QHeaderView>
#include <QMenu>
#include <QMessageBox>
#include <QPainter>
#include <QStandardItemModel>
#include <QThread>
#include <QTimer>
#include <QToolBar>
#include <QToolButton>
#include <QWheelEvent>
#include <QWidget>
#include <algorithm>
#include <set>

namespace Drilling {

namespace {

// Выбор режима строки. Колесо над ячейкой листает таблицу, а не меняет режим:
// иначе прокрутка длинной таблицы молча переводила бы строки, над которыми
// прошёл курсор, со сверловки на карман.
class ModeBox final : public QComboBox {
public:
    using QComboBox::QComboBox;

protected:
    void wheelEvent(QWheelEvent* event) override { event->ignore(); }
};

} // namespace

// Контур паза, расширенный до нужной ширины. delta у Geo::Inflate -- ПОЛНАЯ
// ширина, а offset здесь задан как смещение кромки, отсюда удвоение. Замыкать
// контуры руками больше не нужно: из точного домена они приходят замкнутыми.
Geo::Polylines offset(const Geo::Polyline& path, double offset) {
    return Geo::Inflate(Geo::Polylines{path}, offset * 2.0).contours();
}

// Ключ программы для строки: инструмент базы плюс хэш правки, если она есть.
static ToolKey toolKeyOf(const Row& row) {
    return {row.toolId, row.toolOverride ? row.toolOverride->hash() : size_t{}};
}

// Имя программы по инструменту. У правленного -- хвост из хэша правки: без
// него обычная и правленная программы одного инструмента звались бы одинаково,
// а по имени fileHandler() находит id асинхронного прогона, и раскладка дерева
// (GCode::layoutTree) сложила бы обе в одну папку. '*' тут нельзя -- после
// сохранения имя становится путём на диске.
static QString programTag(const Tool& tool, const ToolKey& key) {
    if(!key.hash)
        return tool.nameEnc();
    return tool.nameEnc() + u"_E"_s + QString::number(key.hash % 1679616u, 36).rightJustified(4, u'0').toUpper();
}

// Хвост имени УП фрезеровки отверстий: за один Create одна фреза даёт и
// карман, и профиль, и каждый -- попутно и встречно. Под одним именем такие УП
// раскладка дерева сложила бы в одну программу (одна папка, одно имя в
// задании), хотя это разные обработки. Попутное -- без пометки.
static QString modeTag(Mode mode) {
    switch(mode) {
    case Mode::PocketClimb        : return u"_POCKET"_s;
    case Mode::PocketConventional : return u"_POCKET-CONV"_s;
    case Mode::ProfileClimb       : return u"_PROFILE"_s;
    case Mode::ProfileConventional: return u"_PROFILE-CONV"_s;
    case Mode::Drilling           : break;
    }
    return {};
}

// Режим строк УП, сделанной формой до режимов у строк: режим, направление и
// сторона были на всю форму. Профиль -- всегда внутри: снаружи и по контуру
// отверстие больше не обрабатывается, сторона такой УП не переносится.
static Mode modeOf(GCType type, bool conventional) {
    switch(type) {
    case GCType::Pocket : return conventional ? Mode::PocketConventional : Mode::PocketClimb;
    case GCType::Profile: return conventional ? Mode::ProfileConventional : Mode::ProfileClimb;
    default             : return Mode::Drilling;
    }
}

// Группа в дереве по умолчанию: все УП одного файла сверловки (сверловка,
// пазы, профиль и карман по отверстиям) -- в одну группу с его именем. Новые
// УП ложатся туда же, где уже лежат прежние УП этого файла: так они находят
// группу и после переименования. Сверловка и пазы помнят свой файл сами
// (srcFileId), профиль и карман -- чужие File без этого поля, их форма
// помнит в srcPrograms_. Заменяемой на месте УП группу оставит
// File::initFrom -- её мог переложить пользователь.
QString Form::drillGroup(const AbstractFile* srcFile) const {
    if(!srcFile) return {}; // окружности-фигуры, а не файл -- без группы
    for(const File* sibling: App::project().files<File>())
        if(sibling->srcFileId() == srcFile->id() && !sibling->group().isEmpty())
            return sibling->group();
    if(auto it = srcPrograms_.find(srcFile->id()); it != srcPrograms_.end())
        for(const auto& [id, programName]: it->second)
            // id в проекте переиспользуются -- сверяется и имя программы.
            if(auto* sibling = dynamic_cast<const GCode::File*>(App::project().file(id));
                sibling && sibling->programName() == programName && !sibling->group().isEmpty())
                return sibling->group();
    return srcFile->shortName();
}

/////////////////////////////////////////////

Form::Form(GCode::Plugin* plugin)
    : GCode::Form{plugin, nullptr}
    , ui(new Ui::DrillForm) {
    ui->setupUi(content);

    grid->setRowStretch(2, 1);
    grid->setRowStretch(7, 0);

    initToolTable();

    pbCreate->setEnabled(false);

    {
        MySettings settings;
        settings.beginGroup(u"DrillForm"_s);
        settings.getValue(ui->chbxZoomToSelected);
        // Режим, сторона и направление на всю форму ушли в строки таблицы
        // (столбец Mode) -- их прежние ключи больше не читаются. Не переносятся
        // нарочно: режим прошлой формы по умолчанию для всех строк оставил бы
        // их без инструмента (подбор по диаметру -- только сверловке).
        for(const QString& key: {u"rb_drilling"_s, u"rb_pocket"_s, u"rb_profile"_s, u"rb_on"_s, u"rb_in"_s, u"rb_out"_s, u"rbClimb"_s, u"rbConventional"_s})
            settings.remove(key);
        settings.endGroup();
    }

    //
    connect(ui->pbPickUpTools, &QPushButton::clicked, this, &Form::pickUpTool);

    updateButtonIconSize();
    updateFiles();
    setWindowTitle(tr("GCType::Drill Toolpath"));
    App::drillForm.set(this);
}

Form::~Form() {
    App::drillForm.set(nullptr);

    MySettings settings;
    settings.beginGroup(u"DrillForm"_s);
    settings.setValue(ui->chbxZoomToSelected);
    settings.endGroup();

    // for (auto* var : App::grView().items<Gi::DataSolid>())
    // delete var;

    delete ui;
}

using GrTy = GraphicObject::Type;
Criteria criterias[]{
    {
        std::vector{GraphicObject::FlStamp} //
    },
};

void Form::updateFiles() {
    disconnect(ui->cbxFile, &QComboBox::currentIndexChanged, this, &Form::on_cbxFileCurrentIndexChanged);
    ui->cbxFile->clear();

    // Первыми -- файлы сверловки Excellon (из них по умолчанию и сверлят),
    // среди них металлизированные (PTH) раньше неметаллизированных (NPTH),
    // дальше остальное (круглые объекты Gerber и т.п.). Раньше порядок был
    // порядком загрузки, и по умолчанию выбирался первый попавшийся слой.
    std::vector<AbstractFile*> files;
    for(auto file: App::project().files())
        if(!file->getDataForGC(criterias, GCType::Drill, true).empty())
            files.push_back(file);
    const auto rank = [](const AbstractFile* file) {
        if(file->type() != "Excellon"_hash32) return 2;
        return file->shortName().contains(u"NPTH"_s, Qt::CaseInsensitive) ? 1 : 0;
    };
    std::ranges::stable_sort(files, {}, rank);
    for(auto file: files)
        ui->cbxFile->addItem(file->icon(), file->shortName(), QVariant::fromValue(file));

    if(auto shapes = App::project().shapes(::Gi::Type::ShCircle); shapes.size()) {
        ui->cbxFile->addItem(shapes.front()->icon(), shapes.front()->name(), QVariant::fromValue(shapes));
    }

    if(ui->cbxFile->count())
        on_cbxFileCurrentIndexChanged();

    connect(ui->cbxFile, &QComboBox::currentIndexChanged, this, &Form::on_cbxFileCurrentIndexChanged);
}

bool Form::canToShow() {
    // if (App::project().files(FileType::Excellon).size() > 0)
    return true;

    // QComboBox cbx;
    // for (auto type : {FileType::Gerber_, FileType::Dxf_}) {
    // for (auto file : App::project().files(type)) {
    // App::filePlugin(int(file->type()))->addToGcForm(file, &cbx);
    // if (cbx.count())
    // return true;
    // }
    // }
    // QMessageBox::information(nullptr, {}, tr("No data to process."));
    // return false;
}

void Form::initToolTable() {
    ui->toolTable->setIconSize(QSize(IconSize, IconSize));
    ui->toolTable->setContextMenuPolicy(Qt::CustomContextMenu);
    ui->toolTable->horizontalHeader()->setContextMenuPolicy(Qt::CustomContextMenu);
    ui->toolTable->setWordWrap(false);
    ui->toolTable->horizontalHeader()->setMinimumHeight(ui->toolTable->verticalHeader()->defaultSectionSize());

    connect(ui->toolTable, &QTableView::customContextMenuRequested, this, &Form::on_customContextMenuRequested);

    connect(ui->toolTable->horizontalHeader(), &QHeaderView::customContextMenuRequested, this, &Form::customContextMenuRequested);
    connect(ui->toolTable, &QTableView::doubleClicked, this, &Form::on_doubleClicked);

    auto cornerButton = ui->toolTable->findChild<QAbstractButton*>();
    if(!cornerButton)
        exit(-67);
    header = new Header{Qt::Vertical, ui->toolTable};
    ui->toolTable->setVerticalHeader(header);
    checkBox = new QCheckBox{cornerButton};
    checkBox->setFocusPolicy(Qt::NoFocus);
    checkBox->setGeometry(Header::getRect(cornerButton->rect()) /*.translated(1, -4)*/);
    connect(checkBox, &QCheckBox::clicked, [this](bool checked) {
        if(1) { // NOTE CheckBox behavior #112
            Qt::CheckState state = header->checkBoxState;
            header->setAll(checked ? (state == Qt::Unchecked ? 1 : 0) : 0);
        } else {
            // qWarning() << checkBox->isChecked() << checkBox->isTristate() << checkBox->checkState() << checked;
            // header->setAll((checkBox->checkState() == Qt::PartiallyChecked) ^ checked);
            // header->setAll((checkBox->checkState() == Qt::PartiallyChecked) ^ checked);
            checkBox->setChecked(!!checkBox->checkState());
            header->setAll(!checkBox->checkState());
        }
    });
    connect(header, &Header::onChecked, [this](int /*idx*/) {
        if(model) {
            // fl -- отмеченные (общая галка), ready -- те, что пойдут в расчёт:
            // галка без инструмента только подсвечивает отверстия.
            int fl{}, ready{};
            for(int i{}; i < model->rowCount(); ++i)
                if(model->useForCalc(i)) {
                    ++fl;
                    if(model->toolId(i) > Tool::ID::Null) ++ready;
                }
            if(1) { // NOTE CheckBox behavior #112
                header->checkBoxState = !fl ? Qt::Unchecked : (fl == model->rowCount() ? Qt::Checked : Qt::PartiallyChecked);
                checkBox->setCheckState(header->checkBoxState);
            } else {
                checkBox->setCheckState(!fl ? Qt::Unchecked
                                            : (fl == model->rowCount() ? Qt::Checked
                                                                       : Qt::PartiallyChecked));
            }
            pbCreate->setEnabled(ready);
        }
    });
}

void Form::on_cbxFileCurrentIndexChanged() {
    auto execFile = [this] {
        qDebug() << file << file->id();
        if(!App::project().contains(file)) return;
        // auto peview = std::any_cast<Preview>(App::filePlugin(int(file->type()))->getDataForGC(file, plugin));
        // auto peview = ui->cbxFile->currentData().value<std::vector<const GraphicObject*>>();
        // for (auto* var : peview)
        // qDebug() << var->name << var->pos;

        using Key = std::pair<QString, bool>;
        using Val = std::vector<const GraphicObject*>;
        std::map<Key, Val> map;

        static std::vector<GraphicObject> gos;
        gos = file->getDataForGC(criterias, GCType::Drill);

        for(auto& var: gos) {
            // if(var.raw.has_value())
            // qWarning() << std::any_cast<double>(var.raw);
            map[Key{var.name, var.path.size() > 1}].emplace_back(&var);
        }

        model = new Model{map.size(), ui->toolTable};
        auto& data = model->data();

        QColor color{App::settings().theme() == DarkTheme ? Qt::white : Qt::black};

        for(int i{}; auto& [key, val]: map) {
            auto& row = data[i++];
            row.icon = !key.second ? drawIcon(val.front()->fill, color) : drawDrillIcon(key.second ? Qt::red : color);
            row.name = QString(key.first).split(u'|');
            row.name.back() += u": Ø"_s + QString::number(std::any_cast<double>(val.front()->raw));
            row.diameter = std::any_cast<double>(val.front()->raw);
            row.isSlot = key.second;
            for(auto* go: val)
                new Gi::Preview{
                    (go->path.size() > 1 ? Geo::Polyline{go->path} : Geo::Polyline{Geo::Vertex{go->pos}}),
                    row.diameter,
                    data.back().toolId,
                    row,
                    go->fill.contours()};
        }
    };
    auto execShapes = [this] {
        auto shapes = ui->cbxFile->currentData(Qt::UserRole).value<std::vector<Shapes::AbstractShape*>>();
        qInfo() << shapes.size();

        using Key = std::pair<QString, double>;
        using Val = std::vector<Shapes::AbstractShape*>;
        std::map<Key, Val> map;

        for(auto* shape: shapes)
            if(shape->isVisible())
                map[Key{shape->name(), std::any_cast<double>(shape->getVal(ShCirc::Shape::Diameter))}].emplace_back(shape);

        model = new Model{map.size(), ui->toolTable};
        auto& data = model->data();

        QColor color{App::settings().theme() == DarkTheme ? Qt::white : Qt::black};

        for(int i{}; auto& [key, shapes]: map) {
            auto& row = data[i++];
            row.icon = shapes.front()->icon();
            row.name = QString(key.first).split(u'|');
            row.diameter = std::any_cast<double>(shapes.front()->getVal(ShCirc::Shape::Diameter));
            row.name.back() += u": Ø%1"_s.arg(row.diameter);
            row.isSlot = false;
            for(auto* shape: shapes)
                new Gi::Preview{
                    Geo::Polyline{Geo::Vertex{std::any_cast<QPointF>(shape->getVal(ShCirc::Shape::Center))}},
                    row.diameter,
                    data.back().toolId,
                    row,
                    Geo::fromPath(shape->shape())};
        }
    };
    try {
        file = ui->cbxFile->currentData(Qt::UserRole).value<AbstractFile*>();
        if(file) execFile();
        else execShapes();
        App::grView().scene()->update();
    } catch(const std::exception& exc) {
        qDebug("%s: %s", __FUNCTION__, exc.what()); //-V576
        return;
    } catch(...) {
        exit(-99);
        // delete ui->toolTable->model();
        return;
    }

    pickUpTool();

    delete ui->toolTable->model();
    ui->toolTable->setModel(model);
    connect(model, &Model::set, header, &Header::set);
    initModeBoxes();
    initEditButtons();
    ui->toolTable->resizeColumnsToContents();
    ui->toolTable->verticalHeader()->setSectionResizeMode(QHeaderView::Fixed);
    ui->toolTable->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    ui->toolTable->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    // Ширина -- по спискам режима и кнопкам правки (index-виджеты view
    // учитывает в sizeHint столбца).
    ui->toolTable->horizontalHeader()->setSectionResizeMode(Model::Mode, QHeaderView::ResizeToContents);
    ui->toolTable->horizontalHeader()->setSectionResizeMode(Model::Edit, QHeaderView::ResizeToContents);
    connect(ui->toolTable->selectionModel(), &QItemSelectionModel::selectionChanged, this, &Form::on_selectionChanged);

    header->onChecked();
}

Tool::Operation Form::operation(const Tool& tool, Mode mode, bool slot) const {
    return Tool::programOperation(tool.type(), mode == Mode::Drilling && !slot);
}

Tool Form::resolved(const Tool& tool, Mode mode, bool slot) const {
    return App::toolHolder().resolve(tool, operation(tool, mode, slot), App::project().material());
}

Tool Form::rowTool(const Row& row) const { return row.toolOverride ? *row.toolOverride : resolved(row.tool(), row.mode, row.isSlot); }

void Form::initModeBoxes() {
    if(!model)
        return;
    for(int row{}; row < model->rowCount(); ++row) {
        auto* box = new ModeBox{ui->toolTable};
        // Фокус не нужен: стрелки клавиатуры меняли бы режим молча, как колесо.
        box->setFocusPolicy(Qt::NoFocus);
        box->setFrame(false);
        // Ширина -- по самому длинному режиму, а не по текущему: столбец
        // подгоняется под списки один раз, смена режима его не перекраивает.
        box->setSizeAdjustPolicy(QComboBox::AdjustToContents);
        box->setToolTip(tr("How to process these holes. If the row is selected, the choice applies to all selected rows."));
        for(Mode mode: modes)
            box->addItem(Model::modeIcon(mode), Model::modeName(mode), static_cast<int>(mode));
        // activated -- только выбор пользователя; режим из модели ставит
        // updateModeBox(), и сюда он не приходит.
        connect(box, &QComboBox::activated, this, [this, box, row, tableModel = model](int index) {
            if(model != tableModel)
                return;
            // Строка среди выделенных -- режим всем выделенным, как и правка
            // инструмента; иначе -- ей одной.
            std::vector<int> rows{row};
            if(ui->toolTable->selectionModel()->isSelected(model->index(row, Model::Tool)))
                rows = selectedRows();
            setRowsMode(rows, static_cast<Mode>(box->itemData(index).toInt()));
            updateModeBox(row); // режим мог не смениться -- список снова по модели
        });
        // Index-виджет view удаляет сам при смене или удалении модели.
        ui->toolTable->setIndexWidget(model->index(row, Model::Mode), box);
        updateModeBox(row);
    }
    // Режим строки меняют и не через её список: выбор на нескольких строках,
    // сверло вместо фрезы (Model::setToolId), правка УП. И доступные режимы --
    // по инструменту строки: всё это приходит через dataChanged.
    connect(model, &QAbstractItemModel::dataChanged, this, [this, tableModel = model](const QModelIndex& topLeft, const QModelIndex& bottomRight) {
        if(model != tableModel)
            return;
        for(int row = topLeft.row(); row <= bottomRight.row(); ++row)
            updateModeBox(row);
    });
}

void Form::updateModeBox(int row) {
    if(!model || row < 0 || row >= model->rowCount())
        return;
    auto* box = qobject_cast<QComboBox*>(ui->toolTable->indexWidget(model->index(row, Model::Mode)));
    if(!box)
        return;
    // Чего инструмент строки не умеет -- серым и с причиной в подсказке пункта.
    auto* items = qobject_cast<QStandardItemModel*>(box->model());
    for(int i{}; i < box->count(); ++i) {
        const bool allowed = model->modeAllowed(row, static_cast<Mode>(box->itemData(i).toInt()));
        if(QStandardItem* item = items ? items->item(i) : nullptr) {
            item->setEnabled(allowed);
            // Недоступный режим бывает только у строки с инструментом (сверло,
            // лазер), и инструмент этот известен -- modeAllowed его нашёл.
            item->setToolTip(allowed ? QString{}
                    : model->tool(row).type() == Tool::Drill
                    ? tr("A drill only drills: to mill these holes, choose an end mill")
                    : tr("A laser does not drill: to drill these holes, choose a drill or an end mill"));
        }
    }
    box->setCurrentIndex(box->findData(static_cast<int>(model->mode(row))));
}

void Form::fillModeMenu(QMenu* menu, const std::vector<int>& rows) {
    for(Mode mode: modes) {
        auto* action = menu->addAction(Model::modeIcon(mode), Model::modeName(mode));
        action->setCheckable(true);
        action->setChecked(!rows.empty() && r::all_of(rows, [this, mode](int row) { return model->mode(row) == mode; }));
        // Доступен, если его умеет инструмент хоть одной строки; строки, чей
        // инструмент не умеет (сверло -- фрезеровку), остаются как были.
        action->setEnabled(r::any_of(rows, [this, mode](int row) { return model->modeAllowed(row, mode); }));
        connect(action, &QAction::triggered, this, [this, rows, mode, tableModel = model] {
            if(model == tableModel)
                setRowsMode(rows, mode);
        });
    }
}

void Form::setRowsMode(const std::vector<int>& rows, Mode mode) {
    if(!model)
        return;
    const auto& tools = App::toolHolder().tools();
    for(int row: rows) {
        const Row& data = model->data()[row];
        const Mode before = data.mode;
        if(before == mode || !model->setMode(row, mode))
            continue;
        // Правка инструмента «только для этой УП» сделана под операцию прежнего
        // режима: подачи и обороты сверловки фрезеровке не годятся (и наоборот).
        // Если наборы режимов дают инструменту базы под эти операции разное,
        // правка уходит -- строка берёт инструмент базы под новую операцию. Без
        // наборов правка операции не касается и остаётся. Инструмента в базе уже
        // нет -- правка единственная его копия, она остаётся всегда.
        if(!data.toolOverride)
            continue;
        if(auto it = tools.find(data.toolId); it != tools.end()
            && operation(it->second, before, data.isSlot) != operation(it->second, mode, data.isSlot)
            && resolved(it->second, before, data.isSlot).hash() != resolved(it->second, mode, data.isSlot).hash())
            model->setToolOverride(row, std::nullopt);
    }
}

void Form::setRowsTool(const std::vector<int>& rows, const Tool& tool) {
    if(!model)
        return;
    QStringList refused;
    for(int row: rows) {
        // Паз -- только концевой фрезой: его фрезеруют и при сверловке. Строке
        // отверстий -- то, что умеет её режим, и сверло всегда: строку в
        // режиме фрезеровки оно переводит на сверловку (Model::setToolId).
        if(!toolFits(tool.type(), model->mode(row), model->isSlot(row))) {
            refused.append(model->data()[row].name.back());
            continue;
        }
        // Новый выбор заменяет старый инструмент независимо от текущей галки --
        // setToolId сам взводит её обратно.
        model->setToolId(row, tool.id());
    }
    if(!refused.isEmpty())
        QMessageBox::information(this, {}, tr("\"%1\" is not suitable for: %2").arg(tool.name(), refused.join(u", "_s)));
}

std::vector<Tool::Type> Form::toolTypesOf(const std::vector<int>& rows) const {
    std::vector<Tool::Type> types;
    if(!model)
        return types;
    for(Tool::Type type: {Tool::Drill, Tool::EndMill, Tool::Engraver, Tool::Laser})
        if(r::any_of(rows, [this, type](int row) { return r::contains(toolTypes(model->mode(row), model->isSlot(row)), type); }))
            types.push_back(type);
    return types;
}

std::vector<int> Form::selectedRows() const {
    std::vector<int> rows;
    for(const QModelIndex& index: selectedIndexes())
        rows.push_back(index.row());
    r::sort(rows);
    rows.erase(r::unique(rows).begin(), rows.end());
    return rows;
}

void Form::initEditButtons() {
    if(!model)
        return;
    for(int row{}; row < model->rowCount(); ++row) {
        auto* button = new QToolButton{ui->toolTable};
        button->setFocusPolicy(Qt::NoFocus);
        button->setAutoRaise(true);
        button->setIcon(QIcon::fromTheme(u"document-edit"_s));
        button->setToolTip(tr("Edit tool for this toolpath only (the tool database is not changed)"));
        // Править можно только назначенный инструмент. pickUpTool() отработал
        // ещё до setModel() и без этой связи -- начальное состояние из модели.
        button->setEnabled(model->toolId(row) > Tool::ID::Null);
        connect(button, &QToolButton::clicked, this, [this, row] { editRowTool(row); });
        // Index-виджет view удаляет сам при смене или удалении модели.
        ui->toolTable->setIndexWidget(model->index(row, Model::Edit), button);
    }
    connect(model, &Model::set, this, [this, tableModel = model](int row, bool on) {
        if(auto* button = ui->toolTable->indexWidget(tableModel->index(row, Model::Edit)))
            button->setEnabled(on);
    });
}

void Form::editRowTool(int row) {
    if(!model || row < 0 || row >= model->rowCount() || model->toolId(row) <= Tool::ID::Null)
        return;
    const Model* const tableModel = model;
    const Tool::ID id = model->toolId(row);

    // Правится КОПИЯ действующего инструмента строки -- база не трогается.
    // Действующий -- с набором режимов под операцию строки (её режим) и
    // материал проекта: его УП и получит, его и правят.
    const bool slot = model->data()[row].isSlot;
    const Mode mode = model->mode(row);
    const Tool current = rowTool(model->data()[row]);
    const Tool::Operation rowOperation = operation(current, mode, slot);
    ToolEditDialog dialog{this};
    dialog.setOperation(rowOperation);
    dialog.setTool(current);
    // setTool() сам взводит признак правки (поля дёргают valueChanged) --
    // сброс: OK без единой правки ничего не меняет.
    dialog.setWindowModified(false);
    if(dialog.exec() != QDialog::Accepted || !dialog.isWindowModified())
        return;
    // Пока был открыт диалог, таблицу могли перестроить.
    if(model != tableModel || model->toolId(row) != id || model->mode(row) != mode)
        return;

    Tool tool = dialog.tool();
    // Диалог при OK обнуляет id (tool_editdialog.cpp) -- возвращаем id базы:
    // на нём держатся связь строки с базой, ключ программы и восстановление
    // при правке УП.
    tool.setId(id);
    // Правка, совпавшая с тем, что дала бы база (с набором режимов), -- уже не правка.
    ToolOverride toolOverride;
    const auto& tools = App::toolHolder().tools();
    if(auto it = tools.find(id); it == tools.end() || resolved(it->second, mode, slot).hash() != tool.hash())
        toolOverride = std::move(tool);

    // Строка среди выделенных -- правка всем выделенным с тем же инструментом
    // базы и той же операцией: правка сделана под операцию строки, подачи
    // сверловки фрезеровке отверстий не годятся.
    std::set<int> rows{row};
    if(ui->toolTable->selectionModel()->isSelected(model->index(row, Model::Tool)))
        for(const QModelIndex& index: selectedIndexes())
            if(model->toolId(index.row()) == id && operation(current, model->mode(index.row()), model->isSlot(index.row())) == rowOperation)
                rows.insert(index.row());
    for(int r: rows)
        model->setToolOverride(r, toolOverride);
}

void Form::selectRowTool(ToolDatabase& tdb, int row) const {
    if(!model || row < 0 || row >= model->rowCount() || model->toolId(row) <= Tool::ID::Null)
        return;
    // Ни правки, ни инструмента в базе (его удалили) -- подсвечивать нечего, а
    // Row::tool() -- это .at().
    if(!model->hasOverride(row) && !App::toolHolder().tools().contains(model->toolId(row)))
        return;
    // Как ToolSelectorForm::on_pbSelect_clicked: по uid, а у снимка без uid --
    // по id, если под ним тот же инструмент. Правка строки -- копия инструмента
    // базы с его uid, по ней база и находится.
    if(const Tool* found = App::toolHolder().findTool(model->tool(row)))
        tdb.setCurrentTool(found->id());
}

int Form::toolRowOf(const QModelIndexList& indexes, int preferred) const {
    if(!model) return -1;
    if(preferred > -1 && preferred < model->rowCount() && model->toolId(preferred) > Tool::ID::Null)
        return preferred;
    for(const QModelIndex& index: indexes)
        if(model->toolId(index.row()) > Tool::ID::Null)
            return index.row();
    return -1;
}

void Form::on_doubleClicked(const QModelIndex& current) {
    if(!model || current.column() != Model::Tool)
        return;
    const int row = current.row();
    const Model* const tableModel = model;
    // Что строка принимает в своём режиме: паз -- концевую фрезу, сверловка --
    // сверло или фрезу, карман и профиль -- фрезу, гравёр, лазер и сверло
    // (оно переведёт строку на сверловку).
    ToolDatabase tdb(this, toolTypes(model->mode(row), model->isSlot(row)));
    selectRowTool(tdb, row);
    // Пока была открыта база, таблицу могли перестроить.
    if(tdb.exec() && model == tableModel)
        setRowsTool({row}, tdb.tool());
}

void Form::on_selectionChanged(const QItemSelection& selected, const QItemSelection& deselected) {
    for(auto&& index: selected.indexes())
        for(auto&& item: model->data()[index.row()].items)
            item->setSelected(true);
    for(auto&& index: deselected.indexes())
        for(auto&& item: model->data()[index.row()].items)
            item->setSelected(false);
    zoomToSelected();
}

void Form::on_customContextMenuRequested(const QPoint& pos) {

    if(selectedIndexes().isEmpty())
        return;
    QMenu menu;
    // Режим -- всем выделенным строкам, чей инструмент его умеет.
    fillModeMenu(menu.addMenu(tr("&Mode")), selectedRows());

    // База -- на инструменте строки под курсором (иначе первой выделенной с ним).
    const int toolRow = toolRowOf(selectedIndexes(), ui->toolTable->indexAt(pos).row());
    menu.addAction(QIcon::fromTheme(u"view-form"_s), tr("&Select Tool"), [this, toolRow] {
        const std::vector<int> rows = selectedRows();
        const Model* const tableModel = model;
        // Всё, что принимает хоть одна выделенная строка; что какой строке не
        // подошло, скажет setRowsTool().
        ToolDatabase tdb(this, toolTypesOf(rows));
        selectRowTool(tdb, toolRow);
        if(tdb.exec() && model == tableModel)
            setRowsTool(rows, tdb.tool());
    });

    // Правка "только для этой УП": строка под курсором, иначе первая выделенная
    // с инструментом; уходит и остальным выделенным с тем же инструментом базы.
    int editRow = -1;
    if(const QModelIndex at = ui->toolTable->indexAt(pos); at.isValid() && model->toolId(at.row()) > Tool::ID::Null)
        editRow = at.row();
    else
        for(QModelIndex current: selectedIndexes())
            if(model->toolId(current.row()) > Tool::ID::Null) {
                editRow = current.row();
                break;
            }
    if(editRow > -1)
        menu.addAction(QIcon::fromTheme(u"document-edit"_s), tr("&Edit tool for this toolpath…"), [this, editRow] {
            editRowTool(editRow);
        });

    // Сброс -- только там, где инструмент строки ещё есть в базе.
    auto resettable = [this](int row) {
        return model->hasOverride(row) && App::toolHolder().tools().contains(model->toolId(row));
    };
    for(QModelIndex current: selectedIndexes()) {
        if(resettable(current.row())) {
            menu.addAction(QIcon::fromTheme(u"edit-undo"_s), tr("Reset to &database tool"), [this, resettable] {
                for(QModelIndex current: selectedIndexes())
                    if(resettable(current.row()))
                        model->setToolOverride(current.row(), std::nullopt);
            });
            break;
        }
    }

    for(QModelIndex current: selectedIndexes()) {
        if(model->toolId(current.row()) != Tool::ID::Null) {
            menu.addAction(QIcon::fromTheme(u"list-remove"_s), tr("&Remove Tool"), [this] {
                for(QModelIndex current: selectedIndexes())
                    model->setToolId(current.row(), Tool::ID::Null);
            });
            break;
        }
    }

    menu.exec(ui->toolTable->viewport()->mapToGlobal(pos));
}

void Form::customContextMenuRequested(const QPoint& pos) {
    if(!model)
        return;
    QMenu menu;
    // Режим всем строкам сразу -- то, что раньше задавал переключатель на всю
    // форму; строки, чей инструмент его не умеет, остаются как были.
    std::vector<int> all;
    for(int row{}; row < model->rowCount(); ++row)
        all.push_back(row);
    fillModeMenu(menu.addMenu(tr("&Mode for everyone")), all);

    menu.addAction(QIcon::fromTheme(u"view-form"_s), tr("&Choose a Tool for everyone"), [this, all] {
        ui->toolTable->selectAll();
        const Model* const tableModel = model;
        ToolDatabase tdb(this, toolTypesOf(all));
        selectRowTool(tdb, toolRowOf(selectedIndexes()));
        if(tdb.exec() && model == tableModel)
            setRowsTool(all, tdb.tool());
    });
    menu.addAction(QIcon::fromTheme(u"list-remove"_s), tr("&Remove Tool for everyone"), [this] {
        ui->toolTable->selectAll();
        for(QModelIndex current: selectedIndexes())
            model->setToolId(current.row(), Tool::ID::Null);
        for(int i{}; i < model->rowCount(); ++i)
            if(model->toolId(i) != Tool::ID::Null)
                return;
    });
    menu.exec(ui->toolTable->horizontalHeader()->mapToGlobal(pos));
}

// Подбор -- и при показе файла, и кнопкой. Прежде кнопка подбирала только
// строкам без инструмента, а после показа файла он был уже у всех -- нажатие
// не меняло ничего. Теперь она возвращает строкам автоматический выбор: и
// выбранное вручную, и правку «только для этой УП» -- тоже. А брал прежний
// подбор первый подходящий по диаметру инструмент в порядке базы -- по
// убыванию id (Tools -- std::greater), сверло или фрезу без разбора: фрезы
// заводят обычно после свёрл, у них id больше, и отверстие под размер фрезы
// получало фрезу.
void Form::pickUpTool() {
    if(!model) return;
    // Каждый раз заново: базу могли править, пока форма открыта.
    const TreeOrder order = toolTreeOrder();
    const auto& tools = App::toolHolder().tools();
    for(int row{}; row < model->rowCount(); ++row) {
        const Row& data = model->data()[row];
        // Строки кармана и профиля pickTool() не подбирает: фреза под них
        // должна быть меньше отверстия, а насколько -- решает пользователь.
        const Tool::ID id = pickTool(data.mode, data.isSlot, data.diameter, tools, order);
        // Подходящего нет -- строка остаётся как была: выбранное вручную под
        // отверстие без своего сверла (3.0 под 3.2) кнопка не стирает.
        if(id <= Tool::ID::Null)
            continue;
        // Уже он, и без правки -- менять нечего (и галку тоже).
        if(id == data.toolId && !data.toolOverride)
            continue;
        model->setToolId(row, id); // новый выбор сбрасывает и правку
    }
}

void Form::errorOccurred() {
    [[maybe_unused]] auto tpc = (GCode::Creator*)sender();
    // tpc->continueCalc(ErrorDialog(std::move(tpc->items), this).exec());
}

QModelIndexList Form::selectedIndexes() const { return ui->toolTable->selectionModel()->selectedIndexes(); }

void Form::zoomToSelected() {
    if(ui->chbxZoomToSelected->isChecked())
        App::grView().zoomToSelected();
}

void Form::computePaths() {
    if(!model)
        return;
    auto indexes = [](const auto& range) {
        QString indexes;
        for(int32_t id: range) {
            if(indexes.size())
                indexes += u","_s;
            indexes += QString::number(id);
        }
        return indexes;
    };

    // Последняя УП синхронных веток (пазы, простые точки): они добавляют УП
    // сами, мимо fileHandler(), и раскладывают дерево здесь же, в конце.
    int32_t revealId = -1;

    // Очередь прогонов к Create не пуста в одном случае: прогон отменили, а
    // Create нажали снова, пока он ещё не прислал файл (кнопка «Cancel» снова
    // «Create» сразу после отмены). Он досчитывается -- его файл уйдёт в
    // корзину; не начатые прогоны того клика больше не нужны.
    const bool runInFlight = !pendingRuns_.empty();
    if(runInFlight) {
        pendingRuns_.erase(std::next(pendingRuns_.begin()), pendingRuns_.end());
        pendingRuns_.front().discard = true;
    }

    // УП сверловки -- в проект: на место прежней УП того же ключа за этот показ
    // формы, иначе новой. Галки строк снимаются только теперь, когда УП по ним
    // реально получилась.
    auto place = [this, &revealId](const ProgramKey& key, File* gcode, const std::vector<int>& rows) {
        if(auto it = createdFileIds_.find(key); it != createdFileIds_.end() && App::project().file(it->second))
            App::project().replaceFile(it->second, gcode);
        else
            createdFileIds_[key] = App::project().addFile(gcode);
        if(gcode->id() > -1) revealId = gcode->id();
        for(int row: rows)
            model->setCreate(row, false);
    };
    // Строка идёт в расчёт: инструмент есть, галка стоит. Что из неё считать --
    // по её режиму.
    auto computed = [](const Row& row) { return row.toolId > Tool::ID::Null && row.useForCalc; };
    // Файл сверловки строк -- в ключе УП: УП другого файла тем же инструментом
    // эти не заменят.
    const int32_t srcFileId = file ? file->id() : -1;

    { // Сверловка, пазы: фрезеруются вдоль путей (drilling.js)
        // Инструмент -- копией в самих данных: у правленной строки он не из
        // базы, и в УП (gcp.tools, а с ним file.tool.note скриптов) должен уйти он.
        // Инструмент базы -- с набором режимов под операцию и материал проекта.
        struct Data {
            Tool tool;
            Geo::Polylines paths;
            std::vector<int> toolsApertures;
        };

        std::map<ToolKey, Data> pathsMap;

        for(int i{}; auto&& row: *model) {
            if(computed(row) && row.mode == Mode::Drilling && row.isSlot) {
                auto& data = pathsMap[toolKeyOf(row)];
                data.tool = rowTool(row);
                data.toolsApertures.push_back(i);
                for(auto& item: row.items) {
                    if(!item->isUsed()) continue;
                    // Паз шире фрезы -- обход контура, отстоящего от осевой на
                    // ПОЛОВИНУ разницы ширин: тогда край реза ляжет ровно в край
                    // паза. Было на всю разницу -- паз выходил шире на неё же.
                    if(item->fit(dsbxDepth->value()))
                        for(Geo::Polyline& path: offset(item->paths().front(), (item->sourceDiameter() - item->tool().diameter()) * 0.5))
                            data.paths.push_back(path);
                    else
                        data.paths.push_back(item->paths().front());
                }
            }
            ++i;
        }

        for(auto& [key, data]: pathsMap) {
            if(data.paths.size()) {
                GCode::Params slotGcp{data.tool, dsbxDepth->value(), std::move(data.paths)};
                // Пути -- пазы: drilling.js фрезерует их вдоль, а не сверлит по
                // вершинам (раньше он брал лишь вершины ПЕРВОГО пути, прочие
                // пазы терялись). Ключ -- до конструктора: тот сразу пишет текст.
                slotGcp.params[GCode::Params::SlotPaths] = 1;
                auto* gcode = new File{std::move(slotGcp)};
                gcode->setRows(rowRefs(data.toolsApertures));
                gcode->setWorckType(static_cast<int>(GCType::Drill));
                gcode->setSrcFileId(file ? file->id() : -1);
                gcode->setFileName(
                    programTag(data.tool, key)
                    + u"_T"_s + indexes(data.toolsApertures));
                if(file) gcode->setSide(file->side());
                gcode->setGroup(drillGroup(file));
                place({srcFileId, Mode::Drilling, true, key}, gcode, data.toolsApertures);
            }
        }
    }

    { // Сверловка, отверстия: точками
        struct Data {
            Tool tool; // действующий: правка строки или инструмент базы
            Geo::Polyline drillPath;
            std::vector<int> toolsApertures;
        };

        std::map<ToolKey, Data> pathsMap;

        for(int i{}; auto&& row: *model) {
            if(computed(row) && row.mode == Mode::Drilling && !row.isSlot) {
                auto& data = pathsMap[toolKeyOf(row)];
                data.tool = rowTool(row);
                data.toolsApertures.push_back(i);
                // Лазер не сверлит (и в сверловку строку с ним Model не пускает).
                // Было "!= Engraver || != Laser" -- истинно всегда; гравёр
                // (V-фреза) оставлен: им накернивают центры.
                if(data.tool.type() != Tool::Laser)
                    for(auto& item: row.items)
                        if(item->isUsed())
                            data.drillPath.emplace_back(item->pos());
            }
            ++i;
        }

        for(auto& [key, val]: pathsMap) {
            if(val.drillPath.size()) {
                GCode::sortByProximity(val.drillPath, App::home().pos());
                // Центры отверстий едут одной полилинией: saveDrill читает их
                // из toolPathss, а не как контур -- полигоном тут ничего не
                // задаётся, у набора точек нет ни площади, ни обхода.
                auto* gcode = new File{
                    GCode::Params{
                                  val.tool,
                                  dsbxDepth->value(),
                                  Geo::Polylines{std::move(val.drillPath)},
                                  }
                };

                gcode->setRows(rowRefs(val.toolsApertures));
                gcode->setWorckType(static_cast<int>(GCType::Drill));
                gcode->setSrcFileId(file ? file->id() : -1);
                gcode->setFileName(programTag(val.tool, key) + /*type_ +*/ indexes(val.toolsApertures));
                if(file) gcode->setSide(file->side());
                gcode->setGroup(drillGroup(file));
                place({srcFileId, Mode::Drilling, false, key}, gcode, val.toolsApertures);
            }
        }
    }

    { // Фрезеровка отверстий и пазов: карман и профиль, всегда внутри
        struct Data {
            Tool tool;
            Geo::Polylines paths;
            std::vector<int> toolsApertures;
        };

        // По режиму -- обработка с направлением: одна фреза за клик даёт
        // столько УП, сколько режимов у её строк.
        std::map<std::pair<Mode, ToolKey>, Data> pathsMap;

        for(int i{}; auto&& row: *model) {
            // Сверло не фрезерует: Model строку с ним в фрезеровку не пускает, а
            // попади она туда всё же -- не обрабатывается вовсе.
            if(computed(row) && row.mode != Mode::Drilling && row.tool().type() != Tool::Drill) {
                auto& data = pathsMap[{row.mode, toolKeyOf(row)}];
                data.tool = rowTool(row);
                data.toolsApertures.push_back(i);
                // Контур отверстия (у паза -- его тело): карман выбирает его
                // целиком, профиль идёт по стенке изнутри.
                for(auto& item: row.items)
                    if(item->isUsed())
                        data.paths.append_range(item->offset());
            }
            ++i;
        }

        for(auto& [modeKey, data]: pathsMap) {
            if(data.paths.empty())
                continue;
            const auto& [mode, key] = modeKey;
            const ProgramKey programKey{srcFileId, mode, false, key};
            std::vector<int32_t> replaced;
            if(auto it = createdFileIds_.find(programKey); it != createdFileIds_.end() && App::project().file(it->second))
                replaced = {it->second};
            GCode::Params runGcp;
            runGcp.setConvent(isConventional(mode));
            // Отверстие обрабатывается только изнутри: и карман, и профиль.
            runGcp.setSide(GCode::Inner);
            runGcp.tools = {data.tool};
            runGcp.params[GCode::Params::Depth] = dsbxDepth->value();
            runGcp.closedCurves = Geo::Polygons{data.paths};
            pendingRuns_.push_back({rowRefs(data.toolsApertures), programKey, programTag(data.tool, key) + modeTag(mode),
                std::move(replaced), std::move(runGcp)});
        }
    }

    // Прогоны профиля и кармана -- по одному (см. PendingRun): первый --
    // сейчас, следующие -- по мере прихода файлов. Пока досчитывается
    // отменённый, первый запустит его файл.
    if(!runInFlight && !pendingRuns_.empty())
        startRun();

    if(revealId > -1) {
        GCode::layoutAndReveal(revealId);
        // Готовую УП -- в буфер обмена, как у асинхронных веток (их копирует
        // GCode::Form::fileHandler): пазы и простые точки идут мимо него.
        if(auto* ready = App::project().file<GCode::File>(revealId)) ready->copyToClipboard();
    }

    QTimer::singleShot(1, Qt::CoarseTimer, [this] { header->onChecked(); });
}

std::vector<RowRef> Form::rowRefs(const std::vector<int>& indexes) const {
    std::vector<RowRef> refs;
    if(!model) return refs;
    const auto& rows = model->data();
    for(int i: indexes)
        if(0 <= i && std::cmp_less(i, rows.size()))
            refs.emplace_back(rows[i].name.join(u'|'), rows[i].toolId, rows[i].useForCalc, rows[i].toolOverride.has_value());
    return refs;
}

void Form::uncheckRows(const std::vector<RowRef>& rows) {
    if(!model) return;
    auto& data = model->data();
    for(const RowRef& ref: rows)
        for(size_t i{}; i < data.size(); ++i)
            if(data[i].name.join(u'|') == ref.name) {
                model->setCreate(static_cast<int>(i), false);
                break;
            }
}

void Form::startRun() {
    PendingRun& run = pendingRuns_.front();
    // Общие поля базы -- этого прогона: по ним startProgress() подписывает
    // полосу, а createToolpath забирает параметры в рабочий поток.
    fileName_ = run.programName;
    gcp = std::move(run.gcp);
    if(isPocket(run.key.mode))
        setCreator(new PocketOffset::Creator);
    else
        setCreator(new Profile::Creator);
    fileCount = 1;
    emit createToolpath(&gcp);
}

void Form::fileHandler(GCode::File* file_) {
    // Сюда приходят только профиль и карман по отверстиям, и голова очереди --
    // прогон, приславший этот файл (см. PendingRun). Синхронные ветки (пазы,
    // простые точки) добавляют УП сами, мимо fileHandler().
    PendingRun run;
    const bool isAsyncRun = !pendingRuns_.empty();
    if(isAsyncRun) {
        run = std::move(pendingRuns_.front());
        pendingRuns_.pop_front();
        if(run.discard) {
            // Прогон отменённого клика: его файл в проект не идёт, а за ним
            // ждёт своей очереди новый Create.
            delete file_;
            if(!pendingRuns_.empty()) startRun();
            return;
        }
        fileName_ = run.programName;
        // Что заменить -- своё, из очереди, и файл у прогона один: база по
        // fileCount == 0 убирает лишнее и раскладывает дерево -- для каждого
        // прогона.
        replacedIds_ = std::move(run.replacedIds);
        fileCount = 1;
    }

    // Файл отменённого прогона база удаляет -- после её вызова трогать file_
    // можно, только если он принят. Принятый уже в проекте и со своим id; по
    // имени программы id не найти: имя -- инструмент, и у УП из другого файла
    // сверловки или из фигур оно то же.
    const bool accepted = file_ && !canceled();

    // Профиль и карман считает чужой Creator, а результат приходит асинхронно:
    // галки снимаем только теперь, когда успех уже известен -- файл есть, и
    // прогон не отменён.
    if(accepted)
        uncheckRows(run.rows);

    const int32_t srcFileId = run.key.srcFileId;
    if(auto* drillFile = dynamic_cast<File*>(file_); drillFile) {
        drillFile->setRows(std::move(run.rows));
        drillFile->setWorckType(static_cast<int>(isPocket(run.key.mode) ? GCType::Pocket : GCType::Profile));
        drillFile->setSrcFileId(srcFileId);
    }
    // Профиль и карман по отверстиям -- в группу файла сверловки, к сверловке
    // и пазам. Разложит и покажет база, в конце прогона. Файл -- прогона, а не
    // выбранный в форме сейчас: пока прогон ждал очереди, выбрать могли другой.
    if(file_) file_->setGroup(drillGroup(srcFileId > -1 ? App::project().file(srcFileId) : nullptr));

    GCode::Form::fileHandler(file_);

    if(isAsyncRun && accepted) {
        createdFileIds_[run.key] = file_->id();
        if(srcFileId > -1) {
            auto& programs = srcPrograms_[srcFileId];
            std::pair<int32_t, QString> entry{file_->id(), file_->programName()};
            if(std::find(programs.begin(), programs.end(), entry) == programs.end()) // замена на месте -- тот же id
                programs.push_back(std::move(entry));
        }
    }

    // Следующий прогон -- только теперь, когда этот досчитан. Отмена уносит и
    // все не начатые прогоны своего клика.
    if(canceled())
        pendingRuns_.clear();
    if(!pendingRuns_.empty())
        startRun();
    else
        header->onChecked(); // галки сняты: «Create» и общая галка -- по таблице
}

void Form::editFile(GCode::File* file_) {
    auto* drillFile = dynamic_cast<File*>(file_);
    if(!drillFile) return GCode::Form::editFile(file_);

    // Режим строк этой УП: сверловка (точки и пазы). УП с профилем или карманом
    // по отверстиям на всю форму (worckType) -- тот же режим строкам, направление
    // -- из её параметров; сторона не переносится: профиль отверстий теперь
    // всегда внутри. В проектах без ключа worckType -- 0, это сверловка.
    const GCode::Params& params = drillFile->params();
    const Mode mode = modeOf(static_cast<GCType>(drillFile->worckType()),
        params.params.contains(GCode::Params::Convent) && params.convent());

    // Таблица строится по выбранному в cbxFile файлу: без него имена строк не с
    // чем сопоставлять. Смена индекса перестраивает модель синхронно.
    if(drillFile->srcFileId() > -1)
        for(int i{}; i < ui->cbxFile->count(); ++i)
            if(auto* f = ui->cbxFile->itemData(i, Qt::UserRole).value<AbstractFile*>();
                f && f->id() == drillFile->srcFileId()) {
                ui->cbxFile->setCurrentIndex(i);
                break;
            }

    if(model) {
        // Восстанавливаются ТОЛЬКО свои строки: у остальных галка снимается,
        // иначе повторный расчёт захватил бы чужое.
        std::map<QString, const RowRef*> byName;
        for(const RowRef& ref: drillFile->rows()) byName.emplace(ref.name, &ref);

        auto& rows = model->data();
        for(size_t i{}; i < rows.size(); ++i) {
            auto it = byName.find(rows[i].name.join(u'|'));
            const bool mine = it != byName.end();
            if(mine && it->second->toolId > Tool::ID::Null) {
                // Правка "только для этой УП" живёт лишь копией в самой УП
                // (gcp.tools) -- оттуда и восстанавливается. Копия берётся и
                // тогда, когда инструмента в базе уже нет: иначе .at() уронил бы
                // и таблицу, и превью.
                ToolOverride toolOverride;
                if(it->second->overridden || !App::toolHolder().tools().contains(it->second->toolId))
                    toolOverride = drillFile->tool();
                model->setToolId(static_cast<int>(i), it->second->toolId, std::move(toolOverride));
            }
            // Режим -- после инструмента: строку со сверлом Model в фрезеровку
            // не пустит. Правка инструмента из УП остаётся -- она под этот режим.
            if(mine)
                model->setMode(static_cast<int>(i), mode);
            model->setCreate(static_cast<int>(i), mine && it->second->useForCalc);
        }
        header->onChecked();

        // «Обновить» заменяет эту самую УП, а не кладёт рядом копию: свои УП
        // форма помнит по ключу (createdFileIds_), а на правку из дерева
        // открывается с чистой памятью (showEvent). Ключ -- тот, что
        // computePaths() даст её строкам. Только если в форме -- её файл: на
        // чужом (свой закрыт) строки с теми же именами -- другие отверстия.
        const int32_t srcFileId = file ? file->id() : -1;
        if(mode == Mode::Drilling && srcFileId == drillFile->srcFileId())
            for(const Row& row: rows)
                if(row.toolId > Tool::ID::Null && byName.contains(row.name.join(u'|'))) {
                    createdFileIds_[{srcFileId, Mode::Drilling, params.slotPaths(), toolKeyOf(row)}] = drillFile->id();
                    break;
                }
    }

    GCode::Form::editFile(file_);
}

void Form::updateName() { }

void Form::hideEvent(QHideEvent* event) { // NOTE clean and hide pr gi
    // Свернули окно программы (spontaneous) -- форма не закрыта: таблица с
    // выбранным файлом и инструментами строк остаётся. Прежде она удалялась,
    // и после разворачивания форма начинала с первого файла без инструментов.
    if(event->spontaneous()) return QWidget::hideEvent(event);
    delete ui->toolTable->model();
    model = nullptr;
    event->accept();
}

} // namespace Drilling

#include "moc_drill_form.cpp"
