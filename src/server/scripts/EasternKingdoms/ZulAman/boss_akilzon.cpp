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
 * @file boss_akilzon.cpp
 * @brief 祖阿曼副本 - 阿基尔松Boss脚本模块
 *
 * 本模块实现了阿基尔松Boss的战斗逻辑，包括：
 * - 静电干扰技能
 * - 闪电召唤技能
 * - 风之阵技能
 * - 雷电气风暴技能（核心机制）
 * - 召唤飞行之鹰
 * - 天气系统控制（战斗中会下雨）
 *
 * 阿基尔松是祖阿曼的第一个Boss，是一只鹰神化身。
 * 完成度: 75%
 * 待完成: 闪电召唤的计时器缺失，声音ID缺失
 */

/* ScriptData
SDName: boss_Akilzon
SD%Complete: 75%
SDComment: Missing timer for Call Lightning and Sound ID's
SQLUpdate:
#Temporary fix for Soaring Eagles

EndScriptData */

#include "ScriptMgr.h"
#include "CellImpl.h"
#include "GridNotifiersImpl.h"
#include "MiscPackets.h"
#include "MotionMaster.h"
#include "ObjectAccessor.h"
#include "ScriptedCreature.h"
#include "TemporarySummon.h"
#include "Weather.h"
#include "zulaman.h"

/**
 * @brief 技能枚举
 *
 * 定义阿基尔松使用的所有技能ID
 */
enum Spells
{
    SPELL_STATIC_DISRUPTION     = 43622,  ///< 静电干扰 - 对目标及其周围造成自然伤害
    SPELL_STATIC_VISUAL         = 45265,  ///< 静电视觉效果
    SPELL_CALL_LIGHTNING        = 43661,  ///< 闪电召唤 - 对目标造成自然伤害（计时器缺失）
    SPELL_GUST_OF_WIND          = 43621,  ///< 风之阵 - 将目标击退
    SPELL_ELECTRICAL_STORM      = 43648,  ///< 雷电气风暴 - 核心技能，持续伤害
    SPELL_BERSERK               = 45078,  ///< 狂暴 - 10分钟后进入狂暴
    SPELL_ELECTRICAL_OVERLOAD   = 43658,  ///< 电气过载
    SPELL_EAGLE_SWOOP           = 44732,  ///< 鹰俯冲 - 飞行之鹰使用的技能
    SPELL_ZAP                   = 43137,  ///< 电击 - 风暴云使用的技能
    SPELL_SAND_STORM            = 25160   ///< 沙尘暴
};

/**
 * @brief 对话和喊话枚举
 *
 * 定义阿基尔松的各种对话ID
 */
enum Says
{
    SAY_AGGRO                   = 0,  ///< 开战喊话
    SAY_SUMMON                  = 1,  ///< 召唤飞行之鹰
    SAY_INTRO                   = 2,  ///< 开场白（脚本中未使用）
    SAY_ENRAGE                  = 3,  ///< 狂暴喊话
    SAY_KILL                    = 4,  ///< 击杀玩家
    SAY_DEATH                   = 5   ///< 死亡喊话
};

/**
 * @brief 其他常量枚举
 *
 * 定义NPC ID和位置常量
 */
enum Misc
{
    NPC_SOARING_EAGLE           = 24858,  ///< 飞行之鹰NPC ID
    SE_LOC_X_MAX                = 400,    ///< 飞行之鹰生成X坐标最大值
    SE_LOC_X_MIN                = 335,    ///< 飞行之鹰生成X坐标最小值
    SE_LOC_Y_MAX                = 1435,   ///< 飞行之鹰生成Y坐标最大值
    SE_LOC_Y_MIN                = 1370    ///< 飞行之鹰生成Y坐标最小值
};

/**
 * @brief 事件枚举
 *
 * 定义Boss战斗中使用的各种事件类型
 */
enum Events
{
    EVENT_STATIC_DISRUPTION     = 1,  ///< 静电干扰事件
    EVENT_GUST_OF_WIND          = 2,  ///< 风之阵事件
    EVENT_CALL_LIGHTNING        = 3,  ///< 闪电召唤事件
    EVENT_ELECTRICAL_STORM      = 4,  ///< 雷电气风暴事件
    EVENT_RAIN                  = 5,  ///< 下雨事件
    EVENT_SUMMON_EAGLES         = 6,  ///< 召唤飞行之鹰事件
    EVENT_STORM_SEQUENCE        = 7,  ///< 风暴序列事件
    EVENT_ENRAGE                = 8   ///< 狂暴事件
};

/**
 * @brief 阿基尔松Boss脚本类
 *
 * 实现阿基尔松的完整战斗逻辑
 */
class boss_akilzon : public CreatureScript
{
    public:
        /**
         * @brief 构造函数
         */
        boss_akilzon() : CreatureScript("boss_akilzon") { }

        /**
         * @brief 阿基尔松AI类
         *
         * 继承自BossAI，实现阿基尔松的所有战斗行为
         */
        struct boss_akilzonAI : public BossAI
        {
            /**
             * @brief 构造函数
             * @param creature 生物对象指针
             */
            boss_akilzonAI(Creature* creature) : BossAI(creature, BOSS_AKILZON)
            {
                Initialize();
            }

            /**
             * @brief 初始化所有GUID和状态变量
             *
             * 清空所有保存的GUID，重置风暴计数和天气状态
             */
            void Initialize()
            {
                TargetGUID.Clear();
                CloudGUID.Clear();
                CycloneGUID.Clear();
                for (ObjectGuid& guid : BirdGUIDs)
                    guid.Clear();

                StormCount = 0;    // 风暴阶段计数器
                isRaining = false; // 是否正在下雨
            }

            /**
             * @brief 重置Boss状态
             *
             * 当Boss脱离战斗或重置时调用
             * 恢复天气为晴朗状态
             */
            void Reset() override
            {
                _Reset();

                Initialize();

                // 重置天气为晴朗
                SetWeather(WEATHER_STATE_FINE, 0.0f);
            }

            /**
             * @brief 进入战斗
             * @param who 进入战斗的目标
             *
             * 启动所有战斗事件的计时器
             */
            void JustEngagedWith(Unit* who) override
            {
                BossAI::JustEngagedWith(who);

                // 安排各技能的初始施放时间
                events.ScheduleEvent(EVENT_STATIC_DISRUPTION, 10s, 20s); // 静电干扰：10-20秒
                events.ScheduleEvent(EVENT_GUST_OF_WIND, 20s, 30s);      // 风之阵：20-30秒
                events.ScheduleEvent(EVENT_CALL_LIGHTNING, 10s, 20s);    // 闪电召唤：随机时间
                events.ScheduleEvent(EVENT_ELECTRICAL_STORM, 1min);      // 雷电气风暴：60秒
                events.ScheduleEvent(EVENT_RAIN, 47s, 52s);              // 下雨：47-52秒
                events.ScheduleEvent(EVENT_ENRAGE, 10min);               // 狂暴：10分钟

                Talk(SAY_AGGRO);
            }

            /**
             * @brief Boss死亡
             * @param killer 击杀者（未使用）
             */
            void JustDied(Unit* /*killer*/) override
            {
                Talk(SAY_DEATH);
                _JustDied();
            }

            /**
             * @brief 击杀单位
             * @param who 被击杀的单位
             *
             * 当击杀玩家时播放击杀喊话
             */
            void KilledUnit(Unit* who) override
            {
                if (who->GetTypeId() == TYPEID_PLAYER)
                    Talk(SAY_KILL);
            }

            /**
             * @brief 设置天气
             * @param weather 天气状态类型
             * @param grade 天气强度等级
             *
             * 向副本中所有玩家发送天气更新包
             * 用于雷电气风暴期间制造下雨效果
             */
            void SetWeather(WeatherState weather, float grade)
            {
                Map* map = me->GetMap();
                if (!map->IsDungeon())
                    return;

                map->SendToPlayers(WorldPackets::Misc::Weather(weather, grade).Write());
            }

            /**
             * @brief 处理风暴序列
             * @param Cloud 风暴云单位指针
             *
             * 执行雷电气风暴的序列化伤害处理
             * 阶段1: 开始，阶段2-9: 伤害递增，阶段10: 结束
             *
             * @note 伤害每阶段翻倍，需要玩家及时躲避
             */
            void HandleStormSequence(Unit* Cloud) // 1: begin, 2-9: tick, 10: end
            {
                if (StormCount < 10 && StormCount > 1)
                {
                    // 计算伤害：基础800，每阶段翻倍
                    int32 bp0 = 800;
                    for (uint8 i = 2; i < StormCount; ++i)
                        bp0 *= 2;

                    // 搜索周围所有有效目标
                    std::list<Unit*> tempUnitMap;
                    Trinity::AnyAoETargetUnitInObjectRangeCheck u_check(me, me, SIZE_OF_GRIDS);
                    Trinity::UnitListSearcher<Trinity::AnyAoETargetUnitInObjectRangeCheck> searcher(me, tempUnitMap, u_check);
                    Cell::VisitAllObjects(me, searcher, SIZE_OF_GRIDS);

                    // 对距离风暴云6码以外的目标造成伤害
                    for (std::list<Unit*>::const_iterator i = tempUnitMap.begin(); i != tempUnitMap.end(); ++i)
                    {
                        if (Unit* target = (*i))
                        {
                            if (Cloud && !Cloud->IsWithinDist(target, 6, false))
                            {
                                CastSpellExtraArgs args;
                                args.TriggerFlags = TRIGGERED_FULL_MASK;
                                args.OriginalCaster = me->GetGUID();
                                args.AddSpellMod(SPELLVALUE_BASE_POINT0, bp0);
                                Cloud->CastSpell(target, SPELL_ZAP, args);
                            }
                        }
                    }

                    // 创建视觉效果：随机位置的闪电触发器
                    float x, y, z;
                    z = me->GetPositionZ();
                    uint8 maxCount = 5 + rand32() % 5;
                    for (uint8 i = 0; i < maxCount; ++i)
                    {
                        x = 343.0f + rand32() % 60;
                        y = 1380.0f + rand32() % 60;
                        if (Unit* trigger = me->SummonTrigger(x, y, z, 0, 2s))
                        {
                            trigger->SetFaction(FACTION_FRIENDLY);
                            trigger->SetMaxHealth(100000);
                            trigger->SetHealth(100000);
                            trigger->RemoveUnitFlag(UNIT_FLAG_UNINTERACTIBLE);
                            if (Cloud)
                            {
                                CastSpellExtraArgs args;
                                args.TriggerFlags = TRIGGERED_FULL_MASK;
                                args.OriginalCaster = Cloud->GetGUID();
                                args.AddSpellMod(SPELLVALUE_BASE_POINT0, bp0);
                                Cloud->CastSpell(trigger, SPELL_ZAP, args);
                            }
                        }
                    }
                }

                ++StormCount;

                // 风暴结束
                if (StormCount > 10)
                {
                    StormCount = 0; // finish
                    events.ScheduleEvent(EVENT_SUMMON_EAGLES, 5s);  // 风暴结束后召唤飞行之鹰
                    me->InterruptNonMeleeSpells(false);
                    CloudGUID.Clear();
                    if (Cloud)
                        Cloud->KillSelf();
                    SetWeather(WEATHER_STATE_FINE, 0.0f);  // 恢复晴天
                    isRaining = false;
                }
                events.ScheduleEvent(EVENT_STORM_SEQUENCE, 1s);
            }

            /**
             * @brief 更新AI
             * @param diff 距离上次更新的时间间隔（毫秒）
             *
             * 主更新函数，处理所有战斗事件
             *
             * 战斗阶段技能循环：
             * - 静电干扰：每10-18秒
             * - 风之阵：每20-30秒
             * - 闪电召唤：每12-17秒
             * - 雷电气风暴：每60秒（核心技能）
             * - 召唤飞行之鹰：雷电气风暴结束后5秒
             */
            void UpdateAI(uint32 diff) override
            {
                if (!UpdateVictim())
                    return;

                events.Update(diff);

                while (uint32 eventId = events.ExecuteEvent())
                {
                    switch (eventId)
                    {
                        case EVENT_STATIC_DISRUPTION:
                            {
                                // 选择随机目标施放静电干扰
                                Unit* target = SelectTarget(SelectTargetMethod::Random, 1);
                                if (!target)
                                    target = me->GetVictim();
                                if (target)
                                {
                                    TargetGUID = target->GetGUID();
                                    DoCast(target, SPELL_STATIC_DISRUPTION, false);
                                }
                                events.ScheduleEvent(EVENT_STATIC_DISRUPTION, 10s, 18s);
                                break;
                            }
                        case EVENT_GUST_OF_WIND:
                            {
                                // 选择随机目标施放风之阵，将其击退
                                Unit* target = SelectTarget(SelectTargetMethod::Random, 1);
                                if (!target)
                                    target = me->GetVictim();
                                if (target)
                                    DoCast(target, SPELL_GUST_OF_WIND);
                                events.ScheduleEvent(EVENT_GUST_OF_WIND, 20s, 30s);
                                break;
                            }
                        case EVENT_CALL_LIGHTNING:
                            // 对当前目标施放闪电召唤
                            DoCastVictim(SPELL_CALL_LIGHTNING);
                            events.ScheduleEvent(EVENT_CALL_LIGHTNING, 12s, 17s);
                            break;
                        case EVENT_ELECTRICAL_STORM:
                            {
                                // 核心技能：雷电气风暴
                                Unit* target = SelectTarget(SelectTargetMethod::Random, 0, 50, true);
                                if (!target)
                                {
                                    EnterEvadeMode();
                                    return;
                                }
                                // 在目标位置创建云视觉
                                target->CastSpell(target, 44007, true);
                                DoCast(target, SPELL_ELECTRICAL_STORM, false);
                                float x, y, z;
                                target->GetPosition(x, y, z);

                                // 召唤风暴云
                                Unit* Cloud = me->SummonTrigger(x, y, me->GetPositionZ()+16, 0, 15s);
                                if (Cloud)
                                    {
                                        CloudGUID = Cloud->GetGUID();
                                        Cloud->SetDisableGravity(true);
                                        Cloud->StopMoving();
                                        Cloud->SetObjectScale(1.0f);
                                        Cloud->SetFaction(FACTION_FRIENDLY);
                                        Cloud->SetMaxHealth(9999999);
                                        Cloud->SetHealth(9999999);
                                        Cloud->RemoveUnitFlag(UNIT_FLAG_UNINTERACTIBLE);
                                    }
                                StormCount = 1;
                                events.ScheduleEvent(EVENT_ELECTRICAL_STORM, 1min);
                                events.ScheduleEvent(EVENT_RAIN, 47s, 52s);
                                break;
                            }
                        case EVENT_RAIN:
                            // 控制天气：开始下大雨
                            if (!isRaining)
                            {
                                SetWeather(WEATHER_STATE_HEAVY_RAIN, 0.9999f);
                                isRaining = true;
                            }
                            else
                                events.ScheduleEvent(EVENT_RAIN, 1s);
                            break;
                        case EVENT_STORM_SEQUENCE:
                            {
                                // 执行风暴序列
                                Unit* target = ObjectAccessor::GetUnit(*me, CloudGUID);
                                if (!target || !target->IsAlive())
                                {
                                    EnterEvadeMode();
                                    return;
                                }
                                else if (Unit* Cyclone = ObjectAccessor::GetUnit(*me, CycloneGUID))
                                    Cyclone->CastSpell(target, SPELL_SAND_STORM, true);
                                HandleStormSequence(target);
                                break;
                            }
                        case EVENT_SUMMON_EAGLES:
                            // 召唤8只飞行之鹰
                            Talk(SAY_SUMMON);

                            float x, y, z;
                            me->GetPosition(x, y, z);

                            for (uint8 i = 0; i < 8; ++i)
                            {
                                Unit* bird = ObjectAccessor::GetUnit(*me, BirdGUIDs[i]);
                                if (!bird) // 飞行之鹰已死亡或不存在
                                {
                                    if (Unit* target = SelectTarget(SelectTargetMethod::Random, 0))
                                    {
                                        x = target->GetPositionX() + irand(-10, 10);
                                        y = target->GetPositionY() + irand(-10, 10);
                                        z = target->GetPositionZ() + urand(16, 20);
                                        if (z > 95)
                                            z = 95.0f - urand(0, 5);
                                    }
                                    Creature* creature = me->SummonCreature(NPC_SOARING_EAGLE, x, y, z, 0, TEMPSUMMON_CORPSE_DESPAWN);
                                    if (creature)
                                    {
                                        AddThreat(me->GetVictim(), 1.0f, creature);
                                        creature->AI()->AttackStart(me->GetVictim());
                                        BirdGUIDs[i] = creature->GetGUID();
                                    }
                                }
                            }
                            break;
                        case EVENT_ENRAGE:
                             // 进入狂暴状态
                             Talk(SAY_ENRAGE);
                             DoCast(me, SPELL_BERSERK, true);
                            events.ScheduleEvent(EVENT_ENRAGE, 10min);
                            break;
                        default:
                            break;
                    }
                }

                DoMeleeAttackIfReady();
            }

            private:
                ObjectGuid BirdGUIDs[8];  ///< 8只飞行之鹰的GUID数组
                ObjectGuid TargetGUID;    ///< 静电干扰目标GUID
                ObjectGuid CycloneGUID;   ///< 旋风GUID
                ObjectGuid CloudGUID;     ///< 风暴云GUID
                uint8  StormCount;        ///< 风暴阶段计数器（1-10）
                bool   isRaining;         ///< 是否正在下雨
        };

        /**
         * @brief 获取AI实例
         * @param creature 生物对象指针
         * @return 返回阿基尔松AI实例
         */
        CreatureAI* GetAI(Creature* creature) const override
        {
            return GetZulAmanAI<boss_akilzonAI>(creature);
        }
};

/**
 * @brief 飞行之鹰NPC脚本类
 *
 * 实现飞行之鹰的行为逻辑
 * 飞行之鹰会在空中飞行，间歇性地俯冲攻击玩家
 */
class npc_akilzon_eagle : public CreatureScript
{
    public:
        /**
         * @brief 构造函数
         */
        npc_akilzon_eagle() : CreatureScript("npc_akilzon_eagle") { }

        /**
         * @brief 飞行之鹰AI类
         *
         * 继承自ScriptedAI，实现飞行之鹰的俯冲攻击行为
         */
        struct npc_akilzon_eagleAI : public ScriptedAI
        {
            /**
             * @brief 构造函数
             * @param creature 生物对象指针
             */
            npc_akilzon_eagleAI(Creature* creature) : ScriptedAI(creature)
            {
                Initialize();
            }

            /**
             * @brief 初始化计时器和状态变量
             */
            void Initialize()
            {
                EagleSwoop_Timer = urand(5000, 10000);  // 俯冲冷却：5-10秒
                arrived = true;                          // 已到达目标点
                TargetGUID.Clear();
            }

            uint32 EagleSwoop_Timer;  ///< 俯冲技能计时器
            bool arrived;             ///< 是否已到达移动目标点
            ObjectGuid TargetGUID;    ///< 俯冲目标GUID

            /**
             * @brief 重置状态
             */
            void Reset() override
            {
                Initialize();
                me->SetDisableGravity(true);  // 飞行之鹰可以飞行
            }

            /**
             * @brief 进入战斗
             * @param who 进入战斗的目标（未使用）
             */
            void JustEngagedWith(Unit* /*who*/) override
            {
                DoZoneInCombat();
            }

            /**
             * @brief 视线内移动处理
             * @param who 进入视线范围的单位（未使用）
             *
             * 飞行之鹰不响应视线触发
             */
            void MoveInLineOfSight(Unit* /*who*/) override { }

            /**
             * @brief 移动完成通知
             * @param type 移动类型（未使用）
             * @param id 移动ID（未使用）
             *
             * 当飞行之鹰到达目标点时触发
             * 如果是俯冲攻击到达，则施放俯冲技能
             */
            void MovementInform(uint32, uint32) override
            {
                arrived = true;
                if (TargetGUID)
                {
                    if (Unit* target = ObjectAccessor::GetUnit(*me, TargetGUID))
                        DoCast(target, SPELL_EAGLE_SWOOP, true);  // 施放俯冲技能
                    TargetGUID.Clear();
                    me->SetSpeedRate(MOVE_RUN, 1.2f);  // 恢复正常移动速度
                    EagleSwoop_Timer = urand(5000, 10000);  // 重置俯冲冷却
                }
            }

            /**
             * @brief 更新AI
             * @param diff 距离上次更新的时间间隔（毫秒）
             *
             * 飞行之鹰的移动和攻击逻辑
             * - 在空中随机移动
             * - 俯冲计时器归零后俯冲攻击玩家
             */
            void UpdateAI(uint32 diff) override
            {
                // 更新俯冲计时器
                if (EagleSwoop_Timer <= diff)
                    EagleSwoop_Timer = 0;
                else
                    EagleSwoop_Timer -= diff;

                if (arrived)
                {
                    if (Unit* target = SelectTarget(SelectTargetMethod::Random, 0))
                    {
                        float x, y, z;
                        if (EagleSwoop_Timer)
                        {
                            // 随机移动到目标附近的高空
                            x = target->GetPositionX() + irand(-10, 10);
                            y = target->GetPositionY() + irand(-10, 10);
                            z = target->GetPositionZ() + urand(10, 15);
                            if (z > 95)
                                z = 95.0f - urand(0, 5);
                        }
                        else
                        {
                            // 俯冲攻击：快速移动到目标位置
                            target->GetContactPoint(me, x, y, z);
                            z += 2;
                            me->SetSpeedRate(MOVE_RUN, 5.0f);  // 加速俯冲
                            TargetGUID = target->GetGUID();
                        }
                        me->GetMotionMaster()->MovePoint(0, x, y, z);
                        arrived = false;
                    }
                }
            }
        };

        /**
         * @brief 获取AI实例
         * @param creature 生物对象指针
         * @return 返回飞行之鹰AI实例
         */
        CreatureAI* GetAI(Creature* creature) const override
        {
            return GetZulAmanAI<npc_akilzon_eagleAI>(creature);
        }
};

/**
 * @brief 注册阿基尔松Boss脚本
 *
 * 将阿基尔松Boss和飞行之鹰脚本注册到脚本系统中
 */
void AddSC_boss_akilzon()
{
    new boss_akilzon();
    new npc_akilzon_eagle();
}
