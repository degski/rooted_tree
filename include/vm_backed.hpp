
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
#        define _AMD64_ // srw_lock
#    endif
#    include <windef.h>
#    include <WinBase.h>
#    include <Memoryapi.h>

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
#include <set>
#include <stdexcept>
#include <thread>
#include <type_traits>
#include <utility>

#include <tbb/spin_mutex.h>

#include <hedley.hpp>

#if ( defined( __clang__ ) or defined( __GNUC__ ) ) and not defined( _MSC_VER )
#    include <deque>
#else
#    include <boost/container/deque.hpp>
#endif

namespace sax {

namespace detail ::vm_vector {

template<typename T>
#if ( defined( __clang__ ) or defined( __GNUC__ ) ) and not defined( _MSC_VER )
using deque = std::deque<T>;
#else
using deque = boost::container::deque<T>;
#endif

} // namespace detail::vm_vector

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
        m_end{ m_begin }, m_committed_b{ 0 } {
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
        m_committed_b = rc;
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
            m_committed_b   = 0;
        }
    }

    [[nodiscard]] constexpr size_type capacity ( ) const noexcept { return Capacity; }
    [[nodiscard]] size_type size ( ) const noexcept {
        return reinterpret_cast<value_type *> ( m_end ) - reinterpret_cast<value_type *> ( m_begin );
    }
    [[nodiscard]] constexpr size_type max_size ( ) const noexcept { return capacity ( ); }

    template<typename... Args>
    [[maybe_unused]] reference emplace_back ( Args &&... value_ ) {
        if ( HEDLEY_UNLIKELY ( size_b ( ) == m_committed_b ) ) {
            size_type cib = std::min ( m_committed_b ? grow ( m_committed_b ) : alloc_page_size_b, capacity_b ( ) );
            if ( HEDLEY_UNLIKELY ( not VirtualAlloc ( m_end, cib - m_committed_b, MEM_COMMIT, PAGE_READWRITE ) ) )
                throw std::bad_alloc ( );
            m_committed_b = cib;
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
    size_type m_committed_b;
};

namespace detail ::vm_vector {

template<typename Data>
struct vm_epilog : public Data {
    tbb::spin_mutex lock;
    tbb::atomic<char const> atom;
    template<typename... Args>
    vm_epilog ( Args &&... args_ ) : Data{ std::forward<Args> ( args_ )... }, atom{ 1 } { };
};

struct srw_lock final {

    srw_lock ( ) noexcept             = default;
    srw_lock ( srw_lock && ) noexcept = default;
    ~srw_lock ( )                     = default;
    srw_lock & operator= ( srw_lock && ) noexcept = default;

    srw_lock ( srw_lock const & ) = delete;
    srw_lock & operator= ( srw_lock const & ) = delete;

    void lock ( ) noexcept { AcquireSRWLockExclusive ( &m_handle ); }
    [[nodiscard]] bool try_lock ( ) noexcept { return 0 != TryAcquireSRWLockExclusive ( &m_handle ); }
    void unlock ( ) noexcept { ReleaseSRWLockExclusive ( &m_handle ); } // Look at this...

    void lock ( ) const noexcept { AcquireSRWLockShared ( &m_handle ); }
    [[nodiscard]] bool try_lock ( ) const noexcept { return 0 != TryAcquireSRWLockShared ( &m_handle ); }
    void unlock ( ) const noexcept { ReleaseSRWLockShared ( &m_handle ); }

    private:
    mutable SRWLOCK m_handle = SRWLOCK_INIT;
};

} // namespace detail::vm_vector

template<typename ValueType, std::size_t Capacity>
struct vm_concurrent_vector {

    using is_windows = std::integral_constant<bool, static_cast<bool> ( _MSC_VER )>;

    static constexpr std::size_t thread_reserve_size = 16;

    using value_type = detail::vm_vector::vm_epilog<ValueType>;

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

    using mutex = detail::vm_vector::srw_lock;

    struct thread_data {
        pointer begin = nullptr, end = nullptr;
    };

    using thread_data_deque         = detail::vm_vector::deque<thread_data>;
    using thread_data_deque_vector  = std::vector<thread_data_deque>;
    using instance_data_map         = std::map<vm_concurrent_vector *, thread_data_deque>;
    using instance_data_map_kv_pair = typename instance_data_map::value_type;

    // Virtual memory.

    [[nodiscard]] pointer vm_reserve ( std::size_t size_ ) {
        if constexpr ( is_windows::value )
            return reinterpret_cast<pointer> ( VirtualAlloc ( nullptr, size_, MEM_RESERVE, PAGE_READWRITE ) );
        else
            ;
    }

    HEDLEY_NEVER_INLINE void vm_allocate ( std::size_t size_ ) {
        if constexpr ( is_windows::value ) {
            if ( HEDLEY_UNLIKELY ( not VirtualAlloc ( m_begin + m_committed_b, size_, MEM_COMMIT, PAGE_READWRITE ) ) )
                throw std::bad_alloc ( );
        }
        else {
            ;
        }
        m_committed_b += size_;
    }

    void vm_free ( ) noexcept {
        if constexpr ( is_windows::value )
            VirtualFree ( m_begin, 0, MEM_RELEASE );
        else
            ;
        m_committed_b = 0;
    }

    // Constructors.

    vm_concurrent_vector ( ) :
        m_thread_data_deque{ make_thread_data_deque ( ) }, m_begin{ vm_reserve ( capacity_b ( ) ) }, m_end{ m_begin },
        m_committed_b{ 0 } {
        if ( HEDLEY_UNLIKELY ( not m_begin ) )
            throw std::bad_alloc ( );
    };

    vm_concurrent_vector ( std::initializer_list<ValueType> il_ ) : vm_concurrent_vector{ } {
        vm_allocate ( alloc_page_size_b );
        for ( ValueType & v : il_ )
            *m_end++ = value_type{ v };
    }

    explicit vm_concurrent_vector ( size_type s_, value_type const & v_ = value_type{ } ) : vm_concurrent_vector{ } {
        vm_allocate ( round_up_to_alloc_page_size_b ( s_ * sizeof ( value_type ) ) );
        for ( pointer e = m_begin + std::min ( s_, capacity ( ) ); m_end < e; ++m_end )
            new ( m_end ) value_type{ v_ };
    }

    ~vm_concurrent_vector ( ) {
        destruct_thread_data_deque ( );
        if constexpr ( not std::is_trivial<value_type>::value )
            for ( value_type & v : *this )
                v.~value_type ( );
        if ( HEDLEY_LIKELY ( m_begin ) ) {
            vm_free ( );
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
        thread_data & tl = get_thread_data ( );
        if ( tl.begin == tl.end ) {
            {
                std::lock_guard lock ( m_end_mutex );
                if ( HEDLEY_PREDICT ( ( size_b ( ) + thread_reserve_size ) >= m_committed_b, false,
                                      1.0 - static_cast<double> ( sizeof ( value_type ) ) /
                                                static_cast<double> ( alloc_page_size_b ) ) )
                    vm_allocate ( alloc_page_size_b );
                tl.begin = m_end;
                m_end += thread_reserve_size;
            }
            tl.end = tl.begin + thread_reserve_size;
        }
        return *new ( tl.begin++ ) value_type{ std::forward<Args> ( value_ )... };
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

    [[nodiscard]] constexpr std::size_t round_up_to_alloc_page_size_b ( std::size_t n_ ) const noexcept {
        return ( ( n_ + alloc_page_size_b - 1 ) / alloc_page_size_b ) * alloc_page_size_b;
    }

    [[nodiscard]] constexpr std::size_t capacity_b ( ) const noexcept {
        return round_up_to_alloc_page_size_b ( Capacity * sizeof ( value_type ) );
    }
    [[nodiscard]] std::size_t size_b ( ) const noexcept {
        return static_cast<std::size_t> ( reinterpret_cast<char *> ( m_end ) - reinterpret_cast<char *> ( m_begin ) );
    }

    static mutex s_instance_mutex, s_thread_mutex;
    static instance_data_map s_instance;
    static thread_data_deque_vector s_freelist;

    [[nodiscard]] thread_data_deque & make_thread_data_deque ( ) {
        std::pair this_thread_data_deque = { this, thread_data_deque{} };
        std::lock_guard lock ( s_instance_mutex );
        return insert_instance_this_deque ( std::move ( this_thread_data_deque ) );
    }

    void destruct_thread_data_deque ( ) noexcept {
        std::lock_guard lock ( s_instance_mutex );
        s_freelist.emplace_back ( );
        auto it = s_instance.find ( this );
        std::swap ( s_freelist.back ( ), it->second );
        s_instance.erase ( it );
    }

    [[nodiscard]] thread_data & make_thread_data ( ) const {
        std::lock_guard lock ( s_thread_mutex );
        return m_thread_data_deque.emplace_back ( );
    }

    [[nodiscard]] thread_data & get_thread_data ( ) const {
        static thread_local thread_data & data = make_thread_data ( );
        return data;
    }

    [[nodiscard]] HEDLEY_NEVER_INLINE thread_data_deque &
    insert_instance_this_deque ( instance_data_map_kv_pair && this_thread_data_deque_ ) {
        if ( s_freelist.size ( ) ) {
            thread_data_deque & tdd = s_instance.insert ( { this, std::move ( s_freelist.back ( ) ) } ).first->second;
            s_freelist.pop_back ( );
            return tdd;
        }
        else {
            return s_instance.insert ( std::move ( this_thread_data_deque_ ) ).first->second;
        }
    }

    thread_data_deque & m_thread_data_deque;

    pointer m_begin, m_end;
    std::size_t m_committed_b;
    mutex m_end_mutex;
};

template<typename ValueType, std::size_t Capacity>
typename vm_concurrent_vector<ValueType, Capacity>::mutex vm_concurrent_vector<ValueType, Capacity>::s_instance_mutex;

template<typename ValueType, std::size_t Capacity>
typename vm_concurrent_vector<ValueType, Capacity>::mutex vm_concurrent_vector<ValueType, Capacity>::s_thread_mutex;

template<typename ValueType, std::size_t Capacity>
typename vm_concurrent_vector<ValueType, Capacity>::instance_data_map vm_concurrent_vector<ValueType, Capacity>::s_instance;

template<typename ValueType, std::size_t Capacity>
typename vm_concurrent_vector<ValueType, Capacity>::thread_data_deque_vector vm_concurrent_vector<ValueType, Capacity>::s_freelist;

} // namespace sax
