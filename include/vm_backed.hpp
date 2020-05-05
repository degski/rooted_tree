
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

#ifndef NOMINMAX
#    define NOMINMAX
#endif

#ifndef _AMD64_
#    define _AMD64_ // srw_lock
#endif
#include <windef.h>
#include <WinBase.h>
#include <Memoryapi.h>

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstdlib>

#include <algorithm>
#include <initializer_list>
#include <memory>
#include <mutex>
#include <new>
#include <stdexcept>
#include <type_traits>
#include <utility>

#include <tbb/concurrent_vector.h>
#include <tbb/spin_mutex.h>

#include <hedley.hpp>

#include <atomic>

namespace sax {

struct spin_lock final {

    spin_lock ( ) noexcept {}
    spin_lock ( const spin_lock & ) = delete;
    spin_lock & operator= ( const spin_lock & ) = delete;

    void lock ( ) noexcept {
        while ( flag.test_and_set ( std::memory_order_acquire ) )
            ;
    }
    [[nodiscard]] bool try_lock ( ) noexcept { return not( flag.test_and_set ( std::memory_order_acquire ) ); }
    void unlock ( ) noexcept { flag.clear ( std::memory_order_release ); }

    private:
    std::atomic_flag flag = ATOMIC_FLAG_INIT;
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

template<typename ValueType, typename SizeType, SizeType Capacity>
struct vm_vector {

    using value_type = ValueType;

    using pointer       = value_type *;
    using const_pointer = value_type const *;

    using reference       = value_type &;
    using const_reference = value_type const &;
    using rv_reference    = value_type &&;

    using size_type       = SizeType;
    using difference_type = std::make_signed<size_type>;

    using iterator               = pointer;
    using const_iterator         = const_pointer;
    using reverse_iterator       = pointer;
    using const_reverse_iterator = const_pointer;

    vm_vector ( ) :
        m_begin{ reinterpret_cast<pointer> ( VirtualAlloc ( nullptr, capacity_b ( ), MEM_RESERVE, PAGE_READWRITE ) ) },
        m_end{ m_begin }, m_committed_b{ 0u } {
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
            m_committed_b   = 0u;
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
            size_type cib = std::min ( m_committed_b ? grow ( m_committed_b ) : allocation_page_size_b, capacity_b ( ) );
            if ( HEDLEY_UNLIKELY ( not VirtualAlloc ( m_end, cib - m_committed_b, MEM_COMMIT, PAGE_READWRITE ) ) )
                throw std::bad_alloc ( );
            m_committed_b = cib;
        }
        return *new ( m_end++ ) value_type{ std::forward<Args> ( value_ )... };
    }
    [[maybe_unused]] reference push_back ( const_reference value_ ) { return emplace_back ( value_type{ value_ } ); }

    void pop_back ( ) noexcept {
        assert ( size ( ) );
        if constexpr ( not std::is_trivial<value_type>::value )
            back ( ).~value_type ( );
        --m_end;
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
    static constexpr size_type page_size_b            = static_cast<size_type> ( 65'536 );         // 64KB
    static constexpr size_type allocation_page_size_b = static_cast<size_type> ( 1'600 * 65'536 ); // 100MB

    [[nodiscard]] size_type required_b ( size_type const & r_ ) const noexcept {
        std::size_t req = r_ * sizeof ( value_type );
        return req % page_size_b ? ( ( req + page_size_b ) / page_size_b ) * page_size_b : req;
    }
    [[nodiscard]] constexpr size_type capacity_b ( ) const noexcept {
        constexpr std::size_t cap = Capacity * sizeof ( value_type );
        return cap % page_size_b ? ( ( static_cast<size_type> ( cap ) + page_size_b ) / page_size_b ) * page_size_b
                                 : static_cast<size_type> ( cap );
    }
    [[nodiscard]] size_type size_b ( ) const noexcept {
        return static_cast<size_type> ( reinterpret_cast<char *> ( m_end ) - reinterpret_cast<char *> ( m_begin ) );
    }

    [[nodiscard]] static size_type grow ( size_type const & cap_b_ ) noexcept { return cap_b_ + allocation_page_size_b; }
    [[nodiscard]] static size_type shrink ( size_type const & cap_b_ ) noexcept { return cap_b_ - allocation_page_size_b; }

    pointer m_begin, m_end;
    size_type m_committed_b;
};

template<typename Data>
struct vm_epilog : public Data {
    tbb::spin_mutex lock;
    tbb::atomic<char> atom;
    template<typename... Args>
    vm_epilog ( Args &&... args_ ) : Data{ std::forward<Args> ( args_ )... }, atom{ 1 } { };
};

template<typename ValueType, typename SizeType, SizeType Capacity>
struct vm_concurrent_vector {

    using value_type = vm_epilog<ValueType>;

    using pointer       = value_type *;
    using const_pointer = value_type const *;

    using reference       = value_type &;
    using const_reference = value_type const &;
    using rv_reference    = value_type &&;

    using size_type       = SizeType;
    using difference_type = std::make_signed<size_type>;

    using iterator               = pointer;
    using const_iterator         = const_pointer;
    using reverse_iterator       = pointer;
    using const_reverse_iterator = const_pointer;

    using mutex       = sax::srw_lock;
    using scoped_lock = std::scoped_lock;

    vm_concurrent_vector ( ) :
        m_begin{ reinterpret_cast<pointer> ( VirtualAlloc ( nullptr, capacity_b ( ), MEM_RESERVE, PAGE_READWRITE ) ) },
        m_end{ m_begin }, m_committed_b{ 0u } {
        if ( HEDLEY_UNLIKELY ( not m_begin ) )
            throw std::bad_alloc ( );
    };

    vm_concurrent_vector ( std::initializer_list<value_type> il_ ) : vm_concurrent_vector{ } { // no!!
        for ( value_type const & v : il_ )
            push_back ( v );
    }

    explicit vm_concurrent_vector ( size_type const s_, value_type const & v_ ) : vm_concurrent_vector{ } {
        size_type rc = required_b ( s_ );
        if ( HEDLEY_UNLIKELY ( not VirtualAlloc ( m_end, rc, MEM_COMMIT, PAGE_READWRITE ) ) )
            throw std::bad_alloc ( );
        m_committed_b = rc;
        for ( pointer e = m_begin + std::min ( s_, capacity ( ) ); m_end < e; ++m_end )
            new ( m_end ) value_type{ v_ };
    }

    ~vm_concurrent_vector ( ) {
        if constexpr ( not std::is_trivial<value_type>::value ) {
            for ( value_type & v : *this )
                v.~value_type ( );
        }
        if ( HEDLEY_LIKELY ( m_begin ) ) {
            VirtualFree ( m_begin, capacity_b ( ), MEM_RELEASE );
            m_end = m_begin = nullptr;
            m_committed_b   = 0u;
        }
    }

    [[nodiscard]] constexpr size_type capacity ( ) const noexcept { return Capacity; }
    [[nodiscard]] size_type size ( ) const noexcept {
        return reinterpret_cast<value_type *> ( m_end ) - reinterpret_cast<value_type *> ( m_begin );
    }
    [[nodiscard]] constexpr size_type max_size ( ) const noexcept { return capacity ( ); }

    template<typename... Args>
    [[maybe_unused]] reference emplace_back ( Args &&... value_ ) {
        pointer p;
        {
            std::scoped_lock lock ( m_mutex );
            if ( HEDLEY_UNLIKELY ( size_b ( ) == m_committed_b ) ) {
                size_type cib = std::min ( m_committed_b ? grow ( m_committed_b ) : allocation_page_size_b, capacity_b ( ) );
                if ( HEDLEY_UNLIKELY ( not VirtualAlloc ( m_end, cib - m_committed_b, MEM_COMMIT, PAGE_READWRITE ) ) )
                    throw std::bad_alloc ( );
                m_committed_b = cib;
            }
            p = m_end++;
        }
        return *new ( p ) value_type{ std::forward<Args> ( value_ )... };
    }
    [[maybe_unused]] reference push_back ( const_reference value_ ) { return emplace_back ( value_type{ value_ } ); }

    void pop_back ( ) noexcept {
        assert ( size ( ) );
        if constexpr ( not std::is_trivial<value_type>::value )
            back ( ).~value_type ( );
        --m_end;
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
        while ( not element_->atom ) // await construction.
            std::this_thread::yield ( );
    }
    void await_construction ( const_reference element_ ) const noexcept { await_construction ( std::addressof ( element_ ) ); }
    void await_construction ( size_type element_ ) const noexcept { await_construction ( data ( ) + element_ ); }

    private:
    static constexpr size_type page_size_b            = static_cast<size_type> ( 65'536 );         // 64KB
    static constexpr size_type allocation_page_size_b = static_cast<size_type> ( 1'600 * 65'536 ); // 100MB

    [[nodiscard]] size_type required_b ( size_type const & r_ ) const noexcept {
        std::size_t req = r_ * sizeof ( value_type );
        return req % page_size_b ? ( ( req + page_size_b ) / page_size_b ) * page_size_b : req;
    }
    [[nodiscard]] constexpr size_type capacity_b ( ) const noexcept {
        constexpr std::size_t cap = Capacity * sizeof ( value_type );
        return cap % page_size_b ? ( ( static_cast<size_type> ( cap ) + page_size_b ) / page_size_b ) * page_size_b
                                 : static_cast<size_type> ( cap );
    }
    [[nodiscard]] size_type size_b ( ) const noexcept {
        return static_cast<size_type> ( reinterpret_cast<char *> ( m_end ) - reinterpret_cast<char *> ( m_begin ) );
    }

    [[nodiscard]] static size_type grow ( size_type const & cap_b_ ) noexcept { return cap_b_ + allocation_page_size_b; }
    [[nodiscard]] static size_type shrink ( size_type const & cap_b_ ) noexcept { return cap_b_ - allocation_page_size_b; }

    pointer m_begin, m_end;
    mutex m_mutex;
    size_type m_committed_b;
};

} // namespace sax
