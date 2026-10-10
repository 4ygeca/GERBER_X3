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
#pragma once

#include "ggcore_export.h"

#include "tool_model.h"
#include <QTreeView>

class QDropEvent;
class QPushButton;

class GGCORE_EXPORT ToolTreeView : public QTreeView {
    Q_OBJECT
public:
    explicit ToolTreeView(QWidget* parent = nullptr);
    ~ToolTreeView() override = default;
    void updateItem();
    void setButtons(const std::vector<QPushButton*>& buttons);

signals:
    void itemSelected(ToolItem* item);

protected:
    void dropEvent(QDropEvent* event) override;

private:
    void newGroup();
    void newTool();
    void deleteItem();
    void copyTool();

    void updateActions();
    void selectAndReveal(const QModelIndex& index);
    void restoreExpanded(const QModelIndex& index);
    ToolModel* model_;
    enum {
        Copy,
        Delete,
        New,
        NewGroup,
    };
    std::vector<QPushButton*> buttons_;
};
