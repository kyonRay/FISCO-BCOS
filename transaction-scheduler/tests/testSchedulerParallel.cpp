#include "bcos-framework/ledger/LedgerConfig.h"
#include "bcos-framework/storage2/MemoryStorage.h"
#include "bcos-framework/storage2/Storage.h"
#include "bcos-tars-protocol/protocol/BlockHeaderImpl.h"
#include "bcos-tars-protocol/protocol/TransactionReceiptFactoryImpl.h"
#include "bcos-transaction-scheduler/MultiLayerStorage.h"
#include <bcos-crypto/hash/Keccak256.h>
#include <bcos-tars-protocol/protocol/TransactionImpl.h>
#include <bcos-task/Wait.h>
#include <bcos-transaction-scheduler/SchedulerParallelImpl.h>
#include <boost/test/unit_test.hpp>

using namespace bcos;
using namespace bcos::storage2;
using namespace bcos::executor_v1;
using namespace bcos::scheduler_v1;
using namespace std::string_view_literals;

struct MockExecutorParallel
{
    template <class Storage>
    struct ExecuteContext
    {
        template <int step>
        task::Task<protocol::TransactionReceipt::Ptr> executeStep()
        {
            co_return {};
        }
    };

    auto createExecuteContext(auto& storage, protocol::BlockHeader const& blockHeader,
        protocol::Transaction const& transaction, int32_t contextID,
        ledger::LedgerConfig const& ledgerConfig, bool call)
        -> task::Task<ExecuteContext<std::decay_t<decltype(storage)>>>
    {
        co_return {};
    }

    task::Task<protocol::TransactionReceipt::Ptr> executeTransaction(auto& storage,
        protocol::BlockHeader const& blockHeader, protocol::Transaction const& transaction,
        int contextID, ledger::LedgerConfig const&, auto&& waitOperator, auto&&...)
    {
        co_return {};
    }
};

class TestSchedulerParallelFixture
{
public:
    using MutableStorage = memory_storage::MemoryStorage<StateKey, StateValue,
        memory_storage::Attribute(memory_storage::ORDERED | memory_storage::LOGICAL_DELETION)>;
    using BackendStorage = memory_storage::MemoryStorage<StateKey, StateValue,
        memory_storage::Attribute(memory_storage::ORDERED | memory_storage::CONCURRENT),
        std::hash<StateKey>>;

    TestSchedulerParallelFixture()
      : cryptoSuite(std::make_shared<bcos::crypto::CryptoSuite>(
            std::make_shared<bcos::crypto::Keccak256>(), nullptr, nullptr)),
        receiptFactory(cryptoSuite),
        multiLayerStorage(backendStorage)
    {}

    BackendStorage backendStorage;
    bcos::crypto::CryptoSuite::Ptr cryptoSuite;
    bcostars::protocol::TransactionReceiptFactoryImpl receiptFactory;
    MultiLayerStorage<MutableStorage, void, BackendStorage> multiLayerStorage;
    crypto::Hash::Ptr hashImpl = std::make_shared<bcos::crypto::Keccak256>();
};

BOOST_FIXTURE_TEST_SUITE(TestSchedulerParallel, TestSchedulerParallelFixture)

BOOST_AUTO_TEST_CASE(simple)
{
    task::syncWait([&, this]() -> task::Task<void> {
        MockExecutorParallel executor;
        SchedulerParallelImpl<MutableStorage> scheduler;

        bcostars::protocol::BlockHeaderImpl blockHeader(
            [inner = bcostars::BlockHeader()]() mutable { return std::addressof(inner); });
        auto transactions =
            ::ranges::iota_view<int, int>(0, 100) | ::ranges::views::transform([](int index) {
                return std::make_unique<bcostars::protocol::TransactionImpl>(
                    [inner = bcostars::Transaction()]() mutable { return std::addressof(inner); });
            }) |
            ::ranges::to<std::vector<std::unique_ptr<bcostars::protocol::TransactionImpl>>>();

        auto view = fork(multiLayerStorage);
        view.newMutable();
        ledger::LedgerConfig ledgerConfig;
        auto receipts = co_await scheduler.executeBlock(view, executor, blockHeader,
            transactions | ::ranges::views::transform([](auto& ptr) -> auto& { return *ptr; }),
            ledgerConfig);
        BOOST_CHECK_EQUAL(transactions.size(), receipts.size());

        co_return;
    }());
    // Wait for tbb
}

constexpr static size_t MOCK_USER_COUNT = 1000;
struct MockConflictExecutor
{
    template <class Storage>
    struct ExecuteContext
    {
        protocol::Transaction const* transaction;
        Storage* storage;

        std::string fromAddress;
        std::string toAddress;

        template <int step>
        task::Task<protocol::TransactionReceipt::Ptr> executeStep()
        {
            if constexpr (step == 0)
            {
                auto input = transaction->input();
                auto inputNum = boost::lexical_cast<int>(
                    std::string_view((const char*)input.data(), input.size()));
                fromAddress = std::to_string(inputNum % MOCK_USER_COUNT);
                toAddress = std::to_string((inputNum + (MOCK_USER_COUNT / 2)) % MOCK_USER_COUNT);
            }
            else if constexpr (step == 1)
            {
                StateKey fromKey{"t_test"sv, fromAddress};
                auto fromEntry = co_await storage2::readOne(*storage, fromKey);
                fromEntry->set(boost::lexical_cast<std::string>(
                    boost::lexical_cast<int>(fromEntry->get()) - 1));
                co_await storage2::writeOne(*storage, fromKey, *fromEntry);

                // Read toKey and +1
                StateKey toKey{"t_test"sv, toAddress};
                auto toEntry = co_await storage2::readOne(*storage, toKey);
                toEntry->set(
                    boost::lexical_cast<std::string>(boost::lexical_cast<int>(toEntry->get()) + 1));
                co_await storage2::writeOne(*storage, toKey, *toEntry);
            }
            else if constexpr (step == 2)
            {
                co_return std::shared_ptr<bcos::protocol::TransactionReceipt>(
                    (bcos::protocol::TransactionReceipt*)0x10086, [](auto* p) {});
            }
            co_return {};
        }
    };

    auto createExecuteContext(auto& storage, protocol::BlockHeader const& blockHeader,
        protocol::Transaction const& transaction, int32_t contextID,
        ledger::LedgerConfig const& ledgerConfig, bool call)
        -> task::Task<ExecuteContext<std::decay_t<decltype(storage)>>>
    {
        co_return ExecuteContext<std::decay_t<decltype(storage)>>{
            .transaction = std::addressof(transaction),
            .storage = std::addressof(storage),
            .fromAddress = {},
            .toAddress = {}};
    }

    task::Task<protocol::TransactionReceipt::Ptr> executeTransaction(auto& storage,
        protocol::BlockHeader const& blockHeader, protocol::Transaction const& transaction,
        int contextID, ledger::LedgerConfig const& /*unused*/, bool /*unused*/)
    {
        co_return {};
    }
};

BOOST_AUTO_TEST_CASE(conflict)
{
    task::syncWait([&, this]() -> task::Task<void> {
        MockConflictExecutor executor;
        SchedulerParallelImpl<MutableStorage> scheduler;

        auto view1 = fork(multiLayerStorage);
        view1.newMutable();
        auto& front = mutableStorage(view1);
        pushView(multiLayerStorage, std::move(view1));

        constexpr static int INITIAL_VALUE = 100000;
        for (auto i : ::ranges::views::iota(0LU, MOCK_USER_COUNT))
        {
            StateKey key{"t_test"sv, boost::lexical_cast<std::string>(i)};
            storage::Entry entry;
            entry.set(boost::lexical_cast<std::string>(INITIAL_VALUE));
            co_await storage2::writeOne(front, key, std::move(entry));
        }

        bcostars::protocol::BlockHeaderImpl blockHeader(
            [inner = bcostars::BlockHeader()]() mutable { return std::addressof(inner); });
        constexpr static auto TRANSACTION_COUNT = 1000;
        auto transactions =
            ::ranges::views::iota(0, TRANSACTION_COUNT) | ::ranges::views::transform([](int index) {
                auto transaction = std::make_unique<bcostars::protocol::TransactionImpl>();
                auto num = boost::lexical_cast<std::string>(index);
                transaction->mutableInner().data.input.assign(num.begin(), num.end());

                return transaction;
            }) |
            ::ranges::to<std::vector<std::unique_ptr<bcostars::protocol::TransactionImpl>>>();

        auto transactionRefs =
            transactions | ::ranges::views::transform([](auto& ptr) -> auto& { return *ptr; });
        auto view = fork(multiLayerStorage);
        view.newMutable();
        ledger::LedgerConfig ledgerConfig;
        auto receipts = co_await scheduler.executeBlock(
            view, executor, blockHeader, transactionRefs, ledgerConfig);
        auto& front2 = mutableStorage(view);
        pushView(multiLayerStorage, std::move(view));

        for (auto i : ::ranges::views::iota(0LU, MOCK_USER_COUNT))
        {
            StateKey key{"t_test"sv, boost::lexical_cast<std::string>(i)};
            auto entry = co_await storage2::readOne(front2, key);
            BOOST_CHECK_EQUAL(boost::lexical_cast<int>(entry->get()), INITIAL_VALUE);
        }
        for (auto const& receipt : receipts)
        {
            if (!receipt)
            {
                BOOST_FAIL("receipt is null!");
            }
            BOOST_CHECK_EQUAL(receipt.get(), (bcos::protocol::TransactionReceipt*)0x10086);
        }

        co_return;
    }());
}

BOOST_AUTO_TEST_SUITE_END()