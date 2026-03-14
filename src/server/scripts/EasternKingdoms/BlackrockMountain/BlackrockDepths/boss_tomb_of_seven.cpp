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
 * @file boss_tomb_of_seven.cpp
 * @brief 黑石深渊副本七贤之墓BOSS脚本实现
 *
 * 本文件实现了七贤之墓事件中的两个关键BOSS：
 * 1. 忧郁者·黑铁矮人(Gloomrel) - 负责教授黑铁熔炼技能和刷新幽灵圣杯
 * 2. 厄运者·黑铁矮人(Doomrel) - 七贤之墓事件的触发者，击败后开始七贤挑战
 *
 * 七贤之墓是黑石深渊中的特殊事件，玩家需要依次击败7个黑铁矮人幽灵。
 * 事件由厄运者触发，击败他后会开启整个七贤挑战流程。
 */

#include "ScriptMgr.h"
#include "blackrock_depths.h"
#include "InstanceScript.h"
#include "Player.h"
#include "ScriptedCreature.h"
#include "ScriptedGossip.h"

/**
 * @brief 忧郁者相关法术ID枚举
 * 定义忧郁者使用的技能和教学相关法术
 */
enum Spells
{
    SPELL_SMELT_DARK_IRON       = 14891,  // 黑铁熔炼技能 - 永久技能
    SPELL_LEARN_SMELT           = 14894,  // 学习熔炼法术 - 教学法术
};

/**
 * @brief 任务ID枚举
 * 定义与七贤之墓相关的任务
 */
enum Quests
{
    QUEST_SPECTRAL_CHALICE      = 4083    // 幽灵圣杯任务
};

/**
 * @brief 杂项数据枚举
 * 定义技能点要求和阶段数据
 */
enum Misc
{
    DATA_SKILLPOINT_MIN         = 230     // 学习黑铁熔炼所需的最小采矿技能点数
};

/**
 * @brief 阶段枚举
 * 定义对话系统的不同阶段
 */
enum Phases
{
    PHASE_ONE                   = 1,      // 对话阶段一
    PHASE_TWO                   = 2       // 对话阶段二
};

/// 闲聊选项文本定义
#define GOSSIP_ITEM_TEACH_1 "Teach me the art of smelting dark iron"  // "教我黑铁熔炼技术"
#define GOSSIP_ITEM_TEACH_2 "Continue..."                             // "继续..."
#define GOSSIP_ITEM_TEACH_3 "[PH] Continue..."                        // "[占位] 继续..."
#define GOSSIP_ITEM_TRIBUTE "I want to pay tribute"                   // "我要献上祭品"

/**
 * @brief 忧郁者·黑铁矮人BOSS脚本类
 *
 * 继承自CreatureScript，为忧郁者提供对话和功能支持。
 * 忧郁者不参与战斗，主要功能是：
 * 1. 教授玩家黑铁熔炼技能（需要完成幽灵圣杯任务且采矿技能>=230）
 * 2. 刷新幽灵圣杯游戏对象（用于触发七贤之墓事件）
 */
class boss_gloomrel : public CreatureScript
{
    public:
        /**
         * @brief 构造函数
         * 初始化BOSS脚本，注册脚本名称为"boss_gloomrel"
         */
        boss_gloomrel() : CreatureScript("boss_gloomrel") { }

        /**
         * @brief 忧郁者AI结构体
         *
         * 继承自ScriptedAI，实现忧郁者的对话功能。
         * 忧郁者是一个友善的NPC，提供黑铁熔炼教学和幽灵圣杯刷新服务。
         */
        struct boss_gloomrelAI : public ScriptedAI
        {
            /**
             * @brief 构造函数
             * @param creature 生物对象指针
             * 初始化AI并获取实例脚本指针
             */
            boss_gloomrelAI(Creature* creature) : ScriptedAI(creature), instance(creature->GetInstanceScript()) { }

            InstanceScript* instance;  ///< 副本实例脚本指针，用于访问副本数据

            /**
             * @brief 闲聊选项选择事件处理
             * @param player 选择闲聊选项的玩家对象
             * @param menuId 菜单ID（当前未使用）
             * @param gossipListId 闲聊列表ID
             * @return 返回true表示事件已处理
             *
             * 处理玩家与忧郁者对话时的选项选择事件：
             * - GOSSIP_ACTION_INFO_DEF + 1: 显示黑铁熔炼教学对话第二步
             * - GOSSIP_ACTION_INFO_DEF + 11: 教授玩家黑铁熔炼技能
             * - GOSSIP_ACTION_INFO_DEF + 2: 显示献祭对话第二步
             * - GOSSIP_ACTION_INFO_DEF + 22: 刷新幽灵圣杯
             *
             * 调用时机：玩家选择对话选项时
             */
            bool OnGossipSelect(Player* player, uint32 /*menuId*/, uint32 gossipListId) override
            {
                uint32 const action = player->PlayerTalkClass->GetGossipOptionAction(gossipListId);
                ClearGossipMenuFor(player);
                switch (action)
                {
                    case GOSSIP_ACTION_INFO_DEF + 1:
                        // 显示黑铁熔炼教学对话第二步
                        AddGossipItemFor(player, GOSSIP_ICON_CHAT, GOSSIP_ITEM_TEACH_2, GOSSIP_SENDER_MAIN, GOSSIP_ACTION_INFO_DEF + 11);
                        SendGossipMenuFor(player, 2606, me->GetGUID());
                        break;
                    case GOSSIP_ACTION_INFO_DEF + 11:
                        // 教授玩家黑铁熔炼技能
                        CloseGossipMenuFor(player);
                        player->CastSpell(player, SPELL_LEARN_SMELT, false);
                        break;
                    case GOSSIP_ACTION_INFO_DEF + 2:
                        // 显示献祭对话第二步
                        AddGossipItemFor(player, GOSSIP_ICON_CHAT, GOSSIP_ITEM_TEACH_3, GOSSIP_SENDER_MAIN, GOSSIP_ACTION_INFO_DEF + 22);
                        SendGossipMenuFor(player, 2604, me->GetGUID());
                        break;
                    case GOSSIP_ACTION_INFO_DEF + 22:
                        // 刷新幽灵圣杯，5分钟后消失
                        CloseGossipMenuFor(player);
                        // 注意：5分钟的刷新时间是预期的吗？游戏对象模板可能有任务完成后的消失数据
                        instance->DoRespawnGameObject(instance->GetGuidData(DATA_GO_CHALICE), 5min);
                        break;
                }
                return true;
            }

            /**
             * @brief 闲聊问候事件处理
             * @param player 与NPC交互的玩家对象
             * @return 返回true表示事件已处理
             *
             * 当玩家与忧郁者对话时，根据玩家条件显示不同的闲聊选项：
             * 1. 如果玩家已完成幽灵圣杯任务、采矿技能>=230且未学会黑铁熔炼，显示教学选项
             * 2. 如果玩家未完成幽灵圣杯任务且采矿技能>=230，显示献祭选项
             *
             * 调用时机：玩家右键点击NPC打开对话时
             */
            bool OnGossipHello(Player* player) override
            {
                // 条件1：已完成幽灵圣杯任务、采矿技能>=230、未学会黑铁熔炼 -> 显示教学选项
                if (player->GetQuestRewardStatus(QUEST_SPECTRAL_CHALICE) == 1 && player->GetSkillValue(SKILL_MINING) >= DATA_SKILLPOINT_MIN && !player->HasSpell(SPELL_SMELT_DARK_IRON))
                    AddGossipItemFor(player, GOSSIP_ICON_CHAT, GOSSIP_ITEM_TEACH_1, GOSSIP_SENDER_MAIN, GOSSIP_ACTION_INFO_DEF + 1);

                // 条件2：未完成幽灵圣杯任务、采矿技能>=230 -> 显示献祭选项
                if (player->GetQuestRewardStatus(QUEST_SPECTRAL_CHALICE) == 0 && player->GetSkillValue(SKILL_MINING) >= DATA_SKILLPOINT_MIN)
                    AddGossipItemFor(player, GOSSIP_ICON_CHAT, GOSSIP_ITEM_TRIBUTE, GOSSIP_SENDER_MAIN, GOSSIP_ACTION_INFO_DEF + 2);

                SendGossipMenuFor(player, player->GetGossipTextId(me), me->GetGUID());
                return true;
            }
        };

        /**
         * @brief 获取AI实例
         * @param creature 需要获取AI的生物对象
         * @return 返回忧郁者的AI实例指针
         *
         * 工厂方法，为指定的生物对象创建并返回对应的AI实例。
         *
         * 调用时机：生物对象创建时由核心引擎调用
         */
        CreatureAI* GetAI(Creature* creature) const override
        {
            return GetBlackrockDepthsAI<boss_gloomrelAI>(creature);
        }
};

/**
 * @brief 厄运者相关法术ID枚举
 * 定义厄运者在战斗中使用的所有法术技能
 */
enum DoomrelSpells
{
    SPELL_SHADOWBOLTVOLLEY                                 = 15245,  // 暗影箭齐射 - 对周围敌人造成暗影伤害
    SPELL_IMMOLATE                                         = 12742,  // 献祭 - 持续火焰伤害
    SPELL_CURSEOFWEAKNESS                                  = 12493,  // 虚弱诅咒 - 降低目标攻击强度
    SPELL_DEMONARMOR                                       = 13787,  // 恶魔护甲 - 增加护甲和暗影抗性
    SPELL_SUMMON_VOIDWALKERS                               = 15092   // 召唤虚空行者 - 生命值低于50%时召唤帮手
};

/**
 * @brief 厄运者对话文本枚举
 * 定义与厄运者对话相关的菜单和文本ID
 */
enum DoomrelText
{
    GOSSIP_SELECT_DOOMREL                                  = 1828,   // 厄运者对话菜单ID
    GOSSIP_MENU_ID_CONTINUE                                = 1,      // "继续"菜单项ID

    GOSSIP_MENU_CHALLENGE                                  = 1947,   // 挑战菜单ID
    GOSSIP_MENU_ID_CHALLENGE                               = 0       // 挑战菜单项ID
};

/**
 * @brief 厄运者事件ID枚举
 * 定义战斗事件调度器使用的事件类型
 */
enum DoomrelEvents
{
    EVENT_SHADOW_BOLT_VOLLEY                               = 1,      // 暗影箭齐射事件
    EVENT_IMMOLATE                                         = 2,      // 献祭事件
    EVENT_CURSE_OF_WEAKNESS                                = 3,      // 虚弱诅咒事件
    EVENT_DEMONARMOR                                       = 4,      // 恶魔护甲事件
    EVENT_SUMMON_VOIDWALKERS                               = 5       // 召唤虚空行者事件
};

/**
 * @brief 厄运者·黑铁矮人BOSS脚本类
 *
 * 继承自CreatureScript，为厄运者提供对话和战斗AI支持。
 * 厄运者是七贤之墓事件的关键触发者，玩家需要通过对话挑战他来开启事件。
 * 战斗特点：
 * - 术士型BOSS，使用暗影箭齐射、献祭、虚弱诅咒等技能
 * - 生命值低于50%时召唤虚空行者助战
 * - 击败后会开启整个七贤之墓挑战流程
 */
class boss_doomrel : public CreatureScript
{
    public:
        /**
         * @brief 构造函数
         * 初始化BOSS脚本，注册脚本名称为"boss_doomrel"
         */
        boss_doomrel() : CreatureScript("boss_doomrel") { }

        /**
         * @brief 厄运者AI结构体
         *
         * 继承自ScriptedAI，实现厄运者的对话和战斗逻辑。
         * 该AI管理BOSS的对话交互、法术释放和七贤事件触发。
         */
        struct boss_doomrelAI : public ScriptedAI
        {
            /**
             * @brief 构造函数
             * @param creature 生物对象指针
             * 初始化AI，设置实例脚本指针和成员变量
             */
            boss_doomrelAI(Creature* creature) : ScriptedAI(creature)
            {
                Initialize();
                _instance = creature->GetInstanceScript();
            }

            /**
             * @brief 初始化成员变量
             * 将虚空行者召唤标志重置为false
             */
            void Initialize()
            {
                _voidwalkers = false;
            }

            void Reset() override
            {
                Initialize();

                // 重置为友善阵营，允许玩家对话
                me->SetFaction(FACTION_FRIENDLY);

                // 设置为对玩家免疫，避免误伤
                // 事件开始前已设置，这里重新设置
                me->SetImmuneToPC(true);

                // 如果已击杀所有七贤BOSS，移除对话标志；否则设置对话标志
                if (_instance->GetData(DATA_GHOSTKILL) >= 7)
                    me->RemoveNpcFlag(UNIT_NPC_FLAG_GOSSIP);
                else
                    me->SetNpcFlag(UNIT_NPC_FLAG_GOSSIP);
            }

            /**
             * @brief 进入战斗事件处理
             * @param who 进入战斗的目标单位（当前未使用）
             *
             * 当BOSS进入战斗状态时调用，调度所有战斗法术的初始释放时间。
             *
             * 调用时机：BOSS被攻击或主动攻击玩家时
             */
            void JustEngagedWith(Unit* /*who*/) override
            {
                _events.ScheduleEvent(EVENT_SHADOW_BOLT_VOLLEY, 10s);   // 10秒后首次施放暗影箭齐射
                _events.ScheduleEvent(EVENT_IMMOLATE, 18s);             // 18秒后首次施放献祭
                _events.ScheduleEvent(EVENT_CURSE_OF_WEAKNESS, 5s);     // 5秒后首次施放虚弱诅咒
                _events.ScheduleEvent(EVENT_DEMONARMOR, 16s);           // 16秒后首次施放恶魔护甲
            }

            /**
             * @brief 受到伤害事件处理
             * @param attacker 攻击者单位（当前未使用）
             * @param damage 伤害值（当前未使用）
             * @param damageType 伤害类型（当前未使用）
             * @param spellInfo 法术信息（当前未使用）
             *
             * 当BOSS生命值降至50%以下时，召唤虚空行者助战。
             * 该事件只会触发一次。
             *
             * 调用时机：BOSS受到任何伤害时
             */
            void DamageTaken(Unit* /*attacker*/, uint32& /*damage*/, DamageEffectType /*damageType*/, SpellInfo const* /*spellInfo = nullptr*/) override
            {
                // 如果尚未召唤虚空行者且生命值低于50%
                if (!_voidwalkers && !HealthAbovePct(50))
                {
                    DoCastVictim(SPELL_SUMMON_VOIDWALKERS, true);
                    _voidwalkers = true;
                }
            }

            /**
             * @brief 进入逃避模式事件处理
             * @param why 逃避原因
             *
             * 当BOSS脱离战斗时，重置七贤之墓事件。
             * 这会关闭事件，允许玩家重新开始。
             *
             * 调用时机：BOSS脱离战斗、团灭时
             */
            void EnterEvadeMode(EvadeReason why) override
            {
                ScriptedAI::EnterEvadeMode(why);

                // 重置七贤之墓事件，清空事件触发者GUID
                _instance->SetGuidData(DATA_EVENSTARTER, ObjectGuid::Empty);
            }

            /**
             * @brief 死亡事件处理
             * @param killer 击杀者单位（当前未使用）
             *
             * 当BOSS死亡时，增加七贤之墓击杀计数，推进事件进度。
             *
             * 调用时机：BOSS被击杀时
             */
            void JustDied(Unit* /*killer*/) override
            {
                // 增加七贤之墓击杀计数1点
                _instance->SetData(DATA_GHOSTKILL, 1);
            }

            /**
             * @brief 更新AI逻辑
             * @param diff 距离上次更新的时间差（毫秒）
             *
             * 每个游戏循环周期调用一次，处理战斗逻辑。
             * 包括检查战斗状态、更新事件计时器、执行法术释放。
             *
             * 调用时机：每个游戏Tick（约每50毫秒）
             * 性能注意事项：频繁调用，已优化处理逻辑
             */
            void UpdateAI(uint32 diff) override
            {
                // 如果没有有效的攻击目标，则不执行任何操作
                if (!UpdateVictim())
                    return;

                // 更新事件计时器
                _events.Update(diff);

                // 执行所有到期的事件
                while (uint32 eventId = _events.ExecuteEvent())
                {
                    switch (eventId)
                    {
                        case EVENT_SHADOW_BOLT_VOLLEY:
                            // 施放暗影箭齐射，对周围敌人造成暗影伤害
                            DoCastVictim(SPELL_SHADOWBOLTVOLLEY);
                            _events.ScheduleEvent(EVENT_SHADOW_BOLT_VOLLEY, 12s);
                            break;
                        case EVENT_IMMOLATE:
                            // 对随机目标施放献祭，造成持续火焰伤害
                            if (Unit* target = SelectTarget(SelectTargetMethod::Random, 0, 100.0f, true))
                                DoCast(target, SPELL_IMMOLATE);
                            _events.ScheduleEvent(EVENT_IMMOLATE, 25s);
                            break;
                        case EVENT_CURSE_OF_WEAKNESS:
                            // 施放虚弱诅咒，降低目标的攻击强度
                            DoCastVictim(SPELL_CURSEOFWEAKNESS);
                            _events.ScheduleEvent(EVENT_CURSE_OF_WEAKNESS, 45s);
                            break;
                        case EVENT_DEMONARMOR:
                            // 施放恶魔护甲，增加护甲和暗影抗性
                            DoCast(me, SPELL_DEMONARMOR);
                            _events.ScheduleEvent(EVENT_DEMONARMOR, 5min);
                            break;
                        default:
                            break;
                    }
                }

                DoMeleeAttackIfReady();
            }

            /**
             * @brief 闲聊选项选择事件处理
             * @param player 选择闲聊选项的玩家对象
             * @param menuId 菜单ID（当前未使用）
             * @param gossipListId 闲聊列表ID
             * @return 返回true表示事件已处理
             *
             * 处理玩家与厄运者对话时的选项选择事件：
             * - GOSSIP_ACTION_INFO_DEF + 1: 显示挑战对话第二步
             * - GOSSIP_ACTION_INFO_DEF + 2: 开始七贤之墓事件
             *
             * 调用时机：玩家选择对话选项时
             */
            bool OnGossipSelect(Player* player, uint32 /*menuId*/, uint32 gossipListId) override
            {
                uint32 const action = player->PlayerTalkClass->GetGossipOptionAction(gossipListId);
                ClearGossipMenuFor(player);

                switch (action)
                {
                    case GOSSIP_ACTION_INFO_DEF + 1:
                        // 显示挑战对话第二步
                        InitGossipMenuFor(player, GOSSIP_SELECT_DOOMREL);
                        AddGossipItemFor(player, GOSSIP_SELECT_DOOMREL, GOSSIP_MENU_ID_CONTINUE, GOSSIP_SENDER_MAIN, GOSSIP_ACTION_INFO_DEF + 2);
                        SendGossipMenuFor(player, 2605, me->GetGUID());
                        break;
                    case GOSSIP_ACTION_INFO_DEF + 2:
                        // 开始七贤之墓事件
                        CloseGossipMenuFor(player);
                        // 将BOSS设置为黑铁矮人阵营（敌对）
                        me->SetFaction(FACTION_DARK_IRON_DWARVES);
                        // 取消对玩家的免疫
                        me->SetImmuneToPC(false);
                        // 攻击触发事件的玩家
                        me->AI()->AttackStart(player);

                        // 设置事件触发者GUID，启动七贤之墓事件
                        _instance->SetGuidData(DATA_EVENSTARTER, player->GetGUID());
                        break;
                }
                return true;
            }

            /**
             * @brief 闲聊问候事件处理
             * @param player 与NPC交互的玩家对象
             * @return 返回true表示事件已处理
             *
             * 当玩家与厄运者对话时，显示挑战选项。
             *
             * 调用时机：玩家右键点击NPC打开对话时
             */
            bool OnGossipHello(Player* player) override
            {
                // 显示挑战选项
                InitGossipMenuFor(player, GOSSIP_MENU_CHALLENGE);
                AddGossipItemFor(player, GOSSIP_MENU_CHALLENGE, GOSSIP_MENU_ID_CHALLENGE, GOSSIP_SENDER_MAIN, GOSSIP_ACTION_INFO_DEF + 1);
                SendGossipMenuFor(player, 2601, me->GetGUID());

                return true;
            }

        private:
            InstanceScript* _instance;  ///< 副本实例脚本指针，用于访问副本数据
            EventMap _events;           ///< 事件映射表，用于管理法术释放的计时和调度
            bool _voidwalkers;          ///< 是否已召唤虚空行者标志，防止重复召唤
        };

        /**
         * @brief 获取AI实例
         * @param creature 需要获取AI的生物对象
         * @return 返回厄运者的AI实例指针
         *
         * 工厂方法，为指定的生物对象创建并返回对应的AI实例。
         *
         * 调用时机：生物对象创建时由核心引擎调用
         */
        CreatureAI* GetAI(Creature* creature) const override
        {
            return GetBlackrockDepthsAI<boss_doomrelAI>(creature);
        }
};

/**
 * @brief 注册七贤之墓BOSS脚本
 *
 * 将忧郁者和厄运者BOSS脚本注册到脚本系统中。
 * 该函数在服务器启动时被脚本加载器调用，用于初始化所有脚本。
 *
 * 调用时机：服务器启动时的脚本加载阶段
 * 性能注意事项：仅启动时调用一次，无运行时性能影响
 */
void AddSC_boss_tomb_of_seven()
{
    new boss_gloomrel();
    new boss_doomrel();
}
