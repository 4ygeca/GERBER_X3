// This is an open source non-commercial project. Dear PVS-Studio, please check it.
// PVS-Studio Static Code Analyzer for C, C++, C#, and Java: https://pvs-studio.com
/********************************************************************************
 * Author : Damir Bakiev *
 * Version : na *
 * Date : XXXXX XX, 2025 *
 * Website : na *
 * Copyright : Damir Bakiev 2016-2026 *
 * License : *
 * Use, modification & distribution is subject to Boost Software License Ver 1. *
 * http://www.boost.org/LICENSE_1_0.txt *
 ********************************************************************************/
#include "tool_editform.h"
#include "ui_tooleditform.h"
#include "utils.h"

#include "app.h"
#include "tool_item.h"
#include <QDebug>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QMessageBox>
#include <QRegularExpressionValidator>
#include <QSettings>

#define TR QCoreApplication::translate

ToolEditForm::ToolEditForm(QWidget* parent)
    : QWidget{parent}
    , ui{new Ui::ToolEditForm{}} {
    ui->setupUi(this);
    // clang-format AlignArrayOfStructures: Left
    update = {
        {ui->dsbxAngle,             &ToolEditForm::updateDsbxAngle            },
        {ui->dsbxDiameter,          &ToolEditForm::updateDsbxDiameter         },
        {ui->dsbxMDiam,             &ToolEditForm::updateDsbxHoleDiam         },
        {ui->dsbxFeedRate,          &ToolEditForm::updateDsbxFeedRate         },
        {ui->dsbxLenght,            &ToolEditForm::updateDsbxLenght           },
        {ui->dsbxOneTurnCut,        &ToolEditForm::updateDsbxOneTurnCut       },
        {ui->dsbxOneTurnCutPercent, &ToolEditForm::updateDsbxOneTurnCutPercent},
        {ui->dsbxPlungeRate,        &ToolEditForm::updateDsbxPlungeRate       },
        {ui->dsbxSpindleSpeed,      &ToolEditForm::updateDsbxSpindleSpeed     },
        {ui->dsbxStepover,          &ToolEditForm::updateDsbxStepover         },
        {ui->dsbxStepoverPercent,   &ToolEditForm::updateDsbxStepoverPercent  },
        {ui->dsbxDepth,             &ToolEditForm::updateDsbxPassDepth        },
        {ui->dsbxPass,              &ToolEditForm::updateDsbxPassDepth        },
        {ui->dsbxThreadPitch,       &ToolEditForm::updateDsbxPassDepth        },
    };
    for(auto [dsbx, _]: update)
        connect(dsbx, &QDoubleSpinBox::valueChanged, this, &ToolEditForm::valueChanged);

    get = {
        std::pair{ui->dsbxAngle,        &Tool::angle       },
        std::pair{ui->dsbxMDiam,        &Tool::angle       },
        std::pair{ui->dsbxDepth,        &Tool::passDepth   },
        std::pair{ui->dsbxDiameter,     &Tool::diameter    },
        std::pair{ui->dsbxFeedRate,     &Tool::feedRate    },
        std::pair{ui->dsbxLenght,       &Tool::lenght      },
        std::pair{ui->dsbxOneTurnCut,   &Tool::oneTurnCut  },
        std::pair{ui->dsbxPass,         &Tool::passDepth   },
        std::pair{ui->dsbxPlungeRate,   &Tool::plungeRate  },
        std::pair{ui->dsbxSpindleSpeed, &Tool::spindleSpeed},
        std::pair{ui->dsbxStepover,     &Tool::stepover    },
        std::pair{ui->dsbxThreadPitch,  &Tool::passDepth   },
    };

    set = {
        std::pair{ui->dsbxAngle,        &Tool::setAngle       },
        std::pair{ui->dsbxMDiam,        &Tool::setAngle       },
        std::pair{ui->dsbxDepth,        &Tool::setPassDepth   },
        std::pair{ui->dsbxDiameter,     &Tool::setDiameter    },
        std::pair{ui->dsbxFeedRate,     &Tool::setFeedRate    },
        std::pair{ui->dsbxLenght,       &Tool::setLenght      },
        std::pair{ui->dsbxOneTurnCut,   &Tool::setOneTurnCut  },
        std::pair{ui->dsbxPass,         &Tool::setPassDepth   },
        std::pair{ui->dsbxPlungeRate,   &Tool::setPlungeRate  },
        std::pair{ui->dsbxSpindleSpeed, &Tool::setSpindleSpeed},
        std::pair{ui->dsbxStepover,     &Tool::setStepover    },
        std::pair{ui->dsbxThreadPitch,  &Tool::setPassDepth   },
    };

    connect(ui->cbxFeedSpeeds, &QComboBox::currentIndexChanged, this, [this](int index) {
        double lastFeed = feed;
        // Смена единиц -- не правка подач: точные мм/мин переживают пересчёт полей.
        const double feedRate = tool_.feedRate(), plungeRate = tool_.plungeRate();
        switch(index) {
        case mmPerSec: feed = 1. / 60.; break;   // mm/sec
        case mmPerMin: feed = 1.; break;         // mm/min!!!
        case cmPerMin: feed = 1. / 10.; break;   // cm/min
        case mPerMin : feed = 1. / 1000.; break; // m/min
        default      : break;
        }

        bool fl1 = parentWidget()->isWindowModified();
        bool fl2 = ui->pbApply->isEnabled();

        ui->dsbxFeedRate->setSuffix(u" "_s + ui->cbxFeedSpeeds->currentText());
        ui->dsbxFeedRate->setValue((ui->dsbxFeedRate->value() / lastFeed) * feed);

        ui->dsbxPlungeRate->setSuffix(u" "_s + ui->cbxFeedSpeeds->currentText());
        ui->dsbxPlungeRate->setValue((ui->dsbxPlungeRate->value() / lastFeed) * feed);
        tool_.setFeedRate(feedRate);
        tool_.setPlungeRate(plungeRate);

        parentWidget()->setWindowModified(fl1);
        ui->pbApply->setEnabled(fl2);
    });

    connect(ui->leName, &QLineEdit::textChanged, this, [this](const QString& arg1) { tool_.setName(arg1); setChanged(); });
    connect(ui->leName, &QLineEdit::textEdited, this, [this](const QString& arg1) { tool_.setName(arg1); setChanged(); ui->chbxAutoName->setChecked(false); });
    connect(ui->teNote, &QTextEdit::textChanged, this, [this] { tool_.setNote(ui->teNote->toPlainText()); setChanged(); });
    // Флаги «авто» -- в правку сразу: окно местной правки отдаёт tool_ без Apply.
    connect(ui->chbxAutoName, &QCheckBox::toggled, this, [this](bool checked) { tool_.setAutoName(checked); });

    // Код уходит в комментарии УП, и по нему отправитель ищет инструмент:
    // только [A-Za-z0-9._-], до 20 знаков. Пусто -- автоматический.
    ui->leCode->setValidator(new QRegularExpressionValidator{QRegularExpression{Tool::codePattern()}, ui->leCode});
    ui->leCode->setMaxLength(Tool::MaxCodeLength);
    connect(ui->leCode, &QLineEdit::textChanged, this, [this](const QString& code) { tool_.setCode(code); setChanged(); });
    connect(ui->leCode, &QLineEdit::textEdited, this, [this] { ui->chbxAutoCode->setChecked(false); });
    connect(ui->chbxAutoCode, &QCheckBox::toggled, this, [this](bool checked) { tool_.setAutoCode(checked); });
    connect(ui->chbxAutoCode, &QCheckBox::clicked, this, [this](bool checked) {
        if(checked) updateCode();
        setChanged();
    });

    // Цикл сверления: номера -- договор со скриптами (Tool::DrillCycle).
    ui->cbxDrillCycle->addItem(tr("Auto"), Tool::CycleAuto);
    ui->cbxDrillCycle->addItem(tr("Expand (G0/G1)"), Tool::CycleExpand);
    ui->cbxDrillCycle->addItem(tr("G81"), Tool::CycleG81);
    ui->cbxDrillCycle->addItem(tr("G82 dwell"), Tool::CycleG82);
    ui->cbxDrillCycle->addItem(tr("G83 peck"), Tool::CycleG83);
    ui->cbxDrillCycle->addItem(tr("G73 chip break"), Tool::CycleG73);
    connect(ui->cbxDrillCycle, &QComboBox::currentIndexChanged, this, [this] {
        tool_.setDrillCycle(ui->cbxDrillCycle->currentData().toInt());
        setChanged();
    });
    connect(ui->dsbxDwell, &QDoubleSpinBox::valueChanged, this, [this](double dwell) {
        tool_.setDwell(dwell);
        setChanged();
    });
    connect(ui->chbxRetractToR, &QCheckBox::toggled, this, [this](bool retract) {
        tool_.setRetractToR(retract);
        setChanged();
    });
    // Режим мощности лазера (M4/M3) -- такой же режим резания, как мощность S:
    // у собственных значений и у каждого набора свой.
    connect(ui->chbxLaserDynamic, &QCheckBox::toggled, this, [this](bool dynamic) {
        tool_.setLaserDynamic(dynamic);
        setChanged();
    });

    // Наборы режимов «операция x материал»
    ui->tbAddPreset->setIcon(QIcon::fromTheme(u"list-add"_s));
    ui->tbRemovePreset->setIcon(QIcon::fromTheme(u"list-remove"_s));
    connect(ui->cbxPreset, &QComboBox::activated, this, [this](int index) { selectPreset(ui->cbxPreset->itemData(index).toInt()); });
    connect(ui->tbAddPreset, &QToolButton::clicked, this, &ToolEditForm::addPreset);
    connect(ui->tbRemovePreset, &QToolButton::clicked, this, &ToolEditForm::removePreset);

    // Подписи «оборотов» -- переведённые, из формы: у лазера на их месте мощность.
    spindleLabel_ = ui->lblSpindleSpeed->text();
    spindleSuffix_ = ui->dsbxSpindleSpeed->suffix();

    ui->cbxUnits->setVisible(false);
    ui->cbxToolType->addItem(QIcon::fromTheme(u"endmill"_s), TR("ToolEditForm", "End Mill", nullptr), Tool::EndMill);
    ui->cbxToolType->addItem(QIcon::fromTheme(u"drill"_s), TR("ToolEditForm", "Drill", nullptr), Tool::Drill);
    ui->cbxToolType->addItem(QIcon::fromTheme(u"engraving"_s), TR("ToolEditForm", "Engraver", nullptr), Tool::Engraver);
    ui->cbxToolType->addItem(QIcon::fromTheme(u"thread_mill"_s), TR("ToolEditForm", "Thread Mill", nullptr), Tool::ThreadMill);
    ui->cbxToolType->addItem(QIcon::fromTheme(u"laser"_s), TR("ToolEditForm", "Laser", nullptr), Tool::Laser);

    connect(ui->cbxToolType, &QComboBox::currentIndexChanged, this, &ToolEditForm::setupToolWidgets);

    ui->lblWarn->setPixmap(QIcon::fromTheme(u"window-close"_s).pixmap({16, 16}));
    ui->lblWarn->setToolTip(QApplication::translate("ToolEditForm",
        "If the offset value is more than 50%, unmilled areas are possible.\nThese errors do not appear in the visualization.",
        "При значении отступа более 50% возможны не отфрезерованные участки. Эти ошибки не отображаются в визуализации."));
    ui->lblWarn->setVisible(false);

    QSettings settings;
    ui->cbxFeedSpeeds->setCurrentIndex(settings.value(u"cbxFeedSpeeds"_s).toInt());
    // ui->cbxUnits->setCurrentIndex(settings.value(u"cbxUnits"_s).toInt());
    rebuildPresets();
    setVisibleToolWidgets(false);
    setChanged(false);
}

ToolEditForm::~ToolEditForm() {
    QSettings settings;
    settings.setValue(u"cbxFeedSpeeds"_s, ui->cbxFeedSpeeds->currentIndex());
    // settings.setValue(u"cbxUnits"_s, ui->cbxUnits->currentIndex());
    delete ui;
    ui = nullptr;
}

void ToolEditForm::setItem(ToolItem* item) {
    if(item == nullptr)
        return;
    item_ = item;
    if(item_->isTool()) {
        setTool(item_->tool());
        setVisibleToolWidgets(true);
    } else {
        ui->leName->setText(item_->name());
        ui->teNote->setText(item_->note());
        setVisibleToolWidgets(false);
    }
    setChanged(false);
}

void ToolEditForm::setTool(const Tool& tool) {
    // qDebug(__FUNCTION__);
    tool_ = tool;
    // Наборы и собственные значения -- отдельно: поля показывают одно из них.
    presets_ = tool.presets();
    defaults_ = tool.cuttingData();
    presetIndex_ = -1;

    const std::array dsbxList{
        ui->dsbxAngle,
        ui->dsbxDepth,
        ui->dsbxFeedRate,
        ui->dsbxLenght,
        ui->dsbxOneTurnCut,
        ui->dsbxPass,
        ui->dsbxPlungeRate,
        ui->dsbxStepover,
        ui->dsbxThreadPitch,
    };

    for(DoubleSpinBox* dsbx: dsbxList)
        dsbx->setMaximum(std::numeric_limits<double>::max());

    // Подачи в инструменте -- мм/мин, в полях -- в выбранных единицах (feed),
    // как и в updateDsbxFeedRate/PlungeRate (val / feed). Прежде сюда писались
    // сырые мм/мин, и при мм/сек правка в диалоге возвращала подачи x60.
    for(auto [dsbx, get]: get) {
        const bool rate = dsbx == ui->dsbxFeedRate || dsbx == ui->dsbxPlungeRate;
        dsbx->setValue((tool.*get)() * (rate ? feed : 1.0));
    }
    // Поле округляет до своих знаков -- точные значения берём из инструмента.
    tool_.setFeedRate(tool.feedRate());
    tool_.setPlungeRate(tool.plungeRate());
    loadDrillFields();
    loadLaserFields();

    for(DoubleSpinBox* dsbx: dsbxList)
        dsbx->setProperty("lastVal", qFuzzyIsNull(dsbx->value()) ? QVariant{} : QVariant{dsbx->value()});

    for(int i{}; i < ui->cbxToolType->count(); ++i) {
        if(ui->cbxToolType->itemData(i).value<Tool::Type>() == tool.type()) {
            if(ui->cbxToolType->currentIndex() == i)
                setupToolWidgets(tool_.type());
            ui->cbxToolType->setCurrentIndex(i);
            break;
        }
    }

    rebuildPresets();
    ui->chbxAutoName->setChecked(tool.autoName());
    ui->leName->setText(tool.name());
    ui->teNote->setText(tool.note());
    ui->chbxAutoCode->setChecked(tool.autoCode());
    ui->leCode->setText(tool.code());
    updateWarn();
}

void ToolEditForm::setChanged(bool fl) {
    ui->pbApply->setEnabled(fl);
    parentWidget()->setWindowModified(fl);
}

void ToolEditForm::setVisibleToolWidgets(bool visible) {
    for(auto* w: std::initializer_list<QWidget*>{
            ui->cbxFeedSpeeds,
            ui->cbxToolType,
            ui->cbxUnits,
            ui->chbxAutoName,
            ui->chbxAutoCode,
            ui->grbxCuttingParameters,
            ui->grbxFeedSpeeds,
            ui->grbxGeometry,
            ui->lblCode,
            ui->lblToolType,
            ui->lblUnits,
            ui->leCode,
        })
        w->setVisible(visible);
    // Наборы режимов правятся только в базе: у снимка в окне местной правки их нет.
    ui->wgtPresets->setVisible(visible && dialog_);
    setMinimumWidth(width());
}

void ToolEditForm::setupToolWidgets(int) {
    auto currType = ui->cbxToolType->currentData().value<Tool::Type>();
    tool_.setType(currType);

    // Тип меняется только на собственных значениях инструмента (при показанном
    // наборе список типов закрыт) -- наборы других операций просто не видны.
    rebuildPresets();
    showRows(true);

    setChanged();
    updateName();
    updateCode();
    updateWarn(); // setRowVisible(hlayStepover) показывает и lblWarn -- он в той же строке
}

Tool::Operation ToolEditForm::shownOperation() const {
    if(presetIndex_ >= 0) return presets_[presetIndex_].operation;
    if(operation_) return *operation_; // окно местной правки: операция УП
    // Собственные значения: сверло сверлит, остальные -- по типу.
    return Tool::programOperation(tool_.type(), tool_.type() == Tool::Drill);
}

void ToolEditForm::showRows(bool restoreLast) {
    // строки, которые скрываются/показываются в зависимости от типа инструмента
    const std::array trackedDsbx{
        ui->dsbxAngle,
        ui->dsbxFeedRate,
        ui->dsbxLenght,
        ui->dsbxOneTurnCut,
        ui->dsbxPass,
        ui->dsbxThreadPitch,
        ui->dsbxDepth,
        ui->dsbxPlungeRate,
        ui->dsbxStepover,
        ui->dsbxMDiam,
    };

    // перед скрытием запоминаем значения ещё видимых полей, чтобы восстановить их при возврате к этому типу
    if(restoreLast)
        for(auto* dsbx: trackedDsbx)
            if(dsbx->isVisible()) dsbx->setProperty("lastVal", dsbx->value());

    // скрываем все строки во всех формах — дальше покажем только нужные
    for(QFormLayout* form: {ui->formGeometry, ui->formCutParam, ui->formFeedSpeeds})
        for(int row: v::iota(0, form->rowCount()))
            form->setRowVisible(row, false);

    // строки, общие для любого типа инструмента
    ui->formGeometry->setRowVisible(ui->lblUnits, true);
    ui->formGeometry->setRowVisible(ui->dsbxDiameter, true);
    ui->formFeedSpeeds->setRowVisible(ui->dsbxSpindleSpeed, true);
    ui->formCutParam->setRowVisible(ui->label_2, true); // Vc и число зубьев

    // Значения полей возвращаются только при смене типа. Смена набора режимов
    // ставит в поля значения самого набора (loadCutFields).
    // одиночное поле: виджет сам является и меткой, и полем своей строки формы
    auto show = [restoreLast](QFormLayout* form, DoubleSpinBox* dsbx, double max, std::optional<double> defVal = {}) {
        dsbx->setMaximum(max);
        if(restoreLast) {
            if(auto lastVal = dsbx->property("lastVal"); lastVal.isValid()) // восстановить последнее значение
                dsbx->setValue(lastVal.toDouble());
            else if(defVal) // значение по умолчанию
                dsbx->setValue(defVal.value());
        }
        form->setRowVisible(dsbx, true);
    };

    // парное поле: dsbx делит строку формы с процентным дублёром через вложенный QHBoxLayout
    auto showPair = [restoreLast](QFormLayout* form, QLayout* row, DoubleSpinBox* dsbx, double max) {
        dsbx->setMaximum(max);
        if(auto lastVal = dsbx->property("lastVal"); restoreLast && lastVal.isValid()) // восстановить последнее значение
            dsbx->setValue(lastVal.toDouble());
        form->setRowVisible(row, true);
    };

    const double diameter = ui->dsbxDiameter->value();

    // Геометрия -- по типу: она у инструмента одна, при любом наборе.
    switch(tool_.type()) {
    case Tool::Drill:
        show(ui->formGeometry, ui->dsbxAngle, 180., 120.);
        show(ui->formGeometry, ui->dsbxLenght, 100000.);
        break;
    case Tool::EndMill:
        show(ui->formGeometry, ui->dsbxLenght, 100000.);
        break;
    case Tool::ThreadMill:
        show(ui->formGeometry, ui->dsbxMDiam, 10.);
        show(ui->formGeometry, ui->dsbxLenght, 100000.);
        break;
    case Tool::Engraver:
        show(ui->formGeometry, ui->dsbxAngle, 180., 90.);
        show(ui->formGeometry, ui->dsbxLenght, 100000.);
        break;
    case Tool::Laser: // у лазера геометрия -- только диаметр
    default         : break;
    }

    // Режимы -- по операции показанных значений: фреза с набором сверловки
    // показывает проход (клевок Q) и цикл сверления, как сверло.
    const auto operation = shownOperation();
    switch(operation) {
    case Tool::Operation::Drilling:
        show(ui->formCutParam, ui->dsbxPass, 10.);
        showPair(ui->formCutParam, ui->hlayOneTurnCut, ui->dsbxOneTurnCut, diameter);
        show(ui->formFeedSpeeds, ui->dsbxPlungeRate, 100000.);
        ui->formCutParam->setRowVisible(ui->cbxDrillCycle, true);
        ui->formCutParam->setRowVisible(ui->dsbxDwell, true);
        ui->formCutParam->setRowVisible(ui->chbxRetractToR, true);
        break;
    case Tool::Operation::Milling:
        show(ui->formCutParam, ui->dsbxDepth, 10.);
        show(ui->formFeedSpeeds, ui->dsbxFeedRate, 100000.);
        showPair(ui->formCutParam, ui->hlayOneTurnCut, ui->dsbxOneTurnCut, diameter);
        show(ui->formFeedSpeeds, ui->dsbxPlungeRate, 100000.);
        showPair(ui->formCutParam, ui->hlayStepover, ui->dsbxStepover, diameter);
        break;
    case Tool::Operation::Threading:
        show(ui->formCutParam, ui->dsbxThreadPitch, 10.);
        show(ui->formFeedSpeeds, ui->dsbxFeedRate, 100000.);
        showPair(ui->formCutParam, ui->hlayOneTurnCut, ui->dsbxOneTurnCut, diameter);
        show(ui->formFeedSpeeds, ui->dsbxPlungeRate, 100000.);
        showPair(ui->formCutParam, ui->hlayStepover, ui->dsbxStepover, diameter);
        break;
    case Tool::Operation::Laser:
        show(ui->formFeedSpeeds, ui->dsbxFeedRate, 100000.);
        showPair(ui->formCutParam, ui->hlayStepover, ui->dsbxStepover, diameter);
        ui->formFeedSpeeds->setRowVisible(ui->chbxLaserDynamic, true);
        break;
    }

    // У лазера «обороты» -- мощность (S).
    const bool laser = operation == Tool::Operation::Laser;
    ui->lblSpindleSpeed->setText(laser ? tr("Laser Power (S)") : spindleLabel_);
    ui->dsbxSpindleSpeed->setSuffix(laser ? QString{} : spindleSuffix_);
    updateWarn();
}

void ToolEditForm::rebuildPresets() {
    const QSignalBlocker blocker{ui->cbxPreset};
    ui->cbxPreset->clear();
    ui->cbxPreset->addItem(tr("Tool defaults"), -1);
    // Наборы операций, недопустимых для типа (тип сменили), не показываются и
    // не сохраняются (assembled).
    for(int i{}; std::cmp_less(i, presets_.size()); ++i)
        if(Tool::isOperationAllowed(tool_.type(), presets_[i].operation))
            ui->cbxPreset->addItem(App::toolHolder().presetName(presets_[i]), i);
    ui->cbxPreset->setCurrentIndex(std::max(0, ui->cbxPreset->findData(presetIndex_)));
    updatePresetButtons();
}

void ToolEditForm::updatePresetButtons() {
    ui->tbRemovePreset->setEnabled(presetIndex_ >= 0);
    // Тип -- свойство инструмента: меняется, только пока показаны его собственные значения.
    ui->cbxToolType->setEnabled(dialog_ && presetIndex_ < 0);
}

void ToolEditForm::storeShown() {
    // Поля пишут в tool_ на каждое изменение -- его режимы и есть показанные.
    Tool::CuttingPreset& target = presetIndex_ < 0 ? defaults_ : presets_[presetIndex_];
    Tool::CuttingPreset shown = tool_.cuttingData();
    shown.operation = target.operation;
    shown.material = target.material;
    target = shown;
}

void ToolEditForm::selectPreset(int index) {
    if(index == presetIndex_ || index >= static_cast<int>(presets_.size()))
        return;
    storeShown();
    presetIndex_ = index;
    tool_.setCuttingData(index < 0 ? defaults_ : presets_[index]);
    // Просмотр другого набора -- не правка.
    const bool fl1 = parentWidget()->isWindowModified();
    const bool fl2 = ui->pbApply->isEnabled();
    showRows(false);
    loadCutFields();
    parentWidget()->setWindowModified(fl1);
    ui->pbApply->setEnabled(fl2);
    {
        const QSignalBlocker blocker{ui->cbxPreset};
        ui->cbxPreset->setCurrentIndex(std::max(0, ui->cbxPreset->findData(presetIndex_)));
    }
    updatePresetButtons();
}

void ToolEditForm::loadCutFields() {
    // Значения -- из tool_ (показанные режимы). Подачи -- последними: с
    // галками авторасчёта обороты и съём за оборот пересчитали бы их.
    const double feedRate = tool_.feedRate(), plungeRate = tool_.plungeRate(), passDepth = tool_.passDepth();
    ui->dsbxSpindleSpeed->setValue(tool_.spindleSpeed());
    ui->dsbxOneTurnCut->setValue(tool_.oneTurnCut());
    ui->dsbxStepover->setValue(tool_.stepover());
    // Шаг резьбы резьбофрезы -- её геометрия: поля прохода её не трогают
    // (все три пишут в passDepth).
    if(tool_.type() != Tool::ThreadMill) {
        ui->dsbxPass->setValue(passDepth);
        ui->dsbxDepth->setValue(passDepth);
    }
    ui->dsbxFeedRate->setValue(feedRate * feed);
    ui->dsbxPlungeRate->setValue(plungeRate * feed);
    // Поле округляет до своих знаков -- точные значения из набора.
    tool_.setFeedRate(feedRate);
    tool_.setPlungeRate(plungeRate);
    if(tool_.type() != Tool::ThreadMill)
        tool_.setPassDepth(passDepth);
    loadDrillFields();
    loadLaserFields();
    updateWarn();
}

void ToolEditForm::loadDrillFields() {
    // Поля -- вслед за tool_, сами они его не правят.
    const QSignalBlocker blockCycle{ui->cbxDrillCycle};
    const QSignalBlocker blockDwell{ui->dsbxDwell};
    const QSignalBlocker blockRetract{ui->chbxRetractToR};
    ui->cbxDrillCycle->setCurrentIndex(std::max(0, ui->cbxDrillCycle->findData(tool_.drillCycle())));
    ui->dsbxDwell->setValue(tool_.dwell());
    ui->chbxRetractToR->setChecked(tool_.retractToR());
}

void ToolEditForm::loadLaserFields() {
    // Как и поля сверловки -- вслед за tool_, без правки.
    const QSignalBlocker blocker{ui->chbxLaserDynamic};
    ui->chbxLaserDynamic->setChecked(tool_.laserDynamic());
}

void ToolEditForm::addPreset() {
    // Операция -- из допустимых для типа, материал -- «любой» или из списка базы.
    QDialog dialog{this};
    dialog.setWindowTitle(tr("New cutting data"));
    auto* form = new QFormLayout{&dialog};
    auto* cbxOperation = new QComboBox{&dialog};
    for(Tool::Operation operation: Tool::operations(tool_.type()))
        cbxOperation->addItem(Tool::operationName(operation), static_cast<int>(operation));
    cbxOperation->setCurrentIndex(std::max(0, cbxOperation->findData(static_cast<int>(shownOperation()))));
    auto* cbxMaterial = new QComboBox{&dialog};
    cbxMaterial->addItem(QCoreApplication::translate("ToolHolder", "Any material"), QString{});
    for(const auto& material: App::toolHolder().materials())
        cbxMaterial->addItem(material.name, material.uid);
    form->addRow(tr("Operation:"), cbxOperation);
    form->addRow(tr("Material:"), cbxMaterial);
    auto* buttons = new QDialogButtonBox{QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog};
    form->addRow(buttons);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    if(dialog.exec() != QDialog::Accepted || cbxOperation->currentIndex() < 0)
        return;

    const auto operation = static_cast<Tool::Operation>(cbxOperation->currentData().toInt());
    const QString material = cbxMaterial->currentData().toString();
    if(r::any_of(presets_, [&](const Tool::CuttingPreset& preset) { return preset.operation == operation && preset.material == material; })) {
        QMessageBox::information(this, tr("New cutting data"),
            tr("Cutting data for \"%1\" already exist.").arg(App::toolHolder().presetName({.operation = operation, .material = material})));
        return;
    }

    // Новый набор -- копия показанных значений.
    storeShown();
    Tool::CuttingPreset preset = tool_.cuttingData();
    preset.operation = operation;
    preset.material = material;
    presets_.push_back(std::move(preset));
    rebuildPresets();
    selectPreset(static_cast<int>(presets_.size()) - 1);
    setChanged();
}

void ToolEditForm::removePreset() {
    if(presetIndex_ < 0)
        return;
    // Удаляемый набор не сохраняем -- в поля сразу собственные значения.
    presets_.erase(presets_.begin() + presetIndex_);
    presetIndex_ = -1;
    tool_.setCuttingData(defaults_);
    showRows(false);
    loadCutFields();
    rebuildPresets();
    setChanged();
}

void ToolEditForm::materialsChanged(const std::set<QString>& removed) {
    if(!removed.empty()) {
        storeShown();
        int shown = -1;
        std::vector<Tool::CuttingPreset> kept;
        for(int i{}; std::cmp_less(i, presets_.size()); ++i) {
            if(removed.contains(presets_[i].material)) continue;
            if(i == presetIndex_) shown = static_cast<int>(kept.size());
            kept.push_back(presets_[i]);
        }
        presets_ = std::move(kept);
        // Показанный набор ушёл вместе с материалом -- в поля собственные значения.
        if(presetIndex_ >= 0 && shown < 0) {
            presetIndex_ = -1;
            tool_.setCuttingData(defaults_);
            const bool fl1 = parentWidget()->isWindowModified();
            const bool fl2 = ui->pbApply->isEnabled();
            showRows(false);
            loadCutFields();
            parentWidget()->setWindowModified(fl1);
            ui->pbApply->setEnabled(fl2);
        } else {
            presetIndex_ = shown;
        }
    }
    rebuildPresets(); // имена материалов могли смениться
}

// Набор режимов годен для УП: те же нули, что ловит isValid() у собственных
// значений инструмента, -- по операции набора.
static bool cuttingPresetComplete(const Tool::CuttingPreset& preset) {
    switch(preset.operation) {
    case Tool::Operation::Drilling : return preset.passDepth > 0.0 && preset.plungeRate > 0.0;
    case Tool::Operation::Milling  : return preset.passDepth > 0.0 && preset.feedRate > 0.0 && preset.stepover > 0.0 && preset.plungeRate > 0.0;
    case Tool::Operation::Laser    : return preset.feedRate > 0.0 && preset.stepover > 0.0;
    case Tool::Operation::Threading: return preset.feedRate > 0.0 && preset.plungeRate > 0.0; // шаг резьбы -- геометрия
    }
    return true;
}

Tool ToolEditForm::assembled() {
    storeShown();
    Tool tool{tool_};
    tool.setCuttingData(defaults_);
    std::vector<Tool::CuttingPreset> presets;
    for(const Tool::CuttingPreset& preset: presets_)
        if(Tool::isOperationAllowed(tool.type(), preset.operation))
            presets.push_back(preset);
    tool.setPresets(std::move(presets));
    return tool;
}

void ToolEditForm::warnDuplicateCode(const Tool& tool) {
    // Код уходит в УП, и по нему отправитель ищет инструмент: одинаковые коды
    // -- повод предупредить, но не запрет.
    QStringList same;
    for(const auto& [id, other]: App::toolHolder().tools())
        if(id != tool.id() && other.code().compare(tool.code(), Qt::CaseInsensitive) == 0)
            same.push_back(other.name());
    if(same.isEmpty())
        return;
    QMessageBox::warning(this, tr("Duplicate tool code"),
        tr("The code \"%1\" is also used by:\n%2\n\nThe code goes into the G-code, and the sender finds the tool by it.")
            .arg(tool.code(), same.join(u'\n')));
}

void ToolEditForm::valueChanged(double val) {
    if(auto dsbx = qobject_cast<QDoubleSpinBox*>(sender()); dsbx)
        (this->*update[dsbx])(val);
    updateWarn(); // WARNING возможны 'непрорезы'
    updateName();
    updateCode();
    setChanged();
}

void ToolEditForm::updateWarn() {
    // Ровно 50 % (ввод в процентах) не должен зажигать значок из-за погрешности double.
    const double stepover = ui->dsbxStepover->value();
    const double halfDiameter = ui->dsbxDiameter->value() * 0.5;
    // У сверла строки шага нет -- и значку негде стоять.
    ui->lblWarn->setVisible(ui->formCutParam->isRowVisible(ui->hlayStepover)
        && stepover > halfDiameter && !qFuzzyCompare(stepover, halfDiameter));
}

void ToolEditForm::on_pbApply_clicked() {
    bool fl{};

    for(auto [dsbx, set]: set)
        if(dsbx->isVisible() && qFuzzyIsNull(dsbx->value()))
            dsbx->flicker(), fl = true;

    if(fl)
        return;

    // Подачи в tool_ уже в мм/мин (setTool, updateDsbx*Rate) -- из полей их не
    // перечитываем: там выбранные единицы, округлённые до знаков поля.
    for(auto [dsbx, set]: set)
        if(dsbx->isVisible() && dsbx != ui->dsbxFeedRate && dsbx != ui->dsbxPlungeRate)
            (tool_.*set)(dsbx->value());

    // Пустой код -- автоматический: галка снова стоит, код -- по геометрии.
    if(ui->leCode->text().isEmpty()) {
        ui->chbxAutoCode->setChecked(true);
        updateCode();
    }
    // Флаги «авто» -- до копии в базу: прежде autoName ставился уже после неё
    // и доходил до базы только со следующим Apply.
    tool_.setAutoName(ui->chbxAutoName->isChecked());
    tool_.setAutoCode(ui->chbxAutoCode->isChecked());

    if(!item_)
        return;
    const Tool tool = assembled(); // собственные значения режимов + наборы
    if(!tool.isValid())
        return;
    // Нули показанного набора ловит мигание полей выше, а набор, правленный
    // раньше и сменённый на другой, ушёл бы в базу с нулевой подачей или
    // проходом -- и в УП: F0 или проходы по 0 мм.
    for(const Tool::CuttingPreset& preset: tool.presets())
        if(!cuttingPresetComplete(preset)) {
            QMessageBox::warning(this, tr("Cutting data"),
                tr("The cutting data \"%1\" have a zero feed, pass depth or stepover. Fill them in before applying.")
                    .arg(App::toolHolder().presetName(preset)));
            return;
        }
    item_->setName(tool.name());
    item_->setNote(tool.note());
    item_->tool() = tool;
    emit itemChanged(item_);
    setChanged(false);
    warnDuplicateCode(tool);
}

void ToolEditForm::setDialog() {
    dialog_ = false;
    ui->pbApply->setVisible(false);
    ui->cbxToolType->setEnabled(dialog_);
    setVisibleToolWidgets(true);
}

void ToolEditForm::updateName() {
    if(!ui->chbxAutoName->isChecked())
        return;
    ui->leName->setText([this](auto type) {
        switch(type) {
        case Tool::EndMill   : return tr("End Mill (Ø%1 mm)").arg(ui->dsbxDiameter->value());
        case Tool::Engraver  : return tr("Engrave (%2\302\260 %1 mm tip)").arg(ui->dsbxDiameter->value()).arg(ui->dsbxAngle->value());
        case Tool::Drill     : return tr("Drill (Ø%1 mm)").arg(ui->dsbxDiameter->value());
        case Tool::Laser     : return tr("Laser (Ø%1 mm)").arg(ui->dsbxDiameter->value());
        case Tool::ThreadMill: return tr("Thread (Ø%1 mm)").arg(ui->dsbxDiameter->value());
        case Tool::Group     :
        default              : return QString{};
        }
    }(ui->cbxToolType->currentData().value<Tool::Type>()));
}

void ToolEditForm::updateCode() {
    // Код по геометрии из tool_: поля пишут в него до этого вызова.
    if(ui->chbxAutoCode->isChecked())
        ui->leCode->setText(tool_.codeAuto());
}

void ToolEditForm::updateDsbxAngle(double val) {
    // qDebug() << __FUNCTION__ << val;
    tool_.setAngle(val);
}

void ToolEditForm::updateDsbxHoleDiam(double val) {
    tool_.setAngle(val);
}

void ToolEditForm::updateDsbxDiameter(double val) {
    // qDebug() << __FUNCTION__ << val;
    tool_.setDiameter(val);
    ui->dsbxOneTurnCut->setMaximum(val);
    ui->dsbxStepover->setMaximum(val);
    if(ui->dsbxStepover->value() == 0.)
        ui->dsbxStepover->setValue(val * 0.5);
    if(ui->dsbxOneTurnCut->value() == 0.)
        ui->dsbxOneTurnCut->setValue(val * 0.1);
    emit ui->dsbxOneTurnCutPercent->valueChanged(ui->dsbxOneTurnCutPercent->value());
    emit ui->dsbxStepoverPercent->valueChanged(ui->dsbxStepoverPercent->value());
}

void ToolEditForm::updateDsbxFeedRate(double val) {
    // qDebug() << __FUNCTION__ << val;
    tool_.setFeedRate(val / feed);
}

void ToolEditForm::updateDsbxOneTurnCut(double val) {
    // qDebug() << __FUNCTION__ << val;
    tool_.setOneTurnCut(val);
    ui->dsbxOneTurnCutPercent->setValue(tool_.diameter() > 0. ? val / (tool_.diameter() * 0.01) : 0.);
    if(ui->chbxFeedRate->isChecked())
        ui->dsbxFeedRate->setValue(tool_.oneTurnCut() * tool_.spindleSpeed() * feed);
    if(ui->chbxPlungeRate->isChecked())
        ui->dsbxPlungeRate->setValue(tool_.oneTurnCut() * tool_.spindleSpeed() * feed);
}

void ToolEditForm::updateDsbxPassDepth(double val) {
    // qDebug() << __FUNCTION__ << val;
    tool_.setPassDepth(val);
}

void ToolEditForm::updateDsbxPlungeRate(double val) {
    // qDebug() << __FUNCTION__ << val;
    tool_.setPlungeRate(val / feed);
}

void ToolEditForm::updateDsbxSpindleSpeed(double val) {
    // qDebug() << __FUNCTION__ << val;
    tool_.setSpindleSpeed(val); /*rpm*/
    if(ui->chbxFeedRate->isChecked())
        ui->dsbxFeedRate->setValue(tool_.oneTurnCut() * tool_.spindleSpeed() * feed);
    if(ui->chbxPlungeRate->isChecked())
        ui->dsbxPlungeRate->setValue(tool_.oneTurnCut() * tool_.spindleSpeed() * feed);
}

void ToolEditForm::updateDsbxStepover(double val) {
    // qDebug() << __FUNCTION__ << val;
    tool_.setStepover(val);
    ui->dsbxStepoverPercent->setValue(tool_.diameter() > 0. ? val / (tool_.diameter() * 0.01) : 0.);
}

void ToolEditForm::updateDsbxLenght(double val) {
    // qDebug() << __FUNCTION__ << val;
    tool_.setLenght(val);
}

void ToolEditForm::updateDsbxOneTurnCutPercent(double val) {
    // qDebug() << __FUNCTION__ << val;
    ui->dsbxOneTurnCut->setValue(val * (tool_.diameter() * 0.01));
}

void ToolEditForm::updateDsbxStepoverPercent(double val) {
    // qDebug() << __FUNCTION__ << val;
    ui->dsbxStepover->setValue(val * (tool_.diameter() * 0.01));
}

#include "moc_tool_editform.cpp"
