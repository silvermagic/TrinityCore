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
 * @file ObjectGuid.h
 * @brief 游戏对象唯一标识符（GUID）系统
 *
 * 本模块实现了游戏中所有对象的唯一标识符系统。GUID（Globally Unique Identifier）
 * 是一个64位的唯一标识符，用于标识游戏中的各种对象（玩家、生物、物品、游戏对象等）。
 *
 * GUID结构：
 * - 高16位：对象类型（HighGuid），标识对象类型（如玩家、生物、物品等）
 * - 中间24位：实体ID（Entry），对于某些对象类型存储其模板ID
 * - 低24位或32位：计数器（Counter），唯一序列号
 *
 * 主要功能：
 * - 提供类型安全的GUID创建和管理
 * - 支持GUID的打包和解包，优化网络传输
 * - 提供GUID生成器，自动生成唯一ID
 * - 支持多种对象类型的GUID区分
 */

#ifndef ObjectGuid_h__
#define ObjectGuid_h__

#include "Define.h"
#include <array>
#include <functional>
#include <list>
#include <memory>
#include <set>
#include <string>
#include <type_traits>
#include <unordered_set>
#include <vector>

/**
 * @enum TypeID
 * @brief 对象类型ID枚举
 *
 * 定义游戏中所有对象的基本类型ID，客户端和服务器使用相同的类型ID进行通信。
 * 每个对象实例都有一个固定的类型ID，用于快速类型判断。
 */
enum TypeID
{
    TYPEID_OBJECT        = 0,  ///< 基础对象类型（所有对象的基类）
    TYPEID_ITEM          = 1,  ///< 物品类型
    TYPEID_CONTAINER     = 2,  ///< 容器类型（背包、银行等）
    TYPEID_UNIT          = 3,  ///< 单位类型（生物、玩家、宠物等）
    TYPEID_PLAYER        = 4,  ///< 玩家类型
    TYPEID_GAMEOBJECT    = 5,  ///< 游戏对象类型（矿点、草药、陷阱等）
    TYPEID_DYNAMICOBJECT = 6,  ///< 动态对象类型（法术效果区域等）
    TYPEID_CORPSE        = 7   ///< 尸体类型
};

#define NUM_CLIENT_OBJECT_TYPES             8  ///< 客户端识别的对象类型数量

/**
 * @enum TypeMask
 * @brief 对象类型掩码枚举
 *
 * 定义对象类型的位掩码，用于快速的类型组合判断。
 * 可以通过位运算同时检查多个类型，提高类型检查效率。
 */
enum TypeMask
{
    TYPEMASK_OBJECT         = 0x0001,  ///< 所有对象都具有此标志
    TYPEMASK_ITEM           = 0x0002,  ///< 物品类型掩码
    TYPEMASK_CONTAINER      = 0x0004,  ///< 容器类型掩码（TYPEMASK_ITEM | 0x0004）
    TYPEMASK_UNIT           = 0x0008,  ///< 单位类型掩码（生物、玩家、宠物等）
    TYPEMASK_PLAYER         = 0x0010,  ///< 玩家类型掩码
    TYPEMASK_GAMEOBJECT     = 0x0020,  ///< 游戏对象类型掩码
    TYPEMASK_DYNAMICOBJECT  = 0x0040,  ///< 动态对象类型掩码
    TYPEMASK_CORPSE         = 0x0080,  ///< 尸体类型掩码

    TYPEMASK_SEER           = TYPEMASK_UNIT | TYPEMASK_DYNAMICOBJECT,  ///< 能够"看见"的对象类型
    TYPEMASK_WORLDOBJECT    = TYPEMASK_UNIT | TYPEMASK_GAMEOBJECT | TYPEMASK_DYNAMICOBJECT | TYPEMASK_CORPSE  ///< 世界对象类型（有位置的对象）
};

/**
 * @enum class HighGuid
 * @brief GUID高位类型枚举
 *
 * 定义GUID的高16位类型标识，用于区分不同类型的游戏对象。
 * 这些值与客户端协议保持一致，确保客户端和服务器对GUID的理解相同。
 *
 * GUID高位类型决定了GUID的结构：
 * - 全局类型（Global）：只有计数器，没有Entry（如玩家、物品）
 * - 地图特定类型（MapSpecific）：包含Entry和计数器（如生物、游戏对象）
 */
enum class HighGuid
{
    Item           = 0x4000,  ///< 物品（blizz 4000）
    Container      = 0x4000,  ///< 容器（blizz 4000，与Item相同）
    Player         = 0x0000,  ///< 玩家（blizz 0000）
    GameObject     = 0xF110,  ///< 游戏对象（blizz F110）
    Transport      = 0xF120,  ///< 运输工具（blizz F120，用于GAMEOBJECT_TYPE_TRANSPORT）
    Unit           = 0xF130,  ///< 生物（blizz F130）
    Pet            = 0xF140,  ///< 宠物（blizz F140）
    Vehicle        = 0xF150,  ///< 载具（blizz F550）
    DynamicObject  = 0xF100,  ///< 动态对象（blizz F100）
    Corpse         = 0xF101,  ///< 尸体（blizz F100）
    Mo_Transport   = 0x1FC0,  ///< 移动运输工具（blizz 1FC0，用于GAMEOBJECT_TYPE_MO_TRANSPORT）
    Instance       = 0x1F40,  ///< 副本ID（blizz 1F40）
    Group          = 0x1F50,  ///< 队伍ID
};

/**
 * @struct ObjectGuidTraits
 * @brief GUID类型特性模板
 *
 * 用于在编译时确定GUID类型的特性（全局或地图特定）。
 * 通过模板特化实现类型安全的GUID创建。
 *
 * @tparam high GUID的高位类型
 */
template<HighGuid high>
struct ObjectGuidTraits
{
    static bool const Global = false;        ///< 是否为全局GUID类型
    static bool const MapSpecific = false;   ///< 是否为地图特定GUID类型
};

/**
 * @brief 定义全局GUID类型的特性宏
 * @param highguid GUID高位类型
 *
 * 全局GUID类型只有计数器，没有Entry字段。
 * 例如：玩家、物品、队伍等。
 */
#define GUID_TRAIT_GLOBAL(highguid) \
    template<> struct ObjectGuidTraits<highguid> \
    { \
        static bool const Global = true; \
        static bool const MapSpecific = false; \
    };

/**
 * @brief 定义地图特定GUID类型的特性宏
 * @param highguid GUID高位类型
 *
 * 地图特定GUID类型包含Entry和计数器两个字段。
 * 例如：生物、游戏对象、宠物等。
 */
#define GUID_TRAIT_MAP_SPECIFIC(highguid) \
    template<> struct ObjectGuidTraits<highguid> \
    { \
        static bool const Global = false; \
        static bool const MapSpecific = true; \
    };

// 全局GUID类型定义
GUID_TRAIT_GLOBAL(HighGuid::Player)        ///< 玩家GUID是全局的
GUID_TRAIT_GLOBAL(HighGuid::Item)          ///< 物品GUID是全局的
GUID_TRAIT_GLOBAL(HighGuid::Mo_Transport)  ///< 移动运输工具GUID是全局的
GUID_TRAIT_GLOBAL(HighGuid::Group)         ///< 队伍GUID是全局的
GUID_TRAIT_GLOBAL(HighGuid::Instance)      ///< 副本GUID是全局的

// 地图特定GUID类型定义
GUID_TRAIT_MAP_SPECIFIC(HighGuid::Transport)       ///< 运输工具GUID是地图特定的
GUID_TRAIT_MAP_SPECIFIC(HighGuid::Unit)            ///< 生物GUID是地图特定的
GUID_TRAIT_MAP_SPECIFIC(HighGuid::Vehicle)         ///< 载具GUID是地图特定的
GUID_TRAIT_MAP_SPECIFIC(HighGuid::Pet)             ///< 宠物GUID是地图特定的
GUID_TRAIT_MAP_SPECIFIC(HighGuid::GameObject)      ///< 游戏对象GUID是地图特定的
GUID_TRAIT_MAP_SPECIFIC(HighGuid::DynamicObject)   ///< 动态对象GUID是地图特定的
GUID_TRAIT_MAP_SPECIFIC(HighGuid::Corpse)          ///< 尸体GUID是地图特定的

class ByteBuffer;
class ObjectGuid;
class PackedGuid;

/**
 * @struct PackedGuidReader
 * @brief 打包GUID读取辅助结构
 *
 * 用于从网络包中读取打包格式的GUID。
 * 打包格式通过省略零字节来减少网络传输数据量。
 */
struct PackedGuidReader
{
    /**
     * @brief 构造函数
     * @param guid 要读取到的GUID引用
     */
    explicit PackedGuidReader(ObjectGuid& guid) : Guid(guid) { }
    ObjectGuid& Guid;  ///< 目标GUID引用
};

/**
 * @struct PackedGuidWriter
 * @brief 打包GUID写入辅助结构
 *
 * 用于将GUID以打包格式写入网络包。
 * 打包格式通过省略零字节来减少网络传输数据量。
 */
struct PackedGuidWriter
{
    /**
     * @brief 构造函数
     * @param guid 要写入的GUID常量引用
     */
    explicit PackedGuidWriter(ObjectGuid const& guid) : Guid(guid) { }
    ObjectGuid const& Guid;  ///< 源GUID常量引用
};

/**
 * @class ObjectGuid
 * @brief 游戏对象全局唯一标识符类
 *
 * ObjectGuid 是游戏中所有对象的唯一标识符，使用64位整数存储。
 * GUID由三个部分组成：
 * - 高16位（HighGuid）：对象类型（如玩家、生物、物品等）
 * - 中间24位（Entry）：对于地图特定对象，存储其模板ID；对于全局对象不使用
 * - 低24位或32位（Counter）：唯一序列号
 *
 * GUID的设计特点：
 * - 类型安全：通过模板系统确保正确创建不同类型的GUID
 * - 网络优化：支持打包传输，减少数据量
 * - 快速判断：提供多种类型判断方法
 * - 唯一性保证：通过生成器确保全局唯一
 *
 * 使用示例：
 * @code
 * // 创建玩家GUID
 * ObjectGuid playerGuid = ObjectGuid::Create<HighGuid::Player>(counter);
 *
 * // 创建生物GUID
 * ObjectGuid creatureGuid = ObjectGuid::Create<HighGuid::Unit>(entry, counter);
 *
 * // 类型判断
 * if (guid.IsPlayer()) { ... }
 * @endcode
 */
class TC_GAME_API ObjectGuid
{
    public:
        static ObjectGuid const Empty;  ///< 空GUID常量，表示无效GUID

        typedef uint32 LowType;  ///< GUID计数器类型定义

        /**
         * @brief 创建全局类型GUID的模板函数
         * @tparam type GUID高位类型
         * @param counter 计数器值
         * @return 创建的ObjectGuid对象
         *
         * 此函数仅对全局类型GUID可用（如玩家、物品等）。
         * 通过SFINAE技术确保类型安全。
         */
        template<HighGuid type>
        static typename std::enable_if<ObjectGuidTraits<type>::Global, ObjectGuid>::type Create(LowType counter) { return Global(type, counter); }

        /**
         * @brief 创建地图特定类型GUID的模板函数
         * @tparam type GUID高位类型
         * @param entry 实体模板ID
         * @param counter 计数器值
         * @return 创建的ObjectGuid对象
         *
         * 此函数仅对地图特定类型GUID可用（如生物、游戏对象等）。
         * 通过SFINAE技术确保类型安全。
         */
        template<HighGuid type>
        static typename std::enable_if<ObjectGuidTraits<type>::MapSpecific, ObjectGuid>::type Create(uint32 entry, LowType counter) { return MapSpecific(type, entry, counter); }

        /**
         * @brief 默认构造函数，创建空GUID
         */
        ObjectGuid() : _guid(0) { }

        /**
         * @brief 从原始值构造GUID
         * @param guid 64位原始GUID值
         */
        explicit ObjectGuid(uint64 guid) : _guid(guid) { }

        /**
         * @brief 从高位类型、实体ID和计数器构造GUID（地图特定类型）
         * @param hi 高位类型
         * @param entry 实体ID
         * @param counter 计数器
         *
         * GUID结构：[高位类型(16位)][实体ID(24位)][计数器(24位)]
         */
        ObjectGuid(HighGuid hi, uint32 entry, LowType counter) : _guid(counter ? uint64(counter) | (uint64(entry) << 24) | (uint64(hi) << 48) : 0) { }

        /**
         * @brief 从高位类型和计数器构造GUID（全局类型）
         * @param hi 高位类型
         * @param counter 计数器
         *
         * GUID结构：[高位类型(16位)][计数器(32位)]
         */
        ObjectGuid(HighGuid hi, LowType counter) : _guid(counter ? uint64(counter) | (uint64(hi) << 48) : 0) { }

        /**
         * @brief 转换为uint64操作符
         * @return GUID的原始64位值
         */
        operator uint64() const { return _guid; }

        /**
         * @brief 以打包格式读取GUID
         * @return PackedGuidReader辅助对象
         *
         * 用于从网络包中读取打包格式的GUID。
         */
        PackedGuidReader ReadAsPacked() { return PackedGuidReader(*this); }

        /**
         * @brief 设置GUID值
         * @param guid 新的GUID值
         */
        void Set(uint64 guid) { _guid = guid; }

        /**
         * @brief 清空GUID（设置为0）
         */
        void Clear() { _guid = 0; }

        /**
         * @brief 以打包格式写入GUID
         * @return PackedGuidWriter辅助对象
         *
         * 用于将GUID以打包格式写入网络包。
         */
        PackedGuidWriter WriteAsPacked() const { return PackedGuidWriter(*this); }

        /**
         * @brief 获取GUID的原始64位值
         * @return GUID的原始值
         */
        uint64   GetRawValue() const { return _guid; }

        /**
         * @brief 获取GUID的高位类型
         * @return 高位类型枚举值
         */
        HighGuid GetHigh() const { return HighGuid((_guid >> 48) & 0x0000FFFF); }

        /**
         * @brief 获取实体ID
         * @return 实体ID，如果该GUID类型不包含实体ID则返回0
         */
        uint32   GetEntry() const { return HasEntry() ? uint32((_guid >> 24) & UI64LIT(0x0000000000FFFFFF)) : 0; }

        /**
         * @brief 获取计数器值
         * @return 计数器值
         *
         * 根据GUID类型，计数器可能占用24位或32位。
         */
        LowType  GetCounter()  const
        {
            return HasEntry()
                   ? LowType(_guid & UI64LIT(0x0000000000FFFFFF))
                   : LowType(_guid & UI64LIT(0x00000000FFFFFFFF));
        }

        /**
         * @brief 获取指定类型GUID的最大计数器值
         * @param high GUID高位类型
         * @return 最大计数器值
         *
         * 地图特定类型：0x00FFFFFF (24位)
         * 全局类型：0xFFFFFFFF (32位)
         */
        static LowType GetMaxCounter(HighGuid high)
        {
            return HasEntry(high)
                   ? LowType(0x00FFFFFF)
                   : LowType(0xFFFFFFFF);
        }

        /**
         * @brief 获取当前GUID的最大计数器值
         * @return 最大计数器值
         */
        ObjectGuid::LowType GetMaxCounter() const { return GetMaxCounter(GetHigh()); }

        // ============================================================================
        // 类型判断方法
        // ============================================================================

        bool IsEmpty()             const { return _guid == 0; }
        bool IsCreature()          const { return GetHigh() == HighGuid::Unit; }
        bool IsPet()               const { return GetHigh() == HighGuid::Pet; }
        bool IsVehicle()           const { return GetHigh() == HighGuid::Vehicle; }
        bool IsCreatureOrPet()     const { return IsCreature() || IsPet(); }
        bool IsCreatureOrVehicle() const { return IsCreature() || IsVehicle(); }
        bool IsAnyTypeCreature()   const { return IsCreature() || IsPet() || IsVehicle(); }
        bool IsPlayer()            const { return !IsEmpty() && GetHigh() == HighGuid::Player; }
        bool IsUnit()              const { return IsAnyTypeCreature() || IsPlayer(); }
        bool IsItem()              const { return GetHigh() == HighGuid::Item; }
        bool IsGameObject()        const { return GetHigh() == HighGuid::GameObject; }
        bool IsDynamicObject()     const { return GetHigh() == HighGuid::DynamicObject; }
        bool IsCorpse()            const { return GetHigh() == HighGuid::Corpse; }
        bool IsTransport()         const { return GetHigh() == HighGuid::Transport; }
        bool IsMOTransport()       const { return GetHigh() == HighGuid::Mo_Transport; }
        bool IsAnyTypeGameObject() const { return IsGameObject() || IsTransport() || IsMOTransport(); }
        bool IsInstance()          const { return GetHigh() == HighGuid::Instance; }
        bool IsGroup()             const { return GetHigh() == HighGuid::Group; }

        /**
         * @brief 获取GUID对应的类型ID
         * @param high GUID高位类型
         * @return 类型ID枚举值
         *
         * 将HighGuid转换为对应的TypeID。
         */
        static TypeID GetTypeId(HighGuid high)
        {
            switch (high)
            {
                case HighGuid::Item:         return TYPEID_ITEM;
                //case HighGuid::Container:    return TYPEID_CONTAINER; HighGuid::Container == HighGuid::Item currently
                case HighGuid::Unit:         return TYPEID_UNIT;
                case HighGuid::Pet:          return TYPEID_UNIT;
                case HighGuid::Player:       return TYPEID_PLAYER;
                case HighGuid::GameObject:   return TYPEID_GAMEOBJECT;
                case HighGuid::DynamicObject: return TYPEID_DYNAMICOBJECT;
                case HighGuid::Corpse:       return TYPEID_CORPSE;
                case HighGuid::Mo_Transport: return TYPEID_GAMEOBJECT;
                case HighGuid::Vehicle:      return TYPEID_UNIT;
                // unknown
                case HighGuid::Instance:
                case HighGuid::Group:
                default:                    return TYPEID_OBJECT;
            }
        }

        /**
         * @brief 获取当前GUID的类型ID
         * @return 类型ID枚举值
         */
        TypeID GetTypeId() const { return GetTypeId(GetHigh()); }

        // 比较操作符
        bool operator!() const { return IsEmpty(); }
        bool operator==(ObjectGuid const& right) const = default;
        std::strong_ordering operator<=>(ObjectGuid const& right) const = default;

        /**
         * @brief 获取类型名称字符串
         * @param high GUID高位类型
         * @return 类型名称的C字符串
         */
        static char const* GetTypeName(HighGuid high);

        /**
         * @brief 获取当前GUID的类型名称
         * @return 类型名称的C字符串
         */
        char const* GetTypeName() const { return !IsEmpty() ? GetTypeName(GetHigh()) : "None"; }

        /**
         * @brief 将GUID转换为字符串表示
         * @return GUID的字符串表示
         *
         * 格式："GUID Full: 0xXXXXXXXXXXXXXXXX Type: XXX Entry: XXX Low: XXX"
         */
        std::string ToString() const;

    private:
        /**
         * @brief 判断指定类型是否包含实体ID
         * @param high GUID高位类型
         * @return 如果包含实体ID返回true
         */
        static bool HasEntry(HighGuid high)
        {
            switch (high)
            {
                case HighGuid::Item:
                case HighGuid::Player:
                case HighGuid::DynamicObject:
                case HighGuid::Corpse:
                case HighGuid::Mo_Transport:
                case HighGuid::Instance:
                case HighGuid::Group:
                    return false;
                case HighGuid::GameObject:
                case HighGuid::Transport:
                case HighGuid::Unit:
                case HighGuid::Pet:
                case HighGuid::Vehicle:
                default:
                    return true;
            }
        }

        /**
         * @brief 判断当前GUID是否包含实体ID
         * @return 如果包含实体ID返回true
         */
        bool HasEntry() const { return HasEntry(GetHigh()); }

        /**
         * @brief 创建全局类型GUID
         * @param type GUID高位类型
         * @param counter 计数器
         * @return 创建的ObjectGuid对象
         */
        static ObjectGuid Global(HighGuid type, LowType counter);

        /**
         * @brief 创建地图特定类型GUID
         * @param type GUID高位类型
         * @param entry 实体ID
         * @param counter 计数器
         * @return 创建的ObjectGuid对象
         */
        static ObjectGuid MapSpecific(HighGuid type, uint32 entry, LowType counter);

        // 禁止错误的类型转换构造
        explicit ObjectGuid(uint32 const&) = delete;                 // no implementation, used to catch wrong type assignment
        ObjectGuid(HighGuid, uint32, uint64 counter) = delete;       // no implementation, used to catch wrong type assignment
        ObjectGuid(HighGuid, uint64 counter) = delete;               // no implementation, used to catch wrong type assignment

        uint64 _guid;  ///< GUID的64位存储值
};

// ============================================================================
// GUID容器类型定义
// ============================================================================

using GuidSet = std::set<ObjectGuid>;                          ///< GUID有序集合
using GuidList = std::list<ObjectGuid>;                        ///< GUID链表
using GuidVector = std::vector<ObjectGuid>;                    ///< GUID向量
using GuidUnorderedSet = std::unordered_set<ObjectGuid>;       ///< GUID无序集合

// minimum buffer size for packed guid is 9 bytes
#define PACKED_GUID_MIN_BUFFER_SIZE 9  ///< 打包GUID的最小缓冲区大小（9字节）

/**
 * @class PackedGuid
 * @brief 打包GUID类，用于网络传输优化
 *
 * PackedGuid 通过省略GUID中的零字节来减少网络传输数据量。
 * 打包格式：第一个字节是掩码，指示哪些字节非零，后面跟着非零字节。
 *
 * 打包算法：
 * 1. 扫描GUID的8个字节
 * 2. 第一个字节（掩码）记录哪些字节非零
 * 3. 后续只存储非零字节
 *
 * 例如：GUID 0x00000000F1300001 打包后为 0x03 0xF1 0x30 0x01（仅4字节）
 */
class TC_GAME_API PackedGuid
{
    friend TC_GAME_API ByteBuffer& operator<<(ByteBuffer& buf, PackedGuid const& guid);

    public:
        /**
         * @brief 默认构造函数
         */
        explicit PackedGuid() : _packedSize(1), _packedGuid() { }

        /**
         * @brief 从ObjectGuid构造打包GUID
         * @param guid 要打包的GUID
         */
        explicit PackedGuid(ObjectGuid guid) { Set(guid); }

        /**
         * @brief 设置要打包的GUID
         * @param guid 要打包的GUID
         */
        void Set(ObjectGuid guid);

        /**
         * @brief 获取打包后的数据大小
         * @return 打包后的字节数
         */
        std::size_t size() const { return _packedSize; }

    private:
        uint8 _packedSize;  ///< 打包后的数据大小
        std::array<uint8, PACKED_GUID_MIN_BUFFER_SIZE> _packedGuid;  ///< 打包后的数据缓冲区
};

/**
 * @class ObjectGuidGenerator
 * @brief GUID生成器类
 *
 * 负责为特定类型的对象生成唯一的GUID。
 * 每种对象类型都有自己独立的GUID生成器，确保同类型对象的GUID唯一。
 *
 * 工作原理：
 * - 维护一个递增的计数器，每次生成GUID时递增
 * - 监控计数器是否接近上限，触发警告
 * - 处理计数器溢出情况（通常会导致服务器关闭）
 *
 * 使用示例：
 * @code
 * ObjectGuidGenerator creatureGuidGen(HighGuid::Unit);
 * ObjectGuid::LowType counter = creatureGuidGen.Generate();
 * ObjectGuid guid = ObjectGuid::Create<HighGuid::Unit>(entry, counter);
 * @endcode
 */
class TC_GAME_API ObjectGuidGenerator
{
public:
    /**
     * @brief 构造函数
     * @param high GUID高位类型
     * @param start 起始计数器值（默认为1）
     */
    explicit ObjectGuidGenerator(HighGuid high, ObjectGuid::LowType start = 1) : _high(high), _nextGuid(start) { }

    /**
     * @brief 析构函数
     */
    ~ObjectGuidGenerator() = default;

    /**
     * @brief 设置下一个计数器值
     * @param val 新的计数器值
     *
     * 用于从数据库加载时恢复GUID计数器状态。
     */
    void Set(ObjectGuid::LowType val) { _nextGuid = val; }

    /**
     * @brief 生成新的GUID计数器值
     * @return 新的计数器值
     *
     * 生成唯一计数器，并检查是否接近上限。
     * 如果计数器溢出，将关闭服务器。
     */
    ObjectGuid::LowType Generate();

    /**
     * @brief 获取下一个可用的计数器值
     * @return 下一个可用的计数器值
     */
    ObjectGuid::LowType GetNextAfterMaxUsed() const { return _nextGuid; }

protected:
    /**
     * @brief 处理计数器溢出
     *
     * 当计数器达到最大值时调用，记录错误并关闭服务器。
     */
    void HandleCounterOverflow();

    /**
     * @brief 检查GUID使用情况并触发警告
     *
     * 当GUID使用量达到警告阈值时触发系统警告。
     */
    void CheckGuidTrigger();

    HighGuid _high;              ///< GUID高位类型
    ObjectGuid::LowType _nextGuid;  ///< 下一个可用的计数器值
};

TC_GAME_API ByteBuffer& operator<<(ByteBuffer& buf, ObjectGuid const& guid);
TC_GAME_API ByteBuffer& operator>>(ByteBuffer& buf, ObjectGuid&       guid);

TC_GAME_API ByteBuffer& operator<<(ByteBuffer& buf, PackedGuid const& guid);
TC_GAME_API ByteBuffer& operator<<(ByteBuffer& buf, PackedGuidWriter const& guid);
TC_GAME_API ByteBuffer& operator>>(ByteBuffer& buf, PackedGuidReader const& guid);

namespace std
{
    template<>
    struct hash<ObjectGuid>
    {
        public:
            size_t operator()(ObjectGuid const& key) const
            {
                return std::hash<uint64>()(key.GetRawValue());
            }
    };
}

#endif // ObjectGuid_h__
