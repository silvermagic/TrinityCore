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
 * @file ScriptedGossip.h
 * @brief 脚本化闲聊系统模块头文件
 *
 * 本模块提供了NPC闲聊菜单的辅助函数,用于简化脚本中闲聊菜单的创建和管理。
 * 主要功能包括:
 * - 闲聊菜单的初始化和清除
 * - 添加闲聊选项(支持数据库和动态文本)
 * - 发送和关闭闲聊菜单
 * - 获取闲聊选项的发送者和动作
 */

#ifndef TRINITY_SCRIPTEDGOSSIP_H
#define TRINITY_SCRIPTEDGOSSIP_H

#include "GossipDef.h"
#include "QuestDef.h"

/** 闲聊选项文本常量 - 浏览商品 */
#define GOSSIP_TEXT_BROWSE_GOODS        "I'd like to browse your goods."
/** 闲聊选项文本常量 - 训练技能 */
#define GOSSIP_TEXT_TRAIN               "Train me!"

/**
 * @brief 商业技能枚举
 *
 * 定义了游戏中的商业技能ID和相关常量
 */
enum eTradeskill
{
    // ==================== 商业技能ID定义 ====================
    TRADESKILL_ALCHEMY                  = 1,   ///< 炼金术
    TRADESKILL_BLACKSMITHING            = 2,   ///< 锻造
    TRADESKILL_COOKING                  = 3,   ///< 烹饪
    TRADESKILL_ENCHANTING               = 4,   ///< 附魔
    TRADESKILL_ENGINEERING              = 5,   ///< 工程学
    TRADESKILL_FIRSTAID                 = 6,   ///< 急救
    TRADESKILL_HERBALISM                = 7,   ///< 草药学
    TRADESKILL_LEATHERWORKING           = 8,   ///< 制皮
    TRADESKILL_POISONS                  = 9,   ///< 毒药
    TRADESKILL_TAILORING                = 10,  ///< 裁缝
    TRADESKILL_MINING                   = 11,  ///< 采矿
    TRADESKILL_FISHING                  = 12,  ///< 钓鱼
    TRADESKILL_SKINNING                 = 13,  ///< 剥皮
    TRADESKILL_JEWLCRAFTING             = 14,  ///< 珠宝加工
    TRADESKILL_INSCRIPTION              = 15,  ///< 铭文

    // ==================== 商业技能等级定义 ====================
    TRADESKILL_LEVEL_NONE               = 0,   ///< 无等级
    TRADESKILL_LEVEL_APPRENTICE         = 1,   ///< 学徒级
    TRADESKILL_LEVEL_JOURNEYMAN         = 2,   ///< 熟练级
    TRADESKILL_LEVEL_EXPERT             = 3,   ///< 专家级
    TRADESKILL_LEVEL_ARTISAN            = 4,   ///< 工匠级
    TRADESKILL_LEVEL_MASTER             = 5,   ///< 大师级
    TRADESKILL_LEVEL_GRAND_MASTER       = 6,   ///< 宗师级

    // ==================== 闲聊动作定义 ====================
    GOSSIP_ACTION_TRADE                 = 1,    ///< 交易动作
    GOSSIP_ACTION_TRAIN                 = 2,    ///< 训练动作
    GOSSIP_ACTION_TAXI                  = 3,    ///< 飞行点动作
    GOSSIP_ACTION_GUILD                 = 4,    ///< 公会动作
    GOSSIP_ACTION_BATTLE                = 5,    ///< 战场动作
    GOSSIP_ACTION_BANK                  = 6,    ///< 银行动作
    GOSSIP_ACTION_INN                   = 7,    ///< 旅店动作
    GOSSIP_ACTION_HEAL                  = 8,    ///< 治疗动作
    GOSSIP_ACTION_TABARD                = 9,    ///< 战袍动作
    GOSSIP_ACTION_AUCTION               = 10,   ///< 拍卖行动作
    GOSSIP_ACTION_INN_INFO              = 11,   ///< 旅店信息动作
    GOSSIP_ACTION_UNLEARN               = 12,   ///< 遗忘技能动作
    GOSSIP_ACTION_INFO_DEF              = 1000, ///< 默认信息动作起始ID

    // ==================== 闲聊发送者定义 ====================
    GOSSIP_SENDER_MAIN                  = 1,   ///< 主发送者
    GOSSIP_SENDER_INN_INFO              = 2,   ///< 旅店信息发送者
    GOSSIP_SENDER_INFO                  = 3,   ///< 信息发送者
    GOSSIP_SENDER_SEC_PROFTRAIN         = 4,   ///< 专业训练发送者(次要)
    GOSSIP_SENDER_SEC_CLASSTRAIN        = 5,   ///< 职业训练发送者(次要)
    GOSSIP_SENDER_SEC_BATTLEINFO        = 6,   ///< 战场信息发送者(次要)
    GOSSIP_SENDER_SEC_BANK              = 7,   ///< 银行发送者(次要)
    GOSSIP_SENDER_SEC_INN               = 8,   ///< 旅店发送者(次要)
    GOSSIP_SENDER_SEC_MAILBOX           = 9,   ///< 邮箱发送者(次要)
    GOSSIP_SENDER_SEC_STABLEMASTER      = 10   ///< 兽栏管理员发送者(次要)
};

class Creature;

/**
 * @brief 获取闲聊菜单选项的发送者ID
 * @param player 玩家对象指针
 * @param menuId 菜单ID
 * @return 发送者ID
 *
 * 从玩家的闲聊菜单中获取指定菜单ID对应的发送者ID
 */
uint32 TC_GAME_API GetGossipSenderFor(Player* player, uint32 menuId);

/**
 * @brief 获取闲聊菜单选项的动作ID
 * @param player 玩家对象指针
 * @param gossipListId 闲聊列表ID
 * @return 动作ID
 *
 * 从玩家的闲聊菜单中获取指定列表ID对应的动作ID
 */
uint32 TC_GAME_API GetGossipActionFor(Player* player, uint32 gossipListId);

/**
 * @brief 初始化闲聊菜单
 * @param player 玩家对象指针
 * @param menuId 菜单ID
 *
 * 为玩家初始化指定ID的闲聊菜单
 */
void TC_GAME_API InitGossipMenuFor(Player* player, uint32 menuId);

/**
 * @brief 清除闲聊菜单
 * @param player 玩家对象指针
 *
 * 清除玩家的闲聊菜单,移除所有选项
 */
void TC_GAME_API ClearGossipMenuFor(Player* player);

/**
 * @brief 添加闲聊菜单项(使用提供的文本,不从数据库加载)
 * @param player 玩家对象指针
 * @param icon 选项图标
 * @param text 选项文本
 * @param sender 发送者ID
 * @param action 动作ID
 *
 * 添加一个自定义文本的闲聊菜单项
 */
// Using provided text, not from DB
void TC_GAME_API AddGossipItemFor(Player* player, GossipOptionIcon icon, std::string const& text, uint32 sender, uint32 action);

/**
 * @brief 添加闲聊菜单项(带弹出确认框)
 * @param player 玩家对象指针
 * @param icon 选项图标
 * @param text 选项文本
 * @param sender 发送者ID
 * @param action 动作ID
 * @param popupText 弹出框文本
 * @param popupMoney 弹出框所需金钱
 * @param coded 是否使用编码文本
 *
 * 添加一个带弹出确认框的闲聊菜单项
 */
// Using provided texts, not from DB
void TC_GAME_API AddGossipItemFor(Player* player, GossipOptionIcon icon, std::string const& text, uint32 sender, uint32 action, std::string const& popupText, uint32 popupMoney, bool coded);

/**
 * @brief 添加闲聊菜单项(从数据库加载信息)
 * @param player 玩家对象指针
 * @param gossipMenuID 闲聊菜单ID
 * @param gossipMenuItemID 闲聊菜单项ID
 * @param sender 发送者ID
 * @param action 动作ID
 *
 * 使用数据库中的闲聊菜单项信息添加菜单项
 */
// Uses gossip item info from DB
void TC_GAME_API AddGossipItemFor(Player* player, uint32 gossipMenuID, uint32 gossipMenuItemID, uint32 sender, uint32 action);

/**
 * @brief 发送闲聊菜单(使用GUID)
 * @param player 玩家对象指针
 * @param npcTextID NPC文本ID
 * @param guid NPC的GUID
 *
 * 向玩家发送指定NPC文本和GUID的闲聊菜单
 */
void TC_GAME_API SendGossipMenuFor(Player* player, uint32 npcTextID, ObjectGuid const& guid);

/**
 * @brief 发送闲聊菜单(使用生物对象)
 * @param player 玩家对象指针
 * @param npcTextID NPC文本ID
 * @param creature NPC生物对象指针
 *
 * 向玩家发送指定NPC文本和生物对象的闲聊菜单
 */
void TC_GAME_API SendGossipMenuFor(Player* player, uint32 npcTextID, Creature const* creature);

/**
 * @brief 关闭闲聊菜单
 * @param player 玩家对象指针
 *
 * 关闭玩家的闲聊菜单
 */
void TC_GAME_API CloseGossipMenuFor(Player* player);

#endif
