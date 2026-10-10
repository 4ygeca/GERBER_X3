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
        <location filename="gbr_etchdialog.cpp" line="92"/>
        <source>unknown error</source>
        <translation>неизвестная ошибка</translation>
    </message>
    <message>
        <location filename="gbr_etchdialog.cpp" line="108"/>
        <source>Etch Compensation</source>
        <translation>Компенсация подтрава</translation>
    </message>
    <message>
        <location filename="gbr_etchdialog.cpp" line="138"/>
        <source> mm</source>
        <translation> мм</translation>
    </message>
    <message>
        <location filename="gbr_etchdialog.cpp" line="141"/>
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
        <location filename="gbr_etchdialog.cpp" line="147"/>
        <source>Unattributed objects are copper</source>
        <translation>Объекты без атрибутов — медь</translation>
    </message>
    <message>
        <location filename="gbr_etchdialog.cpp" line="149"/>
        <source>Dark objects without an X2 aperture function (%TA.AperFunction) and outside any net (%TO.N).
Turn it off if text and logos share apertures with tracks, then pick copper classes in the table.</source>
        <translation>Тёмные объекты без X2-функции апертуры (%TA.AperFunction) и вне цепей (%TO.N).
Выключите, если текст и логотипы нарисованы теми же апертурами, что и дорожки, и отметьте классы меди в таблице.</translation>
    </message>
    <message>
        <location filename="gbr_etchdialog.cpp" line="153"/>
        <source>Auto</source>
        <translation>Авто</translation>
    </message>
    <message>
        <location filename="gbr_etchdialog.cpp" line="154"/>
        <source>Keep exact size</source>
        <translation>Сохранять размер</translation>
    </message>
    <message>
        <location filename="gbr_etchdialog.cpp" line="155"/>
        <source>Shrink with copper</source>
        <translation>Сужать вместе с медью</translation>
    </message>
    <message>
        <location filename="gbr_etchdialog.cpp" line="157"/>
        <source>Clear objects (%LPC) without an X2 aperture function.
Auto: exact size if the file has X2 attributes (KiCad drill marks),
otherwise they are voids in copper and shrink with it.</source>
        <translation>Вырезы (%LPC) без X2-функции апертуры.
Авто: точный размер, если у файла есть X2-атрибуты (метки сверловки KiCad),
иначе это пустоты в меди, и они сужаются вместе с ней.</translation>
    </message>
    <message>
        <location filename="gbr_etchdialog.cpp" line="162"/>
        <source>Keep aperture holes exact size</source>
        <translation>Не сужать отверстия апертур</translation>
    </message>
    <message>
        <location filename="gbr_etchdialog.cpp" line="164"/>
        <source>Holes of flashed apertures (e.g. %ADD10C,1.6X0.8*%) keep their size
where nothing else covers them.</source>
        <translation>Отверстия вспышек апертур (например, %ADD10C,1.6X0.8*%) сохраняют размер
там, где их ничто не накрывает.</translation>
    </message>
    <message>
        <location filename="gbr_etchdialog.cpp" line="175"/>
        <source> (not a copper layer)</source>
        <translation> (не слой меди)</translation>
    </message>
    <message>
        <location filename="gbr_etchdialog.cpp" line="176"/>
        <source>&lt;b&gt;%1&lt;/b&gt;: X2 aperture functions: %2; nets: %3; file function: %4</source>
        <translation>&lt;b&gt;%1&lt;/b&gt;: X2-функции апертур: %2; цепей: %3; функция файла: %4</translation>
    </message>
    <message>
        <location filename="gbr_etchdialog.cpp" line="178"/>
        <source>yes</source>
        <translation>есть</translation>
    </message>
    <message>
        <location filename="gbr_etchdialog.cpp" line="178"/>
        <source>no</source>
        <translation>нет</translation>
    </message>
    <message>
        <location filename="gbr_etchdialog.cpp" line="190"/>
        <source>Class</source>
        <translation>Класс</translation>
    </message>
    <message>
        <location filename="gbr_etchdialog.cpp" line="190"/>
        <source>Function</source>
        <translation>Функция</translation>
    </message>
    <message>
        <location filename="gbr_etchdialog.cpp" line="190"/>
        <source>Objects</source>
        <translation>Объекты</translation>
    </message>
    <message>
        <location filename="gbr_etchdialog.cpp" line="190"/>
        <source>By rules</source>
        <translation>По правилам</translation>
    </message>
    <message>
        <location filename="gbr_etchdialog.cpp" line="198"/>
        <source>Checked classes are compensated as copper; for clear objects the void shrinks with the copper.
Bold: chosen manually; the &quot;By rules&quot; column shows the automatic choice.</source>
        <translation>Отмеченные классы компенсируются как медь; у вырезов пустота сужается вместе с медью.
Жирным — выбрано вручную; колонка «По правилам» показывает автоматический выбор.</translation>
    </message>
    <message>
        <location filename="gbr_etchdialog.cpp" line="207"/>
        <source>Reset to rules</source>
        <translation>Сбросить к правилам</translation>
    </message>
    <message>
        <location filename="gbr_etchdialog.cpp" line="211"/>
        <source>Toolpaths created earlier are not recalculated.</source>
        <translation>Созданные ранее УП не пересчитываются.</translation>
    </message>
    <message>
        <location filename="gbr_etchdialog.cpp" line="219"/>
        <source>Short circuits: %1</source>
        <translation>Замыканий: %1</translation>
    </message>
    <message>
        <location filename="gbr_etchdialog.cpp" line="221"/>
        <source>The files have no nets (X2 %TO.N): short circuits are not checked.</source>
        <translation>В файлах нет цепей (X2 %TO.N): замыкания не проверяются.</translation>
    </message>
    <message>
        <location filename="gbr_etchdialog.cpp" line="231"/>
        <source>Offset per edge:</source>
        <translation>Смещение кромки:</translation>
    </message>
    <message>
        <location filename="gbr_etchdialog.cpp" line="233"/>
        <source>Clear objects without function:</source>
        <translation>Вырезы без функции:</translation>
    </message>
    <message>
        <location filename="gbr_etchdialog.cpp" line="283"/>
        <source>Regions</source>
        <translation>Области</translation>
    </message>
    <message>
        <location filename="gbr_etchdialog.cpp" line="292"/>
        <source>%1 (%2 clear)</source>
        <translation>%1 (вырезов: %2)</translation>
    </message>
    <message>
        <location filename="gbr_etchdialog.cpp" line="320"/>
        <source>copper</source>
        <translation>медь</translation>
    </message>
    <message>
        <location filename="gbr_etchdialog.cpp" line="321"/>
        <source>not copper</source>
        <translation>не медь</translation>
    </message>
    <message>
        <location filename="gbr_etchdialog.cpp" line="322"/>
        <source>mixed</source>
        <translation>смешанно</translation>
    </message>
    <message>
        <location filename="gbr_etchdialog.cpp" line="394"/>
        <location filename="gbr_etchdialog.cpp" line="422"/>
        <source>Etch compensation failed:
%1</source>
        <translation>Компенсация подтрава не удалась:
%1</translation>
    </message>
    <message>
        <location filename="gbr_etchdialog.cpp" line="404"/>
        <source>Short circuits: %1 → %2</source>
        <translation>Замыканий: %1 → %2</translation>
    </message>
    <message>
        <location filename="gbr_etchdialog.cpp" line="436"/>
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
        <location filename="gbr_file.cpp" line="124"/>
        <source>Normal</source>
        <translation>Обычный</translation>
    </message>
    <message>
        <location filename="gbr_file.cpp" line="124"/>
        <source>Normal view</source>
        <translation>Обычный вид</translation>
    </message>
    <message>
        <location filename="gbr_file.cpp" line="125"/>
        <source>Aperture paths</source>
        <translation>Пути апертур</translation>
    </message>
    <message>
        <location filename="gbr_file.cpp" line="125"/>
        <source>Displays only aperture paths of copper
without width and without contacts</source>
        <translation>Показывает только пути апертур меди,
без ширины и без контактных площадок</translation>
    </message>
    <message>
        <location filename="gbr_file.cpp" line="126"/>
        <source>Components</source>
        <translation>Компоненты</translation>
    </message>
    <message>
        <location filename="gbr_file.cpp" line="126"/>
        <source>Show components</source>
        <translation>Показать компоненты</translation>
    </message>
    <message>
        <location filename="gbr_file.cpp" line="618"/>
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
        <location filename="gbr_file.cpp" line="164"/>
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
