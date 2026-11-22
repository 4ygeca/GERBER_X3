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
#pragma once

#include "ggcore_export.h"

#include "doublespinbox.h"

#include "tool.h"
#include <QWidget>
#include <array>
#include <flat_map>
#include <optional>
#include <set>

namespace Ui {
class ToolEditForm;
}

class ToolItem;

class GGCORE_EXPORT ToolEditForm : public QWidget {
    Q_OBJECT
    friend class ToolEditDialog;

public:
    explicit ToolEditForm(QWidget* parent = nullptr);
    ~ToolEditForm();

    void setItem(ToolItem* item);
    void setTool(const Tool& tool);
    void setDialog();
    // Операция УП правленного снимка (окно местной правки): по ней видны поля
    // режимов. Зовётся до setTool.
    void setOperation(Tool::Operation operation) { operation_ = operation; }
    // Список материалов правили (окно «Материалы…»): наборы удалённых материалов
    // уходят и из правки, имена в списке наборов -- новые.
    void materialsChanged(const std::set<QString>& removed);
    enum Feeds {
        mmPerSec,
        mmPerMin,
        cmPerMin,
        mPerMin
    };

public slots:
    void on_pbApply_clicked();

signals:
    void itemChanged(ToolItem* item);
    void toolChanged(const Tool& item);

private:
    Ui::ToolEditForm* ui;

    void valueChanged(double value);

    ToolItem* item_{nullptr};
    // Правка инструмента. Значения режимов в нём (и в полях) -- показанные:
    // собственные инструмента или выбранного набора; сами наборы и
    // собственные значения хранятся отдельно, пока показано другое.
    Tool tool_;
    std::vector<Tool::CuttingPreset> presets_;
    Tool::CuttingPreset defaults_;
    int presetIndex_{-1}; // показанный набор в presets_; -1 -- собственные значения
    std::optional<Tool::Operation> operation_;
    double feed{1.0};
    bool dialog_{true}; // true -- в окне базы, false -- окно местной правки (setDialog)
    QString spindleLabel_;
    QString spindleSuffix_;

    void updateName();
    void updateCode();
    // Тот же код у других инструментов базы -- подсказка рядом с полем.
    void updateCodeHint();
    void updateWarn();
    void setChanged(bool fl = true);
    void setVisibleToolWidgets(bool visible);
    void setupToolWidgets(int type);
    // Строки полей: геометрия -- по типу, режимы -- по операции показанных
    // значений. restoreLast -- вернуть полям прежние значения (смена типа).
    void showRows(bool restoreLast);
    Tool::Operation shownOperation() const;
    // Наборы режимов
    void rebuildPresets();
    void updatePresetButtons();
    void selectPreset(int index);
    void storeShown();
    void loadCutFields();
    void loadDrillFields();
    void loadLaserFields();
    void addPreset();
    void removePreset();
    // Инструмент для базы: собственные значения режимов и все наборы.
    Tool assembled();
    using Key = decltype(&Tool::angle);

    void updateDsbxAngle(double val);
    void updateDsbxHoleDiam(double val);
    void updateDsbxDiameter(double val);
    void updateDsbxFeedRate(double val);
    void updateDsbxOneTurnCut(double val);
    void updateDsbxPassDepth(double val);
    void updateDsbxPlungeRate(double val);
    void updateDsbxSpindleSpeed(double val);
    void updateDsbxStepover(double val);
    void updateDsbxLenght(double val);
    void updateDsbxOneTurnCutPercent(double val);
    void updateDsbxStepoverPercent(double val);

    std::flat_map<QDoubleSpinBox*, decltype(&ToolEditForm::updateDsbxAngle)> update;
    std::array<std::pair<DoubleSpinBox*, decltype(&Tool::angle)>, 12> get;
    std::array<std::pair<DoubleSpinBox*, decltype(&Tool::setAngle)>, 12> set;

    // std::vector<DoubleSpinBox*> update;
};
