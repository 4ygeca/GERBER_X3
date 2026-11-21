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

QString Tool::newUid() { return QUuid::createUuid().toString(QUuid::WithoutBraces); }

QString Tool::identityKey() const {
    if(!data.uid.isEmpty()) return data.uid;
    // Снимок из проекта до uid: другого способа узнать тот же инструмент нет.
    return u"type%1:d%2:a%3:l%4"_s.arg(+data.type).arg(toolCodeNumber(data.diameter), toolCodeNumber(data.angle), toolCodeNumber(data.lenght));
}

QString Tool::code() const { return data.code.isEmpty() ? codeAuto() : data.code; }

QString Tool::codeAuto() const {
    // Длина рабочей части в коде -- только заданная: у старых инструментов
    // везде прежнее значение по умолчанию 1.0, длиной оно не является.
    const QString length = data.lenght > 1.0 ? u'x' + toolCodeNumber(data.lenght) : QString{};
    QString code;
    switch(data.type) {
    case Tool::Drill     : code = u'D' + toolCodeNumber(data.diameter) + length; break;
    case Tool::EndMill   : code = u'E' + toolCodeNumber(data.diameter) + length; break;
    case Tool::Engraver  : code = u'V' + toolCodeNumber(data.diameter) + u'A' + toolCodeNumber(data.angle); break;
    case Tool::Laser     : code = u'L' + toolCodeNumber(data.diameter); break;
    case Tool::ThreadMill: code = u'T' + toolCodeNumber(data.diameter) + length; break;
    default              : break;
    }
    return code.left(MaxCodeLength);
}

bool Tool::fillMissingCode() {
    if(!data.code.isEmpty()) return false;
    // Типа без автоматического кода код так и останется пустым: «досоздано»
    // -- только то, что взято, иначе tools.json переписывался бы на каждом запуске.
    const QString code = codeAuto();
    if(code.isEmpty()) return false;
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
}

std::vector<Tool::Operation> Tool::operations(Type type) {
    switch(type) {
    case Drill     : return {Operation::Drilling, Operation::Milling};
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

    if(!std::exchange(materialsRead_, true)) {
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
        if(tool.fillMissingCode())
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
