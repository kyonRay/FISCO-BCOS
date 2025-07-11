#pragma once
#include "bcos-task/Task.h"
#include "bcos-task/Trait.h"
#include <optional>
#include <range/v3/range.hpp>
#include <range/v3/view/indirect.hpp>
#include <range/v3/view/single.hpp>
#include <range/v3/view/transform.hpp>
#include <range/v3/view/zip.hpp>
#include <type_traits>

// tag_invoke storage interface
namespace bcos::storage2
{

inline constexpr struct DIRECT_TYPE
{
} DIRECT;

inline constexpr struct RANGE_SEEK_TYPE
{
} RANGE_SEEK;

template <class Invoke>
using ReturnType = typename task::AwaitableReturnType<Invoke>;
template <class Tag, class Storage, class... Args>
concept HasTag = requires(Tag tag, Storage storage, Args&&... args) {
    requires task::IsAwaitable<decltype(tag_invoke(tag, storage, std::forward<Args>(args)...))>;
};

inline constexpr struct ReadSome
{
    auto operator()(auto& storage, ::ranges::input_range auto keys, auto&&... args) const
        -> task::Task<ReturnType<decltype(tag_invoke(
            *this, storage, std::move(keys), std::forward<decltype(args)>(args)...))>>
        requires ::ranges::range<ReturnType<decltype(tag_invoke(
            *this, storage, std::move(keys), std::forward<decltype(args)>(args)...))>>
    {
        co_return co_await tag_invoke(
            *this, storage, std::move(keys), std::forward<decltype(args)>(args)...);
    }
} readSome;

inline constexpr struct WriteSome
{
    auto operator()(auto& storage, ::ranges::input_range auto keyValues, auto&&... args) const
        -> task::Task<ReturnType<decltype(tag_invoke(
            *this, storage, std::move(keyValues), std::forward<decltype(args)>(args)...))>>
        requires(std::tuple_size_v<::ranges::range_value_t<decltype(keyValues)>> >= 2)
    {
        co_await tag_invoke(
            *this, storage, std::move(keyValues), std::forward<decltype(args)>(args)...);
    }
} writeSome;

inline constexpr struct RemoveSome
{
    auto operator()(auto& storage, ::ranges::input_range auto keys, auto&&... args) const
        -> task::Task<ReturnType<decltype(tag_invoke(
            *this, storage, std::move(keys), std::forward<decltype(args)>(args)...))>>
    {
        co_await tag_invoke(*this, storage, std::move(keys), std::forward<decltype(args)>(args)...);
    }
} removeSome;

struct NOT_EXISTS_TYPE
{
};
constexpr inline struct DELETED_TYPE
{
} deleteItem;

template <class Value>
using StorageValueType = std::variant<NOT_EXISTS_TYPE, DELETED_TYPE, Value>;

template <class IteratorType>
concept Iterator =
    requires(IteratorType iterator) { requires task::IsAwaitable<decltype(iterator.next())>; };
inline constexpr struct Range
{
    auto operator()(auto& storage, auto&&... args) const -> task::Task<
        ReturnType<decltype(tag_invoke(*this, storage, std::forward<decltype(args)>(args)...))>>
        requires Iterator<
            ReturnType<decltype(tag_invoke(*this, storage, std::forward<decltype(args)>(args)...))>>
    {
        co_return co_await tag_invoke(*this, storage, std::forward<decltype(args)>(args)...);
    }
} range;

namespace detail
{
auto toSingleView(auto&& item)
{
    if constexpr (std::is_lvalue_reference_v<decltype(item)>)
    {
        return ::ranges::views::single(std::addressof(item)) | ::ranges::views::indirect;
    }
    else
    {
        return ::ranges::views::single(std::forward<decltype(item)>(item));
    }
}
}  // namespace detail

inline constexpr struct ReadOne
{
    auto operator()(auto& storage, auto key, auto&&... args) const
        -> task::Task<std::optional<typename std::decay_t<decltype(storage)>::Value>>
    {
        co_return co_await tag_invoke(
            *this, storage, std::move(key), std::forward<decltype(args)>(args)...);
    }
} readOne;

inline constexpr struct WriteOne
{
    auto operator()(auto& storage, auto key, auto value, auto&&... args) const -> task::Task<void>
    {
        co_await tag_invoke(*this, storage, std::move(key), std::move(value),
            std::forward<decltype(args)>(args)...);
    }
} writeOne;

inline constexpr struct RemoveOne
{
    auto operator()(auto& storage, auto key, auto&&... args) const -> task::Task<void>
    {
        if constexpr (HasTag<RemoveOne, decltype(storage), decltype(key), decltype(args)...>)
        {
            co_await tag_invoke(
                *this, storage, std::move(key), std::forward<decltype(args)>(args)...);
        }
        else
        {
            co_await removeSome(storage, detail::toSingleView(std::move(key)),
                std::forward<decltype(args)>(args)...);
        }
    }
} removeOne;

inline constexpr struct ExistsOne
{
    auto operator()(auto& storage, auto key, auto&&... args) const -> task::Task<bool>
    {
        if constexpr (HasTag<ExistsOne, decltype(storage), decltype(key), decltype(args)...>)
        {
            co_return co_await tag_invoke(
                *this, storage, std::move(key), std::forward<decltype(args)>(args)...);
        }
        else
        {
            auto result =
                co_await readOne(storage, std::move(key), std::forward<decltype(args)>(args)...);
            co_return result.has_value();
        }
    }
} existsOne;

inline constexpr struct Merge
{
    auto operator()(auto& toStorage, auto& fromStorage, auto&&... args) const -> task::Task<void>
    {
        co_await tag_invoke(*this, toStorage, fromStorage, std::forward<decltype(args)>(args)...);
    }
} merge;

#if (defined(__GNUC__) && ((__GNUC__ * 100 + GNUC_MINOR) >= 1203)) || \
    (defined(__clang__) && (__clang_major__ > 13))
template <class Storage, class Key>
concept ReadableStorage = requires(Storage& storage, Key key, std::array<Key, 1> keys) {
    { readSome(storage, keys) } -> task::IsAwaitable;
    { readOne(storage, key) } -> task::IsAwaitable;
};
template <class Storage, class Key, class Value>
concept WritableStorage = requires(
    Storage& storage, Key key, Value value, std::array<std::pair<Key, Value>, 1> keyValues) {
    { writeSome(storage, keyValues) } -> task::IsAwaitable;
    { writeOne(storage, key, value) } -> task::IsAwaitable;
};
#else
template <class Storage, class Key>
concept ReadableStorage = true;
template <class Storage, class Key, class Value>
concept WritableStorage = true;
#endif
template <class Storage, class Key, class Value>
concept ReadWriteStorage = ReadableStorage<Storage, Key> && WritableStorage<Storage, Key, Value>;

template <auto& Tag>
using tag_t = std::decay_t<decltype(Tag)>;
}  // namespace bcos::storage2
