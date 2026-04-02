
#include "../bcos-transaction-executor/RollbackableStorage.h"
#include "bcos-framework/storage2/MemoryStorage.h"
#include "bcos-framework/storage2/Storage.h"
#include "bcos-framework/transaction-executor/StateKey.h"
#include "bcos-task/Task.h"
#include "bcos-task/Wait.h"
#include "bcos-utilities/Ranges.h"
#include <boost/test/unit_test.hpp>

using namespace bcos;
using namespace bcos::storage2;
using namespace bcos::executor_v1;
using namespace std::string_view_literals;

namespace bcos::executor_v1
{

// Storage with LOGICAL_DELETION — tombstones are preserved as DELETED_TYPE entries
// instead of being physically erased from the container.
using TombstoneStorage = memory_storage::MemoryStorage<StateKey, StateValue,
    memory_storage::ORDERED | memory_storage::LOGICAL_DELETION>;

// DIRECT read overloads required by Rollbackable (HasReadOneDirect / HasReadSomeDirect)
auto tag_invoke(tag_t<readSome> /*unused*/, TombstoneStorage& storage,
    RANGES::input_range auto&& keys, DIRECT_TYPE /*unused*/)
    -> task::Task<task::AwaitableReturnType<decltype(readSome(storage, keys))>>
{
    co_return co_await readSome(storage, std::forward<decltype(keys)>(keys));
}

auto tag_invoke(
    tag_t<readOne> /*unused*/, TombstoneStorage& storage, const auto& key, DIRECT_TYPE /*unused*/)
    -> task::Task<task::AwaitableReturnType<decltype(readOne(storage, key))>>
{
    co_return co_await readOne(storage, key);
}

}  // namespace bcos::executor_v1

BOOST_AUTO_TEST_SUITE(FIB72_TombstoneRollback)

BOOST_AUTO_TEST_CASE(rollbackWritesTombstoneNotPhysicalDelete)
{
    // FIB-72 scenario with layered storage:
    //
    // Lower layer holds a pre-existing value for "Key1". The upper (mutable) layer is
    // wrapped by Rollbackable. A new value is written through Rollbackable and then
    // rolled back.
    //
    // Correct (tombstone):  rollback writes DELETED_TYPE to the upper layer.
    //   MultiLayerStorage::fillMissingValues sees DELETED_TYPE → stops → returns nullopt.
    //
    // Incorrect (physical delete):  rollback erases the entry from the upper layer.
    //   fillMissingValues sees NOT_EXISTS_TYPE → falls through to lower layer →
    //   returns the stale "LowerValue".

    task::syncWait([]() -> task::Task<void> {
        // Lower layer: pre-existing value
        TombstoneStorage lowerLayer;
        StateKey testKey{"table1"sv, "Key1"sv};
        co_await writeOne(lowerLayer, testKey, storage::Entry{"LowerValue"});

        // Upper layer: empty, LOGICAL_DELETION enabled
        TombstoneStorage upperLayer;
        Rollbackable rollbackable(upperLayer);

        auto savepoint = rollbackable.current();

        // Write through Rollbackable.
        // Rollbackable reads DIRECT from upperLayer → nullopt (key absent).
        // Records oldValue = nullopt.
        co_await writeOne(rollbackable, StateKey{"table1"sv, "Key1"sv}, storage::Entry{"NewValue"});

        auto written = co_await readOne(rollbackable, StateKey{"table1"sv, "Key1"sv});
        BOOST_REQUIRE(written);
        BOOST_CHECK_EQUAL(written->get(), "NewValue");

        // Rollback: oldValue was nullopt → calls removeOne(upperLayer, key) WITHOUT DIRECT
        // → writes DELETED_TYPE tombstone (not a physical erase).
        co_await rollbackable.rollback(savepoint);

        // Public readOne returns nullopt for both DELETED_TYPE and NOT_EXISTS_TYPE,
        // so it alone cannot distinguish tombstone from physical delete.
        auto afterRollback = co_await readOne(rollbackable, StateKey{"table1"sv, "Key1"sv});
        BOOST_CHECK(!afterRollback);

        // Inspect the upper layer via the range iterator to verify the tombstone.
        // The entry should exist with DELETED_TYPE, not be physically absent.
        auto iterator = co_await range(upperLayer);
        auto item = co_await iterator.next();

        BOOST_REQUIRE_MESSAGE(item.has_value(),
            "Upper layer must contain a tombstone entry after rollback, not be empty. "
            "A physical delete would leave the upper layer empty, causing layered reads "
            "to fall through to the lower layer and return a stale value.");

        auto& [entryKey, rawValue] = *item;
        BOOST_CHECK_EQUAL(entryKey, testKey);
        BOOST_CHECK_MESSAGE(std::holds_alternative<DELETED_TYPE>(rawValue),
            "Entry must be DELETED_TYPE (tombstone). "
            "In MultiLayerStorage::fillMissingValues, DELETED_TYPE blocks fallthrough "
            "while NOT_EXISTS_TYPE allows it.");

        // Confirm there is exactly one entry in the upper layer
        auto noMore = co_await iterator.next();
        BOOST_CHECK(!noMore.has_value());

        // Verify the lower layer still holds the original value — without the tombstone,
        // a layered read would return this stale value.
        auto lowerValue = co_await readOne(lowerLayer, testKey);
        BOOST_REQUIRE(lowerValue);
        BOOST_CHECK_EQUAL(lowerValue->get(), "LowerValue");

        co_return;
    }());
}

BOOST_AUTO_TEST_SUITE_END()
