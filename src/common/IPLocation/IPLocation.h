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
 * @file IPLocation.h
 * @brief IP地理位置查询模块
 *
 * 该模块提供了基于IP地址的地理位置查询功能。通过加载IP地址数据库，
 * 可以根据客户端的IP地址查询其所在的国家信息，用于统计分析、访问控制等场景。
 * 主要功能包括：
 * - 加载和管理IP地理位置数据库
 * - 根据IP地址查询对应的地理位置记录
 * - 提供单例访问接口以便全局使用
 */

#include "Define.h"
#include <string>
#include <vector>

/**
 * @brief IP地理位置记录结构体
 *
 * 该结构体用于存储单个IP地址范围的地理位置信息，
 * 包含IP范围起止地址和对应的国家代码及名称。
 */
struct IpLocationRecord
{
    /**
     * @brief 默认构造函数
     *
     * 初始化IP范围为0，国家代码和名称为空字符串
     */
    IpLocationRecord() : IpFrom(0), IpTo(0) { }

    /**
     * @brief 参数化构造函数
     * @param ipFrom IP地址范围起始值（uint32格式）
     * @param ipTo IP地址范围结束值（uint32格式）
     * @param countryCode 国家代码（如"CN"、"US"等ISO 3166-1标准代码）
     * @param countryName 国家名称（如"China"、"United States"等）
     */
    IpLocationRecord(uint32 ipFrom, uint32 ipTo, std::string countryCode, std::string countryName)
        : IpFrom(ipFrom), IpTo(ipTo), CountryCode(std::move(countryCode)), CountryName(std::move(countryName)) { }

    uint32 IpFrom;              ///< IP地址范围起始值（uint32格式）
    uint32 IpTo;                ///< IP地址范围结束值（uint32格式）
    std::string CountryCode;    ///< 国家代码（ISO 3166-1标准，如"CN"、"US"等）
    std::string CountryName;    ///< 国家全名（如"China"、"United States"等）
};

/**
 * @brief IP地理位置存储管理类
 *
 * 该类负责加载和管理IP地理位置数据库，并提供查询接口。
 * 采用单例模式设计，全局可通过sIPLocation宏访问唯一实例。
 * 数据库文件通常位于服务器的数据目录中，在服务器启动时加载。
 */
class TC_COMMON_API IpLocationStore
{
    public:
        /**
         * @brief 默认构造函数
         */
        IpLocationStore();

        /**
         * @brief 析构函数
         */
        ~IpLocationStore();

        /**
         * @brief 获取单例实例
         * @return 返回IpLocationStore的单例指针
         *
         * 该方法实现了单例模式，确保全局只有一个IpLocationStore实例。
         * 通常通过sIPLocation宏来调用此方法。
         */
        static IpLocationStore* Instance();

        /**
         * @brief 加载IP地理位置数据库
         *
         * 从数据文件中加载IP地理位置记录到内存中。
         * 该方法通常在服务器启动时调用一次。
         * 如果加载失败或文件不存在，将记录错误日志但不会中断服务器启动。
         */
        void Load();

        /**
         * @brief 根据IP地址查询地理位置记录
         * @param ipAddress IP地址字符串（如"192.168.1.1"）
         * @return 返回匹配的IpLocationRecord指针，如果未找到则返回nullptr
         *
         * 该方法将字符串格式的IP地址转换为uint32格式，
         * 然后在已加载的记录中进行二分查找，返回匹配的地理位置记录。
         * 如果IP地址格式无效或数据库未加载，将返回nullptr。
         */
        IpLocationRecord const* GetLocationRecord(std::string const& ipAddress) const;

    private:
        std::vector<IpLocationRecord> _ipLocationStore; ///< IP地理位置记录存储容器，按IP范围排序
};

#define sIPLocation IpLocationStore::Instance() ///< 全局访问点宏，用于便捷访问IpLocationStore单例
