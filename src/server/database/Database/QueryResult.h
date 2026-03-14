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
 * @file QueryResult.h
 * @brief SQL查询结果集封装模块
 *
 * 本模块提供了对MySQL查询结果的封装，包括：
 * - ResultSet：普通查询结果集（文本协议）
 * - PreparedResultSet：预处理查询结果集（二进制协议）
 *
 * 两种结果集的区别：
 * - ResultSet：使用MySQL文本协议，数据以字符串形式传输，需要转换
 * - PreparedResultSet：使用MySQL预处理语句二进制协议，数据以二进制形式传输，更高效
 *
 * 使用场景：
 * - 执行SELECT查询后获取返回的数据行
 * - 遍历结果集中的每一行数据
 * - 通过Field类访问具体的字段值
 */

#ifndef QUERYRESULT_H
#define QUERYRESULT_H

#include "Define.h"
#include "DatabaseEnvFwd.h"
#include <vector>

/**
 * @class ResultSet
 * @brief 普通SQL查询结果集封装类
 *
 * 该类封装了MySQL文本协议查询的结果，提供逐行访问数据的能力。
 * 使用MySQL C API的原生结果集结构，通过mysql_fetch_row()遍历数据。
 *
 * 特点：
 * - 使用MySQL文本协议，数据以字符串形式传输
 * - 支持逐行读取，不一次性加载所有数据到内存
 * - 通过Field对象访问字段值，自动进行类型转换
 *
 * 生命周期：
 * 1. 执行SQL查询后创建ResultSet对象
 * 2. 调用NextRow()遍历每一行数据
 * 3. 通过Fetch()或operator[]访问字段
 * 4. 遍历完成后自动清理资源
 *
 * 使用示例：
 * @code
 * QueryResult result = Database.Query("SELECT * FROM players");
 * if (result)
 * {
 *     do
 *     {
 *         Field* fields = result->Fetch();
 *         uint32 id = fields[0].GetUInt32();
 *         std::string name = fields[1].GetString();
 *     } while (result->NextRow());
 * }
 * @endcode
 *
 * 线程安全：非线程安全，应在单线程中使用
 */
class TC_DATABASE_API ResultSet
{
    public:
        /**
         * @brief 构造函数
         *
         * @brief 简要说明：使用MySQL原生结果集创建ResultSet对象
         * @param result MySQL结果集对象指针
         * @param fields MySQL字段元数据数组指针
         * @param rowCount 结果行数
         * @param fieldCount 字段数量
         *
         * 调用时机：执行普通SQL查询后由数据库层创建
         * 性能注意事项：会分配Field数组内存，开销与字段数成正比
         */
        ResultSet(MySQLResult* result, MySQLField* fields, uint64 rowCount, uint32 fieldCount);

        /**
         * @brief 析构函数，清理MySQL结果集资源
         */
        ~ResultSet();

        /**
         * @brief 移动到下一行
         *
         * @brief 简要说明：从结果集中读取下一行数据
         * @return bool 成功读取返回true，没有更多行或出错返回false
         *
         * 调用时机：在循环中反复调用以遍历所有行
         * 性能注意事项：会更新_currentRow的值，开销较小
         *
         * 执行流程：
         * 1. 调用mysql_fetch_row()获取下一行
         * 2. 如果没有更多行，清理资源并返回false
         * 3. 更新_currentRow字段的值
         */
        bool NextRow();

        /**
         * @brief 获取结果行数
         *
         * @brief 简要说明：返回结果集中的总行数
         * @return uint64 行数
         *
         * 调用时机：在遍历前获取总行数用于进度显示等
         * 性能注意事项：O(1)时间复杂度
         */
        uint64 GetRowCount() const { return _rowCount; }

        /**
         * @brief 获取字段数量
         *
         * @brief 简要说明：返回每行的字段数量
         * @return uint32 字段数
         *
         * 调用时机：在访问字段前确定字段数量
         * 性能注意事项：O(1)时间复杂度
         */
        uint32 GetFieldCount() const { return _fieldCount; }

        /**
         * @brief 获取当前行数据
         *
         * @brief 简要说明：返回当前行的字段数组指针
         * @return Field* 字段数组指针，可通过索引访问各字段
         *
         * 调用时机：在NextRow()返回true后调用
         * 性能注意事项：O(1)时间复杂度，返回指针
         */
        Field* Fetch() const { return _currentRow; }

        /**
         * @brief 数组访问运算符
         *
         * @brief 简要说明：通过索引访问当前行的字段
         * @param index 字段索引（从0开始）
         * @return Field const& 字段常量引用
         *
         * 调用时机：访问特定字段时使用
         * 性能注意事项：O(1)时间复杂度
         */
        Field const& operator[](std::size_t index) const;

    protected:
        /**
         * @brief 字段元数据数组
         *
         * 存储每个字段的元数据，包括表名、字段名、类型等信息
         */
        std::vector<QueryResultFieldMetadata> _fieldMetadata;

        /**
         * @brief 结果集总行数
         */
        uint64 _rowCount;

        /**
         * @brief 当前行数据数组
         *
         * 指向Field数组的指针，每个元素对应一个字段
         * NextRow()会更新此数组的内容
         */
        Field* _currentRow;

        /**
         * @brief 字段数量
         */
        uint32 _fieldCount;

    private:
        /**
         * @brief 清理资源
         *
         * 释放MySQL结果集和Field数组内存
         */
        void CleanUp();

        /**
         * @brief MySQL原生结果集对象
         */
        MySQLResult* _result;

        /**
         * @brief MySQL字段元数据数组
         */
        MySQLField* _fields;

        // 禁用拷贝构造和赋值，确保资源唯一所有权
        ResultSet(ResultSet const& right) = delete;
        ResultSet& operator=(ResultSet const& right) = delete;
};

/**
 * @class PreparedResultSet
 * @brief 预处理SQL查询结果集封装类
 *
 * 该类封装了MySQL预处理语句查询的结果，与ResultSet不同，它使用二进制协议。
 * 在构造时会一次性加载所有结果数据到内存，然后支持随机访问。
 *
 * 特点：
 * - 使用MySQL二进制协议，数据传输更高效
 * - 一次性加载所有结果到内存，支持随机访问
 * - 通过Field对象访问字段值，自动进行类型转换
 * - 支持NULL值和二进制数据
 *
 * 与ResultSet的区别：
 * - ResultSet使用文本协议，数据以字符串形式传输
 * - PreparedResultSet使用二进制协议，数据保持原生格式
 * - PreparedResultSet在构造时加载所有数据，ResultSet逐行加载
 *
 * 使用示例：
 * @code
 * PreparedStatement* stmt = Database.GetPreparedStatement(CHAR_SEL_PLAYER);
 * stmt->setUInt32(0, playerId);
 * PreparedQueryResult result = Database.Query(stmt);
 * if (result)
 * {
 *     do
 *     {
 *         Field* fields = result->Fetch();
 *         uint32 id = fields[0].GetUInt32();
 *         std::string name = fields[1].GetString();
 *     } while (result->NextRow());
 * }
 * @endcode
 *
 * 线程安全：非线程安全，应在单线程中使用
 */
class TC_DATABASE_API PreparedResultSet
{
    public:
        /**
         * @brief 构造函数
         *
         * @brief 简要说明：创建预处理结果集并加载所有数据
         * @param stmt MySQL预处理语句对象指针
         * @param result MySQL结果元数据对象指针
         * @param rowCount 预估行数
         * @param fieldCount 字段数量
         *
         * 调用时机：执行预处理查询后由数据库层创建
         * 性能注意事项：会加载所有数据到内存，开销与结果集大小成正比
         *
         * 初始化流程：
         * 1. 分配绑定缓冲区
         * 2. 调用mysql_stmt_store_result()加载所有数据
         * 3. 根据字段元数据设置绑定
         * 4. 遍历所有行，将数据存入m_rows向量
         */
        PreparedResultSet(MySQLStmt* stmt, MySQLResult* result, uint64 rowCount, uint32 fieldCount);

        /**
         * @brief 析构函数，清理MySQL资源
         */
        ~PreparedResultSet();

        /**
         * @brief 移动到下一行
         *
         * @brief 简要说明：将当前行位置移动到下一行
         * @return bool 成功返回true，已到末尾返回false
         *
         * 调用时机：在循环中反复调用以遍历所有行
         * 性能注意事项：O(1)时间复杂度，只是更新位置索引
         *
         * 注意：与ResultSet不同，这里不会真正加载数据，数据已在构造时加载
         */
        bool NextRow();

        /**
         * @brief 获取结果行数
         *
         * @brief 简要说明：返回结果集中的总行数
         * @return uint64 行数
         *
         * 调用时机：在遍历前获取总行数
         * 性能注意事项：O(1)时间复杂度
         */
        uint64 GetRowCount() const { return m_rowCount; }

        /**
         * @brief 获取字段数量
         *
         * @brief 简要说明：返回每行的字段数量
         * @return uint32 字段数
         *
         * 调用时机：在访问字段前确定字段数量
         * 性能注意事项：O(1)时间复杂度
         */
        uint32 GetFieldCount() const { return m_fieldCount; }

        /**
         * @brief 获取当前行数据
         *
         * @brief 简要说明：返回当前行的字段数组指针
         * @return Field* 字段数组指针
         *
         * 调用时机：在NextRow()返回true后调用
         * 性能注意事项：O(1)时间复杂度
         */
        Field* Fetch() const;

        /**
         * @brief 数组访问运算符
         *
         * @brief 简要说明：通过索引访问当前行的字段
         * @param index 字段索引（从0开始）
         * @return Field const& 字段常量引用
         *
         * 调用时机：访问特定字段时使用
         * 性能注意事项：O(1)时间复杂度
         */
        Field const& operator[](std::size_t index) const;

    protected:
        /**
         * @brief 字段元数据数组
         *
         * 存储每个字段的元数据，包括表名、字段名、类型等信息
         */
        std::vector<QueryResultFieldMetadata> m_fieldMetadata;

        /**
         * @brief 所有行的数据存储
         *
         * 一次性存储所有行的所有字段数据
         * 布局：[row0_field0, row0_field1, ..., row1_field0, row1_field1, ...]
         */
        std::vector<Field> m_rows;

        /**
         * @brief 结果集总行数
         */
        uint64 m_rowCount;

        /**
         * @brief 当前行位置索引
         *
         * 范围从0到m_rowCount-1，NextRow()会递增此值
         */
        uint64 m_rowPosition;

        /**
         * @brief 字段数量
         */
        uint32 m_fieldCount;

    private:
        /**
         * @brief MySQL绑定结构数组
         *
         * 用于mysql_stmt_bind_result()，描述如何接收每个字段的数据
         */
        MySQLBind* m_rBind;

        /**
         * @brief MySQL预处理语句对象
         */
        MySQLStmt* m_stmt;

        /**
         * @brief 字段元数据结果集
         *
         * 由mysql_stmt_result_metadata()返回，包含字段信息
         */
        MySQLResult* m_metadataResult;

        /**
         * @brief 清理资源
         *
         * 释放MySQL元数据结果集和绑定缓冲区
         */
        void CleanUp();

        /**
         * @brief 内部方法：获取下一行数据
         *
         * @brief 简要说明：从MySQL获取下一行数据到缓冲区
         * @return bool 成功返回true，失败或无数据返回false
         *
         * 调用时机：仅在构造函数中调用
         * 性能注意事项：会调用mysql_stmt_fetch()，开销取决于数据大小
         */
        bool _NextRow();

        // 禁用拷贝构造和赋值，确保资源唯一所有权
        PreparedResultSet(PreparedResultSet const& right) = delete;
        PreparedResultSet& operator=(PreparedResultSet const& right) = delete;
};

#endif
