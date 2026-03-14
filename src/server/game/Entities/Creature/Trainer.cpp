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
 * @file Trainer.cpp
 * @brief 训练师系统实现文件
 *
 * 本文件实现了训练师系统的核心功能，包括：
 * - 训练师法术列表的发送
 * - 玩家学习法术的处理
 * - 法术学习条件检查（等级、技能、前置法术等）
 * - 训练师与玩家的交互验证
 *
 * 训练师系统支持多种类型：
 * - 职业训练师（Class）：教授职业技能
 * - 坐骑训练师（Mount）：教授种族坐骑
 * - 商业技能训练师（Tradeskill）：教授专业技能
 * - 宠物训练师（Pet）：教授宠物技能
 *
 * @note 该模块与 NPC 交互、法术系统和玩家进度系统紧密关联
 */

#include "Trainer.h"
#include "Creature.h"
#include "NPCPackets.h"
#include "Player.h"
#include "SpellInfo.h"
#include "SpellMgr.h"

namespace Trainer
{
    /**
     * @brief 检查训练师法术是否需要通过施法来学习
     *
     * 判断该训练师法术是否具有学习法术效果。如果具有 SPELL_EFFECT_LEARN_SPELL 效果，
     * 则需要通过施法方式学习；否则可以直接学习。
     *
     * @return true 如果需要通过施法学习
     * @return false 如果可以直接学习
     *
     * @note 这个方法决定了 TeachSpell 中是调用 CastSpell 还是 LearnSpell
     */
    bool Spell::IsCastable() const
    {
        return sSpellMgr->AssertSpellInfo(SpellId)->HasEffect(SPELL_EFFECT_LEARN_SPELL);
    }

    /**
     * @brief Trainer 构造函数
     *
     * 初始化训练师对象，设置训练师 ID、类型、需求条件和可教授的法术列表。
     *
     * @param trainerId 训练师 ID，用于数据库关联
     * @param type 训练师类型（职业/坐骑/商业技能/宠物）
     * @param requirement 训练师需求条件，根据类型不同含义不同：
     *                    - Class/Pet: 职业 ID
     *                    - Mount: 种族 ID
     *                    - Tradeskill: 需要的法术 ID
     * @param greeting 训练师默认问候语
     * @param spells 可教授的法术列表
     *
     * @note 问候语默认存储为 DEFAULT_LOCALE，可通过 AddGreetingLocale 添加其他语言版本
     */
    Trainer::Trainer(uint32 trainerId, Type type, uint32 requirement, std::string greeting, std::vector<Spell> spells) : _trainerId(trainerId), _type(type), _requirement(requirement), _spells(std::move(spells))
    {
        _greeting[DEFAULT_LOCALE] = std::move(greeting);
    }

    /**
     * @brief 向玩家发送训练师可教授的法术列表
     *
     * 构建并发送训练师法术列表数据包，包含所有符合玩家职业和种族条件的法术。
     * 每个法术包含价格、需求等级、需求技能和可用状态等信息。
     *
     * @param npc 训练师 NPC 对象指针
     * @param player 目标玩家对象指针
     * @param locale 客户端语言区域设置，用于返回对应语言的问候语
     *
     * @note 该方法会进行以下处理：
     *       1. 计算玩家的声望折扣
     *       2. 过滤不符合玩家职业/种族的法术
     *       3. 判断是否为主专业技能第一等级（占用专业点数）
     *       4. 设置每个法术的可用状态和价格
     *
     * @performance 法术数量较多时会产生较大数据包，但通常训练师法术数量有限
     */
    void Trainer::SendSpells(Creature const* npc, Player const* player, LocaleConstant locale) const
    {
        // 获取玩家的声望折扣系数（范围 0.0-1.0）
        float reputationDiscount = player->GetReputationPriceDiscount(npc);

        WorldPackets::NPC::TrainerList trainerList;
        trainerList.TrainerGUID = npc->GetGUID();
        trainerList.TrainerType = AsUnderlyingType(_type);
        trainerList.Greeting = GetGreeting(locale);
        trainerList.Spells.reserve(_spells.size());

        // 遍历所有训练师法术，构建可显示的法术列表
        for (Spell const& trainerSpell : _spells)
        {
            // 跳过不符合玩家职业和种族要求的法术
            if (!player->IsSpellFitByClassAndRace(trainerSpell.SpellId))
                continue;

            SpellInfo const* trainerSpellInfo = sSpellMgr->AssertSpellInfo(trainerSpell.SpellId);

            // 检查是否为主专业技能的第一等级（学习时需要占用一个专业槽位）
            bool primaryProfessionFirstRank = false;
            for (SpellEffectInfo const& spellEffectInfo : trainerSpellInfo->GetEffects())
            {
                if (!spellEffectInfo.IsEffect(SPELL_EFFECT_LEARN_SPELL))
                    continue;

                SpellInfo const* learnedSpellInfo = sSpellMgr->GetSpellInfo(spellEffectInfo.TriggerSpell);
                if (learnedSpellInfo && learnedSpellInfo->IsPrimaryProfessionFirstRank())
                    primaryProfessionFirstRank = true;
            }

            // 构建法术信息结构
            trainerList.Spells.emplace_back();
            WorldPackets::NPC::TrainerListSpell& trainerListSpell = trainerList.Spells.back();
            trainerListSpell.SpellID = trainerSpell.SpellId;
            trainerListSpell.Usable = AsUnderlyingType(GetSpellState(player, &trainerSpell));
            trainerListSpell.MoneyCost = int32(trainerSpell.MoneyCost * reputationDiscount);
            trainerListSpell.PointCost[0] = 0; // 法术不消耗天赋点
            trainerListSpell.PointCost[1] = (primaryProfessionFirstRank ? 1 : 0); // 主专业技能第一等级消耗1个专业点数
            trainerListSpell.ReqLevel = trainerSpell.ReqLevel;
            trainerListSpell.ReqSkillLine = trainerSpell.ReqSkillLine;
            trainerListSpell.ReqSkillRank = trainerSpell.ReqSkillRank;
            std::copy(trainerSpell.ReqAbility.begin(), trainerSpell.ReqAbility.end(), trainerListSpell.ReqAbility.begin());
        }

        player->SendDirectMessage(trainerList.Write());
    }

    /**
     * @brief 教授玩家指定法术
     *
     * 处理玩家向训练师学习法术的完整流程，包括：
     * - 验证训练师是否适用于该玩家
     * - 验证法术是否存在于训练师法术列表中
     * - 验证玩家是否满足学习条件
     * - 扣除金币（考虑声望折扣）
     * - 执行法术学习（施法学习或直接学习）
     * - 发送学习结果通知
     *
     * @param npc 训练师 NPC 对象指针
     * @param player 学习法术的玩家对象指针
     * @param spellId 要学习的法术 ID
     *
     * @note 该方法会修改玩家状态（金币、已学法术列表）
     * @note 学习方式取决于法术是否具有 LEARN_SPELL 效果：
     *       - 有 LEARN_SPELL 效果：通过施法学习（触发相关效果）
     *       - 无 LEARN_SPELL 效果：直接学习
     *
     * @see CanTeachSpell 学习条件检查
     * @see SendTeachFailure 发送学习失败消息
     * @see SendTeachSucceeded 发送学习成功消息
     */
    void Trainer::TeachSpell(Creature const* npc, Player* player, uint32 spellId) const
    {
        // 步骤1：验证训练师是否适用于该玩家
        if (!IsTrainerValidForPlayer(player))
            return;

        // 步骤2：查找训练师是否教授该法术
        Spell const* trainerSpell = GetSpell(spellId);
        if (!trainerSpell)
        {
            SendTeachFailure(npc, player, spellId, FailReason::Unavailable);
            return;
        }

        // 步骤3：验证玩家是否满足学习条件
        if (!CanTeachSpell(player, trainerSpell))
        {
            SendTeachFailure(npc, player, spellId, FailReason::NotEnoughSkill);
            return;
        }

        // 步骤4：检查金币是否足够（应用声望折扣）
        float reputationDiscount = player->GetReputationPriceDiscount(npc);
        int32 moneyCost = int32(trainerSpell->MoneyCost * reputationDiscount);
        if (!player->HasEnoughMoney(moneyCost))
        {
            SendTeachFailure(npc, player, spellId, FailReason::NotEnoughMoney);
            return;
        }

        // 步骤5：扣除金币
        player->ModifyMoney(-moneyCost);

        // 步骤6：播放训练视觉效果
        npc->SendPlaySpellVisual(179); // 训练师施法视觉特效
        npc->SendPlaySpellImpact(player->GetGUID(), 362); // 玩家受击视觉特效

        // 步骤7：学习法术 - 根据法术类型选择学习方式
        // 如果法术具有 LEARN_SPELL 效果，需要通过施法来触发学习过程
        // 否则直接学习到玩家的法术书中
        if (trainerSpell->IsCastable())
            player->CastSpell(player, trainerSpell->SpellId, true);
        else
            player->LearnSpell(trainerSpell->SpellId, false);

        // 步骤8：发送学习成功消息
        SendTeachSucceeded(npc, player, spellId);
    }

    /**
     * @brief 根据法术 ID 获取训练师法术信息
     *
     * 在训练师的法术列表中查找指定 ID 的法术。
     *
     * @param spellId 要查找的法术 ID
     * @return Spell const* 找到则返回法术指针，未找到返回 nullptr
     *
     * @performance 使用线性查找，时间复杂度 O(n)，适用于训练师法术数量较少的场景
     */
    Spell const* Trainer::GetSpell(uint32 spellId) const
    {
        auto itr = std::find_if(_spells.begin(), _spells.end(), [spellId](Spell const& trainerSpell)
        {
            return trainerSpell.SpellId == spellId;
        });

        if (itr != _spells.end())
            return &(*itr);

        return nullptr;
    }

    /**
     * @brief 检查玩家是否可以学习指定法术
     *
     * 验证玩家是否满足学习该法术的所有条件，包括：
     * - 法术状态是否为可用（未学会且满足所有前置条件）
     * - 是否有足够的主专业技能点数（针对主专业技能第一等级）
     *
     * @param player 待检查的玩家对象指针
     * @param trainerSpell 训练师法术信息指针
     * @return true 可以学习该法术
     * @return false 不能学习该法术
     *
     * @note 该方法在 GetSpellState 检查的基础上，额外验证专业点数限制
     * @note 玩家最多只能学习两个主专业技能
     *
     * @see GetSpellState 获取法术状态
     */
    bool Trainer::CanTeachSpell(Player const* player, Spell const* trainerSpell) const
    {
        // 检查法术状态是否为可用
        SpellState state = GetSpellState(player, trainerSpell);
        if (state != SpellState::Available)
            return false;

        SpellInfo const* trainerSpellInfo = sSpellMgr->AssertSpellInfo(trainerSpell->SpellId);

        // 检查是否为主专业技能第一等级，以及玩家是否有空闲专业点数
        for (SpellEffectInfo const& spellEffectInfo : trainerSpellInfo->GetEffects())
        {
            if (!spellEffectInfo.IsEffect(SPELL_EFFECT_LEARN_SPELL))
                continue;

            SpellInfo const* learnedSpellInfo = sSpellMgr->GetSpellInfo(spellEffectInfo.TriggerSpell);
            // 如果是主专业技能第一等级，且玩家没有空闲专业点数，则无法学习
            if (learnedSpellInfo && learnedSpellInfo->IsPrimaryProfessionFirstRank() && !player->GetFreePrimaryProfessionPoints())
                return false;
        }

        return true;
    }

    /**
     * @brief 获取训练师法术对玩家的可用状态
     *
     * 全面检查玩家对该法术的学习状态和所有前置条件，返回法术的可用性状态。
     * 检查顺序（优先级从高到低）：
     * 1. 是否已学会该法术
     * 2. 是否符合职业和种族要求
     * 3. 是否满足技能要求
     * 4. 是否满足前置法术要求
     * 5. 是否满足等级要求
     * 6. 是否满足法术链前置要求（前一等级）
     * 7. 是否满足额外法术需求
     *
     * @param player 待检查的玩家对象指针
     * @param trainerSpell 训练师法术信息指针
     * @return SpellState 法术状态：
     *         - Known: 玩家已学会
     *         - Available: 可以学习
     *         - Unavailable: 不可用（不满足条件）
     *
     * @note 该方法是训练师系统的核心验证逻辑，影响客户端显示的可用性图标
     * @note 对于带有 LEARN_SPELL 效果的法术，会检查触发法术的前置条件
     *
     * @performance 包含多次法术信息查询，对于大量法术检查可能有性能影响
     */
    SpellState Trainer::GetSpellState(Player const* player, Spell const* trainerSpell) const
    {
        // 检查1：玩家是否已学会该法术
        if (player->HasSpell(trainerSpell->SpellId))
            return SpellState::Known;

        // 检查2：是否符合职业和种族要求
        if (!player->IsSpellFitByClassAndRace(trainerSpell->SpellId))
            return SpellState::Unavailable;

        // 检查3：是否满足技能等级要求
        if (trainerSpell->ReqSkillLine && player->GetBaseSkillValue(trainerSpell->ReqSkillLine) < trainerSpell->ReqSkillRank)
            return SpellState::Unavailable;

        // 检查4：是否满足前置能力要求
        for (int32 reqAbility : trainerSpell->ReqAbility)
            if (reqAbility && !player->HasSpell(reqAbility))
                return SpellState::Unavailable;

        // 检查5：是否满足等级要求
        if (player->GetLevel() < trainerSpell->ReqLevel)
            return SpellState::Unavailable;

        // 检查6：检查法术链前置要求（是否已学会前一等级）
        bool hasLearnSpellEffect = false;
        bool knowsAllLearnedSpells = true;
        for (SpellEffectInfo const& spellEffectInfo : sSpellMgr->AssertSpellInfo(trainerSpell->SpellId)->GetEffects())
        {
            if (!spellEffectInfo.IsEffect(SPELL_EFFECT_LEARN_SPELL))
                continue;

            hasLearnSpellEffect = true;
            // 检查是否已学会该效果触发的法术
            if (!player->HasSpell(spellEffectInfo.TriggerSpell))
                knowsAllLearnedSpells = false;

            // 检查触发法术的前置法术链
            if (uint32 previousRankSpellId = sSpellMgr->GetPrevSpellInChain(spellEffectInfo.TriggerSpell))
                if (!player->HasSpell(previousRankSpellId))
                    return SpellState::Unavailable;
        }

        // 对于没有 LEARN_SPELL 效果的法术，检查自身的前置法术链
        if (!hasLearnSpellEffect)
        {
            if (uint32 previousRankSpellId = sSpellMgr->GetPrevSpellInChain(trainerSpell->SpellId))
                if (!player->HasSpell(previousRankSpellId))
                    return SpellState::Unavailable;
        }
        // 对于带有 LEARN_SPELL 效果的法术，如果所有触发的法术都已学会，则标记为已知
        else if (knowsAllLearnedSpells)
            return SpellState::Known;

        // 检查7：是否满足额外法术需求（由数据库定义的复杂前置条件）
        for (auto const& requirePair : sSpellMgr->GetSpellsRequiredForSpellBounds(trainerSpell->SpellId))
            if (!player->HasSpell(requirePair.second))
                return SpellState::Unavailable;

        return SpellState::Available;
    }

    /**
     * @brief 验证训练师是否适用于该玩家
     *
     * 根据训练师类型检查玩家是否满足使用该训练师的基本条件。
     * 不同类型的训练师有不同的验证规则：
     * - Class/Pet: 玩家职业必须匹配
     * - Mount: 玩家种族必须匹配
     * - Tradeskill: 玩家必须已学会指定法术
     *
     * @param player 待验证的玩家对象指针
     * @return true 训练师适用于该玩家
     * @return false 训练师不适用于该玩家
     *
     * @note 如果训练师没有设置需求条件（requirement=0），则对所有玩家有效
     */
    bool Trainer::IsTrainerValidForPlayer(Player const* player) const
    {
        // 如果没有设置需求条件，则对所有玩家有效
        if (!GetTrainerRequirement())
            return true;

        switch (GetTrainerType())
        {
            case Type::Class:
            case Type::Pet:
                // Class/Pet 训练师：检查玩家职业是否匹配
                return player->GetClass() == GetTrainerRequirement();
            case Type::Mount:
                // Mount 训练师：检查玩家种族是否匹配
                return player->GetRace() == GetTrainerRequirement();
            case Type::Tradeskill:
                // Tradeskill 训练师：检查玩家是否已学会指定法术
                return player->HasSpell(GetTrainerRequirement());
            default:
                break;
        }

        return true;
    }

    /**
     * @brief 发送法术学习失败消息给玩家
     *
     * 构建并发送训练师购买失败数据包，通知客户端学习失败的原因。
     *
     * @param npc 训练师 NPC 对象指针
     * @param player 目标玩家对象指针
     * @param spellId 学习失败的法术 ID
     * @param reason 失败原因枚举值：
     *               - Unavailable: 法术不可用
     *               - NotEnoughMoney: 金币不足
     *               - NotEnoughSkill: 技能等级不足
     *
     * @note 该方法仅发送失败通知，不做任何状态修改
     */
    void Trainer::SendTeachFailure(Creature const* npc, Player const* player, uint32 spellId, FailReason reason) const
    {
        WorldPackets::NPC::TrainerBuyFailed trainerBuyFailed;
        trainerBuyFailed.TrainerGUID = npc->GetGUID();
        trainerBuyFailed.SpellID = spellId;
        trainerBuyFailed.TrainerFailedReason = AsUnderlyingType(reason);
        player->SendDirectMessage(trainerBuyFailed.Write());
    }

    /**
     * @brief 发送法术学习成功消息给玩家
     *
     * 构建并发送训练师购买成功数据包，通知客户端法术学习成功。
     *
     * @param npc 训练师 NPC 对象指针
     * @param player 目标玩家对象指针
     * @param spellId 学习成功的法术 ID
     *
     * @note 该方法在法术成功学习后调用，触发客户端更新法术书
     */
    void Trainer::SendTeachSucceeded(Creature const* npc, Player const* player, uint32 spellId) const
    {
        WorldPackets::NPC::TrainerBuySucceeded trainerBuySucceeded;
        trainerBuySucceeded.TrainerGUID = npc->GetGUID();
        trainerBuySucceeded.SpellID = spellId;
        player->SendDirectMessage(trainerBuySucceeded.Write());
    }

    /**
     * @brief 获取指定语言的问候语
     *
     * 返回训练师在指定语言下的问候语。如果该语言版本不存在，则返回默认语言版本。
     *
     * @param locale 请求的语言区域设置
     * @return std::string const& 问候语字符串的常量引用
     *
     * @note 保证至少返回 DEFAULT_LOCALE 版本的问候语
     */
    std::string const& Trainer::GetGreeting(LocaleConstant locale) const
    {
        // 如果请求的语言版本不存在，回退到默认语言
        if (_greeting[locale].empty())
            return _greeting[DEFAULT_LOCALE];

        return _greeting[locale];
    }

    /**
     * @brief 添加指定语言的问候语
     *
     * 为训练师添加或更新指定语言版本的问候语。支持多语言本地化。
     *
     * @param locale 目标语言区域设置
     * @param greeting 该语言的问候语文本
     *
     * @note 问候语存储在固定大小的数组中，通过 locale 作为索引
     * @note 可以多次调用以支持多种语言
     */
    void Trainer::AddGreetingLocale(LocaleConstant locale, std::string greeting)
    {
        _greeting[locale] = std::move(greeting);
    }
}
