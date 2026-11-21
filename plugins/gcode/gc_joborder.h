/********************************************************************************
 * Author    :  Damir Bakiev                                                    *
 * Version   :  na                                                              *
 * Date      :  XXXXX XX, 2026                                                  *
 * Website   :  na                                                              *
 * Copyright :  Damir Bakiev 2016-2026                                          *
 * License   :                                                                  *
 * Use, modification & distribution is subject to Boost Software License Ver 1. *
 * http://www.boost.org/LICENSE_1_0.txt                                         *
 ********************************************************************************/
#pragma once

#include <QString>

#include <algorithm>
#include <numeric>
#include <tuple>
#include <vector>

namespace GCode {

// Порядок шагов задания и номера T -- одними ключами, без проекта, постов и
// GUI: так их проверяет и отдельный тест. Ключи из УП собирает gc_job.cpp.

// Что о шаге нужно порядку по умолчанию.
struct JobOrderKey {
    QString identity;    // Tool::identityKey() -- один физический инструмент
    int toolRank{};      // сверло 0, фреза 1, гравёр 2, прочие 3
    double diameter{};   // внутри своего ранга -- по возрастанию
    int operationRank{}; // сверловка 0, фрезеровка 1, прочее 2
    bool cutout{};       // вырезка заготовки
};

// Порядок по умолчанию -- индексы keys. Сначала по инструменту: группы
// одного физического инструмента идут сверлами по возрастанию диаметра,
// затем фрезами, граверами и прочими так же; внутри группы -- сверловка,
// фрезеровка и вырезка заготовки последней; группа, в которой есть вырезка,
// -- в самом конце задания: заготовку вырезают последним шагом, после неё
// плата уже не держится. При равенстве -- исходный порядок (дерево УП).
inline std::vector<int> jobDefaultOrder(const std::vector<JobOrderKey>& keys) {
    struct Group {
        QString identity;
        bool cutout{};
        int toolRank{};
        double diameter{};
    };
    std::vector<Group> groups;
    std::vector<int> groupOf(keys.size());
    for(size_t i{}; i < keys.size(); ++i) {
        auto it = std::ranges::find(groups, keys[i].identity, &Group::identity);
        // Ранг и диаметр -- у первого шага группы: у одного инструмента они
        // одни на все его шаги.
        if(it == groups.end())
            it = groups.insert(groups.end(), Group{keys[i].identity, false, keys[i].toolRank, keys[i].diameter});
        it->cutout = it->cutout || keys[i].cutout;
        groupOf[i] = static_cast<int>(it - groups.begin());
    }

    std::vector<int> groupOrder(groups.size());
    std::iota(groupOrder.begin(), groupOrder.end(), 0);
    std::ranges::stable_sort(groupOrder, {}, [&groups](int group) {
        return std::tuple{groups[group].cutout, groups[group].toolRank, groups[group].diameter};
    });
    std::vector<int> groupPlace(groups.size());
    for(size_t place{}; place < groupOrder.size(); ++place)
        groupPlace[groupOrder[place]] = static_cast<int>(place);

    std::vector<int> order(keys.size());
    std::iota(order.begin(), order.end(), 0);
    std::ranges::stable_sort(order, {}, [&](int step) {
        return std::tuple{groupPlace[groupOf[step]], keys[step].cutout, keys[step].operationRank};
    });
    return order;
}

// Номера T и смены инструмента по шагам в порядке исполнения.
struct JobPlan {
    std::vector<int> toolNumbers; // номер T каждого шага
    std::vector<bool> toolChange; // перед шагом -- смена инструмента
    int tools{};                  // физических инструментов
    int toolChanges{};            // блоков смены (первый -- тоже смена: инструмент ставит оператор)
    bool wrapped{};               // номера пошли по кругу
};

// Номера локальны для задания: T1..Tn в порядке первого применения. У
// контроллера с пределом номеров (maxToolNumber > 0, у grblHAL N_TOOLS = 8)
// номера сверх него идут по кругу: T(max+1) -- снова T1. Инструмент для
// отправителя -- его код в комментариях, а не номер. Смена -- перед первым
// шагом и перед каждым, чей инструмент не тот, что у предыдущего: вернулись
// к уже бывшему инструменту -- смена снова, под его прежним номером.
inline JobPlan jobPlan(const std::vector<QString>& identities, int maxToolNumber) {
    JobPlan plan;
    std::vector<QString> tools; // в порядке первого применения
    for(size_t i{}; i < identities.size(); ++i) {
        auto it = std::ranges::find(tools, identities[i]);
        if(it == tools.end()) it = tools.insert(tools.end(), identities[i]);
        const int index = static_cast<int>(it - tools.begin());
        plan.toolNumbers.push_back(maxToolNumber > 0 ? index % maxToolNumber + 1 : index + 1);
        const bool change = i == 0 || identities[i] != identities[i - 1];
        plan.toolChange.push_back(change);
        plan.toolChanges += change;
    }
    plan.tools = static_cast<int>(tools.size());
    plan.wrapped = maxToolNumber > 0 && plan.tools > maxToolNumber;
    return plan;
}

} // namespace GCode
