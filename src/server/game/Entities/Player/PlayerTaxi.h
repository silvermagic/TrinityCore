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
 * @file PlayerTaxi.h
 * @brief 玩家飞行坐骑系统管理模块
 *
 * 本模块负责管理玩家的飞行坐骑系统，包括：
 * - 出租车节点的解锁和存储（玩家已知的飞行点）
 * - 飞行路径的管理（多段飞行路线）
 * - 飞行数据的序列化与反序列化
 * - 飞行状态查询
 *
 * 主要功能：
 * - 管理玩家已解锁的出租车节点（飞行点）
 * - 管理当前正在进行的飞行路线
 * - 处理跨阵营飞行时飞行管理员的阵营信息
 *
 * 出租车系统：
 * 在魔兽世界中，玩家可以通过与飞行管理员交互，在各个已解锁的飞行点之间飞行。
 * 每个飞行点称为一个"出租车节点"（Taxi Node）。
 */

#ifndef PlayerTaxi_h__
#define PlayerTaxi_h__

#include "DBCEnums.h"
#include "Define.h"
#include <deque>
#include <iosfwd>
#include <string>

class ByteBuffer;
struct FactionTemplateEntry;

/**
 * @brief 玩家出租车系统管理类
 *
 * 管理玩家的飞行坐骑相关数据，包括已解锁的飞行点和当前飞行路线。
 *
 * 核心概念：
 * - 出租车节点（Taxi Node）：游戏中的飞行点，每个节点有唯一ID
 * - 出租车掩码（Taxi Mask）：位掩码数组，每位代表一个节点是否已解锁
 * - 飞行路线（Taxi Destinations）：飞行路径上的节点队列
 *
 * 主要职责：
 * - 管理玩家已解锁的飞行点（掩码存储）
 * - 管理当前正在进行的飞行路线（目的地队列）
 * - 初始化种族/职业/等级相关的默认飞行点
 * - 序列化/反序列化飞行数据用于数据库存储
 * - 提供飞行状态查询接口
 */
class TC_GAME_API PlayerTaxi
{
    public:
        /**
         * @brief 构造函数
         *
         * 初始化飞行管理器阵营ID为0，所有飞行节点掩码清零
         */
        PlayerTaxi() : m_flightMasterFactionId(0) { m_taximask.fill(0); }

        /**
         * @brief 析构函数
         */
        ~PlayerTaxi() { }

        // ==================== 节点管理 ====================

        /**
         * @brief 初始化玩家的出租车节点
         * @param race 种族ID
         * @param chrClass 职业ID
         * @param level 等级
         *
         * 根据种族、职业和等级初始化玩家已知的飞行点。
         * - 死亡骑士：解锁所有旧大陆飞行点
         * - 各种族：解锁对应主城和起始区域的飞行点
         * - 等级68+：解锁破碎之日暂存区飞行点
         *
         * @note 调用时机：创建新角色或重置玩家数据时
         */
        void InitTaxiNodesForLevel(uint32 race, uint32 chrClass, uint8 level);

        /**
         * @brief 从字符串加载飞行节点掩码
         * @param data 空格分隔的节点掩码字符串
         * @return 加载成功返回true，有警告返回false
         *
         * 从数据库加载玩家的飞行节点数据。
         * 只加载现有节点，忽略无效节点。
         *
         * @note 调用时机：从数据库加载玩家数据时
         */
        bool LoadTaxiMask(std::string const& data);

        /**
         * @brief 检查飞行节点是否已知
         * @param nodeidx 节点索引（从1开始）
         * @return 已知返回true，否则返回false
         *
         * 使用位掩码检查指定飞行点是否已解锁。
         * 节点索引转掩码公式：field = (idx-1)/32, bit = (idx-1)%32
         */
        bool IsTaximaskNodeKnown(uint32 nodeidx) const
        {
            uint8  field   = uint8((nodeidx - 1) / 32);      // 计算字段索引
            uint32 submask = 1 << ((nodeidx-1) % 32);        // 计算位掩码
            return (m_taximask[field] & submask) == submask; // 位与运算检查
        }

        /**
         * @brief 设置飞行节点为已知
         * @param nodeidx 节点索引（从1开始）
         * @return 新设置返回true，已存在返回false
         *
         * 将指定飞行点设置为已解锁状态。
         */
        bool SetTaximaskNode(uint32 nodeidx)
        {
            uint8  field   = uint8((nodeidx - 1) / 32);
            uint32 submask = 1 << ((nodeidx - 1) % 32);
            if ((m_taximask[field] & submask) != submask)
            {
                m_taximask[field] |= submask;  // 位或运算设置
                return true;
            }
            else
                return false;
        }

        /**
         * @brief 将飞行节点掩码写入字节缓冲区
         * @param data 目标字节缓冲区
         * @param all true=写入所有现有节点，false=只写入已知节点
         *
         * 用于向客户端发送飞行节点数据。
         */
        void AppendTaximaskTo(ByteBuffer& data, bool all);

        // ==================== 目的地管理 ====================

        /**
         * @brief 从字符串加载飞行目的地列表
         * @param values 空格分隔的目的地字符串（阵营ID + 节点列表）
         * @param team 阵营（联盟/部落）
         * @return 加载成功返回true，失败返回false
         *
         * 从数据库恢复飞行中的路线。
         * 字符串格式：飞行管理员阵营ID 节点1 节点2 ...
         * 会验证路径完整性。
         *
         * @note 调用时机：玩家登录时恢复飞行状态
         */
        [[nodiscard]] bool LoadTaxiDestinationsFromString(std::string const& values, uint32 team);

        /**
         * @brief 将飞行目的地列表保存为字符串
         * @return 空格分隔的目的地字符串
         *
         * 将当前飞行路线序列化为字符串，用于数据库存储。
         * 格式：飞行管理员阵营ID 节点1 节点2 ...
         */
        std::string SaveTaxiDestinationsToString();

        /**
         * @brief 清空飞行目的地列表
         */
        void ClearTaxiDestinations() { m_TaxiDestinations.clear(); }

        /**
         * @brief 添加飞行目的地
         * @param dest 目的地节点ID
         */
        void AddTaxiDestination(uint32 dest) { m_TaxiDestinations.push_back(dest); }

        /**
         * @brief 获取飞行起点
         * @return 起点节点ID，无目的地返回0
         */
        uint32 GetTaxiSource() const { return m_TaxiDestinations.empty() ? 0 : m_TaxiDestinations.front(); }

        /**
         * @brief 获取下一个飞行目的地
         * @return 下一个目的地节点ID，无目的地返回0
         */
        uint32 GetTaxiDestination() const { return m_TaxiDestinations.size() < 2 ? 0 : m_TaxiDestinations[1]; }

        /**
         * @brief 获取当前飞行路径ID
         * @return 路径ID，无有效路径返回0
         *
         * 获取从当前点到下一点的路径ID，用于查找飞行路线。
         */
        uint32 GetCurrentTaxiPath() const;

        /**
         * @brief 前进到下一个飞行目的地
         * @return 新的目的地节点ID
         *
         * 移除当前节点，返回新的下一个目的地。
         * 用于飞行过程中的路径推进。
         */
        uint32 NextTaxiDestination()
        {
            m_TaxiDestinations.pop_front();  // 移除当前节点
            return GetTaxiDestination();     // 返回新目的地
        }

        /**
         * @brief 获取飞行路径
         * @return 目的地队列的常量引用
         */
        std::deque<uint32> const& GetPath() const { return m_TaxiDestinations; }

        /**
         * @brief 检查是否有飞行目的地
         * @return 有目的地返回true，否则返回false
         */
        bool empty() const { return m_TaxiDestinations.empty(); }

        /**
         * @brief 获取飞行管理员的阵营模板
         * @return 阵营模板条目指针，无效返回nullptr
         *
         * 用于确定飞行时的阵营关系（如跨阵营飞行）。
         */
        FactionTemplateEntry const* GetFlightMasterFactionTemplate() const;

        /**
         * @brief 设置飞行管理员阵营模板ID
         * @param factionTemplateId 阵营模板ID
         */
        void SetFlightMasterFactionTemplateId(uint32 factionTemplateId) { m_flightMasterFactionId = factionTemplateId; }

        /**
         * @brief 输出运算符重载
         * @param ss 输出字符串流
         * @param taxi 玩家出租车对象
         * @return 输出流的引用
         *
         * 将飞行节点掩码输出到字符串流，用于数据库存储。
         */
        friend std::ostringstream& operator<<(std::ostringstream& ss, PlayerTaxi const& taxi);

    private:
        TaxiMask m_taximask;              ///< 飞行节点掩码数组，每位代表一个节点是否已解锁
        std::deque<uint32> m_TaxiDestinations;  ///< 飞行目的地队列，存储飞行路线上的节点ID
        uint32 m_flightMasterFactionId;   ///< 飞行管理员阵营模板ID，用于跨阵营飞行
};

/**
 * @brief 输出运算符重载
 * @param ss 输出字符串流
 * @param taxi 玩家出租车对象
 * @return 输出流的引用
 *
 * 将飞行节点掩码序列化为空格分隔的字符串。
 */
std::ostringstream& operator<<(std::ostringstream& ss, PlayerTaxi const& taxi);

#endif // PlayerTaxi_h__
