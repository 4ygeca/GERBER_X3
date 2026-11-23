<?xml version="1.0" encoding="utf-8"?>
<!DOCTYPE TS>
<TS version="2.1" language="ru_RU" sourcelanguage="en_US">
<context>
    <name>DrillForm</name>
    <message>
        <location filename="drillform.ui" line="20"/>
        <source>Form</source>
        <translation>Форма</translation>
    </message>
    <message>
        <location filename="drillform.ui" line="44"/>
        <source>Zoom To Selected</source>
        <translation>Увеличить до выбранных</translation>
    </message>
    <message>
        <source>Work Type</source>
        <translation type="vanished">Тип работы</translation>
    </message>
    <message>
        <source>Only drilling</source>
        <translation type="vanished">Только сверловка</translation>
    </message>
    <message>
        <source>Pocket</source>
        <translation type="vanished">Карман</translation>
    </message>
    <message>
        <source>Profile</source>
        <translation type="vanished">Профиль</translation>
    </message>
    <message>
        <source>Aligin</source>
        <translation type="vanished">Расположение</translation>
    </message>
    <message>
        <source>On</source>
        <translation type="vanished">Поверху</translation>
    </message>
    <message>
        <source>In</source>
        <translation type="vanished">Внутри</translation>
    </message>
    <message>
        <source>Out</source>
        <translation type="vanished">Снаружи</translation>
    </message>
    <message>
        <location filename="drillform.ui" line="57"/>
        <source>File:</source>
        <translation>Файл:</translation>
    </message>
    <message>
        <location filename="drillform.ui" line="67"/>
        <source>Pick the tools of the drilling rows again by the hole diameter. Only a tool whose working length reaches the drilling depth fits (a drill together with its point); among those a drill goes before an end mill, then the exact diameter, then the order in the tool database.
Tools chosen or edited by hand are replaced. Pocket and profile rows and rows without a suitable tool stay as they are.</source>
        <translation>Заново подобрать инструменты строк сверловки по диаметру отверстия. Подходит только инструмент, рабочая длина которого достаёт до глубины сверления (у сверла — вместе с заточкой); из них сверло идёт раньше фрезы, затем точный диаметр, затем порядок в базе инструментов.
Выбранные или изменённые вручную инструменты заменяются. Строки кармана и профиля, а также строки без подходящего инструмента остаются как есть.</translation>
    </message>
    <message>
        <location filename="drillform.ui" line="71"/>
        <source>Pick Up Tools</source>
        <translation>Подобрать инструменты</translation>
    </message>
    <message>
        <location filename="drillform.ui" line="84"/>
        <source>Material:</source>
        <translation>Материал:</translation>
    </message>
    <message>
        <location filename="drillform.ui" line="94"/>
        <source>Material for the cutting data of the rows: the project material (G-Code Properties) or the one chosen here.
A tool edited for this toolpath only keeps its own values.</source>
        <translation>Материал для режимов резания строк: материал проекта («Свойства G-кода») или выбранный здесь.
Инструмент, изменённый только для этой УП, сохраняет свои значения.</translation>
    </message>
    <message>
        <source>Direction</source>
        <translation type="vanished">Направление</translation>
    </message>
    <message>
        <source>Climb</source>
        <translation type="vanished">Попутное</translation>
    </message>
    <message>
        <source>Conventional</source>
        <translation type="vanished">Встречное</translation>
    </message>
</context>
<context>
    <name>DrillPlugin::Form</name>
    <message>
        <source>Drill Toolpath</source>
        <translation type="vanished">Сверловка</translation>
    </message>
    <message>
        <source>No data to process.</source>
        <translation type="vanished">Нет данных для расчёта.</translation>
    </message>
    <message>
        <source>&amp;Select Tool</source>
        <translation type="vanished">&amp;Выбрать инструмент</translation>
    </message>
    <message>
        <source>&quot; not suitable for T</source>
        <translation type="vanished">&quot; не подходит для Т</translation>
    </message>
    <message>
        <source>&amp;Remove Tool</source>
        <translation type="vanished">&amp;Убрать инструмент</translation>
    </message>
    <message>
        <source>&amp;Choose a Tool for everyone</source>
        <translation type="vanished">&amp;Выбрать инструмент для всех</translation>
    </message>
    <message>
        <source>&amp;Remove Tool for everyone</source>
        <translation type="vanished">&amp;Удалить инструмент для всех</translation>
    </message>
</context>
<context>
    <name>DrillPlugin::Model</name>
    <message>
        <source>Tool</source>
        <translation type="vanished">Инструмент</translation>
    </message>
    <message>
        <source>Slot</source>
        <translation type="vanished">Слот</translation>
    </message>
    <message>
        <source>Select Tool</source>
        <translation type="vanished">Выбрать инструмент</translation>
    </message>
    <message>
        <source>Aperture</source>
        <translation type="vanished">Апертура</translation>
    </message>
</context>
<context>
    <name>Drilling::Form</name>
    <message>
        <location filename="drill_form.cpp" line="273"/>
        <source>Group:</source>
        <translation>Группа:</translation>
    </message>
    <message>
        <location filename="drill_form.cpp" line="274"/>
        <source>No group</source>
        <translation>Без группы</translation>
    </message>
    <message>
        <location filename="drill_form.cpp" line="275"/>
        <source>Group (folder in the toolpath tree) for all toolpaths of this Create.
Toolpaths of the same file, mode and tool already in this group are updated in place,
a new name makes a new group, an empty one puts the toolpaths outside groups.</source>
        <translation>Группа (папка в дереве УП) для всех УП этого «Создать».
УП того же файла, режима и инструмента, уже лежащие в этой группе, обновляются на месте,
новое имя создаёт новую группу, пустое — кладёт УП вне групп.</translation>
    </message>
    <message>
        <location filename="drill_form.cpp" line="301"/>
        <source>GCType::Drill Toolpath</source>
        <translation>УП сверловки</translation>
    </message>
    <message>
        <location filename="drill_form.cpp" line="558"/>
        <source>Project material (%1)</source>
        <translation>Материал проекта (%1)</translation>
    </message>
    <message>
        <location filename="drill_form.cpp" line="558"/>
        <source>not set</source>
        <translation>не задан</translation>
    </message>
    <message>
        <location filename="drill_form.cpp" line="607"/>
        <source>How to process these holes. If the row is selected, the choice applies to all selected rows.</source>
        <translation>Как обрабатывать эти отверстия. Если строка выделена, выбор применяется ко всем выделенным строкам.</translation>
    </message>
    <message>
        <location filename="drill_form.cpp" line="654"/>
        <source>A drill only drills: to mill these holes, choose an end mill</source>
        <translation>Сверло только сверлит: чтобы фрезеровать эти отверстия, выберите фрезу</translation>
    </message>
    <message>
        <location filename="drill_form.cpp" line="655"/>
        <source>A laser does not drill: to drill these holes, choose a drill or an end mill</source>
        <translation>Лазер не сверлит: чтобы сверлить эти отверстия, выберите сверло или фрезу</translation>
    </message>
    <message>
        <location filename="drill_form.cpp" line="717"/>
        <source>&quot;%1&quot; is not suitable for: %2</source>
        <translation>«%1» не подходит для: %2</translation>
    </message>
    <message>
        <location filename="drill_form.cpp" line="747"/>
        <source>Edit tool for this toolpath only (the tool database is not changed)</source>
        <translation>Изменить инструмент только для этой УП (база инструментов не меняется)</translation>
    </message>
    <message>
        <location filename="drill_form.cpp" line="867"/>
        <source>&amp;Mode</source>
        <translation>&amp;Режим</translation>
    </message>
    <message>
        <location filename="drill_form.cpp" line="871"/>
        <source>&amp;Select Tool</source>
        <translation>&amp;Выбрать инструмент</translation>
    </message>
    <message>
        <source>&quot; not suitable for Tu</source>
        <translation type="vanished">» не подходит для Tu</translation>
    </message>
    <message>
        <location filename="drill_form.cpp" line="895"/>
        <source>&amp;Edit tool for this toolpath…</source>
        <translation>&amp;Изменить инструмент для этой УП…</translation>
    </message>
    <message>
        <location filename="drill_form.cpp" line="905"/>
        <source>Reset to &amp;database tool</source>
        <translation>Вернуть инструмент из &amp;базы</translation>
    </message>
    <message>
        <location filename="drill_form.cpp" line="916"/>
        <source>&amp;Remove Tool</source>
        <translation>&amp;Убрать инструмент</translation>
    </message>
    <message>
        <location filename="drill_form.cpp" line="936"/>
        <source>&amp;Mode for everyone</source>
        <translation>&amp;Режим для всех</translation>
    </message>
    <message>
        <location filename="drill_form.cpp" line="938"/>
        <source>&amp;Choose a Tool for everyone</source>
        <translation>&amp;Выбрать инструмент для всех</translation>
    </message>
    <message>
        <location filename="drill_form.cpp" line="947"/>
        <source>&amp;Remove Tool for everyone</source>
        <translation>&amp;Удалить инструмент для всех</translation>
    </message>
</context>
<context>
    <name>Drilling::Model</name>
    <message>
        <location filename="drill_model.cpp" line="244"/>
        <source>Select Tool</source>
        <translation>Выбрать инструмент</translation>
    </message>
    <message>
        <location filename="drill_model.cpp" line="270"/>
        <source>Edited for this toolpath only (the tool database is not changed)</source>
        <translation>Изменён только для этой УП (база инструментов не меняется)</translation>
    </message>
    <message>
        <location filename="drill_model.cpp" line="275"/>
        <source>Cutting data: %1</source>
        <translation>Режимы: %1</translation>
    </message>
    <message>
        <location filename="drill_model.cpp" line="277"/>
        <source>Cutting data: tool defaults</source>
        <translation>Режимы: значения инструмента по умолчанию</translation>
    </message>
    <message>
        <location filename="drill_model.cpp" line="300"/>
        <source>Aperture</source>
        <translation>Апертура</translation>
    </message>
    <message>
        <location filename="drill_model.cpp" line="300"/>
        <location filename="drill_model.cpp" line="303"/>
        <source>Tool</source>
        <translation>Инструмент</translation>
    </message>
    <message>
        <location filename="drill_model.cpp" line="301"/>
        <source>Mode</source>
        <translation>Режим</translation>
    </message>
    <message>
        <location filename="drill_model.cpp" line="332"/>
        <source>Drilling</source>
        <translation>Сверловка</translation>
    </message>
    <message>
        <location filename="drill_model.cpp" line="333"/>
        <source>Pocket · climb</source>
        <translation>Карман · попутное</translation>
    </message>
    <message>
        <location filename="drill_model.cpp" line="334"/>
        <source>Pocket · conventional</source>
        <translation>Карман · встречное</translation>
    </message>
    <message>
        <location filename="drill_model.cpp" line="335"/>
        <source>Profile · climb</source>
        <translation>Профиль · попутное</translation>
    </message>
    <message>
        <location filename="drill_model.cpp" line="336"/>
        <source>Profile · conventional</source>
        <translation>Профиль · встречное</translation>
    </message>
</context>
</TS>
