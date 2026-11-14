#ifndef DKR_SDK2_DEVELOPMENT_HPP
#define DKR_SDK2_DEVELOPMENT_HPP
#include "development.h"
#include "json.h"
/* Optional freestanding C++17 conveniences. No STL, exceptions, RTTI, native
 * allocations, threads, static constructors or host-sized ABI records. */
namespace dkr {
class Entity {
    Dkr2Handle handle_{};

  public:
    constexpr Entity() = default;
    constexpr explicit Entity(Dkr2Handle value) : handle_(value) {}
    constexpr Dkr2Handle handle() const {
        return handle_;
    }
    int read(unsigned property, Dkr2Value& output) const {
        Dkr2Read value{};
        value.handle = handle_;
        value.property = property;
        const int status = dkr2_read(&value, 1);
        if (status)
            return status;
        if (!value.status)
            output = value.value;
        return value.status;
    }
    int set(unsigned property, Dkr2Value value) const {
        const Dkr2Edit edit{handle_, property, value};
        return dkr2_edit(&edit, 1);
    }
    int set_float(unsigned property, float real) const {
        Dkr2Value value{};
        value.real = real;
        return set(property, value);
    }
    int set_integer(unsigned property, int integer) const {
        Dkr2Value value{};
        value.integer = integer;
        return set(property, value);
    }
};
class Transaction {
    Dkr2Edit edits_[32]{};
    unsigned count_ = 0;
    int status_ = 0;

  public:
    int add(Entity target, unsigned property, Dkr2Value value) {
        if (count_ == 32)
            return status_ = DKR2_BUDGET;
        edits_[count_++] = {target.handle(), property, value};
        return status_;
    }
    int real(Entity target, unsigned property, float value) {
        Dkr2Value word{};
        word.real = value;
        return add(target, property, word);
    }
    int integer(Entity target, unsigned property, int value) {
        Dkr2Value word{};
        word.integer = value;
        return add(target, property, word);
    }
    int commit() {
        return status_ ? status_ : dkr2_edit(edits_, count_);
    }
    void reset() {
        count_ = 0;
        status_ = 0;
    }
    unsigned size() const {
        return count_;
    }
};
template <class T> class Buffer {
    static_assert(__is_trivially_copyable(T), "Guest buffers hold plain data, not objects needing constructors");
    static_assert(alignof(T) <= 16, "Guest allocation alignment is 16 bytes");
    T* pointer_ = nullptr;
    unsigned count_ = 0;

  public:
    Buffer() = default;
    explicit Buffer(unsigned count)
        : pointer_(count && count <= 65536 / sizeof(T) ? static_cast<T*>(dkr_alloc(count * sizeof(T))) : nullptr),
          count_(pointer_ ? count : 0) {}
    Buffer(const Buffer&) = delete;
    Buffer& operator=(const Buffer&) = delete;
    Buffer(Buffer&& other) noexcept : pointer_(other.pointer_), count_(other.count_) {
        other.pointer_ = nullptr;
        other.count_ = 0;
    }
    Buffer& operator=(Buffer&& other) noexcept {
        if (this != &other) {
            if (pointer_)
                dkr_free(pointer_);
            pointer_ = other.pointer_;
            count_ = other.count_;
            other.pointer_ = nullptr;
            other.count_ = 0;
        }
        return *this;
    }
    ~Buffer() {
        if (pointer_)
            dkr_free(pointer_);
    }
    explicit operator bool() const {
        return pointer_ != nullptr;
    }
    T* data() {
        return pointer_;
    }
    const T* data() const {
        return pointer_;
    }
    unsigned size() const {
        return count_;
    }
    // As with a C array, indexing is the author's responsibility; a buffer is
    // not permission to read past the allocation or construct host objects.
    T& operator[](unsigned index) {
        return pointer_[index];
    }
};
class JsonWriter {
    DkrJson writer_;

  public:
    JsonWriter(char* destination, unsigned capacity) : writer_{destination, capacity, 0, 0} {
        if (capacity)
            destination[0] = 0;
    }
    JsonWriter& raw(const char* value) {
        dkr_json_raw(&writer_, value);
        return *this;
    }
    JsonWriter& text(const char* value) {
        dkr_json_text(&writer_, value);
        return *this;
    }
    JsonWriter& key(const char* value) {
        dkr_json_key(&writer_, value);
        return *this;
    }
    JsonWriter& integer(int value) {
        dkr_json_integer(&writer_, value);
        return *this;
    }
    JsonWriter& real(float value) {
        dkr_json_float(&writer_, value);
        return *this;
    }
    unsigned size() const {
        return writer_.used;
    }
    bool failed() const {
        return writer_.failed != 0;
    }
    int publish() const {
        return writer_.failed ? DKR2_BUDGET : dkr2_ui_publish(writer_.bytes, writer_.used);
    }
};
} // namespace dkr
#endif
