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
#include "tool_codeformatdialog.h"
#include "app.h"

#include <QDialogButtonBox>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QRegularExpressionValidator>
#include <QVBoxLayout>

namespace {

// Образец геометрии для просмотра кода.
struct CodeFormatSample {
    Tool::Type type;
    double diameter;
    double length;
    double angle;
};

// Образцы -- из договорённости о кодах: на них видно и ведущий 0 диаметра, и
// длину в десятых мм, и необязательную часть. Длина 0.1 -- «не задана»: так
// помечено сломанное сверло, пока ждёт замену.
constexpr CodeFormatSample codeFormatSamples[]{
    {Tool::Drill,      0.35, 5,   120},
    {Tool::Drill,      0.5,  10,  120},
    {Tool::Drill,      0.5,  0.1, 120},
    {Tool::EndMill,    0.8,  9,   0  },
    {Tool::EndMill,    1.96, 8,   0  },
    {Tool::EndMill,    1,    0,   0  },
    {Tool::Engraver,   0.2,  0,   30 },
    {Tool::Engraver,   0.1,  10,  60 },
    {Tool::Laser,      0.05, 0,   0  },
    {Tool::Laser,      0.1,  0,   0  },
    {Tool::ThreadMill, 1,    6,   0  },
    {Tool::ThreadMill, 2.5,  12,  0  },
};

// Геометрия образца: «Ø0.35 L5», у гравёра и угол. Пробелы неразрывные --
// строка образцов переносится только между ними.
QString codeFormatSampleText(const CodeFormatSample& sample) {
    QString text = u"Ø"_s + QString::number(sample.diameter);
    if(sample.type == Tool::Engraver)
        text += u'\u00A0' + QString::number(sample.angle) + u'°';
    if(sample.length > 0.0)
        text += u"\u00A0L"_s + QString::number(sample.length);
    return text;
}

// Имена типов -- те же строки, что в списке типов окна инструмента.
QString codeFormatTypeName(Tool::Type type) {
    switch(type) {
    case Tool::Drill     : return QCoreApplication::translate("ToolEditForm", "Drill");
    case Tool::EndMill   : return QCoreApplication::translate("ToolEditForm", "End Mill");
    case Tool::Engraver  : return QCoreApplication::translate("ToolEditForm", "Engraver");
    case Tool::Laser     : return QCoreApplication::translate("ToolEditForm", "Laser");
    case Tool::ThreadMill: return QCoreApplication::translate("ToolEditForm", "Thread Mill");
    default              : return {};
    }
}

} // namespace

CodeFormatDialog::CodeFormatDialog(QWidget* parent)
    : QDialog{parent} {
    setWindowTitle(tr("Code format"));
    setMinimumWidth(fontMetrics().averageCharWidth() * 72);

    auto* layout = new QVBoxLayout{this};

    auto* help = new QLabel{tr("Placeholders: {D} -- the diameter without the point: as many decimals as the diameter needs, "
                               "at least one, with a leading 0 below 1 mm (0.35 -> 035, 0.8 -> 08, 1 -> 10); "
                               "{L} -- the working length in tenths of a millimetre (5 -> 50, 10 -> 100); "
                               "{d}, {l} -- the diameter and the length with the point (0.8, 9); {A} -- the angle. "
                               "A part in [ ] is written only when all its values are set: the working length is over 1 mm, "
                               "the angle is over 0. Letters, digits, '.', '_' and '-' are written as they are."),
        this};
    help->setWordWrap(true);
    layout->addWidget(help);

    // Коды образцов -- серым: это подсказка, не поле.
    QPalette grey = palette();
    grey.setColor(QPalette::WindowText, grey.color(QPalette::PlaceholderText));

    auto* form = new QFormLayout;
    form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    const Tool::CodeFormat& current = App::toolHolder().codeFormat();
    const Tool::CodeFormat defaults;
    for(Tool::Type type: {Tool::Drill, Tool::EndMill, Tool::Engraver, Tool::Laser, Tool::ThreadMill}) {
        auto* edit = new QLineEdit{current.pattern(type), this};
        edit->setObjectName(u"pattern%1"_s.arg(+type));
        // Длину не ограничиваем: код и так не длиннее Tool::MaxCodeLength (это
        // видно в образцах), а setText обрезал бы шаблон, правленный в tools.json.
        edit->setValidator(new QRegularExpressionValidator{QRegularExpression{Tool::codeFormatPattern()}, edit});
        // Пустое поле -- шаблон по умолчанию, он и виден в пустом поле.
        edit->setPlaceholderText(defaults.pattern(type));
        auto* preview = new QLabel{this};
        preview->setObjectName(u"preview%1"_s.arg(+type));
        preview->setPalette(grey);
        preview->setWordWrap(true);
        auto* field = new QVBoxLayout;
        field->setSpacing(2);
        field->addWidget(edit);
        field->addWidget(preview);
        form->addRow(codeFormatTypeName(type) + u':', field);
        rows_.push_back({type, edit, preview});
        connect(edit, &QLineEdit::textChanged, this, &CodeFormatDialog::updatePreview);
    }
    layout->addLayout(form);

    auto* note = new QLabel{tr("OK recomputes the automatic codes of the tools; codes set by hand stay as they are. "
                               "The code goes into the G-code, and the sender finds the tool by it: "
                               "the code patterns of the sender plugin must follow this format."),
        this};
    note->setWordWrap(true);
    layout->addWidget(note);

    auto* buttonBox = new QDialogButtonBox{QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this};
    QPushButton* pbDefaults = buttonBox->addButton(tr("Defaults"), QDialogButtonBox::ResetRole);
    pbOk_ = buttonBox->button(QDialogButtonBox::Ok);
    layout->addWidget(buttonBox);

    connect(pbDefaults, &QPushButton::clicked, this, &CodeFormatDialog::restoreDefaults);
    connect(buttonBox, &QDialogButtonBox::accepted, this, &CodeFormatDialog::accept);
    connect(buttonBox, &QDialogButtonBox::rejected, this, &CodeFormatDialog::reject);

    updatePreview();
}

Tool::CodeFormat CodeFormatDialog::format() const {
    Tool::CodeFormat result; // шаблоны по умолчанию
    for(const Row& row: rows_)
        if(!row.edit->text().isEmpty())
            result.setPattern(row.type, row.edit->text());
    return result;
}

void CodeFormatDialog::accept() {
    // Enter в поле с незаконченным шаблоном.
    if(!pbOk_->isEnabled())
        return;
    App::toolHolder().setCodeFormat(format());
    QDialog::accept();
}

void CodeFormatDialog::updatePreview() {
    const Tool::CodeFormat format = this->format();
    bool acceptable = true;
    for(const Row& row: rows_) {
        // Валидатор пропускает и недописанное («D{D}[L{L}» -- скобку ещё не
        // закрыли): такой шаблон в базу не уходит.
        if(!row.edit->hasAcceptableInput()) {
            acceptable = false;
            row.preview->setText(tr("The pattern is not finished: a [ or a { is not closed"));
            continue;
        }
        // Код -- тем же Tool::codeAuto, что и у инструментов базы, с подменой
        // пустого кода шаблоном по умолчанию.
        QStringList samples;
        for(const CodeFormatSample& sample: codeFormatSamples) {
            if(sample.type != row.type)
                continue;
            Tool tool{sample.diameter};
            tool.setType(sample.type);
            tool.setLenght(sample.length);
            tool.setAngle(sample.angle);
            samples.push_back(codeFormatSampleText(sample) + u"\u00A0→\u00A0"_s + tool.codeAuto(format));
        }
        row.preview->setText(samples.join(u",  "_s));
    }
    pbOk_->setEnabled(acceptable);
}

void CodeFormatDialog::restoreDefaults() {
    const Tool::CodeFormat defaults;
    for(const Row& row: rows_)
        row.edit->setText(defaults.pattern(row.type));
}

#include "moc_tool_codeformatdialog.cpp"
