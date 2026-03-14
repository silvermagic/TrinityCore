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
 * @file SpellPackets.cpp
 * @brief 法术系统网络数据包实现
 *
 * 本文件实现了法术系统相关的所有网络数据包的序列化和反序列化功能。
 * 主要功能包括：
 * - 客户端请求包的读取（取消施法、移除光环等）
 * - 服务器响应包的写入（施法开始、施法完成、符文同步等）
 * - 中间数据结构的序列化（目标位置、符文数据、弹道轨迹等）
 *
 * 这些数据包实现了客户端与服务器之间法术系统的完整通信协议。
 */

#include "SpellPackets.h"
#include "SharedDefines.h"
#include "Spell.h"
#include "SpellInfo.h"

/**
 * @brief 读取取消施法数据包
 *
 * 从客户端发送的网络包中读取施法ID和法术ID。
 * 调用时机：客户端主动取消施法时（如移动、按ESC键）。
 */
void WorldPackets::Spells::CancelCast::Read()
{
    _worldPacket >> CastID;     // 读取客户端施法标识符
    _worldPacket >> SpellID;    // 读取要取消的法术ID
}

/**
 * @brief 读取取消光环数据包
 *
 * 从客户端发送的网络包中读取要移除的法术ID。
 * 调用时机：玩家右键点击光环图标移除时。
 */
void WorldPackets::Spells::CancelAura::Read()
{
    _worldPacket >> SpellID;    // 读取要取消的光环法术ID
}

/**
 * @brief 读取宠物取消光环数据包
 *
 * 从客户端发送的网络包中读取宠物GUID和法术ID。
 * 调用时机：玩家取消宠物身上的光环时。
 */
void WorldPackets::Spells::PetCancelAura::Read()
{
    _worldPacket >> PetGUID;    // 读取宠物的全局唯一标识符
    _worldPacket >> SpellID;    // 读取要取消的光环法术ID
}

/**
 * @brief 读取取消引导法术数据包
 *
 * 从客户端发送的网络包中读取引导法术ID。
 * 调用时机：玩家取消正在引导的法术（如暴风雪、奥术飞弹）。
 */
void WorldPackets::Spells::CancelChannelling::Read()
{
    _worldPacket >> ChannelSpell;    // 读取要取消的引导法术ID
}

/**
 * @brief 序列化法术未命中状态
 * @param data 字节缓冲区
 * @param spellMissStatus 未命中状态数据
 * @return 字节缓冲区引用
 *
 * 将未命中目标的状态信息写入网络包。
 * 如果是反射情况，需要额外写入反射状态。
 */
ByteBuffer& operator<<(ByteBuffer& data, WorldPackets::Spells::SpellMissStatus const& spellMissStatus)
{
    data << uint64(spellMissStatus.TargetGUID);     // 写入目标GUID
    data << uint8(spellMissStatus.Reason);          // 写入未命中原因

    // 如果是反射，需要额外写入反射状态
    if (spellMissStatus.Reason == SPELL_MISS_REFLECT)
        data << uint8(spellMissStatus.ReflectStatus);

    return data;
}

/**
 * @brief 序列化目标位置
 * @param data 字节缓冲区
 * @param targetLocation 目标位置数据
 * @return 字节缓冲区引用
 *
 * 将目标位置信息写入网络包。
 * 包括载具GUID（相对坐标）和实际坐标位置。
 */
ByteBuffer& operator<<(ByteBuffer& data, WorldPackets::Spells::TargetLocation const& targetLocation)
{
    // 写入载具GUID（压缩格式），用于相对坐标定位（如在船上、电梯上等）
    data << targetLocation.Transport.WriteAsPacked();
    // 写入目标位置的XYZ坐标流
    data << targetLocation.Location.PositionXYZStream();
    return data;
}

/**
 * @brief 序列化法术目标数据
 * @param data 字节缓冲区
 * @param spellTargetData 法术目标数据
 * @return 字节缓冲区引用
 *
 * 将完整的法术目标信息写入网络包。
 * 根据目标标志位的不同，写入不同类型的目标数据：
 * - 单位目标（玩家、NPC等）
 * - 物品目标
 * - 源位置（某些特殊法术需要）
 * - 目标位置（区域法术）
 * - 目标名称（特殊法术）
 */
ByteBuffer& operator<<(ByteBuffer& data, WorldPackets::Spells::SpellTargetData const& spellTargetData)
{
    // 写入目标标志位，决定后续包含哪些目标数据
    data << uint32(spellTargetData.Flags);

    // 如果存在单位目标，写入压缩格式的GUID
    if (spellTargetData.Unit)
        data << spellTargetData.Unit->WriteAsPacked();

    // 如果存在物品目标，写入压缩格式的GUID
    if (spellTargetData.Item)
        data << spellTargetData.Item->WriteAsPacked();

    // 如果存在源位置，写入源位置数据
    if (spellTargetData.SrcLocation)
        data << *spellTargetData.SrcLocation;

    // 如果存在目标位置，写入目标位置数据
    if (spellTargetData.DstLocation)
        data << *spellTargetData.DstLocation;

    // 如果存在目标名称，写入名称字符串
    if (spellTargetData.Name)
        data << *spellTargetData.Name;

    return data;
}

/**
 * @brief 序列化符文数据
 * @param data 字节缓冲区
 * @param runeData 符文数据
 * @return 字节缓冲区引用
 *
 * 将死亡骑士的符文冷却数据写入网络包。
 * 包括起始索引、符文数量和各符文的冷却时间。
 */
ByteBuffer& operator<<(ByteBuffer& data, WorldPackets::Spells::RuneData const& runeData)
{
    data << uint8(runeData.Start);     // 写入起始符文索引
    data << uint8(runeData.Count);     // 写入符文数量

    // 写入每个符文的冷却时间
    for (uint8 cooldown : runeData.Cooldowns)
        data << uint8(cooldown);

    return data;
}

/**
 * @brief 序列化导弹轨迹结果
 * @param data 字节缓冲区
 * @param traj 导弹轨迹数据
 * @return 字节缓冲区引用
 *
 * 将法术导弹的弹道轨迹信息写入网络包。
 * 包括发射仰角和飞行时间，用于客户端显示正确的投射物轨迹。
 */
ByteBuffer& operator<<(ByteBuffer& data, WorldPackets::Spells::MissileTrajectoryResult const& traj)
{
    data << float(traj.Pitch);         // 写入发射仰角（弧度）
    data << uint32(traj.TravelTime);   // 写入飞行时间（毫秒）
    return data;
}

/**
 * @brief 序列化法术弹药数据
 * @param data 字节缓冲区
 * @param spellAmmo 弹药数据
 * @return 字节缓冲区引用
 *
 * 将射击类法术使用的弹药显示信息写入网络包。
 * 用于客户端正确显示弓箭、枪械等武器的弹药效果。
 */
ByteBuffer& operator<<(ByteBuffer& data, WorldPackets::Spells::SpellAmmo const& spellAmmo)
{
    data << uint32(spellAmmo.DisplayID);          // 写入弹药显示模型ID
    data << uint32(spellAmmo.InventoryType);      // 写入背包槽位类型
    return data;
}

/**
 * @brief 序列化生物免疫信息
 * @param data 字节缓冲区
 * @param immunities 免疫信息
 * @return 字节缓冲区引用
 *
 * 将生物对法术类型的免疫信息写入网络包。
 * 用于客户端显示免疫状态和进行相应的视觉反馈。
 */
ByteBuffer& operator<<(ByteBuffer& data, WorldPackets::Spells::CreatureImmunities const& immunities)
{
    data << uint32(immunities.School);     // 写入法术类型掩码
    data << uint32(immunities.Value);      // 写入免疫值
    return data;
}

/**
 * @brief 序列化法术施放数据
 * @param data 字节缓冲区
 * @param spellCastData 法术施放数据
 * @return 字节缓冲区引用
 *
 * 将完整的法术施放信息写入网络包。这是法术系统最核心的序列化函数。
 * 包含施法者信息、法术ID、施法时间、目标列表、能量消耗等所有施法相关数据。
 *
 * 性能注意事项：
 * - 命中和未命中目标数量限制为255个，超过会导致客户端崩溃
 * - 大范围法术（如ID 40647）可能超出限制，需要裁剪目标列表
 * - 使用压缩格式传输GUID以减少网络流量
 */
ByteBuffer& operator<<(ByteBuffer& data, WorldPackets::Spells::SpellCastData const& spellCastData)
{
    // 写入施法者和施法单位GUID（压缩格式）
    data << spellCastData.CasterGUID.WriteAsPacked();
    data << spellCastData.CasterUnit.WriteAsPacked();

    // 写入施法基本信息
    data << uint8(spellCastData.CastID);                // 客户端施法标识符
    data << uint32(spellCastData.SpellID);              // 法术ID
    data << uint32(spellCastData.CastFlags);            // 施法标志位
    data << uint32(spellCastData.CastTime);             // 施法时间戳

    // 如果存在命中目标和未命中状态列表，则写入目标信息
    if (spellCastData.HitTargets && spellCastData.MissStatus)
    {
        // 命中和未命中目标计数都是uint8类型，限制每种类型最多255个目标
        // 发送超过255个目标会导致客户端崩溃（计数器会溢出）
        // 某些大范围法术（如ID 40647）可能轻易达到此限制
        // 需要限制发送的目标数量，同时保持命中和未命中的正确计数
        static std::size_t const PACKET_TARGET_LIMIT = std::numeric_limits<uint8>::max();

        // 裁剪命中目标列表到上限
        if (spellCastData.HitTargets->size() > PACKET_TARGET_LIMIT)
            spellCastData.HitTargets->resize(PACKET_TARGET_LIMIT);

        // 写入命中目标数量和列表
        data << uint8(spellCastData.HitTargets->size());
        for (ObjectGuid const& target : *spellCastData.HitTargets)
            data << uint64(target);

        // 裁剪未命中目标列表到上限
        if (spellCastData.MissStatus->size() > PACKET_TARGET_LIMIT)
            spellCastData.MissStatus->resize(PACKET_TARGET_LIMIT);

        // 写入未命中目标数量和状态列表
        data << uint8(spellCastData.MissStatus->size());
        for (WorldPackets::Spells::SpellMissStatus const& status : *spellCastData.MissStatus)
            data << status;
    }

    // 写入目标数据
    data << spellCastData.Target;

    // 可选数据：剩余能量值
    if (spellCastData.RemainingPower)
        data << uint32(*spellCastData.RemainingPower);

    // 可选数据：剩余符文数据（死亡骑士专用）
    if (spellCastData.RemainingRunes)
        data << *spellCastData.RemainingRunes;

    // 可选数据：导弹轨迹结果（投射物法术）
    if (spellCastData.MissileTrajectory)
        data << *spellCastData.MissileTrajectory;

    // 可选数据：弹药显示信息（射击类法术）
    if (spellCastData.Ammo)
        data << *spellCastData.Ammo;

    // 可选数据：生物免疫信息
    if (spellCastData.Immunities)
        data << *spellCastData.Immunities;

    // 如果施法标志包含视觉链效果，写入额外数据
    if (spellCastData.CastFlags & CAST_FLAG_VISUAL_CHAIN)
    {
        data << uint32(0);  // 未知数据1
        data << uint32(0);  // 未知数据2
    }

    // 如果目标标志包含目标位置，写入额外字节
    if (spellCastData.Target.Flags & TARGET_FLAG_DEST_LOCATION)
        data << uint8(0);

    return data;
}

/**
 * @brief 写入法术施放完成数据包
 * @return 写入完成后的世界包指针
 *
 * 将法术施放完成数据写入网络包，发送给客户端。
 * 调用时机：服务器完成法术施放处理后，通知客户端施法已完成。
 * 包括所有命中和未命中目标的详细信息。
 */
WorldPacket const* WorldPackets::Spells::SpellGo::Write()
{
    _worldPacket << Cast;    // 序列化完整的施法数据
    return &_worldPacket;
}

/**
 * @brief 写入法术施法开始数据包
 * @return 写入完成后的世界包指针
 *
 * 将法术施法开始数据写入网络包，发送给客户端。
 * 调用时机：服务器开始处理有施法时间的法术时。
 * 让客户端显示施法条和施法动画。
 */
WorldPacket const* WorldPackets::Spells::SpellStart::Write()
{
    _worldPacket << Cast;    // 序列化完整的施法数据
    return &_worldPacket;
}

/**
 * @brief 写入符文重同步数据包
 * @return 写入完成后的世界包指针
 *
 * 将死亡骑士的符文状态同步数据写入网络包，发送给客户端。
 * 调用时机：需要完全更新符文状态时（如登录、重置等）。
 * 包括所有符文的类型和冷却时间信息。
 */
WorldPacket const* WorldPackets::Spells::ResyncRunes::Write()
{
    _worldPacket << Count;    // 写入符文数量

    // 写入每个符文的类型和冷却时间
    for (WorldPackets::Spells::ResyncRune const& rune : Runes)
    {
        _worldPacket << rune.RuneType;     // 符文类型
        _worldPacket << rune.Cooldown;     // 冷却时间
    }

    return &_worldPacket;
}

/**
 * @brief 写入坐骑结果数据包
 * @return 写入完成后的世界包指针
 *
 * 将坐骑操作的结果写入网络包，发送给客户端。
 * 调用时机：玩家尝试召唤坐骑后，返回成功或失败状态。
 * 结果码指示操作是否成功或失败原因。
 */
WorldPacket const* WorldPackets::Spells::MountResult::Write()
{
    _worldPacket << int32(Result);    // 写入坐骑结果码

    return &_worldPacket;
}
