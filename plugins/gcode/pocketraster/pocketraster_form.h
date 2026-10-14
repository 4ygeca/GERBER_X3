/*******************************************************************************
 * Author    :  Damir Bakiev                                                    *
 * Version   :  na                                                              *
 * Date      :  XXXXX XX, 2025                                                  *
 * Website   :  na                                                              *
 * Copyright :  Damir Bakiev 2016-2026                                          *
 * License   :                                                                  *
 * Use, modification & distribution is subject to Boost Software License Ver 1. *
 * http://www.boost.org/LICENSE_1_0.txt                                         *
 *******************************************************************************/
#pragma once

#include "gc_form.h"
#include "gc_plugin.h"
#include "pocketraster.h"
#include <QToolBar>

namespace Ui {
class PocketRasterForm;
}

namespace PocketRaster {

class Form : public GCode::Form {
    Q_OBJECT

public:
    explicit Form(GCode::Plugin* plugin);
    ~Form();

    // Внешняя граница растра HLDI снаружи -- в порядке пунктов cbxBorder.
    enum class Border {
        Auto,      // слой контура платы, если он охватывает выделенное, иначе рамка, иначе по выделенному
        Outline,   // слой контура платы (Edge.Cuts, X2 Profile)
        Frame,     // граница платы -- фиолетовая рамка проекта
        Selection, // габарит выделенного плюс миллиметр -- ровно как было всегда
    };

private slots:
    void onNameTextChanged(const QString& arg1);

private:
    Ui::PocketRasterForm* ui;

    int direction{};
    void updatePixmap();
    void rb_clicked();

    // Что станет границей. Считается в GUI-потоке: проект и сцену читать
    // можно только здесь, расчёт получает готовое в Params::boundary.
    struct BorderChoice {
        Geo::Polygons region{}; // пусто -- прежняя рамка по габариту выделенного
        QString label{};        // что будет взято -- для надписи под списком
        QString question{};     // не пусто -- спросить перед расчётом
    };
    // selection -- параметры уже собранного выделенного; без них (надпись) --
    // только то, что известно о проекте.
    BorderChoice resolveBorder(const GCode::Params* selection) const;
    Border borderMode() const;
    // Граница нужна только растру HLDI снаружи: фрезерный растр снаружи --
    // вырезы, и до края платы он пошёл бы фрезеровать всю плату.
    bool borderEnabled() const;
    void updateBorder();
    const QStringList names;
    static inline const QString pixmaps[]{
        u"pock_rast_climb"_s,
        u"pock_rast_conv"_s,
    };
    // QWidget interface
protected:
    void resizeEvent(QResizeEvent* event) override;
    void showEvent(QShowEvent* event) override;

    // FormsUtil interface
protected:
    void computePaths() override;
    void updateName() override;
    // Контур платы при растре HLDI снаружи -- граница, а не материал: иначе
    // выделенный вместе с маской он поглотил бы все площадки внутри себя.
    bool skipGi(const Gi::Item* gi) const override;

public:
    void editFile(GCode::File* file) override;
};

class GCPluginImpl final : public GCode::Plugin {
    Q_OBJECT
    Q_PLUGIN_METADATA(IID GCodeInterface_iid FILE "description.json")
    Q_INTERFACES(GCode::Plugin)
    Form form{this};
    // GCode::Plugin interface
public:
    QIcon icon() const override { return QIcon::fromTheme(u"raster-path"_s); }
    QKeySequence keySequence() const override { return {u"Ctrl+Shift+R"_s}; }
    QWidget* createForm() override { return &form; }
    QString gcName() const override { return u"PocketRaster"_s; };
    uint32_t type() const override { return POCKET_RASTER; }
    AbstractFile* loadFile(std::string_view json) const override { return Serial::load<File>(json); }
    std::string_view typeName() const override { return Serial::typeNameOf<File>(); }
};

} // namespace PocketRaster
