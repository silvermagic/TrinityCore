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

#include "QuestDef.h"
#include "DatabaseEnv.h"
#include "DBCStores.h"
#include "Log.h"
#include "ObjectMgr.h"
#include "Opcodes.h"
#include "Player.h"
#include "QuestPackets.h"
#include "QuestPools.h"
#include "World.h"

/**
 * @brief Quest类构造函数 - 从数据库记录初始化任务数据
 *
 * @param questRecord 数据库查询结果字段数组，包含任务模板表的所有字段
 *
 * @details 该构造函数负责从 quest_template 表的数据库记录中解析并初始化所有任务属性
 *          主要流程：
 *          1. 加载基本任务属性（ID、等级、类型、标志等）
 *          2. 加载阵营和声望要求
 *          3. 加载奖励信息（经验、金钱、物品、法术等）
 *          4. 加载任务目标和需求物品
 *          5. 加载任务文本（标题、目标、详情等）
 *          6. 统计有效的奖励物品和需求物品数量
 */
Quest::Quest(Field* questRecord)
{
    // 加载任务基本信息
    _id = questRecord[0].GetUInt32();                    // 任务ID
    _method = questRecord[1].GetUInt8();                 // 任务获取方式（0=自动完成，1=自动接取，2=普通）
    _level = questRecord[2].GetInt16();                  // 任务等级（-1表示玩家等级）
    _minLevel = questRecord[3].GetUInt8();               // 接取任务最低等级
    _zoneOrSort = questRecord[4].GetInt16();             // 区域或分类ID
    _type = questRecord[5].GetUInt16();                  // 任务类型（普通、精英、副本等）
    _suggestedPlayers = questRecord[6].GetUInt8();       // 建议玩家数量
    _timeAllowed = questRecord[7].GetUInt32();           // 限时任务的时间限制（分钟）
    _allowableRaces = questRecord[8].GetUInt16();        // 允许的种族掩码

    // 加载阵营和声望要求
    _requiredFactionId1 = questRecord[9].GetUInt16();    // 要求阵营1的ID
    _requiredFactionId2 = questRecord[10].GetUInt16();   // 要求阵营2的ID
    _requiredFactionValue1 = questRecord[11].GetInt32(); // 要求阵营1的声望值
    _requiredFactionValue2 = questRecord[12].GetInt32(); // 要求阵营2的声望值

    // 加载奖励信息
    _rewardNextQuest = questRecord[13].GetUInt32();      // 下一个任务ID
    _rewardXPDifficulty = questRecord[14].GetUInt8();    // 经验奖励难度等级
    _rewardMoney = questRecord[15].GetInt32();           // 金钱奖励（负数表示需要支付）
    _rewardBonusMoney = questRecord[16].GetUInt32();     // 满级时的额外金钱奖励
    _rewardDisplaySpell = questRecord[17].GetUInt32();   // 显示的法术奖励（用于图标显示）
    _rewardSpell = questRecord[18].GetInt32();           // 实际施放的法术奖励ID
    _rewardHonor = questRecord[19].GetUInt32();          // 荣誉点数奖励
    _rewardKillHonor = questRecord[20].GetFloat();       // 击杀荣誉乘数
    _startItem = questRecord[21].GetUInt32();            // 开始任务时给予的物品
    _flags = questRecord[22].GetUInt32();                // 任务标志位
    _rewardTitleId = questRecord[23].GetUInt8();         // 奖励的头衔ID
    _requiredPlayerKills = questRecord[24].GetUInt8();   // 需要击杀的玩家数量
    _rewardTalents = questRecord[25].GetUInt8();         // 奖励的天赋点数
    _rewardArenaPoints = questRecord[26].GetUInt16();    // 奖励的竞技场点数

    // 加载固定奖励物品（最多4个）
    for (uint32 i = 0; i < QUEST_REWARDS_COUNT; ++i)
    {
        RewardItemId[i] = questRecord[27 + i * 2].GetUInt32();      // 物品ID
        RewardItemIdCount[i] = questRecord[28 + i * 2].GetUInt16(); // 物品数量

        // 统计有效奖励物品数量
        if (RewardItemId[i])
            ++_rewItemsCount;
    }

    // 加载可选奖励物品（最多6个）
    for (uint32 i = 0; i < QUEST_REWARD_CHOICES_COUNT; ++i)
    {
        RewardChoiceItemId[i] = questRecord[35 + i * 2].GetUInt32();      // 物品ID
        RewardChoiceItemCount[i] = questRecord[36 + i * 2].GetUInt16();   // 物品数量

        // 统计有效可选奖励物品数量
        if (RewardChoiceItemId[i])
            ++_rewChoiceItemsCount;
    }

    // 加载阵营奖励（最多5个）
    for (uint32 i = 0; i < QUEST_REPUTATIONS_COUNT; ++i)
    {
        RewardFactionId[i] = questRecord[47 + i * 3].GetUInt16();             // 阵营ID
        RewardFactionValueId[i] = questRecord[48 + i * 3].GetInt32();         // 声望值变化
        RewardFactionValueIdOverride[i] = questRecord[49 + i * 3].GetInt32(); // 声望值覆盖
    }

    // 加载POI（兴趣点）信息
    _poiContinent = questRecord[62].GetUInt16();         // 地图ID
    _poiX = questRecord[63].GetFloat();                  // X坐标
    _poiY = questRecord[64].GetFloat();                  // Y坐标
    _poiPriority = questRecord[65].GetUInt32();          // 优先级

    // 加载任务文本信息
    _title = questRecord[66].GetString();                // 任务标题
    _objectives = questRecord[67].GetString();           // 任务目标描述
    _details = questRecord[68].GetString();              // 任务详情文本
    _areaDescription = questRecord[69].GetString();      // 区域描述
    _completedText = questRecord[70].GetString();        // 完成文本

    // 加载NPC/游戏对象击杀目标（最多4个）
    for (uint32 i = 0; i < QUEST_OBJECTIVES_COUNT; ++i)
    {
        RequiredNpcOrGo[i] = questRecord[71+i].GetInt32();       // NPC或GO的ID（正数NPC，负数GO）
        RequiredNpcOrGoCount[i] = questRecord[75+i].GetUInt16(); // 需要击杀/交互的数量
        ObjectiveText[i] = questRecord[100+i].GetString();       // 目标描述文本

        // 统计有效目标数量
        if (RequiredNpcOrGo[i])
            ++_reqCreatureOrGOcount;
    }

    // 加载任务物品来源信息（最多4个）
    for (uint32 i = 0; i < QUEST_SOURCE_ITEM_IDS_COUNT; ++i)
    {
        ItemDrop[i] = questRecord[79+i].GetUInt32();          // 掉落物品的物品ID
        ItemDropQuantity[i] = questRecord[83+i].GetUInt16();  // 掉落数量
    }

    // 加载需要的物品目标（最多6个）
    for (uint32 i = 0; i < QUEST_ITEM_OBJECTIVES_COUNT; ++i)
    {
        RequiredItemId[i] = questRecord[87+i].GetUInt32();    // 需要的物品ID
        RequiredItemCount[i] = questRecord[93+i].GetUInt16(); // 需要的物品数量

        // 统计有效需求物品数量
        if (RequiredItemId[i])
            ++_reqItemsCount;
    }

    // int8 Unknown0 = questRecord[99].GetUInt8();
    // int32 VerifiedBuild = questRecord[104].GetInt32();
}

/**
 * @brief 加载任务详情的表情动作数据
 *
 * @param fields 数据库字段数组，来自 quest_details 表
 *
 * @details 从 quest_details 表加载任务接取时NPC的表情动作和延迟时间
 *          主要流程：
 *          1. 验证并加载4个表情动作ID（Emote1-4）
 *          2. 加载对应的表情动作延迟时间
 *          3. 如果表情ID无效，记录错误日志并跳过
 */
void Quest::LoadQuestDetails(Field* fields)
{
    // 加载4个表情动作ID，验证每个表情是否存在
    for (int i = 0; i < QUEST_EMOTE_COUNT; ++i)
    {
        if (!sEmotesStore.LookupEntry(fields[1+i].GetUInt16()))
        {
            TC_LOG_ERROR("sql.sql", "Table `quest_details` has non-existing Emote{} ({}) set for quest {}. Skipped.", 1+i, fields[1+i].GetUInt16(), fields[0].GetUInt32());
            continue;
        }

        DetailsEmote[i] = fields[1+i].GetUInt16();
    }

    // 加载4个表情动作的延迟时间
    for (int i = 0; i < QUEST_EMOTE_COUNT; ++i)
        DetailsEmoteDelay[i] = fields[5+i].GetUInt32();
}

/**
 * @brief 加载任务请求物品时的表情和文本数据
 *
 * @param fields 数据库字段数组，来自 quest_request_items 表
 *
 * @details 从 quest_request_items 表加载玩家与NPC交互提交物品时的数据
 *          主要流程：
 *          1. 加载任务完成时的表情动作ID
 *          2. 加载任务未完成时的表情动作ID
 *          3. 验证表情ID是否有效
 *          4. 加载请求物品的提示文本
 */
void Quest::LoadQuestRequestItems(Field* fields)
{
    _emoteOnComplete = fields[1].GetUInt16();    // 任务完成时NPC的表情
    _emoteOnIncomplete = fields[2].GetUInt16();  // 任务未完成时NPC的表情

    // 验证完成时的表情ID是否有效
    if (!sEmotesStore.LookupEntry(_emoteOnComplete))
        TC_LOG_ERROR("sql.sql", "Table `quest_request_items` has non-existing EmoteOnComplete ({}) set for quest {}.", _emoteOnComplete, fields[0].GetUInt32());

    // 验证未完成时的表情ID是否有效
    if (!sEmotesStore.LookupEntry(_emoteOnIncomplete))
        TC_LOG_ERROR("sql.sql", "Table `quest_request_items` has non-existing EmoteOnIncomplete ({}) set for quest {}.", _emoteOnIncomplete, fields[0].GetUInt32());

    _requestItemsText = fields[3].GetString();   // 请求物品时的文本提示
}

/**
 * @brief 加载任务奖励时的表情和文本数据
 *
 * @param fields 数据库字段数组，来自 quest_offer_reward 表
 *
 * @details 从 quest_offer_reward 表加载玩家提交任务获得奖励时的数据
 *          主要流程：
 *          1. 验证并加载4个奖励时的表情动作ID
 *          2. 加载对应的表情动作延迟时间
 *          3. 加载奖励提示文本
 */
void Quest::LoadQuestOfferReward(Field* fields)
{
    // 加载4个奖励表情动作ID，验证每个表情是否存在
    for (uint32 i = 0; i < QUEST_EMOTE_COUNT; ++i)
    {
        if (!sEmotesStore.LookupEntry(fields[1 + i].GetUInt16()))
        {
            TC_LOG_ERROR("sql.sql", "Table `quest_offer_reward` has non-existing Emote{} ({}) set for quest {}. Skipped.", 1 + i, fields[1 + i].GetUInt16(), fields[0].GetUInt32());
            continue;
        }

        OfferRewardEmote[i] = fields[1 + i].GetUInt16();
    }

    // 加载4个表情动作的延迟时间
    for (uint32 i = 0; i < QUEST_EMOTE_COUNT; ++i)
        OfferRewardEmoteDelay[i] = fields[5 + i].GetUInt32();

    _offerRewardText = fields[9].GetString();   // 奖励时的文本提示
}

/**
 * @brief 加载任务模板扩展数据
 *
 * @param fields 数据库字段数组，来自 quest_template_addon 表
 *
 * @details 从 quest_template_addon 表加载任务的扩展属性
 *          主要流程：
 *          1. 加载等级限制和职业要求
 *          2. 加载前置任务和后续任务链信息
 *          3. 加载技能和声望要求
 *          4. 加载邮件奖励相关信息
 *          5. 处理特殊标志，如自动接取标志
 */
void Quest::LoadQuestTemplateAddon(Field* fields)
{
    _maxLevel = fields[1].GetUInt8();                   // 接取任务最大等级
    _requiredClasses = fields[2].GetUInt32();           // 要求的职业掩码
    _sourceSpellid = fields[3].GetUInt32();             // 触发任务的法术ID
    _prevQuestId = fields[4].GetInt32();                // 前置任务ID（负数表示需要完成，正数表示需要激活）
    _nextQuestId = fields[5].GetUInt32();               // 下一个任务ID
    _exclusiveGroup = fields[6].GetInt32();             // 互斥组ID（同组任务只能完成一个）
    _breadcrumbForQuestId = fields[7].GetInt32();       // 引导任务ID
    _rewardMailTemplateId = fields[8].GetUInt32();      // 奖励邮件模板ID
    _rewardMailDelay = fields[9].GetUInt32();           // 奖励邮件延迟时间（秒）
    _requiredSkillId = fields[10].GetUInt16();          // 要求的技能ID
    _requiredSkillPoints = fields[11].GetUInt16();      // 要求的技能点数
    _requiredMinRepFaction = fields[12].GetUInt16();    // 要求的最低声望阵营ID
    _requiredMaxRepFaction = fields[13].GetUInt16();    // 要求的最高声望阵营ID
    _requiredMinRepValue = fields[14].GetInt32();       // 要求的最低声望值
    _requiredMaxRepValue = fields[15].GetInt32();       // 要求的最高声望值
    _startItemCount = fields[16].GetUInt8();            // 开始物品数量
    _specialFlags = fields[17].GetUInt8();              // 特殊标志

    // 如果特殊标志包含自动接取，则设置对应的任务标志
    if (_specialFlags & QUEST_SPECIAL_FLAGS_AUTO_ACCEPT)
        _flags |= QUEST_FLAGS_AUTO_ACCEPT;
}

/**
 * @brief 加载任务邮件发送者信息
 *
 * @param fields 数据库字段数组，来自 quest_mail_sender 表
 *
 * @details 加载任务完成奖励邮件的发送者信息
 *          主要流程：设置奖励邮件的发送者实体ID
 */
void Quest::LoadQuestMailSender(Field* fields)
{
    _rewardMailSenderEntry = fields[1].GetUInt32();   // 邮件发送者的NPC或实体ID
}

/**
 * @brief 计算任务的经验值奖励
 *
 * @param player 玩家对象指针，用于获取玩家等级
 * @return uint32 计算后的经验值奖励
 *
 * @details 根据玩家等级和任务难度计算实际的经验奖励
 *          主要流程：
 *          1. 确定任务等级（如果任务等级为-1则使用玩家等级）
 *          2. 从XP表中查找基础经验值
 *          3. 计算等级差异因子（任务等级与玩家等级的差异）
 *          4. 应用等级差异因子调整经验值
 *          5. 确保经验值不低于最小缩放比例
 *          6. 返回四舍五入后的经验值
 */
uint32 Quest::GetXPReward(Player const* player) const
{
    if (player)
    {
        // 确定任务等级：-1表示使用玩家当前等级
        int32 quest_level = (_level == -1 ? player->GetLevel() : _level);

        // 从XP表中查找对应等级的基础经验值
        QuestXPEntry const* xpentry = sQuestXPStore.LookupEntry(quest_level);
        if (!xpentry)
            return 0;

        // 计算等级差异因子（范围1-10）
        // 公式：2 * (任务等级 - 玩家等级) + 20
        // 等级差越大，因子越小，奖励越少
        int32 diffFactor = 2 * (quest_level - player->GetLevel()) + 20;
        if (diffFactor < 1)
            diffFactor = 1;
        else if (diffFactor > 10)
            diffFactor = 10;

        // 计算基础经验值并四舍五入
        uint32 xp = RoundXPValue(diffFactor * xpentry->Difficulty[_rewardXPDifficulty] / 10);

        // 如果配置了最小经验缩放比例，确保经验值不低于最小值
        if (sWorld->getIntConfig(CONFIG_MIN_QUEST_SCALED_XP_RATIO))
        {
            uint32 minScaledXP = RoundXPValue(xpentry->Difficulty[_rewardXPDifficulty]) * sWorld->getIntConfig(CONFIG_MIN_QUEST_SCALED_XP_RATIO) / 100;
            xp = std::max(minScaledXP, xp);
        }

        return xp;
    }

    return 0;
}

/**
 * @brief 检查任务是否可以被接取
 *
 * @param questId 任务ID
 * @return true 任务可以接取
 * @return false 任务不可接取
 *
 * @details 静态方法，检查任务池管理器中该任务是否处于激活状态
 *          主要流程：检查任务池中任务的激活状态
 */
/*static*/ bool Quest::IsTakingQuestEnabled(uint32 questId)
{
    if (!sQuestPoolMgr->IsQuestActive(questId))
        return false;

    return true;
}

/**
 * @brief 构建任务奖励数据包
 *
 * @param rewards 输出参数，奖励数据结构
 * @param player 玩家对象指针，用于计算玩家相关的奖励
 * @param sendHiddenRewards 是否发送隐藏的奖励
 *
 * @details 构建发送给客户端的任务奖励信息
 *          主要流程：
 *          1. 检查是否隐藏奖励，如果不隐藏或强制显示则继续
 *          2. 添加可选奖励物品列表（包含显示ID）
 *          3. 添加固定奖励物品列表（包含显示ID）
 *          4. 计算金钱和经验奖励
 *          5. 设置荣誉、法术、头衔、天赋、竞技场点数等奖励
 *          6. 设置阵营声望奖励
 */
void Quest::BuildQuestRewards(WorldPackets::Quest::QuestRewards& rewards, Player* player, bool sendHiddenRewards) const
{
    // 检查是否需要隐藏奖励，如果设置了隐藏标志且不强制显示，则跳过物品奖励
    if (!HasFlag(QUEST_FLAGS_HIDDEN_REWARDS) || sendHiddenRewards)
    {
        // 构建可选奖励物品列表（玩家可以从中选择一个）
        for (uint32 i = 0; i < QUEST_REWARD_CHOICES_COUNT; ++i)
        {
            if (!RewardChoiceItemId[i])
                continue;

            // 获取物品的显示ID用于客户端展示
            uint32 displayID = 0;
            if (ItemTemplate const* itemTemplate = sObjectMgr->GetItemTemplate(RewardChoiceItemId[i]))
                displayID = itemTemplate->DisplayInfoID;

            rewards.UnfilteredChoiceItems.emplace_back(RewardChoiceItemId[i], RewardChoiceItemCount[i], displayID);
        }

        // 构建固定奖励物品列表（玩家获得所有）
        for (uint32 i = 0; i < QUEST_REWARDS_COUNT; ++i)
        {
            if (!RewardItemId[i])
                continue;

            // 获取物品的显示ID用于客户端展示
            uint32 displayID = 0;
            if (ItemTemplate const* itemTemplate = sObjectMgr->GetItemTemplate(RewardItemId[i]))
                displayID = itemTemplate->DisplayInfoID;

            rewards.RewardItems.emplace_back(RewardItemId[i], RewardItemIdCount[i], displayID);
        }

        // 设置金钱奖励（考虑玩家等级和倍率）
        rewards.RewardMoney = GetRewOrReqMoney(player);
        // 设置经验奖励（应用服务器倍率）
        rewards.RewardXPDifficulty = GetXPReward(player) * sWorld->getRate(RATE_XP_QUEST);
    }

    // 设置荣誉奖励（乘以10以满足客户端格式要求）
    rewards.RewardHonor = 10 * CalculateHonorGain(player->GetQuestLevel(this));
    // 设置显示的法术奖励（用于图标显示，如果没有施放法术则施放此法术）
    rewards.RewardDisplaySpell = GetRewSpell();
    // 设置实际施放的法术奖励
    rewards.RewardSpell = GetRewSpellCast();
    // 设置奖励的头衔ID
    rewards.RewardTitleId = GetCharTitleId();
    // 设置奖励的天赋点数
    rewards.RewardTalents = GetBonusTalents();
    // 设置奖励的竞技场点数
    rewards.RewardArenaPoints = GetRewArenaPoints();

    // 设置阵营声望奖励ID
    for (uint32 i = 0; i < QUEST_REPUTATIONS_COUNT; ++i)
        rewards.RewardFactionID[i] = RewardFactionId[i];

    // 设置阵营声望值变化
    for (uint32 i = 0; i < QUEST_REPUTATIONS_COUNT; ++i)
        rewards.RewardFactionValue[i] = RewardFactionValueId[i];

    // 设置阵营声望值覆盖
    for (uint32 i = 0; i < QUEST_REPUTATIONS_COUNT; ++i)
        rewards.RewardFactionValueOverride[i] = RewardFactionValueIdOverride[i];
}

/**
 * @brief 获取任务需要的金钱或奖励的金钱
 *
 * @param player 玩家对象指针，用于判断是否满级
 * @return int32 正数表示奖励的金钱，负数表示需要支付的金钱
 *
 * @details 根据任务配置和玩家状态计算金钱相关数值
 *          主要流程：
 *          1. 如果金钱值为负数，表示需要玩家支付的金钱（任务需求）
 *          2. 如果玩家不存在或未满级，返回普通金钱奖励（应用倍率）
 *          3. 如果玩家已满级，返回普通金钱和额外金钱中的较大值
 */
int32 Quest::GetRewOrReqMoney(Player const* player) const
{
    // 如果是负数，表示需要玩家支付的金钱（任务需求）
    if (_rewardMoney < 0)
        return _rewardMoney;

    // 正数表示奖励金钱
    if (!player || !player->IsMaxLevel())
    {
        // 玩家未满级，返回普通金钱奖励（应用金钱倍率）
        return int32(_rewardMoney * sWorld->getRate(RATE_MONEY_QUEST));
    }
    else
    {
        // 玩家已满级，返回普通金钱和额外金钱中的较大值（满级时金钱替代经验）
        return std::max(int32(GetRewMoneyMaxLevel()), int32(_rewardMoney * sWorld->getRate(RATE_MONEY_QUEST)));
    }
}

/**
 * @brief 获取满级玩家的额外金钱奖励
 *
 * @return uint32 满级时奖励的铜币数量
 *
 * @details 计算玩家达到等级上限时的额外金钱奖励
 *          主要流程：
 *          1. 检查任务是否有满级不给金钱的标志
 *          2. 返回额外金钱奖励（应用满级金钱倍率）
 */
uint32 Quest::GetRewMoneyMaxLevel() const
{
    // 如果任务设置了满级不给金钱的标志，返回0
    if (HasFlag(QUEST_FLAGS_NO_MONEY_FROM_XP))
        return 0;

    // 返回额外金钱奖励（应用满级任务金钱倍率）
    return uint32(_rewardBonusMoney * sWorld->getRate(RATE_MONEY_MAX_LEVEL_QUEST));
}

/**
 * @brief 检查任务是否可以自动接取
 *
 * @return true 可以自动接取
 * @return false 不能自动接取
 *
 * @details 检查任务是否具有自动接取属性
 *          主要流程：检查服务器配置是否忽略自动接取，以及任务是否有自动接取标志
 */
bool Quest::IsAutoAccept() const
{
    return !sWorld->getBoolConfig(CONFIG_QUEST_IGNORE_AUTO_ACCEPT) && HasFlag(QUEST_FLAGS_AUTO_ACCEPT);
}

/**
 * @brief 检查任务是否可以自动完成
 *
 * @return true 可以自动完成
 * @return false 不能自动完成
 *
 * @details 检查任务是否具有自动完成属性
 *          主要流程：检查服务器配置是否忽略自动完成，以及任务方法或标志是否支持自动完成
 */
bool Quest::IsAutoComplete() const
{
    return !sWorld->getBoolConfig(CONFIG_QUEST_IGNORE_AUTO_COMPLETE) && (_method == 0 || HasFlag(QUEST_FLAGS_AUTOCOMPLETE));
}

/**
 * @brief 检查任务是否为团队副本任务
 *
 * @param difficulty 难度设置
 * @return true 是团队副本任务
 * @return false 不是团队副本任务
 *
 * @details 根据任务类型和难度判断是否为团队副本任务
 *          主要流程：
 *          1. 根据任务类型判断（QUEST_TYPE_RAID系列）
 *          2. 对于10人/25人特定类型，结合难度判断
 *          3. 检查任务标志中是否有团队标志
 */
bool Quest::IsRaidQuest(Difficulty difficulty) const
{
    switch (_type)
    {
        case QUEST_TYPE_RAID:       // 通用团队副本任务
            return true;
        case QUEST_TYPE_RAID_10:    // 10人团队副本任务
            return !(difficulty & RAID_DIFFICULTY_MASK_25MAN);
        case QUEST_TYPE_RAID_25:    // 25人团队副本任务
            return difficulty & RAID_DIFFICULTY_MASK_25MAN;
        default:
            break;
    }

    // 检查任务标志中的团队标志
    if ((_flags & QUEST_FLAGS_RAID) != 0)
        return true;

    return false;
}

/**
 * @brief 检查任务是否允许在团队中完成
 *
 * @param difficulty 难度设置
 * @return true 允许在团队中完成
 * @return false 不允许在团队中完成
 *
 * @details 判断任务是否可以在团队状态下进行
 *          主要流程：
 *          1. 如果是团队副本任务，允许在团队中完成
 *          2. 否则检查服务器配置是否忽略团队限制
 */
bool Quest::IsAllowedInRaid(Difficulty difficulty) const
{
    if (IsRaidQuest(difficulty))
        return true;

    return sWorld->getBoolConfig(CONFIG_QUEST_IGNORE_RAID);
}

/**
 * @brief 计算任务的荣誉奖励
 *
 * @param level 玩家等级
 * @return uint32 荣誉点数
 *
 * @details 根据玩家等级和任务配置计算荣誉奖励
 *          主要流程：
 *          1. 限制等级不超过最大等级
 *          2. 从TeamContributionPoints表中查找基础荣誉值
 *          3. 应用荣誉乘数和额外荣誉加成
 */
uint32 Quest::CalculateHonorGain(uint8 level) const
{
    // 限制等级不超过游戏最大等级
    if (level > GT_MAX_LEVEL)
        level = GT_MAX_LEVEL;

    uint32 honor = 0;

    if (GetRewHonorAddition() > 0 || GetRewHonorMultiplier() > 0.0f)
    {
        // 从表中查找对应等级的团队贡献点数（存储的是等级-1的数据）
        TeamContributionPointsEntry const* tc = sTeamContributionPointsStore.LookupEntry(level);
        if (!tc)
            return 0;

        // 计算荣誉：基础值 * 荣誉乘数 * 0.1 + 额外荣誉
        honor = uint32(tc->Data * GetRewHonorMultiplier() * 0.1f);
        honor += GetRewHonorAddition();
    }

    return honor;
}

/**
 * @brief 检查任务是否可以增加已完成任务计数器
 *
 * @return true 可以增加计数器
 * @return false 不能增加计数器
 *
 * @details 判断任务完成后是否应该计入玩家的已完成任务统计
 *          主要流程：排除地下城查找器任务、日常任务、非周/月/季节性的可重复任务
 *          这些任务不会被视为服务器端的已完成任务，影响计数器和客户端的已完成任务请求
 */
bool Quest::CanIncreaseRewardedQuestCounters() const
{
    // 地下城查找器/日常/可重复（非周/月/季节性）任务不被视为服务器端的已完成任务
    // 这影响计数器和客户端对已完成任务的请求
    return (!IsDFQuest() && !IsDaily() && (!IsRepeatable() || IsWeekly() || IsMonthly() || IsSeasonal()));
}

/**
 * @brief 初始化所有语言的任务查询数据
 *
 * @details 为所有支持的语言环境预先构建任务查询数据包
 *          主要流程：遍历所有语言环境，为每种语言构建查询响应数据
 */
void Quest::InitializeQueryData()
{
    for (uint8 loc = LOCALE_enUS; loc < TOTAL_LOCALES; ++loc)
        QueryData[loc] = BuildQueryData(static_cast<LocaleConstant>(loc));
}

/**
 * @brief 构建任务查询数据包
 *
 * @param loc 语言环境常量
 * @return WorldPacket 任务查询响应数据包
 *
 * @details 构建指定语言环境的任务详细信息数据包，用于响应客户端的任务查询请求
 *          主要流程：
 *          1. 获取默认（英文）文本
 *          2. 如果存在本地化文本，则覆盖为对应语言的文本
 *          3. 填充任务的基本信息（ID、等级、类型等）
 *          4. 填充任务奖励信息（物品、金钱、经验、荣誉等）
 *          5. 填充任务目标信息（NPC、物品、目标文本等）
 *          6. 构建并返回数据包
 */
WorldPacket Quest::BuildQueryData(LocaleConstant loc) const
{
    WorldPackets::Quest::QueryQuestInfoResponse response;

    // 获取默认文本（英文）
    std::string locQuestTitle = GetTitle();
    std::string locQuestDetails = GetDetails();
    std::string locQuestObjectives = GetObjectives();
    std::string locQuestAreaDescription = GetAreaDescription();
    std::string locQuestCompletedText = GetCompletedText();

    // 获取默认的目标文本
    std::string locQuestObjectiveText[QUEST_OBJECTIVES_COUNT];
    for (uint8 i = 0; i < QUEST_OBJECTIVES_COUNT; ++i)
        locQuestObjectiveText[i] = ObjectiveText[i];

    // 如果存在本地化数据，则用本地化文本覆盖默认文本
    if (QuestLocale const* localeData = sObjectMgr->GetQuestLocale(GetQuestId()))
    {
        ObjectMgr::GetLocaleString(localeData->Title, loc, locQuestTitle);
        ObjectMgr::GetLocaleString(localeData->Details, loc, locQuestDetails);
        ObjectMgr::GetLocaleString(localeData->Objectives, loc, locQuestObjectives);
        ObjectMgr::GetLocaleString(localeData->AreaDescription, loc, locQuestAreaDescription);
        ObjectMgr::GetLocaleString(localeData->CompletedText, loc, locQuestCompletedText);

        for (uint8 i = 0; i < QUEST_OBJECTIVES_COUNT; ++i)
            ObjectMgr::GetLocaleString(localeData->ObjectiveText[i], loc, locQuestObjectiveText[i]);
    }

    // 填充任务基本信息
    response.Info.QuestID = GetQuestId();
    response.Info.QuestMethod = GetQuestMethod();
    response.Info.QuestLevel = GetQuestLevel();
    response.Info.QuestMinLevel = GetMinLevel();
    response.Info.QuestSortID = GetZoneOrSort();

    response.Info.QuestType = GetType();
    response.Info.SuggestedGroupNum = GetSuggestedPlayers();

    // 填充阵营要求信息
    response.Info.RequiredFactionId[0] = GetRepObjectiveFaction();
    response.Info.RequiredFactionValue[0] = GetRepObjectiveValue();

    response.Info.RequiredFactionId[1] = GetRepObjectiveFaction2();
    response.Info.RequiredFactionValue[1] = GetRepObjectiveValue2();

    // 填充奖励信息
    response.Info.RewardNextQuest = GetNextQuestInChain();
    response.Info.RewardXPDifficulty = GetXPId();

    response.Info.RewardMoney = GetRewOrReqMoney();
    response.Info.RewardBonusMoney = GetRewMoneyMaxLevel();
    response.Info.RewardDisplaySpell = GetRewSpell();
    response.Info.RewardSpell = GetRewSpellCast();

    response.Info.RewardHonor = GetRewHonorAddition();
    response.Info.RewardKillHonor = GetRewHonorMultiplier();

    response.Info.StartItem = GetSrcItemId();
    response.Info.Flags = GetFlags();
    response.Info.RewardTitleId = GetCharTitleId();
    response.Info.RequiredPlayerKills = GetPlayersSlain();
    response.Info.RewardTalents = GetBonusTalents();
    response.Info.RewardArenaPoints = GetRewArenaPoints();

    // 填充固定奖励物品
    for (uint8 i = 0; i < QUEST_REWARDS_COUNT; ++i)
    {
        response.Info.RewardItems[i] = RewardItemId[i];
        response.Info.RewardAmount[i] = RewardItemIdCount[i];
    }

    // 填充可选奖励物品
    for (uint8 i = 0; i < QUEST_REWARD_CHOICES_COUNT; ++i)
    {
        response.Info.UnfilteredChoiceItems[i].ItemID = RewardChoiceItemId[i];
        response.Info.UnfilteredChoiceItems[i].Quantity = RewardChoiceItemCount[i];
    }

    // 填充阵营声望奖励
    for (uint8 i = 0; i < QUEST_REPUTATIONS_COUNT; ++i)             // 奖励阵营ID
        response.Info.RewardFactionID[i] = RewardFactionId[i];

    for (uint8 i = 0; i < QUEST_REPUTATIONS_COUNT; ++i)             // 声望值变化
        response.Info.RewardFactionValue[i] = RewardFactionValueId[i];

    for (uint8 i = 0; i < QUEST_REPUTATIONS_COUNT; ++i)             // 声望值覆盖（未知用途，通常为0）
        response.Info.RewardFactionValueOverride[i] = RewardFactionValueIdOverride[i];

    // 填充POI（兴趣点）信息
    response.Info.POIContinent = GetPOIContinent();
    response.Info.POIx = GetPOIx();
    response.Info.POIy = GetPOIy();
    response.Info.POIPriority = GetPointOpt();

    // 填充本地化文本
    response.Info.Title = locQuestTitle;
    response.Info.Objectives = locQuestObjectives;
    response.Info.Details = locQuestDetails;
    response.Info.AreaDescription = locQuestAreaDescription;
    response.Info.CompletedText = locQuestCompletedText;

    // 填充任务目标信息（NPC/游戏对象击杀和物品来源）
    for (uint8 i = 0; i < QUEST_OBJECTIVES_COUNT; ++i)
    {
        response.Info.RequiredNpcOrGo[i] = RequiredNpcOrGo[i];
        response.Info.RequiredNpcOrGoCount[i] = RequiredNpcOrGoCount[i];
        response.Info.ItemDrop[i] = ItemDrop[i];
    }

    // 填充需求物品信息
    for (uint8 i = 0; i < QUEST_ITEM_OBJECTIVES_COUNT; ++i)
    {
        response.Info.RequiredItemId[i] = RequiredItemId[i];
        response.Info.RequiredItemCount[i] = RequiredItemCount[i];
    }

    // 填充本地化的目标文本
    for (uint8 i = 0; i < QUEST_OBJECTIVES_COUNT; ++i)
        response.Info.ObjectiveText[i] = locQuestObjectiveText[i];

    response.Write();
    response.ShrinkToFit();
    return response.Move();
}

/**
 * @brief 在任务标题前添加任务等级
 *
 * @param title 任务标题字符串引用（会被修改）
 * @param level 要添加的等级值
 *
 * @details 在任务标题前添加格式化的等级标记
 *          主要流程：将等级值格式化为 "[等级] 标题" 的形式
 *          示例：输入 "Westfall Stew" 和等级 13，输出 "[13] Westfall Stew"
 */
void Quest::AddQuestLevelToTitle(std::string &title, int32 level)
{
    // 在任务标题前添加等级标记
    // 示例：[13] Westfall Stew

    std::stringstream questTitlePretty;
    questTitlePretty << "[" << level << "] " << title;
    title = questTitlePretty.str();
}

/**
 * @brief 将经验值四舍五入到最接近的整数
 *
 * @param xp 原始经验值
 * @return uint32 四舍五入后的经验值
 *
 * @details 根据经验值大小进行不同精度的四舍五入
 *          主要流程：
 *          1. 0-100：四舍五入到最近的5
 *          2. 101-500：四舍五入到最近的10
 *          3. 501-1000：四舍五入到最近的25
 *          4. 1001+：四舍五入到最近的50
 */
uint32 Quest::RoundXPValue(uint32 xp)
{
    if (xp <= 100)
        return 5 * ((xp + 2) / 5);
    else if (xp <= 500)
        return 10 * ((xp + 5) / 10);
    else if (xp <= 1000)
        return 25 * ((xp + 12) / 25);
    else
        return 50 * ((xp + 25) / 50);
}
