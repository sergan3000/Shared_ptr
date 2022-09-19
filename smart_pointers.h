#include <iostream>
#include <memory>

template<typename T>
class WeakPtr;

struct BaseControlBlock {
    size_t shared_count = 0;
    size_t weak_count = 0;

    virtual void destroy_ptr() = 0;

    virtual void* get_ptr() = 0;

    virtual void deallocate_block() = 0;

    virtual ~BaseControlBlock() = default;
};

template<typename T, typename Deleter = std::default_delete<T>, typename Alloc = std::allocator<T>>
struct ControlBlockRegular : BaseControlBlock {
    T* ptr;
    Deleter del;
    Alloc alloc;

    ControlBlockRegular(T* ptr, Deleter del, Alloc alloc) : ptr(ptr), del(std::move(del)), alloc(std::move(alloc)) {}

    void destroy_ptr() override {
        del(ptr);
        ptr = nullptr;
    }

    void deallocate_block() override {
        typename std::allocator_traits<Alloc>::template rebind_alloc<ControlBlockRegular>(alloc).deallocate(this, 1);
    }

    void* get_ptr() override {
        return ptr;
    }
};

template<typename T, typename Alloc = std::allocator<T>>
struct ControlBlockMakeShared : BaseControlBlock {
    Alloc alloc;
    T object;

    template<typename... Args>
    ControlBlockMakeShared(Alloc&& alloc, Args&& ... args)
            : alloc(std::move(alloc)), object(std::forward<Args>(args)...) {}

    void destroy_ptr() override {
        std::allocator_traits<Alloc>::destroy(alloc, &object);
    }

    void deallocate_block() override {
        using AllocShared = typename std::allocator_traits<Alloc>::template rebind_alloc<ControlBlockMakeShared<T, Alloc>>;
        AllocShared allocShared = alloc;
        alloc.~Alloc();
        std::allocator_traits<AllocShared>::deallocate(allocShared, this, 1);
    }

    void* get_ptr() override {
        return &object;
    }
};

template<typename T>
class SharedPtr {
private:
    T* ptr = nullptr;
    BaseControlBlock* block = nullptr;

public:
    template<typename U>
    friend
    class WeakPtr;

    template<typename U>
    friend
    class SharedPtr;

    void swap(SharedPtr<T>& other) {
        std::swap(ptr, other.ptr);
        std::swap(block, other.block);
    }

    T* get() const {
        return ptr;
    }

    SharedPtr() = default;

    template<typename U, typename Deleter = std::default_delete<T>, typename Alloc = std::allocator<T>>
    SharedPtr<T>(U* ptr, Deleter del = Deleter(), Alloc alloc = Alloc()) : ptr(ptr) {
        using AllocRegular = typename std::allocator_traits<Alloc>::template rebind_alloc<ControlBlockRegular<U, Deleter, Alloc>>;
        AllocRegular alloc_regular = alloc;
        auto block_object = std::allocator_traits<AllocRegular>::allocate(alloc_regular, 1);
        new(block_object) ControlBlockRegular<U, Deleter, Alloc>(ptr, del, alloc);
        block = block_object;
        block->shared_count++;
    }

    size_t use_count() const {
        return block->shared_count;
    }


    template<typename Alloc = std::allocator<T>, typename... Args>
    SharedPtr<T>(Alloc alloc = Alloc(), Args&& ... args) {
        using AllocShared = typename std::allocator_traits<Alloc>::template rebind_alloc<ControlBlockMakeShared<T, Alloc>>;
        AllocShared allocShared = alloc;
        auto new_block = std::allocator_traits<AllocShared>::allocate(allocShared, 1);
        std::allocator_traits<AllocShared>::construct(allocShared, new_block, (allocShared),
                                                      std::forward<Args>(args)...);
        block = new_block;
        ptr = &new_block->object;
        block->shared_count++;
    }

    SharedPtr<T>(const SharedPtr<T>& other) : ptr(other.ptr), block(other.block) {
        if (other.block != nullptr) block->shared_count++;
    }

    template<typename U>
    SharedPtr<T>(const SharedPtr<U>& other) : ptr(other.ptr), block(other.block) {
        if (other.block) block->shared_count++;
    }

    SharedPtr<T>(SharedPtr<T>&& other) : ptr(other.ptr), block(other.block) {
        other.ptr = nullptr;
        other.block = nullptr;
    }

    template<typename U>
    SharedPtr<T>(SharedPtr<U>&& other) : ptr(other.ptr), block(other.block) {
        other.ptr = nullptr;
        other.block = nullptr;
    }

    SharedPtr<T>(const WeakPtr<T>& weak) : ptr(static_cast<T*>(weak.block->get_ptr())), block(weak.block) {
        block->shared_count++;
    }

    SharedPtr<T>& operator=(const SharedPtr<T>& other) {
        auto copy = SharedPtr<T>(other);
        swap(copy);
        return *this;
    }

    template<typename U>
    SharedPtr<T>& operator=(const SharedPtr<U>& other) {
        auto copy = SharedPtr<T>(other);
        swap(copy);
        return *this;
    }

    SharedPtr<T>& operator=(SharedPtr<T>&& other) {
        auto copy = SharedPtr<T>(std::move(other));
        swap(copy);
        return *this;
    }

    template<typename U>
    SharedPtr<T>& operator=(SharedPtr<U>&& other) {
        auto copy = SharedPtr<T>(std::move(other));
        swap(copy);
        return *this;
    }

    void reset() {
        *this = SharedPtr<T>();
    }

    void reset(T* new_ptr) {
        *this = SharedPtr<T>(new_ptr);
    }

    template<typename U>
    void reset(U* new_ptr) {
        *this = SharedPtr<T>(new_ptr);
    }

    T* operator->() const {
        return ptr;
    }

    T& operator*() const {
        return *ptr;
    }

    ~SharedPtr() {
        if (block == nullptr) return;
        block->shared_count--;
        if (block->shared_count) return;
        block->destroy_ptr();
        if (!block->weak_count) {
            block->deallocate_block();
            block = nullptr;
        }
    }
};


template<typename T>
class WeakPtr {
private:
    BaseControlBlock* block = nullptr;
public:

    template<typename U>
    friend
    class WeakPtr;

    template<typename U>
    friend
    class SharedPtr;

    WeakPtr() {}

    WeakPtr(const SharedPtr<T>& shared) : block(shared.block) {
        block->weak_count++;
    }

    void swap(WeakPtr<T>& other) {
        std::swap(block, other.block);
    }

    size_t use_count() {
        return block->shared_count;
    }

    template<typename U>
    WeakPtr(const SharedPtr<U>& shared) : block(shared.block) {
        block->weak_count++;
    }

    WeakPtr(const WeakPtr<T>& weak) : block(weak.block) {
        block->weak_count++;
    }

    template<typename U>
    WeakPtr(const WeakPtr<U>& weak) : block(weak.block) {
        block->weak_count++;
    }

    WeakPtr(WeakPtr<T>&& weak) : block(weak.block) {
        weak.block = nullptr;
    }

    template<typename U>
    WeakPtr(WeakPtr<U>&& weak) : block(weak.block) {
        weak.block = nullptr;
    }

    WeakPtr& operator=(const WeakPtr<T>& other) {
        auto copy = other;
        swap(copy);
        return *this;
    }

    template<typename U>
    WeakPtr& operator=(const WeakPtr<U>& other) {
        auto copy = other;
        swap(copy);
        return *this;
    }

    WeakPtr& operator=(WeakPtr<T>&& other) {
        auto copy = other;
        swap(copy);
        return *this;
    }

    template<typename U>
    WeakPtr& operator=(WeakPtr<U>&& other) {
        auto copy = other;
        swap(copy);
        return *this;
    }

    bool expired() const {
        return block->shared_count == 0;
    }

    SharedPtr<T> lock() const {
        return expired() ? SharedPtr<T>() : SharedPtr<T>(*this);
    }

    ~WeakPtr() {
        if (block == nullptr) return;
        block->weak_count--;
        if (!block->shared_count && !block->weak_count) {
            block->deallocate_block();
            block = nullptr;
        }
    }

};

template<typename T, typename Allocator = std::allocator<T>, typename... Args>
SharedPtr<T> allocateShared(const Allocator& alloc = Allocator(), Args&& ... args) {
    return SharedPtr<T>(alloc, std::forward<Args>(args)...);
}

template<typename T, typename... Args>
SharedPtr<T> makeShared(Args&& ... args) {
    return allocateShared<T>(std::allocator<T>(), std::forward<Args>(args)...);
}