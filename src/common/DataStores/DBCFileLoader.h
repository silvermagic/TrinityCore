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
 * @file DBCFileLoader.h
 * @brief DBC 数据文件加载器
 *
 * 本文件实现了 DBC (Database Cache) 文件的加载和解析功能。
 * DBC 是 Blizzard 游戏使用的一种二进制数据库缓存文件格式，
 * 用于存储游戏配置数据，如法术信息、物品属性、地图数据等。
 *
 * DBC 文件格式结构：
 * - 文件头 (Header, 20 字节)：
 *   - 魔数 'WDBC' (4 字节)：用于标识 DBC 文件
 *   - 记录数量 (4 字节)：数据区中的记录总数
 *   - 字段数量 (4 字节)：每条记录包含的字段数
 *   - 记录大小 (4 字节)：单条记录的字节大小
 *   - 字符串表大小 (4 字节)：字符串表的总字节数
 * - 数据区 (Data Section)：recordCount × recordSize 字节
 * - 字符串表 (String Table)：stringSize 字节，存储所有字符串数据
 *
 * 字段类型：
 * - 字符串字段存储的是相对字符串表的偏移量
 * - 数值字段直接存储数据
 */

#ifndef DBC_FILE_LOADER_H
#define DBC_FILE_LOADER_H

#include "Define.h"
#include "Errors.h"
#include "Utilities/ByteConverter.h"

/**
 * @brief DBC 字段格式枚举
 *
 * 定义 DBC 文件中各字段的数据类型。
 * 这些格式标识符用于解析和验证 DBC 文件的数据结构。
 */
enum DbcFieldFormat
{
    FT_NA='x',                                              ///< 未使用或未知类型，4 字节大小
    FT_NA_BYTE='X',                                         ///< 未使用或未知类型，1 字节大小
    FT_STRING='s',                                          ///< 字符串指针 (char*)，存储字符串表偏移量
    FT_FLOAT='f',                                           ///< 单精度浮点数 (float)，4 字节
    FT_INT='i',                                             ///< 无符号 32 位整数 (uint32)，4 字节
    FT_BYTE='b',                                            ///< 无符号 8 位整数 (uint8)，1 字节
    FT_SORT='d',                                            ///< 排序字段，用于排序但不包含在数据中
    FT_IND='n',                                             ///< 索引字段，类似于 FT_SORT 但解析到数据中
    FT_LOGIC='l',                                           ///< 布尔逻辑值 (boolean)
    FT_SQL_PRESENT='p',                                     ///< SQL 格式中标记该列存在于 SQL DBC 表中
    FT_SQL_ABSENT='a'                                       ///< SQL 格式中标记该列不存在于 SQL DBC 表中
};

/**
 * @brief DBC 文件加载器类
 *
 * 负责加载、解析和管理 DBC 文件数据。
 * 提供记录访问接口，支持按字段类型读取数据。
 *
 * 主要功能：
 * - 加载 DBC 文件到内存
 * - 解析文件头和数据区
 * - 提供记录迭代和字段访问接口
 * - 支持字符串表查找
 * - 自动生成数据结构和字符串表
 *
 * 使用示例：
 * @code
 * DBCFileLoader dbc;
 * if (dbc.Load("spell.dbc", "niifffs"))
 * {
 *     for (uint32 i = 0; i < dbc.GetNumRows(); ++i)
 *     {
 *         DBCFileLoader::Record record = dbc.getRecord(i);
 *         uint32 id = record.getUInt(0);
 *         const char* name = record.getString(5);
 *     }
 * }
 * @endcode
 */
class TC_COMMON_API DBCFileLoader
{
    public:
        /**
         * @brief 默认构造函数
         *
         * 初始化所有成员变量为默认值。
         */
        DBCFileLoader();

        /**
         * @brief 析构函数
         *
         * 释放已分配的内存资源，包括数据区和字符串表。
         */
        ~DBCFileLoader();

        /**
         * @brief 加载 DBC 文件
         *
         * 从指定路径加载 DBC 文件并解析其内容。
         * 根据提供的格式字符串验证字段类型和数量。
         *
         * @param filename DBC 文件的路径
         * @param fmt 字段格式字符串，由 DbcFieldFormat 枚举字符组成
         * @return 加载成功返回 true，失败返回 false
         *
         * @note 格式字符串长度应与 DBC 文件的字段数量匹配
         */
        bool Load(const char *filename, const char *fmt);

        /**
         * @brief DBC 记录访问类
         *
         * 提供对单条 DBC 记录的字段访问接口。
         * 通过偏移量和字段信息读取各种类型的数据。
         *
         * 该类为内部类，由 DBCFileLoader 创建和管理，
         * 不应由用户直接实例化。
         */
        class Record
        {
            public:
                /**
                 * @brief 获取指定字段的浮点数值
                 *
                 * @param field 字段索引 (从 0 开始)
                 * @return 字段对应的浮点数值
                 * @note 会自动处理字节序转换
                 */
                float getFloat(size_t field) const
                {
                    ASSERT(field < file.fieldCount);
                    float val = *reinterpret_cast<float*>(offset+file.GetOffset(field));
                    EndianConvert(val);
                    return val;
                }

                /**
                 * @brief 获取指定字段的无符号 32 位整数值
                 *
                 * @param field 字段索引 (从 0 开始)
                 * @return 字段对应的 32 位无符号整数值
                 * @note 会自动处理字节序转换
                 */
                uint32 getUInt(size_t field) const
                {
                    ASSERT(field < file.fieldCount);
                    uint32 val = *reinterpret_cast<uint32*>(offset+file.GetOffset(field));
                    EndianConvert(val);
                    return val;
                }

                /**
                 * @brief 获取指定字段的无符号 8 位整数值
                 *
                 * @param field 字段索引 (从 0 开始)
                 * @return 字段对应的 8 位无符号整数值
                 */
                uint8 getUInt8(size_t field) const
                {
                    ASSERT(field < file.fieldCount);
                    return *reinterpret_cast<uint8*>(offset+file.GetOffset(field));
                }

                /**
                 * @brief 获取指定字段的字符串
                 *
                 * 字符串字段存储的是相对字符串表的偏移量，
                 * 该方法自动解析偏移量并返回实际的字符串指针。
                 *
                 * @param field 字段索引 (从 0 开始)
                 * @return 指向字符串数据的常量指针
                 * @note 返回的指针指向 DBC 文件的字符串表，
                 *       该内存在 DBCFileLoader 销毁前一直有效
                 */
                const char *getString(size_t field) const
                {
                    ASSERT(field < file.fieldCount);
                    size_t stringOffset = getUInt(field);
                    ASSERT(stringOffset < file.stringSize);
                    return reinterpret_cast<char*>(file.stringTable + stringOffset);
                }

            private:
                /**
                 * @brief 私有构造函数
                 *
                 * 由 DBCFileLoader 调用创建记录对象。
                 *
                 * @param file_ 所属的 DBC 文件加载器引用
                 * @param offset_ 记录数据在数据区中的起始偏移量
                 */
                Record(DBCFileLoader &file_, unsigned char *offset_): offset(offset_), file(file_) { }

                unsigned char *offset;  ///< 记录数据在数据区中的起始地址
                DBCFileLoader &file;    ///< 所属的 DBC 文件加载器引用

                friend class DBCFileLoader;

        };

        /**
         * @brief 获取指定 ID 的记录
         *
         * 根据索引返回对应的记录对象，用于访问该记录的所有字段。
         *
         * @param id 记录索引 (0 到 recordCount-1)
         * @return 记录对象，可通过其方法访问字段数据
         */
        Record getRecord(size_t id);

        /**
         * @brief 获取记录总数
         *
         * @return DBC 文件中包含的记录数量
         */
        uint32 GetNumRows() const { return recordCount; }

        /**
         * @brief 获取单条记录的大小
         *
         * @return 单条记录的字节大小
         */
        uint32 GetRowSize() const { return recordSize; }

        /**
         * @brief 获取字段数量
         *
         * @return 每条记录包含的字段数量
         */
        uint32 GetCols() const { return fieldCount; }

        /**
         * @brief 获取指定字段的偏移量
         *
         * 返回字段在记录中的字节偏移量。
         *
         * @param id 字段索引 (从 0 开始)
         * @return 字段在记录中的字节偏移量，如果无效则返回 0
         */
        uint32 GetOffset(size_t id) const { return (fieldsOffset != nullptr && id < fieldCount) ? fieldsOffset[id] : 0; }

        /**
         * @brief 检查是否已加载 DBC 文件
         *
         * @return 如果已成功加载 DBC 文件返回 true，否则返回 false
         */
        bool IsLoaded() const { return data != nullptr; }

        /**
         * @brief 自动生成数据表
         *
         * 根据格式字符串自动生成数据表结构，
         * 返回的数据表可直接用于存储到内存数据库中。
         *
         * @param fmt 字段格式字符串
         * @param count [out] 返回生成的记录数量
         * @param indexTable [out] 返回索引表指针数组
         * @return 生成的数据表指针，失败返回 nullptr
         */
        char* AutoProduceData(char const* fmt, uint32& count, char**& indexTable);

        /**
         * @brief 自动生成字符串表
         *
         * 从数据表中提取所有字符串字段，
         * 生成统一的字符串表供本地化使用。
         *
         * @param fmt 字段格式字符串
         * @param dataTable 已生成的数据表指针
         * @return 生成的字符串表指针，失败返回 nullptr
         */
        char* AutoProduceStrings(char const* fmt, char* dataTable);

        /**
         * @brief 计算格式字符串对应的记录大小
         *
         * 根据字段格式字符串计算单条记录应有的字节大小。
         * 可选地返回索引字段的位置。
         *
         * @param format 字段格式字符串
         * @param index_pos [out] 可选参数，返回索引字段的位置
         * @return 计算得出的记录大小 (字节数)
         */
        static uint32 GetFormatRecordSize(const char * format, int32 * index_pos = nullptr);

    private:
        uint32 recordSize;          ///< 单条记录的字节大小
        uint32 recordCount;         ///< 记录总数
        uint32 fieldCount;          ///< 每条记录的字段数量
        uint32 stringSize;          ///< 字符串表的总字节大小
        uint32 *fieldsOffset;       ///< 字段偏移量数组，存储每个字段在记录中的偏移
        unsigned char *data;        ///< 数据区指针，存储所有记录的原始数据
        unsigned char *stringTable; ///< 字符串表指针，存储所有字符串数据

        /// 禁止拷贝构造
        DBCFileLoader(DBCFileLoader const& right) = delete;
        /// 禁止赋值操作
        DBCFileLoader& operator=(DBCFileLoader const& right) = delete;
};
#endif
