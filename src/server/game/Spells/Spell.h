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
 * @file Spell.h
 * @brief 法术系统核心头文件
 *
 * 本文件定义了 TrinityCore 法术系统的核心类和数据结构。
 * 法术系统是 WoW 服务器中最复杂的系统之一，负责处理所有法术的施放、
 * 目标选择、效果应用、伤害计算等逻辑。
 *
 * 主要组成部分：
 * - Spell 类：法术执行的核心类，管理法术的整个生命周期
 * - SpellCastTargets：法术目标管理类
 * - SpellDestination：法术目标位置类
 * - SpellValue：法术数值容器
 * - 各种枚举类型：定义法术状态、标志、效果处理模式等
 *
 * 法术执行流程：
 * 1. 玩家/生物发起施法请求
 * 2. 创建 Spell 对象，初始化目标和施法者信息
 * 3. 检查施法条件（距离、法力、冷却、姿态等）
 * 4. 开始施法（对于有施法时间的法术）或立即施法
 * 5. 选择目标（包括隐式目标选择）
 * 6. 处理法术效果（伤害、治疗、光环、召唤等）
 * 7. 应用伤害/治疗到目标
 * 8. 触发相关的事件和光环
 * 9. 清理和完成
 *
 * @see Spell.cpp 实现文件
 * @see SpellInfo.h 法术信息定义
 */

#ifndef __SPELL_H
#define __SPELL_H

#include "ConditionMgr.h"
#include "DBCEnums.h"
#include "ObjectGuid.h"
#include "Position.h"
#include "SharedDefines.h"
#include "SpellDefines.h"
#include "UniqueTrackablePtr.h"
#include <memory>

namespace WorldPackets
{
    namespace Spells
    {
        struct SpellTargetData;
        struct SpellAmmo;
        struct SpellCastData;
    }
}

class Aura;
class AuraEffect;
class Corpse;
class DamageInfo;
class DynamicObject;
class DynObjAura;
class GameObject;
class Item;
class Object;
class PathGenerator;
class Player;
class SpellEffectInfo;
class SpellEvent;
class SpellImplicitTargetInfo;
class SpellInfo;
class SpellScript;
class Unit;
class UnitAura;
class WorldObject;
class WorldPacket;
struct SummonPropertiesEntry;
enum AuraType : uint32;
enum CurrentSpellTypes : uint8;
enum LootType : uint8;
enum SpellCastTargetFlags : uint32;
enum SpellTargetCheckTypes : uint8;
enum SpellTargetObjectTypes : uint8;
enum SpellValueMod : uint8;
enum TriggerCastFlags : uint32;
enum WeaponAttackType : uint8;

#define SPELL_CHANNEL_UPDATE_INTERVAL (1 * IN_MILLISECONDS)
#define MAX_SPELL_RANGE_TOLERANCE 3.0f
#define TRAJECTORY_MISSILE_SIZE 3.0f

// 施法标志位
enum SpellCastFlags
{
    CAST_FLAG_NONE               = 0x00000000,
    CAST_FLAG_PENDING            = 0x00000001,              // AOE战斗日志？
    CAST_FLAG_UNKNOWN_2          = 0x00000002,
    CAST_FLAG_UNKNOWN_3          = 0x00000004,
    CAST_FLAG_UNKNOWN_4          = 0x00000008,              // 忽略AOE视觉效果
    CAST_FLAG_UNKNOWN_5          = 0x00000010,
    CAST_FLAG_AMMO               = 0x00000020,              // 弹药视觉效果
    CAST_FLAG_UNKNOWN_7          = 0x00000040,
    CAST_FLAG_UNKNOWN_8          = 0x00000080,
    CAST_FLAG_UNKNOWN_9          = 0x00000100,
    CAST_FLAG_UNKNOWN_10         = 0x00000200,
    CAST_FLAG_UNKNOWN_11         = 0x00000400,
    CAST_FLAG_POWER_LEFT_SELF    = 0x00000800,              // 自身剩余能量
    CAST_FLAG_UNKNOWN_13         = 0x00001000,
    CAST_FLAG_UNKNOWN_14         = 0x00002000,
    CAST_FLAG_UNKNOWN_15         = 0x00004000,
    CAST_FLAG_UNKNOWN_16         = 0x00008000,
    CAST_FLAG_UNKNOWN_17         = 0x00010000,
    CAST_FLAG_ADJUST_MISSILE     = 0x00020000,              // 调整导弹
    CAST_FLAG_NO_GCD             = 0x00040000,              // 来自魅惑/召唤的法术无GCD（载具法术为例）
    CAST_FLAG_VISUAL_CHAIN       = 0x00080000,              // 视觉链
    CAST_FLAG_UNKNOWN_21         = 0x00100000,
    CAST_FLAG_RUNE_LIST          = 0x00200000,              // 符文列表
    CAST_FLAG_UNKNOWN_23         = 0x00400000,
    CAST_FLAG_UNKNOWN_24         = 0x00800000,
    CAST_FLAG_UNKNOWN_25         = 0x01000000,
    CAST_FLAG_UNKNOWN_26         = 0x02000000,
    CAST_FLAG_IMMUNITY           = 0x04000000,              // 免疫
    CAST_FLAG_UNKNOWN_28         = 0x08000000,
    CAST_FLAG_UNKNOWN_29         = 0x10000000,
    CAST_FLAG_UNKNOWN_30         = 0x20000000,
    CAST_FLAG_UNKNOWN_31         = 0x40000000,
    CAST_FLAG_UNKNOWN_32         = 0x80000000
};

// 法术范围标志
enum SpellRangeFlag
{
    SPELL_RANGE_DEFAULT             = 0,
    SPELL_RANGE_MELEE               = 1,     // 近战范围
    SPELL_RANGE_RANGED              = 2      // 猎人射程和远程武器
};

// 法术值结构体
struct SpellValue
{
    explicit  SpellValue(SpellInfo const* proto);
    int32     EffectBasePoints[MAX_SPELL_EFFECTS];  // 效果基础点数
    uint32    MaxAffectedTargets;                   // 最大影响目标数
    float     RadiusMod;                            // 半径修正
    uint8     AuraStackAmount;                      // 光环堆叠数量
    float     CriticalChance;                       // 暴击几率
};

// 法术状态枚举
enum SpellState
{
    SPELL_STATE_NULL      = 0,   // 空状态
    SPELL_STATE_PREPARING = 1,   // 准备中
    SPELL_STATE_CASTING   = 2,   // 施法中
    SPELL_STATE_FINISHED  = 3,   // 已完成
    SPELL_STATE_IDLE      = 4,   // 空闲
    SPELL_STATE_DELAYED   = 5    // 延迟
};

// 法术效果处理模式
enum SpellEffectHandleMode
{
    SPELL_EFFECT_HANDLE_LAUNCH,        // 发射
    SPELL_EFFECT_HANDLE_LAUNCH_TARGET, // 发射目标
    SPELL_EFFECT_HANDLE_HIT,           // 命中
    SPELL_EFFECT_HANDLE_HIT_TARGET     // 命中目标
};

typedef std::vector<std::pair<uint32, ObjectGuid>> DispelList;

static const uint32 SPELL_INTERRUPT_NONPLAYER = 32747;

// ============================================================================
// Spell 类 - 法术系统核心类
// 负责处理法术的施放、目标选择、效果应用等所有法术相关逻辑
// ============================================================================
class TC_GAME_API Spell
{
    friend class SpellScript;
    public:

        // =====================================================================
        // 法术效果处理函数
        // =====================================================================

        // 空效果
        void EffectNULL();
        // 未使用效果
        void EffectUnused();
        // 分心效果
        void EffectDistract();
        // 拉动效果
        void EffectPull();
        // 学派伤害效果
        void EffectSchoolDMG();
        // 环境伤害效果
        void EffectEnvironmentalDMG();
        // 即死效果
        void EffectInstaKill();
        // 模拟效果（自定义脚本效果）
        void EffectDummy();
        // 传送单位效果
        void EffectTeleportUnits();
        // 应用光环效果
        void EffectApplyAura();
        // 发送事件效果
        void EffectSendEvent();
        // 能量燃烧效果
        void EffectPowerBurn();
        // 能量吸取效果
        void EffectPowerDrain();
        // 治疗效果
        void EffectHeal();
        // 绑定效果（绑定炉石等）
        void EffectBind();
        // 生命吸取效果
        void EffectHealthLeech();
        // 任务完成效果
        void EffectQuestComplete();
        // 创建物品效果
        void EffectCreateItem();
        // 创建物品效果2
        void EffectCreateItem2();
        // 创建随机物品效果
        void EffectCreateRandomItem();
        // 持久区域光环效果
        void EffectPersistentAA();
        // 充能效果（恢复能量）
        void EffectEnergize();
        // 开锁效果
        void EffectOpenLock();
        // 召唤改变物品效果
        void EffectSummonChangeItem();
        // 熟练度效果
        void EffectProficiency();
        // 召唤类型效果
        void EffectSummonType();
        // 学习法术效果
        void EffectLearnSpell();
        // 驱散效果
        void EffectDispel();
        // 双持效果
        void EffectDualWield();
        // 偷窃效果
        void EffectPickPocket();
        // 添加远视效果
        void EffectAddFarsight();
        // 取消天赋训练效果
        void EffectUntrainTalents();
        // 治疗机械效果
        void EffectHealMechanical();
        // 跳跃效果
        void EffectJump();
        // 跳跃到目标位置效果
        void EffectJumpDest();
        // 后跳效果
        void EffectLeapBack();
        // 清除任务效果
        void EffectQuestClear();
        // 传送单位面向施法者效果
        void EffectTeleUnitsFaceCaster();
        // 学习技能效果
        void EffectLearnSkill();
        // 增加荣誉效果
        void EffectAddHonor();
        // 贸易技能效果
        void EffectTradeSkill();
        // 永久附魔物品效果
        void EffectEnchantItemPerm();
        // 临时附魔物品效果
        void EffectEnchantItemTmp();
        // 驯服生物效果
        void EffectTameCreature();
        // 召唤宠物效果
        void EffectSummonPet();
        // 学习宠物法术效果
        void EffectLearnPetSpell();
        // 武器伤害效果
        void EffectWeaponDmg();
        // 强制施法效果
        void EffectForceCast();
        // 触发法术效果
        void EffectTriggerSpell();
        // 触发导弹法术效果
        void EffectTriggerMissileSpell();
        // 威胁效果
        void EffectThreat();
        // 治疗满血效果
        void EffectHealMaxHealth();
        // 打断施法效果
        void EffectInterruptCast();
        // 召唤野生物体效果
        void EffectSummonObjectWild();
        // 脚本效果
        void EffectScriptEffect();
        // 圣域效果
        void EffectSanctuary();
        // 增加连击点效果
        void EffectAddComboPoints();
        // 决斗效果
        void EffectDuel();
        // 卡住效果（脱卡）
        void EffectStuck();
        // 召唤玩家效果
        void EffectSummonPlayer();
        // 激活物体效果
        void EffectActivateObject();
        // 应用雕文效果
        void EffectApplyGlyph();
        // 附魔手持物品效果
        void EffectEnchantHeldItem();
        // 召唤物体效果
        void EffectSummonObject();
        // 复活效果
        void EffectResurrect();
        // 招架效果
        void EffectParry();
        // 格挡效果
        void EffectBlock();
        // 跳跃效果
        void EffectLeap();
        // 传输效果
        void EffectTransmitted();
        // 分解效果
        void EffectDisEnchant();
        // 醉酒效果
        void EffectInebriate();
        // 喂养宠物效果
        void EffectFeedPet();
        // 解散宠物效果
        void EffectDismissPet();
        // 声望效果
        void EffectReputation();
        // 强制取消选择效果
        void EffectForceDeselect();
        // 自我复活效果
        void EffectSelfResurrect();
        // 剥皮效果
        void EffectSkinning();
        // 冲锋效果
        void EffectCharge();
        // 冲锋到目标位置效果
        void EffectChargeDest();
        // 探矿效果
        void EffectProspecting();
        // 研磨效果
        void EffectMilling();
        // 重命名宠物效果
        void EffectRenamePet();
        // 发送出租车效果
        void EffectSendTaxi();
        // 击退效果
        void EffectKnockBack();
        // 拉向效果
        void EffectPullTowards();
        // 拉向目标位置效果
        void EffectPullTowardsDest();
        // 驱散机制效果
        void EffectDispelMechanic();
        // 复活宠物效果
        void EffectResurrectPet();
        // 销毁所有图腾效果
        void EffectDestroyAllTotems();
        // 耐久度伤害效果
        void EffectDurabilityDamage();
        // 技能效果
        void EffectSkill();
        // 嘲讽效果
        void EffectTaunt();
        // 耐久度伤害百分比效果
        void EffectDurabilityDamagePCT();
        // 修改威胁百分比效果
        void EffectModifyThreatPercent();
        // 新复活效果
        void EffectResurrectNew();
        // 增加额外攻击效果
        void EffectAddExtraAttacks();
        // 灵魂治疗效果
        void EffectSpiritHeal();
        // 剥玩家尸体效果
        void EffectSkinPlayerCorpse();
        // 偷取有益Buff效果
        void EffectStealBeneficialBuff();
        // 遗忘专精效果
        void EffectUnlearnSpecialization();
        // 百分比治疗效果
        void EffectHealPct();
        // 百分比充能效果
        void EffectEnergizePct();
        // 触发召唤仪式效果
        void EffectTriggerRitualOfSummoning();
        // 召唤战友朋友效果
        void EffectSummonRaFFriend();
        // 个人击杀信用效果
        void EffectKillCreditPersonal();
        // 击杀信用效果
        void EffectKillCredit();
        // 任务失败效果
        void EffectQuestFail();
        // 任务开始效果
        void EffectQuestStart();
        // 重定向威胁效果
        void EffectRedirectThreat();
        // 游戏物体伤害效果
        void EffectGameObjectDamage();
        // 游戏物体修复效果
        void EffectGameObjectRepair();
        // 游戏物体设置破坏状态效果
        void EffectGameObjectSetDestructionState();
        // 激活符文效果
        void EffectActivateRune();
        // 创建驯服宠物效果
        void EffectCreateTamedPet();
        // 发现出租车效果
        void EffectDiscoverTaxi();
        // 泰坦之握效果
        void EffectTitanGrip();
        // 棱镜附魔物品效果
        void EffectEnchantItemPrismatic();
        // 播放音乐效果
        void EffectPlayMusic();
        // 专精数量效果
        void EffectSpecCount();
        // 激活专精效果
        void EffectActivateSpec();
        // 播放声音效果
        void EffectPlaySound();
        // 移除光环效果
        void EffectRemoveAura();
        // 施法按钮效果
        void EffectCastButtons();
        // 充能法力宝石效果
        void EffectRechargeManaGem();

        typedef std::unordered_set<Aura*> UsedSpellMods;

        // 构造函数 - 初始化法术对象
        Spell(WorldObject* caster, SpellInfo const* info, TriggerCastFlags triggerFlags, ObjectGuid originalCasterGUID = ObjectGuid::Empty);
        // 析构函数 - 清理法术资源
        ~Spell();

        // 初始化显式目标
        void InitExplicitTargets(SpellCastTargets const& targets);
        // 选择显式目标
        void SelectExplicitTargets();

        // 选择法术目标
        void SelectSpellTargets();
        void SelectEffectImplicitTargets(SpellEffectInfo const& spellEffectInfo, SpellImplicitTargetInfo const& targetType, uint32 effectMask);
        void SelectImplicitChannelTargets(SpellEffectInfo const& spellEffectInfo, SpellImplicitTargetInfo const& targetType, uint32 effMask);
        void SelectImplicitNearbyTargets(SpellEffectInfo const& spellEffectInfo, SpellImplicitTargetInfo const& targetType, uint32 effMask);
        void SelectImplicitConeTargets(SpellEffectInfo const& spellEffectInfo, SpellImplicitTargetInfo const& targetType, uint32 effMask);
        void SelectImplicitAreaTargets(SpellEffectInfo const& spellEffectInfo, SpellImplicitTargetInfo const& targetType, uint32 effMask);
        void SelectImplicitCasterDestTargets(SpellEffectInfo const& spellEffectInfo, SpellImplicitTargetInfo const& targetType);
        void SelectImplicitTargetDestTargets(SpellEffectInfo const& spellEffectInfo, SpellImplicitTargetInfo const& targetType);
        void SelectImplicitDestDestTargets(SpellEffectInfo const& spellEffectInfo, SpellImplicitTargetInfo const& targetType);
        void SelectImplicitCasterObjectTargets(SpellEffectInfo const& spellEffectInfo, SpellImplicitTargetInfo const& targetType, uint32 effMask);
        void SelectImplicitTargetObjectTargets(SpellEffectInfo const& spellEffectInfo, SpellImplicitTargetInfo const& targetType, uint32 effMask);
        void SelectImplicitChainTargets(SpellEffectInfo const& spellEffectInfo, SpellImplicitTargetInfo const& targetType, WorldObject* target, uint32 effMask);
        void SelectImplicitTrajTargets(SpellEffectInfo const& spellEffectInfo, SpellImplicitTargetInfo const& targetType);

        void SelectEffectTypeImplicitTargets(SpellEffectInfo const& spellEffectInfo);

        uint32 GetSearcherTypeMask(SpellTargetObjectTypes objType, ConditionContainer* condList);
        template<class SEARCHER> void SearchTargets(SEARCHER& searcher, uint32 containerMask, WorldObject* referer, Position const* pos, float radius);

        WorldObject* SearchNearbyTarget(float range, SpellTargetObjectTypes objectType, SpellTargetCheckTypes selectionType, ConditionContainer* condList = nullptr);
        void SearchAreaTargets(std::list<WorldObject*>& targets, float range, Position const* position, WorldObject* referer, SpellTargetObjectTypes objectType, SpellTargetCheckTypes selectionType, ConditionContainer* condList);
        void SearchChainTargets(std::list<WorldObject*>& targets, uint32 chainTargets, WorldObject* target, SpellTargetObjectTypes objectType, SpellTargetCheckTypes selectType, ConditionContainer* condList, bool isChainHeal);

        // 搜索法术焦点对象
        GameObject* SearchSpellFocus();

        // 准备施法 - 初始化施法过程
        SpellCastResult prepare(SpellCastTargets const& targets, AuraEffect const* triggeredByAura = nullptr);
        // 取消施法
        void cancel();
        // 更新施法状态
        void update(uint32 difftime);
        // 执行施法
        void cast(bool skipCheck = false);
        // 完成施法
        void finish(bool ok = true);
        // 消耗法力/能量
        void TakePower();
        // 消耗弹药
        void TakeAmmo();

        // 消耗符文能量
        void TakeRunePower(bool didHit);
        // 消耗施法材料
        void TakeReagents();
        // 消耗施法物品
        void TakeCastItem();

        // 检查施法条件
        SpellCastResult CheckCast(bool strict, uint32* param1 = nullptr, uint32* param2 = nullptr);
        // 检查宠物施法条件
        SpellCastResult CheckPetCast(Unit* target);

        // 处理器
        void handle_immediate();
        uint64 handle_delayed(uint64 t_offset);
        // 处理器辅助函数
        void _handle_immediate_phase();
        void _handle_finish_phase();

        // 检查物品条件
        SpellCastResult CheckItems(uint32* param1, uint32* param2) const;
        // 检查施法距离
        SpellCastResult CheckRange(bool strict) const;
        // 检查能量/法力是否足够
        SpellCastResult CheckPower() const;
        // 检查符文消耗
        SpellCastResult CheckRuneCost(uint32 runeCostID) const;
        // 检查施法者光环状态
        SpellCastResult CheckCasterAuras(uint32* param1) const;
        // 检查移动状态
        SpellCastResult CheckMovement() const;

        // 检查法术是否被光环效果取消
        bool CheckSpellCancelsAuraEffect(AuraType auraType, uint32* param1) const;
        // 检查法术是否被魅惑取消
        bool CheckSpellCancelsCharm(uint32* param1) const;
        // 检查法术是否被眩晕取消
        bool CheckSpellCancelsStun(uint32* param1) const;
        // 检查法术是否被沉默取消
        bool CheckSpellCancelsSilence(uint32* param1) const;
        // 检查法术是否被安抚取消
        bool CheckSpellCancelsPacify(uint32* param1) const;
        // 检查法术是否被恐惧取消
        bool CheckSpellCancelsFear(uint32* param1) const;
        // 检查法术是否被困惑取消
        bool CheckSpellCancelsConfuse(uint32* param1) const;

        // 计算伤害值
        int32 CalculateDamage(SpellEffectInfo const& spellEffectInfo) const;

        // 处理延迟法术
        void Delayed();
        // 处理延迟引导法术
        void DelayedChannel();
        // 获取法术状态
        uint32 getState() const { return m_spellState; }
        // 设置法术状态
        void setState(uint32 state) { m_spellState = state; }

        // 创建物品
        void DoCreateItem(uint32 itemId);
        // 更新法术施放数据目标
        void UpdateSpellCastDataTargets(WorldPackets::Spells::SpellCastData& data);
        // 更新法术施放数据弹药
        void UpdateSpellCastDataAmmo(WorldPackets::Spells::SpellAmmo& data);

        // 检查效果目标是否有效
        bool CheckEffectTarget(Unit const* target, SpellEffectInfo const& spellEffectInfo, Position const* losPosition) const;
        // 是否可以自动施法
        bool CanAutoCast(Unit* target);
        // 检查源位置
        void CheckSrc();
        // 检查目标位置
        void CheckDst();

        // 写入施法结果信息
        static void WriteCastResultInfo(WorldPacket& data, Player* caster, SpellInfo const* spellInfo, uint8 castCount, SpellCastResult result, SpellCustomErrors customError, uint32* param1 = nullptr, uint32* param2 = nullptr);
        // 发送施法结果（静态版本）
        static void SendCastResult(Player* caster, SpellInfo const* spellInfo, uint8 castCount, SpellCastResult result, SpellCustomErrors customError = SPELL_CUSTOM_ERROR_NONE, uint32* param1 = nullptr, uint32* param2 = nullptr);
        // 发送施法结果
        void SendCastResult(SpellCastResult result, uint32* param1 = nullptr, uint32* param2 = nullptr) const;
        // 发送宠物施法结果
        void SendPetCastResult(SpellCastResult result);
        // 发送坐骑结果
        void SendMountResult(MountResult result);
        // 发送法术开始消息
        void SendSpellStart();
        // 发送法术执行消息
        void SendSpellGo();
        // 发送法术冷却消息
        void SendSpellCooldown();
        // 发送执行日志
        void SendLogExecute();
        // 执行日志效果：吸取目标能量
        void ExecuteLogEffectTakeTargetPower(uint8 effIndex, Unit* target, uint32 powerType, uint32 powerTaken, float gainMultiplier);
        // 执行日志效果：额外攻击
        void ExecuteLogEffectExtraAttacks(uint8 effIndex, Unit* victim, uint32 attCount);
        // 执行日志效果：打断施法
        void ExecuteLogEffectInterruptCast(uint8 effIndex, Unit* victim, uint32 spellId);
        // 执行日志效果：耐久度伤害
        void ExecuteLogEffectDurabilityDamage(uint8 effIndex, Unit* victim, int32 itemId, int32 slot);
        // 执行日志效果：开锁
        void ExecuteLogEffectOpenLock(uint8 effIndex, Object* obj);
        // 执行日志效果：创建物品
        void ExecuteLogEffectCreateItem(uint8 effIndex, uint32 entry);
        // 执行日志效果：销毁物品
        void ExecuteLogEffectDestroyItem(uint8 effIndex, uint32 entry);
        // 执行日志效果：召唤对象
        void ExecuteLogEffectSummonObject(uint8 effIndex, WorldObject* obj);
        // 执行日志效果：取消召唤对象
        void ExecuteLogEffectUnsummonObject(uint8 effIndex, WorldObject* obj);
        // 执行日志效果：复活
        void ExecuteLogEffectResurrect(uint8 effIndex, Unit* target);
        // 发送施法被打断消息
        void SendInterrupted(uint8 result);
        // 发送引导更新消息
        void SendChannelUpdate(uint32 time);
        // 发送引导开始消息
        void SendChannelStart(uint32 duration);
        // 发送复活请求
        void SendResurrectRequest(Player* target);

        // 处理法术效果
        void HandleEffects(Unit* pUnitTarget, Item* pItemTarget, GameObject* pGoTarget, Corpse* pCorpseTarget, SpellEffectInfo const& spellEffectInfo, SpellEffectHandleMode mode);
        // 处理威胁值法术
        void HandleThreatSpells();

        // =====================================================================
        // 公共成员变量
        // =====================================================================

        // 法术信息（只读）
        SpellInfo const* const m_spellInfo;
        // 施法物品（如使用饰品）
        Item* m_CastItem;
        // 施法物品GUID
        ObjectGuid m_castItemGUID;
        // 施法物品Entry ID
        uint32 m_castItemEntry;
        // 施法计数
        uint8 m_cast_count;
        // 是否来自客户端
        bool m_fromClient;
        // 雕文索引
        uint32 m_glyphIndex;
        // 法术目标容器
        SpellCastTargets m_targets;

        // 添加连击点获取
        void AddComboPointGain(Unit* target, int8 amount)
        {
            if (target != m_comboTarget)
            {
                m_comboTarget = target;
                m_comboPointGain = amount;
            }
            else
                m_comboPointGain += amount;
        }
        // 连击目标
        Unit* m_comboTarget;
        // 连击点获取量
        int8 m_comboPointGain;
        // 自定义错误码
        SpellCustomErrors m_customError;

        // 已应用的光环修改器集合
        UsedSpellMods m_appliedMods;

        // 获取施法时间
        int32 GetCastTime() const { return m_casttime; }
        // 是否自动重复施法
        bool IsAutoRepeat() const { return m_autoRepeat; }
        // 设置自动重复
        void SetAutoRepeat(bool rep) { m_autoRepeat = rep; }
        // 重置计时器
        void ReSetTimer() { m_timer = m_casttime > 0 ? m_casttime : 0; }
        // 是否为触发法术
        bool IsTriggered() const;
        // 是否忽略冷却
        bool IsIgnoringCooldowns() const;
        // 是否禁用专注
        bool IsFocusDisabled() const;
        // 是否禁用触发
        bool IsProcDisabled() const;
        // 引导法术是否激活
        bool IsChannelActive() const;
        // 是否为自动动作重置法术
        bool IsAutoActionResetSpell() const;
        // 是否为有益法术
        bool IsPositive() const;

        // 是否由指定光环触发
        bool IsTriggeredByAura(SpellInfo const* auraSpellInfo) const { return (auraSpellInfo == m_triggeredByAuraSpell); }

        // 是否可删除
        bool IsDeletable() const { return !m_referencedFromCurrentSpell && !m_executedCurrently; }
        // 设置是否从当前法术引用
        void SetReferencedFromCurrent(bool yes) { m_referencedFromCurrentSpell = yes; }
        // 是否可被打断
        bool IsInterruptable() const { return !m_executedCurrently; }
        // 设置是否正在执行
        void SetExecutedCurrently(bool yes) {m_executedCurrently = yes;}
        // 获取延迟开始时间
        uint64 GetDelayStart() const { return m_delayStart; }
        // 设置延迟开始时间
        void SetDelayStart(uint64 m_time) { m_delayStart = m_time; }
        // 获取延迟时刻
        uint64 GetDelayMoment() const { return m_delayMoment; }
        // 计算目标的延迟时刻
        uint64 CalculateDelayMomentForDst() const;
        // 重新计算目标的延迟时刻
        void RecalculateDelayMomentForDst();
        // 获取符文状态
        uint8 GetRuneState() const { return m_runesState; }
        // 设置符文状态
        void SetRuneState(uint8 value) { m_runesState = value; }

        // 是否需要发送给客户端
        bool IsNeedSendToClient() const;

        // 获取当前法术容器类型
        CurrentSpellTypes GetCurrentContainer() const;

        // 获取施法者
        WorldObject* GetCaster() const { return m_caster; }
        // 获取原始施法者
        Unit* GetOriginalCaster() const { return m_originalCaster; }
        // 获取法术信息
        SpellInfo const* GetSpellInfo() const { return m_spellInfo; }
        // 获取能量消耗
        int32 GetPowerCost() const { return m_powerCost; }

        // 更新指针（必须在时间延迟后调用法术代码时使用）
        bool UpdatePointers();

        // 清理目标列表
        void CleanupTargetList();

        // 设置法术值修改
        void SetSpellValue(SpellValueMod mod, int32 value);

        // 指向自身容器的指针（如果适用）
        Spell** m_selfContainer;

        std::string GetDebugInfo() const;

        Trinity::unique_weak_ptr<Spell> GetWeakPtr() const;

        // 调用脚本抵抗吸收计算处理器
        void CallScriptOnResistAbsorbCalculateHandlers(DamageInfo const& damageInfo, uint32& resistAmount, int32& absorbAmount);

    protected:
        // 是否有公共冷却时间
        bool HasGlobalCooldown() const;
        // 触发公共冷却时间
        void TriggerGlobalCooldown();
        // 取消公共冷却时间
        void CancelGlobalCooldown();
        // 内部施法函数
        void _cast(bool skipCheck = false);

        // 发送战利品
        void SendLoot(ObjectGuid guid, LootType loottype);
        // 获取最小最大范围
        std::pair<float, float> GetMinMaxRange(bool strict) const;

        // =====================================================================
        // 保护成员变量
        // =====================================================================

        // 施法者（玩家或生物）
        WorldObject* const m_caster;

        // 法术值对象
        SpellValue* const m_spellValue;

        // 原始施法者GUID（真实施法源，如光环施法者等），用于法术目标选择
        // 例如：由受害者光环触发的范围伤害法术，伤害光环施法者的敌人
        ObjectGuid m_originalCasterGUID;
        // 原始施法者（缓存指针，在Spell::UpdatePointers()中更新）
        Unit* m_originalCaster;

        // =====================================================================
        // 法术数据
        // =====================================================================
        // 法术学派掩码（可为某些法术覆盖，如魔杖射击）
        SpellSchoolMask m_spellSchoolMask;
        // 武器攻击类型（用于基于武器的攻击）
        WeaponAttackType m_attackType;
        // 计算的法术能量消耗（仅在Spell::prepare中初始化）
        int32 m_powerCost;
        // 计算的施法时间（仅在Spell::prepare中初始化）
        int32 m_casttime;
        // 计算的引导法术持续时间（用于计算正确的推迟）
        int32 m_channeledDuration;
        // 是否可反射
        bool m_canReflect;
        // 是否自动重复
        bool m_autoRepeat;
        // 符文状态
        uint8 m_runesState;

        // 伤害延迟计数
        uint8 m_delayAtDamageCount;
        // 是否不可再延迟
        bool IsDelayableNoMore()
        {
            if (m_delayAtDamageCount >= 2)
                return true;

            ++m_delayAtDamageCount;
            return false;
        }

        // =====================================================================
        // 延迟法术系统
        // =====================================================================
        // 法术延迟开始时间（由事件处理器填充，零表示刚开始）
        uint64 m_delayStart;
        // 下次延迟调用的时刻（内部使用）
        uint64 m_delayMoment;
        // 是否已处理立即动作（仅用于延迟法术）
        bool m_immediateHandled;

        // =====================================================================
        // 这些变量同时用于延迟法术系统和修改的立即法术系统
        // =====================================================================
        // 标记为引用状态，防止删除和访问悬空指针
        bool m_referencedFromCurrentSpell;
        // 标记为正在执行，防止删除和访问悬空指针
        bool m_executedCurrently;
        // 是否需要连击点
        bool m_needComboPoints;
        // 应用乘数掩码
        uint8 m_applyMultiplierMask;
        // 伤害乘数数组
        float m_damageMultipliers[MAX_SPELL_EFFECTS];

        // =====================================================================
        // 当前目标（仅在法术效果中使用）
        // =====================================================================
        // 单位目标
        Unit* unitTarget;
        // 物品目标
        Item* itemTarget;
        // 游戏对象目标
        GameObject* gameObjTarget;
        // 尸体目标
        Corpse* m_corpseTarget;
        // 目标位置
        WorldLocation* destTarget;
        // 伤害值
        int32 damage;
        // 目标命中信息
        SpellMissInfo targetMissInfo;
        // 效果处理模式
        SpellEffectHandleMode effectHandleMode;
        // 效果信息
        SpellEffectInfo const* effectInfo;
        // 获取效果处理器的单位施法者
        Unit* GetUnitCasterForEffectHandlers() const;
        // 单位光环
        UnitAura* _spellAura;
        // 动态对象光环
        DynObjAura* _dynObjAura;

        // -------------------------------------------
        // 法术焦点对象
        GameObject* focusObject;

        // =====================================================================
        // 效果中的伤害和治疗计算
        // =====================================================================
        // 效果中的伤害统计
        int32 m_damage;
        // 效果中的治疗统计
        int32 m_healing;

        // ******************************************
        // 法术触发系统
        // ******************************************
        // 攻击者触发标志
        uint32 m_procAttacker;
        // 受害者触发标志
        uint32 m_procVictim;
        // 命中掩码
        uint32 m_hitMask;
        // 准备触发系统数据
        void prepareDataForTriggerSystem();

        // *****************************************
        // 法术目标子系统
        // *****************************************
        // 目标存储结构和数据
        struct TargetInfoBase
        {
            virtual void PreprocessTarget(Spell* /*spell*/) { }
            virtual void DoTargetSpellHit(Spell* spell, SpellEffectInfo const& spellEffectInfo) = 0;
            virtual void DoDamageAndTriggers(Spell* /*spell*/) { }

            uint8 EffectMask = 0;

        protected:
            TargetInfoBase() { }
            virtual ~TargetInfoBase() { }
        };

        // 单位目标信息
        struct TargetInfo : public TargetInfoBase
        {
            void PreprocessTarget(Spell* spell) override;
            void DoTargetSpellHit(Spell* spell, SpellEffectInfo const& spellEffectInfo) override;
            void DoDamageAndTriggers(Spell* spell) override;

            ObjectGuid TargetGUID;
            uint64 TimeDelay = 0ULL;
            int32 Damage = 0;
            int32 Healing = 0;

            SpellMissInfo MissCondition = SPELL_MISS_NONE;
            SpellMissInfo ReflectResult = SPELL_MISS_NONE;

            bool IsAlive = false;
            bool IsCrit = false;
            bool ScaleAura = false;

            // 在PreprocessTarget中设置的信息，被DoTargetSpellHit使用
            DiminishingGroup DRGroup = DIMINISHING_NONE;
            int32 AuraDuration = 0;
            SpellInfo const* AuraSpellInfo = nullptr;
            int32 AuraBasePoints[MAX_SPELL_EFFECTS] = { };
            bool Positive = true;
            UnitAura* HitAura = nullptr;

        private:
            Unit* _spellHitTarget = nullptr; // 例如被反射时改变
            bool _enablePVP = false;         // 是否需要在DoDamageAndTriggers中启用PVP
        };
        // 目标列表（多目标法术）
        std::vector<TargetInfo> m_UniqueTargetInfo;
        // 引导目标效果掩码（需要存活目标的掩码）
        uint8 m_channelTargetEffectMask;

        // 游戏对象目标信息
        struct GOTargetInfo : public TargetInfoBase
        {
            void DoTargetSpellHit(Spell* spell, SpellEffectInfo const& spellEffectInfo) override;

            ObjectGuid TargetGUID;
            uint64 TimeDelay = 0ULL;
        };
        // 游戏对象目标列表
        std::vector<GOTargetInfo> m_UniqueGOTargetInfo;

        // 物品目标信息
        struct ItemTargetInfo : public TargetInfoBase
        {
            void DoTargetSpellHit(Spell* spell, SpellEffectInfo const& spellEffectInfo) override;

            Item* TargetItem = nullptr;
        };
        // 物品目标列表
        std::vector<ItemTargetInfo> m_UniqueItemInfo;

        // 尸体目标信息
        struct CorpseTargetInfo : public TargetInfoBase
        {
            void DoTargetSpellHit(Spell* spell, SpellEffectInfo const& spellEffectInfo) override;

            ObjectGuid TargetGUID;
            uint64 TimeDelay = 0ULL;
        };
        // 尸体目标列表
        std::vector<CorpseTargetInfo> m_UniqueCorpseTargetInfo;

        template <class Container>
        void DoProcessTargetContainer(Container& targetContainer);

        // 目标目的地数组
        SpellDestination m_destTargets[MAX_SPELL_EFFECTS];

        // 添加单位目标
        void AddUnitTarget(Unit* target, uint32 effectMask, bool checkIfValid = true, bool implicit = true, Position const* losPosition = nullptr);
        // 添加游戏对象目标
        void AddGOTarget(GameObject* target, uint32 effectMask);
        // 添加物品目标
        void AddItemTarget(Item* item, uint32 effectMask);
        // 添加尸体目标
        void AddCorpseTarget(Corpse* target, uint32 effectMask);
        // 添加目的地目标
        void AddDestTarget(SpellDestination const& dest, uint32 effIndex);

        // 预处理法术发射
        void PreprocessSpellLaunch(TargetInfo& targetInfo);
        // 预处理法术命中
        SpellMissInfo PreprocessSpellHit(Unit* unit, bool scaleAura, TargetInfo& targetInfo);
        // 执行法术效果命中
        void DoSpellEffectHit(Unit* unit, SpellEffectInfo const& spellEffectInfo, TargetInfo& targetInfo);

        // 法术命中时执行触发器
        void DoTriggersOnSpellHit(Unit* unit, uint8 effMask);
        // 更新引导法术目标列表
        bool UpdateChanneledTargetList();
        // 是否有效的死亡或存活目标
        bool IsValidDeadOrAliveTarget(Unit const* target) const;
        // 处理发射阶段
        void HandleLaunchPhase();
        // 对发射目标执行效果
        void DoEffectOnLaunchTarget(TargetInfo& targetInfo, float multiplier, SpellEffectInfo const& spellEffectInfo);

        // 准备目标处理
        void PrepareTargetProcessing();
        // 完成目标处理
        void FinishTargetProcessing();

        // 法术执行日志
        void InitEffectExecuteData(uint8 effIndex);
        void AssertEffectExecuteData() const;

        // 脚本系统
        void LoadScripts();
        void CallScriptBeforeCastHandlers();
        void CallScriptOnCastHandlers();
        void CallScriptAfterCastHandlers();
        SpellCastResult CallScriptCheckCastHandlers();
        bool CallScriptEffectHandlers(SpellEffIndex effIndex, SpellEffectHandleMode mode);
        void CallScriptSuccessfulDispel(SpellEffIndex effIndex);
        void CallScriptBeforeHitHandlers(SpellMissInfo missInfo);
        void CallScriptOnHitHandlers();
        void CallScriptAfterHitHandlers();
        void CallScriptObjectAreaTargetSelectHandlers(std::list<WorldObject*>& targets, SpellEffIndex effIndex, SpellImplicitTargetInfo const& targetType);
        void CallScriptObjectTargetSelectHandlers(WorldObject*& target, SpellEffIndex effIndex, SpellImplicitTargetInfo const& targetType);
        void CallScriptDestinationTargetSelectHandlers(SpellDestination& target, SpellEffIndex effIndex, SpellImplicitTargetInfo const& targetType);
        bool CheckScriptEffectImplicitTargets(uint32 effIndex, uint32 effIndexToCheck);
        // 已加载的脚本列表
        std::vector<SpellScript*> m_loadedScripts;

        // 命中触发法术信息
        struct HitTriggerSpell
        {
            HitTriggerSpell(SpellInfo const* spellInfo, SpellInfo const* auraSpellInfo, int32 procChance) :
                triggeredSpell(spellInfo), triggeredByAura(auraSpellInfo), chance(procChance) { }

            SpellInfo const* triggeredSpell;
            SpellInfo const* triggeredByAura;
            // uint8 triggeredByEffIdx          可能稍后需要 - 目前未知需求
            int32 chance;
        };

        // 是否可以在命中时执行触发器
        bool CanExecuteTriggersOnHit(uint8 effMask, SpellInfo const* triggeredByAura = nullptr) const;
        // 准备在命中时执行的触发器
        void PrepareTriggersExecutedOnHit();
        typedef std::vector<HitTriggerSpell> HitTriggerSpellList;
        // 命中触发法术列表
        HitTriggerSpellList m_hitTriggerSpells;

        // 效果辅助函数
        void SummonGuardian(SpellEffectInfo const& spellEffectInfo, uint32 entry, SummonPropertiesEntry const* properties, uint32 numSummons);
        void CalculateJumpSpeeds(SpellEffectInfo const& spellEffectInfo, float dist, float& speedXY, float& speedZ);

        SpellCastResult CanOpenLock(SpellEffectInfo const& spellEffectInfo, uint32 lockid, SkillType& skillid, int32& reqSkillValue, int32& skillValue);
        // -------------------------------------------

        // 法术状态（准备、施法中、完成等）
        uint32 m_spellState;
        // 计时器
        int32 m_timer;

        // 法术事件
        SpellEvent* _spellEvent;
        // 触发标志（被动、物品触发等）
        TriggerCastFlags _triggeredCastFlags;

        // 如果需要，这可以被光环副本替换
        // 我们不能存储原始光环链接以防止访问已删除的光环
        // 同时需要在光环删除后需要光环数据
        // 触发光环法术信息
        SpellInfo const* m_triggeredByAuraSpell;

        // 光环缩放掩码
        uint8 m_auraScaleMask;
        // 预生成的路径
        std::unique_ptr<PathGenerator> m_preGeneratedPath;

        // 效果执行数据
        ByteBuffer* m_effectExecuteData[MAX_SPELL_EFFECTS];

        Spell(Spell const& right) = delete;
        Spell& operator=(Spell const& right) = delete;
};

// Trinity命名空间 - 法术目标检查工具类
namespace Trinity
{
    // 世界对象法术目标检查基类
    struct TC_GAME_API WorldObjectSpellTargetCheck
    {
        protected:
            WorldObject* _caster;                        // 施法者
            WorldObject* _referer;                       // 引用者
            SpellInfo const* _spellInfo;                 // 法术信息
            SpellTargetCheckTypes _targetSelectionType;  // 目标选择类型
            std::unique_ptr<ConditionSourceInfo> _condSrcInfo;
            ConditionContainer const* _condList;         // 条件列表

            WorldObjectSpellTargetCheck(WorldObject* caster, WorldObject* referer, SpellInfo const* spellInfo,
                SpellTargetCheckTypes selectionType, ConditionContainer const* condList);
            ~WorldObjectSpellTargetCheck();

            bool operator()(WorldObject* target) const;
    };

    // 世界对象附近目标检查
    struct TC_GAME_API WorldObjectSpellNearbyTargetCheck : public WorldObjectSpellTargetCheck
    {
        float _range;                    // 范围
        Position const* _position;       // 位置
        WorldObjectSpellNearbyTargetCheck(float range, WorldObject* caster, SpellInfo const* spellInfo,
            SpellTargetCheckTypes selectionType, ConditionContainer const* condList);

        bool operator()(WorldObject* target);
    };

    // 世界对象区域目标检查
    struct TC_GAME_API WorldObjectSpellAreaTargetCheck : public WorldObjectSpellTargetCheck
    {
        float _range;                    // 范围
        Position const* _position;       // 位置
        WorldObjectSpellAreaTargetCheck(float range, Position const* position, WorldObject* caster,
            WorldObject* referer, SpellInfo const* spellInfo, SpellTargetCheckTypes selectionType, ConditionContainer const* condList);

        bool operator()(WorldObject* target) const;
    };

    // 世界对象锥形目标检查
    struct TC_GAME_API WorldObjectSpellConeTargetCheck : public WorldObjectSpellAreaTargetCheck
    {
        float _coneAngle;                // 锥形角度
        WorldObjectSpellConeTargetCheck(float coneAngle, float range, WorldObject* caster,
            SpellInfo const* spellInfo, SpellTargetCheckTypes selectionType, ConditionContainer const* condList);

        bool operator()(WorldObject* target) const;
    };

    // 世界对象轨迹目标检查
    struct TC_GAME_API WorldObjectSpellTrajTargetCheck : public WorldObjectSpellTargetCheck
    {
        float _range;                    // 范围
        Position const* _position;       // 位置
        WorldObjectSpellTrajTargetCheck(float range, Position const* position, WorldObject* caster,
            SpellInfo const* spellInfo, SpellTargetCheckTypes selectionType, ConditionContainer const* condList);

        bool operator()(WorldObject* target) const;
    };
}

// 法术效果处理函数指针类型
using SpellEffectHandlerFn = void(Spell::*)();

#endif
