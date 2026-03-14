/*
 * This file is part of the TrinityCore Project. See AUTHORS file for Copyright information
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program. If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef CREATUREAIIMPL_H
#define CREATUREAIIMPL_H

/**
 * @file CreatureAIImpl.h
 * @brief CreatureAI 实现辅助工具和定义
 *
 * 本文件提供了 CreatureAI 系统的核心实现工具，包括：
 * - 随机选择工具函数
 * - AI 法术目标类型定义
 * - AI 法术条件定义
 * - AI 法术信息结构体
 * - 实例脚本辅助函数
 *
 * 这些工具被广泛应用于各种 CreatureAI 实现中，用于简化 AI 行为的编程。
 */

#include "Random.h"
#include <type_traits>
#include <functional>

class WorldObject;

/**
 * @brief 从多个值中随机选择一个并返回
 *
 * 这是一个模板函数，用于从传入的多个参数中随机选择一个返回。
 * 所有参数必须具有相同的类型（或可转换为相同类型）。
 *
 * @tparam First 第一个参数的类型
 * @tparam Second 第二个参数的类型
 * @tparam Rest 其余参数的类型包
 * @param first 第一个候选值
 * @param second 第二个候选值
 * @param rest 其余候选值（可变参数）
 * @return First const& 随机选择的值的常量引用
 *
 * @example
 * @code
 * // 从三个法术中随机选择一个施放
 * uint32 spellId = RAND(12345, 23456, 34567);
 *
 * // 从多个字符串中随机选择一个
 * std::string const& text = RAND("攻击!", "冲锋!", "撤退!");
 * @endcode
 */
template<typename First, typename Second, typename... Rest>
inline First const& RAND(First const& first, Second const& second, Rest const&... rest)
{
    std::reference_wrapper<typename std::add_const<First>::type> const pack[] = { first, second, rest... };
    return pack[urand(0, sizeof...(rest) + 1)].get();
}

/**
 * @enum AITarget
 * @brief 定义 AI 法术的目标类型
 *
 * 此枚举用于指定 AI 施放法术时的目标选择策略。
 * 不同的目标类型适用于不同的法术类型和战术需求。
 */
enum AITarget
{
    AITARGET_SELF,      ///< 自己 - 用于增益法术、治疗法术等（例如：自我治疗、自我buff）
    AITARGET_VICTIM,    ///< 当前目标 - 用于攻击性法术、减益法术等（例如：对敌人施放伤害法术）
    AITARGET_ENEMY,     ///< 敌对单位 - 用于需要选择敌方目标的法术（例如：群体攻击、随机敌人攻击）
    AITARGET_ALLY,      ///< 友方单位 - 用于辅助法术、治疗法术等（例如：治疗队友、给队友加buff）
    AITARGET_BUFF,      ///< 需要增益的目标 - 用于寻找需要增益的友方单位（例如：寻找没有buff的队友）
    AITARGET_DEBUFF     ///< 需要驱散的目标 - 用于寻找需要驱散减益的友方单位（例如：驱散队友的debuff）
};

/**
 * @enum AICondition
 * @brief 定义 AI 法术的触发条件
 *
 * 此枚举用于指定 AI 法术在什么情况下可以被施放。
 * 这些条件控制了法术的施放时机。
 */
enum AICondition
{
    AICOND_AGGRO,       ///< 进入战斗时 - 当生物进入战斗状态时触发（例如：开战时的初始buff）
    AICOND_COMBAT,      ///< 战斗中 - 在战斗过程中持续可以施放（例如：常规攻击技能）
    AICOND_DIE          ///< 死亡时 - 当生物即将死亡时触发（例如：死亡时的自爆技能、临终遗言）
};

/**
 * @def AI_DEFAULT_COOLDOWN
 * @brief AI 法术的默认冷却时间（毫秒）
 *
 * 当没有为 AI 法术指定冷却时间时，使用此默认值。
 * 数值为 5000 毫秒（5秒）。
 */
#define AI_DEFAULT_COOLDOWN 5000

/**
 * @struct AISpellInfoType
 * @brief 存储 AI 法术的元数据信息
 *
 * 此结构体用于存储 AI 施放法术所需的各种配置信息，包括目标类型、触发条件、冷却时间和施法距离等。
 * 这些信息用于 AI 决策系统，判断何时以及如何施放特定法术。
 *
 * 通常通过 GetAISpellInfo() 函数获取预定义的法术信息。
 */
struct AISpellInfoType
{
    /**
     * @brief 默认构造函数
     *
     * 初始化所有成员变量为默认值：
     * - target: AITARGET_SELF（目标为自己）
     * - condition: AICOND_COMBAT（战斗中触发）
     * - cooldown: AI_DEFAULT_COOLDOWN（默认冷却时间）
     * - realCooldown: 0（实际冷却时间未初始化）
     * - maxRange: 0.0f（最大施法距离未初始化）
     */
    AISpellInfoType() : target(AITARGET_SELF), condition(AICOND_COMBAT)
        , cooldown(AI_DEFAULT_COOLDOWN), realCooldown(0), maxRange(0.0f){ }

    AITarget target;        ///< 法术目标类型（自己、敌人、友方等）
    AICondition condition;  ///< 法术触发条件（进入战斗、战斗中、死亡时）
    uint32 cooldown;        ///< 冷却时间（毫秒）- AI 决策使用的冷却时间
    uint32 realCooldown;    ///< 实际冷却时间（毫秒）- 法术本身的冷却时间
    float maxRange;         ///< 最大施法距离（码）- 法术可以施放的最大距离
};

/**
 * @brief 获取指定法术的 AI 信息
 *
 * 此函数从全局 AI 法术信息表中获取指定法术 ID 对应的元数据信息。
 * 这些信息包括目标类型、触发条件、冷却时间等，用于 AI 决策。
 *
 * @param i 法术 ID（Spell ID）
 * @return AISpellInfoType* 指向法术信息的指针，如果不存在则返回默认信息
 *
 * @note 返回的指针指向全局数据结构，不应被修改或删除
 * @note 此函数通常在 CreatureAI::UpdateAI() 中使用，用于判断是否可以施放特定法术
 */
AISpellInfoType* GetAISpellInfo(uint32 i);

/**
 * @brief 检查实例是否运行指定的脚本
 *
 * 此函数检查世界对象所在的实例地图是否正在运行指定的脚本。
 * 用于判断特定实例脚本是否激活。
 *
 * @param obj 要检查的世界对象（通常是生物或玩家）
 * @param scriptName 脚本名称（例如 "instance_naxxramas"）
 * @return true 如果实例正在运行指定脚本
 * @return false 如果实例未运行指定脚本或对象不在实例中
 *
 * @note 此函数是线程安全的，可以从任何线程调用
 *
 * @example
 * @code
 * // 检查是否在纳克萨玛斯副本中
 * if (InstanceHasScript(me, "instance_naxxramas"))
 * {
 *     // 执行纳克萨玛斯特有的逻辑
 * }
 * @endcode
 */
TC_GAME_API bool InstanceHasScript(WorldObject const* obj, char const* scriptName);

/**
 * @brief 获取实例脚本的 AI 实例
 *
 * 这是一个模板函数，用于在特定实例脚本激活时创建对应的 AI 实例。
 * 如果实例未运行指定脚本，则返回 nullptr。
 *
 * 此函数简化了实例特定 AI 的创建过程，避免了手动检查和创建的冗余代码。
 *
 * @tparam AI 要创建的 AI 类类型
 * @tparam T 生物类型（通常是 Creature）
 * @param obj 生物对象指针
 * @param scriptName 实例脚本名称
 * @return AI* 新创建的 AI 实例指针，如果实例脚本不匹配则返回 nullptr
 *
 * @note 调用者负责管理返回的 AI 指针的生命周期
 * @note 此函数通常在 CreatureAI 自定义工厂函数中使用
 *
 * @example
 * @code
 * // 在 ScriptLoader 中注册
 * CreatureAI* GetAI_boss_kelthuzad(Creature* creature)
 * {
 *     return GetInstanceAI<boss_kelthuzadAI>(creature, "instance_naxxramas");
 * }
 *
 * // 这样，只有在纳克萨玛斯副本中，才会创建 boss_kelthuzadAI
 * // 在其他地方，该生物将使用默认 AI
 * @endcode
 */
template <class AI, class T>
AI* GetInstanceAI(T* obj, char const* scriptName)
{
    if (InstanceHasScript(obj, scriptName))
        return new AI(obj);

    return nullptr;
}

#endif
