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
 * @file AuthHandler.cpp
 * @brief 认证响应处理模块
 *
 * 本模块负责处理客户端登录认证相关的消息发送,包括:
 * - 发送认证响应消息(成功/失败/排队)
 * - 发送客户端缓存版本信息
 *
 * 这些消息在玩家登录世界服务器时发送,用于完成登录流程
 */

#include "Opcodes.h"
#include "WorldSession.h"
#include "WorldPacket.h"

/**
 * @brief 发送认证响应消息给客户端
 * @param code 认证结果代码(如AUTH_OK, AUTH_FAILED等)
 * @param shortForm 是否使用短格式(不包含排队信息)
 * @param queuePos 排队位置(当服务器满员时使用)
 *
 * 调用时机: 玩家登录世界服务器时,在验证账号和角色后调用
 *
 * 消息包含:
 * - 认证结果代码
 * - 计费信息(剩余时间、计费计划标志、休息时间)
 * - 账号资料片等级(0=经典,1=TBC,2=WOTLK)
 * - 排队位置(如果服务器满员)
 * - 免费角色迁移标志
 *
 * 性能注意: 这是一个关键路径函数,在玩家登录时调用
 */
void WorldSession::SendAuthResponse(uint8 code, bool shortForm, uint32 queuePos)
{
    WorldPacket packet(SMSG_AUTH_RESPONSE, 1 + 4 + 1 + 4 + 1 + (4 + 1));
    packet << uint8(code);                                // 认证结果代码
    packet << uint32(0);                                  // BillingTimeRemaining - 计费剩余时间
    packet << uint8(0);                                   // BillingPlanFlags - 计费计划标志
    packet << uint32(0);                                  // BillingTimeRested - 休息时间
    packet << uint8(Expansion());                         // 资料片等级: 0=经典, 1=TBC, 2=WOTLK, 需要在数据库中为每个账号手动设置

    if (!shortForm)
    {
        // 长格式: 包含排队和迁移信息
        packet << uint32(queuePos);                       // Queue position - 排队位置
        packet << uint8(0);                               // Realm has a free character migration - bool - 是否有免费角色迁移
    }

    SendPacket(&packet);
}

/**
 * @brief 发送客户端缓存版本信息
 * @param version 缓存版本号
 *
 * 调用时机: 玩家登录成功后发送,用于客户端缓存同步
 *
 * 客户端会根据版本号判断是否需要更新本地缓存数据
 * 这可以减少客户端加载时间,避免重复下载不变的数据
 */
void WorldSession::SendClientCacheVersion(uint32 version)
{
    WorldPacket data(SMSG_CLIENTCACHE_VERSION, 4);
    data << uint32(version);
    SendPacket(&data);
}
