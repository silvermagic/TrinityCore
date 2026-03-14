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
 * @file CreatureData.h
 * @brief 生物实体数据定义模块
 *
 * 本模块定义了游戏中所有生物（NPC、怪物等）相关的数据结构。
 *
 * 主要内容：
 * - 生物静态标志枚举（CreatureStaticFlags 1-4）：定义生物的各种属性和行为标志
 * - 生物额外标志枚举（CreatureFlagsExtra）：服务器端额外的生物标志
 * - 生物移动类型枚举：定义地面、飞行、追逐、随机移动类型
 * - 生物移动数据结构：存储移动相关配置
 * - 生物模板结构（CreatureTemplate）：从creature_template表加载的生物静态数据
 * - 生物基础属性结构（CreatureBaseStats）：基础HP、法力、护甲等数值
 * - 生物实例数据（CreatureData）：从creature表加载的生物实例数据
 * - 商人数据结构（VendorItem/VendorItemData）：商人售卖物品数据
 *
 * 数据来源：
 * - creature_template表：生物模板数据
 * - creature表：生物实例数据
 * - creature_addon表：生物附加数据
 * - npc_vendor表：商人售卖物品
 */

#ifndef CreatureData_h__
#define CreatureData_h__

#include "DBCEnums.h"
#include "SharedDefines.h"
#include "SpawnData.h"
#include "UnitDefines.h"
#include "WorldPacket.h"
#include <string>
#include <unordered_map>
#include <vector>
#include <cmath>

struct ItemTemplate;
enum class VisibilityDistanceType : uint8;

/**
 * @brief 生物静态标志枚举（第一组）
 *
 * 定义生物的基本静态属性标志，这些标志从creature_template表加载。
 * 对应客户端的creature_template数据，影响生物的行为和属性。
 * 标志可以组合使用（位掩码）。
 */
enum CreatureStaticFlags
{
    CREATURE_STATIC_FLAG_MOUNTABLE                         = 0x00000001,  ///< 可作为坐骑使用
    CREATURE_STATIC_FLAG_NO_XP                             = 0x00000002,  ///< 不提供经验值（对应CREATURE_FLAG_EXTRA_NO_XP）
    CREATURE_STATIC_FLAG_NO_LOOT                           = 0x00000004,  ///< 无战利品
    CREATURE_STATIC_FLAG_UNKILLABLE                        = 0x00000008,  ///< 不可杀死
    CREATURE_STATIC_FLAG_TAMEABLE                          = 0x00000010,  ///< 可驯服（对应CREATURE_TYPE_FLAG_TAMEABLE）
    CREATURE_STATIC_FLAG_IMMUNE_TO_PC                      = 0x00000020,  ///< 对玩家角色免疫（对应UNIT_FLAG_IMMUNE_TO_PC）
    CREATURE_STATIC_FLAG_IMMUNE_TO_NPC                     = 0x00000040,  ///< 对NPC免疫（对应UNIT_FLAG_IMMUNE_TO_NPC）
    CREATURE_STATIC_FLAG_CAN_WIELD_LOOT                    = 0x00000080,  ///< 可以装备战利品
    CREATURE_STATIC_FLAG_SESSILE                           = 0x00000100,  ///< 固定不动（对应creature_template_movement.Rooted = 1）
    CREATURE_STATIC_FLAG_UNINTERACTIBLE                    = 0x00000200,  ///< 不可交互（对应UNIT_FLAG_UNINTERACTIBLE）
    CREATURE_STATIC_FLAG_NO_AUTOMATIC_REGEN                = 0x00000400,  ///< 无自动回复（不使用UNIT_FLAG2_REGENERATE_POWER）
    CREATURE_STATIC_FLAG_DESPAWN_INSTANTLY                 = 0x00000800,  ///< 死亡后立即消失
    CREATURE_STATIC_FLAG_CORPSE_RAID                       = 0x00001000,  ///< 尸体归属团队
    CREATURE_STATIC_FLAG_CREATOR_LOOT                      = 0x00002000,  ///< 只有创建者可拾取（如工程学假人）
    CREATURE_STATIC_FLAG_NO_DEFENSE                        = 0x00004000,  ///< 无防御
    CREATURE_STATIC_FLAG_NO_SPELL_DEFENSE                  = 0x00008000,  ///< 无法术防御
    CREATURE_STATIC_FLAG_BOSS_MOB                          = 0x00010000,  ///< 团队Boss生物（对应CREATURE_TYPE_FLAG_BOSS_MOB）
    CREATURE_STATIC_FLAG_COMBAT_PING                       = 0x00020000,  ///< 战斗Ping
    CREATURE_STATIC_FLAG_AQUATIC                           = 0x00040000,  ///< 水生生物（仅限水中，对应creature_template_movement.Ground = 0）
    CREATURE_STATIC_FLAG_AMPHIBIOUS                        = 0x00080000,  ///< 两栖生物（对应creature_template_movement.Swim = 1）
    CREATURE_STATIC_FLAG_NO_MELEE_FLEE                     = 0x00100000,  ///< 无近战逃跑（阻止近战，但不阻止追逐，不使生物被动）
    CREATURE_STATIC_FLAG_VISIBLE_TO_GHOSTS                 = 0x00200000,  ///< 对幽灵可见（对应CREATURE_TYPE_FLAG_VISIBLE_TO_GHOSTS）
    CREATURE_STATIC_FLAG_PVP_ENABLING                      = 0x00400000,  ///< 启用PvP标志（旧UNIT_FLAG_PVP_ENABLING，现用UNIT_BYTES_2）
    CREATURE_STATIC_FLAG_DO_NOT_PLAY_WOUND_ANIM            = 0x00800000,  ///< 不播放受伤动画（对应CREATURE_TYPE_FLAG_DO_NOT_PLAY_WOUND_ANIM）
    CREATURE_STATIC_FLAG_NO_FACTION_TOOLTIP                = 0x01000000,  ///< 无阵营提示框（对应CREATURE_TYPE_FLAG_NO_FACTION_TOOLTIP）
    CREATURE_STATIC_FLAG_IGNORE_COMBAT                     = 0x02000000,  ///< 忽略战斗（实际只将反应状态改为被动）
    CREATURE_STATIC_FLAG_ONLY_ATTACK_PVP_ENABLING          = 0x04000000,  ///< 只攻击启用PvP的目标
    CREATURE_STATIC_FLAG_CALLS_GUARDS                      = 0x08000000,  ///< 呼叫卫兵（玩家在仇恨范围内时召唤卫兵）
    CREATURE_STATIC_FLAG_CAN_SWIM                          = 0x10000000,  ///< 可游泳（对应UNIT_FLAG_CAN_SWIM）
    CREATURE_STATIC_FLAG_FLOATING                          = 0x20000000,  ///< 悬浮（对应creature_template_movement.Flight = 1）
    CREATURE_STATIC_FLAG_MORE_AUDIBLE                      = 0x40000000,  ///< 更大的听觉范围（对应CREATURE_TYPE_FLAG_MORE_AUDIBLE）
    CREATURE_STATIC_FLAG_LARGE_AOI                         = 0x80000000   ///< 大感兴趣区域（对应UnitFlags2 0x200000）
};

/**
 * @brief 生物静态标志枚举（第二组）
 *
 * 定义生物的扩展静态属性标志，提供更多高级功能配置。
 */
enum CreatureStaticFlags2
{
    CREATURE_STATIC_FLAG_2_NO_PET_SCALING                  = 0x00000001,  ///< 无宠物缩放
    CREATURE_STATIC_FLAG_2_FORCE_PARTY_MEMBERS_INTO_COMBAT = 0x00000002,  ///< 强制队伍成员进入战斗（原描述：强制团队战斗）
    CREATURE_STATIC_FLAG_2_RAID_LOCK_ON_DEATH              = 0x00000004,  ///< 死亡时锁定团队副本（对应CREATURE_FLAG_EXTRA_INSTANCE_BIND）
    CREATURE_STATIC_FLAG_2_SPELL_ATTACKABLE                = 0x00000008,  ///< 可被法术攻击（对应CREATURE_TYPE_FLAG_SPELL_ATTACKABLE）
    CREATURE_STATIC_FLAG_2_NO_CRUSHING_BLOWS               = 0x00000010,  ///< 无碾压攻击（对应CREATURE_FLAG_EXTRA_NO_CRUSHING_BLOWS）
    CREATURE_STATIC_FLAG_2_NO_OWNER_THREAT                 = 0x00000020,  ///< 对主人无威胁
    CREATURE_STATIC_FLAG_2_NO_WOUNDED_SLOWDOWN             = 0x00000040,  ///< 受伤不减速
    CREATURE_STATIC_FLAG_2_USE_CREATOR_BONUSES             = 0x00000080,  ///< 使用创建者加成
    CREATURE_STATIC_FLAG_2_IGNORE_FEIGN_DEATH              = 0x00000100,  ///< 忽略假死（对应CREATURE_FLAG_EXTRA_IGNORE_FEIGN_DEATH）
    CREATURE_STATIC_FLAG_2_IGNORE_SANCTUARY                = 0x00000200,  ///< 忽略避难所状态
    CREATURE_STATIC_FLAG_2_ACTION_TRIGGERS_WHILE_CHARMED   = 0x00000400,  ///< 被魅惑时动作触发器生效
    CREATURE_STATIC_FLAG_2_INTERACT_WHILE_DEAD             = 0x00000800,  ///< 死亡时可交互（对应CREATURE_TYPE_FLAG_INTERACT_WHILE_DEAD）
    CREATURE_STATIC_FLAG_2_NO_INTERRUPT_SCHOOL_COOLDOWN    = 0x00001000,  ///< 法术被打断不会触发系别冷却
    CREATURE_STATIC_FLAG_2_RETURN_SOUL_SHARD_TO_MASTER_OF_PET = 0x00002000,  ///< 将灵魂碎片返回给宠物主人
    CREATURE_STATIC_FLAG_2_SKIN_WITH_HERBALISM             = 0x00004000,  ///< 用草药学剥皮（对应CREATURE_TYPE_FLAG_SKIN_WITH_HERBALISM）
    CREATURE_STATIC_FLAG_2_SKIN_WITH_MINING                = 0x00008000,  ///< 用采矿剥皮（对应CREATURE_TYPE_FLAG_SKIN_WITH_MINING）
    CREATURE_STATIC_FLAG_2_ALERT_CONTENT_TEAM_ON_DEATH     = 0x00010000,  ///< 死亡时警报内容团队
    CREATURE_STATIC_FLAG_2_ALERT_CONTENT_TEAM_AT_90PTC_HP  = 0x00020000,  ///< 生命值90%时警报内容团队
    CREATURE_STATIC_FLAG_2_ALLOW_MOUNTED_COMBAT            = 0x00040000,  ///< 允许骑乘战斗（对应CREATURE_TYPE_FLAG_ALLOW_MOUNTED_COMBAT）
    CREATURE_STATIC_FLAG_2_PVP_ENABLING_OOC                = 0x00080000,  ///< 非战斗状态启用PvP
    CREATURE_STATIC_FLAG_2_NO_DEATH_MESSAGE                = 0x00100000,  ///< 无死亡消息（对应CREATURE_TYPE_FLAG_NO_DEATH_MESSAGE）
    CREATURE_STATIC_FLAG_2_IGNORE_PATHING_FAILURE          = 0x00200000,  ///< 忽略寻路失败
    CREATURE_STATIC_FLAG_2_FULL_SPELL_LIST                 = 0x00400000,  ///< 完整法术列表
    CREATURE_STATIC_FLAG_2_DOES_NOT_REDUCE_REPUTATION_FOR_RAIDS = 0x00800000,  ///< 团队不减少声望
    CREATURE_STATIC_FLAG_2_IGNORE_MISDIRECTION             = 0x01000000,  ///< 忽略误导
    CREATURE_STATIC_FLAG_2_HIDE_BODY                       = 0x02000000,  ///< 隐藏尸体（对应UNIT_FLAG2_HIDE_BODY）
    CREATURE_STATIC_FLAG_2_SPAWN_DEFENSIVE                 = 0x04000000,  ///< 以防御状态生成
    CREATURE_STATIC_FLAG_2_SERVER_ONLY                     = 0x08000000,  ///< 仅服务器端
    CREATURE_STATIC_FLAG_2_CAN_SAFE_FALL                   = 0x10000000,  ///< 可安全坠落（原描述：无碰撞）
    CREATURE_STATIC_FLAG_2_CAN_ASSIST                      = 0x20000000,  ///< 可协助（对应CREATURE_TYPE_FLAG_CAN_ASSIST）
    CREATURE_STATIC_FLAG_2_NO_SKILL_GAINS                  = 0x40000000,  ///< 无技能提升（对应CREATURE_FLAG_EXTRA_NO_SKILL_GAINS）
    CREATURE_STATIC_FLAG_2_NO_PET_BAR                      = 0x80000000   ///< 无宠物动作条（对应CREATURE_TYPE_FLAG_NO_PET_BAR）
};

/**
 * @brief 生物静态标志枚举（第三组）
 *
 * 定义生物的更多高级静态属性标志，包括各种特殊行为和状态。
 */
enum CreatureStaticFlags3
{
    CREATURE_STATIC_FLAG_3_NO_DAMAGE_HISTORY              = 0x00000001,  ///< 无伤害历史
    CREATURE_STATIC_FLAG_3_DONT_PVP_ENABLE_OWNER          = 0x00000002,  ///< 不对主人启用PvP
    CREATURE_STATIC_FLAG_3_DO_NOT_FADE_IN                 = 0x00000004,  ///< 不淡入（对应UNIT_FLAG2_DO_NOT_FADE_IN）
    CREATURE_STATIC_FLAG_3_MASK_UID                       = 0x00000008,  ///< 掩码UID（对应CREATURE_TYPE_FLAG_MASK_UID，原描述：战斗日志中非唯一）
    CREATURE_STATIC_FLAG_3_SKIN_WITH_ENGINEERING          = 0x00000010,  ///< 用工程学剥皮（对应CREATURE_TYPE_FLAG_SKIN_WITH_ENGINEERING）
    CREATURE_STATIC_FLAG_3_NO_AGGRO_ON_LEASH              = 0x00000020,  ///< 脱离时无仇恨
    CREATURE_STATIC_FLAG_3_NO_FRIENDLY_AREA_AURAS         = 0x00000040,  ///< 无友方区域光环
    CREATURE_STATIC_FLAG_3_EXTENDED_CORPSE_DURATION       = 0x00000080,  ///< 延长尸体持续时间
    CREATURE_STATIC_FLAG_3_CANNOT_SWIM                    = 0x00000100,  ///< 不能游泳（对应UNIT_FLAG_CANNOT_SWIM）
    CREATURE_STATIC_FLAG_3_TAMEABLE_EXOTIC                = 0x00000200,  ///< 可驯服珍奇宠物（对应CREATURE_TYPE_FLAG_TAMEABLE_EXOTIC）
    CREATURE_STATIC_FLAG_3_GIGANTIC_AOI                   = 0x00000400,  ///< 巨型感兴趣区域（MoP后对应UnitFlags2 0x400000）
    CREATURE_STATIC_FLAG_3_INFINITE_AOI                   = 0x00000800,  ///< 无限感兴趣区域（MoP后对应UnitFlags2 0x40000000）
    CREATURE_STATIC_FLAG_3_CANNOT_PENETRATE_WATER         = 0x00001000,  ///< 不能穿透水面（水上行走）
    CREATURE_STATIC_FLAG_3_NO_NAME_PLATE                  = 0x00002000,  ///< 无姓名板（对应CREATURE_TYPE_FLAG_NO_NAME_PLATE）
    CREATURE_STATIC_FLAG_3_CHECKS_LIQUIDS                 = 0x00004000,  ///< 检查液体
    CREATURE_STATIC_FLAG_3_NO_THREAT_FEEDBACK             = 0x00008000,  ///< 无威胁反馈
    CREATURE_STATIC_FLAG_3_USE_MODEL_COLLISION_SIZE       = 0x00010000,  ///< 使用模型碰撞大小（对应CREATURE_TYPE_FLAG_USE_MODEL_COLLISION_SIZE）
    CREATURE_STATIC_FLAG_3_ATTACKER_IGNORES_FACING        = 0x00020000,  ///< 攻击者忽略朝向（3.3.5仅火箭推进弹头使用）
    CREATURE_STATIC_FLAG_3_ALLOW_INTERACTION_WHILE_IN_COMBAT = 0x00040000,  ///< 战斗中允许交互（对应CREATURE_TYPE_FLAG_ALLOW_INTERACTION_WHILE_IN_COMBAT）
    CREATURE_STATIC_FLAG_3_SPELL_CLICK_FOR_PARTY_ONLY     = 0x00080000,  ///< 法术点击仅限队伍
    CREATURE_STATIC_FLAG_3_FACTION_LEADER                 = 0x00100000,  ///< 阵营领袖
    CREATURE_STATIC_FLAG_3_IMMUNE_TO_PLAYER_BUFFS         = 0x00200000,  ///< 对玩家增益免疫
    CREATURE_STATIC_FLAG_3_COLLIDE_WITH_MISSILES          = 0x00400000,  ///< 与导弹碰撞（对应CREATURE_TYPE_FLAG_COLLIDE_WITH_MISSILES）
    CREATURE_STATIC_FLAG_3_CAN_BE_MULTITAPPED             = 0x00800000,  ///< 可被多人攻击（原描述：不锁定，记入威胁列表）
    CREATURE_STATIC_FLAG_3_DO_NOT_PLAY_MOUNTED_ANIMATIONS = 0x01000000,  ///< 不播放骑乘动画（对应CREATURE_TYPE_FLAG_DO_NOT_PLAY_MOUNTED_ANIMATIONS）
    CREATURE_STATIC_FLAG_3_CANNOT_TURN                    = 0x02000000,  ///< 不能转身（对应UNIT_FLAG2_CANNOT_TURN）
    CREATURE_STATIC_FLAG_3_ENEMY_CHECK_IGNORES_LOS        = 0x04000000,  ///< 敌人检查忽略视线
    CREATURE_STATIC_FLAG_3_FOREVER_CORPSE_DURATION        = 0x08000000,  ///< 永久尸体持续时间（7天）
    CREATURE_STATIC_FLAG_3_PETS_ATTACK_WITH_3D_PATHING    = 0x10000000,  ///< 宠物使用3D路径攻击（如科隆加恩）
    CREATURE_STATIC_FLAG_3_LINK_ALL                       = 0x20000000,  ///< 链接所有（对应CREATURE_TYPE_FLAG_LINK_ALL）
    CREATURE_STATIC_FLAG_3_AI_CAN_AUTO_TAKEOFF_IN_COMBAT  = 0x40000000,  ///< AI可在战斗中自动起飞
    CREATURE_STATIC_FLAG_3_AI_CAN_AUTO_LAND_IN_COMBAT     = 0x80000000   ///< AI可在战斗中自动降落
};

/**
 * @brief 生物静态标志枚举（第四组）
 *
 * 定义生物的更多特殊静态属性标志，主要用于特定的游戏机制和Boss战斗。
 */
enum CreatureStaticFlags4
{
    CREATURE_STATIC_FLAG_4_NO_BIRTH_ANIM                       = 0x00000001,  ///< 无出生动画（SMSG_UPDATE_OBJECT的"NoBirthAnim"）
    CREATURE_STATIC_FLAG_4_TREAT_AS_PLAYER_FOR_DIMINISHING_RETURNS = 0x00000002,  ///< 对递减效果视为玩家（主要用于ToC冠军）
    CREATURE_STATIC_FLAG_4_TREAT_AS_PLAYER_FOR_PVP_DEBUFF_DURATION = 0x00000004,  ///< 对PvP减益持续时间视为玩家（主要用于ToC冠军）
    CREATURE_STATIC_FLAG_4_INTERACT_ONLY_WITH_CREATOR          = 0x00000008,  ///< 仅与创建者交互（对应CREATURE_TYPE_FLAG_INTERACT_ONLY_WITH_CREATOR）
    CREATURE_STATIC_FLAG_4_DO_NOT_PLAY_UNIT_EVENT_SOUNDS       = 0x00000010,  ///< 不播放单位事件音效（对应CREATURE_TYPE_FLAG_DO_NOT_PLAY_UNIT_EVENT_SOUNDS）
    CREATURE_STATIC_FLAG_4_HAS_NO_SHADOW_BLOB                  = 0x00000020,  ///< 无阴影blob（对应CREATURE_TYPE_FLAG_HAS_NO_SHADOW_BLOB）
    CREATURE_STATIC_FLAG_4_DEALS_TRIPLE_DAMAGE_TO_PC_CONTROLLED_PETS = 0x00000040,  ///< 对玩家控制的宠物造成三倍伤害
    CREATURE_STATIC_FLAG_4_NO_NPC_DAMAGE_BELOW_85PTC           = 0x00000080,  ///< 低于85%生命值NPC不造成伤害
    CREATURE_STATIC_FLAG_4_OBEYS_TAUNT_DIMINISHING_RETURNS     = 0x00000100,  ///< 遵循嘲讽递减（对应CREATURE_FLAG_EXTRA_OBEYS_TAUNT_DIMINISHING_RETURNS）
    CREATURE_STATIC_FLAG_4_NO_MELEE_APPROACH                   = 0x00000200,  ///< 无近战接近
    CREATURE_STATIC_FLAG_4_UPDATE_CREATURE_RECORD_WHEN_INSTANCE_CHANGES_DIFFICULTY = 0x00000400,  ///< 副本改变难度时更新生物记录（仅Snobold Vassal使用）
    CREATURE_STATIC_FLAG_4_CANNOT_DAZE                         = 0x00000800,  ///< 不能造成眩晕（战斗眩晕）
    CREATURE_STATIC_FLAG_4_FLAT_HONOR_AWARD                    = 0x00001000,  ///< 固定荣誉奖励
    CREATURE_STATIC_FLAG_4_IGNORE_LOS_WHEN_CASTING_ON_ME       = 0x00002000,  ///< 对我施法时忽略视线（3.3.5仅冰墓使用）
    CREATURE_STATIC_FLAG_4_GIVE_QUEST_KILL_CREDIT_WHILE_OFFLINE = 0x00004000,  ///< 离线时给予任务击杀荣誉
    CREATURE_STATIC_FLAG_4_TREAT_AS_RAID_UNIT_FOR_HELPFUL_SPELLS = 0x00008000,  ///< 对有益法术视为团队单位（对应CREATURE_TYPE_FLAG_TREAT_AS_RAID_UNIT，瓦莉瑟瑞亚·梦行者使用）
    CREATURE_STATIC_FLAG_4_DONT_REPOSITION_IF_MELEE_TARGET_IS_TOO_CLOSE = 0x00010000,  ///< 近战目标太近时不重新定位
    CREATURE_STATIC_FLAG_4_PET_OR_GUARDIAN_AI_DONT_GO_BEHIND_TARGET = 0x00020000,  ///< 宠物或守护者AI不绕到目标背后
    CREATURE_STATIC_FLAG_4_5_MINUTE_LOOT_ROLL_TIMER            = 0x00040000,  ///< 5分钟战利品分配计时器（巫妖王使用）
    CREATURE_STATIC_FLAG_4_FORCE_GOSSIP                        = 0x00080000,  ///< 强制对话（对应CREATURE_TYPE_FLAG_FORCE_GOSSIP）
    CREATURE_STATIC_FLAG_4_DONT_REPOSITION_WITH_FRIENDS_IN_COMBAT = 0x00100000,  ///< 战斗中有友方时不重新定位
    CREATURE_STATIC_FLAG_4_DO_NOT_SHEATHE                      = 0x00200000,  ///< 不收起武器（对应CREATURE_TYPE_FLAG_DO_NOT_SHEATHE）
    CREATURE_STATIC_FLAG_4_IGNORE_SPELL_MIN_RANGE_RESTRICTIONS = 0x00400000,  ///< 忽略法术最小射程限制（对应UnitFlags2 0x8000000）
    CREATURE_STATIC_FLAG_4_SUPPRESS_INSTANCE_WIDE_RELEASE_IN_COMBAT = 0x00800000,  ///< 抑制全副本战斗释放
    CREATURE_STATIC_FLAG_4_PREVENT_SWIM                        = 0x01000000,  ///< 阻止游泳（对应UnitFlags2 0x1000000）
    CREATURE_STATIC_FLAG_4_HIDE_IN_COMBAT_LOG                  = 0x02000000,  ///< 在战斗日志中隐藏（对应UnitFlags2 0x2000000）
    CREATURE_STATIC_FLAG_4_ALLOW_NPC_COMBAT_WHILE_UNINTERACTIBLE = 0x04000000,  ///< 不可交互时允许NPC战斗
    CREATURE_STATIC_FLAG_4_PREFER_NPCS_WHEN_SEARCHING_FOR_ENEMIES = 0x08000000,  ///< 搜索敌人时优先选择NPC
    CREATURE_STATIC_FLAG_4_ONLY_GENERATE_INITIAL_THREAT        = 0x10000000,  ///< 只生成初始威胁
    CREATURE_STATIC_FLAG_4_DO_NOT_TARGET_ON_INTERACTION        = 0x20000000,  ///< 交互时不改变目标（对应CREATURE_TYPE_FLAG_DO_NOT_TARGET_ON_INTERACTION）
    CREATURE_STATIC_FLAG_4_DO_NOT_RENDER_OBJECT_NAME           = 0x40000000,  ///< 不渲染对象名称（对应CREATURE_TYPE_FLAG_DO_NOT_RENDER_OBJECT_NAME）
    CREATURE_STATIC_FLAG_4_QUEST_BOSS                          = 0x80000000   ///< 任务Boss（对应CREATURE_TYPE_FLAG_QUEST_BOSS）
};

/**
 * @brief 生物额外标志枚举（服务器端专用）
 *
 * 这些标志仅在服务器端使用，不发送给客户端。
 * 用于控制生物的各种特殊行为和属性。
 */
// EnumUtils: DESCRIBE THIS
enum CreatureFlagsExtra : uint32
{
    CREATURE_FLAG_EXTRA_INSTANCE_BIND        = 0x00000001,  ///< 击杀后绑定副本进度（团队Boss）
    CREATURE_FLAG_EXTRA_CIVILIAN             = 0x00000002,  ///< 平民（不主动攻击，忽略阵营/声望敌对关系）
    CREATURE_FLAG_EXTRA_NO_PARRY             = 0x00000004,  ///< 不能招架
    CREATURE_FLAG_EXTRA_NO_PARRY_HASTEN      = 0x00000008,  ///< 招架后不能反击
    CREATURE_FLAG_EXTRA_NO_BLOCK             = 0x00000010,  ///< 不能格挡
    CREATURE_FLAG_EXTRA_NO_CRUSHING_BLOWS    = 0x00000020,  ///< 不能造成碾压攻击
    CREATURE_FLAG_EXTRA_NO_XP                = 0x00000040,  ///< 击杀不提供经验值
    CREATURE_FLAG_EXTRA_TRIGGER              = 0x00000080,  ///< 触发器生物（不可见，用于触发事件）
    CREATURE_FLAG_EXTRA_NO_TAUNT             = 0x00000100,  ///< 对嘲讽光环和"攻击我"效果免疫
    CREATURE_FLAG_EXTRA_NO_MOVE_FLAGS_UPDATE = 0x00000200,  ///< 不更新移动标志
    CREATURE_FLAG_EXTRA_GHOST_VISIBILITY     = 0x00000400,  ///< 仅对死亡玩家可见
    CREATURE_FLAG_EXTRA_USE_OFFHAND_ATTACK   = 0x00000800,  ///< 使用副手攻击
    CREATURE_FLAG_EXTRA_NO_SELL_VENDOR       = 0x00001000,  ///< 玩家不能向此商人出售物品
    CREATURE_FLAG_EXTRA_CANNOT_ENTER_COMBAT  = 0x00002000,  ///< 不允许进入战斗
    CREATURE_FLAG_EXTRA_WORLDEVENT           = 0x00004000,  ///< 世界事件生物标志
    CREATURE_FLAG_EXTRA_GUARD                = 0x00008000,  ///< 卫兵
    CREATURE_FLAG_EXTRA_IGNORE_FEIGN_DEATH   = 0x00010000,  ///< 忽略假死
    CREATURE_FLAG_EXTRA_NO_CRIT              = 0x00020000,  ///< 不能造成暴击
    CREATURE_FLAG_EXTRA_NO_SKILL_GAINS       = 0x00040000,  ///< 不会提升武器技能
    CREATURE_FLAG_EXTRA_OBEYS_TAUNT_DIMINISHING_RETURNS = 0x00080000,  ///< 嘲讽受递减效果影响
    CREATURE_FLAG_EXTRA_ALL_DIMINISH         = 0x00100000,  ///< 所有控制效果受递减影响（与玩家相同）
    CREATURE_FLAG_EXTRA_NO_PLAYER_DAMAGE_REQ = 0x00200000,  ///< 不需要玩家伤害即可获得击杀荣誉
    CREATURE_FLAG_EXTRA_UNUSED_22            = 0x00400000,  ///< 未使用
    CREATURE_FLAG_EXTRA_UNUSED_23            = 0x00800000,  ///< 未使用
    CREATURE_FLAG_EXTRA_UNUSED_24            = 0x01000000,  ///< 未使用
    CREATURE_FLAG_EXTRA_UNUSED_25            = 0x02000000,  ///< 未使用
    CREATURE_FLAG_EXTRA_UNUSED_26            = 0x04000000,  ///< 未使用
    CREATURE_FLAG_EXTRA_UNUSED_27            = 0x08000000,  ///< 未使用
    CREATURE_FLAG_EXTRA_DUNGEON_BOSS         = 0x10000000,  ///< 地下城Boss（动态设置，不要在数据库中添加）
    CREATURE_FLAG_EXTRA_IGNORE_PATHFINDING   = 0x20000000,  ///< 忽略寻路
    CREATURE_FLAG_EXTRA_IMMUNITY_KNOCKBACK   = 0x40000000,  ///< 免疫击退效果
    CREATURE_FLAG_EXTRA_UNUSED_31            = 0x80000000,  ///< 未使用

    // 掩码组合
    CREATURE_FLAG_EXTRA_UNUSED               = (CREATURE_FLAG_EXTRA_UNUSED_22 |
                                                CREATURE_FLAG_EXTRA_UNUSED_23 | CREATURE_FLAG_EXTRA_UNUSED_24 | CREATURE_FLAG_EXTRA_UNUSED_25 |
                                                CREATURE_FLAG_EXTRA_UNUSED_26 | CREATURE_FLAG_EXTRA_UNUSED_27 | CREATURE_FLAG_EXTRA_UNUSED_31), // SKIP

    CREATURE_FLAG_EXTRA_DB_ALLOWED           = (0xFFFFFFFF & ~(CREATURE_FLAG_EXTRA_UNUSED | CREATURE_FLAG_EXTRA_DUNGEON_BOSS)) // SKIP
};

/**
 * @brief 生物地面移动类型枚举
 *
 * 定义生物在地面上的移动方式
 */
enum class CreatureGroundMovementType : uint8
{
    None,   ///< 无地面移动
    Run,    ///< 奔跑
    Hover,  ///< 悬浮移动

    Max     ///< 枚举最大值
};

/**
 * @brief 生物飞行移动类型枚举
 *
 * 定义生物的飞行能力
 */
enum class CreatureFlightMovementType : uint8
{
    None,           ///< 无飞行能力
    DisableGravity, ///< 禁用重力（悬浮）
    CanFly,         ///< 可飞行

    Max             ///< 枚举最大值
};

/**
 * @brief 生物追逐移动类型枚举
 *
 * 定义生物追逐目标时的移动方式
 */
enum class CreatureChaseMovementType : uint8
{
    Run,        ///< 奔跑追逐
    CanWalk,    ///< 可以行走追逐
    AlwaysWalk, ///< 总是行走追逐

    Max         ///< 枚举最大值
};

/**
 * @brief 生物随机移动类型枚举
 *
 * 定义生物随机游荡时的移动方式
 */
enum class CreatureRandomMovementType : uint8
{
    Walk,      ///< 行走
    CanRun,    ///< 可以奔跑
    AlwaysRun, ///< 总是奔跑

    Max        ///< 枚举最大值
};

/**
 * @brief 生物移动数据结构体
 *
 * 存储生物的移动相关配置，包括地面、飞行、游泳等移动类型和限制。
 * 数据来源：creature_template_movement表
 */
struct TC_GAME_API CreatureMovementData
{
    CreatureMovementData();  ///< 构造函数，初始化默认值

    CreatureGroundMovementType Ground;  ///< 地面移动类型
    CreatureFlightMovementType Flight;  ///< 飞行移动类型
    bool Swim;                          ///< 是否可游泳
    bool Rooted;                        ///< 是否固定不动
    CreatureChaseMovementType Chase;    ///< 追逐移动类型
    CreatureRandomMovementType Random;  ///< 随机移动类型
    uint32 InteractionPauseTimer;       ///< 交互暂停计时器（毫秒）

    /**
     * @brief 检查是否允许地面移动
     * @return 允许返回true，否则返回false
     */
    bool IsGroundAllowed() const { return Ground != CreatureGroundMovementType::None; }

    /**
     * @brief 检查是否允许游泳
     * @return 允许返回true，否则返回false
     */
    bool IsSwimAllowed() const { return Swim; }

    /**
     * @brief 检查是否允许飞行
     * @return 允许返回true，否则返回false
     */
    bool IsFlightAllowed() const { return Flight != CreatureFlightMovementType::None; }

    /**
     * @brief 检查是否固定不动
     * @return 固定返回true，否则返回false
     */
    bool IsRooted() const { return Rooted; }

    /**
     * @brief 获取追逐移动类型
     * @return 追逐移动类型
     */
    CreatureChaseMovementType GetChase() const { return Chase; }

    /**
     * @brief 获取随机移动类型
     * @return 随机移动类型
     */
    CreatureRandomMovementType GetRandom() const { return Random; }

    /**
     * @brief 获取交互暂停计时器
     * @return 暂停时间（毫秒）
     */
    uint32 GetInteractionPauseTimer() const { return InteractionPauseTimer; }

    /**
     * @brief 转换为字符串表示
     * @return 移动数据的字符串描述
     */
    std::string ToString() const;
};

/// 生物生命值回复间隔（2秒）
static const uint32 CREATURE_REGEN_INTERVAL = 2 * IN_MILLISECONDS;
/// 宠物集中值回复间隔（4秒）
static const uint32 PET_FOCUS_REGEN_INTERVAL = 4 * IN_MILLISECONDS;
/// 生物无法寻路时的脱离时间（5秒）
static const uint32 CREATURE_NOPATH_EVADE_TIME = 5 * IN_MILLISECONDS;

/// 最大击杀荣誉数量（一个生物可以给予多个击杀荣誉）
static const uint8 MAX_KILL_CREDIT = 2;
/// 最大模型ID数量
static const uint32 MAX_CREATURE_MODELS = 4;
/// 最大任务物品数量
static const uint32 MAX_CREATURE_QUEST_ITEMS = 6;
/// 最大法术数量
static const uint32 MAX_CREATURE_SPELLS = 8;

/**
 * @brief 生物模板数据结构体
 *
 * 从creature_template表加载的生物静态数据模板。
 * 定义了生物的基本属性、模型、技能、战利品等信息。
 * 所有同类型的生物实例共享同一个模板。
 */
// from `creature_template` table
struct TC_GAME_API CreatureTemplate
{
    uint32  Entry;                              ///< 生物条目ID（主键）
    uint32  DifficultyEntry[MAX_DIFFICULTY - 1]; ///< 各难度下的条目ID
    uint32  KillCredit[MAX_KILL_CREDIT];        ///< 击杀荣誉ID列表
    uint32  Modelid1;                           ///< 模型ID 1
    uint32  Modelid2;                           ///< 模型ID 2
    uint32  Modelid3;                           ///< 模型ID 3
    uint32  Modelid4;                           ///< 模型ID 4（最多4个模型）
    std::string  Name;                          ///< 名称
    std::string  Title;                         ///< 称号/副标题
    std::string  IconName;                      ///< 图标名称
    uint32  GossipMenuId;                       ///< 对话菜单ID
    uint8   minlevel;                           ///< 最小等级
    uint8   maxlevel;                           ///< 最大等级
    uint32  expansion;                          ///< 资料片ID
    uint32  faction;                            ///< 阵营模板ID
    uint32  npcflag;                            ///< NPC功能标志（商人、任务等）
    float   speed_walk;                         ///< 行走速度
    float   speed_run;                          ///< 奔跑速度
    float   scale;                              ///< 缩放比例
    uint32  rank;                               ///< 等级（普通/精英/稀有等）
    uint32  dmgschool;                          ///< 伤害学校（法术伤害类型）
    uint32  BaseAttackTime;                     ///< 基础攻击间隔（毫秒）
    uint32  RangeAttackTime;                    ///< 远程攻击间隔（毫秒）
    float   BaseVariance;                       ///< 基础伤害变化量
    float   RangeVariance;                      ///< 远程伤害变化量
    uint32  unit_class;                         ///< 职业类型（枚举Classes，生物只有4种已知职业）
    uint32  unit_flags;                         ///< 单位标志（枚举UnitFlags掩码值）
    uint32  unit_flags2;                        ///< 单位标志2（枚举UnitFlags2掩码值）
    uint32  dynamicflags;                       ///< 动态标志
    CreatureFamily  family;                     ///< 生物家族（枚举CreatureFamily值，可选）
    uint32  type;                               ///< 生物类型（枚举CreatureType值）
    uint32  type_flags;                         ///< 生物类型标志（枚举CreatureTypeFlags掩码值）
    uint32  lootid;                             ///< 战利品模板ID
    uint32  pickpocketLootId;                   ///< 偷窃战利品模板ID
    uint32  SkinLootId;                         ///< 剥皮战利品模板ID
    int32   resistance[MAX_SPELL_SCHOOL];       ///< 各魔法抗性值
    uint32  spells[MAX_CREATURE_SPELLS];        ///< 技能列表（最多8个）
    uint32  PetSpellDataId;                     ///< 宠物技能数据ID
    uint32  VehicleId;                          ///< 载具ID
    uint32  mingold;                            ///< 最小掉落金币
    uint32  maxgold;                            ///< 最大掉落金币
    std::string AIName;                         ///< AI名称
    uint32  MovementType;                       ///< 移动类型
    CreatureMovementData Movement;              ///< 移动数据
    float   HoverHeight;                        ///< 悬浮高度
    float   ModHealth;                          ///< 生命值倍率
    float   ModMana;                            ///< 法力值倍率
    float   ModArmor;                           ///< 护甲倍率
    float   ModDamage;                          ///< 伤害倍率
    float   ModExperience;                      ///< 经验值倍率
    bool    RacialLeader;                       ///< 是否为种族领袖
    uint32  movementId;                         ///< 移动ID
    bool    RegenHealth;                        ///< 是否回复生命值
    uint32  MechanicImmuneMask;                 ///< 机制免疫掩码
    uint32  SpellSchoolImmuneMask;              ///< 法术学校免疫掩码
    uint32  flags_extra;                        ///< 额外标志（服务器端）
    uint32  ScriptID;                           ///< 脚本ID
    WorldPacket QueryData[TOTAL_LOCALES];       ///< 各语言的查询数据缓存

    /**
     * @brief 获取随机有效模型ID
     * @return 随机一个有效的模型ID
     */
    uint32  GetRandomValidModelId() const;

    /**
     * @brief 获取第一个有效模型ID
     * @return 第一个有效的模型ID
     */
    uint32  GetFirstValidModelId() const;

    /**
     * @brief 获取第一个不可见模型ID
     * @return 第一个不可见的模型ID，无则返回0
     */
    uint32  GetFirstInvisibleModel() const;

    /**
     * @brief 获取第一个可见模型ID
     * @return 第一个可见的模型ID，无则返回0
     */
    uint32  GetFirstVisibleModel() const;

    /**
     * @brief 获取剥皮所需技能
     * @return 技能类型（草药学、采矿、工程学或剥皮）
     *
     * 根据生物类型标志判断剥皮所需的专业技能
     */
    SkillType GetRequiredLootSkill() const
    {
        if (type_flags & CREATURE_TYPE_FLAG_SKIN_WITH_HERBALISM)
            return SKILL_HERBALISM;  // 草药学
        else if (type_flags & CREATURE_TYPE_FLAG_SKIN_WITH_MINING)
            return SKILL_MINING;     // 采矿
        else if (type_flags & CREATURE_TYPE_FLAG_SKIN_WITH_ENGINEERING)
            return SKILL_ENGINEERING; // 工程学
        else
            return SKILL_SKINNING;   // 剥皮（默认情况）
    }

    /**
     * @brief 检查是否为珍奇宠物
     * @return 是珍奇宠物返回true，否则返回false
     */
    bool IsExotic() const
    {
        return (type_flags & CREATURE_TYPE_FLAG_TAMEABLE_EXOTIC) != 0;
    }

    /**
     * @brief 检查是否可驯服
     * @param canTameExotic 是否可以驯服珍奇宠物
     * @return 可驯服返回true，否则返回false
     *
     * 检查条件：
     * 1. 类型必须是野兽
     * 2. 必须有家族
     * 3. 必须有可驯服标志
     * 4. 如果是珍奇宠物，需要珍奇驯服能力
     */
    bool IsTameable(bool canTameExotic) const
    {
        // 检查类型、家族和可驯服标志
        if (type != CREATURE_TYPE_BEAST || family == CREATURE_FAMILY_NONE || (type_flags & CREATURE_TYPE_FLAG_TAMEABLE) == 0)
            return false;

        // 如果可以驯服珍奇宠物，则可以驯服任何可驯服的生物
        return canTameExotic || !IsExotic();
    }

    /**
     * @brief 初始化查询数据
     *
     * 为所有语言构建生物查询数据包缓存
     */
    void InitializeQueryData();

    /**
     * @brief 构建查询数据包
     * @param loc 语言常量
     * @return 构建的数据包
     */
    WorldPacket BuildQueryData(LocaleConstant loc) const;
};

#pragma pack(push, 1)

/**
 * @brief 生物基础属性结构体
 *
 * 定义生物的基础数值（HP、法力、护甲、攻击强度等）。
 * 用于计算生物的最终属性值。
 * 数据来源：creature_classlevelstats表
 *
 * 计算公式：
 * - 最终HP = BaseHealth[资料片] * ModHealth
 * - 最终法力 = BaseMana * ModMana
 * - 最终护甲 = BaseArmor * ModArmor
 */
struct TC_GAME_API CreatureBaseStats
{
    uint32 BaseHealth[MAX_EXPANSIONS];      ///< 各资料片的基础生命值
    uint32 BaseMana;                        ///< 基础法力值
    uint32 BaseArmor;                       ///< 基础护甲值
    uint32 AttackPower;                     ///< 攻击强度
    uint32 RangedAttackPower;               ///< 远程攻击强度
    float BaseDamage[MAX_EXPANSIONS];       ///< 各资料片的基础伤害

    // 辅助函数

    /**
     * @brief 生成最终生命值
     * @param info 生物模板指针
     * @return 计算后的生命值
     *
     * 计算公式：BaseHealth[资料片] * ModHealth
     */
    uint32 GenerateHealth(CreatureTemplate const* info) const
    {
        return uint32(ceil(BaseHealth[info->expansion] * info->ModHealth));
    }

    /**
     * @brief 生成最终法力值
     * @param info 生物模板指针
     * @return 计算后的法力值
     *
     * 计算公式：BaseMana * ModMana
     * 法力值可以为0
     */
    uint32 GenerateMana(CreatureTemplate const* info) const
    {
        // 法力值可以为0
        if (!BaseMana)
            return 0;

        return uint32(ceil(BaseMana * info->ModMana));
    }

    /**
     * @brief 生成最终护甲值
     * @param info 生物模板指针
     * @return 计算后的护甲值
     *
     * 计算公式：BaseArmor * ModArmor
     */
    uint32 GenerateArmor(CreatureTemplate const* info) const
    {
        return uint32(ceil(BaseArmor * info->ModArmor));
    }

    /**
     * @brief 生成基础伤害
     * @param info 生物模板指针
     * @return 基础伤害值
     *
     * 直接返回对应资料片的基础伤害值
     */
    float GenerateBaseDamage(CreatureTemplate const* info) const
    {
        return BaseDamage[info->expansion];
    }

    /**
     * @brief 获取基础属性
     * @param level 等级
     * @param unitClass 职业
     * @return 基础属性结构体指针
     *
     * 从全局缓存中获取指定等级和职业的基础属性
     */
    static CreatureBaseStats const* GetBaseStats(uint8 level, uint8 unitClass);
};

/**
 * @brief 生物本地化数据结构体
 *
 * 存储生物名称和称号的多语言翻译数据
 */
struct CreatureLocale
{
    std::vector<std::string> Name;   ///< 名称翻译列表
    std::vector<std::string> Title;  ///< 称号翻译列表
};

/**
 * @brief 装备信息结构体
 *
 * 定义生物的装备物品ID列表
 */
struct EquipmentInfo
{
    uint32  ItemEntry[MAX_EQUIPMENT_ITEMS];  ///< 物品条目ID数组
};

/**
 * @brief 生物实例数据结构体
 *
 * 从creature表加载的生物实例数据，继承自SpawnData。
 * 包含生物在世界中的具体状态和属性。
 * 每个生成的生物都有一个对应的数据实例。
 */
// from `creature` table
struct CreatureData : public SpawnData
{
    CreatureData() : SpawnData(SPAWN_TYPE_CREATURE) { }
    uint32 displayid = 0;           ///< 显示模型ID（覆盖模板模型）
    int8 equipmentId = 0;           ///< 装备模板ID
    float wander_distance = 0.0f;   ///< 游荡距离
    uint32 currentwaypoint = 0;     ///< 当前路径点ID
    uint32 curhealth = 0;           ///< 当前生命值
    uint32 curmana = 0;             ///< 当前法力值
    uint8 movementType = 0;         ///< 移动类型
    uint32 npcflag = 0;             ///< NPC功能标志（覆盖模板标志）
    uint32 unit_flags = 0;          ///< 单位标志（覆盖模板标志）
    uint32 dynamicflags = 0;        ///< 动态标志
};

/**
 * @brief 生物模型信息结构体
 *
 * 存储生物模型的碰撞和视觉相关信息
 */
struct CreatureModelInfo
{
    float bounding_radius;      ///< 边界半径（碰撞检测用）
    float combat_reach;         ///< 战斗触及距离
    uint8 gender;               ///< 性别
    uint32 modelid_other_gender; ///< 异性模型ID
    bool is_trigger;            ///< 是否为触发器模型
};

/**
 * @brief 栖息地类型枚举值
 *
 * 定义生物可以存在的环境类型，可以组合使用
 */
enum InhabitTypeValues
{
    INHABIT_GROUND = 1,  ///< 陆地
    INHABIT_WATER  = 2,  ///< 水中
    INHABIT_AIR    = 4,  ///< 空中
    INHABIT_ROOT   = 8,  ///< 固定（植物等）
    INHABIT_ANYWHERE = INHABIT_GROUND | INHABIT_WATER | INHABIT_AIR | INHABIT_ROOT  ///< 任意位置
};

#pragma pack(pop)

/**
 * @brief 生物附加数据结构体
 *
 * 从creature_addon表加载的生物附加数据。
 * 用于为特定生物实例添加额外的状态和效果。
 */
// `creature_addon` table
struct CreatureAddon
{
    uint32 path_id;          ///< 移动路径ID
    uint32 mount;            ///< 坐骑模型ID
    uint8 standState;        ///< 站立状态
    uint8 animTier;          ///< 动画层级
    uint8 sheathState;       ///< 武器收纳状态
    uint8 pvpFlags;          ///< PvP标志
    uint8 visFlags;          ///< 可见性标志
    uint32 emote;            ///< 表情ID
    std::vector<uint32> auras;  ///< 光环法术ID列表
    VisibilityDistanceType visibilityDistanceType;  ///< 可见距离类型
};

// ==================== 商人系统 ====================

/**
 * @brief 商人售卖物品结构体
 *
 * 定义商人售卖的单个物品信息
 */
struct VendorItem
{
    /**
     * @brief 构造函数
     * @param _item 物品ID
     * @param _maxcount 最大库存数量
     * @param _incrtime 库存恢复时间
     * @param _ExtendedCost 扩展成本ID
     */
    VendorItem(uint32 _item, int32 _maxcount, uint32 _incrtime, uint32 _ExtendedCost)
        : item(_item), maxcount(_maxcount), incrtime(_incrtime), ExtendedCost(_ExtendedCost) { }

    uint32 item;        ///< 物品条目ID
    uint32 maxcount;    ///< 最大库存数量（0表示无限）
    uint32 incrtime;    ///< 库存恢复时间（秒），maxcount!=0时有效
    uint32 ExtendedCost;///< 扩展成本ID（特殊货币成本）

    /**
     * @brief 检查购买是否需要金币
     * @param pProto 物品模板指针
     * @return 需要金币返回true，否则返回false
     *
     * 即使有扩展成本，也可能需要金币
     */
    bool IsGoldRequired(ItemTemplate const* pProto) const;
};

/**
 * @brief 商人售卖物品数据结构体
 *
 * 管理商人售卖的所有物品列表
 */
struct VendorItemData
{
    std::vector<VendorItem> m_items;  ///< 售卖物品列表

    /**
     * @brief 获取指定槽位的物品
     * @param slot 槽位索引
     * @return 物品指针，槽位无效返回nullptr
     */
    VendorItem const* GetItem(uint32 slot) const
    {
        if (slot >= m_items.size())
            return nullptr;

        return &m_items[slot];
    }

    /**
     * @brief 检查售卖列表是否为空
     * @return 为空返回true，否则返回false
     */
    bool Empty() const { return m_items.empty(); }

    /**
     * @brief 获取售卖物品数量
     * @return 物品数量
     */
    uint8 GetItemCount() const { return m_items.size(); }

    /**
     * @brief 添加售卖物品
     * @param item 物品ID
     * @param maxcount 最大库存数量
     * @param ptime 库存恢复时间
     * @param ExtendedCost 扩展成本ID
     */
    void AddItem(uint32 item, int32 maxcount, uint32 ptime, uint32 ExtendedCost)
    {
        m_items.emplace_back(item, maxcount, ptime, ExtendedCost);
    }

    /**
     * @brief 移除售卖物品
     * @param item_id 物品ID
     * @return 移除成功返回true，否则返回false
     */
    bool RemoveItem(uint32 item_id);

    /**
     * @brief 查找指定物品和成本的售卖项
     * @param item_id 物品ID
     * @param extendedCost 扩展成本ID
     * @return 找到返回物品指针，否则返回nullptr
     */
    VendorItem const* FindItemCostPair(uint32 item_id, uint32 extendedCost) const;

    /**
     * @brief 清空售卖列表
     */
    void Clear()
    {
        m_items.clear();
    }
};

#endif // CreatureData_h__
