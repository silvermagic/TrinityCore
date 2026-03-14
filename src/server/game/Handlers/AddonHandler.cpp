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
 * @file AddonHandler.cpp
 * @brief 插件处理器实现文件
 *
 * 实现了插件数据包的解压缩和响应包构建功能。
 * 主要处理客户端发送的插件列表,验证每个插件的有效性。
 */

#include "zlib.h"
#include "AddonHandler.h"
#include "Opcodes.h"
#include "Log.h"

/**
 * @brief 获取AddonHandler单例实例
 * @return AddonHandler实例指针
 *
 * 使用Meyer's Singleton模式,通过静态局部变量实现线程安全的单例
 */
AddonHandler* AddonHandler::instance()
{
    static AddonHandler instance;
    return &instance;
}

/**
 * @brief 构建插件信息响应包
 * @param source 源数据包(客户端发送的压缩插件数据)
 * @param target 目标数据包(服务器构建的响应包)
 * @return true表示构建成功,false表示构建失败
 *
 * 该函数处理客户端发送的插件信息,解压数据并验证每个插件,
 * 然后构建响应包返回给客户端。
 */
bool AddonHandler::BuildAddonPacket(WorldPacket* source, WorldPacket* target)
{
    ByteBuffer AddOnPacked;      // 解压后的插件数据缓冲区
    uLongf AddonRealSize;        // 解压后的实际数据大小
    uint32 CurrentPosition;      // 当前读取位置
    uint32 TempValue;            // 临时变量

    // 检查数据包是否完整,损坏的插件包不会来自真实客户端
    if (source->rpos() + 4 > source->size())
        return false;

    // 读取压缩前原始数据的实际大小
    *source >> TempValue;

    // 空插件包,无需处理,不会来自真实客户端
    if (!TempValue)
        return false;

    // ZLIB要求使用uLongf类型,需要转换
    AddonRealSize = TempValue;

    // 记录当前读取位置(压缩数据起始位置)
    CurrentPosition = source->rpos();

    // 调整缓冲区大小以容纳解压后的数据
    AddOnPacked.resize(AddonRealSize);

    // 使用zlib解压缩插件数据
    if (uncompress(AddOnPacked.contents(), &AddonRealSize, source->contents() + CurrentPosition, source->size() - CurrentPosition) == Z_OK)
    {
        // 初始化响应包,使用SMSG_ADDON_INFO操作码
        target->Initialize(SMSG_ADDON_INFO);

        uint32 addonsCount;
        AddOnPacked >> addonsCount;  // 读取插件数量

        // 遍历处理每个插件
        for (uint32 i = 0; i < addonsCount; ++i)
        {
            std::string addonName;  // 插件名称
            uint8 enabled;          // 是否启用标志
            uint32 crc, unk2;       // CRC校验码和未知字段

            // 检查插件数据格式是否正确
            if (AddOnPacked.rpos()+1 > AddOnPacked.size())
                return false;

            AddOnPacked >> addonName;

            // 再次检查剩余数据格式是否正确
            if (AddOnPacked.rpos()+1+4+4 > AddOnPacked.size())
                return false;

            AddOnPacked >> enabled >> crc >> unk2;

            // 记录插件信息调试日志
            TC_LOG_DEBUG("network", "ADDON: Name: {}, Enabled: 0x{:x}, CRC: 0x{:x}, Unknown2: 0x{:x}", addonName, enabled, crc, unk2);

            // 构建插件状态响应:
            // state = 2 表示插件已启用
            // state = 1 表示插件已禁用
            uint8 state = (enabled ? 2 : 1);
            *target << uint8(state);

            // unk1字段: 1表示已启用,0表示未启用
            uint8 unk1 = (enabled ? 1 : 0);
            *target << uint8(unk1);

            if (unk1)
            {
                // 检查是否为标准插件CRC(0x4c1c776d)
                // 标准插件不需要额外的验证数据
                uint8 unk = (crc != 0x4c1c776d);
                *target << uint8(unk);

                if (unk)
                {
                    // 非标准插件需要发送256字节的验证数据表
                    // 这是Blizzard定义的固定密钥表,用于验证非标准插件
                    unsigned char tdata[256] =
                    {
                        0xC3, 0x5B, 0x50, 0x84, 0xB9, 0x3E, 0x32, 0x42, 0x8C, 0xD0, 0xC7, 0x48, 0xFA, 0x0E, 0x5D, 0x54,
                        0x5A, 0xA3, 0x0E, 0x14, 0xBA, 0x9E, 0x0D, 0xB9, 0x5D, 0x8B, 0xEE, 0xB6, 0x84, 0x93, 0x45, 0x75,
                        0xFF, 0x31, 0xFE, 0x2F, 0x64, 0x3F, 0x3D, 0x6D, 0x07, 0xD9, 0x44, 0x9B, 0x40, 0x85, 0x59, 0x34,
                        0x4E, 0x10, 0xE1, 0xE7, 0x43, 0x69, 0xEF, 0x7C, 0x16, 0xFC, 0xB4, 0xED, 0x1B, 0x95, 0x28, 0xA8,
                        0x23, 0x76, 0x51, 0x31, 0x57, 0x30, 0x2B, 0x79, 0x08, 0x50, 0x10, 0x1C, 0x4A, 0x1A, 0x2C, 0xC8,
                        0x8B, 0x8F, 0x05, 0x2D, 0x22, 0x3D, 0xDB, 0x5A, 0x24, 0x7A, 0x0F, 0x13, 0x50, 0x37, 0x8F, 0x5A,
                        0xCC, 0x9E, 0x04, 0x44, 0x0E, 0x87, 0x01, 0xD4, 0xA3, 0x15, 0x94, 0x16, 0x34, 0xC6, 0xC2, 0xC3,
                        0xFB, 0x49, 0xFE, 0xE1, 0xF9, 0xDA, 0x8C, 0x50, 0x3C, 0xBE, 0x2C, 0xBB, 0x57, 0xED, 0x46, 0xB9,
                        0xAD, 0x8B, 0xC6, 0xDF, 0x0E, 0xD6, 0x0F, 0xBE, 0x80, 0xB3, 0x8B, 0x1E, 0x77, 0xCF, 0xAD, 0x22,
                        0xCF, 0xB7, 0x4B, 0xCF, 0xFB, 0xF0, 0x6B, 0x11, 0x45, 0x2D, 0x7A, 0x81, 0x18, 0xF2, 0x92, 0x7E,
                        0x98, 0x56, 0x5D, 0x5E, 0x69, 0x72, 0x0A, 0x0D, 0x03, 0x0A, 0x85, 0xA2, 0x85, 0x9C, 0xCB, 0xFB,
                        0x56, 0x6E, 0x8F, 0x44, 0xBB, 0x8F, 0x02, 0x22, 0x68, 0x63, 0x97, 0xBC, 0x85, 0xBA, 0xA8, 0xF7,
                        0xB5, 0x40, 0x68, 0x3C, 0x77, 0x86, 0x6F, 0x4B, 0xD7, 0x88, 0xCA, 0x8A, 0xD7, 0xCE, 0x36, 0xF0,
                        0x45, 0x6E, 0xD5, 0x64, 0x79, 0x0F, 0x17, 0xFC, 0x64, 0xDD, 0x10, 0x6F, 0xF3, 0xF5, 0xE0, 0xA6,
                        0xC3, 0xFB, 0x1B, 0x8C, 0x29, 0xEF, 0x8E, 0xE5, 0x34, 0xCB, 0xD1, 0x2A, 0xCE, 0x79, 0xC3, 0x9A,
                        0x0D, 0x36, 0xEA, 0x01, 0xE0, 0xAA, 0x91, 0x20, 0x54, 0xF0, 0x72, 0xD8, 0x1E, 0xC7, 0x89, 0xD2
                    };
                    target->append(tdata, sizeof(tdata));
                }

                // 追加4字节的未知数据(通常为0)
                *target << uint32(0);
            }

            // unk3字段: 已禁用的插件设置为1,启用的插件设置为0
            uint8 unk3 = (enabled ? 0 : 1);
            *target << uint8(unk3);

            if (unk3)
            {
                // 对于禁用的插件,追加额外的字节
                // 可能是空字符串(以null结尾)
                *target << uint8(0);
            }
        }

        // 读取未知字段unk4(协议保留字段)
        uint32 unk4;
        AddOnPacked >> unk4;

        // 写入额外插件计数(当前为0,表示没有额外插件)
        uint32 count = 0;
        *target << uint32(count);

        // 检查是否所有数据都已读取完毕
        // 如果还有未读数据,记录调试日志
        if (AddOnPacked.rpos() != AddOnPacked.size())
            TC_LOG_DEBUG("network", "packet under read!");
    }
    else
    {
        // zlib解压缩失败
        TC_LOG_ERROR("network", "Addon packet uncompress error :(");
        return false;
    }

    return true;
}
