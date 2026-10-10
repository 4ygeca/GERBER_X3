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
// Заголовки Qt -- первыми (см. gi.h).
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDir>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPageLayout>
#include <QPageSize>
#include <QPrintPreviewDialog>
#include <QPrinter>
#include <QPushButton>
#include <QSettings>
#include <QTimer>
#include <QToolButton>
#include <QTreeWidget>
#include <QVBoxLayout>

#include "plotdialog.h"

#include "abstract_file.h"
#include "app.h"
#include "gc_file.h"
#include "project.h"

#include <algorithm>
#include <exception>

PlotDialog::PlotDialog(const QString& lastPath, Mode mode, QWidget* parent)
    : QDialog{parent} {
    setObjectName(u"PlotDialog"_s);
    setWindowTitle(tr("Plot / Export"));
    setWindowFlag(Qt::WindowContextHelpButtonHint, false);

    Plot::Options opt;
    opt.load();

    // Слои ----------------------------------------------------------------
    files_ = new QTreeWidget{this};
    files_->setColumnCount(ColCount);
    files_->setHeaderLabels({tr("Plot"), tr("File"), tr("Side"), tr("Mirror"), tr("Negative")});
    files_->headerItem()->setToolTip(ColMirror, tr("Mirror about the centre of the common page"));
    files_->headerItem()->setToolTip(ColNegative, tr("Ink everywhere on the page except the copper"));
    files_->setRootIsDecorated(false);
    files_->setUniformRowHeights(true);
    files_->header()->setStretchLastSection(false);
    files_->header()->setSectionResizeMode(QHeaderView::ResizeToContents);
    files_->header()->setSectionResizeMode(ColFile, QHeaderView::Stretch);

    auto* all = new QPushButton{tr("All"), this};
    auto* none = new QPushButton{tr("None", "select no layers"), this};
    auto* mirrorTop = new QPushButton{QIcon::fromTheme(u"flip-horizontal"_s), tr("Mirror top layers"), this};
    mirrorTop->setToolTip(tr("Mirror the top-side layers and clear the mirror of the bottom ones -- "
                             "as needed for toner transfer and films laid face down"));
    connect(all, &QPushButton::clicked, this, [this] { setColumnChecked(ColExport, true); });
    connect(none, &QPushButton::clicked, this, [this] { setColumnChecked(ColExport, false); });
    connect(mirrorTop, &QPushButton::clicked, this, &PlotDialog::mirrorTopLayers);

    auto* filesButtons = new QHBoxLayout;
    filesButtons->addWidget(all);
    filesButtons->addWidget(none);
    filesButtons->addStretch();
    filesButtons->addWidget(mirrorTop);

    auto* filesBox = new QGroupBox{tr("Layers"), this};
    auto* filesLayout = new QVBoxLayout{filesBox};
    filesLayout->addWidget(files_);
    filesLayout->addLayout(filesButtons);

    // Настройки -----------------------------------------------------------
    const auto spinBox = [this](double lowest, double highest, int decimals, double step, const QString& suffix, double value) {
        auto* box = new QDoubleSpinBox{this};
        box->setDecimals(decimals); // до диапазона и значения: по нему они округляются
        box->setRange(lowest, highest);
        box->setSingleStep(step);
        box->setSuffix(suffix);
        box->setValue(value);
        return box;
    };
    const QString mm = tr(" mm");

    format_ = new QComboBox{this};
    format_->addItem(u"SVG"_s, static_cast<int>(Plot::Format::Svg));
    format_->addItem(tr("DXF R12 (outlines only)"), static_cast<int>(Plot::Format::Dxf));
    format_->addItem(u"PDF"_s, static_cast<int>(Plot::Format::Pdf));
    format_->setItemData(format_->findData(static_cast<int>(Plot::Format::Svg)),
        tr("Filled layers, 1 unit = 1 mm. Recommended for CorelDRAW and Inkscape."), Qt::ToolTipRole);
    format_->setItemData(format_->findData(static_cast<int>(Plot::Format::Dxf)),
        tr("R12 has no fills: only the outlines are written, in millimetres. "
           "Fill them in the drawing program (e.g. Combine in CorelDRAW)."),
        Qt::ToolTipRole);
    format_->setItemData(format_->findData(static_cast<int>(Plot::Format::Pdf)),
        tr("Page of exactly the plot size. Print it at 100 % (no fit to page)."), Qt::ToolTipRole);
    format_->setCurrentIndex(std::max(0, format_->findData(static_cast<int>(opt.format))));
    format_->setToolTip(tr("File format for Export. Print preview does not depend on it."));

    layout_ = new QComboBox{this};
    layout_->addItem(tr("One file (page) per layer"), static_cast<int>(Plot::Layout::PerLayer));
    layout_->addItem(tr("All layers on one sheet"), static_cast<int>(Plot::Layout::Tiled));
    layout_->setCurrentIndex(std::max(0, layout_->findData(static_cast<int>(opt.layout))));
    layout_->setToolTip(tr("On one sheet the layers lie side by side; for printing they are "
                           "arranged to fit the paper best."));

    gap_ = spinBox(0.0, 100.0, 2, 1.0, mm, opt.gap);
    margin_ = spinBox(0.0, 100.0, 2, 1.0, mm, opt.margin);
    margin_->setToolTip(tr("Border around the common extent of all checked layers. "
                           "The page is the same for every layer, so separate files line up."));

    regMarks_ = new QCheckBox{tr("Registration marks"), this};
    regMarks_->setChecked(opt.regMarks);
    regMarks_->setToolTip(tr("Identical crosses in the corners of the margin of every output, "
                             "for aligning the layers. Need a margin of at least 1.25 mm."));

    drillMarks_ = new QComboBox{this};
    drillMarks_->addItem(tr("None", "drill marks"), static_cast<int>(Plot::DrillMarks::None));
    drillMarks_->addItem(tr("Actual size"), static_cast<int>(Plot::DrillMarks::Actual));
    drillMarks_->addItem(tr("Small"), static_cast<int>(Plot::DrillMarks::Small));
    drillMarks_->setCurrentIndex(std::max(0, drillMarks_->findData(static_cast<int>(opt.drillMarks))));
    drillMarks_->setToolTip(tr("Holes of the chosen drill file are cut out of every other layer: "
                               "at actual size or as small centring marks."));
    drillFile_ = new QComboBox{this};
    drillFile_->setToolTip(tr("Drill file whose holes are marked"));
    auto* drillRow = new QHBoxLayout;
    drillRow->addWidget(drillMarks_);
    drillRow->addWidget(drillFile_, 1);

    smallDrill_ = spinBox(0.05, 5.0, 2, 0.05, mm, opt.smallDrill);
    lineWidth_ = spinBox(0.01, 5.0, 3, 0.01, mm, opt.lineWidth);
    lineWidth_->setToolTip(tr("Stroke width for paths without fill: outlines, zero-width traces, "
                              "layers shown as paths"));

    calibX_ = spinBox(50.0, 150.0, 3, 0.1, u" %"_s, opt.calibX);
    calibY_ = spinBox(50.0, 150.0, 3, 0.1, u" %"_s, opt.calibY);
    const QString calibTip = tr("Print only: scale correction of the printer. Print a known length, "
                                "measure it and enter required / measured x 100 %.");
    calibX_->setToolTip(calibTip);
    calibY_->setToolTip(calibTip);
    auto* calibRow = new QHBoxLayout;
    calibRow->addWidget(new QLabel{u"X"_s, this});
    calibRow->addWidget(calibX_, 1);
    calibRow->addWidget(new QLabel{u"Y"_s, this});
    calibRow->addWidget(calibY_, 1);

    auto* optionsBox = new QGroupBox{tr("Options"), this};
    auto* form = new QFormLayout{optionsBox};
    form->addRow(tr("Format:"), format_);
    form->addRow(tr("Layout:"), layout_);
    form->addRow(tr("Gap between layers:"), gap_);
    form->addRow(tr("Margin:"), margin_);
    form->addRow(QString{}, regMarks_);
    form->addRow(tr("Drill marks:"), drillRow);
    form->addRow(tr("Small mark diameter:"), smallDrill_);
    form->addRow(tr("Line width of paths:"), lineWidth_);
    form->addRow(tr("Printer calibration:"), calibRow);

    // Папка вывода -----------------------------------------------------------
    outDir_ = new QLineEdit{this};
    auto* browse = new QToolButton{this};
    browse->setIcon(QIcon::fromTheme(u"document-open"_s));
    browse->setToolTip(tr("Choose the output folder"));
    connect(browse, &QToolButton::clicked, this, &PlotDialog::browseOutDir);
    auto* outRow = new QHBoxLayout;
    outRow->addWidget(new QLabel{tr("Output folder:"), this});
    outRow->addWidget(outDir_, 1);
    outRow->addWidget(browse);

    status_ = new QLabel{this};
    status_->setWordWrap(true);
    status_->setTextInteractionFlags(Qt::TextSelectableByMouse);

    auto* buttons = new QDialogButtonBox{this};
    QPushButton* preview = buttons->addButton(tr("Print preview..."), QDialogButtonBox::ActionRole);
    preview->setIcon(QIcon::fromTheme(u"document-print"_s));
    QPushButton* exportButton = buttons->addButton(tr("Export"), QDialogButtonBox::ActionRole);
    exportButton->setIcon(QIcon::fromTheme(u"document-export"_s));
    buttons->addButton(QDialogButtonBox::Close);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(preview, &QPushButton::clicked, this, &PlotDialog::printPreview);
    connect(exportButton, &QPushButton::clicked, this, &PlotDialog::exportFiles);
    (mode == PrintMode ? preview : exportButton)->setDefault(true);

    auto* root = new QVBoxLayout{this};
    root->addWidget(filesBox, 1);
    root->addWidget(optionsBox);
    root->addLayout(outRow);
    root->addWidget(status_);
    root->addWidget(buttons);

    fillFiles(opt.drillFile);

    // Папка из настроек -- только если выбрана для этого же проекта; иначе
    // папка последнего открытого файла.
    QSettings settings;
    settings.beginGroup(u"Plot"_s);
    const bool sameProject = settings.value(u"outDirProject"_s).toString() == App::project().name();
    resize(600, 640);
    restoreGeometry(settings.value(u"geometry"_s).toByteArray());
    settings.endGroup();
    QString dir = sameProject && !opt.outDir.isEmpty() ? opt.outDir : lastPath;
    if(dir.isEmpty()) dir = QDir::homePath();
    outDir_->setText(QDir::toNativeSeparators(dir));

    connect(layout_, &QComboBox::currentIndexChanged, this, &PlotDialog::updateEnabled);
    connect(drillMarks_, &QComboBox::currentIndexChanged, this, &PlotDialog::updateEnabled);
    updateEnabled();
}

void PlotDialog::done(int result) {
    options().save();
    QSettings settings;
    settings.beginGroup(u"Plot"_s);
    settings.setValue(u"geometry"_s, saveGeometry());
    settings.setValue(u"outDirProject"_s, App::project().name());
    settings.endGroup();
    QDialog::done(result);
}

void PlotDialog::fillFiles(const QString& drillFileName) {
    // Всё, кроме УП: траектории -- не слой платы. Отмечено то, что видно в
    // дереве.
    for(AbstractFile* file: App::project().files()) {
        if(dynamic_cast<GCode::File*>(file)) continue;
        auto* item = new QTreeWidgetItem(files_);
        item->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsUserCheckable);
        item->setData(ColExport, Qt::UserRole, file->id());
        item->setCheckState(ColExport, file->isVisible() ? Qt::Checked : Qt::Unchecked);
        item->setIcon(ColFile, file->icon());
        item->setText(ColFile, file->shortName());
        item->setToolTip(ColFile, QDir::toNativeSeparators(file->name()));
        const bool bottom = file->side() == Side::Bottom;
        item->setText(ColSide, bottom ? tr("Bottom") : tr("Top"));
        item->setData(ColSide, Qt::UserRole, bottom);
        item->setCheckState(ColMirror, Qt::Unchecked);
        item->setCheckState(ColNegative, Qt::Unchecked);
        if(Plot::hasDrills(file))
            drillFile_->addItem(file->icon(), file->shortName(), file->id());
    }
    if(const int index = drillFile_->findText(drillFileName); index >= 0)
        drillFile_->setCurrentIndex(index);
}

void PlotDialog::setColumnChecked(int column, bool checked) {
    for(int i{}; i < files_->topLevelItemCount(); ++i)
        files_->topLevelItem(i)->setCheckState(column, checked ? Qt::Checked : Qt::Unchecked);
}

void PlotDialog::mirrorTopLayers() {
    for(int i{}; i < files_->topLevelItemCount(); ++i) {
        QTreeWidgetItem* item = files_->topLevelItem(i);
        const bool bottom = item->data(ColSide, Qt::UserRole).toBool();
        item->setCheckState(ColMirror, bottom ? Qt::Unchecked : Qt::Checked);
    }
}

void PlotDialog::updateEnabled() {
    gap_->setEnabled(static_cast<Plot::Layout>(layout_->currentData().toInt()) == Plot::Layout::Tiled);
    const bool haveDrills = drillFile_->count() > 0;
    const auto marks = static_cast<Plot::DrillMarks>(drillMarks_->currentData().toInt());
    drillMarks_->setEnabled(haveDrills);
    drillFile_->setEnabled(haveDrills && marks != Plot::DrillMarks::None);
    smallDrill_->setEnabled(haveDrills && marks == Plot::DrillMarks::Small);
}

void PlotDialog::browseOutDir() {
    const QString dir = QFileDialog::getExistingDirectory(this, tr("Output folder"), outDir_->text());
    if(!dir.isEmpty()) outDir_->setText(QDir::toNativeSeparators(dir));
}

Plot::Options PlotDialog::options() const {
    Plot::Options opt;
    opt.format = static_cast<Plot::Format>(format_->currentData().toInt());
    opt.layout = static_cast<Plot::Layout>(layout_->currentData().toInt());
    opt.gap = gap_->value();
    opt.margin = margin_->value();
    opt.regMarks = regMarks_->isChecked();
    opt.drillMarks = static_cast<Plot::DrillMarks>(drillMarks_->currentData().toInt());
    opt.drillFile = drillFile_->currentText();
    opt.smallDrill = smallDrill_->value();
    opt.lineWidth = lineWidth_->value();
    opt.calibX = calibX_->value();
    opt.calibY = calibY_->value();
    opt.outDir = QDir::fromNativeSeparators(outDir_->text().trimmed());
    return opt;
}

std::vector<Plot::Job> PlotDialog::checkedJobs() const {
    std::vector<Plot::Job> result;
    for(int i{}; i < files_->topLevelItemCount(); ++i) {
        const QTreeWidgetItem* item = files_->topLevelItem(i);
        if(item->checkState(ColExport) != Qt::Checked) continue;
        // По id, а не указателем: файл могли перезагрузить, пока окно открыто.
        AbstractFile* file = App::project().file(item->data(ColExport, Qt::UserRole).toInt());
        if(!file) continue;
        result.push_back(Plot::Job{
            file,
            item->checkState(ColMirror) == Qt::Checked,
            item->checkState(ColNegative) == Qt::Checked,
        });
    }
    return result;
}

AbstractFile* PlotDialog::drillFile() const {
    if(drillFile_->count() == 0) return nullptr;
    return App::project().file(drillFile_->currentData().toInt());
}

bool PlotDialog::buildSheet(Plot::Sheet& sheet, const Plot::Options& opt) {
    const std::vector<Plot::Job> jobs = checkedJobs();
    if(jobs.empty()) {
        QMessageBox::information(this, windowTitle(), tr("Check at least one layer to plot."));
        return false;
    }
    status_->clear();
    try {
        // Точные булевы операции (объединение тел, вычитание отверстий,
        // штрихи путей) на больших слоях -- секунды.
        const struct BusyCursor {
            BusyCursor() { QApplication::setOverrideCursor(Qt::WaitCursor); }
            ~BusyCursor() { QApplication::restoreOverrideCursor(); }
        } busy;
        sheet = Plot::build(jobs, drillFile(), opt);
    } catch(const std::exception& ex) {
        QMessageBox::critical(this, windowTitle(), tr("Cannot build the plot geometry:\n%1").arg(QString::fromUtf8(ex.what())));
        return false;
    }
    if(!sheet.empty.isEmpty())
        status_->setText(tr("Nothing to plot in: %1").arg(sheet.empty.join(u", "_s)));
    if(sheet.plates.empty()) {
        QMessageBox::information(this, windowTitle(),
            tr("The checked layers have nothing to plot: no visible copper, holes or paths."));
        return false;
    }
    return true;
}

void PlotDialog::exportFiles() {
    const Plot::Options opt = options();
    const QDir dir{opt.outDir};
    if(opt.outDir.isEmpty() || !dir.exists()) {
        QMessageBox::warning(this, windowTitle(),
            tr("The output folder does not exist:\n%1").arg(QDir::toNativeSeparators(opt.outDir)));
        return;
    }

    Plot::Sheet sheet;
    if(!buildSheet(sheet, opt)) return;

    // Общий лист -- одним файлом с именем проекта.
    const QString combinedName = QFileInfo{App::project().name()}.completeBaseName() + u"_plot"_s;
    const std::vector<Plot::Page> pages = Plot::filePages(sheet, opt, combinedName);
    const QString ext = Plot::extension(opt.format);

    QStringList paths;
    QStringList existing;
    for(const Plot::Page& page: pages) {
        paths.push_back(dir.filePath(page.fileBase + ext));
        if(QFileInfo::exists(paths.back())) existing.push_back(page.fileBase + ext);
    }
    // Спросить ОДИН раз на все файлы.
    if(!existing.isEmpty()) {
        constexpr qsizetype shown = 10;
        QString list = existing.mid(0, shown).join(u'\n');
        if(existing.size() > shown) list += u"\n..."_s;
        if(QMessageBox::question(this, windowTitle(),
               tr("These files already exist and will be overwritten:\n%1\n\nContinue?").arg(list),
               QMessageBox::Yes | QMessageBox::No, QMessageBox::No)
            != QMessageBox::Yes)
            return;
    }

    QStringList errors;
    int written{};
    {
        const struct BusyCursor {
            BusyCursor() { QApplication::setOverrideCursor(Qt::WaitCursor); }
            ~BusyCursor() { QApplication::restoreOverrideCursor(); }
        } busy;
        for(std::size_t i{}; i < pages.size(); ++i) {
            const QString& path = paths[static_cast<qsizetype>(i)];
            if(const QString error = Plot::write(pages[i], path, opt.format); error.isEmpty())
                ++written;
            else
                errors.push_back(QDir::toNativeSeparators(path) + u": "_s + error);
        }
    }
    opt.save();

    QString text = tr("Written %1 file(s) to %2").arg(written).arg(QDir::toNativeSeparators(dir.absolutePath()));
    if(!sheet.empty.isEmpty()) {
        text += u'\n';
        text += tr("Nothing to plot in: %1").arg(sheet.empty.join(u", "_s));
    }
    status_->setText(text);
    if(!errors.isEmpty())
        QMessageBox::warning(this, windowTitle(), tr("Some files were not written:\n%1").arg(errors.join(u'\n')));
}

void PlotDialog::printPreview() {
    const Plot::Options opt = options();
    Plot::Sheet sheet;
    if(!buildSheet(sheet, opt)) return;

    // Бумага и ориентация -- только здесь, ДО показа. В paintRequested принтер
    // не трогаем: там он уже настроен пользователем (Page Setup, выбор
    // принтера), а сам сигнал приходит на каждую перерисовку.
    QPrinter printer{QPrinter::HighResolution};
    QSettings settings;
    settings.beginGroup(u"Plot"_s);
    if(const int sizeId = settings.value(u"pageSize"_s, -1).toInt();
        sizeId >= 0 && sizeId <= static_cast<int>(QPageSize::LastPageSize) && sizeId != static_cast<int>(QPageSize::Custom))
        printer.setPageSize(QPageSize{static_cast<QPageSize::PageSizeId>(sizeId)});
    {
        // Ориентация -- та, в которой раскладка ложится лучше.
        QPageLayout trial = printer.pageLayout();
        const auto fit = [&](QPageLayout::Orientation orientation) {
            trial.setOrientation(orientation);
            return Plot::fitRatio(sheet, opt, trial.paintRect(QPageLayout::Millimeter).size());
        };
        const double portrait = fit(QPageLayout::Portrait);
        const double landscape = fit(QPageLayout::Landscape);
        printer.setPageOrientation(landscape < portrait ? QPageLayout::Landscape : QPageLayout::Portrait);
    }

    QPrintPreviewDialog preview{&printer, this};
    preview.setWindowTitle(tr("Plot -- print preview"));
    QString lastWarning;
    connect(&preview, &QPrintPreviewDialog::paintRequested, &preview,
        [&sheet, &opt, &preview, &lastWarning](QPrinter* target) {
            const QString warning = Plot::print(target, sheet, opt);
            // Предупреждение -- после отрисовки и один раз на каждую новую
            // причину: preview перерисовывается на всякое изменение.
            if(!warning.isEmpty() && warning != lastWarning)
                QTimer::singleShot(0, &preview, [&preview, warning] {
                    QMessageBox::warning(&preview, preview.windowTitle(), warning);
                });
            lastWarning = warning;
        });
    preview.exec();

    settings.setValue(u"pageSize"_s, static_cast<int>(printer.pageLayout().pageSize().id()));
    settings.endGroup();
    opt.save();
}
