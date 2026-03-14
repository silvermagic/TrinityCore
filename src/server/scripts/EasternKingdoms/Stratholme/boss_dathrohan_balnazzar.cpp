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

/* ScriptData
SDName: Boss_Dathrohan_Balnazzar
SD%Complete: 95
SDComment: Possibly need to fix/improve summons after death
SDCategory: Stratholme
EndScriptData */

/**
 * @file    boss_dathrohan_balnazzar.cpp
 * @brief   斯坦索姆副本 - 达索汉/巴纳扎尔Boss战斗AI脚本
 *
 * @details 本模块实现了斯坦索姆副本Boss达索汉的战斗逻辑,该Boss在血量低于40%时会
 *          显露真身变为恐惧魔王巴纳扎尔:
 *          - 达索汉形态: 使用圣骑士技能(十字军打击、圣光打击等)
 *          - 巴纳扎尔形态: 使用术士/牧师技能(暗影震击、精神控制、心灵尖啸等)
 *          - 死亡时召唤僵尸援军
 *          - 剧情背景: 巴纳扎尔伪装成十字军领袖达索汉渗透白银之手
 */

#include "ScriptMgr.h"
#include "ScriptedCreature.h"
#include "stratholme.h"

/**
 * @brief 法术ID枚举
 * 定义达索汉和巴纳扎尔使用的所有法术技能ID
 */
enum Spells
{
    // 达索汉法术 - 圣骑士技能
    SPELL_CRUSADERSHAMMER           = 17286,  // 十字军之锤 - 范围眩晕效果
    SPELL_CRUSADERSTRIKE            = 17281,  // 十字军打击 - 物理伤害技能
    SPELL_HOLYSTRIKE                = 17284,  // 圣光打击 - 武器伤害+3

    // 变身法术
    SPELL_BALNAZZARTRANSFORM        = 17288,  // 巴纳扎尔变身 - 恢复满HP/法力并触发眩晕

    // 巴纳扎尔法术 - 恐惧魔王技能
    SPELL_SHADOWSHOCK               = 17399,  // 暗影震击 - 暗影伤害法术
    SPELL_MINDBLAST                 = 17287,  // 精神冲击 - 暗影伤害法术
    SPELL_PSYCHICSCREAM             = 13704,  // 心灵尖啸 - 范围恐惧效果
    SPELL_SLEEP                     = 12098,  // 深度睡眠 - 使目标沉睡
    SPELL_MINDCONTROL               = 15690   // 精神控制 - 控制目标
};

/**
 * @brief 生物ID枚举
 * 定义相关生物的NPC ID
 */
enum Creatures
{
    NPC_DATHROHAN                   = 10812,  // 达索汉 NPC ID
    NPC_BALNAZZAR                   = 10813,  // 巴纳扎尔 NPC ID
    NPC_ZOMBIE                      = 10698   // 僵尸 NPC ID (可能不正确)
};

/**
 * @struct SummonDef
 * @brief 召唤位置定义结构体
 *
 * @details 用于定义Boss死亡时召唤僵尸的位置和朝向
 */
struct SummonDef
{
    float m_fX, m_fY, m_fZ, m_fOrient;  // X坐标, Y坐标, Z坐标, 朝向角度
};

/**
 * @brief 召唤点位置数组
 *
 * @details 定义了8个僵尸召唤位置,分为两组:
 *          - 第1组(前4个): 在房间一侧
 *          - 第2组(后4个): 在房间另一侧
 */
SummonDef m_aSummonPoint[]=
{
    {3444.156f, -3090.626f, 135.002f, 2.240f},  // G1 前左
    {3449.123f, -3087.009f, 135.002f, 2.240f},  // G1 前右
    {3446.246f, -3093.466f, 135.002f, 2.240f},  // G1 后左
    {3451.160f, -3089.904f, 135.002f, 2.240f},  // G1 后右

    {3457.995f, -3080.916f, 135.002f, 3.784f},  // G2 前左
    {3454.302f, -3076.330f, 135.002f, 3.784f},  // G2 前右
    {3460.975f, -3078.901f, 135.002f, 3.784f},  // G2 后左
    {3457.338f, -3073.979f, 135.002f, 3.784f}   // G2 后右
};

/**
 * @class boss_dathrohan_balnazzar
 * @brief 达索汉/巴纳扎尔Boss脚本类
 *
 * @details 实现双形态Boss的脚本注册和AI创建
 */
class boss_dathrohan_balnazzar : public CreatureScript
{
public:
    boss_dathrohan_balnazzar() : CreatureScript("boss_dathrohan_balnazzar") { }

    /**
     * @brief 获取AI实例
     * @param creature 生物对象指针
     * @return 创建的AI实例指针
     */
    CreatureAI* GetAI(Creature* creature) const override
    {
        return GetStratholmeAI<boss_dathrohan_balnazzarAI>(creature);
    }

    /**
     * @struct boss_dathrohan_balnazzarAI
     * @brief 达索汉/巴纳扎尔Boss AI
     *
     * @details 实现双形态Boss的战斗逻辑:
     *          - 达索汉形态: 使用圣骑士技能进行战斗
     *          - 血量低于40%时变身为巴纳扎尔
     *          - 巴纳扎尔形态: 使用术士/牧师技能
     *          - 死亡时召唤8个僵尸援军
     */
    struct boss_dathrohan_balnazzarAI : public ScriptedAI
    {
        /**
         * @brief 构造函数
         * @param creature 生物对象指针
         */
        boss_dathrohan_balnazzarAI(Creature* creature) : ScriptedAI(creature)
        {
            Initialize();
        }

        /**
         * @brief 初始化成员变量
         *
         * @details 设置所有技能计时器和状态标志的初始值:
         *          - 达索汉技能计时器
         *          - 巴纳扎尔技能计时器
         *          - 变身状态标志
         */
        void Initialize()
        {
            m_uiCrusadersHammer_Timer = 8000;   // 十字军之锤计时器
            m_uiCrusaderStrike_Timer = 12000;   // 十字军打击计时器
            m_uiMindBlast_Timer = 6000;         // 精神冲击计时器
            m_uiHolyStrike_Timer = 18000;       // 圣光打击计时器
            m_uiShadowShock_Timer = 4000;       // 暗影震击计时器
            m_uiPsychicScream_Timer = 16000;    // 心灵尖啸计时器
            m_uiDeepSleep_Timer = 20000;        // 深度睡眠计时器
            m_uiMindControl_Timer = 10000;      // 精神控制计时器
            m_bTransformed = false;             // 是否已变身标志
        }

        // 达索汉阶段技能计时器
        uint32 m_uiCrusadersHammer_Timer;  // 十字军之锤冷却计时器
        uint32 m_uiCrusaderStrike_Timer;   // 十字军打击冷却计时器
        uint32 m_uiHolyStrike_Timer;       // 圣光打击冷却计时器

        // 巴纳扎尔阶段技能计时器
        uint32 m_uiMindBlast_Timer;        // 精神冲击冷却计时器(两阶段共用)
        uint32 m_uiShadowShock_Timer;      // 暗影震击冷却计时器
        uint32 m_uiPsychicScream_Timer;    // 心灵尖啸冷却计时器
        uint32 m_uiDeepSleep_Timer;        // 深度睡眠冷却计时器
        uint32 m_uiMindControl_Timer;      // 精神控制冷却计时器

        // 状态标志
        bool m_bTransformed;               // 是否已完成变身

        /**
         * @brief 重置Boss状态
         *
         * @details 在战斗结束或重置时调用:
         *          - 重新初始化所有计时器和状态
         *          - 如果当前是巴纳扎尔形态,恢复为达索汉
         */
        void Reset() override
        {
            Initialize();

            // 如果当前是巴纳扎尔形态,恢复为达索汉
            if (me->GetEntry() == NPC_BALNAZZAR)
                me->UpdateEntry(NPC_DATHROHAN);
        }

        /**
         * @brief Boss死亡处理
         * @param killer 击杀者(可为nullptr)
         *
         * @details Boss死亡时调用:
         *          - 在预设的8个召唤点召唤僵尸
         *          - 僵尸会在1小时后消失
         */
        void JustDied(Unit* /*killer*/) override
        {
            // 计算召唤点数量
            static uint32 uiCount = sizeof(m_aSummonPoint)/sizeof(SummonDef);

            // 在所有召唤点召唤僵尸
            for (uint8 i=0; i<uiCount; ++i)
                me->SummonCreature(NPC_ZOMBIE,
                m_aSummonPoint[i].m_fX, m_aSummonPoint[i].m_fY, m_aSummonPoint[i].m_fZ, m_aSummonPoint[i].m_fOrient,
                TEMPSUMMON_TIMED_DESPAWN, 1h);
        }

        /**
         * @brief 进入战斗
         * @param who 进入战斗的目标
         *
         * @details 进入战斗时调用(目前无特殊逻辑)
         */
        void JustEngagedWith(Unit* /*who*/) override
        {
        }

        /**
         * @brief 更新AI
         * @param uiDiff 距离上次更新的时间差(毫秒)
         *
         * @details 主循环逻辑,每帧调用:
         *          1. 检查是否有有效目标
         *          2. 根据变身状态执行不同的技能循环:
         *             - 未变身(达索汉): 使用圣骑士技能,检查血量触发变身
         *             - 已变身(巴纳扎尔): 使用术士/牧师技能
         *          3. 执行近战攻击
         *
         * @par 达索汉阶段技能:
         *          - 精神冲击: 6秒首次,之后15-20秒间隔
         *          - 十字军之锤: 8秒首次,之后12秒间隔
         *          - 十字军打击: 12秒首次,之后15秒间隔
         *          - 圣光打击: 18秒首次,之后15秒间隔
         *          - 变身: 血量低于40%时触发
         *
         * @par 巴纳扎尔阶段技能:
         *          - 精神冲击: 继续使用,15-20秒间隔
         *          - 暗影震击: 4秒首次,之后11秒间隔
         *          - 心灵尖啸: 16秒首次,之后20秒间隔
         *          - 深度睡眠: 20秒首次,之后15秒间隔
         *          - 精神控制: 10秒首次,之后15秒间隔
         */
        void UpdateAI(uint32 uiDiff) override
        {
            // 检查是否有有效目标
            if (!UpdateVictim())
                return;

            // 未变身阶段 - 达索汉形态
            if (!m_bTransformed)
            {
                // 精神冲击 (两个形态都使用)
                if (m_uiMindBlast_Timer <= uiDiff)
                {
                    DoCastVictim(SPELL_MINDBLAST);
                    m_uiMindBlast_Timer = urand(15000, 20000);
                } else m_uiMindBlast_Timer -= uiDiff;

                // 十字军之锤
                if (m_uiCrusadersHammer_Timer <= uiDiff)
                {
                    DoCastVictim(SPELL_CRUSADERSHAMMER);
                    m_uiCrusadersHammer_Timer = 12000;
                } else m_uiCrusadersHammer_Timer -= uiDiff;

                // 十字军打击
                if (m_uiCrusaderStrike_Timer <= uiDiff)
                {
                    DoCastVictim(SPELL_CRUSADERSTRIKE);
                    m_uiCrusaderStrike_Timer = 15000;
                } else m_uiCrusaderStrike_Timer -= uiDiff;

                // 圣光打击
                if (m_uiHolyStrike_Timer <= uiDiff)
                {
                    DoCastVictim(SPELL_HOLYSTRIKE);
                    m_uiHolyStrike_Timer = 15000;
                } else m_uiHolyStrike_Timer -= uiDiff;

                // 检查是否满足变身条件(血量低于40%)
                if (HealthBelowPct(40))
                {
                    // 中断当前施法
                    if (me->IsNonMeleeSpellCast(false))
                        me->InterruptNonMeleeSpells(false);

                    // 变身为巴纳扎尔: 恢复满HP/法力并触发眩晕
                    DoCast(me, SPELL_BALNAZZARTRANSFORM);
                    me->UpdateEntry(NPC_BALNAZZAR);
                    m_bTransformed = true;
                }
            }
            // 已变身阶段 - 巴纳扎尔形态
            else
            {
                // 精神冲击 (继续使用)
                if (m_uiMindBlast_Timer <= uiDiff)
                {
                    DoCastVictim(SPELL_MINDBLAST);
                    m_uiMindBlast_Timer = urand(15000, 20000);
                } else m_uiMindBlast_Timer -= uiDiff;

                // 暗影震击
                if (m_uiShadowShock_Timer <= uiDiff)
                {
                    DoCastVictim(SPELL_SHADOWSHOCK);
                    m_uiShadowShock_Timer = 11000;
                } else m_uiShadowShock_Timer -= uiDiff;

                // 心灵尖啸
                if (m_uiPsychicScream_Timer <= uiDiff)
                {
                    if (Unit* target = SelectTarget(SelectTargetMethod::Random, 0))
                        DoCast(target, SPELL_PSYCHICSCREAM);

                    m_uiPsychicScream_Timer = 20000;
                } else m_uiPsychicScream_Timer -= uiDiff;

                // 深度睡眠
                if (m_uiDeepSleep_Timer <= uiDiff)
                {
                    if (Unit* target = SelectTarget(SelectTargetMethod::Random, 0))
                        DoCast(target, SPELL_SLEEP);

                    m_uiDeepSleep_Timer = 15000;
                } else m_uiDeepSleep_Timer -= uiDiff;

                // 精神控制
                if (m_uiMindControl_Timer <= uiDiff)
                {
                    DoCastVictim(SPELL_MINDCONTROL);
                    m_uiMindControl_Timer = 15000;
                } else m_uiMindControl_Timer -= uiDiff;
            }

            // 执行近战攻击
            DoMeleeAttackIfReady();
        }
    };

};

/**
 * @brief 注册脚本
 *
 * @details 将达索汉/巴纳扎尔Boss脚本注册到脚本系统
 */
void AddSC_boss_dathrohan_balnazzar()
{
    new boss_dathrohan_balnazzar();
}
