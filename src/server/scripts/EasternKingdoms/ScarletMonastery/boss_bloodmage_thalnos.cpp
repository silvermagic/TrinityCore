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
 * @file boss_bloodmage_thalnos.cpp
 * @brief 血色修道院BOSS血法师萨尔诺斯战斗脚本
 *
 * 本模块实现了血色修道院（墓地）BOSS血法师萨尔诺斯的战斗AI：
 * - 血法师萨尔诺斯（亡灵法师）
 * - 血色修道院墓地区域的BOSS
 *
 * 战斗机制：
 * 1. 使用火焰和暗影魔法进行攻击
 * 2. 定期施放火焰冲击、暗影箭、火焰尖刺和火焰新星
 * 3. 核心机制：血量低于35%时喊出特殊台词
 *
 * 特殊事件：
 * - 血量低于35%时触发特殊台词，警告玩家即将进入最后阶段
 */

#include "scarlet_monastery.h"
#include "ScriptedCreature.h"
#include "ScriptMgr.h"

/**
 * @brief 血法师萨尔诺斯对话文本ID枚举
 *
 * 定义萨尔诺斯在战斗中的各种对话文本ID
 */
enum BloodmageThalnosYells
{
    SAY_AGGRO = 0,   // 进入战斗时的喊话
    SAY_HEALTH = 1,  // 血量低于35%时的喊话
    SAY_KILL = 2     // 击杀玩家时的喊话
};

/**
 * @brief 血法师萨尔诺斯法术ID枚举
 *
 * 定义萨尔诺斯使用的所有法术ID
 */
enum BloodmageThalnosSpells
{
    SPELL_FLAMESHOCK = 8053,   // 火焰冲击 - 火焰伤害并造成持续伤害
    SPELL_SHADOWBOLT = 1106,   // 暗影箭 - 主要暗影伤害技能
    SPELL_FLAMESPIKE = 8814,   // 火焰尖刺 - 火焰AOE技能
    SPELL_FIRENOVA = 16079     // 火焰新星 - 强力火焰AOE技能
};

/**
 * @brief 血法师萨尔诺斯事件ID枚举
 *
 * 定义战斗中使用的定时事件ID，用于技能冷却和施放计划
 */
enum BloodmageThalnosEvents
{
    EVENT_FLAME_SHOCK = 1, // 火焰冲击事件
    EVENT_SHADOW_BOLT,     // 暗影箭事件
    EVENT_FLAME_SPIKE,     // 火焰尖刺事件
    EVENT_FIRE_NOVA        // 火焰新星事件
};

/**
 * @brief 血法师萨尔诺斯BOSS AI结构体
 *
 * 实现萨尔诺斯的战斗AI逻辑，包括：
 * - 火焰冲击、暗影箭、火焰尖刺、火焰新星等技能
 * - 血量35%时的特殊喊话
 *
 * 战斗流程：
 * 1. 进入战斗时喊话并安排技能事件
 * 2. 定期施放火焰冲击（10-15秒冷却）、暗影箭（2秒冷却）、火焰尖刺（30秒冷却）、火焰新星（40秒冷却）
 * 3. 血量低于35%时喊出特殊台词
 * 4. 击杀玩家时喊话
 */
struct boss_bloodmage_thalnos : public BossAI
{
    /**
     * @brief 构造函数
     * @param creature 生物对象指针
     *
     * 初始化BOSS AI，设置血量喊话标志为false（表示还未触发）
     */
    boss_bloodmage_thalnos(Creature* creature) : BossAI(creature, DATA_BLOODMAGE_THALNOS)
    {
        _hpYell = false;
    }

    /**
     * @brief 重置AI状态
     *
     * 当战斗重置时调用，重置所有状态变量和事件
     * 将血量喊话标志重置为false
     */
    void Reset() override
    {
        _hpYell = false;
        _Reset();
    }

    /**
     * @brief 进入战斗事件
     * @param who 进入战斗的目标
     *
     * 当萨尔诺斯进入战斗时：
     * - 喊出战斗台词
     * - 安排技能施放事件
     *
     * 技能安排：
     * - 火焰冲击：10秒后施放
     * - 暗影箭：2秒后施放
     * - 火焰尖刺：8秒后施放
     * - 火焰新星：40秒后施放
     */
    void JustEngagedWith(Unit* who) override
    {
        Talk(SAY_AGGRO);
        BossAI::JustEngagedWith(who);
        events.ScheduleEvent(EVENT_FLAME_SHOCK, 10s);
        events.ScheduleEvent(EVENT_SHADOW_BOLT, 2s);
        events.ScheduleEvent(EVENT_FLAME_SPIKE, 8s);
        events.ScheduleEvent(EVENT_FIRE_NOVA, 40s);
    }

    /**
     * @brief 击杀单位事件
     * @param victim 被击杀的单位
     *
     * 当萨尔诺斯击杀玩家时喊话
     */
    void KilledUnit(Unit* victim) override
    {
        if (victim->GetTypeId() == TYPEID_PLAYER)
            Talk(SAY_KILL);
    }

    /**
     * @brief 受到伤害事件
     * @param attacker 攻击者
     * @param damage 伤害值（引用，可修改）
     * @param damageType 伤害类型
     * @param spellInfo 法术信息
     *
     * 核心机制：处理萨尔诺斯的血量喊话逻辑
     * - 当血量低于35%且未喊过话时，喊出特殊台词
     */
    void DamageTaken(Unit* /*attacker*/, uint32& damage, DamageEffectType /*damageType*/, SpellInfo const* /*spellInfo = nullptr*/) override
    {
        if (!_hpYell && me->HealthBelowPctDamaged(35, damage))
        {
            Talk(SAY_HEALTH);
            _hpYell = true;
        }
    }

    /**
     * @brief 执行事件处理器
     * @param eventId 事件ID
     *
     * 处理定时触发的技能施放事件
     */
    void ExecuteEvent(uint32 eventId) override
    {
        switch (eventId)
        {
            case EVENT_FLAME_SHOCK:
                DoCastVictim(SPELL_FLAMESHOCK);
                events.Repeat(10s, 15s);
                break;
            case EVENT_SHADOW_BOLT:
                DoCastVictim(SPELL_SHADOWBOLT);
                events.Repeat(2s);
                break;
            case EVENT_FLAME_SPIKE:
                DoCastVictim(SPELL_FLAMESPIKE);
                events.Repeat(30s);
                break;
            case EVENT_FIRE_NOVA:
                DoCastVictim(SPELL_FIRENOVA);
                events.Repeat(40s);
                break;
            default:
                break;
        }
    }

private:
    bool _hpYell;  // 是否已触发血量喊话，用于血量35%判定
};

/**
 * @brief 注册BOSS脚本
 *
 * 将血法师萨尔诺斯的AI注册到脚本系统中
 */
void AddSC_boss_bloodmage_thalnos()
{
    RegisterScarletMonasteryCreatureAI(boss_bloodmage_thalnos);
}
