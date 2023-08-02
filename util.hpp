#pragma once
#include "common.hpp"
#include <cstring>
#include <cassert>

namespace Util {
template<typename T>
class IchigoVector {
public:
    IchigoVector(u64 initial_capacity) : m_capacity(initial_capacity), m_data(new T[initial_capacity]) {}
    IchigoVector() : IchigoVector(16) {}
    ~IchigoVector() { delete[] m_data; }

    T &at(u64 i) { assert(i < m_size); return m_data[i]; }
    u64 size() const { return m_size; }

    void insert(u64 i, T item) {
        assert(i <= m_size);

        if (m_size == m_capacity)
            expand();

        if (i == m_size) {
            m_data[m_size++] = item;
            return;
        }

        std::memmove(&m_data[i + 1], &m_data[i], (m_size - i) * sizeof(T));
        m_data[i] = item;
        ++m_size;
    }

    void append(T item) {
        if (m_size == m_capacity)
            expand();

        m_data[m_size++] = item;
    }

    T remove(u64 i) {
        assert(i < m_size);

        if (i == m_size - 1)
            return m_data[--m_size];

        T ret = m_data[i];
        std::memmove(&m_data[i], &m_data[i + 1], (m_size - i - 1) * sizeof(T));
        --m_size;
        return ret;
    }

private:
    u64 m_capacity;
    u64 m_size = 0;
    T *m_data;

    void expand() {
        T *new_data = new T[m_capacity *= 2];
        std::memcpy(new_data, m_data, m_size * sizeof(T));
        delete[] m_data;
        m_data = new_data;
    }
};
}
