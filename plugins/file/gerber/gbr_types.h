/********************************************************************************
 * Author    :  Damir Bakiev                                                    *
 * Version   :  na                                                              *
 * Date      :  March 25, 2023                                                  *
 * Website   :  na                                                              *
 * Copyright :  Damir Bakiev 2016-2023                                          *
 * License   :                                                                  *
 * Use, modification & distribution is subject to Boost Software License Ver 1. *
 * http://www.boost.org/LICENSE_1_0.txt                                         *
 *******************************************************************************/
#pragma once

#include "md5.h"
#include "plugintypes.h"

#include <QDebug>
#include <QObject>
#include <map>
#include <vector>

#define DEPRECATED

namespace Gerber {

constexpr auto GERBER = "Gerber"_hash32;

class GbrObj : public QObject {
    Q_OBJECT
public:
    GbrObj() { }
    virtual ~GbrObj() { }
};

class AbstractAperture;

using ApertureMap = std::map<int, std::shared_ptr<AbstractAperture>>;

enum ZeroOmissionMode : bool {
    OmitLeadingZeros,
#ifdef DEPRECATED
    OmitTrailingZeros,
#endif
};

enum UnitMode : bool {
    Inches,
    Millimeters,
};

enum ImagePolarity : bool {
    Positive,
    Negative,
};

enum CoordinateValuesNotation : bool {
    AbsoluteNotation,
#ifdef DEPRECATED
    IncrementalNotation,
#endif
};

enum InterpolationMode {
    Linear = 1,
    ClockwiseCircular = 2,
    CounterClockwiseCircular = 3
};

enum RegionMode : bool {
    Off,
    On
};

enum QuadrantMode {
    Undef,
    Single,
    Multi
};

enum Operation {
    D01 = 1,
    D02 = 2,
    D03 = 3
};

enum Layer {
    Assy,
    Silk,
    Paste,
    Mask,
    Copper,
    Board,
};

enum Mirroring {
    NoMirroring,
    X_Mirroring,
    Y_Mirroring,
    XY_Mirroring,
};

enum GCode {
    // Graphics state operators defining the interpolateparameter defining the interpolate operation (D01).
    G01 = 1, // Sets the interpolation mode tolinear
    G02 = 2, // Sets the interpolation mode to ‘Clockwise circular interpolation’
    G03 = 3, // Sets the interpolation mode to ‘Counterclockwise circularinterpolation’
    // Comment
    G04 = 4,
    // Region mode
    G36 = 36, // Begin region
    G37 = 37, // End region
#ifdef DEPRECATED
    // Change aperture
    G54 = 54,
    // Units mode
    G70 = 70, // Inces
    G71 = 71, // Millimeteres
#endif
    // Graphics state operators defining the quadrant modeparameter, amodifier of the circular interpolation mode.
    G74 = 74, // Sets quadrant mode to ’Singlequadrant’
    G75 = 75, // Sets quadrant mode to ’Multiquadrant’
#ifdef DEPRECATED
    // Absolute / relative coordinates
    G90 = 90, // Absolute
    G91 = 91, // Relative (incremental)
#endif
};

enum PrimitiveType {
    Aperture,
    Line,
    Region,
};

class File;

struct Format {
    UnitMode unitMode = Millimeters;

    // Warning: Trailing zero omission is deprecated
    ZeroOmissionMode zeroOmisMode = OmitLeadingZeros;

    // Warning: Currently the only allowed notation is absolute notation. The incremental notation is deprecated.
    CoordinateValuesNotation coordValueNotation = AbsoluteNotation;

    // Warning: Using less than 4 decimal places is deprecated.
    enum {
        IntegerDefVal = 3,
        DecimalDefVal = 4
    };

    int xInteger = IntegerDefVal;
    int xDecimal = DecimalDefVal;
    int yInteger = IntegerDefVal;
    int yDecimal = DecimalDefVal;
};

class State {
    friend class File;
    [[= Serial::skip]] File* file_ = nullptr; // восстанавливает File::postLoad
    Operation dCode_ = D02;
    GCode gCode_ = G01;
    ImagePolarity imgPolarity_ = Positive;
    InterpolationMode interpolation_ = Linear;
    PrimitiveType type_ = Aperture;
    QuadrantMode quadrant_ = Undef;
    RegionMode region_ = Off;
    int aperture_{};
    int lineNum_{};
    QPointF curPos_;
    Mirroring mirroring_ = NoMirroring;
    double scaling_ = 1.0;
    double rotating_{};
    int32_t net_{-1}; // номер X2-цепи (%TO.N) в File::nets_, -1 -- вне цепи
    // X2-функция (%TA.AperFunction, Attr::Aperture::Function), -1 -- атрибута
    // нет. Вспышка и штрих -- снимок словаря на %AD своей апертуры, область --
    // словарь на G37. Нужна компенсации подтрава: медь от графики на меди.
    int32_t aperFunction_{-1};

public:
    State(File* const file = nullptr)
        : file_{file} { }

    inline auto file() const { return file_; }

    inline auto dCode() const { return dCode_; }
    inline void setDCode(Operation dCode) { dCode_ = dCode; }

    inline auto gCode() const { return gCode_; }
    inline void setGCode(GCode gCode) { gCode_ = gCode; }

    inline auto imgPolarity() const { return imgPolarity_; }
    inline void setImgPolarity(ImagePolarity imgPolarity) { imgPolarity_ = imgPolarity; }

    inline auto interpolation() const { return interpolation_; }
    inline void setInterpolation(InterpolationMode interpolation) { interpolation_ = interpolation; }

    inline auto type() const { return type_; }
    inline void setType(PrimitiveType type) { type_ = type; }

    inline auto quadrant() const { return quadrant_; }
    inline void setQuadrant(QuadrantMode quadrant) { quadrant_ = quadrant; }

    inline auto region() const { return region_; }
    inline void setRegion(RegionMode region) { region_ = region; }

    inline auto aperture() const { return aperture_; }
    inline void setAperture(int aperture) { aperture_ = aperture; }

    inline auto net() const { return net_; }
    inline void setNet(int32_t net) { net_ = net; }

    inline auto aperFunction() const { return aperFunction_; }
    inline void setAperFunction(int32_t aperFunction) { aperFunction_ = aperFunction; }

    inline auto& curPos() { return curPos_; }
    inline auto curPos() const { return curPos_; }
    inline void setCurPos(const QPointF& curPos) { curPos_ = curPos; }

    inline auto mirroring() const { return mirroring_; }
    inline void setMirroring(Mirroring mirroring) { mirroring_ = mirroring; }

    inline auto scaling() const { return scaling_; }
    inline void setScaling(double scaling) { scaling_ = scaling; }

    inline auto rotating() const { return rotating_; }
    inline void setRotating(double rotating) { rotating_ = rotating; }
};

struct GrObject : GraphicObject {

    [[= Serial::skip]] File* gFile{nullptr}; // восстанавливает File::postLoad
    State state;

    // public:
    GrObject() = default;
    GrObject(int32_t id, const State& state, Geo::Polygons&& paths, File* gFile, Type type, Geo::Polyline&& path = {})
        : gFile{gFile}
        , state{state} {
        GraphicObject::id = id;
        GraphicObject::fill = std::move(paths);
        GraphicObject::path = std::move(path);
        GraphicObject::type = type;
    }
};

// Компенсация подтрава -- как «коррекция ширины дорожек» PostScript в KiCad, но
// преобразованием самого файла Gerber: каждая кромка МЕДИ уходит наружу на
// offset (дорожки и площадки шире на 2*offset, зазоры уже на столько же), а
// графика на меди (текст, логотипы) остаётся как есть. Действует на слитый
// слой, а с ним -- на экран, УП и вывод на печать (File::mergeCompensated).
struct EtchComp {
    // Вырез (%LPC) без X2-функции: метка сверловки KiCad точного размера или
    // пустота в меди (зазор заливки старого образца), что сужается вместе с ней.
    enum ClearMode : int32_t {
        AutoClear = -1, // есть X2 -- точно, нет -- сужать
        ExactClear,
        ShrinkClear,
    };

    double offset{};          // мм на кромку, >= 0; 0 -- компенсации нет
    bool plainIsCopper{true}; // тёмное без X2-функции и вне цепи -- медь
    ClearMode clearMode{AutoClear};
    bool keepPadHoles{true};           // отверстия апертур (%ADD10C,1.6X0.8) не сужать
    std::map<int32_t, bool> overrides; // ручной выбор по ключу класса (keyOf)
    // Ручное исключение отдельных объектов -- текст и логотипы на общих с
    // трассами апертурах, которые выбором класса не отделить: ключи objectKey
    // по возрастанию. Такой объект -- не медь, что бы ни решили правила.
    std::vector<int64_t> excluded;

    bool active() const { return offset > 0.0; }

    // Ключ объекта для исключения: не номер (он другой после перезагрузки с
    // диска), а сама геометрия -- тип, полярность, апертура по имени (не по
    // D-коду, CAD нумерует их заново), точка вспышки и вершины пути в
    // микронах. Изменённый в CAD объект получает другой ключ, и исключение
    // с него снимается (File::initFrom).
    static int64_t objectKey(const GrObject& go);
    bool isExcluded(const GrObject& go) const;

    // Ключ класса для ручного выбора: штрих и вспышка -- D-код апертуры
    // (неотрицателен), область -- своя X2-функция (D-код у области случаен):
    // -1 без атрибута, -2 - f для функции f.
    static constexpr int32_t regionKey(int32_t function) { return -2 - function; }
    static int32_t keyOf(const GrObject& go) {
        return go.state.type() == Region ? regionKey(go.state.aperFunction()) : go.state.aperture();
    }

    // Медь ли объект: исключение, ручной выбор, иначе X2-функция, иначе для выреза
    // clearMode, иначе цепь (%TO.N), иначе plainIsCopper. Для выреза «медь» --
    // это пустота в меди, сужаемая раздуванием, а не медь -- точный вырез.
    // x2 -- есть ли у файла X2-атрибуты вообще (File::hasX2).
    bool copper(const GrObject& go, bool x2) const;

    bool operator==(const EtchComp&) const = default;
};

struct StepRepeatStr {
    void reset() {
        x = 0;
        y = 0;
        i = 0.0;
        j = 0.0;
        storage.clear();
    }
    int x{};
    int y{};
    double i{};
    double j{};
    QList<GrObject> storage;
};

class Settings {
protected:
    static inline bool cleanPolygons_;
    static inline double cleanPolygonsDist_ = 0.0005;

    static inline bool simplifyRegions_;
    static inline bool skipDuplicates_;

public:
    static bool cleanPolygons() { return cleanPolygons_; }
    static double cleanPolygonsDist() { return cleanPolygonsDist_; }

    static bool simplifyRegions() { return simplifyRegions_; }
    static bool skipDuplicates() { return skipDuplicates_; }
};

} // namespace Gerber
