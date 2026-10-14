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
#include "gc_file.h"
#include "gi_drill.h"
#include "gi_group.h"
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
#include <QVBoxLayout>
#include <algorithm>
#include <cmath>

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
    , used_{origUsed_}
    , sources_{holeSources()} {
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

    // Отверстия
    auto* gbxHoles = new QGroupBox{tr("Drill holes"), this};
    cbxSource_ = new QComboBox{gbxHoles};
    for(const HoleSource& source: sources_)
        cbxSource_->addItem(tr("%1 (%2 holes)").arg(source.name).arg(int(source.holes.size())));
    cbxSource_->setToolTip(tr("Select the tooling holes on the scene before opening this dialog\n"
                              "to snap the pins exactly to them."));
    pbSnap_ = new QPushButton{QIcon::fromTheme(u"drill"_s), tr("Snap to nearest holes"), gbxHoles};
    pbSnap_->setToolTip(tr("Each used pin moves to the centre of the nearest hole."));
    pbSnap_->setAutoDefault(false);
    lblSnap_ = new QLabel{gbxHoles};
    lblSnap_->setWordWrap(true);
    if(sources_.empty()) {
        cbxSource_->setEnabled(false);
        pbSnap_->setEnabled(false);
        lblSnap_->setText(tr("The project has no round drill holes."));
    }

    auto* holesLayout = new QGridLayout{gbxHoles};
    holesLayout->addWidget(new QLabel{tr("Holes:"), gbxHoles}, 0, 0);
    holesLayout->addWidget(cbxSource_, 0, 1);
    holesLayout->addWidget(pbSnap_, 0, 2);
    holesLayout->addWidget(lblSnap_, 1, 0, 1, 3);
    holesLayout->setColumnStretch(1, 1);

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
    connect(pbSnap_, &QPushButton::clicked, this, &PinsDialog::snapToHoles);
    connect(buttons, &QDialogButtonBox::accepted, this, &PinsDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &PinsDialog::reject);

    Pin::showSymmetryMark(true);
    setPointMode(pointMode_); // подписи по режиму, поля, предпросмотр
}

PinsDialog::~PinsDialog() { Pin::showSymmetryMark(false); }

std::vector<PinsDialog::HoleSource> PinsDialog::holeSources() {
    // Только круглые отверстия: у паза нет одной точки, куда встал бы штифт.
    auto holeOf = [](QGraphicsItem* item, std::vector<Hole>& holes) {
        if(!item || item->type() != Gi::Type::Drill) return;
        auto* drill = static_cast<Gi::Drill*>(item);
        if(drill->isSlot() || drill->path().isEmpty()) return;
        // Трансформация файла (поворот, масштаб, зеркало) -- в sceneTransform.
        const double scale = std::sqrt(std::abs(drill->sceneTransform().determinant()));
        holes.push_back({drill->mapToScene(drill->path().front()), drill->diameter() * scale});
    };

    std::vector<HoleSource> sources;
    if(auto* view = App::grView.ptr(); view && view->scene()) {
        HoleSource selected{tr("Selected holes"), {}};
        for(QGraphicsItem* item: view->scene()->selectedItems())
            holeOf(item, selected.holes);
        if(!selected.holes.empty()) sources.push_back(std::move(selected));
    }
    for(AbstractFile* file: App::project().files()) {
        // Отверстия УП (резьбофрезерование) -- не сверловка платы.
        if(!file || dynamic_cast<GCode::File*>(file)) continue;
        HoleSource source{file->shortName(), {}};
        for(Gi::Group* group: file->itemGroups())
            if(group)
                for(const auto& item: *group)
                    holeOf(item.get(), source.holes);
        if(!source.holes.empty()) sources.push_back(std::move(source));
    }
    return sources;
}

QPointF PinsDialog::origin() const { return App::zero.ptr() ? App::zero().pos() : QPointF{}; }

void PinsDialog::setPointMode(bool point) {
    pointMode_ = point;
    // Ось -> точка: трапеция становится прямоугольником (ширину задаёт пара,
    // где используемых больше). Точка -> ось: прямоугольник симметричен и
    // относительно оси, ничего не двигается.
    pos_ = Pin::conform(pos_, used_, pointMode_);
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
    updateFields();
    updatePreview();
}

void PinsDialog::snapToHoles() {
    const int src = cbxSource_->currentIndex();
    if(src < 0 || src >= int(sources_.size())) return;
    const std::vector<Hole>& holes = sources_[size_t(src)].holes;
    if(holes.empty()) return;

    const double axis = Pin::flipAxisX(pos_, used_);
    QStringList info;
    bool snapped[4]{};
    for(int i{}; i < 4; ++i) {
        if(!used_[i]) continue;
        // Ближайшее к булавке отверстие. Обе булавки пары могут прийти к
        // одному отверстию на оси -- это схема с двумя штифтами на оси.
        const Hole* best{};
        double bestDist{};
        for(const Hole& hole: holes)
            if(const double dist = QLineF{pos_[i], hole.pos}.length(); !best || dist < bestDist) {
                best = &hole;
                bestDist = dist;
            }
        pos_[i] = best->pos;
        snapped[i] = true;
        info << tr("Pin %1: moved by %2 mm to a hole Ø%3 mm")
                    .arg(i + 1)
                    .arg(bestDist, 0, 'f', 3)
                    .arg(best->diameter, 0, 'f', 3);
    }
    if(info.isEmpty()) {
        lblSnap_->setText(tr("No pins are used: nothing to snap."));
        return;
    }
    // Неиспользуемый партнёр привязанной булавки -- зеркально ей. Ось задают
    // уже привязанные отверстия, если есть пара с обеими используемыми
    // булавками; иначе ось ничем не задана -- остаётся прежняя.
    const bool fullPair = (used_[0] && used_[1]) || (used_[2] && used_[3]);
    const double mirror = fullPair ? Pin::flipAxisX(pos_, used_) : axis;
    for(int i{}; i < 4; ++i)
        if(snapped[i] && !used_[Pin::partner(i)])
            pos_[Pin::partner(i)] = {2.0 * mirror - pos_[i].x(), pos_[i].y()};
    // Расстановка в окне всегда симметрична: несимметрично стоящие
    // отверстия сдвигают булавки лишь до симметричного положения по режиму.
    pos_ = Pin::conform(pos_, used_, pointMode_);

    lblSnap_->setText(info.join(u'\n'));
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
    const bool moved = pos_ != orig_;
    if(moved) {
        App::project().setPinsPos(pos_.data());
        // Расставлены вручную: сохранение УП не должно звать к
        // авторасстановке -- она эту расстановку и снесла бы.
        App::project().setPinsPlaced(true);
    }
    if(moved || used_ != origUsed_)
        GCode::regenerateGCodeFiles();
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
