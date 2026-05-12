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
 * @file SchemaVersion.h
 * @author: kyonRay
 * @date: 2026-05-12
 */
#pragma once

#include <rocksdb/db.h>
#include <optional>
#include <stdexcept>

namespace bcos::storage2::rocksdb
{

struct SchemaVersionMismatch : public std::runtime_error
{
    using std::runtime_error::runtime_error;
};

class SchemaVersion
{
public:
    static constexpr char kKey[] = "__schema_version__";

    /// Write (or overwrite) the schema version integer as ASCII into the default CF.
    static void write(::rocksdb::DB& db, int version);

    /// Read the persisted schema version.
    /// Returns std::nullopt if the key is absent (legacy / fresh DB).
    /// Never throws on I/O success — a missing key is not an error.
    static std::optional<int> read(::rocksdb::DB& db);

    /// Verify that the persisted version equals @p expected.
    /// Throws SchemaVersionMismatch if:
    ///   - the key is absent (legacy DB treated as mismatch when an expectation is provided), or
    ///   - the stored value differs from @p expected.
    static void checkExpected(::rocksdb::DB& db, int expected);
};

}  // namespace bcos::storage2::rocksdb
