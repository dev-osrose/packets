#pragma once

#include "packet.h"
#include <msgpack.hpp>

#include <functional>
#include <type_traits>
#include <string>
#include <variant>
#include <vector>
#include <optional>
#include <deque>

namespace {
template< class T >
struct remove_cvref {
    typedef std::remove_cv_t<std::remove_reference_t<T>> type;
};

template <typename T>
using remove_cvref_t = typename remove_cvref<T>::type;

template <typename Variant, std::size_t... I>
constexpr std::array<std::function<Variant()>, sizeof...(I)> build_constructors(std::index_sequence<I...>) {
    return {
        []() {
            using IType = remove_cvref_t<decltype(std::get<I>(std::declval<Variant>()))>;
            return Variant(std::in_place_index_t<I>{}, IType{});
            }...
    };
}

template <typename... Args, typename Indices = std::make_index_sequence<sizeof...(Args)>>
void select_variant(std::variant<Args...>& variant, size_t index) {
    static auto constructors = build_constructors<std::variant<Args...>>(Indices{});
    variant = std::move(constructors[index]());
}
}

namespace Packet {

struct Serializer : public VisitorBase<Serializer> {
    virtual ~Serializer() = default;
    msgpack::sbuffer sbuf;
    msgpack::packer<msgpack::sbuffer> pk;
    Serializer() : pk(&sbuf) {}
    
    virtual bool visit_sequence(size_t length) {
        pk.pack_array(length);
        return true;
    }
    virtual bool visit_enum(uint16_t& data) {
        pk.pack_map(1);
        pk.pack(data);
        pk.pack_nil();
        return true;
    }
    virtual bool operator()(uint8_t& data) {
        pk.pack(data);
        return true;
    }
    virtual bool operator()(int8_t& data) {
        pk.pack(data);
        return true;
    }
    virtual bool operator()(uint16_t& data) {
        pk.pack(data);
        return true;
    }
    virtual bool operator()(int16_t& data) {
        pk.pack(data);
        return true;
    }
    virtual bool operator()(uint32_t& data) {
        pk.pack(data);
        return true;
    }
    virtual bool operator()(int32_t& data) {
        pk.pack(data);
        return true;
    }
    virtual bool operator()(uint64_t& data) {
        pk.pack(data);
        return true;
    }
    virtual bool operator()(int64_t& data) {
        pk.pack(data);
        return true;
    }
    virtual bool operator()(float& data) {
        pk.pack(data);
        return true;
    }
    virtual bool operator()(double& data) {
        pk.pack(data);
        return true;
    }
    virtual bool operator()(std::string& data) {
        pk.pack(data);
        return true;
    }
    virtual bool operator()(std::monostate&) {
        pk.pack_nil();
        return true;
    }
    template <typename T>
    bool operator(std::optional<T>& data) {
        if (data) {
            return (*this)(data.value());
        } else {
            return visit_null();
        }
    }
    template <typename T, size_t N>
    bool operator(std::array<T, N>& data) {
        pk.pack_array(data.size());
        for (const auto& item : data) {
            pk.pack(item);
        }
        return true;
    }
    template <typename T>
    bool operator()(std::vector<T>& data) {
        pk.pack_array(data.size());
        for (const auto& item : data) {
            pk.pack(item);
        }
        return true;
    }
    template <typename... Args>
    bool visit_choice(std::variant<Args...>& data) {
        pk.pack_map(1);
        pk.pack(a.index());
        return std::visit(*static_cast<VisitorBase<Serializer>*>(this), a);
    }
};

struct Deserializer : public VisitorBase<Deserializer> {
    virtual ~Deserializer() = default;
    msgpack::object_handle h;
    std::deque<std::reference_wrapper<const msgpack::object>> oh;
    msgpack::unpacker pac;
    Deserializer(const msgpack::sbuffer& sbuf) {
        pac.reserve_buffer(sbuf.size());
        memcpy(pac.buffer(), sbuf.data(), sbuf.size());
        pac.buffer_consumed(sbuf.size());
        pac.next(h);
        oh.push_back(h.get());
    }
    
    bool visit_sequence(size_t length) {
        const auto& o = oh.front().get();
        oh.pop_front();
        if (o.type != msgpack::type::ARRAY || o.via.array.size != length) {
            return false;
        }
        for (size_t i = 0; i < o.via.array.size; ++i) {
            oh.push_back(o.via.array.ptr[i]);
        }
        return true;
    }
    virtual bool visit_enum(uint16_t& data) {
        const auto& o = oh.front().get();
        oh.pop_front();
        if (o.type != msgpack::type::MAP || o.via.map.size != 1) {
            return false;
        }
        if (o.via.map.ptr[0].val.type != msgpack::type::NIL) {
            return false;
        }
        o.via.map.ptr[0].key.convert(data);
        return true;
    }
    virtual bool operator()(uint8_t& data) {
        const auto& o = oh.front().get();
        oh.pop_front();
        o.convert(data);
        return true;
    }
    virtual bool operator()(int8_t& data) {
        const auto& o = oh.front().get();
        oh.pop_front();
        o.convert(data);
        return true;
    }
    virtual bool operator()(uint16_t& data) {
        const auto& o = oh.front().get();
        oh.pop_front();
        o.convert(data);
        return true;
    }
    virtual bool operator()(int16_t& data) {
        const auto& o = oh.front().get();
        oh.pop_front();
        o.convert(data);
        return true;
    }
    virtual bool operator()(uint32_t& data) {
        const auto& o = oh.front().get();
        oh.pop_front();
        o.convert(data);
        return true;
    }
    virtual bool operator()(int32_t& data) {
        const auto& o = oh.front().get();
        oh.pop_front();
        o.convert(data);
        return true;
    }
    virtual bool operator()(uint64_t& data) {
        const auto& o = oh.front().get();
        oh.pop_front();
        o.convert(data);
        return true;
    }
    virtual bool operator()(int64_t& data) {
        const auto& o = oh.front().get();
        oh.pop_front();
        o.convert(data);
        return true;
    }
    virtual bool operator()(float& data) {
        const auto& o = oh.front().get();
        oh.pop_front();
        o.convert(data);
        return true;
    }
    virtual bool operator()(double& data) {
        const auto& o = oh.front().get();
        oh.pop_front();
        o.convert(data);
        return true;
    }
    virtual bool operator()(std::string& data) {
        const auto& o = oh.front().get();
        oh.pop_front();
        o.convert(data);
        return true;
    }
    virtual bool operator()(std::monostate&) {
        const auto& o = oh.pop_front().get();
        if [[unlikely]] (o.type != msgpack::type::NIL) {
            return false;
        }
        return true;
    }
    template <typename T>
    bool operator(std::optional<T>& data) {
        const auto& o = oh.pop_front().get();
        if (o.type != msgpack::type::NIL) {
            data = o.as<T>();
            return true;
        }
        return true;
    }
    template <typename T, size_t N>
    bool operator(std::array<T, N>& data) {
        const auto& o = oh.front().get();
        oh.pop_front();
        if (o.type != msgpack::type::ARRAY || o.via.array.size != N) {
            return false;
        }
        for (size_t i = 0; i < o.via.array.size; ++i) {
            o.via.array.ptr[i].convert(data[i]);
        }
        return true;
    }
    template <typename T>
    bool operator()(std::vector<T>& data) {
        data.clear();
        const auto& o = oh.front().get();
        oh.pop_front();
        if (o.type != msgpack::type::ARRAY) {
            return false;
        }
        data.reserve(o.via.array.size);
        for (size_t i = 0; i < o.via.array.size; ++i) {
            data.emplace_back(o.via.array.ptr[i].as<T>());
        }
        return true;
    }
    template <typename... Args>
    bool visit_choice(std::variant<Args...>& data) {
        const auto& o = oh.front().get();
        oh.pop_front();
        if (o.type != msgpack::type::MAP || o.via.map.size != 1) {
            return false;
        }
        const size_t index = o.via.map.ptr[0].key.as<size_t>();
        oh.push_front(o.via.map.ptr[0].val);
        select_variant(a, index);
        return std::visit(*static_cast<VisitorBase<Deserializer>*>(this), a);
    }
};
}