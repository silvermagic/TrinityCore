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
 * @file PlayerDump.cpp
 * @brief 玩家角色数据导出/导入模块实现
 *
 * 实现了玩家角色数据的完整导出和导入功能。
 * 支持角色所有关联数据的导出，包括宠物、邮件、物品、装备套装等。
 *
 * 核心数据结构：
 * - DumpTables[]：定义需要导出的所有表及其类型
 * - BaseTables[]：定义基础表，用于收集关联对象GUID
 * - CharacterTables：动态加载的表结构信息
 *
 * 关键处理流程：
 * 1. 导出时先收集所有关联GUID，然后按依赖顺序导出各表
 * 2. 导入时解析SQL并重新映射所有GUID，避免冲突
 */

#include "PlayerDump.h"
#include "AccountMgr.h"
#include "CharacterCache.h"
#include "Common.h"
#include "DatabaseEnv.h"
#include "Log.h"
#include "ObjectMgr.h"
#include "Player.h"
#include "StringConvert.h"
#include "World.h"
#include <fstream>
#include <sstream>

// ============================================================================
// 静态数据定义
// ============================================================================

/**
 * @brief GUID类型枚举
 *
 * 定义转储数据中不同类型对象的GUID分类，
 * 用于在导入时正确处理GUID映射。
 */
enum GuidType : uint8
{
    // 32位GUID类型
    GUID_TYPE_ACCOUNT,       ///< 账号ID
    GUID_TYPE_CHAR,          ///< 角色GUID
    GUID_TYPE_PET,           ///< 宠物GUID
    GUID_TYPE_MAIL,          ///< 邮件ID
    GUID_TYPE_ITEM,          ///< 物品GUID

    // 64位GUID类型
    GUID_TYPE_EQUIPMENT_SET, ///< 装备套装ID（64位）

    // 特殊类型
    GUID_TYPE_NULL           ///< 设置为NULL（用于删除标记字段）
};

/**
 * @brief 文件关闭器（RAII辅助结构）
 *
 * 用于智能指针自动关闭FILE指针，确保资源正确释放。
 */
struct FileCloser
{
    /**
     * @brief 函数调用运算符，关闭文件
     * @param f 要关闭的文件指针
     */
    void operator()(FILE* f) const
    {
        if (f)
            fclose(f);
    }
};
typedef std::unique_ptr<FILE, FileCloser> FileHandle; ///< 文件句柄智能指针类型

/**
 * @brief 获取文件句柄（RAII方式）
 *
 * @param path 文件路径
 * @param mode 打开模式（如 "r", "w"）
 * @return FileHandle 文件句柄智能指针
 *
 * @note 当 FileHandle 超出作用域时自动关闭文件
 */
inline FileHandle GetFileHandle(char const* path, char const* mode)
{
    return FileHandle(fopen(path, mode), FileCloser());
}

/**
 * @brief 基础表结构定义
 *
 * 定义需要收集关联GUID的基础表信息，
 * 用于在导出时收集宠物、邮件、物品、装备套装等GUID。
 */
struct BaseTable
{
    char const* TableName;   ///< 表名
    char const* PrimaryKey;  ///< 主键字段名
    char const* PlayerGuid;  ///< 玩家GUID字段名（用于查询条件）

    GuidType StoredType;     ///< 存储的GUID类型
};

/**
 * @brief 基础表配置数组
 *
 * 列出所有需要收集关联对象GUID的表。
 * 这些表的主键值会被收集，用于后续导出关联数据。
 */
BaseTable const BaseTables[] =
{
    { "character_pet",           "id",      "owner",      GUID_TYPE_PET           }, ///< 宠物表
    { "mail",                    "id",      "receiver",   GUID_TYPE_MAIL          }, ///< 邮件表
    { "item_instance",           "guid",    "owner_guid", GUID_TYPE_ITEM          }, ///< 物品实例表

    { "character_equipmentsets", "setguid", "guid",       GUID_TYPE_EQUIPMENT_SET }  ///< 装备套装表
};

/**
 * @brief 转储表配置结构
 *
 * 定义单个转储表的名称和类型。
 */
struct DumpTable
{
    char const* Name;    ///< 表名
    DumpTableType Type;  ///< 表类型，决定处理方式
};

/**
 * @brief 转储表配置数组
 *
 * 定义所有需要转储的表及其处理顺序。
 * 表的顺序很重要：必须先处理依赖项的来源表，再处理依赖表。
 *
 * 顺序说明：
 * - characters 必须第一个处理（主表）
 * - mail 必须在 mail_items 之前
 * - character_pet 必须在 pet_* 表之前
 * - character_inventory 和 mail_items 必须在 item_instance 之前
 * - item_instance 必须在 character_gifts 之前
 */
DumpTable const DumpTables[] =
{
    { "characters",                     DTT_CHARACTER    }, ///< 角色主表
    { "character_account_data",         DTT_CHAR_TABLE   }, ///< 账号数据
    { "character_achievement",          DTT_CHAR_TABLE   }, ///< 成就
    { "character_achievement_progress", DTT_CHAR_TABLE   }, ///< 成就进度
    { "character_action",               DTT_CHAR_TABLE   }, ///< 动作条
    { "character_aura",                 DTT_CHAR_TABLE   }, ///< 光环效果
    { "character_declinedname",         DTT_CHAR_TABLE   }, ///< 名字变格（俄语）
    { "character_equipmentsets",        DTT_EQSET_TABLE  }, ///< 装备套装
    { "character_fishingsteps",         DTT_CHAR_TABLE   }, ///< 钓鱼步骤
    { "character_glyphs",               DTT_CHAR_TABLE   }, ///< 铭文
    { "character_homebind",             DTT_CHAR_TABLE   }, ///< 炉石绑定点
    { "character_inventory",            DTT_INVENTORY    }, ///< 背包物品（产出物品GUID）
    { "character_pet",                  DTT_PET          }, ///< 宠物（产出宠物GUID）
    { "character_pet_declinedname",     DTT_PET          }, ///< 宠物名字变格
    { "character_queststatus",          DTT_CHAR_TABLE   }, ///< 任务状态
    { "character_queststatus_daily",    DTT_CHAR_TABLE   }, ///< 每日任务
    { "character_queststatus_weekly",   DTT_CHAR_TABLE   }, ///< 每周任务
    { "character_queststatus_monthly",  DTT_CHAR_TABLE   }, ///< 每月任务
    { "character_queststatus_seasonal", DTT_CHAR_TABLE   }, ///< 季节任务
    { "character_queststatus_rewarded", DTT_CHAR_TABLE   }, ///< 已奖励任务
    { "character_reputation",           DTT_CHAR_TABLE   }, ///< 声望
    { "character_skills",               DTT_CHAR_TABLE   }, ///< 技能
    { "character_spell",                DTT_CHAR_TABLE   }, ///< 法术
    { "character_spell_cooldown",       DTT_CHAR_TABLE   }, ///< 法术冷却
    { "character_talent",               DTT_CHAR_TABLE   }, ///< 天赋
    { "mail",                           DTT_MAIL         }, ///< 邮件（产出邮件ID）
    { "mail_items",                     DTT_MAIL_ITEM    }, ///< 邮件物品（必须在mail之后）
    { "pet_aura",                       DTT_PET_TABLE    }, ///< 宠物光环（必须在character_pet之后）
    { "pet_spell",                      DTT_PET_TABLE    }, ///< 宠物法术（必须在character_pet之后）
    { "pet_spell_cooldown",             DTT_PET_TABLE    }, ///< 宠物法术冷却（必须在character_pet之后）
    { "item_instance",                  DTT_ITEM         }, ///< 物品实例（必须在character_inventory和mail_items之后）
    { "character_gifts",                DTT_ITEM_GIFT    }  ///< 角色礼物（必须在item_instance之后）
};

/// 转储表总数（编译时计算）
uint32 const DUMP_TABLE_COUNT = std::extent<decltype(DumpTables)>::value;

// ============================================================================
// 辅助类定义
// ============================================================================

/**
 * @brief 字符串事务类
 *
 * 辅助类，用于将多条SQL查询语句累积到一个字符串中。
 * 在导出过程中收集所有INSERT语句，最终生成完整的转储数据。
 */
class StringTransaction
{
    public:
        /// 默认构造函数
        StringTransaction() : _buf() { }

        /**
         * @brief 追加SQL语句
         * @param sql SQL语句字符串
         *
         * 将SQL语句添加到缓冲区，并追加换行符。
         */
        void Append(char const* sql)
        {
            std::ostringstream oss;
            oss << sql << '\n';
            _buf += oss.str();
        }

        /**
         * @brief 获取累积的缓冲区内容
         * @return 缓冲区字符串的C风格指针
         */
        char const* GetBuffer() const
        {
            return _buf.c_str();
        }

    private:
        std::string _buf; ///< SQL语句累积缓冲区
};

/**
 * @brief 表字段结构（动态数据，启动时加载）
 *
 * 存储单个字段的元数据，包括字段名、GUID类型和依赖关系。
 */
struct TableField
{
    std::string FieldName;             ///< 字段名

    GuidType FieldGuidType = GUID_TYPE_ACCOUNT; ///< 字段存储的GUID类型
    bool IsDependentField = false;     ///< 是否为依赖字段（需要在导入时重新映射GUID）
};

/**
 * @brief 表结构定义（动态数据，启动时加载）
 *
 * 存储单张表的完整元数据，包括表名、WHERE条件字段和所有字段信息。
 * 在初始化时从数据库动态加载并缓存。
 */
struct TableStruct
{
    std::string TableName;             ///< 表名
    std::string WhereFieldName;        ///< WHERE条件的字段名（用于查询条件）
    std::vector<TableField> TableFields; ///< 所有字段的列表

    // 用于快速查找
    std::unordered_map<std::string /*fieldName*/, int32 /*index*/> FieldIndices; ///< 字段名到索引的映射
};

/// 所有角色相关表的结构信息（启动时初始化）
std::vector<TableStruct> CharacterTables;

// ============================================================================
// 辅助函数
// ============================================================================

/**
 * @brief 不区分大小写比较字符串（仅支持拉丁字符）
 *
 * @param left 左侧字符串
 * @param right 右侧字符串
 * @return true 字符串相等（忽略大小写）
 * @return false 字符串不相等
 *
 * @note 仅支持拉丁字符，非拉丁字符可能导致比较错误
 */
inline bool StringsEqualCaseInsensitive(std::string const& left, std::string const& right)
{
    std::string upperLeftString = left;
    bool leftResult = Utf8ToUpperOnlyLatin(upperLeftString);
    ASSERT(leftResult);

    std::string upperRightString = right;
    bool rightResult = Utf8ToUpperOnlyLatin(upperRightString);
    ASSERT(rightResult);

    return upperLeftString == upperRightString;
}

/**
 * @brief 按名称查找列（不区分大小写）
 *
 * @param tableStruct 表结构引用
 * @param columnName 列名
 * @return 字段迭代器，未找到则返回 end()
 */
inline auto FindColumnByName(TableStruct& tableStruct, std::string const& columnName) -> decltype(tableStruct.TableFields.begin())
{
    return std::find_if(tableStruct.TableFields.begin(), tableStruct.TableFields.end(), [columnName](TableField const& tableField) -> bool
    {
        return StringsEqualCaseInsensitive(tableField.FieldName, columnName);
    });
}

/**
 * @brief 按名称获取列索引
 *
 * @param tableStruct 表结构引用
 * @param columnName 列名
 * @return 列索引，未找到返回 -1
 *
 * @note 使用预构建的索引映射，查找效率为 O(1)
 */
inline int32 GetColumnIndexByName(TableStruct const& tableStruct, std::string const& columnName)
{
    auto itr = tableStruct.FieldIndices.find(columnName);
    if (itr == tableStruct.FieldIndices.end())
        return -1;

    return itr->second;
}

/**
 * @brief 标记列为依赖字段
 *
 * 将指定列标记为依赖字段，在导入时需要重新映射GUID。
 *
 * @param tableStruct 表结构引用
 * @param columnName 列名
 * @param dependentType 依赖的GUID类型
 *
 * @note 如果列不存在或已被标记，将触发断言失败
 */
inline void MarkDependentColumn(TableStruct& tableStruct, std::string const& columnName, GuidType dependentType)
{
    auto itr = FindColumnByName(tableStruct, columnName);
    if (itr == tableStruct.TableFields.end())
    {
        TC_LOG_FATAL("server.loading", "Column `{}` declared in table `{}` marked as dependent but doesn't exist, PlayerDump will not work properly, please update table definitions",
            columnName, tableStruct.TableName);
        ABORT();
        return;
    }

    if (itr->IsDependentField)
    {
        TC_LOG_FATAL("server.loading", "Attempt to mark column `{}` in table `{}` as dependent column but already marked! please check your code.",
            columnName, tableStruct.TableName);
        ABORT();
        return;
    }

    itr->IsDependentField = true;
    itr->FieldGuidType = dependentType;
}

/**
 * @brief 标记WHERE条件字段
 *
 * 设置表的WHERE条件字段，用于查询时过滤数据。
 *
 * @param tableStruct 表结构引用
 * @param whereField WHERE条件字段名
 *
 * @note 每个表只能有一个WHERE字段，重复设置会触发断言失败
 */
inline void MarkWhereField(TableStruct& tableStruct, std::string const& whereField)
{
    ASSERT(tableStruct.WhereFieldName.empty());

    auto whereFieldItr = FindColumnByName(tableStruct, whereField);
    if (whereFieldItr == tableStruct.TableFields.end())
    {
        TC_LOG_FATAL("server.loading", "Column name `{}` set as 'WHERE' column for table `{}` doesn't exist. PlayerDump won't work properly",
            whereField, tableStruct.TableName);
        ABORT();
        return;
    }

    tableStruct.WhereFieldName = whereField;
}

/**
 * @brief 断言基础表配置有效
 *
 * 验证基础表配置中的表名和字段名是否存在于已加载的表结构中。
 *
 * @param baseTable 基础表配置
 *
 * @note 在初始化完成后调用，用于验证配置正确性
 */
inline void AssertBaseTable(BaseTable const& baseTable)
{
    auto itr = std::find_if(CharacterTables.begin(), CharacterTables.end(), [baseTable](TableStruct const& tableStruct) -> bool
    {
        return StringsEqualCaseInsensitive(tableStruct.TableName, baseTable.TableName);
    });

    ASSERT(itr != CharacterTables.end());

    auto columnItr = FindColumnByName(*itr, baseTable.PrimaryKey);
    ASSERT(columnItr != itr->TableFields.end());

    columnItr = FindColumnByName(*itr, baseTable.PlayerGuid);
    ASSERT(columnItr != itr->TableFields.end());
}

// ============================================================================
// 初始化函数
// ============================================================================

/**
 * @brief 初始化转储表结构
 *
 * 从数据库读取所有转储表的字段结构，建立字段索引和依赖关系映射。
 * 此函数在服务器启动时调用一次。
 *
 * 处理流程：
 * 1. 遍历 DumpTables 数组中的所有表
 * 2. 执行 DESC 命令获取表的字段结构
 * 3. 根据表类型标记WHERE条件和依赖字段
 * 4. 执行完整性检查
 *
 * @note 必须在使用任何转储功能前调用
 */
void PlayerDump::InitializeTables()
{
    uint32 oldMSTime = getMSTime();

    // 遍历所有需要转储的表
    for (DumpTable const& dumpTable : DumpTables)
    {
        TableStruct t;
        t.TableName = dumpTable.Name;

        // 从数据库获取表结构
        QueryResult result = CharacterDatabase.PQuery("DESC {}", dumpTable.Name);
        // 预处理语句在启动时已验证，表必须存在
        ASSERT(result);

        // 解析每个字段
        int32 i = 0;
        do
        {
            std::string columnName = (*result)[0].GetString();
            t.FieldIndices.emplace(columnName, i++); // 建立字段名到索引的映射

            TableField f;
            f.FieldName = columnName;

            // 将字段名转为大写，用于后续比较
            bool toUpperResult = Utf8ToUpperOnlyLatin(columnName);
            ASSERT(toUpperResult);

            t.TableFields.emplace_back(std::move(f));
        } while (result->NextRow());

        // 根据表类型标记依赖字段和WHERE条件
        switch (dumpTable.Type)
        {
            case DTT_CHARACTER: // 角色主表
                MarkWhereField(t, "guid");

                MarkDependentColumn(t, "guid", GUID_TYPE_CHAR);
                MarkDependentColumn(t, "account", GUID_TYPE_ACCOUNT);

                // 删除信息字段设为NULL
                MarkDependentColumn(t, "deleteInfos_Account", GUID_TYPE_NULL);
                MarkDependentColumn(t, "deleteInfos_Name", GUID_TYPE_NULL);
                MarkDependentColumn(t, "deleteDate", GUID_TYPE_NULL);
                break;
            case DTT_CHAR_TABLE: // 角色关联表
                MarkWhereField(t, "guid");

                MarkDependentColumn(t, "guid", GUID_TYPE_CHAR);
                break;
            case DTT_EQSET_TABLE: // 装备套装表
                MarkWhereField(t, "guid");

                MarkDependentColumn(t, "guid", GUID_TYPE_CHAR);
                MarkDependentColumn(t, "setguid", GUID_TYPE_EQUIPMENT_SET);

                // item0 - item18（19个装备槽位）
                for (uint32 j = 0; j < EQUIPMENT_SLOT_END; ++j)
                {
                    std::string itColumn = Trinity::StringFormat("item{}", j);
                    MarkDependentColumn(t, itColumn, GUID_TYPE_ITEM);
                }
                break;
            case DTT_INVENTORY: // 背包表
                MarkWhereField(t, "guid");

                MarkDependentColumn(t, "guid", GUID_TYPE_CHAR);
                MarkDependentColumn(t, "bag", GUID_TYPE_ITEM);
                MarkDependentColumn(t, "item", GUID_TYPE_ITEM);
                break;
            case DTT_MAIL: // 邮件表
                MarkWhereField(t, "receiver");

                MarkDependentColumn(t, "id", GUID_TYPE_MAIL);
                MarkDependentColumn(t, "receiver", GUID_TYPE_CHAR);
                break;
            case DTT_MAIL_ITEM: // 邮件物品表
                MarkWhereField(t, "mail_id");

                MarkDependentColumn(t, "mail_id", GUID_TYPE_MAIL);
                MarkDependentColumn(t, "item_guid", GUID_TYPE_ITEM);
                MarkDependentColumn(t, "receiver", GUID_TYPE_CHAR);
                break;
            case DTT_ITEM: // 物品实例表
                MarkWhereField(t, "guid");

                MarkDependentColumn(t, "guid", GUID_TYPE_ITEM);
                MarkDependentColumn(t, "owner_guid", GUID_TYPE_CHAR);
                break;
            case DTT_ITEM_GIFT: // 角色礼物表
                MarkWhereField(t, "item_guid");

                MarkDependentColumn(t, "guid", GUID_TYPE_CHAR);
                MarkDependentColumn(t, "item_guid", GUID_TYPE_ITEM);
                break;
            case DTT_PET: // 宠物表
                MarkWhereField(t, "owner");

                MarkDependentColumn(t, "id", GUID_TYPE_PET);
                MarkDependentColumn(t, "owner", GUID_TYPE_CHAR);
                break;
            case DTT_PET_TABLE: // 宠物关联表
                MarkWhereField(t, "guid");

                MarkDependentColumn(t, "guid", GUID_TYPE_PET);
                break;
            default:
                TC_LOG_FATAL("server.loading", "Wrong dump table type {}, probably added a new table type without updating code", uint32(dumpTable.Type));
                ABORT();
                return;
        }

        CharacterTables.emplace_back(std::move(t));
    }

    // 完整性检查：确保所有表都设置了WHERE字段
    for (TableStruct const& tableStruct : CharacterTables)
    {
        if (tableStruct.WhereFieldName.empty())
        {
            TC_LOG_FATAL("server.loading", "Table `{}` defined in player dump doesn't have a WHERE query field", tableStruct.TableName);
            ABORT();
        }
    }

    // 验证基础表配置正确性
    for (BaseTable const& baseTable : BaseTables)
        AssertBaseTable(baseTable);

    // 确保表数量一致
    ASSERT(CharacterTables.size() == DUMP_TABLE_COUNT);

    TC_LOG_INFO("server.loading", ">> Initialized tables for PlayerDump in {} ms.", GetMSTimeDiffToNow(oldMSTime));
}

// ============================================================================
// 底层辅助函数
// ============================================================================

/**
 * @brief 在SQL语句中查找指定列的值位置
 *
 * 解析INSERT语句，找到指定列的值在VALUES子句中的位置。
 *
 * @param ts 表结构
 * @param str SQL语句字符串
 * @param column 列名
 * @param s [out] 值起始位置
 * @param e [out] 值结束位置
 * @return true 查找成功
 * @return false 列不存在或SQL格式错误
 *
 * @note 支持转义的单引号处理
 */
inline bool FindColumn(TableStruct const& ts, std::string const& str, std::string const& column, std::string::size_type& s, std::string::size_type& e)
{
    int32 columnIndex = GetColumnIndexByName(ts, column);
    if (columnIndex == -1)
        return false;

    // 数组索引从0开始，需要补偿
    ++columnIndex;

    // 查找 VALUES (' 的位置
    s = str.find("VALUES ('");
    if (s == std::string::npos)
        return false;
    s += 9;

    // 找到第一个值的结束引号（处理转义）
    do
    {
        e = str.find('\'', s);
        if (e == std::string::npos)
            return false;
    } while (str[e - 1] == '\\');

    // 定位到目标列
    for (int32 i = 1; i < columnIndex; ++i)
    {
        do
        {
            // "', '" 的长度
            s = e + 4;
            e = str.find('\'', s);
            if (e == std::string::npos)
                return false;
        } while (str[e - 1] == '\\');
    }
    return true;
}

/**
 * @brief 从SQL INSERT语句中提取表名
 *
 * @param str SQL语句字符串
 * @return 表名，解析失败返回空字符串
 *
 * @note 假设语句格式为 "INSERT INTO `表名` ..."
 */
inline std::string GetTableName(std::string const& str)
{
    // "INSERT INTO `" 的长度
    static std::string::size_type const s = 13;
    std::string::size_type e = str.find('`', s);
    if (e == std::string::npos)
        return "";

    return str.substr(s, e - s);
}

/**
 * @brief 验证SQL语句中的字段列表
 *
 * 检查INSERT语句中的列名是否与数据库表结构匹配。
 * 用于确保转储文件与当前数据库结构兼容。
 *
 * @param ts 表结构
 * @param str SQL语句字符串
 * @param lineNumber 行号（用于日志输出）
 * @return true 验证通过
 * @return false 字段不兼容或格式错误
 *
 * @note 支持新旧两种格式：
 *       - 旧格式：INSERT INTO `table` VALUES (...)
 *       - 新格式：INSERT INTO `table` (`col1`, `col2`) VALUES (...)
 */
inline bool ValidateFields(TableStruct const& ts, std::string const& str, size_t lineNumber)
{
    std::string::size_type s = str.find("` VALUES (");
    if (s != std::string::npos) // 旧格式（无列名）
        return true;

    // 新格式带列名，需要验证以避免执行无效查询
    s = str.find("` (`");
    if (s == std::string::npos)
    {
        TC_LOG_ERROR("misc", "LoadPlayerDump: (line {}) dump format not recognized.", lineNumber);
        return false;
    }
    s += 4;

    std::string::size_type valPos = str.find("VALUES ('");
    std::string::size_type e = str.find('`', s);
    if (e == std::string::npos || valPos == std::string::npos)
    {
        TC_LOG_ERROR("misc", "LoadPlayerDump: (line {}) unexpected end of line", lineNumber);
        return false;
    }

    // 逐个验证列名
    do
    {
        std::string column = str.substr(s, e - s);
        int32 columnIndex = GetColumnIndexByName(ts, column);
        if (columnIndex == -1)
        {
            TC_LOG_ERROR("misc", "LoadPlayerDump: (line {}) unknown column name `{}` for table `{}`, aborting due to incompatible DB structure.", lineNumber, column, ts.TableName);
            return false;
        }

        // "`, `" 的长度
        s = e + 4;
        e = str.find('`', s);
    } while (e < valPos);

    return true;
}

/**
 * @brief 替换SQL语句中指定列的值
 *
 * @param ts 表结构
 * @param str SQL语句字符串（会被修改）
 * @param column 列名
 * @param with 新值
 * @param allowZero 是否允许保持0值不变
 * @return true 替换成功
 * @return false 列未找到
 */
inline bool ChangeColumn(TableStruct const& ts, std::string& str, std::string const& column, std::string const& with, bool allowZero = false)
{
    std::string::size_type s, e;
    if (!FindColumn(ts, str, column, s, e))
        return false;

    if (allowZero && str.substr(s, e - s) == "0")
        return true; // 不是错误，保持0值

    str.replace(s, e - s, with);
    return true;
}

/**
 * @brief 获取SQL语句中指定列的值
 *
 * @param ts 表结构
 * @param str SQL语句字符串
 * @param column 列名
 * @return 列值字符串，未找到返回空字符串
 */
inline std::string GetColumn(TableStruct const& ts, std::string& str, std::string const& column)
{
    std::string::size_type s, e;
    if (!FindColumn(ts, str, column, s, e))
        return "";

    return str.substr(s, e - s);
}

/**
 * @brief 注册新GUID映射
 *
 * 为旧GUID分配新的GUID值，避免导入时的GUID冲突。
 *
 * @tparam T GUID类型
 * @param oldGuid 旧GUID
 * @param guidMap GUID映射表
 * @param guidOffset GUID偏移量（起始值）
 * @return 新GUID
 *
 * @note 如果旧GUID已存在映射，直接返回已分配的新GUID
 */
template <typename T, template<class, class, class...> class MapType, class... Rest>
inline T RegisterNewGuid(T oldGuid, MapType<T, T, Rest...>& guidMap, T guidOffset)
{
    auto itr = guidMap.find(oldGuid);
    if (itr != guidMap.end())
        return itr->second;

    // 分配新GUID = 偏移量 + 当前映射数量
    T newguid = guidOffset + T(guidMap.size());
    guidMap.emplace(oldGuid, newguid);
    return newguid;
}

/**
 * @brief 替换SQL语句中的GUID值
 *
 * 解析指定列的GUID值，重新映射并替换。
 *
 * @tparam T GUID类型
 * @param ts 表结构
 * @param str SQL语句字符串（会被修改）
 * @param column 列名
 * @param guidMap GUID映射表
 * @param guidOffset GUID偏移量
 * @param allowZero 是否允许0值保持不变
 * @return true 替换成功
 * @return false 替换失败
 */
template <typename T, template<class, class, class...> class MapType, class... Rest>
inline bool ChangeGuid(TableStruct const& ts, std::string& str, std::string const& column, MapType<T, T, Rest...>& guidMap, T guidOffset, bool allowZero = false)
{
    T oldGuid = Trinity::StringTo<T>(GetColumn(ts, str, column)).template value_or<T>(0);
    if (allowZero && !oldGuid)
        return true; // 不是错误，保持0值

    std::string chritem;
    T newGuid = RegisterNewGuid(oldGuid, guidMap, guidOffset);
    chritem = std::to_string(newGuid);

    return ChangeColumn(ts, str, column, chritem, allowZero);
}

/**
 * @brief 追加表数据转储到事务
 *
 * 将查询结果转换为INSERT语句并追加到字符串事务中。
 *
 * @param trans 字符串事务对象
 * @param tableStruct 表结构信息
 * @param result 数据库查询结果
 *
 * @note 生成的INSERT语句包含完整列名列表
 * @note NULL值会被转换为字符串 'NULL'
 */
inline void AppendTableDump(StringTransaction& trans, TableStruct const& tableStruct, QueryResult result)
{
    if (!result)
        return;

    do
    {
        // 构建带列名的INSERT语句
        std::ostringstream ss;
        ss << "INSERT INTO `" << tableStruct.TableName << "` (";
        for (auto itr = tableStruct.TableFields.begin(); itr != tableStruct.TableFields.end();)
        {
            ss << '`' << itr->FieldName << '`';
            ++itr;

            if (itr != tableStruct.TableFields.end())
                ss << ", ";
        }
        ss << ") VALUES (";

        // 构建VALUES子句
        uint32 const fieldSize = uint32(tableStruct.TableFields.size());
        Field* fields = result->Fetch();

        for (uint32 i = 0; i < fieldSize;)
        {
            char const* cString = fields[i].GetCString();
            ++i;

            // 空指针表示NULL值
            if (!cString)
                ss << "'NULL'";
            else
            {
                std::string s(cString);
                CharacterDatabase.EscapeString(s); // 转义特殊字符
                ss << '\'' << s << '\'';
            }

            if (i != fieldSize)
                ss << ", ";
        }
        ss << ");";

        trans.Append(ss.str().c_str());
    } while (result->NextRow());
}

/**
 * @brief 生成单GUID的WHERE条件字符串
 *
 * @param field 字段名
 * @param guid GUID值
 * @return WHERE条件字符串
 */
inline std::string GenerateWhereStr(std::string const& field, ObjectGuid::LowType guid)
{
    std::ostringstream whereStr;
    whereStr << field << " = '" << guid << '\'';
    return whereStr.str();
}

/**
 * @brief 生成GUID集合的WHERE条件字符串
 *
 * @tparam T GUID类型
 * @param field 字段名
 * @param guidSet GUID集合
 * @return WHERE条件字符串（使用IN子句）
 *
 * @note 如果字符串长度接近MAX_QUERY_LEN，会提前终止
 */
template <typename T, template<class, class...> class SetType, class... Rest>
inline std::string GenerateWhereStr(std::string const& field, SetType<T, Rest...> const& guidSet)
{
    std::ostringstream whereStr;
    whereStr << field << " IN ('";
    for (auto itr = guidSet.begin(); itr != guidSet.end();)
    {
        whereStr << *itr;
        ++itr;

        // 接近最大查询长度时提前终止
        if (whereStr.str().size() > MAX_QUERY_LEN - 50)
            break;

        if (itr != guidSet.end())
            whereStr << "','";
    }
    whereStr << "')";
    return whereStr.str();
}

// ============================================================================
// 导出相关高层函数
// ============================================================================

/**
 * @brief 填充关联对象的GUID集合
 *
 * 从数据库收集角色关联的所有宠物、邮件、物品和装备套装的GUID。
 * 这些GUID用于后续导出关联表数据。
 *
 * @param guid 角色低GUID
 *
 * 处理流程：
 * 遍历 BaseTables 数组，根据玩家GUID查询各基础表的主键值：
 * 1. character_pet -> 收集宠物GUID
 * 2. mail -> 收集邮件ID
 * 3. item_instance -> 收集物品GUID
 * 4. character_equipmentsets -> 收集装备套装ID
 */
void PlayerDumpWriter::PopulateGuids(ObjectGuid::LowType guid)
{
    for (BaseTable const& baseTable : BaseTables)
    {
        // 只处理有效的基础表类型
        switch (baseTable.StoredType)
        {
            case GUID_TYPE_ITEM:
            case GUID_TYPE_MAIL:
            case GUID_TYPE_PET:
            case GUID_TYPE_EQUIPMENT_SET:
                break;
            default:
                return;
        }

        // 查询该角色在该表中的所有关联对象
        std::string whereStr = GenerateWhereStr(baseTable.PlayerGuid, guid);
        QueryResult result = CharacterDatabase.PQuery("SELECT {} FROM {} WHERE {}", baseTable.PrimaryKey, baseTable.TableName, whereStr);
        if (!result)
            continue;

        do
        {
            // 根据类型收集到对应的集合
            switch (baseTable.StoredType)
            {
                case GUID_TYPE_ITEM:
                    if (ObjectGuid::LowType itemLowGuid = (*result)[0].GetUInt32())
                        _items.insert(itemLowGuid);
                    break;
                case GUID_TYPE_MAIL:
                    if (ObjectGuid::LowType mailLowGuid = (*result)[0].GetUInt32())
                        _mails.insert(mailLowGuid);
                    break;
                case GUID_TYPE_PET:
                    if (ObjectGuid::LowType petLowGuid = (*result)[0].GetUInt32())
                        _pets.insert(petLowGuid);
                    break;
                case GUID_TYPE_EQUIPMENT_SET:
                    if (uint64 eqSetId = (*result)[0].GetUInt64())
                        _itemSets.insert(eqSetId);
                    break;
                default:
                    break;
            }
        } while (result->NextRow());
    }
}

/**
 * @brief 追加单表数据到转储事务
 *
 * 根据表类型生成相应的查询条件，获取数据并追加到转储字符串。
 *
 * @param trans 字符串事务对象
 * @param guid 角色低GUID
 * @param tableStruct 表结构信息
 * @param dumpTable 转储表配置
 * @return true 追加成功
 * @return false 角色已删除（deleteInfos_Account非空）
 *
 * 根据表类型确定WHERE条件：
 * - DTT_ITEM/DTT_ITEM_GIFT: 使用物品GUID集合
 * - DTT_PET_TABLE: 使用宠物GUID集合
 * - DTT_MAIL_ITEM: 使用邮件ID集合
 * - DTT_EQSET_TABLE: 使用装备套装ID集合
 * - 其他: 使用角色GUID
 */
bool PlayerDumpWriter::AppendTable(StringTransaction& trans, ObjectGuid::LowType guid, TableStruct const& tableStruct, DumpTable const& dumpTable)
{
    std::string whereStr;
    switch (dumpTable.Type)
    {
        case DTT_ITEM:
        case DTT_ITEM_GIFT:
            if (_items.empty())
                return true;

            whereStr = GenerateWhereStr(tableStruct.WhereFieldName, _items);
            break;
        case DTT_PET_TABLE:
            if (_pets.empty())
                return true;

            whereStr = GenerateWhereStr(tableStruct.WhereFieldName, _pets);
            break;
        case DTT_MAIL_ITEM:
            if (_mails.empty())
                return true;

            whereStr = GenerateWhereStr(tableStruct.WhereFieldName, _mails);
            break;
        case DTT_EQSET_TABLE:
            if (_itemSets.empty())
                return true;

            whereStr = GenerateWhereStr(tableStruct.WhereFieldName, _itemSets);
            break;
        default:
            // 其他情况使用单GUID条件
            whereStr = GenerateWhereStr(tableStruct.WhereFieldName, guid);
            break;
    }

    // 执行查询
    QueryResult result = CharacterDatabase.PQuery("SELECT * FROM {} WHERE {}", dumpTable.Name, whereStr);
    switch (dumpTable.Type)
    {
        case DTT_CHARACTER:
            if (result)
            {
                // 检查角色是否已标记删除（deleteInfos_Account非空表示角色在删除队列中）
                int32 index = GetColumnIndexByName(tableStruct, "deleteInfos_Account");
                ASSERT(index != -1); // 启动时已检查

                if ((*result)[index].GetUInt32())
                    return false;
            }
            break;
        default:
            break;
    }

    // 追加数据到转储事务
    AppendTableDump(trans, tableStruct, result);
    return true;
}

/**
 * @brief 获取角色转储数据到字符串
 *
 * 核心导出函数，收集角色所有数据并生成SQL格式的转储字符串。
 *
 * @param guid 角色低GUID
 * @param dump [out] 输出的转储数据字符串
 * @return true 导出成功
 * @return false 导出失败（角色已删除或不存在）
 *
 * 导出流程：
 * 1. 添加安全警告头
 * 2. 收集关联对象GUID（宠物、邮件、物品、装备套装）
 * 3. 按依赖顺序遍历所有表并生成INSERT语句
 * 4. 检查角色是否在删除队列中
 *
 * @note 生成的转储数据只能通过PDUMP命令导入
 */
bool PlayerDumpWriter::GetDump(ObjectGuid::LowType guid, std::string& dump)
{
    // 添加重要的安全警告头
    dump =  "IMPORTANT NOTE: THIS DUMPFILE IS MADE FOR USE WITH THE 'PDUMP' COMMAND ONLY - EITHER THROUGH INGAME CHAT OR ON CONSOLE!\n";
    dump += "IMPORTANT NOTE: DO NOT apply it directly - it will irreversibly DAMAGE and CORRUPT your database! You have been warned!\n\n";

    StringTransaction trans;

    // 收集关联对象的GUID
    PopulateGuids(guid);

    // 按顺序导出所有表
    for (uint32 i = 0; i < DUMP_TABLE_COUNT; ++i)
        if (!AppendTable(trans, guid, CharacterTables[i], DumpTables[i]))
            return false;

    dump += trans.GetBuffer();

    /// @todo 添加副本/队伍数据
    /// @todo 添加转储级别选项以跳过某些非重要表

    return true;
}

/**
 * @brief 将角色数据导出到文件
 *
 * @param file 目标文件路径
 * @param guid 角色低GUID
 * @return DumpReturn 导出结果状态码
 *
 * 安全检查：
 * - CONFIG_PDUMP_NO_PATHS: 禁止使用路径分隔符，防止目录遍历
 * - CONFIG_PDUMP_NO_OVERWRITE: 禁止覆盖已存在的文件
 *
 * @see GetDump()
 */
DumpReturn PlayerDumpWriter::WriteDumpToFile(std::string const& file, ObjectGuid::LowType guid)
{
    // 安全检查：禁止使用路径
    if (sWorld->getBoolConfig(CONFIG_PDUMP_NO_PATHS))
        if (strchr(file.c_str(), '\\') || strchr(file.c_str(), '/'))
            return DUMP_FILE_OPEN_ERROR;

    // 安全检查：禁止覆盖已存在的文件
    if (sWorld->getBoolConfig(CONFIG_PDUMP_NO_OVERWRITE))
    {
        // 检查文件是否已存在
        if (GetFileHandle(file.c_str(), "r"))
            return DUMP_FILE_OPEN_ERROR;
    }

    // 打开文件进行写入
    FileHandle fout = GetFileHandle(file.c_str(), "w");
    if (!fout)
        return DUMP_FILE_OPEN_ERROR;

    // 执行导出
    DumpReturn ret = DUMP_SUCCESS;
    std::string dump;
    if (!GetDump(guid, dump))
        ret = DUMP_CHARACTER_DELETED;

    // 写入文件
    fprintf(fout.get(), "%s", dump.c_str());
    return ret;
}

/**
 * @brief 将角色数据导出到字符串
 *
 * @param dump [out] 输出的转储数据字符串
 * @param guid 角色低GUID
 * @return DumpReturn 导出结果状态码
 *
 * @note 此方法是 WriteDumpToFile 的字符串版本
 */
DumpReturn PlayerDumpWriter::WriteDumpToString(std::string& dump, ObjectGuid::LowType guid)
{
    DumpReturn ret = DUMP_SUCCESS;
    if (!GetDump(guid, dump))
        ret = DUMP_CHARACTER_DELETED;
    return ret;
}

// ============================================================================
// 导入相关高层函数
// ============================================================================

/**
 * @brief 修复SQL语句中的NULL字段
 *
 * 将转储文件中的字符串 'NULL' 转换为SQL的 NULL 关键字。
 *
 * @param line SQL语句字符串（会被修改）
 *
 * @note 导出时NULL值被存储为字符串 'NULL'，
 *       导入时需要转换回真正的NULL以匹配数据库定义
 */
inline void FixNULLfields(std::string& line)
{
    static std::string const NullString("'NULL'");
    size_t pos = line.find(NullString);
    while (pos != std::string::npos)
    {
        line.replace(pos, NullString.length(), "NULL");
        pos = line.find(NullString);
    }
}

/**
 * @brief 从输入流加载转储数据（核心实现）
 *
 * 解析转储文件/字符串，导入角色数据到数据库。
 * 自动处理GUID映射以避免与现有数据冲突。
 *
 * @param input 输入流
 * @param account 目标账号ID
 * @param name 新角色名称（可为空，使用原名或自动生成）
 * @param guid 指定的角色GUID（0表示自动分配）
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
 * GUID映射说明：
 * - 所有GUID（角色、物品、邮件、宠物、装备套装）都会被重新映射
 * - 新GUID = 当前最大GUID + 映射索引
 * - 这确保导入的角色不会与现有角色冲突
 */
DumpReturn PlayerDumpReader::LoadDump(std::istream& input, uint32 account, std::string name, ObjectGuid::LowType guid)
{
    // 检查账号角色数量限制（最多10个）
    uint32 charcount = AccountMgr::GetCharactersCount(account);
    if (charcount >= 10)
        return DUMP_TOO_MANY_CHARS;

    std::string newguid, chraccount;

    // 确定角色GUID：检查指定GUID是否可用
    bool incHighest = true;
    if (guid && guid < sObjectMgr->GetGenerator<HighGuid::Player>().GetNextAfterMaxUsed())
    {
        // 检查指定GUID是否已被占用
        CharacterDatabasePreparedStatement* stmt = CharacterDatabase.GetPreparedStatement(CHAR_SEL_CHECK_GUID);
        stmt->setUInt32(0, guid);

        if (PreparedQueryResult result = CharacterDatabase.Query(stmt))
            guid = sObjectMgr->GetGenerator<HighGuid::Player>().GetNextAfterMaxUsed(); // 已被占用，使用下一个可用GUID
        else
            incHighest = false; // GUID可用，不需要增加最大值计数器
    }
    else
        guid = sObjectMgr->GetGenerator<HighGuid::Player>().GetNextAfterMaxUsed();

    // 规范化并验证角色名称
    if (!normalizePlayerName(name))
        name.clear();

    if (ObjectMgr::CheckPlayerName(name, sWorld->GetDefaultDbcLocale(), true) == CHAR_NAME_SUCCESS)
    {
        // 检查名称是否已被使用
        CharacterDatabasePreparedStatement* stmt = CharacterDatabase.GetPreparedStatement(CHAR_SEL_CHECK_NAME);
        stmt->setString(0, name);

        if (PreparedQueryResult result = CharacterDatabase.Query(stmt))
            name.clear(); // 名称已被使用，使用转储文件中的原名
    }
    else
        name.clear();

    // 角色GUID和账号ID的字符串形式（用于SQL替换）
    newguid = std::to_string(guid);
    chraccount = std::to_string(account);

    // 初始化GUID映射表和偏移量
    std::map<ObjectGuid::LowType, ObjectGuid::LowType> items;
    ObjectGuid::LowType itemLowGuidOffset = sObjectMgr->GetGenerator<HighGuid::Item>().GetNextAfterMaxUsed();

    std::map<ObjectGuid::LowType, ObjectGuid::LowType> mails;
    ObjectGuid::LowType mailLowGuidOffset = sObjectMgr->_mailId;

    std::map<ObjectGuid::LowType, ObjectGuid::LowType> petIds;
    ObjectGuid::LowType petLowGuidOffset = sObjectMgr->_hiPetNumber;

    std::map<uint64, uint64> equipmentSetIds;
    uint64 equipmentSetGuidOffset = sObjectMgr->_equipmentSetGuid;

    std::string line;

    // 角色信息（用于更新缓存）
    uint8 gender = GENDER_NONE;
    uint8 race = RACE_NONE;
    uint8 playerClass = CLASS_NONE;
    uint8 level = 1;

    // 用于日志输出
    size_t lineNumber = 0;

    // 开始数据库事务
    CharacterDatabaseTransaction trans = CharacterDatabase.BeginTransaction();
    while (std::getline(input, line))
    {
        ++lineNumber;

        // 跳过空行
        size_t nw_pos = line.find_first_not_of(" \t\n\r\7");
        if (nw_pos == std::string::npos)
            continue;

        // 跳过安全警告头
        static std::string const SkippedLine = "IMPORTANT NOTE:";
        if (line.substr(nw_pos, SkippedLine.size()) == SkippedLine)
            continue;

        // 提取表名
        std::string tn = GetTableName(line);
        if (tn.empty())
        {
            TC_LOG_ERROR("misc", "LoadPlayerDump: (line {}) Can't extract table name!", lineNumber);
            return DUMP_FILE_BROKEN;
        }

        // 查找表类型
        DumpTableType type = DTT_CHARACTER;
        uint32 i;
        for (i = 0; i < DUMP_TABLE_COUNT; ++i)
        {
            if (tn == DumpTables[i].Name)
            {
                type = DumpTables[i].Type;
                break;
            }
        }

        if (i == DUMP_TABLE_COUNT)
        {
            TC_LOG_ERROR("misc", "LoadPlayerDump: (line {}) Unknown table: `{}`!", lineNumber, tn);
            return DUMP_FILE_BROKEN;
        }

        // 验证字段兼容性
        TableStruct const& ts = CharacterTables[i];
        if (!ValidateFields(ts, line, lineNumber))
            return DUMP_FILE_BROKEN;

        // 逐字段处理GUID重映射
        for (TableField const& field : ts.TableFields)
        {
            if (!field.IsDependentField)
                continue;

            switch (field.FieldGuidType)
            {
                case GUID_TYPE_ACCOUNT:
                    // 替换账号ID
                    if (!ChangeColumn(ts, line, field.FieldName, chraccount))
                        return DUMP_FILE_BROKEN;
                    break;
                case GUID_TYPE_CHAR:
                    // 替换角色GUID
                    if (!ChangeColumn(ts, line, field.FieldName, newguid))
                        return DUMP_FILE_BROKEN;
                    break;
                case GUID_TYPE_PET:
                    // 重映射宠物GUID
                    if (!ChangeGuid(ts, line, field.FieldName, petIds, petLowGuidOffset))
                        return DUMP_FILE_BROKEN;
                    break;
                case GUID_TYPE_MAIL:
                    // 重映射邮件ID
                    if (!ChangeGuid(ts, line, field.FieldName, mails, mailLowGuidOffset))
                        return DUMP_FILE_BROKEN;
                    break;
                case GUID_TYPE_ITEM:
                    // 重映射物品GUID（允许0值）
                    if (!ChangeGuid(ts, line, field.FieldName, items, itemLowGuidOffset, true))
                        return DUMP_FILE_BROKEN;
                    break;
                case GUID_TYPE_EQUIPMENT_SET:
                    // 重映射装备套装ID
                    if (!ChangeGuid(ts, line, field.FieldName, equipmentSetIds, equipmentSetGuidOffset))
                        return DUMP_FILE_BROKEN;
                    break;
                case GUID_TYPE_NULL:
                {
                    // 设置为NULL（用于删除标记字段）
                    static std::string const NullString("NULL");
                    if (!ChangeColumn(ts, line, field.FieldName, NullString))
                        return DUMP_FILE_BROKEN;
                    break;
                }
            }
        }

        // 特殊表的额外处理
        switch (type)
        {
            case DTT_CHARACTER:
            {
                // 提取角色信息用于缓存更新
                race = Trinity::StringTo<uint8>(GetColumn(ts, line, "race")).value_or<uint8>(0);
                playerClass = Trinity::StringTo<uint8>(GetColumn(ts, line, "class")).value_or<uint8>(0);
                gender = Trinity::StringTo<uint8>(GetColumn(ts, line, "gender")).value_or<uint8>(0);
                level = Trinity::StringTo<uint8>(GetColumn(ts, line, "level")).value_or<uint8>(0);

                if (name.empty())
                {
                    // 名称不可用，生成临时名称（原名称部分 + GUID十六进制）
                    std::string guidPart = Trinity::StringFormat("{:X}", guid);
                    std::size_t maxCharsFromOriginalName = MAX_PLAYER_NAME - guidPart.length();

                    name = GetColumn(ts, line, "name").substr(0, maxCharsFromOriginalName) + guidPart;

                    // 设置角色在登录时需要重命名
                    if (!ChangeColumn(ts, line, "name", name))
                        return DUMP_FILE_BROKEN;
                    if (!ChangeColumn(ts, line, "at_login", "1"))
                        return DUMP_FILE_BROKEN;
                }
                else if (!ChangeColumn(ts, line, "name", name))
                    return DUMP_FILE_BROKEN;
                break;
            }
            default:
                break;
        }

        // 修复NULL字段格式
        FixNULLfields(line);

        // 将SQL语句添加到事务
        trans->Append(line.c_str());
    }

    // 检查读取是否成功
    if (input.fail() && !input.eof())
        return DUMP_FILE_BROKEN;

    // 提交数据库事务
    CharacterDatabase.CommitTransaction(trans);

    // 添加角色到缓存（名称冲突时玩家需要在登录时重命名）
    sCharacterCache->AddCharacterCacheEntry(ObjectGuid(HighGuid::Player, guid), account, name, gender, race, playerClass, level);

    // 更新各类GUID计数器
    sObjectMgr->GetGenerator<HighGuid::Item>().Set(sObjectMgr->GetGenerator<HighGuid::Item>().GetNextAfterMaxUsed() + items.size());
    sObjectMgr->_mailId += mails.size();
    sObjectMgr->_hiPetNumber += petIds.size();
    sObjectMgr->_equipmentSetGuid += equipmentSetIds.size();

    // 如果使用了新GUID，增加玩家GUID计数器
    if (incHighest)
        sObjectMgr->GetGenerator<HighGuid::Player>().Generate();

    // 更新账号角色数量
    sWorld->UpdateRealmCharCount(account);

    return DUMP_SUCCESS;
}

/**
 * @brief 从字符串加载角色转储数据
 *
 * @param dump 转储数据字符串
 * @param account 目标账号ID
 * @param name 新角色名称（可为空）
 * @param guid 指定的角色GUID（0表示自动分配）
 * @return DumpReturn 导入结果状态码
 *
 * @note 此方法是 LoadDumpFromFile 的字符串版本，用于内存操作
 */
DumpReturn PlayerDumpReader::LoadDumpFromString(std::string const& dump, uint32 account, std::string name, ObjectGuid::LowType guid)
{
    std::istringstream input(dump);
    return LoadDump(input, account, name, guid);
}

/**
 * @brief 从文件加载角色转储数据
 *
 * @param file 转储文件路径
 * @param account 目标账号ID
 * @param name 新角色名称（可为空）
 * @param guid 指定的角色GUID（0表示自动分配）
 * @return DumpReturn 导入结果状态码
 *
 * @note 文件必须包含有效的SQL INSERT语句
 */
DumpReturn PlayerDumpReader::LoadDumpFromFile(std::string const& file, uint32 account, std::string name, ObjectGuid::LowType guid)
{
    std::ifstream input(file);
    if (!input)
        return DUMP_FILE_OPEN_ERROR;
    return LoadDump(input, account, name, guid);
}
