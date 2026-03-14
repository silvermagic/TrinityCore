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
 * @file Trainer.h
 * @brief 训练师系统模块
 *
 * 本文件定义了 NPC 训练师系统的核心类和结构，用于处理玩家与训练师 NPC 的交互，
 * 包括技能学习、技能列表展示、训练条件验证等功能。训练师系统支持职业训练、
 * 坐骑训练、专业技能训练和宠物训练等多种类型。
 */

#ifndef Trainer_h__
#define Trainer_h__

#include "Common.h"
#include <array>
#include <vector>

class Creature;
class ObjectMgr;
class Player;

/**
 * @namespace Trainer
 * @brief 训练师系统命名空间
 *
 * 包含所有与训练师相关的类型定义、结构体和类。
 */
namespace Trainer
{
    /**
     * @enum Type
     * @brief 训练师类型枚举
     *
     * 定义训练师的不同类型，用于确定训练师能够教授的技能类别。
     */
    enum class Type : uint32
    {
        Class = 0,      ///< 职业训练师 - 教授职业技能和能力
        Mount = 1,      ///< 坐骑训练师 - 教授骑术和坐骑技能
        Tradeskill = 2, ///< 专业技能训练师 - 教授专业技能配方
        Pet = 3         ///< 宠物训练师 - 教授宠物技能
    };

    /**
     * @enum SpellState
     * @brief 技能状态枚举
     *
     * 定义训练师技能列表中技能的可用状态，用于客户端界面显示。
     */
    enum class SpellState : uint8
    {
        Available = 0,   ///< 可学习 - 玩家满足所有学习条件
        Unavailable = 1, ///< 不可学习 - 玩家不满足学习条件
        Known = 2        ///< 已学会 - 玩家已经掌握该技能
    };

    /**
     * @enum FailReason
     * @brief 训练失败原因枚举
     *
     * 定义技能学习失败的具体原因，用于向客户端发送失败消息。
     */
    enum class FailReason : uint32
    {
        Unavailable = 0,    ///< 技能不可用 - 不满足基本学习条件
        NotEnoughMoney = 1, ///< 金币不足 - 玩家没有足够的金币支付训练费用
        NotEnoughSkill = 2  ///< 技能等级不足 - 玩家的前置技能等级不满足要求
    };

    /**
     * @struct Spell
     * @brief 训练师技能结构体
     *
     * 存储单个训练师技能的完整信息，包括技能 ID、费用、前置要求等。
     * 这些数据从数据库 trainer_spell 表加载。
     */
    struct TC_GAME_API Spell
    {
        uint32 SpellId = 0;                 ///< 技能 ID - 要教授的法术/技能 ID
        uint32 MoneyCost = 0;               ///< 训练费用 - 学习该技能需要的金币数量（铜币单位）
        uint32 ReqSkillLine = 0;            ///< 前置技能线 - 需要的专业技能 ID（如锻造、裁缝等）
        uint32 ReqSkillRank = 0;            ///< 前置技能等级 - 需要的专业技能等级
        std::array<uint32, 3> ReqAbility = { }; ///< 前置能力 - 需要先学会的技能 ID（最多 3 个）
        uint8 ReqLevel = 0;                 ///< 需求等级 - 玩家学习该技能需要的最低角色等级

        /**
         * @brief 检查技能是否可施放
         * @return 如果技能可以施放返回 true，否则返回 false
         *
         * 检查技能是否具有施放效果，某些训练师技能可能只是被动效果。
         */
        bool IsCastable() const;
    };

    /**
     * @class Trainer
     * @brief 训练师核心类
     *
     * 管理单个训练师 NPC 的所有训练相关功能，包括技能列表维护、训练条件验证、
     * 技能教学、客户端消息发送等。每个训练师 NPC 实例对应一个 Trainer 对象。
     *
     * 训练师对象在服务器启动时由 ObjectMgr 从数据库加载，并缓存供后续使用。
     * 支持多语言问候语，根据玩家客户端语言设置显示对应语言的问候消息。
     */
    class TC_GAME_API Trainer
    {
    public:
        /**
         * @brief 构造函数
         * @param trainerId 训练师 ID，对应数据库 trainer 表的主键
         * @param type 训练师类型（职业/坐骑/专业技能/宠物）
         * @param requirement 训练师要求（如职业限制、阵营限制等）
         * @param greeting 默认问候语（未设置本地化时的默认文本）
         * @param spells 训练师可教授的技能列表
         *
         * 构建训练师对象并初始化基本属性。问候语后续可通过 AddGreetingLocale
         * 添加多语言版本。
         */
        Trainer(uint32 trainerId, Type type, uint32 requirement, std::string greeting, std::vector<Spell> spells);

        /**
         * @brief 根据技能 ID 获取训练师技能
         * @param spellId 要查找的技能 ID
         * @return 如果找到返回指向 Spell 结构体的指针，否则返回 nullptr
         *
         * 在训练师的技能列表中查找指定 ID 的技能。时间复杂度 O(n)，n 为技能数量。
         */
        Spell const* GetSpell(uint32 spellId) const;

        /**
         * @brief 获取训练师所有技能列表
         * @return 技能列表的常量引用
         *
         * 返回训练师可教授的所有技能，用于遍历或批量处理。
         * 此函数为内联函数，无性能开销。
         */
        std::vector<Spell> const& GetSpells() const { return _spells; }

        /**
         * @brief 向玩家发送训练师技能列表
         * @param npc 训练师 NPC 对象指针
         * @param player 玩家对象指针
         * @param locale 客户端语言设置
         *
         * 向客户端发送 SMSG_TRAINER_LIST 数据包，包含训练师所有可教授技能的列表、
         * 费用、前置要求和当前状态（可学/不可学/已学）。同时发送训练师问候语。
         * 客户端收到后会打开训练师界面。
         */
        void SendSpells(Creature const* npc, Player const* player, LocaleConstant locale) const;

        /**
         * @brief 检查玩家是否可以学习指定技能
         * @param player 玩家对象指针
         * @param trainerSpell 训练师技能对象指针
         * @return 如果玩家满足学习条件返回 true，否则返回 false
         *
         * 检查内容包括：
         * - 玩家是否已经学会该技能
         * - 玩家等级是否满足要求
         * - 玩家金币是否足够
         * - 前置技能要求是否满足
         * - 专业技能等级是否达标
         * - 前置能力是否已学会
         *
         * 此函数用于训练前的最终验证，确保学习操作可以成功执行。
         */
        bool CanTeachSpell(Player const* player, Spell const* trainerSpell) const;

        /**
         * @brief 教授玩家技能
         * @param npc 训练师 NPC 对象指针
         * @param player 玩家对象指针（会被修改）
         * @param spellId 要教授的技能 ID
         *
         * 执行技能学习流程：
         * 1. 验证玩家是否满足学习条件
         * 2. 扣除金币（学习费用）
         * 3. 教授技能（添加到玩家技能列表）
         * 4. 向客户端发送成功/失败消息
         *
         * 此函数会修改玩家状态（金币、技能列表），因此 player 参数为非 const。
         */
        void TeachSpell(Creature const* npc, Player* player, uint32 spellId) const;

        /**
         * @brief 获取训练师类型
         * @return 训练师类型枚举值
         *
         * 返回训练师的类型（职业/坐骑/专业技能/宠物），用于客户端界面显示。
         * 此函数为内联函数，无性能开销。
         */
        Type GetTrainerType() const { return _type; }

        /**
         * @brief 获取训练师要求
         * @return 训练师要求值（具体含义取决于训练师类型）
         *
         * 返回训练师的要求条件，例如：
         * - 职业训练师：要求的职业 ID
         * - 坐骑训练师：要求的阵营或种族
         * - 专业技能训练师：要求的专业技能 ID
         *
         * 此函数为内联函数，无性能开销。
         */
        uint32 GetTrainerRequirement() const { return _requirement; }

        /**
         * @brief 检查训练师对玩家是否有效
         * @param player 玩家对象指针
         * @return 如果玩家可以使用该训练师返回 true，否则返回 false
         *
         * 检查玩家是否满足训练师的基本要求（如职业限制、阵营限制等）。
         * 这是在打开训练师界面前的初步检查，用于判断 NPC 是否应该显示训练功能。
         */
        bool IsTrainerValidForPlayer(Player const* player) const;

    private:
        /**
         * @brief 获取技能对玩家的状态
         * @param player 玩家对象指针
         * @param trainerSpell 训练师技能对象指针
         * @return 技能状态枚举值（可学/不可学/已学）
         *
         * 内部辅助函数，用于确定技能在训练师界面中的显示状态。
         * 检查玩家是否已学会技能、是否满足学习条件等。
         */
        SpellState GetSpellState(Player const* player, Spell const* trainerSpell) const;

        /**
         * @brief 发送技能学习失败消息
         * @param npc 训练师 NPC 对象指针
         * @param player 玩家对象指针
         * @param spellId 学习失败的技能 ID
         * @param reason 失败原因
         *
         * 向客户端发送 SMSG_TRAINER_BUY_FAILED 数据包，通知玩家技能学习失败。
         * 客户端会根据失败原因显示相应的错误消息。
         */
        void SendTeachFailure(Creature const* npc, Player const* player, uint32 spellId, FailReason reason) const;

        /**
         * @brief 发送技能学习成功消息
         * @param npc 训练师 NPC 对象指针
         * @param player 玩家对象指针
         * @param spellId 学习成功的技能 ID
         *
         * 向客户端发送 SMSG_TRAINER_BUY_SUCCEEDED 数据包，通知玩家技能学习成功。
         * 客户端会更新技能列表和界面显示。
         */
        void SendTeachSucceeded(Creature const* npc, Player const* player, uint32 spellId) const;

        /**
         * @brief 获取指定语言的问候语
         * @param locale 语言设置
         * @return 问候语文本
         *
         * 内部辅助函数，根据玩家客户端语言设置返回对应的问候语。
         * 如果指定语言没有设置问候语，返回默认问候语。
         */
        std::string const& GetGreeting(LocaleConstant locale) const;

        friend ObjectMgr; ///< ObjectMgr 需要访问私有函数 AddGreetingLocale

        /**
         * @brief 添加本地化问候语
         * @param locale 语言设置
         * @param greeting 该语言的问候语文本
         *
         * 由 ObjectMgr 调用，用于在加载训练师数据时添加多语言问候语。
         * 此函数为私有函数，仅 ObjectMgr 可以访问。
         */
        void AddGreetingLocale(LocaleConstant locale, std::string greeting);

        uint32 _trainerId;                          ///< 训练师 ID - 对应数据库 trainer 表的主键
        Type _type;                                 ///< 训练师类型 - 职业/坐骑/专业技能/宠物
        uint32 _requirement;                        ///< 训练师要求 - 职业限制/阵营限制/专业技能限制等
        std::vector<Spell> _spells;                 ///< 技能列表 - 训练师可教授的所有技能
        std::array<std::string, TOTAL_LOCALES> _greeting; ///< 多语言问候语 - 支持所有客户端语言
    };
}

#endif // Trainer_h__
