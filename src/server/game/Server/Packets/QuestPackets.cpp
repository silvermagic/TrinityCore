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
 * @file QuestPackets.cpp
 * @brief 任务系统网络数据包实现
 *
 * 本文件实现了任务系统相关网络数据包的序列化和反序列化功能,包括:
 * - 读取客户端任务查询请求
 * - 序列化任务信息响应数据
 * - 序列化任务详情显示数据
 * - 序列化任务奖励消息数据
 *
 * 这些数据包的实现遵循魔兽世界3.3.5版本的协议规范,
 * 负责将任务数据在服务器和客户端之间正确传输。
 */

#include "QuestPackets.h"

/**
 * @brief 读取客户端发送的任务查询请求
 *
 * 从客户端发送的网络包中读取任务ID,用于后续查询任务详细信息。
 * 这是一个简单的读取操作,仅包含一个uint32的任务ID字段。
 */
void WorldPackets::Quest::QueryQuestInfo::Read()
{
    _worldPacket >> QuestID;
}

/**
 * @brief 序列化任务信息响应数据到网络包
 * @return 返回序列化完成的世界包指针
 *
 * 将QuestInfo结构体中的所有任务信息按照客户端协议格式序列化到网络包。
 * 这是一个复杂的序列化过程,包含多个数据段:
 * 1. 基本信息:任务ID、等级、类型等
 * 2. 阵营要求:联盟/部落的阵营和声望要求
 * 3. 奖励信息:经验、金钱、物品、法术、荣誉等
 * 4. POI信息:任务位置点的地图坐标
 * 5. 文本信息:标题、描述、目标等字符串
 * 6. 目标信息:需要击杀的NPC、收集的物品等
 *
 * @note 如果任务设置了QUEST_FLAGS_HIDDEN_REWARDS标志,则隐藏奖励物品和金钱信息
 * @note GameObject的ID需要特殊处理,客户端期望格式为(id | 0x80000000)
 */
WorldPacket const* WorldPackets::Quest::QueryQuestInfoResponse::Write()
{
    // 写入任务基本属性
    _worldPacket << uint32(Info.QuestID);
    _worldPacket << uint32(Info.QuestMethod);
    _worldPacket << uint32(Info.QuestLevel);
    _worldPacket << uint32(Info.QuestMinLevel);
    _worldPacket << uint32(Info.QuestSortID);

    _worldPacket << uint32(Info.QuestType);
    _worldPacket << uint32(Info.SuggestedGroupNum);

    // 写入阵营要求信息(联盟和部落)
    for (uint8 i = 0; i < PVP_TEAMS_COUNT; ++i)
    {
        _worldPacket << uint32(Info.RequiredFactionId[i]);
        _worldPacket << uint32(Info.RequiredFactionValue[i]);
    }

    // 写入后续任务和奖励经验
    _worldPacket << uint32(Info.RewardNextQuest);
    _worldPacket << uint32(Info.RewardXPDifficulty);

    // 处理隐藏奖励标志:如果设置了隐藏奖励,则发送0而不是实际金钱
    if ((Info.Flags & QUEST_FLAGS_HIDDEN_REWARDS) != 0)
        _worldPacket << uint32(0);
    else
        _worldPacket << uint32(Info.RewardMoney);

    // 写入其他奖励信息
    _worldPacket << uint32(Info.RewardBonusMoney);
    _worldPacket << uint32(Info.RewardDisplaySpell);
    _worldPacket << int32(Info.RewardSpell);

    _worldPacket << uint32(Info.RewardHonor);
    _worldPacket << float(Info.RewardKillHonor);
    _worldPacket << uint32(Info.StartItem);
    _worldPacket << uint32(Info.Flags & 0xFFFF); // 只发送低16位标志
    _worldPacket << uint32(Info.RewardTitleId);
    _worldPacket << uint32(Info.RequiredPlayerKills);
    _worldPacket << uint32(Info.RewardTalents);
    _worldPacket << uint32(Info.RewardArenaPoints);
    _worldPacket << uint32(Info.RewardFactionFlags);

    // 处理物品奖励:如果设置了隐藏奖励,则发送全0数据
    if ((Info.Flags & QUEST_FLAGS_HIDDEN_REWARDS) != 0)
    {
        // 隐藏固定奖励物品
        for (uint8 i = 0; i < QUEST_REWARDS_COUNT; ++i)
            _worldPacket << uint32(0) << uint32(0);
        // 隐藏可选择奖励物品
        for (uint8 i = 0; i < QUEST_REWARD_CHOICES_COUNT; ++i)
            _worldPacket << uint32(0) << uint32(0);
    }
    else
    {
        // 写入固定奖励物品(物品ID和数量)
        for (uint8 i = 0; i < QUEST_REWARDS_COUNT; ++i)
        {
            _worldPacket << uint32(Info.RewardItems[i]);
            _worldPacket << uint32(Info.RewardAmount[i]);
        }
        // 写入可选择奖励物品(物品ID和数量)
        for (uint8 i = 0; i < QUEST_REWARD_CHOICES_COUNT; ++i)
        {
            _worldPacket << uint32(Info.UnfilteredChoiceItems[i].ItemID);
            _worldPacket << uint32(Info.UnfilteredChoiceItems[i].Quantity);
        }
    }

    // 写入声望奖励信息
    for (uint8 i = 0; i < QUEST_REPUTATIONS_COUNT; ++i)             // 奖励声望阵营ID
        _worldPacket << uint32(Info.RewardFactionID[i]);

    for (uint8 i = 0; i < QUEST_REPUTATIONS_COUNT; ++i)             // 声望值(columnid+1 QuestFactionReward.dbc?)
        _worldPacket << int32(Info.RewardFactionValue[i]);

    for (uint8 i = 0; i < QUEST_REPUTATIONS_COUNT; ++i)             // 声望值覆盖(未知用途,通常为0)
        _worldPacket << int32(Info.RewardFactionValueOverride[i]);

    // 写入任务POI(兴趣点)信息,用于在地图上显示任务位置
    _worldPacket << uint32(Info.POIContinent);
    _worldPacket << float(Info.POIx);
    _worldPacket << float(Info.POIy);
    _worldPacket << uint32(Info.POIPriority);

    // 写入任务文本信息(标题、目标、详情、区域描述、完成文本)
    _worldPacket << Info.Title;
    _worldPacket << Info.Objectives;
    _worldPacket << Info.Details;
    _worldPacket << Info.AreaDescription;
    _worldPacket << Info.CompletedText;

    // 写入任务目标信息(NPC/GameObject击杀或交互目标)
    for (uint8 i = 0; i < QUEST_OBJECTIVES_COUNT; ++i)
    {
        // GameObject ID需要特殊处理:客户端期望格式为(id | 0x80000000)
        // 这是因为Creature和GameObject共享同一个字段,需要区分
        if (Info.RequiredNpcOrGo[i] < 0)
            _worldPacket << uint32((Info.RequiredNpcOrGo[i] * (-1)) | 0x80000000);    // GameObject ID设置最高位
        else
            _worldPacket << uint32(Info.RequiredNpcOrGo[i]);                          // Creature ID正常发送

        _worldPacket << uint32(Info.RequiredNpcOrGoCount[i]); // 需要击杀/交互的数量

        _worldPacket << uint32(Info.ItemDrop[i]);             // 相关的任务物品掉落ID
        _worldPacket << uint32(0);                            // 需要来源数量?(协议中存在但未使用)
    }

    // 写入物品收集目标(需要收集的物品ID和数量)
    for (uint8 i = 0; i < QUEST_ITEM_OBJECTIVES_COUNT; ++i)
    {
        _worldPacket << uint32(Info.RequiredItemId[i]);
        _worldPacket << uint32(Info.RequiredItemCount[i]);
    }

    // 写入自定义目标文本(用于特殊任务目标描述)
    for (uint8 i = 0; i < QUEST_OBJECTIVES_COUNT; ++i)
        _worldPacket << Info.ObjectiveText[i];

    return &_worldPacket;
}

/**
 * @brief 序列化任务详情数据到网络包
 * @return 返回序列化完成的世界包指针
 *
 * 将任务给予者的任务详情序列化到网络包,用于在客户端显示任务详情界面。
 * 序列化内容包括:
 * 1. 任务给予者和通知单位的GUID
 * 2. 任务基本信息(标题、详情、目标)
 * 3. 任务标志和建议组队人数
 * 4. 奖励物品列表(可选择奖励和固定奖励)
 * 5. 奖励数据(金钱、经验、荣誉、法术等)
 * 6. 声望奖励信息
 * 7. NPC表情序列
 *
 * @note 奖励物品使用动态数组,可以包含任意数量的物品
 * @note 表情序列用于NPC在展示任务时的动态表现
 */
WorldPacket const* WorldPackets::Quest::QuestGiverQuestDetails::Write()
{
    // 写入任务给予者和通知单位的GUID
    _worldPacket << QuestGiverGUID;
    _worldPacket << InformUnit;
    _worldPacket << uint32(QuestID);

    // 写入任务文本信息
    _worldPacket << Title;
    _worldPacket << Details;
    _worldPacket << Objectives;

    // 写入任务标志
    _worldPacket << uint8(AutoLaunched);
    _worldPacket << uint32(Flags);
    _worldPacket << uint32(SuggestedGroupNum);
    _worldPacket << uint8(StartCheat);

    // 写入可选择奖励物品列表(玩家从中选择其一)
    _worldPacket << uint32(Rewards.UnfilteredChoiceItems.size());
    for (WorldPackets::Quest::QuestChoiceItem const& item : Rewards.UnfilteredChoiceItems)
    {
        _worldPacket << uint32(item.ItemID);
        _worldPacket << uint32(item.Quantity);
        _worldPacket << uint32(item.DisplayID);
    }

    // 写入固定奖励物品列表(玩家必定获得)
    _worldPacket << uint32(Rewards.RewardItems.size());
    for (WorldPackets::Quest::QuestChoiceItem const& item : Rewards.RewardItems)
    {
        _worldPacket << uint32(item.ItemID);
        _worldPacket << uint32(item.Quantity);
        _worldPacket << uint32(item.DisplayID);
    }

    // 写入其他奖励信息(金钱、经验、荣誉、法术、天赋等)
    _worldPacket << uint32(Rewards.RewardMoney);
    _worldPacket << uint32(Rewards.RewardXPDifficulty);
    _worldPacket << uint32(Rewards.RewardHonor);
    _worldPacket << float(Rewards.RewardKillHonor);
    _worldPacket << uint32(Rewards.RewardDisplaySpell);
    _worldPacket << int32(Rewards.RewardSpell);
    _worldPacket << uint32(Rewards.RewardTitleId);
    _worldPacket << uint32(Rewards.RewardTalents);
    _worldPacket << uint32(Rewards.RewardArenaPoints);
    _worldPacket << uint32(Rewards.RewardFactionFlags);

    // 写入声望奖励信息
    for (uint32 factionId : Rewards.RewardFactionID)
        _worldPacket << uint32(factionId);

    for (int32 value : Rewards.RewardFactionValue)
        _worldPacket << int32(value);

    for (int32 valueOverride : Rewards.RewardFactionValueOverride)
        _worldPacket << int32(valueOverride);

    // 写入NPC表情序列(用于任务对话时的动画表现)
    _worldPacket << int32(DescEmotes.size());
    for (QuestDescEmote const& emote : DescEmotes)
    {
        _worldPacket << uint32(emote.Type);
        _worldPacket << uint32(emote.Delay);
    }

    return &_worldPacket;
}

/**
 * @brief 序列化任务奖励消息数据到网络包
 * @return 返回序列化完成的世界包指针
 *
 * 将任务完成后的奖励信息序列化到网络包,用于在客户端显示任务完成界面。
 * 序列化内容包括:
 * 1. 任务给予者的GUID和任务ID
 * 2. 任务标题和奖励文本
 * 3. 任务标志和建议组队人数
 * 4. NPC表情序列(奖励时的动画表现)
 * 5. 奖励物品列表(可选择奖励和固定奖励)
 * 6. 奖励数据(金钱、经验、荣誉等)
 * 7. 声望奖励信息
 *
 * @note 表情数据的字段顺序与QuestGiverQuestDetails不同,Delay和Type的顺序相反
 * @note 存在一个未知字段(0),在协议中读取但未使用
 */
WorldPacket const* WorldPackets::Quest::QuestGiverOfferRewardMessage::Write()
{
    // 写入任务给予者GUID和任务ID
    _worldPacket << QuestGiverGUID;
    _worldPacket << uint32(QuestID);

    // 写入任务标题和奖励文本
    _worldPacket << Title;
    _worldPacket << RewardText;

    // 写入任务标志
    _worldPacket << uint8(AutoLaunched);
    _worldPacket << uint32(Flags);
    _worldPacket << uint32(SuggestedGroupNum);

    // 写入NPC表情序列(注意:这里Delay和Type的顺序与QuestGiverQuestDetails相反)
    _worldPacket << uint32(Emotes.size());
    for (WorldPackets::Quest::QuestDescEmote const& emote : Emotes)
    {
        _worldPacket << uint32(emote.Delay);
        _worldPacket << uint32(emote.Type);
    }

    // 写入可选择奖励物品列表
    _worldPacket << uint32(Rewards.UnfilteredChoiceItems.size());
    for (WorldPackets::Quest::QuestChoiceItem const& item : Rewards.UnfilteredChoiceItems)
    {
        _worldPacket << uint32(item.ItemID);
        _worldPacket << uint32(item.Quantity);
        _worldPacket << uint32(item.DisplayID);
    }

    // 写入固定奖励物品列表
    _worldPacket << uint32(Rewards.RewardItems.size());
    for (WorldPackets::Quest::QuestChoiceItem const& item : Rewards.RewardItems)
    {
        _worldPacket << uint32(item.ItemID);
        _worldPacket << uint32(item.Quantity);
        _worldPacket << uint32(item.DisplayID);
    }

    // 写入其他奖励信息
    _worldPacket << uint32(Rewards.RewardMoney);
    _worldPacket << uint32(Rewards.RewardXPDifficulty);

    _worldPacket << uint32(Rewards.RewardHonor);
    _worldPacket << float(Rewards.RewardKillHonor);
    _worldPacket << uint32(0); // 未知值,在包处理器中读取但未使用
    _worldPacket << uint32(Rewards.RewardDisplaySpell);
    _worldPacket << int32(Rewards.RewardSpell);
    _worldPacket << uint32(Rewards.RewardTitleId);
    _worldPacket << uint32(Rewards.RewardTalents);
    _worldPacket << uint32(Rewards.RewardArenaPoints);
    _worldPacket << uint32(Rewards.RewardFactionFlags);

    // 写入声望奖励信息
    for (uint32 factionId : Rewards.RewardFactionID)
        _worldPacket << uint32(factionId);

    for (uint32 value : Rewards.RewardFactionValue)
        _worldPacket << int32(value);

    for (uint32 valueOverride : Rewards.RewardFactionValueOverride)
        _worldPacket << int32(valueOverride);

    return &_worldPacket;
}
