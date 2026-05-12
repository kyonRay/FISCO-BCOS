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
 * @brief Schema version read/write/checkExpected facility for RocksDB (spec §8.2)
 * @file SchemaVersion.cpp
 * @author: kyonRay
 * @date: 2026-05-12
 */
#include "SchemaVersion.h"
#include <fmt/format.h>
#include <rocksdb/write_batch.h>
#include <charconv>
#include <string>

namespace bcos::storage2::rocksdb
{

void SchemaVersion::write(::rocksdb::DB& db, int version)
{
    std::string value = std::to_string(version);
    ::rocksdb::WriteOptions wo;
    auto status = db.Put(wo, db.DefaultColumnFamily(), kKey, value);
    if (!status.ok())
    {
        throw std::runtime_error(fmt::format("SchemaVersion::write failed: {}", status.ToString()));
    }
}

std::optional<int> SchemaVersion::read(::rocksdb::DB& db)
{
    std::string value;
    ::rocksdb::ReadOptions ro;
    auto status = db.Get(ro, db.DefaultColumnFamily(), kKey, &value);
    if (status.IsNotFound())
    {
        return std::nullopt;
    }
    if (!status.ok())
    {
        throw std::runtime_error(fmt::format("SchemaVersion::read failed: {}", status.ToString()));
    }

    int version = 0;
    auto [ptr, ec] = std::from_chars(value.data(), value.data() + value.size(), version);
    if (ec != std::errc{} || ptr != value.data() + value.size())
    {
        throw std::runtime_error(
            fmt::format("SchemaVersion::read: stored value '{}' is not a valid integer", value));
    }
    return version;
}

void SchemaVersion::checkExpected(::rocksdb::DB& db, int expected)
{
    auto actual = read(db);
    if (!actual.has_value())
    {
        throw SchemaVersionMismatch(
            fmt::format("SchemaVersion::checkExpected: key '{}' is absent "
                        "(expected version {})",
                kKey, expected));
    }
    if (*actual != expected)
    {
        throw SchemaVersionMismatch(
            fmt::format("SchemaVersion::checkExpected: stored version {} != expected version {}",
                *actual, expected));
    }
}

}  // namespace bcos::storage2::rocksdb
