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
 * @file npcs_special.cpp
 * @brief 特殊NPC脚本模块集合
 *
 * 本模块包含游戏中各种特殊NPC的脚本实现，涵盖以下功能：
 *
 * 主要功能模块：
 * - npc_air_force_bots: 空中防御机器人（检测和攻击飞行玩家）
 * - npc_chicken_cluck: 鸡（隐藏任务触发机制）
 * - npc_dancing_flames: 舞动火焰（仲夏节节日NPC）
 * - npc_torch_tossing_target_bunny_controller: 火把投掷目标控制器
 * - npc_midsummer_bunny_pole: 仲夏节彩带柱
 * - npc_doctor: 医生NPC（治疗任务）
 * - npc_injured_patient: 受伤病人
 * - npc_garments_of_quests: 任务长袍相关NPC
 * - npc_guardian: 守护者
 * - npc_steam_tonk: 蒸汽坦克
 * - npc_tournament_mount: 比赛坐骑
 * - npc_brewfest_reveler: 美酒节狂欢者
 * - npc_wormhole: 虫洞传送NPC
 * - npc_pet_trainer: 宠物训练师
 * - npc_experience: 经验值相关NPC
 * - npc_spring_rabbit: 春节兔子
 * - npc_imp_in_a_ball: 球中的小鬼（占卜物品）
 * - npc_stable_master: 稳定大师
 * - npc_train_wrecker: 火车破坏者（冬日节）
 * - npc_argent_squire_gruntling: 银色侍从/步兵
 * - npc_bountiful_table: 丰收节餐桌
 *
 * 这些脚本实现了游戏中各种特殊交互、节日活动、隐藏任务等复杂功能。
 */

#include "ScriptMgr.h"
#include "CellImpl.h"
#include "CombatAI.h"
#include "Containers.h"
#include "CreatureTextMgr.h"
#include "GameEventMgr.h"
#include "GridNotifiersImpl.h"
#include "Log.h"
#include "MotionMaster.h"
#include "MoveSplineInit.h"
#include "ObjectAccessor.h"
#include "ObjectMgr.h"
#include "PassiveAI.h"
#include "Pet.h"
#include "ScriptedEscortAI.h"
#include "ScriptedGossip.h"
#include "SmartAI.h"
#include "SpellAuras.h"
#include "SpellHistory.h"
#include "SpellMgr.h"
#include "Vehicle.h"
#include "World.h"

/*########
# npc_air_force_bots
#########*/

/**
 * @brief 空中防御机器人类型枚举
 *
 * 定义两种不同类型的空中防御机器人行为模式
 */
enum AirForceBots
{
    TRIPWIRE,   ///< 绊线型机器人：不攻击飞行玩家，检测范围较小（15码）
    ALARMBOT,   ///< 警报机器人：攻击飞行玩家，施放守卫标记，检测范围大（100码）

    SPELL_GUARDS_MARK = 38067  ///< 守卫标记法术ID，用于标记敌对玩家
};

/// 绊线型机器人的检测范围（码）
float constexpr RANGE_TRIPWIRE =  15.0f;
/// 警报机器人的检测范围（码）
float constexpr RANGE_ALARMBOT = 100.0f;

/**
 * @brief 空中防御机器人刷新配置结构体
 *
 * 定义每种空中防御机器人NPC的刷新配置，包括：
 * - 自身NPC Entry ID
 * - 关联的守卫NPC Entry ID
 * - 机器人类型（绊线/警报）
 */
struct AirForceSpawn
{
    uint32 myEntry;       ///< 当前NPC的Entry ID（检测者）
    uint32 otherEntry;    ///< 关联守卫NPC的Entry ID（被召唤的守卫）
    AirForceBots type;    ///< 机器人类型
};

/**
 * @brief 空中防御机器人刷新配置数组
 *
 * 定义了所有空中防御机器人的配置数据，包括联盟、部落和中立阵营的各种机器人。
 * 每个配置包含检测机器人和被召唤的守卫之间的映射关系。
 */
AirForceSpawn constexpr airforceSpawns[] =
{
    {2614,  15241, ALARMBOT}, // 空军警报机器人（联盟）
    {2615,  15242, ALARMBOT}, // 空军警报机器人（部落）
    {21974, 21976, ALARMBOT}, // 空军警报机器人（52区）
    {21993, 15242, ALARMBOT}, // 空军守卫哨站（部落 - 蝙蝠骑士）
    {21996, 15241, ALARMBOT}, // 空军守卫哨站（联盟 - 狮鹫）
    {21997, 21976, ALARMBOT}, // 空军守卫哨站（地精 - 52区 - 飞艇）
    {21999, 15241, TRIPWIRE}, // 空军绊线 - 屋顶（联盟）
    {22001, 15242, TRIPWIRE}, // 空军绊线 - 屋顶（部落）
    {22002, 15242, TRIPWIRE}, // 空军绊线 - 地面（部落）
    {22003, 15241, TRIPWIRE}, // 空军绊线 - 地面（联盟）
    {22063, 21976, TRIPWIRE}, // 空军绊线 - 屋顶（地精 - 52区）
    {22065, 22064, ALARMBOT}, // 空军守卫哨站（虚灵 - 风暴尖塔）
    {22066, 22067, ALARMBOT}, // 空军守卫哨站（占星者 - 龙鹰）
    {22068, 22064, TRIPWIRE}, // 空军绊线 - 屋顶（虚灵 - 风暴尖塔）
    {22069, 22064, ALARMBOT}, // 空军警报机器人（风暴尖塔）
    {22070, 22067, TRIPWIRE}, // 空军绊线 - 屋顶（占星者）
    {22071, 22067, ALARMBOT}, // 空军警报机器人（占星者）
    {22078, 22077, ALARMBOT}, // 空军警报机器人（奥尔多）
    {22079, 22077, ALARMBOT}, // 空军守卫哨站（奥尔多 - 狮鹫）
    {22080, 22077, TRIPWIRE}, // 空军绊线 - 屋顶（奥尔多）
    {22086, 22085, ALARMBOT}, // 空军警报机器人（孢子村）
    {22087, 22085, ALARMBOT}, // 空军守卫哨站（孢子村 - 孢子蝠）
    {22088, 22085, TRIPWIRE}, // 空军绊线 - 屋顶（孢子村）
    {22090, 22089, ALARMBOT}, // 空军守卫哨站（托什雷的基地 - 飞行器）
    {22124, 22122, ALARMBOT}, // 空军警报机器人（塞纳里奥）
    {22125, 22122, ALARMBOT}, // 空军守卫哨站（塞纳里奥 - 风暴乌鸦）
    {22126, 22122, ALARMBOT}  // 空军绊线 - 屋顶（塞纳里奥远征队）
};

/**
 * @brief 空中防御机器人脚本类
 *
 * 实现空中防御机器人的AI逻辑，用于检测并召唤守卫攻击敌对玩家。
 * 主要功能：
 * - 检测进入范围内的敌对玩家
 * - 召唤对应的守卫NPC
 * - 对警报型机器人：施放守卫标记法术
 * - 对绊线型机器人：不检测飞行玩家
 *
 * 该系统用于防止玩家在某些区域非法飞行或进入禁区。
 */
class npc_air_force_bots : public CreatureScript
{
public:
    /**
     * @brief 构造函数
     *
     * 初始化空中防御机器人脚本，注册脚本名称为"npc_air_force_bots"
     */
    npc_air_force_bots() : CreatureScript("npc_air_force_bots") { }

    /**
     * @brief 空中防御机器人AI结构体
     *
     * 继承自NullCreatureAI，实现被动检测和守卫召唤逻辑。
     * 不进行主动战斗行为，而是召唤守卫进行攻击。
     */
    struct npc_air_force_botsAI : public NullCreatureAI
    {
        /**
         * @brief 根据NPC Entry ID查找对应的刷新配置
         * @param entry NPC的Entry ID
         * @return AirForceSpawn const& 返回对应的刷新配置引用
         *
         * 遍历配置数组，查找匹配的机器人配置。
         * 如果找不到配置或配置无效，将触发断言错误。
         *
         * @note 此函数在AI构造时调用，性能影响小
         */
        static AirForceSpawn const& FindSpawnFor(uint32 entry)
        {
            for (AirForceSpawn const& spawn : airforceSpawns)
            {
                if (spawn.myEntry == entry)
                {
                    ASSERT_NODEBUGINFO(sObjectMgr->GetCreatureTemplate(spawn.otherEntry), "Invalid creature entry %u in 'npc_air_force_bots' script", spawn.otherEntry);
                    return spawn;
                }
            }
            ASSERT_NODEBUGINFO(false, "Unhandled creature with entry %u is assigned 'npc_air_force_bots' script", entry);
        }

        /**
         * @brief AI构造函数
         * @param creature NPC生物对象指针
         *
         * 初始化AI实例，查找并缓存当前NPC的配置数据
         */
        npc_air_force_botsAI(Creature* creature) : NullCreatureAI(creature), _spawn(FindSpawnFor(creature->GetEntry())) {}

        /**
         * @brief 获取或召唤守卫NPC
         * @return Creature* 返回守卫NPC指针，如果召唤失败则返回nullptr
         *
         * 首先尝试获取已存在的守卫，如果不存在则召唤新的守卫。
         * 守卫会在脱离战斗5分钟后自动消失。
         *
         * @note 使用TEMPSUMMON_TIMED_DESPAWN_OUT_OF_COMBAT确保守卫不会永久存在
         */
        Creature* GetOrSummonGuard()
        {
            Creature* guard = ObjectAccessor::GetCreature(*me, _myGuard);

            if (!guard && (guard = me->SummonCreature(_spawn.otherEntry, 0.0f, 0.0f, 0.0f, 0.0f, TEMPSUMMON_TIMED_DESPAWN_OUT_OF_COMBAT, 5min)))
                _myGuard = guard->GetGUID();

            return guard;
        }

        /**
         * @brief AI更新函数
         * @param diff 距离上次更新的时间间隔（毫秒）
         *
         * 处理待攻击目标列表，为每个目标召唤守卫并使其进入战斗。
         * 对警报型机器人，还会施放守卫标记法术。
         *
         * @note 此函数每帧调用，需保持高效
         */
        void UpdateAI(uint32 /*diff*/) override
        {
            // 如果没有待攻击目标，直接返回
            if (_toAttack.empty())
                return;

            // 获取或召唤守卫
            Creature* guard = GetOrSummonGuard();
            if (!guard)
                return;

            // 如果守卫已死亡，保留目标列表等待守卫生成
            if (!guard->IsAlive())
                return;

            // 遍历所有待攻击目标
            for (ObjectGuid guid : _toAttack)
            {
                Unit* target = ObjectAccessor::GetUnit(*me, guid);
                if (!target)
                    continue;
                // 跳过已经与守卫交战的目标
                if (guard->IsEngagedBy(target))
                    continue;

                // 使守卫攻击目标
                guard->EngageWithTarget(target);
                // 如果是警报型机器人，施放守卫标记法术
                if (_spawn.type == ALARMBOT)
                    guard->CastSpell(target, SPELL_GUARDS_MARK, true);
            }

            // 清空待攻击列表
            _toAttack.clear();
        }

        /**
         * @brief 视线检测函数
         * @param who 进入视线的单位对象
         *
         * 当单位进入NPC的视线范围时调用此函数。
         * 检测逻辑：
         * 1. 只检测玩家
         * 2. 检查是否已在待攻击列表中
         * 3. 检查距离（根据机器人类型）
         * 4. 检查敌对关系
         * 5. 检查是否为有效攻击目标
         * 6. 绊线型机器人额外检查飞行状态
         *
         * @note 此函数可能频繁调用，已优化检测顺序
         */
        void MoveInLineOfSight(Unit* who) override
        {
            // 守卫只会对玩家生成
            if (who->GetTypeId() != TYPEID_PLAYER)
                return;

            // 如果已计划攻击此玩家，跳过重复检测
            if (_toAttack.find(who->GetGUID()) != _toAttack.end())
                return;

            // 检查是否在检测范围内
            if (!who->IsWithinDistInMap(me, (_spawn.type == ALARMBOT) ? RANGE_ALARMBOT : RANGE_TRIPWIRE))
                return;

            // 检查是否敌对
            if (!(me->IsHostileTo(who) || who->IsHostileTo(me)))
                return;

            // 检查是否为有效攻击目标
            if (!me->IsValidAttackTarget(who))
                return;

            // 绊线型机器人不检测飞行玩家
            if ((_spawn.type == TRIPWIRE) && who->IsFlying())
                return;

            // 将目标添加到待攻击列表
            _toAttack.insert(who->GetGUID());
        }

        private:
            AirForceSpawn const& _spawn;           ///< 当前机器人的配置数据引用
            ObjectGuid _myGuard;                   ///< 当前召唤的守卫GUID
            std::unordered_set<ObjectGuid> _toAttack;  ///< 待攻击目标列表

    };

    /**
     * @brief 获取AI实例工厂函数
     * @param creature 需要创建AI的生物对象指针
     * @return CreatureAI* 返回新创建的空中防御机器人AI实例
     */
    CreatureAI* GetAI(Creature* creature) const override
    {
        return new npc_air_force_botsAI(creature);
    }
};

/*########
# npc_chicken_cluck
#########*/

/**
 * @brief 鸡NPC枚举定义
 *
 * 定义鸡NPC的表情和任务ID
 */
enum ChickenCluck
{
    EMOTE_HELLO_A       = 0,    ///< 联盟玩家触发时的问候表情
    EMOTE_HELLO_H       = 1,    ///< 部落玩家触发时的问候表情
    EMOTE_CLUCK_TEXT    = 2,    ///< 咯咯叫文本表情

    QUEST_CLUCK         = 3861  ///< 隐藏任务"CLUCK!"的任务ID
};

/**
 * @brief 鸡NPC脚本类
 *
 * 实现隐藏彩蛋任务"CLUCK!"的触发机制。
 * 这是一个经典的魔兽世界彩蛋，玩家需要对鸡使用"/chicken"表情多次，
 * 有极低几率触发鸡变成任务NPC，提供特殊任务。
 *
 * 功能特点：
 * - 玩家对鸡使用"/chicken"表情有1/30几率触发任务
 * - 任务标志会在2分钟后自动重置
 * - 完成任务后需要使用"/cheer"表情才能再次交互
 */
class npc_chicken_cluck : public CreatureScript
{
public:
    /**
     * @brief 构造函数
     *
     * 初始化鸡NPC脚本，注册脚本名称为"npc_chicken_cluck"
     */
    npc_chicken_cluck() : CreatureScript("npc_chicken_cluck") { }

    /**
     * @brief 鸡NPC的AI结构体
     *
     * 实现鸡的特殊行为逻辑，包括隐藏任务的触发机制。
     */
    struct npc_chicken_cluckAI : public ScriptedAI
    {
        /**
         * @brief AI构造函数
         * @param creature NPC生物对象指针
         *
         * 初始化AI并设置重置计时器
         */
        npc_chicken_cluckAI(Creature* creature) : ScriptedAI(creature)
        {
            Initialize();
        }

        /**
         * @brief 初始化函数
         *
         * 重置标志计时器为120秒（2分钟）
         */
        void Initialize()
        {
            ResetFlagTimer = 120000;
        }

        uint32 ResetFlagTimer;  ///< 任务标志重置计时器（毫秒），2分钟后自动重置

        /**
         * @brief 重置函数
         *
         * 重置鸡NPC的状态：
         * - 重置计时器
         * - 设置阵营为猎物阵营（中立被动）
         * - 移除任务给予者标志
         */
        void Reset() override
        {
            Initialize();
            me->SetFaction(FACTION_PREY);
            me->RemoveNpcFlag(UNIT_NPC_FLAG_QUESTGIVER);
        }

        /**
         * @brief 进入战斗回调
         * @param who 进入战斗的目标（未使用）
         *
         * 空实现，鸡不会主动战斗
         */
        void JustEngagedWith(Unit* /*who*/) override { }

        /**
         * @brief AI更新函数
         * @param diff 距离上次更新的时间间隔（毫秒）
         *
         * 主要功能：
         * - 如果鸡已成为任务NPC，2分钟后自动重置状态
         * - 处理战斗逻辑（如果有敌人）
         *
         * @note 计时器确保每个玩家都需要重新触发任务
         */
        void UpdateAI(uint32 diff) override
        {
            // 如果已成为任务NPC，在2分钟后重置标志，让下一个玩家重新触发事件
            if (me->HasNpcFlag(UNIT_NPC_FLAG_QUESTGIVER))
            {
                if (ResetFlagTimer <= diff)
                {
                    EnterEvadeMode();
                    return;
                }
                else
                    ResetFlagTimer -= diff;
            }

            // 如果有战斗目标，进行近战攻击
            if (UpdateVictim())
                DoMeleeAttackIfReady();
        }

        /**
         * @brief 接收表情回调
         * @param player 发送表情的玩家
         * @param emote 表情类型ID
         *
         * 处理玩家对鸡使用的表情：
         * - /chicken (小鸡表情)：1/30几率触发任务，需要玩家未接受过任务
         * - /cheer (欢呼表情)：任务完成后再次触发交互
         *
         * @note 这是魔兽世界中最著名的隐藏彩蛋之一
         */
        void ReceiveEmote(Player* player, uint32 emote) override
        {
            switch (emote)
            {
                case TEXT_EMOTE_CHICKEN:  // /chicken 表情
                    // 玩家未接受过任务且有1/30几率触发
                    if (player->GetQuestStatus(QUEST_CLUCK) == QUEST_STATUS_NONE && rand32() % 30 == 1)
                    {
                        me->SetNpcFlag(UNIT_NPC_FLAG_QUESTGIVER);
                        me->SetFaction(FACTION_FRIENDLY);
                        // 根据玩家阵营显示不同的问候语
                        Talk(player->GetTeam() == HORDE ? EMOTE_HELLO_H : EMOTE_HELLO_A);
                    }
                    break;
                case TEXT_EMOTE_CHEER:  // /cheer 表情
                    // 任务完成后才能再次交互
                    if (player->GetQuestStatus(QUEST_CLUCK) == QUEST_STATUS_COMPLETE)
                    {
                        me->SetNpcFlag(UNIT_NPC_FLAG_QUESTGIVER);
                        me->SetFaction(FACTION_FRIENDLY);
                        Talk(EMOTE_CLUCK_TEXT);
                    }
                    break;
            }
        }

        /**
         * @brief 任务接受回调
         * @param player 接受任务的玩家（未使用）
         * @param quest 被接受的任务对象
         *
         * 当玩家接受"CLUCK!"任务时，重置鸡的状态
         */
        void OnQuestAccept(Player* /*player*/, Quest const* quest) override
        {
            if (quest->GetQuestId() == QUEST_CLUCK)
                Reset();
        }

        /**
         * @brief 任务奖励回调
         * @param player 完成任务的玩家（未使用）
         * @param quest 被完成的任务对象
         * @param opt 奖励选项（未使用）
         *
         * 当玩家完成"CLUCK!"任务领取奖励时，重置鸡的状态
         */
        void OnQuestReward(Player* /*player*/, Quest const* quest, uint32 /*opt*/) override
        {
            if (quest->GetQuestId() == QUEST_CLUCK)
                Reset();
        }
    };

    /**
     * @brief 获取AI实例工厂函数
     * @param creature 需要创建AI的生物对象指针
     * @return CreatureAI* 返回新创建的鸡NPC AI实例
     */
    CreatureAI* GetAI(Creature* creature) const override
    {
        return new npc_chicken_cluckAI(creature);
    }
};

/*######
## npc_dancing_flames
######*/

/**
 * @brief 舞动火焰法术枚举定义
 *
 * 定义舞动火焰NPC使用的法术ID
 */
enum DancingFlames
{
    SPELL_SUMMON_BRAZIER    = 45423,  ///< 召唤火盆法术
    SPELL_BRAZIER_DANCE     = 45427,  ///< 火盆舞蹈法术
    SPELL_FIERY_SEDUCTION   = 47057   ///< 火焰诱惑法术（对玩家施放）
};

/**
 * @brief 舞动火焰NPC AI结构体
 *
 * 仲夏节活动的特殊NPC，会在火盆旁跳舞并响应玩家的表情。
 * 主要功能：
 * - 召唤火盆并跳舞
 * - 响应玩家的各种表情（亲吻、挥手、鞠躬、笑话、跳舞）
 * - 对跳舞表情施放火焰诱惑法术
 *
 * @note 表情响应有1.5秒延迟，连续发送表情会取消前一个表情的响应
 */
struct npc_dancing_flames : public ScriptedAI
{
    /**
     * @brief 构造函数
     * @param creature NPC生物对象指针
     */
    npc_dancing_flames(Creature* creature) : ScriptedAI(creature) { }

    /**
     * @brief 重置函数
     *
     * 初始化舞动火焰的状态：
     * - 召唤火盆
     * - 施放火盆舞蹈法术
     * - 设置舞蹈表情状态
     * - 稍微提升高度（1.05码）
     */
    void Reset() override
    {
        DoCastSelf(SPELL_SUMMON_BRAZIER, true);
        DoCastSelf(SPELL_BRAZIER_DANCE, false);
        me->SetEmoteState(EMOTE_STATE_DANCE);
        float x, y, z;
        me->GetPosition(x, y, z);
        me->Relocate(x, y, z + 1.05f);
    }

    /**
     * @brief AI更新函数
     * @param diff 距离上次更新的时间间隔（毫秒）
     *
     * 更新任务调度器，处理延迟的表情响应
     */
    void UpdateAI(uint32 diff) override
    {
        _scheduler.Update(diff);
    }

    /**
     * @brief 接收表情回调
     * @param player 发送表情的玩家
     * @param emote 表情类型ID
     *
     * 处理玩家对舞动火焰使用的表情：
     * - /kiss（亲吻）：1.5秒后回应害羞表情
     * - /wave（挥手）：1.5秒后回应挥手表情
     * - /bow（鞠躬）：1.5秒后回应鞠躬表情
     * - /joke（笑话）：1.5秒后回应大笑表情
     * - /dance（跳舞）：立即施放火焰诱惑法术
     *
     * @note 表情响应不是即时的，大约延迟1500毫秒
     * @note 如果玩家发送表情太快，火焰会取消之前的响应，只响应最新的表情
     * @note 需要在视线内且距离30码内才能响应
     */
    void ReceiveEmote(Player* player, uint32 emote) override
    {
        // 检查是否在视线内且距离在30码内
        if (me->IsWithinLOS(player->GetPositionX(), player->GetPositionY(), player->GetPositionZ()) && me->IsWithinDistInMap(player, 30.0f))
        {
            // 表情响应不是即时的，大约1500毫秒后回应
            // 如果先/bow然后/wave，在舞动火焰鞠躬回应之前，她不会鞠躬只会挥手
            // 如果玩家发送表情太快，她不会响应
            // 这意味着她只会用最新的表情替换当前计划的事件
            _scheduler.CancelAll();

            switch (emote)
            {
                case TEXT_EMOTE_KISS:  // 亲吻表情
                    _scheduler.Schedule(1500ms, [this](TaskContext /*context*/)
                    {
                        me->HandleEmoteCommand(EMOTE_ONESHOT_SHY);  // 回应害羞表情
                    });
                    break;
                case TEXT_EMOTE_WAVE:  // 挥手表情
                    _scheduler.Schedule(1500ms, [this](TaskContext /*context*/)
                    {
                        me->HandleEmoteCommand(EMOTE_ONESHOT_WAVE);  // 回应挥手
                    });
                    break;
                case TEXT_EMOTE_BOW:  // 鞠躬表情
                    _scheduler.Schedule(1500ms, [this](TaskContext /*context*/)
                    {
                        me->HandleEmoteCommand(EMOTE_ONESHOT_BOW);  // 回应鞠躬
                    });
                    break;
                case TEXT_EMOTE_JOKE:  // 笑话表情
                    _scheduler.Schedule(1500ms, [this](TaskContext /*context*/)
                    {
                        me->HandleEmoteCommand(EMOTE_ONESHOT_LAUGH);  // 回应大笑
                    });
                    break;
                case TEXT_EMOTE_DANCE:  // 跳舞表情
                    // 如果玩家没有火焰诱惑光环，施放法术
                    if (!player->HasAura(SPELL_FIERY_SEDUCTION))
                    {
                        DoCast(player, SPELL_FIERY_SEDUCTION, true);
                        me->SetFacingTo(me->GetAbsoluteAngle(player));  // 面向玩家
                    }
                    break;
            }
        }
    }

private:
    TaskScheduler _scheduler;  ///< 任务调度器，用于延迟表情响应
};

/*######
## npc_torch_tossing_target_bunny_controller
######*/

/**
 * @brief 火把投掷目标枚举
 */
enum TorchTossingTarget
{
    SPELL_TORCH_TARGET_PICKER      = 45907  ///< 火把目标选择器法术
};

/**
 * @brief 火把投掷目标兔子控制器脚本类
 *
 * 仲夏节活动的辅助NPC，用于控制火把投掷目标的选择。
 * 定期施放目标选择法术，管理火把投掷小游戏的逻辑。
 */
class npc_torch_tossing_target_bunny_controller : public CreatureScript
{
public:
    /**
     * @brief 构造函数
     */
    npc_torch_tossing_target_bunny_controller() : CreatureScript("npc_torch_tossing_target_bunny_controller") { }

    /**
     * @brief 火把投掷目标兔子控制器AI结构体
     */
    struct npc_torch_tossing_target_bunny_controllerAI : public ScriptedAI
    {
        /**
         * @brief 构造函数
         * @param creature NPC生物对象指针
         */
        npc_torch_tossing_target_bunny_controllerAI(Creature* creature) : ScriptedAI(creature) { }

        /**
         * @brief 重置函数
         *
         * 设置定时施放火把目标选择器法术的调度：
         * - 2秒后开始第一次施放
         * - 之后每5秒重复施放
         * - 每次施放会在3秒后再施放一次（总共每周期施放2次）
         */
        void Reset() override
        {
            _scheduler.Schedule(Seconds(2), [this](TaskContext context)
            {
                me->CastSpell(nullptr, SPELL_TORCH_TARGET_PICKER);
                _scheduler.Schedule(Seconds(3), [this](TaskContext /*context*/)
                {
                    me->CastSpell(nullptr, SPELL_TORCH_TARGET_PICKER);
                });
                context.Repeat(Seconds(5));
            });
        }

        /**
         * @brief AI更新函数
         * @param diff 距离上次更新的时间间隔（毫秒）
         */
        void UpdateAI(uint32 diff) override
        {
            _scheduler.Update(diff);
        }

    private:
        TaskScheduler _scheduler;  ///< 任务调度器
    };

    /**
     * @brief 获取AI实例工厂函数
     * @param creature 需要创建AI的生物对象指针
     * @return CreatureAI* 返回新创建的AI实例
     */
    CreatureAI* GetAI(Creature* creature) const override
    {
        return new npc_torch_tossing_target_bunny_controllerAI(creature);
    }
};

/*######
## npc_midsummer_bunny_pole
######*/

enum RibbonPoleData
{
    GO_RIBBON_POLE              = 181605,
    SPELL_RIBBON_DANCE_COSMETIC = 29726,
    SPELL_RED_FIRE_RING         = 46836,
    SPELL_BLUE_FIRE_RING        = 46842,
    EVENT_CAST_RED_FIRE_RING    = 1,
    EVENT_CAST_BLUE_FIRE_RING   = 2
};

class npc_midsummer_bunny_pole : public CreatureScript
{
public:
    npc_midsummer_bunny_pole() : CreatureScript("npc_midsummer_bunny_pole") { }

    struct npc_midsummer_bunny_poleAI : public ScriptedAI
    {
        npc_midsummer_bunny_poleAI(Creature* creature) : ScriptedAI(creature)
        {
            Initialize();
        }

        void Initialize()
        {
            events.Reset();
            running = false;
        }

        void Reset() override
        {
            Initialize();
        }

        void DoAction(int32 /*action*/) override
        {
            // Don't start event if it's already running.
            if (running)
                return;

            running = true;
            events.ScheduleEvent(EVENT_CAST_RED_FIRE_RING, 1ms);
        }

        bool checkNearbyPlayers()
        {
            // Returns true if no nearby player has aura "Test Ribbon Pole Channel".
            std::list<Player*> players;
            Trinity::UnitAuraCheck check(true, SPELL_RIBBON_DANCE_COSMETIC);
            Trinity::PlayerListSearcher<Trinity::UnitAuraCheck> searcher(me, players, check);
            Cell::VisitWorldObjects(me, searcher, 10.0f);

            return players.empty();
        }

        void UpdateAI(uint32 diff) override
        {
            if (!running)
                return;

            events.Update(diff);

            switch (events.ExecuteEvent())
            {
            case EVENT_CAST_RED_FIRE_RING:
            {
                if (checkNearbyPlayers())
                {
                    Reset();
                    return;
                }

                if (GameObject* go = me->FindNearestGameObject(GO_RIBBON_POLE, 10.0f))
                    me->CastSpell(go, SPELL_RED_FIRE_RING, true);

                events.ScheduleEvent(EVENT_CAST_BLUE_FIRE_RING, 5s);
            }
            break;
            case EVENT_CAST_BLUE_FIRE_RING:
            {
                if (checkNearbyPlayers())
                {
                    Reset();
                    return;
                }

                if (GameObject* go = me->FindNearestGameObject(GO_RIBBON_POLE, 10.0f))
                    me->CastSpell(go, SPELL_BLUE_FIRE_RING, true);

                events.ScheduleEvent(EVENT_CAST_RED_FIRE_RING, 5s);
            }
            break;
            }
        }

    private:
        EventMap events;
        bool running;
    };

    CreatureAI* GetAI(Creature* creature) const override
    {
        return new npc_midsummer_bunny_poleAI(creature);
    }
};

/*######
## Triage quest - 分类救治任务
######*/

/**
 * @brief 医生NPC枚举定义
 *
 * 定义医生任务中使用的常量
 */
enum Doctor
{
    SAY_DOC             = 0,    ///< 医生对话ID

    DOCTOR_ALLIANCE     = 12939,  ///< 联盟医生NPC ID (Gustaf Vanhowzen)
    DOCTOR_HORDE        = 12920,  ///< 部落医生NPC ID (Gregory Victor)
    ALLIANCE_COORDS     = 7,      ///< 联盟坐标点数量
    HORDE_COORDS        = 6       ///< 部落坐标点数量
};

/**
 * @brief 联盟病床位置坐标数组
 *
 * 定义联盟营地中7个病床的位置，用于召唤受伤士兵
 */
Position const AllianceCoords[]=
{
    {-3757.38f, -4533.05f, 14.16f, 3.62f},  // 从入口看最远右上铺位
    {-3754.36f, -4539.13f, 14.16f, 5.13f},  // 最远左上铺位
    {-3749.54f, -4540.25f, 14.28f, 3.34f},  // 最右侧铺位
    {-3742.10f, -4536.85f, 14.28f, 3.64f},  // 靠近入口右侧铺位
    {-3755.89f, -4529.07f, 14.05f, 0.57f},  // 最左侧铺位
    {-3749.51f, -4527.08f, 14.07f, 5.26f},  // 中左侧铺位
    {-3746.37f, -4525.35f, 14.16f, 5.22f},  // 靠近入口左侧铺位
};

/// 联盟士兵跑向的X坐标
#define A_RUNTOX -3742.96f
/// 联盟士兵跑向的Y坐标
#define A_RUNTOY -4531.52f
/// 联盟士兵跑向的Z坐标
#define A_RUNTOZ 11.91f

/**
 * @brief 部落病床位置坐标数组
 *
 * 定义部落营地中6个病床的位置，用于召唤受伤士兵
 */
Position const HordeCoords[]=
{
    {-1013.75f, -3492.59f, 62.62f, 4.34f},  // 左后
    {-1017.72f, -3490.92f, 62.62f, 4.34f},  // 右后
    {-1015.77f, -3497.15f, 62.82f, 4.34f},  // 左中
    {-1019.51f, -3495.49f, 62.82f, 4.34f},  // 右中
    {-1017.25f, -3500.85f, 62.98f, 4.34f},  // 左前
    {-1020.95f, -3499.21f, 62.98f, 4.34f}   // 右前
};

/// 部落士兵跑向的X坐标
#define H_RUNTOX -1016.44f
/// 部落士兵跑向的Y坐标
#define H_RUNTOY -3508.48f
/// 部落士兵跑向的Z坐标
#define H_RUNTOZ 62.96f

/**
 * @brief 联盟士兵NPC ID数组
 *
 * 定义三种不同受伤程度的联盟士兵NPC ID
 */
uint32 const AllianceSoldierId[3] =
{
    12938,  ///< 受伤的联盟士兵
    12936,  ///< 重伤的联盟士兵
    12937   ///< 垂死的联盟士兵
};

/**
 * @brief 部落士兵NPC ID数组
 *
 * 定义三种不同受伤程度的部落士兵NPC ID
 */
uint32 const HordeSoldierId[3] =
{
    12923,  ///< 受伤的士兵
    12924,  ///< 重伤的士兵
    12925   ///< 垂死的士兵
};

/*######
## npc_doctor (handles both Gustaf Vanhowzen and Gregory Victor)
######*/
/**
 * @brief 医生NPC脚本类
 *
 * 实现联盟和部落医生NPC的功能，用于"分类救治"任务（Triage quest）。
 * 这是一个经典的魔兽世界任务，要求玩家在限定时间内救治15名受伤士兵，
 * 最多只能让6名士兵死亡。
 *
 * 主要功能：
 * - 管理受伤士兵的召唤
 * - 追踪玩家救治进度
 * - 判定任务成功或失败
 *
 * @note 此脚本同时处理联盟医生Gustaf Vanhowzen和部落医生Gregory Victor
 */
class npc_doctor : public CreatureScript
{
public:
    /**
     * @brief 构造函数
     */
    npc_doctor() : CreatureScript("npc_doctor") { }

    /**
     * @brief 医生NPC的AI结构体
     */
    struct npc_doctorAI : public ScriptedAI
    {
        /**
         * @brief 构造函数
         * @param creature NPC生物对象指针
         */
        npc_doctorAI(Creature* creature) : ScriptedAI(creature)
        {
            Initialize();
        }

        /**
         * @brief 初始化函数
         *
         * 重置所有成员变量到初始状态
         */
        void Initialize()
        {
            PlayerGUID.Clear();

            SummonPatientTimer = 10000;
            SummonPatientCount = 0;
            PatientDiedCount = 0;
            PatientSavedCount = 0;

            Patients.clear();
            Coordinates.clear();

            Event = false;
        }

        ObjectGuid PlayerGUID;          ///< 触发任务的玩家GUID
        uint32 SummonPatientTimer;      ///< 召唤病人的计时器（毫秒）
        uint32 SummonPatientCount;      ///< 已召唤的病人数量
        uint32 PatientDiedCount;        ///< 死亡的病人数量
        uint32 PatientSavedCount;       ///< 救治成功的病人数量
        bool Event;                     ///< 事件是否正在运行

        GuidList Patients;              ///< 当前病人的GUID列表
        std::vector<Position const*> Coordinates;  ///< 可用的坐标点列表

        /**
         * @brief 重置函数
         *
         * 重置AI状态并移除不可交互标志
         */
        void Reset() override
        {
            Initialize();
            me->RemoveUnitFlag(UNIT_FLAG_UNINTERACTIBLE);
        }

        /**
         * @brief 开始任务事件
         * @param player 触发任务的玩家
         *
         * 初始化任务事件，根据医生阵营加载对应的坐标点
         */
        void BeginEvent(Player* player)
        {
            PlayerGUID = player->GetGUID();

            SummonPatientTimer = 10000;
            SummonPatientCount = 0;
            PatientDiedCount = 0;
            PatientSavedCount = 0;

            // 根据医生阵营加载对应的坐标点
            switch (me->GetEntry())
            {
                case DOCTOR_ALLIANCE:
                    for (uint8 i = 0; i < ALLIANCE_COORDS; ++i)
                        Coordinates.push_back(&AllianceCoords[i]);
                    break;
                case DOCTOR_HORDE:
                    for (uint8 i = 0; i < HORDE_COORDS; ++i)
                        Coordinates.push_back(&HordeCoords[i]);
                    break;
            }

            Event = true;
            me->SetUnitFlag(UNIT_FLAG_UNINTERACTIBLE);  // 任务期间不可交互
        }

        /**
         * @brief 病人死亡回调
         * @param point 病人死亡的位置
         *
         * 当病人死亡时调用，增加死亡计数。
         * 如果死亡超过5人，任务失败。
         */
        void PatientDied(Position const* point)
        {
            Player* player = ObjectAccessor::GetPlayer(*me, PlayerGUID);
            if (player && ((player->GetQuestStatus(6624) == QUEST_STATUS_INCOMPLETE) || (player->GetQuestStatus(6622) == QUEST_STATUS_INCOMPLETE)))
            {
                ++PatientDiedCount;

                // 死亡超过5人，任务失败
                if (PatientDiedCount > 5 && Event)
                {
                    if (player->GetQuestStatus(6624) == QUEST_STATUS_INCOMPLETE)
                        player->FailQuest(6624);
                    else if (player->GetQuestStatus(6622) == QUEST_STATUS_INCOMPLETE)
                        player->FailQuest(6622);

                    Reset();
                    return;
                }

                // 将坐标点放回队列供重复使用
                Coordinates.push_back(point);
            }
            else
                // 如果没有玩家或玩家放弃了任务，重置事件
                Reset();
        }

        /**
         * @brief 病人救治成功回调
         * @param soldier 被救治的士兵（未使用）
         * @param player 救治病人的玩家
         * @param point 病人的位置
         *
         * 当病人被成功救治时调用，增加救治成功计数。
         * 如果救治成功达到15人，任务完成。
         */
        void PatientSaved(Creature* /*soldier*/, Player* player, Position const* point)
        {
            if (player && PlayerGUID == player->GetGUID())
            {
                if ((player->GetQuestStatus(6624) == QUEST_STATUS_INCOMPLETE) || (player->GetQuestStatus(6622) == QUEST_STATUS_INCOMPLETE))
                {
                    ++PatientSavedCount;

                    if (PatientSavedCount == 15)
                    {
                        if (!Patients.empty())
                        {
                            for (GuidList::const_iterator itr = Patients.begin(); itr != Patients.end(); ++itr)
                            {
                                if (Creature* patient = ObjectAccessor::GetCreature(*me, *itr))
                                    patient->setDeathState(JUST_DIED);
                            }
                        }

                        if (player->GetQuestStatus(6624) == QUEST_STATUS_INCOMPLETE)
                            player->AreaExploredOrEventHappens(6624);
                        else if (player->GetQuestStatus(6622) == QUEST_STATUS_INCOMPLETE)
                            player->AreaExploredOrEventHappens(6622);

                        Reset();
                        return;
                    }

                    Coordinates.push_back(point);
                }
            }
        }

        void UpdateAI(uint32 diff) override;

        void JustEngagedWith(Unit* /*who*/) override { }

        void OnQuestAccept(Player* player, Quest const* quest) override
        {
            if ((quest->GetQuestId() == 6624) || (quest->GetQuestId() == 6622))
                BeginEvent(player);
        }
    };

    CreatureAI* GetAI(Creature* creature) const override
    {
        return new npc_doctorAI(creature);
    }
};

/*#####
## npc_injured_patient (handles all the patients, no matter Horde or Alliance)
#####*/

class npc_injured_patient : public CreatureScript
{
public:
    npc_injured_patient() : CreatureScript("npc_injured_patient") { }

    struct npc_injured_patientAI : public ScriptedAI
    {
        npc_injured_patientAI(Creature* creature) : ScriptedAI(creature)
        {
            Initialize();
        }

        void Initialize()
        {
            DoctorGUID.Clear();
            Coord = nullptr;
        }

        ObjectGuid DoctorGUID;
        Position const* Coord;

        void Reset() override
        {
            Initialize();

            //no select
            me->RemoveUnitFlag(UNIT_FLAG_UNINTERACTIBLE);

            //no regen health
            me->SetUnitFlag(UNIT_FLAG_IN_COMBAT);

            //to make them lay with face down
            me->SetStandState(UNIT_STAND_STATE_DEAD);

            uint32 mobId = me->GetEntry();

            switch (mobId)
            {                                                   //lower max health
                case 12923:
                case 12938:                                     //Injured Soldier
                    me->SetHealth(me->CountPctFromMaxHealth(75));
                    break;
                case 12924:
                case 12936:                                     //Badly injured Soldier
                    me->SetHealth(me->CountPctFromMaxHealth(50));
                    break;
                case 12925:
                case 12937:                                     //Critically injured Soldier
                    me->SetHealth(me->CountPctFromMaxHealth(25));
                    break;
            }
        }

        void JustEngagedWith(Unit* /*who*/) override { }

        void SpellHit(WorldObject* caster, SpellInfo const* spellInfo) override
        {
            Player* player = caster->ToPlayer();
            if (!player || !me->IsAlive() || spellInfo->Id != 20804)
                return;

            if (player->GetQuestStatus(6624) == QUEST_STATUS_INCOMPLETE || player->GetQuestStatus(6622) == QUEST_STATUS_INCOMPLETE)
                if (DoctorGUID)
                    if (Creature* doctor = ObjectAccessor::GetCreature(*me, DoctorGUID))
                        ENSURE_AI(npc_doctor::npc_doctorAI, doctor->AI())->PatientSaved(me, player, Coord);

            //make uninteractible
            me->SetUnitFlag(UNIT_FLAG_UNINTERACTIBLE);

            //regen health
            me->RemoveUnitFlag(UNIT_FLAG_IN_COMBAT);

            //stand up
            me->SetStandState(UNIT_STAND_STATE_STAND);

            Talk(SAY_DOC);

            uint32 mobId = me->GetEntry();
            me->SetWalk(false);

            switch (mobId)
            {
                case 12923:
                case 12924:
                case 12925:
                    me->GetMotionMaster()->MovePoint(0, H_RUNTOX, H_RUNTOY, H_RUNTOZ);
                    break;
                case 12936:
                case 12937:
                case 12938:
                    me->GetMotionMaster()->MovePoint(0, A_RUNTOX, A_RUNTOY, A_RUNTOZ);
                    break;
            }
        }

        void UpdateAI(uint32 /*diff*/) override
        {
            //lower HP on every world tick makes it a useful counter, not officlone though
            if (me->IsAlive() && me->GetHealth() > 6)
                me->ModifyHealth(-5);

            if (me->IsAlive() && me->GetHealth() <= 6)
            {
                me->RemoveUnitFlag(UNIT_FLAG_IN_COMBAT);
                me->SetUnitFlag(UNIT_FLAG_UNINTERACTIBLE);
                me->setDeathState(JUST_DIED);
                me->SetDynamicFlag(32);

                if (DoctorGUID)
                    if (Creature* doctor = ObjectAccessor::GetCreature((*me), DoctorGUID))
                        ENSURE_AI(npc_doctor::npc_doctorAI, doctor->AI())->PatientDied(Coord);
            }
        }
    };

    CreatureAI* GetAI(Creature* creature) const override
    {
        return new npc_injured_patientAI(creature);
    }
};

void npc_doctor::npc_doctorAI::UpdateAI(uint32 diff)
{
    if (Event && SummonPatientCount >= 20)
    {
        Reset();
        return;
    }

    if (Event)
    {
        if (SummonPatientTimer <= diff)
        {
            if (Coordinates.empty())
                return;

            uint32 patientEntry = 0;

            switch (me->GetEntry())
            {
                case DOCTOR_ALLIANCE:
                    patientEntry = AllianceSoldierId[rand32() % 3];
                    break;
                case DOCTOR_HORDE:
                    patientEntry = HordeSoldierId[rand32() % 3];
                    break;
                default:
                    TC_LOG_ERROR("scripts", "Invalid entry for Triage doctor. Please check your database");
                    return;
            }

            std::vector<Position const*>::iterator point = Coordinates.begin();
            std::advance(point, urand(0, Coordinates.size() - 1));

            if (Creature* Patient = me->SummonCreature(patientEntry, **point, TEMPSUMMON_TIMED_DESPAWN_OUT_OF_COMBAT, 5s))
            {
                //303, this flag appear to be required for client side item->spell to work (TARGET_SINGLE_FRIEND)
                Patient->SetUnitFlag(UNIT_FLAG_PLAYER_CONTROLLED);

                Patients.push_back(Patient->GetGUID());
                ENSURE_AI(npc_injured_patient::npc_injured_patientAI, Patient->AI())->DoctorGUID = me->GetGUID();
                ENSURE_AI(npc_injured_patient::npc_injured_patientAI, Patient->AI())->Coord = *point;

                Coordinates.erase(point);
            }

            SummonPatientTimer = 10000;
            ++SummonPatientCount;
        }
        else
            SummonPatientTimer -= diff;
    }
}

/*######
## npc_garments_of_quests
######*/

/// @todo get text for each NPC

enum Garments
{
    SPELL_LESSER_HEAL_R2    = 2052,
    SPELL_FORTITUDE_R1      = 1243,

    QUEST_MOON              = 5621,
    QUEST_LIGHT_1           = 5624,
    QUEST_LIGHT_2           = 5625,
    QUEST_SPIRIT            = 5648,
    QUEST_DARKNESS          = 5650,

    ENTRY_SHAYA             = 12429,
    ENTRY_ROBERTS           = 12423,
    ENTRY_DOLF              = 12427,
    ENTRY_KORJA             = 12430,
    ENTRY_DG_KEL            = 12428,

    // used by 12429, 12423, 12427, 12430, 12428, but signed for 12429
    SAY_THANKS              = 0,
    SAY_GOODBYE             = 1,
    SAY_HEALED              = 2,
};

class npc_garments_of_quests : public CreatureScript
{
public:
    npc_garments_of_quests() : CreatureScript("npc_garments_of_quests") { }

    struct npc_garments_of_questsAI : public EscortAI
    {
        npc_garments_of_questsAI(Creature* creature) : EscortAI(creature)
        {
            switch (me->GetEntry())
            {
                case ENTRY_SHAYA:
                    quest = QUEST_MOON;
                    break;
                case ENTRY_ROBERTS:
                    quest = QUEST_LIGHT_1;
                    break;
                case ENTRY_DOLF:
                    quest = QUEST_LIGHT_2;
                    break;
                case ENTRY_KORJA:
                    quest = QUEST_SPIRIT;
                    break;
                case ENTRY_DG_KEL:
                    quest = QUEST_DARKNESS;
                    break;
                default:
                    quest = 0;
                    break;
            }

            Initialize();
        }

        void Initialize()
        {
            IsHealed = false;
            CanRun = false;

            RunAwayTimer = 5000;
        }

        ObjectGuid CasterGUID;

        bool IsHealed;
        bool CanRun;

        uint32 RunAwayTimer;
        uint32 quest;

        void Reset() override
        {
            CasterGUID.Clear();

            Initialize();

            me->SetStandState(UNIT_STAND_STATE_KNEEL);
            // expect database to have RegenHealth=0
            me->SetHealth(me->CountPctFromMaxHealth(70));
        }

        void JustEngagedWith(Unit* /*who*/) override { }

        void SpellHit(WorldObject* caster, SpellInfo const* spellInfo) override
        {
            if (spellInfo->Id == SPELL_LESSER_HEAL_R2 || spellInfo->Id == SPELL_FORTITUDE_R1)
            {
                //not while in combat
                if (me->IsInCombat())
                    return;

                //nothing to be done now
                if (IsHealed && CanRun)
                    return;

                if (Player* player = caster->ToPlayer())
                {
                    if (quest && player->GetQuestStatus(quest) == QUEST_STATUS_INCOMPLETE)
                    {
                        if (IsHealed && !CanRun && spellInfo->Id == SPELL_FORTITUDE_R1)
                        {
                            Talk(SAY_THANKS, player);
                            CanRun = true;
                        }
                        else if (!IsHealed && spellInfo->Id == SPELL_LESSER_HEAL_R2)
                        {
                            CasterGUID = player->GetGUID();
                            me->SetStandState(UNIT_STAND_STATE_STAND);
                            Talk(SAY_HEALED, player);
                            IsHealed = true;
                        }
                    }

                    // give quest credit, not expect any special quest objectives
                    if (CanRun)
                        player->TalkedToCreature(me->GetEntry(), me->GetGUID());
                }
            }
        }

        void UpdateAI(uint32 diff) override
        {
            if (CanRun && !me->IsInCombat())
            {
                if (RunAwayTimer <= diff)
                {
                    if (Unit* unit = ObjectAccessor::GetUnit(*me, CasterGUID))
                    {
                        switch (me->GetEntry())
                        {
                            case ENTRY_SHAYA:
                            case ENTRY_ROBERTS:
                            case ENTRY_DOLF:
                            case ENTRY_KORJA:
                            case ENTRY_DG_KEL:
                                Talk(SAY_GOODBYE, unit);
                                break;
                        }

                        Start(false, true);
                    }
                    else
                        EnterEvadeMode();                       //something went wrong

                    RunAwayTimer = 30000;
                }
                else
                    RunAwayTimer -= diff;
            }

            EscortAI::UpdateAI(diff);
        }
    };

    CreatureAI* GetAI(Creature* creature) const override
    {
        return new npc_garments_of_questsAI(creature);
    }
};

/*######
## npc_guardian
######*/

enum GuardianSpells
{
    SPELL_DEATHTOUCH            = 5
};

class npc_guardian : public CreatureScript
{
public:
    npc_guardian() : CreatureScript("npc_guardian") { }

    struct npc_guardianAI : public ScriptedAI
    {
        npc_guardianAI(Creature* creature) : ScriptedAI(creature) { }

        void Reset() override
        {
            me->SetUnitFlag(UNIT_FLAG_NON_ATTACKABLE);
        }

        void JustEngagedWith(Unit* /*who*/) override
        {
        }

        void UpdateAI(uint32 /*diff*/) override
        {
            if (!UpdateVictim())
                return;

            if (me->isAttackReady())
            {
                DoCastVictim(SPELL_DEATHTOUCH, true);
                me->resetAttackTimer();
            }
        }
    };

    CreatureAI* GetAI(Creature* creature) const override
    {
        return new npc_guardianAI(creature);
    }
};

class npc_steam_tonk : public CreatureScript
{
public:
    npc_steam_tonk() : CreatureScript("npc_steam_tonk") { }

    struct npc_steam_tonkAI : public ScriptedAI
    {
        npc_steam_tonkAI(Creature* creature) : ScriptedAI(creature) { }

        void Reset() override { }
        void JustEngagedWith(Unit* /*who*/) override { }

        void OnPossess(bool apply)
        {
            if (apply)
            {
                // Initialize the action bar without the melee attack command
                me->InitCharmInfo();
                me->GetCharmInfo()->InitEmptyActionBar(false);

                me->SetReactState(REACT_PASSIVE);
            }
            else
                me->SetReactState(REACT_AGGRESSIVE);
        }
    };

    CreatureAI* GetAI(Creature* creature) const override
    {
        return new npc_steam_tonkAI(creature);
    }
};

enum TournamentPennantSpells
{
    SPELL_PENNANT_STORMWIND_ASPIRANT        = 62595,
    SPELL_PENNANT_STORMWIND_VALIANT         = 62596,
    SPELL_PENNANT_STORMWIND_CHAMPION        = 62594,
    SPELL_PENNANT_GNOMEREGAN_ASPIRANT       = 63394,
    SPELL_PENNANT_GNOMEREGAN_VALIANT        = 63395,
    SPELL_PENNANT_GNOMEREGAN_CHAMPION       = 63396,
    SPELL_PENNANT_SEN_JIN_ASPIRANT          = 63397,
    SPELL_PENNANT_SEN_JIN_VALIANT           = 63398,
    SPELL_PENNANT_SEN_JIN_CHAMPION          = 63399,
    SPELL_PENNANT_SILVERMOON_ASPIRANT       = 63401,
    SPELL_PENNANT_SILVERMOON_VALIANT        = 63402,
    SPELL_PENNANT_SILVERMOON_CHAMPION       = 63403,
    SPELL_PENNANT_DARNASSUS_ASPIRANT        = 63404,
    SPELL_PENNANT_DARNASSUS_VALIANT         = 63405,
    SPELL_PENNANT_DARNASSUS_CHAMPION        = 63406,
    SPELL_PENNANT_EXODAR_ASPIRANT           = 63421,
    SPELL_PENNANT_EXODAR_VALIANT            = 63422,
    SPELL_PENNANT_EXODAR_CHAMPION           = 63423,
    SPELL_PENNANT_IRONFORGE_ASPIRANT        = 63425,
    SPELL_PENNANT_IRONFORGE_VALIANT         = 63426,
    SPELL_PENNANT_IRONFORGE_CHAMPION        = 63427,
    SPELL_PENNANT_UNDERCITY_ASPIRANT        = 63428,
    SPELL_PENNANT_UNDERCITY_VALIANT         = 63429,
    SPELL_PENNANT_UNDERCITY_CHAMPION        = 63430,
    SPELL_PENNANT_ORGRIMMAR_ASPIRANT        = 63431,
    SPELL_PENNANT_ORGRIMMAR_VALIANT         = 63432,
    SPELL_PENNANT_ORGRIMMAR_CHAMPION        = 63433,
    SPELL_PENNANT_THUNDER_BLUFF_ASPIRANT    = 63434,
    SPELL_PENNANT_THUNDER_BLUFF_VALIANT     = 63435,
    SPELL_PENNANT_THUNDER_BLUFF_CHAMPION    = 63436,
    SPELL_PENNANT_ARGENT_CRUSADE_ASPIRANT   = 63606,
    SPELL_PENNANT_ARGENT_CRUSADE_VALIANT    = 63500,
    SPELL_PENNANT_ARGENT_CRUSADE_CHAMPION   = 63501,
    SPELL_PENNANT_EBON_BLADE_ASPIRANT       = 63607,
    SPELL_PENNANT_EBON_BLADE_VALIANT        = 63608,
    SPELL_PENNANT_EBON_BLADE_CHAMPION       = 63609
};

enum TournamentMounts
{
    NPC_STORMWIND_STEED                     = 33217,
    NPC_IRONFORGE_RAM                       = 33316,
    NPC_GNOMEREGAN_MECHANOSTRIDER           = 33317,
    NPC_EXODAR_ELEKK                        = 33318,
    NPC_DARNASSIAN_NIGHTSABER               = 33319,
    NPC_ORGRIMMAR_WOLF                      = 33320,
    NPC_DARK_SPEAR_RAPTOR                   = 33321,
    NPC_THUNDER_BLUFF_KODO                  = 33322,
    NPC_SILVERMOON_HAWKSTRIDER              = 33323,
    NPC_FORSAKEN_WARHORSE                   = 33324,
    NPC_ARGENT_WARHORSE                     = 33782,
    NPC_ARGENT_STEED_ASPIRANT               = 33845,
    NPC_ARGENT_HAWKSTRIDER_ASPIRANT         = 33844
};

enum TournamentQuestsAchievements
{
    ACHIEVEMENT_CHAMPION_STORMWIND          = 2781,
    ACHIEVEMENT_CHAMPION_DARNASSUS          = 2777,
    ACHIEVEMENT_CHAMPION_IRONFORGE          = 2780,
    ACHIEVEMENT_CHAMPION_GNOMEREGAN         = 2779,
    ACHIEVEMENT_CHAMPION_THE_EXODAR         = 2778,
    ACHIEVEMENT_CHAMPION_ORGRIMMAR          = 2783,
    ACHIEVEMENT_CHAMPION_SEN_JIN            = 2784,
    ACHIEVEMENT_CHAMPION_THUNDER_BLUFF      = 2786,
    ACHIEVEMENT_CHAMPION_UNDERCITY          = 2787,
    ACHIEVEMENT_CHAMPION_SILVERMOON         = 2785,
    ACHIEVEMENT_ARGENT_VALOR                = 2758,
    ACHIEVEMENT_CHAMPION_ALLIANCE           = 2782,
    ACHIEVEMENT_CHAMPION_HORDE              = 2788,

    QUEST_VALIANT_OF_STORMWIND              = 13593,
    QUEST_A_VALIANT_OF_STORMWIND            = 13684,
    QUEST_VALIANT_OF_DARNASSUS              = 13706,
    QUEST_A_VALIANT_OF_DARNASSUS            = 13689,
    QUEST_VALIANT_OF_IRONFORGE              = 13703,
    QUEST_A_VALIANT_OF_IRONFORGE            = 13685,
    QUEST_VALIANT_OF_GNOMEREGAN             = 13704,
    QUEST_A_VALIANT_OF_GNOMEREGAN           = 13688,
    QUEST_VALIANT_OF_THE_EXODAR             = 13705,
    QUEST_A_VALIANT_OF_THE_EXODAR           = 13690,
    QUEST_VALIANT_OF_ORGRIMMAR              = 13707,
    QUEST_A_VALIANT_OF_ORGRIMMAR            = 13691,
    QUEST_VALIANT_OF_SEN_JIN                = 13708,
    QUEST_A_VALIANT_OF_SEN_JIN              = 13693,
    QUEST_VALIANT_OF_THUNDER_BLUFF          = 13709,
    QUEST_A_VALIANT_OF_THUNDER_BLUFF        = 13694,
    QUEST_VALIANT_OF_UNDERCITY              = 13710,
    QUEST_A_VALIANT_OF_UNDERCITY            = 13695,
    QUEST_VALIANT_OF_SILVERMOON             = 13711,
    QUEST_A_VALIANT_OF_SILVERMOON           = 13696
};

class npc_tournament_mount : public CreatureScript
{
    public:
        npc_tournament_mount() : CreatureScript("npc_tournament_mount") { }

        struct npc_tournament_mountAI : public VehicleAI
        {
            npc_tournament_mountAI(Creature* creature) : VehicleAI(creature)
            {
                _pennantSpellId = 0;
            }

            void PassengerBoarded(Unit* passenger, int8 /*seatId*/, bool apply) override
            {
                Player* player = passenger->ToPlayer();
                if (!player)
                    return;

                if (apply)
                {
                    _pennantSpellId = GetPennantSpellId(player);
                    player->CastSpell(nullptr, _pennantSpellId, true);
                }
                else
                    player->RemoveAurasDueToSpell(_pennantSpellId);
            }

        private:
            uint32 _pennantSpellId;

            uint32 GetPennantSpellId(Player* player) const
            {
                switch (me->GetEntry())
                {
                    case NPC_ARGENT_STEED_ASPIRANT:
                    case NPC_STORMWIND_STEED:
                    {
                        if (player->HasAchieved(ACHIEVEMENT_CHAMPION_STORMWIND))
                            return SPELL_PENNANT_STORMWIND_CHAMPION;
                        else if (player->GetQuestRewardStatus(QUEST_VALIANT_OF_STORMWIND) || player->GetQuestRewardStatus(QUEST_A_VALIANT_OF_STORMWIND))
                            return SPELL_PENNANT_STORMWIND_VALIANT;
                        else
                            return SPELL_PENNANT_STORMWIND_ASPIRANT;
                    }
                    case NPC_GNOMEREGAN_MECHANOSTRIDER:
                    {
                        if (player->HasAchieved(ACHIEVEMENT_CHAMPION_GNOMEREGAN))
                            return SPELL_PENNANT_GNOMEREGAN_CHAMPION;
                        else if (player->GetQuestRewardStatus(QUEST_VALIANT_OF_GNOMEREGAN) || player->GetQuestRewardStatus(QUEST_A_VALIANT_OF_GNOMEREGAN))
                            return SPELL_PENNANT_GNOMEREGAN_VALIANT;
                        else
                            return SPELL_PENNANT_GNOMEREGAN_ASPIRANT;
                    }
                    case NPC_DARK_SPEAR_RAPTOR:
                    {
                        if (player->HasAchieved(ACHIEVEMENT_CHAMPION_SEN_JIN))
                            return SPELL_PENNANT_SEN_JIN_CHAMPION;
                        else if (player->GetQuestRewardStatus(QUEST_VALIANT_OF_SEN_JIN) || player->GetQuestRewardStatus(QUEST_A_VALIANT_OF_SEN_JIN))
                            return SPELL_PENNANT_SEN_JIN_VALIANT;
                        else
                            return SPELL_PENNANT_SEN_JIN_ASPIRANT;
                    }
                    case NPC_ARGENT_HAWKSTRIDER_ASPIRANT:
                    case NPC_SILVERMOON_HAWKSTRIDER:
                    {
                        if (player->HasAchieved(ACHIEVEMENT_CHAMPION_SILVERMOON))
                            return SPELL_PENNANT_SILVERMOON_CHAMPION;
                        else if (player->GetQuestRewardStatus(QUEST_VALIANT_OF_SILVERMOON) || player->GetQuestRewardStatus(QUEST_A_VALIANT_OF_SILVERMOON))
                            return SPELL_PENNANT_SILVERMOON_VALIANT;
                        else
                            return SPELL_PENNANT_SILVERMOON_ASPIRANT;
                    }
                    case NPC_DARNASSIAN_NIGHTSABER:
                    {
                        if (player->HasAchieved(ACHIEVEMENT_CHAMPION_DARNASSUS))
                            return SPELL_PENNANT_DARNASSUS_CHAMPION;
                        else if (player->GetQuestRewardStatus(QUEST_VALIANT_OF_DARNASSUS) || player->GetQuestRewardStatus(QUEST_A_VALIANT_OF_DARNASSUS))
                            return SPELL_PENNANT_DARNASSUS_VALIANT;
                        else
                            return SPELL_PENNANT_DARNASSUS_ASPIRANT;
                    }
                    case NPC_EXODAR_ELEKK:
                    {
                        if (player->HasAchieved(ACHIEVEMENT_CHAMPION_THE_EXODAR))
                            return SPELL_PENNANT_EXODAR_CHAMPION;
                        else if (player->GetQuestRewardStatus(QUEST_VALIANT_OF_THE_EXODAR) || player->GetQuestRewardStatus(QUEST_A_VALIANT_OF_THE_EXODAR))
                            return SPELL_PENNANT_EXODAR_VALIANT;
                        else
                            return SPELL_PENNANT_EXODAR_ASPIRANT;
                    }
                    case NPC_IRONFORGE_RAM:
                    {
                        if (player->HasAchieved(ACHIEVEMENT_CHAMPION_IRONFORGE))
                            return SPELL_PENNANT_IRONFORGE_CHAMPION;
                        else if (player->GetQuestRewardStatus(QUEST_VALIANT_OF_IRONFORGE) || player->GetQuestRewardStatus(QUEST_A_VALIANT_OF_IRONFORGE))
                            return SPELL_PENNANT_IRONFORGE_VALIANT;
                        else
                            return SPELL_PENNANT_IRONFORGE_ASPIRANT;
                    }
                    case NPC_FORSAKEN_WARHORSE:
                    {
                        if (player->HasAchieved(ACHIEVEMENT_CHAMPION_UNDERCITY))
                            return SPELL_PENNANT_UNDERCITY_CHAMPION;
                        else if (player->GetQuestRewardStatus(QUEST_VALIANT_OF_UNDERCITY) || player->GetQuestRewardStatus(QUEST_A_VALIANT_OF_UNDERCITY))
                            return SPELL_PENNANT_UNDERCITY_VALIANT;
                        else
                            return SPELL_PENNANT_UNDERCITY_ASPIRANT;
                    }
                    case NPC_ORGRIMMAR_WOLF:
                    {
                        if (player->HasAchieved(ACHIEVEMENT_CHAMPION_ORGRIMMAR))
                            return SPELL_PENNANT_ORGRIMMAR_CHAMPION;
                        else if (player->GetQuestRewardStatus(QUEST_VALIANT_OF_ORGRIMMAR) || player->GetQuestRewardStatus(QUEST_A_VALIANT_OF_ORGRIMMAR))
                            return SPELL_PENNANT_ORGRIMMAR_VALIANT;
                        else
                            return SPELL_PENNANT_ORGRIMMAR_ASPIRANT;
                    }
                    case NPC_THUNDER_BLUFF_KODO:
                    {
                        if (player->HasAchieved(ACHIEVEMENT_CHAMPION_THUNDER_BLUFF))
                            return SPELL_PENNANT_THUNDER_BLUFF_CHAMPION;
                        else if (player->GetQuestRewardStatus(QUEST_VALIANT_OF_THUNDER_BLUFF) || player->GetQuestRewardStatus(QUEST_A_VALIANT_OF_THUNDER_BLUFF))
                            return SPELL_PENNANT_THUNDER_BLUFF_VALIANT;
                        else
                            return SPELL_PENNANT_THUNDER_BLUFF_ASPIRANT;
                    }
                    case NPC_ARGENT_WARHORSE:
                    {
                        if (player->HasAchieved(ACHIEVEMENT_CHAMPION_ALLIANCE) || player->HasAchieved(ACHIEVEMENT_CHAMPION_HORDE))
                            return player->GetClass() == CLASS_DEATH_KNIGHT ? SPELL_PENNANT_EBON_BLADE_CHAMPION : SPELL_PENNANT_ARGENT_CRUSADE_CHAMPION;
                        else if (player->HasAchieved(ACHIEVEMENT_ARGENT_VALOR))
                            return player->GetClass() == CLASS_DEATH_KNIGHT ? SPELL_PENNANT_EBON_BLADE_VALIANT : SPELL_PENNANT_ARGENT_CRUSADE_VALIANT;
                        else
                            return player->GetClass() == CLASS_DEATH_KNIGHT ? SPELL_PENNANT_EBON_BLADE_ASPIRANT : SPELL_PENNANT_ARGENT_CRUSADE_ASPIRANT;
                    }
                    default:
                        return 0;
                }
            }
        };

        CreatureAI* GetAI(Creature* creature) const override
        {
            return new npc_tournament_mountAI(creature);
        }
};

/*####
## npc_brewfest_reveler
####*/

enum BrewfestReveler
{
    SPELL_BREWFEST_TOAST = 41586
};

class npc_brewfest_reveler : public CreatureScript
{
    public:
        npc_brewfest_reveler() : CreatureScript("npc_brewfest_reveler") { }

        struct npc_brewfest_revelerAI : public ScriptedAI
        {
            npc_brewfest_revelerAI(Creature* creature) : ScriptedAI(creature) { }

            void ReceiveEmote(Player* player, uint32 emote) override
            {
                if (!IsHolidayActive(HOLIDAY_BREWFEST))
                    return;

                if (emote == TEXT_EMOTE_DANCE)
                    me->CastSpell(player, SPELL_BREWFEST_TOAST, false);
            }
        };

        CreatureAI* GetAI(Creature* creature) const override
        {
            return new npc_brewfest_revelerAI(creature);
        }
};

/*######
# npc_brewfest_reveler_2
######*/

Emote const BrewfestRandomEmote[] =
{
    EMOTE_ONESHOT_QUESTION,
    EMOTE_ONESHOT_APPLAUD,
    EMOTE_ONESHOT_SHOUT,
    EMOTE_ONESHOT_EAT_NO_SHEATHE,
    EMOTE_ONESHOT_LAUGH_NO_SHEATHE
};

struct npc_brewfest_reveler_2 : ScriptedAI
{
    enum BrewfestReveler2
    {
        NPC_BREWFEST_REVELER    = 24484,

        EVENT_FILL_LIST         = 1,
        EVENT_FACE_TO           = 2,
        EVENT_EMOTE             = 3,
        EVENT_NEXT              = 4
    };

    npc_brewfest_reveler_2(Creature* creature) : ScriptedAI(creature) { }

    void Reset() override
    {
        _events.Reset();
        _events.ScheduleEvent(EVENT_FILL_LIST, 1s, 2s);
    }

    // Copied from old script. I don't know if this is 100% correct.
    void ReceiveEmote(Player* player, uint32 emote) override
    {
        if (!IsHolidayActive(HOLIDAY_BREWFEST))
            return;

        if (emote == TEXT_EMOTE_DANCE)
            me->CastSpell(player, SPELL_BREWFEST_TOAST, false);
    }

    void UpdateAI(uint32 diff) override
    {
        UpdateVictim();

        _events.Update(diff);

        while (uint32 eventId = _events.ExecuteEvent())
        {
            switch (eventId)
            {
                case EVENT_FILL_LIST:
                {
                    std::list<Creature*> creatureList;
                    GetCreatureListWithEntryInGrid(creatureList, me, NPC_BREWFEST_REVELER, 5.0f);
                    for (Creature* creature : creatureList)
                        if (creature != me)
                            _revelerGuids.push_back(creature->GetGUID());

                    _events.ScheduleEvent(EVENT_FACE_TO, 1s, 2s);
                    break;
                }
                case EVENT_FACE_TO:
                {
                    // Turn to random brewfest reveler within set range
                    if (!_revelerGuids.empty())
                        if (Creature* creature = ObjectAccessor::GetCreature(*me, Trinity::Containers::SelectRandomContainerElement(_revelerGuids)))
                            me->SetFacingToObject(creature);

                    _events.ScheduleEvent(EVENT_EMOTE, 2s, 6s);
                    break;
                }
                case EVENT_EMOTE:
                    // Play random emote or dance
                    if (roll_chance_i(50))
                    {
                        me->HandleEmoteCommand(Trinity::Containers::SelectRandomContainerElement(BrewfestRandomEmote));
                        _events.ScheduleEvent(EVENT_NEXT, 4s, 6s);
                    }
                    else
                    {
                        me->SetEmoteState(EMOTE_STATE_DANCE);
                        _events.ScheduleEvent(EVENT_NEXT, 8s, 12s);
                    }
                    break;
                case EVENT_NEXT:
                    // If dancing stop before next random state
                    if (me->GetEmoteState() == EMOTE_STATE_DANCE)
                        me->SetEmoteState(EMOTE_ONESHOT_NONE);

                    // Random EVENT_EMOTE or EVENT_FACETO
                    if (roll_chance_i(50))
                        _events.ScheduleEvent(EVENT_FACE_TO, 1s);
                    else
                        _events.ScheduleEvent(EVENT_EMOTE, 1s);
                    break;
                default:
                    break;
                }
        }
    }

private:
    EventMap _events;
    GuidVector _revelerGuids;
};

struct npc_training_dummy : NullCreatureAI
{
    npc_training_dummy(Creature* creature) : NullCreatureAI(creature) { }

    void JustEnteredCombat(Unit* who) override
    {
        _combatTimer[who->GetGUID()] = 5s;
    }

    void DamageTaken(Unit* attacker, uint32& damage, DamageEffectType damageType, SpellInfo const* /*spellInfo = nullptr*/) override
    {
        damage = 0;

        if (!attacker || damageType == DOT)
            return;

        _combatTimer[attacker->GetGUID()] = 5s;
    }

    void UpdateAI(uint32 diff) override
    {
        for (auto itr = _combatTimer.begin(); itr != _combatTimer.end();)
        {
            itr->second -= Milliseconds(diff);
            if (itr->second <= 0s)
            {
                // The attacker has not dealt any damage to the dummy for over 5 seconds. End combat.
                auto const& pveRefs = me->GetCombatManager().GetPvECombatRefs();
                auto it = pveRefs.find(itr->first);
                if (it != pveRefs.end())
                    it->second->EndCombat();

                itr = _combatTimer.erase(itr);
            }
            else
                ++itr;
        }
    }
private:
    std::unordered_map<ObjectGuid /*attackerGUID*/, Milliseconds /*combatTime*/> _combatTimer;
};

/*######
# npc_wormhole
######*/

enum NPC_Wormhole
{
    MENU_ID_WORMHOLE      = 10668, // "This tear in the fabric of time and space looks ominous."
    NPC_TEXT_WORMHOLE     = 14785, // (not 907 "What brings you to this part of the world, $n?")
    GOSSIP_OPTION_1       = 0,     // "Borean Tundra"
    GOSSIP_OPTION_2       = 1,     // "Howling Fjord"
    GOSSIP_OPTION_3       = 2,     // "Sholazar Basin"
    GOSSIP_OPTION_4       = 3,     // "Icecrown"
    GOSSIP_OPTION_5       = 4,     // "Storm Peaks"
    GOSSIP_OPTION_6       = 5,     // "Underground..."

    SPELL_BOREAN_TUNDRA   = 67834, // 0
    SPELL_HOWLING_FJORD   = 67838, // 1
    SPELL_SHOLAZAR_BASIN  = 67835, // 2
    SPELL_ICECROWN        = 67836, // 3
    SPELL_STORM_PEAKS     = 67837, // 4
    SPELL_UNDERGROUND     = 68081  // 5
};

class npc_wormhole : public CreatureScript
{
    public:
        npc_wormhole() : CreatureScript("npc_wormhole") { }

        struct npc_wormholeAI : public PassiveAI
        {
            npc_wormholeAI(Creature* creature) : PassiveAI(creature)
            {
                Initialize();
            }

            void Initialize()
            {
                _showUnderground = urand(0, 100) == 0; // Guessed value, it is really rare though
            }

            void InitializeAI() override
            {
                Initialize();
            }

            bool OnGossipHello(Player* player) override
            {
                InitGossipMenuFor(player, MENU_ID_WORMHOLE);
                if (me->IsSummon())
                {
                    if (player == me->ToTempSummon()->GetSummoner())
                    {
                        AddGossipItemFor(player, MENU_ID_WORMHOLE, GOSSIP_OPTION_1, GOSSIP_SENDER_MAIN, GOSSIP_ACTION_INFO_DEF + 1);
                        AddGossipItemFor(player, MENU_ID_WORMHOLE, GOSSIP_OPTION_2, GOSSIP_SENDER_MAIN, GOSSIP_ACTION_INFO_DEF + 2);
                        AddGossipItemFor(player, MENU_ID_WORMHOLE, GOSSIP_OPTION_3, GOSSIP_SENDER_MAIN, GOSSIP_ACTION_INFO_DEF + 3);
                        AddGossipItemFor(player, MENU_ID_WORMHOLE, GOSSIP_OPTION_4, GOSSIP_SENDER_MAIN, GOSSIP_ACTION_INFO_DEF + 4);
                        AddGossipItemFor(player, MENU_ID_WORMHOLE, GOSSIP_OPTION_5, GOSSIP_SENDER_MAIN, GOSSIP_ACTION_INFO_DEF + 5);

                        if (_showUnderground)
                            AddGossipItemFor(player, MENU_ID_WORMHOLE, GOSSIP_OPTION_6, GOSSIP_SENDER_MAIN, GOSSIP_ACTION_INFO_DEF + 6);

                        SendGossipMenuFor(player, NPC_TEXT_WORMHOLE, me->GetGUID());
                    }
                }

                return true;
            }

            bool OnGossipSelect(Player* player, uint32 /*menuId*/, uint32 gossipListId) override
            {
                uint32 const action = player->PlayerTalkClass->GetGossipOptionAction(gossipListId);
                ClearGossipMenuFor(player);

                switch (action)
                {
                    case GOSSIP_ACTION_INFO_DEF + 1: // Borean Tundra
                        CloseGossipMenuFor(player);
                        DoCast(player, SPELL_BOREAN_TUNDRA, false);
                        break;
                    case GOSSIP_ACTION_INFO_DEF + 2: // Howling Fjord
                        CloseGossipMenuFor(player);
                        DoCast(player, SPELL_HOWLING_FJORD, false);
                        break;
                    case GOSSIP_ACTION_INFO_DEF + 3: // Sholazar Basin
                        CloseGossipMenuFor(player);
                        DoCast(player, SPELL_SHOLAZAR_BASIN, false);
                        break;
                    case GOSSIP_ACTION_INFO_DEF + 4: // Icecrown
                        CloseGossipMenuFor(player);
                        DoCast(player, SPELL_ICECROWN, false);
                        break;
                    case GOSSIP_ACTION_INFO_DEF + 5: // Storm peaks
                        CloseGossipMenuFor(player);
                        DoCast(player, SPELL_STORM_PEAKS, false);
                        break;
                    case GOSSIP_ACTION_INFO_DEF + 6: // Underground
                        CloseGossipMenuFor(player);
                        DoCast(player, SPELL_UNDERGROUND, false);
                        break;
                }

                return true;
            }

        private:
            bool _showUnderground;
        };

        CreatureAI* GetAI(Creature* creature) const override
        {
            return new npc_wormholeAI(creature);
        }
};

/*######
## npc_pet_trainer
######*/

enum PetTrainer
{
    MENU_ID_PET_UNLEARN      = 6520,
    OPTION_ID_PLEASE_DO      = 0
};

class npc_pet_trainer : public CreatureScript
{
public:
    npc_pet_trainer() : CreatureScript("npc_pet_trainer") { }

    struct npc_pet_trainerAI : public ScriptedAI
    {
        npc_pet_trainerAI(Creature* creature) : ScriptedAI(creature) { }

        bool OnGossipSelect(Player* player, uint32 menuId, uint32 gossipListId) override
        {
            if (menuId == MENU_ID_PET_UNLEARN && gossipListId == OPTION_ID_PLEASE_DO)
            {
                player->ResetPetTalents();
                CloseGossipMenuFor(player);
            }
            return false;
        }
    };

    CreatureAI* GetAI(Creature* creature) const override
    {
        return new npc_pet_trainerAI(creature);
    }
};

/*######
## npc_experience
######*/

enum BehstenSlahtz
{
    MENU_ID_XP_ON_OFF  = 10638,
    NPC_TEXT_XP_ON_OFF = 14736,
    OPTION_ID_XP_OFF   = 0,     // "I no longer wish to gain experience."
    OPTION_ID_XP_ON    = 1      // "I wish to start gaining experience again."
};

class npc_experience : public CreatureScript
{
public:
    npc_experience() : CreatureScript("npc_experience") { }

    struct npc_experienceAI : public ScriptedAI
    {
        npc_experienceAI(Creature* creature) : ScriptedAI(creature) { }

        bool OnGossipHello(Player* player) override
        {
            InitGossipMenuFor(player, MENU_ID_XP_ON_OFF);
            if (player->HasFlag(PLAYER_FLAGS, PLAYER_FLAGS_NO_XP_GAIN)) // not gaining XP
            {
                AddGossipItemFor(player, MENU_ID_XP_ON_OFF, OPTION_ID_XP_ON, GOSSIP_SENDER_MAIN, GOSSIP_ACTION_INFO_DEF + 1);
                SendGossipMenuFor(player, NPC_TEXT_XP_ON_OFF, me->GetGUID());
            }
            else // currently gaining XP
            {
                AddGossipItemFor(player, MENU_ID_XP_ON_OFF, OPTION_ID_XP_OFF, GOSSIP_SENDER_MAIN, GOSSIP_ACTION_INFO_DEF + 2);
                SendGossipMenuFor(player, NPC_TEXT_XP_ON_OFF, me->GetGUID());
            }
            return true;
        }

        bool OnGossipSelect(Player* player, uint32 /*menuId*/, uint32 gossipListId) override
        {
            uint32 const action = player->PlayerTalkClass->GetGossipOptionAction(gossipListId);

            switch (action)
            {
                case GOSSIP_ACTION_INFO_DEF + 1: // XP ON selected
                    player->RemoveFlag(PLAYER_FLAGS, PLAYER_FLAGS_NO_XP_GAIN); // turn on XP gain
                    break;
                case GOSSIP_ACTION_INFO_DEF + 2: // XP OFF selected
                    player->SetFlag(PLAYER_FLAGS, PLAYER_FLAGS_NO_XP_GAIN); // turn off XP gain
                    break;
            }
            CloseGossipMenuFor(player);
            return false;
        }
    };

    CreatureAI* GetAI(Creature* creature) const override
    {
        return new npc_experienceAI(creature);
    }
};

/*#####
# npc_spring_rabbit
#####*/

enum rabbitSpells
{
    SPELL_SPRING_FLING          = 61875,
    SPELL_SPRING_RABBIT_JUMP    = 61724,
    SPELL_SPRING_RABBIT_WANDER  = 61726,
    SPELL_SUMMON_BABY_BUNNY     = 61727,
    SPELL_SPRING_RABBIT_IN_LOVE = 61728,
    NPC_SPRING_RABBIT           = 32791
};

class npc_spring_rabbit : public CreatureScript
{
public:
    npc_spring_rabbit() : CreatureScript("npc_spring_rabbit") { }

    CreatureAI* GetAI(Creature* creature) const override
    {
        return new npc_spring_rabbitAI(creature);
    }

    struct npc_spring_rabbitAI : public ScriptedAI
    {
        npc_spring_rabbitAI(Creature* creature) : ScriptedAI(creature)
        {
            Initialize();
        }

        void Initialize()
        {
            inLove = false;
            rabbitGUID.Clear();
            jumpTimer = urand(5000, 10000);
            bunnyTimer = urand(10000, 20000);
            searchTimer = urand(5000, 10000);
        }

        bool inLove;
        uint32 jumpTimer;
        uint32 bunnyTimer;
        uint32 searchTimer;
        ObjectGuid rabbitGUID;

        void Reset() override
        {
            Initialize();
            if (Unit* owner = me->GetOwner())
                me->GetMotionMaster()->MoveFollow(owner, PET_FOLLOW_DIST, PET_FOLLOW_ANGLE);
        }

        void JustEngagedWith(Unit* /*who*/) override { }

        void DoAction(int32 /*param*/) override
        {
            inLove = true;
            if (Unit* owner = me->GetOwner())
                owner->CastSpell(owner, SPELL_SPRING_FLING, true);
        }

        void UpdateAI(uint32 diff) override
        {
            if (inLove)
            {
                if (jumpTimer <= diff)
                {
                    if (Unit* rabbit = ObjectAccessor::GetUnit(*me, rabbitGUID))
                        DoCast(rabbit, SPELL_SPRING_RABBIT_JUMP);
                    jumpTimer = urand(5000, 10000);
                } else jumpTimer -= diff;

                if (bunnyTimer <= diff)
                {
                    DoCast(SPELL_SUMMON_BABY_BUNNY);
                    bunnyTimer = urand(20000, 40000);
                } else bunnyTimer -= diff;
            }
            else
            {
                if (searchTimer <= diff)
                {
                    if (Creature* rabbit = me->FindNearestCreature(NPC_SPRING_RABBIT, 10.0f))
                    {
                        if (rabbit == me || rabbit->HasAura(SPELL_SPRING_RABBIT_IN_LOVE))
                            return;

                        me->AddAura(SPELL_SPRING_RABBIT_IN_LOVE, me);
                        DoAction(1);
                        rabbit->AddAura(SPELL_SPRING_RABBIT_IN_LOVE, rabbit);
                        rabbit->AI()->DoAction(1);
                        rabbit->CastSpell(rabbit, SPELL_SPRING_RABBIT_JUMP, true);
                        rabbitGUID = rabbit->GetGUID();
                    }
                    searchTimer = urand(5000, 10000);
                } else searchTimer -= diff;
            }
        }
    };
};

class npc_imp_in_a_ball : public CreatureScript
{
private:
    enum
    {
        SAY_RANDOM,

        EVENT_TALK = 1,
    };

public:
    npc_imp_in_a_ball() : CreatureScript("npc_imp_in_a_ball") { }

    struct npc_imp_in_a_ballAI : public ScriptedAI
    {
        npc_imp_in_a_ballAI(Creature* creature) : ScriptedAI(creature)
        {
            summonerGUID.Clear();
        }

        void IsSummonedBy(WorldObject* summoner) override
        {
            if (summoner->GetTypeId() == TYPEID_PLAYER)
            {
                summonerGUID = summoner->GetGUID();
                events.ScheduleEvent(EVENT_TALK, 3s);
            }
        }

        void UpdateAI(uint32 diff) override
        {
            events.Update(diff);

            if (events.ExecuteEvent() == EVENT_TALK)
            {
                if (Player* owner = ObjectAccessor::GetPlayer(*me, summonerGUID))
                {
                    sCreatureTextMgr->SendChat(me, SAY_RANDOM, owner,
                        owner->GetGroup() ? CHAT_MSG_MONSTER_PARTY : CHAT_MSG_MONSTER_WHISPER, LANG_ADDON, TEXT_RANGE_NORMAL);
                }
            }
        }

    private:
        EventMap events;
        ObjectGuid summonerGUID;
    };

    CreatureAI* GetAI(Creature* creature) const override
    {
        return new npc_imp_in_a_ballAI(creature);
    }
};

enum StableMasters
{
    SPELL_MINIWING                  = 54573,
    SPELL_JUBLING                   = 54611,
    SPELL_DARTER                    = 54619,
    SPELL_WORG                      = 54631,
    SPELL_SMOLDERWEB                = 54634,
    SPELL_CHIKEN                    = 54677,
    SPELL_WOLPERTINGER              = 54688,

    STABLE_MASTER_GOSSIP_SUB_MENU   = 9820
};

class npc_stable_master : public CreatureScript
{
    public:
        npc_stable_master() : CreatureScript("npc_stable_master") { }

        struct npc_stable_masterAI : public SmartAI
        {
            npc_stable_masterAI(Creature* creature) : SmartAI(creature) { }

            bool OnGossipSelect(Player* player, uint32 menuId, uint32 gossipListId) override
            {
                SmartAI::OnGossipSelect(player, menuId, gossipListId);
                if (menuId != STABLE_MASTER_GOSSIP_SUB_MENU)
                    return false;

                switch (gossipListId)
                {
                    case 0:
                        player->CastSpell(player, SPELL_MINIWING, false);
                        break;
                    case 1:
                        player->CastSpell(player, SPELL_JUBLING, false);
                        break;
                    case 2:
                        player->CastSpell(player, SPELL_DARTER, false);
                        break;
                    case 3:
                        player->CastSpell(player, SPELL_WORG, false);
                        break;
                    case 4:
                        player->CastSpell(player, SPELL_SMOLDERWEB, false);
                        break;
                    case 5:
                        player->CastSpell(player, SPELL_CHIKEN, false);
                        break;
                    case 6:
                        player->CastSpell(player, SPELL_WOLPERTINGER, false);
                        break;
                    default:
                        return false;
                }

                player->PlayerTalkClass->SendCloseGossip();
                return false;
            }
        };

        CreatureAI* GetAI(Creature* creature) const override
        {
            return new npc_stable_masterAI(creature);
        }
};

enum TrainWrecker
{
    GO_TOY_TRAIN          = 193963,
    SPELL_TOY_TRAIN_PULSE =  61551,
    SPELL_WRECK_TRAIN     =  62943,
    EVENT_DO_JUMP         =      1,
    EVENT_DO_FACING       =      2,
    EVENT_DO_WRECK        =      3,
    EVENT_DO_DANCE        =      4,
    MOVEID_CHASE          =      1,
    MOVEID_JUMP           =      2
};
class npc_train_wrecker : public CreatureScript
{
    public:
        npc_train_wrecker() : CreatureScript("npc_train_wrecker") { }

        struct npc_train_wreckerAI : public NullCreatureAI
        {
            npc_train_wreckerAI(Creature* creature) : NullCreatureAI(creature), _isSearching(true), _nextAction(0), _timer(1 * IN_MILLISECONDS) { }

            GameObject* VerifyTarget() const
            {
                if (GameObject* target = ObjectAccessor::GetGameObject(*me, _target))
                    return target;
                me->HandleEmoteCommand(EMOTE_ONESHOT_RUDE);
                me->DespawnOrUnsummon(3s);
                return nullptr;
            }

            void UpdateAI(uint32 diff) override
            {
                if (_isSearching)
                {
                    if (diff < _timer)
                        _timer -= diff;
                    else
                    {
                        if (GameObject* target = me->FindNearestGameObject(GO_TOY_TRAIN, 15.0f))
                        {
                            _isSearching = false;
                            _target = target->GetGUID();
                            me->SetWalk(true);
                            me->GetMotionMaster()->MovePoint(MOVEID_CHASE, target->GetNearPosition(3.0f, target->GetAbsoluteAngle(me)));
                        }
                        else
                            _timer = 3 * IN_MILLISECONDS;
                    }
                }
                else
                {
                    switch (_nextAction)
                    {
                        case EVENT_DO_JUMP:
                            if (GameObject* target = VerifyTarget())
                                me->GetMotionMaster()->MoveJump(*target, 5.0, 10.0, MOVEID_JUMP);
                            _nextAction = 0;
                            break;
                        case EVENT_DO_FACING:
                            if (GameObject* target = VerifyTarget())
                            {
                                me->SetFacingTo(target->GetOrientation());
                                me->HandleEmoteCommand(EMOTE_ONESHOT_ATTACK1H);
                                _timer = 1.5 * AsUnderlyingType(IN_MILLISECONDS);
                                _nextAction = EVENT_DO_WRECK;
                            }
                            else
                                _nextAction = 0;
                            break;
                        case EVENT_DO_WRECK:
                            if (diff < _timer)
                            {
                                _timer -= diff;
                                break;
                            }
                            if (GameObject* target = VerifyTarget())
                            {
                                me->CastSpell(target, SPELL_WRECK_TRAIN, false);
                                _timer = 2 * IN_MILLISECONDS;
                                _nextAction = EVENT_DO_DANCE;
                            }
                            else
                                _nextAction = 0;
                            break;
                        case EVENT_DO_DANCE:
                            if (diff < _timer)
                            {
                                _timer -= diff;
                                break;
                            }
                            me->SetEmoteState(EMOTE_ONESHOT_DANCE);
                            me->DespawnOrUnsummon(5s);
                            _nextAction = 0;
                            break;
                        default:
                            break;
                    }
                }
            }

            void MovementInform(uint32 /*type*/, uint32 id) override
            {
                if (id == MOVEID_CHASE)
                    _nextAction = EVENT_DO_JUMP;
                else if (id == MOVEID_JUMP)
                    _nextAction = EVENT_DO_FACING;
            }

        private:
            bool _isSearching;
            uint8 _nextAction;
            uint32 _timer;
            ObjectGuid _target;
        };

        CreatureAI* GetAI(Creature* creature) const override
        {
            return new npc_train_wreckerAI(creature);
        }
};

/*######
## npc_argent_squire/gruntling
######*/

enum Pennants
{
    SPELL_DARNASSUS_PENNANT     = 63443,
    SPELL_EXODAR_PENNANT        = 63439,
    SPELL_GNOMEREGAN_PENNANT    = 63442,
    SPELL_IRONFORGE_PENNANT     = 63440,
    SPELL_STORMWIND_PENNANT     = 62727,
    SPELL_SENJIN_PENNANT        = 63446,
    SPELL_UNDERCITY_PENNANT     = 63441,
    SPELL_ORGRIMMAR_PENNANT     = 63444,
    SPELL_SILVERMOON_PENNANT    = 63438,
    SPELL_THUNDERBLUFF_PENNANT  = 63445,
    SPELL_AURA_POSTMAN_S        = 67376,
    SPELL_AURA_SHOP_S           = 67377,
    SPELL_AURA_BANK_S           = 67368,
    SPELL_AURA_TIRED_S          = 67401,
    SPELL_AURA_BANK_G           = 68849,
    SPELL_AURA_POSTMAN_G        = 68850,
    SPELL_AURA_SHOP_G           = 68851,
    SPELL_AURA_TIRED_G          = 68852,
    SPELL_TIRED_PLAYER          = 67334
};

enum ArgentPetGossipOptions
{
    GOSSIP_OPTION_BANK                            = 0,
    GOSSIP_OPTION_SHOP                            = 1,
    GOSSIP_OPTION_MAIL                            = 2,
    GOSSIP_OPTION_DARNASSUS_SENJIN_PENNANT        = 3,
    GOSSIP_OPTION_EXODAR_UNDERCITY_PENNANT        = 4,
    GOSSIP_OPTION_GNOMEREGAN_ORGRIMMAR_PENNANT    = 5,
    GOSSIP_OPTION_IRONFORGE_SILVERMOON_PENNANT    = 6,
    GOSSIP_OPTION_STORMWIND_THUNDERBLUFF_PENNANT  = 7
};

enum Misc
{
    NPC_ARGENT_SQUIRE  = 33238
};

struct ArgentPonyBannerSpells
{
    uint32 spellSquire;
    uint32 spellGruntling;
};

ArgentPonyBannerSpells const bannerSpells[5] =
{
    { SPELL_DARNASSUS_PENNANT, SPELL_SENJIN_PENNANT },
    { SPELL_EXODAR_PENNANT, SPELL_UNDERCITY_PENNANT },
    { SPELL_GNOMEREGAN_PENNANT, SPELL_ORGRIMMAR_PENNANT },
    { SPELL_IRONFORGE_PENNANT, SPELL_SILVERMOON_PENNANT },
    { SPELL_STORMWIND_PENNANT, SPELL_THUNDERBLUFF_PENNANT }
};

class npc_argent_squire_gruntling : public CreatureScript
{
public:
    npc_argent_squire_gruntling() : CreatureScript("npc_argent_squire_gruntling") { }

    struct npc_argent_squire_gruntlingAI : public ScriptedAI
    {
        npc_argent_squire_gruntlingAI(Creature* creature) : ScriptedAI(creature)
        {
            ScheduleTasks();
        }

        void ScheduleTasks()
        {
            _scheduler
                .Schedule(Seconds(1), [this](TaskContext /*context*/)
                {
                    if (Aura* ownerTired = me->GetOwner()->GetAura(SPELL_TIRED_PLAYER))
                        if (Aura* squireTired = me->AddAura(IsArgentSquire() ? SPELL_AURA_TIRED_S : SPELL_AURA_TIRED_G, me))
                            squireTired->SetDuration(ownerTired->GetDuration());
                })
                .Schedule(Seconds(1), [this](TaskContext context)
                {
                    if ((me->HasAura(SPELL_AURA_TIRED_S) || me->HasAura(SPELL_AURA_TIRED_G)) && me->HasNpcFlag(UNIT_NPC_FLAG_BANKER | UNIT_NPC_FLAG_MAILBOX | UNIT_NPC_FLAG_VENDOR))
                        me->RemoveNpcFlag(UNIT_NPC_FLAG_BANKER | UNIT_NPC_FLAG_MAILBOX | UNIT_NPC_FLAG_VENDOR);
                    context.Repeat();
                });
        }

        bool OnGossipSelect(Player* player, uint32 /*menuId*/, uint32 gossipListId) override
        {
            switch (gossipListId)
            {
                case GOSSIP_OPTION_BANK:
                {
                    me->SetNpcFlag(UNIT_NPC_FLAG_BANKER);
                    uint32 _bankAura = IsArgentSquire() ? SPELL_AURA_BANK_S : SPELL_AURA_BANK_G;
                    if (!me->HasAura(_bankAura))
                        DoCastSelf(_bankAura);

                    if (!player->HasAura(SPELL_TIRED_PLAYER))
                        player->CastSpell(player, SPELL_TIRED_PLAYER, true);
                    break;
                }
                case GOSSIP_OPTION_SHOP:
                {
                    me->SetNpcFlag(UNIT_NPC_FLAG_VENDOR);
                    uint32 _shopAura = IsArgentSquire() ? SPELL_AURA_SHOP_S : SPELL_AURA_SHOP_G;
                    if (!me->HasAura(_shopAura))
                        DoCastSelf(_shopAura);

                    if (!player->HasAura(SPELL_TIRED_PLAYER))
                        player->CastSpell(player, SPELL_TIRED_PLAYER, true);
                    break;
                }
                case GOSSIP_OPTION_MAIL:
                {
                    me->SetNpcFlag(UNIT_NPC_FLAG_MAILBOX);
                    player->GetSession()->SendShowMailBox(me->GetGUID());

                    uint32 _mailAura = IsArgentSquire() ? SPELL_AURA_POSTMAN_S : SPELL_AURA_POSTMAN_G;
                    if (!me->HasAura(_mailAura))
                        DoCastSelf(_mailAura);

                    if (!player->HasAura(SPELL_TIRED_PLAYER))
                        player->CastSpell(player, SPELL_TIRED_PLAYER, true);
                    break;
                }
                case GOSSIP_OPTION_DARNASSUS_SENJIN_PENNANT:
                case GOSSIP_OPTION_EXODAR_UNDERCITY_PENNANT:
                case GOSSIP_OPTION_GNOMEREGAN_ORGRIMMAR_PENNANT:
                case GOSSIP_OPTION_IRONFORGE_SILVERMOON_PENNANT:
                case GOSSIP_OPTION_STORMWIND_THUNDERBLUFF_PENNANT:
                    if (IsArgentSquire())
                        DoCastSelf(bannerSpells[gossipListId - 3].spellSquire, true);
                    else
                        DoCastSelf(bannerSpells[gossipListId - 3].spellGruntling, true);
                    break;
            }
            player->PlayerTalkClass->SendCloseGossip();
            return false;
        }

        void UpdateAI(uint32 diff) override
        {
            _scheduler.Update(diff);
        }

        bool IsArgentSquire() const { return me->GetEntry() == NPC_ARGENT_SQUIRE; }

    private:
        TaskScheduler _scheduler;
    };

    CreatureAI* GetAI(Creature *creature) const override
    {
        return new npc_argent_squire_gruntlingAI(creature);
    }
};

enum BountifulTable
{
    SEAT_TURKEY_CHAIR                       = 0,
    SEAT_CRANBERRY_CHAIR                    = 1,
    SEAT_STUFFING_CHAIR                     = 2,
    SEAT_SWEET_POTATO_CHAIR                 = 3,
    SEAT_PIE_CHAIR                          = 4,
    SEAT_FOOD_HOLDER                        = 5,
    SEAT_PLATE_HOLDER                       = 6,
    NPC_THE_TURKEY_CHAIR                    = 34812,
    NPC_THE_CRANBERRY_CHAIR                 = 34823,
    NPC_THE_STUFFING_CHAIR                  = 34819,
    NPC_THE_SWEET_POTATO_CHAIR              = 34824,
    NPC_THE_PIE_CHAIR                       = 34822,
    SPELL_CRANBERRY_SERVER                  = 61793,
    SPELL_PIE_SERVER                        = 61794,
    SPELL_STUFFING_SERVER                   = 61795,
    SPELL_TURKEY_SERVER                     = 61796,
    SPELL_SWEET_POTATOES_SERVER             = 61797
};

typedef std::unordered_map<uint32 /*Entry*/, uint32 /*Spell*/> ChairSpells;
ChairSpells const _chairSpells =
{
    { NPC_THE_CRANBERRY_CHAIR, SPELL_CRANBERRY_SERVER },
    { NPC_THE_PIE_CHAIR, SPELL_PIE_SERVER },
    { NPC_THE_STUFFING_CHAIR, SPELL_STUFFING_SERVER },
    { NPC_THE_TURKEY_CHAIR, SPELL_TURKEY_SERVER },
    { NPC_THE_SWEET_POTATO_CHAIR, SPELL_SWEET_POTATOES_SERVER },
};

class CastFoodSpell : public BasicEvent
{
    public:
        CastFoodSpell(Unit* owner, uint32 spellId) : _owner(owner), _spellId(spellId) { }

        bool Execute(uint64 /*execTime*/, uint32 /*diff*/) override
        {
            _owner->CastSpell(_owner, _spellId, true);
            return true;
        }

    private:
        Unit* _owner;
        uint32 _spellId;
};

class npc_bountiful_table : public CreatureScript
{
public:
    npc_bountiful_table() : CreatureScript("npc_bountiful_table") { }

    struct npc_bountiful_tableAI : public PassiveAI
    {
        npc_bountiful_tableAI(Creature* creature) : PassiveAI(creature) { }

        void PassengerBoarded(Unit* who, int8 seatId, bool /*apply*/) override
        {
            float x = 0.0f;
            float y = 0.0f;
            float z = 0.0f;
            float o = 0.0f;

            switch (seatId)
            {
                case SEAT_TURKEY_CHAIR:
                    x = 3.87f;
                    y = 2.07f;
                    o = 3.700098f;
                    break;
                case SEAT_CRANBERRY_CHAIR:
                    x = 3.87f;
                    y = -2.07f;
                    o = 2.460914f;
                    break;
                case SEAT_STUFFING_CHAIR:
                    x = -2.52f;
                    break;
                case SEAT_SWEET_POTATO_CHAIR:
                    x = -0.09f;
                    y = -3.24f;
                    o = 1.186824f;
                    break;
                case SEAT_PIE_CHAIR:
                    x = -0.18f;
                    y = 3.24f;
                    o = 5.009095f;
                    break;
                case SEAT_FOOD_HOLDER:
                case SEAT_PLATE_HOLDER:
                    if (Vehicle* holders = who->GetVehicleKit())
                        holders->InstallAllAccessories(true);
                    return;
                default:
                    break;
            }

            std::function<void(Movement::MoveSplineInit&)> initializer = [=](Movement::MoveSplineInit& init)
            {
                init.DisableTransportPathTransformations();
                init.MoveTo(x, y, z, false);
                init.SetFacing(o);
            };
            who->GetMotionMaster()->LaunchMoveSpline(std::move(initializer), EVENT_VEHICLE_BOARD, MOTION_PRIORITY_HIGHEST);
            who->m_Events.AddEvent(new CastFoodSpell(who, _chairSpells.at(who->GetEntry())), who->m_Events.CalculateTime(1s));
            if (who->GetTypeId() == TYPEID_UNIT)
                who->SetDisplayId(who->ToCreature()->GetCreatureTemplate()->Modelid1);
        }
    };

    CreatureAI* GetAI(Creature* creature) const override
    {
        return new npc_bountiful_tableAI(creature);
    }
};

/**
 * @brief 虚空区域法术枚举
 */
enum VoidZone
{
    SPELL_CONSUMPTION     = 28874  ///< 消耗法术ID
};

/**
 * @brief 虚空区域NPC AI结构体
 *
 * 虚空区域是一个被动区域效果NPC，出现后会施放消耗法术。
 */
struct npc_gen_void_zone : public ScriptedAI
{
    /**
     * @brief 构造函数
     * @param creature NPC生物对象指针
     */
    npc_gen_void_zone(Creature* creature) : ScriptedAI(creature) { }

    /**
     * @brief 初始化AI
     *
     * 设置NPC为被动反应状态
     */
    void InitializeAI() override
    {
        me->SetReactState(REACT_PASSIVE);
    }

    /**
     * @brief 出现回调
     *
     * NPC出现2秒后施放消耗法术
     */
    void JustAppeared() override
    {
        _scheduler.Schedule(2s, [this](TaskContext /*task*/)
        {
            DoCastSelf(SPELL_CONSUMPTION);
        });
    }

    /**
     * @brief AI更新函数
     * @param diff 距离上次更新的时间间隔（毫秒）
     */
    void UpdateAI(uint32 diff) override
    {
        _scheduler.Update(diff);
    }

private:
    TaskScheduler _scheduler;  ///< 任务调度器
};

/**
 * @brief 注册所有特殊NPC脚本到脚本系统
 *
 * 此函数由脚本加载器在服务器启动时调用，用于将所有特殊NPC脚本注册到游戏中。
 * 创建并初始化所有特殊NPC脚本实例，使其能够在游戏中生效。
 *
 * 注册的脚本包括：
 * - 空中防御机器人
 * - 鸡（隐藏任务）
 * - 舞动火焰（仲夏节）
 * - 火把投掷目标控制器
 * - 仲夏节彩带柱
 * - 医生NPC
 * - 受伤病人
 * - 任务长袍相关NPC
 * - 守护者
 * - 蒸汽坦克
 * - 比赛坐骑
 * - 美酒节狂欢者
 * - 训练假人
 * - 虫洞传送NPC
 * - 宠物训练师
 * - 经验值相关NPC
 * - 春节兔子
 * - 球中的小鬼
 * - 稳定大师
 * - 火车破坏者
 * - 银色侍从/步兵
 * - 丰收节餐桌
 * - 虚空区域
 *
 * @note 此函数在服务器启动时由脚本系统自动调用，不应手动调用
 */
void AddSC_npcs_special()
{
    new npc_air_force_bots();
    new npc_chicken_cluck();
    RegisterCreatureAI(npc_dancing_flames);
    new npc_torch_tossing_target_bunny_controller();
    new npc_midsummer_bunny_pole();
    new npc_doctor();
    new npc_injured_patient();
    new npc_garments_of_quests();
    new npc_guardian();
    new npc_steam_tonk();
    new npc_tournament_mount();
    new npc_brewfest_reveler();
    RegisterCreatureAI(npc_brewfest_reveler_2);
    RegisterCreatureAI(npc_training_dummy);
    new npc_wormhole();
    new npc_pet_trainer();
    new npc_experience();
    new npc_spring_rabbit();
    new npc_imp_in_a_ball();
    new npc_stable_master();
    new npc_train_wrecker();
    new npc_argent_squire_gruntling();
    new npc_bountiful_table();
    RegisterCreatureAI(npc_gen_void_zone);
}
