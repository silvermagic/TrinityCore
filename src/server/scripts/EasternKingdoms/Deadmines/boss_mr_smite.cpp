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
 * @file    boss_mr_smite.cpp
 * @brief   死亡矿坑副本Boss - 斯尼德的伐木机(Mr. Smite)的AI实现
 *
 * @details 该模块实现了死亡矿坑副本中的Boss斯尼德的伐木机的战斗逻辑。
 *          该Boss具有三阶段战斗机制，根据血量变化切换武器：
 *          - 第一阶段(100%-66%)：使用单手剑
 *          - 第二阶段(66%-33%)：切换为双斧
 *          - 第三阶段(33%-0%)：切换为双手锤
 *          Boss在每次阶段转换时会移动到武器箱更换武器，并在此期间停止攻击。
 *
 * @note    该脚本使用了来自ACID脚本的计时器和对话数据。
 *
 * ScriptData
 * SDName: Boss Mr.Smite
 * SD%Complete:
 * SDComment: Timers and say taken from acid script
 * EndScriptData
 */

#include "ScriptMgr.h"
#include "deadmines.h"
#include "GameObject.h"
#include "InstanceScript.h"
#include "MotionMaster.h"
#include "ObjectAccessor.h"
#include "ScriptedCreature.h"

/**
 * @brief 法术ID枚举定义
 */
enum Spells
{
    SPELL_TRASH             = 3391,   ///< 痛击技能 - 额外的近战攻击
    SPELL_SMITE_STOMP       = 6432,   ///< 斯尼德的践踏 - 阶段转换时的范围眩晕技能
    SPELL_SMITE_SLAM        = 6435    ///< 斯尼德的猛击 - 对当前目标的强力攻击
};

/**
 * @brief 武器装备ID枚举定义
 */
enum Equips
{
    EQUIP_SWORD             = 5191,   ///< 单手剑 - 第一阶段使用
    EQUIP_AXE               = 5196,   ///< 单手斧 - 第二阶段双持使用
    EQUIP_MACE              = 7230    ///< 双手锤 - 第三阶段使用
};

/**
 * @brief 对话文本ID枚举定义
 */
enum Texts
{
    SAY_PHASE_1             = 2,      ///< 第一阶段转换对话 - 66%血量时触发
    SAY_PHASE_2             = 3       ///< 第二阶段转换对话 - 33%血量时触发
};

/**
 * @class boss_mr_smite
 * @brief 斯尼德的伐木机Boss脚本类
 *
 * @details 该类负责注册和管理斯尼德的伐木机Boss的AI实例。
 *          它继承自CreatureScript，提供了获取AI实例的接口。
 */
class boss_mr_smite : public CreatureScript
{
public:
    /**
     * @brief 构造函数，注册脚本名称
     */
    boss_mr_smite() : CreatureScript("boss_mr_smite") { }

    /**
     * @brief 获取Boss的AI实例
     *
     * @param creature 生物对象指针
     * @return CreatureAI* 返回Boss的AI实例指针
     */
    CreatureAI* GetAI(Creature* creature) const override
    {
        return GetDeadminesAI<boss_mr_smiteAI>(creature);
    }

    /**
     * @struct boss_mr_smiteAI
     * @brief 斯尼德的伐木机Boss的AI实现结构体
     *
     * @details 实现了三阶段战斗机制：
     *          - 第一阶段：单手剑战斗
     *          - 第二阶段：双持斧头战斗
     *          - 第三阶段：双手锤战斗
     *          Boss在阶段转换时会移动到武器箱更换武器。
     */
    struct boss_mr_smiteAI : public ScriptedAI
    {
        /**
         * @brief 构造函数，初始化Boss AI
         *
         * @param creature 生物对象指针
         */
        boss_mr_smiteAI(Creature* creature) : ScriptedAI(creature)
        {
            Initialize();
            instance = creature->GetInstanceScript();
        }

        /**
         * @brief 初始化成员变量
         *
         * @details 重置所有计时器和状态标志为初始值
         */
        void Initialize()
        {
            uiTrashTimer = urand(5000, 9000);  ///< 痛击技能计时器，随机5-9秒
            uiSlamTimer = 9000;                 ///< 猛击技能计时器，初始9秒

            uiHealth = 0;                       ///< 当前阶段标志，0=初始，1=第一阶段转换，2=第二阶段转换

            uiPhase = 0;                        ///< 阶段转换流程阶段，0=正常战斗，1-4=转换流程
            uiTimer = 0;                        ///< 阶段转换计时器

            uiIsMoving = false;                 ///< 是否正在移动到武器箱
        }

        InstanceScript* instance;               ///< 副本实例脚本指针

        uint32 uiTrashTimer;                    ///< 痛击技能冷却计时器
        uint32 uiSlamTimer;                     ///< 猛击技能冷却计时器

        uint8 uiHealth;                         ///< Boss血量阶段标志(0, 1, 2)

        uint32 uiPhase;                         ///< 阶段转换流程控制变量
        uint32 uiTimer;                         ///< 阶段转换计时器

        bool uiIsMoving;                        ///< 是否正在移动标志

        /**
         * @brief 重置Boss状态
         *
         * @details 在Boss重置或脱离战斗时调用。
         *          重置所有计时器、装备、站立状态和反应状态。
         *          性能注意事项：装备设置会调用数据库查询，应避免频繁调用。
         */
        void Reset() override
        {
            Initialize();

            // 设置初始装备：单手剑，副手不装备
            SetEquipmentSlots(false, EQUIP_SWORD, EQUIP_UNEQUIP, EQUIP_NO_CHANGE);
            me->SetStandState(UNIT_STAND_STATE_STAND);      // 设置为站立状态
            me->SetReactState(REACT_AGGRESSIVE);            // 设置为主动攻击状态
            me->SetNoCallAssistance(true);                  // 禁止呼叫援助
        }

        /**
         * @brief 进入战斗回调
         *
         * @param who 仇恨目标
         *
         * @details Boss进入战斗时触发，当前未实现特殊逻辑。
         */
        void JustEngagedWith(Unit* /*who*/) override
        {
        }

        /**
         * @brief 检查技能施放概率
         *
         * @return bool true表示施放技能，false表示不施放
         *
         * @details 有15%的概率返回false，85%的概率返回true。
         *          用于控制技能施放的随机性。
         */
        bool bCheckChances()
        {
            uint32 uiChances = urand(0, 99);
            if (uiChances <= 15)
                return false;
            else
                return true;
        }

        /**
         * @brief 更新AI逻辑
         *
         * @param uiDiff 自上次更新以来的时间差（毫秒）
         *
         * @details 每帧调用，负责：
         *          1. 更新技能冷却并施放技能
         *          2. 检查血量触发阶段转换
         *          3. 执行阶段转换流程
         *          4. 执行近战攻击
         *
         * @note 性能注意事项：该函数每帧调用，应保持高效。
         */
        void UpdateAI(uint32 uiDiff) override
        {
            // 如果没有仇恨目标，则不执行任何操作
            if (!UpdateVictim())
                return;

            // 如果不在移动状态，则处理战斗技能
            if (!uiIsMoving) // 在阶段转换期间停止技能施放
            {
                // 痛击技能计时器检查
                if (uiTrashTimer <= uiDiff)
                {
                    if (bCheckChances())
                        DoCast(me, SPELL_TRASH);
                    uiTrashTimer = urand(6000, 15500);  // 重新设置随机冷却时间
                }
                else uiTrashTimer -= uiDiff;

                // 猛击技能计时器检查
                if (uiSlamTimer <= uiDiff)
                {
                    if (bCheckChances())
                        DoCastVictim(SPELL_SMITE_SLAM);
                    uiSlamTimer = 11000;  // 重置冷却时间为11秒
                }
                else uiSlamTimer -= uiDiff;

            }

            // 检查血量阈值，触发阶段转换
            // 第一阶段转换：血量低于66%
            // 第二阶段转换：血量低于33%
            if ((uiHealth == 0 && !HealthAbovePct(66)) || (uiHealth == 1 && !HealthAbovePct(33)))
            {
                ++uiHealth;
                // 施放践踏技能，造成范围眩晕
                DoCastAOE(SPELL_SMITE_STOMP, false);
                SetCombatMovement(false);                // 停止战斗移动
                me->AttackStop();                        // 停止攻击
                me->InterruptNonMeleeSpells(false);      // 中断非近战法术
                me->SetReactState(REACT_PASSIVE);        // 设置为被动状态
                uiTimer = 2500;                          // 设置阶段转换计时器
                uiPhase = 1;                             // 进入阶段转换流程

                // 根据血量阶段播放对应的对话
                switch (uiHealth)
                {
                    case 1:
                        Talk(SAY_PHASE_1);
                        break;
                    case 2:
                        Talk(SAY_PHASE_2);
                        break;
                }
            }

            // 阶段转换流程处理
            if (uiPhase)
            {
                if (uiTimer <= uiDiff)
                {
                    switch (uiPhase)
                    {
                        case 1:  // 移动到武器箱
                        {
                            if (uiIsMoving)
                                break;

                            // 获取武器箱的GameObject并移动到其位置
                            if (GameObject* go = ObjectAccessor::GetGameObject(*me, instance->GetGuidData(DATA_SMITE_CHEST)))
                            {
                                me->GetMotionMaster()->Clear();
                                me->GetMotionMaster()->MovePoint(1, go->GetPositionX() - 1.5f, go->GetPositionY() + 1.4f, go->GetPositionZ());
                                uiIsMoving = true;
                            }
                            break;
                        }
                        case 2:  // 更换武器
                            // 根据血量阶段选择不同的武器配置
                            if (uiHealth == 1)
                                SetEquipmentSlots(false, EQUIP_AXE, EQUIP_AXE, EQUIP_NO_CHANGE);  // 双持斧头
                            else
                                SetEquipmentSlots(false, EQUIP_MACE, EQUIP_UNEQUIP, EQUIP_NO_CHANGE);  // 双手锤
                            uiTimer = 500;
                            uiPhase = 3;
                            break;
                        case 3:  // 站立
                            me->SetStandState(UNIT_STAND_STATE_STAND);
                            uiTimer = 750;
                            uiPhase = 4;
                            break;
                        case 4:  // 恢复战斗状态
                            me->SetReactState(REACT_AGGRESSIVE);           // 恢复主动攻击状态
                            SetCombatMovement(true);                       // 恢复战斗移动
                            me->GetMotionMaster()->MoveChase(me->GetVictim(), me->m_CombatDistance);  // 追击目标
                            uiIsMoving = false;
                            uiPhase = 0;  // 重置阶段，恢复正常战斗
                            break;
                    }
                } else uiTimer -= uiDiff;
            }

            DoMeleeAttackIfReady();
        }

        /**
         * @brief 移动完成通知回调
         *
         * @param uiType 移动类型
         * @param uiId 移动点ID（未使用）
         *
         * @details 当Boss移动到武器箱位置后触发。
         *          设置Boss朝向武器箱并下跪，然后开始更换武器的流程。
         */
        void MovementInform(uint32 uiType, uint32 /*uiId*/) override
        {
            // 只处理点移动类型
            if (uiType != POINT_MOTION_TYPE)
                return;

            me->SetFacingTo(5.47f);                         // 设置朝向武器箱
            me->SetStandState(UNIT_STAND_STATE_KNEEL);      // 设置下跪状态（从武器箱中取武器）

            uiTimer = 2000;  // 设置2秒后进入武器更换阶段
            uiPhase = 2;     // 进入武器更换流程
        }
    };
};

/**
 * @brief 注册斯尼德的伐木机Boss脚本
 *
 * @details 该函数在服务器启动时被调用，用于注册Boss脚本实例。
 *          这是TrinityCore脚本系统的标准入口点。
 */
void AddSC_boss_mr_smite()
{
    new boss_mr_smite();
}
