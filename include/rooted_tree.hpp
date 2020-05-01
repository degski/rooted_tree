
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

#define USE_IO true
#define USE_CEREAL false

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstdlib>

#if USE_IO
#    include <iostream>
#endif

#include <limits>
#include <utility>
#include <vector>

#if ( defined( __clang__ ) or defined( __GNUC__ ) )
#    pragma GCC diagnostic push
#    pragma GCC diagnostic ignored "-Wmicrosoft-enum-value"
#endif
#ifndef TBB_SUPPRESS_DEPRECATED_MESSAGES
#    define UNDEF_TBB_SUPPRESS_DEPRECATED_MESSAGES
#    define TBB_SUPPRESS_DEPRECATED_MESSAGES 1
#endif
#include <tbb/tbb.h>
#if defined( UNDEF_TBB_SUPPRESS_DEPRECATED_MESSAGES )
#    undef TBB_SUPPRESS_DEPRECATED_MESSAGES
#    undef UNDEF_TBB_SUPPRESS_DEPRECATED_MESSAGES
#endif
#if ( defined( __clang__ ) or defined( __GNUC__ ) )
#    pragma GCC diagnostic pop
#endif

#if ( defined( __clang__ ) or defined( __GNUC__ ) ) and not defined( _MSC_VER )
#    include <deque>
#else
#    include <boost/container/deque.hpp>
#endif

#if USE_CEREAL
#    include <cereal/cereal.hpp>
#    include <cereal/types/vector.hpp>
// https://github.com/degski/cereal/blob/master/include/cereal/types/tbb_concurrent_vector.hpp
#    include <cereal/types/tbb_concurrent_vector.hpp>
#endif

namespace sax {

// Node-Id.
struct nid {

    int id;

    nid ( ) noexcept {} // id uninitialized.
    constexpr explicit nid ( int && value_ ) noexcept : id{ std::move ( value_ ) } {}
    constexpr explicit nid ( int const & value_ ) noexcept : id{ value_ } {}

    [[nodiscard]] bool operator== ( nid const rhs_ ) const noexcept { return id == rhs_.id; }
    [[nodiscard]] bool operator!= ( nid const rhs_ ) const noexcept { return id != rhs_.id; }

    [[nodiscard]] bool is_valid ( ) const noexcept { return id; }
    [[nodiscard]] bool is_invalid ( ) const noexcept { return not is_valid ( ); }

    [[nodiscard]] nid operator++ ( ) noexcept { return nid{ ++id }; }
    [[nodiscard]] nid operator++ ( int ) noexcept { return nid{ id++ }; }

#if USE_IO
    template<typename Stream>
    [[maybe_unused]] friend Stream & operator<< ( Stream & out_, nid const id_ ) noexcept {
        if ( nid_invalid_v == id_.id ) {
            if constexpr ( std::is_same<typename Stream::char_type, wchar_t>::value ) {
                out_ << L'*';
            }
            else {
                out_ << '*';
            }
        }
        else {
            out_ << id_.id;
        }
        return out_;
    }
#endif

    static constexpr int nid_invalid_v = 0;

#if USE_CEREAL
    private:
    friend class cereal::access;
    template<class Archive>
    inline void serialize ( Archive & ar_ ) {
        ar_ ( id );
    }
#endif
};

namespace detail {

template<typename T>
using zeroing_vector = std::vector<T, tbb::zero_allocator<T>>;
using id_vector      = zeroing_vector<nid>;

#if ( defined( __clang__ ) or defined( __GNUC__ ) ) and not defined( _MSC_VER )
using id_deque = std::deque<nid, tbb::tbb_allocator<nid>>;
#else
using id_deque = boost::container::deque<nid, tbb::tbb_allocator<nid>>;
#endif

// De-queue.
[[nodiscard]] nid de ( id_deque & d_ ) noexcept {
    assert ( d_.size ( ) );
    nid v = d_.front ( );
    d_.pop_front ( );
    return v;
}

// En-queue.
template<typename T>
[[maybe_unused]] auto en ( id_deque & d_, T const & v_ ) {
    return d_.push_back ( v_ );
}

inline constexpr int reserve_size = 1'024;

// Hooks.

template<bool>
struct rooted_tree_base_hook {};

template<>
struct rooted_tree_base_hook<false> { // 16 bytes.
    nid up = nid{ 0 }, prev = nid{ 0 }, tail = nid{ 0 };
    int fan = 0; // 0 <= fan-out < 2'147'483'648.

#if USE_CEREAL
    private:
    friend class cereal::access;
    template<class Archive>
    inline void serialize ( Archive & ar_ ) {
        ar_ ( up, prev, tail, fan );
    }
#endif
};

template<>
struct rooted_tree_base_hook<true> { // 16 bytes.
    nid up, prev, tail;
    short fan; // 0 <= fan-out < 32'768.
    tbb::spin_mutex mutex;
    tbb::atomic<char const> done = 1; // Indicates node is constructed (allocated with zeroed memory).

#if USE_CEREAL
    private:
    friend class cereal::access;
    template<class Archive>
    inline void serialize ( Archive & ar_ ) {
        ar_ ( up, prev, tail, fan );
    }
#endif
};

// The tree has 1 (one, and only one,) root.
template<typename Node, bool Concurrent = false>
struct rooted_tree_base {

    using hook = rooted_tree_base_hook<Concurrent>;

    private:
    using node_vector = std::conditional_t<Concurrent, tbb::concurrent_vector<Node, tbb::zero_allocator<Node>>, std::vector<Node>>;

    public:
    struct dummy_mutex final {
        dummy_mutex ( ) noexcept                = default;
        dummy_mutex ( dummy_mutex const & )     = delete;
        dummy_mutex ( dummy_mutex && ) noexcept = delete;

        dummy_mutex & operator= ( dummy_mutex const & ) = delete;
        dummy_mutex & operator= ( dummy_mutex && ) noexcept = delete;

        void lock ( ) noexcept { return; };
        bool try_lock ( ) noexcept { return true; };
        void unlock ( ) noexcept { return; };
    };

    struct dummy_scoped_lock final {
        dummy_scoped_lock ( ) noexcept                      = delete;
        dummy_scoped_lock ( dummy_scoped_lock const & )     = delete;
        dummy_scoped_lock ( dummy_scoped_lock && ) noexcept = default;
        template<typename... Args>
        dummy_scoped_lock ( Args &&... ) noexcept {}

        dummy_scoped_lock & operator= ( dummy_scoped_lock const & ) = delete;
        dummy_scoped_lock & operator= ( dummy_scoped_lock && ) noexcept = default;
    };

    using mutex           = std::conditional_t<Concurrent, tbb::spin_mutex, dummy_mutex>;
    using scoped_lock     = std::conditional_t<Concurrent, tbb::spin_mutex::scoped_lock, dummy_scoped_lock>;
    using size_type       = int;
    using difference_type = int;
    using value_type      = typename node_vector::value_type;
    using reference       = typename node_vector::reference;
    using pointer         = typename node_vector::pointer;
    using iterator        = typename node_vector::iterator;
    using const_reference = typename node_vector::const_reference;
    using const_pointer   = typename node_vector::const_pointer;
    using const_iterator  = typename node_vector::const_iterator;

    rooted_tree_base ( ) {
        nodes.reserve ( reserve_size );
        // Create invalid-node (node 0 (zero)).
        if constexpr ( Concurrent ) {
            nodes.grow_by ( 1 );
        }
        else {
            nodes.emplace_back ( );
        }
    }

    template<typename... Args>
    rooted_tree_base ( Args &&... args_ ) : rooted_tree_base ( ) {
        // Emplace a root-node.
        emplace ( invalid, std::forward<Args> ( args_ )... );
    }

    [[nodiscard]] const_pointer data ( ) const noexcept { return nodes.data ( ); }
    [[nodiscard]] pointer data ( ) noexcept { return nodes.data ( ); }

    [[nodiscard]] const_iterator begin ( ) const noexcept { return nodes.begin ( ); }
    [[nodiscard]] const_iterator cbegin ( ) const noexcept { return nodes.cbegin ( ); }
    [[nodiscard]] iterator begin ( ) noexcept { return nodes.begin ( ); }

    [[nodiscard]] const_iterator end ( ) const noexcept { return nodes.end ( ); }
    [[nodiscard]] const_iterator cend ( ) const noexcept { return nodes.cend ( ); }
    [[nodiscard]] iterator end ( ) noexcept { return nodes.end ( ); }

    [[nodiscard]] value_type & operator[] ( nid node_ ) noexcept {
        return nodes[ static_cast<typename node_vector::size_type> ( node_.id ) ];
    }
    [[nodiscard]] value_type const & operator[] ( nid node_ ) const noexcept {
        return nodes[ static_cast<typename node_vector::size_type> ( node_.id ) ];
    }

    [[nodiscard]] value_type & operator[] ( size_type node_ ) noexcept { return nodes[ node_ ]; }
    [[nodiscard]] value_type const & operator[] ( size_type node_ ) const noexcept { return nodes[ node_ ]; }

    void reserve ( size_type c_ ) { nodes.reserve ( static_cast<typename node_vector::size_type> ( c_ ) ); }

    // Insert a node (add a child to a parent). Insert the root-node by passing 'invalid' as parameter to source_ (once).
    [[maybe_unused]] nid insert ( nid source_, value_type && node_ ) noexcept {
        if constexpr ( Concurrent ) {
            iterator target = nodes.push_back ( std::move ( node_ ) );
            nid id{ static_cast<size_type> ( std::distance ( begin ( ), target ) ) };
            target->up.id       = source_.id;
            value_type & source = nodes[ source_.id ];
            {
                scoped_lock lock ( source.mutex );
                target->prev = std::exchange ( source.tail, id );
                source.fan += 1;
            }
            return id;
        }
        else {
            nid id{ static_cast<size_type> ( nodes.size ( ) ) };
            nodes.push_back ( std::move ( node_ ) );
            value_type & target = nodes.back ( );
            target.up           = source_;
            value_type & source = nodes[ source_.id ];
            target.prev         = std::exchange ( source.tail, id );
            source.fan += 1;
            return id;
        }
    }
    // Insert a node (add a child to a parent). Insert the root-node by passing 'invalid' as parameter to source_ (once).
    [[maybe_unused]] nid insert ( nid source_, value_type const & node_ ) noexcept {
        if constexpr ( Concurrent ) {
            iterator target = nodes.push_back ( node_ );
            nid id{ static_cast<size_type> ( std::distance ( begin ( ), target ) ) };
            target->up.id       = source_.id;
            value_type & source = nodes[ source_.id ];
            {
                scoped_lock lock ( source.mutex );
                target->prev = std::exchange ( source.tail, id );
                source.fan += 1;
            }
            return id;
        }
        else {
            nid id{ static_cast<size_type> ( nodes.size ( ) ) };
            nodes.push_back ( node_ );
            value_type & target = nodes.back ( );
            target.up           = source_;
            value_type & source = nodes[ source_.id ];
            target.prev         = std::exchange ( source.tail, id );
            source.fan += 1;
            return id;
        }
    }

    // Emplace a node (add a child to a parent). Emplace the root-node by passing 'invalid' as parameter to source_ (once).
    template<typename... Args>
    [[maybe_unused]] nid emplace ( nid source_, Args &&... args_ ) noexcept {
        if constexpr ( Concurrent ) {
            iterator target = nodes.emplace_back ( std::forward<Args> ( args_ )... );
            nid id{ static_cast<size_type> ( std::distance ( begin ( ), target ) ) };
            target->up.id       = source_.id;
            value_type & source = nodes[ source_.id ];
            {
                scoped_lock lock ( source.mutex );
                target->prev = std::exchange ( source.tail, id );
                source.fan += 1;
            }
            return id;
        }
        else {
            nid id{ static_cast<size_type> ( nodes.size ( ) ) };
            value_type & target = nodes.emplace_back ( std::forward<Args> ( args_ )... );
            target.up           = source_;
            value_type & source = nodes[ source_.id ];
            target.prev         = std::exchange ( source.tail, id );
            source.fan += 1;
            return id;
        }
    }

    // It is safe to destroy the node the interator is pointing at. Note: level_iterator is a (rather) heavy object.
    class level_iterator {

        friend struct rooted_tree_base;

        rooted_tree_base & tree;
        id_deque queue;
        size_type max_depth, depth, count;
        nid parent;

        public:
        level_iterator ( rooted_tree_base & tree_, size_type max_depth_ = 0, nid node_ = rooted_tree_base::root ) noexcept :
            tree{ tree_ }, max_depth{ max_depth_ }, parent{ node_ } {
            if ( ( not max_depth ) or ( max_depth > 1 ) )
                for ( nid child = tree[ parent.id ].tail; child.is_valid ( ); child = tree[ child.id ].prev )
                    en ( queue, child );
            count = static_cast<size_type> ( queue.size ( ) );
            depth = 1 + static_cast<size_type> ( 0 != count );
        }

        [[maybe_unused]] level_iterator & operator++ ( ) noexcept {
            if ( queue.size ( ) ) {
                if ( not count ) {
                    count = static_cast<size_type> ( queue.size ( ) );
                    if ( ( not max_depth ) or ( max_depth > 1 ) ) {
                        if ( max_depth == depth++ ) {
                            parent = rooted_tree_base::invalid;
                            return *this;
                        }
                    }
                }
                parent = de ( queue );
                count -= 1;
                for ( nid child = tree[ parent.id ].tail; child.is_valid ( ); child = tree[ child.id ].prev )
                    en ( queue, child );
                return *this;
            }
            else {
                parent = rooted_tree_base::invalid;
                return *this;
            }
        }

        [[nodiscard]] reference operator* ( ) const noexcept { return tree[ parent.id ]; }
        [[nodiscard]] pointer operator-> ( ) const noexcept { return tree.data ( ) + parent.id; }

        [[nodiscard]] bool is_valid ( ) const noexcept { return parent.is_valid ( ); }
        [[nodiscard]] nid id ( ) const noexcept { return parent; }
    };

    // It is safe to destroy the node the interator is pointing at. Note: level_iterator is a (rather) heavy object.
    class const_level_iterator {

        friend struct rooted_tree_base;

        rooted_tree_base const & tree;
        id_deque queue;
        size_type max_depth, depth, count;
        nid parent;

        public:
        const_level_iterator ( rooted_tree_base const & tree_, size_type max_depth_ = 0,
                               nid node_ = rooted_tree_base::root ) noexcept :
            tree{ tree_ },
            max_depth{ max_depth_ }, parent{ node_ } {
            if ( ( not max_depth ) or ( max_depth > 1 ) )
                for ( nid child = tree[ parent.id ].tail; child.is_valid ( ); child = tree[ child.id ].prev )
                    en ( queue, child );
            count = static_cast<size_type> ( queue.size ( ) );
            depth = 1 + static_cast<size_type> ( 0 != count );
        }

        [[maybe_unused]] const_level_iterator & operator++ ( ) noexcept {
            if ( queue.size ( ) ) {
                if ( not count ) {
                    count = static_cast<size_type> ( queue.size ( ) );
                    if ( ( not max_depth ) or ( max_depth > 1 ) ) {
                        if ( max_depth == depth++ ) {
                            parent = rooted_tree_base::invalid;
                            return *this;
                        }
                    }
                }
                parent = de ( queue );
                count -= 1;
                for ( nid child = tree[ parent.id ].tail; child.is_valid ( ); child = tree[ child.id ].prev )
                    en ( queue, child );
                return *this;
            }
            else {
                parent = rooted_tree_base::invalid;
                return *this;
            }
        }

        [[nodiscard]] const_reference operator* ( ) const noexcept { return tree[ parent.id ]; }
        [[nodiscard]] const_pointer operator-> ( ) const noexcept { return tree.data ( ) + parent.id; }

        [[nodiscard]] bool is_valid ( ) const noexcept { return parent.is_valid ( ); }
        [[nodiscard]] nid id ( ) const noexcept { return parent; }
    };

    class out_iterator {

        friend struct rooted_tree_base;

        rooted_tree_base & tree;
        nid node;

        public:
        out_iterator ( rooted_tree_base & tree_, nid node_ ) noexcept : tree{ tree_ }, node{ tree[ node_.id ].tail } {}

        [[maybe_unused]] out_iterator & operator++ ( ) noexcept {
            node = tree[ node.id ].prev;
            return *this;
        }

        [[nodiscard]] reference operator* ( ) const noexcept { return tree[ node.id ]; }
        [[nodiscard]] pointer operator-> ( ) const noexcept { return tree.data ( ) + node.id; }

        [[nodiscard]] bool is_valid ( ) const noexcept { return node.is_valid ( ); }
        [[nodiscard]] nid id ( ) const noexcept { return node; }
    };

    class const_out_iterator {

        friend struct rooted_tree_base;

        rooted_tree_base const & tree;
        nid node;

        public:
        const_out_iterator ( rooted_tree_base const & tree_, nid node_ ) noexcept : tree{ tree_ }, node{ tree[ node_.id ].tail } {}

        [[maybe_unused]] const_out_iterator & operator++ ( ) noexcept {
            node = tree[ node.id ].prev;
            return *this;
        }

        [[nodiscard]] const_reference operator* ( ) const noexcept { return tree[ node.id ]; }
        [[nodiscard]] const_pointer operator-> ( ) const noexcept { return tree.data ( ) + node.id; }

        [[nodiscard]] bool is_valid ( ) const noexcept { return node.is_valid ( ); }
        [[nodiscard]] nid id ( ) const noexcept { return node; }
    };

    class up_iterator {

        friend struct rooted_tree_base;

        rooted_tree_base & tree;
        nid node;

        public:
        up_iterator ( rooted_tree_base & tree_, nid node_ ) noexcept : tree{ tree_ }, node{ node_ } {}

        [[maybe_unused]] up_iterator & operator++ ( ) noexcept {
            node = tree[ node.id ].up;
            return *this;
        }

        [[nodiscard]] reference operator* ( ) const noexcept { return tree[ node.id ]; }
        [[nodiscard]] pointer operator-> ( ) const noexcept { return tree.data ( ) + node.id; }

        [[nodiscard]] bool is_valid ( ) const noexcept { return node.is_valid ( ); }
        [[nodiscard]] nid id ( ) const noexcept { return node; }
    };

    class const_up_iterator {

        friend struct rooted_tree_base;

        rooted_tree_base const & tree;
        nid node;

        public:
        const_up_iterator ( rooted_tree_base const & tree_, nid node_ ) noexcept : tree{ tree_ }, node{ node_ } {}

        [[maybe_unused]] const_up_iterator & operator++ ( ) noexcept {
            node = tree[ node.id ].up;
            return *this;
        }

        [[nodiscard]] const_reference operator* ( ) const noexcept { return tree[ node.id ]; }
        [[nodiscard]] const_pointer operator-> ( ) const noexcept { return tree.data ( ) + node.id; }

        [[nodiscard]] bool is_valid ( ) const noexcept { return node.is_valid ( ); }
        [[nodiscard]] nid id ( ) const noexcept { return node; }
    };

    // The (maximum) depth (or height) is the number of nodes along the longest path from the (by default
    // root-node) node down to the farthest leaf node.
    [[nodiscard]] size_type height ( nid root_ = root ) const {
        id_deque queue ( 1, root_ );
        size_type depth = 0, count = 0;
        while ( ( count = static_cast<size_type> ( queue.size ( ) ) ) ) {
            while ( count-- ) {
                nid parent = de ( queue );
                for ( nid child = nodes[ parent.id ].tail; child.is_valid ( ); child = nodes[ child.id ].prev )
                    en ( queue, child );
            }
            depth += 1;
        }
        return depth;
    }

    node_vector nodes;

    [[nodiscard]] static constexpr std::size_t max_fan_out ( ) noexcept {
        return std::numeric_limits<decltype ( hook::fan )>::max ( );
    }

    static constexpr nid invalid = nid{ 0 }, root = nid{ 1 };

#if USE_CEREAL
    private:
    friend class cereal::access;
    template<class Archive>
    inline void serialize ( Archive & ar_ ) {
        ar_ ( nodes );
    }
#endif
}; // namespace detail

} // namespace detail

template<typename Node>
using rooted_tree      = detail::rooted_tree_base<Node, false>;
using rooted_tree_hook = detail::rooted_tree_base_hook<false>;

template<typename Node>
using concurrent_rooted_tree      = detail::rooted_tree_base<Node, true>;
using concurrent_rooted_tree_hook = detail::rooted_tree_base_hook<true>;

} // namespace sax

#undef USE_CEREAL
#undef USE_IO
