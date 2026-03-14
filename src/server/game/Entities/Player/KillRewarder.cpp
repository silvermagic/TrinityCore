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
 * @file KillRewarder.cpp
 * @brief 击杀奖励系统实现文件
 *
 * 本文件实现了 KillRewarder 类，负责管理玩家击杀单位后的奖励发放流程。
 * 奖励内容包括：
 * - 经验值（XP）
 * - 荣誉点数
 * - 声望
 * - 击杀计数（用于任务目标）
 *
 * 奖励流程支持单人模式和组队模式，能够处理战场、副本等不同场景的奖励计算。
 */

#include "KillRewarder.h"
#include "SpellAuraEffects.h"
#include "Creature.h"
#include "Formulas.h"
#include "Group.h"
#include "Guild.h"
#include "GuildMgr.h"
#include "InstanceScript.h"
#include "Pet.h"
#include "Player.h"

/**
 * @class KillRewarder
 * @brief 击杀奖励管理器
 *
 * 封装了玩家击杀单位后的奖励发放逻辑。该类负责计算并分发经验值、荣誉、声望和击杀计数。
 *
 * 奖励触发时机：
 * - Unit::Kill() 中玩家击杀单位时
 * - Battleground::RewardXPAtKill() 战场击杀时
 *
 * 奖励算法流程：
 *
 * 1. 初始化内部变量为默认值
 *
 * 2. 若玩家在队伍中，初始化队伍计算所需变量：
 *    2.1. _count - 奖励范围内的存活队伍成员数量
 *    2.2. _sumLevel - 奖励范围内存活队伍成员的等级总和
 *    2.3. _maxLevel - 奖励范围内存活队伍成员的最高等级
 *    2.4. _maxNotGrayMember - 奖励范围内对目标不为灰色（有经验）的存活成员的最高等级
 *    2.5. _isFullXP - 标识所有队伍成员对目标都有经验（奖励100%经验，否则50%）
 *
 * 3. 奖励击杀者（必要时包括队伍）：
 *    3.1. 若击杀者在队伍中，奖励队伍
 *         3.1.1. 基于对目标有经验的队伍成员最高等级初始化经验值
 *         3.1.2. 若队伍在团队副本中调整组队倍率（战场除外）
 *         3.1.3. 奖励奖励范围内的每个队伍成员（包括死亡成员）
 *    3.2. 奖励单人击杀者（非组队情况）
 *         3.2.1. 基于击杀者等级初始化经验值
 *         3.2.2. 奖励击杀者
 *
 * 4. 奖励玩家：
 *    4.1. 发放荣誉（玩家必须存活且不在战场）
 *    4.2. 发放经验值
 *         4.2.1. 若玩家在队伍中，调整经验：
 *                - 若玩家等级高于非灰色成员最高等级，经验设为0
 *                - 若 _isFullXP 为 false，经验减半
 *         4.2.2. 应用影响经验值的光环效果
 *         4.2.3. 给予玩家经验
 *         4.2.4. 若玩家有宠物，奖励宠物经验（单人100%，组队50%）
 *    4.3. 发放声望（玩家必须不在战场）
 *    4.4. 发放击杀计数（玩家必须不在队伍中，或必须存活或无尸体）
 *
 * 5. 记录副本遭遇战进度
 */

/**
 * @brief 构造函数，初始化击杀奖励器
 *
 * 初始化所有内部变量，判断是否为PvP击杀，并初始化队伍数据。
 *
 * @param killer 击杀者玩家指针
 * @param victim 被击杀的单位指针
 * @param isBattleGround 是否在战场中
 *
 * PvP判定逻辑：
 * - 受害者是玩家
 * - 受害者被玩家控制且不是载具
 */
KillRewarder::KillRewarder(Player* killer, Unit* victim, bool isBattleGround) :
    // 1. 初始化内部变量为默认值
    _killer(killer), _victim(victim), _group(killer->GetGroup()),
    _groupRate(1.0f), _maxNotGrayMember(nullptr), _count(0), _sumLevel(0), _xp(0),
    _isFullXP(false), _maxLevel(0), _isBattleGround(isBattleGround), _isPvP(false)
{
    // 如果受害者是玩家，标记为PvP击杀
    if (victim->GetTypeId() == TYPEID_PLAYER)
        _isPvP = true;
    // 如果受害者被玩家控制且不是载具，也标记为PvP击杀
    else if (victim->GetCharmerOrOwnerGUID().IsPlayer())
        _isPvP = !victim->IsVehicle();

    // 初始化队伍相关数据
    _InitGroupData();
}

/**
 * @brief 初始化队伍数据
 *
 * 若玩家在队伍中，遍历队伍成员并统计以下信息：
 * - 奖励范围内的存活成员数量
 * - 成员等级总和
 * - 最高等级成员
 * - 对目标有经验的最高等级成员
 * - 是否全员对目标都有经验
 *
 * 性能说明：
 * - 仅遍历一次队伍成员列表
 * - 只计算存活且在奖励范围内的成员
 * - 击杀者本身无条件计入统计
 */
inline void KillRewarder::_InitGroupData()
{
    if (_group)
    {
        // 2. 若玩家在队伍中，初始化队伍计算所需变量
        for (GroupReference* itr = _group->GetFirstMember(); itr != nullptr; itr = itr->next())
            if (Player* member = itr->GetSource())
                // 检查成员是否为击杀者本人，或（在奖励范围内且存活）
                if (_killer == member || (member->IsAtGroupRewardDistance(_victim) && member->IsAlive()))
                {
                    const uint8 lvl = member->GetLevel();
                    // 2.1. _count - 奖励范围内的存活队伍成员数量
                    ++_count;
                    // 2.2. _sumLevel - 奖励范围内存活队伍成员的等级总和
                    _sumLevel += lvl;
                    // 2.3. _maxLevel - 奖励范围内存活队伍成员的最高等级
                    if (_maxLevel < lvl)
                        _maxLevel = lvl;
                    // 2.4. _maxNotGrayMember - 奖励范围内对目标有经验的存活成员的最高等级
                    // 对于某等级玩家，若目标等级高于其灰色等级，则目标对该玩家有经验
                    uint32 grayLevel = Trinity::XP::GetGrayLevel(lvl);
                    if (_victim->GetLevel() > grayLevel && (!_maxNotGrayMember || _maxNotGrayMember->GetLevel() < lvl))
                        _maxNotGrayMember = member;
                }
        // 2.5. _isFullXP - 标识所有队伍成员对目标都有经验
        // 若最高等级成员对目标有经验，则奖励100%经验；否则仅奖励50%
        _isFullXP = _maxNotGrayMember && (_maxLevel == _maxNotGrayMember->GetLevel());
    }
    else
        // 单人情况，计数为1
        _count = 1;
}

/**
 * @brief 初始化经验值
 *
 * 基于指定玩家的等级计算击杀目标可获得的初始经验值。
 *
 * @param player 用于计算经验值的玩家指针
 *
 * 经验值获取条件（需满足其一）：
 * - 在战场中
 * - 非PvP击杀且击杀者不在载具上
 *
 * 经验值计算使用 Trinity::XP::Gain 公式，考虑玩家等级、目标等级、战场加成等因素。
 */
inline void KillRewarder::_InitXP(Player* player)
{
    // 获取击杀的初始经验值
    // 经验值获取条件：
    // - 在战场中，或
    // - 非PvP击杀且击杀者不在载具上
    if (_isBattleGround || (!_isPvP && !_killer->GetVehicle()))
        _xp = Trinity::XP::Gain(player, _victim, _isBattleGround);
}

/**
 * @brief 奖励荣誉点数
 *
 * 向指定玩家发放击杀目标的荣誉奖励。
 *
 * @param player 接受荣誉奖励的玩家指针
 *
 * 前置条件：玩家必须存活
 *
 * 荣誉计算考虑参与击杀的玩家数量（_count），由 Player::RewardHonor 处理。
 */
inline void KillRewarder::_RewardHonor(Player* player)
{
    // 只有存活的玩家才能获得荣誉
    if (player->IsAlive())
        player->RewardHonor(_victim, _count, -1, true);
}

/**
 * @brief 奖励经验值
 *
 * 向指定玩家发放经验值奖励，并处理组队情况下的经验分配。
 *
 * @param player 接受经验奖励的玩家指针
 * @param rate 经验倍率（组队时基于等级比例，单人时为1.0）
 *
 * 经验值调整逻辑：
 * 1. 组队经验调整：
 *    - 若玩家等级高于非灰色成员最高等级，经验为0（防止高等级带小号）
 *    - 若 _isFullXP 为 false（部分成员对目标无经验），经验减半
 *    - 存活的非灰色成员获得正常经验，死亡或灰色成员经验为0
 *
 * 2. 应用经验加成光环（SPELL_AURA_MOD_XP_PCT）
 *
 * 3. 宠物经验：
 *    - 单人击杀：宠物获得100%经验
 *    - 组队击杀：宠物获得50%经验
 *
 * 性能说明：
 * - 光环效果查询通过 GetTotalAuraMultiplier 优化
 * - 宠物经验仅在玩家获得经验时才计算
 */
inline void KillRewarder::_RewardXP(Player* player, float rate)
{
    uint32 xp(_xp);
    if (_group)
    {
        // 4.2.1. 若玩家在队伍中，调整经验：
        //        - 若玩家等级高于非灰色成员最高等级，经验为0
        //        - 若 _isFullXP 为 false，经验减半
        if (_maxNotGrayMember && player->IsAlive() &&
            _maxNotGrayMember->GetLevel() >= player->GetLevel())
            xp = _isFullXP ?
            uint32(xp * rate) :             // 全员都有经验，奖励全额经验
            uint32(xp * rate / 2) + 1;      // 部分成员无经验，仅奖励一半经验
        else
            xp = 0;  // 玩家等级过高或已死亡，无经验
    }
    if (xp)
    {
        // 4.2.2. 应用影响经验值的光环效果（SPELL_AURA_MOD_XP_PCT）
        xp *= player->GetTotalAuraMultiplier(SPELL_AURA_MOD_XP_PCT);

        // 4.2.3. 给予玩家经验
        player->GiveXP(xp, _victim, _groupRate);
        if (Pet* pet = player->GetPet())
            // 4.2.4. 若玩家有宠物，奖励宠物经验（单人100%，组队50%）
            pet->GivePetXP(_group ? xp / 2 : xp);
    }
}

/**
 * @brief 奖励声望
 *
 * 向指定玩家发放击杀目标的声望奖励。
 *
 * @param player 接受声望奖励的玩家指针
 * @param rate 声望倍率（组队时基于等级比例，副本中固定为1.0）
 *
 * 声望奖励规则：
 * - 玩家无需存活，死亡玩家和尸体也可获得声望
 * - 声望倍率在副本中固定为100%，组队时按等级比例分配
 *
 * @see Player::RewardReputation
 */
inline void KillRewarder::_RewardReputation(Player* player, float rate)
{
    // 4.3. 发放声望（玩家必须不在战场）
    // 即使是死亡玩家和尸体也能获得声望
    player->RewardReputation(_victim, rate);
}

/**
 * @brief 奖励击杀计数
 *
 * 为玩家记录击杀怪物计数，用于任务进度和成就统计。
 *
 * @param player 接受击杀计数的玩家指针
 *
 * 击杀计数条件：
 * - 单人模式：无条件记录
 * - 组队模式：玩家必须存活或无尸体（防止死亡玩家重复计数）
 *
 * 功能：
 * 1. 触发 KilledMonster 事件，更新任务进度
 * 2. 更新击杀生物类型成就进度
 *
 * 仅对生物类型的目标生效（玩家击杀不计入怪物击杀计数）。
 */
inline void KillRewarder::_RewardKillCredit(Player* player)
{
    // 4.4. 发放击杀计数（单人无条件，组队需玩家存活或无尸体）
    if (!_group || player->IsAlive() || !player->GetCorpse())
        if (Creature* target = _victim->ToCreature())
        {
            // 记录击杀怪物，触发相关任务进度更新
            player->KilledMonster(target->GetCreatureTemplate(), target->GetGUID());
            // 更新击杀生物类型成就进度
            player->UpdateAchievementCriteria(ACHIEVEMENT_CRITERIA_TYPE_KILL_CREATURE_TYPE, target->GetCreatureType(), 1, target);
        }
}

/**
 * @brief 奖励单个玩家
 *
 * 完整的玩家奖励流程，依次发放荣誉、经验、声望和击杀计数。
 *
 * @param player 接受奖励的玩家指针
 * @param isDungeon 是否在副本中
 *
 * 奖励流程：
 * 1. 非战场情况：
 *    - 发放荣誉（需存活）
 *    - 若目标是玩家，记录玩家击杀计数（用于任务）
 *
 * 2. 非PvP或战场情况：
 *    - 计算个人倍率：
 *      * 组队：组队倍率 × 玩家等级 / 队伍等级总和
 *      * 单人：固定为1.0
 *    - 发放经验（若 _xp > 0）
 *    - 非战场情况：
 *      * 发放声望（副本中固定100%倍率）
 *      * 发放击杀计数
 *
 * 性能说明：
 * - 倍率计算仅在需要时执行
 * - 各奖励子函数独立判断是否执行
 */
void KillRewarder::_RewardPlayer(Player* player, bool isDungeon)
{
    // 4. 奖励玩家
    if (!_isBattleGround)
    {
        // 4.1. 发放荣誉（玩家必须存活且不在战场）
        _RewardHonor(player);
        // 4.1.1 为玩家击杀任务发送击杀计数
        if (_victim->GetTypeId() == TYPEID_PLAYER)
            player->KilledPlayerCredit();
    }
    // 仅在PvE或战场中发放经验
    // 仅在PvE中发放声望和击杀计数
    if (!_isPvP || _isBattleGround)
    {
        // 计算个人倍率
        float const rate = _group ?
            _groupRate * float(player->GetLevel()) / _sumLevel : // 组队倍率基于等级总和比例
            1.0f;                                                // 个人倍率为100%
        if (_xp)
            // 4.2. 发放经验
            _RewardXP(player, rate);
        if (!_isBattleGround)
        {
            // 若击杀者在副本中，所有成员获得全额声望
            _RewardReputation(player, isDungeon ? 1.0f : rate);
            _RewardKillCredit(player);
        }
    }
}

/**
 * @brief 奖励整个队伍
 *
 * 处理组队情况下的奖励分发，包括经验初始化、组队倍率计算和成员遍历奖励。
 *
 * 前置条件：队伍至少有一名成员在奖励范围内
 *
 * 奖励流程：
 * 1. 初始化经验值：
 *    - 基于对目标有经验的最高等级成员计算初始经验
 *    - 避免因高等级成员导致低等级成员无经验
 *
 * 2. 组队倍率计算：
 *    - 仅在非战场情况下调整
 *    - 团队副本根据人数进一步降低倍率
 *    - 使用 Trinity::XP::xp_in_group_rate 公式
 *
 * 3. 遍历奖励队伍成员：
 *    - 击杀者本人无条件获得奖励
 *    - 其他成员需在奖励范围内
 *    - 包括死亡和尸体状态的成员
 *
 * 性能优化：
 * - 仅在经验值非零或非战场时才执行奖励流程
 * - 战场仅奖励经验，跳过声望和击杀计数
 * - 提前计算副本和团队标识，避免重复查询
 *
 * @see _RewardPlayer
 * @see Trinity::XP::xp_in_group_rate
 */
void KillRewarder::_RewardGroup()
{
    if (_maxLevel)
    {
        if (_maxNotGrayMember)
            // 3.1.1. 基于对目标有经验的队伍成员最高等级初始化经验值
            _InitXP(_maxNotGrayMember);
        // 避免不必要的计算和调用
        // 仅在经验值非零或非战场时继续处理
        // （战场仅奖励经验，因此这是必要条件）
        if (!_isBattleGround || _xp)
        {
            // 判断是否在副本中（非PvP情况）
            bool const isDungeon = !_isPvP && sMapStore.LookupEntry(_killer->GetMapId())->IsDungeon();
            if (!_isBattleGround)
            {
                // 3.1.2. 若队伍在团队中，调整组队倍率（战场除外）
                bool const isRaid = !_isPvP && sMapStore.LookupEntry(_killer->GetMapId())->IsRaid() && _group->isRaidGroup();
                _groupRate = Trinity::XP::xp_in_group_rate(_count, isRaid);
            }

            // 3.1.3. 奖励奖励范围内的每个队伍成员（包括死亡或尸体状态）
            for (GroupReference* itr = _group->GetFirstMember(); itr != nullptr; itr = itr->next())
            {
                if (Player* member = itr->GetSource())
                {
                    // 击杀者可能不在奖励范围内，直接检查
                    if (_killer == member || member->IsAtGroupRewardDistance(_victim))
                    {
                        _RewardPlayer(member, isDungeon);
                    }
                }
            }
        }
    }
}

/**
 * @brief 执行奖励流程
 *
 * 奖励系统的入口函数，根据玩家是否组队选择不同的奖励路径。
 *
 * 执行流程：
 * 1. 组队情况：
 *    - 调用 _RewardGroup() 处理整个队伍的奖励
 *    - 自动处理组队倍率和成员遍历
 *
 * 2. 单人情况：
 *    - 基于击杀者等级初始化经验值
 *    - 调用 _RewardPlayer() 奖励击杀者
 *
 * 3. 副本遭遇战进度：
 *    - 仅对副本Boss生效
 *    - 更新副本的Boss击杀状态
 *    - 触发副本相关事件
 *
 * 性能优化：
 * - 仅在经验值非零或非战场时才执行玩家奖励
 * - 副本遭遇战进度仅查询一次地图实例脚本
 *
 * 调用时机：
 * - Unit::Kill() 中玩家击杀单位时
 * - Battleground::RewardXPAtKill() 战场击杀时
 */
void KillRewarder::Reward()
{
    // 3. 奖励击杀者（必要时包括队伍）
    if (_group)
        // 3.1. 若击杀者在队伍中，奖励队伍
        _RewardGroup();
    else
    {
        // 3.2. 奖励单人击杀者（非组队情况）
        // 3.2.1. 基于击杀者等级初始化经验值
        _InitXP(_killer);
        // 避免不必要的计算和调用
        // 仅在经验值非零或非战场时继续处理
        // （战场仅奖励经验，因此这是必要条件）
        if (!_isBattleGround || _xp)
            // 3.2.2. 奖励击杀者
            _RewardPlayer(_killer, false);
    }

    // 5. 记录副本遭遇战进度
    if (Creature* victim = _victim->ToCreature())
        if (victim->IsDungeonBoss())
            if (InstanceScript* instance = _victim->GetInstanceScript())
                instance->UpdateEncounterStateForKilledCreature(_victim->GetEntry(), _victim);
}
