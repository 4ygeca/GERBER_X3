// This is an open source non-commercial project. Dear PVS-Studio, please check it.
// PVS-Studio Static Code Analyzer for C, C++, C#, and Java: https://pvs-studio.com
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
#include "gi_pinsdialog.h"

#include "abstract_file.h"
#include "gi_drill.h"
#include "gi_group.h"
#include "md5.h"
#include "project.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QGraphicsScene>
#include <QGridLayout>
#include <QGroupBox>
#include <QKeyEvent>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QScopedValueRollback>
#include <QSettings>
#include <QSignalBlocker>
#include <QVBoxLayout>
#include <algorithm>
#include <cmath>
#include <limits>
#include <map>
#include <optional>
#include <ranges>
#include <set>
#include <tuple>

namespace Gi {

PinsDialog::PinsDialog(QWidget* parent)
    : QDialog{parent}
    , orig_{Pin::positions()}
    , origUsed_{Pin::usedFlags()}
    , origPointMode_{Pin::pointSymmetry()}
    , pointMode_{origPointMode_}
    // Окно сразу показывает симметричную расстановку: несимметричная (из
    // прежних версий) выравнивается под режим. На сцене это лишь
    // предпросмотр -- Cancel вернёт как было.
    , pos_{Pin::conform(orig_, origUsed_, origPointMode_)}
    , used_{origUsed_} {
    setObjectName(u"PinsDialog"_s);
    setWindowTitle(tr("Pins"));
    setWindowFlag(Qt::WindowContextHelpButtonHint, false);

    {
        // Отсчёт от начала сцены, "держать пары симметричными" и допуск
        // проверки симметрии больше не настраиваются -- ключи прежних версий
        // не нужны.
        QSettings settings;
        settings.beginGroup(u"Pin"_s);
        for(const QString& key: {u"relativeToZero"_s, u"keepSymmetric"_s, u"symmetryTolerance"_s})
            settings.remove(key);
    }

    auto makeSpinBox = [this](double min, double max) {
        auto* dsbx = new QDoubleSpinBox{this};
        dsbx->setDecimals(3);
        dsbx->setRange(min, max);
        dsbx->setSingleStep(0.1);
        dsbx->setSuffix(tr(" mm"));
        // Значение -- по Enter, стрелкам или уходу из поля, а не на каждую
        // набранную цифру: иначе булавка (и её партнёр) прыгала бы по
        // промежуточным числам.
        dsbx->setKeyboardTracking(false);
        return dsbx;
    };

    // Координаты. Отсчёт -- всегда от точки Zero, как в подсказке булавки,
    // в строке состояния ("Zeroed") и в самой УП: другой отсчёт только путал.
    auto* gbxCoordinates = new QGroupBox{tr("Coordinates relative to the Zero point (as in G-code)"), this};
    chbxPoint_ = new QCheckBox{tr("Symmetry point: all four pins move together"), gbxCoordinates};
    chbxPoint_->setToolTip(tr("Off: symmetry axis. Pins 1 ↔ 2 and 4 ↔ 3 are mirrored about the vertical flip axis,\n"
                              "a pin moves together with its mirror partner.\n"
                              "On: symmetry point. The pins form a rectangle centred on the point,\n"
                              "a pin moves all four.\n"
                              "In both modes the board is flipped around the vertical line through the axis or the point."));
    chbxPoint_->setChecked(pointMode_);

    auto* grid = new QGridLayout;
    grid->addWidget(new QLabel{u"X"_s, gbxCoordinates}, 0, 1, Qt::AlignHCenter);
    grid->addWidget(new QLabel{u"Y"_s, gbxCoordinates}, 0, 2, Qt::AlignHCenter);
    grid->addWidget(new QLabel{tr("Used"), gbxCoordinates}, 0, 3, Qt::AlignHCenter);
    for(int i{}; i < 4; ++i) {
        grid->addWidget(new QLabel{tr("Pin %1:").arg(i + 1), gbxCoordinates}, i + 1, 0);
        grid->addWidget(dsbxX_[i] = makeSpinBox(-10000, 10000), i + 1, 1);
        grid->addWidget(dsbxY_[i] = makeSpinBox(-10000, 10000), i + 1, 2);
        grid->addWidget(chbxUsed_[i] = new QCheckBox{gbxCoordinates}, i + 1, 3, Qt::AlignHCenter);
        chbxUsed_[i]->setChecked(used_[i]);
    }
    // Ось переворота (X) либо точка симметрии (X, Y) -- подпись и поле Y
    // меняет setPointMode.
    grid->addWidget(lblCenter_ = new QLabel{gbxCoordinates}, 5, 0);
    grid->addWidget(dsbxCenterX_ = makeSpinBox(-10000, 10000), 5, 1);
    grid->addWidget(dsbxCenterY_ = makeSpinBox(-10000, 10000), 5, 2);
    dsbxCenterY_->setToolTip(tr("Centre of the pin rectangle. The board is flipped around the vertical line through it.\n"
                                "Changing it moves all pins together."));

    lblStatus_ = new QLabel{gbxCoordinates};
    lblStatus_->setTextFormat(Qt::RichText);
    lblStatus_->setWordWrap(true);

    auto* coordinatesLayout = new QVBoxLayout{gbxCoordinates};
    coordinatesLayout->addWidget(chbxPoint_);
    coordinatesLayout->addLayout(grid);
    coordinatesLayout->addWidget(lblStatus_);

    // Отверстия. Наборы -- по разу при открытии: выделение и файлы, пока
    // окно модально, не меняются, а подписи (симметричен ли набор) зависят от
    // режима -- их обновляет setPointMode.
    qsizetype selected{};
    sets_ = holeSets(selected);
    auto* gbxHoles = new QGroupBox{tr("Drill holes"), this};
    cbxSource_ = new QComboBox{gbxHoles};
    cbxSource_->setSizeAdjustPolicy(QComboBox::AdjustToContents); // подпись меняется с режимом
    for(size_t i{}; i < sets_.size(); ++i)
        cbxSource_->addItem(QString{});
    cbxSource_->setToolTip(tr("Sets of 3 or 4 round holes of one diameter from the Excellon files\n"
                              "(by tool and by diameter). To use other holes, select 3 or 4 of them\n"
                              "on the scene before opening this window."));
    pbSnap_ = new QPushButton{QIcon::fromTheme(u"drill"_s), tr("Place pins on these holes"), gbxHoles};
    pbSnap_->setToolTip(tr("The pins are placed exactly on the holes if the holes are symmetric\n"
                           "about the flip axis or the symmetry point, as the mode requires.\n"
                           "Otherwise you get a warning, and the pins stay where they are."));
    pbSnap_->setAutoDefault(false);
    lblSnap_ = new QLabel{gbxHoles};
    lblSnap_->setTextFormat(Qt::PlainText); // имя файла -- не разметка
    lblSnap_->setWordWrap(true);
    QStringList info;
    if(sets_.empty()) {
        cbxSource_->setEnabled(false);
        pbSnap_->setEnabled(false);
        info << tr("No Excellon file has 3 or 4 round holes of one diameter.\n"
                   "Select the tooling holes on the scene before opening this window.");
    }
    // Выделено, но не 3-4 -- набора из выделенного нет; сказать, почему.
    if(selected > 0 && (selected < 3 || selected > 4))
        info << tr("Round Excellon holes selected on the scene: %1. To place the pins on selected holes, select 3 or 4 of them.")
                    .arg(selected);
    holesInfo_ = info.join(u'\n');
    lblSnap_->setText(holesInfo_);

    auto* holesLayout = new QGridLayout{gbxHoles};
    holesLayout->addWidget(cbxSource_, 0, 0);
    holesLayout->addWidget(pbSnap_, 0, 1);
    holesLayout->addWidget(lblSnap_, 1, 0, 1, 2);
    holesLayout->setColumnStretch(0, 1);

    auto* buttons = new QDialogButtonBox{QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this};

    auto* mainLayout = new QVBoxLayout{this};
    mainLayout->addWidget(gbxCoordinates);
    mainLayout->addWidget(gbxHoles);
    mainLayout->addStretch();
    mainLayout->addWidget(buttons);

    connect(chbxPoint_, &QCheckBox::toggled, this, &PinsDialog::setPointMode);
    for(int i{}; i < 4; ++i) {
        connect(dsbxX_[i], &QDoubleSpinBox::valueChanged, this, [this, i] { pinEdited(i, true); });
        connect(dsbxY_[i], &QDoubleSpinBox::valueChanged, this, [this, i] { pinEdited(i, false); });
        connect(chbxUsed_[i], &QCheckBox::toggled, this, [this, i](bool checked) {
            used_[i] = checked;
            updateFields(); // ось считается по используемым
            updatePreview();
        });
    }
    connect(dsbxCenterX_, &QDoubleSpinBox::valueChanged, this, [this] { centerEdited(true); });
    connect(dsbxCenterY_, &QDoubleSpinBox::valueChanged, this, [this] { centerEdited(false); });
    connect(pbSnap_, &QPushButton::clicked, this, &PinsDialog::placeOnHoles);
    connect(buttons, &QDialogButtonBox::accepted, this, &PinsDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &PinsDialog::reject);

    Pin::showSymmetryMark(true);
    setPointMode(pointMode_); // подписи по режиму, наборы, поля, предпросмотр
}

PinsDialog::~PinsDialog() { Pin::showSymmetryMark(false); }

std::vector<PinsDialog::HoleSet> PinsDialog::holeSets(qsizetype& selected) {
    struct Round {
        QPointF centre; // координаты сцены
        double diameter;
        double eps; // разрешение координат на сцене
        int tool;
    };
    // Разрешение файла считается по всем его отверстиям -- раз на файл.
    std::map<const AbstractFile*, double> resolutions;
    // Только круглые отверстия Excellon: у паза нет одной точки, куда встал бы
    // штифт, а точки сверловки УП (и самих булавок, "Pin_") -- не отверстия
    // платы. Файл сверловки -- плагин, отсюда его класс не виден: по типу.
    auto roundHole = [&resolutions](QGraphicsItem* item) -> std::optional<Round> {
        if(!item || item->type() != Gi::Type::Drill) return std::nullopt;
        auto* drill = static_cast<Gi::Drill*>(item);
        const AbstractFile* file = drill->file();
        if(!file || file->type() != "Excellon"_hash32) return std::nullopt;
        if(drill->isSlot() || drill->path().isEmpty() || !(drill->diameter() > 0.0)) return std::nullopt;
        auto it = resolutions.find(file);
        if(it == resolutions.end())
            it = resolutions.emplace(file, file->coordinateResolution()).first;
        // Формат файла неизвестен -- шаг как у типичного метрического 3.3.
        const double step = it->second > 0.0 ? it->second : 0.001;
        // Трансформация файла (масштаб, поворот, зеркало) -- в sceneTransform:
        // и центр, и шаг сетки -- уже на сцене.
        const QTransform t = drill->sceneTransform();
        const double scale = std::max(std::hypot(t.m11(), t.m12()), std::hypot(t.m21(), t.m22()));
        // Снизу -- шум double и трансформации, сверху -- дюймовый 2.3 (0.0254
        // мм): на ещё более грубой сетке координаты целые, и симметричные
        // отверстия симметричны и так.
        return Round{t.map(drill->path().front()), drill->diameter(), std::clamp(step * scale, 1e-4, 0.03),
            static_cast<int>(drill->toolId())};
    };
    // Повтор (одно отверстие дважды в файле, оно же в двух инструментах) --
    // одно отверстие: штифт в него встаёт один. Больше limit разных --
    // набор уже не годится, дальше не считать.
    struct Distinct {
        std::vector<QPointF> centres;
        double eps{};
    };
    auto distinct = [](const std::vector<Round>& holes, size_t limit) {
        Distinct result;
        for(const Round& hole: holes)
            result.eps = std::max(result.eps, hole.eps);
        for(const Round& hole: holes) {
            if(std::ranges::any_of(result.centres, [&](QPointF c) { return QLineF{c, hole.centre}.length() <= result.eps; }))
                continue;
            result.centres.push_back(hole.centre);
            if(result.centres.size() > limit) break;
        }
        // Порядок -- снизу вверх, слева направо: сообщения и выбор пар не
        // зависят от порядка отверстий в файле.
        std::ranges::sort(result.centres, [](QPointF a, QPointF b) { return a.y() != b.y() ? a.y() < b.y() : a.x() < b.x(); });
        return result;
    };

    std::vector<HoleSet> sets;
    // Выделенные на сцене -- так пользователь сам выбирает отверстия, если
    // набора из файла нет (например, рядом ещё отверстия того же диаметра).
    // ПКМ по булавке выделения не снимает.
    selected = 0;
    if(auto* view = App::grView.ptr(); view && view->scene()) {
        std::vector<Round> holes;
        for(QGraphicsItem* item: view->scene()->selectedItems())
            if(auto hole = roundHole(item)) holes.push_back(*hole);
        // Больше четырёх разных -- набор уже не годится, а их точное число
        // для подсказки не нужно: при выделенной целиком панели попарное
        // сравнение десятков тысяч отверстий держало бы окно секундами.
        Distinct found = distinct(holes, 4);
        selected = found.centres.size() > 4 ? static_cast<qsizetype>(holes.size())
                                            : static_cast<qsizetype>(found.centres.size());
        if(selected == 3 || selected == 4) {
            HoleSet set;
            set.centres = std::move(found.centres);
            set.eps = found.eps;
            std::set<long long> diameters;
            for(const Round& hole: holes)
                diameters.insert(std::llround(hole.diameter * 1e4));
            if(diameters.size() == 1) {
                set.diameter = holes.front().diameter;
            } else {
                // Штифты одного диаметра в отверстия разного размера не
                // сядут одинаково. Набор показывается, а почему он не
                // годится -- скажет попытка поставить на него булавки.
                QStringList list;
                for(long long key: diameters)
                    list << QString::number(static_cast<double>(key) * 1e-4, 'f', 3);
                set.error = tr("The selected holes have different diameters: Ø%1 mm. The pins need 3 or 4 holes of one diameter.")
                                .arg(list.join(u", Ø"_s))
                    + u'\n' + tr("The pins were not moved.");
            }
            sets.push_back(std::move(set));
        }
    }

    std::vector<HoleSet> fileSets;
    for(AbstractFile* file: App::project().files("Excellon"_hash32)) {
        std::vector<Round> holes;
        for(Gi::Group* group: file->itemGroups())
            if(group)
                for(const auto& item: *group)
                    if(auto hole = roundHole(item.get())) holes.push_back(*hole);
        // Технологические отверстия узнаются по размеру. Инструмент у них
        // обычно свой, но бывает общим с другими отверстиями того же
        // диаметра, а в объединённом файле (KiCad: PTH + NPTH) один диаметр
        // делят несколько инструментов. Поэтому кучки -- и по инструменту, и
        // по диаметру; одинаковые наборы -- по разу.
        std::map<int, std::vector<Round>> byTool;
        std::map<long long, std::vector<Round>> byDiameter;
        for(const Round& hole: holes) {
            byTool[hole.tool].push_back(hole);
            byDiameter[std::llround(hole.diameter * 1e4)].push_back(hole);
        }
        const auto first = static_cast<std::ptrdiff_t>(fileSets.size());
        auto add = [&](const std::vector<Round>& bucket) {
            Distinct found = distinct(bucket, 4);
            if(found.centres.size() < 3 || found.centres.size() > 4) return;
            if(std::any_of(fileSets.begin() + first, fileSets.end(), [&](const HoleSet& set) { return set.centres == found.centres; }))
                return;
            std::set<int> tools;
            for(const Round& hole: bucket)
                tools.insert(hole.tool);
            QStringList toolNames;
            for(int tool: tools)
                toolNames << u"T%1"_s.arg(tool);
            HoleSet set;
            set.name = u"%1 (%2)"_s.arg(file->shortName(), toolNames.join(u", "_s));
            set.centres = std::move(found.centres);
            set.diameter = bucket.front().diameter;
            set.eps = found.eps;
            fileSets.push_back(std::move(set));
        };
        for(const std::vector<Round>& bucket: byTool | std::views::values)
            add(bucket);
        for(const std::vector<Round>& bucket: byDiameter | std::views::values)
            add(bucket);
    }
    // Технологические отверстия обычно самые крупные из круглых на плате.
    std::ranges::stable_sort(fileSets, std::ranges::greater{}, &HoleSet::diameter);
    std::ranges::move(fileSets, std::back_inserter(sets));
    return sets;
}

PinsDialog::Fit PinsDialog::fitAxis(const HoleSet& set, QPointF org) {
    Fit result;
    const std::vector<QPointF>& h = set.centres;
    const int n = static_cast<int>(h.size());
    if(n < 3 || n > 4) {
        result.error = tr("The pins need 3 or 4 round holes of one diameter.") + u'\n' + tr("The pins were not moved.");
        return result;
    }
    const double eps = set.eps;
    const auto number = [](double value, int decimals) { return QString::number(value, 'f', decimals); };
    // Промах: мелкий -- до десятых микрона, крупный -- до микрона.
    const auto offset = [](double value) { return QString::number(value, 'f', value < 0.1 ? 4 : 3); };

    // Зеркало о вертикаль переставляет отверстия набора между собой, и сумма
    // их X не меняется: ось симметричного набора -- среднее X, сколько бы
    // отверстий ни стояло на самой оси.
    double axis{};
    for(const QPointF& p: h)
        axis += p.x();
    axis /= n;

    // Раскладки: каждое отверстие либо на оси (mate[i] == i), либо в паре с
    // зеркальным. Отверстий 3-4 -- раскладок не больше десяти, перебор.
    std::vector<std::vector<int>> layouts;
    std::vector<int> mate(static_cast<size_t>(n), -1);
    auto enumerate = [&](auto&& self) -> void {
        const auto unset = std::ranges::find(mate, -1);
        if(unset == mate.end()) {
            layouts.push_back(mate);
            return;
        }
        const auto i = static_cast<int>(unset - mate.begin());
        mate[i] = i;
        self(self);
        for(int j = i + 1; j < n; ++j)
            if(mate[j] < 0) {
                mate[i] = j;
                mate[j] = i;
                self(self);
                mate[j] = -1;
            }
        mate[i] = -1;
    };
    enumerate(enumerate);

    // Отклонение -- в долях допустимого округления: одна координата (X
    // отверстия на оси, Y пары) -- до eps, сумма двух (x_i + x_j - 2a) -- до
    // 2 eps: каждая координата в файле округлена не больше чем на шаг.
    struct Worst {
        double score{-1.0};
        int hole{-1};
        int other{-1}; // пара отверстия; == hole -- отверстие на оси
    };
    const auto worstOf = [&](const std::vector<int>& m) {
        Worst worst;
        for(int i{}; i < n; ++i) {
            const int j = m[i];
            if(j < i) continue; // пара -- один раз
            const double score = j == i ? std::abs(h[i].x() - axis) / eps
                                        : std::max(std::abs(h[i].y() - h[j].y()) / eps,
                                              std::abs(h[i].x() + h[j].x() - 2.0 * axis) / (2.0 * eps));
            if(score > worst.score) worst = {score, i, j};
        }
        return worst;
    };
    const auto onAxis = [n](const std::vector<int>& m) {
        int count{};
        for(int i{}; i < n; ++i)
            count += m[i] == i;
        return count;
    };
    // Четыре булавки -- две зеркальные пары; пара встаёт либо на два
    // зеркальных отверстия, либо обеими булавками на одно отверстие на оси.
    const auto fitsPins = [&](const std::vector<int>& m) { return onAxis(m) == (n == 4 ? 0 : 1); };
    // Лучшая раскладка: симметричная, что булавкам годится; иначе симметричная
    // с наименьшим числом отверстий на оси; иначе -- ближайшая к симметрии:
    // по ней и объяснить, что не так.
    const auto rank = [&](const std::vector<int>& m, const Worst& w) {
        const bool fails = w.score > 1.0;
        return std::tuple{fails, !fails && !fitsPins(m), fails ? 0 : onAxis(m), w.score};
    };
    const std::vector<int>* best{};
    Worst bestWorst;
    for(const std::vector<int>& m: layouts)
        if(const Worst w = worstOf(m); !best || rank(m, w) < rank(*best, bestWorst)) {
            best = &m;
            bestWorst = w;
        }
    const std::vector<int>& layout = *best;

    if(bestWorst.score > 1.0) {
        int hole = bestWorst.hole;
        int other = bestWorst.other;
        double lineX = axis;
        // Три отверстия -- пара и одно на оси: ось тогда задаёт пара (так её
        // видит и конструктор платы), и промах отверстия на оси -- от неё.
        if(n == 3 && onAxis(layout) == 1) {
            int c{};
            while(layout[c] != c)
                ++c;
            const int l = c == 0 ? 1 : 0;
            const int r = layout[l];
            lineX = (h[l].x() + h[r].x()) * 0.5;
            if(std::abs(h[l].y() - h[r].y()) >= std::abs(h[c].x() - lineX)) {
                hole = l;
                other = r;
            } else {
                hole = other = c;
            }
        }
        const QPointF& p = h[hole];
        const QPointF& q = h[other];
        const QString lineText = number(lineX - org.x(), 3);
        const QString miss = hole == other
            ? tr("The hole X %1, Y %2 should lie on the flip axis X = %3 mm, but is %4 mm off it.")
                  .arg(number(p.x() - org.x(), 3), number(p.y() - org.y(), 3), lineText, offset(std::abs(p.x() - lineX)))
            : tr("The mirror image of the hole X %1, Y %2 about %3 misses the hole X %4, Y %5 by %6 mm.")
                  .arg(number(p.x() - org.x(), 3), number(p.y() - org.y(), 3), tr("the flip axis X = %1 mm").arg(lineText),
                      number(q.x() - org.x(), 3), number(q.y() - org.y(), 3),
                      offset(std::hypot(2.0 * lineX - p.x() - q.x(), p.y() - q.y())));
        result.error = QStringList{tr("The holes are not symmetric about a vertical flip axis."), miss,
            tr("The comparison is exact: only the rounding of the file coordinates (resolution %1 mm) is allowed for.")
                .arg(QString::number(eps, 'g', 3)),
            tr("The pins were not moved.")}
                           .join(u'\n');
        return result;
    }
    result.symmetric = true;
    if(!fitsPins(layout)) {
        result.error = tr("The holes are symmetric about the flip axis X = %1 mm, but %2 of them lie on the axis itself. "
                          "Four pins are two mirror pairs: a pair stands either on two mirror holes or on one hole on the axis, "
                          "so the pins cannot take all of these holes.")
                           .arg(number(axis - org.x(), 3), QString::number(onAxis(layout)))
            + u'\n' + tr("The pins were not moved.");
        return result;
    }

    // Ряды булавок: пара отверстий либо одно отверстие на оси (обе булавки
    // пары -- на нём).
    struct Row {
        double centre;    // X середины ряда
        double halfWidth; // 0 -- отверстие на оси
        double y;
        int left;
        int right;
    };
    std::vector<Row> rows;
    for(int i{}; i < n; ++i) {
        const int j = layout[i];
        if(j == i) {
            rows.push_back({h[i].x(), 0.0, h[i].y(), i, i});
        } else if(i < j) {
            const int l = h[i].x() < h[j].x() ? i : j;
            const int r = l == i ? j : i;
            rows.push_back({(h[l].x() + h[r].x()) * 0.5, (h[r].x() - h[l].x()) * 0.5, (h[l].y() + h[r].y()) * 0.5, l, r});
        }
    }
    // Нижний ряд (меньшая Y: сцена перевёрнута) -- булавки 0 (левая) и 1,
    // верхний -- 3 и 2. На одной высоте нижним считается более широкий:
    // выбор не должен зависеть от порядка отверстий в файле.
    if(std::abs(rows[0].y - rows[1].y) > eps ? rows[0].y > rows[1].y : rows[0].halfWidth < rows[1].halfWidth)
        std::swap(rows[0], rows[1]);
    const Row& bottom = rows[0];
    const Row& top = rows[1];
    // Ось -- посередине между центрами рядов: так булавки уходят от
    // отверстий меньше всего, а пары зеркальны ровно -- без допуска.
    const double a = (bottom.centre + top.centre) * 0.5;
    result.pos[0] = {a - bottom.halfWidth, bottom.y};
    result.pos[1] = {a + bottom.halfWidth, bottom.y};
    result.pos[2] = {a + top.halfWidth, top.y};
    result.pos[3] = {a - top.halfWidth, top.y};
    const int holeOfPin[4]{bottom.left, bottom.right, top.right, top.left};
    for(int i{}; i < 4; ++i)
        result.maxOffset = std::max(result.maxOffset, QLineF{result.pos[i], h[holeOfPin[i]]}.length());
    if(bottom.left == bottom.right)
        result.notes << tr("Pins %1 and %2 both stand on the hole on the flip axis.").arg(1).arg(2);
    if(top.left == top.right)
        result.notes << tr("Pins %1 and %2 both stand on the hole on the flip axis.").arg(3).arg(4);
    return result;
}

PinsDialog::Fit PinsDialog::fitPoint(const HoleSet& set, QPointF org) {
    Fit result;
    const std::vector<QPointF>& h = set.centres;
    const int n = static_cast<int>(h.size());
    const double eps = set.eps;
    const auto number = [](double value, int decimals) { return QString::number(value, 'f', decimals); };
    // Промах: мелкий -- до десятых микрона, крупный -- до микрона.
    const auto offset = [](double value) { return QString::number(value, 'f', value < 0.1 ? 4 : 3); };
    // Подходит под ось -- подсказать: точка тут лишь не тот режим.
    const auto refuse = [&](const QString& why) {
        QStringList text{tr("Symmetry about a point needs 4 holes at the corners of a rectangle with sides along X and Y.")};
        if(!why.isEmpty())
            text << why;
        if(fitAxis(set, org).error.isEmpty())
            text << tr("These holes are symmetric about a vertical flip axis: turn off \"Symmetry point\" to place the pins on them.");
        text << tr("The pins were not moved.");
        result.error = text.join(u'\n');
        return result;
    };
    if(n != 4)
        return refuse(tr("The set has only %1 holes, so one of the four pins would have no hole.").arg(n));

    QPointF c;
    for(const QPointF& p: h)
        c += p;
    c /= 4.0;
    // Каждое отверстие -- угол: у него есть зеркальное через вертикаль точки
    // (та же Y) и через горизонталь (та же X). Отверстие на самой линии
    // отражается в себя -- угла там нет. Отклонения -- в долях допустимого
    // округления, как и у оси; в сообщение -- наибольшее.
    double worstScore = 1.0;
    QString worst;
    for(int i{}; i < n; ++i)
        for(const bool vertical: {true, false}) {
            const QPointF m = vertical ? QPointF{2.0 * c.x() - h[i].x(), h[i].y()} : QPointF{h[i].x(), 2.0 * c.y() - h[i].y()};
            int j = -1; // ближайшее к отражению другое отверстие
            for(int k{}; k < n; ++k)
                if(k != i && (j < 0 || QLineF{m, h[k]}.length() < QLineF{m, h[j]}.length())) j = k;
            const double offLine = vertical ? std::abs(h[i].x() - c.x()) : std::abs(h[i].y() - c.y());
            // Вдоль линии -- одна координата, поперёк -- сумма двух.
            const double along = vertical ? std::abs(h[i].y() - h[j].y()) : std::abs(h[i].x() - h[j].x());
            const double across = vertical ? std::abs(h[i].x() + h[j].x() - 2.0 * c.x()) : std::abs(h[i].y() + h[j].y() - 2.0 * c.y());
            const double score = offLine > eps ? std::max(along / eps, across / (2.0 * eps)) : std::numeric_limits<double>::max();
            if(score <= worstScore) continue;
            worstScore = score;
            const QString line = vertical ? tr("the vertical line X = %1 mm through the symmetry point").arg(number(c.x() - org.x(), 3))
                                          : tr("the horizontal line Y = %1 mm through the symmetry point").arg(number(c.y() - org.y(), 3));
            // На самой линии отверстие отражается в себя: промаха нет, а
            // углом прямоугольника оно быть не может -- это и сказать.
            if(offLine <= eps)
                worst = tr("The hole X %1, Y %2 lies on %3, so it cannot be a corner of the rectangle.")
                            .arg(number(h[i].x() - org.x(), 3), number(h[i].y() - org.y(), 3), line);
            else
                worst = tr("The mirror image of the hole X %1, Y %2 about %3 misses the hole X %4, Y %5 by %6 mm.")
                            .arg(number(h[i].x() - org.x(), 3), number(h[i].y() - org.y(), 3), line,
                                number(h[j].x() - org.x(), 3), number(h[j].y() - org.y(), 3), offset(QLineF{m, h[j]}.length()));
        }
    if(!worst.isEmpty())
        return refuse(worst + u'\n'
            + tr("The comparison is exact: only the rounding of the file coordinates (resolution %1 mm) is allowed for.")
                  .arg(QString::number(eps, 'g', 3)));

    // Прямоугольник с центром в точке: полуширина и полувысота -- средние,
    // так булавки уходят от отверстий меньше всего.
    double a{}, b{};
    for(const QPointF& p: h) {
        a += std::abs(p.x() - c.x());
        b += std::abs(p.y() - c.y());
    }
    a /= 4.0;
    b /= 4.0;
    result.pos[0] = c + QPointF{-a, -b};
    result.pos[1] = c + QPointF{a, -b};
    result.pos[2] = c + QPointF{a, b};
    result.pos[3] = c + QPointF{-a, b};
    // Булавка -- в отверстие своей четверти: 0 -- левая нижняя (сцена
    // перевёрнута по Y), дальше против часовой.
    bool taken[4]{};
    for(const QPointF& p: h) {
        const int idx = p.y() < c.y() ? (p.x() < c.x() ? 0 : 1) : (p.x() < c.x() ? 3 : 2);
        if(std::exchange(taken[idx], true)) // проверки выше такого не пропускают
            return refuse({});
        result.maxOffset = std::max(result.maxOffset, QLineF{result.pos[idx], p}.length());
    }
    result.symmetric = true;
    return result;
}

PinsDialog::Fit PinsDialog::fit(const HoleSet& set) const {
    if(!set.error.isEmpty()) {
        Fit refused;
        refused.error = set.error;
        return refused;
    }
    return pointMode_ ? fitPoint(set, origin()) : fitAxis(set, origin());
}

void PinsDialog::updateSetLabels() {
    const QPointF org = origin();
    for(size_t i{}; i < sets_.size(); ++i) {
        const HoleSet& set = sets_[i];
        double minX{set.centres.front().x()}, maxX{minX}, minY{set.centres.front().y()}, maxY{minY};
        QStringList holes; // в подсказку: какие именно отверстия
        for(const QPointF& p: set.centres) {
            minX = std::min(minX, p.x());
            maxX = std::max(maxX, p.x());
            minY = std::min(minY, p.y());
            maxY = std::max(maxY, p.y());
            holes << u"X %1, Y %2"_s.arg(QString::number(p.x() - org.x(), 'f', 3), QString::number(p.y() - org.y(), 'f', 3));
        }
        // Размах отличает технологические отверстия от мелочи того же
        // диаметра: ряд выводов TO-220 тоже "симметричен".
        const QString count = QString::number(set.centres.size());
        const QString diameter = QString::number(set.diameter, 'f', 3);
        const QString width = QString::number(maxX - minX, 'f', 1);
        const QString height = QString::number(maxY - minY, 'f', 1);
        QString text;
        if(!set.name.isEmpty())
            text = tr("%1: %2 × Ø%3 mm, %4 × %5 mm").arg(set.name, count, diameter, width, height);
        else if(set.diameter > 0.0)
            text = tr("Selected on the scene: %1 holes Ø%2 mm, %3 × %4 mm").arg(count, diameter, width, height);
        else
            text = tr("Selected on the scene: %1 holes of different diameters").arg(count);
        // Симметрия -- по режиму окна: набор может годиться под ось, но не
        // под точку (трапеция, три отверстия).
        if(set.error.isEmpty()) {
            const Fit result = fit(set);
            const Fit axis = pointMode_ ? fitAxis(set, org) : result;
            if(result.error.isEmpty())
                text = tr("%1 — symmetric").arg(text);
            else if(pointMode_ && axis.error.isEmpty())
                text = tr("%1 — symmetric about the flip axis only").arg(text);
            else if(result.symmetric || axis.symmetric) // ромб: симметричен, но не под четыре булавки
                text = tr("%1 — symmetric, but does not fit four pins").arg(text);
            else
                text = tr("%1 — not symmetric").arg(text);
        }
        cbxSource_->setItemText(static_cast<int>(i), text);
        cbxSource_->setItemData(static_cast<int>(i), holes.join(u'\n'), Qt::ToolTipRole);
    }
}

QPointF PinsDialog::origin() const { return App::zero.ptr() ? App::zero().pos() : QPointF{}; }

void PinsDialog::setPointMode(bool point) {
    pointMode_ = point;
    // Ось -> точка: трапеция становится прямоугольником (ширину задаёт пара,
    // где используемых больше). Точка -> ось: прямоугольник симметричен и
    // относительно оси, ничего не двигается.
    const Pin::Positions before = pos_;
    pos_ = Pin::conform(pos_, used_, pointMode_);
    if(pos_ != before)
        lblSnap_->setText(holesInfo_); // булавки ушли с отверстий -- итог расстановки устарел
    updateSetLabels();
    lblCenter_->setText(point ? tr("Symmetry point:") : tr("Flip axis X:"));
    dsbxCenterX_->setToolTip(point ? dsbxCenterY_->toolTip()
                                   : tr("Vertical axis the board is flipped around for the bottom side.\n"
                                        "Changing it moves all pins together."));
    dsbxCenterY_->setVisible(point);
    updateFields();
    updatePreview();
}

void PinsDialog::pinEdited(int idx, bool isX) {
    if(updating_) return;
    // Центр -- до правки: в режиме точки его Y -- среднее по всем четырём, и
    // правленая булавка увела бы его, а с ним и противоположную пару.
    const QPointF c = Pin::center(pos_, used_);
    const QPointF org = origin();
    QPointF pt = pos_[idx];
    // Меняется только правленая координата: вторая осталась бы округлённой
    // до трёх знаков поля, а булавка могла стоять на отверстии точнее.
    if(isX)
        pt.setX(dsbxX_[idx]->value() + org.x());
    else
        pt.setY(dsbxY_[idx]->value() + org.y());
    // Остальные -- по симметрии режима: партнёр зеркально через ось, в
    // режиме точки ещё и вторая пара.
    pos_ = Pin::place(pos_, idx, pt, c, pointMode_);
    lblSnap_->setText(holesInfo_); // булавки ушли с отверстий -- итог расстановки устарел
    updateFields();
    updatePreview();
}

void PinsDialog::centerEdited(bool isX) {
    if(updating_) return;
    // Ось (точка) сдвигается вместе со всей расстановкой: при сдвиге всех
    // булавок на одно и то же середины пар уходят ровно туда же. Сдвиг -- только
    // по правленой координате: вторая в поле округлена до трёх знаков.
    const QPointF org = origin();
    const QPointF c = Pin::center(pos_, used_);
    const QPointF delta = isX ? QPointF{dsbxCenterX_->value() + org.x() - c.x(), 0.0}
                              : QPointF{0.0, dsbxCenterY_->value() + org.y() - c.y()};
    for(QPointF& pt: pos_)
        pt += delta;
    lblSnap_->setText(holesInfo_);
    updateFields();
    updatePreview();
}

void PinsDialog::placeOnHoles() {
    const int idx = cbxSource_->currentIndex();
    if(idx < 0 || idx >= static_cast<int>(sets_.size())) return;
    const HoleSet& set = sets_[static_cast<size_t>(idx)];
    const Fit result = fit(set);
    if(!result.error.isEmpty()) {
        // Булавки не двигаются: на несимметричные штифты перевёрнутая плата
        // не сядет, а "почти" симметричная расстановка сдвинула бы нижнюю
        // сторону.
        lblSnap_->setText(result.error);
        QMessageBox::warning(this, windowTitle(), result.error);
        return;
    }
    pos_ = result.pos;
    // Каждая булавка теперь стоит на отверстии -- используются все: и ось
    // переворота считается по всем четырём.
    for(int i{}; i < 4; ++i) {
        used_[i] = true;
        const QSignalBlocker blocker{chbxUsed_[i]};
        chbxUsed_[i]->setChecked(true);
    }
    // Расстановка симметрична ровно, а от центров отверстий булавки уходят
    // лишь на округление координат в файле.
    QStringList lines{
        tr("The pins are on the holes Ø%1 mm. The largest offset of a pin from a hole centre is %2 mm: "
           "the rounding of the file coordinates (resolution %3 mm).")
            .arg(QString::number(set.diameter, 'f', 3), QString::number(result.maxOffset, 'f', 4), QString::number(set.eps, 'g', 3))};
    lines << result.notes;
    // Отверстия на сцене -- только первой копии платы, а в УП нижняя сторона
    // каждой копии зеркалится об одну и ту же ось.
    if(App::project().stepsX() * App::project().stepsY() > 1)
        lines << tr("Step and repeat: the flip axis is taken on the first board copy, where these holes are.");
    lblSnap_->setText(lines.join(u'\n'));
    updateFields();
    updatePreview();
}

std::vector<int> PinsDialog::unpairedPins() const {
    const double axis = Pin::flipAxisX(pos_, used_);
    std::vector<int> pins;
    for(int i{}; i < 4; ++i)
        // Булавка на самой оси после переворота встаёт в своё же отверстие.
        if(used_[i] && !used_[Pin::partner(i)] && std::abs(pos_[i].x() - axis) > 1e-6)
            pins.push_back(i);
    return pins;
}

void PinsDialog::updateFields() {
    const QScopedValueRollback guard{updating_, true};
    const QPointF org = origin();
    for(int i{}; i < 4; ++i) {
        dsbxX_[i]->setValue(pos_[i].x() - org.x());
        dsbxY_[i]->setValue(pos_[i].y() - org.y());
    }
    const QPointF c = Pin::center(pos_, used_);
    dsbxCenterX_->setValue(c.x() - org.x());
    dsbxCenterY_->setValue(c.y() - org.y());
}

void PinsDialog::updatePreview() {
    Pin::setPos(pos_.data());
    // Метка -- по режиму окна, а не проекта: в проект он уйдёт только по OK.
    const QPointF c = Pin::center(pos_, used_);
    Pin::updateSymmetryMark(pointMode_, c, pos_);

    const auto red = [](const QString& text) -> QString {
        return u"<span style=\"color:#e04040\">"_s + text + u"</span>"_s;
    };

    const QPointF org = origin();
    QStringList lines;
    if(pointMode_)
        lines << tr("Symmetry point: X = %1 mm, Y = %2 mm. The board is flipped around the vertical line through it.")
                     .arg(c.x() - org.x(), 0, 'f', 3)
                     .arg(c.y() - org.y(), 0, 'f', 3);
    else
        lines << tr("Flip axis: X = %1 mm").arg(c.x() - org.x(), 0, 'f', 3);
    if(std::ranges::find(used_, true) == used_.end())
        lines << red(tr("No pins are used."));
    for(int i: unpairedPins())
        lines << red(tr("Pin %1 is used, but its mirror partner, pin %2, is not: after flipping the board, there is no hole for pin %1.")
                         .arg(i + 1)
                         .arg(Pin::partner(i) + 1));
    lblStatus_->setText(lines.join(u"<br>"_s));
}

void PinsDialog::accept() {
    // Положения симметричны всегда, а флаги "Используется" -- нет: у
    // используемой булавки без используемого партнёра после переворота нет
    // отверстия.
    if(!unpairedPins().empty()
        && QMessageBox::warning(this, windowTitle(),
               tr("Some used pins have no used mirror partner: after flipping the board, there are no holes for them.\n\n"
                  "Apply anyway?"),
               QMessageBox::Yes | QMessageBox::No, QMessageBox::No)
            != QMessageBox::Yes)
        return;

    // По этой оси посчитаны нынешние УП нижней стороны.
    const double before = Pin::flipAxisX(orig_, origUsed_);
    // Режим -- первым: Pin::setPos ниже обновляет подсказки булавок, а они
    // пишутся по режиму.
    App::project().setPinsPointSymmetry(pointMode_);
    const auto pins = App::pins();
    // Флаги -- раньше пересчёта УП: от них зависит ось переворота.
    for(int i{}; i < 4; ++i)
        if(used_[i] != origUsed_[i]) {
            App::project().setPinUsed(used_[i], i);
            pins[i]->update();
        }
    Pin::setPos(pos_.data());
    // Без изменений проект не помечается изменённым.
    if(pos_ != orig_) {
        App::project().setPinsPos(pos_.data());
        // Расставлены вручную: сохранение УП не должно звать к
        // авторасстановке -- она эту расстановку и снесла бы.
        App::project().setPinsPlaced(true);
    }
    Pin::flipAxisMoved(before);
    QDialog::accept();
}

void PinsDialog::keyPressEvent(QKeyEvent* event) {
    // Поле со значением Enter уже приняло (keyboardTracking выключен) и
    // пропустило дальше -- QDialog нажал бы кнопку по умолчанию, OK.
    if((event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter)
        && qobject_cast<QAbstractSpinBox*>(focusWidget())) {
        event->accept();
        return;
    }
    QDialog::keyPressEvent(event);
}

void PinsDialog::reject() {
    Pin::setPos(orig_.data()); // проект не трогали -- вернуть только сцену
    QDialog::reject();
}

} // namespace Gi

#include "moc_gi_pinsdialog.cpp"
