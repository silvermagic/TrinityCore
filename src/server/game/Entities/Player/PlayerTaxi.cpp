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
 * @file PlayerTaxi.cpp
 * @brief 玩家飞行坐骑系统管理模块实现
 *
 * 本文件实现了玩家飞行系统的核心功能，包括：
 * - 飞行节点的初始化和加载
 * - 飞行路线的管理和序列化
 * - 飞行路径的查询和处理
 */

#include "PlayerTaxi.h"
#include "DBCStores.h"
#include "ObjectMgr.h"
#include "Player.h"
#include "StringConvert.h"
#include <sstream>

/**
 * @brief 初始化玩家的出租车节点
 * @param race 种族ID
 * @param chrClass 职业ID
 * @param level 等级
 *
 * 根据种族、职业和等级设置玩家初始已知的飞行点。
 *
 * 处理流程：
 * 1. 职业特殊处理：死亡骑士解锁所有旧大陆飞行点
 * 2. 种族主城飞行点：根据种族解锁对应主城飞行点
 * 3. 新大陆起始点：根据阵营解锁对应起始飞行点
 * 4. 等级限制点：高等级解锁特殊飞行点
 */
void PlayerTaxi::InitTaxiNodesForLevel(uint32 race, uint32 chrClass, uint8 level)
{
    // 职业特定初始已知节点
    switch (chrClass)
    {
        case CLASS_DEATH_KNIGHT:
        {
            // 死亡骑士解锁所有旧大陆飞行点
            for (uint8 i = 0; i < TaxiMaskSize; ++i)
                m_taximask[i] |= sOldContinentsNodesMask[i];
            break;
        }
    }

    // 种族特定初始已知节点：主城和飞行枢纽掩码
    switch (race)
    {
        case RACE_HUMAN:    SetTaximaskNode(2);  break;     // 人类 - 暴风城
        case RACE_ORC:      SetTaximaskNode(23); break;     // 兽人 - 奥格瑞玛
        case RACE_DWARF:    SetTaximaskNode(6);  break;     // 矮人 - 铁炉堡
        case RACE_NIGHTELF: SetTaximaskNode(26);
            SetTaximaskNode(27); break;     // 暗夜精灵 - 达纳苏斯
        case RACE_UNDEAD_PLAYER: SetTaximaskNode(11); break;// 被遗忘者 - 幽暗城
        case RACE_TAUREN:   SetTaximaskNode(22); break;     // 牛头人 - 雷霆崖
        case RACE_GNOME:    SetTaximaskNode(6);  break;     // 侏儒 - 铁炉堡
        case RACE_TROLL:    SetTaximaskNode(23); break;     // 巨魔 - 奥格瑞玛
        case RACE_BLOODELF: SetTaximaskNode(82); break;     // 血精灵 - 银月城
        case RACE_DRAENEI:  SetTaximaskNode(94); break;     // 德莱尼 - 埃索达
    }

    // 新大陆起始掩码（只能在新地图访问）
    // 联盟和部落有不同的起始飞行点
    switch (Player::TeamForRace(race))
    {
        case ALLIANCE: SetTaximaskNode(100); break;  // 联盟起始点
        case HORDE:    SetTaximaskNode(99);  break;  // 部落起始点
    }

    // 等级相关的飞行枢纽
    if (level >= 68)
        SetTaximaskNode(213);                               // 破碎之日暂存区（奎尔丹纳斯岛）
}

/**
 * @brief 从字符串加载飞行节点掩码
 * @param data 空格分隔的节点掩码字符串
 * @return 加载成功无警告返回true，有警告返回false
 *
 * 从数据库加载玩家的飞行节点数据。
 * 字符串格式：掩码0 掩码1 掩码2 ...（每个掩码是uint32的十进制值）
 *
 * 处理逻辑：
 * - 只加载有效的飞行节点（与全局掩码做与运算）
 * - 如果输入掩码包含无效节点，会产生警告
 * - 如果解析失败，该位置设置为0并产生警告
 */
bool PlayerTaxi::LoadTaxiMask(std::string const& data)
{
    bool warn = false;  // 警告标志
    // 将字符串按空格分割为token列表
    std::vector<std::string_view> tokens = Trinity::Tokenize(data, ' ', false);

    // 遍历每个掩码字段
    for (uint8 index = 0; (index < TaxiMaskSize) && (index < tokens.size()); ++index)
    {
        // 尝试将字符串转换为uint32
        if (Optional<uint32> mask = Trinity::StringTo<uint32>(tokens[index]))
        {
            // 加载并只设置现有飞行节点的位
            // 与全局节点掩码做与运算，过滤掉不存在的节点
            m_taximask[index] = sTaxiNodesMask[index] & *mask;
            if (m_taximask[index] != *mask)
                warn = true;  // 输入包含无效节点，产生警告
        }
        else
        {
            // 解析失败，设置为0
            m_taximask[index] = 0;
            warn = true;
        }
    }
    return !warn;
}

/**
 * @brief 将飞行节点掩码写入字节缓冲区
 * @param data 目标字节缓冲区
 * @param all true=写入所有现有节点，false=只写入已知节点
 *
 * 用于构建发送给客户端的数据包。
 * - all=true：发送所有可用的飞行节点（用于显示地图）
 * - all=false：只发送玩家已解锁的飞行节点
 */
void PlayerTaxi::AppendTaximaskTo(ByteBuffer& data, bool all)
{
    if (all)
    {
        // 写入所有现有的飞行节点（全局掩码）
        for (uint8 i = 0; i < TaxiMaskSize; ++i)
            data << uint32(sTaxiNodesMask[i]);              // 所有现有节点
    }
    else
    {
        // 写入玩家已知的飞行节点
        for (uint8 i = 0; i < TaxiMaskSize; ++i)
            data << uint32(m_taximask[i]);                  // 已知节点
    }
}

/**
 * @brief 从字符串加载飞行目的地列表
 * @param values 空格分隔的目的地字符串（阵营ID + 节点列表）
 * @param team 阵营（联盟/部落），用于验证飞行坐骑
 * @return 加载成功返回true，失败返回false
 *
 * 从数据库恢复飞行中的路线。
 * 字符串格式：<飞行管理员阵营ID> <节点1> <节点2> ...
 *
 * 处理流程：
 * 1. 清空现有目的地列表
 * 2. 解析飞行管理员阵营ID
 * 3. 解析所有飞行节点
 * 4. 验证路径完整性（每段路径必须存在）
 * 5. 验证飞行坐骑是否存在
 *
 * @note 调用时机：玩家登录时恢复飞行状态
 */
bool PlayerTaxi::LoadTaxiDestinationsFromString(const std::string& values, uint32 team)
{
    // 清空现有目的地列表
    ClearTaxiDestinations();

    // 按空格分割字符串
    std::vector<std::string_view> tokens = Trinity::Tokenize(values, ' ', false);
    auto itr = tokens.begin();

    // 第一个token是飞行管理员阵营ID
    if (itr != tokens.end())
    {
        if (Optional<uint32> faction = Trinity::StringTo<uint32>(*itr))
            m_flightMasterFactionId = *faction;
        else
            return false;  // 阵营ID解析失败
    }
    else
        return false;  // 空字符串

    // 解析后续的飞行节点
    while ((++itr) != tokens.end())
    {
        if (Optional<uint32> node = Trinity::StringTo<uint32>(*itr))
            AddTaxiDestination(*node);
        else
            return false;  // 节点ID解析失败
    }

    // 如果没有目的地，加载成功
    if (m_TaxiDestinations.empty())
        return true;

    // 检查完整性：目的地列表至少需要起点和终点
    if (m_TaxiDestinations.size() < 2)
        return false;

    // 验证路径完整性：检查每段路径是否存在
    for (size_t i = 1; i < m_TaxiDestinations.size(); ++i)
    {
        uint32 cost;
        uint32 path;
        sObjectMgr->GetTaxiPath(m_TaxiDestinations[i - 1], m_TaxiDestinations[i], path, cost);
        if (!path)
            return false;  // 路径不存在
    }

    // 验证飞行坐骑是否存在（任务飞行路径可能没有坐骑）
    if (!sObjectMgr->GetTaxiMountDisplayId(GetTaxiSource(), team, true))
        return false;  // 没有设置坐骑，无法加载飞行路径

    return true;
}

/**
 * @brief 将飞行目的地列表保存为字符串
 * @return 空格分隔的目的地字符串，无目的地返回空字符串
 *
 * 将当前飞行路线序列化为字符串，用于数据库存储。
 * 输出格式：<飞行管理员阵营ID> <节点1> <节点2> ...
 *
 * @note 断言：目的地列表必须为空或至少有2个节点
 */
std::string PlayerTaxi::SaveTaxiDestinationsToString()
{
    // 如果没有目的地，返回空字符串
    if (m_TaxiDestinations.empty())
        return "";

    // 断言：目的地列表至少有起点和终点
    ASSERT(m_TaxiDestinations.size() >= 2);

    // 构建输出字符串
    std::ostringstream ss;
    ss << m_flightMasterFactionId << ' ';  // 写入飞行管理员阵营ID

    // 写入所有目的地节点
    for (size_t i = 0; i < m_TaxiDestinations.size(); ++i)
        ss << m_TaxiDestinations[i] << ' ';

    return ss.str();
}

/**
 * @brief 获取当前飞行路径ID
 * @return 路径ID，无有效路径返回0
 *
 * 获取从当前起点到下一个目的地的飞行路径ID。
 * 用于查找具体的飞行路线数据（路径点、飞行时间等）。
 */
uint32 PlayerTaxi::GetCurrentTaxiPath() const
{
    // 至少需要起点和终点
    if (m_TaxiDestinations.size() < 2)
        return 0;

    uint32 path;
    uint32 cost;

    // 查询从起点到下一点的路径
    sObjectMgr->GetTaxiPath(m_TaxiDestinations[0], m_TaxiDestinations[1], path, cost);

    return path;
}

/**
 * @brief 输出运算符重载
 * @param ss 输出字符串流
 * @param taxi 玩家出租车对象
 * @return 输出流的引用
 *
 * 将飞行节点掩码序列化为空格分隔的字符串，用于数据库存储。
 * 输出格式：<掩码0> <掩码1> <掩码2> ...
 */
std::ostringstream& operator<<(std::ostringstream& ss, PlayerTaxi const& taxi)
{
    // 遍历所有掩码字段并输出
    for (uint8 i = 0; i < TaxiMaskSize; ++i)
        ss << taxi.m_taximask[i] << ' ';
    return ss;
}

/**
 * @brief 获取飞行管理员的阵营模板
 * @return 阵营模板条目指针，无效ID返回nullptr
 *
 * 从阵营模板存储中查找飞行管理员的阵营信息。
 * 用于确定飞行时的阵营关系（如跨阵营飞行时的敌对关系）。
 */
FactionTemplateEntry const* PlayerTaxi::GetFlightMasterFactionTemplate() const
{
    return sFactionTemplateStore.LookupEntry(m_flightMasterFactionId);
}
