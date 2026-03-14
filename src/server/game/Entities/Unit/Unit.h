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
 * @file Unit.h
 * @brief 单位（Unit）基类定义文件
 *
 * 本文件定义了 Unit 类及其相关类型、枚举和辅助结构。
 * Unit 是所有可战斗游戏实体（玩家、生物、宠物等）的基类。
 *
 * @mainpage Unit 模块职责
 *
 * @section main_content 主要内容
 *
 * @subsection enums 枚举类型定义
 * - VictimState：受害者状态（命中、闪避、招架等）
 * - UnitState：单位状态标志（昏迷、恐惧、施法中等）
 * - DeathState：死亡状态（存活、刚死、尸体、死亡）
 * - CombatRating：战斗等级类型
 * - DamageEffectType：伤害效果类型
 * - UnitTypeMask：单位类型掩码
 * - UnitMods：单位属性修正类型
 *
 * @subsection structs 辅助结构
 * - DiminishingReturn：递减效果数据
 * - CalcDamageInfo：近战伤害计算结果
 * - SpellNonMeleeDamage：法术伤害信息
 * - HealInfo：治疗信息
 * - ProcEventInfo：触发事件信息
 * - CharmInfo：魅惑信息
 *
 * @subsection unit_class Unit 类声明
 * - 所有单位共享的核心功能接口
 * - 属性管理（生命值、能量、属性等）
 * - 战斗系统（攻击、伤害、威胁等）
 * - 法术系统（施法、光环等）
 * - 移动系统（移动生成器、速度等）
 * - AI 系统（AI 状态管理）
 * - 状态管理（状态标志、死亡状态等）
 *
 * @section design_pattern 设计模式
 * - 继承：Unit 继承自 WorldObject
 * - 组合：包含 ThreatManager, CombatManager, MotionMaster 等子系统
 * - 观察者：事件系统、回调机制
 *
 * @section related_files 相关文件
 * - Unit.cpp：类实现
 * - Player.h：玩家类（继承 Unit）
 * - Creature.h：生物类（继承 Unit）
 */

#ifndef __UNIT_H
#define __UNIT_H

#include "Object.h"
#include "CombatManager.h"
#include "SpellAuraDefines.h"
#include "ThreatManager.h"
#include "Timer.h"
#include "UnitDefines.h"
#include "Util.h"
#include <map>
#include <memory>
#include <stack>
#include <queue>

// ============================================================================
// 常量定义
// ============================================================================

#define VISUAL_WAYPOINT 1       // 用于显示路径点的生物ID，仅对GM可见
#define WORLD_TRIGGER 12999     // 世界触发器生物ID

// 魅惑/附身相关法术槽限制
#define MAX_SPELL_CHARM         4   // 魅惑法术最大数量
#define MAX_SPELL_VEHICLE       6   // 载具法术最大数量
#define MAX_SPELL_POSSESS       8   // 附身法术最大数量
#define MAX_SPELL_CONTROL_BAR   10  // 控制栏最大法术数量

#define MAX_AGGRO_RESET_TIME 10 // 重置仇恨的时间（秒）
#define MAX_AGGRO_RADIUS 45.0f  // 最大仇恨半径（码）

// ============================================================================
// 受害者状态枚举
// ============================================================================
// 描述攻击结果时的受害者状态
// 用于近战攻击结果判定和伤害计算
// ============================================================================
enum VictimState
{
    VICTIMSTATE_INTACT         = 0, // 完好：攻击者未命中
    VICTIMSTATE_HIT            = 1, // 命中：受害者被击中（包括被格挡的命中）
    VICTIMSTATE_DODGE          = 2, // 闪避：受害者闪避了攻击
    VICTIMSTATE_PARRY          = 3, // 招架：受害者招架了攻击
    VICTIMSTATE_INTERRUPT      = 4, // 打断：攻击打断了受害者的动作
    VICTIMSTATE_BLOCKS         = 5, // 格挡：受害者格挡了攻击（未使用？即使完全格挡也不设置）
    VICTIMSTATE_EVADES         = 6, // 规避：受害者脱离战斗
    VICTIMSTATE_IS_IMMUNE      = 7, // 免疫：受害者免疫此次攻击
    VICTIMSTATE_DEFLECTS       = 8  // 偏斜：攻击被偏斜
};

// ============================================================================
// 背包槽位枚举
// ============================================================================
// 定义背包和槽位的特殊值
// ============================================================================
enum InventorySlot
{
    NULL_BAG                   = 0,   // 空背包标识
    NULL_SLOT                  = 255  // 空槽位标识
};

struct AbstractFollower;
struct FactionTemplateEntry;
struct LiquidData;
struct LiquidTypeEntry;
struct SpellValue;

class Aura;
class AuraApplication;
class AuraEffect;
class Creature;
class DynamicObject;
class GameClient;
class GameObject;
class Guardian;
class Item;
class Minion;
class MotionMaster;
class Pet;
class PetAura;
class Spell;
class SpellCastTargets;
class SpellEffectInfo;
class SpellHistory;
class SpellInfo;
class Totem;
class Transport;
class TransportBase;
class UnitAI;
class UnitAura;
class Vehicle;
class VehicleJoinEvent;

enum MovementGeneratorType : uint8;
enum ZLiquidStatus : uint32;

namespace Movement
{
    class MoveSpline;
}

typedef std::list<Unit*> UnitList;                         // 单位列表类型

/**
 * @class DispelableAura
 * @brief 可驱散光环包装器
 *
 * 封装光环的驱散相关信息，用于驱散和偷取法术系统。
 * 包含光环实例、驱散几率和可驱散层数。
 *
 * @section usage 使用场景
 * - 驱散法术（如净化、驱散魔法）
 * - 偷取法术（如法术偷取）
 * - 吞噬魔法等效果
 *
 * @section mechanism 驱散机制
 * 驱散时需要：
 * 1. 通过 RollDispel() 判定是否成功驱散（基于几率）
 * 2. 通过 DecrementCharge() 减少层数
 * 3. 如果层数耗尽，光环被移除
 */
class TC_GAME_API DispelableAura
{
    public:
        /**
         * @brief 构造函数
         * @param aura 被包装的光环
         * @param dispelChance 驱散几率（百分比，0-100）
         * @param dispelCharges 可驱散的层数
         */
        DispelableAura(Aura* aura, int32 dispelChance, uint8 dispelCharges);
        ~DispelableAura();

        /**
         * @brief 获取光环实例
         * @return 光环指针
         */
        Aura* GetAura() const { return _aura; }

        /**
         * @brief 进行驱散掷骰
         * @return 如果驱散成功返回 true
         *
         * 基于 _chance 进行随机判定。
         */
        bool RollDispel() const;

        /**
         * @brief 获取剩余可驱散层数
         * @return 层数
         */
        uint8 GetDispelCharges() const { return _charges; }

        /**
         * @brief 增加层数
         *
         * 用于某些特殊效果增加层数。
         */
        void IncrementCharges() { ++_charges; }

        /**
         * @brief 减少一层
         * @return true 如果还有剩余层数，false 如果层数已耗尽
         *
         * 驱散成功后调用。
         */
        bool DecrementCharge()
        {
            if (!_charges)
                return false;

            --_charges;
            return _charges > 0;
        }

    private:
        Aura* _aura;                                        ///< 被包装的光环
        int32 _chance;                                      ///< 驱散几率（百分比）
        uint8 _charges;                                     ///< 可驱散层数
};
typedef std::vector<DispelableAura> DispelChargesList;       ///< 可驱散光环列表

typedef std::unordered_multimap<uint32 /*type*/, uint32 /*spellId*/> SpellImmuneContainer;  // 法术免疫容器

// ============================================================================
// 属性修正类型枚举
// ============================================================================
enum UnitModifierFlatType
{
    BASE_VALUE = 0,                                         // 基础值
    TOTAL_VALUE = 1,                                        // 总值
    MODIFIER_TYPE_FLAT_END = 2
};

enum UnitModifierPctType
{
    BASE_PCT = 0,                                           // 基础百分比
    TOTAL_PCT = 1,                                          // 总百分比
    MODIFIER_TYPE_PCT_END = 2
};

// ============================================================================
// 武器伤害范围枚举
// ============================================================================
enum WeaponDamageRange
{
    MINDAMAGE,                                              // 最小伤害
    MAXDAMAGE                                               // 最大伤害
};

// ============================================================================
// 单位属性修正类型枚举
// ============================================================================
// 定义所有可被光环修正的属性类型
// 注意：枚举顺序必须与 Stats、Powers、SpellSchools 对应
// ============================================================================
enum UnitMods
{
    UNIT_MOD_STAT_STRENGTH,                                 // 力量（必须与 Stats 枚举顺序一致）
    UNIT_MOD_STAT_AGILITY,                                  // 敏捷
    UNIT_MOD_STAT_STAMINA,                                  // 耐力
    UNIT_MOD_STAT_INTELLECT,                                // 智力
    UNIT_MOD_STAT_SPIRIT,                                   // 精神
    UNIT_MOD_HEALTH,                                        // 生命值
    UNIT_MOD_MANA,                                          // 法力值（必须与 Powers 枚举顺序一致）
    UNIT_MOD_RAGE,                                          // 怒气
    UNIT_MOD_FOCUS,                                         // 集中值
    UNIT_MOD_ENERGY,                                        // 能量值
    UNIT_MOD_HAPPINESS,                                     // 快乐值
    UNIT_MOD_RUNE,                                          // 符文
    UNIT_MOD_RUNIC_POWER,                                   // 符文能量
    UNIT_MOD_ARMOR,                                         // 护甲（必须与 SpellSchools 枚举顺序一致）
    UNIT_MOD_RESISTANCE_HOLY,                               // 神圣抗性
    UNIT_MOD_RESISTANCE_FIRE,                               // 火焰抗性
    UNIT_MOD_RESISTANCE_NATURE,                             // 自然抗性
    UNIT_MOD_RESISTANCE_FROST,                              // 冰霜抗性
    UNIT_MOD_RESISTANCE_SHADOW,                             // 暗影抗性
    UNIT_MOD_RESISTANCE_ARCANE,                             // 奥术抗性
    UNIT_MOD_ATTACK_POWER,                                  // 攻击强度
    UNIT_MOD_ATTACK_POWER_RANGED,                           // 远程攻击强度
    UNIT_MOD_DAMAGE_MAINHAND,                               // 主手武器伤害
    UNIT_MOD_DAMAGE_OFFHAND,                                // 副手武器伤害
    UNIT_MOD_DAMAGE_RANGED,                                 // 远程武器伤害
    UNIT_MOD_END,
    // 同义词定义，便于范围遍历
    UNIT_MOD_STAT_START = UNIT_MOD_STAT_STRENGTH,
    UNIT_MOD_STAT_END = UNIT_MOD_STAT_SPIRIT + 1,
    UNIT_MOD_RESISTANCE_START = UNIT_MOD_ARMOR,
    UNIT_MOD_RESISTANCE_END = UNIT_MOD_RESISTANCE_ARCANE + 1,
    UNIT_MOD_POWER_START = UNIT_MOD_MANA,
    UNIT_MOD_POWER_END = UNIT_MOD_RUNIC_POWER + 1
};

// ============================================================================
// 基础修正组枚举
// ============================================================================
enum BaseModGroup
{
    CRIT_PERCENTAGE,                                        // 近战暴击百分比
    RANGED_CRIT_PERCENTAGE,                                 // 远程暴击百分比
    OFFHAND_CRIT_PERCENTAGE,                                // 副手暴击百分比
    SHIELD_BLOCK_VALUE,                                     // 盾牌格挡值
    BASEMOD_END
};

enum BaseModType
{
    FLAT_MOD,                                               // 固定修正
    PCT_MOD,                                                // 百分比修正
    MOD_END
};

// ============================================================================
// 死亡状态枚举
// ============================================================================
// 描述单位的生命/死亡状态
// ============================================================================
enum DeathState
{
    ALIVE          = 0,                                     // 存活状态
    JUST_DIED      = 1,                                     // 刚死亡（本帧死亡）
    CORPSE         = 2,                                     // 尸体状态（可复活）
    DEAD           = 3,                                     // 完全死亡（等待重生或删除）
    JUST_RESPAWNED = 4                                      // 刚复活
};

// ============================================================================
// 单位状态枚举
// ============================================================================
// 单位状态标志位，使用位掩码组合多个状态
// 这些状态影响单位的移动、施法、战斗等行为
// ============================================================================
enum UnitState : uint32
{
    UNIT_STATE_DIED                  = 0x00000001,          // 玩家有假死光环
    UNIT_STATE_MELEE_ATTACKING       = 0x00000002,          // 正在进行近战攻击
    UNIT_STATE_CHARMED               = 0x00000004,          // 被魅惑（有魅惑光环）
    UNIT_STATE_STUNNED               = 0x00000008,          // 昏迷状态
    UNIT_STATE_ROAMING               = 0x00000010,          // 漫游中
    UNIT_STATE_CHASE                 = 0x00000020,          // 追逐中
    UNIT_STATE_FOCUSING              = 0x00000040,          // 聚焦中（专注目标）
    UNIT_STATE_FLEEING               = 0x00000080,          // 逃跑中
    UNIT_STATE_IN_FLIGHT             = 0x00000100,          // 飞行模式（玩家飞行坐骑）
    UNIT_STATE_FOLLOW                = 0x00000200,          // 跟随中
    UNIT_STATE_ROOT                  = 0x00000400,          // 定身状态
    UNIT_STATE_CONFUSED              = 0x00000800,          // 迷惑状态
    UNIT_STATE_DISTRACTED            = 0x00001000,          // 分心状态
    UNIT_STATE_ISOLATED              = 0x00002000,          // 孤立状态（区域光环不影响其他玩家）
    UNIT_STATE_ATTACK_PLAYER         = 0x00004000,          // 正在攻击玩家
    UNIT_STATE_CASTING               = 0x00008000,          // 正在施法
    UNIT_STATE_POSSESSED             = 0x00010000,          // 被附身状态
    UNIT_STATE_CHARGING              = 0x00020000,          // 冲锋中
    UNIT_STATE_JUMPING               = 0x00040000,          // 跳跃中
    UNIT_STATE_FOLLOW_FORMATION      = 0x00080000,          // 跟随编队中
    UNIT_STATE_MOVE                  = 0x00100000,          // 移动中（通用）
    UNIT_STATE_ROTATING              = 0x00200000,          // 旋转中
    UNIT_STATE_EVADE                 = 0x00400000,          // 脱离战斗中
    UNIT_STATE_ROAMING_MOVE          = 0x00800000,          // 漫游移动中
    UNIT_STATE_CONFUSED_MOVE         = 0x01000000,          // 迷惑移动中
    UNIT_STATE_FLEEING_MOVE          = 0x02000000,          // 逃跑移动中
    UNIT_STATE_CHASE_MOVE            = 0x04000000,          // 追逐移动中
    UNIT_STATE_FOLLOW_MOVE           = 0x08000000,          // 跟随移动中
    UNIT_STATE_IGNORE_PATHFINDING    = 0x10000000,          // 忽略寻路
    UNIT_STATE_FOLLOW_FORMATION_MOVE = 0x20000000,          // 编队跟随移动中

    // === 组合状态 ===
    UNIT_STATE_ALL_STATE_SUPPORTED = UNIT_STATE_DIED | UNIT_STATE_MELEE_ATTACKING | UNIT_STATE_CHARMED | UNIT_STATE_STUNNED | UNIT_STATE_ROAMING | UNIT_STATE_CHASE
                                   | UNIT_STATE_FOCUSING | UNIT_STATE_FLEEING | UNIT_STATE_IN_FLIGHT | UNIT_STATE_FOLLOW | UNIT_STATE_ROOT | UNIT_STATE_CONFUSED
                                   | UNIT_STATE_DISTRACTED | UNIT_STATE_ISOLATED | UNIT_STATE_ATTACK_PLAYER | UNIT_STATE_CASTING
                                   | UNIT_STATE_POSSESSED | UNIT_STATE_CHARGING | UNIT_STATE_JUMPING | UNIT_STATE_MOVE | UNIT_STATE_ROTATING
                                   | UNIT_STATE_EVADE | UNIT_STATE_ROAMING_MOVE | UNIT_STATE_CONFUSED_MOVE | UNIT_STATE_FLEEING_MOVE
                                   | UNIT_STATE_CHASE_MOVE | UNIT_STATE_FOLLOW_MOVE | UNIT_STATE_IGNORE_PATHFINDING | UNIT_STATE_FOLLOW_FORMATION_MOVE,

    UNIT_STATE_UNATTACKABLE        = UNIT_STATE_IN_FLIGHT,  // 不可攻击状态
    UNIT_STATE_MOVING              = UNIT_STATE_ROAMING_MOVE | UNIT_STATE_CONFUSED_MOVE | UNIT_STATE_FLEEING_MOVE | UNIT_STATE_CHASE_MOVE | UNIT_STATE_FOLLOW_MOVE | UNIT_STATE_FOLLOW_FORMATION_MOVE,  // 任何移动状态
    UNIT_STATE_CONTROLLED          = UNIT_STATE_CONFUSED | UNIT_STATE_STUNNED | UNIT_STATE_FLEEING,  // 受控制状态
    UNIT_STATE_LOST_CONTROL        = UNIT_STATE_CONTROLLED | UNIT_STATE_POSSESSED | UNIT_STATE_JUMPING | UNIT_STATE_CHARGING,  // 失去控制状态
    UNIT_STATE_CANNOT_AUTOATTACK   = UNIT_STATE_CONTROLLED | UNIT_STATE_CHARGING | UNIT_STATE_CASTING,  // 不能自动攻击
    UNIT_STATE_SIGHTLESS           = UNIT_STATE_LOST_CONTROL | UNIT_STATE_EVADE,  // 无视野状态
    UNIT_STATE_CANNOT_TURN         = UNIT_STATE_LOST_CONTROL | UNIT_STATE_ROTATING | UNIT_STATE_FOCUSING,  // 不能转向
    UNIT_STATE_NOT_MOVE            = UNIT_STATE_ROOT | UNIT_STATE_STUNNED | UNIT_STATE_DIED | UNIT_STATE_DISTRACTED,  // 不能移动

    UNIT_STATE_ALL_ERASABLE        = UNIT_STATE_ALL_STATE_SUPPORTED & ~(UNIT_STATE_IGNORE_PATHFINDING),  // 可清除的状态
    UNIT_STATE_ALL_STATE           = 0xffffffff
};

// 移动速度全局常量声明
TC_GAME_API extern float baseMoveSpeed[MAX_MOVE_TYPE];              // 所有单位的基础移动速度
TC_GAME_API extern float playerBaseMoveSpeed[MAX_MOVE_TYPE];        // 玩家的基础移动速度

// ============================================================================
// 移动变更类型枚举
// ============================================================================
// 描述移动状态的变更类型，用于玩家移动确认系统
// ============================================================================
enum class MovementChangeType : uint8
{
    INVALID,                                                // 无效类型

    ROOT,                                                   // 定身状态变更
    WATER_WALK,                                             // 水面行走状态变更
    SET_HOVER,                                              // 悬停状态变更
    SET_CAN_FLY,                                            // 飞行能力变更
    SET_CAN_TRANSITION_BETWEEN_SWIM_AND_FLY,               // 游泳/飞行转换能力变更
    FEATHER_FALL,                                           // 羽落状态变更
    GRAVITY_DISABLE,                                        // 重力禁用状态变更

    SPEED_CHANGE_WALK,                                      // 行走速度变更
    SPEED_CHANGE_RUN,                                       // 跑步速度变更
    SPEED_CHANGE_RUN_BACK,                                  // 后退速度变更
    SPEED_CHANGE_SWIM,                                      // 游泳速度变更
    SPEED_CHANGE_SWIM_BACK,                                 // 游泳后退速度变更
    RATE_CHANGE_TURN,                                       // 转身速率变更
    SPEED_CHANGE_FLIGHT_SPEED,                              // 飞行速度变更
    SPEED_CHANGE_FLIGHT_BACK_SPEED,                         // 飞行后退速度变更
    RATE_CHANGE_PITCH,                                      // 俯仰速率变更

    SET_COLLISION_HGT,                                      // 碰撞高度变更
    TELEPORT,                                               // 传送
    KNOCK_BACK                                              // 击退
};

// ============================================================================
// 玩家移动待处理变更结构
// ============================================================================
// 存储等待客户端确认的移动变更信息
// ============================================================================
struct PlayerMovementPendingChange
{
    PlayerMovementPendingChange();

    uint32 movementCounter = 0;                             // 移动计数器
    MovementChangeType movementChangeType = MovementChangeType::INVALID;  // 变更类型
    uint32 time;                                            // 时间戳

    float newValue = 0.0f;                                  // 速度或高度变更时使用
    bool apply = false;                                     // 移动标志变更时使用
    struct KnockbackInfo                                    // 击退信息
    {
        float vcos = 0.0f;                                  // 方向余弦值
        float vsin = 0.0f;                                  // 方向正弦值
        float speedXY = 0.0f;                               // 水平速度
        float speedZ = 0.0f;                                // 垂直速度
    } knockbackInfo;                                        // 击退时使用
};

// ============================================================================
// 战斗等级枚举
// ============================================================================
// 定义所有战斗属性等级类型，用于装备属性和玩家属性计算
// ============================================================================
enum CombatRating
{
    CR_WEAPON_SKILL             = 0,                        // 武器技能等级
    CR_DEFENSE_SKILL            = 1,                        // 防御技能等级
    CR_DODGE                    = 2,                        // 闪避等级
    CR_PARRY                    = 3,                        // 招架等级
    CR_BLOCK                    = 4,                        // 格挡等级
    CR_HIT_MELEE                = 5,                        // 近战命中等级
    CR_HIT_RANGED               = 6,                        // 远程命中等级
    CR_HIT_SPELL                = 7,                        // 法术命中等级
    CR_CRIT_MELEE               = 8,                        // 近战暴击等级
    CR_CRIT_RANGED              = 9,                        // 远程暴击等级
    CR_CRIT_SPELL               = 10,                       // 法术暴击等级
    CR_HIT_TAKEN_MELEE          = 11,                       // 被近战命中等级
    CR_HIT_TAKEN_RANGED         = 12,                       // 被远程命中等级
    CR_HIT_TAKEN_SPELL          = 13,                       // 被法术命中等级
    CR_CRIT_TAKEN_MELEE         = 14,                       // 被近战暴击等级
    CR_CRIT_TAKEN_RANGED        = 15,                       // 被远程暴击等级
    CR_CRIT_TAKEN_SPELL         = 16,                       // 被法术暴击等级
    CR_HASTE_MELEE              = 17,                       // 近战急速等级
    CR_HASTE_RANGED             = 18,                       // 远程急速等级
    CR_HASTE_SPELL              = 19,                       // 法术急速等级
    CR_WEAPON_SKILL_MAINHAND    = 20,                       // 主手武器技能等级
    CR_WEAPON_SKILL_OFFHAND     = 21,                       // 副手武器技能等级
    CR_WEAPON_SKILL_RANGED      = 22,                       // 远程武器技能等级
    CR_EXPERTISE                = 23,                       // 精准等级
    CR_ARMOR_PENETRATION        = 24                        // 护甲穿透等级
};

#define MAX_COMBAT_RATING         25                        // 战斗等级类型数量

// ============================================================================
// 伤害效果类型枚举
// ============================================================================
// 描述伤害/治疗的类型，影响光环触发和伤害计算
// ============================================================================
enum DamageEffectType : uint8
{
    DIRECT_DAMAGE           = 0,                            // 直接伤害：普通武器攻击（非职业技能或法术）
    SPELL_DIRECT_DAMAGE     = 1,                            // 法术直接伤害：法术/职业技能伤害
    DOT                     = 2,                            // 持续伤害（DoT）
    HEAL                    = 3,                            // 治疗
    NODAMAGE                = 4,                            // 无伤害：应用于生命值但不触发法术中断标志
    SELF_DAMAGE             = 5                             // 自我伤害
};

// ============================================================================
// 单位类型掩码枚举
// ============================================================================
// 用于标识单位的类型（召唤物、宠物、图腾等）
// ============================================================================
enum UnitTypeMask
{
    UNIT_MASK_NONE                  = 0x00000000,           // 无类型
    UNIT_MASK_SUMMON                = 0x00000001,           // 召唤物
    UNIT_MASK_MINION                = 0x00000002,           // 仆从
    UNIT_MASK_GUARDIAN              = 0x00000004,           // 守护者
    UNIT_MASK_TOTEM                 = 0x00000008,           // 图腾
    UNIT_MASK_PET                   = 0x00000010,           // 宠物
    UNIT_MASK_VEHICLE               = 0x00000020,           // 载具
    UNIT_MASK_PUPPET                = 0x00000040,           // 傀儡
    UNIT_MASK_HUNTER_PET            = 0x00000080,           // 猎人宠物
    UNIT_MASK_CONTROLABLE_GUARDIAN  = 0x00000100,           // 可控制的守护者
    UNIT_MASK_ACCESSORY             = 0x00000200            // 载具附属单位
};

// ============================================================================
// 递减效果结构
// ============================================================================
// 跟踪控制效果的递减（Diminishing Returns）
// 相同类型的控制效果在短时间内重复施加时效果会递减
// ============================================================================
struct DiminishingReturn
{
    DiminishingReturn() : stack(0), hitTime(0), hitCount(DIMINISHING_LEVEL_1) { }

    void Clear()
    {
        stack = 0;
        hitTime = 0;
        hitCount = DIMINISHING_LEVEL_1;
    }

    uint16                  stack;                          // 堆叠次数
    uint32                  hitTime;                        // 上次被命中的时间
    uint32                  hitCount;                       // 递减等级
};

// ============================================================================
// 近战命中结果枚举
// ============================================================================
// 描述近战攻击的命中结果类型
// ============================================================================
enum MeleeHitOutcome : uint8
{
    MELEE_HIT_EVADE,                                        // 规避（目标脱离战斗）
    MELEE_HIT_MISS,                                         // 未命中
    MELEE_HIT_DODGE,                                        // 闪避
    MELEE_HIT_BLOCK,                                        // 格挡
    MELEE_HIT_PARRY,                                        // 招架
    MELEE_HIT_GLANCING,                                     // 偏斜
    MELEE_HIT_CRIT,                                         // 暴击
    MELEE_HIT_CRUSHING,                                     // 碾压
    MELEE_HIT_NORMAL                                        // 普通命中
};

// ============================================================================
// DispelInfo 类 - 驱散信息
// ============================================================================
// 封装驱散法术的相关信息
// ============================================================================
class DispelInfo
{
    public:
        /**
         * @brief 构造函数
         * @param dispeller 驱散者
         * @param dispellerSpellId 驱散法术ID
         * @param chargesRemoved 移除的层数
         */
        explicit DispelInfo(WorldObject* dispeller, uint32 dispellerSpellId, uint8 chargesRemoved) :
            _dispeller(dispeller), _dispellerSpell(dispellerSpellId), _chargesRemoved(chargesRemoved) { }

        WorldObject* GetDispeller() const { return _dispeller; }
        uint32 GetDispellerSpellId() const { return _dispellerSpell; }
        uint8 GetRemovedCharges() const { return _chargesRemoved; }
        void SetRemovedCharges(uint8 amount) { _chargesRemoved = amount; }
    private:
        WorldObject* _dispeller;                             // 驱散者
        uint32 _dispellerSpell;                              // 驱散法术ID
        uint8 _chargesRemoved;                               // 移除的层数
};

// ============================================================================
// CleanDamage 结构 - 净伤害信息
// ============================================================================
// 存储经过减伤后的净伤害数据，用于怒气计算等
// ============================================================================
struct CleanDamage
{
    CleanDamage(uint32 mitigated, uint32 absorbed, WeaponAttackType _attackType, MeleeHitOutcome _hitOutCome) :
    absorbed_damage(absorbed), mitigated_damage(mitigated), attackType(_attackType), hitOutCome(_hitOutCome) { }

    uint32 absorbed_damage;                                 // 被吸收的伤害
    uint32 mitigated_damage;                                // 被减伤的伤害

    WeaponAttackType attackType;                            // 攻击类型
    MeleeHitOutcome hitOutCome;                             // 命中结果
};

struct CalcDamageInfo;                                      // 前向声明
struct SpellNonMeleeDamage;                                 // 前向声明

/**
 * @class DamageInfo
 * @brief 伤害信息容器
 *
 * 封装伤害计算的完整结果，用于 proc 系统、光环触发等。
 * 提供统一的伤害信息接口，无论是近战伤害还是法术伤害。
 *
 * @section damage_flow 伤害处理流程
 * 1. 创建 DamageInfo 实例（从 CalcDamageInfo 或 SpellNonMeleeDamage）
 * 2. 应用吸收效果（AbsorbDamage）
 * 3. 应用抵抗效果（ResistDamage）
 * 4. 应用格挡效果（BlockDamage）
 * 5. 处理最终伤害（DealDamage）
 *
 * @section proc_system 与触发系统的关系
 * DamageInfo 是 proc 系统的核心数据结构。
 * 当伤害发生时，系统会检查所有可能的触发效果，
 * 并根据 DamageInfo 中的信息决定是否触发。
 *
 * @see CalcDamageInfo
 * @see SpellNonMeleeDamage
 * @see HealInfo
 * @see ProcEventInfo
 */
class TC_GAME_API DamageInfo
{
    private:
        Unit* const m_attacker;                             ///< 攻击者
        Unit* const m_victim;                               ///< 受害者
        uint32 m_damage;                                    ///< 最终伤害
        SpellInfo const* const m_spellInfo;                 ///< 法术信息（物理攻击为 nullptr）
        SpellSchoolMask const m_schoolMask;                 ///< 伤害类型掩码
        DamageEffectType const m_damageType;                ///< 伤害效果类型
        WeaponAttackType m_attackType;                      ///< 攻击类型
        uint32 m_absorb;                                    ///< 被吸收量
        uint32 m_resist;                                    ///< 被抵抗量
        uint32 m_block;                                     ///< 被格挡量
        uint32 m_hitMask;                                   ///< 命中掩码

        /**
         * @brief 合并构造函数（用于 proc）
         * @param dmg1 第一个伤害信息
         * @param dmg2 第二个伤害信息
         *
         * 合并两个伤害信息，用于多段伤害的触发判定。
         */
        DamageInfo(DamageInfo const& dmg1, DamageInfo const& dmg2);

    public:
        /**
         * @brief 构造函数
         * @param attacker 攻击者
         * @param victim 受害者
         * @param damage 伤害值
         * @param spellInfo 法术信息
         * @param schoolMask 伤害学校掩码
         * @param damageType 伤害效果类型
         * @param attackType 攻击类型
         */
        DamageInfo(Unit* attacker, Unit* victim, uint32 damage, SpellInfo const* spellInfo, SpellSchoolMask schoolMask, DamageEffectType damageType, WeaponAttackType attackType);

        /**
         * @brief 从 CalcDamageInfo 构造（合并包装器）
         * @param dmgInfo 近战伤害计算结果
         */
        explicit DamageInfo(CalcDamageInfo const& dmgInfo);

        /**
         * @brief 从 CalcDamageInfo 构造（指定伤害索引）
         * @param dmgInfo 近战伤害计算结果
         * @param damageIndex 伤害索引（0=主手，1=副手）
         */
        DamageInfo(CalcDamageInfo const& dmgInfo, uint8 damageIndex);

        /**
         * @brief 从 SpellNonMeleeDamage 构造
         * @param spellNonMeleeDamage 法术伤害信息
         * @param damageType 伤害效果类型
         * @param attackType 攻击类型
         * @param hitMask 命中掩码
         */
        DamageInfo(SpellNonMeleeDamage const& spellNonMeleeDamage, DamageEffectType damageType, WeaponAttackType attackType, uint32 hitMask);

        /**
         * @brief 修改伤害值
         * @param amount 修改量（正数增加，负数减少）
         */
        void ModifyDamage(int32 amount);

        /**
         * @brief 吸收伤害
         * @param amount 吸收量
         *
         * 增加吸收量并减少最终伤害。
         */
        void AbsorbDamage(uint32 amount);

        /**
         * @brief 抵抗伤害
         * @param amount 抵抗量
         *
         * 增加抵抗量并减少最终伤害。
         */
        void ResistDamage(uint32 amount);

        /**
         * @brief 格挡伤害
         * @param amount 格挡量
         *
         * 增加格挡量并减少最终伤害。
         */
        void BlockDamage(uint32 amount);

        /// @name 访问器
        /// @{
        Unit* GetAttacker() const { return m_attacker; }
        Unit* GetVictim() const { return m_victim; }
        SpellInfo const* GetSpellInfo() const { return m_spellInfo; }
        SpellSchoolMask GetSchoolMask() const { return m_schoolMask; }
        DamageEffectType GetDamageType() const { return m_damageType; }
        WeaponAttackType GetAttackType() const { return m_attackType; }
        uint32 GetDamage() const { return m_damage; }
        uint32 GetAbsorb() const { return m_absorb; }
        uint32 GetResist() const { return m_resist; }
        uint32 GetBlock() const { return m_block; }
        uint32 GetHitMask() const;
        /// @}
};

/**
 * @class HealInfo
 * @brief 治疗信息容器
 *
 * 封装治疗计算的完整结果，结构与 DamageInfo 类似。
 * 用于 proc 系统、治疗加成计算等。
 *
 * @section heal_flow 治疗处理流程
 * 1. 创建 HealInfo 实例
 * 2. 应用治疗加成效果
 * 3. 计算过量治疗
 * 4. 应用治疗吸收（如牧师抑制）
 * 5. 处理最终治疗
 *
 * @section heal_absorb 治疗吸收
 * 某些效果可以"吸收"治疗量（如抑制效果）。
 * 吸收的治疗不会转化为生命值。
 *
 * @see DamageInfo
 * @see ProcEventInfo
 */
class TC_GAME_API HealInfo
{
    private:
        Unit* const _healer;                                ///< 治疗者
        Unit* const _target;                                ///< 目标
        uint32 _heal;                                       ///< 治疗量
        uint32 _effectiveHeal;                              ///< 实际治疗量（去除过量治疗）
        uint32 _absorb;                                     ///< 被吸收量
        SpellInfo const* const _spellInfo;                  ///< 法术信息
        SpellSchoolMask const _schoolMask;                  ///< 法术类型掩码
        uint32 _hitMask;                                    ///< 命中掩码

    public:
        /**
         * @brief 构造函数
         * @param healer 治疗者
         * @param target 目标
         * @param heal 治疗量
         * @param spellInfo 法术信息
         * @param schoolMask 法术学校掩码
         */
        HealInfo(Unit* healer, Unit* target, uint32 heal, SpellInfo const* spellInfo, SpellSchoolMask schoolMask);

        /**
         * @brief 吸收治疗
         * @param amount 吸收量
         *
         * 某些效果可以吸收治疗（如抑制效果）。
         */
        void AbsorbHeal(uint32 amount);

        /**
         * @brief 设置实际治疗量
         * @param amount 实际治疗量
         *
         * 在计算过量治疗后设置。
         */
        void SetEffectiveHeal(uint32 amount) { _effectiveHeal = amount; }

        /// @name 访问器
        /// @{
        Unit* GetHealer() const { return _healer; }
        Unit* GetTarget() const { return _target; }
        uint32 GetHeal() const { return _heal; }
        uint32 GetEffectiveHeal() const { return _effectiveHeal; }
        uint32 GetAbsorb() const { return _absorb; }
        SpellInfo const* GetSpellInfo() const { return _spellInfo; }
        SpellSchoolMask GetSchoolMask() const { return _schoolMask; }
        uint32 GetHitMask() const;
        /// @}
};

/**
 * @class ProcEventInfo
 * @brief 触发事件信息容器
 *
 * 封装触发事件（proc）的所有相关信息。
 * 用于光环触发、特效触发等场景。
 *
 * @section proc_system 触发系统概述
 * Proc（Programmed Random Occurrence）是一种机制，
 * 允许在特定事件发生时触发额外效果。
 *
 * @subsection proc_types 常见触发类型
 * - 攻击命中时触发（如风怒武器）
 * - 被攻击时触发（如反射盾）
 * - 施法时触发（如法术连击）
 * - 暴击时触发（如暴击回蓝）
 * - 治疗时触发（如治疗增效）
 *
 * @subsection proc_phases 法术阶段
 * - 准备阶段（PRE_CAST）
 * - 施法阶段（CAST）
 * - 命中阶段（HIT）
 * - 触发阶段（TRIGGER）
 *
 * @see DamageInfo
 * @see HealInfo
 */
class TC_GAME_API ProcEventInfo
{
    public:
        /**
         * @brief 构造函数
         * @param actor 行动者（触发事件者）
         * @param actionTarget 行动目标（被施法/攻击的目标）
         * @param procTarget 触发目标（触发效果作用的目标）
         * @param typeMask 触发类型掩码
         * @param spellTypeMask 法术类型掩码
         * @param spellPhaseMask 法术阶段掩码
         * @param hitMask 命中掩码
         * @param spell 当前法术实例
         * @param damageInfo 伤害信息（可为 nullptr）
         * @param healInfo 治疗信息（可为 nullptr）
         */
        ProcEventInfo(Unit* actor, Unit* actionTarget, Unit* procTarget, uint32 typeMask,
                      uint32 spellTypeMask, uint32 spellPhaseMask, uint32 hitMask,
                      Spell* spell, DamageInfo* damageInfo, HealInfo* healInfo);

        /// @name 基本信息
        /// @{
        Unit* GetActor() { return _actor; }                 ///< 获取行动者
        Unit* GetActionTarget() const { return _actionTarget; }  ///< 获取行动目标
        Unit* GetProcTarget() const { return _procTarget; } ///< 获取触发目标
        /// @}

        /// @name 掩码信息
        /// @{
        uint32 GetTypeMask() const { return _typeMask; }    ///< 获取触发类型掩码
        uint32 GetSpellTypeMask() const { return _spellTypeMask; }  ///< 获取法术类型掩码
        uint32 GetSpellPhaseMask() const { return _spellPhaseMask; }  ///< 获取法术阶段掩码
        uint32 GetHitMask() const { return _hitMask; }      ///< 获取命中掩码
        /// @}

        /**
         * @brief 获取法术信息
         * @return 法术信息，如果不是法术触发返回 nullptr
         */
        SpellInfo const* GetSpellInfo() const;

        /**
         * @brief 获取法术学校掩码
         * @return 法术学校掩码
         */
        SpellSchoolMask GetSchoolMask() const;

        /**
         * @brief 获取伤害信息
         * @return 伤害信息指针，如果不是伤害触发返回 nullptr
         */
        DamageInfo* GetDamageInfo() const { return _damageInfo; }

        /**
         * @brief 获取治疗信息
         * @return 治疗信息指针，如果不是治疗触发返回 nullptr
         */
        HealInfo* GetHealInfo() const { return _healInfo; }

        /**
         * @brief 获取触发法术
         * @return 法术实例指针，如果不是法术触发返回 nullptr
         */
        Spell const* GetProcSpell() const { return _spell; }

    private:
        Unit* const _actor;                                 ///< 行动者
        Unit* const _actionTarget;                          ///< 行动目标
        Unit* const _procTarget;                            ///< 触发目标
        uint32 _typeMask;                                   ///< 触发类型掩码
        uint32 _spellTypeMask;                              ///< 法术类型掩码
        uint32 _spellPhaseMask;                             ///< 法术阶段掩码
        uint32 _hitMask;                                    ///< 命中掩码
        Spell* _spell;                                      ///< 当前法术
        DamageInfo* _damageInfo;                            ///< 伤害信息
        HealInfo* _healInfo;                                ///< 治疗信息
};

// ============================================================================
// CalcDamageInfo 结构 - 近战伤害计算结果
// ============================================================================
// 用于 Unit::CalculateMeleeDamage 的内部计算
// 结构与 SMSG_ATTACKERSTATEUPDATE 操作码发送的数据匹配
// ============================================================================
struct CalcDamageInfo
{
    Unit* Attacker;                                         // 攻击者
    Unit* Target;                                           // 目标

    struct
    {
        uint32 DamageSchoolMask;                            // 伤害类型掩码
        uint32 Damage;                                      // 伤害值
        uint32 Absorb;                                      // 吸收量
        uint32 Resist;                                      // 抵抗量
    } Damages[2];                                           // 伤害数组[0=主手/基础, 1=副手/额外]

    uint32 Blocked;                                         // 格挡量
    uint32 HitInfo;                                         // 命中信息标志
    uint32 TargetState;                                     // 目标状态

    // 辅助字段
    WeaponAttackType AttackType;                            // 攻击类型
    uint32 ProcAttacker;                                    // 攻击者触发标志
    uint32 ProcVictim;                                      // 受害者触发标志
    uint32 CleanDamage;                                     // 净伤害（仅用于怒气计算）
    MeleeHitOutcome HitOutCome;                             // 命中结果（应使用 TargetState，待移除）
};

// ============================================================================
// SpellNonMeleeDamage 结构 - 法术伤害信息
// ============================================================================
// 基于 SMSG_SPELLNONMELEEDAMAGELOG 操作码发送的数据结构
// ============================================================================
struct TC_GAME_API SpellNonMeleeDamage
{
    SpellNonMeleeDamage(Unit* _attacker, Unit* _target, uint32 _SpellID, uint32 _schoolMask)
        : target(_target), attacker(_attacker), SpellID(_SpellID), damage(0), overkill(0), schoolMask(_schoolMask),
        absorb(0), resist(0), periodicLog(false), unused(false), blocked(0), HitInfo(0), cleanDamage(0), fullBlock(false)
    { }

    Unit   *target;                                         // 目标
    Unit   *attacker;                                       // 攻击者
    uint32 SpellID;                                         // 法术ID
    uint32 damage;                                          // 伤害值
    uint32 overkill;                                        // 过量伤害
    uint32 schoolMask;                                      // 伤害类型掩码
    uint32 absorb;                                          // 吸收量
    uint32 resist;                                          // 抵抗量
    bool   periodicLog;                                     // 是否为周期性日志
    bool   unused;                                          // 未使用
    uint32 blocked;                                         // 格挡量
    uint32 HitInfo;                                         // 命中信息
    // 辅助字段
    uint32 cleanDamage;                                     // 净伤害
    bool   fullBlock;                                       // 是否完全格挡
};

// ============================================================================
// SpellPeriodicAuraLogInfo 结构 - 周期性光环日志信息
// ============================================================================
// 存储周期性光环（DoT/HoT）的效果日志信息
// ============================================================================
struct SpellPeriodicAuraLogInfo
{
    SpellPeriodicAuraLogInfo(AuraEffect const* _auraEff, uint32 _damage, uint32 _overDamage, uint32 _absorb, uint32 _resist, float _multiplier, bool _critical)
        : auraEff(_auraEff), damage(_damage), overDamage(_overDamage), absorb(_absorb), resist(_resist), multiplier(_multiplier), critical(_critical){ }

    AuraEffect const* auraEff;                              // 光环效果
    uint32 damage;                                          // 伤害/治疗量
    uint32 overDamage;                                      // 过量伤害/过量治疗
    uint32 absorb;                                          // 吸收量
    uint32 resist;                                          // 抵抗量
    float  multiplier;                                      // 倍率
    bool   critical;                                        // 是否暴击
};

uint32 createProcHitMask(SpellNonMeleeDamage* damageInfo, SpellMissInfo missCondition);

enum CurrentSpellTypes : uint8
{
    CURRENT_MELEE_SPELL             = 0,
    CURRENT_GENERIC_SPELL           = 1,
    CURRENT_CHANNELED_SPELL         = 2,
    CURRENT_AUTOREPEAT_SPELL        = 3
};

#define CURRENT_FIRST_NON_MELEE_SPELL 1
#define CURRENT_MAX_SPELL             4

#define UNIT_ACTION_BUTTON_ACTION(X) (uint32(X) & 0x00FFFFFF)
#define UNIT_ACTION_BUTTON_TYPE(X)   ((uint32(X) & 0xFF000000) >> 24)
#define MAKE_UNIT_ACTION_BUTTON(A, T) (uint32(A) | (uint32(T) << 24))

struct UnitActionBarEntry
{
    UnitActionBarEntry() : packedData(uint32(ACT_DISABLED) << 24) { }

    uint32 packedData;

    // helper
    ActiveStates GetType() const { return ActiveStates(UNIT_ACTION_BUTTON_TYPE(packedData)); }
    uint32 GetAction() const { return UNIT_ACTION_BUTTON_ACTION(packedData); }
    bool IsActionBarForSpell() const
    {
        ActiveStates Type = GetType();
        return Type == ACT_DISABLED || Type == ACT_ENABLED || Type == ACT_PASSIVE;
    }

    void SetActionAndType(uint32 action, ActiveStates type)
    {
        packedData = MAKE_UNIT_ACTION_BUTTON(action, type);
    }

    void SetType(ActiveStates type)
    {
        packedData = MAKE_UNIT_ACTION_BUTTON(UNIT_ACTION_BUTTON_ACTION(packedData), type);
    }

    void SetAction(uint32 action)
    {
        packedData = (packedData & 0xFF000000) | UNIT_ACTION_BUTTON_ACTION(action);
    }
};

typedef std::list<Player*> SharedVisionList;

enum CharmType
{
    CHARM_TYPE_CHARM,
    CHARM_TYPE_POSSESS,
    CHARM_TYPE_VEHICLE,
    CHARM_TYPE_CONVERT
};

typedef UnitActionBarEntry CharmSpellInfo;

enum ActionBarIndex
{
    ACTION_BAR_INDEX_START = 0,
    ACTION_BAR_INDEX_PET_SPELL_START = 3,
    ACTION_BAR_INDEX_PET_SPELL_END = 7,
    ACTION_BAR_INDEX_END = 10
};

#define MAX_UNIT_ACTION_BAR_INDEX (ACTION_BAR_INDEX_END-ACTION_BAR_INDEX_START)

struct TC_GAME_API CharmInfo
{
    public:
        explicit CharmInfo(Unit* unit);
        ~CharmInfo();
        void RestoreState();
        uint32 GetPetNumber() const { return _petnumber; }
        void SetPetNumber(uint32 petnumber, bool statwindow);

        void SetCommandState(CommandStates st) { _CommandState = st; }
        CommandStates GetCommandState() const { return _CommandState; }
        bool HasCommandState(CommandStates state) const { return (_CommandState == state); }

        void InitPossessCreateSpells();
        void InitCharmCreateSpells();
        void InitPetActionBar();
        void InitEmptyActionBar(bool withAttack = true);

                                                            //return true if successful
        bool AddSpellToActionBar(SpellInfo const* spellInfo, ActiveStates newstate = ACT_DECIDE, uint8 preferredSlot = 0);
        bool RemoveSpellFromActionBar(uint32 spell_id);
        void LoadPetActionBar(const std::string& data);
        void BuildActionBar(WorldPacket* data);
        void SetSpellAutocast(SpellInfo const* spellInfo, bool state);
        void SetActionBar(uint8 index, uint32 spellOrAction, ActiveStates type)
        {
            PetActionBar[index].SetActionAndType(spellOrAction, type);
        }
        UnitActionBarEntry const* GetActionBarEntry(uint8 index) const { return &(PetActionBar[index]); }

        void ToggleCreatureAutocast(SpellInfo const* spellInfo, bool apply);

        CharmSpellInfo* GetCharmSpell(uint8 index) { return &(_charmspells[index]); }

        void SetIsCommandAttack(bool val);
        bool IsCommandAttack();
        void SetIsCommandFollow(bool val);
        bool IsCommandFollow();
        void SetIsAtStay(bool val);
        bool IsAtStay();
        void SetIsFollowing(bool val);
        bool IsFollowing();
        void SetIsReturning(bool val);
        bool IsReturning();
        void SaveStayPosition();
        void GetStayPosition(float &x, float &y, float &z);

    private:

        Unit* _unit;
        UnitActionBarEntry PetActionBar[MAX_UNIT_ACTION_BAR_INDEX];
        CharmSpellInfo _charmspells[4];
        CommandStates _CommandState;
        uint32 _petnumber;

        //for restoration after charmed
        ReactStates     _oldReactState;

        bool _isCommandAttack;
        bool _isCommandFollow;
        bool _isAtStay;
        bool _isFollowing;
        bool _isReturning;
        float _stayX;
        float _stayY;
        float _stayZ;
};

// for clearing special attacks
#define REACTIVE_TIMER_START 4000

enum ReactiveType
{
    REACTIVE_DEFENSE        = 0,
    REACTIVE_HUNTER_PARRY   = 1,
    REACTIVE_OVERPOWER      = 2,
    REACTIVE_WOLVERINE_BITE = 3,
    MAX_REACTIVE
};

struct PositionUpdateInfo
{
    void Reset()
    {
        Relocated = false;
        Turned = false;
    }

    bool Relocated = false;
    bool Turned = false;
};

// delay time next attack to prevent client attack animation problems
#define ATTACK_DISPLAY_DELAY 200
#define MAX_PLAYER_STEALTH_DETECT_RANGE 30.0f               // max distance for detection targets by player

/**
 * @class Unit
 * @brief 单位基类 - 所有可战斗游戏实体的基类
 *
 * Unit 是游戏中所有可战斗实体（玩家、生物、宠物、图腾等）的基类。
 * 它提供了战斗系统、法术系统、光环系统、移动系统等核心功能。
 *
 * @section unit_responsibilities 职责
 * - @subsubsection attr_management 属性管理
 *   - 生命值、法力值、能量值的管理
 *   - 基础属性（力量、敏捷、耐力、智力、精神）的管理
 *   - 抗性值的管理
 *   - 等级、种族、职业、性别的管理
 *
 * - @subsubsection combat_system 战斗系统
 *   - 近战攻击处理（主手、副手、远程）
 *   - 伤害计算和分配
 *   - 威胁管理和仇恨列表
 *   - 战斗状态管理
 *   - 免疫状态管理
 *
 * - @subsubsection spell_system 法术系统
 *   - 法术施放管理
 *   - 光环（Buff/Debuff）管理
 *   - 法术冷却管理
 *   - 法术免疫管理
 *
 * - @subsubsection movement_system 移动系统
 *   - 移动速度管理（行走、跑步、游泳、飞行等）
 *   - 移动生成器管理
 *   - 位置更新和同步
 *   - 载具系统支持
 *
 * - @subsubsection ai_system AI 系统
 *   - AI 实例管理
 *   - AI 状态切换（支持 AI 栈）
 *   - 魅惑/附身 AI 处理
 *
 * - @subsubsection state_management 状态管理
 *   - 单位状态标志（昏迷、恐惧、定身等）
 *   - 死亡状态管理
 *   - 控制状态管理
 *
 * @section inheritance 继承关系
 * @code
 * Object -> WorldObject -> Unit -> Player
 *                       -> Unit -> Creature -> Pet
 *                                         -> Totem
 *                                         -> TempSummon
 * @endcode
 *
 * @section subsystems 组合的子系统
 * - ThreatManager：威胁管理器
 * - CombatManager：战斗管理器
 * - MotionMaster：移动生成器管理器
 * - SpellHistory：法术历史（冷却管理）
 * - Vehicle：载具系统（可选）
 * - UnitAI：AI 系统
 *
 * @section thread_safety 线程安全
 * Unit 的所有操作必须在地图线程中执行。
 * 跨线程访问需要通过消息传递机制。
 *
 * @see WorldObject
 * @see Player
 * @see Creature
 * @see ThreatManager
 * @see CombatManager
 * @see MotionMaster
 */
class TC_GAME_API Unit : public WorldObject
{
    friend class WorldSession;
    public:
        // ====================================================================
        // 类型定义
        // ====================================================================
        typedef std::set<Unit*> AttackerSet;            // 攻击者集合类型
        typedef std::set<Unit*> ControlList;            // 控制列表类型
        typedef std::vector<Unit*> UnitVector;          // 单位向量类型

        typedef std::multimap<uint32, Aura*> AuraMap;   // 光环映射（法术ID -> 光环）
        typedef std::pair<AuraMap::const_iterator, AuraMap::const_iterator> AuraMapBounds;
        typedef std::pair<AuraMap::iterator, AuraMap::iterator> AuraMapBoundsNonConst;

        typedef std::multimap<uint32,  AuraApplication*> AuraApplicationMap;  // 光环应用映射
        typedef std::pair<AuraApplicationMap::const_iterator, AuraApplicationMap::const_iterator> AuraApplicationMapBounds;
        typedef std::pair<AuraApplicationMap::iterator, AuraApplicationMap::iterator> AuraApplicationMapBoundsNonConst;

        typedef std::multimap<AuraStateType,  AuraApplication*> AuraStateAurasMap;  // 光环状态映射
        typedef std::pair<AuraStateAurasMap::const_iterator, AuraStateAurasMap::const_iterator> AuraStateAurasMapBounds;

        typedef std::list<AuraEffect*> AuraEffectList;  // 光环效果列表
        typedef std::list<Aura*> AuraList;              // 光环列表
        typedef std::list<AuraApplication*> AuraApplicationList;  // 光环应用列表
        typedef std::array<DiminishingReturn, DIMINISHING_MAX> Diminishing;  // 递减效果数组

        typedef std::vector<std::pair<uint8 /*procEffectMask*/, AuraApplication*>> AuraApplicationProcContainer;

        typedef std::map<uint8, AuraApplication*> VisibleAuraMap;  // 可见光环映射

        virtual ~Unit();

        // ====================================================================
        // AI 系统
        // ====================================================================

        /**
         * @brief 检查AI是否启用
         * @return 如果AI实例存在返回 true
         */
        bool IsAIEnabled() const { return (i_AI != nullptr); }

        /**
         * @brief AI 更新时钟
         * @param diff 距离上次更新的时间（毫秒）
         *
         * 每帧调用一次，驱动AI逻辑更新。
         * 由 Unit::Update 调用。
         */
        void AIUpdateTick(uint32 diff);

        /**
         * @brief 获取当前AI实例
         * @return 当前AI指针，可能为 nullptr
         */
        UnitAI* GetAI() const { return i_AI.get(); }

        /**
         * @brief 调度AI更改
         *
         * 标记需要在下次更新时切换AI。
         * 用于延迟AI切换以避免在AI更新中切换。
         */
        void ScheduleAIChange();

        /**
         * @brief 推送新AI到栈
         * @param newAI 新的AI实例
         *
         * 将当前AI压栈并切换到新AI。
         * 用于临时AI切换（如魅惑）。
         */
        void PushAI(UnitAI* newAI);

        /**
         * @brief 弹出AI栈顶
         * @return 如果成功弹出返回 true，栈为空返回 false
         *
         * 恢复到之前的AI状态。
         */
        bool PopAI();

    protected:
        void SetAI(UnitAI* newAI);
        UnitAI* GetTopAI() const { return i_AIs.empty() ? nullptr : i_AIs.top().get(); }
        void RefreshAI();
        UnitAI* GetScheduledChangeAI();
        bool HasScheduledAIChange() const;
    public:

        /**
         * @brief 将单位添加到世界
         *
         * 重写 WorldObject::AddToWorld。
         * 执行：初始化AI、注册到地图、发送创建包等。
         */
        void AddToWorld() override;

        /**
         * @brief 将单位从世界移除
         *
         * 重写 WorldObject::RemoveFromWorld。
         * 执行：清理战斗、移除光环、发送销毁包等。
         */
        void RemoveFromWorld() override;

        void CleanupBeforeRemoveFromMap(bool finalCleanup);

        /**
         * @brief 删除前的清理
         * @param finalCleanup 是否为最终清理
         *
         * 在析构函数或批量删除前调用。
         * 移除所有交叉引用以避免悬空指针。
         */
        void CleanupsBeforeDelete(bool finalCleanup = true) override;

        uint32 GetDynamicFlags() const override { return GetUInt32Value(UNIT_DYNAMIC_FLAGS); }
        void ReplaceAllDynamicFlags(uint32 flag) override { SetUInt32Value(UNIT_DYNAMIC_FLAGS, flag); }

        virtual bool IsAffectedByDiminishingReturns() const { return (GetCharmerOrOwnerPlayerOrPlayerItself() != nullptr); }
        DiminishingLevels GetDiminishing(DiminishingGroup group) const;
        void IncrDiminishing(SpellInfo const* auraSpellInfo, bool triggered);
        bool ApplyDiminishingToDuration(SpellInfo const* auraSpellInfo, bool triggered, int32& duration, WorldObject* caster, DiminishingLevels previousLevel) const;
        void ApplyDiminishingAura(DiminishingGroup group, bool apply);
        void ClearDiminishings();

        // ====================================================================
        // 核心更新函数
        // ====================================================================

        /**
         * @brief 更新单位状态
         * @param time 距离上次更新的时间（毫秒）
         *
         * 每帧调用一次，是单位的主更新循环。
         *
         * @section update_order 更新顺序
         * 1. 攻击计时器更新 - 检查是否可以进行下一次攻击
         * 2. 法术施放更新 - 更新正在施放的法术
         * 3. 光环更新 - 更新所有光环效果
         * 4. 移动更新 - 更新移动样条位置
         * 5. AI 更新 - 调用 AI 的 UpdateAI
         * 6. 再生更新 - 生命值和能量恢复
         * 7. 各种计时器更新
         *
         * @note 派生类（Player、Creature）会扩展此函数。
         * @warning 不应在 Update 中执行耗时过长的操作，以免影响服务器性能。
         */
        virtual void Update(uint32 time) override;

        // ====================================================================
        // 攻击计时器管理
        // ====================================================================

        /**
         * @brief 设置攻击计时器
         * @param type 武器攻击类型（主手、副手、远程）
         * @param time 计时器值（毫秒）
         */
        void setAttackTimer(WeaponAttackType type, uint32 time) { m_attackTimer[type] = time; }

        /**
         * @brief 重置攻击计时器
         * @param type 武器攻击类型，默认为 BASE_ATTACK（主手）
         *
         * 将计时器设置为当前武器的攻击间隔。
         */
        void resetAttackTimer(WeaponAttackType type = BASE_ATTACK);

        /**
         * @brief 获取攻击计时器当前值
         * @param type 武器攻击类型
         * @return 计时器值（毫秒）
         */
        uint32 getAttackTimer(WeaponAttackType type) const { return m_attackTimer[type]; }

        /**
         * @brief 检查攻击是否就绪
         * @param type 武器攻击类型，默认为 BASE_ATTACK
         * @return 如果计时器归零返回 true
         *
         * 表示可以执行下一次攻击。
         */
        bool isAttackReady(WeaponAttackType type = BASE_ATTACK) const { return m_attackTimer[type] == 0; }

        /**
         * @brief 检查是否有副手武器
         * @return 如果装备了副手武器返回 true
         */
        bool haveOffhandWeapon() const;

        /**
         * @brief 检查是否可以双持武器
         * @return 如果可以双持返回 true
         */
        bool CanDualWield() const { return m_canDualWield; }

        /**
         * @brief 设置是否可以双持武器
         * @param value 是否可以双持
         */
        virtual void SetCanDualWield(bool value) { m_canDualWield = value; }

        // ====================================================================
        // 战斗范围
        // ====================================================================

        /**
         * @brief 获取战斗触及距离
         * @return 战斗触及距离（码）
         *
         * 单位能够攻击的最大距离。
         * 用于判定攻击范围和路径阻挡。
         */
        float GetCombatReach() const override { return GetFloatValue(UNIT_FIELD_COMBATREACH); }

        /**
         * @brief 设置战斗触及距离
         * @param combatReach 战斗触及距离（码）
         */
        void SetCombatReach(float combatReach) { SetFloatValue(UNIT_FIELD_COMBATREACH, combatReach); }

        /**
         * @brief 获取边界半径
         * @return 边界半径（码）
         *
         * 单位的碰撞体积半径。
         * 用于碰撞检测和移动阻挡。
         */
        float GetBoundingRadius() const { return GetFloatValue(UNIT_FIELD_BOUNDINGRADIUS); }

        /**
         * @brief 设置边界半径
         * @param boundingRadius 边界半径（码）
         */
        void SetBoundingRadius(float boundingRadius) { SetFloatValue(UNIT_FIELD_BOUNDINGRADIUS, boundingRadius); }

        /**
         * @brief 检查是否在战斗范围内
         * @param obj 目标单位
         * @param dist2compare 比较距离
         * @return 如果在范围内返回 true
         *
         * 考虑双方的战斗触及距离。
         */
        bool IsWithinCombatRange(Unit const* obj, float dist2compare) const;

        /**
         * @brief 检查是否在近战范围内
         * @param obj 目标单位
         * @return 如果在近战范围内返回 true
         *
         * 使用当前位置检查。
         */
        bool IsWithinMeleeRange(Unit const* obj) const { return IsWithinMeleeRangeAt(GetPosition(), obj); }

        /**
         * @brief 检查指定位置是否在近战范围内
         * @param pos 检查位置
         * @param obj 目标单位
         * @return 如果在近战范围内返回 true
         */
        bool IsWithinMeleeRangeAt(Position const& pos, Unit const* obj) const;

        /**
         * @brief 获取近战攻击距离
         * @param target 目标单位
         * @return 近战距离（码）
         *
         * 计算双方的战斗触及距离之和。
         */
        float GetMeleeRange(Unit const* target) const;

        /**
         * @brief 获取近战伤害的法术学校掩码
         * @param attackType 攻击类型
         * @param damageIndex 伤害索引
         * @return 法术学校掩码
         *
         * 纯虚函数，由派生类实现。
         * 通常返回物理伤害掩码。
         */
        virtual SpellSchoolMask GetMeleeDamageSchoolMask(WeaponAttackType attackType = BASE_ATTACK, uint8 damageIndex = 0) const = 0;

        // ====================================================================
        // 武器能力
        // ====================================================================

        /**
         * @brief 是否可以双持武器
         *
         * 公开成员变量，允许外部直接访问。
         * 由 SetCanDualWield 设置。
         */
        bool m_canDualWield;

        // ====================================================================
        // 攻击者管理
        // ====================================================================

        /**
         * @brief 添加攻击者（内部函数）
         * @param pAttacker 攻击者
         *
         * 仅从 Unit::Attack 调用。
         * 将攻击者添加到 m_attackers 集合。
         */
        void _addAttacker(Unit* pAttacker);

        /**
         * @brief 移除攻击者（内部函数）
         * @param pAttacker 攻击者
         *
         * 仅从 Unit::AttackStop 调用。
         * 从 m_attackers 集合移除攻击者。
         */
        void _removeAttacker(Unit* pAttacker);

        /**
         * @brief 获取帮助者应该攻击的目标
         * @return 应该被攻击的目标，可能为 nullptr
         *
         * 用于守卫、宠物等帮助玩家的逻辑。
         * 优先返回正在攻击此单位的目标。
         */
        Unit* getAttackerForHelper() const;

        // ====================================================================
        // 攻击行为
        // ====================================================================

        /**
         * @brief 开始攻击目标
         * @param victim 攻击目标
         * @param meleeAttack 是否为近战攻击
         * @return 如果成功开始攻击返回 true
         *
         * 设置攻击目标、启动近战攻击循环。
         * 会停止当前法术施放（除自动射击外）。
         * 进入战斗状态（如果尚未在战斗中）。
         */
        bool Attack(Unit* victim, bool meleeAttack);

        /**
         * @brief 停止施法
         * @param except_spellid 不中断的法术ID（0表示中断所有）
         *
         * 中断所有正在施放的法术。
         */
        void CastStop(uint32 except_spellid = 0);

        /**
         * @brief 停止攻击
         * @return 如果之前在攻击返回 true
         *
         * 停止近战攻击循环，清除攻击目标。
         * 不会退出战斗状态。
         */
        bool AttackStop();

        /**
         * @brief 移除所有攻击者
         *
         * 清空攻击者列表。
         * 在死亡或脱离战斗时调用。
         */
        void RemoveAllAttackers();

        /**
         * @brief 获取攻击者集合
         * @return 攻击者集合的常引用
         */
        AttackerSet const& getAttackers() const { return m_attackers; }

        /**
         * @brief 检查是否正在攻击玩家
         * @return 如果攻击目标是玩家返回 true
         */
        bool isAttackingPlayer() const;

        /**
         * @brief 获取当前攻击目标
         * @return 当前攻击目标，可能为 nullptr
         */
        Unit* GetVictim() const { return m_attacking; }

        /**
         * @brief 确保获取有效的攻击目标
         * @return 当前攻击目标（断言非空）
         *
         * 仅在100%确定有受害者时使用。
         * 如果 m_attacking 为空会触发断言失败。
         */
        Unit* EnsureVictim() const
        {
            ASSERT(m_attacking);
            return m_attacking;
        }

        /**
         * @brief 验证攻击者和自己的目标
         *
         * 检查攻击者列表和攻击目标的有效性。
         * 清理已删除或无效的目标。
         */
        void ValidateAttackersAndOwnTarget();

        // ====================================================================
        // 战斗控制
        // ====================================================================

        /**
         * @brief 停止战斗
         * @param includingCast 是否同时停止施法
         * @param mutualPvP 是否互相停止PvP战斗
         *
         * 退出战斗状态，清除战斗列表。
         */
        void CombatStop(bool includingCast = false, bool mutualPvP = true);

        /**
         * @brief 停止战斗（包括宠物）
         * @param includingCast 是否同时停止施法
         *
         * 同时停止此单位和其宠物的战斗。
         */
        void CombatStopWithPets(bool includingCast = false);

        /**
         * @brief 停止攻击指定阵营
         * @param faction_id 阵营ID
         *
         * 停止攻击属于指定阵营的单位。
         * 用于阵营变更等情况。
         */
        void StopAttackFaction(uint32 faction_id);

        /**
         * @brief 选择附近目标
         * @param exclude 排除的单位（可为 nullptr）
         * @param dist 搜索距离，默认为近战距离
         * @return 选中的目标，可能为 nullptr
         *
         * 从附近单位中随机选择一个敌对目标。
         */
        Unit* SelectNearbyTarget(Unit* exclude = nullptr, float dist = NOMINAL_MELEE_RANGE) const;

        /**
         * @brief 发送近战攻击停止包
         * @param victim 攻击目标（可为 nullptr）
         *
         * 通知客户端近战攻击已停止。
         */
        void SendMeleeAttackStop(Unit* victim = nullptr);

        /**
         * @brief 发送近战攻击开始包
         * @param victim 攻击目标
         *
         * 通知客户端近战攻击已开始。
         */
        void SendMeleeAttackStart(Unit* victim);

        // ====================================================================
        // 单位状态管理
        // ====================================================================
        void AddUnitState(uint32 f) { m_state |= f; }
        bool HasUnitState(const uint32 f) const { return (m_state & f) != 0; }
        void ClearUnitState(uint32 f) { m_state &= ~f; }
        bool CanFreeMove() const;

        // ====================================================================
        // 单位类型判断
        // ====================================================================
        uint32 HasUnitTypeMask(uint32 mask) const { return mask & m_unitTypeMask; }
        void AddUnitTypeMask(uint32 mask) { m_unitTypeMask |= mask; }
        bool IsSummon() const   { return (m_unitTypeMask & UNIT_MASK_SUMMON) != 0; }      // 是否为召唤物
        bool IsGuardian() const { return (m_unitTypeMask & UNIT_MASK_GUARDIAN) != 0; }    // 是否为守护者
        bool IsPet() const      { return (m_unitTypeMask & UNIT_MASK_PET) != 0; }         // 是否为宠物
        bool IsHunterPet() const{ return (m_unitTypeMask & UNIT_MASK_HUNTER_PET) != 0; }  // 是否为猎人宠物
        bool IsTotem() const    { return (m_unitTypeMask & UNIT_MASK_TOTEM) != 0; }       // 是否为图腾
        bool IsVehicle() const  { return (m_unitTypeMask & UNIT_MASK_VEHICLE) != 0; }     // 是否为载具

        // ====================================================================
        // 基础属性：等级、种族、职业、性别
        // ====================================================================
        uint8 GetLevel() const { return uint8(GetUInt32Value(UNIT_FIELD_LEVEL)); }
        uint8 GetLevelForTarget(WorldObject const* /*target*/) const override { return GetLevel(); }
        void SetLevel(uint8 lvl, bool sendUpdate = true);
        uint8 GetRace() const { return GetByteValue(UNIT_FIELD_BYTES_0, UNIT_BYTES_0_OFFSET_RACE); }
        void SetRace(uint8 race) { SetByteValue(UNIT_FIELD_BYTES_0, UNIT_BYTES_0_OFFSET_RACE, race); }
        uint32 GetRaceMask() const { return 1 << (GetRace() - 1); }
        uint8 GetClass() const { return GetByteValue(UNIT_FIELD_BYTES_0, UNIT_BYTES_0_OFFSET_CLASS); }
        void SetClass(uint8 classId) { SetByteValue(UNIT_FIELD_BYTES_0, UNIT_BYTES_0_OFFSET_CLASS, classId); }
        uint32 GetClassMask() const { return 1 << (GetClass() - 1); }
        Gender GetGender() const { return Gender(GetByteValue(UNIT_FIELD_BYTES_0, UNIT_BYTES_0_OFFSET_GENDER)); }
        void SetGender(Gender gender) { SetByteValue(UNIT_FIELD_BYTES_0, UNIT_BYTES_0_OFFSET_GENDER, gender); }
        virtual Gender GetNativeGender() const { return GetGender(); }
        virtual void SetNativeGender(Gender gender) { SetGender(gender); }

        // ====================================================================
        // 属性值：力量、敏捷、耐力、智力、精神
        // ====================================================================
        float GetStat(Stats stat) const { return float(GetUInt32Value(UNIT_FIELD_STAT0 + int32(stat))); }
        void SetStat(Stats stat, int32 val) { SetStatInt32Value(UNIT_FIELD_STAT0 + int32(stat), val); }
        uint32 GetArmor() const { return GetResistance(SPELL_SCHOOL_NORMAL); }
        void SetArmor(int32 val) { SetResistance(SPELL_SCHOOL_NORMAL, val); }

        // ====================================================================
        // 抗性值
        // ====================================================================
        int32 GetResistance(SpellSchools school) const { return GetInt32Value(UNIT_FIELD_RESISTANCES + int32(school)); }
        int32 GetResistance(SpellSchoolMask mask) const;
        void SetResistance(SpellSchools school, int32 val) { SetStatInt32Value(UNIT_FIELD_RESISTANCES + int32(school), val); }
        static float CalculateAverageResistReduction(WorldObject const* caster, SpellSchoolMask schoolMask, Unit const* victim, SpellInfo const* spellInfo = nullptr);

        // ====================================================================
        // 生命值管理
        // ====================================================================

        /**
         * @brief 获取当前生命值
         * @return 当前生命值
         */
        uint32 GetHealth()    const { return GetUInt32Value(UNIT_FIELD_HEALTH); }

        /**
         * @brief 获取最大生命值
         * @return 最大生命值
         */
        uint32 GetMaxHealth() const { return GetUInt32Value(UNIT_FIELD_MAXHEALTH); }

        /**
         * @brief 检查是否满血
         * @return 如果当前生命值等于最大生命值返回 true
         */
        bool IsFullHealth() const { return GetHealth() == GetMaxHealth(); }

        /**
         * @brief 检查生命值是否低于指定百分比
         * @param pct 百分比值（0-100）
         * @return 如果低于指定百分比返回 true
         */
        bool HealthBelowPct(int32 pct) const { return GetHealth() < CountPctFromMaxHealth(pct); }

        /**
         * @brief 检查生命值在受到伤害后是否低于指定百分比
         * @param pct 百分比值（0-100）
         * @param damage 即将受到的伤害
         * @return 如果伤害后低于指定百分比返回 true
         */
        bool HealthBelowPctDamaged(int32 pct, uint32 damage) const { return int64(GetHealth()) - int64(damage) < int64(CountPctFromMaxHealth(pct)); }

        /**
         * @brief 检查生命值是否高于指定百分比
         * @param pct 百分比值（0-100）
         * @return 如果高于指定百分比返回 true
         */
        bool HealthAbovePct(int32 pct) const { return GetHealth() > CountPctFromMaxHealth(pct); }

        /**
         * @brief 检查生命值在治疗后是否高于指定百分比
         * @param pct 百分比值（0-100）
         * @param heal 即将获得的治疗
         * @return 如果治疗后高于指定百分比返回 true
         */
        bool HealthAbovePctHealed(int32 pct, uint32 heal) const { return uint64(GetHealth()) + uint64(heal) > CountPctFromMaxHealth(pct); }

        /**
         * @brief 获取当前生命值百分比
         * @return 生命值百分比（0.0-100.0）
         */
        float GetHealthPct() const { return GetMaxHealth() ? 100.f * GetHealth() / GetMaxHealth() : 0.0f; }

        /**
         * @brief 计算最大生命值的指定百分比
         * @param pct 百分比值（0-100）
         * @return 最大生命值的指定百分比的值
         */
        uint32 CountPctFromMaxHealth(int32 pct) const { return CalculatePct(GetMaxHealth(), pct); }

        /**
         * @brief 计算当前生命值的指定百分比
         * @param pct 百分比值（0-100）
         * @return 当前生命值的指定百分比的值
         */
        uint32 CountPctFromCurHealth(int32 pct) const { return CalculatePct(GetHealth(), pct); }

        /**
         * @brief 设置当前生命值
         * @param val 新的生命值
         *
         * 会自动限制在 [0, MaxHealth] 范围内。
         * 触发生命值更新事件。
         */
        void SetHealth(uint32 val);

        /**
         * @brief 设置最大生命值
         * @param val 新的最大生命值
         *
         * 如果当前生命值超过新的最大值，会被调整。
         * 触发属性更新。
         */
        void SetMaxHealth(uint32 val);

        /**
         * @brief 设置为满血
         *
         * 便捷函数，将生命值设置为最大值。
         */
        inline void SetFullHealth() { SetHealth(GetMaxHealth()); }

        /**
         * @brief 修改生命值
         * @param val 生命值变化量（正数为治疗，负数为伤害）
         * @return 实际变化量
         *
         * 处理治疗或伤害，考虑上下限。
         * 会触发相关光环效果（如治疗加成、伤害吸收等）。
         */
        int32 ModifyHealth(int32 val);

        /**
         * @brief 计算生命值增益
         * @param dVal 拟议的生命值变化
         * @return 实际可以获得的增益量
         *
         * 预测ModifyHealth的结果，不实际修改。
         */
        int32 GetHealthGain(int32 dVal);

        // ====================================================================
        // 能量值管理（法力、怒气、集中、能量、快乐值、符文、灵魂碎片等）
        // ====================================================================

        /**
         * @brief 获取能量类型
         * @return 当前能量类型（Powers枚举）
         *
         * 可能的值：MANA、RAGE、FOCUS、ENERGY、HAPPINESS、RUNE、RUNIC_POWER等。
         * 不同职业有不同的能量类型。
         */
        Powers GetPowerType() const { return Powers(GetByteValue(UNIT_FIELD_BYTES_0, UNIT_BYTES_0_OFFSET_POWER_TYPE)); }

        /**
         * @brief 设置能量类型
         * @param power 新的能量类型
         * @param sendUpdate 是否发送更新包给客户端
         *
         * 切换单位的能量类型。
         * 例如：德鲁伊变形会切换能量类型。
         */
        void SetPowerType(Powers power, bool sendUpdate = true);

        /**
         * @brief 更新显示的能量类型
         *
         * 向客户端发送当前能量类型的更新。
         */
        void UpdateDisplayPower();

        /**
         * @brief 获取指定类型能量的当前值
         * @param power 能量类型
         * @return 当前能量值
         */
        uint32 GetPower(Powers power) const { return GetUInt32Value(UNIT_FIELD_POWER1 + int32(power)); }

        /**
         * @brief 获取指定类型能量的最大值
         * @param power 能量类型
         * @return 最大能量值
         */
        uint32 GetMaxPower(Powers power) const { return GetUInt32Value(UNIT_FIELD_MAXPOWER1 + int32(power)); }

        /**
         * @brief 获取指定类型能量的百分比
         * @param power 能量类型
         * @return 能量百分比（0.0-100.0）
         */
        float GetPowerPct(Powers power) const { return GetMaxPower(power) ? 100.f * GetPower(power) / GetMaxPower(power) : 0.0f; }

        /**
         * @brief 计算最大能量的指定百分比
         * @param power 能量类型
         * @param pct 百分比值（0-100）
         * @return 最大能量的指定百分比的值
         */
        int32 CountPctFromMaxPower(Powers power, int32 pct) const { return CalculatePct(GetMaxPower(power), pct); }

        /**
         * @brief 设置能量值
         * @param power 能量类型
         * @param val 新的能量值
         * @param withPowerUpdate 是否触发能量更新事件
         * @param force 是否强制设置（跳过某些限制）
         *
         * 会自动限制在 [0, MaxPower] 范围内。
         */
        void SetPower(Powers power, uint32 val, bool withPowerUpdate = true, bool force = false);

        /**
         * @brief 设置最大能量值
         * @param power 能量类型
         * @param val 新的最大能量值
         */
        void SetMaxPower(Powers power, uint32 val);

        /**
         * @brief 设置能量为满
         * @param power 能量类型
         *
         * 便捷函数，将能量设置为最大值。
         */
        inline void SetFullPower(Powers power) { SetPower(power, GetMaxPower(power)); }

        /**
         * @brief 修改能量值
         * @param power 能量类型
         * @param val 能量变化量（正数增加，负数减少）
         * @param withPowerUpdate 是否触发能量更新事件
         * @return 实际变化量
         *
         * 处理能量增加或减少。
         * 可能触发相关光环效果（如能量恢复、消耗减免等）。
         */
        int32 ModifyPower(Powers power, int32 val, bool withPowerUpdate = true);

        uint32 GetAttackTime(WeaponAttackType att) const;
        void SetAttackTime(WeaponAttackType att, uint32 val) { SetFloatValue(UNIT_FIELD_BASEATTACKTIME + int32(att), val * m_modAttackSpeedPct[att]); }
        void ApplyAttackTimePercentMod(WeaponAttackType att, float val, bool apply);
        void ApplyCastTimePercentMod(float val, bool apply);

        void SetModCastingSpeed(float castingSpeed) { SetFloatValue(UNIT_MOD_CAST_SPEED, castingSpeed); }

        UnitFlags GetUnitFlags() const { return UnitFlags(GetUInt32Value(UNIT_FIELD_FLAGS)); }
        bool HasUnitFlag(UnitFlags flags) const { return HasFlag(UNIT_FIELD_FLAGS, flags); }
        void SetUnitFlag(UnitFlags flags) { SetFlag(UNIT_FIELD_FLAGS, flags); }
        void RemoveUnitFlag(UnitFlags flags) { RemoveFlag(UNIT_FIELD_FLAGS, flags); }
        void ReplaceAllUnitFlags(UnitFlags flags) { SetUInt32Value(UNIT_FIELD_FLAGS, flags); }

        UnitFlags2 GetUnitFlags2() const { return UnitFlags2(GetUInt32Value(UNIT_FIELD_FLAGS_2)); }
        bool HasUnitFlag2(UnitFlags2 flags) const { return HasFlag(UNIT_FIELD_FLAGS_2, flags); }
        void SetUnitFlag2(UnitFlags2 flags) { SetFlag(UNIT_FIELD_FLAGS_2, flags); }
        void RemoveUnitFlag2(UnitFlags2 flags) { RemoveFlag(UNIT_FIELD_FLAGS_2, flags); }
        void ReplaceAllUnitFlags2(UnitFlags2 flags) { SetUInt32Value(UNIT_FIELD_FLAGS_2, flags); }

        void SetCreatedBySpell(int32 spellId) { SetUInt32Value(UNIT_CREATED_BY_SPELL, spellId); }

        Emote GetEmoteState() const { return Emote(GetUInt32Value(UNIT_NPC_EMOTESTATE)); }
        void SetEmoteState(Emote emote) { SetUInt32Value(UNIT_NPC_EMOTESTATE, emote); }

        SheathState GetSheath() const { return SheathState(GetByteValue(UNIT_FIELD_BYTES_2, UNIT_BYTES_2_OFFSET_SHEATH_STATE)); }
        virtual void SetSheath(SheathState sheathed) { SetByteValue(UNIT_FIELD_BYTES_2, UNIT_BYTES_2_OFFSET_SHEATH_STATE, sheathed); }

        // faction template id
        uint32 GetFaction() const override { return GetUInt32Value(UNIT_FIELD_FACTIONTEMPLATE); }
        void SetFaction(uint32 faction) override { SetUInt32Value(UNIT_FIELD_FACTIONTEMPLATE, faction); }

        bool IsInPartyWith(Unit const* unit) const;
        bool IsInRaidWith(Unit const* unit) const;
        void GetPartyMembers(std::list<Unit*> &units);
        bool IsContestedGuard() const;

        UnitPVPStateFlags GetPvpFlags() const { return UnitPVPStateFlags(GetByteValue(UNIT_FIELD_BYTES_2, UNIT_BYTES_2_OFFSET_PVP_FLAG)); }
        bool HasPvpFlag(UnitPVPStateFlags flags) const { return HasByteFlag(UNIT_FIELD_BYTES_2, UNIT_BYTES_2_OFFSET_PVP_FLAG, flags); }
        void SetPvpFlag(UnitPVPStateFlags flags) { SetByteFlag(UNIT_FIELD_BYTES_2, UNIT_BYTES_2_OFFSET_PVP_FLAG, flags); }
        void RemovePvpFlag(UnitPVPStateFlags flags) { RemoveByteFlag(UNIT_FIELD_BYTES_2, UNIT_BYTES_2_OFFSET_PVP_FLAG, flags); }
        void ReplaceAllPvpFlags(UnitPVPStateFlags flags) { SetByteValue(UNIT_FIELD_BYTES_2, UNIT_BYTES_2_OFFSET_PVP_FLAG, flags); }

        bool IsInSanctuary() const { return HasPvpFlag(UNIT_BYTE2_FLAG_SANCTUARY); }
        bool IsPvP() const { return HasPvpFlag(UNIT_BYTE2_FLAG_PVP); }
        bool IsFFAPvP() const { return HasPvpFlag(UNIT_BYTE2_FLAG_FFA_PVP); }
        virtual void SetPvP(bool state);

        UnitPetFlag GetPetFlags() const { return UnitPetFlag(GetByteValue(UNIT_FIELD_BYTES_2, UNIT_BYTES_2_OFFSET_PET_FLAGS)); }
        bool HasPetFlag(UnitPetFlag flags) const { return HasByteFlag(UNIT_FIELD_BYTES_2, UNIT_BYTES_2_OFFSET_PET_FLAGS, flags); }
        void SetPetFlag(UnitPetFlag flags) { SetByteFlag(UNIT_FIELD_BYTES_2, UNIT_BYTES_2_OFFSET_PET_FLAGS, flags); }
        void RemovePetFlag(UnitPetFlag flags) { RemoveByteFlag(UNIT_FIELD_BYTES_2, UNIT_BYTES_2_OFFSET_PET_FLAGS, flags); }
        void ReplaceAllPetFlags(UnitPetFlag flags) { SetByteValue(UNIT_FIELD_BYTES_2, UNIT_BYTES_2_OFFSET_PET_FLAGS, flags); }

        uint32 GetCreatureType() const;
        uint32 GetCreatureTypeMask() const;

        UnitStandStateType GetStandState() const { return UnitStandStateType(GetByteValue(UNIT_FIELD_BYTES_1, UNIT_BYTES_1_OFFSET_STAND_STATE)); }
        bool IsSitState() const;
        bool IsStandState() const;
        void SetStandState(UnitStandStateType state);

        void SetVisFlag(UnitVisFlags flags) { SetByteFlag(UNIT_FIELD_BYTES_1, UNIT_BYTES_1_OFFSET_VIS_FLAG, flags); }
        void RemoveVisFlag(UnitVisFlags flags) { RemoveByteFlag(UNIT_FIELD_BYTES_1, UNIT_BYTES_1_OFFSET_VIS_FLAG, flags); }
        void ReplaceAllVisFlags(UnitVisFlags flags) { SetByteValue(UNIT_FIELD_BYTES_1, UNIT_BYTES_1_OFFSET_VIS_FLAG, flags); }

        AnimTier GetAnimTier() const { return AnimTier(GetByteValue(UNIT_FIELD_BYTES_1, UNIT_BYTES_1_OFFSET_ANIM_TIER)); }
        void SetAnimTier(AnimTier animTier);

        bool IsMounted() const { return HasUnitFlag(UNIT_FLAG_MOUNT); }
        uint32 GetMountDisplayId() const { return GetUInt32Value(UNIT_FIELD_MOUNTDISPLAYID); }
        void SetMountDisplayId(uint32 mountDisplayId) { SetUInt32Value(UNIT_FIELD_MOUNTDISPLAYID, mountDisplayId); }
        void Mount(uint32 mount, uint32 vehicleId = 0, uint32 creatureEntry = 0);
        void Dismount();

        uint32 GetMaxSkillValueForLevel(Unit const* target = nullptr) const { return (target ? GetLevelForTarget(target) : GetLevel()) * 5; }

        // ====================================================================
        // 伤害处理
        // ====================================================================

        /**
         * @brief 应用伤害修正
         * @param victim 受害者
         * @param damage [输入/输出] 伤害值
         * @param absorb [输出] 被吸收的伤害量
         *
         * 静态函数，处理通用的伤害修正逻辑。
         * 包括塞纳里奥议会光环等特殊修正。
         */
        static void DealDamageMods(Unit const* victim, uint32& damage, uint32* absorb);

        /**
         * @brief 造成伤害
         * @param attacker 攻击者
         * @param victim 受害者
         * @param damage 伤害值
         * @param cleanDamage 净伤害信息（用于怒气计算）
         * @param damagetype 伤害类型
         * @param damageSchoolMask 伤害学校掩码
         * @param spellProto 法术信息（物理伤害为 nullptr）
         * @param durabilityLoss 是否造成装备耐久损失
         * @return 实际造成的伤害值
         *
         * 核心伤害处理函数。
         * 处理：吸收、抵抗、反射、死亡判定等。
         * 触发相关光环和成就。
         */
        static uint32 DealDamage(Unit* attacker, Unit* victim, uint32 damage, CleanDamage const* cleanDamage = nullptr, DamageEffectType damagetype = DIRECT_DAMAGE, SpellSchoolMask damageSchoolMask = SPELL_SCHOOL_MASK_NORMAL, SpellInfo const* spellProto = nullptr, bool durabilityLoss = true);

        /**
         * @brief 击杀单位
         * @param attacker 攻击者
         * @param victim 受害者
         * @param durabilityLoss 是否造成装备耐久损失
         *
         * 将单位标记为死亡，处理死亡相关逻辑。
         * 触发死亡事件、掉落、经验分配等。
         */
        static void Kill(Unit* attacker, Unit* victim, bool durabilityLoss = true);

        /**
         * @brief 自杀
         * @param durabilityLoss 是否造成装备耐久损失
         *
         * 便捷函数，单位自己击杀自己。
         */
        void KillSelf(bool durabilityLoss = true) { Unit::Kill(this, this, durabilityLoss); }

        /**
         * @brief 造成治疗
         * @param healInfo 治疗信息
         *
         * 核心治疗处理函数。
         * 处理：治疗加成、吸收、过量治疗等。
         */
        static void DealHeal(HealInfo& healInfo);

        /**
         * @brief 处理技能和光环触发
         * @param actor 行动者
         * @param actionTarget 行动目标
         * @param typeMaskActor 行动者触发类型掩码
         * @param typeMaskActionTarget 目标触发类型掩码
         * @param spellTypeMask 法术类型掩码
         * @param spellPhaseMask 法术阶段掩码
         * @param hitMask 命中掩码
         * @param spell 当前法术
         * @param damageInfo 伤害信息
         * @param healInfo 治疗信息
         *
         * 处理所有触发效果（Proc）。
         * 例如：暴击触发特效、被击触发特效等。
         */
        static void ProcSkillsAndAuras(Unit* actor, Unit* actionTarget, uint32 typeMaskActor, uint32 typeMaskActionTarget,
                                uint32 spellTypeMask, uint32 spellPhaseMask, uint32 hitMask, Spell* spell,
                                DamageInfo* damageInfo, HealInfo* healInfo);

        void GetProcAurasTriggeredOnEvent(AuraApplicationProcContainer& aurasTriggeringProc, AuraApplicationList* procAuras, ProcEventInfo& eventInfo);
        void TriggerAurasProcOnEvent(Unit* actionTarget, uint32 typeMaskActor, uint32 typeMaskActionTarget,
                                     uint32 spellTypeMask, uint32 spellPhaseMask, uint32 hitMask, Spell* spell,
                                     DamageInfo* damageInfo, HealInfo* healInfo);
        void TriggerAurasProcOnEvent(ProcEventInfo& eventInfo, AuraApplicationProcContainer& procAuras);

        /**
         * @brief 执行表情命令
         * @param emoteId 表情ID
         *
         * 让单位执行指定表情。
         */
        void HandleEmoteCommand(Emote emoteId);

        /**
         * @brief 更新攻击者状态
         * @param victim 攻击目标
         * @param attType 攻击类型（主手、副手、远程）
         * @param extra 是否为额外攻击
         *
         * 每次攻击循环调用，处理近战攻击。
         * 包括：命中判定、伤害计算、触发效果等。
         */
        void AttackerStateUpdate (Unit* victim, WeaponAttackType attType = BASE_ATTACK, bool extra = false);

        /**
         * @brief 计算近战伤害
         * @param victim 攻击目标
         * @param damageInfo [输出] 伤害计算结果
         * @param attackType 攻击类型
         *
         * 计算近战攻击的所有可能结果：
         * 未命中、闪避、招架、格挡、暴击、普通命中等。
         * 填充 CalcDamageInfo 结构。
         */
        void CalculateMeleeDamage(Unit* victim, CalcDamageInfo* damageInfo, WeaponAttackType attackType = BASE_ATTACK);

        /**
         * @brief 执行近战伤害
         * @param damageInfo 伤害信息
         * @param durabilityLoss 是否造成装备耐久损失
         *
         * 应用已计算好的近战伤害。
         * 处理死亡、怒气获得、威胁等。
         */
        void DealMeleeDamage(CalcDamageInfo* damageInfo, bool durabilityLoss);

        /**
         * @brief 为目标处理额外攻击
         * @param victim 目标
         * @param count 额外攻击次数
         *
         * 执行风怒武器、剑类专精等额外攻击。
         */
        void HandleProcExtraAttackFor(Unit* victim, uint32 count);

        /**
         * @brief 设置上次额外攻击的法术ID
         * @param spellId 法术ID
         */
        void SetLastExtraAttackSpell(uint32 spellId) { _lastExtraAttackSpell = spellId; }

        /**
         * @brief 获取上次额外攻击的法术ID
         * @return 法术ID
         */
        uint32 GetLastExtraAttackSpell() const { return _lastExtraAttackSpell; }

        /**
         * @brief 添加额外攻击次数
         * @param count 额外攻击次数
         */
        void AddExtraAttacks(uint32 count);

        /**
         * @brief 设置上次受伤目标的GUID
         * @param guid 目标GUID
         */
        void SetLastDamagedTargetGuid(ObjectGuid guid) { _lastDamagedTargetGuid = guid; }

        /**
         * @brief 获取上次受伤目标的GUID
         * @return 目标GUID
         */
        ObjectGuid GetLastDamagedTargetGuid() const { return _lastDamagedTargetGuid; }

        /**
         * @brief 计算法术伤害接收
         * @param damageInfo [输出] 伤害信息
         * @param damage 基础伤害值
         * @param spellInfo 法术信息
         * @param attackType 攻击类型
         * @param crit 是否暴击
         * @param blocked 是否被格挡
         * @param spell 当前法术实例
         *
         * 计算法术对目标的最终伤害。
         * 包括：抗性、吸收、暴击加成等。
         */
        void CalculateSpellDamageTaken(SpellNonMeleeDamage* damageInfo, int32 damage, SpellInfo const* spellInfo, WeaponAttackType attackType = BASE_ATTACK, bool crit = false, bool blocked = false, Spell* spell = nullptr);

        /**
         * @brief 执行法术伤害
         * @param damageInfo 伤害信息
         * @param durabilityLoss 是否造成装备耐久损失
         *
         * 应用已计算好的法术伤害。
         */
        void DealSpellDamage(SpellNonMeleeDamage const* damageInfo, bool durabilityLoss);

        // player or player's pet resilience (-1%)
        float GetMeleeCritChanceReduction() const { return GetCombatRatingReduction(CR_CRIT_TAKEN_MELEE); }
        float GetRangedCritChanceReduction() const { return GetCombatRatingReduction(CR_CRIT_TAKEN_RANGED); }
        float GetSpellCritChanceReduction() const { return GetCombatRatingReduction(CR_CRIT_TAKEN_SPELL); }

        // player or player's pet resilience (-1%)
        uint32 GetMeleeCritDamageReduction(uint32 damage) const { return GetCombatRatingDamageReduction(CR_CRIT_TAKEN_MELEE, 2.2f, 33.0f, damage); }
        uint32 GetRangedCritDamageReduction(uint32 damage) const { return GetCombatRatingDamageReduction(CR_CRIT_TAKEN_RANGED, 2.2f, 33.0f, damage); }
        uint32 GetSpellCritDamageReduction(uint32 damage) const { return GetCombatRatingDamageReduction(CR_CRIT_TAKEN_SPELL, 2.2f, 33.0f, damage); }

        // player or player's pet resilience (-1%), cap 100%
        uint32 GetMeleeDamageReduction(uint32 damage) const { return GetCombatRatingDamageReduction(CR_CRIT_TAKEN_MELEE, 2.0f, 100.0f, damage); }
        uint32 GetRangedDamageReduction(uint32 damage) const { return GetCombatRatingDamageReduction(CR_CRIT_TAKEN_RANGED, 2.0f, 100.0f, damage); }
        uint32 GetSpellDamageReduction(uint32 damage) const { return GetCombatRatingDamageReduction(CR_CRIT_TAKEN_SPELL, 2.0f, 100.0f, damage); }

        virtual bool CanApplyResilience() const;
        static void ApplyResilience(Unit const* victim, float* crit, int32* damage, bool isCrit, CombatRating type);

        int32 CalculateAOEAvoidance(int32 damage, uint32 schoolMask, ObjectGuid const& casterGuid) const;

        float MeleeSpellMissChance(Unit const* victim, WeaponAttackType attType, int32 skillDiff, uint32 spellId) const override;
        SpellMissInfo MeleeSpellHitResult(Unit* victim, SpellInfo const* spellInfo) const override;

        float GetUnitDodgeChance(WeaponAttackType attType, Unit const* victim) const;
        float GetUnitParryChance(WeaponAttackType attType, Unit const* victim) const;
        float GetUnitBlockChance(WeaponAttackType attType, Unit const* victim) const;
        float GetUnitMissChance() const;
        float GetUnitCriticalChanceDone(WeaponAttackType attackType) const;
        float GetUnitCriticalChanceTaken(Unit const* attacker, WeaponAttackType attackType, float critDone) const;
        float GetUnitCriticalChanceAgainst(WeaponAttackType attackType, Unit const* victim) const;
        int32 GetMechanicResistChance(SpellInfo const* spellInfo) const;
        bool CanUseAttackType(uint8 attacktype) const;

        virtual uint32 GetShieldBlockValue() const = 0;
        uint32 GetShieldBlockValue(uint32 soft_cap, uint32 hard_cap) const;
        uint32 GetDefenseSkillValue(Unit const* target = nullptr) const;
        uint32 GetWeaponSkillValue(WeaponAttackType attType, Unit const* target = nullptr) const;

        float GetWeaponProcChance() const;
        float GetPPMProcChance(uint32 WeaponSpeed, float PPM, SpellInfo const* spellProto) const;

        MeleeHitOutcome RollMeleeOutcomeAgainst(Unit const* victim, WeaponAttackType attType) const;

        NPCFlags GetNpcFlags() const { return NPCFlags(GetUInt32Value(UNIT_NPC_FLAGS)); }
        bool HasNpcFlag(NPCFlags flags) const { return HasFlag(UNIT_NPC_FLAGS, flags) != 0; }
        void SetNpcFlag(NPCFlags flags) { SetFlag(UNIT_NPC_FLAGS, flags); }
        void RemoveNpcFlag(NPCFlags flags) { RemoveFlag(UNIT_NPC_FLAGS, flags); }
        void ReplaceAllNpcFlags(NPCFlags flags) { SetUInt32Value(UNIT_NPC_FLAGS, flags); }

        bool IsVendor()         const { return HasNpcFlag(UNIT_NPC_FLAG_VENDOR); }
        bool IsTrainer()        const { return HasNpcFlag(UNIT_NPC_FLAG_TRAINER); }
        bool IsQuestGiver()     const { return HasNpcFlag(UNIT_NPC_FLAG_QUESTGIVER); }
        bool IsGossip()         const { return HasNpcFlag(UNIT_NPC_FLAG_GOSSIP); }
        bool IsTaxi()           const { return HasNpcFlag(UNIT_NPC_FLAG_FLIGHTMASTER); }
        bool IsGuildMaster()    const { return HasNpcFlag(UNIT_NPC_FLAG_PETITIONER); }
        bool IsBattleMaster()   const { return HasNpcFlag(UNIT_NPC_FLAG_BATTLEMASTER); }
        bool IsBanker()         const { return HasNpcFlag(UNIT_NPC_FLAG_BANKER); }
        bool IsInnkeeper()      const { return HasNpcFlag(UNIT_NPC_FLAG_INNKEEPER); }
        bool IsSpiritHealer()   const { return HasNpcFlag(UNIT_NPC_FLAG_SPIRITHEALER); }
        bool IsSpiritGuide()    const { return HasNpcFlag(UNIT_NPC_FLAG_SPIRITGUIDE); }
        bool IsTabardDesigner() const { return HasNpcFlag(UNIT_NPC_FLAG_TABARDDESIGNER); }
        bool IsAuctioner()      const { return HasNpcFlag(UNIT_NPC_FLAG_AUCTIONEER); }
        bool IsArmorer()        const { return HasNpcFlag(UNIT_NPC_FLAG_REPAIR); }
        bool IsServiceProvider() const;
        bool IsSpiritService() const { return HasNpcFlag(UNIT_NPC_FLAG_SPIRITHEALER | UNIT_NPC_FLAG_SPIRITGUIDE); }
        bool IsCritter() const { return GetCreatureType() == CREATURE_TYPE_CRITTER; }

        bool IsInFlight()  const { return HasUnitState(UNIT_STATE_IN_FLIGHT); }

        /// ====================== 威胁和战斗 ====================
        bool CanHaveThreatList() const { return m_threatManager.CanHaveThreatList(); }
        // 此值可以与 IsInCombat 不同，例如：
        // - 当投射物法术正飞向生物时（发射时进入战斗 - 击中时产生威胁和仇恨）
        // - 当生物没有目标了，但 AI 还没有结束交战逻辑
        virtual bool IsEngaged() const { return IsInCombat(); }
        bool IsEngagedBy(Unit const* who) const { return CanHaveThreatList() ? IsThreatenedBy(who) : IsInCombatWith(who); }
        void EngageWithTarget(Unit* who);               // 将目标添加到威胁列表（如果适用），否则只设置战斗状态

        // ====================================================================
        // 战斗管理器
        // ====================================================================
        CombatManager& GetCombatManager() { return m_combatManager; }
        CombatManager const& GetCombatManager() const { return m_combatManager; }
        void AtTargetAttacked(Unit* target, bool canInitialAggro);

        // ====================================================================
        // 免疫状态
        // ====================================================================
        bool IsImmuneToAll() const { return IsImmuneToPC() && IsImmuneToNPC(); }
        void SetImmuneToAll(bool apply, bool keepCombat);
        virtual void SetImmuneToAll(bool apply) { SetImmuneToAll(apply, false); }
        bool IsImmuneToPC() const { return HasUnitFlag(UNIT_FLAG_IMMUNE_TO_PC); }       // 是否免疫玩家
        void SetImmuneToPC(bool apply, bool keepCombat);
        virtual void SetImmuneToPC(bool apply) { SetImmuneToPC(apply, false); }
        bool IsImmuneToNPC() const { return HasUnitFlag(UNIT_FLAG_IMMUNE_TO_NPC); }     // 是否免疫NPC
        void SetImmuneToNPC(bool apply, bool keepCombat);
        virtual void SetImmuneToNPC(bool apply) { SetImmuneToNPC(apply, false); }

        // ====================================================================
        // 战斗状态
        // ====================================================================
        bool IsInCombat() const { return HasUnitFlag(UNIT_FLAG_IN_COMBAT); }            // 是否在战斗中
        bool IsInCombatWith(Unit const* who) const { return who && m_combatManager.IsInCombatWith(who); }
        void SetInCombatWith(Unit* enemy, bool addSecondUnitSuppressed = false) { if (enemy) m_combatManager.SetInCombatWith(enemy, addSecondUnitSuppressed); }
        void ClearInCombat() { m_combatManager.EndAllCombat(); }
        void UpdatePetCombatState();

        // ====================================================================
        // 威胁管理
        // ====================================================================
        bool IsThreatened() const;
        bool IsThreatenedBy(Unit const* who) const { return who && m_threatManager.IsThreatenedBy(who, true); }
        // 直接访问威胁管理器 - 接口时要小心
        // 作为一般规则，任何单位指针必须在传递给 threatmanager 方法之前进行空指针检查
        // threatmanager 不会为您检查空指针 - 误用 = 崩溃
        ThreatManager& GetThreatManager() { return m_threatManager; }
        ThreatManager const& GetThreatManager() const { return m_threatManager; }

        void SendClearTarget();

        bool HasAuraTypeWithFamilyFlags(AuraType auraType, uint32 familyName, flag96 familyFlags) const;
        bool virtual HasSpell(uint32 /*spellID*/) const { return false; }
        bool HasBreakableByDamageAuraType(AuraType type, uint32 excludeAura = 0) const;
        bool HasBreakableByDamageCrowdControlAura(Unit* excludeCasterChannel = nullptr) const;

        bool HasStealthAura()      const { return HasAuraType(SPELL_AURA_MOD_STEALTH); }
        bool HasInvisibilityAura() const { return HasAuraType(SPELL_AURA_MOD_INVISIBILITY); }
        bool IsFeared()  const { return HasAuraType(SPELL_AURA_MOD_FEAR); }
        bool IsRooted() const { return HasAuraType(SPELL_AURA_MOD_ROOT); }
        bool IsPolymorphed() const;
        bool IsFrozen() const { return HasAuraState(AURA_STATE_FROZEN); }

        bool isTargetableForAttack(bool checkFakeDeath = true) const;

        bool IsInWater() const;
        bool IsUnderWater() const;
        bool isInAccessiblePlaceFor(Creature const* c) const;

        void SendHealSpellLog(HealInfo& healInfo, bool critical = false);
        int32 HealBySpell(HealInfo& healInfo, bool critical = false);
        void SendEnergizeSpellLog(Unit* victim, uint32 spellId, int32 damage, Powers powerType);
        void EnergizeBySpell(Unit* victim, uint32 spellId, int32 damage, Powers powerType);
        void EnergizeBySpell(Unit* victim, SpellInfo const* spellInfo, int32 damage, Powers powerType);

        /**
         * @brief 添加光环到目标
         * @param spellId 法术ID
         * @param target 目标单位
         * @return 创建的光环实例，失败返回 nullptr
         *
         * 便捷函数，在指定目标上创建光环。
         * 施法者为此单位。
         */
        Aura* AddAura(uint32 spellId, Unit* target);

        /**
         * @brief 添加光环到目标（高级版本）
         * @param spellInfo 法术信息
         * @param effMask 效果掩码
         * @param target 目标单位
         * @return 创建的光环实例，失败返回 nullptr
         *
         * 允许指定效果掩码，用于部分效果的光环。
         */
        Aura* AddAura(SpellInfo const* spellInfo, uint8 effMask, Unit* target);

        /**
         * @brief 设置光环层数
         * @param spellId 法术ID
         * @param target 目标单位
         * @param stack 层数
         *
         * 如果光环存在则设置层数，否则创建新光环。
         */
        void SetAuraStack(uint32 spellId, Unit* target, uint32 stack);

        void SendPlaySpellVisual(uint32 id) const;
        void SendPlaySpellImpact(ObjectGuid guid, uint32 id) const;

        void DeMorph();

        /**
         * @brief 发送攻击状态更新包
         * @param damageInfo 伤害信息
         *
         * 向周围玩家发送攻击结果。
         */
        void SendAttackStateUpdate(CalcDamageInfo* damageInfo);

        /**
         * @brief 发送攻击状态更新包（详细参数）
         * @param HitInfo 命中信息标志
         * @param target 目标
         * @param SwingType 攻击类型
         * @param damageSchoolMask 伤害学校掩码
         * @param Damage 伤害值
         * @param AbsorbDamage 吸收量
         * @param Resist 抵抗量
         * @param TargetState 目标状态
         * @param BlockedAmount 格挡量
         */
        void SendAttackStateUpdate(uint32 HitInfo, Unit* target, uint8 SwingType, SpellSchoolMask damageSchoolMask, uint32 Damage, uint32 AbsorbDamage, uint32 Resist, VictimState TargetState, uint32 BlockedAmount);

        /**
         * @brief 发送非法术近战伤害日志
         * @param log 伤害日志
         */
        void SendSpellNonMeleeDamageLog(SpellNonMeleeDamage const* log);

        /**
         * @brief 发送非法术近战伤害日志（详细参数）
         * @param target 目标
         * @param spellID 法术ID
         * @param damage 伤害值
         * @param damageSchoolMask 伤害学校掩码
         * @param absorbedDamage 吸收量
         * @param resist 抵抗量
         * @param isPeriodic 是否为周期性伤害
         * @param blocked 格挡量
         * @param criticalHit 是否暴击
         * @param split 是否为分裂伤害
         */
        void SendSpellNonMeleeDamageLog(Unit* target, uint32 spellID, uint32 damage, SpellSchoolMask damageSchoolMask, uint32 absorbedDamage, uint32 resist, bool isPeriodic, uint32 blocked, bool criticalHit = false, bool split = false);

        /**
         * @brief 发送周期性光环日志
         * @param pInfo 日志信息
         *
         * 用于 DoT/HoT 的周期性效果。
         */
        void SendPeriodicAuraLog(SpellPeriodicAuraLogInfo* pInfo);

        /**
         * @brief 发送法术伤害抵抗消息
         * @param target 目标
         * @param spellId 法术ID
         */
        void SendSpellDamageResist(Unit* target, uint32 spellId);

        /**
         * @brief 发送法术伤害免疫消息
         * @param target 目标
         * @param spellId 法术ID
         */
        void SendSpellDamageImmune(Unit* target, uint32 spellId);

        void NearTeleportTo(Position const& pos, bool casting = false);
        void NearTeleportTo(float x, float y, float z, float orientation, bool casting = false) { NearTeleportTo(Position(x, y, z, orientation), casting); }
        void SendTeleportPacket(Position const& pos, bool teleportingTransport = false);
        virtual bool UpdatePosition(float x, float y, float z, float ang, bool teleport = false);
        // returns true if unit's position really changed
        virtual bool UpdatePosition(Position const& pos, bool teleport = false);
        void UpdateOrientation(float orientation);
        void UpdateHeight(float newZ);

        void KnockbackFrom(float x, float y, float speedXY, float speedZ);
        void JumpTo(float speedXY, float speedZ, bool forward = true, Optional<Position> dest = {});
        void JumpTo(WorldObject* obj, float speedZ, bool withOrientation = false);

        void MonsterMoveWithSpeed(float x, float y, float z, float speed, bool generatePath = false, bool forceDestination = false);
        void SendMovementFlagUpdate(bool self = false);

        void SetHoverHeight(float hoverHeight) { SetFloatValue(UNIT_FIELD_HOVERHEIGHT, hoverHeight); }

        bool IsGravityDisabled() const { return m_movementInfo.HasMovementFlag(MOVEMENTFLAG_DISABLE_GRAVITY); }
        bool IsWalking() const { return m_movementInfo.HasMovementFlag(MOVEMENTFLAG_WALKING); }
        bool IsHovering() const { return m_movementInfo.HasMovementFlag(MOVEMENTFLAG_HOVER); }
        virtual bool SetWalk(bool enable);
        virtual bool SetDisableGravity(bool disable, bool packetOnly = false, bool updateAnimTier = true);
        virtual bool SetSwim(bool enable);
        virtual bool SetCanFly(bool enable, bool packetOnly = false);
        virtual bool SetWaterWalking(bool enable, bool packetOnly = false);
        virtual bool SetFeatherFall(bool enable, bool packetOnly = false);
        virtual bool SetHover(bool enable, bool packetOnly = false, bool updateAnimTier = true);

        void SetInFront(WorldObject const* target);
        void SetFacingTo(float const ori, bool force = true);
        void SetFacingToObject(WorldObject const* object, bool force = true);

        void BuildHeartBeatMsg(WorldPacket* data) const;

        bool IsAlive() const { return (m_deathState == ALIVE); }
        bool isDying() const { return (m_deathState == JUST_DIED); }
        bool isDead() const { return (m_deathState == DEAD || m_deathState == CORPSE); }
        bool IsGhouled() const;
        DeathState getDeathState() const { return m_deathState; }
        virtual void setDeathState(DeathState s);           // overwrited in Creature/Player/Pet

        ObjectGuid GetOwnerGUID() const override { return GetGuidValue(UNIT_FIELD_SUMMONEDBY); }
        void SetOwnerGUID(ObjectGuid owner);
        ObjectGuid GetCreatorGUID() const { return GetGuidValue(UNIT_FIELD_CREATEDBY); }
        void SetCreatorGUID(ObjectGuid creator) { SetGuidValue(UNIT_FIELD_CREATEDBY, creator); }
        ObjectGuid GetMinionGUID() const { return GetGuidValue(UNIT_FIELD_SUMMON); }
        void SetMinionGUID(ObjectGuid guid) { SetGuidValue(UNIT_FIELD_SUMMON, guid); }
        ObjectGuid GetPetGUID() const { return m_SummonSlot[SUMMON_SLOT_PET]; }
        void SetPetGUID(ObjectGuid guid) { m_SummonSlot[SUMMON_SLOT_PET] = guid; }
        ObjectGuid GetCritterGUID() const { return GetGuidValue(UNIT_FIELD_CRITTER); }
        void SetCritterGUID(ObjectGuid guid) { SetGuidValue(UNIT_FIELD_CRITTER, guid); }

        ObjectGuid GetCharmerGUID() const { return GetGuidValue(UNIT_FIELD_CHARMEDBY); }
        Unit* GetCharmer() const { return m_charmer; }

        ObjectGuid GetCharmedGUID() const { return GetGuidValue(UNIT_FIELD_CHARM); }
        Unit* GetCharmed() const { return m_charmed; }

        bool IsControlledByPlayer() const { return m_ControlledByPlayer; }
        Player* GetControllingPlayer() const;
        ObjectGuid GetCharmerOrOwnerGUID() const override { return IsCharmed() ? GetCharmerGUID() : GetOwnerGUID(); }
        bool IsCharmedOwnedByPlayerOrPlayer() const { return GetCharmerOrOwnerOrOwnGUID().IsPlayer(); }

        Guardian* GetGuardianPet() const;
        Minion* GetFirstMinion() const;
        Unit* GetCharmerOrOwner() const { return IsCharmed() ? GetCharmer() : GetOwner(); }

        void SetMinion(Minion *minion, bool apply);
        void GetAllMinionsByEntry(std::list<Creature*>& Minions, uint32 entry);
        void RemoveAllMinionsByEntry(uint32 entry);
        void SetCharm(Unit* target, bool apply);
        Unit* GetNextRandomRaidMemberOrPet(float radius);
        bool SetCharmedBy(Unit* charmer, CharmType type, AuraApplication const* aurApp = nullptr);
        void RemoveCharmedBy(Unit* charmer);
        void RestoreFaction();

        ControlList m_Controlled;
        Unit* GetFirstControlled() const;
        void RemoveAllControlled();

        bool IsCharmed() const { return !GetCharmerGUID().IsEmpty(); }
        bool IsCharming() const { return !GetCharmedGUID().IsEmpty(); }
        bool isPossessed() const { return HasUnitState(UNIT_STATE_POSSESSED); }
        bool isPossessedByPlayer() const;
        bool isPossessing() const;
        bool isPossessing(Unit* u) const;

        CharmInfo* GetCharmInfo() { return m_charmInfo; }
        CharmInfo* InitCharmInfo();
        void DeleteCharmInfo();
        void SetPetNumberForClient(uint32 petNumber) { SetUInt32Value(UNIT_FIELD_PETNUMBER, petNumber); }
        void SetPetNameTimestamp(uint32 timestamp) { SetUInt32Value(UNIT_FIELD_PET_NAME_TIMESTAMP, timestamp); }

        // base client control of this unit (possess effects, vehicles and similar). Not affected by temporary CC.
        bool IsCharmerOrSelfPlayer() const { return GetCharmerOrSelf()->IsPlayer(); }
        Unit* GetCharmerOrSelf() const;
        Player* GetCharmerOrSelfPlayer() const { return ToPlayer(GetCharmerOrSelf());}
        Unit* GetCharmedOrSelf() const { return IsCharming() ? GetCharmed() : const_cast<Unit*>(this); }

        // real time client control status of this unit (possess effects, vehicles and similar). For example, if this unit is a player temporarly under fear, it will return false.
        bool IsMovedByClient() const { return _gameClientMovingMe != nullptr; }
        bool IsMovedByServer() const { return !IsMovedByClient(); }
        GameClient* GetGameClientMovingMe() const { return _gameClientMovingMe; }
        void SetGameClientMovingMe(GameClient* gameClientMovingMe) { _gameClientMovingMe = gameClientMovingMe; }

        SharedVisionList const& GetSharedVisionList() { return m_sharedVision; }
        void AddPlayerToVision(Player* player);
        void RemovePlayerFromVision(Player* player);
        bool HasSharedVision() const { return !m_sharedVision.empty(); }
        void RemoveBindSightAuras();
        void RemoveCharmAuras();

        Pet* CreateTamedPetFrom(Creature* creatureTarget, uint32 spell_id = 0);
        Pet* CreateTamedPetFrom(uint32 creatureEntry, uint32 spell_id = 0);
        bool InitTamedPet(Pet* pet, uint8 level, uint32 spell_id);

        // aura apply/remove helpers - you should better not use these
        Aura* _TryStackingOrRefreshingExistingAura(AuraCreateInfo& createInfo);
        void _AddAura(UnitAura* aura, Unit* caster);
        AuraApplication* _CreateAuraApplication(Aura* aura, uint8 effMask);
        void _ApplyAuraEffect(Aura* aura, uint8 effIndex);
        void _ApplyAura(AuraApplication* aurApp, uint8 effMask);
        void _UnapplyAura(AuraApplicationMap::iterator& i, AuraRemoveMode removeMode);
        void _UnapplyAura(AuraApplication* aurApp, AuraRemoveMode removeMode);
        void _RemoveNoStackAurasDueToAura(Aura* aura, bool owned);
        void _RegisterAuraEffect(AuraEffect* aurEff, bool apply);

        // m_ownedAuras container management
        AuraMap      & GetOwnedAuras()       { return m_ownedAuras; }
        AuraMap const& GetOwnedAuras() const { return m_ownedAuras; }

        void RemoveOwnedAura(AuraMap::iterator& i, AuraRemoveMode removeMode = AURA_REMOVE_BY_DEFAULT);
        void RemoveOwnedAura(uint32 spellId, ObjectGuid casterGUID = ObjectGuid::Empty, uint8 reqEffMask = 0, AuraRemoveMode removeMode = AURA_REMOVE_BY_DEFAULT);
        void RemoveOwnedAura(Aura* aura, AuraRemoveMode removeMode = AURA_REMOVE_BY_DEFAULT);

        Aura* GetOwnedAura(uint32 spellId, ObjectGuid casterGUID = ObjectGuid::Empty, ObjectGuid itemCasterGUID = ObjectGuid::Empty, uint8 reqEffMask = 0, Aura* except = nullptr) const;

        // m_appliedAuras container management
        AuraApplicationMap      & GetAppliedAuras()       { return m_appliedAuras; }
        AuraApplicationMap const& GetAppliedAuras() const { return m_appliedAuras; }

        void RemoveAura(AuraApplicationMap::iterator &i, AuraRemoveMode mode = AURA_REMOVE_BY_DEFAULT);
        void RemoveAura(uint32 spellId, ObjectGuid casterGUID = ObjectGuid::Empty, uint8 reqEffMask = 0, AuraRemoveMode removeMode = AURA_REMOVE_BY_DEFAULT);
        void RemoveAura(AuraApplication * aurApp, AuraRemoveMode mode = AURA_REMOVE_BY_DEFAULT);
        void RemoveAura(Aura* aur, AuraRemoveMode mode = AURA_REMOVE_BY_DEFAULT);

        // Convenience methods removing auras by predicate
        void RemoveAppliedAuras(std::function<bool(AuraApplication const*)> const& check, AuraRemoveMode removeMode = AURA_REMOVE_BY_DEFAULT);
        void RemoveOwnedAuras(std::function<bool(Aura const*)> const& check, AuraRemoveMode removeMode = AURA_REMOVE_BY_DEFAULT);

        // Optimized overloads taking advantage of map key
        void RemoveAppliedAuras(uint32 spellId, std::function<bool(AuraApplication const*)> const& check, AuraRemoveMode removeMode = AURA_REMOVE_BY_DEFAULT);
        void RemoveOwnedAuras(uint32 spellId, std::function<bool(Aura const*)> const& check, AuraRemoveMode removeMode = AURA_REMOVE_BY_DEFAULT);

        void RemoveAurasByType(AuraType auraType, std::function<bool(AuraApplication const*)> const& check, AuraRemoveMode removeMode = AURA_REMOVE_BY_DEFAULT);

        void RemoveAurasDueToSpell(uint32 spellId, ObjectGuid casterGUID = ObjectGuid::Empty, uint8 reqEffMask = 0, AuraRemoveMode removeMode = AURA_REMOVE_BY_DEFAULT);
        void RemoveAuraFromStack(uint32 spellId, ObjectGuid casterGUID = ObjectGuid::Empty, AuraRemoveMode removeMode = AURA_REMOVE_BY_DEFAULT);
        void RemoveAurasDueToSpellByDispel(uint32 spellId, uint32 dispellerSpellId, ObjectGuid casterGUID, WorldObject* dispeller, uint8 chargesRemoved = 1);
        void RemoveAurasDueToSpellBySteal(uint32 spellId, ObjectGuid casterGUID, WorldObject* stealer);
        void RemoveAurasDueToItemSpell(uint32 spellId, ObjectGuid castItemGuid);
        void RemoveAurasByType(AuraType auraType, ObjectGuid casterGUID = ObjectGuid::Empty, Aura* except = nullptr, bool negative = true, bool positive = true);
        void RemoveNotOwnSingleTargetAuras(uint32 newPhase = 0x0);
        void RemoveAurasWithInterruptFlags(uint32 flag, uint32 except = 0);
        void RemoveAurasWithAttribute(uint32 flags);
        void RemoveAurasWithFamily(SpellFamilyNames family, uint32 familyFlag1, uint32 familyFlag2, uint32 familyFlag3, ObjectGuid casterGUID);
        void RemoveAurasWithMechanic(uint32 mechanicMaskToRemove, AuraRemoveMode removeMode = AURA_REMOVE_BY_DEFAULT, uint32 exceptSpellId = 0, bool withEffectMechanics = false);
        void RemoveMovementImpairingAuras(bool withRoot);
        void RemoveAurasByShapeShift();

        void RemoveAreaAurasDueToLeaveWorld();
        void RemoveAllAuras();
        void RemoveArenaAuras();
        void RemoveAurasOnEvade();
        void RemoveAllAurasOnDeath();
        void RemoveAllAurasRequiringDeadTarget();
        void RemoveAllAurasExceptType(AuraType type);
        void RemoveAllAurasExceptType(AuraType type1, AuraType type2); /// @todo: once we support variadic templates use them here
        void RemoveAllGroupBuffsFromCaster(ObjectGuid casterGUID);
        void DelayOwnedAuras(uint32 spellId, ObjectGuid caster, int32 delaytime);

        void _RemoveAllAuraStatMods();
        void _ApplyAllAuraStatMods();

        AuraEffectList const& GetAuraEffectsByType(AuraType type) const { return m_modAuras[type]; }
        AuraEffectList& GetAuraEffectsByType(AuraType type) { return m_modAuras[type]; }
        AuraList      & GetSingleCastAuras()       { return m_scAuras; }
        AuraList const& GetSingleCastAuras() const { return m_scAuras; }

        AuraEffect* GetAuraEffect(uint32 spellId, uint8 effIndex, ObjectGuid casterGUID = ObjectGuid::Empty) const;
        AuraEffect* GetAuraEffectOfRankedSpell(uint32 spellId, uint8 effIndex, ObjectGuid casterGUID = ObjectGuid::Empty) const;
        AuraEffect* GetAuraEffect(AuraType type, SpellFamilyNames name, uint32 iconId, uint8 effIndex) const; // spell mustn't have familyflags
        AuraEffect* GetAuraEffect(AuraType type, SpellFamilyNames family, uint32 familyFlag1, uint32 familyFlag2, uint32 familyFlag3, ObjectGuid casterGUID = ObjectGuid::Empty) const;
        AuraEffect* GetDummyAuraEffect(SpellFamilyNames name, uint32 iconId, uint8 effIndex) const;

        AuraApplication * GetAuraApplication(uint32 spellId, ObjectGuid casterGUID = ObjectGuid::Empty, ObjectGuid itemCasterGUID = ObjectGuid::Empty, uint8 reqEffMask = 0, AuraApplication * except = nullptr) const;
        Aura* GetAura(uint32 spellId, ObjectGuid casterGUID = ObjectGuid::Empty, ObjectGuid itemCasterGUID = ObjectGuid::Empty, uint8 reqEffMask = 0) const;

        AuraApplication * GetAuraApplicationOfRankedSpell(uint32 spellId, ObjectGuid casterGUID = ObjectGuid::Empty, ObjectGuid itemCasterGUID = ObjectGuid::Empty, uint8 reqEffMask = 0, AuraApplication * except = nullptr) const;
        Aura* GetAuraOfRankedSpell(uint32 spellId, ObjectGuid casterGUID = ObjectGuid::Empty, ObjectGuid itemCasterGUID = ObjectGuid::Empty, uint8 reqEffMask = 0) const;

        void GetDispellableAuraList(WorldObject const* caster, uint32 dispelMask, DispelChargesList& dispelList, bool isReflect = false) const;

        bool HasAuraEffect(uint32 spellId, uint8 effIndex, ObjectGuid caster = ObjectGuid::Empty) const;
        uint32 GetAuraCount(uint32 spellId) const;
        bool HasAura(uint32 spellId, ObjectGuid casterGUID = ObjectGuid::Empty, ObjectGuid itemCasterGUID = ObjectGuid::Empty, uint8 reqEffMask = 0) const;
        bool HasAuraType(AuraType auraType) const;
        bool HasAuraTypeWithCaster(AuraType auraType, ObjectGuid caster) const;
        bool HasAuraTypeWithMiscvalue(AuraType auraType, int32 miscValue) const;
        bool HasAuraTypeWithAffectMask(AuraType auraType, SpellInfo const* affectedSpell) const;
        bool HasAuraTypeWithValue(AuraType auraType, int32 value) const;
        bool HasAuraTypeWithTriggerSpell(AuraType auratype, uint32 triggerSpell) const;
        bool HasNegativeAuraWithInterruptFlag(uint32 flag, ObjectGuid guid = ObjectGuid::Empty) const;
        bool HasAuraWithMechanic(uint32 mechanicMask) const;
        bool HasStrongerAuraWithDR(SpellInfo const* auraSpellInfo, Unit* caster, bool triggered) const;

        AuraEffect* IsScriptOverriden(SpellInfo const* spell, int32 script) const;
        uint32 GetDiseasesByCaster(ObjectGuid casterGUID, bool remove = false);
        uint32 GetDoTsByCaster(ObjectGuid casterGUID) const;

        int32 GetTotalAuraModifier(AuraType auraType) const;
        float GetTotalAuraMultiplier(AuraType auraType) const;
        int32 GetMaxPositiveAuraModifier(AuraType auraType) const;
        int32 GetMaxNegativeAuraModifier(AuraType auraType) const;

        int32 GetTotalAuraModifier(AuraType auraType, std::function<bool(AuraEffect const*)> const& predicate) const;
        float GetTotalAuraMultiplier(AuraType auraType, std::function<bool(AuraEffect const*)> const& predicate) const;
        int32 GetMaxPositiveAuraModifier(AuraType auraType, std::function<bool(AuraEffect const*)> const& predicate) const;
        int32 GetMaxNegativeAuraModifier(AuraType auraType, std::function<bool(AuraEffect const*)> const& predicate) const;

        int32 GetTotalAuraModifierByMiscMask(AuraType auraType, uint32 misc_mask) const;
        float GetTotalAuraMultiplierByMiscMask(AuraType auraType, uint32 misc_mask) const;
        int32 GetMaxPositiveAuraModifierByMiscMask(AuraType auraType, uint32 misc_mask, AuraEffect const* except = nullptr) const;
        int32 GetMaxNegativeAuraModifierByMiscMask(AuraType auraType, uint32 misc_mask) const;

        int32 GetTotalAuraModifierByMiscValue(AuraType auraType, int32 misc_value) const;
        float GetTotalAuraMultiplierByMiscValue(AuraType auraType, int32 misc_value) const;
        int32 GetMaxPositiveAuraModifierByMiscValue(AuraType auraType, int32 misc_value) const;
        int32 GetMaxNegativeAuraModifierByMiscValue(AuraType auraType, int32 misc_value) const;

        int32 GetTotalAuraModifierByAffectMask(AuraType auraType, SpellInfo const* affectedSpell) const;
        float GetTotalAuraMultiplierByAffectMask(AuraType auraType, SpellInfo const* affectedSpell) const;
        int32 GetMaxPositiveAuraModifierByAffectMask(AuraType auraType, SpellInfo const* affectedSpell) const;
        int32 GetMaxNegativeAuraModifierByAffectMask(AuraType auraType, SpellInfo const* affectedSpell) const;

        void UpdateResistanceBuffModsMod(SpellSchools school);
        void InitStatBuffMods();
        void UpdateStatBuffMod(Stats stat);
        void SetCreateStat(Stats stat, float val) { m_createStats[stat] = val; }
        void SetCreateHealth(uint32 val) { SetUInt32Value(UNIT_FIELD_BASE_HEALTH, val); }
        uint32 GetCreateHealth() const { return GetUInt32Value(UNIT_FIELD_BASE_HEALTH); }
        void SetCreateMana(uint32 val) { SetUInt32Value(UNIT_FIELD_BASE_MANA, val); }
        uint32 GetCreateMana() const { return GetUInt32Value(UNIT_FIELD_BASE_MANA); }
        uint32 GetCreatePowerValue(Powers power) const;
        float GetPosStat(Stats stat) const { return GetFloatValue(UNIT_FIELD_POSSTAT0 + int32(stat)); }
        float GetNegStat(Stats stat) const { return GetFloatValue(UNIT_FIELD_NEGSTAT0 + int32(stat)); }
        float GetCreateStat(Stats stat) const { return m_createStats[stat]; }

        uint32 GetChannelSpellId() const { return GetUInt32Value(UNIT_CHANNEL_SPELL); }
        void SetChannelSpellId(uint32 channelSpellId) { SetUInt32Value(UNIT_CHANNEL_SPELL, channelSpellId); }
        ObjectGuid GetChannelObjectGuid() const { return GetGuidValue(UNIT_FIELD_CHANNEL_OBJECT); }
        void SetChannelObjectGuid(ObjectGuid guid) { SetGuidValue(UNIT_FIELD_CHANNEL_OBJECT, guid); }

        void SetCurrentCastSpell(Spell* pSpell);
        void InterruptSpell(CurrentSpellTypes spellType, bool withDelayed = true, bool withInstant = true);
        void FinishSpell(CurrentSpellTypes spellType, bool ok = true);

        // set withDelayed to true to account delayed spells as cast
        // delayed+channeled spells are always accounted as cast
        // we can skip channeled or delayed checks using flags
        bool IsNonMeleeSpellCast(bool withDelayed, bool skipChanneled = false, bool skipAutorepeat = false, bool isAutoshoot = false, bool skipInstant = true) const;

        // set withDelayed to true to interrupt delayed spells too
        // delayed+channeled spells are always interrupted
        void InterruptNonMeleeSpells(bool withDelayed, uint32 spellid = 0, bool withInstant = true);

        Spell* GetCurrentSpell(CurrentSpellTypes spellType) const { return m_currentSpells[spellType]; }
        Spell* GetCurrentSpell(uint32 spellType) const { return m_currentSpells[spellType]; }
        Spell* FindCurrentSpellBySpellId(uint32 spell_id) const;
        int32 GetCurrentSpellCastTime(uint32 spell_id) const;

        virtual bool HasSpellFocus(Spell const* /*focusSpell*/ = nullptr) const { return false; }
        virtual bool IsMovementPreventedByCasting() const;

        SpellHistory* GetSpellHistory() { return _spellHistory; }
        SpellHistory const* GetSpellHistory() const { return _spellHistory; }

        ObjectGuid m_SummonSlot[MAX_SUMMON_SLOT];
        ObjectGuid m_ObjectSlot[MAX_GAMEOBJECT_SLOT];

        ShapeshiftForm GetShapeshiftForm() const { return ShapeshiftForm(GetByteValue(UNIT_FIELD_BYTES_2, UNIT_BYTES_2_OFFSET_SHAPESHIFT_FORM)); }
        void SetShapeshiftForm(ShapeshiftForm form);

        bool IsInFeralForm() const;

        bool IsInDisallowedMountForm() const;

        float m_modMeleeHitChance;
        float m_modRangedHitChance;
        float m_modSpellHitChance;
        float m_baseSpellCritChance;

        // ====================================================================
        // 攻击速度和计时器
        // ====================================================================
        float m_modAttackSpeedPct[MAX_ATTACK];          // 攻击速度百分比修饰（主手、副手、远程）
        uint32 m_attackTimer[MAX_ATTACK];               // 攻击计时器（主手、副手、远程）

        // ====================================================================
        // 属性系统
        // ====================================================================
        void HandleStatFlatModifier(UnitMods unitMod, UnitModifierFlatType modifierType, float amount, bool apply);
        void ApplyStatPctModifier(UnitMods unitMod, UnitModifierPctType modifierType, float amount);

        void SetStatFlatModifier(UnitMods unitMod, UnitModifierFlatType modifierType, float val);
        void SetStatPctModifier(UnitMods unitMod, UnitModifierPctType modifierType, float val);

        float GetFlatModifierValue(UnitMods unitMod, UnitModifierFlatType modifierType) const;
        float GetPctModifierValue(UnitMods unitMod, UnitModifierPctType modifierType) const;

        void UpdateUnitMod(UnitMods unitMod);

        // only players have item requirements
        virtual bool CheckAttackFitToAuraRequirement(WeaponAttackType /*attackType*/, AuraEffect const* /*aurEff*/) const { return true; }

        virtual void UpdateDamageDoneMods(WeaponAttackType attackType, int32 skipEnchantSlot = -1);
        void UpdateAllDamageDoneMods();

        void UpdateDamagePctDoneMods(WeaponAttackType attackType);
        void UpdateAllDamagePctDoneMods();

        float GetTotalStatValue(Stats stat) const;
        float GetTotalAuraModValue(UnitMods unitMod) const;
        SpellSchools GetSpellSchoolByAuraGroup(UnitMods unitMod) const;
        Stats GetStatByAuraGroup(UnitMods unitMod) const;
        Powers GetPowerTypeByAuraGroup(UnitMods unitMod) const;
        bool CanModifyStats() const { return m_canModifyStats; }
        void SetCanModifyStats(bool modifyStats) { m_canModifyStats = modifyStats; }
        virtual bool UpdateStats(Stats stat) = 0;
        virtual bool UpdateAllStats() = 0;
        virtual void UpdateResistances(uint32 school) = 0;
        virtual void UpdateAllResistances();
        virtual void UpdateArmor() = 0;
        virtual void UpdateMaxHealth() = 0;
        virtual void UpdateMaxPower(Powers power) = 0;
        virtual void UpdateAttackPowerAndDamage(bool ranged = false) = 0;
        void SetAttackPower(int32 attackPower) { SetInt32Value(UNIT_FIELD_ATTACK_POWER, attackPower); }
        void SetAttackPowerModPos(int32 attackPowerMod) { SetInt16Value(UNIT_FIELD_ATTACK_POWER_MODS, 0, attackPowerMod); }
        void SetAttackPowerModNeg(int32 attackPowerMod) { SetInt16Value(UNIT_FIELD_ATTACK_POWER_MODS, 1, attackPowerMod); }
        void SetAttackPowerMultiplier(float attackPowerMult) { SetFloatValue(UNIT_FIELD_ATTACK_POWER_MULTIPLIER, attackPowerMult); }
        void SetRangedAttackPower(int32 attackPower) { SetInt32Value(UNIT_FIELD_RANGED_ATTACK_POWER, attackPower); }
        void SetRangedAttackPowerModPos(int32 attackPowerMod) { SetInt16Value(UNIT_FIELD_RANGED_ATTACK_POWER_MODS, 0, attackPowerMod); }
        void SetRangedAttackPowerModNeg(int32 attackPowerMod) { SetInt16Value(UNIT_FIELD_RANGED_ATTACK_POWER_MODS, 1, attackPowerMod); }
        void SetRangedAttackPowerMultiplier(float attackPowerMult) { SetFloatValue(UNIT_FIELD_RANGED_ATTACK_POWER_MULTIPLIER, attackPowerMult); }
        virtual void UpdateDamagePhysical(WeaponAttackType attType);
        float GetTotalAttackPowerValue(WeaponAttackType attType) const;
        float GetWeaponDamageRange(WeaponAttackType attType, WeaponDamageRange type, uint8 damageIndex = 0) const;
        void SetBaseWeaponDamage(WeaponAttackType attType, WeaponDamageRange damageRange, float value, uint8 damageIndex = 0) { m_weaponDamage[attType][damageRange][damageIndex] = value; }
        virtual void CalculateMinMaxDamage(WeaponAttackType attType, bool normalized, bool addTotalPct, float& minDamage, float& maxDamage, uint8 damageIndex) const = 0;
        uint32 CalculateDamage(WeaponAttackType attType, bool normalized, bool addTotalPct, uint8 itemDamagesMask = 0) const;
        float GetAPMultiplier(WeaponAttackType attType, bool normalized) const;

        bool isInFrontInMap(Unit const* target, float distance, float arc = float(M_PI)) const;
        bool isInBackInMap(Unit const* target, float distance, float arc = float(M_PI)) const;

        // Visibility system
        bool IsVisible() const;
        void SetVisible(bool x);

        // common function for visibility checks for player/creatures with detection code
        void SetPhaseMask(uint32 newPhaseMask, bool update) override;// overwrite WorldObject::SetPhaseMask
        void UpdateObjectVisibility(bool forced = true) override;

        SpellImmuneContainer m_spellImmune[MAX_SPELL_IMMUNITY];
        uint32 m_lastSanctuaryTime;

        VisibleAuraMap const& GetVisibleAuras() const { return m_visibleAuras; }
        AuraApplication* GetVisibleAura(uint8 slot) const;
        void SetVisibleAura(uint8 slot, AuraApplication* aurApp);
        void RemoveVisibleAura(uint8 slot);

        bool HasInterruptFlag(uint32 flags) const { return (m_interruptMask & flags) != 0; }
        void AddInterruptMask(uint32 flags) { m_interruptMask |= flags; }
        void UpdateInterruptMask();

        virtual float GetNativeObjectScale() const { return 1.0f; }
        virtual void RecalculateObjectScale();
        uint32 GetDisplayId() const { return GetUInt32Value(UNIT_FIELD_DISPLAYID); }
        virtual void SetDisplayId(uint32 modelId);
        uint32 GetNativeDisplayId() const { return GetUInt32Value(UNIT_FIELD_NATIVEDISPLAYID); }
        void RestoreDisplayId();
        void SetNativeDisplayId(uint32 displayId) { SetUInt32Value(UNIT_FIELD_NATIVEDISPLAYID, displayId); }
        void SetTransformSpell(uint32 spellid) { m_transformSpell = spellid;}
        uint32 GetTransformSpell() const { return m_transformSpell;}

        // DynamicObject management
        void _RegisterDynObject(DynamicObject* dynObj);
        void _UnregisterDynObject(DynamicObject* dynObj);
        DynamicObject* GetDynObject(uint32 spellId) const;
        std::vector<DynamicObject*> GetDynObjects(uint32 spellId) const;
        void RemoveDynObject(uint32 spellId);
        void RemoveAllDynObjects();

        GameObject* GetGameObject(uint32 spellId) const;
        std::vector<GameObject*> GetGameObjects(uint32 spellId) const;
        void AddGameObject(GameObject* gameObj);
        void RemoveGameObject(GameObject* gameObj, bool del);
        void RemoveGameObject(uint32 spellid, bool del);
        void RemoveAllGameObjects();

        void ModifyAuraState(AuraStateType flag, bool apply);
        uint32 BuildAuraStateUpdateForTarget(Unit const* target) const;
        bool HasAuraState(AuraStateType flag, SpellInfo const* spellProto = nullptr, Unit const* Caster = nullptr) const;
        void UnsummonAllTotems();
        bool IsMagnet() const;
        Unit* GetMeleeHitRedirectTarget(Unit* victim, SpellInfo const* spellInfo = nullptr);

        int32 SpellBaseDamageBonusDone(SpellSchoolMask schoolMask) const;
        uint32 SpellDamageBonusDone(Unit* victim, SpellInfo const* spellProto, uint32 pdamage, DamageEffectType damagetype, SpellEffectInfo const& spellEffectInfo, Optional<float> const& donePctTotal, uint32 stack = 1) const;
        float SpellDamagePctDone(Unit* victim, SpellInfo const* spellProto, DamageEffectType damagetype) const;
        uint32 SpellDamageBonusTaken(Unit* caster, SpellInfo const* spellProto, uint32 pdamage, DamageEffectType damagetype) const;
        int32 SpellBaseHealingBonusDone(SpellSchoolMask schoolMask) const;
        uint32 SpellHealingBonusDone(Unit* victim, SpellInfo const* spellProto, uint32 healamount, DamageEffectType damagetype, SpellEffectInfo const& spellEffectInfo, Optional<float> const& donePctTotal, uint32 stack = 1) const;
        float SpellHealingPctDone(Unit* victim, SpellInfo const* spellProto) const;
        uint32 SpellHealingBonusTaken(Unit* caster, SpellInfo const* spellProto, uint32 healamount, DamageEffectType damagetype) const;

        uint32 MeleeDamageBonusDone(Unit* pVictim, uint32 damage, WeaponAttackType attType, SpellInfo const* spellProto = nullptr, SpellSchoolMask damageSchoolMask = SPELL_SCHOOL_MASK_NORMAL);
        uint32 MeleeDamageBonusTaken(Unit* attacker, uint32 pdamage, WeaponAttackType attType, SpellInfo const* spellProto = nullptr, SpellSchoolMask damageSchoolMask = SPELL_SCHOOL_MASK_NORMAL);

        bool IsBlockCritical();
        float SpellCritChanceDone(SpellInfo const* spellInfo, SpellSchoolMask schoolMask, WeaponAttackType attackType = BASE_ATTACK, bool isPeriodic = false) const;
        float SpellCritChanceTaken(Unit const* caster, SpellInfo const* spellInfo, SpellSchoolMask schoolMask, float doneChance, WeaponAttackType attackType = BASE_ATTACK, bool isPeriodic = false) const;
        static uint32 SpellCriticalDamageBonus(Unit const* caster, SpellInfo const* spellProto, uint32 damage, Unit* victim);
        static uint32 SpellCriticalHealingBonus(Unit const* caster, SpellInfo const* spellProto, uint32 damage, Unit* victim);

        void SetLastManaUse(uint32 spellCastTime) { m_lastManaUse = spellCastTime; }
        bool IsUnderLastManaUseEffect() const;

        uint32 GetCastingTimeForBonus(SpellInfo const* spellProto, DamageEffectType damagetype, uint32 CastingTime) const;
        float CalculateDefaultCoefficient(SpellInfo const* spellInfo, DamageEffectType damagetype) const;

        void ApplySpellImmune(uint32 spellId, uint32 op, uint32 type, bool apply);
        virtual bool IsImmunedToSpell(SpellInfo const* spellInfo, WorldObject const* caster, bool requireImmunityPurgesEffectAttribute = false) const;
        uint32 GetSchoolImmunityMask() const;
        uint32 GetDamageImmunityMask() const;
        uint32 GetMechanicImmunityMask() const;

        bool IsImmunedToDamage(SpellSchoolMask meleeSchoolMask) const;
        bool IsImmunedToDamage(SpellInfo const* spellInfo) const;
        virtual bool IsImmunedToSpellEffect(SpellInfo const* spellInfo, SpellEffectInfo const& spellEffectInfo, WorldObject const* caster, bool requireImmunityPurgesEffectAttribute = false) const;

        static bool IsDamageReducedByArmor(SpellSchoolMask damageSchoolMask, SpellInfo const* spellInfo = nullptr);
        static uint32 CalcArmorReducedDamage(Unit const* attacker, Unit* victim, uint32 damage, SpellInfo const* spellInfo, WeaponAttackType attackType = MAX_ATTACK, uint8 attackerLevel = 0);
        static uint32 CalcSpellResistedDamage(Unit const* attacker, Unit* victim, uint32 damage, SpellSchoolMask schoolMask, SpellInfo const* spellInfo);
        static void CalcAbsorbResist(DamageInfo& damageInfo, Spell* spell = nullptr);
        static void CalcHealAbsorb(HealInfo& healInfo);

        void UpdateSpeed(UnitMoveType mtype);
        float GetSpeed(UnitMoveType mtype) const;
        float GetSpeedRate(UnitMoveType mtype) const { return m_speed_rate[mtype]; }
        void SetSpeed(UnitMoveType mtype, float newValue);
        void SetSpeedRate(UnitMoveType mtype, float rate);
    private:
        void SetSpeedRateReal(UnitMoveType mtype, float rate);

    public:
        float CalculateSpellpowerCoefficientLevelPenalty(SpellInfo const* spellInfo) const;

        void FollowerAdded(AbstractFollower* f) { m_followingMe.insert(f); }
        void FollowerRemoved(AbstractFollower* f) { m_followingMe.erase(f); }
        void RemoveAllFollowers();

        MotionMaster* GetMotionMaster() { return i_motionMaster; }
        MotionMaster const* GetMotionMaster() const { return i_motionMaster; }
        virtual MovementGeneratorType GetDefaultMovementType() const;

        bool IsStopped() const { return !(HasUnitState(UNIT_STATE_MOVING)); }
        void StopMoving();
        void PauseMovement(uint32 timer = 0, uint8 slot = 0, bool forced = true); // timer in ms
        void ResumeMovement(uint32 timer = 0, uint8 slot = 0); // timer in ms

        void AddUnitMovementFlag(uint32 f) { m_movementInfo.AddMovementFlag(f); }
        void RemoveUnitMovementFlag(uint32 f) { m_movementInfo.RemoveMovementFlag(f); }
        bool HasUnitMovementFlag(uint32 f) const { return m_movementInfo.HasMovementFlag(f); }
        uint32 GetUnitMovementFlags() const { return m_movementInfo.GetMovementFlags(); }
        void SetUnitMovementFlags(uint32 f) { m_movementInfo.SetMovementFlags(f); }

        void AddExtraUnitMovementFlag(uint32 f) { m_movementInfo.AddExtraMovementFlag(f); }
        void RemoveExtraUnitMovementFlag(uint32 f) { m_movementInfo.RemoveExtraMovementFlag(f); }
        bool HasExtraUnitMovementFlag(uint32 f) const { return m_movementInfo.HasExtraMovementFlag(f); }
        uint32 GetExtraUnitMovementFlags() const { return m_movementInfo.GetExtraMovementFlags(); }
        void SetExtraUnitMovementFlags(uint32 f) { m_movementInfo.SetExtraMovementFlags(f); }

        bool IsSplineEnabled() const;

        void SetControlled(bool apply, UnitState state);
        void ApplyControlStatesIfNeeded();

        ///-----------Combo point system-------------------
        // This unit having CP on other units
        uint8 GetComboPoints(Unit const* who = nullptr) const { return (who && m_comboTarget != who) ? 0 : m_comboPoints; }
        uint8 GetComboPoints(ObjectGuid const& guid) const { return (m_comboTarget && m_comboTarget->GetGUID() == guid) ? m_comboPoints : 0; }
        Unit* GetComboTarget() const { return m_comboTarget; }
        ObjectGuid GetComboTargetGUID() const { return m_comboTarget ? m_comboTarget->GetGUID() : ObjectGuid::Empty; }
        void AddComboPoints(Unit* target, int8 count);
        void AddComboPoints(int8 count) { AddComboPoints(nullptr, count); }
        void ClearComboPoints();
        void SendComboPoints();
        // Other units having CP on this unit
        void AddComboPointHolder(Unit* unit) { m_ComboPointHolders.insert(unit); }
        void RemoveComboPointHolder(Unit* unit) { m_ComboPointHolders.erase(unit); }
        void ClearComboPointHolders();

        ///----------Pet responses methods-----------------
        void SendPetActionFeedback(uint8 msg);
        void SendPetTalk(uint32 pettalk);
        void SendPetAIReaction(ObjectGuid guid);
        ///----------End of Pet responses methods----------

        void PropagateSpeedChange();

        // reactive attacks
        void ClearAllReactives();
        void StartReactiveTimer(ReactiveType reactive) { m_reactiveTimer[reactive] = REACTIVE_TIMER_START;}
        void UpdateReactives(uint32 p_time);

        // group updates
        void UpdateAuraForGroup(uint8 slot);

        // proc trigger system
        bool CanProc() const { return !m_procDeep; }
        void SetCantProc(bool apply);

        uint32 GetModelForForm(ShapeshiftForm form, uint32 spellId) const;

        friend class VehicleJoinEvent;
        ObjectGuid LastCharmerGUID;
        bool CreateVehicleKit(uint32 id, uint32 creatureEntry);
        void RemoveVehicleKit();
        Vehicle* GetVehicleKit() const { return m_vehicleKit.get(); }
        Trinity::unique_weak_ptr<Vehicle> GetVehicleKitWeakPtr() const { return m_vehicleKit; }
        Vehicle* GetVehicle() const { return m_vehicle; }
        void SetVehicle(Vehicle* vehicle) { m_vehicle = vehicle; }
        bool IsOnVehicle(Unit const* vehicle) const;
        Unit* GetVehicleBase() const;
        Unit* GetVehicleRoot() const;
        Creature* GetVehicleCreatureBase() const;
        ObjectGuid GetTransGUID()   const override;
        /// Returns the transport this unit is on directly (if on vehicle and transport, return vehicle)
        TransportBase* GetDirectTransport() const;

        void HandleSpellClick(Unit* clicker, int8 seatId = -1);
        void EnterVehicle(Unit* base, int8 seatId = -1);
        virtual void ExitVehicle(Position const* exitPosition = nullptr);
        void ChangeSeat(int8 seatId, bool next = true);

        // Should only be called by AuraEffect::HandleAuraControlVehicle(AuraApplication const* auraApp, uint8 mode, bool apply) const;
        void _ExitVehicle(Position const* exitPosition = nullptr);
        void _EnterVehicle(Vehicle* vehicle, int8 seatId, AuraApplication const* aurApp = nullptr);

        void BuildMovementPacket(ByteBuffer* data) const;
        static void BuildMovementPacket(Position const& pos, Position const& transportPos, MovementInfo const& movementInfo, ByteBuffer* data);

        bool isMoving() const   { return m_movementInfo.HasMovementFlag(MOVEMENTFLAG_MASK_MOVING); }
        bool isTurning() const  { return m_movementInfo.HasMovementFlag(MOVEMENTFLAG_MASK_TURNING); }
        virtual bool CanFly() const = 0;
        bool IsFlying() const   { return m_movementInfo.HasMovementFlag(MOVEMENTFLAG_FLYING | MOVEMENTFLAG_DISABLE_GRAVITY); }
        bool IsFalling() const;
        virtual bool CanEnterWater() const = 0;
        virtual bool CanSwim() const;

        float GetHoverOffset() const
        {
            return HasUnitMovementFlag(MOVEMENTFLAG_HOVER) ? GetFloatValue(UNIT_FIELD_HOVERHEIGHT) : 0.0f;
        }

        uint32 GetMovementCounterAndInc() { return m_movementCounter++; }
        PlayerMovementPendingChange& PeakFirstPendingMovementChange();
        PlayerMovementPendingChange PopPendingMovementChange();
        void PushPendingMovementChange(PlayerMovementPendingChange newChange);
        bool HasPendingMovementChange() const { return !m_pendingMovementChanges.empty(); }
        bool HasPendingMovementChange(MovementChangeType changeType) const;
        void PurgeAndApplyPendingMovementChanges(bool informObservers = true);

        void RewardRage(uint32 damage, uint32 weaponSpeedHitFactor, bool attacker);

        virtual float GetFollowAngle() const { return static_cast<float>(M_PI/2); }

        void OutDebugInfo() const;
        virtual bool IsLoading() const { return false; }
        bool IsDuringRemoveFromWorld() const {return m_duringRemoveFromWorld;}

        Pet* ToPet() { if (IsPet()) return reinterpret_cast<Pet*>(this); else return nullptr; }
        Pet const* ToPet() const { if (IsPet()) return reinterpret_cast<Pet const*>(this); else return nullptr; }

        Totem* ToTotem() { if (IsTotem()) return reinterpret_cast<Totem*>(this); else return nullptr; }
        Totem const* ToTotem() const { if (IsTotem()) return reinterpret_cast<Totem const*>(this); else return nullptr; }

        TempSummon* ToTempSummon() { if (IsSummon()) return reinterpret_cast<TempSummon*>(this); else return nullptr; }
        TempSummon const* ToTempSummon() const { if (IsSummon()) return reinterpret_cast<TempSummon const*>(this); else return nullptr; }

        ObjectGuid GetTarget() const { return GetGuidValue(UNIT_FIELD_TARGET); }
        virtual void SetTarget(ObjectGuid /*guid*/) = 0;

        void SetInstantCast(bool set) { _instantCast = set; }
        bool CanInstantCast() const { return _instantCast; }

        // ====================================================================
        // 移动信息
        // ====================================================================
        Movement::MoveSpline * movespline;              // 当前移动样条（用于平滑移动）

        int32 GetHighestExclusiveSameEffectSpellGroupValue(AuraEffect const* aurEff, AuraType auraType, bool checkMiscValue = false, int32 miscValue = 0) const;
        bool IsHighestExclusiveAura(Aura const* aura, bool removeOtherAuraApplications = false);
        bool IsHighestExclusiveAuraEffect(SpellInfo const* spellInfo, AuraType auraType, int32 effectAmount, uint8 auraEffectMask, bool removeOtherAuraApplications = false);

        virtual void Talk(std::string_view text, ChatMsg msgType, Language language, float textRange, WorldObject const* target);
        virtual void Say(std::string_view text, Language language, WorldObject const* target = nullptr);
        virtual void Yell(std::string_view text, Language language, WorldObject const* target = nullptr);
        virtual void TextEmote(std::string_view text, WorldObject const* target = nullptr, bool isBossEmote = false);
        virtual void Whisper(std::string_view text, Language language, Player* target, bool isBossWhisper = false);
        virtual void Talk(uint32 textId, ChatMsg msgType, float textRange, WorldObject const* target);
        virtual void Say(uint32 textId, WorldObject const* target = nullptr);
        virtual void Yell(uint32 textId, WorldObject const* target = nullptr);
        virtual void TextEmote(uint32 textId, WorldObject const* target = nullptr, bool isBossEmote = false);
        virtual void Whisper(uint32 textId, Player* target, bool isBossWhisper = false);

        float GetCollisionHeight() const override;
        uint32 GetVirtualItemId(uint32 slot) const;
        void SetVirtualItem(uint32 slot, uint32 itemId);

        // returns if the unit can't enter combat
        bool IsCombatDisallowed() const { return _isCombatDisallowed; }
        // enables / disables combat interaction of this unit
        void SetIsCombatDisallowed(bool apply) { _isCombatDisallowed = apply; }

        std::string GetDebugInfo() const override;

    protected:
        explicit Unit (bool isWorldObject);

        void BuildValuesUpdate(uint8 updatetype, ByteBuffer* data, Player const* target) const override;

        void _UpdateSpells(uint32 time);
        void _DeleteRemovedAuras();

        void _UpdateAutoRepeatSpell();

        // ====================================================================
        // 基础状态标志
        // ====================================================================

        /**
         * @brief 是否被玩家控制
         *
         * 当此单位被玩家控制时为 true。
         * 包括玩家自己的角色、被魅惑的单位、宠物等。
         * 影响移动确认系统和某些客户端行为。
         */
        bool m_ControlledByPlayer;

        /**
         * @brief 自动重复施法的首次施法标志
         *
         * 用于自动射击等自动重复法术。
         * 首次施法时的行为可能与后续自动施法不同。
         */
        bool m_AutoRepeatFirstCast;

        // ====================================================================
        // 基础属性值
        // ====================================================================

        /**
         * @brief 创建时的基础属性值数组
         *
         * 存储单位创建时的基础属性值（未修正的原始值）。
         * 索引对应 Stats 枚举：力量、敏捷、耐力、智力、精神。
         * 用于属性计算的基础值。
         */
        float m_createStats[MAX_STATS];

        // ====================================================================
        // 战斗相关
        // ====================================================================

        /**
         * @brief 攻击者集合
         *
         * 存储所有正在攻击此单位的单位。
         * 用于计算战斗状态、仇恨传递等。
         * 当单位死亡或脱离战斗时会清空。
         */
        AttackerSet m_attackers;

        /**
         * @brief 当前正在攻击的目标
         *
         * 此单位当前近战攻击的目标。
         * 可能为 nullptr 表示没有攻击目标。
         * 通过 Attack() 和 AttackStop() 函数管理。
         */
        Unit* m_attacking;

        /**
         * @brief 死亡状态
         *
         * 描述单位的生命/死亡状态。
         * 可能的值：ALIVE（存活）、JUST_DIED（刚死亡）、
         * CORPSE（尸体状态）、DEAD（完全死亡）、JUST_RESPAWNED（刚复活）。
         */
        DeathState m_deathState;

        /**
         * @brief 触发深度计数器
         *
         * 用于防止光环触发导致的无限递归。
         * 当进入触发处理时递增，退出时递减。
         * 如果值 > 0，则禁止新的触发。
         */
        int32 m_procDeep;

        // ====================================================================
        // 动态对象和游戏对象
        // ====================================================================

        typedef std::list<DynamicObject*> DynObjectList;

        /**
         * @brief 属于此单位的动态对象列表
         *
         * 存储此单位创建的动态对象（如暴风雪、奉献等区域效果）。
         * 当单位被移除或死亡时会自动清理。
         */
        DynObjectList m_dynObj;

        typedef std::list<GameObject*> GameObjectList;

        /**
         * @brief 属于此单位的游戏对象列表
         *
         * 存储此单位创建的游戏对象（如陷阱、图腾等）。
         * 当单位被移除时会自动清理。
         */
        GameObjectList m_gameObj;

        // ====================================================================
        // 法术相关
        // ====================================================================

        /**
         * @brief 变身法术ID
         *
         * 当前使单位变形的法术ID。
         * 用于德鲁伊变形、术士恶魔变形等。
         * 0 表示没有变身效果。
         */
        uint32 m_transformSpell;

        /**
         * @brief 当前正在施放的法术数组
         *
         * 存储各类型的当前施放法术：
         * - CURRENT_MELEE_SPELL：近战攻击（自动攻击）
         * - CURRENT_GENERIC_SPELL：普通法术
         * - CURRENT_CHANNELED_SPELL：引导法术
         * - CURRENT_AUTOREPEAT_SPELL：自动重复法术（自动射击）
         * 每种类型同时只能有一个法术在施放。
         */
        Spell* m_currentSpells[CURRENT_MAX_SPELL];

        // ====================================================================
        // 光环系统
        // ====================================================================

        /**
         * @brief 此单位拥有的光环映射
         *
         * 此单位施放到自己或他人身上的光环。
         * 键为法术ID，值为 Aura 指针。
         * 例如：玩家施放的持续治疗光环会存储在此。
         */
        AuraMap m_ownedAuras;

        /**
         * @brief 应用到此单位的光环映射
         *
         * 所有生效在此单位身上的光环应用。
         * 键为法术ID，值为 AuraApplication 指针。
         * 一个光环可能同时应用在多个目标上。
         */
        AuraApplicationMap m_appliedAuras;

        /**
         * @brief 已移除的光环列表
         *
         * 存储已被标记为删除但尚未清理的光环。
         * 延迟删除是为了避免在迭代过程中修改容器。
         * 在下一帧更新时会清理这些光环。
         */
        AuraList m_removedAuras;

        /**
         * @brief 光环更新迭代器
         *
         * 用于批量更新光环时记录当前位置。
         * 支持分帧更新大量光环以避免卡顿。
         */
        AuraMap::iterator m_auraUpdateIterator;

        /**
         * @brief 已移除光环计数
         *
         * 记录自上次清理以来移除的光环数量。
         * 用于性能监控和调试。
         */
        uint32 m_removedAurasCount;

        /**
         * @brief 按类型分类的光环效果列表
         *
         * 将光环效果按 AuraType 分类存储，便于快速查找。
         * 例如：所有增加攻击强度的光环效果存储在 m_modAuras[SPELL_AURA_MOD_ATTACK_POWER]。
         * 大大提高了光环效果查询效率。
         */
        AuraEffectList m_modAuras[TOTAL_AURAS];

        /**
         * @brief 单次施法光环列表
         *
         * 存储单次施法产生的新光环。
         * 用于处理光环堆叠和刷新逻辑。
         * 在光环应用后会被清理。
         */
        AuraList m_scAuras;

        /**
         * @brief 可中断的光环列表
         *
         * 存储所有拥有中断标志的光环应用。
         * 当单位受到伤害、移动等事件时，可以快速找到需要检查的光环。
         * 例如：施法时被打断会检查此列表。
         */
        AuraApplicationList m_interruptableAuras;

        /**
         * @brief 光环状态映射
         *
         * 将光环状态类型映射到拥有该状态的光环应用。
         * 用于快速判断单位是否满足特定光环状态条件。
         * 例如：检查是否有 AURA_STATE_FROZEN（冰冻状态）。
         */
        AuraStateAurasMap m_auraStateAuras;

        /**
         * @brief 中断掩码
         *
         * 所有可中断光环的中断标志的按位或。
         * 用于快速判断某个事件是否可能中断任何光环。
         * 避免遍历所有光环。
         */
        uint32 m_interruptMask;

        // ====================================================================
        // 属性修饰系统
        // ====================================================================

        /**
         * @brief 光环固定值修饰器组
         *
         * 二维数组，存储各属性的固定值修饰。
         * 第一维：UnitMods 枚举（属性类型）
         * 第二维：BASE_VALUE（基础值修正）/ TOTAL_VALUE（总值修正）
         * 例如：增加100点力量的光环会修改此数组。
         */
        float m_auraFlatModifiersGroup[UNIT_MOD_END][MODIFIER_TYPE_FLAT_END];

        /**
         * @brief 光环百分比修饰器组
         *
         * 二维数组，存储各属性的百分比修饰。
         * 第一维：UnitMods 枚举（属性类型）
         * 第二维：BASE_PCT（基础百分比）/ TOTAL_PCT（总百分比）
         * 例如：增加10%生命值的光环会修改此数组。
         */
        float m_auraPctModifiersGroup[UNIT_MOD_END][MODIFIER_TYPE_PCT_END];

        /**
         * @brief 武器伤害数组
         *
         * 三维数组，存储武器伤害范围。
         * 第一维：攻击类型（主手、副手、远程）
         * 第二维：伤害范围（最小伤害、最大伤害）
         * 第三维：伤害索引（用于多段伤害武器）
         */
        float m_weaponDamage[MAX_ATTACK][2][2];

        /**
         * @brief 是否可以修改属性
         *
         * 在某些情况下（如初始化、加载）需要暂时禁止属性修改。
         * 为 false 时，属性修改请求会被延迟或忽略。
         */
        bool m_canModifyStats;

        /**
         * @brief 可见光环映射
         *
         * 存储客户端可见的光环应用，按槽位索引。
         * 用于向客户端发送光环更新包。
         * 只有8个可见槽位，其他光环对客户端不可见。
         */
        VisibleAuraMap m_visibleAuras;

        // ====================================================================
        // 移动速度
        // ====================================================================

        /**
         * @brief 移动速度倍率数组
         *
         * 存储各类型移动速度相对于基础速度的倍率。
         * 索引对应 UnitMoveType 枚举：
         * WALK（行走）、RUN（跑步）、RUN_BACK（后退）、
         * SWIM（游泳）、SWIM_BACK（游泳后退）、FLIGHT（飞行）等。
         * 倍率 1.0 表示基础速度，2.0 表示双倍速度。
         */
        float m_speed_rate[MAX_MOVE_TYPE];

        // ====================================================================
        // 单位控制关系
        // ====================================================================

        /**
         * @brief 施放魅惑效果的单位
         *
         * 正在魅惑此单位的单位（魅惑者）。
         * 例如：术士魅惑目标时，术士是 charmer。
         * 魅惑期间，此单位受魅惑者控制。
         */
        Unit* m_charmer;

        /**
         * @brief 被魅惑的单位
         *
         * 被此单位魅惑的单位（被魅惑者）。
         * 例如：术士魅惑目标时，目标是 charmed。
         */
        Unit* m_charmed;

        /**
         * @brief 魅惑信息结构
         *
         * 当单位被魅惑或成为宠物时，存储相关信息。
         * 包括宠物法术栏、命令状态、停留位置等。
         * 只有被魅惑/控制的单位才有此结构。
         */
        CharmInfo* m_charmInfo;

        /**
         * @brief 共享视野列表
         *
         * 存储与此单位共享视野的玩家列表。
         * 用于心灵视野等效果，允许玩家看到单位看到的内容。
         * 一个单位可以有多个玩家共享视野。
         */
        SharedVisionList m_sharedVision;

        /**
         * @brief 正在控制此单位移动的游戏客户端
         *
         * 当玩家控制此单位时，指向对应的 GameClient。
         * 非玩家单位或由服务器控制的单位为 nullptr。
         * 用于确定移动权限和确认流程。
         */
        GameClient* _gameClientMovingMe;

        // ====================================================================
        // 移动系统
        // ====================================================================

        /**
         * @brief 移动生成器管理器
         *
         * 管理单位的所有移动行为。
         * 支持多种移动生成器：追逐、跟随、漫游、逃跑等。
         * 使用栈结构管理，支持移动中断和恢复。
         */
        MotionMaster* i_motionMaster;

        /**
         * @brief 反应计时器数组
         *
         * 存储各种反应技能的计时器。
         * 索引对应 ReactiveType 枚举：
         * DEFENSE（防御）、HUNTER_PARRY（猎人招架）、
         * OVERPOWER（压制）、WOLVERINE_BITE（狼咬）。
         * 计时器用于限制反应技能的使用频率。
         */
        uint32 m_reactiveTimer[MAX_REACTIVE];

        /**
         * @brief 资源再生计时器
         *
         * 控制生命值和能量值的再生频率。
         * 每2秒触发一次再生（默认）。
         * 战斗状态、脱战状态可能有不同的再生速率。
         */
        uint32 m_regenTimer;

        // ====================================================================
        // 载具系统
        // ====================================================================

        /**
         * @brief 当前乘坐的载具
         *
         * 当单位作为乘客乘坐载具时，指向该载具对象。
         * 未乘坐载具时为 nullptr。
         * 载具可以是坐骑、攻城器械等。
         */
        Vehicle* m_vehicle;

        /**
         * @brief 载具组件
         *
         * 当此单位本身是载具时，存储载具相关数据。
         * 包括座位信息、乘客列表等。
         * 使用智能指针管理生命周期。
         */
        Trinity::unique_trackable_ptr<Vehicle> m_vehicleKit;

        /**
         * @brief 单位类型掩码
         *
         * 使用位标志标识单位的类型。
         * 可能的值：SUMMON（召唤物）、MINION（仆从）、GUARDIAN（守护者）、
         * TOTEM（图腾）、PET（宠物）、VEHICLE（载具）、HUNTER_PET（猎人宠物）等。
         * 用于快速判断单位类型。
         */
        uint32 m_unitTypeMask;

        /**
         * @brief 最后所在的液体类型
         *
         * 记录单位当前或最后所在的液体类型（水、岩浆、淤泥等）。
         * 用于液体相关效果的计算。
         * nullptr 表示不在液体中。
         */
        LiquidTypeEntry const* _lastLiquid;

        bool IsAlwaysVisibleFor(WorldObject const* seer) const override;
        bool IsAlwaysDetectableFor(WorldObject const* seer) const override;

        void DisableSpline();

        void ProcessPositionDataChanged(PositionFullTerrainStatus const& data) override;
        virtual void ProcessTerrainStatusUpdate(ZLiquidStatus oldLiquidStatus, Optional<LiquidData> const& newLiquidData);

        // notifiers
        virtual void AtEnterCombat() { }
        virtual void AtExitCombat();

        virtual void AtEngage(Unit* /*target*/) {}
        virtual void AtDisengage() {}

    private:

        void UpdateSplineMovement(uint32 t_diff);
        void UpdateSplinePosition();
        void InterruptMovementBasedAuras();
        void CheckPendingMovementAcks();

        // player or player's pet
        float GetCombatRatingReduction(CombatRating cr) const;
        uint32 GetCombatRatingDamageReduction(CombatRating cr, float rate, float cap, uint32 damage) const;

        void ProcSkillsAndReactives(bool isVictim, Unit* procTarget, uint32 typeMask, uint32 hitMask, WeaponAttackType attType);

    protected:
        void SetFeared(bool apply);
        void SetConfused(bool apply);
        void SetStunned(bool apply);
        void SetRooted(bool apply);

        uint32 m_rootTimes;                             // 定身次数计数

    private:

        // ====================================================================
        // 状态标志
        // ====================================================================

        /**
         * @brief 单位状态标志
         *
         * 使用位掩码存储单位的多种状态。
         * 包括：昏迷、恐惧、定身、施法中、移动中等。
         * 对应 UnitState 枚举。
         * 注意：派生类也不应直接修改此成员，应使用 AddUnitState/ClearUnitState。
         */
        uint32 m_state;

        /**
         * @brief 上次使用法力的时间
         *
         * 记录上次施放消耗法力的法术的时间（毫秒）。
         * 用于判断是否处于"施法后法力不恢复"状态。
         * 法力恢复规则：施法后5秒内不恢复法力（五秒规则）。
         */
        uint32 m_lastManaUse;

        /**
         * @brief 样条同步计时器
         *
         * 用于同步移动样条的位置。
         * 确保客户端和服务器位置一致。
         * 定期发送位置同步包。
         */
        TimeTracker m_splineSyncTimer;

        /**
         * @brief 递减效果数组
         *
         * 存储各类型控制效果的递减状态。
         * 索引对应 DiminishingGroup 枚举。
         * 用于实现控制技能的递减机制：
         * 连续被相同类型的控制技能命中时，持续时间递减。
         */
        Diminishing m_Diminishing;

        // ====================================================================
        // 威胁和战斗管理
        // ====================================================================

        friend class CombatManager;

        /**
         * @brief 战斗管理器
         *
         * 管理单位的战斗状态和参战目标。
         * 处理进入/退出战斗、战斗范围检查等。
         * 与威胁管理器协同工作。
         */
        CombatManager m_combatManager;

        friend class ThreatManager;

        /**
         * @brief 威胁管理器
         *
         * 管理单位的威胁列表和仇恨值。
         * 处理威胁增加/减少、目标选择等。
         * 生物使用威胁列表选择攻击目标。
         */
        ThreatManager m_threatManager;

        // ====================================================================
        // AI 系统
        // ====================================================================

        /**
         * @brief 更新魅惑AI
         *
         * 当单位被魅惑时，切换到魅惑AI。
         */
        void UpdateCharmAI();

        /**
         * @brief 恢复被禁用的AI
         *
         * 当魅惑效果结束时，恢复原始AI。
         */
        void RestoreDisabledAI();

        typedef std::stack<std::shared_ptr<UnitAI>> UnitAIStack;

        /**
         * @brief AI栈
         *
         * 存储AI切换历史，支持AI状态的保存和恢复。
         * 例如：魅惑结束后恢复原始AI。
         * 使用栈结构确保正确的恢复顺序。
         */
        UnitAIStack i_AIs;

        /**
         * @brief 当前AI
         *
         * 单位当前使用的AI实例。
         * 控制单位的行为（攻击、移动、施法等）。
         * 可能是原始AI、魅惑AI或脚本自定义AI。
         */
        std::shared_ptr<UnitAI> i_AI;

        /**
         * @brief AI是否被锁定
         *
         * 防止在AI更新过程中切换AI。
         * 避免AI状态不一致和崩溃。
         */
        bool m_aiLocked;

        /**
         * @brief 跟随此单位的跟随者集合
         *
         * 存储所有正在跟随此单位的 AbstractFollower 对象。
         * 用于管理和清理跟随关系。
         */
        std::unordered_set<AbstractFollower*> m_followingMe;

        // ====================================================================
        // 连击点系统
        // ====================================================================

        /**
         * @brief 连击点目标
         *
         * 当前连击点积累的目标。
         * 盗贼和德鲁士使用连击点系统。
         * 连击点只能积累在一个目标上。
         */
        Unit* m_comboTarget;

        /**
         * @brief 连击点数量
         *
         * 当前积累的连击点数量（0-5）。
         * 用于终结技的伤害计算。
         */
        int8 m_comboPoints;

        /**
         * @brief 持有此单位连击点的单位集合
         *
         * 存储所有在此单位上积累连击点的单位。
         * 当此单位死亡或消失时，需要通知这些单位清除连击点。
         */
        std::unordered_set<Unit*> m_ComboPointHolders;

        // ====================================================================
        // 额外攻击
        // ====================================================================

        /**
         * @brief 上次额外攻击的法术ID
         *
         * 记录触发额外攻击的法术ID。
         * 用于防止同一个法术多次触发额外攻击。
         */
        uint32 _lastExtraAttackSpell;

        /**
         * @brief 额外攻击目标映射
         *
         * 存储每个目标待执行的额外攻击次数。
         * 键：目标GUID，值：额外攻击次数。
         * 用于风怒武器、剑类专精等效果。
         */
        std::unordered_map<ObjectGuid /*guid*/, uint32 /*count*/> extraAttacksTargets;

        /**
         * @brief 上次受伤目标的GUID
         *
         * 记录最近一次受到此单位伤害的目标。
         * 用于某些需要"最近伤害目标"的法术效果。
         */
        ObjectGuid _lastDamagedTargetGuid;

        // ====================================================================
        // 清理和状态标志
        // ====================================================================

        /**
         * @brief 清理完成标志
         *
         * 标记单位是否已完成清理流程。
         * 防止在清理后再次添加内容导致崩溃。
         * 清理后不应再访问此单位的任何成员。
         */
        bool m_cleanupDone;

        /**
         * @brief 正在从世界移除标志
         *
         * 标记单位正在执行 RemoveFromWorld 流程。
         * 在此期间某些操作应该被跳过或延迟。
         */
        bool m_duringRemoveFromWorld;

        /**
         * @brief 瞬发施法标志
         *
         * 标记单位是否可以瞬发施法。
         * 用于某些特殊效果，使所有法术变为瞬发。
         */
        bool _instantCast;

        // ====================================================================
        // 魅惑前的状态保存
        // ====================================================================

        /**
         * @brief 魅惑前的阵营ID
         *
         * 保存魅惑前的原始阵营ID。
         * 魅惑期间可能会改变阵营。
         * 魅惑结束后恢复原始阵营。
         */
        uint32 _oldFactionId;

        /**
         * @brief 魅惑前是否在行走
         *
         * 保存魅惑前的行走状态。
         * 魅惑结束后恢复原始行走状态。
         */
        bool _isWalkingBeforeCharm;

        /**
         * @brief 法术历史
         *
         * 管理法术冷却、类别冷却和全局冷却。
         * 处理冷却时间查询、重置等。
         * 玩家和生物都有法术历史。
         */
        SpellHistory* _spellHistory;

        /**
         * @brief 位置更新信息
         *
         * 跟踪位置和朝向是否发生变化。
         * 用于优化更新包的发送。
         */
        PositionUpdateInfo _positionUpdateInfo;

        /**
         * @brief 是否禁止进入战斗
         *
         * 标记单位是否被禁止进入战斗状态。
         * 某些特殊单位或状态需要禁用战斗。
         */
        bool _isCombatDisallowed;

        // ====================================================================
        // 玩家移动字段
        // ====================================================================
        /* Player Movement fields START*/

        /**
         * @brief 移动计数器
         *
         * 当玩家控制此单位时，如果对此单位进行了需要客户端确认的更改
         * （例如速度更改、定身状态变更），此计数器会递增。
         * 用于确保客户端和服务器状态同步。
         */
        uint32 m_movementCounter;

        /**
         * @brief 待处理的移动更改队列
         *
         * 存储等待客户端确认的移动变更信息。
         * 包括速度变更、定身状态、飞行状态等。
         * 客户端确认后才会真正应用变更。
         */
        std::deque<PlayerMovementPendingChange> m_pendingMovementChanges;

        /* Player Movement fields END*/
};

namespace Trinity
{
    // Binary predicate for sorting Units based on percent value of a power
    class PowerPctOrderPred
    {
        public:
            PowerPctOrderPred(Powers power, bool ascending = true) : _power(power), _ascending(ascending) { }

            bool operator()(WorldObject const* objA, WorldObject const* objB) const
            {
                Unit const* a = objA->ToUnit();
                Unit const* b = objB->ToUnit();
                float rA = a ? a->GetPowerPct(_power) : 0.0f;
                float rB = b ? b->GetPowerPct(_power) : 0.0f;
                return _ascending ? rA < rB : rA > rB;
            }

            bool operator()(Unit const* a, Unit const* b) const
            {
                float rA = a->GetPowerPct(_power);
                float rB = b->GetPowerPct(_power);
                return _ascending ? rA < rB : rA > rB;
            }

        private:
            Powers const _power;
            bool const _ascending;
    };

    // Binary predicate for sorting Units based on percent value of health
    class HealthPctOrderPred
    {
        public:
            HealthPctOrderPred(bool ascending = true) : _ascending(ascending) { }

            bool operator()(WorldObject const* objA, WorldObject const* objB) const
            {
                Unit const* a = objA->ToUnit();
                Unit const* b = objB->ToUnit();
                float rA = (a && a->GetMaxHealth()) ? float(a->GetHealth()) / float(a->GetMaxHealth()) : 0.0f;
                float rB = (b && b->GetMaxHealth()) ? float(b->GetHealth()) / float(b->GetMaxHealth()) : 0.0f;
                return _ascending ? rA < rB : rA > rB;
            }

            bool operator() (Unit const* a, Unit const* b) const
            {
                float rA = a->GetMaxHealth() ? float(a->GetHealth()) / float(a->GetMaxHealth()) : 0.0f;
                float rB = b->GetMaxHealth() ? float(b->GetHealth()) / float(b->GetMaxHealth()) : 0.0f;
                return _ascending ? rA < rB : rA > rB;
            }

        private:
            bool const _ascending;
    };
}

#endif
