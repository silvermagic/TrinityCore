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
 * @file    sunken_temple.cpp
 * @brief   沉没的神庙副本辅助脚本
 * @details 实现沉没的神庙副本中的辅助功能，包括：
 *          - 区域触发器脚本（玛法里奥·怒风的召唤）
 *          - 游戏对象脚本（阿塔莱雕像谜题交互）
 *          - 法术脚本（贾玛兰的妖术效果）
 *
 *          主要功能：
 *          1. at_malfurion_stormrage: 区域触发器，在特定任务条件下召唤玛法里奥·怒风 NPC
 *          2. go_atalai_statue: 处理雕像谜题的交互逻辑
 *          3. spell_sunken_temple_hex_of_jammalan: 处理妖术法术的移除效果
 *          4. spell_sunken_temple_hex_of_jammalan_transform: 处理妖术变形效果
 *
 * ScriptData
 * SDName: Sunken_Temple
 * SD%Complete: 100
 * SDComment: Area Trigger + Puzzle event support
 * SDCategory: Sunken Temple
 * EndScriptData
 *
 * ContentData
 * at_malfurion_Stormrage_trigger
 * EndContentData
 */

#include "ScriptMgr.h"
#include "GameObject.h"
#include "GameObjectAI.h"
#include "InstanceScript.h"
#include "Map.h"
#include "Player.h"
#include "ScriptedCreature.h"
#include "SpellAuraEffects.h"
#include "SpellScript.h"
#include "sunken_temple.h"

/*#####
# at_malfurion_Stormrage_trigger
#####*/

/**
 * @enum MalfurionMisc
 * @brief 玛法里奥相关常量枚举
 * @details 定义玛法里奥·怒风 NPC 和相关任务的 ID。
 */
enum MalfurionMisc
{
    NPC_MALFURION_STORMRAGE           = 15362,  ///< 玛法里奥·怒风 NPC ID
    QUEST_ERANIKUS_TYRANT_OF_DREAMS   = 8733,   ///< 任务：伊兰尼库斯，梦境暴君
    QUEST_THE_CHARGE_OF_DRAGONFLIGHTS = 8555,   ///< 任务：巨龙军团的冲锋
};

/**
 * @class at_malfurion_stormrage
 * @brief 玛法里奥·怒风区域触发器脚本
 * @details 当玩家进入特定区域时触发，召唤玛法里奥·怒风 NPC。
 *          这是一个任务相关的区域触发器，用于任务链：
 *          "巨龙军团的冲锋" -> "伊兰尼库斯，梦境暴君"
 *
 * 触发条件：
 * 1. 玩家所在地图有实例脚本
 * 2. 玩家附近 15 码内没有玛法里奥·怒风（避免重复召唤）
 * 3. 玩家已完成任务 "巨龙军团的冲锋"
 * 4. 玩家未完成任务 "伊兰尼库斯，梦境暴君"
 *
 * 召唤细节：
 * - 召唤位置：玩家当前位置
 * - 面向角度：-1.52 弧度（约 -87 度，面向西方）
 * - 存在时间：100 秒后消失，或死亡后立即消失
 */
class at_malfurion_stormrage : public AreaTriggerScript
{
    public:
        at_malfurion_stormrage() : AreaTriggerScript("at_malfurion_stormrage") { }

        /**
         * @brief 区域触发器触发事件
         * @param player 进入区域的玩家指针
         * @param at 区域触发器数据（本脚本中未使用）
         * @return false 表示不阻止其他触发器的处理
         * @details 当玩家进入区域触发器范围时调用。
         *          检查任务条件并召唤玛法里奥·怒风。
         */
        bool OnTrigger(Player* player, AreaTriggerEntry const* /*at*/) override
        {
            // 检查玩家是否在副本中、附近没有玛法里奥、已完成前置任务且未完成当前任务
            if (player->GetInstanceScript() && !player->FindNearestCreature(NPC_MALFURION_STORMRAGE, 15.0f) &&
                player->GetQuestStatus(QUEST_THE_CHARGE_OF_DRAGONFLIGHTS) == QUEST_STATUS_REWARDED && player->GetQuestStatus(QUEST_ERANIKUS_TYRANT_OF_DREAMS) != QUEST_STATUS_REWARDED)
            {
                // 召唤玛法里奥·怒风，在玩家当前位置，面向西方
                player->SummonCreature(NPC_MALFURION_STORMRAGE, player->GetPositionX(), player->GetPositionY(), player->GetPositionZ(), -1.52f, TEMPSUMMON_TIMED_OR_DEAD_DESPAWN, 100s);
            }
            return false;
        }
};

/*#####
# go_atalai_statue
#####*/

/**
 * @class go_atalai_statue
 * @brief 阿塔莱雕像游戏对象脚本
 * @details 处理阿塔莱雕像的交互逻辑，用于雕像谜题机制。
 *          玩家必须按特定顺序点击 6 个雕像才能召唤 Boss 阿塔拉里恩。
 *
 * 谜题机制：
 * - 雕像必须按顺序激活（1->2->3->4->5->6）
 * - 点击雕像时，通知实例脚本当前激活的是哪个雕像
 * - 实例脚本负责验证激活顺序是否正确
 * - 正确顺序激活最后一个雕像后，召唤 Boss 阿塔拉里恩
 */
class go_atalai_statue : public GameObjectScript
{
    public:
        go_atalai_statue() : GameObjectScript("go_atalai_statue") { }

        /**
         * @struct go_atalai_statueAI
         * @brief 阿塔莱雕像 AI 实现
         * @details 管理雕像的交互逻辑。
         */
        struct go_atalai_statueAI : public GameObjectAI
        {
            /**
             * @brief 构造函数
             * @param go 雕像游戏对象指针
             * @details 初始化 AI 并获取实例脚本引用。
             */
            go_atalai_statueAI(GameObject* go) : GameObjectAI(go), instance(go->GetInstanceScript()) { }

            InstanceScript* instance;  ///< 副本实例脚本指针

            /**
             * @brief 玩家与雕像交互事件
             * @param player 与雕像交互的玩家指针（本脚本中未使用）
             * @return false 表示允许继续处理默认交互逻辑
             * @details 当玩家点击雕像时调用。
             *          将雕像的 ID 通知实例脚本，由实例脚本处理激活逻辑。
             * @note 雕像 ID（GetEntry()）对应 GO_ATALAI_STATUE1 到 GO_ATALAI_STATUE6
             */
            bool OnGossipHello(Player* /*player*/) override
            {
                // 通知实例脚本当前点击的雕像 ID
                // 实例脚本会在 Update 函数中验证激活顺序
                instance->SetData(EVENT_STATE, me->GetEntry());
                return false;
            }
        };

        /**
         * @brief 获取雕像 AI 实例
         * @param go 雕像游戏对象指针
         * @return 返回新创建的 AI 实例
         * @details 使用 GetSunkenTempleAI 模板函数确保 AI 只在沉没的神庙副本中创建。
         */
        GameObjectAI* GetAI(GameObject* go) const override
        {
            return GetSunkenTempleAI<go_atalai_statueAI>(go);
        }
};

/**
 * @enum HexOfJammalan
 * @brief 贾玛兰妖术相关法术枚举
 * @details 定义先知贾玛兰使用的妖术法术链中的法术 ID。
 */
enum HexOfJammalan
{
    SPELL_HEX_OF_JAMMALAN_TRANSFORM    = 12480,  ///< 妖术变形 - 将目标变形为青蛙
    SPELL_HEX_OF_JAMMALAN_CHARM        = 12483   ///< 妖术魅惑 - 魅惑被变形的目标
};

/**
 * @class spell_sunken_temple_hex_of_jammalan
 * @brief 贾玛兰妖术光环脚本（法术 ID: 12479）
 * @details 处理先知贾玛兰的妖术法术的主要效果。
 *
 * 法术机制：
 * 1. 施放妖术法术（12479）到目标
 * 2. 妖术持续一段时间（由法术数据决定）
 * 3. 当妖术效果自然到期（非驱散/消失）时：
 *    - 对目标施放变形法术（12480），将目标变形为青蛙
 *    - 对目标施放魅惑法术（12483），使目标受施法者控制
 *
 * 光环移除时的处理逻辑：
 * - 自然到期（AURA_REMOVE_BY_EXPIRE）：触发变形和魅惑
 * - 其他方式移除（驱散、消失等）：不触发额外效果
 *
 * @note 此法术是 Boss 先知贾玛兰的核心技能之一
 */
// 12479 - Hex of Jammal'an
class spell_sunken_temple_hex_of_jammalan : public AuraScript
{
    PrepareAuraScript(spell_sunken_temple_hex_of_jammalan);

    /**
     * @brief 验证法术依赖
     * @param spellInfo 法术信息（本脚本中未使用）
     * @return true 如果所有依赖法术存在，false 如果缺少依赖法术
     * @details 验证变形和魅惑法术是否存在于数据库中。
     */
    bool Validate(SpellInfo const* /*spellInfo*/) override
    {
        return ValidateSpellInfo({ SPELL_HEX_OF_JAMMALAN_TRANSFORM, SPELL_HEX_OF_JAMMALAN_CHARM });
    }

    /**
     * @brief 光环移除后处理
     * @param aurEff 光环效果指针（本脚本中未使用）
     * @param mode 光环效果处理模式（本脚本中未使用）
     * @details 当妖术光环被移除时调用。
     *          只在光环自然到期时触发变形和魅惑效果。
     */
    void AfterRemove(AuraEffect const* /*aurEff*/, AuraEffectHandleModes /*mode*/)
    {
        // 只处理自然到期的情况，驱散等其他移除方式不触发效果
        if (GetTargetApplication()->GetRemoveMode() != AURA_REMOVE_BY_EXPIRE)
            return;

        Unit* target = GetTarget();   // 妖术目标
        Unit* caster = GetCaster();   // 施法者（先知贾玛兰）

        // 确保施法者存在且存活
        if (!caster || !caster->IsAlive())
            return;

        // 施放变形法术，将目标变形为青蛙
        caster->CastSpell(target, SPELL_HEX_OF_JAMMALAN_TRANSFORM, true);
        // 施放魅惑法术，控制目标
        caster->CastSpell(target, SPELL_HEX_OF_JAMMALAN_CHARM, true);
    }

    /**
     * @brief 注册光环效果处理函数
     * @details 注册 AfterRemove 处理函数到光环移除事件。
     */
    void Register() override
    {
        AfterEffectRemove += AuraEffectRemoveFn(spell_sunken_temple_hex_of_jammalan::AfterRemove, EFFECT_0, SPELL_AURA_DUMMY, AURA_EFFECT_HANDLE_REAL);
    }
};

/**
 * @class spell_sunken_temple_hex_of_jammalan_transform
 * @brief 贾玛兰妖术变形光环脚本（法术 ID: 12480）
 * @details 处理妖术变形效果的清除逻辑。
 *
 * 法术机制：
 * - 变形光环使目标变为青蛙形态
 * - 当变形效果结束时（无论何种原因），自动移除相关的魅惑效果
 * - 这确保了变形和魅惑效果同步结束，避免玩家在变回人形后仍被魅惑
 *
 * @note 此脚本与 spell_sunken_temple_hex_of_jammalan 配合使用
 */
// 12480 - Hex of Jammal'an
class spell_sunken_temple_hex_of_jammalan_transform : public AuraScript
{
    PrepareAuraScript(spell_sunken_temple_hex_of_jammalan_transform);

    /**
     * @brief 验证法术依赖
     * @param spellInfo 法术信息（本脚本中未使用）
     * @return true 如果依赖法术存在，false 如果缺少依赖法术
     * @details 验证魅惑法术是否存在于数据库中。
     */
    bool Validate(SpellInfo const* /*spellInfo*/) override
    {
        return ValidateSpellInfo({ SPELL_HEX_OF_JAMMALAN_CHARM });
    }

    /**
     * @brief 变形效果移除后处理
     * @param aurEff 光环效果指针（本脚本中未使用）
     * @param mode 光环效果处理模式（本脚本中未使用）
     * @details 当变形光环被移除时调用。
     *          移除所有相关的魅惑效果，确保变形结束后玩家恢复自由。
     */
    void AfterRemove(AuraEffect const* /*aurEff*/, AuraEffectHandleModes /*mode*/)
    {
        // 移除妖术魅惑效果
        GetTarget()->RemoveAurasDueToSpell(SPELL_HEX_OF_JAMMALAN_CHARM);
    }

    /**
     * @brief 注册光环效果处理函数
     * @details 注册 AfterRemove 处理函数到变形光环移除事件。
     */
    void Register() override
    {
        AfterEffectRemove += AuraEffectRemoveFn(spell_sunken_temple_hex_of_jammalan_transform::AfterRemove, EFFECT_0, SPELL_AURA_TRANSFORM, AURA_EFFECT_HANDLE_REAL);
    }
};

/**
 * @brief 注册沉没的神庙副本辅助脚本
 * @details 此函数由脚本加载器在服务器启动时调用。
 *          注册以下脚本：
 *          1. at_malfurion_stormrage: 玛法里奥·怒风区域触发器
 *          2. go_atalai_statue: 阿塔莱雕像游戏对象脚本
 *          3. spell_sunken_temple_hex_of_jammalan: 贾玛兰妖术法术脚本
 *          4. spell_sunken_temple_hex_of_jammalan_transform: 妖术变形法术脚本
 */
void AddSC_sunken_temple()
{
    new at_malfurion_stormrage();                     // 注册区域触发器脚本
    new go_atalai_statue();                           // 注册雕像游戏对象脚本
    RegisterSpellScript(spell_sunken_temple_hex_of_jammalan);          // 注册妖术法术脚本
    RegisterSpellScript(spell_sunken_temple_hex_of_jammalan_transform); // 注册妖术变形法术脚本
}
