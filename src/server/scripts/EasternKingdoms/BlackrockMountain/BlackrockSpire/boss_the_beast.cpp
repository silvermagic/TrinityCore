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
 * @file boss_the_beast.cpp
 * @brief 黑石塔 - 野兽 BOSS 脚本
 *
 * 本模块实现了黑石塔上层副本中 BOSS 野兽的 AI 行为。
 * 野兽是一只巨大的熔岩犬,位于黑石塔上层的兽穴区域。
 *
 * 主要功能:
 * - 火焰技能: 火球术、火焰冲击、火焰破裂
 * - 控制技能: 恐惧咆哮、狂暴冲锋
 * - 特殊机制: 兽穴周围的兽人精英会在玩家接近时逃跑并死亡
 * - 剥皮事件: 剥皮后会触发芬克尔·艾因霍恩的对话
 */

#include "ScriptMgr.h"
#include "blackrock_spire.h"
#include "InstanceScript.h"
#include "MotionMaster.h"
#include "ObjectAccessor.h"
#include "Player.h"
#include "ScriptedCreature.h"
#include "SpellAuraEffects.h"

/**
 * @brief 野兽使用的法术 ID 枚举
 */
enum BeastSpells
{
    SPELL_FLAMEBREAK                = 16785,    ///< 火焰破裂 - 对周围敌人造成火焰伤害
    SPELL_IMMOLATE                  = 15570,    ///< 献祭 - 使目标燃烧,持续受到火焰伤害
    SPELL_TERRIFYINGROAR            = 14100,    ///< 恐惧咆哮 - 使周围敌人恐惧
    SPELL_BERSERKER_CHARGE          = 16636,    ///< 狂暴冲锋 - 冲向目标造成伤害
    SPELL_FIREBALL                  = 16788,    ///< 火球术 - 对目标造成火焰伤害
    SPELL_FIREBLAST                 = 16144,    ///< 火焰冲击 - 对目标造成火焰伤害
    SPELL_FINKLE_IS_EINHORN         = 16710,    ///< 芬克尔是艾因霍恩 - 剥皮事件触发法术
    SPELL_SUICIDE                   = 7         ///< 自杀 - 用于兽人死亡
};

/**
 * @brief 野兽战斗事件 ID 枚举
 */
enum BeastEvents
{
    EVENT_FLAME_BREAK               = 1,        ///< 火焰破裂事件
    EVENT_IMMOLATE                  = 2,        ///< 献祭事件
    EVENT_TERRIFYING_ROAR           = 3,        ///< 恐惧咆哮事件
    EVENT_BERSERKER_CHARGE          = 4,        ///< 狂暴冲锋事件
    EVENT_FIREBALL                  = 5,        ///< 火球术事件
    EVENT_FIREBLAST                 = 6         ///< 火焰冲击事件
};

/**
 * @brief 野兽杂项定义枚举
 */
enum BeastMisc
{
    DATA_BEAST_REACHED              = 1,        ///< 野兽已激活数据标志
    DATA_BEAST_ROOM                 = 2,        ///< 野兽房间数据标志
    BEAST_MOVEMENT_ID               = 1379690,  ///< 野兽移动路径 ID

    NPC_BLACKHAND_ELITE             = 10317,    ///< 黑手精英 NPC ID

    SAY_BLACKHAND_DOOMD             = 0         ///< 黑手精英的台词 ID
};

/// 兽人逃跑的目标位置
Position const OrcsRunawayPosition = { 34.163567f, -536.852356f, 110.935196f, 6.056306f };

/**
 * @brief 兽人死亡事件类
 *
 * 自定义事件类,用于延迟执行兽人的自杀法术。
 * 当兽人逃跑后,会在一定时间后死亡。
 */
class OrcDeathEvent : public BasicEvent
{
public:
    /**
     * @brief 构造函数
     * @param me 生物对象指针
     */
    OrcDeathEvent(Creature* me) : _me(me) { }

    /**
     * @brief 执行事件
     * @param time 事件触发时间(未使用)
     * @param diff 时间差(未使用)
     * @return 总是返回 true
     */
    bool Execute(uint64 /*time*/, uint32 /*diff*/) override
    {
        _me->CastSpell(_me, SPELL_SUICIDE, true);
        return true;
    }

private:
    Creature* _me;  ///< 需要死亡的生物指针
};

/**
 * @brief 野兽 BOSS AI 结构体
 *
 * 继承自 BossAI,实现了野兽的战斗逻辑和特殊机制。
 * 野兽是一只强大的熔岩犬,会使用多种火焰技能和控制技能。
 */
struct boss_the_beast : public BossAI
{
    /**
     * @brief 构造函数
     * @param creature 生物对象指针
     *
     * 初始化成员变量:
     * - _beastReached: 野兽是否已被玩家激活
     * - _orcYelled: 兽人是否已经喊话
     */
    boss_the_beast(Creature* creature) : BossAI(creature, DATA_THE_BEAST), _beastReached(false), _orcYelled(false) { }

    /**
     * @brief 重置 BOSS 状态
     *
     * 当 BOSS 脱离战斗或重置时调用。
     * 如果野兽已被激活,则恢复巡逻路径。
     */
    void Reset() override
    {
        _Reset();
        if (_beastReached)
            me->GetMotionMaster()->MovePath(BEAST_MOVEMENT_ID, true);
    }

    /**
     * @brief 被法术命中时的处理
     * @param caster 施法者(未使用)
     * @param spellInfo 法术信息
     *
     * 当野兽被法术命中时调用。
     * 检测剥皮法术,触发芬克尔·艾因霍恩的事件。
     */
    void SpellHit(WorldObject* /*caster*/, SpellInfo const* spellInfo) override
    {
        // 如果是剥皮法术且野兽已死亡
        if (spellInfo->HasEffect(SPELL_EFFECT_SKINNING))
            if (!me->IsAlive()) // 这可能不会发生,但作为安全检查
                DoCastAOE(SPELL_FINKLE_IS_EINHORN, true);
    }

    /**
     * @brief 设置自定义数据
     * @param type 数据类型
     * @param data 数据值(未使用)
     *
     * 处理区域触发器发来的事件。
     *
     * @par DATA_BEAST_ROOM
     * 当玩家进入野兽房间时触发,让兽人喊话。
     *
     * @par DATA_BEAST_REACHED
     * 当玩家激活野兽时触发,让野兽开始巡逻,兽人逃跑并死亡。
     */
    void SetData(uint32 type, uint32 /*data*/) override
    {
        switch (type)
        {
            case DATA_BEAST_ROOM:
            {
                if (!_orcYelled)
                {
                    // 如果还没有搜索过附近的兽人,则搜索
                    if (_nearbyOrcsGUIDs.empty())
                        FindNearbyOrcs();

                    // 如果列表仍然为空,说明兽人不存在
                    if (_nearbyOrcsGUIDs.empty())
                        return;

                    _orcYelled = true;

                    // 只需要一个兽人来说话
                    if (Creature* orc = ObjectAccessor::GetCreature(*me, _nearbyOrcsGUIDs.front()))
                        orc->AI()->Talk(SAY_BLACKHAND_DOOMD);
                }
                break;
            }
            case DATA_BEAST_REACHED:
            {
                if (!_beastReached)
                {
                    _beastReached = true;
                    // 开始沿着路径巡逻
                    me->GetMotionMaster()->MovePath(BEAST_MOVEMENT_ID, true);

                    // 搜索附近的兽人
                    if (_nearbyOrcsGUIDs.empty())
                        FindNearbyOrcs();

                    // 让所有兽人逃跑并死亡
                    for (ObjectGuid guid : _nearbyOrcsGUIDs)
                    {
                        if (Creature* orc = ObjectAccessor::GetCreature(*me, guid))
                        {
                            // 移动到逃跑位置
                            orc->GetMotionMaster()->MovePoint(1, orc->GetRandomPoint(OrcsRunawayPosition, 5.0f));
                            // 6秒后死亡
                            orc->m_Events.AddEvent(new OrcDeathEvent(orc), me->m_Events.CalculateTime(6s));
                            // 设置为被动状态,不攻击
                            orc->SetReactState(REACT_PASSIVE);
                        }
                    }
                    // 处理玩家在区域触发器之间登录的情况(服务器崩溃或重启)
                    // 执行进入房间时的脚本部分
                    // 否则当有人踩到之前的区域触发器时,会看到奇怪的行为(死亡的怪物喊话/移动)
                    SetData(DATA_BEAST_ROOM, DATA_BEAST_ROOM);
                }
                break;
            }
        }
    }

    /**
     * @brief 进入战斗
     * @param who 仇恨目标
     *
     * 当 BOSS 进入战斗时调用。
     * 初始化所有技能的施放计时器。
     *
     * 技能施放时机:
     * - 火焰破裂: 12秒后首次施放
     * - 献祭: 3秒后首次施放
     * - 恐惧咆哮: 23秒后首次施放
     * - 狂暴冲锋: 2秒后首次施放
     * - 火球术: 8-21秒后首次施放
     * - 火焰冲击: 5-8秒后首次施放
     */
    void JustEngagedWith(Unit* who) override
    {
        BossAI::JustEngagedWith(who);
        events.ScheduleEvent(EVENT_FLAME_BREAK, 12s);
        events.ScheduleEvent(EVENT_IMMOLATE, 3s);
        events.ScheduleEvent(EVENT_TERRIFYING_ROAR, 23s);
        events.ScheduleEvent(EVENT_BERSERKER_CHARGE, 2s);
        events.ScheduleEvent(EVENT_FIREBALL, 8s, 21s);
        events.ScheduleEvent(EVENT_FIREBLAST, 5s, 8s);
    }

    /**
     * @brief 更新 AI 逻辑
     * @param diff 距离上次更新的时间间隔(毫秒)
     *
     * 每个游戏循环周期调用,处理 BOSS 的战斗行为。
     *
     * 执行流程:
     * 1. 检查是否有有效的战斗目标
     * 2. 更新事件计时器
     * 3. 如果正在施法则等待
     * 4. 处理到期的技能事件
     * 5. 执行近战攻击
     *
     * 性能注意事项:
     * - 使用事件系统管理技能冷却
     * - 施法状态检查确保不会打断正在施放的法术
     */
    void UpdateAI(uint32 diff) override
    {
        // 检查是否有有效的战斗目标
        if (!UpdateVictim())
            return;

        // 更新事件计时器
        events.Update(diff);

        // 如果正在施法,则等待施法完成
        if (me->HasUnitState(UNIT_STATE_CASTING))
            return;

        // 处理所有到期的事件
        while (uint32 eventId = events.ExecuteEvent())
        {
            switch (eventId)
            {
                case EVENT_FLAME_BREAK:
                    // 对当前目标施放火焰破裂
                    DoCastVictim(SPELL_FLAMEBREAK);
                    // 10秒后再次施放
                    events.Repeat(Seconds(10));
                    break;
                case EVENT_IMMOLATE:
                    // 随机选择一个目标施放献祭
                    if (Unit* target = SelectTarget(SelectTargetMethod::Random, 0, 100.f, true))
                        DoCast(target, SPELL_IMMOLATE);
                    // 8秒后再次施放
                    events.Repeat(Seconds(8));
                    break;
                case EVENT_TERRIFYING_ROAR:
                    // 对当前目标施放恐惧咆哮
                    DoCastVictim(SPELL_TERRIFYINGROAR);
                    // 20秒后再次施放
                    events.Repeat(Seconds(20));
                    break;
                case EVENT_BERSERKER_CHARGE:
                    // 随机选择一个目标施放狂暴冲锋
                    if (Unit* target = SelectTarget(SelectTargetMethod::Random, 0, 38.f, true))
                        DoCast(target, SPELL_BERSERKER_CHARGE);
                    // 15-23秒后再次施放
                    events.Repeat(Seconds(15), Seconds(23));
                    break;
                case EVENT_FIREBALL:
                    // 对当前目标施放火球术
                    DoCastVictim(SPELL_FIREBALL);
                    // 8-21秒后再次施放
                    events.Repeat(Seconds(8), Seconds(21));
                    break;
                case EVENT_FIREBLAST:
                    // 对当前目标施放火焰冲击
                    DoCastVictim(SPELL_FIREBLAST);
                    // 5-8秒后再次施放
                    events.Repeat(Seconds(5), Seconds(8));
                    break;
            }

            // 如果开始施法,则退出循环等待下一帧
            if (me->HasUnitState(UNIT_STATE_CASTING))
                return;
        }

        // 执行近战攻击
        DoMeleeAttackIfReady();
    }

    /**
     * @brief 查找附近的兽人
     *
     * 搜索野兽周围 50 码范围内的黑手精英,并保存它们的 GUID。
     * 这些兽人会在野兽被激活时逃跑并死亡。
     */
    void FindNearbyOrcs()
    {
        std::vector<Creature*> temp;
        me->GetCreatureListWithEntryInGrid(temp, NPC_BLACKHAND_ELITE, 50.0f);

        for (Creature* creature : temp)
            _nearbyOrcsGUIDs.push_back(creature->GetGUID());
    }

private:
    bool _beastReached;                 ///< 野兽是否已被激活
    bool _orcYelled;                    ///< 兽人是否已经喊话
    GuidVector _nearbyOrcsGUIDs;        ///< 附近兽人的 GUID 列表
};

/**
 * @brief 触发野兽移动的区域触发器脚本
 *
 * 当玩家触发 AT ID 2066 时,激活野兽开始巡逻。
 */
class at_trigger_the_beast_movement : public AreaTriggerScript
{
public:
    /**
     * @brief 构造函数
     */
    at_trigger_the_beast_movement() : AreaTriggerScript("at_trigger_the_beast_movement") { }

    /**
     * @brief 区域触发器触发时的处理
     * @param player 触发区域触发器的玩家
     * @param at 区域触发器数据(未使用)
     * @return 是否成功处理
     */
    bool OnTrigger(Player* player, const AreaTriggerEntry* /*at*/) override
    {
        // 管理员不触发
        if (player->IsGameMaster())
            return false;

        // 获取实例脚本
        if (InstanceScript* instance = player->GetInstanceScript())
        {
            // 通知野兽已被激活
            if (Creature* beast = ObjectAccessor::GetCreature(*player, instance->GetGuidData(DATA_THE_BEAST)))
                beast->AI()->SetData(DATA_BEAST_REACHED, DATA_BEAST_REACHED);
            return true;
        }
        return false;
    }
};

/**
 * @brief 野兽房间区域触发器脚本
 *
 * 当玩家进入野兽房间时,让附近的兽人喊话。
 */
class at_the_beast_room : public AreaTriggerScript
{
public:
    /**
     * @brief 构造函数
     */
    at_the_beast_room() : AreaTriggerScript("at_the_beast_room") { }

    /**
     * @brief 区域触发器触发时的处理
     * @param player 触发区域触发器的玩家
     * @param at 区域触发器数据(未使用)
     * @return 是否成功处理
     */
    bool OnTrigger(Player* player, const AreaTriggerEntry* /*at*/) override
    {
        // 管理员不触发
        if (player->IsGameMaster())
            return false;

        // 获取实例脚本
        if (InstanceScript* instance = player->GetInstanceScript())
        {
            // 通知野兽有玩家进入房间
            if (Creature* beast = ObjectAccessor::GetCreature(*player, instance->GetGuidData(DATA_THE_BEAST)))
                beast->AI()->SetData(DATA_BEAST_ROOM, DATA_BEAST_ROOM);
            return true;
        }
        return false;
    }
};

/**
 * @brief 注册 BOSS 脚本
 *
 * 将野兽的 AI 和区域触发器注册到脚本系统中。
 */
void AddSC_boss_thebeast()
{
    RegisterBlackrockSpireCreatureAI(boss_the_beast);
    new at_trigger_the_beast_movement();
    new at_the_beast_room();
}
