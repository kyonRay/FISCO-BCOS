/**
 *  Copyright (C) 2026 FISCO BCOS.
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
 */
#include "Web3TxToTars.h"
#include <bcos-crypto/signature/secp256k1/Secp256k1Crypto.h>
#include <bcos-framework/protocol/Transaction.h>
#include <bcos-utilities/Common.h>
#include <range/v3/algorithm/move.hpp>

namespace bcos::rpc
{
bcostars::Transaction takeToTarsTransaction(Web3Transaction& web3Tx)
{
    bcostars::Transaction tarsTx{};
    tarsTx.data.to = (web3Tx.to.has_value()) ? web3Tx.to.value().hexPrefixed() : "";
    tarsTx.data.input.reserve(web3Tx.data.size());
    ::ranges::move(web3Tx.data, std::back_inserter(tarsTx.data.input));

    tarsTx.data.value = "0x" + web3Tx.value.str(0, std::ios_base::hex);
    tarsTx.data.gasLimit = web3Tx.gasLimit;
    if (static_cast<uint8_t>(web3Tx.type) >= static_cast<uint8_t>(TransactionType::EIP1559))
    {
        tarsTx.data.maxFeePerGas = "0x" + web3Tx.maxFeePerGas.str(0, std::ios_base::hex);
        tarsTx.data.maxPriorityFeePerGas =
            "0x" + web3Tx.maxPriorityFeePerGas.str(0, std::ios_base::hex);
    }
    else
    {
        tarsTx.data.gasPrice = "0x" + web3Tx.maxPriorityFeePerGas.str(0, std::ios_base::hex);
    }
    tarsTx.type = static_cast<tars::Char>(bcos::protocol::TransactionType::Web3Transaction);

    // Only call encodeForSign() once, store in extraTransactionBytes for TxValidator::verify()
    auto encodedForSign = web3Tx.encodeForSign();
    tarsTx.extraTransactionBytes.reserve(encodedForSign.size());
    ::ranges::move(encodedForSign, std::back_inserter(tarsTx.extraTransactionBytes));

    // FISCO BCOS signature is r||s||v
    tarsTx.signature.reserve(crypto::SECP256K1_SIGNATURE_LEN);
    ::ranges::move(web3Tx.signatureR, std::back_inserter(tarsTx.signature));
    ::ranges::move(web3Tx.signatureS, std::back_inserter(tarsTx.signature));
    tarsTx.signature.push_back(static_cast<tars::Char>(web3Tx.signatureV));

    tarsTx.data.nonce = toQuantity(web3Tx.nonce);
    tarsTx.data.chainID = std::to_string(web3Tx.chainId.value_or(0));

    // dataHash and sender left empty -- TxValidator::verify() computes them
    return tarsTx;
}
}  // namespace bcos::rpc
