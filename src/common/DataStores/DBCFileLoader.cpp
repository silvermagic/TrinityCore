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
 * @file DBCFileLoader.cpp
 * @brief DBC 文件加载器实现
 *
 * DBC (Database Client) 文件是魔兽世界客户端使用的数据库文件格式。
 * 本文件实现了 DBC 文件的加载、解析和数据处理功能。
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "DBCFileLoader.h"
#include "Errors.h"

/**
 * @brief 构造函数，初始化 DBC 文件加载器的所有成员变量
 *
 * 职责：
 *   初始化所有成员变量为零值或空指针
 *
 * 主要流程：
 *   1. 将记录大小、记录数、字段数、字符串大小初始化为 0
 *   2. 将字段偏移数组、数据指针、字符串表指针初始化为 nullptr
 */
DBCFileLoader::DBCFileLoader() : recordSize(0), recordCount(0), fieldCount(0), stringSize(0), fieldsOffset(nullptr), data(nullptr), stringTable(nullptr) { }

/**
 * @brief 加载 DBC 文件并解析其结构
 *
 * 职责：
 *   从磁盘加载 DBC 文件，解析文件头信息，计算字段偏移量，读取数据到内存
 *
 * 参数：
 *   filename - DBC 文件的完整路径
 *   fmt      - 字段格式字符串，描述每个字段的数据类型（'b'=字节, 'X'=字节, 其他=4字节）
 *
 * 返回值：
 *   true  - 加载成功
 *   false - 加载失败（文件不存在、格式错误、读取失败等）
 *
 * 主要流程：
 *   1. 清理之前加载的数据
 *   2. 打开 DBC 文件（二进制模式）
 *   3. 读取并验证文件头（必须为 'WDBC' 魔数）
 *   4. 读取记录数、字段数、记录大小、字符串表大小
 *   5. 根据格式字符串计算每个字段的偏移量
 *   6. 分配内存并读取所有记录数据和字符串表
 *   7. 关闭文件
 *
 * DBC 文件结构：
 *   Header (20 bytes):
 *     - 魔数 'WDBC' (4 bytes)
 *     - 记录数 (4 bytes)
 *     - 字段数 (4 bytes)
 *     - 记录大小 (4 bytes)
 *     - 字符串表大小 (4 bytes)
 *   Data:
 *     - 记录数据 (recordSize * recordCount bytes)
 *     - 字符串表 (stringSize bytes)
 */
bool DBCFileLoader::Load(char const* filename, char const* fmt)
{
    uint32 header;
    // 如果之前已加载数据，先清理
    if (data)
    {
        delete [] data;
        data = nullptr;
    }

    // 以二进制模式打开 DBC 文件
    FILE* f = fopen(filename, "rb");
    if (!f)
        return false;

    // 读取并验证魔数 'WDBC'
    if (fread(&header, 4, 1, f) != 1)                        // Number of records
    {
        fclose(f);
        return false;
    }

    EndianConvert(header);

    if (header != 0x43424457)                                //'WDBC'
    {
        fclose(f);
        return false;
    }

    // 读取记录数量
    if (fread(&recordCount, 4, 1, f) != 1)                   // Number of records
    {
        fclose(f);
        return false;
    }

    EndianConvert(recordCount);

    // 读取字段数量
    if (fread(&fieldCount, 4, 1, f) != 1)                    // Number of fields
    {
        fclose(f);
        return false;
    }

    EndianConvert(fieldCount);

    // 读取单条记录的大小
    if (fread(&recordSize, 4, 1, f) != 1)                    // Size of a record
    {
        fclose(f);
        return false;
    }

    EndianConvert(recordSize);

    // 读取字符串表的总大小
    if (fread(&stringSize, 4, 1, f) != 1)                    // String size
    {
        fclose(f);
        return false;
    }

    EndianConvert(stringSize);

    // 计算每个字段在记录中的偏移量
    fieldsOffset = new uint32[fieldCount];
    fieldsOffset[0] = 0;
    for (uint32 i = 1; i < fieldCount; ++i)
    {
        fieldsOffset[i] = fieldsOffset[i - 1];
        if (fmt[i - 1] == 'b' || fmt[i - 1] == 'X')         // byte fields
            fieldsOffset[i] += sizeof(uint8);
        else                                                // 4 byte fields (int32/float/strings)
            fieldsOffset[i] += sizeof(uint32);
    }

    // 分配内存：记录数据 + 字符串表
    data = new unsigned char[recordSize * recordCount + stringSize];
    stringTable = data + recordSize*recordCount;

    // 一次性读取所有记录数据和字符串表
    if (fread(data, recordSize * recordCount + stringSize, 1, f) != 1)
    {
        fclose(f);
        return false;
    }

    fclose(f);

    return true;
}

/**
 * @brief 析构函数，释放动态分配的内存
 *
 * 职责：
 *   清理 DBC 文件加载器占用的所有动态内存
 *
 * 主要流程：
 *   1. 释放记录数据和字符串表占用的内存（同一块内存）
 *   2. 释放字段偏移数组
 */
DBCFileLoader::~DBCFileLoader()
{
    delete[] data;

    delete[] fieldsOffset;
}

/**
 * @brief 获取指定索引的记录对象
 *
 * 职责：
 *   根据索引返回记录的封装对象，用于访问该记录中的各个字段
 *
 * 参数：
 *   id - 记录的索引位置（从 0 开始）
 *
 * 返回值：
 *   Record 对象，提供了访问记录字段的方法
 *
 * 主要流程：
 *   1. 断言数据已加载
 *   2. 计算记录在数据缓冲区中的起始地址
 *   3. 构造并返回 Record 对象
 */
DBCFileLoader::Record DBCFileLoader::getRecord(size_t id)
{
    ASSERT(data);
    return Record(*this, data + id * recordSize);
}

/**
 * @brief 根据格式字符串计算记录的结构大小
 *
 * 职责：
 *   遍历格式字符串，计算转换后的结构体大小，并确定索引字段位置
 *
 * 参数：
 *   format    - 字段格式字符串，描述每个字段的数据类型
 *   index_pos - 输出参数，返回索引字段的位置（如果有）
 *
 * 返回值：
 *   记录的结构体大小（字节）
 *
 * 主要流程：
 *   1. 遍历格式字符串中的每个字符
 *   2. 根据字段类型累加结构体大小：
 *      - FT_FLOAT:  float 大小
 *      - FT_INT:    uint32 大小
 *      - FT_STRING: char* 指针大小
 *      - FT_BYTE:   uint8 大小
 *      - FT_IND:    uint32 大小，同时标记为索引字段
 *      - FT_SORT:   仅标记索引位置，不占用空间
 *      - FT_NA/FT_NA_BYTE: 不占用空间
 *      - FT_LOGIC:  触发错误（不支持）
 *   3. 通过 index_pos 返回索引字段位置
 *
 * 字段格式字符说明：
 *   FT_FLOAT ('f') - 浮点数字段
 *   FT_INT   ('i') - 整数字段
 *   FT_STRING ('s') - 字符串字段（存储为指针）
 *   FT_BYTE  ('b') - 字节字段
 *   FT_IND   ('n') - 索引字段（用于快速查找）
 *   FT_SORT  ('s') - 排序字段
 *   FT_NA    ('x') - 不存在的字段
 *   FT_NA_BYTE ('X') - 不存在的字节字段
 *   FT_LOGIC ('l') - 逻辑字段（已废弃）
 */
uint32 DBCFileLoader::GetFormatRecordSize(char const* format, int32* index_pos)
{
    uint32 recordsize = 0;
    int32 i = -1;
    for (uint32 x = 0; format[x]; ++x)
    {
        switch (format[x])
        {
            case FT_FLOAT:
                recordsize += sizeof(float);
                break;
            case FT_INT:
                recordsize += sizeof(uint32);
                break;
            case FT_STRING:
                recordsize += sizeof(char*);
                break;
            case FT_SORT:
                i = x;
                break;
            case FT_IND:
                i = x;
                recordsize += sizeof(uint32);
                break;
            case FT_BYTE:
                recordsize += sizeof(uint8);
                break;
            case FT_NA:
            case FT_NA_BYTE:
                break;
            case FT_LOGIC:
                ABORT_MSG("Attempted to load DBC files that do not have field types that match what is in the core. Check DBCfmt.h or your DBC files.");
                break;
            default:
                ABORT_MSG("Unknown field format character in DBCfmt.h");
                break;
        }
    }

    if (index_pos)
        *index_pos = i;

    return recordsize;
}

/**
 * @brief 自动生成数据表并构建索引
 *
 * 职责：
 *   将 DBC 文件中的原始数据转换为结构化的数据表，并根据索引字段构建查找表
 *
 * 参数：
 *   format    - 字段格式字符串
 *   records   - 输出参数，返回记录数（如果有索引，则为最大索引值+1）
 *   indexTable - 输出参数，返回索引表指针数组
 *
 * 返回值：
 *   指向数据表的指针，失败返回 nullptr
 *
 * 主要流程：
 *   1. 验证格式字符串长度与字段数匹配
 *   2. 计算结构体大小并确定索引字段位置
 *   3. 如果有索引字段：
 *      - 遍历所有记录找到最大索引值
 *      - 分配索引表（大小为最大索引值+1）
 *      - 清零索引表
 *   4. 否则：
 *      - 索引表大小等于记录数
 *   5. 分配数据表内存
 *   6. 遍历所有记录：
 *      - 根据索引字段填充索引表
 *      - 根据格式字符串提取各字段数据
 *      - 字符串字段暂时设置为 nullptr（后续由 AutoProduceStrings 填充）
 *   7. 返回数据表指针
 *
 * 示例：
 *   格式字符串 "sxfi" 表示：
 *   struct {
 *       char* field0;  // FT_STRING
 *       // field1 跳过 (FT_NA)
 *       float field2;  // FT_FLOAT
 *       uint32 field3; // FT_INT
 *   } entry;
 *   本函数将生成 entry[rows] 数据
 */
char* DBCFileLoader::AutoProduceData(char const* format, uint32& records, char**& indexTable)
{
    /*
    format STRING, NA, FLOAT, NA, INT <=>
    struct{
    char* field0,
    float field1,
    int field2
    }entry;

    this func will generate  entry[rows] data;
    */

    typedef char* ptr;
    // 验证格式字符串长度与字段数是否匹配
    if (strlen(format) != fieldCount)
        return nullptr;

    // 获取结构体大小和索引字段位置
    int32 i;
    uint32 recordsize = GetFormatRecordSize(format, &i);

    // 如果有索引字段，构建基于索引的查找表
    if (i >= 0)
    {
        uint32 maxi = 0;
        // 查找最大索引值
        for (uint32 y = 0; y < recordCount; ++y)
        {
            uint32 ind = getRecord(y).getUInt(i);
            if (ind > maxi)
                maxi = ind;
        }

        ++maxi;
        records = maxi;
        // 分配索引表并清零
        indexTable = new ptr[maxi];
        memset(indexTable, 0, maxi * sizeof(ptr));
    }
    else
    {
        // 没有索引字段，索引表大小等于记录数
        records = recordCount;
        indexTable = new ptr[recordCount];
    }

    // 分配数据表内存
    char* dataTable = new char[recordCount * recordsize];

    uint32 offset = 0;

    // 遍历所有记录，提取字段数据
    for (uint32 y = 0; y < recordCount; ++y)
    {
        // 填充索引表
        if (i >= 0)
            indexTable[getRecord(y).getUInt(i)] = &dataTable[offset];
        else
            indexTable[y] = &dataTable[offset];

        // 根据格式提取各字段
        for (uint32 x=0; x < fieldCount; ++x)
        {
            switch (format[x])
            {
                case FT_FLOAT:
                    *((float*)(&dataTable[offset])) = getRecord(y).getFloat(x);
                    offset += sizeof(float);
                    break;
                case FT_IND:
                case FT_INT:
                    *((uint32*)(&dataTable[offset])) = getRecord(y).getUInt(x);
                    offset += sizeof(uint32);
                    break;
                case FT_BYTE:
                    *((uint8*)(&dataTable[offset])) = getRecord(y).getUInt8(x);
                    offset += sizeof(uint8);
                    break;
                case FT_STRING:
                    // 字符串字段暂时设置为 nullptr，将在 AutoProduceStrings 中填充
                    *((char**)(&dataTable[offset])) = nullptr;   // will replace non-empty or "" strings in AutoProduceStrings
                    offset += sizeof(char*);
                    break;
                case FT_LOGIC:
                    ABORT_MSG("Attempted to load DBC files that do not have field types that match what is in the core. Check DBCfmt.h or your DBC files.");
                    break;
                case FT_NA:
                case FT_NA_BYTE:
                case FT_SORT:
                    // 这些字段类型不占用空间
                    break;
                default:
                    ABORT_MSG("Unknown field format character in DBCfmt.h");
                    break;
            }
        }
    }

    return dataTable;
}

/**
 * @brief 自动生成字符串池并填充字符串指针
 *
 * 职责：
 *   复制字符串表到新的内存池，并将数据表中的字符串字段指针指向字符串池中的对应位置
 *
 * 参数：
 *   format   - 字段格式字符串
 *   dataTable - AutoProduceData 生成的数据表指针
 *
 * 返回值：
 *   指向字符串池的指针，失败返回 nullptr
 *
 * 主要流程：
 *   1. 验证格式字符串长度与字段数匹配
 *   2. 复制原始字符串表到新的字符串池
 *   3. 遍历所有记录的所有字段
 *   4. 对于字符串类型字段：
 *      - 如果该位置尚未填充（为 nullptr 或空字符串）
 *      - 获取记录中的字符串（相对于原始字符串表的偏移）
 *      - 计算在新字符串池中的对应位置
 *      - 填充字符串指针
 *   5. 返回字符串池指针
 *
 * 注意：
 *   字符串在 DBC 文件中以偏移量形式存储，指向字符串表中的位置
 *   本函数将偏移量转换为实际的字符串指针，指向新分配的字符串池
 */
char* DBCFileLoader::AutoProduceStrings(char const* format, char* dataTable)
{
    // 验证格式字符串长度与字段数是否匹配
    if (strlen(format) != fieldCount)
        return nullptr;

    // 复制字符串表到新的内存池
    char* stringPool = new char[stringSize];
    memcpy(stringPool, stringTable, stringSize);

    uint32 offset = 0;

    // 遍历所有记录，填充字符串字段指针
    for (uint32 y = 0; y < recordCount; ++y)
    {
        for (uint32 x = 0; x < fieldCount; ++x)
        {
            switch (format[x])
            {
                case FT_FLOAT:
                    offset += sizeof(float);
                    break;
                case FT_IND:
                case FT_INT:
                    offset += sizeof(uint32);
                    break;
                case FT_BYTE:
                    offset += sizeof(uint8);
                    break;
                case FT_STRING:
                {
                    // 仅填充尚未填充的条目
                    char** slot = (char**)(&dataTable[offset]);
                    if (!*slot || !**slot)
                    {
                        // 获取记录中的字符串（返回的是相对于 stringTable 的指针）
                        const char * st = getRecord(y).getString(x);
                        // 计算在新字符串池中的对应位置
                        *slot = stringPool + (st - (char const*)stringTable);
                    }
                    offset += sizeof(char*);
                    break;
                 }
                 case FT_LOGIC:
                     ABORT_MSG("Attempted to load DBC files that does not have field types that match what is in the core. Check DBCfmt.h or your DBC files.");
                     break;
                 case FT_NA:
                 case FT_NA_BYTE:
                 case FT_SORT:
                     // 这些字段类型不占用空间
                     break;
                 default:
                     ABORT_MSG("Unknown field format character in DBCfmt.h");
                     break;
            }
        }
    }

    return stringPool;
}
