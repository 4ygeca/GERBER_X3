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
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QScopedValueRollback>
#include <QSettings>
#include <QVBoxLayout>
#include <cmath>

namespace Gi {

PinsDialog::PinsDialog(QWidget* parent, Mode mode)
    : QDialog{parent}
    , orig_{Pin::positions()}
    , origUsed_{Pin::usedFlags()}
    , pos_{orig_}
    , used_{origUsed_}
    , sources_{holeSources()} {
    setObjectName(u"PinsDialog"_s);
    setWindowTitle(tr("Pins"));
    setWindowFlag(Qt::WindowContextHelpButtonHint, false);

    QSettings settings;
    settings.beginGroup(u"Pin"_s);

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

    // Координаты
    auto* gbxCoordinates = new QGroupBox{tr("Coordinates"), this};
    chbxRelative_ = new QCheckBox{tr("Relative to the Zero point (G-code coordinates)"), gbxCoordinates};
    chbxRelative_->setChecked(settings.value(u"relativeToZero"_s, true).toBool());

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
    grid->addWidget(new QLabel{tr("Flip axis X:"), gbxCoordinates}, 5, 0);
    grid->addWidget(dsbxAxis_ = makeSpinBox(-10000, 10000), 5, 1);
    dsbxAxis_->setToolTip(tr("Vertical axis the board is flipped around for the bottom side.\n"
                             "Changing it moves all pins together."));

    chbxKeepSymmetric_ = new QCheckBox{tr("Keep pairs symmetric (pins 1 ↔ 2 and 4 ↔ 3)"), gbxCoordinates};
    chbxKeepSymmetric_->setToolTip(tr("Changing a pin also moves its mirror partner, the flip axis stays in place."));
    chbxKeepSymmetric_->setChecked(settings.value(u"keepSymmetric"_s, true).toBool());

    auto* coordinatesLayout = new QVBoxLayout{gbxCoordinates};
    coordinatesLayout->addWidget(chbxRelative_);
    coordinatesLayout->addLayout(grid);
    coordinatesLayout->addWidget(chbxKeepSymmetric_);

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

    // Проверка симметрии
    auto* gbxCheck = new QGroupBox{tr("Symmetry check"), this};
    dsbxTolerance_ = makeSpinBox(0.001, 10);
    dsbxTolerance_->setSingleStep(0.01);
    dsbxTolerance_->setValue(settings.value(u"symmetryTolerance"_s, 0.05).toDouble());
    dsbxTolerance_->setToolTip(tr("Allowed miss of a mirrored pin from its partner after flipping the board."));
    lblStatus_ = new QLabel{gbxCheck};
    lblStatus_->setTextFormat(Qt::RichText);
    lblStatus_->setWordWrap(true);

    auto* checkLayout = new QGridLayout{gbxCheck};
    checkLayout->addWidget(new QLabel{tr("Tolerance:"), gbxCheck}, 0, 0);
    checkLayout->addWidget(dsbxTolerance_, 0, 1);
    checkLayout->addWidget(lblStatus_, 1, 0, 1, 3);
    checkLayout->setColumnStretch(2, 1);

    auto* buttons = new QDialogButtonBox{QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this};

    auto* mainLayout = new QVBoxLayout{this};
    mainLayout->addWidget(gbxCoordinates);
    mainLayout->addWidget(gbxHoles);
    mainLayout->addWidget(gbxCheck);
    mainLayout->addStretch();
    mainLayout->addWidget(buttons);

    updateFields();

    for(int i{}; i < 4; ++i) {
        connect(dsbxX_[i], &QDoubleSpinBox::valueChanged, this, [this, i] { pinEdited(i, true); });
        connect(dsbxY_[i], &QDoubleSpinBox::valueChanged, this, [this, i] { pinEdited(i, false); });
        connect(chbxUsed_[i], &QCheckBox::toggled, this, [this, i](bool checked) {
            used_[i] = checked;
            updateFields(); // ось считается по используемым
            updatePreview();
        });
    }
    connect(dsbxAxis_, &QDoubleSpinBox::valueChanged, this, &PinsDialog::axisEdited);
    connect(chbxRelative_, &QCheckBox::toggled, this, [this] {
        updateFields();
        updatePreview();
    });
    connect(dsbxTolerance_, &QDoubleSpinBox::valueChanged, this, [this] { updatePreview(); });
    connect(pbSnap_, &QPushButton::clicked, this, &PinsDialog::snapToHoles);
    connect(buttons, &QDialogButtonBox::accepted, this, &PinsDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &PinsDialog::reject);

    Pin::showFlipAxis(true);
    if(mode == SnapToHoles)
        snapToHoles();
    updatePreview();
}

PinsDialog::~PinsDialog() { Pin::showFlipAxis(false); }

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

bool PinsDialog::hasHoles() { return !holeSources().empty(); }

QPointF PinsDialog::origin() const {
    return chbxRelative_->isChecked() && App::zero.ptr() ? App::zero().pos() : QPointF{};
}

void PinsDialog::pinEdited(int idx, bool isX) {
    if(updating_) return;
    const QPointF org = origin();
    QPointF pt = pos_[idx];
    // Меняется только правленая координата: вторая осталась бы округлённой
    // до трёх знаков поля, а булавка могла стоять на отверстии точнее.
    if(isX)
        pt.setX(dsbxX_[idx]->value() + org.x());
    else
        pt.setY(dsbxY_[idx]->value() + org.y());
    if(chbxKeepSymmetric_->isChecked()) {
        // Ось -- до правки: она стоит, партнёр отражается относительно неё.
        const double axis = Pin::flipAxisX(pos_, used_);
        pos_[Pin::partner(idx)] = {2.0 * axis - pt.x(), pt.y()};
    }
    pos_[idx] = pt;
    updateFields();
    updatePreview();
}

void PinsDialog::axisEdited(double value) {
    if(updating_) return;
    // Ось сдвигается вместе со всей расстановкой: при сдвиге всех булавок по X
    // на одно и то же середины пар уходят ровно туда же.
    const double delta = value + origin().x() - Pin::flipAxisX(pos_, used_);
    for(QPointF& pt: pos_)
        pt.rx() += delta;
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
    // Неиспользуемый партнёр привязанной булавки -- зеркально ей относительно
    // прежней оси, как при перетаскивании пары.
    if(chbxKeepSymmetric_->isChecked())
        for(int i{}; i < 4; ++i)
            if(snapped[i] && !used_[Pin::partner(i)])
                pos_[Pin::partner(i)] = {2.0 * axis - pos_[i].x(), pos_[i].y()};

    lblSnap_->setText(info.join(u'\n'));
    updateFields();
    updatePreview();
}

void PinsDialog::updateFields() {
    const QScopedValueRollback guard{updating_, true};
    const QPointF org = origin();
    for(int i{}; i < 4; ++i) {
        dsbxX_[i]->setValue(pos_[i].x() - org.x());
        dsbxY_[i]->setValue(pos_[i].y() - org.y());
    }
    dsbxAxis_->setValue(Pin::flipAxisX(pos_, used_) - org.x());
}

void PinsDialog::updatePreview() {
    Pin::setPos(pos_.data());
    const Pin::Symmetry sym = Pin::symmetry(pos_, used_);
    Pin::updateFlipAxis(sym.axis, pos_);

    const double tolerance = dsbxTolerance_->value();
    const auto colored = [](const QString& text, bool bad) -> QString {
        return (bad ? u"<span style=\"color:#e04040\">"_s : u"<span style=\"color:#30a030\">"_s) + text + u"</span>"_s;
    };

    QStringList lines;
    lines << tr("Flip axis: X = %1 mm").arg(sym.axis - origin().x(), 0, 'f', 3);
    bool anyUsed{};
    for(int i{}; i < 4; ++i) {
        const int target = sym.target[i];
        if(target < 0) continue;
        anyUsed = true;
        QString line = tr("Pin %1 → pin %2: %3 mm").arg(i + 1).arg(target + 1).arg(sym.deviation[i], 0, 'f', 3);
        const bool bad = sym.deviation[i] > tolerance;
        if(bad && !used_[Pin::partner(i)]) {
            line += u' ';
            line += tr("(pin %1 is not used)").arg(Pin::partner(i) + 1);
        }
        lines << (bad ? colored(line, true) : line);
    }
    if(!anyUsed)
        lines << colored(tr("No pins are used."), true);
    else if(sym.maxDeviation > tolerance)
        lines << colored(tr("The deviation of %1 mm exceeds the tolerance: after flipping, the bottom side will be offset.")
                             .arg(sym.maxDeviation, 0, 'f', 3),
            true);
    else
        lines << colored(tr("Symmetric within the tolerance: the board can be flipped on these pins."), false);
    lblStatus_->setText(lines.join(u"<br>"_s));
}

void PinsDialog::saveSettings() const {
    QSettings settings;
    settings.beginGroup(u"Pin"_s);
    settings.setValue(u"relativeToZero"_s, chbxRelative_->isChecked());
    settings.setValue(u"keepSymmetric"_s, chbxKeepSymmetric_->isChecked());
    settings.setValue(u"symmetryTolerance"_s, dsbxTolerance_->value());
}

void PinsDialog::accept() {
    const Pin::Symmetry sym = Pin::symmetry(pos_, used_);
    const double tolerance = dsbxTolerance_->value();
    if(sym.maxDeviation > tolerance
        && QMessageBox::warning(this, windowTitle(),
               tr("The pins are not symmetric about the flip axis: the deviation of %1 mm exceeds the tolerance of %2 mm.\n"
                  "After flipping, the bottom side will be offset.\n\n"
                  "Apply anyway?")
                   .arg(sym.maxDeviation, 0, 'f', 3)
                   .arg(tolerance, 0, 'f', 3),
               QMessageBox::Yes | QMessageBox::No, QMessageBox::No)
            != QMessageBox::Yes)
        return;

    saveSettings();
    const auto pins = App::pins();
    // Флаги -- раньше пересчёта УП: от них зависит ось переворота.
    for(int i{}; i < 4; ++i)
        if(used_[i] != origUsed_[i]) {
            App::project().setPinUsed(used_[i], i);
            pins[i]->update();
        }
    Pin::setPos(pos_.data());
    App::project().setPinsPos(pos_.data());
    if(pos_ != orig_ || used_ != origUsed_)
        GCode::regenerateGCodeFiles();
    QDialog::accept();
}

void PinsDialog::reject() {
    saveSettings();
    Pin::setPos(orig_.data()); // проект не трогали -- вернуть только сцену
    QDialog::reject();
}

} // namespace Gi

#include "moc_gi_pinsdialog.cpp"
