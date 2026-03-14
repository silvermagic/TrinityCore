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
 * @file UpdateFieldFlags.h
 * @brief 更新字段可见性标志定义模块
 *
 * 本文件定义了游戏对象更新字段的可见性权限控制系统。在 WoW 协议中，不同类型的观察者
 * （玩家自己、队友、物品拥有者等）对不同更新字段具有不同的访问权限。这些标志用于：
 * - 控制哪些更新字段应该发送给哪些观察者
 * - 实现游戏中信息可见性的分层机制（如隐藏其他玩家的私人信息）
 * - 优化网络传输，避免发送客户端无权查看的数据
 *
 * @see UpdateFields.h 更新字段索引定义
 * @see Object.cpp 更新字段标志的实际使用
 */

#ifndef _UPDATEFIELDFLAGS_H
#define _UPDATEFIELDFLAGS_H

#include "UpdateFields.h"
#include "Define.h"

/**
 * @brief 更新字段可见性标志枚举
 *
 * 定义更新字段的访问权限标志，用于控制不同类型的观察者能否看到特定字段数据。
 * 这些标志可以组合使用（位掩码），一个字段可以同时具有多个可见性标志。
 *
 * 可见性判断逻辑：
 * - 当观察者满足某个标志条件时，该字段对该观察者可见
 * - 多个标志为 OR 关系，满足任一条件即可见
 * - 未设置任何标志的字段对所有观察者不可见（UF_FLAG_NONE）
 *
 * @note 这些标志对应客户端的字段可见性规则，必须与客户端协议保持一致
 */
enum UpdatefieldFlags
{
    UF_FLAG_NONE         = 0x000,  ///< 无可见性标志，字段对所有观察者不可见
    UF_FLAG_PUBLIC       = 0x001,  ///< 公开标志，所有观察者可见（如玩家名称、等级）
    UF_FLAG_PRIVATE      = 0x002,  ///< 私有标志，仅对象自身可见（如玩家的法力值、技能冷却）
    UF_FLAG_OWNER        = 0x004,  ///< 拥有者标志，仅对象的创建者/拥有者可见（如宠物的属性）
    UF_FLAG_UNUSED1      = 0x008,  ///< 未使用标志1，保留字段
    UF_FLAG_ITEM_OWNER   = 0x010,  ///< 物品拥有者标志，仅物品的主人可见（如背包物品属性）
    UF_FLAG_SPECIAL_INFO = 0x020,  ///< 特殊信息标志，具有特殊权限的观察者可见（如GM查看隐藏信息）
    UF_FLAG_PARTY_MEMBER = 0x040,  ///< 队友标志，仅队伍成员可见（如队友的生命值、增益效果）
    UF_FLAG_UNUSED2      = 0x080,  ///< 未使用标志2，保留字段
    UF_FLAG_DYNAMIC      = 0x100   ///< 动态标志，需要动态计算的可见性（如根据距离、任务状态判断）
};

/**
 * @brief 物品更新字段标志数组
 *
 * 存储物品（Item）类型对象所有更新字段的可见性标志。
 * 数组索引对应 UpdateFields.h 中定义的物品字段索引（CONTAINER_FIELD_*）。
 *
 * @note 在服务器启动时初始化，运行时只读访问
 * @see Item::BuildUpdate 更新包构建时使用
 */
TC_GAME_API extern uint32 ItemUpdateFieldFlags[CONTAINER_END];

/**
 * @brief 单位更新字段标志数组
 *
 * 存储单位（Unit，包括玩家 Player 和生物 Creature）类型对象所有更新字段的可见性标志。
 * 数组索引对应 UpdateFields.h 中定义的单位字段索引（UNIT_FIELD_*、PLAYER_FIELD_*）。
 * 这是最复杂的更新字段系统，包含玩家特有的字段。
 *
 * @note 在服务器启动时初始化，运行时只读访问
 * @see Player::BuildUpdate 玩家更新包构建时使用
 * @see Creature::BuildUpdate 生物更新包构建时使用
 */
TC_GAME_API extern uint32 UnitUpdateFieldFlags[PLAYER_END];

/**
 * @brief游戏对象更新字段标志数组
 *
 * 存储游戏对象（GameObject，如箱子、门、传送门等）类型对象所有更新字段的可见性标志。
 * 数组索引对应 UpdateFields.h 中定义的游戏对象字段索引（GAMEOBJECT_FIELD_*）。
 *
 * @note 在服务器启动时初始化，运行时只读访问
 * @see GameObject::BuildUpdate 更新包构建时使用
 */
TC_GAME_API extern uint32 GameObjectUpdateFieldFlags[GAMEOBJECT_END];

/**
 * @brief 动态对象更新字段标志数组
 *
 * 存储动态对象（DynamicObject，如法术效果区域）类型对象所有更新字段的可见性标志。
 * 数组索引对应 UpdateFields.h 中定义的动态对象字段索引（DYNAMICOBJECT_FIELD_*）。
 *
 * @note 在服务器启动时初始化，运行时只读访问
 * @see DynamicObject::BuildUpdate 更新包构建时使用
 */
TC_GAME_API extern uint32 DynamicObjectUpdateFieldFlags[DYNAMICOBJECT_END];

/**
 * @brief 尸体更新字段标志数组
 *
 * 存储尸体（Corpse，玩家死亡后的尸体对象）类型对象所有更新字段的可见性标志。
 * 数组索引对应 UpdateFields.h 中定义的尸体字段索引（CORPSE_FIELD_*）。
 *
 * @note 在服务器启动时初始化，运行时只读访问
 * @see Corpse::BuildUpdate 更新包构建时使用
 */
TC_GAME_API extern uint32 CorpseUpdateFieldFlags[CORPSE_END];

#endif // _UPDATEFIELDFLAGS_H
