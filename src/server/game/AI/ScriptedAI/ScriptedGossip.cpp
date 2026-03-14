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
 * @file ScriptedGossip.cpp
 * @brief 脚本化闲聊系统模块实现文件
 *
 * 本文件实现了闲聊菜单的辅助函数,为脚本提供简化的NPC对话菜单操作接口:
 * - 菜单初始化和清除
 * - 菜单项添加
 * - 菜单发送和关闭
 * - 选项信息查询
 */

#include "ScriptedGossip.h"
#include "Creature.h"
#include "Player.h"

/**
 * @brief 获取闲聊菜单选项的发送者ID
 * @param player 玩家对象指针
 * @param menuId 菜单ID
 * @return 发送者ID
 *
 * 从玩家的PlayerTalkClass中获取指定菜单ID对应的发送者ID
 */
uint32 GetGossipSenderFor(Player* player, uint32 menuId)
{
    return player->PlayerTalkClass->GetGossipOptionSender(menuId);
}

/**
 * @brief 获取闲聊菜单选项的动作ID
 * @param player 玩家对象指针
 * @param gossipListId 闲聊列表ID
 * @return 动作ID
 *
 * 从玩家的PlayerTalkClass中获取指定列表ID对应的动作ID
 */
uint32 GetGossipActionFor(Player* player, uint32 gossipListId)
{
    return player->PlayerTalkClass->GetGossipOptionAction(gossipListId);
}

/**
 * @brief 初始化闲聊菜单
 * @param player 玩家对象指针
 * @param menuId 菜单ID
 *
 * 设置玩家闲聊菜单的菜单ID,为后续添加菜单项做准备
 */
void InitGossipMenuFor(Player* player, uint32 menuId)
{
    player->PlayerTalkClass->GetGossipMenu().SetMenuId(menuId);
}

/**
 * @brief 清除闲聊菜单
 * @param player 玩家对象指针
 *
 * 清除玩家的所有闲聊菜单,通常在开始新的对话前调用
 */
void ClearGossipMenuFor(Player* player)
{
    player->PlayerTalkClass->ClearMenus();
}

/**
 * @brief 添加闲聊菜单项(使用提供的文本,不从数据库加载)
 * @param player 玩家对象指针
 * @param icon 选项图标
 * @param text 选项文本
 * @param sender 发送者ID
 * @param action 动作ID
 *
 * 添加一个自定义文本的闲聊菜单项,菜单ID为-1表示不从数据库加载
 */
// Using provided text, not from DB
void AddGossipItemFor(Player* player, GossipOptionIcon icon, std::string const& text, uint32 sender, uint32 action)
{
    // 菜单ID为-1表示不使用数据库信息,直接使用提供的文本
    player->PlayerTalkClass->GetGossipMenu().AddMenuItem(-1, icon, text, sender, action, "", 0);
}

/**
 * @brief 添加闲聊菜单项(带弹出确认框)
 * @param player 玩家对象指针
 * @param icon 选项图标
 * @param text 选项文本
 * @param sender 发送者ID
 * @param action 动作ID
 * @param popupText 弹出框文本(确认提示)
 * @param popupMoney 弹出框所需金钱
 * @param coded 是否使用编码文本
 *
 * 添加一个带弹出确认框的闲聊菜单项,点击后会显示确认对话框
 */
// Using provided texts, not from DB
void AddGossipItemFor(Player* player, GossipOptionIcon icon, std::string const& text, uint32 sender, uint32 action, std::string const& popupText, uint32 popupMoney, bool coded)
{
    // 菜单ID为-1表示不使用数据库信息,直接使用提供的文本和弹出框信息
    player->PlayerTalkClass->GetGossipMenu().AddMenuItem(-1, icon, text, sender, action, popupText, popupMoney, coded);
}

/**
 * @brief 添加闲聊菜单项(从数据库加载信息)
 * @param player 玩家对象指针
 * @param gossipMenuID 闲聊菜单ID
 * @param gossipMenuItemID 闲聊菜单项ID
 * @param sender 发送者ID
 * @param action 动作ID
 *
 * 使用数据库中定义的闲聊菜单项信息添加菜单项,文本和图标从数据库加载
 */
// Uses gossip item info from DB
void AddGossipItemFor(Player* player, uint32 gossipMenuID, uint32 gossipMenuItemID, uint32 sender, uint32 action)
{
    // 使用数据库中的菜单ID和菜单项ID添加菜单项
    player->PlayerTalkClass->GetGossipMenu().AddMenuItem(gossipMenuID, gossipMenuItemID, sender, action);
}

/**
 * @brief 发送闲聊菜单(使用GUID)
 * @param player 玩家对象指针
 * @param npcTextID NPC文本ID(定义NPC说的对话内容)
 * @param guid NPC的GUID
 *
 * 向玩家发送闲聊菜单,显示NPC的对话内容和可用选项
 */
void SendGossipMenuFor(Player* player, uint32 npcTextID, ObjectGuid const& guid)
{
    player->PlayerTalkClass->SendGossipMenu(npcTextID, guid);
}

/**
 * @brief 发送闲聊菜单(使用生物对象)
 * @param player 玩家对象指针
 * @param npcTextID NPC文本ID
 * @param creature NPC生物对象指针
 *
 * 向玩家发送闲聊菜单,如果生物对象有效则使用其GUID
 */
void SendGossipMenuFor(Player* player, uint32 npcTextID, Creature const* creature)
{
    // 检查生物对象是否有效
    if (creature)
        SendGossipMenuFor(player, npcTextID, creature->GetGUID());
}

/**
 * @brief 关闭闲聊菜单
 * @param player 玩家对象指针
 *
 * 关闭玩家的闲聊菜单对话框
 */
void CloseGossipMenuFor(Player* player)
{
    player->PlayerTalkClass->SendCloseGossip();
}
