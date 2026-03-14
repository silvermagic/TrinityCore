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
 * @file zone_undercity.cpp
 * @brief 幽暗城(Undercity)区域脚本模块
 *
 * 本模块实现了幽暗城区域内的NPC交互和任务逻辑，包括：
 * - 任务9180: 前往幽暗城(Journey to Undercity) - 希尔瓦娜斯的挽歌事件
 * - 任务1846: 龙骨胫骨(Dragonmaw Shinbones) - 胫骨弯曲测试
 * - 希尔瓦娜斯·风行者的AI和战斗逻辑
 * - 高等精灵哀悼者的视觉效果
 *
 * 幽暗城是被遗忘者的首都，位于东部王国大陆的洛丹伦废墟之下，
 * 由希尔瓦娜斯·风行者领导。
 *
 * ScriptData
 * SDName: Undercity
 * SD%Complete: 95
 * SDComment: Quest support: 6628, 9180(post-event).
 * SDCategory: Undercity
 * EndScriptData
 *
 * ContentData
 * npc_lady_sylvanas_windrunner - 希尔瓦娜斯·风行者女士，被遗忘者的女王
 * npc_highborne_lamenter - 高等精灵哀悼者
 * npc_parqual_fintallas - 帕夸·芬塔拉斯（已移除或未实现）
 * EndContentData
 */

#include "ScriptMgr.h"
#include "MotionMaster.h"
#include "ObjectAccessor.h"
#include "Player.h"
#include "ScriptedCreature.h"
#include "ScriptedGossip.h"
#include "SpellScript.h"

/*######
## npc_lady_sylvanas_windrunner
## 希尔瓦娜斯·风行者女士
######*/

/**
 * @brief 希尔瓦娜斯·风行者相关枚举定义
 *
 * 定义了希尔瓦娜斯的任务、对话、NPC、法术和事件ID
 */
enum Sylvanas
{
    // 任务ID
    QUEST_JOURNEY_TO_UNDERCITY      = 9180,  ///< 任务：前往幽暗城

    // 对话和表情ID
    EMOTE_LAMENT_END                = 0,     ///< 挽歌结束表情
    SAY_LAMENT_END                  = 1,     ///< 挽歌结束对话
    EMOTE_LAMENT                    = 2,     ///< 开始挽歌表情

    // Ambassador Sunsorrow（哀伤大使）
    SAY_SUNSORROW_WHISPER           = 0,     ///< 哀伤大使的低语

    // 音效ID
    SOUND_CREDIT                    = 10896, ///< 任务完成音效

    // NPC ID
    NPC_HIGHBORNE_LAMENTER          = 21628, ///< 高等精灵哀悼者
    NPC_HIGHBORNE_BUNNY             = 21641, ///< 高等精灵引导生物（用于法术效果）
    NPC_AMBASSADOR_SUNSORROW        = 16287, ///< 哀伤大使

    // 法术ID - 挽歌事件相关
    SPELL_HIGHBORNE_AURA            = 37090, ///< 高等精灵光环法术
    SPELL_SYLVANAS_CAST             = 36568, ///< 希尔瓦娜斯施法效果
    //SPELL_RIBBON_OF_SOULS         = 34432, 真正使用的可能是37099
    SPELL_RIBBON_OF_SOULS           = 37099, ///< 灵魂丝带法术（视觉效果）

    // 战斗法术
    SPELL_BLACK_ARROW               = 59712, ///< 黑箭
    SPELL_FADE                      = 20672, ///< 消失
    SPELL_FADE_BLINK                = 29211, ///< 消失闪烁（瞬移）
    SPELL_MULTI_SHOT                = 59713, ///< 多重射击
    SPELL_SHOT                      = 59710, ///< 射击
    SPELL_SUMMON_SKELETON           = 59711, ///< 召唤骷髅

    // 事件ID
    EVENT_FADE                      = 1,     ///< 消失事件
    EVENT_SUMMON_SKELETON           = 2,     ///< 召唤骷髅事件
    EVENT_BLACK_ARROW               = 3,     ///< 黑箭事件
    EVENT_SHOOT                     = 4,     ///< 射击事件
    EVENT_MULTI_SHOT                = 5,     ///< 多重射击事件
    EVENT_LAMENT_OF_THE_HIGHBORN    = 6,     ///< 高等精灵挽歌事件
    EVENT_SUNSORROW_WHISPER         = 7,     ///< 哀伤大使低语事件

    // GUID标识符
    GUID_EVENT_INVOKER              = 1,     ///< 事件触发者GUID标识
};

/**
 * @brief 音效枚举
 */
enum Sounds
{
    SOUND_AGGRO                     = 5886   ///< 进入战斗音效
};

/**
 * @brief 高等精灵哀悼者生成位置数组
 *
 * 定义了4个高等精灵哀悼者的初始生成坐标
 * 格式：{X坐标, Y坐标, 朝向}
 */
float HighborneLoc[4][3]=
{
    {1285.41f, 312.47f, 0.51f},   ///< 第一个哀悼者位置
    {1286.96f, 310.40f, 1.00f},   ///< 第二个哀悼者位置
    {1289.66f, 309.66f, 1.52f},   ///< 第三个哀悼者位置
    {1292.51f, 310.50f, 1.99f},   ///< 第四个哀悼者位置
};

#define HIGHBORNE_LOC_Y             -61.00f   ///< 高等精灵初始Z坐标
#define HIGHBORNE_LOC_Y_NEW         -55.50f   ///< 高等精灵移动后的Z坐标（上升）

/**
 * @brief 希尔瓦娜斯·风行者女士脚本类
 *
 * 实现被遗忘者女王希尔瓦娜斯·风行者的AI行为。
 * 希尔瓦娜斯是幽暗城的统治者，具有以下主要功能：
 *
 * 1. 战斗AI：
 *    - 使用弓箭进行远程攻击
 *    - 消失和瞬移技能
 *    - 召唤骷髅仆从
 *    - 黑箭技能
 *
 * 2. 挽歌事件（任务9180）：
 *    - 玩家完成任务后触发
 *    - 召唤4个高等精灵哀悼者
 *    - 播放特殊的视觉效果
 *    - 哀伤大使向玩家低语
 *
 * 这是游戏中的重要剧情事件，展现了希尔瓦娜斯作为
 * 前高等精灵游侠将军的悲惨过去。
 */
class npc_lady_sylvanas_windrunner : public CreatureScript
{
public:
    npc_lady_sylvanas_windrunner() : CreatureScript("npc_lady_sylvanas_windrunner") { }

    /**
     * @brief 希尔瓦娜斯·风行者AI结构体
     *
     * 继承自ScriptedAI，实现复杂的战斗和事件逻辑
     */
    struct npc_lady_sylvanas_windrunnerAI : public ScriptedAI
    {
        /**
         * @brief 构造函数
         * @param creature 生物实体指针
         */
        npc_lady_sylvanas_windrunnerAI(Creature* creature) : ScriptedAI(creature)
        {
            Initialize();
        }

        /**
         * @brief 初始化成员变量
         *
         * 重置所有状态变量到初始值
         */
        void Initialize()
        {
            LamentEvent = false;     // 挽歌事件标志
            targetGUID.Clear();      // 目标GUID清空
            playerGUID.Clear();      // 玩家GUID清空
        }

        /**
         * @brief 重置AI状态
         *
         * 当NPC重置时调用，清除所有事件和状态
         */
        void Reset() override
        {
            Initialize();
            _events.Reset();  // 清空事件队列
        }

        /**
         * @brief 进入战斗
         * @param who 攻击目标（未使用）
         *
         * 播放战斗音效并安排所有战斗技能的初始触发时间
         */
        void JustEngagedWith(Unit* /*who*/) override
        {
            DoPlaySoundToSet(me, SOUND_AGGRO);
            // 安排战斗技能事件
            _events.ScheduleEvent(EVENT_FADE, 30s);              // 30秒后消失
            _events.ScheduleEvent(EVENT_SUMMON_SKELETON, 20s);   // 20秒后召唤骷髅
            _events.ScheduleEvent(EVENT_BLACK_ARROW, 15s);       // 15秒后黑箭
            _events.ScheduleEvent(EVENT_SHOOT, 8s);              // 8秒后射击
            _events.ScheduleEvent(EVENT_MULTI_SHOT, 10s);        // 10秒后多重射击
        }

        /**
         * @brief 设置事件触发者GUID
         * @param guid 触发事件的玩家GUID
         * @param id GUID类型标识符
         *
         * 当玩家完成任务9180后调用，启动挽歌事件序列
         */
        void SetGUID(ObjectGuid const& guid, int32 id) override
        {
            if (id == GUID_EVENT_INVOKER)
            {
                // 开始挽歌事件
                Talk(EMOTE_LAMENT);                    // 发表表情
                DoPlaySoundToSet(me, SOUND_CREDIT);    // 播放音效
                DoCast(me, SPELL_SYLVANAS_CAST, false); // 施放法术效果
                playerGUID = guid;                     // 保存玩家GUID
                LamentEvent = true;                    // 标记事件开始

                // 召唤4个高等精灵哀悼者
                for (uint8 i = 0; i < 4; ++i)
                    me->SummonCreature(NPC_HIGHBORNE_LAMENTER, HighborneLoc[i][0], HighborneLoc[i][1], HIGHBORNE_LOC_Y, HighborneLoc[i][2], TEMPSUMMON_TIMED_DESPAWN, 160s);

                // 安排事件
                _events.ScheduleEvent(EVENT_LAMENT_OF_THE_HIGHBORN, 2s);   // 2秒后继续挽歌
                _events.ScheduleEvent(EVENT_SUNSORROW_WHISPER, 10s);       // 10秒后哀伤大使低语
            }
        }

        /**
         * @brief 召唤生物回调
         * @param summoned 被召唤的生物
         *
         * 处理高等精灵引导生物的召唤逻辑，用于创建灵魂丝带视觉效果
         */
        void JustSummoned(Creature* summoned) override
        {
            if (summoned->GetEntry() == NPC_HIGHBORNE_BUNNY)
            {
                // 设置引导生物为反重力状态
                summoned->SetDisableGravity(true);

                // 获取上一个引导生物作为目标
                if (Creature* target = ObjectAccessor::GetCreature(*summoned, targetGUID))
                {
                    // 让目标移动到希尔瓦娜斯上方15码处
                    target->GetMotionMaster()->MovePoint(0, target->GetPositionX(), target->GetPositionY(), me->GetPositionZ() + 15.0f, false);
                    target->UpdatePosition(target->GetPositionX(), target->GetPositionY(), me->GetPositionZ()+15.0f, 0.0f);
                    // 向目标施放灵魂丝带法术（视觉效果）
                    summoned->CastSpell(target, SPELL_RIBBON_OF_SOULS, false);
                }

                // 保存当前引导生物的GUID
                targetGUID = summoned->GetGUID();
            }
        }

        /**
         * @brief 更新AI状态
         * @param diff 距上次更新的时间差（毫秒）
         *
         * 主更新循环，处理战斗逻辑和挽歌事件
         *
         * 战斗AI特点：
         * - 远程攻击为主
         * - 消失+瞬移组合技能
         * - 对远距离目标使用多重射击
         */
        void UpdateAI(uint32 diff) override
        {
            // 如果不在战斗且没有挽歌事件，直接返回
            if (!UpdateVictim() && !LamentEvent)
                return;

            _events.Update(diff);

            // 如果正在施法，等待施法完成
            if (me->HasUnitState(UNIT_STATE_CASTING))
                return;

            // 处理事件队列
            while (uint32 eventId = _events.ExecuteEvent())
            {
                switch (eventId)
                {
                    case EVENT_FADE:
                        // 消失技能：模拟潜行移动到其他位置
                        DoCast(me, SPELL_FADE);
                        // 添加闪烁技能模拟潜行移动和在其他位置重新出现
                        DoCast(me, SPELL_FADE_BLINK);
                        // 如果目标超出近战范围，施放多重射击
                        if (Unit* victim = me->GetVictim())
                            if (me->GetDistance(victim) > 10.0f)
                                DoCast(victim, SPELL_MULTI_SHOT);
                        _events.ScheduleEvent(EVENT_FADE, 30s, 35s);
                        break;
                    case EVENT_SUMMON_SKELETON:
                        // 召唤骷髅仆从
                        DoCast(me, SPELL_SUMMON_SKELETON);
                        _events.ScheduleEvent(EVENT_SUMMON_SKELETON, 20s, 30s);
                        break;
                    case EVENT_BLACK_ARROW:
                        // 对目标施放黑箭
                        if (Unit* victim = me->GetVictim())
                            DoCast(victim, SPELL_BLACK_ARROW);
                        _events.ScheduleEvent(EVENT_BLACK_ARROW, 15s, 20s);
                        break;
                    case EVENT_SHOOT:
                        // 普通射击
                        if (Unit* victim = me->GetVictim())
                            DoCast(victim, SPELL_SHOT);
                        _events.ScheduleEvent(EVENT_SHOOT, 8s, 10s);
                        break;
                    case EVENT_MULTI_SHOT:
                        // 多重射击
                        if (Unit* victim = me->GetVictim())
                            DoCast(victim, SPELL_MULTI_SHOT);
                        _events.ScheduleEvent(EVENT_MULTI_SHOT, 10s, 13s);
                        break;
                    case EVENT_LAMENT_OF_THE_HIGHBORN:
                        // 高等精灵挽歌事件处理
                        if (!me->HasAura(SPELL_SYLVANAS_CAST))
                        {
                            // 挽歌结束
                            Talk(SAY_LAMENT_END);
                            Talk(EMOTE_LAMENT_END);
                            LamentEvent = false;
                            me->HandleEmoteCommand(EMOTE_ONESHOT_KNEEL);  // 下跪表情
                            Reset();
                        }
                        else
                        {
                            // 继续挽歌，召唤引导生物产生灵魂丝带效果
                            DoSummon(NPC_HIGHBORNE_BUNNY, me, 10.0f, 3s, TEMPSUMMON_TIMED_DESPAWN);
                            _events.ScheduleEvent(EVENT_LAMENT_OF_THE_HIGHBORN, 2s);
                        }
                        break;
                    case EVENT_SUNSORROW_WHISPER:
                        // 哀伤大使向玩家低语
                        if (Creature* ambassador = me->FindNearestCreature(NPC_AMBASSADOR_SUNSORROW, 20.0f))
                            if (Player* player = ObjectAccessor::GetPlayer(*me, playerGUID))
                                ambassador->AI()->Talk(SAY_SUNSORROW_WHISPER, player);
                        break;
                    default:
                        break;
                }
            }

            DoMeleeAttackIfReady();
        }

        /**
         * @brief 任务奖励回调
         * @param player 完成任务的玩家
         * @param quest 完成的任务
         * @param opt 选项（未使用）
         *
         * 当玩家完成任务9180后触发挽歌事件
         */
        void OnQuestReward(Player* player, Quest const* quest, uint32 /*opt*/) override
        {
            if (quest->GetQuestId() == QUEST_JOURNEY_TO_UNDERCITY)
                SetGUID(player->GetGUID(), GUID_EVENT_INVOKER);
        }

    private:
        EventMap _events;        ///< 事件管理器
        bool LamentEvent;        ///< 挽歌事件标志
        ObjectGuid targetGUID;   ///< 目标GUID（用于灵魂丝带效果）
        ObjectGuid playerGUID;   ///< 触发事件的玩家GUID
    };

    /**
     * @brief 创建AI实例
     * @param creature 生物实体指针
     * @return 新创建的AI实例
     */
    CreatureAI* GetAI(Creature* creature) const override
    {
        return new npc_lady_sylvanas_windrunnerAI(creature);
    }
};

/*######
## npc_highborne_lamenter
## 高等精灵哀悼者
######*/

/**
 * @brief 高等精灵哀悼者脚本类
 *
 * 实现希尔瓦娜斯挽歌事件中的高等精灵哀悼者AI。
 * 这些幽灵般的高等精灵在希尔瓦娜斯吟唱挽歌时出现，
 * 代表她失去的族人和过去的悲惨记忆。
 *
 * 行为流程：
 * 1. 生成后在原地停留10秒
 * 2. 然后上升（Z坐标从-61.00变为-55.50）
 * 3. 在生成17.5秒后施放高等精灵光环法术
 *
 * 这些哀悼者创造了悲伤而庄严的氛围，强调希尔瓦娜斯的悲剧性。
 */
class npc_highborne_lamenter : public CreatureScript
{
public:
    npc_highborne_lamenter() : CreatureScript("npc_highborne_lamenter") { }

    /**
     * @brief 创建AI实例
     * @param creature 生物实体指针
     * @return 新创建的AI实例
     */
    CreatureAI* GetAI(Creature* creature) const override
    {
        return new npc_highborne_lamenterAI(creature);
    }

    /**
     * @brief 高等精灵哀悼者AI结构体
     *
     * 简单的AI，主要负责定时移动和施放光环
     */
    struct npc_highborne_lamenterAI : public ScriptedAI
    {
        /**
         * @brief 构造函数
         * @param creature 生物实体指针
         */
        npc_highborne_lamenterAI(Creature* creature) : ScriptedAI(creature)
        {
            Initialize();
        }

        /**
         * @brief 初始化成员变量
         */
        void Initialize()
        {
            EventMoveTimer = 10000;  // 10秒后开始移动
            EventCastTimer = 17500;  // 17.5秒后施放光环
            EventMove = true;        // 移动事件标志
            EventCast = true;        // 施法事件标志
        }

        uint32 EventMoveTimer;       ///< 移动计时器（毫秒）
        uint32 EventCastTimer;       ///< 施法计时器（毫秒）
        bool EventMove;              ///< 是否触发移动事件
        bool EventCast;              ///< 是否触发施法事件

        /**
         * @brief 重置AI状态
         */
        void Reset() override
        {
            Initialize();
        }

        /**
         * @brief 进入战斗
         * @param who 攻击目标（未使用）
         *
         * 哀悼者不参与战斗
         */
        void JustEngagedWith(Unit* /*who*/) override { }

        /**
         * @brief 更新AI状态
         * @param diff 距上次更新的时间差（毫秒）
         *
         * 处理移动和施法的定时事件
         */
        void UpdateAI(uint32 diff) override
        {
            // 处理移动事件
            if (EventMove)
            {
                if (EventMoveTimer <= diff)
                {
                    // 10秒后，开始上升
                    me->SetDisableGravity(true);  // 启用反重力
                    // 计算移动速度并移动到新的Z坐标
                    me->MonsterMoveWithSpeed(me->GetPositionX(), me->GetPositionY(), HIGHBORNE_LOC_Y_NEW, me->GetDistance(me->GetPositionX(), me->GetPositionY(), HIGHBORNE_LOC_Y_NEW) / (5000 * 0.001f));
                    // 更新位置
                    me->UpdatePosition(me->GetPositionX(), me->GetPositionY(), HIGHBORNE_LOC_Y_NEW, me->GetOrientation());
                    EventMove = false;  // 移动完成
                } else EventMoveTimer -= diff;
            }
            // 处理施法事件
            if (EventCast)
            {
                if (EventCastTimer <= diff)
                {
                    // 17.5秒后，施放高等精灵光环
                    DoCast(me, SPELL_HIGHBORNE_AURA);
                    EventCast = false;  // 施法完成
                } else EventCastTimer -= diff;
            }
        }
    };
};

/*######
## Quest 1846: Dragonmaw Shinbones
## 任务1846：龙骨胫骨
######*/

/**
 * @brief 龙骨胫骨任务法术枚举
 *
 * 定义了测试胫骨时可能产生的法术ID
 */
enum DragonmawShinbones
{
    SPELL_BENDING_SHINBONE1 = 8854,  ///< 弯曲胫骨法术1 - 产生有用物品
    SPELL_BENDING_SHINBONE2 = 8855   ///< 弯曲胫骨法术2 - 产生无用物品
};

/**
 * @brief 弯曲胫骨法术脚本
 *
 * 实现任务"龙骨胫骨"(Quest 1846)中的物品测试机制。
 * 玩家使用龙骨胫骨时，有20%的几率获得有用的弯曲胫骨，
 * 80%的几率获得无用的碎骨。
 *
 * 该脚本对应法术ID: 8856 - Bending Shinbone
 */
// 8856 - Bending Shinbone
class spell_undercity_bending_shinbone : public SpellScript
{
    PrepareSpellScript(spell_undercity_bending_shinbone);

    /**
     * @brief 验证法术数据有效性
     *
     * 在法术脚本注册时调用，验证所需的物品创建法术是否存在于数据库中
     *
     * @param spellInfo 法术信息指针（未使用）
     * @return bool 如果所有依赖的法术都存在则返回true，否则返回false
     *
     * @note 此方法在服务器启动时调用，用于提前发现配置错误
     */
    bool Validate(SpellInfo const* /*spellInfo*/) override
    {
        return ValidateSpellInfo({ SPELL_BENDING_SHINBONE1, SPELL_BENDING_SHINBONE2 });
    }

    /**
     * @brief 处理脚本效果
     *
     * 当法术效果命中时调用，执行随机的物品创建逻辑。
     *
     * @param effIndex 效果索引（未使用）
     *
     * 执行流程：
     * 1. 使用roll_chance_i(20)进行20%的随机判定
     * 2. 成功(20%)：施放SPELL_BENDING_SHINBONE1，创建有用的弯曲胫骨
     * 3. 失败(80%)：施放SPELL_BENDING_SHINBONE2，创建碎骨
     *
     * @note 这是概率性任务物品，玩家可能需要多次尝试
     */
    void HandleScript(SpellEffIndex /*effIndex*/)
    {
        // 20%几率获得有用的弯曲胫骨，80%几率获得碎骨
        GetCaster()->CastSpell(GetCaster(), roll_chance_i(20) ? SPELL_BENDING_SHINBONE1 : SPELL_BENDING_SHINBONE2);
    }

    /**
     * @brief 注册法术效果回调
     *
     * 在脚本初始化时调用，将HandleScript方法绑定到法术效果上。
     * 当法术的EFFECT_0（第0个效果）触发SPELL_EFFECT_SCRIPT_EFFECT类型时，
     * 会调用HandleScript方法。
     */
    void Register() override
    {
        OnEffectHit += SpellEffectFn(spell_undercity_bending_shinbone::HandleScript, EFFECT_0, SPELL_EFFECT_SCRIPT_EFFECT);
    }
};

/*######
## AddSC
## 注册脚本
######*/

/**
 * @brief 注册幽暗城区域脚本
 *
 * 该函数由脚本系统在服务器启动时自动调用，用于注册本文件中定义的所有NPC脚本和法术脚本。
 *
 * @note 函数名遵循TrinityCore命名规范：AddSC_<区域名称>
 * @note 该函数会在WorldSession初始化期间被调用，不应手动调用
 */
void AddSC_undercity()
{
    // 注册希尔瓦娜斯·风行者女士脚本
    new npc_lady_sylvanas_windrunner();
    // 注册高等精灵哀悼者脚本
    new npc_highborne_lamenter();
    // 注册弯曲胫骨法术脚本
    RegisterSpellScript(spell_undercity_bending_shinbone);
}
