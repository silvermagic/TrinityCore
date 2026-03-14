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
 * @file boss_cthun.cpp
 * @brief 克苏恩首领AI脚本
 *
 * 本模块实现了安其拉神殿最终首领克苏恩的AI逻辑，包括：
 * - 两阶段战斗流程（眼球阶段、克苏恩本体阶段）
 * - 眼球阶段：绿色光束（随机目标）和红色光束（旋转扫射）
 * - 本体阶段：吞噬玩家、触手召唤、虚弱状态
 * - 多种触手AI：眼球触手、利爪触手、巨眼触手、巨爪触手、血肉触手
 *
 * 已知问题：
 * - Darkglare（黑暗凝视）追踪问题
 *
 * 战斗阶段详解：
 * 第一阶段（EYE）：
 *   - PHASE_EYE_GREEN_BEAM：50秒，每3秒施放绿色光束
 *   - PHASE_EYE_RED_BEAM：35秒，每秒旋转施放红色光束
 *
 * 第二阶段（CTHUN）：
 *   - PHASE_CTHUN_TRANSITION：转换阶段，10秒
 *   - PHASE_CTHUN_STOMACH：胃部阶段，召唤血肉触手，吞噬玩家
 *   - PHASE_CTHUN_WEAK：虚弱阶段，45秒，正常承受伤害
 *
 * @author TrinityCore Team
 * @date 2024
 */

/* ScriptData
SDName: Boss_Cthun
SD%Complete: 95
SDComment: Darkglare tracking issue
SDCategory: Temple of Ahn'Qiraj
EndScriptData */

#include "ScriptMgr.h"
#include "InstanceScript.h"
#include "Map.h"
#include "ObjectAccessor.h"
#include "Player.h"
#include "ScriptedCreature.h"
#include "temple_of_ahnqiraj.h"
#include "TemporarySummon.h"

/*
 * This is a 2 phases events. Here follows an explanation of the main events and transition between phases and sub-phases.
 *
 * The first phase is the EYE phase: the Eye of C'Thun is active and C'thun is not active.
 *     During this phase, the "Eye of C'Thun" alternates between 2 sub-phases:
 *         - PHASE_EYE_GREEN_BEAM:
 *             50 sec phase during which the Eye mainly casts its Green Beam every 3 sec.
 *         - PHASE_EYE_RED_BEAM:
 *             35 sec phase during which the Eye casts its red beam every sec.
 *     This EYE phase ends when the "Eye of C'Thun" is killed. Then starts the CTHUN phase.
 *
 * The second phase is the CTHUN phase. The Eye of C'Thun is not active and C'Thun is active.
 *     This phase starts with the transformation of the Eye into C'Thun (PHASE_CTHUN_TRANSITION).
 *     After the transformation, C'Thun alternates between 2 sub-phases:
 *         - PHASE_CTHUN_STOMACH:
 *             - C'Thun is almost insensible to all damage (99% damage reduction).
 *             - It spawns 2 tentacles in its stomach.
 *             - C'Thun swallows players.
 *             - This sub-phase ends when the 2 tentacles are killed. Swallowed players are regurgitate.
 *
 *         - PHASE_CTHUN_WEAK:
 *             - weakened C'Thun takes normal damage.
 *             - This sub-phase ends after 45 secs.
 *
 *     This CTHUN phase ends when C'Thun is killed
 *
 * Note:
 * - the current phase is stored in the instance data to be easily shared between the eye and cthun.
 */

/**
 * @brief 克苏恩战斗阶段枚举
 *
 * 定义了克苏恩战斗的所有阶段状态
 */
enum Phases
{
    PHASE_NOT_STARTED                           = 0,  // 战斗未开始

    // Main Phase 1 - EYE - 第一阶段：眼球阶段
    PHASE_EYE_GREEN_BEAM                        = 1,  // 绿色光束阶段（50秒）
    PHASE_EYE_RED_BEAM                          = 2,  // 红色光束阶段（35秒）

    // Main Phase 2 - CTHUN - 第二阶段：克苏恩本体阶段
    PHASE_CTHUN_TRANSITION                      = 3,  // 转换阶段（10秒）
    PHASE_CTHUN_STOMACH                         = 4,  // 胃部阶段（召唤触手，吞噬玩家）
    PHASE_CTHUN_WEAK                            = 5,  // 虚弱阶段（45秒，正常受伤）

    PHASE_CTHUN_DONE                            = 6,  // 战斗结束
};

/**
 * @brief 克苏恩使用的法术ID枚举
 */
enum Spells
{
    // ***** Main Phase 1 ******** - 第一阶段法术
    //Eye Spells - 眼球法术
    SPELL_FREEZE_ANIM                           = 16245,  // 冻结动画，用于红光阶段
    SPELL_GREEN_BEAM                            = 26134,  // 绿色光束，随机目标伤害
    SPELL_DARK_GLARE                            = 26029,  // 黑暗凝视，红光扫射
    SPELL_RED_COLORATION                        = 22518,  // 红色着色效果（可能不是正确的法术，但视觉效果相似）

    //Eye Tentacles Spells - 眼球触手法术
    SPELL_MIND_FLAY                             = 26143,  // 精神鞭笞，持续伤害

    //Claw Tentacles Spells - 利爪触手法术
    SPELL_GROUND_RUPTURE                        = 26139,  // 地面破裂，范围伤害
    SPELL_HAMSTRING                             = 26141,  // 断筋，减速效果

    // ***** Main Phase 2 ****** - 第二阶段法术
    //Body spells - 本体法术
    //SPELL_CARAPACE_CTHUN                        = 26156   // 已从客户端DBC中移除
    SPELL_TRANSFORM                             = 26232,  // 转换法术，眼球变成本体
    SPELL_PURPLE_COLORATION                     = 22581,  // 紫色着色效果（可能不是正确的法术，但视觉效果相似）

    //Eye Tentacles Spells - 眼球触手法术
    //SAME AS PHASE1 - 与第一阶段相同

    //Giant Claw Tentacles - 巨爪触手法术
    SPELL_MASSIVE_GROUND_RUPTURE                = 26100,  // 大型地面破裂

    //Also casts Hamstring - 同时施放断筋
    SPELL_THRASH                                = 3391,   // 痛击，额外攻击

    //Giant Eye Tentacles - 巨眼触手法术
    //CHAIN CASTS "SPELL_GREEN_BEAM" - 连续施放绿色光束

    //Stomach Spells - 胃部法术
    SPELL_MOUTH_TENTACLE                        = 26332,  // 口腔触手，吞噬玩家的视觉效果
    SPELL_EXIT_STOMACH_KNOCKBACK                = 25383,  // 离开胃部击退
    SPELL_DIGESTIVE_ACID                        = 26476,  // 消化酸，胃部持续伤害
};

/**
 * @brief 动作类型枚举
 */
enum Actions
{
    ACTION_FLESH_TENTACLE_KILLED                = 1,  // 血肉触手被击杀
};

/**
 * @brief 克苏恩喊话和表情枚举
 */
enum Yells
{
    //Text emote - 文本表情
    EMOTE_WEAKENED                              = 0,  // 虚弱表情

    // ****** Out of Combat ****** - 非战斗状态
    // Random Wispers - No txt only sound - 随机耳语（无文本，仅声音）
    // The random sound is chosen by the client. - 随机声音由客户端选择
    RANDOM_SOUND_WHISPER                        = 8663,  // 随机耳语音效
};

//Stomach Teleport positions - 胃部传送坐标
#define STOMACH_X                           -8562.0f  // 胃部X坐标
#define STOMACH_Y                           2037.0f   // 胃部Y坐标
#define STOMACH_Z                           -70.0f    // 胃部Z坐标
#define STOMACH_O                           5.05f     // 胃部朝向

//Flesh tentacle positions - 血肉触手位置
const Position FleshTentaclePos[2] =
{
    { -8571.0f, 1990.0f, -98.0f, 1.22f},  // 第一个血肉触手位置
    { -8525.0f, 1994.0f, -98.0f, 2.12f},  // 第二个血肉触手位置
};

//Kick out position - 踢出位置（玩家从胃部出来时的位置）
const Position KickPos = { -8545.0f, 1984.0f, -96.0f, 0.0f};

/**
 * @brief 克苏恩眼球AI脚本类
 *
 * 实现第一阶段的眼球AI，包括：
 * - 绿色光束阶段（50秒）：每3秒随机目标伤害
 * - 红色光束阶段（35秒）：旋转扫射
 * - 触手召唤
 */
class boss_eye_of_cthun : public CreatureScript
{
public:
    /**
     * @brief 构造函数
     */
    boss_eye_of_cthun() : CreatureScript("boss_eye_of_cthun") { }

    /**
     * @brief 获取AI实例
     * @param creature 生物对象指针
     * @return AI实例指针
     */
    CreatureAI* GetAI(Creature* creature) const override
    {
        return GetAQ40AI<eye_of_cthunAI>(creature);
    }

    /**
     * @brief 克苏恩眼球AI结构体
     */
    struct eye_of_cthunAI : public ScriptedAI
    {
        /**
         * @brief 构造函数
         * @param creature 生物对象指针
         */
        eye_of_cthunAI(Creature* creature) : ScriptedAI(creature)
        {
            Initialize();
            instance = creature->GetInstanceScript();

            SetCombatMovement(false);  // 眼球不移动
        }

        /**
         * @brief 初始化成员变量
         *
         * 重置所有计时器和状态变量到初始值
         */
        void Initialize()
        {
            //Phase information - 阶段信息
            PhaseTimer = 50000;                                 //First dark glare in 50 seconds - 第一次黑暗凝视在50秒后

            //Eye beam phase 50 seconds - 眼球光束阶段（50秒）
            BeamTimer = 3000;                                   // 绿色光束每3秒施放
            EyeTentacleTimer = 45000;                           //Always spawns 5 seconds before Dark Beam - 总是在黑暗光束前5秒生成
            ClawTentacleTimer = 12500;                          //4 per Eye beam phase (unsure if they spawn during Dark beam) - 每个眼球阶段4个（不确定是否在黑暗光束期间生成）

            //Dark Beam phase 35 seconds (each tick = 1 second, 35 ticks) - 黑暗光束阶段（35秒，每tick=1秒，35个tick）
            DarkGlareTick = 0;                                  // 当前tick计数
            DarkGlareTickTimer = 1000;                          // tick计时器
            DarkGlareAngle = 0;                                 // 黑暗光束角度
            ClockWise = false;                                  // 是否顺时针旋转
        }

        InstanceScript* instance;  // 实例脚本指针

        //Global variables - 全局变量
        uint32 PhaseTimer;  // 阶段计时器

        //Eye beam phase - 眼球光束阶段
        uint32 BeamTimer;          // 绿色光束计时器
        uint32 EyeTentacleTimer;   // 眼球触手召唤计时器
        uint32 ClawTentacleTimer;  // 利爪触手召唤计时器

        //Dark Glare phase - 黑暗凝视阶段
        uint32 DarkGlareTick;      // 黑暗凝视tick计数
        uint32 DarkGlareTickTimer; // 黑暗凝视tick计时器
        float DarkGlareAngle;      // 黑暗凝视角度
        bool ClockWise;            // 旋转方向（顺时针/逆时针）

        /**
         * @brief 重置AI状态
         *
         * 当眼球脱离战斗时调用，恢复所有状态到初始值
         */
        void Reset() override
        {
            Initialize();

            //Reset flags - 重置标志
            me->RemoveAurasDueToSpell(SPELL_RED_COLORATION);  // 移除红色着色
            me->RemoveAurasDueToSpell(SPELL_FREEZE_ANIM);     // 移除冻结动画
            me->RemoveUnitFlag(UNIT_FLAG_UNINTERACTIBLE | UNIT_FLAG_NON_ATTACKABLE);  // 移除不可交互和不可攻击标志
            me->SetVisible(true);  // 设置可见

            //Reset Phase - 重置阶段
            instance->SetData(DATA_CTHUN_PHASE, PHASE_NOT_STARTED);

            //to avoid having a following void zone - 避免虚空区域跟随
            Creature* pPortal= me->FindNearestCreature(NPC_CTHUN_PORTAL, 10);
            if (pPortal)
                pPortal->SetReactState(REACT_PASSIVE);  // 设置传送门为被动状态
        }

        /**
         * @brief 进入战斗回调
         * @param who 进入战斗的目标（未使用）
         *
         * 让眼球进入战斗并设置阶段为绿色光束阶段
         */
        void JustEngagedWith(Unit* /*who*/) override
        {
            DoZoneInCombat();  // 让所有在战斗区域的敌人进入战斗
            instance->SetData(DATA_CTHUN_PHASE, PHASE_EYE_GREEN_BEAM);  // 设置阶段为绿色光束
        }

        /**
         * @brief 生成眼球触手
         * @param x 相对X坐标偏移
         * @param y 相对Y坐标偏移
         *
         * 在指定位置召唤眼球触手并让其攻击随机目标
         */
        void SpawnEyeTentacle(float x, float y)
        {
            if (Creature* Spawned = DoSpawnCreature(NPC_EYE_TENTACLE, x, y, 0, 0, TEMPSUMMON_CORPSE_DESPAWN, 500ms))
                if (Unit* target = SelectTarget(SelectTargetMethod::Random, 0))
                    if (Spawned->AI())
                        Spawned->AI()->AttackStart(target);
        }

        /**
         * @brief 更新AI主循环
         * @param diff 距离上次更新的时间差（毫秒）
         *
         * 眼球AI核心逻辑，处理两个主要阶段：
         * - 绿色光束阶段：每3秒对随机目标施放绿色光束
         * - 红色光束阶段：旋转扫射，每秒转动一定角度
         *
         * 同时处理触手召唤和阶段转换
         */
        void UpdateAI(uint32 diff) override
        {
            //Check if we have a target - 检查是否有目标
            if (!UpdateVictim())
                return;

            uint32 currentPhase = instance->GetData(DATA_CTHUN_PHASE);
            // 在绿色和红色光束阶段都会召唤眼球触手
            if (currentPhase == PHASE_EYE_GREEN_BEAM || currentPhase == PHASE_EYE_RED_BEAM)
            {
                // EyeTentacleTimer - 眼球触手计时器
                if (EyeTentacleTimer <= diff)
                {
                    //Spawn the 8 Eye Tentacles in the corret spots - 在8个位置生成眼球触手
                    SpawnEyeTentacle(0, 20);                //south - 南
                    SpawnEyeTentacle(10, 10);               //south west - 西南
                    SpawnEyeTentacle(20, 0);                //west - 西
                    SpawnEyeTentacle(10, -10);              //north west - 西北

                    SpawnEyeTentacle(0, -20);               //north - 北
                    SpawnEyeTentacle(-10, -10);             //north east - 东北
                    SpawnEyeTentacle(-20, 0);               // east - 东
                    SpawnEyeTentacle(-10, 10);              // south east - 东南

                    EyeTentacleTimer = 45000;  // 45秒后再次召唤
                } else EyeTentacleTimer -= diff;
            }

            switch (currentPhase)
            {
                case PHASE_EYE_GREEN_BEAM:  // 绿色光束阶段
                    //BeamTimer - 绿色光束计时器
                    if (BeamTimer <= diff)
                    {
                        //SPELL_GREEN_BEAM - 施放绿色光束
                        if (Unit* target = SelectTarget(SelectTargetMethod::Random, 0))
                        {
                            me->InterruptNonMeleeSpells(false);  // 中断非近战法术
                            DoCast(target, SPELL_GREEN_BEAM);    // 施放绿色光束

                            //Correctly update our target - 更新目标
                            me->SetTarget(target->GetGUID());
                        }

                        //Beam every 3 seconds - 每3秒施放一次
                        BeamTimer = 3000;
                    } else BeamTimer -= diff;

                    //ClawTentacleTimer - 利爪触手计时器
                    if (ClawTentacleTimer <= diff)
                    {
                        if (Unit* target = SelectTarget(SelectTargetMethod::Random, 0))
                        {
                            Creature* Spawned = nullptr;

                            //Spawn claw tentacle on the random target - 在随机目标位置生成利爪触手
                            Spawned = me->SummonCreature(NPC_CLAW_TENTACLE, *target, TEMPSUMMON_CORPSE_DESPAWN, 500ms);

                            if (Spawned && Spawned->AI())
                                Spawned->AI()->AttackStart(target);
                        }

                        //One claw tentacle every 12.5 seconds - 每12.5秒生成一个
                        ClawTentacleTimer = 12500;
                    } else ClawTentacleTimer -= diff;

                    //PhaseTimer - 阶段计时器，控制从绿光到红光的转换
                    if (PhaseTimer <= diff)
                    {
                        //Switch to Dark Beam - 切换到黑暗光束阶段
                        instance->SetData(DATA_CTHUN_PHASE, PHASE_EYE_RED_BEAM);

                        me->InterruptNonMeleeSpells(false);
                        me->SetReactState(REACT_PASSIVE);  // 设置为被动反应

                        //Remove any target - 移除任何目标
                        me->SetTarget(ObjectGuid::Empty);

                        //Select random target for dark beam to start on - 选择随机目标作为黑暗光束的起始方向
                        if (Unit* target = SelectTarget(SelectTargetMethod::Random, 0))
                        {
                            //Face our target - 面向目标
                            DarkGlareAngle = me->GetAbsoluteAngle(target);
                            DarkGlareTickTimer = 1000;
                            DarkGlareTick = 0;
                            ClockWise = RAND(true, false);  // 随机选择顺时针或逆时针
                        }

                        //Add red coloration to C'thun - 给克苏恩添加红色着色
                        DoCast(me, SPELL_RED_COLORATION, true);

                        //Freeze animation - 冻结动画
                        DoCast(me, SPELL_FREEZE_ANIM);
                        me->SetOrientation(DarkGlareAngle);
                        me->StopMoving();

                        //Darkbeam for 35 seconds - 黑暗光束持续35秒
                        PhaseTimer = 35000;
                    } else PhaseTimer -= diff;

                    break;

                case PHASE_EYE_RED_BEAM:  // 红色光束阶段
                    if (DarkGlareTick < 35)  // 35个tick
                    {
                        if (DarkGlareTickTimer <= diff)
                        {
                            //Set angle and cast - 设置角度并施放法术
                            // 根据旋转方向计算当前角度（每次转动 PI/35 弧度）
                            float angle = ClockWise ? DarkGlareAngle + DarkGlareTick * float(M_PI) / 35 : DarkGlareAngle - DarkGlareTick * float(M_PI) / 35;

                            me->SetOrientation(angle);
                            me->SetFacingTo(angle);

                            me->StopMoving();

                            //Actual dark glare cast, maybe something missing here? - 实际的黑暗凝视施放
                            DoCast(me, SPELL_DARK_GLARE, false);

                            //Increase tick - 增加tick计数
                            ++DarkGlareTick;

                            //1 second per tick - 每tick 1秒
                            DarkGlareTickTimer = 1000;
                        } else DarkGlareTickTimer -= diff;
                    }

                    //PhaseTimer - 阶段计时器，控制从红光到绿光的转换
                    if (PhaseTimer <= diff)
                    {
                        //Switch to Eye Beam - 切换回眼球光束阶段
                        instance->SetData(DATA_CTHUN_PHASE, PHASE_EYE_GREEN_BEAM);

                        BeamTimer = 3000;
                        ClawTentacleTimer = 12500;              //4 per Eye beam phase (unsure if they spawn during Dark beam)

                        me->InterruptNonMeleeSpells(false);

                        //Remove Red coloration from c'thun - 移除红色着色
                        me->RemoveAurasDueToSpell(SPELL_RED_COLORATION);
                        me->RemoveAurasDueToSpell(SPELL_FREEZE_ANIM);

                        //set it back to aggressive - 设置回主动攻击状态
                        me->SetReactState(REACT_AGGRESSIVE);

                        //Eye Beam for 50 seconds - 眼球光束持续50秒
                        PhaseTimer = 50000;
                    } else PhaseTimer -= diff;

                    break;

                //Transition phase - 转换阶段
                case PHASE_CTHUN_TRANSITION:
                    //Remove any target - 移除目标
                    me->SetTarget(ObjectGuid::Empty);
                    me->SetHealth(0);
                    me->SetVisible(false);  // 隐藏眼球
                    break;

                //Dead phase - 死亡阶段
                case PHASE_CTHUN_DONE:
                    // 移除传送门
                    Creature* pPortal= me->FindNearestCreature(NPC_CTHUN_PORTAL, 10);
                    if (pPortal)
                        pPortal->DespawnOrUnsummon();

                    me->DespawnOrUnsummon();  // 移除眼球
                    break;
            }
        }

        /**
         * @brief 受伤回调
         * @param done_by 伤害来源（未使用）
         * @param damage 伤害值（引用，可修改）
         * @param damageType 伤害类型（未使用）
         * @param spellInfo 法术信息（未使用）
         *
         * 处理眼球受到伤害时的逻辑：
         * - 在绿色或红色光束阶段，如果伤害足以杀死眼球，则假死并转换阶段
         * - 在其他阶段阻止死亡
         */
        void DamageTaken(Unit* /*done_by*/, uint32& damage, DamageEffectType /*damageType*/, SpellInfo const* /*spellInfo = nullptr*/) override
        {
            switch (instance->GetData(DATA_CTHUN_PHASE))
            {
                case PHASE_EYE_GREEN_BEAM:
                case PHASE_EYE_RED_BEAM:
                    //Only if it will kill - 只有伤害足以击杀时才处理
                    if (damage < me->GetHealth())
                        return;

                    //Fake death in phase 0 or 1 (green beam or dark glare phase) - 在绿色光束或黑暗光束阶段假死
                    me->InterruptNonMeleeSpells(false);

                    //Remove Red coloration from c'thun - 移除红色着色
                    me->RemoveAurasDueToSpell(SPELL_RED_COLORATION);

                    //Reset to normal emote state and prevent select and attack - 重置为正常状态并阻止选择和攻击
                    me->SetUnitFlag(UNIT_FLAG_UNINTERACTIBLE | UNIT_FLAG_NON_ATTACKABLE);

                    //Remove Target field - 移除目标字段
                    me->SetTarget(ObjectGuid::Empty);

                    //Death animation/respawning; - 死亡动画/重生
                    instance->SetData(DATA_CTHUN_PHASE, PHASE_CTHUN_TRANSITION);  // 设置阶段为转换阶段

                    me->SetHealth(0);
                    damage = 0;  // 将伤害设为0，避免真正死亡

                    me->InterruptNonMeleeSpells(true);
                    me->RemoveAllAuras();
                    break;

                case PHASE_CTHUN_DONE:
                    //Allow death here - 允许死亡
                    return;

                default:
                    //Prevent death in these phases - 在这些阶段阻止死亡
                    damage = 0;
                    return;
            }
        }
    };

};

class boss_cthun : public CreatureScript
{
public:
    boss_cthun() : CreatureScript("boss_cthun") { }

    CreatureAI* GetAI(Creature* creature) const override
    {
        return GetAQ40AI<cthunAI>(creature);
    }

    struct cthunAI : public BossAI
    {
        cthunAI(Creature* creature) : BossAI(creature, DATA_CTHUN)
        {
            Initialize();
            SetCombatMovement(false);
        }

        void Initialize()
        {
            //One random wisper every 90 - 300 seconds
            WisperTimer = 90000;

            //Phase information
            PhaseTimer = 10000;                                 //Emerge in 10 seconds

            //No hold player for transition
            HoldPlayer.Clear();

            //Body Phase
            EyeTentacleTimer = 30000;
            FleshTentaclesKilled = 0;
            GiantClawTentacleTimer = 15000;                     //15 seconds into body phase (1 min repeat)
            GiantEyeTentacleTimer = 45000;                      //15 seconds into body phase (1 min repeat)
            StomachAcidTimer = 4000;                            //Every 4 seconds
            StomachEnterTimer = 10000;                          //Every 10 seconds
            StomachEnterVisTimer = 0;                           //Always 3.5 seconds after Stomach Enter Timer
            StomachEnterTarget.Clear();                         //Target to be teleported to stomach
        }

        //Out of combat whisper timer
        uint32 WisperTimer;

        //Global variables
        uint32 PhaseTimer;

        //-------------------

        //Phase transition
        ObjectGuid HoldPlayer;

        //Body Phase
        uint32 EyeTentacleTimer;
        uint8 FleshTentaclesKilled;
        uint32 GiantClawTentacleTimer;
        uint32 GiantEyeTentacleTimer;
        uint32 StomachAcidTimer;
        uint32 StomachEnterTimer;
        uint32 StomachEnterVisTimer;
        ObjectGuid StomachEnterTarget;

        //Stomach map, bool = true then in stomach
        std::unordered_map<ObjectGuid, bool> Stomach_Map;

        void Reset() override
        {
            Initialize();
            _Reset();

            //Clear players in stomach and outside
            Stomach_Map.clear();

            //Reset flags
            me->RemoveAurasDueToSpell(SPELL_TRANSFORM);
            me->SetUnitFlag(UNIT_FLAG_UNINTERACTIBLE | UNIT_FLAG_NON_ATTACKABLE);
            me->SetVisible(false);

            instance->SetData(DATA_CTHUN_PHASE, PHASE_NOT_STARTED);
        }

        void SpawnEyeTentacle(float x, float y)
        {
            Creature* Spawned = DoSpawnCreature(NPC_EYE_TENTACLE, x, y, 0, 0, TEMPSUMMON_CORPSE_DESPAWN, 500ms);
            if (Spawned && Spawned->AI())
                if (Unit* target = SelectRandomNotStomach())
                    Spawned->AI()->AttackStart(target);
        }

        Unit* SelectRandomNotStomach()
        {
            if (Stomach_Map.empty())
                return nullptr;

            std::unordered_map<ObjectGuid, bool>::const_iterator i = Stomach_Map.begin();

            std::list<Unit*> temp;
            std::list<Unit*>::const_iterator j;

            //Get all players in map
            while (i != Stomach_Map.end())
            {
                //Check for valid player
                Unit* unit = ObjectAccessor::GetUnit(*me, i->first);

                //Only units out of stomach
                if (unit && i->second == false)
                    temp.push_back(unit);

                ++i;
            }

            if (temp.empty())
                return nullptr;

            j = temp.begin();

            //Get random but only if we have more than one unit on threat list
            if (temp.size() > 1)
                advance(j, rand32() % (temp.size() - 1));

            return (*j);
        }

        void UpdateAI(uint32 diff) override
        {
            //Check if we have a target
            if (!UpdateVictim())
            {
                //No target so we'll use this section to do our random wispers instance wide
                //WisperTimer
                if (WisperTimer <= diff)
                {
                    //Play random sound to the zone
                    Map::PlayerList const& PlayerList = me->GetMap()->GetPlayers();
                    for (Map::PlayerList::const_iterator itr = PlayerList.begin(); itr != PlayerList.end(); ++itr)
                        me->PlayDirectSound(RANDOM_SOUND_WHISPER, itr->GetSource());

                    //One random wisper every 90 - 300 seconds
                    WisperTimer = urand(90000, 300000);
                } else WisperTimer -= diff;

                return;
            }

            me->SetTarget(ObjectGuid::Empty);

            uint32 currentPhase = instance->GetData(DATA_CTHUN_PHASE);
            if (currentPhase == PHASE_CTHUN_STOMACH || currentPhase == PHASE_CTHUN_WEAK)
            {
                // EyeTentacleTimer
                if (EyeTentacleTimer <= diff)
                {
                    //Spawn the 8 Eye Tentacles in the corret spots
                    SpawnEyeTentacle(0, 20);                //south
                    SpawnEyeTentacle(10, 10);               //south west
                    SpawnEyeTentacle(20, 0);                //west
                    SpawnEyeTentacle(10, -10);              //north west

                    SpawnEyeTentacle(0, -20);               //north
                    SpawnEyeTentacle(-10, -10);             //north east
                    SpawnEyeTentacle(-20, 0);               // east
                    SpawnEyeTentacle(-10, 10);              // south east

                    EyeTentacleTimer = 30000; // every 30sec in phase 2
                } else EyeTentacleTimer -= diff;
            }

            switch (currentPhase)
            {
                //Transition phase
                case PHASE_CTHUN_TRANSITION:
                    //PhaseTimer
                    if (PhaseTimer <= diff)
                    {
                        //Switch
                        instance->SetData(DATA_CTHUN_PHASE, PHASE_CTHUN_STOMACH);

                        //Switch to c'thun model
                        me->InterruptNonMeleeSpells(false);
                        DoCast(me, SPELL_TRANSFORM, false);
                        me->SetFullHealth();

                        me->SetVisible(true);
                        me->RemoveUnitFlag(UNIT_FLAG_UNINTERACTIBLE | UNIT_FLAG_NON_ATTACKABLE);

                        //Emerging phase
                        //AttackStart(ObjectAccessor::GetUnit(*me, HoldpPlayer));
                        DoZoneInCombat();

                        //Place all units in threat list on outside of stomach
                        Stomach_Map.clear();

                        for (ThreatReference const* ref : me->GetThreatManager().GetUnsortedThreatList())
                            Stomach_Map[ref->GetVictim()->GetGUID()] = false;   //Outside stomach

                        //Spawn 2 flesh tentacles
                        FleshTentaclesKilled = 0;

                        //Spawn flesh tentacle
                        for (uint8 i = 0; i < 2; i++)
                        {
                            Creature* spawned = me->SummonCreature(NPC_FLESH_TENTACLE, FleshTentaclePos[i], TEMPSUMMON_CORPSE_DESPAWN);
                            if (!spawned)
                                ++FleshTentaclesKilled;
                        }

                        PhaseTimer = 0;
                    } else PhaseTimer -= diff;

                    break;

                //Body Phase
                case PHASE_CTHUN_STOMACH:
                    //Remove Target field
                    me->SetTarget(ObjectGuid::Empty);

                    //Weaken
                    if (FleshTentaclesKilled > 1)
                    {
                        instance->SetData(DATA_CTHUN_PHASE, PHASE_CTHUN_WEAK);

                        Talk(EMOTE_WEAKENED);
                        PhaseTimer = 45000;

                        DoCast(me, SPELL_PURPLE_COLORATION, true);

                        std::unordered_map<ObjectGuid, bool>::iterator i = Stomach_Map.begin();

                        //Kick all players out of stomach
                        while (i != Stomach_Map.end())
                        {
                            //Check for valid player
                            Unit* unit = ObjectAccessor::GetUnit(*me, i->first);

                            //Only move units in stomach
                            if (unit && i->second == true)
                            {
                                //Teleport each player out
                                DoTeleportPlayer(unit, me->GetPositionX(), me->GetPositionY(), me->GetPositionZ() + 10, float(rand32() % 6));

                                //Cast knockback on them
                                DoCast(unit, SPELL_EXIT_STOMACH_KNOCKBACK, true);

                                //Remove the acid debuff
                                unit->RemoveAurasDueToSpell(SPELL_DIGESTIVE_ACID);

                                i->second = false;
                            }
                            ++i;
                        }

                        return;
                    }

                    //Stomach acid
                    if (StomachAcidTimer <= diff)
                    {
                        //Apply aura to all players in stomach
                        std::unordered_map<ObjectGuid, bool>::iterator i = Stomach_Map.begin();

                        while (i != Stomach_Map.end())
                        {
                            //Check for valid player
                            Unit* unit = ObjectAccessor::GetUnit(*me, i->first);

                            //Only apply to units in stomach
                            if (unit && i->second == true)
                            {
                                //Cast digestive acid on them
                                DoCast(unit, SPELL_DIGESTIVE_ACID, true);

                                //Check if player should be kicked from stomach
                                if (unit->IsWithinDist3d(&KickPos, 15.0f))
                                {
                                    //Teleport each player out
                                    DoTeleportPlayer(unit, me->GetPositionX(), me->GetPositionY(), me->GetPositionZ() + 10, float(rand32() % 6));

                                    //Cast knockback on them
                                    DoCast(unit, SPELL_EXIT_STOMACH_KNOCKBACK, true);

                                    //Remove the acid debuff
                                    unit->RemoveAurasDueToSpell(SPELL_DIGESTIVE_ACID);

                                    i->second = false;
                                }
                            }
                            ++i;
                        }

                        StomachAcidTimer = 4000;
                    } else StomachAcidTimer -= diff;

                    //Stomach Enter Timer
                    if (StomachEnterTimer <= diff)
                    {
                        if (Unit* target = SelectRandomNotStomach())
                        {
                            //Set target in stomach
                            Stomach_Map[target->GetGUID()] = true;
                            target->InterruptNonMeleeSpells(false);
                            target->CastSpell(target, SPELL_MOUTH_TENTACLE, me->GetGUID());
                            StomachEnterTarget = target->GetGUID();
                            StomachEnterVisTimer = 3800;
                        }

                        StomachEnterTimer = 13800;
                    } else StomachEnterTimer -= diff;

                    if (StomachEnterVisTimer && StomachEnterTarget)
                    {
                        if (StomachEnterVisTimer <= diff)
                        {
                            //Check for valid player
                            Unit* unit = ObjectAccessor::GetUnit(*me, StomachEnterTarget);

                            if (unit)
                            {
                                DoTeleportPlayer(unit, STOMACH_X, STOMACH_Y, STOMACH_Z, STOMACH_O);
                            }

                            StomachEnterTarget.Clear();
                            StomachEnterVisTimer = 0;
                        } else StomachEnterVisTimer -= diff;
                    }

                    //GientClawTentacleTimer
                    if (GiantClawTentacleTimer <= diff)
                    {
                        if (Unit* target = SelectRandomNotStomach())
                        {
                            //Spawn claw tentacle on the random target
                            if (Creature* spawned = me->SummonCreature(NPC_GIANT_CLAW_TENTACLE, *target, TEMPSUMMON_CORPSE_DESPAWN, 500ms))
                                if (spawned->AI())
                                    spawned->AI()->AttackStart(target);
                        }

                        //One giant claw tentacle every minute
                        GiantClawTentacleTimer = 60000;
                    } else GiantClawTentacleTimer -= diff;

                    //GiantEyeTentacleTimer
                    if (GiantEyeTentacleTimer <= diff)
                    {
                        if (Unit* target = SelectRandomNotStomach())
                        {
                            //Spawn claw tentacle on the random target
                            if (Creature* spawned = me->SummonCreature(NPC_GIANT_EYE_TENTACLE, *target, TEMPSUMMON_CORPSE_DESPAWN, 500ms))
                                if (spawned->AI())
                                    spawned->AI()->AttackStart(target);
                        }

                        //One giant eye tentacle every minute
                        GiantEyeTentacleTimer = 60000;
                    } else GiantEyeTentacleTimer -= diff;

                    break;

                //Weakened state
                case PHASE_CTHUN_WEAK:
                    //PhaseTimer
                    if (PhaseTimer <= diff)
                    {
                        //Switch
                        instance->SetData(DATA_CTHUN_PHASE, PHASE_CTHUN_STOMACH);

                        //Remove purple coloration
                        me->RemoveAurasDueToSpell(SPELL_PURPLE_COLORATION);

                        //Spawn 2 flesh tentacles
                        FleshTentaclesKilled = 0;

                        //Spawn flesh tentacle
                        for (uint8 i = 0; i < 2; i++)
                        {
                            Creature* spawned = me->SummonCreature(NPC_FLESH_TENTACLE, FleshTentaclePos[i], TEMPSUMMON_CORPSE_DESPAWN);
                            if (!spawned)
                                ++FleshTentaclesKilled;
                        }

                        PhaseTimer = 0;
                    } else PhaseTimer -= diff;

                    break;
            }
        }

        void JustDied(Unit* /*killer*/) override
        {
            instance->SetData(DATA_CTHUN_PHASE, PHASE_CTHUN_DONE);
        }

        void DamageTaken(Unit* /*done_by*/, uint32& damage, DamageEffectType /*damageType*/, SpellInfo const* /*spellInfo = nullptr*/) override
        {
            switch (instance->GetData(DATA_CTHUN_PHASE))
            {
                case PHASE_CTHUN_STOMACH:
                    //Not weakened so reduce damage by 99%
                    damage /= 100;
                    if (damage == 0)
                        damage = 1;

                    //Prevent death in non-weakened state
                    if (damage >= me->GetHealth())
                        damage = 0;

                    return;

                case PHASE_CTHUN_WEAK:
                    //Weakened - takes normal damage
                    return;

                default:
                    damage = 0;
                    break;
            }
        }

        void DoAction(int32 param) override
        {
            switch (param)
            {
                case ACTION_FLESH_TENTACLE_KILLED:
                    ++FleshTentaclesKilled;
                    break;
            }
        }
    };

};

class npc_eye_tentacle : public CreatureScript
{
public:
    npc_eye_tentacle() : CreatureScript("npc_eye_tentacle") { }

    CreatureAI* GetAI(Creature* creature) const override
    {
        return GetAQ40AI<eye_tentacleAI>(creature);
    }

    struct eye_tentacleAI : public ScriptedAI
    {
        eye_tentacleAI(Creature* creature) : ScriptedAI(creature)
        {
            MindflayTimer = 500;
            KillSelfTimer = 35000;

            if (Creature* pPortal = me->SummonCreature(NPC_SMALL_PORTAL, *me, TEMPSUMMON_CORPSE_DESPAWN))
            {
                pPortal->SetReactState(REACT_PASSIVE);
                Portal = pPortal->GetGUID();
            }

            SetCombatMovement(false);
        }

        uint32 MindflayTimer;
        uint32 KillSelfTimer;
        ObjectGuid Portal;

        void JustDied(Unit* /*killer*/) override
        {
            if (Unit* p = ObjectAccessor::GetUnit(*me, Portal))
                p->KillSelf();
        }

        void Reset() override
        {
            //Mind flay half a second after we spawn
            MindflayTimer = 500;

            //This prevents eyes from overlapping
            KillSelfTimer = 35000;
        }

        void JustEngagedWith(Unit* /*who*/) override
        {
            DoZoneInCombat();
        }

        void UpdateAI(uint32 diff) override
        {
            //Check if we have a target
            if (!UpdateVictim())
                return;

            //KillSelfTimer
            if (KillSelfTimer <= diff)
            {
                me->KillSelf();
                return;
            } else KillSelfTimer -= diff;

            //MindflayTimer
            if (MindflayTimer <= diff)
            {
                Unit* target = SelectTarget(SelectTargetMethod::Random, 0);
                if (target && !target->HasAura(SPELL_DIGESTIVE_ACID))
                    DoCast(target, SPELL_MIND_FLAY);

                //Mindflay every 10 seconds
                MindflayTimer = 10000;
            } else MindflayTimer -= diff;
        }
    };

};

class npc_claw_tentacle : public CreatureScript
{
public:
    npc_claw_tentacle() : CreatureScript("npc_claw_tentacle") { }

    CreatureAI* GetAI(Creature* creature) const override
    {
        return GetAQ40AI<claw_tentacleAI>(creature);
    }

    struct claw_tentacleAI : public ScriptedAI
    {
        claw_tentacleAI(Creature* creature) : ScriptedAI(creature)
        {
            GroundRuptureTimer = 500;
            HamstringTimer = 2000;
            EvadeTimer = 5000;

            SetCombatMovement(false);

            if (Creature* pPortal = me->SummonCreature(NPC_SMALL_PORTAL, *me, TEMPSUMMON_CORPSE_DESPAWN))
            {
                pPortal->SetReactState(REACT_PASSIVE);
                Portal = pPortal->GetGUID();
            }
        }

        uint32 GroundRuptureTimer;
        uint32 HamstringTimer;
        uint32 EvadeTimer;
        ObjectGuid Portal;

        void JustDied(Unit* /*killer*/) override
        {
            if (Unit* p = ObjectAccessor::GetUnit(*me, Portal))
                p->KillSelf();
        }

        void Reset() override
        {
            //First rupture should happen half a second after we spawn
            GroundRuptureTimer = 500;
            HamstringTimer = 2000;
            EvadeTimer = 5000;
        }

        void JustEngagedWith(Unit* /*who*/) override
        {
            DoZoneInCombat();
        }

        void UpdateAI(uint32 diff) override
        {
            //Check if we have a target
            if (!UpdateVictim())
                return;

            //EvadeTimer
            if (!me->IsWithinMeleeRange(me->GetVictim()))
            {
                if (EvadeTimer <= diff)
                {
                    if (Unit* p = ObjectAccessor::GetUnit(*me, Portal))
                        p->KillSelf();

                    //Dissapear and reappear at new position
                    me->SetVisible(false);

                    Unit* target = SelectTarget(SelectTargetMethod::Random, 0);
                    if (!target)
                    {
                        me->KillSelf();
                        return;
                    }

                    if (!target->HasAura(SPELL_DIGESTIVE_ACID))
                    {
                        me->UpdatePosition(target->GetPositionX(), target->GetPositionY(), target->GetPositionZ(), 0);
                        if (Creature* pPortal = me->SummonCreature(NPC_SMALL_PORTAL, *me, TEMPSUMMON_CORPSE_DESPAWN))
                        {
                            pPortal->SetReactState(REACT_PASSIVE);
                            Portal = pPortal->GetGUID();
                        }

                        GroundRuptureTimer = 500;
                        HamstringTimer = 2000;
                        EvadeTimer = 5000;
                        AttackStart(target);
                    }

                    me->SetVisible(true);
                } else EvadeTimer -= diff;
            }

            //GroundRuptureTimer
            if (GroundRuptureTimer <= diff)
            {
                DoCastVictim(SPELL_GROUND_RUPTURE);
                GroundRuptureTimer = 30000;
            } else GroundRuptureTimer -= diff;

            //HamstringTimer
            if (HamstringTimer <= diff)
            {
                DoCastVictim(SPELL_HAMSTRING);
                HamstringTimer = 5000;
            } else HamstringTimer -= diff;

            DoMeleeAttackIfReady();
        }
    };

};

class npc_giant_claw_tentacle : public CreatureScript
{
public:
    npc_giant_claw_tentacle() : CreatureScript("npc_giant_claw_tentacle") { }

    CreatureAI* GetAI(Creature* creature) const override
    {
        return GetAQ40AI<giant_claw_tentacleAI>(creature);
    }

    struct giant_claw_tentacleAI : public ScriptedAI
    {
        giant_claw_tentacleAI(Creature* creature) : ScriptedAI(creature)
        {
            GroundRuptureTimer = 500;
            HamstringTimer = 2000;
            ThrashTimer = 5000;
            EvadeTimer = 5000;

            SetCombatMovement(false);

            if (Creature* pPortal = me->SummonCreature(NPC_GIANT_PORTAL, *me, TEMPSUMMON_CORPSE_DESPAWN))
            {
                pPortal->SetReactState(REACT_PASSIVE);
                Portal = pPortal->GetGUID();
            }
        }

        uint32 GroundRuptureTimer;
        uint32 ThrashTimer;
        uint32 HamstringTimer;
        uint32 EvadeTimer;
        ObjectGuid Portal;

        void JustDied(Unit* /*killer*/) override
        {
            if (Unit* p = ObjectAccessor::GetUnit(*me, Portal))
                p->KillSelf();
        }

        void Reset() override
        {
            //First rupture should happen half a second after we spawn
            GroundRuptureTimer = 500;
            HamstringTimer = 2000;
            ThrashTimer = 5000;
            EvadeTimer = 5000;
        }

        void JustEngagedWith(Unit* /*who*/) override
        {
            DoZoneInCombat();
        }

        void UpdateAI(uint32 diff) override
        {
            //Check if we have a target
            if (!UpdateVictim())
                return;

            //EvadeTimer
            if (!me->IsWithinMeleeRange(me->GetVictim()))
            {
                if (EvadeTimer <= diff)
                {
                    if (Unit* p = ObjectAccessor::GetUnit(*me, Portal))
                        p->KillSelf();

                    //Dissapear and reappear at new position
                    me->SetVisible(false);

                    Unit* target = SelectTarget(SelectTargetMethod::Random, 0);
                    if (!target)
                    {
                        me->KillSelf();
                        return;
                    }

                    if (!target->HasAura(SPELL_DIGESTIVE_ACID))
                    {
                        me->UpdatePosition(target->GetPositionX(), target->GetPositionY(), target->GetPositionZ(), 0);
                        if (Creature* pPortal = me->SummonCreature(NPC_GIANT_PORTAL, *me, TEMPSUMMON_CORPSE_DESPAWN))
                        {
                            pPortal->SetReactState(REACT_PASSIVE);
                            Portal = pPortal->GetGUID();
                        }

                        GroundRuptureTimer = 500;
                        HamstringTimer = 2000;
                        ThrashTimer = 5000;
                        EvadeTimer = 5000;
                        AttackStart(target);
                    }
                    me->SetVisible(true);
                } else EvadeTimer -= diff;
            }

            //GroundRuptureTimer
            if (GroundRuptureTimer <= diff)
            {
                DoCastVictim(SPELL_GROUND_RUPTURE);
                GroundRuptureTimer = 30000;
            } else GroundRuptureTimer -= diff;

            //ThrashTimer
            if (ThrashTimer <= diff)
            {
                DoCastVictim(SPELL_THRASH);
                ThrashTimer = 10000;
            } else ThrashTimer -= diff;

            //HamstringTimer
            if (HamstringTimer <= diff)
            {
                DoCastVictim(SPELL_HAMSTRING);
                HamstringTimer = 10000;
            } else HamstringTimer -= diff;

            DoMeleeAttackIfReady();
        }
    };

};

class npc_giant_eye_tentacle : public CreatureScript
{
public:
    npc_giant_eye_tentacle() : CreatureScript("npc_giant_eye_tentacle") { }

    CreatureAI* GetAI(Creature* creature) const override
    {
        return GetAQ40AI<giant_eye_tentacleAI>(creature);
    }

    struct giant_eye_tentacleAI : public ScriptedAI
    {
        giant_eye_tentacleAI(Creature* creature) : ScriptedAI(creature)
        {
            BeamTimer = 500;

            SetCombatMovement(false);

            if (Creature* pPortal = me->SummonCreature(NPC_GIANT_PORTAL, *me, TEMPSUMMON_CORPSE_DESPAWN))
            {
                pPortal->SetReactState(REACT_PASSIVE);
                Portal = pPortal->GetGUID();
            }
        }

        uint32 BeamTimer;
        ObjectGuid Portal;

        void JustDied(Unit* /*killer*/) override
        {
            if (Unit* p = ObjectAccessor::GetUnit(*me, Portal))
                p->KillSelf();
        }

        void Reset() override
        {
            //Green Beam half a second after we spawn
            BeamTimer = 500;
        }

        void JustEngagedWith(Unit* /*who*/) override
        {
            DoZoneInCombat();
        }

        void UpdateAI(uint32 diff) override
        {
            //Check if we have a target
            if (!UpdateVictim())
                return;

            //BeamTimer
            if (BeamTimer <= diff)
            {
                Unit* target = SelectTarget(SelectTargetMethod::Random, 0);
                if (target && !target->HasAura(SPELL_DIGESTIVE_ACID))
                    DoCast(target, SPELL_GREEN_BEAM);

                //Beam every 2 seconds
                BeamTimer = 2100;
            } else BeamTimer -= diff;
        }
    };

};

class npc_giant_flesh_tentacle : public CreatureScript
{
public:
    npc_giant_flesh_tentacle() : CreatureScript("npc_giant_flesh_tentacle") { }

    CreatureAI* GetAI(Creature* creature) const override
    {
        return GetAQ40AI<flesh_tentacleAI>(creature);
    }

    struct flesh_tentacleAI : public ScriptedAI
    {
        flesh_tentacleAI(Creature* creature) : ScriptedAI(creature)
        {
            SetCombatMovement(false);
        }

        void JustDied(Unit* /*killer*/) override
        {
            if (TempSummon* summon = me->ToTempSummon())
                if (Unit* summoner = summon->GetSummonerUnit())
                    if (summoner->IsAIEnabled())
                        summoner->GetAI()->DoAction(ACTION_FLESH_TENTACLE_KILLED);
        }
    };

};

//GetAIs

void AddSC_boss_cthun()
{
    new boss_eye_of_cthun();
    new boss_cthun();
    new npc_eye_tentacle();
    new npc_claw_tentacle();
    new npc_giant_claw_tentacle();
    new npc_giant_eye_tentacle();
    new npc_giant_flesh_tentacle();
}
