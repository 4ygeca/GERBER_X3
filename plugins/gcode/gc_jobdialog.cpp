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
#include "gc_jobdialog.h"

#include "app.h"
#include "gc_post.h"
#include "project.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QTableWidget>
#include <QTimer>
#include <QVBoxLayout>

namespace GCode {

JobDialog::JobDialog(Job job, QWidget* parent)
    : QDialog{parent}
    , job_{std::move(job)} {
    setWindowTitle(tr("Save as Job"));

    auto* layout = new QVBoxLayout{this};

    const QString material = App::toolHolder().materialName(App::project().material());
    auto* about = new QLabel{tr("Side: %1. Material: %2.")
                                 .arg(job_.side == Bottom ? tr("bottom") : tr("top"), material.isEmpty() ? tr("not set") : material),
        this};
    layout->addWidget(about);
    auto* hint = new QLabel{tr("Steps in the order of the job: by tool (drills, then end mills, then engravers, "
                               "each by diameter), drilling before milling, the blank cutout last. "
                               "Up and Down change the order; a cutout mark restores the default order."),
        this};
    hint->setWordWrap(true);
    layout->addWidget(hint);

    table_ = new QTableWidget{0, ColCount, this};
    table_->setHorizontalHeaderLabels({tr("T"), tr("Length"), tr("Code"), tr("Tool"), tr("Operation"), tr("Program"), tr("S"), tr("F"), tr("Cutout — last")});
    table_->horizontalHeaderItem(ColTool)->setToolTip(tr("Tool number in this job; bold -- a tool change before the step"));
    table_->horizontalHeaderItem(ColLength)->setToolTip(
        tr("Tool length: from the controller table (G43 H, measured beforehand by the tool table file) "
           "or probed at X0 Y0 in the tool change"));
    table_->horizontalHeaderItem(ColSpeed)->setToolTip(tr("Spindle speed (laser: power)"));
    table_->horizontalHeaderItem(ColFeed)->setToolTip(tr("Feed, mm/min: plunge feed for drilling, cutting feed otherwise"));
    table_->horizontalHeaderItem(ColCutout)->setToolTip(tr("Blank cutout: the very last step of the job"));
    table_->setSelectionBehavior(QAbstractItemView::SelectRows);
    table_->setSelectionMode(QAbstractItemView::SingleSelection);
    table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table_->verticalHeader()->setVisible(false);
    table_->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    table_->horizontalHeader()->setSectionResizeMode(ColProgram, QHeaderView::Stretch);
    layout->addWidget(table_);

    auto* buttons = new QHBoxLayout;
    pbUp_ = new QPushButton{QIcon::fromTheme(u"go-up"_s), tr("Up"), this};
    pbDown_ = new QPushButton{QIcon::fromTheme(u"go-down"_s), tr("Down"), this};
    auto* pbDefault = new QPushButton{tr("Default order"), this};
    buttons->addWidget(pbUp_);
    buttons->addWidget(pbDown_);
    buttons->addWidget(pbDefault);
    buttons->addStretch();
    // Предел номеров T -- здесь, где сразу видно, как он перенумерует шаги.
    // Нужен только при таблице инструментов в самом контроллере (grblHAL
    // N_TOOLS = 8); с библиотекой инструментов у отправителя -- без предела.
    auto* lblMaxTool = new QLabel{tr("Tool numbers up to T:"), this};
    sbxMaxTool_ = new QSpinBox{this};
    sbxMaxTool_->setRange(0, 999);
    sbxMaxTool_->setSpecialValueText(tr("no limit"));
    sbxMaxTool_->setValue(job_.laser ? 0 : jobMaxToolNumber());
    sbxMaxTool_->setEnabled(!job_.laser);
    const QString maxToolHint = tr("The highest tool number the controller takes: only when the controller keeps the tool table "
                                   "itself (grblHAL N_TOOLS = 8). Beyond it the numbers repeat, the tool code tells the tools apart.\n"
                                   "A sender with its own tool library (an ncSender plugin) needs no limit.");
    lblMaxTool->setToolTip(maxToolHint);
    sbxMaxTool_->setToolTip(maxToolHint);
    lblMaxTool->setBuddy(sbxMaxTool_);
    buttons->addWidget(lblMaxTool);
    buttons->addWidget(sbxMaxTool_);
    layout->addLayout(buttons);

    // Длины инструментов: по умолчанию -- не в файле (отправитель, $341);
    // либо таблица контроллера и щуп -- тогда у каждого инструмента свой
    // выбор в колонке «Длина».
    lengthsBox_ = new QGroupBox{tr("Tool lengths"), this};
    auto* lengthsLayout = new QVBoxLayout{lengthsBox_};
    auto* modeRow = new QHBoxLayout;
    auto* lblLengths = new QLabel{tr("Lengths:"), lengthsBox_};
    cbxLengths_ = new QComboBox{lengthsBox_};
    cbxLengths_->addItem(tr("Not in the file: the sender or the controller ($341) measures"), static_cast<int>(JobLengths::Sender));
    cbxLengths_->addItem(tr("Controller tool table and probe (G43 H, touch at X0 Y0)"), static_cast<int>(JobLengths::Controller));
    lblLengths->setBuddy(cbxLengths_);
    modeRow->addWidget(lblLengths);
    modeRow->addWidget(cbxLengths_, 1);
    lengthsLayout->addLayout(modeRow);
    chbxZeroProbe_ = new QCheckBox{tr("Probe Z0 at X0 Y0 at the start of the job"), lengthsBox_};
    lengthsLayout->addWidget(chbxZeroProbe_);
    chbxTableFile_ = new QCheckBox{tr("Tool table file next to the job (<name>_tool_table): measures the tools from the table into their cells"), lengthsBox_};
    lengthsLayout->addWidget(chbxTableFile_);
    layout->addWidget(lengthsBox_);

    summary_ = new QLabel{this};
    summary_->setWordWrap(true);
    layout->addWidget(summary_);
    warnings_ = new QLabel{this};
    warnings_->setWordWrap(true);
    // Жирным, а не цветом: цвет на тёмной теме читался бы плохо.
    QFont bold = warnings_->font();
    bold.setBold(true);
    warnings_->setFont(bold);
    layout->addWidget(warnings_);

    auto* buttonBox = new QDialogButtonBox{QDialogButtonBox::Save | QDialogButtonBox::Cancel, this};
    // Дальше -- выбор имени файла.
    pbSave_ = buttonBox->button(QDialogButtonBox::Save);
    pbSave_->setText(tr("Save..."));
    layout->addWidget(buttonBox);

    connect(pbUp_, &QPushButton::clicked, this, [this] { moveStep(-1); });
    connect(pbDown_, &QPushButton::clicked, this, [this] { moveStep(+1); });
    connect(pbDefault, &QPushButton::clicked, this, &JobDialog::defaultOrder);
    connect(sbxMaxTool_, &QSpinBox::valueChanged, this, [this](int value) {
        job_.maxToolNumber = value;
        setJobMaxToolNumber(value);
        const int32_t current = rowStep(currentRow()); // выделение -- через пересборку таблицы
        fill();
        if(current >= 0) selectStep(current);
    });
    connect(cbxLengths_, &QComboBox::currentIndexChanged, this, [this] {
        job_.lengths = static_cast<JobLengths>(cbxLengths_->currentData().toInt());
        job_.saveLengthSettings();
        const int32_t current = rowStep(currentRow());
        fill();
        if(current >= 0) selectStep(current);
    });
    connect(chbxZeroProbe_, &QCheckBox::toggled, this, [this](bool checked) {
        job_.zeroProbe = checked;
        job_.saveLengthSettings();
        fill();
    });
    connect(chbxTableFile_, &QCheckBox::toggled, this, [this](bool checked) {
        job_.toolTableFile = checked;
        job_.saveLengthSettings();
    });
    connect(table_, &QTableWidget::itemChanged, this, &JobDialog::cutoutToggled);
    connect(table_, &QTableWidget::itemSelectionChanged, this, &JobDialog::updateButtons);
    connect(buttonBox, &QDialogButtonBox::accepted, this, &JobDialog::accept);
    connect(buttonBox, &QDialogButtonBox::rejected, this, &JobDialog::reject);

    fill();
    if(!job_.steps.empty()) selectStep(job_.steps.front().id);
    resize(std::max(sizeHint().width(), 900), 560);
}

JobDialog::~JobDialog() = default;

void JobDialog::fill() {
    const JobPlan plan = job_.plan();
    // Сигналы таблицы -- молчат: иначе флажок новой строки звал бы
    // cutoutToggled посреди пересборки.
    const QSignalBlocker blocker{table_};
    // Списки длины -- виджеты ячеек: setItem их не снимает, а смена у строки
    // после перестановки бывает уже другая.
    for(int row{}; row < table_->rowCount(); ++row) table_->removeCellWidget(row, ColLength);
    table_->setRowCount(static_cast<int>(job_.steps.size()));
    for(int row{}; std::cmp_less(row, job_.steps.size()); ++row) {
        const JobStep& step = job_.steps[row];
        auto cell = [this, row](int column, const QString& text) {
            auto* item = new QTableWidgetItem{text};
            item->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
            table_->setItem(row, column, item);
            return item;
        };
        auto* tool = cell(ColTool, u"T%1"_s.arg(plan.toolNumbers[row]));
        tool->setData(Qt::UserRole, step.id);
        if(job_.laser)
            tool->setToolTip(tr("Laser job: no tool changes"));
        else if(plan.toolChange[row]) {
            QFont font = tool->font();
            font.setBold(true);
            tool->setFont(font);
            tool->setToolTip(tr("Tool change before this step"));
        } else
            tool->setToolTip(tr("The same tool as in the previous step: no tool change"));
        cell(ColLength, {});
        if(job_.controllerLengths()) {
            const bool fromTable = job_.tableTools.contains(step.identity);
            if(plan.toolChange[row]) {
                // Выбор -- у смены инструмента: длина у инструмента одна на
                // все его шаги, и запоминается она за ним.
                auto* combo = new QComboBox{table_};
                combo->addItem(tr("Table (G43 H)"));
                combo->addItem(tr("Probe at X0 Y0"));
                combo->setCurrentIndex(fromTable ? 0 : 1);
                combo->setToolTip(tr("Table: the length measured beforehand by the tool table file, G43 H%1.\n"
                                     "Probe: the tool touches the probe at X0 Y0 in this tool change.")
                                      .arg(plan.toolNumbers[row]));
                const int32_t id = step.id;
                connect(combo, &QComboBox::currentIndexChanged, this, [this, id](int index) { setFromTable(id, index == 0); });
                table_->setCellWidget(row, ColLength, combo);
            } else
                cell(ColLength, fromTable ? tr("Table (G43 H)") : tr("Probe at X0 Y0"))
                    ->setToolTip(tr("The same tool as in the previous step: no tool change, the length stays"));
        }
        cell(ColCode, step.code);
        cell(ColName, step.toolName);
        cell(ColOperation, Tool::operationName(step.operation));
        cell(ColProgram, step.programName);
        cell(ColSpeed, QString::number(step.spindleSpeed))->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
        cell(ColFeed, QString::number(step.feed))->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
        auto* cutout = cell(ColCutout, {});
        cutout->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsUserCheckable);
        cutout->setCheckState(step.cutout ? Qt::Checked : Qt::Unchecked);
        cutout->setToolTip(step.autoCutout
                ? tr("Found automatically: a profile outside the board outline")
                : tr("Mark the blank cutout: it goes last"));
    }

    if(job_.laser)
        summary_->setText(tr("Laser job: one laser, no tool changes; another laser spot is only a comment in the file."));
    else
        summary_->setText(tr("Tools: %1, tool changes: %2.").arg(plan.tools).arg(plan.toolChanges));

    updateLengths();
    QStringList warnings = job_.lengthProblems();
    // Писать нельзя, пока длины не сходятся: Z0 встал бы не там.
    pbSave_->setEnabled(warnings.isEmpty());
    if(plan.wrapped)
        warnings << tr("%1 tools, but the controller takes only T1..T%2: tool numbers repeat (T%3 is T1 again). "
                       "The tool code in the file tells the tools apart.")
                        .arg(plan.tools)
                        .arg(job_.maxToolNumber)
                        .arg(job_.maxToolNumber + 1);
    // Число ячеек таблицы контроллера -- предел номеров T: без него файл
    // замера не знает, что обнулять, а номера сверх ячеек не пойдут по кругу.
    if(job_.controllerLengths() && job_.maxToolNumber == 0)
        warnings << tr("The size of the controller's tool table is not set: set \"Tool numbers up to T\" to its cells "
                       "(grblHAL N_TOOLS, 8) -- up to it the tool table file zeroes the unused cells.");
    if(!job_.skipped.isEmpty())
        warnings << tr("Skipped, no toolpaths: %1").arg(job_.skipped.join(u", "_s));
    warnings_->setText(warnings.join(u'\n'));
    warnings_->setVisible(!warnings.isEmpty());

    updateButtons();
}

void JobDialog::selectStep(int32_t id) {
    for(int row{}; row < table_->rowCount(); ++row)
        if(const auto* item = table_->item(row, ColTool); item && item->data(Qt::UserRole).toInt() == id) {
            table_->setCurrentCell(row, ColTool);
            table_->scrollToItem(item);
            break;
        }
    updateButtons();
}

int JobDialog::currentRow() const {
    const auto rows = table_->selectionModel()->selectedRows();
    return rows.isEmpty() ? -1 : rows.front().row();
}

int32_t JobDialog::rowStep(int row) const {
    const auto* item = row < 0 ? nullptr : table_->item(row, ColTool);
    return item ? item->data(Qt::UserRole).toInt() : -1;
}

int JobDialog::stepIndex(int32_t id) const {
    const auto it = r::find(job_.steps, id, &JobStep::id);
    return it == job_.steps.end() ? -1 : static_cast<int>(it - job_.steps.begin());
}

void JobDialog::moveStep(int delta) {
    const int32_t id = rowStep(currentRow());
    const int index = stepIndex(id);
    const int to = index + delta;
    if(index < 0 || to < 0 || std::cmp_greater_equal(to, job_.steps.size())) return;
    std::swap(job_.steps[index], job_.steps[to]);
    fill();
    selectStep(id);
}

void JobDialog::defaultOrder() {
    const int32_t id = rowStep(currentRow());
    job_.applyDefaultOrder();
    fill();
    selectStep(id);
}

void JobDialog::cutoutToggled(QTableWidgetItem* item) {
    if(!item || item->column() != ColCutout) return;
    const int index = stepIndex(rowStep(item->row()));
    if(index < 0) return;
    JobStep& step = job_.steps[index];
    step.cutout = item->checkState() == Qt::Checked;
    const int32_t id = step.id;
    // Порядок -- сразу: нажатый следом «Сохранить» берёт уже его. Таблица --
    // после возврата в цикл событий: сигнал идёт из самого флажка, а
    // пересборка этот элемент удаляет. До неё строки и шаги не совпадают --
    // шаг строки поэтому всегда по id (rowStep), а не по номеру строки.
    job_.applyDefaultOrder();
    QTimer::singleShot(0, this, [this, id] {
        fill();
        selectStep(id);
    });
}

void JobDialog::updateLengths() {
    const bool can = !job_.laser && App::gcSettings().post().toolLengths();
    const bool controller = job_.controllerLengths();
    {
        const QSignalBlocker blocker{cbxLengths_};
        cbxLengths_->setCurrentIndex(controller ? 1 : 0);
    }
    cbxLengths_->setEnabled(can);
    if(can)
        cbxLengths_->setToolTip(tr("Not in the file: the tool change and the touch-off are the sender's or the controller's "
                                   "(grblHAL $341 = 1).\n"
                                   "Controller tool table and probe (grblHAL $341 = 0, M6 is a pause): every tool takes its "
                                   "length from the table (G43 H) or touches the probe at X0 Y0 in its tool change -- "
                                   "the column \"Length\"."));
    else if(job_.laser)
        cbxLengths_->setToolTip(tr("Laser job: no tool changes"));
    else
        cbxLengths_->setToolTip(tr("The post \"%1\" cannot write tool lengths (grblHAL can).").arg(App::gcSettings().post().name()));
    table_->setColumnHidden(ColLength, !controller);

    // Замер Z0 в начале -- выбор лишь у первого инструмента «из таблицы»: у
    // «щупа» его касание Z0 и задаёт.
    const bool firstFromTable = !job_.steps.empty() && job_.tableTools.contains(job_.steps.front().identity);
    {
        const QSignalBlocker blocker{chbxZeroProbe_};
        chbxZeroProbe_->setChecked(controller && (firstFromTable ? job_.zeroProbe : true));
    }
    chbxZeroProbe_->setEnabled(controller && firstFromTable);
    chbxZeroProbe_->setToolTip(!controller ? QString{}
            : firstFromTable
            ? tr("The first tool goes to X0 Y0, touches the probe and sets Z0 of the work with its table length; "
                 "then a pause to take the probe off. Off: Z0 is set already.")
            : tr("The first tool is probed: its touch at X0 Y0 sets Z0 of the work anyway."));

    const bool table = job_.usesTable();
    {
        const QSignalBlocker blocker{chbxTableFile_};
        chbxTableFile_->setChecked(table && job_.toolTableFile);
    }
    chbxTableFile_->setEnabled(table);
    chbxTableFile_->setToolTip(table
            ? tr("A separate file: every tool from the table touches the probe at one point and its length goes "
                 "into its cell (G10 L10); the cells up to T%1 this job does not use are zeroed (G10 L1).\n"
                 "Run it once for a series of boards, then only the jobs.")
                  .arg(job_.maxToolNumber > 0 ? job_.maxToolNumber : 1)
            : tr("No tool takes its length from the table"));
}

void JobDialog::setFromTable(int32_t id, bool fromTable) {
    const int index = stepIndex(id);
    if(index < 0) return;
    const QString identity = job_.steps[index].identity;
    if(fromTable)
        job_.tableTools.insert(identity);
    else
        job_.tableTools.erase(identity);
    job_.saveLengthSettings();
    // Таблица -- после возврата в цикл событий: сигнал идёт из самого списка,
    // а пересборка его удаляет.
    QTimer::singleShot(0, this, [this, id] {
        fill();
        selectStep(id);
    });
}

void JobDialog::updateButtons() {
    const int row = currentRow();
    pbUp_->setEnabled(row > 0);
    pbDown_->setEnabled(row >= 0 && std::cmp_less(row + 1, job_.steps.size()));
}

} // namespace GCode

#include "moc_gc_jobdialog.cpp"
