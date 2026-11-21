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
#pragma once

#include "tool.h"
#include <QDialog>
#include <set>

class QListWidget;
class QPushButton;

// Список материалов заготовки базы инструментов: добавить, переименовать,
// удалить. Материал, на который ссылаются наборы режимов, удаляется с
// вопросом -- наборы уходят вместе с ним. В базу -- по OK.
class MaterialsDialog : public QDialog {
    Q_OBJECT

public:
    explicit MaterialsDialog(QWidget* parent = nullptr);
    // uid материалов, удалённых по OK: их наборы режимов из базы уже удалены.
    const std::set<QString>& removed() const { return removed_; }

    void accept() override;

private:
    QListWidget* list_;
    QPushButton* pbRename_;
    QPushButton* pbDelete_;
    std::set<QString> removed_;

    void addMaterial();
    void deleteMaterial();
    void updateButtons();
};
