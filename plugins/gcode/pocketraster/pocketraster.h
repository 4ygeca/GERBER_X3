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

#include "gc_creator.h"
#include "gc_file.h"

namespace PocketRaster {

constexpr auto POCKET_RASTER = "PocketRaster"_hash32;

class Creator : public GCode::Creator {
public:
    Creator() { }
    ~Creator() override = default;

    static inline const QString AccDistance = u"AccDistance"_s;
    // Выбор внешней границы растра HLDI снаружи (Form::Border) -- только для
    // правки формой: сама граница уже лежит в Params::boundary, расчёт этот
    // ключ не читает.
    static inline const QString Border = u"Border"_s;
    static inline const QString Fast = u"Fast"_s;
    static inline const QString Pass = u"Pass"_s;
    // Растр HLDI однонаправленный: все строки засвечиваются в одну сторону,
    // между строками -- возврат без луча. Ключ только у таких УП: без него
    // строки идут змейкой, как считались все прежние. Попутное/встречное
    // (Params::Convent) засветке ни к чему, и выбор этот -- свой.
    static inline const QString Unidirectional = u"Unidirectional"_s;
    static inline const QString UseAngle = u"UseAngle"_s;

    enum {
        NoProfilePass,
        First,
        Last
    };
    // Creator interface
protected:
    void create() override; // Creator interface
    uint32_t type() override;
    bool possibleTest() const override { return true; }

private:
    void createRaster(const Tool& tool, const double depth, const double angle, const int prPass);
    void createRasterAccLaser(const Tool& tool, const double depth, const double angle, const int prPass);
    // Засвечиваемые отрезки строк -> один проход головки: чётные пути -- ходы
    // без луча (разгоны, развороты или возвраты, промежутки, выбег), нечётные
    // -- отрезки. unidirectional -- каждая строка слева направо, иначе змейкой.
    void addAcc(Geo::Polylines& src, const double accDistance, const bool unidirectional);
};

// Объект file.ext в pocketraster.js: чем растр этой УП пишется.
class RasterApi final : public QObject {
    Q_OBJECT
    // Растр HLDI (Params Fast, только лазер): первая группа путей -- ОДИН
    // непрерывный проход головки, в нём нечётные пути -- засвечиваемые
    // отрезки, чётные -- ходы с выключенным лучом между ними (Creator::addAcc:
    // разгоны, развороты -- у однонаправленного возвраты к началу следующей
    // строки, -- промежутки внутри строки, выбег). Так же их рисует
    // createGiLaser. Вторая группа, если есть, -- обводка контура.
    Q_PROPERTY(bool hldi READ hldi CONSTANT)

public:
    explicit RasterApi(bool hldi)
        : hldi_{hldi} { }
    bool hldi() const { return hldi_; }

private:
    bool hldi_;
};

class[[= Serial::name("PocketRaster")]] File final : public GCode::File {
public:
    void serialize(Serial::Writer& sb) const override { Serial::writeInto(sb, *this); }
    explicit File();
    explicit File(GCode::Params&& gcp);
    QIcon icon() const override { return QIcon::fromTheme(u"raster-path"_s); }
    uint32_t type() const override { return POCKET_RASTER; }
    void createGi() override;
    QObject* createJsExtension(GCode::GcFileProxy& proxy, QJSEngine& engine) override;

private:
    // Растр HLDI лазером: в первой группе ходы с выключенным лучом (см. RasterApi).
    bool hldi() const;
};

} // namespace PocketRaster
