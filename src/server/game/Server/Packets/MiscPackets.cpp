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
 * @file MiscPackets.cpp
 * @brief 杂项网络数据包实现文件
 *
 * 本文件实现了 MiscPackets.h 中定义的各种杂项网络数据包的序列化（Write）和反序列化（Read）方法。
 * 这些数据包涵盖炉石绑定、镜像计时器、天气系统、音效播放、死亡复活等多种游戏功能。
 *
 * 序列化流程（服务器 -> 客户端）：
 * 1. 服务器创建数据包对象并设置成员变量
 * 2. 调用 Write() 方法将数据序列化到 _worldPacket
 * 3. 通过网络发送给客户端
 *
 * 反序列化流程（客户端 -> 服务器）：
 * 1. 服务器接收到网络数据包
 * 2. 创建对应的数据包对象，传入原始数据
 * 3. 调用 Read() 方法从数据包中提取数据到成员变量
 */

#include "MiscPackets.h"

/**
 * @brief 序列化炉石绑定位置更新数据包
 * @return 序列化后的 WorldPacket 指针
 *
 * 数据包格式（按顺序）：
 * - TaggedPosition<Position::XYZ>: 绑定位置坐标（12 字节）
 * - uint32: 绑定地图 ID（4 字节）
 * - uint32: 绑定区域 ID（4 字节）
 *
 * 注意：序列化顺序与客户端解析顺序必须严格一致
 */
WorldPacket const* WorldPackets::Misc::BindPointUpdate::Write()
{
    // 写入绑定位置坐标（X、Y、Z）
    _worldPacket << BindPosition;
    // 写入绑定地图 ID
    _worldPacket << uint32(BindMapID);
    // 写入绑定区域 ID
    _worldPacket << uint32(BindAreaID);

    return &_worldPacket;
}

/**
 * @brief 序列化玩家绑定确认数据包
 * @return 序列化后的 WorldPacket 指针
 *
 * 数据包格式：
 * - ObjectGuid: 绑定者 GUID（8 字节）
 * - uint32: 区域 ID（4 字节）
 */
WorldPacket const* WorldPackets::Misc::PlayerBound::Write()
{
    // 写入绑定者（NPC）的 GUID
    _worldPacket << BinderID;
    // 写入绑定区域的 ID
    _worldPacket << uint32(AreaID);

    return &_worldPacket;
}

/**
 * @brief 序列化绑定确认对话框数据包
 * @return 序列化后的 WorldPacket 指针
 *
 * 数据包格式：
 * - ObjectGuid: 绑定者 GUID（8 字节）
 */
WorldPacket const* WorldPackets::Misc::BinderConfirm::Write()
{
    // 写入绑定者（NPC）的 GUID，用于客户端显示对话框
    _worldPacket << Unit;

    return &_worldPacket;
}

/**
 * @brief 序列化启动镜像计时器数据包
 * @return 序列化后的 WorldPacket 指针
 *
 * 数据包格式：
 * - uint32: 计时器类型 ID（4 字节）
 * - uint32: 当前值（4 字节）
 * - uint32: 最大值（4 字节）
 * - int32: 计时速率（4 字节）
 * - uint8: 暂停标志（1 字节）
 * - uint32: 法术 ID（4 字节）
 *
 * 计时器说明：
 * - Timer: 计时器类型（如呼吸计时器、疲劳计时器等）
 * - Value: 当前计时值（毫秒）
 * - MaxValue: 计时器最大值（毫秒）
 * - Scale: 计时速率（毫秒/秒），正值递减，负值递增
 * - Paused: 是否初始暂停
 * - SpellID: 关联的法术 ID，用于客户端显示图标
 */
WorldPacket const* WorldPackets::Misc::StartMirrorTimer::Write()
{
    // 写入计时器类型 ID
    _worldPacket << uint32(Timer);
    // 写入当前值
    _worldPacket << uint32(Value);
    // 写入最大值
    _worldPacket << uint32(MaxValue);
    // 写入计时速率（可为负值，表示递增）
    _worldPacket << int32(Scale);
    // 写入暂停标志（0 = 运行，1 = 暂停）
    _worldPacket << uint8(Paused);
    // 写入关联的法术 ID
    _worldPacket << uint32(SpellID);

    return &_worldPacket;
}

/**
 * @brief 序列化暂停/恢复镜像计时器数据包
 * @return 序列化后的 WorldPacket 指针
 *
 * 数据包格式：
 * - uint32: 计时器类型 ID（4 字节）
 * - uint8: 暂停标志（1 字节）
 */
WorldPacket const* WorldPackets::Misc::PauseMirrorTimer::Write()
{
    // 写入计时器类型 ID
    _worldPacket << uint32(Timer);
    // 写入暂停标志（true = 暂停，false = 恢复）
    _worldPacket << uint8(Paused);

    return &_worldPacket;
}

/**
 * @brief 序列化停止镜像计时器数据包
 * @return 序列化后的 WorldPacket 指针
 *
 * 数据包格式：
 * - uint32: 计时器类型 ID（4 字节）
 */
WorldPacket const* WorldPackets::Misc::StopMirrorTimer::Write()
{
    // 写入计时器类型 ID，客户端收到后会移除对应的计时器 UI
    _worldPacket << uint32(Timer);

    return &_worldPacket;
}

/**
 * @brief 序列化使玩家失效数据包
 * @return 序列化后的 WorldPacket 指针
 *
 * 数据包格式：
 * - ObjectGuid: 需要失效处理的玩家 GUID（8 字节）
 */
WorldPacket const* WorldPackets::Misc::InvalidatePlayer::Write()
{
    // 写入需要失效处理的玩家 GUID
    _worldPacket << Guid;

    return &_worldPacket;
}

/**
 * @brief 序列化登录时设置游戏时间和速度数据包
 * @return 序列化后的 WorldPacket 指针
 *
 * 数据包格式：
 * - WowTime: 游戏时间（结构体，包含年月日时分秒等）
 * - float: 时间流逝速度倍率（4 字节）
 * - uint32: 节假日时间偏移量（4 字节）
 *
 * 时间系统说明：
 * - GameTime: 当前游戏世界时间，用于显示昼夜循环
 * - NewSpeed: 时间流逝速度，1.0 = 实时，>1.0 = 加速，<1.0 = 减速
 * - GameTimeHolidayOffset: 节假日特殊时间偏移，用于节日活动
 */
WorldPacket const* WorldPackets::Misc::LoginSetTimeSpeed::Write()
{
    // 写入当前游戏时间
    _worldPacket << GameTime;
    // 写入时间流逝速度倍率
    _worldPacket << float(NewSpeed);
    // 写入节假日时间偏移量
    _worldPacket << uint32(GameTimeHolidayOffset);

    return &_worldPacket;
}

/**
 * @brief 序列化触发电影播放数据包
 * @return 序列化后的 WorldPacket 指针
 *
 * 数据包格式：
 * - uint32: 电影 ID（4 字节）
 *
 * 电影 vs 过场动画：
 * - Movie: 高质量预渲染视频（如资料片开场动画）
 * - Cinematic: 游戏内过场动画（如种族介绍）
 */
WorldPacket const* WorldPackets::Misc::TriggerMovie::Write()
{
    // 写入电影 ID
    _worldPacket << uint32(MovieID);

    return &_worldPacket;
}

/**
 * @brief 序列化触发过场动画数据包
 * @return 序列化后的 WorldPacket 指针
 *
 * 数据包格式：
 * - uint32: 过场动画 ID（4 字节）
 */
WorldPacket const* WorldPackets::Misc::TriggerCinematic::Write()
{
    // 写入过场动画 ID
    _worldPacket << uint32(CinematicID);

    return &_worldPacket;
}

/**
 * @brief 天气数据包默认构造函数
 *
 * 初始化空天气数据包，通常用于清除天气效果
 */
WorldPackets::Misc::Weather::Weather() : ServerPacket(SMSG_WEATHER, 4 + 4 + 1) { }

/**
 * @brief 天气数据包带参数构造函数
 * @param weatherID 天气状态 ID（雨、雪、沙尘等）
 * @param intensity 天气强度（0.0 - 1.0）
 * @param abrupt 是否立即切换（true = 无过渡效果）
 *
 * 常见天气类型：
 * - 雨天（LIGHT_RAIN, MEDIUM_RAIN, HEAVY_RAIN）
 * - 雪天（LIGHT_SNOW, MEDIUM_SNOW, HEAVY_SNOW）
 * - 沙尘暴（DUST_STORM）
 * - 雾天（FOG）
 */
WorldPackets::Misc::Weather::Weather(WeatherState weatherID, float intensity /*= 0.0f*/, bool abrupt /*= false*/)
    : ServerPacket(SMSG_WEATHER, 4 + 4 + 1), Abrupt(abrupt), Intensity(intensity), WeatherID(weatherID) { }

/**
 * @brief 序列化天气状态更新数据包
 * @return 序列化后的 WorldPacket 指针
 *
 * 数据包格式：
 * - uint32: 天气状态 ID（4 字节）
 * - float: 天气强度（4 字节）
 * - uint8: 是否立即切换（1 字节）
 */
WorldPacket const* WorldPackets::Misc::Weather::Write()
{
    // 写入天气状态 ID
    _worldPacket << uint32(WeatherID);
    // 写入天气强度（0.0 = 无天气，1.0 = 最大强度）
    _worldPacket << float(Intensity);
    // 写入切换方式标志（true = 立即切换，false = 渐变过渡）
    _worldPacket << uint8(Abrupt);

    return &_worldPacket;
}

/**
 * @brief 序列化升级信息数据包
 * @return 序列化后的 WorldPacket 指针
 *
 * 数据包格式：
 * - uint32: 新等级（4 字节）
 * - uint32: 生命值增量（4 字节）
 * - uint32[MAX_POWERS]: 各能量值增量（如法力、怒气、能量等）
 * - uint32[MAX_STATS]: 各基础属性增量（力量、敏捷、耐力、智力、精神）
 *
 * 升级系统说明：
 * - 玩家升级时，服务器计算各项属性增量并发送给客户端
 * - 客户端据此显示升级特效和属性面板
 */
WorldPacket const* WorldPackets::Misc::LevelUpInfo::Write()
{
    // 写入新等级
    _worldPacket << uint32(Level);
    // 写入生命值增量
    _worldPacket << uint32(HealthDelta);

    // 写入各能量值增量（法力、怒气、专注值、能量、快乐值等）
    for (uint32 power : PowerDelta)
        _worldPacket << power;

    // 写入各基础属性增量（力量、敏捷、耐力、智力、精神）
    for (uint32 stat : StatDelta)
        _worldPacket << stat;

    return &_worldPacket;
}

/**
 * @brief 序列化播放背景音乐数据包
 * @return 序列化后的 WorldPacket 指针
 *
 * 数据包格式：
 * - uint32: 音效包 ID（4 字节）
 *
 * 音乐播放机制：
 * - 客户端根据 SoundKitID 查找对应的音乐文件并播放
 * - 播放会替换当前背景音乐
 */
WorldPacket const* WorldPackets::Misc::PlayMusic::Write()
{
    // 写入音效包 ID
    _worldPacket << SoundKitID;

    return &_worldPacket;
}

/**
 * @brief 序列化播放对象音效数据包
 * @return 序列化后的 WorldPacket 指针
 *
 * 数据包格式：
 * - uint32: 音效包 ID（4 字节）
 * - ObjectGuid: 音效源对象 GUID（8 字节）
 *
 * 3D 音效说明：
 * - 音效会在源对象位置播放，支持 3D 空间音效
 * - 玩家听到的音量和方向取决于与源对象的距离和相对位置
 */
WorldPacket const* WorldPackets::Misc::PlayObjectSound::Write()
{
    // 写入音效包 ID
    _worldPacket << SoundKitID;
    // 写入音效源对象的 GUID
    _worldPacket << SourceObjectGUID;

    return &_worldPacket;
}

/**
 * @brief 序列化播放音效数据包
 * @return 序列化后的 WorldPacket 指针
 *
 * 数据包格式：
 * - uint32: 音效包 ID（4 字节）
 *
 * 音效播放说明：
 * - 与 PlayObjectSound 不同，此音效不关联特定对象位置
 * - 用于播放系统级音效（如 UI 音效、任务完成音效等）
 */
WorldPacket const* WorldPackets::Misc::PlaySound::Write()
{
    // 写入音效包 ID
    _worldPacket << SoundKitID;

    return &_worldPacket;
}

/**
 * @brief 序列化醉酒状态阈值跨越通知数据包
 * @return 序列化后的 WorldPacket 指针
 *
 * 数据包格式：
 * - ObjectGuid: 玩家 GUID（8 字节）
 * - uint32: 醉酒等级阈值（4 字节）
 * - uint32: 导致醉酒的物品 ID（4 字节）
 *
 * 醉酒系统说明：
 * - 玩家饮用酒精饮料会积累醉酒值
 * - 阈值跨越时触发视觉效果和游戏机制变化
 * - 醉酒等级影响视觉模糊程度和聊天文字变形
 */
WorldPacket const* WorldPackets::Misc::CrossedInebriationThreshold::Write()
{
    // 写入玩家的 GUID
    _worldPacket << Guid;
    // 写入新的醉酒等级阈值
    _worldPacket << uint32(Threshold);
    // 写入导致醉酒的物品 ID（通常是酒类物品）
    _worldPacket << uint32(ItemID);

    return &_worldPacket;
}

/**
 * @brief 序列化光照覆盖数据包
 * @return 序列化后的 WorldPacket 指针
 *
 * 数据包格式：
 * - int32: 区域光照 ID（4 字节）
 * - int32: 覆盖光照 ID（4 字节）
 * - int32: 过渡时间（4 字节）
 *
 * 光照系统说明：
 * - AreaLightID: 原始区域的光照设置 ID
 * - OverrideLightID: 新的光照设置 ID
 * - TransitionMilliseconds: 光照变化的渐变时间，0 表示立即切换
 */
WorldPacket const* WorldPackets::Misc::OverrideLight::Write()
{
    // 写入区域光照 ID
    _worldPacket << int32(AreaLightID);
    // 写入覆盖光照 ID（新的光照设置）
    _worldPacket << int32(OverrideLightID);
    // 写入过渡时间（毫秒），控制光照变化的平滑度
    _worldPacket << int32(TransitionMilliseconds);

    return &_worldPacket;
}

/**
 * @brief 反序列化随机掷骰客户端请求数据包
 *
 * 从数据包中读取掷骰的最小值和最大值。
 * 客户端发送的格式：
 * - uint32: 最小值
 * - uint32: 最大值
 */
void WorldPackets::Misc::RandomRollClient::Read()
{
    // 读取掷骰最小值
    _worldPacket >> Min;
    // 读取掷骰最大值
    _worldPacket >> Max;
}

/**
 * @brief 序列化随机掷骰结果广播数据包
 * @return 序列化后的 WorldPacket 指针
 *
 * 数据包格式：
 * - uint32: 最小值（4 字节）
 * - uint32: 最大值（4 字节）
 * - uint32: 掷骰结果（4 字节）
 * - ObjectGuid: 掷骰者 GUID（8 字节）
 *
 * 广播机制：
 * - 结果会发送给掷骰者及其附近的所有玩家
 * - 用于团队副本分配战利品时的公平掷骰
 */
WorldPacket const* WorldPackets::Misc::RandomRoll::Write()
{
    // 写入掷骰最小值
    _worldPacket << uint32(Min);
    // 写入掷骰最大值
    _worldPacket << uint32(Max);
    // 写入掷骰结果（随机生成的数值，Min <= Result <= Max）
    _worldPacket << uint32(Result);
    // 写入掷骰者的 GUID
    _worldPacket << Roller;

    return &_worldPacket;
}

/**
 * @brief 序列化 UI 时间更新数据包
 * @return 序列化后的 WorldPacket 指针
 *
 * 数据包格式：
 * - uint32: 服务器时间戳（4 字节）
 *
 * 时间同步说明：
 * - 客户端使用此时间显示任务剩余时间、活动倒计时等 UI 元素
 * - 服务器定期发送以保持时间同步
 */
WorldPacket const* WorldPackets::Misc::UITime::Write()
{
    // 写入服务器时间戳（Unix 时间）
    _worldPacket << uint32(Time);

    return &_worldPacket;
}

/**
 * @brief 反序列化切换 PvP 状态客户端请求数据包
 *
 * 数据包格式有两种：
 * 1. 空数据包（仅操作码）：请求切换 PvP 状态（开 <-> 关）
 * 2. 包含 1 字节布尔值：请求设置具体的 PvP 状态（开或关）
 *
 * 客户端行为：
 * - 点击 PvP 按钮时发送空数据包（切换）
 * - 使用宏命令时可能发送带状态值的数据包
 */
void WorldPackets::Misc::TogglePvP::Read()
{
    // 如果数据包包含状态值（大小为 1 字节），则读取
    if (HasPvPStatus())
        Enable = _worldPacket.read<uint8>() != 0;  // 将字节转换为布尔值
}

/**
 * @brief 反序列化世界传送客户端请求数据包
 *
 * 从数据包中读取传送目标信息。
 * 客户端发送的格式：
 * - uint32: 时间参数
 * - uint32: 目标地图 ID
 * - TaggedPosition<Position::XYZ>: 目标坐标
 * - float: 目标朝向
 *
 * 注意：此功能仅供 GM 或开发工具使用，普通玩家无法触发
 */
void WorldPackets::Misc::WorldTeleport::Read()
{
    // 读取时间参数（用途不明，可能与传送验证相关）
    _worldPacket >> Time;
    // 读取目标地图 ID
    _worldPacket >> MapID;
    // 读取目标坐标（X、Y、Z）
    _worldPacket >> Pos;
    // 读取目标朝向（弧度）
    _worldPacket >> Facing;
}

/**
 * @brief 序列化尸体回收延迟数据包
 * @return 序列化后的 WorldPacket 指针
 *
 * 数据包格式：
 * - uint32: 剩余等待时间（4 字节，单位：毫秒）
 *
 * 延迟机制说明：
 * - 玩家死亡后需要等待一定时间才能回收尸体复活
 * - 延迟时间可能因 PvP 死亡、特定法术效果而增加
 * - 客户端据此显示倒计时 UI
 */
WorldPacket const* WorldPackets::Misc::CorpseReclaimDelay::Write()
{
    // 写入剩余等待时间（毫秒）
    _worldPacket << uint32(Remaining);

    return &_worldPacket;
}

/**
 * @brief 序列化死亡释放位置数据包
 * @return 序列化后的 WorldPacket 指针
 *
 * 数据包格式：
 * - int32: 地图 ID（4 字节）
 * - TaggedPosition<Position::XYZ>: 位置坐标（12 字节）
 *
 * 位置说明：
 * - 如果玩家未释放灵魂，位置为尸体位置
 * - 如果玩家已释放灵魂，位置为墓地位置
 */
WorldPacket const* WorldPackets::Misc::DeathReleaseLoc::Write()
{
    // 写入地图 ID
    _worldPacket << int32(MapID);
    // 写入位置坐标（X、Y、Z）
    _worldPacket << Loc;

    return &_worldPacket;
}

/**
 * @brief 序列化复活前通知数据包
 * @return 序列化后的 WorldPacket 指针
 *
 * 数据包格式：
 * - PackedGuid: 玩家 GUID（压缩格式，大小可变）
 *
 * 压缩 GUID 说明：
 * - WriteAsPacked() 将 GUID 压缩为更紧凑的格式，减少数据包大小
 * - 仅发送 GUID 中非零的字节，节省网络带宽
 *
 * 注意：类名拼写错误（PreRessurect），应为 PreResurrect，但为了兼容性保留
 */
WorldPacket const* WorldPackets::Misc::PreRessurect::Write()
{
    // 写入玩家 GUID（使用压缩格式）
    _worldPacket << PlayerGUID.WriteAsPacked();

    return &_worldPacket;
}

/**
 * @brief 反序列化回收尸体客户端请求数据包
 *
 * 从数据包中读取尸体 GUID。
 * 客户端发送的格式：
 * - ObjectGuid: 尸体 GUID
 *
 * 服务器验证：
 * - 玩家必须在尸体附近
 * - 回收延迟时间已过
 * - 玩家处于灵魂状态
 */
void WorldPackets::Misc::ReclaimCorpse::Read()
{
    // 读取尸体 GUID
    _worldPacket >> CorpseGUID;
}

/**
 * @brief 反序列化请求传送到墓地客户端请求数据包
 *
 * 从数据包中读取实例检查标志。
 * 客户端发送的格式：
 * - bool: 是否检查实例限制（1 字节）
 *
 * 实例处理说明：
 * - 如果 CheckInstance 为 true，服务器需要检查玩家是否在实例中
 * - 某些实例可能有特殊的墓地传送规则
 */
void WorldPackets::Misc::RepopRequest::Read()
{
    // 读取实例检查标志
    _worldPacket >> CheckInstance;
}

/**
 * @brief 反序列化复活响应客户端请求数据包
 *
 * 从数据包中读取复活者 GUID 和响应值。
 * 客户端发送的格式：
 * - ObjectGuid: 复活者 GUID
 * - uint8: 响应值（0 = 拒绝，1 = 接受）
 *
 * 复活机制说明：
 * - 圣骑士、萨满、术士等职业可以复活死亡玩家
 * - 死亡玩家收到复活请求后，客户端显示对话框
 * - 玩家选择接受或拒绝，发送此数据包给服务器
 * - 如果接受，玩家在复活者位置复活（或尸体位置）
 */
void WorldPackets::Misc::ResurrectResponse::Read()
{
    // 读取复活者（发起复活请求的玩家）的 GUID
    _worldPacket >> Resurrecter;
    // 读取响应值（0 = 拒绝，1 = 接受）
    _worldPacket >> Response;
}
