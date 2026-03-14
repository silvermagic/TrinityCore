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
 * @file NPCPackets.cpp
 * @brief NPC 交互数据包实现模块
 *
 * 本文件实现了与 NPC 交互相关的网络数据包的序列化和反序列化功能,包括:
 * - 读取玩家与 NPC 的交互请求
 * - 序列化训练师技能列表数据
 * - 处理技能购买请求
 * - 序列化购买结果反馈(成功/失败)
 *
 * 这些数据包处理函数是网络通信的关键部分,负责客户端和服务器之间的数据转换。
 */

#include "NPCPackets.h"

/**
 * @brief 读取 Hello 数据包
 *
 * 从客户端接收的数据包中读取 NPC 的 GUID。
 * 此函数在玩家与各种类型的 NPC 交互时被调用,
 * 包括银行家、战场军官、绑定者、商人和训练师等。
 *
 * 调用时机:
 * - 当玩家右键点击支持交互的 NPC 时
 * - 客户端发送相应的 CMSG_* 消息后,服务器解析数据包时调用
 *
 * 性能注意事项:
 * - 此函数操作非常轻量,仅读取一个 GUID(8 字节)
 * - 不应在此函数中执行任何验证逻辑,仅负责数据解析
 */
void WorldPackets::NPC::Hello::Read()
{
    // 从数据包中读取目标 NPC 的全局唯一标识符
    _worldPacket >> Unit;
}

/**
 * @brief 写入训练师技能列表数据包
 * @return 返回写入完成的数据包常量指针
 *
 * 将训练师的技能列表信息序列化到数据包中,发送给客户端。
 * 数据包格式: 训练师GUID + 训练师类型 + 技能数量 + [技能详细信息] + 问候语
 *
 * 调用时机:
 * - 玩家与训练师 NPC 交互时
 * - 服务器处理完 CMSG_TRAINER_LIST 请求后,向客户端发送技能列表
 *
 * 性能注意事项:
 * - 数据包大小取决于技能数量,应避免技能列表过大
 * - 使用 std::vector 存储技能列表,迭代写入效率较高
 * - 每个技能占用约 40+ 字节(取决于固定数组大小)
 */
WorldPacket const* WorldPackets::NPC::TrainerList::Write()
{
    // 写入训练师的全局唯一标识符
    _worldPacket << TrainerGUID;

    // 写入训练师类型(职业训练师、专业技能训练师等)
    _worldPacket << int32(TrainerType);

    // 写入技能列表的大小(技能数量)
    _worldPacket << int32(Spells.size());

    // 遍历并写入每个技能的详细信息
    for (TrainerListSpell const& spell : Spells)
    {
        // 写入技能ID
        _worldPacket << int32(spell.SpellID);

        // 写入技能可用性状态(0=不可用, 1=可学习, 2=已学会)
        _worldPacket << uint8(spell.Usable);

        // 写入学习该技能需要的金币费用(铜币单位)
        _worldPacket << int32(spell.MoneyCost);

        // 写入天赋点消耗数组(固定大小 2 个元素)
        _worldPacket.append(spell.PointCost.data(), spell.PointCost.size());

        // 写入等级要求
        _worldPacket << uint8(spell.ReqLevel);

        // 写入所需专业技能ID
        _worldPacket << int32(spell.ReqSkillLine);

        // 写入所需专业技能等级
        _worldPacket << int32(spell.ReqSkillRank);

        // 写入前置技能ID数组(固定大小 3 个元素)
        _worldPacket.append(spell.ReqAbility.data(), spell.ReqAbility.size());
    }

    // 写入训练师的问候语文本
    _worldPacket << Greeting;

    return &_worldPacket;
}

/**
 * @brief 读取训练师技能购买请求数据包
 *
 * 从客户端接收的数据包中读取训练师 GUID 和要购买的技能ID。
 * 此函数在玩家点击训练师界面中的"学习"按钮时被调用。
 *
 * 调用时机:
 * - 玩家在训练师界面选择某个技能并确认购买时
 * - 客户端发送 CMSG_TRAINER_BUY_SPELL 消息后,服务器解析数据包时调用
 *
 * 性能注意事项:
 * - 此函数操作非常轻量,仅读取 GUID(8 字节) 和技能ID(4 字节)
 * - 不应在此函数中执行验证逻辑(如金币检查、等级检查等)
 *   这些验证应在业务逻辑层处理
 */
void WorldPackets::NPC::TrainerBuySpell::Read()
{
    // 从数据包中读取训练师的 GUID
    _worldPacket >> TrainerGUID;

    // 从数据包中读取要购买的技能ID
    _worldPacket >> SpellID;
}

/**
 * @brief 写入训练师技能购买失败数据包
 * @return 返回写入完成的数据包常量指针
 *
 * 当玩家尝试从训练师购买技能失败时,将失败信息序列化到数据包中,
 * 发送给客户端以显示失败原因。
 *
 * 数据包格式: 训练师GUID + 技能ID + 失败原因代码
 *
 * 调用时机:
 * - 玩家尝试购买技能但未满足条件时(金币不足、等级不够等)
 * - 服务器验证购买请求失败后,向客户端发送失败通知
 *
 * 性能注意事项:
 * - 固定大小的数据包(16 字节),性能开销极小
 * - 失败原因代码应在业务逻辑层设置
 */
WorldPacket const* WorldPackets::NPC::TrainerBuyFailed::Write()
{
    // 写入训练师的全局唯一标识符
    _worldPacket << TrainerGUID;

    // 写入购买失败的技能ID
    _worldPacket << int32(SpellID);

    // 写入失败原因代码(如金币不足、等级不够、缺少前置技能等)
    _worldPacket << int32(TrainerFailedReason);

    return &_worldPacket;
}

/**
 * @brief 写入训练师技能购买成功数据包
 * @return 返回写入完成的数据包常量指针
 *
 * 当玩家成功从训练师购买技能后,将成功信息序列化到数据包中,
 * 发送给客户端以确认购买成功并更新界面。
 *
 * 数据包格式: 训练师GUID + 技能ID
 *
 * 调用时机:
 * - 玩家购买技能成功后(金币扣除、技能学会等操作完成)
 * - 服务器验证购买请求通过并完成相应处理后,向客户端发送成功确认
 *
 * 性能注意事项:
 * - 固定大小的数据包(12 字节),性能开销极小
 * - 此数据包仅用于确认购买成功,具体的技能学习已在服务器端完成
 */
WorldPacket const* WorldPackets::NPC::TrainerBuySucceeded::Write()
{
    // 写入训练师的全局唯一标识符
    _worldPacket << TrainerGUID;

    // 写入成功购买的技能ID
    _worldPacket << int32(SpellID);

    return &_worldPacket;
}
