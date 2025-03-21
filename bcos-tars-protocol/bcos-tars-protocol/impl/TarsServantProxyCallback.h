/**
 *  Copyright (C) 2021 FISCO BCOS.
 *  SPDX-License-Identifier: Apache-2.0
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 *  Unless required by applicable law or agreed to in writing, software
 *  distributed under the License is distributed on an "AS IS" BASIS,
 *  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *  See the License for the specific language governing permissions and
 *  limitations under the License.
 *
 * @brief tars implementation for tars ServantProxyCallback
 * @file TarsServantProxyCallback.h
 * @author: octopuswang
 * @date 2022-07-24
 */
#pragma once

#include "bcos-utilities/BoostLog.h"
#include "bcos-utilities/Error.h"
#include "bcos-utilities/Timer.h"
#include "servant/ServantProxy.h"
#include <util/tc_autoptr.h>
#include <boost/exception/detail/type_info.hpp>
#include <boost/exception/diagnostic_information.hpp>
#include <exception>
#include <functional>
#include <memory>
#include <set>
#include <shared_mutex>
#include <utility>

#define TARS_PING_PERIOD (3000)

namespace bcostars
{
struct TC_EndpointCompare
{
    bool operator()(const tars::TC_Endpoint& lhs, const tars::TC_Endpoint& rhs) const
    {
        return lhs.toString() < rhs.toString();
    }
};

using EndpointSet = std::set<tars::TC_Endpoint, TC_EndpointCompare>;

using TarsServantProxyOnConnectHandler = std::function<void(const tars::TC_Endpoint& ep)>;
using TarsServantProxyOnCloseHandler = std::function<void(const tars::TC_Endpoint& ep)>;

class TarsServantProxyCallback : public tars::ServantProxyCallback
{
public:
    using Ptr = std::shared_ptr<TarsServantProxyCallback>;
    using ConstPtr = std::shared_ptr<const TarsServantProxyCallback>;
    using UniquePtr = std::unique_ptr<const TarsServantProxyCallback>;

public:
    TarsServantProxyCallback(
        const std::string& _serviceName, const tars::TC_AutoPtr<tars::ServantProxy>& _proxy)
      : m_serviceName(_serviceName), m_proxy(_proxy)
    {
        BCOS_LOG(INFO) << LOG_BADGE("[NEWOBJ][TarsServantProxyCallback]")
                       << LOG_KV("_serviceName", _serviceName) << LOG_KV("this", this);
    }

    TarsServantProxyCallback(TarsServantProxyCallback&&) = delete;
    TarsServantProxyCallback(const TarsServantProxyCallback&) = delete;
    const TarsServantProxyCallback& operator=(const TarsServantProxyCallback&) = delete;
    TarsServantProxyCallback& operator=(TarsServantProxyCallback&&) = delete;

    ~TarsServantProxyCallback() override
    {
        BCOS_LOG(INFO) << LOG_BADGE("[DELOBJ][TarsServantProxyCallback]") << LOG_KV("this", this);
        stop();
    }

public:
    int onDispatch(tars::ReqMessagePtr) override { return 0; }

    void onClose(const tars::TC_Endpoint& ep) override
    {
        try
        {
            static bool isFirstLog = true;
            auto p = addInactiveEndpoint(ep);
            if (isFirstLog)
            {
                isFirstLog = true;
                BCOS_LOG(INFO) << LOG_DESC("onClose") << m_serviceName
                               << LOG_KV("endpoint", ep.toString())
                               << LOG_KV("inActiveEndPointSize", p.second);
            }
            else
            {
                BCOS_LOG(TRACE) << LOG_DESC("onClose") << m_serviceName
                                << LOG_KV("endpoint", ep.toString())
                                << LOG_KV("inActiveEndPointSize", p.second);
            }
            if (p.first && m_onCloseHandler)
            {
                m_onCloseHandler(ep);
            }
        }
        catch (const std::exception& e)
        {
            std::cout << "[TarsServantProxyCallback::onClose] "
                      << "exception: " << boost::diagnostic_information(e) << std::endl;
        }
    }

    void onConnect(const tars::TC_Endpoint& ep) override
    {
        auto p = addActiveEndpoint(ep);
        BCOS_LOG(INFO) << LOG_BADGE("ServantProxyCallback::onConnect") << LOG_KV("this", this)
                       << LOG_KV("serviceName", m_serviceName) << LOG_KV("endpoint", ep.toString())
                       << LOG_KV("result", p.first) << LOG_KV("activeEndpoints size", p.second);

        if (p.first && m_onConnectHandler)
        {
            m_onConnectHandler(ep);
        }
    }

    void heartbeat()
    {
        try
        {
            if (m_proxy)
            {
                m_proxy->tars_async_ping();
            }
        }
        catch (...)
        {}
    }

    void start()
    {
        if (m_keepAlive > 0)
        {
            // for heartbeat
            m_heartbeat = std::make_shared<bcos::Timer>(m_keepAlive, "heartTimer");
            m_heartbeat->registerTimeoutHandler([this]() {
                heartbeat();
                m_heartbeat->restart();
            });
            m_heartbeat->start();
        }
    }

    void stop()
    {
        if (m_heartbeat)
        {
            m_heartbeat->stop();
        }
    }

public:
    const std::string& serviceName() const { return m_serviceName; }

    int32_t keepAlive() const { return m_keepAlive; }
    void setKeepAlive(int32_t _keepAlive) { m_keepAlive = _keepAlive; }

    auto activeEndpoints()
    {
        std::shared_lock l(x_endpoints);
        return m_activeEndpoints;
    }

    EndpointSet inactiveEndpoints()
    {
        std::shared_lock l(x_endpoints);
        return m_inactiveEndpoints;
    }

    std::pair<bool, std::size_t> addActiveEndpoint(const tars::TC_Endpoint& ep)
    {
        std::unique_lock l(x_endpoints);
        auto result = m_activeEndpoints.insert(ep);
        m_inactiveEndpoints.erase(ep);
        return std::make_pair(result.second, m_activeEndpoints.size());
    }

    std::pair<bool, std::size_t> addInactiveEndpoint(const tars::TC_Endpoint& ep)
    {
        {
            std::shared_lock l(x_endpoints);
            if (m_inactiveEndpoints.find(ep) != m_inactiveEndpoints.end())
            {
                return std::make_pair(false, m_inactiveEndpoints.size());
            }
        }

        {
            std::unique_lock l(x_endpoints);
            auto result = m_inactiveEndpoints.insert(ep);
            m_activeEndpoints.erase(ep);

            BCOS_LOG(INFO) << LOG_BADGE("ServantProxyCallback::addInactiveEndpoint")
                           << LOG_KV("this", this) << LOG_KV("result", result.second)
                           << LOG_KV("endpoint", ep.toString());

            return std::make_pair(result.second, m_inactiveEndpoints.size());
        }
    }

    TarsServantProxyOnConnectHandler onConnectHandler() const { return m_onConnectHandler; }
    TarsServantProxyOnCloseHandler onCloseHandler() const { return m_onCloseHandler; }

    void setOnConnectHandler(TarsServantProxyOnConnectHandler _handler)
    {
        m_onConnectHandler = std::move(_handler);
    }

    void setOnCloseHandler(TarsServantProxyOnCloseHandler _handler)
    {
        m_onCloseHandler = std::move(_handler);
    }

    bool available() { return !activeEndpoints().empty(); }

private:
    std::string m_serviceName;

    // lock for m_activeEndpoints and m_inactiveEndpoints
    mutable std::shared_mutex x_endpoints;
    // all active tars endpoints
    EndpointSet m_activeEndpoints;
    // all inactive tars endpoints
    EndpointSet m_inactiveEndpoints;

    std::function<void(const tars::TC_Endpoint& ep)> m_onConnectHandler;
    std::function<void(const tars::TC_Endpoint& ep)> m_onCloseHandler;

    int32_t m_keepAlive = 3000;
    std::shared_ptr<bcos::Timer> m_heartbeat = nullptr;
    // TODO: circle reference
    tars::TC_AutoPtr<tars::ServantProxy> m_proxy;
};

template <typename T>
bool checkConnection(std::string const& _module, std::string const& _func, const T& prx,
    std::function<void(bcos::Error::Ptr)> _errorCallback, bool _callsErrorCallback = true)
{
    auto cb = prx->tars_get_push_callback();
    assert(cb);
    auto* tarsServantProxyCallback = (TarsServantProxyCallback*)cb.get();

    if (tarsServantProxyCallback->available())
    {
        return true;
    }

    if (_errorCallback && _callsErrorCallback)
    {
        std::string errorMessage =
            _module + " calls interface " + _func + " failed for empty connection";
        _errorCallback(BCOS_ERROR_PTR(-1, errorMessage));
    }
    return false;
}

template <typename T>
auto tarsProxyAvailableEndPoints(const T& prx)
{
    auto cb = prx->tars_get_push_callback();
    assert(cb);
    return ((TarsServantProxyCallback*)cb.get())->activeEndpoints();
}

}  // namespace bcostars