<?xml version="1.0" encoding="utf-8"?>
<!DOCTYPE TS>
<TS version="2.1" language="ru_RU" sourcelanguage="en_US">
<context>
    <name>CodeFormatDialog</name>
    <message>
        <source>Code format</source>
        <translation type="vanished">Формат кода</translation>
    </message>
    <message>
        <source>Placeholders: {D} -- the diameter without the point: as many decimals as the diameter needs, at least one, with a leading 0 below 1 mm (0.35 -&gt; 035, 0.8 -&gt; 08, 1 -&gt; 10); {L} -- the working length in tenths of a millimetre (5 -&gt; 50, 10 -&gt; 100); {d}, {l} -- the diameter and the length with the point (0.8, 9); {A} -- the angle. A part in [ ] is written only when all its values are set: the working length is over 1 mm, the angle is over 0. Letters, digits, &apos;.&apos;, &apos;_&apos; and &apos;-&apos; are written as they are.</source>
        <translation type="vanished">Подстановки: {D} — диаметр без точки: знаков после неё столько, сколько нужно диаметру, но не меньше одного, у диаметра меньше 1 мм впереди 0 (0.35 → 035, 0.8 → 08, 1 → 10); {L} — длина рабочей части в десятых миллиметра (5 → 50, 10 → 100); {d}, {l} — диаметр и длина с точкой (0.8, 9); {A} — угол. Часть в [ ] пишется, только если все значения в ней заданы: длина рабочей части больше 1 мм, угол больше 0. Латинские буквы, цифры, «.», «_» и «-» пишутся как есть.</translation>
    </message>
    <message>
        <source>OK recomputes the automatic codes of the tools; codes set by hand stay as they are. The code goes into the G-code, and the sender finds the tool by it: the code patterns of the sender plugin must follow this format.</source>
        <translation type="vanished">По OK автоматические коды инструментов пересчитываются, заданные вручную остаются как есть. Код уходит в G-код, и по нему отправитель находит инструмент: шаблоны кодов в плагине отправителя должны следовать этому формату.</translation>
    </message>
    <message>
        <source>Defaults</source>
        <translation type="vanished">По умолчанию</translation>
    </message>
    <message>
        <source>The pattern is not finished: a [ or a { is not closed</source>
        <translation type="vanished">Шаблон не закончен: не закрыта [ или {</translation>
    </message>
</context>
<context>
    <name>DepthForm</name>
    <message>
        <location filename="tool_selectorform.cpp" line="245"/>
        <source>Depth:</source>
        <translation>Глубина:</translation>
    </message>
</context>
<context>
    <name>MaterialsDialog</name>
    <message>
        <location filename="tool_materialsdialog.cpp" line="25"/>
        <source>Materials</source>
        <translation>Материалы</translation>
    </message>
    <message>
        <location filename="tool_materialsdialog.cpp" line="40"/>
        <source>Add</source>
        <translation>Добавить</translation>
    </message>
    <message>
        <location filename="tool_materialsdialog.cpp" line="41"/>
        <source>Rename</source>
        <translation>Переименовать</translation>
    </message>
    <message>
        <location filename="tool_materialsdialog.cpp" line="42"/>
        <source>Delete</source>
        <translation>Удалить</translation>
    </message>
    <message>
        <location filename="tool_materialsdialog.cpp" line="66"/>
        <source>New material</source>
        <translation>Новый материал</translation>
    </message>
    <message>
        <location filename="tool_materialsdialog.cpp" line="80"/>
        <source>Delete material</source>
        <translation>Удаление материала</translation>
    </message>
    <message numerus="yes">
        <location filename="tool_materialsdialog.cpp" line="81"/>
        <source>Material &quot;%1&quot; is used by %n cutting data set(s).
Delete the material together with them?</source>
        <translation>
            <numerusform>Материал «%1» используется в %n наборе режимов резания.
Удалить материал вместе с ним?</numerusform>
            <numerusform>Материал «%1» используется в %n наборах режимов резания.
Удалить материал вместе с ними?</numerusform>
            <numerusform>Материал «%1» используется в %n наборах режимов резания.
Удалить материал вместе с ними?</numerusform>
        </translation>
    </message>
</context>
<context>
    <name>QObject</name>
    <message>
        <location filename="tool.cpp" line="397"/>
        <source>No valid tool...!!!</source>
        <translation>Инструмент не задан...!!!</translation>
    </message>
    <message>
        <location filename="tool.h" line="129"/>
        <source>Default</source>
        <translation>По умолчанию</translation>
    </message>
</context>
<context>
    <name>Tool</name>
    <message>
        <location filename="tool.cpp" line="276"/>
        <source>Drilling</source>
        <translation>Сверловка</translation>
    </message>
    <message>
        <location filename="tool.cpp" line="277"/>
        <source>Milling</source>
        <translation>Фрезеровка</translation>
    </message>
    <message>
        <location filename="tool.cpp" line="278"/>
        <source>Laser</source>
        <translation>Лазер</translation>
    </message>
    <message>
        <location filename="tool.cpp" line="279"/>
        <source>Threading</source>
        <translation>Резьба</translation>
    </message>
</context>
<context>
    <name>ToolDatabase</name>
    <message>
        <location filename="tooldatabase.ui" line="14"/>
        <source>Tool Database[*]</source>
        <translation>База инструментов[*]</translation>
    </message>
    <message>
        <location filename="tooldatabase.ui" line="35"/>
        <source>New Tool</source>
        <translation>Новый инструмент</translation>
    </message>
    <message>
        <location filename="tooldatabase.ui" line="42"/>
        <source>New Group</source>
        <translation>Новая группа</translation>
    </message>
    <message>
        <location filename="tooldatabase.ui" line="49"/>
        <source>Copy</source>
        <translation>Копировать</translation>
    </message>
    <message>
        <location filename="tooldatabase.ui" line="56"/>
        <source>Delete</source>
        <translation>Удалить</translation>
    </message>
    <message>
        <location filename="tooldatabase.ui" line="63"/>
        <source>Workpiece materials for the cutting data sets</source>
        <translation>Материалы заготовки для наборов режимов резания</translation>
    </message>
    <message>
        <location filename="tooldatabase.ui" line="66"/>
        <source>Materials…</source>
        <translation>Материалы…</translation>
    </message>
    <message>
        <source>Patterns of the automatic tool codes</source>
        <translation type="vanished">Шаблоны автоматических кодов инструментов</translation>
    </message>
    <message>
        <source>Code format…</source>
        <translation type="vanished">Формат кода…</translation>
    </message>
    <message>
        <location filename="tool_database.cpp" line="56"/>
        <source>Invalid tool</source>
        <translation>Недействительный инструмент</translation>
    </message>
</context>
<context>
    <name>ToolEditDialog</name>
    <message>
        <location filename="tooleditdialog.ui" line="20"/>
        <source>Local Tool Edit[*]</source>
        <translation>Редактор инструментов[*]</translation>
    </message>
</context>
<context>
    <name>ToolEditForm</name>
    <message>
        <location filename="tooleditform.ui" line="20"/>
        <source>Tool</source>
        <translation>Инструмент</translation>
    </message>
    <message>
        <location filename="tooleditform.ui" line="41"/>
        <source>Tool Info</source>
        <translation>Информация об инструменте</translation>
    </message>
    <message>
        <source>Drill</source>
        <translation type="vanished">Сверло</translation>
    </message>
    <message>
        <source>End Mill</source>
        <translation type="vanished">Концевая фреза</translation>
    </message>
    <message>
        <source>Engraver</source>
        <translation type="vanished">Гравёр</translation>
    </message>
    <message>
        <source>Laser</source>
        <translation type="vanished">Лазер</translation>
    </message>
    <message>
        <source>Thread Mill</source>
        <translation type="vanished">Резьбофреза</translation>
    </message>
    <message>
        <location filename="tooleditform.ui" line="71"/>
        <source>Name</source>
        <translation>Название</translation>
    </message>
    <message>
        <location filename="tooleditform.ui" line="93"/>
        <source>Tool Type</source>
        <translation>Тип инструмента</translation>
    </message>
    <message>
        <location filename="tooleditform.ui" line="111"/>
        <source>Tool code for the G-code and the sender: letters, digits, &apos;.&apos;, &apos;_&apos;, &apos;-&apos; (up to 20).
Empty -- automatic, by type and geometry (D08L90: drill 0.8, working length 9; the patterns are in tools.json, key &quot;codeFormat&quot;).</source>
        <oldsource>Tool code for the G-code and the sender: letters, digits, &apos;.&apos;, &apos;_&apos;, &apos;-&apos; (up to 20).
Empty -- automatic, by type and geometry (D08L90: drill 0.8, working length 9; the patterns are set by &quot;Code format…&quot; in the tool database).</oldsource>
        <translation>Код инструмента для G-кода и отправителя: латинские буквы, цифры, «.», «_», «-» (до 20).
Пусто — автоматический, по типу и геометрии (D08L90: сверло 0.8, длина рабочей части 9; шаблоны — в tools.json, ключ &quot;codeFormat&quot;).</translation>
    </message>
    <message>
        <location filename="tooleditform.ui" line="115"/>
        <source>Code</source>
        <translation>Код</translation>
    </message>
    <message>
        <location filename="tooleditform.ui" line="125"/>
        <source>Automatic code: follows the type, the geometry and the code format</source>
        <oldsource>Automatic code: follows the type and geometry</oldsource>
        <translation>Автоматический код: следует за типом, геометрией и форматом кода</translation>
    </message>
    <message>
        <location filename="tooleditform.ui" line="153"/>
        <source>Note</source>
        <translation>Примечания</translation>
    </message>
    <message>
        <location filename="tooleditform.ui" line="193"/>
        <source>Geometry</source>
        <translation>Геометрия</translation>
    </message>
    <message>
        <location filename="tooleditform.ui" line="214"/>
        <source>Units</source>
        <translation>Единицы</translation>
    </message>
    <message>
        <location filename="tooleditform.ui" line="230"/>
        <source>mm</source>
        <translation>мм</translation>
    </message>
    <message>
        <location filename="tooleditform.ui" line="235"/>
        <source>inches</source>
        <translation>дюймы</translation>
    </message>
    <message>
        <location filename="tooleditform.ui" line="247"/>
        <source>mm/sec</source>
        <translation>мм/сек</translation>
    </message>
    <message>
        <location filename="tooleditform.ui" line="252"/>
        <source>mm/min</source>
        <translation>мм/мин</translation>
    </message>
    <message>
        <location filename="tooleditform.ui" line="257"/>
        <source>cm/min</source>
        <translation>см/мин</translation>
    </message>
    <message>
        <location filename="tooleditform.ui" line="262"/>
        <source>m/min</source>
        <translation>м/мин</translation>
    </message>
    <message>
        <location filename="tooleditform.ui" line="272"/>
        <source>Diameter</source>
        <translation>Диаметр</translation>
    </message>
    <message>
        <location filename="tooleditform.ui" line="279"/>
        <location filename="tooleditform.ui" line="322"/>
        <location filename="tooleditform.ui" line="338"/>
        <location filename="tooleditform.ui" line="454"/>
        <location filename="tooleditform.ui" line="477"/>
        <location filename="tooleditform.ui" line="500"/>
        <location filename="tooleditform.ui" line="548"/>
        <location filename="tooleditform.ui" line="603"/>
        <source> mm</source>
        <translation> мм</translation>
    </message>
    <message>
        <location filename="tooleditform.ui" line="295"/>
        <source>Angle</source>
        <translation>Угол</translation>
    </message>
    <message>
        <location filename="tooleditform.ui" line="315"/>
        <source>Working length:</source>
        <translation>Длина рабочей части:</translation>
    </message>
    <message>
        <location filename="tooleditform.ui" line="348"/>
        <source>M</source>
        <translation>M</translation>
    </message>
    <message>
        <location filename="tooleditform.ui" line="373"/>
        <source>Cutting data:</source>
        <translation>Режимы резания:</translation>
    </message>
    <message>
        <location filename="tooleditform.ui" line="386"/>
        <source>Cutting data shown in the fields below: the tool&apos;s own values or a set for an operation and material.
The fields edit the shown set; the geometry always belongs to the tool.</source>
        <translation>Режимы резания в полях ниже: собственные значения инструмента или набор для операции и материала.
Поля правят показанный набор; геометрия всегда принадлежит инструменту.</translation>
    </message>
    <message>
        <location filename="tooleditform.ui" line="394"/>
        <source>Add cutting data for an operation and material (copies the values shown)</source>
        <translation>Добавить режимы для операции и материала (копия показанных значений)</translation>
    </message>
    <message>
        <location filename="tooleditform.ui" line="404"/>
        <source>Delete the shown cutting data</source>
        <translation>Удалить показанные режимы</translation>
    </message>
    <message>
        <location filename="tooleditform.ui" line="441"/>
        <location filename="tooleditform.ui" line="451"/>
        <source>Depth of one pass. For the G83/G73 cycles it is the peck depth Q.</source>
        <translation>Глубина одного прохода. Для циклов G83/G73 — глубина клевка Q.</translation>
    </message>
    <message>
        <location filename="tooleditform.ui" line="444"/>
        <source>Pass (Ap)</source>
        <translation>Проход (Ap)</translation>
    </message>
    <message>
        <location filename="tooleditform.ui" line="470"/>
        <source>Thread Pitch</source>
        <translation>Шаг резьбы</translation>
    </message>
    <message>
        <location filename="tooleditform.ui" line="493"/>
        <source>Depth (Ap)</source>
        <translation>Глубина (Ap)</translation>
    </message>
    <message>
        <location filename="tooleditform.ui" line="525"/>
        <source>Stepover (Ae)</source>
        <translation>Шаг в сторону (Ae)</translation>
    </message>
    <message>
        <location filename="tooleditform.ui" line="582"/>
        <source>One-turn Cut (Fz...)</source>
        <translation>Съём за оборот (Fz...)</translation>
    </message>
    <message>
        <source>Vc</source>
        <translation type="vanished">Vc</translation>
    </message>
    <message>
        <location filename="tooleditform.ui" line="637"/>
        <source>Drill cycle:</source>
        <translation>Цикл сверления:</translation>
    </message>
    <message>
        <location filename="tooleditform.ui" line="644"/>
        <source>How the holes are drilled: a canned cycle or plain G0/G1 moves.
Auto -- chosen by the post processor.</source>
        <translation>Как сверлятся отверстия: постоянным циклом или простыми перемещениями G0/G1.
Авто — выбирает постпроцессор.</translation>
    </message>
    <message>
        <location filename="tooleditform.ui" line="652"/>
        <source>Dwell, s:</source>
        <translation>Выдержка, с:</translation>
    </message>
    <message>
        <location filename="tooleditform.ui" line="659"/>
        <source>Dwell at the bottom of the hole (G82)</source>
        <translation>Выдержка на дне отверстия (G82)</translation>
    </message>
    <message>
        <location filename="tooleditform.ui" line="675"/>
        <source>Between holes return to the R plane (G99) instead of the initial plane (G98)</source>
        <translation>Между отверстиями — отвод на плоскость R (G99), а не на исходную (G98)</translation>
    </message>
    <message>
        <location filename="tooleditform.ui" line="678"/>
        <source>Retract to R (G99)</source>
        <translation>Отвод на R (G99)</translation>
    </message>
    <message>
        <location filename="tooleditform.ui" line="715"/>
        <source>Spindle Speed (N)</source>
        <translation>Обороты шпинделя (N)</translation>
    </message>
    <message>
        <location filename="tooleditform.ui" line="747"/>
        <source>Feed Rate (Vf)</source>
        <translation>Подача (Vf)</translation>
    </message>
    <message>
        <source>Side Angle</source>
        <translation type="vanished">Угол заточки</translation>
    </message>
    <message>
        <location filename="tooleditform.ui" line="302"/>
        <source> °</source>
        <translation> °</translation>
    </message>
    <message>
        <source>Lenght:</source>
        <translation type="vanished">Длина:</translation>
    </message>
    <message>
        <source>Hole ∅</source>
        <translation type="vanished">Отверстие ∅</translation>
    </message>
    <message>
        <location filename="tooleditform.ui" line="420"/>
        <source>Cutting Parameters</source>
        <translation>Параметры резания</translation>
    </message>
    <message>
        <source>Pass or Depth</source>
        <translation type="vanished">Проход или глубина</translation>
    </message>
    <message>
        <source>Stepover</source>
        <translation type="vanished">Шаг перекрытия</translation>
    </message>
    <message>
        <location filename="tooleditform.ui" line="564"/>
        <location filename="tooleditform.ui" line="619"/>
        <source> %</source>
        <translation> %</translation>
    </message>
    <message>
        <source>One-turn Cut</source>
        <translation type="vanished">Рез за один оборот</translation>
    </message>
    <message>
        <location filename="tooleditform.ui" line="691"/>
        <source>Feed Speeds</source>
        <translation>Скорости подачи</translation>
    </message>
    <message>
        <source>Spindle Speed</source>
        <translation type="vanished">Скорость шпинделя</translation>
    </message>
    <message>
        <location filename="tooleditform.ui" line="722"/>
        <source> r.p.m.</source>
        <translation> об/мин</translation>
    </message>
    <message>
        <location filename="tooleditform.ui" line="738"/>
        <location filename="tooleditform.ui" line="770"/>
        <source>If checked then automatic calculate!</source>
        <translation>Если отмечено — автоматический расчёт!</translation>
    </message>
    <message>
        <source>Feed Rate</source>
        <translation type="vanished">Скорость подачи</translation>
    </message>
    <message>
        <location filename="tooleditform.ui" line="754"/>
        <location filename="tooleditform.ui" line="783"/>
        <source> mm/sec</source>
        <translation> мм/сек</translation>
    </message>
    <message>
        <location filename="tooleditform.ui" line="776"/>
        <source>Plunge Rate</source>
        <translation>Скорость врезания</translation>
    </message>
    <message>
        <location filename="tooleditform.ui" line="799"/>
        <source>Checked -- M4: the laser power follows the actual speed, the exposure stays even at corners, accelerations and decelerations.
Unchecked -- M3: constant power, the same S at any speed: corners and slow segments get more exposure.</source>
        <translation>Галка стоит — M4: мощность лазера следует за фактической скоростью, засветка остаётся равномерной в углах, на разгонах и торможениях.
Без галки — M3: постоянная мощность, одно и то же S при любой скорости: углы и медленные участки засвечиваются сильнее.</translation>
    </message>
    <message>
        <location filename="tooleditform.ui" line="803"/>
        <source>Dynamic power (M4)</source>
        <translation>Динамическая мощность (M4)</translation>
    </message>
    <message>
        <location filename="tooleditform.ui" line="816"/>
        <source>Apply</source>
        <translation>Применить</translation>
    </message>
    <message>
        <location filename="tool_editform.cpp" line="178"/>
        <source>If the offset value is more than 50%, unmilled areas are possible.
These errors do not appear in the visualization.</source>
        <comment>При значении отступа более 50% возможны не отфрезерованные участки. Эти ошибки не отображаются в визуализации.</comment>
        <translation>При значении отступа более 50% возможны не отфрезерованные участки.
Эти ошибки не отображаются в визуализации.</translation>
    </message>
    <message>
        <source>Pass</source>
        <translation type="vanished">Проход</translation>
    </message>
    <message>
        <source>Depth</source>
        <translation type="vanished">Глубина</translation>
    </message>
    <message>
        <location filename="tool_editform.cpp" line="132"/>
        <source>Auto</source>
        <translation>Авто</translation>
    </message>
    <message>
        <location filename="tool_editform.cpp" line="133"/>
        <source>Expand (G0/G1)</source>
        <translation>Развернуть (G0/G1)</translation>
    </message>
    <message>
        <location filename="tool_editform.cpp" line="134"/>
        <source>G81</source>
        <translation>G81</translation>
    </message>
    <message>
        <location filename="tool_editform.cpp" line="135"/>
        <source>G82 dwell</source>
        <translation>G82 с выдержкой</translation>
    </message>
    <message>
        <location filename="tool_editform.cpp" line="136"/>
        <source>G83 peck</source>
        <translation>G83 клевки</translation>
    </message>
    <message>
        <location filename="tool_editform.cpp" line="137"/>
        <source>G73 chip break</source>
        <translation>G73 ломка стружки</translation>
    </message>
    <message>
        <location filename="tool_editform.cpp" line="429"/>
        <source>Laser Power (S)</source>
        <translation>Мощность лазера (S)</translation>
    </message>
    <message>
        <location filename="tool_editform.cpp" line="437"/>
        <source>Tool defaults</source>
        <translation>По умолчанию</translation>
    </message>
    <message>
        <location filename="tool_editform.cpp" line="526"/>
        <location filename="tool_editform.cpp" line="548"/>
        <source>New cutting data</source>
        <translation>Новые режимы резания</translation>
    </message>
    <message>
        <location filename="tool_editform.cpp" line="536"/>
        <source>Operation:</source>
        <translation>Операция:</translation>
    </message>
    <message>
        <location filename="tool_editform.cpp" line="537"/>
        <source>Material:</source>
        <translation>Материал:</translation>
    </message>
    <message>
        <location filename="tool_editform.cpp" line="549"/>
        <source>Cutting data for &quot;%1&quot; already exist.</source>
        <translation>Режимы для «%1» уже есть.</translation>
    </message>
    <message>
        <source>Same code as: %1</source>
        <translation type="vanished">Такой же код у: %1</translation>
    </message>
    <message>
        <source>Duplicate tool code</source>
        <translation type="vanished">Повтор кода инструмента</translation>
    </message>
    <message>
        <source>The code &quot;%1&quot; is also used by:
%2

The code goes into the G-code, and the sender finds the tool by it.</source>
        <translation type="vanished">Код «%1» есть и у:
%2

Код попадает в G-код, по нему отправитель находит инструмент.</translation>
    </message>
    <message>
        <location filename="tool_editform.cpp" line="683"/>
        <source>Cutting data</source>
        <translation>Режимы резания</translation>
    </message>
    <message>
        <location filename="tool_editform.cpp" line="684"/>
        <source>The cutting data &quot;%1&quot; have a zero feed, pass depth or stepover. Fill them in before applying.</source>
        <translation>В режимах «%1» нулевая подача, глубина прохода или шаг. Заполните их перед применением.</translation>
    </message>
    <message>
        <location filename="tool_editform.cpp" line="707"/>
        <source>End Mill (Ø%1 mm)</source>
        <translation>Концевая фреза (Ø%1 мм)</translation>
    </message>
    <message>
        <location filename="tool_editform.cpp" line="708"/>
        <source>Engrave (%2° %1 mm tip)</source>
        <translation>Гравёр (%2° %1 мм кончик)</translation>
    </message>
    <message>
        <location filename="tool_editform.cpp" line="709"/>
        <source>Drill (Ø%1 mm)</source>
        <translation>Сверло (Ø%1 мм)</translation>
    </message>
    <message>
        <location filename="tool_editform.cpp" line="710"/>
        <source>Laser (Ø%1 mm)</source>
        <translation>Лазер (Ø%1 мм)</translation>
    </message>
    <message>
        <location filename="tool_editform.cpp" line="711"/>
        <source>Thread (Ø%1 mm)</source>
        <translation>Резьба (Ø%1 мм)</translation>
    </message>
</context>
<context>
    <name>ToolHolder</name>
    <message>
        <location filename="tool.cpp" line="654"/>
        <source>unknown (%1)</source>
        <translation>неизвестный (%1)</translation>
    </message>
    <message>
        <location filename="tool.cpp" line="659"/>
        <location filename="tool_editform.cpp" line="533"/>
        <source>Any material</source>
        <translation>Любой материал</translation>
    </message>
    <message>
        <location filename="tool.cpp" line="673"/>
        <source>FR4</source>
        <translation>FR4</translation>
    </message>
    <message>
        <location filename="tool.cpp" line="674"/>
        <source>Aluminium</source>
        <translation>Алюминий</translation>
    </message>
    <message>
        <location filename="tool.cpp" line="675"/>
        <source>Stainless steel</source>
        <translation>Нержавеющая сталь</translation>
    </message>
    <message>
        <location filename="tool.cpp" line="676"/>
        <source>Film</source>
        <translation>Плёнка</translation>
    </message>
    <message>
        <location filename="tool.cpp" line="677"/>
        <source>Plastic</source>
        <translation>Пластик</translation>
    </message>
    <message>
        <location filename="tool.cpp" line="678"/>
        <source>Photoresist</source>
        <translation>Фоторезист</translation>
    </message>
    <message>
        <location filename="tool.cpp" line="679"/>
        <source>Solder mask</source>
        <translation>Паяльная маска</translation>
    </message>
</context>
<context>
    <name>ToolModel</name>
    <message>
        <location filename="tool_model.cpp" line="146"/>
        <source>Name|Note|Id</source>
        <translation>Название|Примечания|Id</translation>
    </message>
</context>
<context>
    <name>ToolSelectorForm</name>
    <message>
        <location filename="tool_selectorform.cpp" line="162"/>
        <source>Edited for this toolpath only (the tool database is not changed)</source>
        <translation>Изменён только для этой УП (база инструментов не меняется)</translation>
    </message>
    <message>
        <location filename="tool_selectorform.cpp" line="164"/>
        <source>Cutting data: %1</source>
        <translation>Режимы: %1</translation>
    </message>
    <message>
        <location filename="tool_selectorform.cpp" line="166"/>
        <source>Cutting data: tool defaults</source>
        <translation>Режимы: значения инструмента по умолчанию</translation>
    </message>
    <message>
        <location filename="tool_selectorform.cpp" line="176"/>
        <source>Project material (%1)</source>
        <translation>Материал проекта (%1)</translation>
    </message>
    <message>
        <location filename="tool_selectorform.cpp" line="176"/>
        <source>not set</source>
        <translation>не задан</translation>
    </message>
    <message>
        <location filename="tool_selectorform.cpp" line="243"/>
        <location filename="tool_selectorform.cpp" line="319"/>
        <source>Tool:</source>
        <translation>Инструмент:</translation>
    </message>
    <message>
        <location filename="tool_selectorform.cpp" line="244"/>
        <location filename="tool_selectorform.cpp" line="322"/>
        <source>Material:</source>
        <translation>Материал:</translation>
    </message>
    <message>
        <location filename="tool_selectorform.cpp" line="315"/>
        <source>Form</source>
        <translation>Форма</translation>
    </message>
    <message>
        <location filename="tool_selectorform.cpp" line="317"/>
        <source>Tool %1:</source>
        <translation>Инструмент %1:</translation>
    </message>
    <message>
        <location filename="tool_selectorform.cpp" line="320"/>
        <source>Edit</source>
        <translation>Редактировать</translation>
    </message>
    <message>
        <location filename="tool_selectorform.cpp" line="321"/>
        <source>Select</source>
        <translation>Выбрать</translation>
    </message>
</context>
<context>
    <name>ToolTreeView</name>
    <message>
        <location filename="tool_treeview.cpp" line="63"/>
        <source>New Group</source>
        <translation>Новая группа</translation>
    </message>
    <message>
        <location filename="tool_treeview.cpp" line="83"/>
        <source>New Tool </source>
        <translation>Новый инструмент </translation>
    </message>
    <message>
        <location filename="tool_treeview.cpp" line="90"/>
        <source>Warning</source>
        <translation>Предупреждение</translation>
    </message>
    <message>
        <location filename="tool_treeview.cpp" line="90"/>
        <source>Are you sure you want to delete the item and all content?</source>
        <translation>Вы уверены, что хотите удалить элемент и всё его содержимое?</translation>
    </message>
</context>
</TS>
