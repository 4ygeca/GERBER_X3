// This is an open source non-commercial project. Dear PVS-Studio, please check it.
// PVS-Studio Static Code Analyzer for C, C++, C#, and Java: https://pvs-studio.com
/********************************************************************************
 * Author    :  Damir Bakiev                                                    *
 * Version   :  na                                                              *
 * Date      :  XXXXX XX, 2025                                                  *
 * Website   :  na                                                              *
 * Copyright :  Damir Bakiev 2016-2026                                          *
 * License   :                                                                  *
 * Use, modification & distribution is subject to Boost Software License Ver 1. *
 * http://www.boost.org/LICENSE_1_0.txt                                         *
 ********************************************************************************/
#include "tool_selectorform.h"
#include "project.h"
#include "settings.h"
#include "tool_database.h"
#include "tool_editdialog.h"
#include <QDebug>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QtWidgets>
#include <app.h>

// Ключи json формы рядом с полями снимка (Tool::read их не знает и пропускает).
static const auto selectorMaterialKey = u"selectorMaterial"_s;
static const auto selectorEditedKey = u"selectorEdited"_s;

ToolSelectorForm::ToolSelectorForm(QWidget* parent)
    : QWidget{parent}
    , counter{static_cast<int>(parent->findChildren<ToolSelectorForm*>().count())}
    , toolFileName_{App::settingsPath() + u'/' + parent->objectName() + QString::number(counter) + u".json"_s} {
    setupUi(this);
    readTool();
    updateMaterials();
    // Материал проекта сменился (свойства G-кода, открытие и закрытие проекта):
    // форма, что за ним следует, берёт снимок под новый материал; правленный
    // снимок остаётся как правлен.
    if(auto* project = App::project.ptr())
        connect(project, &Project::materialChanged, this, [this] {
            if(materialChoice_.isEmpty() && !edited_) resolve();
            updateMaterials(); // имя материала проекта в первой строке
        });
    // activated, а не currentIndexChanged: выбор того же материала -- тоже
    // выбор, он снимает местную правку.
    connect(cbxMaterial, &QComboBox::activated, this, [this](int index) {
        materialChoice_ = cbxMaterial->itemData(index).toString();
        resolve();
    });
    label_->setStyleSheet(tool_.id() < Tool::ID::Folder ? u"QLabel { color: red }"_s : QString{});
}

ToolSelectorForm::~ToolSelectorForm() {
    writeTool();
}

void ToolSelectorForm::setTool(const Tool& tool) {
    const Tool* found = App::toolHolder().findTool(tool);
    base_ = found ? *found : tool;
    setSnapshot(tool);
    label_->setStyleSheet(tool.id() < Tool::ID::Folder ? u"QLabel { color: red }"_s : QString{});
}

const Tool& ToolSelectorForm::tool() const { return tool_; }

void ToolSelectorForm::showEvent(QShowEvent* event) {
    // Список материалов могли править в базе, пока форма была скрыта.
    updateMaterials();
    QWidget::showEvent(event);
}

void ToolSelectorForm::on_pbSelect_clicked() {
    ToolDatabase tdb{
        this,
        std::array{Tool::EndMill, Tool::Engraver, Tool::Laser},
    };
    // Открыть базу на текущем инструменте: по uid, а у снимка без uid -- по id,
    // если под ним тот же инструмент (id локальны для tools.json и после
    // удаления переиспользуются, а инструмент формы мог прийти из проекта).
    if(const Tool* found = App::toolHolder().findTool(tool_))
        tdb.setCurrentTool(found->id());
    if(tdb.exec()) {
        base_ = tdb.tool();
        resolve();
        label_->setStyleSheet(tool_.id() < Tool::ID::Folder ? u"QLabel { color: red }"_s : QString{});
    }
    updateMaterials(); // материалы могли править в окне базы
}

void ToolSelectorForm::on_pbEdit_clicked() {
    ToolEditDialog d{this};
    d.setOperation(operation());
    d.setTool(tool_);
    if(d.exec()) {
        // ToolEditDialog по Ok обнуляет id, но правка тут локальная -- инструмент всё тот же
        // из базы, и по id «Select» откроет базу на нём. Сам id инструмента формы нигде
        // не ищут (УП при сохранении группируются по hash2(), без id).
        Tool t = d.tool();
        t.setId(tool_.id());
        setSnapshot(t);
    }
}

Tool::Operation ToolSelectorForm::operation() const { return Tool::programOperation(base_.type(), false); }

QString ToolSelectorForm::effectiveMaterial() const {
    if(!materialChoice_.isEmpty()) return materialChoice_;
    return App::project.ptr() ? App::project().material() : QString{};
}

const Tool& ToolSelectorForm::source() const {
    // Каким источник сейчас лежит в базе: его могли править после выбора.
    const Tool* found = base_.uid().isEmpty() ? nullptr : App::toolHolder().findTool(base_);
    return found ? *found : base_;
}

Tool ToolSelectorForm::resolved() const { return App::toolHolder().resolve(source(), operation(), effectiveMaterial()); }

void ToolSelectorForm::resolve() {
    edited_ = false;
    tool_ = resolved();
    updateForm();
}

void ToolSelectorForm::setSnapshot(const Tool& tool) {
    const Tool fromBase = resolved();
    // Снимок без uid (старый проект, старый json формы) о новых полях не знает:
    // сверяется только тем, что в нём есть, а новые берёт из базы.
    Tool probe{tool};
    if(probe.uid().isEmpty()) {
        probe.setUid(fromBase.uid());
        probe.setCode(fromBase.code());
        probe.setAutoCode(fromBase.autoCode());
        probe.setDrillCycle(fromBase.drillCycle());
        probe.setDwell(fromBase.dwell());
        probe.setRetractToR(fromBase.retractToR());
        probe.setLaserDynamic(fromBase.laserDynamic());
        probe.setMaterial(fromBase.material());
    }
    // Автоматический код снимка мог быть посчитан прежним форматом кода
    // (D0.8x9 при нынешнем D08L90): это не правка -- код следует за базой.
    if(probe.autoCode() && fromBase.autoCode())
        probe.setCode(fromBase.code());
    edited_ = probe.hash() != fromBase.hash();
    tool_ = edited_ ? tool : fromBase;
    updateForm();
}

void ToolSelectorForm::updateForm() {
    lblPixmap->setPixmap(tool_.icon().pixmap({22, 22}));
    // Правленный снимок -- звёздочкой и курсивом, как правка строки в форме сверловки.
    lblName->setText(edited_ ? u"%1 *"_s.arg(tool_.name()) : tool_.name());
    QFont font = lblName->font();
    font.setItalic(edited_);
    lblName->setFont(font);

    // Откуда режимы снимка -- в подсказке: набор материала, «любой материал»
    // или собственные значения инструмента.
    QString origin;
    if(edited_)
        origin = tr("Edited for this toolpath only (the tool database is not changed)");
    else if(!tool_.material().isEmpty() || source().preset(operation(), {}))
        origin = tr("Cutting data: %1").arg(App::toolHolder().presetName({.operation = operation(), .material = tool_.material()}));
    else
        origin = tr("Cutting data: tool defaults");
    setToolTip(tool_.note().isEmpty() ? origin : tool_.note() + u"\n\n"_s + origin);
    cbxMaterial->setToolTip(origin);
    emit updateName();
}

void ToolSelectorForm::updateMaterials() {
    const QSignalBlocker blocker{cbxMaterial};
    cbxMaterial->clear();
    const QString project = App::project.ptr() ? App::project().material() : QString{};
    cbxMaterial->addItem(tr("Project material (%1)").arg(project.isEmpty() ? tr("not set") : App::toolHolder().materialName(project)), QString{});
    for(const auto& material: App::toolHolder().materials())
        cbxMaterial->addItem(material.name, material.uid);
    int index = materialChoice_.isEmpty() ? 0 : cbxMaterial->findData(materialChoice_);
    if(index < 0) { // выбранный здесь материал удалили из списка
        cbxMaterial->addItem(App::toolHolder().materialName(materialChoice_), materialChoice_);
        index = cbxMaterial->count() - 1;
    }
    cbxMaterial->setCurrentIndex(index);
}

void ToolSelectorForm::readTool() {
    QFile file{toolFileName_};
    QJsonObject json;
    if(file.open(QIODevice::ReadOnly))
        json = QJsonDocument::fromJson(file.readAll()).object();
    else
        qWarning("Couldn't open tools file.");
    Tool stored;
    stored.read(json);
    // value(), не []: [] у неконстантного объекта вставил бы ключ, и contains()
    // ниже принял бы json прежней версии за новый.
    materialChoice_ = json.value(selectorMaterialKey).toString();
    // Источник -- инструмент базы с той же личностью, нет его -- сам снимок.
    const Tool* found = App::toolHolder().findTool(stored);
    base_ = found ? *found : stored;
    if(json.value(selectorEditedKey).toBool()) { // местная правка переживает перезапуск
        tool_ = stored;
        edited_ = true;
        updateForm();
    } else if(json.contains(selectorEditedKey)) {
        resolve(); // снимок по базе -- снова по базе, с её нынешними значениями
    } else {
        setSnapshot(stored); // json прежней версии: правка или нет -- по сверке с базой
    }
}

void ToolSelectorForm::writeTool() const {
    QFile file{toolFileName_};
    if(file.open(QIODevice::WriteOnly)) {
        QJsonObject json;
        tool_.write(json);
        json[selectorMaterialKey] = materialChoice_;
        json[selectorEditedKey] = edited_;
        file.write(QJsonDocument(json).toJson());
    } else {
        qWarning("Couldn't open tools file.");
    }
}

QLabel* ToolSelectorForm::label() const {
    return label_;
}

void ToolSelectorForm::setupUi(QWidget* ToolSelectorForm) {
    if(ToolSelectorForm->objectName().isEmpty())
        ToolSelectorForm->setObjectName(u"ToolSelectorForm"_s);
    ToolSelectorForm->resize(236, 180);

    auto gridLayout = new QGridLayout{ToolSelectorForm};
    gridLayout->setObjectName(u"gridLayout"_s);
    gridLayout->setContentsMargins(0, 0, 0, 0);

    // Метки -- шириной под самую длинную из «Tool:», «Material:» и «Depth:»
    // формы глубины: столбцы меток формы УП стоят ровно (см. DepthForm).
    QFontMetrics fm(font());
    const int labelWidth = std::max({
        fm.horizontalAdvance(QCoreApplication::translate("ToolSelectorForm", "Tool:", nullptr)),
        fm.horizontalAdvance(QCoreApplication::translate("ToolSelectorForm", "Material:", nullptr)),
        fm.horizontalAdvance(QCoreApplication::translate("DepthForm", "Depth:", nullptr)),
    });

    {
        label_ = new QLabel{ToolSelectorForm};
        label_->setObjectName(u"label_"_s);

        {
            QSizePolicy sizePolicy{QSizePolicy::Fixed, QSizePolicy::Preferred};
            sizePolicy.setHorizontalStretch(0);
            sizePolicy.setVerticalStretch(0);
            sizePolicy.setHeightForWidth(label_->sizePolicy().hasHeightForWidth());
            label_->setSizePolicy(sizePolicy);
        }

        label_->setMinimumWidth(labelWidth);
        label_->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        gridLayout->addWidget(label_, 0, 0, 1, 1);
    }

    {
        auto horizontalLayout = new QHBoxLayout();
        horizontalLayout->setObjectName(u"horizontalLayout"_s);

        lblPixmap = new QLabel{ToolSelectorForm};
        lblPixmap->setObjectName(u"lblPixmap"_s);
        horizontalLayout->addWidget(lblPixmap);

        lblName = new QLabel{ToolSelectorForm};
        lblName->setObjectName(u"lblName"_s);
        horizontalLayout->addWidget(lblName);

        horizontalLayout->setStretch(1, 1);
        gridLayout->addLayout(horizontalLayout, 0, 1, 1, 2);
    }
    {
        pbSelect = new QPushButton{ToolSelectorForm};
        pbSelect->setObjectName(u"pbSelect"_s);
        pbSelect->setIcon(QIcon::fromTheme(u"view-form"_s));
        gridLayout->addWidget(pbSelect, 1, 1, 1, 1);
    }

    {
        pbEdit = new QPushButton{ToolSelectorForm};
        pbEdit->setObjectName(u"pbEdit"_s);
        pbEdit->setIcon(QIcon::fromTheme(u"document-edit"_s));
        gridLayout->addWidget(pbEdit, 1, 2, 1, 1);
    }

    {
        lblMaterial = new QLabel{ToolSelectorForm};
        lblMaterial->setObjectName(u"lblMaterial"_s);
        lblMaterial->setSizePolicy(label_->sizePolicy());
        lblMaterial->setMinimumWidth(labelWidth);
        lblMaterial->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        gridLayout->addWidget(lblMaterial, 2, 0, 1, 1);

        cbxMaterial = new QComboBox{ToolSelectorForm};
        cbxMaterial->setObjectName(u"cbxMaterial"_s);
        cbxMaterial->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
        cbxMaterial->setMinimumContentsLength(8);
        gridLayout->addWidget(cbxMaterial, 2, 1, 1, 2);
    }

    retranslateUi(ToolSelectorForm);

    QMetaObject::connectSlotsByName(ToolSelectorForm);
}

void ToolSelectorForm::retranslateUi(QWidget* ToolSelectorForm) {
    ToolSelectorForm->setWindowTitle(QCoreApplication::translate("ToolSelectorForm", "Form", nullptr));
    if(counter > 1)
        label_->setText(QCoreApplication::translate("ToolSelectorForm", "Tool %1:", nullptr).arg(counter));
    else
        label_->setText(QCoreApplication::translate("ToolSelectorForm", "Tool:", nullptr));
    pbEdit->setText(QCoreApplication::translate("ToolSelectorForm", "Edit", nullptr));
    pbSelect->setText(QCoreApplication::translate("ToolSelectorForm", "Select", nullptr));
    lblMaterial->setText(QCoreApplication::translate("ToolSelectorForm", "Material:", nullptr));
}

#include "moc_tool_selectorform.cpp"
