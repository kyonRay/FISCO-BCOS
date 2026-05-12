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
 * @brief Unit tests for SchemaVersion (spec §8.2)
 * @file TestSchemaVersion.cpp
 * @author: kyonRay
 * @date: 2026-05-12
 */
#include <bcos-storage/SchemaVersion.h>
#include <rocksdb/db.h>
#include <rocksdb/options.h>
#include <boost/filesystem.hpp>
#include <boost/test/unit_test.hpp>

using namespace bcos::storage2::rocksdb;

struct SchemaVersionFixture
{
    std::string path = "./schema_v_" + std::to_string(std::random_device{}());

    SchemaVersionFixture()
    {
        ::rocksdb::Options options;
        options.create_if_missing = true;

        ::rocksdb::DB* raw = nullptr;
        ::rocksdb::Status s = ::rocksdb::DB::Open(options, path, &raw);
        BOOST_REQUIRE(s.ok());
        db.reset(raw);
    }

    ~SchemaVersionFixture() { boost::filesystem::remove_all(path); }

    std::unique_ptr<::rocksdb::DB> db;
};

BOOST_FIXTURE_TEST_SUITE(SchemaVersionSuite, SchemaVersionFixture)

BOOST_AUTO_TEST_CASE(WriteAndReadSchemaVersion)
{
    // Write version 1 and verify read-back from the same open DB
    SchemaVersion::write(*db, 1);
    auto result = SchemaVersion::read(*db);
    BOOST_REQUIRE(result.has_value());
    BOOST_CHECK_EQUAL(*result, 1);

    // Durability: close the DB, reopen the same path, read again
    db.reset();

    ::rocksdb::Options options;
    options.create_if_missing = false;
    ::rocksdb::DB* raw = nullptr;
    ::rocksdb::Status s = ::rocksdb::DB::Open(options, path, &raw);
    BOOST_REQUIRE(s.ok());
    db.reset(raw);

    auto result2 = SchemaVersion::read(*db);
    BOOST_REQUIRE(result2.has_value());
    BOOST_CHECK_EQUAL(*result2, 1);
}

BOOST_AUTO_TEST_CASE(LegacyDbReturnsNullopt)
{
    // Fresh DB with no version key written — must return nullopt (legacy-DB compatible)
    auto result = SchemaVersion::read(*db);
    BOOST_CHECK(!result.has_value());
}

BOOST_AUTO_TEST_CASE(MismatchedExpectedVersionThrows)
{
    // Write version 2; checkExpected(db, 1) must throw SchemaVersionMismatch
    SchemaVersion::write(*db, 2);
    BOOST_CHECK_THROW(SchemaVersion::checkExpected(*db, 1), SchemaVersionMismatch);

    // checkExpected(db, 2) must NOT throw
    BOOST_CHECK_NO_THROW(SchemaVersion::checkExpected(*db, 2));

    // Absent-key case: fresh DB (no key) + non-zero expectation → throws
    db.reset();
    boost::filesystem::remove_all(path);

    ::rocksdb::Options opts;
    opts.create_if_missing = true;
    ::rocksdb::DB* raw = nullptr;
    ::rocksdb::Status s = ::rocksdb::DB::Open(opts, path, &raw);
    BOOST_REQUIRE(s.ok());
    db.reset(raw);

    BOOST_CHECK_THROW(SchemaVersion::checkExpected(*db, 1), SchemaVersionMismatch);
}

BOOST_AUTO_TEST_CASE(ReadRejectsValueWithTrailingGarbage)
{
    // Bypass write() to inject a malformed value directly.
    auto status =
        db->Put(::rocksdb::WriteOptions{}, db->DefaultColumnFamily(), SchemaVersion::kKey, "5xyz");
    BOOST_REQUIRE(status.ok());

    BOOST_CHECK_THROW(SchemaVersion::read(*db), std::runtime_error);
}

BOOST_AUTO_TEST_SUITE_END()
