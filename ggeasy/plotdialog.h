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

#include <QDialog>

#include "plot.h"

#include <vector>

class AbstractFile;
class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QLineEdit;
class QTreeWidget;

// Вывод слоёв в точном масштабе: файлы SVG/DXF/PDF и печать (см. plot.h).
//
// Одно окно на оба пути: список слоёв с зеркалом и негативом на каждый,
// общие настройки страницы и меток, папка вывода. Настройки живут в
// QSettings, группа "Plot".
class PlotDialog : public QDialog {
    Q_OBJECT

public:
    // Что делает Enter: окно открывается и из "Экспорта", и из "Печати".
    enum Mode {
        ExportMode,
        PrintMode
    };

    PlotDialog(const QString& lastPath, Mode mode, QWidget* parent = nullptr);

    void done(int result) override;

private:
    enum Column {
        ColExport,
        ColFile,
        ColSide,
        ColMirror,
        ColNegative,
        ColCount
    };

    QTreeWidget* files_;
    QComboBox* format_;
    QComboBox* layout_;
    QDoubleSpinBox* gap_;
    QDoubleSpinBox* margin_;
    QCheckBox* regMarks_;
    QComboBox* drillMarks_;
    QComboBox* drillFile_;
    QDoubleSpinBox* smallDrill_;
    QDoubleSpinBox* lineWidth_;
    QDoubleSpinBox* calibX_;
    QDoubleSpinBox* calibY_;
    QLineEdit* outDir_;
    QLabel* status_;

    void fillFiles(const QString& drillFileName);
    void setColumnChecked(int column, bool checked);
    void mirrorTopLayers();
    void updateEnabled();
    void browseOutDir();

    Plot::Options options() const;
    std::vector<Plot::Job> checkedJobs() const;
    AbstractFile* drillFile() const;
    // Геометрия по текущим настройкам; false -- выводить нечего (пользователю
    // уже сказано почему).
    bool buildSheet(Plot::Sheet& sheet, const Plot::Options& opt);

    void exportFiles();
    void printPreview();
};
