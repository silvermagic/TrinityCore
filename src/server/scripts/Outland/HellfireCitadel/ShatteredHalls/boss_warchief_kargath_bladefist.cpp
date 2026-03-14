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
 * @file boss_warchief_kargath_bladefist.cpp
 * @brief 战争酋长卡加斯·刃拳Boss脚本模块
 *
 * 本模块实现破碎大厅副本最终Boss——战争酋长卡加斯·刃拳的战斗逻辑。
 * 卡加斯是破碎大厅的领袖,以快速的刀刃之舞和召唤援军著称。
 *
 * 战斗机制:
 * 1. 刀刃之舞: Boss快速移动并在房间内穿梭,对经过的玩家造成伤害
 * 2. 召唤援军: 定期召唤破碎大厅的卫兵协助战斗
 * 3. 刺客入侵: 战斗开始时从入口和出口召唤刺客
 * 4. 英雄模式冲锋: 刀刃之舞后对随机目标冲锋
 *
 * 特殊机制:
 * - Boss脱离战斗区域会触发逃避
 * - 每次召唤援军的数量可能增加(有概率增加)
 * - 刀刃之舞期间Boss移动速度加倍
 */

#include "ScriptMgr.h"
#include "InstanceScript.h"
#include "MotionMaster.h"
#include "ObjectAccessor.h"
#include "ScriptedCreature.h"
#include "shattered_halls.h"

/**
 * @brief 对话ID枚举
 */
enum Says
{
    SAY_AGGRO                      = 0,    // 进入战斗时的对话
    SAY_SLAY                       = 1,    // 击杀玩家时的对话
    SAY_DEATH                      = 2,    // 死亡时的对话

    SAY_CALL_EXECUTIONER_A         = 3,    // 呼唤行刑者(联盟)
    SAY_CALL_EXECUTIONER_H         = 4     // 呼唤行刑者(部落)
};

/**
 * @brief 技能ID枚举
 */
enum Spells
{
     SPELL_BLADE_DANCE             = 30739,    // 刀刃之舞 - 对周围敌人造成伤害
     H_SPELL_CHARGE                = 25821     // 英雄模式冲锋 - 快速接近目标
};

/**
 * @brief 生物ID枚举
 */
enum Creatures
{
    NPC_SHATTERED_ASSASSIN         = 17695,    // 破碎刺客 - 从入口和出口刷新
    NPC_HEARTHEN_GUARD             = 17621,    // 坚韧卫兵 - 近战援军
    NPC_SHARPSHOOTER_GUARD         = 17622,    // 神射手卫兵 - 远程援军
    NPC_REAVER_GUARD               = 17623     // 掠夺者卫兵 - 近战援军
};

#define TARGET_NUM                   5         // 刀刃之舞的目标跳转次数

/**
 * @brief 刺客刷新位置
 *
 * 刺客从两个位置刷新:入口和出口
 * 每个位置刷新2个刺客(Y坐标±8)
 */
float AssassEntrance[3] = { 275.136f, -84.29f, 2.3f  }; // 入口位置 // y -8
float AssassExit[3]     = { 184.233f, -84.29f, 2.3f  }; // 出口位置 // y -8
float AddsEntrance[3]   = { 306.036f, -84.29f, 1.93f }; // 援军刷新位置

/**
 * @brief 战争酋长卡加斯·刃拳Boss脚本类
 *
 * 实现卡加斯的AI行为,包括刀刃之舞和召唤援军机制
 */
class boss_warchief_kargath_bladefist : public CreatureScript
{
    public:
        /**
         * @brief 构造函数
         *
         * 注册脚本名称为"boss_warchief_kargath_bladefist"
         */
        boss_warchief_kargath_bladefist() : CreatureScript("boss_warchief_kargath_bladefist") { }

        /**
         * @brief 卡加斯AI结构体
         *
         * 继承自BossAI,实现卡加斯的完整战斗逻辑
         */
        struct boss_warchief_kargath_bladefistAI : public BossAI
        {
            /**
             * @brief 构造函数
             * @param creature 生物指针
             *
             * 初始化基类和成员变量
             */
            boss_warchief_kargath_bladefistAI(Creature* creature) : BossAI(creature, DATA_KARGATH)
            {
                Initialize();
                target_num = 0;
            }

            /**
             * @brief 初始化成员变量
             *
             * 设置所有技能计时器和状态标志为默认值
             */
            void Initialize()
            {
                summoned = 2;  // 初始召唤2个援军
                InBlade = false;  // 不在刀刃之舞状态
                Wait_Timer = 0;  // 等待计时器

                Charge_timer = 0;  // 英雄模式冲锋计时器
                Blade_Dance_Timer = 45000;  // 45秒后刀刃之舞
                Summon_Assistant_Timer = 30000;  // 30秒后召唤援军
                Assassins_Timer = 5000;  // 5秒后召唤刺客
                resetcheck_timer = 5000;  // 5秒检查一次位置
            }

            /**
             * @brief 执行动作
             * @param action 动作ID
             *
             * 处理外部触发的动作:
             * - ACTION_EXECUTIONER_TAUNT: 行刑者嘲讽,根据阵营播放不同对话
             *
             * @调用时机 行刑者脚本触发时
             */
            void DoAction(int32 action) override
            {
                if (action == ACTION_EXECUTIONER_TAUNT)
                {
                    // 根据副本中的阵营播放不同的对话
                    switch (instance->GetData(DATA_TEAM_IN_INSTANCE))
                    {
                        case ALLIANCE:
                            Talk(SAY_CALL_EXECUTIONER_A);  // 联盟对话
                            break;
                        case HORDE:
                            Talk(SAY_CALL_EXECUTIONER_H);  // 部落对话
                            break;
                        default:
                            break;
                    }
                }
            }

            /**
             * @brief 重置Boss状态
             *
             * 恢复Boss到初始状态:
             * 1. 移除所有召唤的援军和刺客
             * 2. 调用基类重置函数
             * 3. 设置移动速度为正常(非刀刃之舞速度)
             * 4. 初始化所有计时器和状态
             *
             * @调用时机 战斗结束、团灭重置、副本重置时
             */
            void Reset() override
            {
                removeAdds();  // 移除所有小怪
                _Reset();
                me->SetSpeedRate(MOVE_RUN, 2);  // 恢复正常移动速度
                me->SetWalk(false);  // 设置为跑步模式

                Initialize();
            }

            /**
             * @brief Boss死亡时调用
             * @param killer 击杀者(未使用)
             *
             * 执行死亡处理:
             * 1. 调用基类死亡函数
             * 2. 播放死亡对话
             * 3. 移除所有小怪
             *
             * @调用时机 Boss血量降为0时
             */
            void JustDied(Unit* /*killer*/) override
            {
                _JustDied();
                Talk(SAY_DEATH);  // 播放死亡对话
                removeAdds();  // 清除所有小怪
            }

            /**
             * @brief 进入战斗时调用
             * @param who 攻击者(未使用)
             *
             * 播放进入战斗对话
             *
             * @调用时机 Boss被玩家攻击时
             */
            void JustEngagedWith(Unit* /*who*/) override
            {
                Talk(SAY_AGGRO);  // 播放进入战斗对话
            }

            /**
             * @brief 召唤生物时调用
             * @param summon 召唤的生物
             *
             * 根据召唤的生物类型进行处理:
             * - 卫兵: 攻击随机目标,加入援军列表
             * - 刺客: 加入刺客列表
             *
             * @调用时机 Boss召唤任何生物时
             */
            void JustSummoned(Creature* summon) override
            {
                switch (summon->GetEntry())
                {
                    case NPC_HEARTHEN_GUARD:
                    case NPC_SHARPSHOOTER_GUARD:
                    case NPC_REAVER_GUARD:
                        // 卫兵攻击随机目标
                        summon->AI()->AttackStart(SelectTarget(SelectTargetMethod::Random, 0));
                        adds.push_back(summon->GetGUID());  // 加入援军列表
                        break;
                    case NPC_SHATTERED_ASSASSIN:
                        assassins.push_back(summon->GetGUID());  // 加入刺客列表
                        break;
                }
            }

            /**
             * @brief 击杀单位时调用
             * @param victim 被击杀的单位
             *
             * 击杀玩家时播放击杀对话
             *
             * @调用时机 Boss击杀任何单位时
             */
            void KilledUnit(Unit* victim) override
            {
                if (victim->GetTypeId() == TYPEID_PLAYER)
                {
                    Talk(SAY_SLAY);  // 播放击杀对话
                }
            }

            /**
             * @brief 移动完成通知
             * @param type 移动类型
             * @param id 移动点ID
             *
             * 处理刀刃之舞期间的移动:
             * 1. 检查是否在刀刃之舞状态
             * 2. 确认是点移动类型且ID为1
             * 3. 如果还有剩余跳转次数,施放刀刃之舞并移动到下一个点
             *
             * @调用时机 Boss到达移动目标点时
             */
            void MovementInform(uint32 type, uint32 id) override
            {
                if (InBlade)
                {
                    // 只处理点移动类型
                    if (type != POINT_MOTION_TYPE)
                        return;

                    // 只处理ID为1的移动
                    if (id != 1)
                        return;

                    // 如果还有剩余跳转次数,防止死循环
                    if (target_num > 0) // to prevent loops
                    {
                        Wait_Timer = 1;  // 设置等待时间,触发下一次移动
                        DoCast(me, SPELL_BLADE_DANCE, true);  // 施放刀刃之舞
                        target_num--;  // 减少跳转次数
                    }
                }
            }

            /**
             * @brief 移除所有召唤的小怪
             *
             * 清除所有援军和刺客:
             * 1. 遍历援军列表,消散存活的援军
             * 2. 遍历刺客列表,消散存活的刺客
             * 3. 清空列表
             *
             * @调用时机 Reset()和JustDied()中调用
             */
            void removeAdds()
            {
                // 移除援军
                for (GuidVector::const_iterator itr = adds.begin(); itr!= adds.end(); ++itr)
                {
                    Creature* creature = ObjectAccessor::GetCreature(*me, *itr);
                    if (creature && creature->IsAlive())
                        creature->DespawnOrUnsummon();
                }
                adds.clear();

                // 移除刺客
                for (GuidVector::const_iterator itr = assassins.begin(); itr!= assassins.end(); ++itr)
                {
                    Creature* creature = ObjectAccessor::GetCreature(*me, *itr);
                    if (creature && creature->IsAlive())
                        creature->DespawnOrUnsummon();
                }
                assassins.clear();
            }

            /**
             * @brief 召唤刺客
             *
             * 在入口和出口各召唤2个刺客:
             * - 入口位置: Y坐标±8各召唤1个
             * - 出口位置: Y坐标±8各召唤1个
             * - 刺客在脱离战斗30秒后消散
             *
             * @调用时机 UpdateAI中Assassins_Timer计时器触发时
             */
            void SpawnAssassin()
            {
                // 入口位置召唤2个刺客(Y坐标±8)
                me->SummonCreature(NPC_SHATTERED_ASSASSIN, AssassEntrance[0], AssassEntrance[1]+8, AssassEntrance[2], 0, TEMPSUMMON_TIMED_DESPAWN_OUT_OF_COMBAT, 30s);
                me->SummonCreature(NPC_SHATTERED_ASSASSIN, AssassEntrance[0], AssassEntrance[1]-8, AssassEntrance[2], 0, TEMPSUMMON_TIMED_DESPAWN_OUT_OF_COMBAT, 30s);
                // 出口位置召唤2个刺客(Y坐标±8)
                me->SummonCreature(NPC_SHATTERED_ASSASSIN, AssassExit[0], AssassExit[1]+8, AssassExit[2], 0, TEMPSUMMON_TIMED_DESPAWN_OUT_OF_COMBAT, 30s);
                me->SummonCreature(NPC_SHATTERED_ASSASSIN, AssassExit[0], AssassExit[1]-8, AssassExit[2], 0, TEMPSUMMON_TIMED_DESPAWN_OUT_OF_COMBAT, 30s);
            }

            /**
             * @brief 更新AI状态(每帧调用)
             * @param diff 距离上次调用的时间(毫秒)
             *
             * 主循环函数,处理卡加斯的所有行为逻辑:
             *
             * 阶段1: 刺客召唤
             * - 战斗开始5秒后召唤4个刺客
             *
             * 阶段2: 刀刃之舞状态
             * - Boss快速移动到随机位置
             * - 每次到达位置施放刀刃之舞
             * - 移动5次后结束刀刃之舞
             * - 英雄模式:刀刃之舞结束后5秒冲锋
             *
             * 阶段3: 常规战斗状态
             * - 30秒后开始刀刃之舞
             * - 25-35秒召唤援军
             * - 英雄模式:刀刃之舞后5秒冲锋
             * - 每5秒检查Boss位置,防止拉出房间
             *
             * @调用时机 游戏主循环每帧调用
             * @性能注意事项 高频调用函数,避免复杂计算
             */
            void UpdateAI(uint32 diff) override
            {
                // 没有目标则返回
                //Return since we have no target
                if (!UpdateVictim())
                    return;

                // 刺客召唤计时器
                if (Assassins_Timer)
                {
                    if (Assassins_Timer <= diff)
                    {
                        SpawnAssassin();  // 召唤刺客
                        Assassins_Timer = 0;  // 不再召唤
                    }
                    else
                        Assassins_Timer -= diff;
                }

                // 刀刃之舞状态处理
                if (InBlade)
                {
                    if (Wait_Timer)
                    {
                        if (Wait_Timer <= diff)
                        {
                            if (target_num <= 0)
                            {
                                // 刀刃之舞结束
                                // stop bladedance
                                InBlade = false;  // 退出刀刃之舞状态
                                me->SetSpeedRate(MOVE_RUN, 2);  // 恢复正常速度
                                me->GetMotionMaster()->MoveChase(me->GetVictim());  // 追击目标
                                Blade_Dance_Timer = 30000;  // 30秒后再次刀刃之舞
                                Wait_Timer = 0;
                                if (IsHeroic())
                                    Charge_timer = 5000;  // 英雄模式:5秒后冲锋
                            }
                            else
                            {
                                // 刀刃之舞中移动
                                //move in bladedance
                                float x, y, randx, randy;
                                // 随机生成目标位置(在房间范围内)
                                randx = 0.0f + rand32() % 40;
                                randy = 0.0f + rand32() % 40;
                                x = 210+ randx;  // X范围: 210-250
                                y = -60- randy;  // Y范围: -60到-100
                                me->GetMotionMaster()->MovePoint(1, x, y, me->GetPositionZ());  // 移动到目标点
                                Wait_Timer = 0;
                            }
                        }
                        else
                            Wait_Timer -= diff;
                    }
                }
                else
                {
                    // 常规战斗状态
                    // 刀刃之舞计时器
                    if (Blade_Dance_Timer)
                    {
                        if (Blade_Dance_Timer <= diff)
                        {
                            // 开始刀刃之舞
                            target_num = TARGET_NUM;  // 设置跳转次数为5
                            Wait_Timer = 1;  // 触发移动
                            InBlade = true;  // 进入刀刃之舞状态
                            Blade_Dance_Timer = 0;
                            me->SetSpeedRate(MOVE_RUN, 4);  // 移动速度加倍
                            return;
                        }
                        else
                            Blade_Dance_Timer -= diff;
                    }

                    // 英雄模式冲锋计时器
                    if (Charge_timer)
                    {
                        if (Charge_timer <= diff)
                        {
                            // 对随机目标冲锋
                            DoCast(SelectTarget(SelectTargetMethod::Random, 0), H_SPELL_CHARGE);
                            Charge_timer = 0;
                        }
                        else
                            Charge_timer -= diff;
                    }

                    // 召唤援军计时器
                    if (Summon_Assistant_Timer <= diff)
                    {
                        // 召唤指定数量的援军
                        for (uint8 i = 0; i < summoned; ++i)
                        {
                            // 随机召唤3种卫兵之一
                            switch (urand(0, 2))
                            {
                                case 0:
                                    me->SummonCreature(NPC_HEARTHEN_GUARD, AddsEntrance[0], AddsEntrance[1], AddsEntrance[2], 0, TEMPSUMMON_TIMED_DESPAWN_OUT_OF_COMBAT, 30s);
                                    break;
                                case 1:
                                    me->SummonCreature(NPC_SHARPSHOOTER_GUARD, AddsEntrance[0], AddsEntrance[1], AddsEntrance[2], 0, TEMPSUMMON_TIMED_DESPAWN_OUT_OF_COMBAT, 30s);
                                    break;
                                case 2:
                                    me->SummonCreature(NPC_REAVER_GUARD, AddsEntrance[0], AddsEntrance[1], AddsEntrance[2], 0, TEMPSUMMON_TIMED_DESPAWN_OUT_OF_COMBAT, 30s);
                                    break;
                            }
                        }
                        // 20%概率增加下次召唤的数量
                        if (urand(0, 9) < 2)
                            ++summoned;
                        Summon_Assistant_Timer = urand(25000, 35000);  // 25-35秒后再次召唤
                    }
                    else
                        Summon_Assistant_Timer -= diff;

                    DoMeleeAttackIfReady();  // 执行近战攻击
                }

                // 位置检查(防止拉出房间)
                if (resetcheck_timer <= diff)
                {
                    uint32 tempx = uint32(me->GetPositionX());
                    // X坐标超出战斗区域(205-255),触发逃避
                    if (tempx > 255 || tempx < 205)
                    {
                        EnterEvadeMode();
                        return;
                    }
                    resetcheck_timer = 5000;  // 5秒后再检查
                }
                else
                    resetcheck_timer -= diff;
            }

            private:
                GuidVector adds;                    ///< 援军GUID列表
                GuidVector assassins;               ///< 刺客GUID列表
                uint32 Charge_timer;                ///< 英雄模式冲锋计时器
                uint32 Blade_Dance_Timer;           ///< 刀刃之舞计时器
                uint32 Summon_Assistant_Timer;      ///< 召唤援军计时器
                uint32 resetcheck_timer;            ///< 位置检查计时器
                uint32 Wait_Timer;                  ///< 刀刃之舞等待计时器
                uint32 Assassins_Timer;             ///< 刺客召唤计时器
                uint32 summoned;                    ///< 当前召唤援军数量
                uint32 target_num;                  ///< 刀刃之舞剩余跳转次数
                bool InBlade;                       ///< 是否在刀刃之舞状态
        };

        /**
         * @brief 获取AI实例
         * @param creature 生物指针
         * @return AI实例指针
         *
         * 工厂方法,创建并返回卡加斯AI实例
         *
         * @调用时机 脚本系统需要创建AI时
         */
        CreatureAI* GetAI(Creature* creature) const override
        {
            return GetShatteredHallsAI<boss_warchief_kargath_bladefistAI>(creature);
        }
};

/**
 * @brief 注册脚本函数
 *
 * 将卡加斯脚本注册到脚本系统
 *
 * @调用时机 服务器启动时,脚本系统初始化阶段
 */
void AddSC_boss_warchief_kargath_bladefist()
{
    new boss_warchief_kargath_bladefist();
}
