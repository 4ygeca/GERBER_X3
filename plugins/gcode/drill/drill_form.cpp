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

#include <QHeaderView>
#include <QMenu>
#include <QMessageBox>
#include <QPainter>
#include <QThread>
#include <QTimer>
#include <QToolBar>
#include <QToolButton>
#include <QWidget>
#include <algorithm>
#include <set>

namespace Drilling {

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
    ui->rb_drilling->setChecked(true);

    {
        MySettings settings;
        settings.beginGroup(u"DrillForm"_s);
        settings.getValue(ui->rbConventional, true);
        settings.getValue(ui->rb_drilling, true);
        settings.getValue(ui->rb_in, true);
        settings.getValue(ui->rb_on);
        settings.getValue(ui->rb_out);
        settings.getValue(ui->rb_pocket);
        settings.getValue(ui->rb_profile);
        settings.getValue(ui->rbClimb);
        settings.getValue(ui->chbxZoomToSelected);
        settings.endGroup();
    }

    //
    connect(ui->pbPickUpTools, &QPushButton::clicked, this, &Form::pickUpTool);
    connect(ui->rb_drilling, &QRadioButton::clicked, this, &Form::updateState);
    connect(ui->rb_in, &QRadioButton::clicked, this, &Form::updateState);
    connect(ui->rb_on, &QRadioButton::clicked, this, &Form::updateState);
    connect(ui->rb_out, &QRadioButton::clicked, this, &Form::updateState);
    connect(ui->rb_pocket, &QRadioButton::clicked, this, &Form::updateState);
    connect(ui->rb_profile, &QRadioButton::clicked, this, &Form::updateState);

    updateButtonIconSize();
    updateState();
    updateFiles();
    setWindowTitle(tr("GCType::Drill Toolpath"));
    App::drillForm.set(this);
}

Form::~Form() {
    App::drillForm.set(nullptr);

    MySettings settings;
    settings.beginGroup(u"DrillForm"_s);
    settings.setValue(ui->rbClimb);
    settings.setValue(ui->rbConventional);
    settings.setValue(ui->rb_drilling);
    settings.setValue(ui->rb_in);
    settings.setValue(ui->rb_on);
    settings.setValue(ui->rb_out);
    settings.setValue(ui->rb_pocket);
    settings.setValue(ui->rb_profile);
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

    for(auto file: App::project().files()) {
        auto gos = file->getDataForGC(criterias, GCType::Drill, true);
        if(gos.size())
            ui->cbxFile->addItem(file->icon(), file->shortName(), QVariant::fromValue(file));
    }

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
            int fl{};
            for(int i{}; i < model->rowCount(); ++i)
                if(model->useForCalc(i)) ++fl;
            if(1) { // NOTE CheckBox behavior #112
                header->checkBoxState = !fl ? Qt::Unchecked : (fl == model->rowCount() ? Qt::Checked : Qt::PartiallyChecked);
                checkBox->setCheckState(header->checkBoxState);
            } else {
                checkBox->setCheckState(!fl ? Qt::Unchecked
                                            : (fl == model->rowCount() ? Qt::Checked
                                                                       : Qt::PartiallyChecked));
            }
            pbCreate->setEnabled(fl);
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
    initEditButtons();
    ui->toolTable->resizeColumnsToContents();
    ui->toolTable->verticalHeader()->setSectionResizeMode(QHeaderView::Fixed);
    ui->toolTable->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    ui->toolTable->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    // Ширина -- по кнопке правки (index-виджеты view учитывает в sizeHint столбца).
    ui->toolTable->horizontalHeader()->setSectionResizeMode(Model::Edit, QHeaderView::ResizeToContents);
    connect(ui->toolTable->selectionModel(), &QItemSelectionModel::selectionChanged, this, &Form::on_selectionChanged);

    header->onChecked();
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
    ToolEditDialog dialog{this};
    dialog.setTool(model->tool(row));
    // setTool() сам взводит признак правки (поля дёргают valueChanged) --
    // сброс: OK без единой правки ничего не меняет.
    dialog.setWindowModified(false);
    if(dialog.exec() != QDialog::Accepted || !dialog.isWindowModified())
        return;
    // Пока был открыт диалог, таблицу могли перестроить.
    if(model != tableModel || model->toolId(row) != id)
        return;

    Tool tool = dialog.tool();
    // Диалог при OK обнуляет id (tool_editdialog.cpp) -- возвращаем id базы:
    // на нём держатся связь строки с базой, ключ программы и восстановление
    // при правке УП.
    tool.setId(id);
    // Правка, совпавшая с инструментом базы, -- уже не правка.
    ToolOverride toolOverride;
    const auto& tools = App::toolHolder().tools();
    if(auto it = tools.find(id); it == tools.end() || it->second.hash() != tool.hash())
        toolOverride = std::move(tool);

    // Строка среди выделенных -- правка всем выделенным с тем же инструментом базы.
    std::set<int> rows{row};
    if(ui->toolTable->selectionModel()->isSelected(model->index(row, Model::Tool)))
        for(const QModelIndex& index: selectedIndexes())
            if(model->toolId(index.row()) == id)
                rows.insert(index.row());
    for(int r: rows)
        model->setToolOverride(r, toolOverride);
}

void Form::on_doubleClicked(const QModelIndex& current) {
    if(current.column() == Model::Tool) {
        // Тот же выбор типов, что в контекстном меню: паз -- концевая фреза,
        // сверловка -- сверло или фреза, обработка по отверстиям -- любой.
        const std::vector<Tool::Type> tools = model->isSlot(current.row())
            ? std::vector<Tool::Type>{Tool::EndMill}
            : (worckType == GCType::Drill) ? std::vector<Tool::Type>{Tool::Drill, Tool::EndMill}
                                           : std::vector<Tool::Type>{Tool::Drill, Tool::EndMill, Tool::Engraver, Tool::Laser};
        ToolDatabase tdb(this, tools);
        if(tdb.exec()) {
            const Tool tool(tdb.tool());
            model->setToolId(current.row(), tool.id());
        }
    }
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
    menu.addAction(QIcon::fromTheme(u"view-form"_s), tr("&Select Tool"), [this] {
        bool flag = true;
        for(QModelIndex current: selectedIndexes()) {
            flag = model->isSlot(current.row());
            if(!flag)
                break;
        }

        std::vector<Tool::Type> tools;
        if(flag)
            tools = std::vector<Tool::Type>{Tool::EndMill};
        else
            tools = (worckType == GCType::Drill) ? std::vector<Tool::Type>{Tool::Drill, Tool::EndMill} : std::vector<Tool::Type>{Tool::Drill, Tool::EndMill, Tool::Engraver, Tool::Laser};

        ToolDatabase tdb(this, tools);
        if(tdb.exec()) {
            const Tool tool(tdb.tool());
            for(QModelIndex current: selectedIndexes()) {
                if(model->isSlot(current.row()) && tool.type() == Tool::EndMill) {
                    model->setToolId(current.row(), tool.id());
                } else if(model->isSlot(current.row()) && tool.type() != Tool::EndMill) {
                    QMessageBox::information(this, {}, u"\""_s + tool.name() + tr("\" not suitable for Tu") + model->data(current.sibling(current.row(), 0), Qt::UserRole).toString() + u"_s-u"_s + model->data(current.sibling(current.row(), 0)).toString() + u"_s-"_s);
                } else if(!model->isSlot(current.row())) {
                    // Новый выбор заменяет старый инструмент независимо от текущей
                    // галки -- setToolId сам взводит её обратно.
                    model->setToolId(current.row(), tool.id());
                }
            }
        }
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
    QMenu menu;
    menu.addAction(QIcon::fromTheme(u"view-form"_s), tr("&Choose a Tool for everyone"), [this] {
        ui->toolTable->selectAll();
        bool fl = true;
        for(QModelIndex current: selectedIndexes()) {
            fl = model->isSlot(current.row());
            if(!fl)
                break;
        }
        std::vector<Tool::Type> tools;
        if(fl)
            tools = std::vector<Tool::Type>{Tool::EndMill};
        else
            tools = (worckType == GCType::Drill) ? std::vector<Tool::Type>{Tool::Drill, Tool::EndMill} : std::vector<Tool::Type>{Tool::Drill, Tool::EndMill, Tool::Engraver, Tool::Laser};
        ToolDatabase tdb(this, tools);
        if(tdb.exec()) {
            const Tool tool(tdb.tool());
            for(QModelIndex current: selectedIndexes()) {
                if(model->isSlot(current.row()) && tool.type() == Tool::EndMill) {
                    model->setToolId(current.row(), tool.id());
                } else if(model->isSlot(current.row()) && tool.type() != Tool::EndMill) {
                    QMessageBox::information(this, {}, u"\""_s + tool.name() + tr("\" not suitable for Tu") + model->data(current.sibling(current.row(), 0), Qt::UserRole).toString() + u"_s-u"_s + model->data(current.sibling(current.row(), 0)).toString() + u"_s-"_s);
                } else if(!model->isSlot(current.row())) {
                    // Новый выбор заменяет старый инструмент независимо от текущей
                    // галки -- setToolId сам взводит её обратно.
                    model->setToolId(current.row(), tool.id());
                }
            }
        }
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

void Form::pickUpTool() {
    if(!model) return;

    const double k = 0.05; // 5%
    int ctr{};
    for(const auto& row: model->data()) {
        // model->setToolId(ctr++, 3);
        // continue;
        const double drillDiameterMin = row.diameter * (1.0 - k);
        const double drillDiameterMax = row.diameter * (1.0 + k);

        auto isFit = [&, this](auto& tool) -> bool {
            const auto diameter = tool.getDiameter(dsbxDepth->value());
            return drillDiameterMin <= diameter && drillDiameterMax >= diameter;
        };
        auto set = !row.isSlot ? std::set{Tool::Drill, Tool::EndMill} : std::set{Tool::EndMill};
        auto filter = v::filter([&set](auto& t) { return set.contains(t.second.type()); });
        for(auto& [id, tool]: App::toolHolder().tools() | filter) {
            qWarning() << tool;
            if(model->toolId(ctr) < Tool::ID::Tool && isFit(tool)) {
                model->setToolId(ctr, id);
                break;
            }
        }

        ++ctr;
    }
}

void Form::updateState() {
    worckType = ui->rb_drilling->isChecked() ? GCType::Drill : (ui->rb_profile->isChecked() ? GCType::Profile : GCType::Pocket);
    ui->grbxSide->setEnabled(worckType == GCType::Profile);
    ui->grbxDirection->setEnabled(worckType == GCType::Profile || worckType == GCType::Pocket);
    side = ui->rb_on->isChecked() ? GCode::On : (ui->rb_out->isChecked() ? GCode::Outer : GCode::Inner);
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

    if(worckType == GCType::Drill) { // slots only
        // Инструмент -- копией в самих данных: у правленной строки он не из
        // базы, и в УП (gcp.tools, а с ним file.tool.note скриптов) должен уйти он.
        struct Data {
            Tool tool;
            Geo::Polylines paths;
            std::vector<int> toolsApertures;
        };

        std::map<ToolKey, Data> pathsMap;

        for(int i{}; auto&& row: *model) {
            if(row.toolId > Tool::ID::Null && row.useForCalc && row.isSlot) {
                auto& data = pathsMap[toolKeyOf(row)];
                data.tool = row.tool();
                data.toolsApertures.push_back(i);
                for(auto& item: row.items) {
                    if(!item->isUsed()) continue;
                    if(item->fit(dsbxDepth->value()))
                        for(Geo::Polyline& path: offset(item->paths().front(), item->sourceDiameter() - item->tool().diameter()))
                            data.paths.push_back(path);
                    else
                        data.paths.push_back(item->paths().front());
                }
            }
            ++i;
        }

        for(auto& [key, data]: pathsMap) {
            if(data.paths.size()) {
                auto* gcode = new File{
                    GCode::Params{
                                  data.tool,
                                  dsbxDepth->value(),
                                  std::move(data.paths),
                                  }
                };
                gcode->setRows(rowRefs(data.toolsApertures));
                gcode->setWorckType(static_cast<int>(worckType));
                gcode->setSrcFileId(file ? file->id() : -1);
                gcode->setFileName(
                    programTag(data.tool, key)
                    + u"_T"_s + indexes(data.toolsApertures));
                if(file) gcode->setSide(file->side());
                gcode->setGroup(drillGroup(file));

                auto it = createdSlotFileIds_.find(key);
                if(it != createdSlotFileIds_.end() && App::project().file(it->second))
                    App::project().replaceFile(it->second, gcode);
                else
                    createdSlotFileIds_[key] = App::project().addFile(gcode);
                if(gcode->id() > -1) revealId = gcode->id();

                // УП по этим строкам реально получилась -- только теперь снимаем галки.
                for(int row: data.toolsApertures)
                    model->setCreate(row, false);
            }
        }
    }

    { // other
        struct Data {
            Tool tool; // действующий: правка строки или инструмент базы
            Geo::Polyline drillPath;
            Geo::Polylines paths;
            std::vector<int> toolsApertures;
        };

        std::map<ToolKey, Data> pathsMap;

        for(int i{}; auto&& row: *model) {
            if(row.toolId > Tool::ID::Null && row.useForCalc) {
                auto& data = pathsMap[toolKeyOf(row)];
                data.tool = row.tool();
                const Tool::Type toolType = data.tool.type();
                data.toolsApertures.push_back(i);
                for(auto& item: row.items) {
                    if(!item->isUsed()) continue;
                    // if (item->isSlot())
                    // continue;
                    switch(worckType) {
                    case GCType::Profile:
                        if(toolType != Tool::Drill) {
                            switch(side) {
                            case GCode::On:
                                data.paths.append_range(item->paths());
                                break;
                            case GCode::Outer:
                                data.paths.append_range(item->offset());
                                break;
                            case GCode::Inner:
                                // if (item->fit(dsbxDepth->value())) {
                                data.paths.append_range(item->offset());
                                // }
                                break;
                            }
                        }
                        break;
                    case GCType::Pocket:
                        if(toolType != Tool::Drill) {
                            // if (toolType != Tool::Drill && item->fit(dsbxDepth->value())) {
                            data.paths.append_range(item->offset());
                            // }
                        }
                        break;
                    case GCType::Drill:
                        // Лазер не сверлит. Было "!= Engraver || != Laser" -- истинно
                        // всегда; гравёр (V-фреза) оставлен: им накернивают центры.
                        if(toolType != Tool::Laser) {
                            data.drillPath.emplace_back(item->pos());
                        }
                        break;
                    default:;
                    }
                }
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
                gcode->setWorckType(static_cast<int>(worckType));
                gcode->setSrcFileId(file ? file->id() : -1);
                gcode->setFileName(programTag(val.tool, key) + /*type_ +*/ indexes(val.toolsApertures));
                if(file) gcode->setSide(file->side());
                gcode->setGroup(drillGroup(file));

                auto it = createdDrillFileIds_.find(key);
                if(it != createdDrillFileIds_.end() && App::project().file(it->second))
                    App::project().replaceFile(it->second, gcode);
                else
                    createdDrillFileIds_[key] = App::project().addFile(gcode);
                if(gcode->id() > -1) revealId = gcode->id();

                // УП по этим строкам реально получилась -- только теперь снимаем галки.
                for(int row: val.toolsApertures)
                    model->setCreate(row, false);
            }
            if(val.paths.size()) {
                switch(worckType) {
                case GCType::Profile: {
                    const QString programName = programTag(val.tool, key);
                    std::vector<int32_t> replaced;
                    if(auto it = createdProfileFileIds_.find(key);
                        it != createdProfileFileIds_.end() && App::project().file(it->second))
                        replaced = {it->second};
                    pendingRuns_.push_back({rowRefs(val.toolsApertures), GCType::Profile, key, programName, std::move(replaced)});
                    fileName_ = programName;
                    gcp = {};
                    gcp.setConvent(ui->rbConventional->isChecked());
                    gcp.setSide(side);
                    gcp.tools = {val.tool};
                    gcp.params[GCode::Params::Depth] = dsbxDepth->value();
                    gcp.closedCurves = Geo::Polygons{val.paths};
                    setCreator(new Profile::Creator);
                    fileCount = 1;
                    emit createToolpath(&gcp);
                } break;
                case GCType::Pocket: {
                    const QString programName = programTag(val.tool, key);
                    std::vector<int32_t> replaced;
                    if(auto it = createdPocketFileIds_.find(key);
                        it != createdPocketFileIds_.end() && App::project().file(it->second))
                        replaced = {it->second};
                    pendingRuns_.push_back({rowRefs(val.toolsApertures), GCType::Pocket, key, programName, std::move(replaced)});
                    fileName_ = programName;
                    gcp = {};
                    gcp.setConvent(ui->rbConventional->isChecked());
                    gcp.setSide(GCode::Inner);
                    gcp.tools = {val.tool};
                    gcp.params[GCode::Params::Depth] = dsbxDepth->value();
                    gcp.closedCurves = Geo::Polygons{val.paths};
                    setCreator(new PocketOffset::Creator);
                    fileCount = 1;
                    emit createToolpath(&gcp);
                } break;
                default: continue;
                }
            }
        }
    }

    if(revealId > -1) GCode::layoutAndReveal(revealId);

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

void Form::fileHandler(GCode::File* file_) {
    // Асинхронная ветка Profile/Pocket по отверстиям кладёт свой контекст в
    // pendingRuns_ FIFO; синхронные ветки (пазы, простые точки) сюда не
    // попадают вовсе -- pendingRuns_ для их файлов пуст.
    PendingRun run;
    const bool isAsyncRun = !pendingRuns_.empty();
    if(isAsyncRun) {
        run = std::move(pendingRuns_.front());
        pendingRuns_.pop_front();
        fileName_ = run.programName;
        // Все прогоны клика досчитаны ещё до первого fileReady, и общие поля
        // формы к этому времени держат последний из них. Что заменить -- своё,
        // из очереди (иначе файл первого прогона заменял бы УП последнего), и
        // файл у прогона один: база по fileCount == 0 убирает лишнее и
        // раскладывает дерево -- для каждого прогона, а не только для первого.
        replacedIds_ = std::move(run.replacedIds);
        fileCount = 1;
    }

    // Профиль и карман считает чужой Creator, а результат приходит асинхронно:
    // галки снимаем только теперь, когда успех (file_ != nullptr) уже известен.
    if(file_)
        uncheckRows(run.rows);

    if(auto* drillFile = dynamic_cast<File*>(file_); drillFile) {
        drillFile->setRows(std::move(run.rows));
        drillFile->setWorckType(static_cast<int>(worckType));
        drillFile->setSrcFileId(file ? file->id() : -1);
    }
    // Профиль и карман по отверстиям -- в группу файла сверловки, к сверловке
    // и пазам. Разложит и покажет база, в конце прогона.
    if(file_) file_->setGroup(drillGroup(file));

    // Файл отменённого прогона база удаляет -- после её вызова трогать file_
    // можно, только если он принят. Принятый уже в проекте и со своим id; по
    // имени программы id не найти: имя -- инструмент, и у УП из другого файла
    // сверловки или из фигур оно то же.
    const bool accepted = file_ && !canceled();
    GCode::Form::fileHandler(file_);

    if(isAsyncRun && accepted) {
        auto& map = run.worckType == GCType::Profile ? createdProfileFileIds_ : createdPocketFileIds_;
        map[run.key] = file_->id();
        if(file) {
            auto& programs = srcPrograms_[file->id()];
            std::pair<int32_t, QString> entry{file_->id(), file_->programName()};
            if(std::find(programs.begin(), programs.end(), entry) == programs.end()) // замена на месте -- тот же id
                programs.push_back(std::move(entry));
        }
    }
}

void Form::editFile(GCode::File* file_) {
    auto* drillFile = dynamic_cast<File*>(file_);
    if(!drillFile) return GCode::Form::editFile(file_);

    // Режим -- первым: от него зависит, какие столбцы и кнопки вообще активны.
    switch(static_cast<GCType>(drillFile->worckType())) {
    case GCType::Profile: ui->rb_profile->setChecked(true); break;
    case GCType::Pocket : ui->rb_pocket->setChecked(true); break;
    default             : ui->rb_drilling->setChecked(true); break;
    }
    updateState();

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
            model->setCreate(static_cast<int>(i), mine && it->second->useForCalc);
        }
        header->onChecked();
    }

    GCode::Form::editFile(file_);
}

void Form::updateName() { }

void Form::hideEvent(QHideEvent* event) { // NOTE clean and hide pr gi
    delete ui->toolTable->model();
    model = nullptr;
    event->accept();
}

} // namespace Drilling

#include "moc_drill_form.cpp"
