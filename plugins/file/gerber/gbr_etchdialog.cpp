// This is an open source non-commercial project. Dear PVS-Studio, please check it.
// PVS-Studio Static Code Analyzer for C, C++, C#, and Java: https://pvs-studio.com
/********************************************************************************
 * Author    :  Damir Bakiev                                                    *
 * Version   :  na                                                              *
 * Date      :  October 10, 2026                                                *
 * Website   :  na                                                              *
 * Copyright :  Damir Bakiev 2016-2026                                          *
 * License   :                                                                  *
 * Use, modification & distribution is subject to Boost Software License Ver 1. *
 * http://www.boost.org/LICENSE_1_0.txt                                         *
 *******************************************************************************/
#include "gbr_etchdialog.h"
#include "gbr_attributes.h"
#include "gbr_file.h"

#include "doublespinbox.h"
#include "gi.h"
#include "graphicsview.h"
#include "project.h"

#include <QApplication>
#include <QBrush>
#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFont>
#include <QFormLayout>
#include <QGraphicsPathItem>
#include <QGraphicsScene>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QMessageBox>
#include <QMetaEnum>
#include <QPen>
#include <QPushButton>
#include <QSignalBlocker>
#include <QTreeWidget>
#include <QVBoxLayout>

#include <algorithm>
#include <exception>
#include <limits>
#include <map>
#include <span>

namespace Gerber {

namespace {

// Не Column: в unity-сборке этот файл попадает в один пакет с gbr_netlist.cpp,
// где в том же безымянном пространстве Gerber уже есть enum Column.
enum EtchColumn {
    ColName,
    ColFunction,
    ColObjects,
    ColRule,
};
constexpr int EntryRole = Qt::UserRole;     // номер файла в entries_
constexpr int ClassRole = Qt::UserRole + 1; // номер класса в Entry::classes, -1 -- строка файла

// Курсор ожидания на время пересчёта -- и при исключении тоже.
struct WaitCursor {
    WaitCursor() { QApplication::setOverrideCursor(Qt::WaitCursor); }
    ~WaitCursor() { QApplication::restoreOverrideCursor(); }
    WaitCursor(const WaitCursor&) = delete;
    WaitCursor& operator=(const WaitCursor&) = delete;
};

// Медь ли объекты класса по настройке: ни один, часть или все.
Qt::CheckState tally(const EtchComp& etchComp, std::span<const GrObject* const> objects, bool x2) {
    size_t copper{};
    for(const GrObject* go: objects)
        if(etchComp.copper(*go, x2)) ++copper;
    if(copper == 0) return Qt::Unchecked;
    return copper == objects.size() ? Qt::Checked : Qt::PartiallyChecked;
}

QString apertureFunctionName(int32_t function) {
    if(function < 0) return u"—"_s;
    const char* key = QMetaEnum::fromType<Attr::Aperture::Function>().valueToKey(int{function});
    return key ? QString::fromLatin1(key) : QString::number(function);
}

QString fileFunctionName(int32_t function) {
    if(function < 0) return u"—"_s;
    const char* key = QMetaEnum::fromType<Attr::File::Function>().valueToKey(int{function});
    return key ? QString::fromLatin1(key) : QString::number(function);
}

// Текст исключения: Geo и CGAL бросают std::exception, разбор -- QString.
QString errorText(const std::exception_ptr& error) {
    try {
        std::rethrow_exception(error);
    } catch(const std::exception& e) {
        return QString::fromUtf8(e.what());
    } catch(const QString& e) {
        return e;
    } catch(...) {
        return EtchDialog::tr("unknown error");
    }
}

// То, что выбрали бы правила сами: без ручного выбора классов и объектов.
EtchComp withoutManual(EtchComp etchComp) {
    etchComp.overrides.clear();
    etchComp.excluded.clear();
    return etchComp;
}

// Тела объектов одним путём -- для подсветки, без объединения: под
// WindingFill тела (канонически против часовой, см. Polygons::toPath)
// складываются, а дырка (навстречу телу) гасит лишь своё тело.
QPainterPath outlineOf(std::span<const GrObject* const> objects) {
    QPainterPath path;
    path.setFillRule(Qt::WindingFill);
    for(const GrObject* go: objects)
        path.addPath(go->fill.toPath());
    return path;
}

} // namespace

EtchDialog::EtchDialog(std::vector<File*> files, QWidget* parent)
    : QDialog{parent}
    , offset_{new DoubleSpinBox{this}}
    , plainIsCopper_{new QCheckBox{this}}
    , clearMode_{new QComboBox{this}}
    , keepPadHoles_{new QCheckBox{this}}
    , info_{new QLabel{this}}
    , tree_{new QTreeWidget{this}}
    , exclude_{new QPushButton{this}}
    , clearExcluded_{new QPushButton{this}}
    , status_{new QLabel{this}} {
    setObjectName(u"EtchDialog"_s);
    setWindowTitle(tr("Etch Compensation"));

    for(File* file: files) {
        Entry& entry = entries_.emplace_back();
        entry.file = file;
        entry.backup = entry.edited = file->etchComp();
        entry.x2 = file->hasX2();
        entry.functions = r::any_of(file->graphicObjects2(), [](const GrObject& go) { return go.state.aperFunction() >= 0; });
        entry.shorts = file->shorts();
        if(!file->nets().empty()) nets_ = true;
        // Классы -- в порядке ключей: сначала области, потом D-коды.
        std::map<int32_t, ObjClass> classes;
        for(const GrObject& go: file->graphicObjects2()) {
            ObjClass& cls = classes[EtchComp::keyOf(go)];
            if(cls.objects.empty()) cls.function = go.state.aperFunction();
            cls.objects.push_back(&go);
            if(go.state.imgPolarity() == Negative) ++cls.clear;
        }
        for(auto&& [key, cls]: classes) {
            cls.key = key;
            entry.classes.push_back(std::move(cls));
        }
    }

    // Общие поля -- по первому файлу, как и в TransformDialog.
    const EtchComp first = entries_.empty() ? EtchComp{} : entries_.front().edited;

    offset_->setObjectName(u"dsbxOffset"_s);
    offset_->setDecimals(3);
    offset_->setRange(0.0, 0.2);
    offset_->setSingleStep(0.005);
    offset_->setSuffix(tr(" mm"));
    offset_->setValue(first.offset);
    // После setRange: тот пишет в подсказку диапазон.
    offset_->setToolTip(tr("Undercut compensation: every copper edge moves outward by this value.\n"
                           "Tracks and pads grow by twice the offset, gaps between them shrink by twice the offset.\n"
                           "KiCad's PostScript \"track width correction\" is the full width change, i.e. twice this value.\n"
                           "Applied in file coordinates, before the affine transform; 0 means no compensation."));

    plainIsCopper_->setObjectName(u"chbxPlainIsCopper"_s);
    plainIsCopper_->setText(tr("Unattributed objects are copper"));
    plainIsCopper_->setChecked(first.plainIsCopper);
    plainIsCopper_->setToolTip(tr("Dark objects without an X2 aperture function (%TA.AperFunction) and outside any net (%TO.N).\n"
                                  "Turn it off if text and logos share apertures with tracks, then pick copper classes in the table."));

    clearMode_->setObjectName(u"cbxClearMode"_s);
    clearMode_->addItem(tr("Auto"), int{EtchComp::AutoClear});
    clearMode_->addItem(tr("Keep exact size"), int{EtchComp::ExactClear});
    clearMode_->addItem(tr("Shrink with copper"), int{EtchComp::ShrinkClear});
    clearMode_->setCurrentIndex(std::max(0, clearMode_->findData(int{first.clearMode})));
    clearMode_->setToolTip(tr("Clear objects (%LPC) without an X2 aperture function.\n"
                              "Auto: exact size if the file has X2 attributes (KiCad drill marks),\n"
                              "otherwise they are voids in copper and shrink with it."));

    keepPadHoles_->setObjectName(u"chbxKeepPadHoles"_s);
    keepPadHoles_->setText(tr("Keep aperture holes exact size"));
    keepPadHoles_->setChecked(first.keepPadHoles);
    keepPadHoles_->setToolTip(tr("Holes of flashed apertures (e.g. %ADD10C,1.6X0.8*%) keep their size\n"
                                 "where nothing else covers them."));

    { // Сводка по файлам: есть ли на что опереться правилам.
        QStringList lines;
        for(const Entry& entry: entries_) {
            const File* file = entry.file;
            QString function = fileFunctionName(file->fileFunction());
            if(file->fileFunction() >= 0 && file->fileFunction() != Attr::File::Copper)
                function += tr(" (not a copper layer)");
            lines.push_back(tr("<b>%1</b>: X2 aperture functions: %2; nets: %3; file function: %4")
                                .arg(file->shortName().toHtmlEscaped(),
                                    entry.functions ? tr("yes") : tr("no"),
                                    QString::number(file->nets().size()),
                                    function.toHtmlEscaped()));
        }
        info_->setObjectName(u"lblInfo"_s);
        info_->setTextFormat(Qt::RichText);
        info_->setWordWrap(true);
        info_->setText(lines.join(u"<br>"_s));
    }

    tree_->setObjectName(u"treeClasses"_s);
    tree_->setColumnCount(4);
    tree_->setHeaderLabels({tr("Class"), tr("Function"), tr("Objects"), tr("By rules")});
    tree_->setUniformRowHeights(true);
    tree_->setSelectionMode(QAbstractItemView::ExtendedSelection);
    tree_->header()->setSectionResizeMode(ColName, QHeaderView::Stretch);
    tree_->header()->setSectionResizeMode(ColFunction, QHeaderView::ResizeToContents);
    tree_->header()->setSectionResizeMode(ColObjects, QHeaderView::ResizeToContents);
    tree_->header()->setSectionResizeMode(ColRule, QHeaderView::ResizeToContents);
    tree_->header()->setStretchLastSection(false);
    tree_->setToolTip(tr("Checked classes are compensated as copper; for clear objects the void shrinks with the copper.\n"
                         "Bold: chosen manually; the \"By rules\" column shows the automatic choice.")
        + u'\n' + tr("Selected rows are highlighted on the scene, double click zooms to them."));
    fillTree();
    connect(tree_, &QTreeWidget::itemChanged, this, &EtchDialog::onItemChanged);
    connect(tree_, &QTreeWidget::itemSelectionChanged, this, &EtchDialog::updateHighlight);
    connect(tree_, &QTreeWidget::itemDoubleClicked, this, &EtchDialog::zoomToHighlight);

    // Правила меняют разметку таблицы, но не слой: пересчёт -- по Apply/OK.
    connect(plainIsCopper_, &QCheckBox::toggled, this, &EtchDialog::updateRows);
    connect(clearMode_, &QComboBox::currentIndexChanged, this, &EtchDialog::updateRows);

    auto* resetButton = new QPushButton{tr("Reset to rules"), this};
    resetButton->setObjectName(u"pbReset"_s);
    connect(resetButton, &QPushButton::clicked, this, &EtchDialog::resetOverrides);

    exclude_->setObjectName(u"pbExclude"_s);
    exclude_->setText(tr("Exclude selected"));
    exclude_->setToolTip(tr("Objects lying in the copper bodies (Normal view) or on the aperture paths\n"
                            "selected on the scene are never compensated, whatever their class says.\n"
                            "Select them on the scene before opening this dialog."));
    connect(exclude_, &QPushButton::clicked, this, &EtchDialog::excludeSelected);

    clearExcluded_->setObjectName(u"pbClearExcluded"_s);
    clearExcluded_->setText(tr("Clear exclusions"));
    clearExcluded_->setToolTip(tr("Excluded objects follow their classes again."));
    connect(clearExcluded_, &QPushButton::clicked, this, &EtchDialog::clearExclusions);
    updateButtons();

    auto* hint = new QLabel{tr("Toolpaths created earlier are not recalculated."), this};
    hint->setObjectName(u"lblHint"_s);
    hint->setEnabled(false);

    status_->setObjectName(u"lblStatus"_s);
    if(nets_) {
        int32_t shorts{};
        for(const Entry& entry: entries_) shorts += entry.shorts;
        status_->setText(tr("Short circuits: %1").arg(shorts));
    } else
        status_->setText(tr("The files have no nets (X2 %TO.N): short circuits are not checked."));

    auto* buttonBox = new QDialogButtonBox{QDialogButtonBox::Ok | QDialogButtonBox::Apply | QDialogButtonBox::Cancel, this};
    buttonBox->setObjectName(u"buttonBox"_s);
    connect(buttonBox, &QDialogButtonBox::accepted, this, &EtchDialog::accept);
    connect(buttonBox, &QDialogButtonBox::rejected, this, &EtchDialog::reject);
    connect(buttonBox->button(QDialogButtonBox::Apply), &QPushButton::clicked, this, [this] { apply(); });

    auto* form = new QFormLayout;
    form->setLabelAlignment(Qt::AlignRight);
    form->addRow(tr("Offset per edge:"), offset_);
    form->addRow(plainIsCopper_);
    form->addRow(tr("Clear objects without function:"), clearMode_);
    form->addRow(keepPadHoles_);

    auto* resetRow = new QHBoxLayout;
    resetRow->addWidget(resetButton);
    resetRow->addWidget(exclude_);
    resetRow->addWidget(clearExcluded_);
    resetRow->addWidget(hint, 1);

    auto* layout = new QVBoxLayout{this};
    layout->setContentsMargins(6, 6, 6, 6);
    layout->addLayout(form);
    layout->addWidget(info_);
    layout->addWidget(tree_, 1);
    layout->addLayout(resetRow);
    layout->addWidget(status_);
    layout->addWidget(buttonBox);

    resize(640, 520);
}

EtchDialog::~EtchDialog() {
    // Таблицу рушит уже ~QWidget, после полей диалога, -- её сигналам сюда
    // больше хода нет.
    tree_->disconnect(this);
    // Подсветка -- только на время диалога. Сцену Qt рушит вместе с её
    // элементами, и тогда удалять уже нечего.
    if(auto* view = App::grView.ptr(); view && view->scene())
        for(const Entry& entry: entries_) delete entry.overlay;
}

EtchComp EtchDialog::settings(const Entry& entry) const {
    EtchComp etchComp = entry.edited;
    etchComp.offset = offset_->value();
    etchComp.plainIsCopper = plainIsCopper_->isChecked();
    etchComp.clearMode = static_cast<EtchComp::ClearMode>(clearMode_->currentData().toInt());
    etchComp.keepPadHoles = keepPadHoles_->isChecked();
    return etchComp;
}

void EtchDialog::fillTree() {
    const QSignalBlocker blocker{tree_};
    tree_->clear();
    for(size_t e{}; e < entries_.size(); ++e) {
        Entry& entry = entries_[e];
        auto* fileRow = new QTreeWidgetItem{tree_};
        fileRow->setText(ColName, entry.file->shortName());
        fileRow->setToolTip(ColName, entry.file->name());
        fileRow->setText(ColFunction, fileFunctionName(entry.file->fileFunction()));
        fileRow->setText(ColObjects, QString::number(entry.file->graphicObjects2().size()));
        fileRow->setTextAlignment(ColObjects, Qt::AlignRight | Qt::AlignVCenter);
        fileRow->setFlags(Qt::ItemIsEnabled);
        fileRow->setData(ColName, EntryRole, static_cast<int>(e));
        fileRow->setData(ColName, ClassRole, -1);

        const ApertureMap* apertures = entry.file->apertures();
        for(size_t c{}; c < entry.classes.size(); ++c) {
            ObjClass& cls = entry.classes[c];
            auto* row = cls.row = new QTreeWidgetItem{fileRow};
            if(cls.key < 0) {
                row->setText(ColName, tr("Regions"));
                // KiCad пишет %TA.AperFunction только для областей через
                // PlotGerberRegion (заливки, площадки); текст шрифтом TrueType и
                // инверсный (knockout) идут голыми G36/G37 -- без функции.
                if(cls.key == EtchComp::regionKey(-1) && entry.functions)
                    row->setToolTip(ColName, tr("Regions without an aperture function in a file with X2 attributes:\n"
                                                "usually KiCad text in a TrueType font or knockout text,\n"
                                                "which KiCad plots without an aperture function."));
            } else {
                QString name = u"D%1"_s.arg(cls.key);
                if(const auto it = apertures->find(cls.key); it != apertures->end() && it->second)
                    name += u"  "_s + it->second->name();
                row->setText(ColName, name);
            }
            row->setText(ColFunction, apertureFunctionName(cls.function));
            const QString count = QString::number(cls.objects.size());
            row->setText(ColObjects, cls.clear ? tr("%1 (%2 clear)").arg(count).arg(cls.clear) : count);
            row->setTextAlignment(ColObjects, Qt::AlignRight | Qt::AlignVCenter);
            row->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsUserCheckable | Qt::ItemNeverHasChildren);
            row->setData(ColName, EntryRole, static_cast<int>(e));
            row->setData(ColName, ClassRole, static_cast<int>(c));
        }

        // Исключённые поштучно -- не класс: отметки нет, выбрать можно (подсветка).
        auto* excludedRow = entry.excludedRow = new QTreeWidgetItem{fileRow};
        excludedRow->setText(ColName, tr("Excluded on scene"));
        excludedRow->setToolTip(ColName, tr("Objects excluded with \"Exclude selected\": never compensated."));
        excludedRow->setText(ColRule, tr("not copper"));
        excludedRow->setTextAlignment(ColObjects, Qt::AlignRight | Qt::AlignVCenter);
        excludedRow->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemNeverHasChildren);
        excludedRow->setData(ColName, EntryRole, static_cast<int>(e));
        excludedRow->setData(ColName, ClassRole, -1);
    }
    tree_->expandAll();
    updateRows();
}

void EtchDialog::updateRows() {
    const QSignalBlocker blocker{tree_};
    for(const Entry& entry: entries_) {
        const EtchComp etchComp = settings(entry);
        const EtchComp rules = withoutManual(etchComp);
        for(const ObjClass& cls: entry.classes)
            updateRow(entry, cls, etchComp, rules);
        updateExcludedRow(entry);
    }
}

void EtchDialog::updateRow(const Entry& entry, const ObjClass& cls, const EtchComp& etchComp, const EtchComp& rules) {
    // Отметка -- то, что будет применено (с ручным выбором), колонка правил --
    // что выбрали бы правила сами. Частичная -- в классе есть и то и другое.
    const Qt::CheckState rule = tally(rules, cls.objects, entry.x2);
    cls.row->setCheckState(ColName, tally(etchComp, cls.objects, entry.x2));
    switch(rule) {
    case Qt::Checked  : cls.row->setText(ColRule, tr("copper")); break;
    case Qt::Unchecked: cls.row->setText(ColRule, tr("not copper")); break;
    default           : cls.row->setText(ColRule, tr("mixed")); break;
    }
    QFont font = cls.row->font(ColName);
    font.setBold(etchComp.overrides.contains(cls.key));
    cls.row->setFont(ColName, font);
}

void EtchDialog::updateExcludedRow(const Entry& entry) {
    // Объектов, а не ключей: у совпавших дублей ключ один.
    const auto count = entry.edited.excluded.empty()
        ? 0
        : r::count_if(entry.file->graphicObjects2(), [&entry](const GrObject& go) { return entry.edited.isExcluded(go); });
    const QSignalBlocker blocker{tree_};
    entry.excludedRow->setText(ColObjects, QString::number(count));
    if(count == 0) entry.excludedRow->setSelected(false);
    entry.excludedRow->setHidden(count == 0);
}

void EtchDialog::onItemChanged(QTreeWidgetItem* item, int column) {
    if(column != ColName || !item || !item->parent()) return;
    const int e = item->data(ColName, EntryRole).toInt();
    const int c = item->data(ColName, ClassRole).toInt();
    if(e < 0 || static_cast<size_t>(e) >= entries_.size()) return;
    Entry& entry = entries_[static_cast<size_t>(e)];
    if(c < 0 || static_cast<size_t>(c) >= entry.classes.size()) return;
    const ObjClass& cls = entry.classes[static_cast<size_t>(c)];

    // Частичную отметку ставит только сама таблица, щелчок даёт полную.
    const Qt::CheckState state = item->checkState(ColName);
    if(state == Qt::PartiallyChecked) return;

    const EtchComp rules = withoutManual(settings(entry));
    // Совпало с правилами -- ручной выбор не нужен, иначе он запоминается.
    // Исключённые поштучно остаются исключёнными: отметка класса их не касается.
    if(tally(rules, cls.objects, entry.x2) == state)
        entry.edited.overrides.erase(cls.key);
    else
        entry.edited.overrides[cls.key] = state == Qt::Checked;

    const QSignalBlocker blocker{tree_};
    updateRow(entry, cls, settings(entry), rules);
}

void EtchDialog::resetOverrides() {
    for(Entry& entry: entries_)
        entry.edited.overrides.clear();
    updateRows();
}

bool EtchDialog::apply() {
    offset_->interpretText(); // набранное, но ещё не принятое счётчиком

    std::vector<EtchComp> previous; // до этого Apply -- откат при ошибке
    previous.reserve(entries_.size());
    bool changed{};
    std::exception_ptr error;
    {
        WaitCursor wait;
        try {
            for(const Entry& entry: entries_) {
                previous.push_back(entry.file->etchComp());
                const EtchComp etchComp = settings(entry);
                if(etchComp != previous.back()) changed = true;
                entry.file->setEtchComp(etchComp);
            }
        } catch(...) {
            error = std::current_exception();
            // Файл, на котором упало, и все до него -- к прежнему.
            for(size_t i{}; i < previous.size(); ++i) {
                try {
                    entries_[i].file->setEtchComp(previous[i]);
                } catch(...) {
                    qWarning() << entries_[i].file->shortName() << u"-- etch compensation rollback failed"_s;
                }
            }
        }
    }
    if(changed) {
        applied_ = true;
        App::project().setChanged();
    }
    // Пересобранные элементы сцены родятся невыделенными -- исключать по
    // прежнему выделению больше нечего.
    updateButtons();

    if(error) {
        QMessageBox::critical(this, windowTitle(), tr("Etch compensation failed:\n%1").arg(errorText(error)));
        return false;
    }

    if(nets_) {
        int32_t before{}, after{};
        for(const Entry& entry: entries_) {
            before += entry.shorts;
            after += entry.file->shorts();
        }
        status_->setText(tr("Short circuits: %1 → %2").arg(before).arg(after));
    }
    return true;
}

void EtchDialog::restore() {
    std::exception_ptr error;
    {
        WaitCursor wait;
        for(const Entry& entry: entries_) {
            try {
                entry.file->setEtchComp(entry.backup);
            } catch(...) {
                if(!error) error = std::current_exception();
            }
        }
    }
    if(error)
        QMessageBox::critical(this, windowTitle(), tr("Etch compensation failed:\n%1").arg(errorText(error)));
}

void EtchDialog::accept() {
    if(!apply()) return;
    if(nets_) {
        // Раздувание закрыло зазоры между цепями -- спросить, а не молча уйти.
        int32_t before{}, after{};
        for(const Entry& entry: entries_) {
            before += entry.shorts;
            after += entry.file->shorts();
        }
        if(after > before
            && QMessageBox::question(this, windowTitle(),
                   tr("Compensation adds short circuits: %1 → %2.\n"
                      "Clearances between nets are closed. Keep it?")
                       .arg(before)
                       .arg(after))
                != QMessageBox::Yes)
            return;
    }
    QDialog::accept();
}

void EtchDialog::reject() {
    if(applied_) restore();
    QDialog::reject();
}

void EtchDialog::updateHighlight() {
    auto* view = App::grView.ptr();
    QGraphicsScene* scene = view ? view->scene() : nullptr;
    if(!scene) return;

    std::optional<WaitCursor> wait; // только если есть что строить
    for(Entry& entry: entries_) {
        QPainterPath path;
        path.setFillRule(Qt::WindingFill);
        for(ObjClass& cls: entry.classes) {
            if(!cls.row->isSelected()) continue;
            if(!cls.outline) {
                if(!wait) wait.emplace();
                cls.outline = outlineOf(cls.objects);
            }
            path.addPath(*cls.outline);
        }
        if(!entry.excludedRow->isHidden() && entry.excludedRow->isSelected()) {
            std::vector<const GrObject*> excluded;
            for(const GrObject& go: entry.file->graphicObjects2())
                if(entry.edited.isExcluded(go)) excluded.push_back(&go);
            path.addPath(outlineOf(excluded));
        }

        if(path.isEmpty()) {
            if(entry.overlay) entry.overlay->hide();
            continue;
        }
        if(!entry.overlay) {
            // Пурпур -- его нет в палитре слоёв (Node::repaint раздаёт оттенки
            // 0..200), обводка -- косметическая, в пиксель при любом масштабе.
            // Выше всех слоёв, но ниже маркеров и превью форм.
            auto* overlay = entry.overlay = new QGraphicsPathItem;
            overlay->setBrush(QColor{255, 0, 255, 120});
            QPen pen{Qt::white};
            pen.setCosmetic(true);
            overlay->setPen(pen);
            overlay->setZValue(std::numeric_limits<double>::max() - 100);
            overlay->setAcceptedMouseButtons(Qt::NoButton);
            overlay->setAcceptHoverEvents(false);
            scene->addItem(overlay);
        }
        entry.overlay->setTransform(entry.file->transform().toQTransform());
        entry.overlay->setPath(path);
        entry.overlay->show();
    }
}

void EtchDialog::zoomToHighlight() {
    QRectF rect;
    for(const Entry& entry: entries_)
        if(entry.overlay && entry.overlay->isVisible()) rect |= entry.overlay->sceneBoundingRect();
    if(auto* view = App::grView.ptr(); view && !rect.isEmpty()) view->fitInView(rect);
}

std::vector<Gi::Item*> EtchDialog::sceneSelection() const {
    std::vector<Gi::Item*> items;
    auto* view = App::grView.ptr();
    if(!view || !view->scene()) return items;
    for(QGraphicsItem* item: view->selectedItems(Gi::Type::DataSolid, Gi::Type::DataPath)) {
        auto* gi = static_cast<Gi::Item*>(item);
        if(r::any_of(entries_, [gi](const Entry& entry) { return gi->file() == entry.file; }))
            items.push_back(gi);
    }
    return items;
}

void EtchDialog::excludeSelected() {
    const std::vector<Gi::Item*> items = sceneSelection();
    bool added{};
    {
        WaitCursor wait;
        for(Entry& entry: entries_) {
            std::vector<int64_t>& excluded = entry.edited.excluded;
            const size_t before = excluded.size();
            excluded.append_range(entry.file->objectKeys(items));
            r::sort(excluded);
            excluded.erase(std::unique(excluded.begin(), excluded.end()), excluded.end());
            if(excluded.size() != before) added = true;
        }
    }
    if(!added) {
        QMessageBox::information(this, windowTitle(),
            tr("Nothing to exclude: no objects of these files lie in the bodies or on the aperture paths selected on the scene.\n"
               "Select them on the scene before opening this dialog."));
        return;
    }

    updateRows();
    // Показать, что исключено: выбрать строки исключённых.
    {
        const QSignalBlocker blocker{tree_};
        tree_->clearSelection();
        for(const Entry& entry: entries_)
            if(!entry.excludedRow->isHidden()) entry.excludedRow->setSelected(true);
    }
    updateHighlight();
    updateButtons();
}

void EtchDialog::clearExclusions() {
    for(Entry& entry: entries_)
        entry.edited.excluded.clear();
    updateRows();
    updateHighlight();
    updateButtons();
}

void EtchDialog::updateButtons() {
    exclude_->setEnabled(!sceneSelection().empty());
    clearExcluded_->setEnabled(r::any_of(entries_, [](const Entry& entry) { return !entry.edited.excluded.empty(); }));
}

} // namespace Gerber

#include "moc_gbr_etchdialog.cpp"
