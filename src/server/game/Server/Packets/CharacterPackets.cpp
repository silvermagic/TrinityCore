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
 * @file CharacterPackets.cpp
 * @brief 角色相关网络数据包实现
 *
 * 本文件实现了CharacterPackets.h中定义的所有数据包类的序列化和反序列化方法:
 * - 客户端数据包的Read()方法:从网络字节流读取数据
 * - 服务器数据包的Write()方法:将数据写入网络字节流
 *
 * 所有方法都使用WorldPacket的流操作符进行数据序列化,确保网络字节序转换正确。
 */

#include "CharacterPackets.h"

/**
 * @brief 读取披风显示状态数据包
 *
 * 从网络数据包中读取布尔值,表示玩家是否要显示披风
 * 数据格式:1字节布尔值
 */
void WorldPackets::Character::ShowingCloak::Read()
{
    // 从数据包读取布尔值,自动处理字节序转换
    _worldPacket >> ShowCloak;
}

/**
 * @brief 读取头盔显示状态数据包
 *
 * 从网络数据包中读取布尔值,表示玩家是否要显示头盔
 * 数据格式:1字节布尔值
 */
void WorldPackets::Character::ShowingHelm::Read()
{
    // 从数据包读取布尔值,自动处理字节序转换
    _worldPacket >> ShowHelm;
}

/**
 * @brief 写入登录世界验证数据包
 * @return 返回序列化后的数据包指针
 *
 * 将地图ID和角色位置信息写入数据包
 * 数据格式:4字节地图ID + 16字节位置信息(XYZ + 朝向O)
 */
WorldPacket const* WorldPackets::Character::LoginVerifyWorld::Write()
{
    // 写入地图ID,强制转换为int32确保字节序正确
    _worldPacket << int32(MapID);

    // 写入位置信息(包含X、Y、Z坐标和朝向O),Position类重载了<<操作符
    _worldPacket << Pos;

    return &_worldPacket;
}

/**
 * @brief 写入登出响应数据包
 * @return 返回序列化后的数据包指针
 *
 * 将登出结果码和立即登出标志写入数据包
 * 数据格式:4字节结果码 + 1字节立即标志
 */
WorldPacket const* WorldPackets::Character::LogoutResponse::Write()
{
    // 写入登出结果码,0表示成功,非0表示错误码
    _worldPacket << uint32(LogoutResult);

    // 写入是否立即登出标志,uint8类型节省空间
    _worldPacket << uint8(Instant);

    return &_worldPacket;
}

/**
 * @brief 读取游戏时间查询请求数据包
 *
 * 从网络数据包中读取是否触发脚本事件的标志
 * 数据格式:1字节布尔值
 */
void WorldPackets::Character::PlayedTimeClient::Read()
{
    // 从数据包读取布尔值,决定是否触发OnPlayTime脚本事件
    _worldPacket >> TriggerScriptEvent;
}

/**
 * @brief 写入游戏时间统计数据包
 * @return 返回序列化后的数据包指针
 *
 * 将总游戏时间、等级游戏时间和脚本事件标志写入数据包
 * 数据格式:4字节总时间 + 4字节等级时间 + 1字节脚本标志 = 9字节
 */
WorldPacket const* WorldPackets::Character::PlayedTime::Write()
{
    // 写入角色总游戏时间(秒),从创建角色开始累计
    _worldPacket << uint32(TotalTime);

    // 写入当前等级的游戏时间(秒),从上次升级开始累计
    _worldPacket << uint32(LevelTime);

    // 写入是否触发脚本事件标志,与客户端请求对应
    _worldPacket << uint8(TriggerScriptEvent);

    return &_worldPacket;
}
