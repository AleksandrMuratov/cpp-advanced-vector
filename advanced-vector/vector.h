#pragma once
#include <cassert>
#include <cstdlib>
#include <new>
#include <utility>
#include <memory>
#include <algorithm>
#include <iterator>

template <typename T>
class RawMemory {
public:
    RawMemory() = default;

    explicit RawMemory(size_t capacity)
        : buffer_(Allocate(capacity))
        , capacity_(capacity) {
    }

    RawMemory(const RawMemory&) = delete;

    RawMemory& operator=(const RawMemory&) = delete;

    RawMemory(RawMemory&& other) noexcept
        : buffer_(std::exchange(other.buffer_, nullptr)),
        capacity_(std::exchange(other.capacity_, 0u))
    {}

    RawMemory& operator=(RawMemory&& other) noexcept {
        buffer_ = std::exchange(other.buffer_, nullptr);
        capacity_ = other.capacity_;
    }

    ~RawMemory() {
        Deallocate(buffer_);
    }

    T* operator+(size_t offset) noexcept {
        // Разрешается получать адрес ячейки памяти, следующей за последним элементом массива
        assert(offset <= capacity_);
        return buffer_ + offset;
    }

    const T* operator+(size_t offset) const noexcept {
        return const_cast<RawMemory&>(*this) + offset;
    }

    const T& operator[](size_t index) const noexcept {
        return const_cast<RawMemory&>(*this)[index];
    }

    T& operator[](size_t index) noexcept {
        assert(index <= capacity_);
        return buffer_[index];
    }

    void Swap(RawMemory& other) noexcept {
        std::swap(buffer_, other.buffer_);
        std::swap(capacity_, other.capacity_);
    }

    const T* GetAddress() const noexcept {
        return buffer_;
    }

    T* GetAddress() noexcept {
        return buffer_;
    }

    size_t Capacity() const {
        return capacity_;
    }

private:
    // Выделяет сырую память под n элементов и возвращает указатель на неё
    static T* Allocate(size_t n) {
        return n != 0 ? static_cast<T*>(operator new(n * sizeof(T))) : nullptr;
    }

    // Освобождает сырую память, выделенную ранее по адресу buf при помощи Allocate
    static void Deallocate(T* buf) noexcept {
        operator delete(buf);
    }

    T* buffer_ = nullptr;
    size_t capacity_ = 0u;
};

template <typename T>
class Vector {
public:
    using iterator = T*;
    using const_iterator = const T*;

    iterator begin() noexcept {
        return data_.GetAddress();
    }
    iterator end() noexcept {
        if (size_ == 0u) {
            return begin();
        }
        return data_ + size_;
    }
    const_iterator begin() const noexcept {
        return data_.GetAddress();
    }
    const_iterator end() const noexcept {
        if (size_ == 0u) {
            return begin();
        }
        return data_ + size_;
    }
    const_iterator cbegin() const noexcept {
        return begin();
    }
    const_iterator cend() const noexcept {
        return end();
    }

    Vector() = default;

    explicit Vector(size_t size)
        : data_(size)
        , size_(size)  
    {
        std::uninitialized_value_construct_n(data_.GetAddress(), size);
    }

    Vector(const Vector& other)
        : data_(other.size_)
        , size_(other.size_)  //
    {
        std::uninitialized_copy_n(other.data_.GetAddress(), size_, data_.GetAddress());
    }

    Vector(Vector&& other) noexcept {
        Swap(other);
    }

    Vector& operator=(const Vector& other) {
        if (this != &other) {
            if (other.Size() > Capacity()) {
                Vector tmp(other);
                Swap(tmp);
            }
            else {
                std::copy_n(other.data_.GetAddress(), std::min(Size(), other.Size()), data_.GetAddress());
                if (Size() > other.Size()) {
                    std::destroy_n(data_ + other.Size(), Size() - other.Size());
                }
                else {
                    std::uninitialized_copy_n(other.data_ + Size(), other.Size() - Size(), data_ + Size());
                }
                size_ = other.size_;
            }
        }
        return *this;
    }

    Vector& operator=(Vector&& other) noexcept {
        Swap(other);
        return *this;
    }

    void Reserve(size_t new_capacity) {
        if (new_capacity <= data_.Capacity()) {
            return;
        }
        RawMemory<T> new_data(new_capacity);
        if constexpr (std::is_nothrow_move_constructible_v<T> || !std::is_copy_constructible_v<T>) {
            std::uninitialized_move_n(data_.GetAddress(), size_, new_data.GetAddress());
        }
        else {
            std::uninitialized_copy_n(data_.GetAddress(), size_, new_data.GetAddress());
        }
        data_.Swap(new_data);
        std::destroy_n(new_data.GetAddress(), size_);
    }

    void Resize(size_t new_size) {
        if (new_size < size_) {
            std::destroy_n(data_ + new_size, size_ - new_size);
        }
        else {
            if (new_size > Capacity()) {
                Reserve(new_size);
            }
            std::uninitialized_value_construct_n(data_ + size_, new_size - size_);
        }
        size_ = new_size;
    }

    void PushBack(const T& value) {
        EmplaceBack(value);
    }

    void PushBack(T&& value) {
        EmplaceBack(std::move(value));
    }

    iterator Insert(const_iterator pos, const T& value) {
        return Emplace(pos, value);
    }

    iterator Insert(const_iterator pos, T&& value) {
        return Emplace(pos, std::move(value));
    }

    iterator Erase(const_iterator pos) {
        size_t pos_index = std::distance(cbegin(), pos);
        std::destroy_at(begin() + pos_index);
        if constexpr (std::is_nothrow_move_constructible_v<T> || !std::is_copy_constructible_v<T>) {
            std::move(begin() + pos_index + 1u, end(), begin() + pos_index);
        }
        else {
            std::copy(begin() + pos_index + 1u, end(), begin() + pos_index);
        }
        --size_;
        return begin() + pos_index;
    }

    template<typename... Args>
    T& EmplaceBack(Args&&... args) {
        if (size_ < Capacity()) {
            new(data_ + size_) T(std::forward<Args>(args)...);
        }
        else {
            RawMemory<T> new_data(size_ == 0u ? 1u : size_ * 2u);
            new(new_data + size_) T(std::forward<Args>(args)...);
            if constexpr (std::is_nothrow_move_constructible_v<T> || !std::is_copy_constructible_v<T>) {
                std::uninitialized_move_n(data_.GetAddress(), size_, new_data.GetAddress());
            }
            else {
                std::uninitialized_copy_n(data_.GetAddress(), size_, new_data.GetAddress());
            }
            data_.Swap(new_data);
            std::destroy_n(new_data.GetAddress(), size_);
        }
        ++size_;
        return data_[size_ - 1u];
    }

    template <typename... Args>
    iterator Emplace(const_iterator pos, Args&&... args) {
        if (pos == cend()) {
            EmplaceBack(std::forward<Args>(args)...);
            return std::prev(end());
        }
        size_t pos_index = std::distance(cbegin(), pos);
        if (size_ < Capacity()) {
            new(data_ + size_) T(std::move(data_[size_ - 1u]));
            std::move_backward(begin() + pos_index, std::prev(end()), end());
            data_[pos_index] = T(std::forward<Args>(args)...);
        }
        else {
            RawMemory<T> new_data(size_ == 0u ? 1u : size_ * 2u);
            new(new_data + pos_index) T(std::forward<Args>(args)...);
            if constexpr (std::is_nothrow_move_constructible_v<T> || !std::is_copy_constructible_v<T>) {
                std::uninitialized_move_n(data_.GetAddress(), pos_index, new_data.GetAddress());
                std::uninitialized_move_n(data_ + pos_index, size_ - pos_index, new_data + (pos_index + 1u));
            }
            else {
                std::uninitialized_copy_n(data_.GetAddress(), pos_index, new_data.GetAddress());
                std::uninitialized_copy_n(data_ + pos_index, size_ - pos_index, new_data + (pos_index + 1u));
            }
            data_.Swap(new_data);
            std::destroy_n(new_data.GetAddress(), size_);
        }
        ++size_;
        return begin() + pos_index;
    }

    void PopBack() {
        assert(size_ > 0u);
        std::destroy_at(data_ + (size_ - 1u));
        --size_;
    }

    size_t Size() const noexcept {
        return size_;
    }

    size_t Capacity() const noexcept {
        return data_.Capacity();
    }

    const T& operator[](size_t index) const noexcept {
        assert(index < size_);
        return data_[index];
    }

    T& operator[](size_t index) noexcept {
        assert(index < size_);
        return data_[index];
    }

    void Swap(Vector& other) noexcept {
        data_.Swap(other.data_);
        std::swap(size_, other.size_);
    }

    ~Vector() {
        std::destroy_n(data_.GetAddress(), size_);
    }

private:
    RawMemory<T> data_;
    size_t size_ = 0u;
};