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

#include "serial.h"
#include "utils.h"
#include <QObject>
#include <QPainterPath>
#include <vector>

class QWidget;
class QIcon;
class QJsonObject;

class Tool {
    friend QDebug operator<<(QDebug debug, const Tool& t);

public:
    enum class ID : int32_t {
        // Folder = -1,
        Null = 0,
        Folder = 0,
        Tool = +1
    };

    enum Type {
        Drill,
        EndMill,
        Engraver,
        Laser,
        ThreadMill,
        Group = 100
    };

    // Операция, под которую берутся режимы резания. Один и тот же физический
    // инструмент работает с разными режимами: фреза 0.5 и сверлит (врезание),
    // и фрезерует отверстия 0.65. Операция УП -- programOperation().
    enum class Operation : int {
        Drilling,
        Milling,
        Laser,
        Threading,
    };

    // Цикл сверления (data.drillCycle). Номера -- договор со скриптами:
    // drilling.js читает file.tool.drillCycle числом, не переставлять.
    enum DrillCycle : int {
        CycleAuto,   // решает постпроцессор
        CycleExpand, // без постоянного цикла -- G0/G1
        CycleG81,    // простое сверление
        CycleG82,    // с выдержкой на дне (dwell)
        CycleG83,    // клевки с полным отводом, Q -- passDepth
        CycleG73,    // клевки с ломкой стружки, Q -- passDepth
    };

    // Набор режимов «операция x материал». Живёт отдельно от геометрии и
    // только в базе (tools.json, ключ "presets" у инструмента): снимок в УП
    // плоский -- ToolHolder::resolve() переносит в него значения набора.
    struct CuttingPreset {
        Operation operation{Operation::Milling};
        QString material{};    // uid материала; пусто -- любой материал
        double spindleSpeed{}; // у лазера -- мощность (S)
        double feedRate{};
        double plungeRate{};
        double passDepth{}; // у резьбофрезы шаг резьбы -- геометрия, набор его не трогает
        double stepover{};
        double oneTurnCut{};
        int drillCycle{};
        double dwell{};
        bool retractToR{};
        // У набора без ключа (tools.json прежней версии) -- M4, как было всегда.
        bool laserDynamic{true};

        void read(const QJsonObject& json);
        void write(QJsonObject& json) const;
    };

    // Код инструмента уходит в комментарии УП, и по нему отправитель (плагин
    // ncSender) ищет физический инструмент: без пробелов, скобок и точки с
    // запятой.
    static constexpr int MaxCodeLength = 20;
    static QString codePattern(); // для валидатора: [A-Za-z0-9._-]{0,20}
    static bool isValidCode(const QString& code);

    // Шаблоны автоматического кода по типам (tools.json, ключ "codeFormat";
    // правятся в окне «Формат кода…» базы). Подстановки:
    //   {D} -- диаметр без точки: точка подразумевается, знаков после неё
    //          столько, сколько нужно диаметру (1..3), у диаметра меньше 1 мм
    //          впереди 0: 0.35 -> 035, 0.5 -> 05, 1 -> 10, 1.96 -> 196;
    //   {L} -- длина рабочей части в десятых мм, от точности диаметра не
    //          зависит: 5 -> 50, 10 -> 100;
    //   {d}, {l} -- диаметр и длина с точкой, кратко: 0.8, 9 (прежний D0.8x9);
    //   {A} -- угол: 30, 22.5.
    // [ ] -- необязательная часть: пишется, только если у всех подстановок в
    // ней есть значение (длина больше 1 мм, угол больше 0). Имена полей --
    // ключи json.
    struct CodeFormat {
        QString drill{u"D{D}[L{L}]"_s};
        QString endmill{u"M{D}[L{L}]"_s};
        QString engraver{u"V{D}A{A}[L{L}]"_s};
        QString laser{u"L{D}"_s};
        QString threadmill{u"T{D}[L{L}]"_s};

        // У типа без кода (группа) -- пусто.
        QString pattern(Type type) const;
        void setPattern(Type type, const QString& pattern);
        // Нет ключа, не строка или пусто -- шаблон по умолчанию.
        void read(const QJsonObject& json);
        void write(QJsonObject& json) const;
        bool operator==(const CodeFormat&) const = default;
    };
    // Для валидатора шаблона: знаки кода, подстановки и [ ] без вложенных.
    static QString codeFormatPattern();
    // Код по шаблону для геометрии: знаки вне [A-Za-z0-9._-] выброшены, не
    // длиннее MaxCodeLength. Шаблон из tools.json мог быть правлен руками --
    // разбор терпимый: незнакомое -- как есть, незакрытая [ -- до конца.
    static QString codeFromPattern(const QString& pattern, double diameter, double length, double angle);

private:
    struct {
        QString name{QObject::tr("Default")};
        QString note;
        double angle{.0};
        double diameter{1.};
        double feedRate{600.};
        double oneTurnCut{0.1};
        double passDepth{2.}; // max thread pitch
        double plungeRate{100.};
        double spindleSpeed{12000.};
        double stepover{0.5};
        double lenght{1.}; // длина рабочей части (не вылет от кольца)
        ID id{ID::Null};
        Type type{EndMill};
        bool autoName{true};
        // Ниже -- поля, которых нет в старых tools.json и проектах: при чтении
        // остаются значения по умолчанию (Tool::read, Serial).
        QString uid;         // постоянная личность инструмента, см. identityKey()
        QString code;        // код для УП и отправителя; пусто -- codeAuto()
        bool autoCode{true}; // код следует за геометрией, как имя за autoName
        int drillCycle{CycleAuto};
        double dwell{0.};       // выдержка на дне, с (G82)
        bool retractToR{false}; // G99: между отверстиями -- на плоскость R, а не на исходную
        QString material;       // uid материала, чей набор режимов применён к ЭТОМУ снимку; пусто -- свои значения
        // Режим мощности лазера: true -- M4, мощность следует за фактической
        // скоростью (так лазер включался всегда); false -- M3, постоянная.
        // Скрипты читают file.tool.laserDynamic (laserTile в common_gcode.js).
        bool laserDynamic{true};
    } data;

public:
    Tool() = default;
    Tool(double diameter) { data.diameter = diameter; }

    // name
    QString nameEnc() const;
    QString name() const { return data.name; }
    void setName(const QString& name) { resetHash(), data.name = name; }
    // note
    QString note() const { return data.note; }
    void setNote(const QString& note) { resetHash(), data.note = note; }
    // type
    Type type() const { return data.type; }
    void setType(int type) { resetHash(), data.type = static_cast<Type>(type); }
    // angle
    double angle() const { return data.angle; }
    void setAngle(double angle) { resetHash(), data.angle = angle; }
    // diameter
    double diameter() const { return data.diameter; }
    void setDiameter(double diameter) { resetHash(), data.diameter = diameter, updatePath(); }
    // feedRate
    double feedRate_mmPerSec() const { return data.feedRate / 60.0; }
    double feedRate() const { return data.feedRate; }
    void setFeedRate(double feedRate) { resetHash(), data.feedRate = feedRate; }
    // oneTurnCut
    double oneTurnCut() const { return data.oneTurnCut; }
    void setOneTurnCut(double oneTurnCut) { resetHash(), data.oneTurnCut = oneTurnCut; }
    // passDepth
    double passDepth() const { return data.passDepth; }
    void setPassDepth(double passDepth) { resetHash(), data.passDepth = passDepth; }
    // plungeRate
    double plungeRate() const { return data.plungeRate; }
    void setPlungeRate(double plungeRate) { resetHash(), data.plungeRate = plungeRate; }
    // spindleSpeed
    double spindleSpeed() const { return data.spindleSpeed; }
    void setSpindleSpeed(double spindleSpeed) { resetHash(), data.spindleSpeed = spindleSpeed; }
    // stepover
    double stepover() const { return data.stepover; }
    void setStepover(double stepover) { resetHash(), data.stepover = stepover; }
    // autoName
    bool autoName() const { return data.autoName; }
    void setAutoName(bool autoName) { resetHash(), data.autoName = autoName; }
    // lenght -- длина рабочей части
    double lenght() const { return data.lenght; }
    void setLenght(double lenght) { resetHash(), data.lenght = lenght; }
    // Thread Hole Diam
    double holeDiam() const { return data.angle; }
    void setHoleDiam(double val) { resetHash(), data.angle = val; }
    // ID
    ID id() const { return data.id; }
    void setId(ID id) { resetHash(), data.id = id; }

    // uid
    QString uid() const { return data.uid; }
    void setUid(const QString& uid) { resetHash(), data.uid = uid; }
    static QString newUid();
    // Личность для «тот же физический инструмент»: uid, а у старых снимков
    // (проекты до uid) -- тип и геометрия.
    QString identityKey() const;
    // code -- заданный, а если пуст, автоматический
    QString code() const;
    void setCode(const QString& code) { resetHash(), data.code = code; }
    bool autoCode() const { return data.autoCode; }
    void setAutoCode(bool autoCode) { resetHash(), data.autoCode = autoCode; }
    // Код по геометрии и формату базы (ToolHolder::codeFormat), по умолчанию:
    // D08L90, D035L50, M10, V02A30, L005, T10L60.
    QString codeAuto() const;
    // То же по данному формату. Шаблон, давший пустой код, -- по шаблону по
    // умолчанию: без кода отправителю инструмент не найти.
    QString codeAuto(const CodeFormat& format) const;
    // Код следует за геометрией (autoCode) -- пересчитать.
    void updateAutoCode() {
        if(data.autoCode) setCode(codeAuto());
    }
    // Код по формату: автоматический -- пересчитать, пустой (старый
    // tools.json) -- взять. Заданный вручную не трогается. true -- сменился.
    bool recomputeAutoCode(const CodeFormat& format);
    // drillCycle, dwell, retractToR
    int drillCycle() const { return data.drillCycle; }
    void setDrillCycle(int cycle) { resetHash(), data.drillCycle = cycle; }
    double dwell() const { return data.dwell; }
    void setDwell(double dwell) { resetHash(), data.dwell = dwell; }
    bool retractToR() const { return data.retractToR; }
    void setRetractToR(bool retract) { resetHash(), data.retractToR = retract; }
    // material
    QString material() const { return data.material; }
    void setMaterial(const QString& material) { resetHash(), data.material = material; }
    // laserDynamic -- M4 (мощность по скорости) или M3 (постоянная)
    bool laserDynamic() const { return data.laserDynamic; }
    void setLaserDynamic(bool dynamic) { resetHash(), data.laserDynamic = dynamic; }

    // Наборы режимов (только у инструментов базы; в hash() не входят).
    const std::vector<CuttingPreset>& presets() const { return presets_; }
    void setPresets(std::vector<CuttingPreset> presets) { presets_ = std::move(presets); }
    // Набор ровно для (операция, материал); пустой материал -- «любой материал».
    const CuttingPreset* preset(Operation operation, const QString& material) const;
    // Собственные значения режимов инструмента (operation и material -- по умолчанию).
    CuttingPreset cuttingData() const;
    // Значения режимов из набора. Шаг резьбы резьбофрезы (passDepth) -- её
    // геометрия, его набор не меняет.
    void setCuttingData(const CuttingPreset& preset);

    // Операции, для которых у инструмента этого типа бывают наборы.
    static std::vector<Operation> operations(Type type);
    static bool isOperationAllowed(Type type, Operation operation);
    // Операция УП: лазер -- Laser, резьбофреза -- Threading, форма сверловки
    // в режиме сверления -- Drilling, всё остальное -- Milling.
    static Operation programOperation(Type type, bool drilling);
    static QString operationName(Operation operation);

    // depth_
    static double depth() { return depth_; }
    static void setDepth(double depth) { depth_ = depth; }

    double getDiameter(double depth) const;
    double getDepth() const;

    void read(const QJsonObject& json);
    void write(QJsonObject& json) const;
    bool isValid() const;
    QIcon icon() const;
    QString errorStr() const;
    void errorMessageBox(QWidget* parent = nullptr) const;
    size_t hash() const;
    size_t hash2() const;

    QPainterPath path(const QPointF& pt = {}) const;
    void updatePath(double depth = 0.0);

private:
    static inline double depth_;

    // Кэши хэшей независимы: hash2() раньше отдавал hash2_, если посчитан был
    // hash_, -- и после hash() возвращал 0. Сбрасываются оба при любой правке.
    void resetHash() { hash_ = hash2_ = {}; }
    [[= Serial::skip]] mutable size_t hash_{};
    [[= Serial::skip]] mutable size_t hash2_{};

    [[= Serial::skip]] QPainterPath path_;

    // Вне data намеренно: Tool::write (и снимки УП в проекте, и json форм)
    // отражает только data, а наборы снимку ни к чему.
    [[= Serial::skip]] std::vector<CuttingPreset> presets_;
};

using CuttingPreset = Tool::CuttingPreset;

using Tools = std::map<Tool::ID, Tool, std::greater<Tool::ID>>;

class ToolHolder {
    friend class ToolItem;
    friend class AbstractFilePlugin;

public:
    // Материал заготовки: uid -- постоянный ключ (на него ссылаются наборы
    // режимов и проекты), name -- имя для людей, правится в базе.
    struct Material {
        QString uid;
        QString name;
    };

private:
    Tools tools_;
    std::vector<Material> materials_;
    Tool::CodeFormat codeFormat_;
    bool rootRead_{}; // материалы и формат кода уже прочитаны: повторное чтение tools.json (окно базы) их не трогает
    ToolHolder(const ToolHolder&) = delete;
    ToolHolder& operator=(const ToolHolder&) = delete;
    ToolHolder(ToolHolder&&) = delete;
    ToolHolder& operator=(ToolHolder&&) = delete;

public:
    ToolHolder() = default;

    const Tool& tool(Tool::ID id) { return tools_.at(id); }
    const Tools& tools() { return tools_; }
    // Инструмент базы с той же личностью, что у снимка: по uid, а у старого
    // снимка без uid -- по id, если совпали тип и имя. Нет -- nullptr.
    const Tool* findTool(const Tool& snapshot) const;
    void readTools();
    // true -- что-то досоздано (uid, коды, список материалов) или код
    // пересчитан по формату: старый tools.json, его надо переписать.
    bool readTools(const QJsonObject& json);
    void writeTools(QJsonObject& json);

    // Формат автоматического кода (tools.json, ключ "codeFormat").
    const Tool::CodeFormat& codeFormat() const { return codeFormat_; }
    // Новый формат: коды инструментов базы с autoCode -- по нему, заданные
    // вручную остаются. true -- какой-то код сменился.
    bool setCodeFormat(const Tool::CodeFormat& format);

    // Материалы
    const std::vector<Material>& materials() const { return materials_; }
    // Новый список. Наборы режимов материалов, которых в нём больше нет,
    // удаляются у всех инструментов базы.
    void setMaterials(std::vector<Material> materials);
    bool hasMaterial(const QString& uid) const;
    // Имя для показа: имя материала, «unknown (uid)» для незнакомого, пусто для пустого.
    QString materialName(const QString& uid) const;
    // «<Операция> · <Материал или Любой материал>»
    QString presetName(const CuttingPreset& preset) const;
    // Сколько наборов режимов у инструментов базы ссылаются на материал.
    int presetsUsing(const QString& uid) const;
    static std::vector<Material> defaultMaterials();

    // Снимок инструмента под операцию и материал: набор (операция, материал)
    // -> набор операции для любого материала -> собственные значения
    // инструмента. Незнакомый материал (проект с другого ПК) -- как без
    // материала. Снимок плоский: наборов в нём нет, material -- чей набор взят.
    Tool resolve(const Tool& base, Tool::Operation operation, const QString& materialUid) const;

private:
    void persist(QJsonObject json);
};

Q_DECLARE_METATYPE(Tool)
Q_DECLARE_METATYPE(Tool::Type)
Q_DECLARE_METATYPE(Tool::ID)
