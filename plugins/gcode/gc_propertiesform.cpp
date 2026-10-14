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
#include "gc_propertiesform.h"
#include "ui_gcodepropertiesform.h"

#include "gc_file.h"
#include "gi_point.h"
#include "project.h"
#include "settings.h"
#include <QMessageBox>
#include <QScopedValueRollback>

namespace GCode {

// Безопасная высота/зазор/глубина подвода влияют на уже сгенерированный текст
// G-кода существующих файлов (File::lines_) — пересобираем его сразу же, а не
// ждём следующего явного "Save Toolpath", иначе экспортированный/показанный
// текст останется устаревшим.

PropertiesForm::PropertiesForm(QWidget* parent)
    : QWidget{parent}
    , ui(new Ui::GCodePropertiesForm) {
    ui->setupUi(this);

    connect(ui->dsbxClearence, &QDoubleSpinBox::valueChanged, [this](double value) {
        if(value > ui->dsbxSafeZ->value())
            ui->dsbxSafeZ->setValue(value);
        if(value < ui->dsbxPlunge->value())
            ui->dsbxPlunge->setValue(value);
    });

    connect(ui->dsbxPlunge, &QDoubleSpinBox::valueChanged, [this](double value) {
        if(value > ui->dsbxSafeZ->value())
            ui->dsbxSafeZ->setValue(value);
        if(value > ui->dsbxClearence->value())
            ui->dsbxClearence->setValue(value);
    });

    connect(ui->dsbxHomeX, &QDoubleSpinBox::valueChanged, App::home.ptr(), &Gi::Marker::setPosX);
    connect(ui->dsbxHomeY, &QDoubleSpinBox::valueChanged, App::home.ptr(), &Gi::Marker::setPosY);
    connect(ui->dsbxZeroX, &QDoubleSpinBox::valueChanged, App::zero.ptr(), &Gi::Marker::setPosX);
    connect(ui->dsbxZeroY, &QDoubleSpinBox::valueChanged, App::zero.ptr(), &Gi::Marker::setPosY);

    connect(ui->dsbxGlue, &QDoubleSpinBox::valueChanged, App::project.ptr(), &Project::setGlue);
    connect(ui->dsbxSpaceX, &QDoubleSpinBox::valueChanged, App::project.ptr(), &Project::setSpaceX);
    connect(ui->dsbxSpaceY, &QDoubleSpinBox::valueChanged, App::project.ptr(), &Project::setSpaceY);
    connect(ui->sbxStepsX, &QSpinBox::valueChanged, App::project.ptr(), &Project::setStepsX);
    connect(ui->sbxStepsY, &QSpinBox::valueChanged, App::project.ptr(), &Project::setStepsY);

    // Поля, от которых зависит текст УП: Home/Zero (маркеры) и мультипликация
    // (шаг и число копий, по ним скрипты раскладывают копии). Применяются они
    // сразу (выше), а УП пересобираются один раз -- когда правка закончена:
    // ушёл фокус или нажат Enter (editingFinished), либо OK. До тех пор
    // отложенный пересчёт ждёт (isEditing), иначе вставал бы на каждую цифру.
    // Шаг и число копий пересчёт заказывают и сами (LayoutFrames::updateRect),
    // Home/Zero -- здесь.
    auto edited = [this](QAbstractSpinBox* box, bool requestRegen) {
        // Поля Home/Zero вслед за маркером пишет updatePosDsbxs -- на каждое
        // движение мыши, пока маркер тащат (поля с тремя знаками округляют
        // положение, и значение меняется почти всегда). Пересчёт тогда -- по
        // отпусканию маркера, а не здесь.
        if(syncingPos_) return;
        if(box->hasFocus())
            editing_ = true; // набор, стрелки, колесо -- ждём конца правки
        else if(requestRegen)
            scheduleRegenerateGCodeFiles(); // кнопки сброса, откат к снимку
    };
    for(QDoubleSpinBox* box: {ui->dsbxHomeX, ui->dsbxHomeY, ui->dsbxZeroX, ui->dsbxZeroY})
        connect(box, &QDoubleSpinBox::valueChanged, this, [edited, box] { edited(box, true); });
    for(QDoubleSpinBox* box: {ui->dsbxSpaceX, ui->dsbxSpaceY})
        connect(box, &QDoubleSpinBox::valueChanged, this, [edited, box] { edited(box, false); });
    for(QSpinBox* box: {ui->sbxStepsX, ui->sbxStepsY})
        connect(box, &QSpinBox::valueChanged, this, [edited, box] { edited(box, false); });
    for(QAbstractSpinBox* box: std::initializer_list<QAbstractSpinBox*>{ui->dsbxHomeX, ui->dsbxHomeY, ui->dsbxZeroX,
            ui->dsbxZeroY, ui->dsbxSpaceX, ui->dsbxSpaceY, ui->sbxStepsX, ui->sbxStepsY})
        connect(box, &QAbstractSpinBox::editingFinished, this, [this] {
            if(std::exchange(editing_, false)) scheduleRegenerateGCodeFiles();
        });

    connect(ui->dsbxClearence, &QDoubleSpinBox::valueChanged, App::project.ptr(), &Project::setClearence);
    connect(ui->dsbxPlunge, &QDoubleSpinBox::valueChanged, App::project.ptr(), &Project::setPlunge);
    connect(ui->dsbxSafeZ, &QDoubleSpinBox::valueChanged, App::project.ptr(), &Project::setSafeZ);
    connect(ui->dsbxThickness, &QDoubleSpinBox::valueChanged, App::project.ptr(), &Project::setBoardThickness);

    // connect(ui->dsbxClearence, &QDoubleSpinBox::valueChanged, this, regenerateGCodeFiles);
    // connect(ui->dsbxPlunge, &QDoubleSpinBox::valueChanged, this, regenerateGCodeFiles);
    // connect(ui->dsbxSafeZ, &QDoubleSpinBox::valueChanged, this, regenerateGCodeFiles);

    connect(ui->dsbxCopperThickness, &QDoubleSpinBox::valueChanged, App::project.ptr(), &Project::setCopperThickness);

    connect(ui->dsbxSafeZ, &QDoubleSpinBox::valueChanged, [this](double value) {
        ui->dsbxSafeZ->setValue(value);
        ui->dsbxSafeZ->setValue(value);
        if(value < ui->dsbxClearence->value())
            ui->dsbxClearence->setValue(value);
        if(value < ui->dsbxPlunge->value())
            ui->dsbxPlunge->setValue(value);
    });

    load();

    connect(ui->pbOk, &QPushButton::clicked, [this, parent] {
        editing_ = false; // пересчёт ниже -- за всё сразу, и сразу
        regenerateGCodeFiles();
        if(ui->dsbxThickness->value() > 0.0
            && ui->dsbxCopperThickness->value() > 0.0
            && ui->dsbxClearence->value() > 0.0
            && ui->dsbxSafeZ->value() > 0.0) {

            committed_ = true; // подтверждено — hideEvent не должен откатывать изменения
            if(parent) parent->close();
            else close(); // без родительского диалога (докнутая панель) закрываемся сами
            return;
        }
        if(ui->dsbxCopperThickness->value() == 0.0) ui->dsbxCopperThickness->flicker();
        if(ui->dsbxThickness->value() == 0.0) ui->dsbxThickness->flicker();
        if(ui->dsbxClearence->value() == 0.0) ui->dsbxClearence->flicker();
    });

    ui->pbOk->setIcon(QIcon::fromTheme(u"dialog-ok-apply"_s));

    for(auto* button: findChildren<QPushButton*>())
        button->setIconSize({16, 16});

    App::gcPropertiesForm.set(this);
}

PropertiesForm::~PropertiesForm() {
    App::gcPropertiesForm.set(nullptr);
    save();
    delete ui;
}

void PropertiesForm::showEvent(QShowEvent* event) {
    QWidget::showEvent(event);
    committed_ = false;
    snapshot_ = {
        .safeZ = ui->dsbxSafeZ->value(),
        .clearence = ui->dsbxClearence->value(),
        .plunge = ui->dsbxPlunge->value(),
        .thickness = ui->dsbxThickness->value(),
        .copperThickness = ui->dsbxCopperThickness->value(),
        .glue = ui->dsbxGlue->value(),
        .spaceX = ui->dsbxSpaceX->value(),
        .spaceY = ui->dsbxSpaceY->value(),
        .stepsX = ui->sbxStepsX->value(),
        .stepsY = ui->sbxStepsY->value(),
        .homeX = ui->dsbxHomeX->value(),
        .homeY = ui->dsbxHomeY->value(),
        .zeroX = ui->dsbxZeroX->value(),
        .zeroY = ui->dsbxZeroY->value(),
    };
}

void PropertiesForm::hideEvent(QHideEvent* event) {
    QWidget::hideEvent(event);
    if(!committed_) applySnapshot(snapshot_);
    // Скрытой форме править нечего: начатая правка (и откат снимка в поле с
    // фокусом) -- в пересчёт сейчас, не дожидаясь ухода фокуса.
    if(std::exchange(editing_, false)) scheduleRegenerateGCodeFiles();
}

void PropertiesForm::applySnapshot(const Snapshot& s) {
    // Порядок важен: safeZ/clearence/plunge подтягивают друг друга через свои
    // valueChanged-обработчики, поэтому восстанавливаем их последними в этой тройке
    // друг за другом, чтобы финальным значением осталось именно значение снимка.
    ui->dsbxThickness->setValue(s.thickness);
    ui->dsbxCopperThickness->setValue(s.copperThickness);
    ui->dsbxGlue->setValue(s.glue);
    ui->dsbxSpaceX->setValue(s.spaceX);
    ui->dsbxSpaceY->setValue(s.spaceY);
    ui->sbxStepsX->setValue(s.stepsX);
    ui->sbxStepsY->setValue(s.stepsY);
    ui->dsbxHomeX->setValue(s.homeX);
    ui->dsbxHomeY->setValue(s.homeY);
    ui->dsbxZeroX->setValue(s.zeroX);
    ui->dsbxZeroY->setValue(s.zeroY);
    ui->dsbxSafeZ->setValue(s.safeZ);
    ui->dsbxClearence->setValue(s.clearence);
    ui->dsbxPlunge->setValue(s.plunge);
}

void PropertiesForm::updatePosDsbxs() {
    // Пересчёта УП здесь нет: метод зовут на каждое движение мыши, пока
    // маркер тащат, и пересчёт всех УП на каждое движение подвешивал программу.
    // Пересобирает тот, кто маркер сдвинул: отпускание, двойной щелчок,
    // авторасстановка, рамка (LayoutFrames::updateRect).
    const QScopedValueRollback syncing{syncingPos_, true};
    ui->dsbxHomeX->setValue(App::home().pos().x());
    ui->dsbxHomeY->setValue(App::home().pos().y());
    ui->dsbxZeroX->setValue(App::zero().pos().x());
    ui->dsbxZeroY->setValue(App::zero().pos().y());
}

void PropertiesForm::updateAll() {
    // ui->dsbxSpaceX;
    // ui->dsbxSpaceY;
    // ui->sbxStepsX;
    // ui->sbxStepsY;
}

void PropertiesForm::load() {
    ui->dsbxSpaceX->setValue(App::project().spaceX());
    ui->dsbxSpaceY->setValue(App::project().spaceY());
    ui->sbxStepsX->setValue(App::project().stepsX());
    ui->sbxStepsY->setValue(App::project().stepsY());

    MySettings settings;
    settings.beginGroup(u"PropertiesForm"_s);
    settings.getValue(ui->dsbxSafeZ, 20);
    settings.getValue(ui->dsbxClearence, 10);
    settings.getValue(ui->dsbxPlunge, 2);
    settings.getValue(ui->dsbxThickness, 1);
    settings.getValue(ui->dsbxCopperThickness, 0.03);
    settings.getValue(ui->dsbxGlue, 0.05);
    settings.endGroup();

    ui->dsbxHomeX->setValue(App::home().pos().x());
    ui->dsbxHomeY->setValue(App::home().pos().y());

    ui->dsbxZeroX->setValue(App::zero().pos().x());
    ui->dsbxZeroY->setValue(App::zero().pos().y());

    App::project().setSafeZ(ui->dsbxSafeZ->value());
    App::project().setBoardThickness(ui->dsbxThickness->value());
    App::project().setCopperThickness(ui->dsbxCopperThickness->value());
    App::project().setClearence(ui->dsbxClearence->value());
    App::project().setPlunge(ui->dsbxPlunge->value());
}

void PropertiesForm::save() {
    if(App::home.ptr()) App::home().setPos(QPointF(ui->dsbxHomeX->value(), ui->dsbxHomeY->value()));
    if(App::zero.ptr()) App::zero().setPos(QPointF(ui->dsbxZeroX->value(), ui->dsbxZeroY->value()));

    MySettings settings;
    settings.beginGroup(u"PropertiesForm"_s);
    settings.setValue(ui->dsbxSafeZ);
    settings.setValue(ui->dsbxClearence);
    settings.setValue(ui->dsbxPlunge);
    settings.setValue(ui->dsbxThickness);
    settings.setValue(ui->dsbxCopperThickness);
    settings.setValue(ui->dsbxGlue);
    settings.endGroup();
    if(!App::project.ptr()) return;
    App::project().setSafeZ(ui->dsbxSafeZ->value());
    App::project().setBoardThickness(ui->dsbxThickness->value());
    App::project().setCopperThickness(ui->dsbxCopperThickness->value());
    App::project().setClearence(ui->dsbxClearence->value());
    App::project().setPlunge(ui->dsbxPlunge->value());
}

void PropertiesForm::on_pbResetHome_clicked() {
    ui->dsbxHomeX->setValue(0);
    ui->dsbxHomeY->setValue(0);
}

void PropertiesForm::on_pbResetZero_clicked() {
    ui->dsbxZeroX->setValue(0);
    ui->dsbxZeroY->setValue(0);
}

} // namespace GCode

#include "moc_gc_propertiesform.cpp"
