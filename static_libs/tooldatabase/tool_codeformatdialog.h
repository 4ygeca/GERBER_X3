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
#include <vector>

class QLabel;
class QLineEdit;
class QPushButton;

// Формат автоматического кода (Tool::CodeFormat): шаблон на тип инструмента,
// под ним -- коды образцов геометрии, «По умолчанию». В базу -- по OK: коды
// инструментов с автоматическим кодом пересчитываются (ToolHolder::setCodeFormat).
class CodeFormatDialog : public QDialog {
    Q_OBJECT

public:
    explicit CodeFormatDialog(QWidget* parent = nullptr);
    // Шаблоны из полей; пустое поле -- шаблон по умолчанию.
    Tool::CodeFormat format() const;

    void accept() override;

private:
    struct Row {
        Tool::Type type;
        QLineEdit* edit;
        QLabel* preview;
    };
    std::vector<Row> rows_;
    QPushButton* pbOk_{};

    void updatePreview();
    void restoreDefaults();
};
