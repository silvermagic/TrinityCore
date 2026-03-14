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
 * @file KillRewarder.h
 * @brief 击杀奖励分配模块
 *
 * 本文件定义了 KillRewarder 类，负责处理玩家击杀单位后的奖励分配逻辑。
 * 主要功能包括：
 * - 经验值计算与分配
 * - 荣誉点数奖励
 * - 声望奖励
 * - 击杀信用奖励
 * - 组队情况下的奖励分配
 *
 * 该模块支持区分普通击杀和战场击杀，并根据团队成员等级和距离进行公平分配。
 */

#ifndef KillRewarder_h__
#define KillRewarder_h__

#include "Define.h"

class Player;
class Unit;
class Group;

/**
 * @class KillRewarder
 * @brief 击杀奖励处理器
 *
 * 负责处理玩家击杀单位后的奖励分配逻辑，包括经验、荣誉、声望、击杀信用等。
 * 该类封装了单人击杀和组队击杀的复杂奖励计算规则，确保奖励分配公平合理。
 *
 * 核心功能：
 * - 根据击杀者和受害者的等级差计算经验值
 * - 处理组队情况下的经验分配（考虑成员等级和距离）
 * - 区分普通击杀和战场击杀的奖励规则
 * - 处理 PvP 击杀的荣誉奖励
 *
 * 使用流程：
 * 1. 构造 KillRewarder 对象，传入击杀者、受害者和战场标志
 * 2. 调用 Reward() 方法执行奖励分配
 * 3. 对象生命周期结束
 *
 * @note 该类不支持拷贝和移动，仅在构造时初始化数据，Reward() 只能调用一次
 */
class TC_GAME_API KillRewarder
{
public:
    /**
     * @brief 构造函数，初始化击杀奖励器
     *
     * 根据击杀者和受害者初始化奖励计算所需的基础数据。
     * 如果击杀者在队伍中，会初始化队伍相关数据。
     *
     * @param killer 击杀者玩家指针，必须非空
     * @param victim 被击杀的单位，可以是 Creature 或 Player
     * @param isBattleGround 是否在战场中击杀，影响荣誉计算规则
     *
     * @note 构造函数会调用 _InitGroupData() 初始化队伍数据
     * @note 如果击杀者不在队伍中，_group 相关成员会被设置为默认值
     */
    KillRewarder(Player* killer, Unit* victim, bool isBattleGround);

    /**
     * @brief 执行奖励分配
     *
     * 根据击杀类型和队伍情况，执行完整的奖励分配流程。
     * 包括经验、荣誉、声望、击杀信用等所有奖励类型的分配。
     *
     * 分配流程：
     * 1. 如果是组队击杀，调用 _RewardGroup() 进行组队奖励分配
     * 2. 如果是单人击杀，调用 _RewardPlayer() 进行个人奖励分配
     *
     * @note 该方法应只调用一次，重复调用会导致重复奖励
     * @note 奖励分配后会自动清理资源，无需手动释放
     */
    void Reward();

private:
    /**
     * @brief 初始化玩家的经验值数据
     *
     * 根据玩家和受害者的等级差计算基础经验值，并判断是否获得完整经验。
     * 等级差过大时，经验值会减少或归零（灰色怪物）。
     *
     * @param player 目标玩家，用于计算该玩家应得的经验
     *
     * @note 该方法会设置 _xp 和 _isFullXP 成员变量
     * @note 经验计算考虑了玩家等级、受害者等级、休息状态等因素
     */
    void _InitXP(Player* player);

    /**
     * @brief 初始化组队数据
     *
     * 收集队伍中符合奖励条件的成员信息，包括成员数量、等级总和、
     * 最高等级成员等，用于后续的经验分配计算。
     *
     * 初始化内容：
     * - 统计符合条件的队伍成员数量（_count）
     * - 计算队伍成员等级总和（_sumLevel）
     * - 找出最高等级的非灰色怪物成员（_maxNotGrayMember）
     * - 记录队伍最高等级（_maxLevel）
     * - 计算队伍奖励系数（_groupRate）
     *
     * @note 仅统计在击杀者附近且符合条件的队伍成员
     * @note 如果击杀者不在队伍中，相关成员保持默认值
     */
    void _InitGroupData();

    /**
     * @brief 奖励荣誉点数
     *
     * 根据 PvP 击杀情况计算并奖励荣誉点数。
     * 荣誉点数主要用于战场和世界 PvP 击杀。
     *
     * @param player 接收荣誉的玩家
     *
     * @note 仅在 _isPvP 为 true 时才给予荣誉奖励
     * @note 荣誉计算考虑了受害者等级、玩家等级差等因素
     */
    void _RewardHonor(Player* player);

    /**
     * @brief 奖励经验值
     *
     * 根据计算的经验值和系数奖励玩家经验。
     * 经验值会乘以指定的系数后分配给玩家。
     *
     * @param player 接收经验的玩家
     * @param rate 经验倍率系数，用于组队分配时的比例调整
     *
     * @note 经验值已在 _InitXP() 中计算完成，这里直接使用 _xp 成员
     * @note rate 参数通常由队伍分配规则决定
     */
    void _RewardXP(Player* player, float rate);

    /**
     * @brief 奖励声望
     *
     * 根据受害者的阵营关系奖励相应阵营的声望。
     * 声望奖励会乘以指定的系数。
     *
     * @param player 接收声望的玩家
     * @param rate 声望倍率系数，用于组队分配时的比例调整
     *
     * @note 声望奖励取决于受害者的阵营配置
     * @note 某些特殊生物可能不提供声望奖励
     */
    void _RewardReputation(Player* player, float rate);

    /**
     * @brief 奖励击杀信用
     *
     * 更新玩家的击杀统计，用于成就、任务等系统。
     * 击杀信用可能是特定类型的怪物或玩家。
     *
     * @param player 接收击杀信用的玩家
     *
     * @note 击杀信用可能触发成就进度更新
     * @note 某些任务需要特定类型的击杀信用
     */
    void _RewardKillCredit(Player* player);

    /**
     * @brief 奖励单个玩家
     *
     * 为单个玩家执行完整的奖励流程，包括荣誉、经验、声望、击杀信用。
     * 主要用于单人击杀或组队中每个成员的独立奖励。
     *
     * @param player 接收奖励的玩家
     * @param isDungeon 是否在副本中，影响经验计算规则
     *
     * @note 该方法会依次调用所有奖励类型的子方法
     * @note 副本中的奖励规则可能与野外不同
     */
    void _RewardPlayer(Player* player, bool isDungeon);

    /**
     * @brief 奖励队伍
     *
     * 为整个队伍执行奖励分配，根据队伍规模和成员等级进行公平分配。
     * 队伍奖励会考虑成员的贡献度和等级差异。
     *
     * 分配规则：
     * - 遍历所有符合条件的队伍成员
     * - 根据成员等级计算应得的奖励比例
     * - 为每个成员调用 _RewardPlayer() 分配奖励
     *
     * @note 只有在击杀者在队伍中时才会调用此方法
     * @note 队伍奖励可能有经验加成（组队加成）
     */
    void _RewardGroup();

    Player* _killer;              ///< 击杀者玩家指针，存储发起击杀的玩家
    Unit* _victim;                ///< 被击杀的单位，可以是怪物或玩家
    Group* _group;                ///< 击杀者所在的队伍，不在队伍中时为 nullptr
    float _groupRate;             ///< 队伍奖励系数，用于计算组队经验加成
    Player* _maxNotGrayMember;    ///< 队伍中等级最高的非灰色怪物成员，用于经验计算
    uint32 _count;                ///< 队伍中符合条件的成员数量（在附近且有效）
    uint32 _sumLevel;             ///< 队伍成员的等级总和，用于经验分配比例计算
    uint32 _xp;                   ///< 基础经验值，在 _InitXP() 中计算
    bool _isFullXP;               ///< 是否获得完整经验（非灰色怪物）
    uint8 _maxLevel;              ///< 队伍中的最高等级，用于经验上限判断
    bool _isBattleGround;         ///< 是否在战场中击杀，影响荣誉计算
    bool _isPvP;                  ///< 是否为 PvP 击杀（玩家击杀玩家），影响奖励类型
};

#endif // KillRewarder_h__
