<?xml version="1.0" encoding="utf-8"?>
<!DOCTYPE TS>
<TS version="2.1" language="ru_RU">
<context>
    <name>Dialog</name>
    <message>
        <location filename="gbrcomp_dialog.cpp" line="82"/>
        <source>Dialog</source>
        <translation>Диалог</translation>
    </message>
</context>
<context>
    <name>Gerber::EtchDialog</name>
    <message>
        <location filename="gbr_etchdialog.cpp" line="101"/>
        <source>unknown error</source>
        <translation>неизвестная ошибка</translation>
    </message>
    <message>
        <location filename="gbr_etchdialog.cpp" line="137"/>
        <source>Etch Compensation</source>
        <translation>Компенсация подтрава</translation>
    </message>
    <message>
        <location filename="gbr_etchdialog.cpp" line="168"/>
        <source> mm</source>
        <translation> мм</translation>
    </message>
    <message>
        <location filename="gbr_etchdialog.cpp" line="171"/>
        <source>Undercut compensation: every copper edge moves outward by this value.
Tracks and pads grow by twice the offset, gaps between them shrink by twice the offset.
KiCad&apos;s PostScript &quot;track width correction&quot; is the full width change, i.e. twice this value.
Applied in file coordinates, before the affine transform; 0 means no compensation.</source>
        <translation>Компенсация подтрава: каждая кромка меди сдвигается наружу на это значение.
Дорожки и площадки становятся шире на удвоенное смещение, зазоры между ними — уже на столько же.
«Коррекция ширины дорожек» PostScript в KiCad — полное изменение ширины, то есть удвоенное это значение.
Применяется в координатах файла, до аффинного преобразования; 0 — без компенсации.</translation>
    </message>
    <message>
        <location filename="gbr_etchdialog.cpp" line="177"/>
        <source>Unattributed objects are copper</source>
        <translation>Объекты без атрибутов — медь</translation>
    </message>
    <message>
        <location filename="gbr_etchdialog.cpp" line="179"/>
        <source>Dark objects without an X2 aperture function (%TA.AperFunction) and outside any net (%TO.N).
Turn it off if text and logos share apertures with tracks, then pick copper classes in the table.</source>
        <translation>Тёмные объекты без X2-функции апертуры (%TA.AperFunction) и вне цепей (%TO.N).
Выключите, если текст и логотипы нарисованы теми же апертурами, что и дорожки, и отметьте классы меди в таблице.</translation>
    </message>
    <message>
        <location filename="gbr_etchdialog.cpp" line="183"/>
        <source>Auto</source>
        <translation>Авто</translation>
    </message>
    <message>
        <location filename="gbr_etchdialog.cpp" line="184"/>
        <source>Keep exact size</source>
        <translation>Сохранять размер</translation>
    </message>
    <message>
        <location filename="gbr_etchdialog.cpp" line="185"/>
        <source>Shrink with copper</source>
        <translation>Сужать вместе с медью</translation>
    </message>
    <message>
        <location filename="gbr_etchdialog.cpp" line="187"/>
        <source>Clear objects (%LPC) without an X2 aperture function.
Auto: exact size if the file has X2 attributes (KiCad drill marks),
otherwise they are voids in copper and shrink with it.</source>
        <translation>Вырезы (%LPC) без X2-функции апертуры.
Авто: точный размер, если у файла есть X2-атрибуты (метки сверловки KiCad),
иначе это пустоты в меди, и они сужаются вместе с ней.</translation>
    </message>
    <message>
        <location filename="gbr_etchdialog.cpp" line="192"/>
        <source>Keep aperture holes exact size</source>
        <translation>Не сужать отверстия апертур</translation>
    </message>
    <message>
        <location filename="gbr_etchdialog.cpp" line="194"/>
        <source>Holes of flashed apertures (e.g. %ADD10C,1.6X0.8*%) keep their size
where nothing else covers them.</source>
        <translation>Отверстия вспышек апертур (например, %ADD10C,1.6X0.8*%) сохраняют размер
там, где их ничто не накрывает.</translation>
    </message>
    <message>
        <location filename="gbr_etchdialog.cpp" line="203"/>
        <source> (not a copper layer)</source>
        <translation> (не слой меди)</translation>
    </message>
    <message>
        <location filename="gbr_etchdialog.cpp" line="204"/>
        <source>&lt;b&gt;%1&lt;/b&gt;: X2 aperture functions: %2; nets: %3; file function: %4</source>
        <translation>&lt;b&gt;%1&lt;/b&gt;: X2-функции апертур: %2; цепей: %3; функция файла: %4</translation>
    </message>
    <message>
        <location filename="gbr_etchdialog.cpp" line="206"/>
        <source>yes</source>
        <translation>есть</translation>
    </message>
    <message>
        <location filename="gbr_etchdialog.cpp" line="206"/>
        <source>no</source>
        <translation>нет</translation>
    </message>
    <message>
        <location filename="gbr_etchdialog.cpp" line="218"/>
        <source>Class</source>
        <translation>Класс</translation>
    </message>
    <message>
        <location filename="gbr_etchdialog.cpp" line="218"/>
        <source>Function</source>
        <translation>Функция</translation>
    </message>
    <message>
        <location filename="gbr_etchdialog.cpp" line="218"/>
        <source>Objects</source>
        <translation>Объекты</translation>
    </message>
    <message>
        <location filename="gbr_etchdialog.cpp" line="218"/>
        <source>By rules</source>
        <translation>По правилам</translation>
    </message>
    <message>
        <location filename="gbr_etchdialog.cpp" line="226"/>
        <source>Checked classes are compensated as copper; for clear objects the void shrinks with the copper.
Bold: chosen manually; the &quot;By rules&quot; column shows the automatic choice.</source>
        <translation>Отмеченные классы компенсируются как медь; у вырезов пустота сужается вместе с медью.
Жирным — выбрано вручную; колонка «По правилам» показывает автоматический выбор.</translation>
    </message>
    <message>
        <location filename="gbr_etchdialog.cpp" line="228"/>
        <source>Selected rows are highlighted on the scene, double click zooms to them.</source>
        <translation>Выбранные строки подсвечены на сцене, двойной щелчок — масштаб к ним.</translation>
    </message>
    <message>
        <location filename="gbr_etchdialog.cpp" line="238"/>
        <source>Reset to rules</source>
        <translation>Сбросить к правилам</translation>
    </message>
    <message>
        <location filename="gbr_etchdialog.cpp" line="243"/>
        <source>Exclude selected</source>
        <translation>Исключить выделенное</translation>
    </message>
    <message>
        <location filename="gbr_etchdialog.cpp" line="244"/>
        <source>Objects lying in the copper bodies (Normal view) or on the aperture paths
selected on the scene are never compensated, whatever their class says.
Select them on the scene before opening this dialog.</source>
        <translation>Объекты в телах меди (вид «Нормальный») или на осевых апертур,
выделенных на сцене, не компенсируются, что бы ни решил их класс.
Выделите их на сцене до открытия этого диалога.</translation>
    </message>
    <message>
        <location filename="gbr_etchdialog.cpp" line="250"/>
        <source>Clear exclusions</source>
        <translation>Снять исключения</translation>
    </message>
    <message>
        <location filename="gbr_etchdialog.cpp" line="251"/>
        <source>Excluded objects follow their classes again.</source>
        <translation>Исключённые объекты снова следуют своим классам.</translation>
    </message>
    <message>
        <location filename="gbr_etchdialog.cpp" line="255"/>
        <source>Toolpaths created earlier are not recalculated.</source>
        <translation>Созданные ранее УП не пересчитываются.</translation>
    </message>
    <message>
        <location filename="gbr_etchdialog.cpp" line="263"/>
        <source>Short circuits: %1</source>
        <translation>Замыканий: %1</translation>
    </message>
    <message>
        <location filename="gbr_etchdialog.cpp" line="265"/>
        <source>The files have no nets (X2 %TO.N): short circuits are not checked.</source>
        <translation>В файлах нет цепей (X2 %TO.N): замыкания не проверяются.</translation>
    </message>
    <message>
        <location filename="gbr_etchdialog.cpp" line="275"/>
        <source>Offset per edge:</source>
        <translation>Смещение кромки:</translation>
    </message>
    <message>
        <location filename="gbr_etchdialog.cpp" line="277"/>
        <source>Clear objects without function:</source>
        <translation>Вырезы без функции:</translation>
    </message>
    <message>
        <location filename="gbr_etchdialog.cpp" line="337"/>
        <source>Regions</source>
        <translation>Области</translation>
    </message>
    <message>
        <location filename="gbr_etchdialog.cpp" line="342"/>
        <source>Regions without an aperture function in a file with X2 attributes:
usually KiCad text in a TrueType font or knockout text,
which KiCad plots without an aperture function.</source>
        <translation>Области без функции апертуры в файле с атрибутами X2:
обычно это текст KiCad шрифтом TrueType или инверсный текст,
которые KiCad выводит без функции апертуры.</translation>
    </message>
    <message>
        <location filename="gbr_etchdialog.cpp" line="353"/>
        <source>%1 (%2 clear)</source>
        <translation>%1 (вырезов: %2)</translation>
    </message>
    <message>
        <location filename="gbr_etchdialog.cpp" line="362"/>
        <source>Excluded on scene</source>
        <translation>Исключены на сцене</translation>
    </message>
    <message>
        <location filename="gbr_etchdialog.cpp" line="363"/>
        <source>Objects excluded with &quot;Exclude selected&quot;: never compensated.</source>
        <translation>Объекты, исключённые кнопкой «Исключить выделенное»: не компенсируются никогда.</translation>
    </message>
    <message>
        <location filename="gbr_etchdialog.cpp" line="391"/>
        <source>copper</source>
        <translation>медь</translation>
    </message>
    <message>
        <location filename="gbr_etchdialog.cpp" line="619"/>
        <source>Nothing to exclude: no objects of these files lie in the bodies or on the aperture paths selected on the scene.
Select them on the scene before opening this dialog.</source>
        <translation>Исключать нечего: в выделенных на сцене телах и на осевых нет объектов этих файлов.
Выделите их на сцене до открытия этого диалога.</translation>
    </message>
    <message>
        <location filename="gbr_etchdialog.cpp" line="364"/>
        <location filename="gbr_etchdialog.cpp" line="392"/>
        <source>not copper</source>
        <translation>не медь</translation>
    </message>
    <message>
        <location filename="gbr_etchdialog.cpp" line="393"/>
        <source>mixed</source>
        <translation>смешанно</translation>
    </message>
    <message>
        <location filename="gbr_etchdialog.cpp" line="479"/>
        <location filename="gbr_etchdialog.cpp" line="507"/>
        <source>Etch compensation failed:
%1</source>
        <translation>Компенсация подтрава не удалась:
%1</translation>
    </message>
    <message>
        <location filename="gbr_etchdialog.cpp" line="489"/>
        <source>Short circuits: %1 → %2</source>
        <translation>Замыканий: %1 → %2</translation>
    </message>
    <message>
        <location filename="gbr_etchdialog.cpp" line="521"/>
        <source>Compensation adds short circuits: %1 → %2.
Clearances between nets are closed. Keep it?</source>
        <translation>Компенсация добавляет замыкания: %1 → %2.
Зазоры между цепями закрылись. Оставить?</translation>
    </message>
</context>
<context>
    <name>Gerber::GbrObj</name>
    <message>
        <location filename="gbrcomp_onent.cpp" line="67"/>
        <source>Rotation: %1
</source>
        <translation>Поворот: %1
</translation>
    </message>
    <message>
        <location filename="gbrcomp_onent.cpp" line="68"/>
        <source>Value: %1
</source>
        <translation>Номинал: %1
</translation>
    </message>
    <message>
        <location filename="gbrcomp_onent.cpp" line="69"/>
        <source>Footprint: %1
</source>
        <translation>Посадочное место: %1
</translation>
    </message>
    <message>
        <location filename="gbrcomp_node.cpp" line="113"/>
        <source>TH|SMD|BGA|Other</source>
        <translation>TH|SMD|BGA|Другой</translation>
    </message>
    <message>
        <location filename="gbrcomp_model.cpp" line="39"/>
        <source>unsorted</source>
        <translation>без сортировки</translation>
    </message>
    <message>
        <location filename="gbrcomp_model.cpp" line="119"/>
        <source>Ref Des</source>
        <translation>Позиц.
обозначение</translation>
    </message>
    <message>
        <location filename="gbrcomp_model.cpp" line="121"/>
        <source>Manufacturer
part
number</source>
        <translation>Артикул
произво-
дителя</translation>
    </message>
    <message>
        <location filename="gbrcomp_model.cpp" line="125"/>
        <source>Value</source>
        <translation>Номинал</translation>
    </message>
    <message>
        <location filename="gbrcomp_model.cpp" line="127"/>
        <source>Mount
type</source>
        <translation>Тип
монтажа</translation>
    </message>
    <message>
        <location filename="gbrcomp_model.cpp" line="130"/>
        <source>Footprint
name</source>
        <translation>Посадочное
место</translation>
    </message>
    <message>
        <location filename="gbrcomp_model.cpp" line="133"/>
        <location filename="gbrcomp_model.cpp" line="136"/>
        <source>Package
name</source>
        <translation>Имя
корпуса</translation>
    </message>
    <message>
        <location filename="gbrcomp_model.cpp" line="139"/>
        <source>Height</source>
        <translation>Высота</translation>
    </message>
    <message>
        <location filename="gbr_parser.cpp" line="581"/>
        <location filename="gbr_parser.cpp" line="722"/>
        <source>Aperture %1 not found! Available %2</source>
        <translation>Апертура %1 не найдена! Доступны %2</translation>
    </message>
    <message>
        <location filename="gbr_parser.cpp" line="705"/>
        <location filename="gbr_parser.cpp" line="708"/>
        <source>line num %1: &apos;%2&apos;, error value.</source>
        <translation>строка %1: «%2», неверное значение.</translation>
    </message>
    <message>
        <location filename="gbr_node.cpp" line="107"/>
        <source>Etch compensation: %1 mm</source>
        <translation>Компенсация подтрава: %1 мм</translation>
    </message>
    <message>
        <location filename="gbr_node.cpp" line="139"/>
        <source>&amp;Hide other</source>
        <translation>С&amp;крыть остальные</translation>
    </message>
    <message>
        <location filename="gbr_node.cpp" line="142"/>
        <source>&amp;Show source</source>
        <translation>Показать &amp;исходный текст</translation>
    </message>
    <message>
        <location filename="gbr_node.cpp" line="165"/>
        <source>Show &amp;Components</source>
        <translation>Показать &amp;компоненты</translation>
    </message>
    <message>
        <location filename="gbr_node.cpp" line="171"/>
        <source>Show &amp;Netlist</source>
        <translation>Показать список &amp;цепей</translation>
    </message>
    <message>
        <location filename="gbr_node.cpp" line="177"/>
        <source>Change color</source>
        <translation>Изменить цвет</translation>
    </message>
    <message>
        <location filename="gbr_node.cpp" line="187"/>
        <source>Etch compensation…</source>
        <translation>Компенсация подтрава…</translation>
    </message>
    <message>
        <location filename="gbr_node.cpp" line="201"/>
        <source>&amp;Close</source>
        <translation>&amp;Закрыть</translation>
    </message>
    <message>
        <location filename="gbr_file.cpp" line="206"/>
        <source>Normal</source>
        <translation>Обычный</translation>
    </message>
    <message>
        <location filename="gbr_file.cpp" line="206"/>
        <source>Normal view</source>
        <translation>Обычный вид</translation>
    </message>
    <message>
        <location filename="gbr_file.cpp" line="207"/>
        <source>Aperture paths</source>
        <translation>Пути апертур</translation>
    </message>
    <message>
        <location filename="gbr_file.cpp" line="207"/>
        <source>Displays only aperture paths of copper
without width and without contacts</source>
        <translation>Показывает только пути апертур меди,
без ширины и без контактных площадок</translation>
    </message>
    <message>
        <location filename="gbr_file.cpp" line="208"/>
        <source>Components</source>
        <translation>Компоненты</translation>
    </message>
    <message>
        <location filename="gbr_file.cpp" line="208"/>
        <source>Show components</source>
        <translation>Показать компоненты</translation>
    </message>
    <message>
        <location filename="gbr_file.cpp" line="767"/>
        <source>Short circuit:</source>
        <translation>Короткое замыкание:</translation>
    </message>
    <message>
        <location filename="gbr_aperture.cpp" line="464"/>
        <source>Bad outline (regular polygon) macro!</source>
        <translation>Неверный макрос контура (правильный многоугольник)!</translation>
    </message>
    <message>
        <location filename="gbr_aperture.cpp" line="501"/>
        <source>Bad thermal macro!</source>
        <translation>Неверный макрос термобарьера!</translation>
    </message>
    <message>
        <location filename="gbr_netlist.cpp" line="54"/>
        <source>Netlist</source>
        <translation>Список цепей</translation>
    </message>
    <message>
        <location filename="gbr_netlist.cpp" line="58"/>
        <source>Filter</source>
        <translation>Фильтр</translation>
    </message>
    <message>
        <location filename="gbr_netlist.cpp" line="63"/>
        <source>Net</source>
        <translation>Цепь</translation>
    </message>
    <message>
        <location filename="gbr_netlist.cpp" line="63"/>
        <source>Objects</source>
        <translation>Объекты</translation>
    </message>
    <message>
        <location filename="gbr_netlist.cpp" line="72"/>
        <source>Double click zooms to the selected nets</source>
        <translation>Двойной щелчок -- приблизить выбранные цепи</translation>
    </message>
</context>
<context>
    <name>Gerber::Plugin</name>
    <message>
        <location filename="gbr_plugin.h" line="32"/>
        <source>Gerber Files</source>
        <translation>Файлы Gerber</translation>
    </message>
    <message>
        <location filename="gbr_plugin.h" line="38"/>
        <source>Gerber (*.gbr)</source>
        <translation>Gerber (*.gbr)</translation>
    </message>
</context>
<context>
    <name>QObject</name>
    <message>
        <location filename="gbr_file.cpp" line="246"/>
        <source>, drill Ø%1mm</source>
        <translation>, сверло Ø%1 мм</translation>
    </message>
</context>
<context>
    <name>SettingsDialog</name>
    <message>
        <location filename="gbr_plugin.cpp" line="120"/>
        <source>Gerber</source>
        <translation>Gerber</translation>
    </message>
    <message>
        <location filename="gbr_plugin.cpp" line="148"/>
        <source>Cleaning Polygons</source>
        <translation>Очистка полигонов</translation>
    </message>
    <message>
        <location filename="gbr_plugin.cpp" line="149"/>
        <source>Skip duplicates</source>
        <translation>Пропускать дубликаты</translation>
    </message>
    <message>
        <location filename="gbr_plugin.cpp" line="150"/>
        <source>Simplify Regions</source>
        <translation>Упрощать области</translation>
    </message>
</context>
</TS>
