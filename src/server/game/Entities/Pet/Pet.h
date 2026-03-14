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
 * @file Pet.h
 * @brief 宠物实体类头文件
 *
 * 本文件定义了 Pet 类,它是宠物系统的核心类,继承自 Guardian 类。
 * 宠物是玩家可以拥有的生物实体,包括猎人宠物(驯服的野兽)和召唤宠物(术士、法师等召唤的宠物)。
 *
 * 主要功能模块:
 * - 宠物生命周期管理:创建、加载、保存、移除
 * - 宠物属性管理:等级、经验值、快乐度、天赋点数
 * - 宠物技能管理:学习、遗忘、自动施放
 * - 宠物状态管理:死亡、复活、持续时间
 * - 数据持久化:保存到数据库、从数据库加载
 *
 * 关键类:
 * - Pet: 宠物实体类,管理单个宠物的所有属性和行为
 * - PetSpell: 宠物技能结构,存储技能状态和类型
 * - PetStable: 宠物存储类,管理玩家的所有宠物
 *
 * 性能考虑:
 * - 宠物更新频率高,Update 方法需要高效处理
 * - 技能和光环加载采用异步查询,避免阻塞主线程
 * - 使用 m_loading 标志防止重复加载
 */

#ifndef TRINITYCORE_PET_H
#define TRINITYCORE_PET_H

#include "PetDefines.h"
#include "TemporarySummon.h"

// 快乐度等级大小常量，用于计算宠物快乐度状态
#define HAPPINESS_LEVEL_SIZE        333000

/**
 * @brief 宠物技能结构体
 *
 * 存储单个宠物技能的相关信息
 */
struct PetSpell
{
    ActiveStates active;    // 技能激活状态（自动施放、禁用等）
    PetSpellState state;    // 技能状态（新建、已改变、未改变等）
    PetSpellType type;      // 技能类型（普通技能、家族技能、天赋技能等）
};

// 宠物技能映射表：技能ID -> PetSpell
typedef std::unordered_map<uint32, PetSpell> PetSpellMap;
// 自动施放技能列表
typedef std::vector<uint32> AutoSpellList;

class Player;
class PetAura;

/**
 * @brief 宠物类
 *
 * 继承自 Guardian 类，表示玩家拥有的宠物实体。
 * 宠物可以是猎人宠物、召唤宠物或其他类型的宠物。
 * 该类管理宠物的技能、快乐度、经验值、天赋等属性，
 * 以及宠物的创建、保存、加载、更新等生命周期操作。
 */
class TC_GAME_API Pet : public Guardian
{
    public:
        /**
         * @brief 构造函数
         * @param owner 宠物的主人（玩家）
         * @param type 宠物类型，默认为 MAX_PET_TYPE（无效类型）
         */
        explicit Pet(Player* owner, PetType type = MAX_PET_TYPE);

        /**
         * @brief 析构函数
         */
        virtual ~Pet();

        /**
         * @brief 将宠物添加到游戏世界中
         *
         * 重写基类方法，执行宠物加入世界时的初始化操作
         */
        void AddToWorld() override;

        /**
         * @brief 将宠物从游戏世界中移除
         *
         * 重写基类方法，执行宠物离开世界时的清理操作
         */
        void RemoveFromWorld() override;

        /**
         * @brief 获取宠物的原生对象缩放比例
         * @return 缩放比例值
         */
        float GetNativeObjectScale() const override;

        /**
         * @brief 设置宠物的显示模型ID
         * @param modelId 模型ID
         */
        void SetDisplayId(uint32 modelId) override;

        /**
         * @brief 获取宠物类型
         * @return 宠物类型枚举值
         */
        PetType getPetType() const { return m_petType; }

        /**
         * @brief 设置宠物类型
         * @param type 宠物类型
         */
        void setPetType(PetType type) { m_petType = type; }

        /**
         * @brief 判断宠物是否为可控宠物
         * @return 如果是召唤宠物或猎人宠物则返回 true
         */
        bool isControlled() const { return getPetType() == SUMMON_PET || getPetType() == HUNTER_PET; }

        /**
         * @brief 判断宠物是否为临时召唤的
         * @return 如果宠物有持续时间限制则返回 true
         */
        bool isTemporarySummoned() const { return m_duration > 0; }

        /**
         * @brief 判断宠物是否为玩家的永久宠物
         * @param owner 要检查的玩家
         * @return 如果是永久宠物（在角色窗口有标签页并设置了 UNIT_FIELD_PETNUMBER）则返回 true
         */
        bool IsPermanentPetFor(Player* owner) const;

        /**
         * @brief 创建宠物
         * @param guidlow 宠物的低阶GUID
         * @param map 所在地图
         * @param phaseMask 相位掩码
         * @param Entry 生物模板ID
         * @param pet_number 宠物编号
         * @return 创建成功返回 true
         */
        bool Create(ObjectGuid::LowType guidlow, Map* map, uint32 phaseMask, uint32 Entry, uint32 pet_number);

        /**
         * @brief 基于现有生物创建宠物基础数据
         * @param creature 源生物
         * @return 创建成功返回 true
         */
        bool CreateBaseAtCreature(Creature* creature);

        /**
         * @brief 基于生物模板创建宠物基础数据
         * @param cinfo 生物模板
         * @param owner 宠物主人
         * @return 创建成功返回 true
         */
        bool CreateBaseAtCreatureInfo(CreatureTemplate const* cinfo, Unit* owner);

        /**
         * @brief 基于驯服的生物模板创建宠物基础数据
         * @param cinfo 生物模板
         * @param map 所在地图
         * @param phaseMask 相位掩码
         * @return 创建成功返回 true
         */
        bool CreateBaseAtTamed(CreatureTemplate const* cinfo, Map* map, uint32 phaseMask);

        /**
         * @brief 获取要加载的宠物信息
         * @param stable 宠物栏数据
         * @param petEntry 宠物模板ID（0表示任意）
         * @param petnumber 宠物编号（0表示任意）
         * @param current 是否只获取当前召唤的宠物
         * @return 返回宠物信息和保存模式的配对
         */
        static std::pair<PetStable::PetInfo const*, PetSaveMode> GetLoadPetInfo(PetStable const& stable, uint32 petEntry, uint32 petnumber, bool current);

        /**
         * @brief 从数据库加载宠物数据
         * @param owner 宠物主人
         * @param petEntry 宠物模板ID（0表示加载当前宠物）
         * @param petnumber 宠物编号（0表示加载当前宠物）
         * @param current 是否加载当前宠物
         * @return 加载成功返回 true
         */
        bool LoadPetFromDB(Player* owner, uint32 petEntry, uint32 petnumber, bool current);

        /**
         * @brief 检查宠物是否正在加载中
         * @return 如果正在加载则返回 true
         */
        bool IsLoading() const override { return m_loading;}

        /**
         * @brief 将宠物保存到数据库
         * @param mode 保存模式
         */
        void SavePetToDB(PetSaveMode mode);

        /**
         * @brief 填充宠物信息结构
         * @param petInfo 要填充的宠物信息指针
         */
        void FillPetInfo(PetStable::PetInfo* petInfo) const;

        /**
         * @brief 移除宠物
         * @param mode 保存模式
         * @param returnreagent 是否返还施法材料
         */
        void Remove(PetSaveMode mode, bool returnreagent = false);

        /**
         * @brief 从数据库删除宠物记录
         * @param guidlow 宠物的低阶GUID
         */
        static void DeleteFromDB(ObjectGuid::LowType guidlow);

        /**
         * @brief 设置宠物的死亡状态
         * @param s 死亡状态
         *
         * 重写 Creature::setDeathState 和 Unit::setDeathState
         */
        void setDeathState(DeathState s) override;

        /**
         * @brief 更新宠物状态
         * @param diff 距离上次更新的时间差（毫秒）
         *
         * 重写 Creature::Update 和 Unit::Update
         */
        void Update(uint32 diff) override;

        /**
         * @brief 获取宠物自动施放技能数量
         * @return 自动施放技能数量
         */
        uint8 GetPetAutoSpellSize() const override { return m_autospells.size(); }

        /**
         * @brief 获取指定位置的自动施放技能ID
         * @param pos 位置索引
         * @return 技能ID，如果位置无效则返回0
         */
        uint32 GetPetAutoSpellOnPos(uint8 pos) const override
        {
            if (pos >= m_autospells.size())
                return 0;
            else
                return m_autospells[pos];
        }

        /**
         * @brief 减少宠物的快乐度
         *
         * 根据时间减少宠物的快乐度值
         */
        void LoseHappiness();

        /**
         * @brief 获取宠物的快乐度状态
         * @return 快乐度状态枚举值（不快乐、满足、快乐等）
         */
        HappinessState GetHappinessState();

        /**
         * @brief 给予宠物经验值
         * @param xp 经验值数量
         */
        void GivePetXP(uint32 xp);

        /**
         * @brief 设置宠物等级
         * @param level 目标等级
         */
        void GivePetLevel(uint8 level);

        /**
         * @brief 设置宠物当前经验值
         * @param xp 经验值
         */
        void SetPetExperience(uint32 xp) { SetUInt32Value(UNIT_FIELD_PETEXPERIENCE, xp); }

        /**
         * @brief 设置宠物升级所需经验值
         * @param xp 升级所需经验值
         */
        void SetPetNextLevelExperience(uint32 xp) { SetUInt32Value(UNIT_FIELD_PETNEXTLEVELEXP, xp); }

        /**
         * @brief 将宠物等级与主人同步
         */
        void SynchronizeLevelWithOwner();

        /**
         * @brief 检查物品是否在宠物食谱中
         * @param item 物品模板
         * @return 如果宠物可以吃该物品则返回 true
         */
        bool HaveInDiet(ItemTemplate const* item) const;

        /**
         * @brief 获取当前食物的受益等级
         * @param itemlevel 物品等级
         * @return 受益等级
         */
        uint32 GetCurrentFoodBenefitLevel(uint32 itemlevel) const;

        /**
         * @brief 设置宠物持续时间
         * @param dur 持续时间（毫秒）
         */
        void SetDuration(int32 dur) { m_duration = dur; }

        /**
         * @brief 获取宠物持续时间
         * @return 持续时间（毫秒）
         */
        int32 GetDuration() const { return m_duration; }

        /*
        bool UpdateStats(Stats stat);
        bool UpdateAllStats();
        void UpdateResistances(uint32 school);
        void UpdateArmor();
        void UpdateMaxHealth();
        void UpdateMaxPower(Powers power);
        void UpdateAttackPowerAndDamage(bool ranged = false);
        void UpdateDamagePhysical(WeaponAttackType attType) override;
        */

        /**
         * @brief 切换技能的自动施放状态
         * @param spellInfo 技能信息
         * @param apply true表示启用自动施放，false表示禁用
         */
        void ToggleAutocast(SpellInfo const* spellInfo, bool apply);

        /**
         * @brief 检查宠物是否拥有指定技能
         * @param spell 技能ID
         * @return 如果拥有则返回 true
         */
        bool HasSpell(uint32 spell) const override;

        /**
         * @brief 学习宠物被动技能
         *
         * 根据宠物模板和等级学习相应的被动技能
         */
        void LearnPetPassives();

        /**
         * @brief 施放宠物光环
         * @param current 是否只对当前宠物生效
         */
        void CastPetAuras(bool current);

        /**
         * @brief 施放单个宠物光环
         * @param aura 宠物光环指针
         */
        void CastPetAura(PetAura const* aura);

        /**
         * @brief 检查光环是否为宠物光环
         * @param aura 光环指针
         * @return 如果是宠物光环则返回 true
         */
        bool IsPetAura(Aura const* aura);

        /**
         * @brief 从数据库加载光环数据
         * @param result 数据库查询结果
         * @param timediff 时间差（用于调整持续时间）
         */
        void _LoadAuras(PreparedQueryResult result, uint32 timediff);

        /**
         * @brief 保存光环数据到数据库
         * @param trans 数据库事务
         */
        void _SaveAuras(CharacterDatabaseTransaction trans);

        /**
         * @brief 从数据库加载技能数据
         * @param result 数据库查询结果
         */
        void _LoadSpells(PreparedQueryResult result);

        /**
         * @brief 保存技能数据到数据库
         * @param trans 数据库事务
         */
        void _SaveSpells(CharacterDatabaseTransaction trans);

        /**
         * @brief 添加技能到宠物技能列表
         * @param spellId 技能ID
         * @param active 激活状态，默认为 ACT_DECIDE
         * @param state 技能状态，默认为 PETSPELL_NEW
         * @param type 技能类型，默认为 PETSPELL_NORMAL
         * @return 添加成功返回 true
         */
        bool addSpell(uint32 spellId, ActiveStates active = ACT_DECIDE, PetSpellState state = PETSPELL_NEW, PetSpellType type = PETSPELL_NORMAL);

        /**
         * @brief 学习技能
         * @param spell_id 技能ID
         * @return 学习成功返回 true
         */
        bool learnSpell(uint32 spell_id);

        /**
         * @brief 学习高等级技能
         * @param spellid 技能ID
         */
        void learnSpellHighRank(uint32 spellid);

        /**
         * @brief 初始化当前等级的升级技能
         */
        void InitLevelupSpellsForLevel();

        /**
         * @brief 遗忘技能
         * @param spell_id 技能ID
         * @param learn_prev 是否学习前置技能
         * @param clear_ab 是否清除动作栏
         * @return 遗忘成功返回 true
         */
        bool unlearnSpell(uint32 spell_id, bool learn_prev, bool clear_ab = true);

        /**
         * @brief 移除技能
         * @param spell_id 技能ID
         * @param learn_prev 是否学习前置技能
         * @param clear_ab 是否清除动作栏
         * @return 移除成功返回 true
         */
        bool removeSpell(uint32 spell_id, bool learn_prev, bool clear_ab = true);

        /**
         * @brief 清理动作栏
         *
         * 移除宠物不再拥有的技能的动作栏槽位
         */
        void CleanupActionBar();

        /**
         * @brief 生成动作栏数据字符串
         * @return 动作栏数据的字符串表示
         */
        std::string GenerateActionBarData() const;

        PetSpellMap     m_spells;       // 宠物技能映射表，存储所有宠物技能及其状态
        AutoSpellList   m_autospells;   // 自动施放技能列表

        /**
         * @brief 初始化宠物创建时的技能
         *
         * 为新创建的宠物初始化默认技能
         */
        void InitPetCreateSpells();

        /**
         * @brief 重置宠物天赋
         * @param involuntarily 是否为非自愿重置（如系统强制重置）
         * @return 重置成功返回 true
         */
        bool resetTalents(bool involuntarily = false);

        /**
         * @brief 为玩家的所有宠物重置天赋
         * @param owner 宠物主人
         * @param online_pet 当前在线的宠物（可选）
         * @param involuntarily 是否为非自愿重置
         */
        static void resetTalentsForAllPetsOf(Player* owner, Pet* online_pet = nullptr, bool involuntarily = false);

        /**
         * @brief 初始化当前等级的天赋
         */
        void InitTalentForLevel();

        /**
         * @brief 获取指定等级的最大天赋点数
         * @param level 宠物等级
         * @return 最大天赋点数
         */
        uint8 GetMaxTalentPointsForLevel(uint8 level) const;

        /**
         * @brief 获取可用天赋点数
         * @return 可用天赋点数
         */
        uint8 GetFreeTalentPoints() const { return GetByteValue(UNIT_FIELD_BYTES_1, UNIT_BYTES_1_OFFSET_PET_TALENTS); }

        /**
         * @brief 设置可用天赋点数
         * @param points 天赋点数
         */
        void SetFreeTalentPoints(uint8 points) { SetByteValue(UNIT_FIELD_BYTES_1, UNIT_BYTES_1_OFFSET_PET_TALENTS, points); }

        uint32  m_usedTalentCount;   // 已使用的天赋点数

        /**
         * @brief 获取团队光环更新掩码
         * @return 光环更新掩码
         */
        uint64 GetAuraUpdateMaskForRaid() const { return m_auraRaidUpdateMask; }

        /**
         * @brief 设置团队光环更新掩码的指定槽位
         * @param slot 光环槽位
         */
        void SetAuraUpdateMaskForRaid(uint8 slot) { m_auraRaidUpdateMask |= (uint64(1) << slot); }

        /**
         * @brief 重置团队光环更新掩码
         */
        void ResetAuraUpdateMaskForRaid() { m_auraRaidUpdateMask = 0; }

        /**
         * @brief 获取宠物的格名称数据
         * @return 格名称数据指针，如果没有则返回 nullptr
         */
        DeclinedName const* GetDeclinedNames() const { return m_declinedname.get(); }

        bool    m_removed;           // 宠物移除标记，防止在宠物已移除（已保存）后再次更新时覆盖数据库中的宠物状态

        /**
         * @brief 获取宠物主人
         * @return 宠物主人指针
         */
        Player* GetOwner() const;

        /**
         * @brief 获取调试信息
         * @return 调试信息字符串
         */
        std::string GetDebugInfo() const override;

    protected:
        uint32  m_happinessTimer;       // 快乐度更新计时器，用于定期减少宠物快乐度
        PetType m_petType;              // 宠物类型（猎人宠物、召唤宠物等）
        int32   m_duration;             // 宠物持续时间（毫秒），到时间后会自动解散（主要用于召唤的守护者，可控宠物不使用）
        uint64  m_auraRaidUpdateMask;   // 团队光环更新掩码，用于追踪需要向团队同步的光环变化
        bool    m_loading;              // 宠物加载中标记，表示宠物正在从数据库加载
        uint32  m_focusRegenTimer;      // 集中值回复计时器，用于定期回复宠物集中值

        std::unique_ptr<DeclinedName> m_declinedname;  // 宠物的格名称数据（用于俄语等需要变格的语言）

    private:
        /**
         * @brief 保存到数据库（重写 Creature::SaveToDB）
         *
         * 此方法不应被调用，宠物应使用 SavePetToDB 方法
         * 如果被调用，程序将中止
         */
        void SaveToDB(uint32, uint8, uint32) override
        {
            ABORT();
        }
};
#endif
