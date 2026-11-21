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
#include "project.h"

#include <QDialogButtonBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QPushButton>
#include <QSignalBlocker>
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
    table_->setHorizontalHeaderLabels({tr("T"), tr("Code"), tr("Tool"), tr("Operation"), tr("Program"), tr("S"), tr("F"), tr("Cutout — last")});
    table_->horizontalHeaderItem(ColTool)->setToolTip(tr("Tool number in this job; bold -- a tool change before the step"));
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
    layout->addLayout(buttons);

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
    buttonBox->button(QDialogButtonBox::Save)->setText(tr("Save..."));
    layout->addWidget(buttonBox);

    connect(pbUp_, &QPushButton::clicked, this, [this] { moveStep(-1); });
    connect(pbDown_, &QPushButton::clicked, this, [this] { moveStep(+1); });
    connect(pbDefault, &QPushButton::clicked, this, &JobDialog::defaultOrder);
    connect(table_, &QTableWidget::itemChanged, this, &JobDialog::cutoutToggled);
    connect(table_, &QTableWidget::itemSelectionChanged, this, &JobDialog::updateButtons);
    connect(buttonBox, &QDialogButtonBox::accepted, this, &JobDialog::accept);
    connect(buttonBox, &QDialogButtonBox::rejected, this, &JobDialog::reject);

    fill();
    if(!job_.steps.empty()) selectStep(job_.steps.front().id);
    resize(std::max(sizeHint().width(), 860), 460);
}

JobDialog::~JobDialog() = default;

void JobDialog::fill() {
    const JobPlan plan = job_.plan();
    // Сигналы таблицы -- молчат: иначе флажок новой строки звал бы
    // cutoutToggled посреди пересборки.
    const QSignalBlocker blocker{table_};
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

    QStringList warnings;
    if(plan.wrapped)
        warnings << tr("%1 tools, but the controller takes only T1..T%2: tool numbers repeat (T%3 is T1 again). "
                       "The tool code in the file tells the tools apart.")
                        .arg(plan.tools)
                        .arg(job_.maxToolNumber)
                        .arg(job_.maxToolNumber + 1);
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

void JobDialog::updateButtons() {
    const int row = currentRow();
    pbUp_->setEnabled(row > 0);
    pbDown_->setEnabled(row >= 0 && std::cmp_less(row + 1, job_.steps.size()));
}

} // namespace GCode

#include "moc_gc_jobdialog.cpp"
