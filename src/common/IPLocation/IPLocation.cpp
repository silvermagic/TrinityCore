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
 * @file IPLocation.cpp
 * @brief IP 地理位置查询模块实现
 *
 * 本模块提供基于 IP 地址的地理位置查询功能，通过加载 CSV 格式的 IP 数据库，
 * 实现对客户端 IP 地址的国家/地区定位。
 *
 * @details
 * 主要功能：
 * - 从 CSV 文件加载 IP 地址范围与国家/地区的映射数据
 * - 支持通过 IP 地址查询对应的地理位置信息
 * - 使用二分查找实现高效查询
 *
 * 地理数据库格式说明：
 * - 文件格式：CSV（逗号分隔值）
 * - 字段顺序：IP起始地址, IP结束地址, 国家代码, 国家名称
 * - IP 地址以 uint32 数字字符串形式存储（网络字节序）
 * - 国家代码为 ISO 3166-1 二字母代码（如 "CN", "US", "JP"）
 * - 示例数据行：
 *   "16777216","16777471","AU","Australia"
 *   "16777472","16778239","CN","China"
 *
 * 使用方法：
 * @code
 * // 加载数据库（通常在服务器启动时调用）
 * sIPLocation->Load();
 *
 * // 查询 IP 地址对应的地理位置
 * IpLocationRecord const* record = sIPLocation->GetLocationRecord("192.168.1.1");
 * if (record)
 * {
 *     std::cout << "Country: " << record->CountryName << std::endl;
 *     std::cout << "Code: " << record->CountryCode << std::endl;
 * }
 * @endcode
 *
 * 配置项：
 * - IPLocationFile: 指定 IP 地理数据库文件的路径
 *
 * @see IpLocationStore
 * @see IpLocationRecord
 */

#include "IPLocation.h"
#include "Config.h"
#include "Errors.h"
#include "IpAddress.h"
#include "Log.h"
#include "StringConvert.h"
#include "Util.h"
#include <fstream>
#include <iostream>

/**
 * @brief 构造函数
 *
 * 初始化 IP 地址定位存储对象，成员向量自动初始化为空
 */
IpLocationStore::IpLocationStore()
{
}

/**
 * @brief 析构函数
 *
 * 清理 IP 地址定位存储对象资源，自动释放向量内存
 */
IpLocationStore::~IpLocationStore()
{
}

/**
 * @brief 加载 IP 地址定位数据库
 *
 * 从配置文件指定的 CSV 文件加载 IP 地址范围到国家/地区的映射数据。
 * 加载完成后会对数据进行排序和完整性验证。
 *
 * @details
 * 处理流程：
 * 1. 清空现有的 IP 定位数据存储
 * 2. 从配置文件读取数据库文件路径（配置项：IPLocationFile）
 * 3. 检查文件是否存在并成功打开
 * 4. 逐行读取 CSV 格式的数据（每行 4 个字段）
 * 5. 清理数据：移除引号、换行符，将国家代码转为小写
 * 6. 将字符串形式的 IP 地址转换为 uint32 数字
 * 7. 将记录添加到存储容器中
 * 8. 对所有记录按 IP 起始地址排序（便于二分查找）
 * 9. 验证 IP 范围没有重叠（确保数据完整性）
 * 10. 记录加载的条目数量
 *
 * CSV 字段说明：
 * - 字段1：IP 起始地址（uint32 数字字符串）
 * - 字段2：IP 结束地址（uint32 数字字符串）
 * - 字段3：国家代码（ISO 3166-1 二字母代码）
 * - 字段4：国家名称
 *
 * @note 如果配置项 IPLocationFile 为空或文件不存在，函数将提前返回
 * @note 如果发现 IP 范围重叠，将触发断言失败
 */
void IpLocationStore::Load()
{
    _ipLocationStore.clear();
    TC_LOG_INFO("server.loading", "Loading IP Location Database...");

    // 从配置文件获取数据库文件路径
    std::string databaseFilePath = sConfigMgr->GetStringDefault("IPLocationFile", "");
    if (databaseFilePath.empty())
        return;

    // 检查文件是否存在
    std::ifstream databaseFile(databaseFilePath);
    if (!databaseFile)
    {
        TC_LOG_ERROR("server.loading", "IPLocation: No ip database file exists ({}).", databaseFilePath);
        return;
    }

    // 检查文件是否成功打开
    if (!databaseFile.is_open())
    {
        TC_LOG_ERROR("server.loading", "IPLocation: Ip database file ({}) can not be opened.", databaseFilePath);
        return;
    }

    // 定义 CSV 字段变量
    std::string ipFrom;
    std::string ipTo;
    std::string countryCode;
    std::string countryName;

    // 逐行读取 CSV 文件
    while (databaseFile.good())
    {
        // 读取各字段，以逗号分隔
        if (!std::getline(databaseFile, ipFrom, ','))
            break;
        if (!std::getline(databaseFile, ipTo, ','))
            break;
        if (!std::getline(databaseFile, countryCode, ','))
            break;
        if (!std::getline(databaseFile, countryName, '\n'))
            break;

        // 移除换行符和回车符
        countryName.erase(std::remove(countryName.begin(), countryName.end(), '\r'), countryName.end());
        countryName.erase(std::remove(countryName.begin(), countryName.end(), '\n'), countryName.end());

        // 移除引号（CSV 格式中可能包含引号）
        ipFrom.erase(std::remove(ipFrom.begin(), ipFrom.end(), '"'), ipFrom.end());
        ipTo.erase(std::remove(ipTo.begin(), ipTo.end(), '"'), ipTo.end());
        countryCode.erase(std::remove(countryCode.begin(), countryCode.end(), '"'), countryCode.end());
        countryName.erase(std::remove(countryName.begin(), countryName.end(), '"'), countryName.end());

        // 将国家代码转换为小写
        strToLower(countryCode);

        // 将字符串形式的 IP 地址转换为 uint32 数字
        Optional<uint32> from = Trinity::StringTo<uint32>(ipFrom);
        if (!from)
            continue;

        Optional<uint32> to = Trinity::StringTo<uint32>(ipTo);
        if (!to)
            continue;

        // 将记录添加到存储容器
        _ipLocationStore.emplace_back(*from, *to, std::move(countryCode), std::move(countryName));
    }

    // 按 IP 起始地址排序，便于后续二分查找
    std::sort(_ipLocationStore.begin(), _ipLocationStore.end(), [](IpLocationRecord const& a, IpLocationRecord const& b) { return a.IpFrom < b.IpFrom; });

    // 验证 IP 范围没有重叠，确保数据完整性
    ASSERT(std::is_sorted(_ipLocationStore.begin(), _ipLocationStore.end(), [](IpLocationRecord const& a, IpLocationRecord const& b) { return a.IpFrom < b.IpTo; }),
        "Overlapping IP ranges detected in database file");

    databaseFile.close();

    TC_LOG_INFO("server.loading", ">> Loaded {} ip location entries.", _ipLocationStore.size());
}

/**
 * @brief 根据 IP 地址查询地理位置记录
 *
 * 在已加载的 IP 定位数据库中查找给定 IP 地址对应的国家/地区信息。
 * 使用二分查找算法实现高效查询，时间复杂度 O(log n)。
 *
 * @param ipAddress IP 地址字符串，格式如 "192.168.1.1" 或 "8.8.8.8"
 *
 * @return 返回指向 IpLocationRecord 的常量指针，包含 IP 范围和国家信息
 *         如果未找到匹配记录或 IP 地址格式无效，返回 nullptr
 *
 * @details
 * 查询算法：
 * 1. 将 IP 地址字符串转换为 boost::asio::ip::address_v4 对象
 * 2. 将 IPv4 地址转换为 uint32 数字（网络字节序）
 * 3. 使用 upper_bound 二分查找第一个 IpTo > ip 的记录
 * 4. 验证 ip 是否在记录的 IP 范围内（IpFrom <= ip <= IpTo）
 * 5. 匹配成功则返回记录指针，否则返回 nullptr
 *
 * @note 此函数线程安全，可在多线程环境中调用
 * @note 必须先调用 Load() 加载数据库，否则始终返回 nullptr
 *
 * @see Load()
 * @see IpLocationRecord
 */
IpLocationRecord const* IpLocationStore::GetLocationRecord(std::string const& ipAddress) const
{
    // 将字符串形式的 IP 地址转换为 IPv4 地址对象
    boost::system::error_code error;
    boost::asio::ip::address_v4 address = Trinity::Net::make_address_v4(ipAddress, error);
    if (error)
        return nullptr;

    // 将 IPv4 地址转换为 uint32 数字
    uint32 ip = Trinity::Net::address_to_uint(address);

    // 使用二分查找定位 IP 所在的记录
    // 查找第一个 IpTo > ip 的记录
    auto itr = std::upper_bound(_ipLocationStore.begin(), _ipLocationStore.end(), ip, [](uint32 ip, IpLocationRecord const& loc) { return ip < loc.IpTo; });
    if (itr == _ipLocationStore.end())
        return nullptr;

    // 验证 IP 是否在记录的范围内
    if (ip < itr->IpFrom)
        return nullptr;

    return &(*itr);
}

/**
 * @brief 获取单例实例
 *
 * 实现 Meyer's Singleton 模式，返回 IpLocationStore 的全局唯一实例。
 * C++11 保证静态局部变量的初始化是线程安全的。
 *
 * @return 返回指向 IpLocationStore 单例实例的指针
 *
 * @details
 * 使用方法：
 * @code
 * // 直接通过单例访问
 * sIPLocation->Load();
 *
 * // 或者调用 Instance()
 * IpLocationStore::Instance()->Load();
 * @endcode
 *
 * @note 推荐使用全局宏 sIPLocation 访问单例
 * @note 此函数线程安全，可在多线程环境中调用
 */
IpLocationStore* IpLocationStore::Instance()
{
    static IpLocationStore instance;
    return &instance;
}
