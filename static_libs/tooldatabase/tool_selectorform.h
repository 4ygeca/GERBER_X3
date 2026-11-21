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
#include "tool.h"
#include <QWidget>

class QComboBox;
class QGridLayout;
class QHBoxLayout;
class QLabel;
class QPushButton;

// Выбор инструмента для форм фрезеровки, лазера и резьбы. Форма отдаёт плоский
// снимок (tool()): инструмент базы с набором режимов под операцию (по типу) и
// материал -- материал проекта или выбранный здесь. Правка (Edit) -- местная:
// снимок остаётся как правлен, пока не выбран снова инструмент или материал.
class GGCORE_EXPORT ToolSelectorForm : public QWidget {
    Q_OBJECT

public:
    explicit ToolSelectorForm(QWidget* parent = nullptr);
    ~ToolSelectorForm();

    // Снимок как есть -- инструмент УП, открытой на правку: в базе его могло
    // уже не остаться. Совпал с тем, что дала бы база, -- обычное состояние,
    // иначе -- правка.
    void setTool(const Tool& tool);
    const Tool& tool() const;
    QLabel* label() const;

signals:
    void updateName();

protected:
    void showEvent(QShowEvent* event) override;

private slots:
    void on_pbSelect_clicked();
    void on_pbEdit_clicked();

private:
    Tool tool_; // действующий снимок
    // Источник снимка: инструмент базы с наборами режимов, а если его в базе
    // нет -- сам снимок (наборов нет, разрешение даёт его же значения).
    Tool base_;
    QString materialChoice_; // пусто -- материал проекта
    bool edited_{};          // снимок правлен здесь (Edit) или пришёл из УП не таким, как дала бы база
    const int counter;
    const QString toolFileName_;

    void readTool();
    void updateForm();
    void writeTool() const;

    Tool::Operation operation() const;
    QString effectiveMaterial() const;
    const Tool& source() const;
    Tool resolved() const;
    // Снимок, который не обязан совпадать с базой (Edit, УП на правку).
    void setSnapshot(const Tool& tool);
    // Снова по базе: выбран инструмент или материал, сменился материал проекта.
    void resolve();
    void updateMaterials();

    ///////////////////////////
    QLabel* label_;
    QLabel* lblPixmap;
    QLabel* lblName;
    QPushButton* pbSelect;
    QPushButton* pbEdit;
    QLabel* lblMaterial;
    QComboBox* cbxMaterial;

    void setupUi(QWidget* ToolSelectorForm);       // setupUi
    void retranslateUi(QWidget* ToolSelectorForm); // retranslateUi
};
