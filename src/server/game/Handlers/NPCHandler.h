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
 * @file NPCHandler.h
 * @brief NPC交互处理模块头文件
 *
 * @职责 定义NPC相关的数据结构,包括:
 *       - NPC对话文本选项结构
 *       - NPC表情动作数据
 *       - 本地化文本存储结构
 *       这些数据结构用于NPC对话系统、任务系统和商城系统等
 */

#ifndef __NPCHANDLER_H
#define __NPCHANDLER_H

/**
 * @struct QEmote
 * @brief 任务表情数据结构
 *
 * @职责 存储任务相关的表情动作信息
 *       用于任务完成或对话时的表情播放
 */
struct QEmote
{
    uint32 _Emote;   ///< 表情ID (Emotes.dbc中的表情类型)
    uint32 _Delay;   ///< 表情延迟时间(毫秒),表示播放下一个表情前的等待时间
};

/// 每个对话选项最大表情数量
#define MAX_GOSSIP_TEXT_EMOTES 3

/**
 * @struct GossipTextOption
 * @brief NPC对话文本选项结构
 *
 * @职责 存储NPC对话的每一个选项的详细信息
 *       包括对话文本、语言、概率和表情动作
 *       当玩家与NPC交互时,系统会根据这些数据展示对话选项
 */
struct GossipTextOption
{
    std::string Text_0;                      ///< 对话文本0 (任务描述/选项文本 - 第一种变体)
    std::string Text_1;                      ///< 对话文本1 (任务描述/选项文本 - 第二种变体)
    uint32 BroadcastTextID;                  ///< 广播文本ID (BroadcastText.dbc引用)
    uint32 Language;                         ///< 语言ID (Languages.dbc引用,0=通用语)
    float Probability;                       ///< 此选项被选中的概率权重 (0.0-1.0)
    QEmote Emotes[MAX_GOSSIP_TEXT_EMOTES];   ///< 表情动作数组 (最多3个连续表情)
};

/// 每个NPC对话最大选项数量
#define MAX_GOSSIP_TEXT_OPTIONS 8

/**
 * @struct GossipText
 * @brief NPC对话完整文本结构
 *
 * @职责 存储一个NPC对话的所有选项
 *       最多包含8个对话选项,每个选项可以有独立的文本和表情
 */
struct GossipText
{
    GossipTextOption Options[MAX_GOSSIP_TEXT_OPTIONS];   ///< 对话选项数组 (最多8个选项)
};

/**
 * @struct PageTextLocale
 * @brief 页面文本本地化结构
 *
 * @职责 存储物品描述、任务文本等分页文本的本地化版本
 *       支持多语言显示
 */
struct PageTextLocale
{
    std::vector<std::string> Text;   ///< 本地化文本列表 (每种语言一个字符串)
};

/**
 * @struct NpcTextLocale
 * @brief NPC文本本地化结构
 *
 * @职责 存储NPC对话文本的多语言版本
 *       每个对话选项支持两种文本变体的本地化
 */
struct NpcTextLocale
{
    /**
     * @brief 默认构造函数
     */
    NpcTextLocale() { }

    std::vector<std::string> Text_0[MAX_GOSSIP_TEXT_OPTIONS];   ///< 每个选项文本0的本地化版本
    std::vector<std::string> Text_1[MAX_GOSSIP_TEXT_OPTIONS];   ///< 每个选项文本1的本地化版本
};
#endif
