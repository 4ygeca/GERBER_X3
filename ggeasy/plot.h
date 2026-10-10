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

// Заголовки Qt -- первыми (см. gi.h): на gcc-16 наши заголовки, попав раньше
// ядра QtCore, сбивают разбор его внутренних.
#include <QPainterPath>
#include <QPointF>
#include <QSizeF>
#include <QString>
#include <QStringList>

#include <numbers> // geo/polyline.h берёт std::numbers::pi, но сам его не включает
#include <span>
#include <vector>

#include "geo/polyline.h"

class AbstractFile;
class QPrinter;

// Вывод слоёв платы в ТОЧНОМ масштабе: печать, PDF, SVG и DXF из одной и той
// же геометрии.
//
// Прежде печать и PDF рисовали сцену (QGraphicsScene::render): вписывали её в
// лист, тащили маркеры, траектории и подсветку выделения, красили в цвета
// слоёв с прозрачностью -- масштаб 1:1 выходил разве что случайно. Здесь
// геометрия берётся у самих элементов данных (заливка -- точным регионом,
// отверстия, пути без заливки -- штрихом заданной ширины) в координатах сцены,
// то есть с трансформацией файла, и выводится чёрным по белому без всякого
// вписывания.
//
// Все слои одного вывода ложатся на ОДНУ общую страницу (габарит всех слоёв
// плюс поле), и зеркалятся вокруг её центра: отдельные файлы и листы
// совмещаются друг с другом без подгонки.
namespace Plot {

enum class Format {
    Svg,
    Dxf, // R12, только контуры -- заливки в R12 нет
    Pdf
};

enum class DrillMarks {
    None,
    Actual, // отверстия в натуральную величину
    Small   // метка-керн заданного диаметра в центре отверстия
};

enum class Layout {
    PerLayer, // слой -- свой файл или своя страница
    Tiled     // все слои рядом на одном листе
};

struct Options {
    Format format{Format::Svg};
    Layout layout{Layout::PerLayer};
    double gap{5.0};    // мм между слоями на общем листе
    double margin{5.0}; // мм поля вокруг общего габарита; в нём же метки совмещения
    bool regMarks{true};
    DrillMarks drillMarks{DrillMarks::None};
    QString drillFile;      // имя файла сверловки -- только чтобы предложить его снова
    double smallDrill{0.4}; // мм, диаметр малой метки
    double lineWidth{0.1};  // мм, ширина штриха для путей без заливки
    double calibX{100.0};   // %, поправка масштаба принтера -- ТОЛЬКО для печати
    double calibY{100.0};
    QString outDir;

    // QSettings, группа "Plot".
    void load();
    void save() const;
};

struct Job {
    AbstractFile* file{};
    bool mirror{};
    bool negative{};
    bool shapes{}; // file == nullptr: видимые фигуры проекта (Shapes)
};

// Слой, готовый к выводу: контуры в мм СВОЕЙ страницы (Y вверх, начало --
// левый нижний угол), заливка по правилу even-odd. Зеркало, рамка негатива и
// метки совмещения уже внутри.
struct Plate {
    QString name;            // имя исходного файла
    QString fileBase;        // имя выходного файла без расширения
    Geo::Polylines contours; // точные, с дугами
    QPainterPath path;       // те же контуры для QPainter, OddEvenFill
};

struct Sheet {
    QSizeF page; // общая страница каждого слоя, мм
    std::vector<Plate> plates;
    QStringList empty; // файлы, в которых выводить нечего
};

// Слой на листе: смещение его страницы от левого нижнего угла листа, мм.
struct Placed {
    const Plate* plate{};
    QPointF offset;
};

// Один выходной файл или одна страница печати. Ссылается на Sheet и живёт не
// дольше него.
struct Page {
    QString fileBase;
    QSizeF size; // мм
    std::vector<Placed> items;
};

// Есть ли у файла отверстия (Gi::Drill) -- кандидат в источник меток сверловки.
bool hasDrills(const AbstractFile* file);

// Геометрия всех заданий на общей странице. Точные булевы операции (CGAL) --
// может занять время на больших слоях.
Sheet build(std::span<const Job> jobs, AbstractFile* drillFile, const Options& opt);

// Раскладка для вывода в файлы: слой на файл либо все слои в ряд в одном
// файле с именем combinedName.
std::vector<Page> filePages(const Sheet& sheet, const Options& opt, const QString& combinedName);

QString extension(Format format);

// Пишет страницу в файл; пустая строка -- успех, иначе текст ошибки.
QString write(const Page& page, const QString& path, Format format);

// Насколько раскладка не влезает в область печати area (мм, уже с поправкой
// калибровки): <= 1 -- влезает. Нужна для выбора ориентации листа.
double fitRatio(const Sheet& sheet, const Options& opt, QSizeF area);

// Печать без вписывания: 1 мм = logicalDpi/25.4 точек, умноженное на
// калибровку, по центру области печати. Страницу, поля и разрешение принтера
// НЕ трогает -- годится для QPrintPreviewDialog::paintRequested, который
// зовётся многократно. Пустая строка -- всё влезло, иначе предупреждение.
QString print(QPrinter* printer, const Sheet& sheet, const Options& opt);

} // namespace Plot
