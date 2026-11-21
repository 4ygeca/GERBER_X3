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

#include "geo/polygon.h"
#include <QJSValue>
#include <QObject>

class QJSEngine;

namespace GCode {

class File;

class GcFileProxy : public QObject {
    Q_OBJECT

    // Инструмент целиком, полями как они лежат в Tool: name, note, angle,
    // diameter, feedRate, oneTurnCut, passDepth, plungeRate, spindleSpeed,
    // stepover, lenght, id, type, autoName. Список берётся рефлексией, так что
    // новое поле у Tool появляется здесь само (так приходят и поля сверловки
    // drillCycle, dwell, retractToR; drilling.js читает их с умолчаниями --
    // у инструмента без них их нет).
    //
    // Это СЫРЫЕ поля, и они не заменяют свойства ниже: toolDiameter -- это
    // эффективный диаметр на глубине (у гравера конус), а threadPitch и
    // threadHoleDiam -- переиспользованные passDepth и angle.
    Q_PROPERTY(QJSValue tool READ toolObject CONSTANT)

    // Tool and feed properties
    Q_PROPERTY(bool laser READ laser CONSTANT)
    Q_PROPERTY(double toolDiameter READ toolDiameter CONSTANT)
    Q_PROPERTY(double toolLength READ toolLength CONSTANT)
    Q_PROPERTY(double toolOneTurnCut READ toolOneTurnCut CONSTANT)
    Q_PROPERTY(double feedRate READ feedRate CONSTANT)
    Q_PROPERTY(double plungeRate READ plungeRate CONSTANT)
    Q_PROPERTY(int spindleSpeed READ spindleSpeed CONSTANT)
    Q_PROPERTY(QString strFeed READ strFeed CONSTANT)
    Q_PROPERTY(QString strPlungeFeed READ strPlungeFeed CONSTANT)
    Q_PROPERTY(QString strSpindle READ strSpindle CONSTANT)

    // Threading (Tool::ThreadMill) properties
    Q_PROPERTY(bool inside READ inside CONSTANT)                   // true: internal thread (hole), false: external thread (rod)
    Q_PROPERTY(bool climb READ climb CONSTANT)                     // true: climb milling, false: conventional milling
    Q_PROPERTY(bool leftHand READ leftHand CONSTANT)               // true: left-hand thread
    Q_PROPERTY(double threadPitch READ threadPitch CONSTANT)       // Tool::passDepth() repurposed as thread pitch P
    Q_PROPERTY(double threadHoleDiam READ threadHoleDiam CONSTANT) // Tool::holeDiam(), pre-machined hole/rod diameter m
    Q_PROPERTY(bool spiralRamp READ spiralRamp CONSTANT)           // ramp down along the contour instead of plunging
    Q_PROPERTY(bool circle READ circle CONSTANT)                   // add a full circle pass at thread bottom
    Q_PROPERTY(bool chamfer READ chamfer CONSTANT)                 // cut a chamfer with the thread mill after threading
    Q_PROPERTY(int starts READ starts CONSTANT)                    // number of thread starts N (multi-start thread)

    // Свойства проекта (форма GCode::Properties) одним объектом -- снимок
    // Project::Header рефлексией: safeZ, clearence, plunge, boardThickness,
    // copperThickness, glue, tailing{spacingX, spacingY, stepsX, stepsY},
    // workRect{x, y, width, height}, home{x, y}, zero{x, y}, pins[], pinsUsed[].
    // Новое поле шапки проекта появляется здесь само.
    Q_PROPERTY(QJSValue properties READ properties CONSTANT)

    // Не раскладывать по тайлам, даже если раскладка включена (Params::NotTile).
    Q_PROPERTY(bool notTile READ notTile CONSTANT)
    Q_PROPERTY(QString name READ name CONSTANT)
    Q_PROPERTY(QString programName READ programName CONSTANT)

    // Циклы сверления текущего поста (post.cycles, см. Post::Cycles): {drill,
    // dwell, peck, chipBreak, cancel, retract: {initial, r}} -- коды G81, G82,
    // G83, G73, G80, G98/G99. Цикла, которого у контроллера нет, в объекте
    // нет; у поста без циклов -- undefined (скрипт разворачивает их сам).
    Q_PROPERTY(QJSValue cycles READ cycles CONSTANT)
    // Единица слова P выдержки (G4 и G82) у поста: "s" или "ms".
    Q_PROPERTY(QString dwellUnit READ dwellUnit CONSTANT)

    // Current depth (updated by savePath spiral logic)
    Q_PROPERTY(double z READ zVal WRITE setZ)

public:
    explicit GcFileProxy(File* file, QJSEngine* engine, QObject* parent = nullptr);

    QJSValue toolObject() const;
    bool laser() const;
    bool spiralRamp() const;
    double toolDiameter() const;
    double toolLength() const;
    double toolOneTurnCut() const;
    double feedRate() const;
    double plungeRate() const;
    int spindleSpeed() const;
    QString strFeed() const;
    QString strPlungeFeed() const;
    QString strSpindle() const;
    bool inside() const;
    bool climb() const;
    bool leftHand() const;
    double threadPitch() const;
    double threadHoleDiam() const;
    bool circle() const;
    bool chamfer() const;
    int starts() const;
    QJSValue properties() const;
    bool notTile() const;
    QString name() const;
    QString programName() const;
    QJSValue cycles() const;
    QString dwellUnit() const;
    double zVal() const;
    void setZ(double z);

    // Return all tool paths transformed for tile offset (ox, oy).
    // Result: array[pathssIdx][pathIdx][vertexIdx] = {x, y, type, cx, cy}
    // Each inner array also has .closed (bool) and .perimeter (double) properties.
    Q_INVOKABLE QJSValue getToolPaths(double ox, double oy);

    // Return array of cut depths (negative values, deepest last).
    Q_INVOKABLE QJSValue getDepths();

    // Emit G0 rapid to (x, y), plunge, and touch Z0 surface (updates file z_).
    Q_INVOKABLE void startPath(double x, double y);

    // Lift tool to clearance height.
    Q_INVOKABLE void endPath();

    // Append a raw gcode line to the output.
    Q_INVOKABLE void addLine(const QString& line);

    // Диалект поста: комментарий и коды шпинделя/лазера.
    Q_INVOKABLE QString comment(const QString& text);
    Q_INVOKABLE QString spindleOn();
    Q_INVOKABLE QString spindleOff();
    Q_INVOKABLE QString laserOn(bool dynamic);
    Q_INVOKABLE QString laserOff();
    // Пауза на seconds в диалекте поста: post.dwell(ctx, seconds), а без неё
    // «G4 P» в единицах поста (fmtDwell).
    Q_INVOKABLE QString dwell(double seconds);

    // Generate gcode lines for one curve from the last getToolPaths() result.
    // pathssIdx / pathIdx select the Geo::Polygons[i][j] curve.
    // reversed = true uses the curve in reverse direction (zigzag alternate pass).
    // perimeter > 0 && depth != 0 enables spiral ramp; otherwise flat cut.
    // Returns JS array of strings.
    Q_INVOKABLE QJSValue savePathLines(int pathssIdx, int pathIdx, bool reversed, double perimeter, double depth);

    // Format a list of gcode tokens, suppressing unchanged values per format flags.
    // parts: JS array of strings such as [g0(), fmtX(1.0), fmtY(2.0)].
    // Letters written: G X Y Z I J R S F Q P L (any other word is dropped).
    Q_INVOKABLE QString formatted(const QJSValue& parts);
    // Следующий formatted() напишет слова этих букв, даже неизменившиеся
    // (например "GXYZRQPF" перед первой строкой цикла сверления).
    Q_INVOKABLE void forceWords(const QString& letters);

    Q_INVOKABLE QString g0();
    Q_INVOKABLE QString g1();
    Q_INVOKABLE QString g2();
    Q_INVOKABLE QString g3();
    Q_INVOKABLE QString fmtX(double v);
    Q_INVOKABLE QString fmtY(double v);
    Q_INVOKABLE QString fmtZ(double v);
    Q_INVOKABLE QString fmtI(double v);
    Q_INVOKABLE QString fmtJ(double v);
    Q_INVOKABLE QString fmtS(int v);
    Q_INVOKABLE QString fmtF(double v);
    // Слова циклов сверления: R -- плоскость отвода, Q -- шаг клевка (оба
    // длины, на сетке вывода, как X/Y/Z), P -- число как есть (без пересчёта
    // единиц), fmtDwell -- P выдержки seconds в единицах поста (dwellUnit).
    Q_INVOKABLE QString fmtR(double v);
    Q_INVOKABLE QString fmtQ(double v);
    Q_INVOKABLE QString fmtP(double v);
    Q_INVOKABLE QString fmtDwell(double seconds);

    // Для расширений (file.ext): траектории последнего getToolPaths(), его
    // смещение тайла и вывод произвольной полилинии тем же savePath.
    const std::vector<Geo::Polylines>& cachedPathss() const { return cachedPathss_; }
    QPointF offset() const { return offset_; }
    QJSValue linesFor(const Geo::Polyline& curve, double perimeter, double depth);
    File* file() const { return file_; }
    QJSEngine* engine() const { return engine_; }

    // Снимок Project::Header как JS-объект на заданном движке.
    static QJSValue propertiesValue(QJSEngine& engine);

private:
    File* file_;
    QJSEngine* engine_;
    QPointF offset_;
    std::vector<Geo::Polylines> cachedPathss_;
};

} // namespace GCode
