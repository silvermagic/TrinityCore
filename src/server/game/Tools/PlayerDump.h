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
 * @file PlayerDump.h
 * @brief 玩家角色数据导出/导入模块
 *
 * 本模块实现了玩家角色数据的完整导出和导入功能，支持：
 * - 将玩家角色数据导出为SQL格式的转储文件或字符串
 * - 从转储文件或字符串导入玩家角色数据
 * - 自动处理GUID映射和冲突解决
 * - 支持宠物、邮件、物品、装备套装等关联数据的完整导出
 *
 * 主要用途：
 * - 角色数据备份和恢复
 * - 角色跨账号转移
 * - 角色数据迁移
 *
 * 使用方式：通过GM命令 .pdump write 和 .pdump load 调用
 */

#ifndef _PLAYER_DUMP_H
#define _PLAYER_DUMP_H

#include <string>
#include <iosfwd>
#include <map>
#include <set>
#include "ObjectGuid.h"

/**
 * @brief 转储表类型枚举
 *
 * 定义了玩家数据转储时需要处理的不同表类型，
 * 每种类型对应不同的数据处理逻辑和依赖关系。
 */
enum DumpTableType
{
    DTT_CHARACTER,      ///< 角色主表 (characters)，存储角色核心数据

    DTT_CHAR_TABLE,     ///< 角色关联表，以角色GUID为外键的表
                        ///< 包括：character_achievement, character_achievement_progress,
                        ///< character_action, character_aura, character_homebind,
                        ///< character_queststatus, character_queststatus_rewarded, character_reputation,
                        ///< character_spell, character_spell_cooldown, character_ticket, character_talent

    DTT_EQSET_TABLE,    ///< 装备套装表 (character_equipmentsets)，<- guid 作为外键

    DTT_INVENTORY,      ///< 角色背包表 (character_inventory)，-> 产出物品GUID集合

    DTT_MAIL,           ///< 邮件表 (mail)，-> 产出邮件ID集合和物品文本
                        ///< 包含 item_text 数据

    DTT_MAIL_ITEM,      ///< 邮件物品表 (mail_items)，<- 邮件ID作为外键
                        ///< -> 产出物品GUID集合

    DTT_ITEM,           ///< 物品实例表 (item_instance)，<- 物品GUID作为外键
                        ///< -> 产出物品文本

    DTT_ITEM_GIFT,      ///< 角色礼物表 (character_gifts)，<- 物品GUID作为外键

    DTT_PET,            ///< 角色宠物表 (character_pet)，-> 产出宠物GUID集合
    DTT_PET_TABLE       ///< 宠物关联表，<- 宠物GUID作为外键
                        ///< 包括：pet_aura, pet_spell, pet_spell_cooldown
};

/**
 * @brief 转储操作返回值枚举
 *
 * 定义了玩家数据导出/导入操作的返回状态码，
 * 用于指示操作是否成功以及失败的具体原因。
 */
enum DumpReturn
{
    DUMP_SUCCESS,           ///< 操作成功完成
    DUMP_FILE_OPEN_ERROR,   ///< 文件打开失败（文件不存在、权限不足或路径非法）
    DUMP_TOO_MANY_CHARS,    ///< 账号角色数量已达上限（最多10个角色）
    DUMP_FILE_BROKEN,       ///< 转储文件损坏或格式无效
    DUMP_CHARACTER_DELETED  ///< 角色已被删除（导出时角色不存在）
};

struct DumpTable;
struct TableStruct;
class StringTransaction;

/**
 * @brief 玩家数据转储基类
 *
 * 提供玩家角色数据导出/导入的基础功能框架，
 * 包含表结构初始化等通用功能。
 *
 * 这是一个抽象基类，具体功能由 PlayerDumpWriter 和 PlayerDumpReader 实现。
 */
class TC_GAME_API PlayerDump
{
    public:
        /**
         * @brief 初始化转储表结构
         *
         * 在服务器启动时调用，从数据库读取所有相关表的字段结构，
         * 建立字段索引和依赖关系映射。
         *
         * @note 必须在使用任何转储功能前调用此方法
         * @note 此方法在 World 初始化期间调用一次
         *
         * 性能注意事项：
         * - 会执行多次数据库查询以获取表结构
         * - 初始化结果存储在静态数据结构中供后续使用
         */
        static void InitializeTables();

    protected:
        /// 默认构造函数（受保护，仅允许子类访问）
        PlayerDump() { }
};

/**
 * @brief 玩家数据转储写入器
 *
 * 负责将玩家角色数据导出为SQL格式的转储数据。
 * 导出的数据可以通过 PlayerDumpReader 导入到数据库中。
 *
 * 主要功能：
 * - 导出角色主表及其所有关联数据
 * - 收集并导出宠物、邮件、物品等关联对象
 * - 生成可执行的SQL INSERT语句
 *
 * 使用示例：
 * @code
 * PlayerDumpWriter writer;
 * DumpReturn result = writer.WriteDumpToFile("backup.sql", playerGuid);
 * if (result == DUMP_SUCCESS) {
 *     // 导出成功
 * }
 * @endcode
 */
class TC_GAME_API PlayerDumpWriter : public PlayerDump
{
    public:
        /// 默认构造函数
        PlayerDumpWriter() { }

        /**
         * @brief 获取角色转储数据到字符串
         *
         * @param guid 角色低GUID
         * @param dump [out] 输出的转储数据字符串
         * @return true 导出成功
         * @return false 导出失败（角色已删除或不存在）
         *
         * @note 转储数据格式为多条SQL INSERT语句
         */
        bool GetDump(ObjectGuid::LowType guid, std::string& dump);

        /**
         * @brief 将角色数据导出到文件
         *
         * @param file 目标文件路径
         * @param guid 角色低GUID
         * @return DumpReturn 导出结果状态码
         *
         * 安全检查：
         * - 如果启用了 CONFIG_PDUMP_NO_PATHS，禁止使用路径分隔符
         * - 如果启用了 CONFIG_PDUMP_NO_OVERWRITE，禁止覆盖已存在的文件
         *
         * @see GetDump()
         */
        DumpReturn WriteDumpToFile(std::string const& file, ObjectGuid::LowType guid);

        /**
         * @brief 将角色数据导出到字符串
         *
         * @param dump [out] 输出的转储数据字符串
         * @param guid 角色低GUID
         * @return DumpReturn 导出结果状态码
         *
         * @note 此方法是 WriteDumpToFile 的字符串版本，用于内存操作
         */
        DumpReturn WriteDumpToString(std::string& dump, ObjectGuid::LowType guid);

    private:
        /**
         * @brief 追加单表数据到转储事务
         *
         * @param trans 字符串事务对象
         * @param guid 角色低GUID
         * @param tableStruct 表结构信息
         * @param dumpTable 转储表配置
         * @return true 追加成功
         * @return false 角色已删除（deleteInfos_Account字段非空）
         */
        bool AppendTable(StringTransaction& trans, ObjectGuid::LowType guid, TableStruct const& tableStruct, DumpTable const& dumpTable);

        /**
         * @brief 填充关联对象的GUID集合
         *
         * 遍历基础表（BaseTables），收集角色关联的所有：
         * - 宠物GUID
         * - 邮件ID
         * - 物品GUID
         * - 装备套装ID
         *
         * @param guid 角色低GUID
         *
         * @note 必须在 AppendTable 之前调用，以确保导出完整数据
         */
        void PopulateGuids(ObjectGuid::LowType guid);

        std::set<ObjectGuid::LowType> _pets;    ///< 收集的宠物GUID集合
        std::set<ObjectGuid::LowType> _mails;   ///< 收集的邮件ID集合
        std::set<ObjectGuid::LowType> _items;   ///< 收集的物品GUID集合

        std::set<uint64> _itemSets;             ///< 收集的装备套装ID集合（64位）
};

/**
 * @brief 玩家数据转储读取器
 *
 * 负责从转储数据导入玩家角色到数据库。
 * 支持从文件或字符串读取，自动处理GUID映射和数据冲突。
 *
 * 主要功能：
 * - 解析SQL INSERT语句并执行导入
 * - 自动重映射GUID以避免冲突
 * - 支持角色重命名和GUID指定
 * - 更新角色缓存和计数器
 *
 * 导入流程：
 * 1. 检查账号角色数量限制
 * 2. 验证/生成新的角色GUID
 * 3. 解析每行SQL并替换GUID
 * 4. 执行事务提交
 * 5. 更新系统计数器
 *
 * 使用示例：
 * @code
 * PlayerDumpReader reader;
 * DumpReturn result = reader.LoadDumpFromFile("backup.sql", accountId, "CharName", 0);
 * @endcode
 */
class TC_GAME_API PlayerDumpReader : public PlayerDump
{
    public:
        /// 默认构造函数
        PlayerDumpReader() { }

        /**
         * @brief 从文件加载角色转储数据
         *
         * @param file 转储文件路径
         * @param account 目标账号ID
         * @param name 新角色名称（可为空，使用原名或自动生成）
         * @param guid 指定的角色GUID（0表示自动分配）
         * @return DumpReturn 导入结果状态码
         *
         * @note 文件必须包含有效的SQL INSERT语句
         */
        DumpReturn LoadDumpFromFile(std::string const& file, uint32 account, std::string name, ObjectGuid::LowType guid);

        /**
         * @brief 从字符串加载角色转储数据
         *
         * @param dump 转储数据字符串
         * @param account 目标账号ID
         * @param name 新角色名称（可为空）
         * @param guid 指定的角色GUID（0表示自动分配）
         * @return DumpReturn 导入结果状态码
         *
         * @note 此方法是 LoadDumpFromFile 的字符串版本
         */
        DumpReturn LoadDumpFromString(std::string const& dump, uint32 account, std::string name, ObjectGuid::LowType guid);

    private:
        /**
         * @brief 从输入流加载转储数据（核心实现）
         *
         * @param input 输入流
         * @param account 目标账号ID
         * @param name 新角色名称
         * @param guid 指定的角色GUID
         * @return DumpReturn 导入结果状态码
         *
         * 处理流程：
         * 1. 检查账号角色数（上限10个）
         * 2. 确定最终角色GUID（检查冲突，必要时使用下一个可用GUID）
         * 3. 验证/处理角色名称
         * 4. 初始化GUID映射表（物品、邮件、宠物、装备套装）
         * 5. 逐行解析SQL，替换相关GUID
         * 6. 执行数据库事务
         * 7. 更新缓存和计数器
         *
         * @note 所有GUID替换确保不会与现有数据冲突
         */
        DumpReturn LoadDump(std::istream& input, uint32 account, std::string name, ObjectGuid::LowType guid);
};

#endif
