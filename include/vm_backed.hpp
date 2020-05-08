
// MIT License
//
// Copyright (c) 2020 degski
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in all
// copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.

#pragma once

#if defined( _MSC_VER )

#    ifndef NOMINMAX
#        define NOMINMAX
#    endif

#    ifndef _AMD64_
#        define _AMD64_
#    endif

#    ifndef WIN32_LEAN_AND_MEAN
#        define WIN32_LEAN_AND_MEAN_DEFINED
#        define WIN32_LEAN_AND_MEAN
#    endif

#    include <windef.h>
#    include <WinBase.h>
#    include <immintrin.h>

#    ifdef WIN32_LEAN_AND_MEAN_DEFINED
#        undef WIN32_LEAN_AND_MEAN_DEFINED
#        undef WIN32_LEAN_AND_MEAN
#    endif

#else

#    include <sys/mman.h>

#endif

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstdlib>

#include <atomic>
#include <algorithm>
#include <initializer_list>
#include <limits>
#include <map>
#include <memory>
#include <mutex>
#include <new>
#include <stdexcept>
#include <thread>
#include <type_traits>
#include <utility>
#include <vector>

#include <plf/plf_colony.h>

#define VM_VECTOR_USE_HEDLEY 1

#if VM_VECTOR_USE_HEDLEY
#    include <hedley.h>
#else
#    define HEDLEY_LIKELY( expr ) ( !!( expr ) )
#    define HEDLEY_UNLIKELY( expr ) ( !!( expr ) )
#    define HEDLEY_PREDICT( expr, res, perc ) ( !!( expr ) )
#    define HEDLEY_NEVER_INLINE
#    define HEDLEY_PURE
#endif

namespace sax { // sax

namespace detail { // sax::detail

HEDLEY_ALWAYS_INLINE void cpu_pause ( ) noexcept {
#if ( defined( __clang__ ) or defined( __GNUC__ ) )
    asm( "pause" );
#else
    _mm_pause ( );
#endif
}

// Returns rounded-up multiple of n.
[[nodiscard]] inline constexpr std::size_t round_multiple ( std::size_t n_, std::size_t multiple_ ) noexcept {
    return ( ( n_ + multiple_ - 1 ) / multiple_ ) * multiple_;
}

namespace vm_vector { // sax::detail::vm_vector

template<typename Pointer>
struct vm {

    [[nodiscard]] Pointer reserve ( std::size_t size_ ) {
#if defined( _MSC_VER )
        return reinterpret_cast<Pointer> ( VirtualAlloc ( nullptr, size_, MEM_RESERVE, PAGE_READWRITE ) );
#else
        return reinterpret_cast<Pointer> ( mmap ( nullptr, size_, PROT_NONE, MAP_ANONYMOUS | MAP_PRIVATE, -1, 0 ) );
#endif
    }

    HEDLEY_NEVER_INLINE void allocate ( void * const pointer_, std::size_t size_ ) {
#if defined( _MSC_VER )
        if ( HEDLEY_UNLIKELY (
                 not VirtualAlloc ( reinterpret_cast<char *> ( pointer_ ) + committed, size_, MEM_COMMIT, PAGE_READWRITE ) ) )
            throw std::bad_alloc ( );
#else
        mmap ( reinterpret_cast<char *> ( pointer_ ) + committed, size_, PROT_READ | PROT_WRITE, MAP_ANONYMOUS | MAP_PRIVATE, -1,
               0 );
#endif
        committed += size_;
    }

    void free ( void * const pointer_, std::size_t size_ ) noexcept {
#if defined( _MSC_VER )
        VirtualFree ( pointer_, 0, MEM_RELEASE );
#else
        munmap ( pointer_, size_ );
#endif
        committed = 0;
    }

    std::size_t committed = 0;
};

struct srw_lock final {

    srw_lock ( ) noexcept             = default;
    srw_lock ( srw_lock const & )     = delete;
    srw_lock ( srw_lock && ) noexcept = delete;
    ~srw_lock ( ) noexcept            = default;

    srw_lock & operator= ( srw_lock const & ) = delete;
    srw_lock & operator= ( srw_lock && ) noexcept = delete;

    // read and write.

    HEDLEY_ALWAYS_INLINE void lock ( ) noexcept { AcquireSRWLockExclusive ( std::addressof ( handle ) ); }
    [[nodiscard]] HEDLEY_ALWAYS_INLINE bool try_lock ( ) noexcept {
        return 0 != TryAcquireSRWLockExclusive ( std::addressof ( handle ) );
    }
    HEDLEY_ALWAYS_INLINE void unlock ( ) noexcept { ReleaseSRWLockExclusive ( std::addressof ( handle ) ); }

    // read.

    HEDLEY_ALWAYS_INLINE void lock ( ) const noexcept {
        AcquireSRWLockShared ( const_cast<PSRWLOCK> ( std::addressof ( handle ) ) );
    }
    [[nodiscard]] HEDLEY_ALWAYS_INLINE bool try_lock ( ) const noexcept {
        return 0 != TryAcquireSRWLockShared ( const_cast<PSRWLOCK> ( std::addressof ( handle ) ) );
    }
    HEDLEY_ALWAYS_INLINE void unlock ( ) const noexcept {
        ReleaseSRWLockShared ( const_cast<PSRWLOCK> ( std::addressof ( handle ) ) );
    }

    private:
    SRWLOCK handle = SRWLOCK_INIT;
};

struct tas_spin_lock final {

    tas_spin_lock ( ) noexcept                  = default;
    tas_spin_lock ( tas_spin_lock const & )     = delete;
    tas_spin_lock ( tas_spin_lock && ) noexcept = delete;
    ~tas_spin_lock ( ) noexcept                 = default;

    tas_spin_lock & operator= ( tas_spin_lock const & ) = delete;
    tas_spin_lock & operator= ( tas_spin_lock && ) noexcept = delete;

    HEDLEY_ALWAYS_INLINE void lock ( ) noexcept {
        while ( flag.exchange ( 1, std::memory_order_acquire ) )
            cpu_pause ( );
    }
    [[nodiscard]] HEDLEY_ALWAYS_INLINE bool try_lock ( ) noexcept { return not flag.exchange ( 1, std::memory_order_acquire ); }
    HEDLEY_ALWAYS_INLINE void unlock ( ) noexcept { flag.store ( 0, std::memory_order_release ); }

    private:
    std::atomic<char> flag = { 0 };
};

template<typename AlignedData>
struct vm_epilog : public AlignedData {
    tas_spin_lock lock;
    std::atomic<char> atom;
    template<typename... Args>
    vm_epilog ( Args &&... args_ ) : AlignedData{ std::forward<Args> ( args_ )... }, atom{ 1 } { };
};

template<typename Data>
struct /* alignas ( 16 ) */ vm_aligner : public Data {
    template<typename... Args>
    vm_aligner ( Args &&... args_ ) : Data{ std::forward<Args> ( args_ )... } { };
};

} // namespace vm_vector
} // namespace detail

template<typename ValueType, std::size_t Capacity>
struct vm_concurrent_vector {

    using is_windows = std::integral_constant<bool, static_cast<bool> ( _MSC_VER )>; // ?

    static constexpr std::size_t thread_reserve_size = 32;

    using value_type = detail::vm_vector::vm_epilog<detail::vm_vector::vm_aligner<ValueType>>;

    using pointer       = value_type *;
    using const_pointer = value_type const *;

    using reference       = value_type &;
    using const_reference = value_type const &;
    using rv_reference    = value_type &&;

    using size_type       = std::size_t;
    using difference_type = std::make_signed<size_type>;

    using iterator               = pointer;
    using const_iterator         = const_pointer;
    using reverse_iterator       = pointer;
    using const_reverse_iterator = const_pointer;

    // using mutex = detail::vm_vector::srw_lock;
    using mutex = detail::vm_vector::tas_spin_lock;

    using vm = detail::vm_vector::vm<pointer>;

    struct thread_local_data {
        pointer begin = nullptr;
    };

    using thread_local_data_colony        = plf::colony<thread_local_data>;
    using thread_local_data_colony_vector = std::vector<thread_local_data_colony>;
    using thread_local_data_map           = std::map<vm_concurrent_vector * const, thread_local_data_colony>;
    using thread_local_data_map_kv_pair   = typename thread_local_data_map::value_type;

    vm_concurrent_vector ( ) :
        m_thread_local_data_colony{ make_colony ( ) }, m_vm{ }, m_begin{ m_vm.reserve ( capacity_b ( ) ) }, m_end{ m_begin } {
        if ( HEDLEY_UNLIKELY ( not m_begin ) )
            throw std::bad_alloc ( );
    };

    vm_concurrent_vector ( std::initializer_list<ValueType> il_ ) : vm_concurrent_vector{ } {
        grow_allocated_by ( alloc_page_size_b );
        for ( ValueType & v : il_ )
            *m_end++ = value_type{ v };
    }

    explicit vm_concurrent_vector ( size_type s_, value_type const & v_ = value_type{ } ) : vm_concurrent_vector{ } {
        grow_allocated_by ( round_alloc_page_size_b ( s_ * sizeof ( value_type ) ) );
        for ( pointer e = m_begin + std::min ( s_, capacity ( ) ); m_end < e; ++m_end )
            new ( m_end ) value_type{ v_ };
    }

    ~vm_concurrent_vector ( ) {
        recycle_colony ( );
        if constexpr ( not std::is_trivial<value_type>::value )
            for ( value_type & v : *this )
                v.~value_type ( );
        if ( HEDLEY_LIKELY ( m_begin ) ) {
            m_vm.free ( m_begin, capacity_b ( ) );
            m_end = m_begin = nullptr;
        }
    }

    [[nodiscard]] constexpr size_type capacity ( ) const noexcept { return capacity_b ( ) / sizeof ( value_type ); }
    [[nodiscard]] size_type size ( ) const noexcept {
        return static_cast<std::size_t> ( reinterpret_cast<value_type *> ( m_end ) - reinterpret_cast<value_type *> ( m_begin ) );
    }
    [[nodiscard]] constexpr size_type max_size ( ) const noexcept { return capacity ( ); }

    template<typename... Args>
    [[maybe_unused]] reference emplace_back ( Args &&... value_ ) {

        constexpr std::size_t thread_reserve_size_b = thread_reserve_size * sizeof ( value_type );

        auto next_end = [ = ] ( pointer p ) {
            return reinterpret_cast<pointer> (
                ( ( reinterpret_cast<std::size_t> ( p ) + thread_reserve_size_b ) / thread_reserve_size_b ) *
                thread_reserve_size_b );
        };

        thread_local_data & tld = get_thread_local_data ( );
        if ( not static_cast<bool> ( reinterpret_cast<std::size_t> ( tld.begin ) & ( thread_reserve_size_b - 1 ) ) ) {
            std::lock_guard lock ( m_end_mutex );
            m_end = next_end ( ( tld.begin = m_end ) );
            if ( HEDLEY_PREDICT ( m_end >= m_begin + m_vm.committed, false,
                                  1.0 -
                                      static_cast<double> ( sizeof ( value_type ) ) / static_cast<double> ( alloc_page_size_b ) ) )
                grow_allocated_by ( alloc_page_size_b );
        }
        return *new ( tld.begin++ ) value_type{ std::forward<Args> ( value_ )... };
    }

    [[maybe_unused]] reference push_back ( const_reference value_ ) { return emplace_back ( value_type{ value_ } ); }
    [[maybe_unused]] reference push_back ( rv_reference value_ ) { return emplace_back ( std::move ( value_ ) ); }

    void pop_back ( ) noexcept {
        {
            std::lock_guard lock ( m_end_mutex );
            assert ( size ( ) );
            m_end--;
        }
        if constexpr ( not std::is_trivial<value_type>::value )
            m_end->~value_type ( );
    }

    [[nodiscard]] const_pointer data ( ) const noexcept { return m_begin; }
    [[nodiscard]] pointer data ( ) noexcept { return const_cast<pointer> ( std::as_const ( *this ).data ( ) ); }

    [[nodiscard]] const_iterator begin ( ) const noexcept { return m_begin; }
    [[nodiscard]] const_iterator cbegin ( ) const noexcept { return begin ( ); }
    [[nodiscard]] iterator begin ( ) noexcept { return const_cast<iterator> ( std::as_const ( *this ).begin ( ) ); }

    [[nodiscard]] const_iterator end ( ) const noexcept { return m_end; }
    [[nodiscard]] const_iterator cend ( ) const noexcept { return end ( ); }
    [[nodiscard]] iterator end ( ) noexcept { return const_cast<iterator> ( std::as_const ( *this ).end ( ) ); }

    [[nodiscard]] const_iterator rbegin ( ) const noexcept { return m_end - 1; }
    [[nodiscard]] const_iterator crbegin ( ) const noexcept { return rbegin ( ); }
    [[nodiscard]] iterator rbegin ( ) noexcept { return const_cast<iterator> ( std::as_const ( *this ).rbegin ( ) ); }

    [[nodiscard]] const_iterator rend ( ) const noexcept { return m_begin - 1; }
    [[nodiscard]] const_iterator crend ( ) const noexcept { return rend ( ); }
    [[nodiscard]] iterator rend ( ) noexcept { return const_cast<iterator> ( std::as_const ( *this ).rend ( ) ); }

    [[nodiscard]] const_reference front ( ) const noexcept { return *begin ( ); }
    [[nodiscard]] reference front ( ) noexcept { return const_cast<reference> ( std::as_const ( *this ).front ( ) ); }

    [[nodiscard]] const_reference back ( ) const noexcept { return *rbegin ( ); }
    [[nodiscard]] reference back ( ) noexcept { return const_cast<reference> ( std::as_const ( *this ).back ( ) ); }

    [[nodiscard]] const_reference at ( size_type const i_ ) const {
        if constexpr ( std::is_signed<size_type>::value ) {
            if ( HEDLEY_LIKELY ( 0 <= i_ and i_ < size ( ) ) )
                return m_begin[ i_ ];
            else
                throw std::runtime_error ( "index out of bounds" );
        }
        else {
            if ( HEDLEY_LIKELY ( i_ < size ( ) ) )
                return m_begin[ i_ ];
            else
                throw std::runtime_error ( "index out of bounds" );
        }
    }
    [[nodiscard]] reference at ( size_type const i_ ) { return const_cast<reference> ( std::as_const ( *this ).at ( i_ ) ); }

    [[nodiscard]] const_reference operator[] ( size_type const i_ ) const noexcept {
        if constexpr ( std::is_signed<size_type>::value )
            assert ( 0 <= i_ and i_ < size ( ) );
        else
            assert ( i_ < size ( ) );
        return m_begin[ i_ ];
    }
    [[nodiscard]] reference operator[] ( size_type const i_ ) noexcept {
        return const_cast<reference> ( std::as_const ( *this ).operator[] ( i_ ) );
    }

    void await_construction ( const_iterator element_ ) const noexcept {
        while ( HEDLEY_UNLIKELY ( not element_->atom ) ) // await construction.
            std::this_thread::yield ( );
    }
    void await_construction ( const_reference element_ ) const noexcept { await_construction ( std::addressof ( element_ ) ); }
    void await_construction ( size_type element_ ) const noexcept { await_construction ( data ( ) + element_ ); }

    // private:
    static constexpr std::size_t alloc_page_size_b = static_cast<std::size_t> ( 1'024 * 65'536 ); // 64MB

    // round up!
    [[nodiscard]] constexpr std::size_t round_alloc_page_size_b ( std::size_t n_ ) const noexcept {
        return detail::round_multiple ( n_, alloc_page_size_b );
    }

    [[nodiscard]] constexpr std::size_t capacity_b ( ) const noexcept {
        return round_alloc_page_size_b ( Capacity * sizeof ( value_type ) );
    }
    [[nodiscard]] std::size_t size_b ( ) const noexcept {
        return static_cast<std::size_t> ( reinterpret_cast<char *> ( m_end ) - reinterpret_cast<char *> ( m_begin ) );
    }

    alignas ( 64 ) static mutex s_this_map_mutex;
    alignas ( 64 ) static mutex s_thread_mutex;

    static thread_local_data_map s_this_map;
    static thread_local_data_colony_vector s_freelist;

    [[nodiscard]] thread_local_data_colony & make_colony ( ) {
        std::pair this_thread_local_data_colony = { this, thread_local_data_colony{} };
        std::lock_guard lock ( s_this_map_mutex );
        return insert_this ( std::move ( this_thread_local_data_colony ) );
    }

    void recycle_colony ( ) noexcept {
        std::lock_guard lock ( s_this_map_mutex );
        s_freelist.emplace_back ( );
        auto it = s_this_map.find ( this );
        std::swap ( s_freelist.back ( ), it->second );
        s_freelist.back ( ).clear ( );
        s_this_map.erase ( it );
    }

    [[nodiscard]] thread_local_data & make_thread_local_data ( ) { // non-const.
        std::lock_guard lock ( s_thread_mutex );
        return *m_thread_local_data_colony.emplace ( );
    }

    [[nodiscard]] thread_local_data & get_thread_local_data ( ) { // non-const.
        static thread_local thread_local_data & this_object_thread_local_data = make_thread_local_data ( );
        return this_object_thread_local_data;
    }

    [[nodiscard]] HEDLEY_NEVER_INLINE thread_local_data_colony &
    insert_this ( thread_local_data_map_kv_pair && this_thread_local_data_ ) {
        if ( s_freelist.size ( ) ) {
            thread_local_data_colony & tdd = s_this_map.insert ( { this, std::move ( s_freelist.back ( ) ) } ).first->second;
            s_freelist.pop_back ( );
            return tdd;
        }
        else {
            return s_this_map.insert ( std::move ( this_thread_local_data_ ) ).first->second;
        }
    }

    void grow_allocated_by ( std::size_t size_ ) { m_vm.allocate ( m_begin, size_ ); }

    thread_local_data_colony & m_thread_local_data_colony;
    vm m_vm;
    pointer m_begin, m_end;
    alignas ( 64 ) mutex m_end_mutex;
}; // namespace sax

template<typename ValueType, std::size_t Capacity>
alignas ( 64 )
    typename vm_concurrent_vector<ValueType, Capacity>::mutex vm_concurrent_vector<ValueType, Capacity>::s_this_map_mutex;
template<typename ValueType, std::size_t Capacity>
alignas ( 64 ) typename vm_concurrent_vector<ValueType, Capacity>::mutex vm_concurrent_vector<ValueType, Capacity>::s_thread_mutex;
template<typename ValueType, std::size_t Capacity>
typename vm_concurrent_vector<ValueType, Capacity>::thread_local_data_map vm_concurrent_vector<ValueType, Capacity>::s_this_map;
template<typename ValueType, std::size_t Capacity>
typename vm_concurrent_vector<ValueType, Capacity>::thread_local_data_colony_vector
    vm_concurrent_vector<ValueType, Capacity>::s_freelist;

template<typename ValueType, std::size_t Capacity>
struct vm_vector {

    using value_type = ValueType;

    using pointer       = value_type *;
    using const_pointer = value_type const *;

    using reference       = value_type &;
    using const_reference = value_type const &;
    using rv_reference    = value_type &&;

    using size_type       = std::size_t;
    using difference_type = std::make_signed<size_type>;

    using iterator               = pointer;
    using const_iterator         = const_pointer;
    using reverse_iterator       = pointer;
    using const_reverse_iterator = const_pointer;

    vm_vector ( ) :
        m_begin{ reinterpret_cast<pointer> ( VirtualAlloc ( nullptr, capacity_b ( ), MEM_RESERVE, PAGE_READWRITE ) ) },
        m_end{ m_begin }, committed{ 0 } {
        if ( HEDLEY_UNLIKELY ( not m_begin ) )
            throw std::bad_alloc ( );
    };

    vm_vector ( std::initializer_list<value_type> il_ ) : vm_vector{ } {
        for ( value_type const & v : il_ )
            push_back ( v );
    }

    explicit vm_vector ( size_type const s_, value_type const & v_ ) : vm_vector{ } {
        size_type rc = required_b ( s_ );
        if ( HEDLEY_UNLIKELY ( not VirtualAlloc ( m_end, rc, MEM_COMMIT, PAGE_READWRITE ) ) )
            throw std::bad_alloc ( );
        committed = rc;
        for ( pointer e = m_begin + std::min ( s_, capacity ( ) ); m_end < e; ++m_end )
            new ( m_end ) value_type{ v_ };
    }

    ~vm_vector ( ) {
        if constexpr ( not std::is_trivial<value_type>::value ) {
            for ( value_type & v : *this )
                v.~value_type ( );
        }
        if ( HEDLEY_LIKELY ( m_begin ) ) {
            VirtualFree ( m_begin, capacity_b ( ), MEM_RELEASE );
            m_end = m_begin = nullptr;
            committed       = 0;
        }
    }

    [[nodiscard]] constexpr size_type capacity ( ) const noexcept { return Capacity; }
    [[nodiscard]] size_type size ( ) const noexcept {
        return reinterpret_cast<value_type *> ( m_end ) - reinterpret_cast<value_type *> ( m_begin );
    }
    [[nodiscard]] constexpr size_type max_size ( ) const noexcept { return capacity ( ); }

    template<typename... Args>
    [[maybe_unused]] reference emplace_back ( Args &&... value_ ) {
        if ( HEDLEY_UNLIKELY ( size_b ( ) == committed ) ) {
            size_type cib = std::min ( committed ? grow ( committed ) : alloc_page_size_b, capacity_b ( ) );
            if ( HEDLEY_UNLIKELY ( not VirtualAlloc ( m_end, cib - committed, MEM_COMMIT, PAGE_READWRITE ) ) )
                throw std::bad_alloc ( );
            committed = cib;
        }
        return *new ( m_end++ ) value_type{ std::forward<Args> ( value_ )... };
    }
    [[maybe_unused]] reference push_back ( const_reference value_ ) { return emplace_back ( value_type{ value_ } ); }
    [[maybe_unused]] reference push_back ( rv_reference value_ ) { return emplace_back ( std::move ( value_ ) ); }

    void pop_back ( ) noexcept {
        assert ( size ( ) );
        m_end--;
        if constexpr ( not std::is_trivial<value_type>::value )
            m_end->~value_type ( );
    }

    [[nodiscard]] const_pointer data ( ) const noexcept { return m_begin; }
    [[nodiscard]] pointer data ( ) noexcept { return const_cast<pointer> ( std::as_const ( *this ).data ( ) ); }

    [[nodiscard]] const_iterator begin ( ) const noexcept { return m_begin; }
    [[nodiscard]] const_iterator cbegin ( ) const noexcept { return begin ( ); }
    [[nodiscard]] iterator begin ( ) noexcept { return const_cast<iterator> ( std::as_const ( *this ).begin ( ) ); }

    [[nodiscard]] const_iterator end ( ) const noexcept { return m_end; }
    [[nodiscard]] const_iterator cend ( ) const noexcept { return end ( ); }
    [[nodiscard]] iterator end ( ) noexcept { return const_cast<iterator> ( std::as_const ( *this ).end ( ) ); }

    [[nodiscard]] const_iterator rbegin ( ) const noexcept { return m_end - 1; }
    [[nodiscard]] const_iterator crbegin ( ) const noexcept { return rbegin ( ); }
    [[nodiscard]] iterator rbegin ( ) noexcept { return const_cast<iterator> ( std::as_const ( *this ).rbegin ( ) ); }

    [[nodiscard]] const_iterator rend ( ) const noexcept { return m_begin - 1; }
    [[nodiscard]] const_iterator crend ( ) const noexcept { return rend ( ); }
    [[nodiscard]] iterator rend ( ) noexcept { return const_cast<iterator> ( std::as_const ( *this ).rend ( ) ); }

    [[nodiscard]] const_reference front ( ) const noexcept { return *begin ( ); }
    [[nodiscard]] reference front ( ) noexcept { return const_cast<reference> ( std::as_const ( *this ).front ( ) ); }

    [[nodiscard]] const_reference back ( ) const noexcept { return *rbegin ( ); }
    [[nodiscard]] reference back ( ) noexcept { return const_cast<reference> ( std::as_const ( *this ).back ( ) ); }

    [[nodiscard]] const_reference at ( size_type const i_ ) const {
        if constexpr ( std::is_signed<size_type>::value ) {
            if ( HEDLEY_LIKELY ( 0 <= i_ and i_ < size ( ) ) )
                return m_begin[ i_ ];
            else
                throw std::runtime_error ( "index out of bounds" );
        }
        else {
            if ( HEDLEY_LIKELY ( i_ < size ( ) ) )
                return m_begin[ i_ ];
            else
                throw std::runtime_error ( "index out of bounds" );
        }
    }
    [[nodiscard]] reference at ( size_type const i_ ) { return const_cast<reference> ( std::as_const ( *this ).at ( i_ ) ); }

    [[nodiscard]] const_reference operator[] ( size_type const i_ ) const noexcept {
        if constexpr ( std::is_signed<size_type>::value )
            assert ( 0 <= i_ and i_ < size ( ) );
        else
            assert ( i_ < size ( ) );
        return m_begin[ i_ ];
    }
    [[nodiscard]] reference operator[] ( size_type const i_ ) noexcept {
        return const_cast<reference> ( std::as_const ( *this ).operator[] ( i_ ) );
    }

    private:
    static constexpr size_type os_vm_page_size_b = static_cast<size_type> ( 65'536 );         // 64KB
    static constexpr size_type alloc_page_size_b = static_cast<size_type> ( 1'600 * 65'536 ); // 100MB

    [[nodiscard]] size_type required_b ( size_type const & r_ ) const noexcept {
        std::size_t req = static_cast<std::size_t> ( r_ ) * sizeof ( value_type );
        return req % os_vm_page_size_b ? ( ( req + os_vm_page_size_b ) / os_vm_page_size_b ) * os_vm_page_size_b : req;
    }
    [[nodiscard]] constexpr size_type capacity_b ( ) const noexcept {
        constexpr std::size_t cap = static_cast<std::size_t> ( Capacity ) * sizeof ( value_type );
        return cap % os_vm_page_size_b ? ( ( cap + os_vm_page_size_b ) / os_vm_page_size_b ) * os_vm_page_size_b : cap;
    }
    [[nodiscard]] size_type size_b ( ) const noexcept {
        return static_cast<size_type> ( reinterpret_cast<char *> ( m_end ) - reinterpret_cast<char *> ( m_begin ) );
    }

    [[nodiscard]] HEDLEY_PURE size_type grow ( size_type const & cap_b_ ) const noexcept { return cap_b_ + alloc_page_size_b; }
    [[nodiscard]] HEDLEY_PURE size_type shrink ( size_type const & cap_b_ ) const noexcept { return cap_b_ - alloc_page_size_b; }

    pointer m_begin, m_end;
    size_type committed;
};

} // namespace sax
