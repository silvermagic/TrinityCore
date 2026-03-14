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
 * @file boss_renataki.cpp
 * @brief 祖尔格拉布副本Boss - 雷纳塔基(Renataki)的AI实现
 *
 * 雷纳塔基是祖尔格拉布"疯狂之缘"(Edge of Madness)区域的四个可选Boss之一。
 * 该Boss是巨魔盗贼形象，具有隐身、伏击和千刃斩等典型盗贼技能。
 *
 * Boss战斗机制：
 * 1. 定期进入隐身状态，变成不可选中的状态
 * 2. 在隐身期间会随机传送并伏击目标
 * 3. 显现后会切换目标并降低当前目标的仇恨值
 * 4. 使用千刃斩技能攻击当前目标
 *
 * @note 该Boss需要通过疯狂之缘的篝火召唤，并且根据游戏事件系统确定何时出现
 */

#include "zulgurub.h"
#include "ScriptedCreature.h"
#include "ScriptMgr.h"

/**
 * @brief 雷纳塔基使用的法术ID枚举
 */
enum Spells
{
    SPELL_AMBUSH = 34794,           ///< 伏击 - 对目标造成大量伤害的偷袭技能
    SPELL_THOUSANDBLADES = 34799    ///< 千刃斩 - 对目标造成武器伤害的攻击技能
};

/**
 * @brief 杂项数据枚举
 */
enum Misc
{
    EQUIP_ID_MAIN_HAND = 0  ///< 主手装备ID（原为物品显示ID 31818，但该ID不存在）
};

/**
 * @brief 雷纳塔基Boss AI结构体
 *
 * 实现了雷纳塔基的战斗AI，包含隐身、伏击、目标切换等核心战斗逻辑。
 * 该Boss具有独特的隐身机制，会周期性地消失并伏击随机目标。
 */
struct boss_renataki : public BossAI
{
    /**
     * @brief 构造函数
     * @param creature 生物对象指针
     */
    boss_renataki(Creature* creature) : BossAI(creature, DATA_EDGE_OF_MADNESS)
    {
        Initialize();
    }

    /**
     * @brief 初始化所有成员变量
     *
     * 设置所有计时器和状态标志的初始值。
     * 在构造函数和Reset时调用，确保Boss状态一致性。
     */
    void Initialize()
    {
        _invisibleTimer = urand(8000, 18000);       ///< 隐身计时器：8-18秒后开始隐身
        _ambushTimer = 3000;                         ///< 伏击计时器：隐身后3秒执行伏击
        _visibleTimer = 4000;                        ///< 显现计时器：伏击后4秒恢复可见
        _aggroTimer = urand(15000, 25000);          ///< 仇恨重置计时器：15-25秒切换目标
        _thousandBladesTimer = urand(4000, 8000);   ///< 千刃斩计时器：4-8秒使用技能
        _invisible = false;                          ///< 是否处于隐身状态
        _ambushed = false;                           ///< 是否已完成伏击
    }

    /**
     * @brief 重置Boss状态
     *
     * 在战斗重置时调用，重置所有计时器和状态变量，
     * 将Boss恢复到初始可战斗状态。
     */
    void Reset() override
    {
        _Reset();
        Initialize();
    }

    /**
     * @brief 更新AI逻辑
     * @param diff 自上次更新以来经过的时间（毫秒）
     *
     * 主要处理以下逻辑：
     * 1. 隐身机制：定期进入隐身状态
     * 2. 伏击机制：隐身期间传送到随机目标并施放伏击
     * 3. 显现机制：伏击后恢复可见状态
     * 4. 目标切换：定期降低当前目标仇恨并切换目标
     * 5. 技能使用：使用千刃斩攻击当前目标
     *
     * @note 隐身期间Boss不可被选中，需要特殊的视觉效果处理
     */
    void UpdateAI(uint32 diff) override
    {
        // 如果没有战斗目标，直接返回
        if (!UpdateVictim())
            return;

        // 处理隐身机制
        if (_invisibleTimer <= diff)
        {
            // 中断当前施法
            me->InterruptSpell(CURRENT_GENERIC_SPELL);
            // 卸下武器，改变外观为隐身模型
            SetEquipmentSlots(false, EQUIP_UNEQUIP, EQUIP_NO_CHANGE, EQUIP_NO_CHANGE);
            me->SetDisplayId(11686);  // 设置为隐身模型的显示ID
            me->SetUnitFlag(UNIT_FLAG_UNINTERACTIBLE);  // 设置为不可交互状态
            _invisible = true;
            // 下次隐身时间：15-30秒
            _invisibleTimer = urand(15000, 30000);
        }
        else
            _invisibleTimer -= diff;

        // 处理隐身期间的伏击行为
        if (_invisible)
        {
            if (_ambushTimer <= diff)
            {
                // 选择随机玩家目标进行伏击
                if (Unit* target = SelectTarget(SelectTargetMethod::Random, 0, 100.0f, true))
                {
                    // 传送到目标位置
                    DoTeleportTo(target->GetPositionX(), target->GetPositionY(), target->GetPositionZ());
                    // 施放伏击技能
                    DoCast(target, SPELL_AMBUSH);
                }

                _ambushed = true;
                _ambushTimer = 3000;
            }
            else
                _ambushTimer -= diff;
        }

        // 处理伏击后的显现机制
        if (_ambushed)
        {
            if (_visibleTimer <= diff)
            {
                // 中断当前施法
                me->InterruptSpell(CURRENT_GENERIC_SPELL);
                // 恢复正常的巨魔外观
                me->SetDisplayId(15268);  // 设置为雷纳塔基的正常显示ID
                SetEquipmentSlots(false, EQUIP_ID_MAIN_HAND, EQUIP_NO_CHANGE, EQUIP_NO_CHANGE);
                me->RemoveUnitFlag(UNIT_FLAG_UNINTERACTIBLE);  // 移除不可交互标志
                _invisible = false;
                _visibleTimer = 4000;
            }
            else
                _visibleTimer -= diff;
        }

        // 重置部分仇恨，使Boss攻击其他玩家
        // 此机制防止Boss过度聚焦单一目标
        if (!_invisible)
        {
            if (_aggroTimer <= diff)
            {
                // 选择新的随机目标
                if (Unit* target = SelectTarget(SelectTargetMethod::Random, 0, 100.0f, true))
                {
                    // 降低当前目标的仇恨值50%
                    if (GetThreat(me->GetVictim()))
                        ModifyThreatByPercent(me->GetVictim(), -50);
                    // 开始攻击新目标
                    AttackStart(target);
                }

                _aggroTimer = urand(7000, 20000);
            }
            else
                _aggroTimer -= diff;

            // 使用千刃斩技能攻击当前目标
            if (_thousandBladesTimer <= diff)
            {
                DoCastVictim(SPELL_THOUSANDBLADES);
                _thousandBladesTimer = urand(7000, 12000);
            }
            else
                _thousandBladesTimer -= diff;
        }

        // 如果准备就绪，执行近战攻击
        DoMeleeAttackIfReady();
    }

private:
    uint32 _invisibleTimer;        ///< 隐身计时器 - 控制何时进入隐身状态
    uint32 _ambushTimer;            ///< 伏击计时器 - 控制隐身期间的伏击时机
    uint32 _visibleTimer;           ///< 显现计时器 - 控制伏击后何时恢复可见
    uint32 _aggroTimer;             ///< 仇恨重置计时器 - 控制目标切换频率
    uint32 _thousandBladesTimer;    ///< 千刃斩计时器 - 控制技能使用频率
    bool _invisible;                ///< 是否处于隐身状态
    bool _ambushed;                 ///< 是否已完成伏击
};

/**
 * @brief 注册雷纳塔基Boss脚本
 *
 * 将雷纳塔基的AI注册到脚本系统中，使其在游戏中可以被正确调用。
 */
void AddSC_boss_renataki()
{
    RegisterZulGurubCreatureAI(boss_renataki);
}
