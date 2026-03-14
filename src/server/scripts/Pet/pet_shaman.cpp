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

/**
 * @file pet_shaman.cpp
 * @brief 萨满宠物AI模块
 *
 * 本模块实现萨满职业相关宠物的AI行为，包括：
 * - 大地元素 (Earth Elemental)：由元素掌握召唤，施放愤怒之土攻击目标
 * - 火元素 (Fire Elemental)：由元素掌握召唤，施放火焰新星、火焰冲击和火盾
 *
 * 特殊行为：
 * - 大地元素是近战宠物，定期施放愤怒之土
 * - 火元素是混合宠物，同时施放近战攻击和法术
 *
 * Ordered alphabetically using scriptname.
 * Scriptnames of files in this file should be prefixed with "npc_pet_sha_".
 */

#include "ScriptMgr.h"
#include "ScriptedCreature.h"

/**
 * @brief 萨满法术ID枚举
 *
 * 定义元素宠物使用的法术ID
 */
enum ShamanSpells
{
    SPELL_SHAMAN_ANGEREDEARTH   = 36213,  ///< 愤怒之土 - 大地元素的主要攻击法术
    SPELL_SHAMAN_FIREBLAST      = 57984,  ///< 火焰冲击 - 火元素的即时伤害法术
    SPELL_SHAMAN_FIRENOVA       = 12470,  ///< 火焰新星 - 火元素的范围伤害法术
    SPELL_SHAMAN_FIRESHIELD     = 13376   ///< 火盾 - 火元素的保护性光环
};

/**
 * @brief 萨满事件ID枚举
 *
 * 定义宠物AI使用的事件ID，用于事件调度器
 */
enum ShamanEvents
{
    // Earth Elemental - 大地元素事件
    EVENT_SHAMAN_ANGEREDEARTH   = 1,      ///< 愤怒之土施放事件

    // Fire Elemental - 火元素事件
    EVENT_SHAMAN_FIRENOVA       = 1,      ///< 火焰新星施放事件
    EVENT_SHAMAN_FIRESHIELD     = 2,      ///< 火盾施放事件
    EVENT_SHAMAN_FIREBLAST      = 3       ///< 火焰冲击施放事件
};

/**
 * @brief 大地元素AI
 *
 * 继承自ScriptedAI，实现萨满大地元素的行为逻辑。
 * 大地元素是近战宠物，会定期施放愤怒之土法术攻击目标。
 *
 * 特殊行为：
 * - 重置时立即施放愤怒之土
 * - 之后每5-20秒施放一次愤怒之土
 * - 主要依靠近战攻击
 */
struct npc_pet_shaman_earth_elemental : public ScriptedAI
{
    /**
     * @brief 构造函数
     * @param creature 生物对象指针
     */
    npc_pet_shaman_earth_elemental(Creature* creature) : ScriptedAI(creature) { }

    /**
     * @brief 重置回调
     *
     * 调用时机：宠物重置时（召唤、脱战等）
     *
     * 功能：
     * 1. 重置事件调度器
     * 2. 立即安排施放愤怒之土（0秒后）
     *
     * 性能注意：重置时一次性调用
     */
    void Reset() override
    {
        _events.Reset();
        _events.ScheduleEvent(EVENT_SHAMAN_ANGEREDEARTH, 0s);
    }

    /**
     * @brief 更新AI
     * @param diff 时间差（毫秒）
     *
     * 调用时机：每个游戏循环tick
     *
     * 功能：
     * 1. 检查是否有有效目标
     * 2. 更新事件调度器
     * 3. 执行愤怒之土事件，施放法术并安排下一次施放
     * 4. 执行近战攻击
     *
     * 性能注意：每帧调用，应保持高效
     */
    void UpdateAI(uint32 diff) override
    {
        // 检查是否有有效目标
        if (!UpdateVictim())
            return;

        // 更新事件调度器
        _events.Update(diff);

        // 执行事件
        if (_events.ExecuteEvent() == EVENT_SHAMAN_ANGEREDEARTH)
        {
            // 施放愤怒之土
            DoCastVictim(SPELL_SHAMAN_ANGEREDEARTH);
            // 安排下一次施放（5-20秒后）
            _events.ScheduleEvent(EVENT_SHAMAN_ANGEREDEARTH, 5s, 20s);
        }

        // 执行近战攻击
        DoMeleeAttackIfReady();
    }

private:
    EventMap _events;  ///< 事件调度器，管理法术施放计时
};

/**
 * @brief 火元素AI
 *
 * 继承自ScriptedAI，实现萨满火元素的行为逻辑。
 * 火元素是混合宠物，会施放多种火焰法术并进行近战攻击。
 *
 * 特殊行为：
 * - 重置时安排施放火焰新星、火焰冲击和火盾
 * - 每5-20秒施放一次火焰新星和火焰冲击
 * - 每2秒施放一次火盾
 * - 如果正在施法，等待完成后再执行其他动作
 */
struct npc_pet_shaman_fire_elemental : public ScriptedAI
{
    /**
     * @brief 构造函数
     * @param creature 生物对象指针
     */
    npc_pet_shaman_fire_elemental(Creature* creature) : ScriptedAI(creature) { }

    /**
     * @brief 重置回调
     *
     * 调用时机：宠物重置时（召唤、脱战等）
     *
     * 功能：
     * 1. 重置事件调度器
     * 2. 安排火焰新星施放（5-20秒后）
     * 3. 安排火焰冲击施放（5-20秒后）
     * 4. 立即安排火盾施放（0秒后）
     *
     * 性能注意：重置时一次性调用
     */
    void Reset() override
    {
        _events.Reset();
        _events.ScheduleEvent(EVENT_SHAMAN_FIRENOVA, 5s, 20s);
        _events.ScheduleEvent(EVENT_SHAMAN_FIREBLAST, 5s, 20s);
        _events.ScheduleEvent(EVENT_SHAMAN_FIRESHIELD, 0s);
    }

    /**
     * @brief 更新AI
     * @param diff 时间差（毫秒）
     *
     * 调用时机：每个游戏循环tick
     *
     * 功能：
     * 1. 检查是否有有效目标
     * 2. 如果正在施法，等待完成
     * 3. 更新事件调度器
     * 4. 执行所有到期的事件：
     *    - 火焰新星：范围伤害法术
     *    - 火盾：保护性光环
     *    - 火焰冲击：即时伤害法术
     * 5. 执行近战攻击
     *
     * 性能注意：每帧调用，应保持高效
     */
    void UpdateAI(uint32 diff) override
    {
        // 检查是否有有效目标
        if (!UpdateVictim())
            return;

        // 如果正在施法，等待完成
        if (me->HasUnitState(UNIT_STATE_CASTING))
            return;

        // 更新事件调度器
        _events.Update(diff);

        // 执行所有到期的事件
        while (uint32 eventId = _events.ExecuteEvent())
        {
            switch (eventId)
            {
                case EVENT_SHAMAN_FIRENOVA:
                    // 施放火焰新星
                    DoCastVictim(SPELL_SHAMAN_FIRENOVA);
                    // 安排下一次施放（5-20秒后）
                    _events.ScheduleEvent(EVENT_SHAMAN_FIRENOVA, 5s, 20s);
                    break;
                case EVENT_SHAMAN_FIRESHIELD:
                    // 施放火盾
                    DoCastVictim(SPELL_SHAMAN_FIRESHIELD);
                    // 安排下一次施放（2秒后）
                    _events.ScheduleEvent(EVENT_SHAMAN_FIRESHIELD, 2s);
                    break;
                case EVENT_SHAMAN_FIREBLAST:
                    // 施放火焰冲击
                    DoCastVictim(SPELL_SHAMAN_FIREBLAST);
                    // 安排下一次施放（5-20秒后）
                    _events.ScheduleEvent(EVENT_SHAMAN_FIREBLAST, 5s, 20s);
                    break;
                default:
                    break;
            }
        }

        // 执行近战攻击
        DoMeleeAttackIfReady();
    }

private:
    EventMap _events;  ///< 事件调度器，管理法术施放计时
};

/**
 * @brief 注册萨满宠物脚本
 *
 * 调用时机：服务器启动时由脚本加载器调用
 *
 * 功能：注册所有萨满宠物AI到脚本系统
 */
void AddSC_shaman_pet_scripts()
{
    RegisterCreatureAI(npc_pet_shaman_earth_elemental);
    RegisterCreatureAI(npc_pet_shaman_fire_elemental);
}
