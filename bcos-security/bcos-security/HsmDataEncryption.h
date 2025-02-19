
/*
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
 */
/**
 * @brief : HSM Data Encryption
 * @author: lucasli,asherli
 * @date: 2024-11-07
 */

#pragma once
#include "Common.h"
#include <bcos-crypto/encrypt/HsmSM4Crypto.h>
#include <bcos-framework/security/StorageEncryptInterface.h>
#include <bcos-tool/NodeConfig.h>
#include <bcos-utilities/FileUtility.h>
#include <memory>

namespace bcos
{
namespace security
{
class HsmDataEncryption : public StorageEncryptInterface
{
public:
    using Ptr = std::shared_ptr<HsmDataEncryption>;
    HsmDataEncryption(const bcos::tool::NodeConfig::Ptr nodeConfig);
    ~HsmDataEncryption() override {}

    // use to encrypt/decrypt in rocksdb
    std::string encrypt(const std::string& data) override
    {
        return encrypt((unsigned char*)(data.data()), data.size());
    }
    std::string decrypt(const std::string& data) override
    {
        return decrypt((unsigned char*)(data.data()), data.size());
    }
    std::string encrypt(uint8_t* data, size_t size);
    std::string decrypt(uint8_t* data, size_t size);

private:
    int m_encKeyIndex;
    std::string m_hsmLibPath;
    bcos::tool::NodeConfig::Ptr m_nodeConfig = nullptr;
    bcos::crypto::HsmSM4Crypto::Ptr m_symmetricEncrypt = nullptr;
};

}  // namespace security

}  // namespace bcos
