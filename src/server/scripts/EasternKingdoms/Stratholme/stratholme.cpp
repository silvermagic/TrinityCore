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
SDName: Stratholme
SD%Complete: 100
SDComment: Misc mobs for instance. go-script to apply aura and start event for quest 8945
SDCategory: Stratholme
EndScriptData */

/* ContentData
go_gauntlet_gate
npc_freed_soul
npc_restless_soul
npc_spectral_ghostly_citizen
EndContentData */

/**
 * @file    stratholme.cpp
 * @brief   斯坦索姆副本杂项NPC和游戏对象脚本
 *
 * @details 本模块实现了斯坦索姆副本中的各种杂项内容:
 *          - 死亡奔跑门(go_gauntlet_gate): 触发45分钟救援任务
 *          - 不安的灵魂(npc_restless_soul): 任务"不安的灵魂"相关NPC
 *          - 幽灵市民(npc_spectral_ghostly_citizen): 可被净化,击杀后可能刷新不安的灵魂
 *          - 解放的灵魂(npc_freed_soul): 任务完成后的奖励NPC
 *          - 伊希达获救法术(spell_ysida_saved_credit): 处理救援任务完成
 *          - 鬼魅光环法术(spell_stratholme_haunting_phantoms): 召唤幻影
 */

#include "ScriptMgr.h"
#include "GameObject.h"
#include "GameObjectAI.h"
#include "Group.h"
#include "InstanceScript.h"
#include "MotionMaster.h"
#include "ObjectAccessor.h"
#include "Player.h"
#include "ScriptedCreature.h"
#include "SpellInfo.h"
#include "SpellScript.h"
#include "SpellAuraEffects.h"
#include "stratholme.h"

/*######
## go_gauntlet_gate (this is the _first_ of the gauntlet gates, two exist)
## 死亡奔跑门(这是两道死亡奔跑门中的第一道)
######*/

/**
 * @class go_gauntlet_gate
 * @brief 死亡奔跑门游戏对象脚本
 *
 * @details 实现死亡奔跑门(挑战之门)的交互逻辑:
 *          - 当玩家与门交互时检查是否接受任务"死者的恳求"(任务ID: 8945)
 *          - 为接受任务的玩家施放"巴隆的最后通牒"光环(45分钟计时)
 *          - 启动副本的巴隆救援计时事件
 *          - 这是著名的45分钟救援任务触发点
 */
class go_gauntlet_gate : public GameObjectScript
{
    public:
        go_gauntlet_gate() : GameObjectScript("go_gauntlet_gate") { }

        /**
         * @struct go_gauntlet_gateAI
         * @brief 死亡奔跑门AI
         *
         * @details 处理门的交互逻辑
         */
        struct go_gauntlet_gateAI : public GameObjectAI
        {
            /**
             * @brief 构造函数
             * @param go 游戏对象指针
             */
            go_gauntlet_gateAI(GameObject* go) : GameObjectAI(go), instance(go->GetInstanceScript()) { }

            InstanceScript* instance;  // 副本实例脚本指针

            /**
             * @brief 玩家交互处理
             * @param player 交互的玩家
             * @return 是否拦截默认行为(返回false允许默认行为)
             *
             * @details 当玩家与门交互时调用:
             *          1. 检查巴隆救援事件是否已开始
             *          2. 如果玩家在队伍中,为所有接受任务的队友施放计时光环
             *          3. 如果玩家单人,为其施放计时光环
             *          4. 启动副本的巴隆救援事件
             */
            bool OnGossipHello(Player* player) override
            {
                // 如果事件已经开始,不重复触发
                if (instance->GetData(TYPE_BARON_RUN) != NOT_STARTED)
                    return false;

                // 处理队伍成员
                if (Group* group = player->GetGroup())
                {
                    for (GroupReference* itr = group->GetFirstMember(); itr != nullptr; itr = itr->next())
                    {
                        Player* pGroupie = itr->GetSource();
                        // 检查队友是否在同一地图
                        if (!pGroupie || !pGroupie->IsInMap(player))
                            continue;

                        // 检查队友是否接受任务且尚未获得光环
                        if (pGroupie->GetQuestStatus(QUEST_DEAD_MAN_PLEA) == QUEST_STATUS_INCOMPLETE &&
                            !pGroupie->HasAura(SPELL_BARON_ULTIMATUM) &&
                            pGroupie->GetMap() == me->GetMap())
                            pGroupie->CastSpell(pGroupie, SPELL_BARON_ULTIMATUM, true);
                    }
                }
                // 处理单人玩家
                else if (player->GetQuestStatus(QUEST_DEAD_MAN_PLEA) == QUEST_STATUS_INCOMPLETE &&
                    !player->HasAura(SPELL_BARON_ULTIMATUM) &&
                    player->GetMap() == me->GetMap())
                    player->CastSpell(player, SPELL_BARON_ULTIMATUM, true);

                // 启动巴隆救援事件
                instance->SetData(TYPE_BARON_RUN, IN_PROGRESS);
                return false;
            }
        };

        /**
         * @brief 获取AI实例
         * @param go 游戏对象指针
         * @return AI实例指针
         */
        GameObjectAI* GetAI(GameObject* go) const override
        {
            return GetStratholmeAI<go_gauntlet_gateAI>(go);
        }
};

/*######
## npc_restless_soul
## 不安的灵魂
######*/

/**
 * @brief 不安的灵魂相关枚举
 */
enum RestlessSoul
{
    // 法术
    SPELL_EGAN_BLASTER      = 17368,  // 伊根的爆破器 - 用于净化不安的灵魂
    SPELL_SOUL_FREED        = 17370,  // 灵魂解放 - 解放后的灵魂获得的光环

    // 任务
    QUEST_RESTLESS_SOUL     = 5282,   // 任务: 不安的灵魂

    // 生物
    NPC_RESTLESS            = 11122,  // 不安的灵魂 NPC ID
    NPC_FREED               = 11136   // 解放的灵魂 NPC ID
};

/**
 * @class npc_restless_soul
 * @brief 不安的灵魂NPC脚本
 *
 * @details 实现任务"不安的灵魂"的逻辑:
 *          - 玩家使用伊根的爆破器(任务物品)净化不安的灵魂
 *          - 被净化后的灵魂会在死亡后召唤解放的灵魂
 *          - 解放的灵魂会跟随玩家并给予任务进度
 */
class npc_restless_soul : public CreatureScript
{
public:
    npc_restless_soul() : CreatureScript("npc_restless_soul") { }

    /**
     * @brief 获取AI实例
     * @param creature 生物指针
     * @return AI实例指针
     */
    CreatureAI* GetAI(Creature* creature) const override
    {
        return GetStratholmeAI<npc_restless_soulAI>(creature);
    }

    /**
     * @struct npc_restless_soulAI
     * @brief 不安的灵魂AI
     *
     * @details 实现不安的灵魂的行为逻辑:
     *          - 响应伊根的爆破器法术
     *          - 被标记后在死亡时召唤解放的灵魂
     *          - 给予玩家任务击杀进度
     */
    struct npc_restless_soulAI : public ScriptedAI
    {
        /**
         * @brief 构造函数
         * @param creature 生物指针
         */
        npc_restless_soulAI(Creature* creature) : ScriptedAI(creature)
        {
            Initialize();
        }

        /**
         * @brief 初始化成员变量
         *
         * @details 设置标记状态和计时器
         */
        void Initialize()
        {
            Tagger.Clear();   // 清空标记者GUID
            Die_Timer = 5000; // 死亡计时器(5秒)
            Tagged = false;   // 是否被标记
        }

        ObjectGuid Tagger;     // 标记者(使用爆破器的玩家)GUID
        uint32 Die_Timer;      // 死亡倒计时计时器
        bool Tagged;           // 是否已被伊根的爆破器标记

        /**
         * @brief 重置AI状态
         */
        void Reset() override
        {
            Initialize();
        }

        /**
         * @brief 进入战斗(不安的灵魂不会主动战斗)
         * @param who 目标
         */
        void JustEngagedWith(Unit* /*who*/) override { }

        /**
         * @brief 法术命中处理
         * @param caster 施法者
         * @param spellInfo 法术信息
         *
         * @details 当法术命中时调用:
         *          - 检查是否为伊根的爆破器法术
         *          - 检查施法者是否有相关任务
         *          - 标记灵魂并记录标记者GUID
         */
        void SpellHit(WorldObject* caster, SpellInfo const* spellInfo) override
        {
            // 如果已标记或不是伊根的爆破器,忽略
            if (Tagged || spellInfo->Id != SPELL_EGAN_BLASTER)
                return;

            // 检查施法者是否为玩家且有相关任务
            Player* player = caster->ToPlayer();
            if (!player || player->GetQuestStatus(QUEST_RESTLESS_SOUL) != QUEST_STATUS_INCOMPLETE)
                return;

            // 标记此灵魂
            Tagged = true;
            Tagger = caster->GetGUID();
        }

        /**
         * @brief 召唤生物处理
         * @param summoned 被召唤的生物
         *
         * @details 当召唤解放的灵魂时调用:
         *          - 给解放的灵魂施放解放光环
         *          - 让解放的灵魂跟随标记者(玩家)
         */
        void JustSummoned(Creature* summoned) override
        {
            summoned->CastSpell(summoned, SPELL_SOUL_FREED, false);

            // 让解放的灵魂跟随玩家
            if (Player* player = ObjectAccessor::GetPlayer(*me, Tagger))
                summoned->GetMotionMaster()->MoveFollow(player, 0.0f, 0.0f);
        }

        /**
         * @brief 死亡处理
         * @param killer 击杀者
         *
         * @details 如果被标记,死亡时召唤解放的灵魂
         */
        void JustDied(Unit* /*killer*/) override
        {
            if (Tagged)
                me->SummonCreature(NPC_FREED, me->GetPositionX(), me->GetPositionY(), me->GetPositionZ(), me->GetOrientation(), TEMPSUMMON_TIMED_DESPAWN, 5min);
        }

        /**
         * @brief 更新AI
         * @param diff 时间差(毫秒)
         *
         * @details 如果被标记,5秒后自杀并给予玩家任务进度
         */
        void UpdateAI(uint32 diff) override
        {
            if (Tagged)
            {
                if (Die_Timer <= diff)
                {
                    // 给玩家任务击杀进度
                    if (Unit* temp = ObjectAccessor::GetUnit(*me, Tagger))
                    {
                        if (Player* player = temp->ToPlayer())
                            player->KilledMonsterCredit(NPC_RESTLESS, me->GetGUID());
                        me->KillSelf();  // 自杀
                    }
                }
                else
                    Die_Timer -= diff;
            }
        }
    };

};

/*######
## npc_spectral_ghostly_citizen
## 幽灵市民
######*/

/**
 * @brief 幽灵市民法术枚举
 */
enum GhostlyCitizenSpells
{
    SPELL_HAUNTING_PHANTOM        = 16336,  // 鬼魅幻影 - 召唤幻影攻击目标
    SPELL_DEBILITATING_TOUCH      = 16333,  // 虚弱之触 - 降低目标属性
    SPELL_SLAP                    = 6754    // 耳光 - 对玩家使用的表情反应法术
};

/**
 * @class npc_spectral_ghostly_citizen
 * @brief 幽灵市民NPC脚本
 *
 * @details 实现斯坦索姆中的幽灵市民:
 *          - 可以被伊根的爆破器净化
 *          - 被净化后死亡时可能召唤不安的灵魂
 *          - 战斗中使用鬼魅幻影和虚弱之触
 *          - 响应玩家的表情动作
 */
class npc_spectral_ghostly_citizen : public CreatureScript
{
public:
    npc_spectral_ghostly_citizen() : CreatureScript("npc_spectral_ghostly_citizen") { }

    /**
     * @brief 获取AI实例
     * @param creature 生物指针
     * @return AI实例指针
     */
    CreatureAI* GetAI(Creature* creature) const override
    {
        return GetStratholmeAI<npc_spectral_ghostly_citizenAI>(creature);
    }

    /**
     * @struct npc_spectral_ghostly_citizenAI
     * @brief 幽灵市民AI
     *
     * @details 实现幽灵市民的战斗和交互逻辑:
     *          - 被净化后在死亡时召唤不安的灵魂
     *          - 战斗中使用两种法术
     *          - 响应玩家表情
     */
    struct npc_spectral_ghostly_citizenAI : public ScriptedAI
    {
        /**
         * @brief 构造函数
         * @param creature 生物指针
         */
        npc_spectral_ghostly_citizenAI(Creature* creature) : ScriptedAI(creature)
        {
            Initialize();
        }

        /**
         * @brief 初始化成员变量
         */
        void Initialize()
        {
            Die_Timer = 5000;      // 死亡计时器(5秒)
            HauntingTimer = 8000;  // 鬼魅幻影计时器(8秒)
            TouchTimer = 2000;     // 虚弱之触计时器(2秒)
            Tagged = false;        // 是否被净化标记
        }

        uint32 Die_Timer;       // 自杀倒计时
        uint32 HauntingTimer;   // 鬼魅幻影冷却计时器
        uint32 TouchTimer;      // 虚弱之触冷却计时器
        bool Tagged;            // 是否已被伊根的爆破器标记

        /**
         * @brief 重置AI状态
         */
        void Reset() override
        {
            Initialize();
        }

        /**
         * @brief 进入战斗(无特殊逻辑)
         * @param who 目标
         */
        void JustEngagedWith(Unit* /*who*/) override { }

        /**
         * @brief 法术命中处理
         * @param caster 施法者(未使用)
         * @param spellInfo 法术信息
         *
         * @details 如果被伊根的爆破器命中,标记为已净化
         */
        void SpellHit(WorldObject* /*caster*/, SpellInfo const* spellInfo) override
        {
            if (!Tagged && spellInfo->Id == SPELL_EGAN_BLASTER)
                Tagged = true;
        }

        /**
         * @brief 死亡处理
         * @param killer 击杀者(未使用)
         *
         * @details 如果被净化,死亡时有概率召唤最多4个不安的灵魂:
         *          - 第1次召唤: 100%概率
         *          - 第2次召唤: 50%概率
         *          - 第3次召唤: 33%概率
         *          - 第4次召唤: 25%概率
         */
        void JustDied(Unit* /*killer*/) override
        {
            if (Tagged)
            {
                for (uint32 i = 1; i <= 4; ++i)
                {
                     //100%, 50%, 33%, 25% chance to spawn
                     // 100%、50%、33%、25%的概率召唤
                     if (urand(1, i) == 1)
                         DoSummon(NPC_RESTLESS, me, 20.0f, 10min);
                }
            }
        }

        /**
         * @brief 更新AI
         * @param diff 时间差(毫秒)
         *
         * @details 主循环逻辑:
         *          1. 如果被净化,5秒后自杀
         *          2. 战斗中施放鬼魅幻影(11秒CD)
         *          3. 战斗中施放虚弱之触(7秒CD)
         *          4. 执行近战攻击
         */
        void UpdateAI(uint32 diff) override
        {
            // 如果被净化,自杀计时
            if (Tagged)
            {
                if (Die_Timer <= diff)
                    me->KillSelf();
                else Die_Timer -= diff;
            }

            // 检查是否有战斗目标
            if (!UpdateVictim())
                return;

            //HauntingTimer - 鬼魅幻影
            if (HauntingTimer <= diff)
            {
                if (Unit* target = SelectTarget(SelectTargetMethod::Random, 0))
                    DoCast(target, SPELL_HAUNTING_PHANTOM);
                HauntingTimer = 11000;
            }
            else HauntingTimer -= diff;

            //TouchTimer - 虚弱之触
            if (TouchTimer <= diff)
            {
                if (Unit* target = SelectTarget(SelectTargetMethod::Random, 0))
                    DoCast(target, SPELL_DEBILITATING_TOUCH);
                TouchTimer = 7000;
            }
            else TouchTimer -= diff;

            DoMeleeAttackIfReady();
        }

        /**
         * @brief 接收表情动作
         * @param player 做表情的玩家
         * @param emote 表情ID
         *
         * @details 幽灵市民会响应玩家的表情:
         *          - 跳舞: 进入脱战状态
         *          - 粗鲁: 近距离打耳光,否则回敬粗鲁表情
         *          - 挥手: 回敬挥手
         *          - 鞠躬: 回敬鞠躬
         *          - 亲吻: 做肌肉展示表情
         */
        void ReceiveEmote(Player* player, uint32 emote) override
        {
            switch (emote)
            {
                case TEXT_EMOTE_DANCE:  // 跳舞 - 脱战
                    EnterEvadeMode();
                    break;
                case TEXT_EMOTE_RUDE:  // 粗鲁
                    if (me->IsWithinDistInMap(player, 5))
                        DoCast(player, SPELL_SLAP, false);  // 近距离打耳光
                    else
                        me->HandleEmoteCommand(EMOTE_ONESHOT_RUDE);  // 远距离回敬粗鲁
                    break;
                case TEXT_EMOTE_WAVE:  // 挥手
                    me->HandleEmoteCommand(EMOTE_ONESHOT_WAVE);
                    break;
                case TEXT_EMOTE_BOW:  // 鞠躬
                    me->HandleEmoteCommand(EMOTE_ONESHOT_BOW);
                    break;
                case TEXT_EMOTE_KISS:  // 亲吻
                    me->HandleEmoteCommand(EMOTE_ONESHOT_FLEX);  // 肌肉展示
                    break;
            }
        }
    };

};

/**
 * @class spell_ysida_saved_credit
 * @brief 伊希达获救任务完成法术脚本
 *
 * @details 法术ID: 31912 - 伊希达获救任务完成触发器
 *          处理任务"死者的恳求"(QUEST_DEAD_MAN_PLEA)的完成逻辑:
 *          - 筛选目标为玩家
 *          - 给予玩家任务完成进度
 *          - 给予玩家击杀伊希达的进度(任务需求)
 */
// 31912 - Ysida Saved Credit Trigger
class spell_ysida_saved_credit : public SpellScript
{
    PrepareSpellScript(spell_ysida_saved_credit);

    /**
     * @brief 验证法术
     * @param spell 法术信息
     * @return 验证是否成功
     */
    bool Validate(SpellInfo const* /*spell*/) override
    {
        return ValidateSpellInfo({ SPELL_YSIDA_SAVED });
    }

    /**
     * @brief 筛选目标
     * @param targets 目标列表
     *
     * @details 移除所有非玩家目标,只保留玩家
     */
    void FilterTargets(std::list<WorldObject*>& targets)
    {
        targets.remove_if([](WorldObject* obj)
        {
            return obj->GetTypeId() != TYPEID_PLAYER;
        });
    }

    /**
     * @brief 处理脚本效果
     * @param effIndex 效果索引(未使用)
     *
     * @details 给予玩家任务完成进度和击杀进度
     */
    void HandleScript(SpellEffIndex /*effIndex*/)
    {
        if (Player* player = GetHitUnit()->ToPlayer())
        {
            // 标记任务区域探索或事件完成
            player->AreaExploredOrEventHappens(QUEST_DEAD_MAN_PLEA);
            // 给予击杀伊希达的进度
            player->KilledMonsterCredit(NPC_YSIDA);
        }
    }

    /**
     * @brief 注册法术脚本钩子
     */
    void Register() override
    {
        OnObjectAreaTargetSelect += SpellObjectAreaTargetSelectFn(spell_ysida_saved_credit::FilterTargets, EFFECT_0, TARGET_UNIT_SRC_AREA_ENTRY);
        OnEffectHitTarget += SpellEffectFn(spell_ysida_saved_credit::HandleScript, EFFECT_0, SPELL_EFFECT_SCRIPT_EFFECT);
    }
};

/**
 * @brief 鬼魅幻影相关法术枚举
 */
enum HauntingPhantoms
{
    SPELL_SUMMON_SPITEFUL_PHANTOM = 16334,  // 召唤怨恨幻影
    SPELL_SUMMON_WRATH_PHANTOM    = 16335   // 召唤愤怒幻影
};

/**
 * @class spell_stratholme_haunting_phantoms
 * @brief 鬼魅幻影光环法术脚本
 *
 * @details 法术ID: 16336 - 鬼魅幻影
 *          周期性召唤幻影攻击目标:
 *          - 随机间隔(30-90秒)
 *          - 50%概率召唤怨恨幻影
 *          - 50%概率召唤愤怒幻影
 *          - 每次触发后重新计算间隔
 */
// 16336 - Haunting Phantoms
class spell_stratholme_haunting_phantoms : public AuraScript
{
    PrepareAuraScript(spell_stratholme_haunting_phantoms);

    /**
     * @brief 计算周期性触发
     * @param aurEff 光环效果
     * @param isPeriodic 是否周期性(输出参数)
     * @param amplitude 触发间隔(输出参数,毫秒)
     *
     * @details 设置光环为周期性,并随机设置触发间隔(30-90秒)
     */
    void CalcPeriodic(AuraEffect const* /*aurEff*/, bool& isPeriodic, int32& amplitude)
    {
        isPeriodic = true;
        amplitude = irand(30, 90) * IN_MILLISECONDS;
    }

    /**
     * @brief 处理周期性触发
     * @param aurEff 光环效果
     *
     * @details 周期性触发时召唤幻影:
     *          - 50%概率召唤怨恨幻影
     *          - 50%概率召唤愤怒幻影
     */
    void HandleDummyTick(AuraEffect const* /*aurEff*/)
    {
        if (roll_chance_i(50))
            GetTarget()->CastSpell(nullptr, SPELL_SUMMON_SPITEFUL_PHANTOM, true);
        else
            GetTarget()->CastSpell(nullptr, SPELL_SUMMON_WRATH_PHANTOM, true);
    }

    /**
     * @brief 更新周期性触发
     * @param aurEff 光环效果
     *
     * @details 每次触发后重新计算下次触发的间隔
     */
    void HandleUpdatePeriodic(AuraEffect* aurEff)
    {
        aurEff->CalculatePeriodic(GetCaster());
    }

    /**
     * @brief 注册光环脚本钩子
     */
    void Register() override
    {
        DoEffectCalcPeriodic += AuraEffectCalcPeriodicFn(spell_stratholme_haunting_phantoms::CalcPeriodic, EFFECT_0, SPELL_AURA_DUMMY);
        OnEffectPeriodic += AuraEffectPeriodicFn(spell_stratholme_haunting_phantoms::HandleDummyTick, EFFECT_0, SPELL_AURA_DUMMY);
        OnEffectUpdatePeriodic += AuraEffectUpdatePeriodicFn(spell_stratholme_haunting_phantoms::HandleUpdatePeriodic, EFFECT_0, SPELL_AURA_DUMMY);
    }
};

/**
 * @brief 注册脚本
 *
 * @details 将所有斯坦索姆杂项脚本注册到脚本系统:
 *          - go_gauntlet_gate: 死亡奔跑门
 *          - npc_restless_soul: 不安的灵魂
 *          - npc_spectral_ghostly_citizen: 幽灵市民
 *          - spell_ysida_saved_credit: 伊希达获救任务完成法术
 *          - spell_stratholme_haunting_phantoms: 鬼魅幻影光环法术
 */
void AddSC_stratholme()
{
    new go_gauntlet_gate();
    new npc_restless_soul();
    new npc_spectral_ghostly_citizen();
    RegisterSpellScript(spell_ysida_saved_credit);
    RegisterSpellScript(spell_stratholme_haunting_phantoms);
}
