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
 * @file LFGPlayerData.h
 * @brief 玩家LFG(地下城查找器)数据管理模块
 *
 * 本文件定义了 LfgPlayerData 类,用于存储和管理玩家在随机副本系统中的所有数据。
 * 该类是 LFG 系统的核心数据结构之一,负责追踪玩家从加入队列到完成副本的整个流程。
 *
 * 主要职责:
 * - 维护玩家的 LFG 状态机(排队中、角色检查、提案、副本中等)
 * - 存储玩家的副本偏好设置(副本列表、角色定位、备注)
 * - 记录玩家的原始队伍信息以支持队伍恢复
 * - 提供成就系统所需的数据(如加入时的队伍规模)
 *
 * @see LFG.h - LFG 系统的基础定义和枚举类型
 * @see LFGMgr.h - LFG 系统的管理器
 */

#ifndef _LFGPLAYERDATA_H
#define _LFGPLAYERDATA_H

#include "LFG.h"

namespace lfg
{

/**
 * @brief 玩家LFG数据容器类 - 存储玩家在随机副本系统中需要的所有数据
 *
 * 该类是玩家参与 LFG(地下城查找器)系统的核心数据结构,负责管理与单个玩家相关的所有 LFG 数据。
 * 每个 Player 对象都持有一个 LfgPlayerData 实例,在玩家使用 LFG 功能期间维护状态。
 *
 * 主要功能:
 * - **状态管理**: 追踪玩家在 LFG 流程中的当前状态和旧状态(支持状态恢复)
 * - **队列配置**: 存储玩家选择的副本、角色定位和备注信息
 * - **队伍关联**: 记录玩家加入 LFG 前的原始队伍,以便流程结束后恢复
 * - **成就追踪**: 记录加入时的队伍规模,用于"随机副本"相关成就判定
 *
 * 状态转换流程:
 * 1. 玩家打开 LFG 界面 -> LFG_STATE_NONE
 * 2. 玩家选择副本并加入队列 -> LFG_STATE_QUEUED
 * 3. 匹配成功,进入角色检查 -> LFG_STATE_ROLECHECK
 * 4. 角色检查通过,等待其他玩家确认 -> LFG_STATE_PROPOSAL
 * 5. 所有玩家确认,进入副本 -> LFG_STATE_DUNGEON
 * 6. 完成副本 -> LFG_STATE_NONE
 *
 * 线程安全:
 * - 该类的所有方法都应在世界线程中调用,不保证线程安全
 * - 修改状态时应确保已获取必要的锁
 *
 * 性能考虑:
 * - 成员变量使用值语义,避免频繁的内存分配
 * - Get 方法返回常量引用,避免不必要的拷贝
 *
 * @note 该类不负责 LFG 的匹配逻辑,仅作为数据容器使用
 * @note 状态恢复机制(RestoreState)用于处理角色检查失败等异常情况
 *
 * @see LfgState - LFG 状态枚举定义
 * @see LFGMgr - LFG 管理器,负责匹配和状态机驱动
 * @see Player - 玩家类,持有 LfgPlayerData 实例
 */
class TC_GAME_API LfgPlayerData
{
    public:
        /**
         * @brief 构造函数 - 初始化玩家的LFG数据为默认状态
         *
         * 初始化所有成员变量为合理的默认值:
         * - m_State = LFG_STATE_NONE (未参与LFG)
         * - m_OldState = LFG_STATE_NONE
         * - m_Team = 0 (未设置)
         * - m_Group = 空GUID
         * - m_Roles = 0 (未选择角色)
         * - m_Comment = 空字符串
         * - m_SelectedDungeons = 空集合
         * - m_NumberOfPartyMembersAtJoin = 0
         *
         * @note 该构造函数在玩家对象创建时自动调用
         * @note 默认状态表示玩家当前未参与任何LFG活动
         */
        LfgPlayerData();

        /**
         * @brief 析构函数 - 清理LFG数据
         *
         * 清理所有资源,包括:
         * - 清空副本列表
         * - 清空备注字符串
         *
         * @note 析构时不需要通知LFG管理器,由Player类的析构流程负责
         */
        ~LfgPlayerData();

        /**
         * @name 通用方法
         * @{
         */

        /**
         * @brief 设置玩家的LFG状态
         *
         * 将玩家切换到新的 LFG 状态,同时保存当前状态到 m_OldState 以便后续恢复。
         * 这是 LFG 状态机的核心方法,每次状态转换都应调用此方法。
         *
         * 状态转换示例:
         * - NONE -> QUEUED: 玩家加入队列
         * - QUEUED -> ROLECHECK: 匹配成功,开始角色检查
         * - ROLECHECK -> PROPOSAL: 角色检查通过
         * - PROPOSAL -> DUNGEON: 所有玩家确认提案
         * - DUNGEON -> NONE: 完成副本
         *
         * @param state 新的LFG状态
         *              - LFG_STATE_NONE: 未参与LFG
         *              - LFG_STATE_QUEUED: 在队列中等待匹配
         *              - LFG_STATE_ROLECHECK: 正在进行角色检查
         *              - LFG_STATE_PROPOSAL: 正在等待提案确认
         *              - LFG_STATE_DUNGEON: 在LFG副本中
         *              - LFG_STATE_FINISHED_DUNGEON: 已完成副本
         *
         * @note 此方法会自动保存旧状态,不需要单独调用
         * @note 状态转换逻辑由 LFGMgr 控制,此方法仅负责数据存储
         *
         * @see RestoreState() - 恢复到旧状态
         * @see LfgState - 状态枚举定义
         */
        void SetState(LfgState state);

        /**
         * @brief 恢复之前的LFG状态
         *
         * 将当前状态(m_State)恢复为之前保存的旧状态(m_OldState)。
         * 这是一个关键的异常处理机制,用于 LFG 流程中的失败回滚。
         *
         * 典型使用场景:
         * 1. **角色检查失败**: 所有玩家选择了相同的角色定位,无法组成有效队伍
         *    - 状态: ROLECHECK -> QUEUED (恢复排队状态)
         * 2. **提案被拒绝**: 某个玩家拒绝了进入副本的提案
         *    - 状态: PROPOSAL -> QUEUED (恢复排队状态)
         * 3. **玩家掉线**: 在提案阶段玩家掉线
         *    - 状态: PROPOSAL -> QUEUED
         *
         * 状态恢复逻辑:
         * @code
         * m_State = m_OldState;  // 恢复当前状态为旧状态
         * @endcode
         *
         * @note 调用此方法后,m_OldState 不会被修改,因此可以多次恢复
         * @note 如果没有调用过 SetState,恢复到 LFG_STATE_NONE
         * @note 此方法由 LFGMgr 在检测到失败条件时调用
         *
         * @see SetState() - 设置新状态时会保存旧状态
         */
        void RestoreState();

        /**
         * @brief 设置玩家的阵营
         *
         * 设置玩家所属的阵营(联盟或部落),这决定了玩家可以加入的 LFG 队列。
         * 阵营通常从玩家对象获取,在玩家加入 LFG 时设置。
         *
         * 阵营的作用:
         * - LFG 系统只会将同阵营的玩家匹配到一起
         * - 某些副本有阵营限制(如特定的阵营专属副本)
         * - 阵营信息用于显示正确的副本列表(某些副本对不同阵营显示不同)
         *
         * @param team 阵营ID
         *             - ALLIANCE (0): 联盟
         *             - HORDE (1): 部落
         *
         * @note 此方法通常在玩家初始化 LFG 数据时调用一次
         * @note 阵营在玩家创建时确定,游戏过程中不会改变
         *
         * @see Player::GetTeamId() - 获取玩家的阵营ID
         */
        void SetTeam(uint8 team);

        /**
         * @brief 设置玩家所在的队伍
         *
         * 记录玩家加入 LFG 时的原始队伍 GUID,用于在 LFG 流程结束后恢复队伍状态。
         * 这是一个关键的恢复机制,支持"预组队"加入随机副本的场景。
         *
         * 使用场景:
         * 1. **预组队加入**: 一个5人队伍一起加入随机副本
         *    - 记录原始队伍 GUID
         *    - 在副本完成后,玩家仍然保持原队伍关系
         *
         * 2. **单人加入**: 独自加入随机副本
         *    - GUID 为空
         *    - 完成副本后,临时队伍解散
         *
         * 3. **部分预组**: 2-4人小队加入
         *    - 记录原始队伍 GUID
         *    - 系统会补充其他玩家填补空缺
         *    - 完成后原小队成员保持组队
         *
         * @param group 队伍的GUID
         *              - 有效GUID: 玩家在队伍中,记录队伍GUID
         *              - 空GUID: 玩家独自一人
         *
         * @note 此方法在玩家加入 LFG 队列时调用
         * @note 在 LFG 流程中,玩家可能会被转移到临时队伍,但原始队伍信息会被保留
         *
         * @see GetGroup() - 获取原始队伍GUID
         */
        void SetGroup(ObjectGuid group);

        /** @} */

        /**
         * @name 队列方法
         * @{
         */

        /**
         * @brief 设置玩家选择的角色定位
         *
         * 设置玩家在 LFG 队列中愿意承担的角色定位(坦克、治疗或输出)。
         * 玩家可以选择多个角色定位,系统会根据队伍需求进行分配。
         *
         * 角色定位采用位标志设计,可以使用位运算组合:
         * @code
         * // 仅选择坦克
         * SetRoles(ROLE_TANK);
         *
         * // 同时选择坦克和治疗(可切换角色)
         * SetRoles(ROLE_TANK | ROLE_HEALER);
         *
         * // 选择所有角色定位
         * SetRoles(ROLE_TANK | ROLE_HEALER | ROLE_DAMAGE);
         * @endcode
         *
         * 角色定位的作用:
         * - **匹配优先级**: 坦克和治疗通常队列较短,匹配更快
         * - **角色检查**: 队伍成员的角色定位不能冲突
         * - **奖励机制**: 选择稀缺角色(坦克/治疗)可能获得额外奖励
         *
         * @param roles 角色定位标志位组合
         *              - ROLE_NONE (0x00): 未选择
         *              - ROLE_TANK (0x01): 坦克 - 承受伤害,保护队友
         *              - ROLE_HEALER (0x02): 治疗 - 恢复生命,辅助队友
         *              - ROLE_DAMAGE (0x04): 输出 - 造成伤害,击杀敌人
         *              - ROLE_LEADER (0x08): 队长 - 额外权限(可与其他角色组合)
         *
         * @note 角色定位由玩家在 LFG 界面中手动选择
         * @note 系统会根据玩家的职业专精自动建议合适的角色定位
         * @note 角色选择会影响匹配速度: 坦克/治疗 > 坦克/输出 > 仅输出
         *
         * @see GetRoles() - 获取当前选择的角色定位
         * @see LFGMgr::CheckRoleCheck() - 角色检查逻辑
         */
        void SetRoles(uint8 roles);

        /**
         * @brief 设置玩家的备注信息
         *
         * 设置玩家在 LFG 队列中显示的备注信息,其他玩家在角色检查阶段可以看到。
         * 备注通常用于表达玩家的偏好、要求或自我介绍。
         *
         * 常见备注示例:
         * - "首次尝试,请多指教" - 新手玩家
         * - "速通,有经验优先" - 追求效率的队伍
         * - "完成任务: [任务名称]" - 明确副本目标
         * - "需要装备: [装备名称]" - 表明需求
         *
         * 备注长度限制:
         * - 客户端通常限制为 255 个字符
         * - 过长的备注会被客户端截断
         *
         * @param comment 备注字符串(UTF-8编码)
         *                - 空字符串表示无备注
         *                - 建议使用简洁明了的描述
         *
         * @note 备注信息存储为值拷贝,性能考虑应避免过长的字符串
         * @note 备注在角色检查阶段对所有队伍成员可见
         * @note 备注内容不会被服务器验证,由客户端负责显示处理
         *
         * @see GetComment() - 获取当前备注
         */
        void SetComment(std::string const& comment);

        /**
         * @brief 设置玩家选择的副本列表
         *
         * 设置玩家想要进入的副本 ID 集合。玩家可以选择多个副本,系统会从中选择一个进行匹配。
         * 这是 LFG 系统的核心配置,决定了玩家会被匹配到哪个副本。
         *
         * 副本选择类型:
         * 1. **随机副本**: 选择"随机副本"选项,系统从所有符合等级的副本中随机选择
         *    - 优点: 额外的奖励(金币、正义点数等)
         *    - 缺点: 无法选择具体副本
         *
         * 2. **指定副本**: 选择具体的副本列表
         *    - 优点: 可以选择想要的副本
         *    - 缺点: 无额外奖励,匹配可能较慢
         *
         * 3. **混合选择**: 选择多个副本 + 随机选项
         *    - 系统优先匹配指定副本
         *    - 如果随机副本奖励更好,可能优先随机
         *
         * 副本列表的处理:
         * - 按优先级排序: 系统优先匹配列表中的第一个副本
         * - 过滤不合法副本: 等级不符、未解锁的副本会被过滤
         * - 同步更新: 选择变更时会通知 LFG 管理器重新匹配
         *
         * @param dungeons 副本ID集合(LfgDungeonSet)
         *                 - LfgDungeonSet 是 std::set<uint32> 的别名
         *                 - 每个元素是一个副本的 ID
         *                 - 空集合表示未选择任何副本
         *
         * @note 此方法会复制整个集合,性能考虑应避免频繁调用
         * @note 副本列表在角色检查通过后不应再改变
         * @note 最终进入的副本由 LFGMgr::ProposalUpdate() 决定
         *
         * @see GetSelectedDungeons() - 获取当前选择的副本列表
         * @see LFGMgr::Join() - 加入队列时设置副本列表
         */
        void SetSelectedDungeons(LfgDungeonSet const& dungeons);

        /** @} */

        /**
         * @name 获取方法
         * @{
         */

        /**
         * @brief 获取玩家当前的LFG状态
         *
         * 返回玩家在 LFG 流程中的当前状态。这是状态查询的核心方法。
         *
         * 状态含义:
         * - LFG_STATE_NONE: 玩家未参与 LFG 活动
         * - LFG_STATE_QUEUED: 玩家在队列中等待匹配
         * - LFG_STATE_ROLECHECK: 正在进行角色检查(匹配成功后)
         * - LFG_STATE_PROPOSAL: 正在等待玩家确认进入副本
         * - LFG_STATE_DUNGEON: 玩家当前在 LFG 副本中
         * - LFG_STATE_FINISHED_DUNGEON: 玩家已完成副本,尚未传送出副本
         *
         * @return 当前的LFG状态(LfgState枚举值)
         *
         * @note 此方法为内联方法,性能开销极小
         * @note 状态由 SetState() 或 RestoreState() 改变
         * @note 状态查询是高频操作,应保持方法的轻量级
         *
         * @see SetState() - 设置新状态
         * @see GetOldState() - 获取旧状态
         */
        LfgState GetState() const;

        /**
         * @brief 获取玩家的旧LFG状态
         *
         * 返回玩家在最近一次状态转换前的状态。用于状态恢复机制。
         *
         * 典型场景:
         * - 当前状态: LFG_STATE_ROLECHECK
         * - 旧状态: LFG_STATE_QUEUED
         * - 如果角色检查失败,调用 RestoreState() 恢复到 QUEUED
         *
         * 状态保存机制:
         * - 每次 SetState() 调用都会保存旧状态
         * - RestoreState() 会使用此旧状态进行恢复
         * - 初始状态下,旧状态为 LFG_STATE_NONE
         *
         * @return 旧的LFG状态(LfgState枚举值)
         *
         * @note 此方法主要用于调试和日志记录
         * @note 正常流程中不需要手动查询旧状态
         * @note 如果从未调用过 SetState(),返回 LFG_STATE_NONE
         *
         * @see RestoreState() - 使用旧状态恢复当前状态
         * @see SetState() - 设置新状态时保存旧状态
         */
        LfgState GetOldState() const;

        /**
         * @brief 获取玩家的阵营
         *
         * 返回玩家所属的阵营(联盟或部落)。阵营决定了玩家可以匹配的队伍。
         *
         * 阵营的作用:
         * - LFG 系统不会将不同阵营的玩家匹配到同一队伍
         * - 某些副本有阵营限制(如阵营专属副本)
         * - 某些成就需要特定阵营才能完成
         *
         * @return 阵营ID
         *         - ALLIANCE (0): 联盟
         *         - HORDE (1): 部落
         *
         * @note 此方法为内联方法,性能开销极小
         * @note 阵营由 SetTeam() 设置,通常在玩家初始化时设置一次
         * @note 阵营在游戏中不会改变
         *
         * @see SetTeam() - 设置玩家阵营
         */
        uint8 GetTeam() const;

        /**
         * @brief 获取玩家所在的队伍GUID
         *
         * 返回玩家加入 LFG 时的原始队伍 GUID。这是恢复机制的关键数据。
         *
         * 返回值含义:
         * - **有效GUID**: 玩家加入 LFG 时在队伍中,这是原队伍的 GUID
         *   - 场景: 预组队(2-5人小队一起加入随机副本)
         *   - 用途: 副本完成后恢复原队伍关系
         *
         * - **空GUID (ObjectGuid::Empty)**: 玩家独自加入 LFG
         *   - 场景: 单人排队
         *   - 用途: 副本完成后临时队伍解散,玩家回到单人状态
         *
         * GUID 生命周期:
         * 1. 玩家加入 LFG 队列 -> SetGroup(当前队伍GUID或空GUID)
         * 2. 匹配成功,系统可能创建临时队伍 -> GUID 不变
         * 3. 完成副本,传送出副本 -> 使用 GUID 恢复队伍状态
         *
         * @return 队伍的GUID
         *         - 有效GUID: 玩家有原始队伍
         *         - 空GUID: 玩家独自加入
         *
         * @note 此方法为内联方法,性能开销极小
         * @note 返回的 GUID 是值拷贝,不是引用(ObjectGuid 是轻量级对象)
         * @note 在 LFG 流程中,GUID 保持不变
         *
         * @see SetGroup() - 设置原始队伍GUID
         * @see Group - 队伍类定义
         */
        ObjectGuid GetGroup() const;

        /** @} */

        /**
         * @name 队列获取方法
         * @{
         */

        /**
         * @brief 获取玩家选择的角色定位
         *
         * 返回玩家在 LFG 队列中选择的角色定位标志位。
         * 返回值是位标志的组合,可以使用位运算判断包含的角色。
         *
         * 返回值解析示例:
         * @code
         * uint8 roles = playerData.GetRoles();
         * bool isTank = (roles & ROLE_TANK) != 0;
         * bool isHealer = (roles & ROLE_HEALER) != 0;
         * bool isDamage = (roles & ROLE_DAMAGE) != 0;
         *
         * // 判断是否选择了多个角色
         * bool isMultiRole = (roles & (ROLE_TANK | ROLE_HEALER)) != 0;
         * @endcode
         *
         * 典型返回值:
         * - 0x01 (ROLE_TANK): 仅坦克
         * - 0x02 (ROLE_HEALER): 仅治疗
         * - 0x04 (ROLE_DAMAGE): 仅输出
         * - 0x03 (ROLE_TANK | ROLE_HEALER): 坦克+治疗
         * - 0x05 (ROLE_TANK | ROLE_DAMAGE): 坦克+输出
         * - 0x07 (全部): 可切换任意角色
         *
         * @return 角色定位标志位组合(位掩码)
         *         - ROLE_NONE (0x00): 未选择
         *         - ROLE_TANK (0x01): 包含坦克
         *         - ROLE_HEALER (0x02): 包含治疗
         *         - ROLE_DAMAGE (0x04): 包含输出
         *         - ROLE_LEADER (0x08): 队长标志
         *
         * @note 此方法为内联方法,性能开销极小
         * @note 角色定位由玩家在客户端 LFG 界面选择
         * @note 系统会根据职业专精过滤不合法的角色选择
         *
         * @see SetRoles() - 设置角色定位
         * @see Player::GetSpecialization() - 获取玩家专精
         */
        uint8 GetRoles() const;

        /**
         * @brief 获取玩家的备注信息
         *
         * 返回玩家设置的备注信息的常量引用。备注会显示给其他队伍成员。
         *
         * 返回值说明:
         * - 返回 const 引用,避免字符串拷贝,性能优化
         * - 空字符串表示玩家未设置备注
         * - 字符串为 UTF-8 编码,支持多语言字符
         *
         * 使用场景:
         * - 角色检查阶段显示给所有队伍成员
         * - 日志记录,用于追踪问题
         * - 统计分析玩家行为
         *
         * 性能考虑:
         * - 返回常量引用,无内存拷贝开销
         * - 不应在高频率循环中反复调用(虽然开销小)
         * - 如需长期保存,应复制字符串而非保存引用
         *
         * @return 备注字符串的常量引用
         *         - 空字符串: 无备注
         *         - 非空字符串: 玩家设置的备注内容
         *
         * @note 返回的引用仅在对象生命周期内有效
         * @note 修改备注需要通过 SetComment() 方法
         * @note 客户端会对备注长度进行限制(通常255字符)
         *
         * @see SetComment() - 设置备注信息
         */
        std::string const& GetComment() const;

        /**
         * @brief 获取玩家选择的副本列表
         *
         * 返回玩家选择加入的副本 ID 集合的常量引用。
         * 这是 LFG 匹配的核心数据,决定了玩家会被匹配到哪个副本。
         *
         * 返回值说明:
         * - 返回 const 引用,避免集合拷贝,性能优化
         * - LfgDungeonSet 是 std::set<uint32> 的别名
         * - 每个元素是一个副本的数据库 ID
         * - 集合已按 ID 排序(std::set 的特性)
         *
         * 集合大小说明:
         * - 空集合 (size == 0): 玩家未选择任何副本,不应加入队列
         * - 单一副本 (size == 1): 玩家指定了特定副本
         * - 多个副本 (size > 1): 玩家愿意进入多个副本中的一个
         * - 随机副本: 通常也是一个特殊的副本 ID
         *
         * 使用示例:
         * @code
         * LfgDungeonSet const& dungeons = playerData.GetSelectedDungeons();
         * for (uint32 dungeonId : dungeons)
         * {
         *     // 处理每个副本 ID
         *     DungeonEntry const* entry = sDungeonStore.LookupEntry(dungeonId);
         *     if (entry)
         *         ProcessDungeon(entry);
         * }
         * @endcode
         *
         * 性能考虑:
         * - 返回常量引用,无集合拷贝开销
         * - 集合大小通常较小(1-10个副本)
         * - 迭代复杂度为 O(n),n 为副本数量
         *
         * @return 副本ID集合的常量引用
         *         - 空集合: 未选择副本
         *         - 非空集合: 选择的副本列表
         *
         * @note 返回的引用仅在对象生命周期内有效
         * @note 修改副本列表需要通过 SetSelectedDungeons() 方法
         * @note 副本 ID 对应 DbcStorage 中的 DungeonEntry
         *
         * @see SetSelectedDungeons() - 设置副本列表
         * @see LFGMgr::Join() - 加入队列时设置副本
         */
        LfgDungeonSet const& GetSelectedDungeons() const;

        /** @} */

        /**
         * @name 成就相关方法
         * @{
         */

        /**
         * @brief 设置玩家加入LFG时的队伍成员数量
         *
         * 记录玩家加入 LFG 队列时的队伍规模,用于成就判定和奖励计算。
         * 这是"随机副本"成就系统的重要数据。
         *
         * 记录时机:
         * - 玩家加入 LFG 队列时立即记录
         * - 即使后续有新成员加入,此值也不会改变
         * - 用于区分"预组队"和"随机匹配"场景
         *
         * 典型值:
         * - 0: 初始化值,表示未设置
         * - 1: 单人排队(玩家自己)
         * - 2-4: 小队排队(预组部分成员)
         * - 5: 满队伍排队(预组完整队伍)
         *
         * 成就应用示例:
         * - "随机副本新手": 单人完成随机副本
         * - "团队玩家": 以5人预组队伍完成随机副本
         * - "带小号": 队伍中有低等级玩家
         *
         * 奖励机制:
         * - 满队伍(5人): 可能获得额外奖励
         * - 随机匹配(单人): 优先匹配其他单人玩家
         * - 小队(2-4人): 需要补充其他玩家
         *
         * @param count 队伍成员数量
         *              - 1: 单人排队
         *              - 2-5: 预组队伍规模
         *
         * @note 此值在 LFG 流程中不会改变
         * @note 即使队伍成员中途离开,此值仍为原始数量
         * @note 用于 GetNumberOfPartyMembersAtJoin() 查询
         *
         * @see GetNumberOfPartyMembersAtJoin() - 获取队伍成员数量
         * @see Player::GetGroup() - 获取玩家所在的队伍对象
         */
        void SetNumberOfPartyMembersAtJoin(uint8 count);

        /**
         * @brief 获取玩家加入LFG时的队伍成员数量
         *
         * 返回玩家加入 LFG 队列时的原始队伍规模,用于成就判定和奖励计算。
         * 这是一个快照值,在整个 LFG 流程中保持不变。
         *
         * 返回值含义:
         * - 0: 未设置(初始值)
         * - 1: 单人排队
         *   - 成就示例: "独狼玩家" - 单人完成随机副本
         * - 2-4: 小队排队(部分预组)
         *   - 场景: 朋友一起排队,但不是满队伍
         *   - 系统会补充其他玩家填补空缺
         * - 5: 满队伍排队
         *   - 成就示例: "公会团" - 以完整公会队伍完成副本
         *   - 优势: 队伍配合默契,效率更高
         *
         * 使用场景:
         * 1. **成就判定**: 判断玩家是否满足特定成就条件
         *    @code
         *    uint8 memberCount = playerData.GetNumberOfPartyMembersAtJoin();
         *    if (memberCount == 1)
         *        AwardAchievement(player, ACHIEVEMENT_LONE_WOLF);
         *    else if (memberCount == 5)
         *        AwardAchievement(player, ACHIEVEMENT_TEAM_PLAYER);
         *    @endcode
         *
         * 2. **奖励计算**: 根据队伍规模给予不同奖励
         *    - 满队伍: 额外金币奖励
         *    - 单人: 额外正义点数
         *
         * 3. **统计分析**: 统计玩家的组队偏好
         *    - 记录玩家更喜欢单排还是组队
         *    - 用于优化匹配算法
         *
         * @return 队伍成员数量
         *         - 0: 未初始化
         *         - 1-5: 加入时的队伍规模
         *
         * @note 此值在副本完成后仍然有效,用于成就判定
         * @note 返回的是快照值,不反映当前队伍状态
         * @note 如果玩家从未加入过 LFG,返回 0
         *
         * @see SetNumberOfPartyMembersAtJoin() - 设置队伍成员数量
         */
        uint8 GetNumberOfPartyMembersAtJoin();

        /** @} */

    private:
        /**
         * @name 通用数据成员
         * @{
         */

        /**
         * @brief 当前LFG状态
         *
         * 存储玩家当前的 LFG 状态,追踪玩家在整个 LFG 流程中的位置。
         * 由 SetState() 设置,由 GetState() 查询。
         *
         * 可能的状态值(参见 LfgState 枚举):
         * - LFG_STATE_NONE: 未参与 LFG,初始状态
         * - LFG_STATE_QUEUED: 已加入队列,等待匹配
         * - LFG_STATE_ROLECHECK: 匹配成功,正在进行角色检查
         * - LFG_STATE_PROPOSAL: 角色检查通过,等待玩家确认
         * - LFG_STATE_DUNGEON: 在 LFG 副本中
         * - LFG_STATE_FINISHED_DUNGEON: 已完成副本,尚未传送出副本
         *
         * 状态转换由 LFGMgr 驱动,不是简单的线性流程:
         * - 可能从 ROLECHECK 回到 QUEUED(检查失败)
         * - 可能从 PROPOSAL 回到 QUEUED(提案被拒绝)
         *
         * @see SetState() - 设置新状态(同时保存旧状态)
         * @see RestoreState() - 恢复到旧状态
         * @see GetState() - 查询当前状态
         */
        LfgState m_State;

        /**
         * @brief 旧的LFG状态
         *
         * 存储最近一次状态转换前的状态,用于状态恢复机制。
         * 每次 SetState() 调用都会将当前 m_State 保存到 m_OldState。
         *
         * 恢复机制的使用场景:
         * - 角色检查失败: ROLECHECK -> QUEUED
         * - 提案被拒绝: PROPOSAL -> QUEUED
         * - 玩家掉线: PROPOSAL -> QUEUED
         *
         * 初始值:
         * - 构造时初始化为 LFG_STATE_NONE
         * - 如果从未调用过 SetState(),保持为 NONE
         *
         * @note RestoreState() 会使用此值恢复 m_State
         * @note 此值不会自动清除,多次 RestoreState() 会恢复到同一个旧状态
         * @see RestoreState() - 使用此值恢复当前状态
         */
        LfgState m_OldState;

        /** @} */

        /**
         * @name 玩家身份数据成员
         * @{
         */

        /**
         * @brief 玩家阵营
         *
         * 存储玩家所属的阵营(联盟或部落),决定可以加入的 LFG 队列。
         * 阵营信息用于匹配阶段的过滤和验证。
         *
         * 可能的值:
         * - ALLIANCE (0): 联盟阵营
         * - HORDE (1): 部落阵营
         *
         * 作用:
         * - 匹配限制: 不同阵营的玩家不会被匹配到同一队伍
         * - 副本过滤: 某些副本有阵营限制
         * - UI显示: 客户端显示对应阵营的副本列表
         *
         * @note 阵营在玩家创建时确定,游戏过程中不会改变
         * @note 通常在玩家对象初始化时设置一次
         */
        uint8 m_Team;

        /**
         * @brief 玩家加入LFG时的原始队伍GUID
         *
         * 存储玩家加入 LFG 队列时所在的队伍 GUID,用于队伍恢复机制。
         * 这是支持"预组队"功能的关键数据。
         *
         * 值的含义:
         * - 有效 GUID: 玩家在队伍中加入 LFG
         *   - 场景: 2-5人小队一起排队
         *   - 用途: 副本完成后恢复原队伍关系
         * - 空 GUID (ObjectGuid::Empty): 玩家独自加入
         *   - 场景: 单人排队
         *   - 用途: 副本完成后临时队伍解散
         *
         * 生命周期:
         * 1. 加入 LFG -> SetGroup() 记录当前队伍
         * 2. 匹配成功 -> 可能转移到临时队伍,m_Group 不变
         * 3. 完成副本 -> 使用 m_Group 恢复队伍状态
         *
         * @note 即使在 LFG 流程中玩家被移动到临时队伍,此值保持不变
         * @see SetGroup() - 设置原始队伍GUID
         * @see GetGroup() - 获取原始队伍GUID
         */
        ObjectGuid m_Group;

        /** @} */

        /**
         * @name 队列配置数据成员
         * @{
         */

        /**
         * @brief 玩家选择的角色定位
         *
         * 存储玩家在 LFG 界面中选择的角色定位标志位。
         * 使用位标志设计,支持多角色选择。
         *
         * 位标志定义:
         * - bit 0 (0x01): ROLE_TANK - 坦克
         * - bit 1 (0x02): ROLE_HEALER - 治疗
         * - bit 2 (0x04): ROLE_DAMAGE - 输出
         * - bit 3 (0x08): ROLE_LEADER - 队长(可与其他角色组合)
         *
         * 常见组合:
         * - 0x01: 仅坦克
         * - 0x03: 坦克 + 治疗
         * - 0x05: 坦克 + 输出
         * - 0x07: 所有角色(全能型)
         *
         * 作用:
         * - 匹配算法: 队伍需要 1坦克 + 1治疗 + 3输出
         * - 角色检查: 验证队伍角色分配是否合理
         * - 奖励机制: 选择稀缺角色(坦克/治疗)可能获得额外奖励
         *
         * @note 玩家可以选择多个角色,系统会根据队伍需求分配
         * @note 角色定位由玩家手动选择,系统会根据职业专精提供建议
         */
        uint8 m_Roles;

        /**
         * @brief 玩家的备注信息
         *
         * 存储玩家在 LFG 界面中填写的备注文本,会显示给其他队伍成员。
         * 用于表达偏好、要求或自我介绍。
         *
         * 特性:
         * - UTF-8 编码,支持多语言字符
         * - 长度限制: 客户端通常限制为 255 字符
         * - 内容: 纯文本,不支持格式化
         *
         * 常见内容:
         * - "首次尝试,请多指教"
         * - "速通,有经验优先"
         * - "完成任务: [任务名称]"
         *
         * @note 使用 std::string 存储,采用值语义
         * @note 空字符串表示玩家未填写备注
         * @note GetComment() 返回常量引用以避免拷贝
         */
        std::string m_Comment;

        /**
         * @brief 玩家选择加入的副本列表
         *
         * 存储玩家选择的副本 ID 集合,是 LFG 匹配的核心数据。
         * 使用 std::set<uint32> 存储,自动排序且保证唯一性。
         *
         * 数据结构:
         * - LfgDungeonSet 是 std::set<uint32> 的类型别名
         * - 每个元素是一个副本的数据库 ID (对应 DbcStore 中的 DungeonEntry)
         * - 集合自动排序(按 ID 升序)
         *
         * 集合内容:
         * - 空集合: 玩家未选择副本(不应加入队列)
         * - 单一副本: 指定特定副本
         * - 多个副本: 玩家愿意进入其中任意一个
         * - 随机副本: 使用特殊的 ID 表示"随机"选项
         *
         * 匹配逻辑:
         * - LFG 管理器会尝试匹配集合中的副本
         * - 优先级: 通常优先匹配列表中的第一个副本
         * - 最终决定: 由 LFGMgr::ProposalUpdate() 确定
         *
         * @note 使用集合保证副本 ID 唯一,避免重复选择
         * @note GetSelectedDungeons() 返回常量引用以避免拷贝
         * @note 集合大小通常为 1-10 个副本
         */
        LfgDungeonSet m_SelectedDungeons;

        /** @} */

        /**
         * @name 成就相关数据成员
         * @{
         */

        /**
         * @brief 加入LFG时的队伍成员数量
         *
         * 存储玩家加入 LFG 队列时的队伍规模快照,用于成就判定和奖励计算。
         * 这是一个快照值,在 LFG 流程中不会改变。
         *
         * 可能的值:
         * - 0: 初始值,表示未设置
         * - 1: 单人排队
         * - 2-4: 小队排队(部分预组)
         * - 5: 满队伍排队(完整预组)
         *
         * 应用场景:
         * - 成就判定: "以特定数量的队伍成员完成副本"
         * - 奖励计算: 满队伍可能获得额外奖励
         * - 统计分析: 追踪玩家的组队偏好
         *
         * 设置时机:
         * - 在玩家调用 LFGMgr::Join() 时记录
         * - 即使后续队伍成员变化,此值也不变
         *
         * @note 这是一个快照值,不反映当前队伍状态
         * @note 用于副本完成后的成就判定
         * @see SetNumberOfPartyMembersAtJoin() - 设置此值
         * @see GetNumberOfPartyMembersAtJoin() - 查询此值
         */
        uint8 m_NumberOfPartyMembersAtJoin;

        /** @} */
};

} // namespace lfg

#endif
