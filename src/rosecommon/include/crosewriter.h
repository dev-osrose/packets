#pragma once

#include <bitset>
#include "iserialize.h"

namespace RoseCommon {

class CRoseBasePolicy {
  public:
    virtual ~CRoseBasePolicy() = default;
    virtual bool set_uint8_t(uint8_t data) = 0;
    virtual bool set_int8_t(int8_t data) = 0;
    virtual bool set_uint16_t(uint16_t data) = 0;
    virtual bool set_int16_t(int16_t data) = 0;
    virtual bool set_uint32_t(uint32_t data) = 0;
    virtual bool set_int32_t(int32_t data) = 0;
    virtual bool set_uint64_t(uint64_t data) = 0;
    virtual bool set_int64_t(int64_t data) = 0;
    virtual bool set_string(const std::string& data) = 0;
    virtual bool set_string(const std::string& data, size_t size) = 0;
    virtual bool set_float(float data) = 0;
    virtual bool set_double(double data) = 0;
    virtual bool set_char(char data) = 0;
    virtual bool set_iserialize(const ISerialize& data) = 0;
    virtual bool set_bits(const uint8_t* data, size_t bit_count) = 0;

    template <size_t N>
    bool set_bitset(const std::bitset<N>& bits) {
      constexpr size_t byte_count = (N + 7) / 8;
      for (size_t i = 0; i < byte_count; ++i) {
        uint8_t byte = 0;

        for (size_t bit = 0; bit < 8; ++bit) {
          const size_t index = i * 8 + bit;
          if (index < N && bits.test(index)) {
            byte |= static_cast<uint8_t>(1u << bit);
          }
        }
        if (!set_uint8_t(byte)) return false;
      }
      return true;
    }

    virtual uint16_t get_size() const = 0;
};

template <typename Policy>
class CRosePolicy : public CRoseBasePolicy {
  public:
    CRosePolicy(uint8_t *buffer, uint16_t size) : m_current(buffer), m_buffer(buffer), m_size(size) {}

    bool set_uint8_t(uint8_t data) override { return write(data); }
    bool set_int8_t(int8_t data) override { return write(data); }
    bool set_uint16_t(uint16_t data) override { return write(data); }
    bool set_int16_t(int16_t data) override { return write(data); }
    bool set_uint32_t(uint32_t data) override { return write(data); }
    bool set_int32_t(int32_t data) override { return write(data); }
    bool set_uint64_t(uint64_t data) override { return write(data); }
    bool set_int64_t(int64_t data) override { return write(data); }
    bool set_string(const std::string& data) override {
       for (const char c : data)
           if (!write(c)) return false;
       return write('\0');
    }
    bool set_string(const std::string& data, size_t size) override {
       for (size_t i = 0; i < size && i < data.size(); ++i)
           if (!write(data[i])) return false;
       return true;
    }
    bool set_float(float data) override { return write(data); }
    bool set_double(double data) override { return write(data); }
    bool set_char(char data) override { return write(data); }
    bool set_iserialize(const ISerialize& data) override { return data.write(*this); }

    uint16_t get_size() const override { return m_size; }

    private:
    uint8_t *m_current, *m_buffer;
    uint16_t m_size;

    template <typename T>
    bool write(const T& data) {
      static_assert(std::is_copy_assignable_v<T>, "CRoseWriter doesn't know how to copy assign this type!");
      return Policy::template write(&m_current, m_buffer, &m_size, data);
    }
};

struct WritePolicy {
  template <typename T>
  static bool write(uint8_t** m_current, uint8_t* m_buffer, uint16_t* m_size, const T& data) {
    if (!*m_current || *m_current + sizeof(T) > m_buffer + *m_size) return false;
    *reinterpret_cast<T*>(*m_current) = data;
    *m_current += sizeof(T);
    return true;
  }
};

struct SizePolicy {
  template <typename T>
  static bool write(uint8_t**, uint8_t*, uint16_t* m_size, const T&) {
    *m_size += sizeof(T);
    return true;
  }
};

using CRoseWriter = CRosePolicy<WritePolicy>;
using CRoseSizer = CRosePolicy<SizePolicy>;

class CRoseReader {
  public:
    CRoseReader(const uint8_t *buffer, uint16_t size) : m_current(buffer), m_buffer(buffer), m_size(size) {}

    bool get_uint8_t(uint8_t& data) { return read(data); }
    bool get_int8_t(int8_t& data) { return read(data); }
    bool get_uint16_t(uint16_t& data) { return read(data); }
    bool get_int16_t(int16_t& data) { return read(data); }
    bool get_uint32_t(uint32_t& data) { return read(data); }
    bool get_int32_t(int32_t& data) { return read(data); }
    bool get_uint64_t(uint64_t& data) { return read(data); }
    bool get_int64_t(int64_t& data) { return read(data); }
    bool get_string(std::string& data) {
      data.clear();
      char c = '\0';
      if (!read(c)) return false;
      while (c != '\0') {
        data.push_back(c);
        if (!read(c)) return false;
      }
      return true;
    }
    bool get_string(std::string& data, size_t size) {
      data.clear();
      for (size_t i = 0; i < size; ++i) {
        char c;
        if (!read(c)) return false;
        data.push_back(c);
      }
      return true;
    }
    bool get_float(float& data) { return read(data); }
    bool get_double(double& data) { return read(data); }
    bool get_char(char& data) { return read(data); }
    bool get_iserialize(ISerialize& data) { return data.read(*this); }

    template <size_t N>
    bool get_bitset(std::bitset<N>& data) {
      constexpr size_t byte_count = (N + 7) / 8;

      data.reset();
      for (size_t i = 0; i < byte_count; ++i) {
        uint8_t byte;
        if (!read(byte)) return false;

        for (size_t bit = 0; bit < 8; ++bit) {
          size_t bit_index = i * 8 + bit;
          if (bit_index < N) {
            data.set(bit_index, (byte & (1u << bit)) != 0);
          }
        }
      }

      return true;
    }

    template <typename T> static T read_at(const uint8_t *const buffer) { return *reinterpret_cast<const T* const>(buffer); }

  private:
    const uint8_t *m_current, *m_buffer;
    uint16_t m_size;

    template <typename T>
    bool read(T& data) {
      static_assert(std::is_copy_assignable_v<T>, "CRoseReader doesn't know how to copy assign this type!");
      if (!m_current || m_current + sizeof(T) > m_buffer + m_size) return false;
      data = *reinterpret_cast<const T* const>(m_current);
      m_current += sizeof(T);
      return true;
    }
};

}
