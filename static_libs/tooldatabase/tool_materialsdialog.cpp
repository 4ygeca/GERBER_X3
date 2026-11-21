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
#include "tool_materialsdialog.h"
#include "app.h"

#include <QDialogButtonBox>
#include <QHBoxLayout>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QVBoxLayout>

MaterialsDialog::MaterialsDialog(QWidget* parent)
    : QDialog{parent} {
    setWindowTitle(tr("Materials"));

    auto* layout = new QVBoxLayout{this};

    // Имя правится по двойному щелчку или F2; uid -- в данных строки, он
    // постоянный: на него ссылаются наборы режимов и проекты.
    list_ = new QListWidget{this};
    for(const auto& material: App::toolHolder().materials()) {
        auto* item = new QListWidgetItem{material.name, list_};
        item->setData(Qt::UserRole, material.uid);
        item->setFlags(item->flags() | Qt::ItemIsEditable);
    }
    layout->addWidget(list_);

    auto* buttons = new QHBoxLayout;
    auto* pbAdd = new QPushButton{QIcon::fromTheme(u"list-add"_s), tr("Add"), this};
    pbRename_ = new QPushButton{QIcon::fromTheme(u"edit-rename"_s), tr("Rename"), this};
    pbDelete_ = new QPushButton{QIcon::fromTheme(u"edit-delete"_s), tr("Delete"), this};
    buttons->addWidget(pbAdd);
    buttons->addWidget(pbRename_);
    buttons->addWidget(pbDelete_);
    buttons->addStretch();
    layout->addLayout(buttons);

    auto* buttonBox = new QDialogButtonBox{QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this};
    layout->addWidget(buttonBox);

    connect(pbAdd, &QPushButton::clicked, this, &MaterialsDialog::addMaterial);
    connect(pbRename_, &QPushButton::clicked, this, [this] {
        if(auto* item = list_->currentItem()) list_->editItem(item);
    });
    connect(pbDelete_, &QPushButton::clicked, this, &MaterialsDialog::deleteMaterial);
    connect(list_, &QListWidget::currentItemChanged, this, &MaterialsDialog::updateButtons);
    connect(buttonBox, &QDialogButtonBox::accepted, this, &MaterialsDialog::accept);
    connect(buttonBox, &QDialogButtonBox::rejected, this, &MaterialsDialog::reject);

    if(list_->count()) list_->setCurrentRow(0);
    updateButtons();
}

void MaterialsDialog::addMaterial() {
    auto* item = new QListWidgetItem{tr("New material"), list_};
    item->setData(Qt::UserRole, Tool::newUid());
    item->setFlags(item->flags() | Qt::ItemIsEditable);
    list_->setCurrentItem(item);
    list_->editItem(item);
}

void MaterialsDialog::deleteMaterial() {
    auto* item = list_->currentItem();
    if(!item) return;
    // Наборы режимов этого материала без него не нужны: спрашиваем и удаляем
    // их вместе с ним (по OK, в ToolHolder::setMaterials).
    const int count = App::toolHolder().presetsUsing(item->data(Qt::UserRole).toString());
    if(count > 0
        && QMessageBox::question(this, tr("Delete material"),
               tr("Material \"%1\" is used by %n cutting data set(s).\nDelete the material together with them?", nullptr, count).arg(item->text()))
            != QMessageBox::Yes)
        return;
    delete item;
    updateButtons();
}

void MaterialsDialog::updateButtons() {
    const bool current = list_->currentItem() != nullptr;
    pbRename_->setEnabled(current);
    pbDelete_->setEnabled(current);
}

void MaterialsDialog::accept() {
    std::vector<ToolHolder::Material> materials;
    for(int row{}; row < list_->count(); ++row) {
        const auto* item = list_->item(row);
        const QString uid = item->data(Qt::UserRole).toString();
        QString name = item->text().trimmed();
        if(name.isEmpty()) name = uid; // безымянный в списках не разглядеть -- хотя бы ключ
        materials.push_back({uid, name});
    }
    removed_.clear();
    for(const auto& material: App::toolHolder().materials())
        if(r::none_of(materials, [&material](const ToolHolder::Material& m) { return m.uid == material.uid; }))
            removed_.insert(material.uid);
    App::toolHolder().setMaterials(std::move(materials));
    QDialog::accept();
}

#include "moc_tool_materialsdialog.cpp"
