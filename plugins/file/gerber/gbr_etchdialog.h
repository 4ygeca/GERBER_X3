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
#pragma once

#include "gbr_types.h"

#include <QDialog>
#include <vector>

class DoubleSpinBox;
class QCheckBox;
class QComboBox;
class QLabel;
class QTreeWidget;
class QTreeWidgetItem;

namespace Gerber {

class File;

// Компенсация подтрава выбранных файлов Gerber (см. EtchComp). Пересчёт
// дорог -- полное слияние слоя с раздуванием меди, -- поэтому только по
// Apply/OK, а не на каждый шаг счётчика; Cancel возвращает то, что было до
// открытия диалога.
class EtchDialog : public QDialog {
    Q_OBJECT

public:
    explicit EtchDialog(std::vector<File*> files, QWidget* parent = nullptr);
    ~EtchDialog() override;

    void accept() override;
    void reject() override;

private:
    // Класс объектов для ручного выбора: штрихи и вспышки -- по D-коду,
    // области -- по своей X2-функции (EtchComp::keyOf).
    struct ObjClass {
        int32_t key{};
        int32_t function{-1}; // X2-функция класса, -1 -- нет
        std::vector<const GrObject*> objects;
        int32_t clear{}; // из них вырезов (%LPC)
        QTreeWidgetItem* row{};
    };
    struct Entry {
        File* file{};
        EtchComp backup; // до открытия диалога -- для Cancel
        EtchComp edited; // ручной выбор таблицы копится здесь (overrides)
        bool x2{};
        int32_t shorts{}; // замыканий до открытия диалога
        std::vector<ObjClass> classes;
    };

    // Настройка файла: общие поля диалога плюс его собственный ручной выбор.
    EtchComp settings(const Entry& entry) const;
    void fillTree();
    void updateRows();
    // etchComp -- что будет применено, rules -- то же без ручного выбора.
    void updateRow(const Entry& entry, const ObjClass& cls, const EtchComp& etchComp, const EtchComp& rules);
    void onItemChanged(QTreeWidgetItem* item, int column);
    void resetOverrides();
    bool apply();
    void restore(); // все файлы -- к backup

    std::vector<Entry> entries_;
    DoubleSpinBox* offset_;
    QCheckBox* plainIsCopper_;
    QComboBox* clearMode_;
    QCheckBox* keepPadHoles_;
    QLabel* info_;
    QTreeWidget* tree_;
    QLabel* status_;
    bool applied_{}; // что-то уже применено -- Cancel откатывает
    bool nets_{};    // хоть у одного файла есть X2-цепи -- замыкания считаются
};

} // namespace Gerber
