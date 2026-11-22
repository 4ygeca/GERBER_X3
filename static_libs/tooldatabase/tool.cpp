// This is an open source non-commercial project. Dear PVS-Studio, please check it.
// PVS-Studio Static Code Analyzer for C, C++, C#, and Java: https://pvs-studio.com
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
#include "tool.h"
#include "reflection.h"
#include "settings.h"
#include <QApplication>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QIcon>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMessageBox>
#include <QRegularExpression>
#include <QUuid>
#include <QtMath>
#include <app.h>
#include <cmath>
#include <optional>
#include <set>

int toolId = qRegisterMetaType<Tool>("Tool");

QDebug operator<<(QDebug debug, const Tool& t) {
    QDebugStateSaver saver{debug};
    debug.nospace() << u"Tool(D "_s << t.data.diameter << u", ID "_s << +t.data.id << u", Ty "_s << t.data.type << ')';
    return debug;
}

// Число для кода и ключа личности: до трёх знаков, без хвостовых нулей, точка
// -- всегда (QString::number от локали не зависит): 0.8, 9, 0.05. Три знака --
// столько показывает поле диаметра, а погрешность double в код не попадает.
static QString toolCodeNumber(double value) {
    QString text = QString::number(value, 'f', 3);
    while(text.endsWith(u'0')) text.chop(1);
    if(text.endsWith(u'.')) text.chop(1);
    if(text == u"-0"_s) text = u"0"_s;
    return text;
}

QString Tool::nameEnc() const {
    switch(data.type) {
    case Tool::Drill     : return u"D-D%1MM"_s.arg(data.diameter);
    case Tool::EndMill   : return u"M-D%1MM"_s.arg(data.diameter);
    case Tool::Engraver  : return u"V-D%1MMA%2DEG"_s.arg(data.diameter).arg(data.angle);
    case Tool::Laser     : return u"L-D%1MM"_s.arg(data.diameter);
    case Tool::ThreadMill: return u"T-D%1MM"_s.arg(data.diameter);
    default              : return {};
    }
}

QString Tool::codePattern() { return u"[A-Za-z0-9._-]{0,%1}"_s.arg(MaxCodeLength); }

bool Tool::isValidCode(const QString& code) {
    static const QRegularExpression re{QRegularExpression::anchoredPattern(codePattern())};
    return re.match(code).hasMatch();
}

// Знак кода -- только ASCII: код идёт и в сообщение оператору, где кириллица
// не годится, и в комментарии, где скобка или ';' обрезали бы строку.
static bool isToolCodeChar(QChar c) {
    const char16_t u = c.unicode();
    return (u >= u'A' && u <= u'Z') || (u >= u'a' && u <= u'z') || (u >= u'0' && u <= u'9') || u == u'.' || u == u'_' || u == u'-';
}

// Поле формата по типу; у типа без кода -- nullptr.
static auto codeFormatField(Tool::Type type) -> QString Tool::CodeFormat::* {
    switch(type) {
    case Tool::Drill     : return &Tool::CodeFormat::drill;
    case Tool::EndMill   : return &Tool::CodeFormat::endmill;
    case Tool::Engraver  : return &Tool::CodeFormat::engraver;
    case Tool::Laser     : return &Tool::CodeFormat::laser;
    case Tool::ThreadMill: return &Tool::CodeFormat::threadmill;
    default              : return nullptr;
    }
}

QString Tool::CodeFormat::pattern(Type type) const {
    const auto field = codeFormatField(type);
    return field ? this->*field : QString{};
}

void Tool::CodeFormat::setPattern(Type type, const QString& pattern) {
    if(const auto field = codeFormatField(type))
        this->*field = pattern;
}

void Tool::CodeFormat::read(const QJsonObject& json) {
    for_each_field(*this, [&json](QString& field, std::string_view name) {
        if(const QString text = json.value(QLatin1StringView{name}).toString(); !text.isEmpty())
            field = text;
    });
}

void Tool::CodeFormat::write(QJsonObject& json) const {
    for_each_field(*this, [&json](const QString& field, std::string_view name) { json[QLatin1StringView{name}] = field; });
}

QString Tool::codeFormatPattern() {
    // Вложенных [ ] нет: «часть в части» ничего не добавляет к правилу «пишется,
    // если все значения заданы», а читать такой шаблон труднее.
    return uR"((?:[A-Za-z0-9._-]|\{[DLdlA]\}|\[(?:[A-Za-z0-9._-]|\{[DLdlA]\})*\])*)"_s;
}

QString Tool::codeFromPattern(const QString& pattern, double diameter, double length, double angle) {
    // Числа -- в тысячных: столько знаков держит поле диаметра, и погрешность
    // double в код не попадает (0.1 + 0.2 -- это 0.3).
    const qint64 diameter3 = std::llround(diameter * 1000.0);
    const qint64 length3 = std::llround(length * 1000.0);
    // Знаков после подразумеваемой точки -- сколько нужно диаметру, но не
    // меньше одного: 0.35 -> 2, 0.8 и 1 -> 1, 0.125 -> 3.
    int places = 3;
    qint64 scaled = diameter3;
    while(places > 1 && scaled % 10 == 0)
        scaled /= 10, --places;
    // До places + 1 цифры -- нулём впереди: 0.35 -> 035, 0.5 -> 05. Без него
    // 0.35 и 3.5 дали бы одно и то же 35.
    const QString implied = QString::number(scaled).rightJustified(places + 1, u'0');
    // Длина -- всегда в десятых мм, от точности диаметра не зависит: L50 у
    // сверла 0.35 и у сверла 1 -- это 5 мм.
    const qint64 length10 = (length3 + (length3 < 0 ? -50 : 50)) / 100;

    struct Value {
        QString text;
        bool set; // значение задано -- часть [ ] с ним пишется
    };
    // 1.0 -- прежнее значение длины по умолчанию, а 0.1 у сломанного сверла
    // хранит его режимы до замены: длиной ни то ни другое не является.
    const bool hasDiameter = diameter3 > 0, hasLength = length3 > 1000, hasAngle = std::llround(angle * 1000.0) > 0;
    auto placeholder = [&](QChar key) -> std::optional<Value> {
        switch(key.unicode()) {
        case u'D': return Value{implied, hasDiameter};
        case u'L': return Value{QString::number(length10), hasLength};
        case u'd': return Value{toolCodeNumber(diameter), hasDiameter};
        case u'l': return Value{toolCodeNumber(length), hasLength};
        case u'A': return Value{toolCodeNumber(angle), hasAngle};
        default  : return std::nullopt;
        }
    };

    QString code;
    QString part; // необязательная часть, пока не закрыта
    bool inPart{}, partSet{};
    for(qsizetype i{}; i < pattern.size(); ++i) {
        const QChar c = pattern[i];
        if(c == u'[' && !inPart) {
            inPart = partSet = true;
            part.clear();
            continue;
        }
        if(c == u']' && inPart) {
            if(partSet) code += part;
            inPart = false;
            continue;
        }
        QString text{c};
        if(c == u'{' && i + 2 < pattern.size() && pattern[i + 2] == u'}') {
            if(const auto value = placeholder(pattern[i + 1])) {
                text = value->text;
                partSet = partSet && value->set;
                i += 2;
            }
        }
        (inPart ? part : code) += text;
    }
    if(inPart && partSet)
        code += part;
    code.removeIf([](QChar c) { return !isToolCodeChar(c); });
    return code.left(MaxCodeLength);
}

QString Tool::newUid() { return QUuid::createUuid().toString(QUuid::WithoutBraces); }

QString Tool::identityKey() const {
    if(!data.uid.isEmpty()) return data.uid;
    // Снимок из проекта до uid: другого способа узнать тот же инструмент нет.
    return u"type%1:d%2:a%3:l%4"_s.arg(+data.type).arg(toolCodeNumber(data.diameter), toolCodeNumber(data.angle), toolCodeNumber(data.lenght));
}

QString Tool::code() const { return data.code.isEmpty() ? codeAuto() : data.code; }

QString Tool::codeAuto() const { return codeAuto(App::toolHolder().codeFormat()); }

QString Tool::codeAuto(const CodeFormat& format) const {
    if(QString code = codeFromPattern(format.pattern(data.type), data.diameter, data.lenght, data.angle); !code.isEmpty())
        return code;
    // Всё в [ ] без значений или одни недопустимые знаки -- код по шаблону по
    // умолчанию. У типа без кода (группа) пусто и там.
    return codeFromPattern(CodeFormat{}.pattern(data.type), data.diameter, data.lenght, data.angle);
}

bool Tool::recomputeAutoCode(const CodeFormat& format) {
    if(!data.autoCode && !data.code.isEmpty())
        return false;
    // Типа без автоматического кода код так и останется пустым: «сменился» --
    // только взятый, иначе tools.json переписывался бы на каждом запуске.
    const QString code = codeAuto(format);
    if(code.isEmpty() || code == data.code)
        return false;
    setCode(code);
    return true;
}

const Tool::CuttingPreset* Tool::preset(Operation operation, const QString& material) const {
    for(const CuttingPreset& preset: presets_)
        if(preset.operation == operation && preset.material == material)
            return &preset;
    return nullptr;
}

Tool::CuttingPreset Tool::cuttingData() const {
    return {
        .spindleSpeed = data.spindleSpeed,
        .feedRate = data.feedRate,
        .plungeRate = data.plungeRate,
        .passDepth = data.passDepth,
        .stepover = data.stepover,
        .oneTurnCut = data.oneTurnCut,
        .drillCycle = data.drillCycle,
        .dwell = data.dwell,
        .retractToR = data.retractToR,
        .laserDynamic = data.laserDynamic,
    };
}

void Tool::setCuttingData(const CuttingPreset& preset) {
    resetHash();
    data.spindleSpeed = preset.spindleSpeed;
    data.feedRate = preset.feedRate;
    data.plungeRate = preset.plungeRate;
    if(data.type != ThreadMill) // у резьбофрезы это шаг резьбы
        data.passDepth = preset.passDepth;
    data.stepover = preset.stepover;
    data.oneTurnCut = preset.oneTurnCut;
    data.drillCycle = preset.drillCycle;
    data.dwell = preset.dwell;
    data.retractToR = preset.retractToR;
    data.laserDynamic = preset.laserDynamic;
}

std::vector<Tool::Operation> Tool::operations(Type type) {
    switch(type) {
    // Сверло только сверлит: режимов фрезеровки у него не бывает. Такие наборы
    // из прежнего tools.json readTools пропускает (недопустимая операция), и
    // при следующем сохранении базы их в файле уже нет.
    case Drill     : return {Operation::Drilling};
    case EndMill   : return {Operation::Milling, Operation::Drilling};
    case Engraver  : return {Operation::Milling};
    case Laser     : return {Operation::Laser};
    case ThreadMill: return {Operation::Threading};
    default        : return {};
    }
}

bool Tool::isOperationAllowed(Type type, Operation operation) { return r::contains(operations(type), operation); }

Tool::Operation Tool::programOperation(Type type, bool drilling) {
    if(type == Laser) return Operation::Laser;
    if(type == ThreadMill) return Operation::Threading;
    return drilling ? Operation::Drilling : Operation::Milling;
}

QString Tool::operationName(Operation operation) {
    switch(operation) {
    case Operation::Drilling : return QCoreApplication::translate("Tool", "Drilling");
    case Operation::Milling  : return QCoreApplication::translate("Tool", "Milling");
    case Operation::Laser    : return QCoreApplication::translate("Tool", "Laser");
    case Operation::Threading: return QCoreApplication::translate("Tool", "Threading");
    }
    return {};
}

double Tool::getDiameter(double depth) const {
    if(type() == Engraver && depth > 0.0 && angle() > 0.0 && angle() <= 90.0) {
        double a = qDegreesToRadians(90 - angle() / 2);
        double d = depth * cos(a) / sin(a);
        return d * 2 + diameter();
    }
    return diameter();
}

double Tool::getDepth() const {
    switch(data.type) {
    case Tool::Drill:
        // Угол вершины вне (0, 180) -- не задан (0 у инструмента без угла) или
        // плоский торец: конуса нет. Иначе при 0 выходил бы tan(90°), и глубина
        // сверления -- порядка 1e16 мм.
        if(!(data.angle > 0.0 && data.angle < 180.0)) return 0.0;
        return data.diameter * 0.5 * tan(qDegreesToRadians((180.0 - data.angle) * 0.5));
    case Tool::EndMill :
    case Tool::Engraver:
    default            : return 0.0;
    }
}

void Tool::read(const QJsonObject& json) {
    for_each_field(data, [&json]<typename T>(T& field, std::string_view name) {
        // Нет ключа -- остаётся значение по умолчанию: старые tools.json и json
        // форм новых полей не знают, а прежнее чтение обнуляло их (и autoCode).
        // Есть -- то же преобразование, что и раньше.
        const QJsonValue value = json[QLatin1StringView{name}];
        if(!value.isUndefined())
            field = value.toVariant().value<T>();
    });
    resetHash();
}

void Tool::write(QJsonObject& json) const {
    for_each_field(data, [&json]<typename T>(const T& field, std::string_view name) {
        if constexpr(std::is_enum_v<T>)
            json[QLatin1StringView{name}] = static_cast<qint64>(field);
        else
            json[QLatin1StringView{name}] = field;
    });
}

void Tool::CuttingPreset::read(const QJsonObject& json) {
    for_each_field(*this, [&json]<typename T>(T& field, std::string_view name) {
        const QJsonValue value = json[QLatin1StringView{name}];
        if(value.isUndefined()) return;
        if constexpr(std::is_enum_v<T>)
            field = static_cast<T>(value.toInt(static_cast<int>(field)));
        else if constexpr(std::is_same_v<T, bool>)
            field = value.toBool(field);
        else if constexpr(std::is_integral_v<T>)
            field = value.toInt(field);
        else if constexpr(std::is_floating_point_v<T>)
            field = value.toDouble(field);
        else
            field = value.toString(field);
    });
}

void Tool::CuttingPreset::write(QJsonObject& json) const {
    for_each_field(*this, [&json]<typename T>(const T& field, std::string_view name) {
        if constexpr(std::is_enum_v<T>)
            json[QLatin1StringView{name}] = static_cast<int>(field);
        else
            json[QLatin1StringView{name}] = field;
    });
}

bool Tool::isValid() const {
    do {
        if(qFuzzyIsNull(data.diameter)) break;
        if(data.type != Laser && qFuzzyIsNull(data.passDepth)) break;
        if(data.type != Drill && qFuzzyIsNull(data.feedRate)) break;
        if(data.type != Drill && qFuzzyIsNull(data.stepover)) break;
        if(data.type != Laser && qFuzzyIsNull(data.plungeRate)) break;
        return true;
    } while(0);
    return false;
}

QIcon Tool::icon() const {
    switch(data.type) {
    case Tool::Drill     : return QIcon::fromTheme(u"drill"_s);
    case Tool::EndMill   : return QIcon::fromTheme(u"endmill"_s);
    case Tool::Engraver  : return QIcon::fromTheme(u"engraving"_s);
    case Tool::Laser     : return QIcon::fromTheme(u"laser"_s);
    case Tool::ThreadMill: return QIcon::fromTheme(u"thread_mill"_s);
    default              : return QIcon();
    }
}

QString Tool::errorStr() const {
    QString errorString;
    if(qFuzzyIsNull(data.diameter))
        errorString += u"Tool diameter = 0!\n"_s;
    if(qFuzzyIsNull(data.passDepth)) {
        if(type() == Drill)
            errorString += u"Pass = 0!\n"_s;
        else
            errorString += u"Depth = 0!\n"_s;
    }
    if(qFuzzyIsNull(data.feedRate))
        errorString += u"Feed rate = 0\n"_s;
    if(qFuzzyIsNull(data.stepover))
        errorString += u"Stepover = 0\n"_s;
    if(qFuzzyIsNull(data.plungeRate))
        errorString += u"Plunge rate = 0!\n"_s;
    return errorString;
}

void Tool::errorMessageBox(QWidget* parent) const {
    QMessageBox::warning(parent, QObject::tr("No valid tool...!!!"), errorStr());
}

size_t Tool::hash() const {
    if(hash_) return hash_;
    std::vector<char> hashData;
    hashData.append_range(data.name.toUtf8());
    hashData.append_range(data.note.toUtf8());
    // Строки личности и набора -- с разделителем: склейка "ab"+"c" и "a"+"bc"
    // иначе давала бы одно и то же.
    for(const QString* text: {&data.uid, &data.code, &data.material}) {
        hashData.push_back('\0');
        hashData.append_range(text->toUtf8());
    }
    auto push_back = [&hashData](auto&& arg) {
        using T = std::decay_t<decltype(arg)>;
        hashData.append_range(std::bit_cast<std::array<char, sizeof(T)>>(arg));
    };
    push_back(data.type);
    push_back(data.angle);
    push_back(data.diameter);
    push_back(data.feedRate);
    push_back(data.oneTurnCut);
    push_back(data.passDepth);
    push_back(data.plungeRate);
    push_back(data.spindleSpeed);
    push_back(data.stepover);
    push_back(data.lenght);
    push_back(data.autoName);
    push_back(data.autoCode);
    push_back(data.drillCycle);
    push_back(data.dwell);
    push_back(data.retractToR);
    push_back(data.laserDynamic);
    push_back(data.id);
    return hash_ = qHash(hashData);
}

size_t Tool::hash2() const {
    // Свой кэш, независимо от hash_ (см. resetHash).
    if(hash2_) return hash2_;
    QByteArray hashData;
    auto push_back = [&hashData](auto&& arg) {
        using T = std::decay_t<decltype(arg)>;
        hashData.append(reinterpret_cast<const char*>(&arg), sizeof(T));
    };
    push_back(data.angle);
    push_back(data.diameter);
    push_back(data.stepover);
    push_back(data.passDepth);
    return hash2_ = qHash(hashData);
}

QPainterPath Tool::path(const QPointF& pt) const { return path_.translated(pt); }

void Tool::updatePath(double depth) {
    const double diameter = getDiameter(depth);
    const double lineKoeff = diameter * 0.7;
    path_ = QPainterPath();
    path_.addEllipse({}, diameter * 0.5, diameter * 0.5);
    path_.moveTo(QPointF(0.0, +lineKoeff));
    path_.lineTo(QPointF(0.0, -lineKoeff));
    path_.moveTo(QPointF(+lineKoeff, 0.0));
    path_.lineTo(QPointF(-lineKoeff, 0.0));
}

///////////////////////////////////////////////////////
/// \brief ToolHolder::tools
///
void ToolHolder::readTools() {
    QJsonDocument loadDoc;

    QFile file(App::settingsPath() + u"/tools.json"_s);

    if(!file.exists())
        file.setFileName(qApp->applicationDirPath() + u"/tools.json"_s); // fallback path
    QJsonParseError error{};
    if(file.exists() && file.open(QIODevice::ReadOnly))
        loadDoc = QJsonDocument::fromJson(file.readAll(), &error);
    else {
        qDebug() << file.errorString();
        // Первый запуск: материалы -- по умолчанию. uid у них постоянные, в
        // tools.json они попадут с первым сохранением базы.
        readTools(QJsonObject{});
        return;
    }
    // Старый tools.json: uid выданы случайные -- записать их сразу, иначе при
    // следующем запуске у тех же инструментов были бы другие. Битый файл не
    // переписываем: из него ничего не прочитано, а запись стёрла бы его.
    if(readTools(loadDoc.object()) && error.error == QJsonParseError::NoError && loadDoc.isObject())
        persist(loadDoc.object());
}

bool ToolHolder::readTools(const QJsonObject& json) {
    bool generated{};

    if(!std::exchange(rootRead_, true)) {
        // Формат кода. Нет ключа (первый запуск, прежний tools.json) -- шаблоны
        // по умолчанию; переписывать файл ради этого незачем: в отличие от имён
        // материалов они от языка интерфейса не зависят.
        codeFormat_ = {};
        codeFormat_.read(json.value(u"codeFormat"_s).toObject());
        if(json.contains(u"materials"_s)) {
            materials_.clear();
            std::set<QString> uids;
            for(const QJsonValue& value: json[u"materials"_s].toArray()) {
                const QJsonObject object = value.toObject();
                Material material{object[u"uid"_s].toString(), object[u"name"_s].toString()};
                if(material.uid.isEmpty() || !uids.insert(material.uid).second)
                    continue; // без ключа на материал не сослаться, а повтор -- тот же материал
                materials_.push_back(std::move(material));
            }
        } else { // ключа нет вовсе -- первый запуск или старый tools.json
            materials_ = defaultMaterials();
            generated = true;
        }
    }

    // uid уже загруженных: повторное чтение (окно базы) их не дублирует.
    std::set<QString> uids;
    for(const auto& [id, tool]: tools_) uids.insert(tool.uid());

    QJsonArray toolArray = json[u"tools"_s].toArray();
    for(int treeIndex{}; treeIndex < toolArray.size(); ++treeIndex) {
        Tool tool;
        QJsonObject toolObject = toolArray[treeIndex].toObject();
        tool.read(toolObject);
        tool.setId(static_cast<Tool::ID>(toolObject[u"id"_s].toInt()));
        if(tools_.contains(tool.id()))
            continue; // как и прежде (try_emplace): загруженный не перечитывается
        std::vector<CuttingPreset> presets;
        for(const QJsonValue& value: toolObject.value(u"presets"_s).toArray()) {
            CuttingPreset preset;
            preset.read(value.toObject());
            // Набор незнакомой операции (файл новее) или повтор -- мимо.
            if(!Tool::isOperationAllowed(tool.type(), preset.operation)
                || r::any_of(presets, [&preset](const CuttingPreset& p) { return p.operation == preset.operation && p.material == preset.material; }))
                continue;
            presets.push_back(std::move(preset));
        }
        tool.setPresets(std::move(presets));
        // Старый tools.json: ни uid, ни кода. Повтор uid (файл правили руками)
        // -- тоже новый: личность у каждого инструмента своя.
        if(tool.uid().isEmpty() || uids.contains(tool.uid()))
            tool.setUid(Tool::newUid()), generated = true;
        uids.insert(tool.uid());
        // Код по формату базы: у старого tools.json его нет вовсе, а
        // автоматический мог быть посчитан прежним форматом (D0.8x9, E1x6).
        if(tool.recomputeAutoCode(codeFormat_))
            generated = true;
        tool.updatePath();
        tools_.try_emplace(tool.id(), tool);
    }
    return generated;
}

void ToolHolder::writeTools(QJsonObject& json) {
    QJsonArray toolArray;
    for(auto& [id, tool]: tools_) {
        QJsonObject toolObject;
        tool.write(toolObject);
        toolObject[u"id"_s] = +id;
        if(!tool.presets().empty()) {
            QJsonArray presets;
            for(const CuttingPreset& preset: tool.presets()) {
                QJsonObject presetObject;
                preset.write(presetObject);
                presets.push_back(presetObject);
            }
            toolObject[u"presets"_s] = presets;
        }
        toolArray.push_back(toolObject);
    }
    json[u"tools"_s] = QJsonValue{toolArray};

    // Материалы -- всегда, и пустым списком тоже: «ключа нет» значит «создать
    // список по умолчанию», а пользователь мог удалить все.
    QJsonArray materialArray;
    for(const Material& material: materials_)
        materialArray.push_back(QJsonObject{
            {u"uid"_s,  material.uid },
            {u"name"_s, material.name},
        });
    json[u"materials"_s] = materialArray;

    // Формат кода -- всегда, и шаблоны по умолчанию тоже: по нему настраивают
    // и шаблоны кодов отправителя (плагин ncSender), в файле он на виду.
    QJsonObject codeFormat;
    codeFormat_.write(codeFormat);
    json[u"codeFormat"_s] = codeFormat;
}

bool ToolHolder::setCodeFormat(const Tool::CodeFormat& format) {
    codeFormat_ = format;
    // Коды следуют за форматом так же, как за геометрией: только автоматические.
    bool changed{};
    for(auto& [id, tool]: tools_)
        if(tool.recomputeAutoCode(codeFormat_))
            changed = true;
    return changed;
}

void ToolHolder::persist(QJsonObject json) {
    const QString path = App::settingsPath() + u"/tools.json"_s;
    QDir{}.mkpath(App::settingsPath());
    // Прежний файл -- однократной копией: откат на старую версию GGEasy.
    // Копия уже есть -- она и есть прежний файл, её не трогаем.
    if(const QString backup = path + u".bak"_s; QFile::exists(path) && !QFile::exists(backup))
        QFile::copy(path, backup);
    writeTools(json); // "tree" и прочие ключи файла остаются как были
    QFile file{path};
    if(file.open(QIODevice::WriteOnly))
        file.write(QJsonDocument{json}.toJson());
    else
        qWarning() << __FUNCTION__ << file.errorString();
}

const Tool* ToolHolder::findTool(const Tool& snapshot) const {
    if(!snapshot.uid().isEmpty()) {
        for(const auto& [id, tool]: tools_)
            if(tool.uid() == snapshot.uid())
                return &tool;
        return nullptr;
    }
    // id локальны для tools.json и после удаления переиспользуются: id верен,
    // только если под ним инструмент того же типа и имени.
    if(auto it = tools_.find(snapshot.id()); it != tools_.end()
        && it->second.type() == snapshot.type() && it->second.name() == snapshot.name())
        return &it->second;
    return nullptr;
}

void ToolHolder::setMaterials(std::vector<Material> materials) {
    // Удалённые из списка материалы уносят свои наборы режимов. Наборы на
    // материалы, которых не было и в прежнем списке, не трогаем: это не
    // правка списка.
    std::set<QString> removed;
    for(const Material& material: materials_)
        if(r::none_of(materials, [&material](const Material& m) { return m.uid == material.uid; }))
            removed.insert(material.uid);
    materials_ = std::move(materials);
    if(removed.empty()) return;
    for(auto& [id, tool]: tools_) {
        auto presets = tool.presets();
        if(std::erase_if(presets, [&removed](const CuttingPreset& preset) { return removed.contains(preset.material); }))
            tool.setPresets(std::move(presets));
    }
}

bool ToolHolder::hasMaterial(const QString& uid) const {
    return !uid.isEmpty() && r::any_of(materials_, [&uid](const Material& material) { return material.uid == uid; });
}

QString ToolHolder::materialName(const QString& uid) const {
    if(uid.isEmpty()) return {};
    for(const Material& material: materials_)
        if(material.uid == uid) return material.name;
    return QCoreApplication::translate("ToolHolder", "unknown (%1)").arg(uid);
}

QString ToolHolder::presetName(const CuttingPreset& preset) const {
    return Tool::operationName(preset.operation) + u" · "_s
        + (preset.material.isEmpty() ? QCoreApplication::translate("ToolHolder", "Any material") : materialName(preset.material));
}

int ToolHolder::presetsUsing(const QString& uid) const {
    int count{};
    for(const auto& [id, tool]: tools_)
        count += static_cast<int>(r::count_if(tool.presets(), [&uid](const CuttingPreset& preset) { return preset.material == uid; }));
    return count;
}

std::vector<ToolHolder::Material> ToolHolder::defaultMaterials() {
    // uid постоянные -- на них ссылаются наборы режимов и проекты; имена --
    // на языке интерфейса при создании, дальше это данные пользователя.
    return {
        {u"fr4"_s,         QCoreApplication::translate("ToolHolder", "FR4")            },
        {u"aluminium"_s,   QCoreApplication::translate("ToolHolder", "Aluminium")      },
        {u"stainless"_s,   QCoreApplication::translate("ToolHolder", "Stainless steel")},
        {u"film"_s,        QCoreApplication::translate("ToolHolder", "Film")           },
        {u"plastic"_s,     QCoreApplication::translate("ToolHolder", "Plastic")        },
        {u"photoresist"_s, QCoreApplication::translate("ToolHolder", "Photoresist")    },
        {u"soldermask"_s,  QCoreApplication::translate("ToolHolder", "Solder mask")    },
    };
}

Tool ToolHolder::resolve(const Tool& base, Tool::Operation operation, const QString& materialUid) const {
    Tool tool{base};
    tool.setPresets({});
    // Незнакомый материал -- как без материала: набора под него нет и быть не должно.
    const CuttingPreset* preset = hasMaterial(materialUid) ? base.preset(operation, materialUid) : nullptr;
    if(!preset) preset = base.preset(operation, {});
    if(preset) {
        tool.setCuttingData(*preset);
        tool.setMaterial(preset->material);
    } else {
        tool.setMaterial({});
    }
    return tool;
}
