/********************************************************************************
 * Author    :  Damir Bakiev                                                    *
 * Version   :  na                                                              *
 * Date      :  XXXXX XX, 2026                                                  *
 * Website   :  na                                                              *
 * Copyright :  Damir Bakiev 2016-2026                                          *
 * License   :                                                                  *
 * Use, modification & distribution is subject to Boost Software License Ver 1. *
 * http://www.boost.org/LICENSE_1_0.txt                                         *
 ********************************************************************************/
#pragma once

#include "gc_job.h"

#include <QDialog>

class QCheckBox;
class QComboBox;
class QGroupBox;
class QLabel;
class QPushButton;
class QSpinBox;
class QTableWidget;
class QTableWidgetItem;

namespace GCode {

// Шаги задания в порядке исполнения: номер T, код и имя инструмента,
// операция, УП, обороты, подача и отметка «вырезка -- последней». Порядок по
// умолчанию -- только предложение: стрелки переставляют шаги как угодно, а
// номера T, смены инструмента и итог пересчитываются сразу. Отметка вырезки
// (и кнопка «Порядок по умолчанию») возвращает порядок по правилу.
class JobDialog : public QDialog {
    Q_OBJECT

public:
    explicit JobDialog(Job job, QWidget* parent = nullptr);
    ~JobDialog() override;

    const Job& job() const { return job_; }

private:
    enum Column {
        ColTool,
        ColLength, // длина инструмента: «из таблицы» или «щуп» -- при таблице контроллера
        ColCode,
        ColName,
        ColOperation,
        ColProgram,
        ColSpeed,
        ColFeed,
        ColCutout,
        ColCount
    };

    // Таблица и итог -- заново по job_.
    void fill();
    // Выделить строку шага id (после пересборки таблицы).
    void selectStep(int32_t id);
    int currentRow() const;
    // Шаг (id его УП) строки row; -1 -- нет такой строки.
    int32_t rowStep(int row) const;
    // Место шага id в job_.steps; -1 -- нет такого.
    int stepIndex(int32_t id) const;
    void moveStep(int delta);
    void defaultOrder();
    void cutoutToggled(QTableWidgetItem* item);
    void updateButtons();
    // Группа длин и колонка длины -- по job_ (без сигналов).
    void updateLengths();
    // Инструмент шага id -- «из таблицы» (true) или «щуп».
    void setFromTable(int32_t id, bool fromTable);

    Job job_;
    QTableWidget* table_{};
    QLabel* summary_{};
    QLabel* warnings_{};
    QPushButton* pbUp_{};
    QPushButton* pbDown_{};
    QPushButton* pbSave_{};
    QSpinBox* sbxMaxTool_{};
    QGroupBox* lengthsBox_{};
    QComboBox* cbxLengths_{};
    QCheckBox* chbxZeroProbe_{};
    QCheckBox* chbxTableFile_{};
};

} // namespace GCode
