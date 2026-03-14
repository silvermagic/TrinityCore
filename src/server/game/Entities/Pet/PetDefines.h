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
 * @file PetDefines.h
 * @brief 宠物系统核心定义头文件
 *
 * 本文件定义了宠物系统的核心数据结构、枚举类型和常量。
 * 包括宠物类型、保存模式、快乐度状态、技能状态、宠物存储结构等。
 * 这些定义被 Pet.cpp 和其他宠物相关模块使用。
 */

#ifndef TRINITYCORE_PET_DEFINES_H
#define TRINITYCORE_PET_DEFINES_H

#include "Define.h"
#include "Optional.h"
#include <array>
#include <string>
#include <vector>

enum ReactStates : uint8;

/**
 * @brief 宠物类型枚举
 *
 * 定义游戏中宠物的类型，不同类型的宠物有不同的行为和属性
 */
enum PetType : uint8
{
    SUMMON_PET              = 0,    // 召唤宠物（术士、法师等的宠物）
    HUNTER_PET              = 1,    // 猎人宠物（驯服的野兽）
    MAX_PET_TYPE            = 4     // 最大宠物类型值（用于边界检查）
};

/**
 * @brief 最大兽栏槽位数量
 *
 * 定义玩家可以存放宠物的兽栏槽位数量
 */
#define MAX_PET_STABLES         4

/**
 * @brief 宠物保存模式枚举
 *
 * 定义宠物在数据库中的保存位置和状态
 * 存储在 character_pet.slot 字段中
 */
enum PetSaveMode : int8
{
    PET_SAVE_AS_DELETED        = -1,                        // 标记为删除（实际不保存）
    PET_SAVE_AS_CURRENT        =  0,                        // 当前宠物槽位（跟随玩家）
    PET_SAVE_FIRST_STABLE_SLOT =  1,                        // 第一个兽栏槽位
    PET_SAVE_LAST_STABLE_SLOT  =  MAX_PET_STABLES,          // 最后一个兽栏槽位索引（包含），更高的值与 PET_SAVE_NOT_IN_SLOT 含义相同
    PET_SAVE_NOT_IN_SLOT       =  100                       // 不在槽位中（避免与兽栏扩展冲突）
};

/**
 * @brief 快乐度状态枚举
 *
 * 定义猎人宠物的快乐度等级，影响宠物的伤害输出
 */
enum HappinessState
{
    UNHAPPY = 1,    // 不快乐：伤害降低25%
    CONTENT = 2,    // 满足：伤害正常
    HAPPY   = 3     // 快乐：伤害提高25%
};

/**
 * @brief 宠物技能状态枚举
 *
 * 定义宠物技能在数据库中的状态，用于追踪技能的变化
 */
enum PetSpellState
{
    PETSPELL_UNCHANGED = 0,   // 未改变（与数据库一致）
    PETSPELL_CHANGED   = 1,   // 已改变（需要更新数据库）
    PETSPELL_NEW       = 2,   // 新技能（需要插入数据库）
    PETSPELL_REMOVED   = 3    // 已移除（需要从数据库删除）
};

/**
 * @brief 宠物技能类型枚举
 *
 * 定义宠物技能的来源类型
 */
enum PetSpellType
{
    PETSPELL_NORMAL = 0,   // 普通技能（学习的技能）
    PETSPELL_FAMILY = 1,   // 家族技能（宠物家族固有技能）
    PETSPELL_TALENT = 2    // 天赋技能（通过天赋获得）
};

/**
 * @brief 动作反馈枚举
 *
 * 定义宠物执行动作时的反馈类型
 */
enum ActionFeedback
{
    FEEDBACK_NONE            = 0,   // 无反馈
    FEEDBACK_PET_DEAD        = 1,   // 宠物已死亡
    FEEDBACK_NOTHING_TO_ATT  = 2,   // 没有攻击目标
    FEEDBACK_CANT_ATT_TARGET = 3    // 无法攻击目标
};

/**
 * @brief 宠物对话类型枚举
 *
 * 定义宠物在特定情况下的对话类型
 */
enum PetTalk
{
    PET_TALK_SPECIAL_SPELL  = 0,   // 施放特殊技能时
    PET_TALK_ATTACK         = 1    // 攻击时
};

/**
 * @brief 宠物跟随距离常量
 *
 * 定义宠物跟随主人时的默认距离（码）
 */
#define PET_FOLLOW_DIST  1.0f

/**
 * @brief 宠物跟随角度常量
 *
 * 定义宠物跟随主人时的默认角度（跟随在主人右侧，90度）
 */
#define PET_FOLLOW_ANGLE float(M_PI/2)

/**
 * @brief 宠物存储类
 *
 * 管理玩家的所有宠物信息，包括当前宠物、兽栏中的宠物和非槽位宠物。
 * 该类用于存储和检索玩家的宠物数据，支持宠物的召唤、存放和管理。
 */
class PetStable
{
public:
    /**
     * @brief 宠物信息结构体
     *
     * 存储单个宠物的详细信息，用于数据库存储和运行时管理
     */
    struct PetInfo
    {
        PetInfo() { }

        std::string Name;               // 宠物名称
        std::string ActionBar;          // 动作栏数据（序列化的字符串）
        uint32 PetNumber = 0;           // 宠物唯一编号（全局唯一标识符）
        uint32 CreatureId = 0;          // 生物模板ID（creature_template.Entry）
        uint32 DisplayId = 0;           // 显示模型ID
        uint32 Experience = 0;          // 当前经验值（猎人宠物）
        uint32 Health = 0;              // 当前生命值
        uint32 Mana = 0;                // 当前法力值
        uint32 Happiness = 0;           // 当前快乐度（猎人宠物）
        uint32 LastSaveTime = 0;        // 上次保存时间戳
        uint32 CreatedBySpellId = 0;    // 创建宠物的法术ID
        uint8 Level = 0;                // 宠物等级
        ReactStates ReactState = ReactStates(0);  // 反应状态（攻击、防御、被动等）
        PetType Type = MAX_PET_TYPE;    // 宠物类型
        bool WasRenamed = false;        // 是否已被重命名
    };

    Optional<PetInfo> CurrentPet;                                   // 当前宠物（PET_SAVE_AS_CURRENT）
    std::array<Optional<PetInfo>, MAX_PET_STABLES> StabledPets;     // 兽栏中的宠物（PET_SAVE_FIRST_STABLE_SLOT - PET_SAVE_LAST_STABLE_SLOT）
    uint32 MaxStabledPets = 0;                                      // 最大兽栏宠物数量
    std::vector<PetInfo> UnslottedPets;                             // 非槽位宠物（PET_SAVE_NOT_IN_SLOT）

    /**
     * @brief 获取非槽位的猎人宠物
     *
     * @return 如果存在且仅有一个非槽位的猎人宠物，返回其信息指针；否则返回 nullptr
     *
     * 说明：用于猎人"召唤宠物"技能，查找可召唤的宠物
     */
    PetInfo const* GetUnslottedHunterPet() const
    {
        return UnslottedPets.size() == 1 && UnslottedPets[0].Type == HUNTER_PET ? &UnslottedPets[0] : nullptr;
    }
};

#endif
