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
 * @file ChatPackets.cpp
 * @brief 聊天系统网络包实现模块
 *
 * 本模块实现了 ChatPackets.h 中定义的所有聊天相关网络包的序列化和反序列化方法。
 * 主要功能包括：
 * - 表情动作包的序列化（服务器发送）和反序列化（客户端接收）
 * - 聊天服务器消息包的序列化
 *
 * 序列化流程：
 * - Write() 方法：将成员变量数据写入网络包缓冲区，用于服务器向客户端发送
 * - Read() 方法：从网络包缓冲区读取数据到成员变量，用于客户端向服务器发送
 *
 * 性能优化：
 * - 使用预分配的缓冲区大小减少动态内存分配
 * - 流式写入操作符 << 提供高效的数据序列化
 *
 * @see ChatPackets.h
 */

#include "ChatPackets.h"

/**
 * @brief 序列化表情动作包
 *
 * 将表情动作ID和执行者GUID序列化到网络包缓冲区。
 *
 * @return 返回指向完成序列化的 WorldPacket 的指针
 *
 * 数据格式：
 * - uint32 EmoteID: 表情动作ID（参考 Emotes.dbc）
 * - ObjectGuid Guid: 执行表情的单位GUID
 *
 * 序列化顺序必须与客户端反序列化顺序一致。
 * ObjectGuid 使用特殊的压缩格式，占用 8 字节（完整GUID）或更少（压缩后）。
 *
 * 调用时机：
 * - 服务器需要向客户端通知表情动作时
 * - 在 WorldSession::HandleEmoteOpcode 处理后广播给周围玩家
 * - 在 Unit::HandleEmoteCommand 中由脚本触发时
 *
 * 性能说明：
 * - 使用流式操作符 << 避免额外的内存拷贝
 * - 预分配的缓冲区大小正好容纳数据，无需动态扩容
 */
WorldPacket const* WorldPackets::Chat::Emote::Write()
{
    // 写入表情动作ID（4字节）
    _worldPacket << EmoteID;
    // 写入执行表情的单位GUID（使用ObjectGuid的重载<<操作符）
    _worldPacket << Guid;

    return &_worldPacket;
}

/**
 * @brief 反序列化客户端表情动作请求包
 *
 * 从接收到的网络包缓冲区中提取表情动作ID。
 *
 * 数据格式：
 * - uint32 EmoteID: 客户端请求的表情动作ID
 *
 * 反序列化顺序必须与客户端序列化顺序一致。
 * 仅读取一个 uint32，数据量小，性能开销极低。
 *
 * 调用时机：
 * - 服务器收到 CMSG_EMOTE 包时
 * - 在 WorldSession::HandleEmoteOpcode 中调用
 * - 读取完成后，数据通过 EmoteID 成员变量访问
 *
 * 错误处理：
 * - 如果包数据不足，流操作会自动处理边界情况
 * - 建议在调用方验证 EmoteID 的有效性
 *
 * 后续处理：
 * 读取完成后，HandleEmoteOpcode 会：
 * 1. 验证玩家是否可以执行表情（未死亡、未沉默等）
 * 2. 设置玩家的表情状态
 * 3. 创建 Emote 服务器包广播给周围玩家
 */
void WorldPackets::Chat::EmoteClient::Read()
{
    // 从网络包中读取表情动作ID（4字节）
    _worldPacket >> EmoteID;
}

/**
 * @brief 序列化聊天服务器消息包
 *
 * 将消息ID和字符串参数序列化到网络包缓冲区。
 *
 * @return 返回指向完成序列化的 WorldPacket 的指针
 *
 * 数据格式：
 * - int32 MessageID: 消息模板ID（客户端根据此ID查找本地化文本）
 * - string StringParam: 消息参数（用于替换模板中的占位符）
 *
 * 消息ID含义：
 * - 正数ID：通常对应数据库中的预定义消息
 * - 负数ID：可能用于特殊系统消息
 * - 客户端根据ID查找对应的本地化文本模板
 *
 * 字符串参数：
 * - 用于动态填充消息模板中的占位符
 * - 例如："玩家 {param} 获得了成就" 中的 {param} 被 StringParam 替换
 * - 字符串使用 UTF-8 编码，支持多语言
 *
 * 序列化说明：
 * - int32 强制转换为有符号整数，确保客户端正确解析
 * - string 序列化包含长度前缀（变长编码）
 *
 * 调用时机：
 * - 服务器需要发送系统公告时
 * - GM 执行公告命令时
 * - 系统事件触发通知时
 *
 * 性能注意事项：
 * - 字符串长度可变，缓冲区会根据实际长度动态调整
 * - 长字符串会占用更多网络带宽，建议控制消息长度
 * - 避免频繁向大量玩家发送包含长字符串的消息
 *
 * 使用示例：
 * @code
 * ChatServerMessage message;
 * message.MessageID = 123;  // 假设123对应"服务器将在 {param} 分钟后重启"
 * message.StringParam = "30";  // 替换占位符
 * player->SendPacket(message.Write());
 * @endcode
 */
WorldPacket const* WorldPackets::Chat::ChatServerMessage::Write()
{
    // 写入消息ID（强制转换为 int32 以匹配客户端期望的有符号整数）
    _worldPacket << int32(MessageID);
    // 写入消息参数字符串（包含长度前缀的UTF-8字符串）
    _worldPacket << StringParam;

    return &_worldPacket;
}
