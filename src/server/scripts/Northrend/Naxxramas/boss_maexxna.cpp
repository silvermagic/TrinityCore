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
 * @file boss_maexxna.cpp
 * @brief 纳克萨玛斯副本BOSS - 麦克斯纳(Maexxna)的AI脚本
 *
 * 模块职责:
 * - 实现麦克斯纳BOSS的战斗逻辑
 * - 管理蛛网缠绕(Web Wrap)机制 - 将玩家抛射到墙上缠绕
 * - 处理蛛网喷射(Web Spray) - 周期性昏迷所有玩家
 * - 实现剧毒震荡和坏死毒液技能
 * - 管理30%血量狂暴机制
 * - 处理小蜘蛛(Spiderling)的召唤
 *
 * 战斗机制:
 * - 蛛网缠绕:每40秒随机缠绕1-2名非坦克玩家,抛射到墙上
 * - 蛛网喷射:每40秒昏迷所有玩家6秒,期间无法行动
 * - 30%血量进入狂暴状态,大幅提高伤害
 * - 定期召唤8-10只小蜘蛛加入战斗
 * - 玩家需要打破蛛网来解救被缠绕的队友
 */

#include "ScriptMgr.h"
#include "InstanceScript.h"
#include "MotionMaster.h"
#include "naxxramas.h"
#include "ObjectAccessor.h"
#include "PassiveAI.h"
#include "ScriptedCreature.h"
#include "SpellMgr.h"
#include "SpellScript.h"

/**
 * @brief 技能ID枚举
 */
enum Spells
{
    SPELL_WEB_WRAP              = 28622,    // 蛛网缠绕 - 缠绕玩家
    SPELL_WEB_SPRAY             = 29484,    // 蛛网喷射 - 昏迷所有玩家
    SPELL_POISON_SHOCK          = 28741,    // 剧毒震荡 - 对周围敌人造成自然伤害
    SPELL_NECROTIC_POISON       = 28776,    // 坏死毒液 - 对坦克造成持续自然伤害
    SPELL_FRENZY                = 54123     // 狂暴技能
};

/// 狂暴辅助技能ID(根据难度选择)
#define SPELL_FRENZY_HELPER RAID_MODE(54123,54124)

/**
 * @brief 表情枚举
 */
enum Emotes
{
    EMOTE_SPIDERS           = 0,    // 召唤小蜘蛛表情
    EMOTE_WEB_WRAP          = 1,    // 蛛网缠绕表情
    EMOTE_WEB_SPRAY         = 2     // 蛛网喷射表情
};

/**
 * @brief 生物ID枚举
 */
enum Creatures
{
    NPC_WEB_WRAP                = 16486,    // 蛛网缠绕NPC - 用于包裹玩家
    NPC_SPIDERLING              = 17055,    // 小蜘蛛NPC
};

/// 蛛网缠绕位置的最大数量
#define MAX_WRAP_POSITION  7

/**
 * @brief 蛛网缠绕位置数组
 *
 * 定义了7个固定的墙壁位置,用于将玩家抛射并缠绕
 * 这些位置分布在房间的墙壁上
 */
const Position WrapPositions[MAX_WRAP_POSITION] =
{
    {3453.818f, -3854.651f, 308.7581f, 4.362833f},
    {3535.042f, -3842.383f, 300.795f,  3.179324f},
    {3538.399f, -3846.088f, 299.964f,  4.310297f},
    {3548.464f, -3854.676f, 298.6075f, 4.546609f},
    {3557.663f, -3870.123f, 297.5027f, 3.756433f},
    {3560.546f, -3879.353f, 297.4843f, 2.508937f},
    {3562.535f, -3892.507f, 298.532f,  6.022466f},
};

/**
 * @brief 事件ID枚举
 */
enum Events
{
    EVENT_NONE,     // 无事件
    EVENT_SPRAY,    // 蛛网喷射事件
    EVENT_SHOCK,    // 剧毒震荡事件
    EVENT_POISON,   // 坏死毒液事件
    EVENT_WRAP,     // 蛛网缠绕事件
    EVENT_SUMMON,   // 召唤小蜘蛛事件
};

/// 蛛网缠绕移动速度(单位:码/秒)
const float WEB_WRAP_MOVE_SPEED = 20.0f;

/**
 * @struct WebTargetSelector
 * @brief 蛛网缠绕目标选择器
 *
 * 用于选择合适的蛛网缠绕目标,过滤掉不符合条件的目标
 * 筛选条件:
 * - 必须是玩家(不选择宠物、守护者等)
 * - 不能是当前坦克目标
 * - 不能是已经被缠绕的目标
 */
struct WebTargetSelector
{
    /**
     * @brief 构造函数
     * @param maexxna 麦克斯纳BOSS对象指针
     */
    WebTargetSelector(Unit* maexxna) : _maexxna(maexxna) {}

    /**
     * @brief 目标筛选运算符
     * @param target 待筛选的目标
     * @return true表示目标符合条件,false表示不符合
     *
     * 筛选规则:
     * 1. 只选择玩家(不选择宠物等)
     * 2. 不选择当前坦克(避免BOSS换目标)
     * 3. 不选择已被缠绕的玩家(避免重复缠绕)
     */
    bool operator()(Unit const* target) const
    {
        // 永远不要缠绕非玩家单位(宠物、守护者等)
        if (target->GetTypeId() != TYPEID_PLAYER)
            return false;
        // 永远不要缠绕当前坦克
        if (_maexxna->GetVictim() == target)
            return false;
        // 永远不要缠绕已经被缠绕的目标
        if (target->HasAura(SPELL_WEB_WRAP))
            return false;
        return true;
    }

    private:
        Unit const* _maexxna;    ///< 麦克斯纳BOSS对象指针
};

/**
 * @struct boss_maexxna
 * @brief 麦克斯纳BOSS的AI实现
 *
 * 继承自BossAI,实现了麦克斯纳的完整战斗逻辑,包括:
 * - 蛛网缠绕机制 - 将玩家抛射到墙上
 * - 蛛网喷射 - 周期性昏迷所有玩家
 * - 剧毒震荡和坏死毒液技能
 * - 30%血量狂暴机制
 * - 小蜘蛛召唤
 */
struct boss_maexxna : public BossAI
{
    /**
     * @brief 构造函数
     * @param creature BOSS生物对象指针
     */
    boss_maexxna(Creature* creature) : BossAI(creature, BOSS_MAEXXNA)  {  }

    /**
     * @brief 进入战斗
     * @param who 进入战斗的目标
     *
     * 初始化所有战斗事件计时器
     * 调用时机: BOSS进入战斗时
     */
    void JustEngagedWith(Unit* who) override
    {
        BossAI::JustEngagedWith(who);
        // 安排蛛网缠绕 - 20秒后
        events.ScheduleEvent(EVENT_WRAP, 20s);
        // 安排蛛网喷射 - 40秒后
        events.ScheduleEvent(EVENT_SPRAY, 40s);
        // 安排剧毒震荡 - 5-10秒后随机时间
        events.ScheduleEvent(EVENT_SHOCK, randtime(Seconds(5), Seconds(10)));
        // 安排坏死毒液 - 10-15秒后随机时间
        events.ScheduleEvent(EVENT_POISON, randtime(Seconds(10), Seconds(15)));
        // 安排召唤小蜘蛛 - 30秒后
        events.ScheduleEvent(EVENT_SUMMON, 30s);
    }

    /**
     * @brief 重置BOSS状态
     *
     * 清空事件计时器,并移除所有玩家身上的蛛网缠绕效果
     * 调用时机: BOSS脱离战斗或副本重置时
     */
    void Reset() override
    {
        _Reset();
        // 移除所有玩家身上的蛛网缠绕效果
        instance->DoRemoveAurasDueToSpellOnPlayers(SPELL_WEB_WRAP);
    }

    /**
     * @brief 更新AI逻辑
     * @param diff 距离上次更新的时间差(毫秒)
     *
     * 处理所有战斗事件的执行,包括蛛网缠绕、蛛网喷射、毒液技能和小蜘蛛召唤
     * 同时检查30%血量狂暴触发条件
     * 调用时机: 每个世界更新周期(默认约50ms)
     * 性能注意事项: 该函数会被频繁调用,应避免复杂计算
     */
    void UpdateAI(uint32 diff) override
    {
        // 检查是否有有效目标,没有则返回
        if (!UpdateVictim())
            return;

        // 30%血量以下自动触发狂暴(只触发一次)
        if (HealthBelowPct(30) && !me->HasAura(SPELL_FRENZY_HELPER))
        {
            DoCast(SPELL_FRENZY);
        }

        // 更新事件计时器
        events.Update(diff);

        // 处理所有到期的事件
        while (uint32 eventId = events.ExecuteEvent())
        {
            switch (eventId)
            {
                case EVENT_WRAP:
                {
                    // 选择蛛网缠绕目标列表
                    std::list<Unit*> targets;
                    // 选择1个(10人)或2个(25人)随机目标
                    SelectTargetList(targets, RAID_MODE(1, 2), SelectTargetMethod::Random, 1, WebTargetSelector(me));
                    if (!targets.empty())
                    {
                        Talk(EMOTE_WEB_WRAP);
                        int8 wrapPos = -1;
                        for (Unit* target : targets)
                        {
                            // 为第一个目标随机选择位置
                            if (wrapPos == -1)
                                wrapPos = urand(0, MAX_WRAP_POSITION - 1);
                            else
                                // 后续目标选择不同的位置(避免两个目标缠绕在同一位置)
                                wrapPos = (wrapPos + urand(1, MAX_WRAP_POSITION - 1)) % MAX_WRAP_POSITION;

                            // 移除目标身上的蛛网喷射效果
                            target->RemoveAura(sSpellMgr->GetSpellIdForDifficulty(SPELL_WEB_SPRAY, me));
                            // 在墙壁位置召唤蛛网NPC
                            if (Creature* wrap = DoSummon(NPC_WEB_WRAP, WrapPositions[wrapPos], 70s, TEMPSUMMON_TIMED_DESPAWN))
                            {
                                // 设置蛛网的受害者GUID(会施加缠绕debuff)
                                wrap->AI()->SetGUID(target->GetGUID());
                                // 让目标跳跃飞向墙壁位置
                                target->GetMotionMaster()->MoveJump(WrapPositions[wrapPos], WEB_WRAP_MOVE_SPEED, WEB_WRAP_MOVE_SPEED);
                            }
                        }
                    }
                    // 安排下一次蛛网缠绕,40秒后
                    events.Repeat(Seconds(40));
                    break;
                }
                case EVENT_SPRAY:
                    // 播放蛛网喷射表情
                    Talk(EMOTE_WEB_SPRAY);
                    // 对所有玩家施放蛛网喷射(昏迷效果)
                    DoCastAOE(SPELL_WEB_SPRAY);
                    // 安排下一次蛛网喷射,40秒后
                    events.Repeat(Seconds(40));
                    break;
                case EVENT_SHOCK:
                    // 施放剧毒震荡(范围伤害)
                    DoCastAOE(SPELL_POISON_SHOCK);
                    // 安排下一次剧毒震荡,10-20秒后随机时间
                    events.Repeat(randtime(Seconds(10), Seconds(20)));
                    break;
                case EVENT_POISON:
                    // 对当前目标施放坏死毒液
                    DoCastVictim(SPELL_NECROTIC_POISON);
                    // 安排下一次坏死毒液,10-20秒后随机时间
                    events.Repeat(randtime(Seconds(10), Seconds(20)));
                    break;
                case EVENT_SUMMON:
                    // 播放召唤小蜘蛛表情
                    Talk(EMOTE_SPIDERS);
                    // 随机召唤8-10只小蜘蛛
                    uint8 amount = urand(8, 10);
                    for (uint8 i = 0; i < amount; ++i)
                        // 在BOSS周围4码范围内召唤小蜘蛛
                        DoSummon(NPC_SPIDERLING, me, 4.0f, 5s, TEMPSUMMON_CORPSE_TIMED_DESPAWN);
                    // 安排下一次召唤,40秒后
                    events.Repeat(Seconds(40));
                    break;
            }
        }

        // 进行近战攻击
        DoMeleeAttackIfReady();
    }
};

/**
 * @struct npc_webwrap
 * @brief 蛛网缠绕NPC的AI实现
 *
 * 继承自NullCreatureAI,实现了蛛网缠绕的静态AI逻辑,包括:
 * - 等待受害者跳跃到达后显示蛛网
 * - 对受害者施加缠绕debuff
 * - 蛛网被摧毁时移除缠绕效果
 *
 * 蛛网NPC是静态的,不进行任何主动行为
 * 玩家需要攻击蛛网来解救被缠绕的队友
 */
struct npc_webwrap : public NullCreatureAI
{
    /**
     * @brief 构造函数
     * @param creature 蛛网生物对象指针
     */
    npc_webwrap(Creature* creature) : NullCreatureAI(creature), visibleTimer(0) { }

    ObjectGuid victimGUID;       ///< 被缠绕的受害者GUID
    uint32 visibleTimer;         ///< 显示计时器(等待受害者到达蛛网位置)

    /**
     * @brief 初始化AI
     *
     * 蛛网初始不可见,等待受害者到达后再显示
     * 调用时机: 蛛网生成时
     */
    void InitializeAI() override
    {
        me->SetVisible(false);
    }

    /**
     * @brief 设置受害者GUID
     * @param guid 受害者的GUID
     * @param id 参数ID(未使用)
     *
     * 设置被缠绕的受害者,计算蛛网显示延迟时间
     * 延迟时间根据受害者到蛛网的距离计算
     * 调用时机: BOSS召唤蛛网并设置受害者时
     */
    void SetGUID(ObjectGuid const& guid, int32 /*id*/) override
    {
        if (!guid)
            return;
        victimGUID = guid;
        if (Unit* victim = ObjectAccessor::GetUnit(*me, victimGUID))
        {
            // 计算显示延迟时间 = 距离 / 移动速度 + 0.5秒缓冲
            visibleTimer = (me->GetDistance2d(victim) / WEB_WRAP_MOVE_SPEED + 0.5f) * AsUnderlyingType(IN_MILLISECONDS);
            // 对受害者施加蛛网缠绕debuff
            victim->CastSpell(victim, SPELL_WEB_WRAP, me->GetGUID());
        }
    }

    /**
     * @brief 更新AI逻辑
     * @param diff 距离上次更新的时间差(毫秒)
     *
     * 处理蛛网显示计时器,当受害者到达后显示蛛网
     * 调用时机: 每个世界更新周期
     * 性能注意事项: 该函数会被频繁调用,应避免复杂计算
     */
    void UpdateAI(uint32 diff) override
    {
        if (!visibleTimer)
            return;

        // 计时器到期,显示蛛网
        if (diff >= visibleTimer)
        {
            visibleTimer = 0;
            me->SetVisible(true);
        }
        else
            visibleTimer -= diff;
    }

    /**
     * @brief 蛛网死亡处理
     * @param killer 击杀者
     *
     * 当蛛网被摧毁时,移除受害者身上的缠绕效果
     * 调用时机: 蛛网被击杀时
     */
    void JustDied(Unit* /*killer*/) override
    {
        if (victimGUID)
            if (Unit* victim = ObjectAccessor::GetUnit(*me, victimGUID))
                // 移除受害者身上的蛛网缠绕效果
                victim->RemoveAurasDueToSpell(SPELL_WEB_WRAP, me->GetGUID());

        // 5秒后消失
        me->DespawnOrUnsummon(5s);
    }
};

/**
 * @brief 注册麦克斯纳BOSS脚本
 *
 * 注册BOSS AI和蛛网缠绕NPC AI到脚本系统
 */
void AddSC_boss_maexxna()
{
    RegisterNaxxramasCreatureAI(boss_maexxna);
    RegisterNaxxramasCreatureAI(npc_webwrap);
}
