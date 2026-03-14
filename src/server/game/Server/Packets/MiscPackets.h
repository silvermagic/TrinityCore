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
 * @file MiscPackets.h
 * @brief 杂项网络数据包定义文件
 *
 * 本文件包含游戏中各种杂项功能的网络数据包类定义，主要涵盖以下功能模块：
 * - 炉石绑定点管理（BindPointUpdate、PlayerBound、BinderConfirm）
 * - 镜像计时器系统（StartMirrorTimer、PauseMirrorTimer、StopMirrorTimer），用于呼吸、疲劳等计时
 * - 玩家状态通知（InvalidatePlayer、LoginSetTimeSpeed、DurabilityDamageDeath）
 * - 媒体播放（TriggerCinematic、TriggerMovie、PlayMusic、PlaySound、PlayObjectSound）
 * - 天气系统（Weather）
 * - 升级信息（LevelUpInfo）
 * - 醉酒系统（CrossedInebriationThreshold）
 * - 光照系统（OverrideLight）
 * - 随机掷骰（RandomRollClient、RandomRoll）
 * - PvP状态（TogglePvP）
 * - 时间同步（UITime）
 * - 世界传送（WorldTeleport）
 * - 死亡与复活系统（CorpseReclaimDelay、DeathReleaseLoc、PreRessurect、ReclaimCorpse、RepopRequest、ResurrectResponse）
 *
 * 所有数据包类继承自 ServerPacket（服务器->客户端）或 ClientPacket（客户端->服务器），
 * 通过实现 Write() 或 Read() 方法进行序列化和反序列化。
 */

#ifndef MiscPackets_h__
#define MiscPackets_h__

#include "Packet.h"
#include "Position.h"
#include "ObjectGuid.h"
#include "Optional.h"
#include "Weather.h"
#include "WowTime.h"
#include <array>

// 前向声明：天气状态枚举
enum WeatherState : uint32;

namespace WorldPackets
{
    /**
     * @namespace Misc
     * @brief 杂项数据包命名空间
     *
     * 包含各种游戏功能相关的网络数据包类定义
     */
    namespace Misc
    {
        /**
         * @class BindPointUpdate
         * @brief 炉石绑定位置更新数据包
         *
         * 继承自 ServerPacket，用于向客户端发送玩家的炉石绑定位置信息。
         * 当玩家设置新的炉石绑定点时，服务器发送此数据包更新客户端的绑定位置。
         *
         * 调用时机：
         * - 玩家与旅店老板交互并设置炉石绑定点时
         * - 使用 GM 命令修改玩家绑定位置时
         */
        class BindPointUpdate final : public ServerPacket
        {
        public:
            /**
             * @brief 构造函数
             *
             * 初始化数据包大小为 20 字节（位置 12 字节 + 地图 ID 4 字节 + 区域 ID 4 字节）
             */
            BindPointUpdate() : ServerPacket(SMSG_BIND_POINT_UPDATE, 20) { }

            /**
             * @brief 序列化数据包
             * @return 序列化后的 WorldPacket 指针
             *
             * 将绑定位置、地图 ID 和区域 ID 写入数据包
             */
            WorldPacket const* Write() override;

            uint32 BindMapID = 0;                          ///< 绑定点的地图 ID
            TaggedPosition<Position::XYZ> BindPosition;    ///< 绑定点的世界坐标（X、Y、Z）
            uint32 BindAreaID = 0;                         ///< 绑定点的区域 ID
        };

        /**
         * @class PlayerBound
         * @brief 玩家绑定确认数据包
         *
         * 继承自 ServerPacket，用于通知客户端玩家已成功绑定到某个区域。
         * 与 BindPointUpdate 不同，此数据包携带绑定者（NPC）的 GUID 和区域 ID。
         *
         * 调用时机：
         * - 玩家成功设置炉石绑定点后，向客户端发送确认信息
         */
        class PlayerBound final : public ServerPacket
        {
        public:
            /**
             * @brief 默认构造函数
             *
             * 初始化数据包大小为 12 字节（GUID 8 字节 + 区域 ID 4 字节）
             */
            PlayerBound() : ServerPacket(SMSG_PLAYER_BOUND, 8 + 4) { }

            /**
             * @brief 带参数的构造函数
             * @param binderId 绑定者（如旅店老板）的 GUID
             * @param areaId 绑定区域的 ID
             */
            PlayerBound(ObjectGuid binderId, uint32 areaId) : ServerPacket(SMSG_PLAYER_BOUND, 8 + 4), BinderID(binderId), AreaID(areaId) { }

            /**
             * @brief 序列化数据包
             * @return 序列化后的 WorldPacket 指针
             */
            WorldPacket const* Write() override;

            ObjectGuid BinderID;     ///< 绑定者（NPC）的 GUID，通常是旅店老板
            uint32 AreaID = 0;       ///< 绑定区域的 ID
        };

        /**
         * @class BinderConfirm
         * @brief 绑定确认对话框数据包
         *
         * 继承自 ServerPacket，用于触发客户端显示绑定确认对话框。
         * 玩家与旅店老板交互时，服务器发送此数据包显示"是否将此地设为家?"的对话框。
         *
         * 调用时机：
         * - 玩家与旅店老板对话并选择绑定选项时
         */
        class BinderConfirm final : public ServerPacket
        {
        public:
            /**
             * @brief 默认构造函数
             *
             * 初始化数据包大小为 8 字节（GUID 大小）
             */
            BinderConfirm() : ServerPacket(SMSG_BINDER_CONFIRM, 8) { }

            /**
             * @brief 带参数的构造函数
             * @param unit 绑定者（NPC）的 GUID
             */
            BinderConfirm(ObjectGuid unit) : ServerPacket(SMSG_BINDER_CONFIRM, 8), Unit(unit) { }

            /**
             * @brief 序列化数据包
             * @return 序列化后的 WorldPacket 指针
             */
            WorldPacket const* Write() override;

            ObjectGuid Unit;  ///< 绑定者（NPC）的 GUID
        };

        /**
         * @class StartMirrorTimer
         * @brief 启动镜像计时器数据包
         *
         * 继承自 ServerPacket，用于启动客户端的镜像计时器（倒计时 UI）。
         * 镜像计时器用于显示各种计时效果，如：
         * - 水下呼吸时间（疲劳条）
         * - 一些特殊法术的持续时间
         * - 任务相关的计时效果
         *
         * 计时器显示为一个从当前值到最大值的进度条，可以正向或反向计数。
         *
         * 调用时机：
         * - 玩家进入深水区域开始计算呼吸时间时
         * - 施加需要显示计时器的法术效果时
         *
         * 性能注意事项：
         * - 频繁更新计时器可能产生大量网络流量，应合理控制更新频率
         */
        class StartMirrorTimer final : public ServerPacket
        {
        public:
            /**
             * @brief 默认构造函数
             *
             * 初始化数据包大小为 21 字节
             */
            StartMirrorTimer() : ServerPacket(SMSG_START_MIRROR_TIMER, 21) { }

            /**
             * @brief 完整参数构造函数
             * @param timer 计时器类型 ID
             * @param value 当前值
             * @param maxValue 最大值
             * @param scale 计时速率（每秒变化量，负值表示递增）
             * @param paused 是否暂停
             * @param spellID 关联的法术 ID（用于客户端显示法术图标）
             */
            StartMirrorTimer(uint32 timer, uint32 value, uint32 maxValue, int32 scale, bool paused, uint32 spellID) :
                ServerPacket(SMSG_START_MIRROR_TIMER, 21), Timer(timer), Value(value), MaxValue(maxValue), Scale(scale), Paused(paused), SpellID(spellID) { }

            /**
             * @brief 序列化数据包
             * @return 序列化后的 WorldPacket 指针
             */
            WorldPacket const* Write() override;

            uint32 Timer = 0;       ///< 计时器类型 ID（例如：FATIGUE_TIMER = 0, BREATH_TIMER = 1 等）
            uint32 Value = 0;       ///< 当前值（毫秒）
            uint32 MaxValue = 0;    ///< 最大值（毫秒）
            int32 Scale = 0;        ///< 计时速率（毫秒/秒），负值表示递增，正值表示递减
            bool Paused = false;    ///< 是否暂停计时器
            uint32 SpellID = 0;     ///< 关联的法术 ID（用于 UI 显示图标）
        };

        /**
         * @class PauseMirrorTimer
         * @brief 暂停/恢复镜像计时器数据包
         *
         * 继承自 ServerPacket，用于暂停或恢复正在运行的镜像计时器。
         *
         * 调用时机：
         * - 玩家离开深水区域，呼吸计时器暂停时
         * - 特定法术效果导致计时器需要暂停/恢复时
         */
        class PauseMirrorTimer final : public ServerPacket
        {
        public:
            /**
             * @brief 默认构造函数
             *
             * 初始化数据包大小为 5 字节（计时器 ID 4 字节 + 暂停标志 1 字节）
             */
            PauseMirrorTimer() : ServerPacket(SMSG_PAUSE_MIRROR_TIMER, 5) { }

            /**
             * @brief 带参数的构造函数
             * @param timer 计时器类型 ID
             * @param paused 是否暂停（true = 暂停，false = 恢复）
             */
            PauseMirrorTimer(uint32 timer, bool paused) : ServerPacket(SMSG_PAUSE_MIRROR_TIMER, 5), Timer(timer), Paused(paused) { }

            /**
             * @brief 序列化数据包
             * @return 序列化后的 WorldPacket 指针
             */
            WorldPacket const* Write() override;

            uint32 Timer = 0;      ///< 计时器类型 ID
            bool Paused = true;    ///< 是否暂停（默认为 true）
        };

        /**
         * @class StopMirrorTimer
         * @brief 停止镜像计时器数据包
         *
         * 继承自 ServerPacket，用于停止并移除客户端的镜像计时器显示。
         *
         * 调用时机：
         * - 玩家完全离开水域，呼吸计时器停止时
         * - 法术效果结束，计时器不再需要时
         */
        class StopMirrorTimer final : public ServerPacket
        {
        public:
            /**
             * @brief 默认构造函数
             *
             * 初始化数据包大小为 4 字节（计时器 ID）
             */
            StopMirrorTimer() : ServerPacket(SMSG_STOP_MIRROR_TIMER, 4) { }

            /**
             * @brief 带参数的构造函数
             * @param timer 计时器类型 ID
             */
            StopMirrorTimer(uint32 timer) : ServerPacket(SMSG_STOP_MIRROR_TIMER, 4), Timer(timer) { }

            /**
             * @brief 序列化数据包
             * @return 序列化后的 WorldPacket 指针
             */
            WorldPacket const* Write() override;

            uint32 Timer = 0;  ///< 计时器类型 ID
        };

        /**
         * @class InvalidatePlayer
         * @brief 使玩家失效数据包
         *
         * 继承自 ServerPacket，用于通知客户端某个玩家对象已失效。
         * 这通常用于清除客户端缓存的玩家数据，强制客户端重新请求该玩家的信息。
         *
         * 调用时机：
         * - 玩家改名后，通知其他客户端刷新该玩家信息
         * - 玩家某些外观或属性发生重大变化需要客户端重新加载时
         */
        class InvalidatePlayer final : public ServerPacket
        {
        public:
            /**
             * @brief 默认构造函数
             *
             * 初始化数据包大小为 8 字节（GUID 大小）
             */
            InvalidatePlayer() : ServerPacket(SMSG_INVALIDATE_PLAYER, 8) { }

            /**
             * @brief 带参数的构造函数
             * @param guid 需要失效处理的玩家 GUID
             */
            InvalidatePlayer(ObjectGuid guid) : ServerPacket(SMSG_INVALIDATE_PLAYER, 8), Guid(guid) { }

            /**
             * @brief 序列化数据包
             * @return 序列化后的 WorldPacket 指针
             */
            WorldPacket const* Write() override;

            ObjectGuid Guid;  ///< 需要失效处理的玩家 GUID
        };

        /**
         * @class LoginSetTimeSpeed
         * @brief 登录时设置游戏时间和速度数据包
         *
         * 继承自 ServerPacket，用于在玩家登录时同步游戏世界时间和时间流逝速度。
         * 客户端根据这些信息显示游戏内的时钟和昼夜循环。
         *
         * 调用时机：
         * - 玩家成功登录世界后，首次发送
         * - 服务器时间设置发生变化时（较少见）
         *
         * 性能注意事项：
         * - 只在必要时发送，避免频繁更新时间同步
         */
        class LoginSetTimeSpeed final : public ServerPacket
        {
        public:
            /**
             * @brief 默认构造函数
             *
             * 初始化数据包大小为 12 字节
             */
            LoginSetTimeSpeed() : ServerPacket(SMSG_LOGIN_SET_TIME_SPEED, 12) { }

            /**
             * @brief 序列化数据包
             * @return 序列化后的 WorldPacket 指针
             */
            WorldPacket const* Write() override;

            float NewSpeed = 0.0f;                ///< 游戏时间流逝速度倍率（相对于真实时间）
            WowTime GameTime;                     ///< 当前游戏时间
            int32 GameTimeHolidayOffset = 0;      ///< 节假日时间偏移量（毫秒）
        };

        /**
         * @class DurabilityDamageDeath
         * @brief 死亡装备耐久度损失通知数据包
         *
         * 继承自 ServerPacket，用于通知客户端玩家因死亡而造成的装备耐久度损失。
         * 这是一个空数据包，仅包含操作码，客户端收到后会自动计算并显示耐久度损失。
         *
         * 调用时机：
         * - 玩家死亡时，通知客户端显示装备损坏效果
         *
         * 性能注意事项：
         * - 数据包非常小（仅操作码），无额外数据负载
         */
        class DurabilityDamageDeath final : public ServerPacket
        {
        public:
            /**
             * @brief 默认构造函数
             *
             * 初始化数据包大小为 0 字节（仅操作码）
             */
            DurabilityDamageDeath() : ServerPacket(SMSG_DURABILITY_DAMAGE_DEATH, 0) { }

            /**
             * @brief 序列化数据包
             * @return 序列化后的 WorldPacket 指针
             *
             * 此数据包不包含额外数据，直接返回空数据包
             */
            WorldPacket const* Write() override { return &_worldPacket; }
        };

        /**
         * @class TriggerCinematic
         * @brief 触发过场动画数据包
         *
         * 继承自 ServerPacket，用于触发客户端播放指定的过场动画（Cinematic）。
         * 过场动画是预录制的视频序列，如新角色创建时的种族介绍动画。
         *
         * 调用时机：
         * - 新角色首次进入游戏时，播放种族介绍动画
         * - 完成特定任务或达成成就时
         * - 使用特定物品触发动画时
         *
         * 性能注意事项：
         * - 动画播放期间玩家无法操作，应谨慎使用
         */
        class TriggerCinematic final : public ServerPacket
        {
        public:
            /**
             * @brief 默认构造函数
             *
             * 初始化数据包大小为 4 字节（动画 ID）
             */
            TriggerCinematic() : ServerPacket(SMSG_TRIGGER_CINEMATIC, 4) { }

            /**
             * @brief 序列化数据包
             * @return 序列化后的 WorldPacket 指针
             */
            WorldPacket const* Write() override;

            uint32 CinematicID = 0;  ///< 过场动画 ID（参考 CinematicSequences.dbc）
        };

        /**
         * @class TriggerMovie
         * @brief 触发电影播放数据包
         *
         * 继承自 ServerPacket，用于触发客户端播放电影序列。
         * 与 Cinematic 不同，Movie 是更高质量的视频序列（如巫妖王之怒开场动画）。
         *
         * 调用时机：
         * - 完成关键任务链时（如史诗任务线的结局）
         * - 特殊游戏事件触发时
         */
        class TriggerMovie final : public ServerPacket
        {
        public:
            /**
             * @brief 默认构造函数
             *
             * 初始化数据包大小为 4 字节（电影 ID）
             */
            TriggerMovie() : ServerPacket(SMSG_TRIGGER_MOVIE, 4) { }

            /**
             * @brief 序列化数据包
             * @return 序列化后的 WorldPacket 指针
             */
            WorldPacket const* Write() override;

            uint32 MovieID = 0;  ///< 电影 ID（参考 Movie.dbc）
        };

        /**
         * @class Weather
         * @brief 天气状态更新数据包
         *
         * 继承自 ServerPacket，用于向客户端发送天气变化信息。
         * 天气系统包括雨、雪、沙尘暴、雾等多种天气类型。
         *
         * 调用时机：
         * - 玩家进入新区域时，根据区域天气设置发送
         * - 天气动态变化时（由游戏世界天气系统触发）
         * - GM 命令强制改变天气时
         *
         * 性能注意事项：
         * - 天气更新不应过于频繁，通常按游戏时间周期变化
         * - Abrupt 标志为 true 时会立即切换天气，可能造成视觉突变
         */
        class TC_GAME_API Weather final : public ServerPacket
        {
        public:
            /**
             * @brief 默认构造函数
             *
             * 初始化数据包大小为 9 字节（天气 ID 4 字节 + 强度 4 字节 + 突变标志 1 字节）
             */
            Weather();

            /**
             * @brief 带参数的构造函数
             * @param weatherID 天气状态 ID（雨、雪、沙尘等）
             * @param intensity 天气强度（0.0 - 1.0）
             * @param abrupt 是否立即切换（true = 立即切换，false = 渐变过渡）
             */
            Weather(WeatherState weatherID, float intensity = 0.0f, bool abrupt = false);

            /**
             * @brief 序列化数据包
             * @return 序列化后的 WorldPacket 指针
             */
            WorldPacket const* Write() override;

            bool Abrupt = false;                    ///< 是否立即切换天气（true = 无过渡效果）
            float Intensity = 0.0f;                 ///< 天气强度（0.0 = 无天气，1.0 = 最大强度）
            WeatherState WeatherID = WeatherState(0);  ///< 天气状态 ID（雨、雪、雾等）
        };

        /**
         * @class LevelUpInfo
         * @brief 升级信息数据包
         *
         * 继承自 ServerPacket，用于向客户端发送玩家升级时获得的属性提升信息。
         * 包含生命值、法力值（及其他能量值）、基础属性的增量。
         *
         * 调用时机：
         * - 玩家经验值达到升级阈值，等级提升时
         *
         * 数据包结构：
         * - 等级：4 字节
         * - 生命值增量：4 字节
         * - 各能量值增量：MAX_POWERS * 4 字节
         * - 各基础属性增量：MAX_STATS * 4 字节
         */
        class LevelUpInfo final : public ServerPacket
        {
        public:
            /**
             * @brief 默认构造函数
             *
             * 初始化数据包大小为 56 字节
             */
            LevelUpInfo() : ServerPacket(SMSG_LEVELUP_INFO, 56) { }

            /**
             * @brief 序列化数据包
             * @return 序列化后的 WorldPacket 指针
             */
            WorldPacket const* Write() override;

            uint32 Level = 0;                                  ///< 新等级
            uint32 HealthDelta = 0;                            ///< 生命值增量
            std::array<uint32, MAX_POWERS> PowerDelta = { };   ///< 各能量值增量（法力、怒气等）
            std::array<uint32, MAX_STATS> StatDelta = { };     ///< 各基础属性增量（力量、敏捷、耐力、智力、精神）
        };

        /**
         * @class PlayMusic
         * @brief 播放背景音乐数据包
         *
         * 继承自 ServerPacket，用于触发客户端播放背景音乐。
         * 音乐播放是全局性的，会替换当前区域背景音乐。
         *
         * 调用时机：
         * - 玩家进入特定区域或场景时
         * - 触发特定事件或任务时
         * - GM 命令强制播放音乐时
         *
         * 性能注意事项：
         * - 音乐文件较大，客户端会在后台加载
         * - 频繁切换音乐可能影响玩家体验
         */
        class TC_GAME_API PlayMusic final : public ServerPacket
        {
        public:
            /**
             * @brief 默认构造函数
             *
             * 初始化数据包大小为 4 字节（音效包 ID）
             */
            PlayMusic() : ServerPacket(SMSG_PLAY_MUSIC, 4) { }

            /**
             * @brief 带参数的构造函数
             * @param soundKitID 音效包 ID（参考 SoundEntries.dbc）
             */
            PlayMusic(uint32 soundKitID) : ServerPacket(SMSG_PLAY_MUSIC, 4), SoundKitID(soundKitID) { }

            /**
             * @brief 序列化数据包
             * @return 序列化后的 WorldPacket 指针
             */
            WorldPacket const* Write() override;

            uint32 SoundKitID = 0;  ///< 音效包 ID（定义音乐或音效）
        };

        /**
         * @class PlayObjectSound
         * @brief 播放对象音效数据包
         *
         * 继承自 ServerPacket，用于播放与特定游戏对象关联的音效。
         * 与 PlayMusic 不同，这是播放一次性音效，且音效源来自特定对象。
         *
         * 调用时机：
         * - NPC 发声或对话时
         * - 游戏对象（门、箱子等）被激活时
         * - 法术施放时播放施法者位置的音效
         *
         * 性能注意事项：
         * - 音效播放是短时的，不影响背景音乐
         * - 带位置信息的音效支持 3D 空间音效
         */
        class TC_GAME_API PlayObjectSound final : public ServerPacket
        {
        public:
            /**
             * @brief 默认构造函数
             *
             * 初始化数据包大小为 12 字节（音效 ID 4 字节 + GUID 8 字节）
             */
            PlayObjectSound() : ServerPacket(SMSG_PLAY_OBJECT_SOUND, 4 + 8) { }

            /**
             * @brief 带参数的构造函数
             * @param sourceObjectGUID 音效源对象的 GUID
             * @param soundKitID 音效包 ID
             */
            PlayObjectSound(ObjectGuid const& sourceObjectGUID, uint32 soundKitID)
                : ServerPacket(SMSG_PLAY_OBJECT_SOUND, 4 + 8), SourceObjectGUID(sourceObjectGUID), SoundKitID(soundKitID) { }

            /**
             * @brief 序列化数据包
             * @return 序列化后的 WorldPacket 指针
             */
            WorldPacket const* Write() override;

            ObjectGuid SourceObjectGUID;  ///< 音效源对象的 GUID
            uint32 SoundKitID = 0;        ///< 音效包 ID

        };

        /**
         * @class PlaySound
         * @brief 播放音效数据包
         *
         * 继承自 ServerPacket，用于播放通用音效。
         * 与 PlayObjectSound 不同，此音效不关联特定对象位置。
         *
         * 调用时机：
         * - 系统级音效（如 UI 点击音效、警告音效）
         * - 任务完成、成就解锁等事件音效
         * - 环境音效
         */
        class TC_GAME_API PlaySound final : public ServerPacket
        {
        public:
            /**
             * @brief 默认构造函数
             *
             * 初始化数据包大小为 4 字节（音效 ID）
             */
            PlaySound() : ServerPacket(SMSG_PLAY_SOUND, 4) { }

            /**
             * @brief 带参数的构造函数
             * @param soundKitID 音效包 ID
             */
            PlaySound(uint32 soundKitID) : ServerPacket(SMSG_PLAY_SOUND, 4), SoundKitID(soundKitID) { }

            /**
             * @brief 序列化数据包
             * @return 序列化后的 WorldPacket 指针
             */
            WorldPacket const* Write() override;

            uint32 SoundKitID = 0;  ///< 音效包 ID
        };

        /**
         * @class CompleteCinematic
         * @brief 过场动画完成通知数据包
         *
         * 继承自 ClientPacket，由客户端发送，通知服务器过场动画已播放完毕。
         * 这是一个空数据包，仅包含操作码。
         *
         * 调用时机：
         * - 客户端播放完过场动画后，自动发送
         *
         * 服务器处理：
         * - 服务器收到后可能触发后续任务进度、成就更新等逻辑
         */
        class CompleteCinematic final : public ClientPacket
        {
        public:
            /**
             * @brief 构造函数
             * @param packet 接收到的原始网络数据包
             */
            CompleteCinematic(WorldPacket&& packet) : ClientPacket(CMSG_COMPLETE_CINEMATIC, std::move(packet)) { }

            /**
             * @brief 反序列化数据包
             *
             * 此数据包不包含额外数据，无需读取
             */
            void Read() override { }
        };

        /**
         * @class NextCinematicCamera
         * @brief 切换过场动画摄像机通知数据包
         *
         * 继承自 ClientPacket，由客户端发送，请求切换到下一个摄像机视角。
         * 某些过场动画支持多个摄像机视角，玩家可以手动切换。
         *
         * 调用时机：
         * - 玩家在观看过场动画时按下摄像机切换键
         *
         * 服务器处理：
         * - 通常服务器只需记录玩家跳过了某个摄像机序列
         */
        class NextCinematicCamera final : public ClientPacket
        {
        public:
            /**
             * @brief 构造函数
             * @param packet 接收到的原始网络数据包
             */
            NextCinematicCamera(WorldPacket&& packet) : ClientPacket(CMSG_NEXT_CINEMATIC_CAMERA, std::move(packet)) { }

            /**
             * @brief 反序列化数据包
             *
             * 此数据包不包含额外数据，无需读取
             */
            void Read() override { }
        };

        /**
         * @class CompleteMovie
         * @brief 电影播放完成通知数据包
         *
         * 继承自 ClientPacket，由客户端发送，通知服务器电影已播放完毕。
         * 这是一个空数据包，仅包含操作码。
         *
         * 调用时机：
         * - 客户端播放完电影后，自动发送
         *
         * 服务器处理：
         * - 服务器收到后可能触发后续任务进度、成就更新等逻辑
         */
        class CompleteMovie final : public ClientPacket
        {
        public:
            /**
             * @brief 构造函数
             * @param packet 接收到的原始网络数据包
             */
            CompleteMovie(WorldPacket&& packet) : ClientPacket(CMSG_COMPLETE_MOVIE, std::move(packet)) { }

            /**
             * @brief 反序列化数据包
             *
             * 此数据包不包含额外数据，无需读取
             */
            void Read() override { }
        };

        /**
         * @class OpeningCinematic
         * @brief 请求播放开场动画数据包
         *
         * 继承自 ClientPacket，由客户端发送，请求播放种族开场动画。
         * 玩家可以在角色选择界面或游戏中重新观看开场动画。
         *
         * 调用时机：
         * - 新角色首次登录
         * - 玩家手动请求播放开场动画时
         *
         * 服务器处理：
         * - 服务器验证玩家种族后，发送相应的 TriggerCinematic 数据包
         */
        class OpeningCinematic final : public ClientPacket
        {
        public:
            /**
             * @brief 构造函数
             * @param packet 接收到的原始网络数据包
             */
            OpeningCinematic(WorldPacket&& packet) : ClientPacket(CMSG_OPENING_CINEMATIC, std::move(packet)) { }

            /**
             * @brief 反序列化数据包
             *
             * 此数据包不包含额外数据，无需读取
             */
            void Read() override { }
        };

        /**
         * @class CrossedInebriationThreshold
         * @brief 醉酒状态阈值跨越通知数据包
         *
         * 继承自 ServerPacket，用于通知客户端玩家跨越了某个醉酒等级阈值。
         * 当玩家饮用酒精饮料时，会积累醉酒值，跨越特定阈值时触发此通知。
         *
         * 醉酒效果包括：
         * - 视觉模糊、摇摆
         * - 移动速度和准确度下降
         * - 无法驾驶坐骑
         * - 社交聊天变为"醉酒语言"
         *
         * 调用时机：
         * - 玩家饮用酒精饮料，醉酒等级提升跨越阈值时
         * - 醉酒值自然衰减，跨越阈值下降时
         *
         * 性能注意事项：
         * - 应仅在阈值变化时发送，避免频繁更新
         */
        class CrossedInebriationThreshold final : public ServerPacket
        {
        public:
            /**
             * @brief 默认构造函数
             *
             * 初始化数据包大小为 16 字节（GUID 8 字节 + 阈值 4 字节 + 物品 ID 4 字节）
             */
            CrossedInebriationThreshold() : ServerPacket(SMSG_CROSSED_INEBRIATION_THRESHOLD, 8 + 4 + 4) { }

            /**
             * @brief 序列化数据包
             * @return 序列化后的 WorldPacket 指针
             */
            WorldPacket const* Write() override;

            ObjectGuid Guid;          ///< 玩家的 GUID
            uint32 Threshold = 0;     ///< 新的醉酒等级阈值
            uint32 ItemID = 0;        ///< 导致醉酒的物品 ID（酒类物品）
        };

        /**
         * @class OverrideLight
         * @brief 光照覆盖数据包
         *
         * 继承自 ServerPacket，用于强制改变特定区域的光照效果。
         * 通常用于特殊场景，如任务场景、副本环境等需要特殊光照的情况。
         *
         * 调用时机：
         * - 玩家进入需要特殊光照的区域时
         * - 任务脚本强制改变区域光照时
         * - 特殊法术效果需要改变环境光照时
         *
         * 性能注意事项：
         * - 光照变化使用渐变过渡，避免视觉突变
         * - 过渡时间由 TransitionMilliseconds 控制
         */
        class OverrideLight final : public ServerPacket
        {
        public:
            /**
             * @brief 默认构造函数
             *
             * 初始化数据包大小为 12 字节（三个 int32）
             */
            OverrideLight() : ServerPacket(SMSG_OVERRIDE_LIGHT, 4 + 4 + 4) { }

            /**
             * @brief 序列化数据包
             * @return 序列化后的 WorldPacket 指针
             */
            WorldPacket const* Write() override;

            int32 AreaLightID = 0;              ///< 区域光照 ID（定义在 Light.dbc 中）
            int32 TransitionMilliseconds = 0;   ///< 过渡时间（毫秒），控制光照变化的平滑度
            int32 OverrideLightID = 0;          ///< 覆盖光照 ID（新的光照设置）
        };

        /**
         * @class RandomRollClient
         * @brief 随机掷骰客户端请求数据包
         *
         * 继承自 ClientPacket，由客户端发送，请求服务器进行随机掷骰。
         * 玩家可以通过 /roll 命令或 UI 按钮发起掷骰请求。
         *
         * 调用时机：
         * - 玩家使用 /roll 命令时
         * - 玩家点击掷骰 UI 按钮时
         *
         * 服务器处理：
         * - 服务器验证范围有效性后，生成随机数
         * - 通过 RandomRoll 数据包将结果广播给附近玩家
         */
        class RandomRollClient final : public ClientPacket
        {
        public:
            /**
             * @brief 构造函数
             * @param packet 接收到的原始网络数据包
             */
            RandomRollClient(WorldPacket&& packet) : ClientPacket(MSG_RANDOM_ROLL, std::move(packet)) { }

            /**
             * @brief 反序列化数据包
             *
             * 从数据包中读取掷骰的最小值和最大值
             */
            void Read() override;

            uint32 Min = 0;  ///< 掷骰最小值
            uint32 Max = 0;  ///< 掷骰最大值
        };

        /**
         * @class RandomRoll
         * @brief 随机掷骰结果广播数据包
         *
         * 继承自 ServerPacket，用于向客户端广播掷骰结果。
         * 结果会发送给掷骰者及其附近的所有玩家。
         *
         * 调用时机：
         * - 服务器处理完 RandomRollClient 请求后，生成随机结果
         * - 将结果广播给附近玩家
         *
         * 性能注意事项：
         * - 仅广播给一定范围内的玩家，避免全服广播
         */
        class RandomRoll final : public ServerPacket
        {
        public:
            /**
             * @brief 默认构造函数
             *
             * 初始化数据包大小为 20 字节（Min 4 + Max 4 + Result 4 + GUID 8）
             */
            RandomRoll() : ServerPacket(MSG_RANDOM_ROLL, 4 + 4 + 4 + 8) { }

            /**
             * @brief 序列化数据包
             * @return 序列化后的 WorldPacket 指针
             */
            WorldPacket const* Write() override;

            uint32 Min = 0;         ///< 掷骰最小值
            uint32 Max = 0;         ///< 掷骰最大值
            uint32 Result = 0;      ///< 掷骰结果（Min <= Result <= Max）
            ObjectGuid Roller;      ///< 掷骰者的 GUID
        };

        /**
         * @class TogglePvP
         * @brief 切换 PvP 状态客户端请求数据包
         *
         * 继承自 ClientPacket，由客户端发送，请求切换玩家的 PvP 状态。
         * 玩家可以手动开启或关闭 PvP 模式（在某些规则下）。
         *
         * 调用时机：
         * - 玩家点击 PvP 按钮时
         * - 玩家使用 /pvp 命令时
         *
         * 服务器处理：
         * - 服务器验证玩家是否可以切换 PvP（受服务器规则、区域限制等）
         * - 更新玩家 PvP 状态，并广播给周围玩家
         *
         * 数据包格式：
         * - 可选的布尔值：数据包大小为 1 字节时包含，否则为空（仅切换）
         */
        class TogglePvP final : public ClientPacket
        {
        public:
            /**
             * @brief 构造函数
             * @param packet 接收到的原始网络数据包
             */
            TogglePvP(WorldPacket&& packet) : ClientPacket(CMSG_TOGGLE_PVP, std::move(packet)) { }

            /**
             * @brief 反序列化数据包
             *
             * 如果数据包包含状态值，则读取；否则为空切换请求
             */
            void Read() override;

            /**
             * @brief 检查数据包是否包含 PvP 状态值
             * @return true 如果数据包包含状态值，false 否则
             */
            bool HasPvPStatus() const { return GetSize() == 1; }

            Optional<bool> Enable;  ///< 可选的 PvP 启用状态（true = 开启，false = 关闭，空 = 切换）
        };

        /**
         * @class UITime
         * @brief UI 时间更新数据包
         *
         * 继承自 ServerPacket，用于同步服务器时间给客户端 UI。
         * 客户端使用此时间显示任务剩余时间、活动倒计时等 UI 元素。
         *
         * 调用时机：
         * - 玩家登录时发送初始时间
         * - 定期发送以保持时间同步
         *
         * 性能注意事项：
         * - 通常每分钟同步一次即可，避免频繁发送
         */
        class UITime final : public ServerPacket
        {
        public:
            /**
             * @brief 默认构造函数
             *
             * 初始化数据包大小为 4 字节（时间戳）
             */
            UITime() : ServerPacket(SMSG_WORLD_STATE_UI_TIMER_UPDATE, 4) { }

            /**
             * @brief 序列化数据包
             * @return 序列化后的 WorldPacket 指针
             */
            WorldPacket const* Write() override;

            uint32 Time = 0;  ///< 服务器时间戳（Unix 时间）
        };

        /**
         * @class WorldTeleport
         * @brief 世界传送客户端请求数据包
         *
         * 继承自 ClientPacket，由客户端发送，请求传送到指定位置。
         * 这是一个 GM 命令或开发工具使用的功能，普通玩家无法使用。
         *
         * 调用时机：
         * - GM 使用传送命令时
         * - 开发调试工具请求传送时
         *
         * 服务器处理：
         * - 服务器验证权限后，执行传送操作
         * - 可能触发地图切换和位置验证
         */
        class WorldTeleport final : public ClientPacket
        {
        public:
            /**
             * @brief 构造函数
             * @param packet 接收到的原始网络数据包
             */
            WorldTeleport(WorldPacket&& packet) : ClientPacket(CMSG_WORLD_TELEPORT, std::move(packet)) { }

            /**
             * @brief 反序列化数据包
             *
             * 从数据包中读取时间、地图 ID、位置和朝向
             */
            void Read() override;

            uint32 Time = 0;                      ///< 时间参数（用途不明，可能与传送验证相关）
            uint32 MapID = 0;                     ///< 目标地图 ID
            TaggedPosition<Position::XYZ> Pos;    ///< 目标坐标（X、Y、Z）
            float Facing = 0.0f;                  ///< 目标朝向（弧度）
        };

        /**
         * @class CorpseReclaimDelay
         * @brief 尸体回收延迟数据包
         *
         * 继承自 ServerPacket，用于通知客户端回收尸体的剩余等待时间。
         * 玩家死亡后需要等待一定时间才能回收尸体（复活）。
         *
         * 调用时机：
         * - 玩家死亡时，发送初始延迟时间
         * - 延迟时间更新时（例如受到某些法术影响）
         *
         * 性能注意事项：
         * - 通常只在死亡时发送一次，延迟时间由客户端倒计时
         */
        class CorpseReclaimDelay : public ServerPacket
        {
        public:
            /**
             * @brief 默认构造函数
             *
             * 初始化数据包大小为 4 字节（延迟时间）
             */
            CorpseReclaimDelay() : ServerPacket(SMSG_CORPSE_RECLAIM_DELAY, 4) { }

            /**
             * @brief 序列化数据包
             * @return 序列化后的 WorldPacket 指针
             */
            WorldPacket const* Write() override;

            uint32 Remaining = 0;  ///< 剩余等待时间（毫秒）
        };

        /**
         * @class DeathReleaseLoc
         * @brief 死亡释放位置数据包
         *
         * 继承自 ServerPacket，用于通知客户端玩家的尸体位置或墓地位置。
         * 玩家死亡后，客户端会显示"释放灵魂"选项，释放后灵魂出现在尸体附近或墓地。
         *
         * 调用时机：
         * - 玩家死亡时，发送尸体位置
         * - 玩家选择"释放灵魂"时，发送墓地位置
         */
        class DeathReleaseLoc : public ServerPacket
        {
        public:
            /**
             * @brief 默认构造函数
             *
             * 初始化数据包大小为 16 字节（地图 ID 4 字节 + 坐标 12 字节）
             */
            DeathReleaseLoc() : ServerPacket(SMSG_DEATH_RELEASE_LOC, 4 + (3 * 4)) { }

            /**
             * @brief 序列化数据包
             * @return 序列化后的 WorldPacket 指针
             */
            WorldPacket const* Write() override;

            int32 MapID = 0;                    ///< 地图 ID
            TaggedPosition<Position::XYZ> Loc;  ///< 位置坐标（尸体或墓地）
        };

        /**
         * @class PreRessurect
         * @brief 复活前通知数据包
         *
         * 继承自 ServerPacket，用于通知客户端玩家即将复活。
         * 这个数据包在正式复活前发送，用于客户端准备复活动画和效果。
         *
         * 注意：类名拼写错误，应为 PreResurrect（缺少 'c'），但为了保持兼容性未修正。
         *
         * 调用时机：
         * - 玩家接受复活请求时，在发送实际复活数据之前
         * - 使用灵魂医者复活时
         *
         * 性能注意事项：
         * - 紧凑打包的 GUID，减少数据包大小
         */
        class PreRessurect : public ServerPacket
        {
        public:
            /**
             * @brief 默认构造函数
             *
             * 初始化数据包大小为 8 字节（GUID 大小）
             */
            PreRessurect() : ServerPacket(SMSG_PRE_RESURRECT, 8) { }

            /**
             * @brief 序列化数据包
             * @return 序列化后的 WorldPacket 指针
             *
             * 注意：使用 WriteAsPacked() 压缩 GUID 以减少数据包大小
             */
            WorldPacket const* Write() override;

            ObjectGuid PlayerGUID;  ///< 即将复活的玩家 GUID
        };

        /**
         * @class ReclaimCorpse
         * @brief 回收尸体客户端请求数据包
         *
         * 继承自 ClientPacket，由客户端发送，请求玩家回收尸体并复活。
         * 玩家靠近自己的尸体时，可以选择回收尸体复活。
         *
         * 调用时机：
         * - 玩家灵魂状态下接近尸体，点击"复活"按钮时
         *
         * 服务器处理：
         * - 验证玩家是否在尸体附近
         * - 检查回收延迟是否已过
         * - 执行复活操作，恢复生命值和法力值
         */
        class ReclaimCorpse final : public ClientPacket
        {
        public:
            /**
             * @brief 构造函数
             * @param packet 接收到的原始网络数据包
             */
            ReclaimCorpse(WorldPacket&& packet) : ClientPacket(CMSG_RECLAIM_CORPSE, std::move(packet)) { }

            /**
             * @brief 反序列化数据包
             *
             * 从数据包中读取尸体 GUID
             */
            void Read() override;

            ObjectGuid CorpseGUID;  ///< 尸体的 GUID
        };

        /**
         * @class RepopRequest
         * @brief 请求传送到墓地客户端请求数据包
         *
         * 继承自 ClientPacket，由客户端发送，请求将灵魂传送到最近的墓地。
         * 玩家死亡后，如果不想跑尸，可以选择"释放灵魂到墓地"。
         *
         * 调用时机：
         * - 玩家死亡后，点击"释放灵魂"按钮时
         * - 玩家灵魂状态下，请求传送到墓地时
         *
         * 服务器处理：
         * - 验证玩家处于死亡或灵魂状态
         * - 查找最近的墓地
         * - 将玩家灵魂传送到墓地
         *
         * 注意：
         * - CheckInstance 标志用于处理实例中的特殊情况
         */
        class RepopRequest final : public ClientPacket
        {
        public:
            /**
             * @brief 构造函数
             * @param packet 接收到的原始网络数据包
             */
            RepopRequest(WorldPacket&& packet) : ClientPacket(CMSG_REPOP_REQUEST, std::move(packet)) { }

            /**
             * @brief 反序列化数据包
             *
             * 从数据包中读取实例检查标志
             */
            void Read() override;

            bool CheckInstance = false;  ///< 是否检查实例限制（true = 需要检查，false = 不检查）
        };

        /**
         * @class ResurrectResponse
         * @brief 复活响应客户端请求数据包
         *
         * 继承自 ClientPacket，由客户端发送，响应其他玩家的复活请求。
         * 当其他玩家（如圣骑士、萨满、术士）发起复活请求时，客户端显示复活对话框，
         * 玩家可以选择接受或拒绝。
         *
         * 调用时机：
         * - 玩家点击复活对话框的"接受"或"拒绝"按钮时
         *
         * 服务器处理：
         * - 如果接受（Response = 1）：执行复活操作
         * - 如果拒绝（Response = 0）：通知复活者请求被拒绝
         */
        class ResurrectResponse final : public ClientPacket
        {
        public:
            /**
             * @brief 构造函数
             * @param packet 接收到的原始网络数据包
             */
            ResurrectResponse(WorldPacket&& packet) : ClientPacket(CMSG_RESURRECT_RESPONSE, std::move(packet)) { }

            /**
             * @brief 反序列化数据包
             *
             * 从数据包中读取复活者 GUID 和响应值
             */
            void Read() override;

            ObjectGuid Resurrecter;  ///< 发起复活的玩家 GUID
            uint8 Response = 0;      ///< 响应值（0 = 拒绝，1 = 接受）
        };
    }
}

#endif // MiscPackets_h__
