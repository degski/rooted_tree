
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
#include <mutex>
#include <type_traits>
#include <utility>
#include <vector>

#if ( defined( __clang__ ) or defined( __GNUC__ ) ) and not defined( _MSC_VER )
#    include <deque>
#else
#    include <boost/container/deque.hpp>
#endif

#include <tbb/concurrent_vector.h> // tbb_config.h needs fixing to make this work with clang-cl.
#include <tbb/spin_mutex.h>

#if USE_CEREAL
#    include <cereal/cereal.hpp>
#    include <cereal/types/vector.hpp>
// https://github.com/degski/cereal/blob/master/include/cereal/types/tbb_concurrent_vector.hpp
#    include <cereal/types/tbb_concurrent_vector.hpp>
#endif

namespace sax {

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
[[nodiscard]] nid de ( id_deque & deq_ ) noexcept {
    assert ( deq_.size ( ) );
    nid v = deq_.front ( );
    deq_.pop_front ( );
    return v;
}

// En-queue.
template<typename T>
[[maybe_unused]] auto en ( id_deque & deq_, T const & v_ ) {
    return deq_.push_back ( v_ );
}

// Pop stack.
[[nodiscard]] nid pop ( id_vector & vec_ ) noexcept {
    assert ( vec_.size ( ) );
    nid v = vec_.back ( );
    vec_.pop_back ( );
    return v;
}

// Push stack.
template<typename T>
[[maybe_unused]] auto push ( id_vector & vec_, T const & v_ ) {
    return vec_.push_back ( v_ );
}

inline constexpr int reserve_size = 1'024;

// Hooks.

struct rooted_tree_hook { // 16 bytes.
    nid up = nid{ 0 }, prev = nid{ 0 }, tail = nid{ 0 };
    int fan = 0; // 0 <= fan-out < 2'147'483'648.

#if USE_IO
    template<typename Stream>
    [[maybe_unused]] friend Stream & operator<< ( Stream & out_, rooted_tree_hook const & nid_ ) noexcept {
        if constexpr ( std::is_same<typename Stream::char_type, wchar_t>::value ) {
            out_ << L'<' << nid_.up << L' ' << nid_.prev << L' ' << nid_.tail << L' ' << nid_.fan << L'>';
        }
        else {
            out_ << '<' << nid_.up << ' ' << nid_.prev << ' ' << nid_.tail << ' ' << nid_.fan << '>';
        }
        return out_;
    }
#endif

#if USE_CEREAL
    private:
    friend class cereal::access;
    template<class Archive>
    inline void serialize ( Archive & ar_ ) {
        ar_ ( up, prev, tail, fan );
    }
#endif
};

template<typename Node>
struct rooted_tree_node_mutex : public Node { // 2 bytes.
    tbb::spin_mutex lock;
    tbb::atomic<char> done = 1; // Indicates node is constructed (allocated with zeroed memory).
    template<typename... Args>
    rooted_tree_node_mutex ( Args &&... args_ ) : Node{ std::forward<Args> ( args_ )... } { };
};

// The rooted tree has 1 root.
template<typename Node, bool Concurrent = false>
struct rooted_tree_base {

    using is_concurrent = std::integral_constant<bool, Concurrent>;

    using value_type = std::conditional_t<is_concurrent::value, rooted_tree_node_mutex<Node>, Node>;

    private:
    using data = std::conditional_t<is_concurrent::value, tbb::concurrent_vector<value_type, tbb::zero_allocator<value_type>>,
                                    std::vector<value_type>>;

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

    using mutex           = std::conditional_t<is_concurrent::value, tbb::spin_mutex, dummy_mutex>;
    using scoped_lock     = std::conditional_t<is_concurrent::value, tbb::spin_mutex::scoped_lock, dummy_scoped_lock>;
    using size_type       = int;
    using difference_type = int;
    using reference       = typename data::reference;
    using pointer         = typename data::pointer;
    using iterator        = typename data::iterator;
    using const_reference = typename data::const_reference;
    using const_pointer   = typename data::const_pointer;
    using const_iterator  = typename data::const_iterator;

    rooted_tree_base ( ) {
        nodes.reserve ( reserve_size );
        if constexpr ( is_concurrent::value )
            nodes.grow_by ( 1 );
        else
            nodes.emplace_back ( );
    }

    template<typename... Args>
    rooted_tree_base ( Args &&... args_ ) : rooted_tree_base ( ) {
        emplace ( invalid, std::forward<Args> ( args_ )... ); // emplace a root-node.
    }

    [[nodiscard]] const_iterator begin ( ) const noexcept { return nodes.begin ( ); }
    [[nodiscard]] const_iterator cbegin ( ) const noexcept { return nodes.cbegin ( ); }
    [[nodiscard]] iterator begin ( ) noexcept { return nodes.begin ( ); }
    [[nodiscard]] const_iterator end ( ) const noexcept { return nodes.end ( ); }
    [[nodiscard]] const_iterator cend ( ) const noexcept { return nodes.cend ( ); }
    [[nodiscard]] iterator end ( ) noexcept { return nodes.end ( ); }
    [[nodiscard]] value_type & operator[] ( nid nid_ ) noexcept { return nodes[ nid_.id ]; }
    [[nodiscard]] value_type const & operator[] ( nid nid_ ) const noexcept { return nodes[ nid_.id ]; }
    [[nodiscard]] value_type & operator[] ( size_type nid_ ) noexcept { return nodes[ nid_ ]; }
    [[nodiscard]] value_type const & operator[] ( size_type nid_ ) const noexcept { return nodes[ nid_ ]; }

    // Not safe/concurrent.
    void reserve ( size_type c_ ) { nodes.reserve ( c_ ); }
    // Not safe/concurrent.
    void clear ( ) { nodes.clear ( ); }
    // Not safe/concurrent.
    void swap ( rooted_tree_base & rhs_ ) noexcept { nodes.swap ( rhs_.nodes ); }

    template<typename This = is_concurrent>
    std::enable_if_t<This::value> lock ( ) noexcept {
        nodes[ invalid.id ].lock.lock ( );
    };
    template<typename This = is_concurrent>
    [[nodiscard]] std::enable_if_t<This::value, bool> try_lock ( ) noexcept {
        return nodes[ invalid.id ].lock.try_lock ( );
    };
    template<typename This = is_concurrent>
    std::enable_if_t<This::value> unlock ( ) noexcept {
        nodes[ invalid.id ].lock.unlock ( );
    };

    // Add a child (-node) to a parent. Add root-node by passing 'invalid' as parameter to pid_.
    [[maybe_unused]] nid insert ( nid pid_, value_type && node_ ) noexcept {
        if constexpr ( is_concurrent::value ) {
            iterator cnode;
            nid cid;
            {
                scoped_lock lock ( nodes[ invalid.id ].lock );
                cnode = nodes.push_back ( std::move ( node_ ) );
                cid   = nid{ static_cast<size_type> ( std::distance ( begin ( ), cnode ) ) };
            }
            return insert_impl ( pid_, cnode, cid );
        }
        else {
            return insert_impl ( pid_, nodes.push_back ( std::move ( node_ ) ), nid{ static_cast<size_type> ( nodes.size ( ) ) } );
        }
    }
    // Add a child (-node) to a parent. Add root-node by passing 'invalid' as parameter to pid_.
    [[maybe_unused]] nid insert ( nid pid_, value_type const & node_ ) noexcept {
        if constexpr ( is_concurrent::value ) {
            iterator cnode;
            nid cid;
            {
                scoped_lock lock ( nodes[ invalid.id ].lock );
                cnode = nodes.push_back ( node_ );
                cid   = nid{ static_cast<size_type> ( std::distance ( begin ( ), cnode ) ) };
            }
            return insert_impl ( pid_, cnode, cid );
        }
        else {
            return insert_impl ( pid_, nodes.push_back ( node_ ), nid{ static_cast<size_type> ( nodes.size ( ) ) } );
        }
    }

    // Add a child (-node) to a parent. Add root-node by passing 'invalid' as parameter to pid_.
    template<typename... Args>
    [[maybe_unused]] nid emplace ( nid pid_, Args &&... args_ ) noexcept {
        if constexpr ( is_concurrent::value ) {
            iterator cnode;
            nid cid;
            {
                scoped_lock lock ( nodes[ invalid.id ].lock );
                cnode = nodes.emplace_back ( std::forward<Args> ( args_ )... );
                cid   = nid{ static_cast<size_type> ( std::distance ( begin ( ), cnode ) ) };
            }
            return insert_impl ( pid_, cnode, cid );
        }
        else {
            return insert_impl ( pid_, nodes.emplace_back ( std::forward<Args> ( args_ )... ),
                                 nid{ static_cast<size_type> ( nodes.size ( ) ) } );
        }
    }

    class internal_iterator {
        friend struct rooted_tree_base;
        rooted_tree_base & tree;
        id_vector stack;
        nid node;

        public:
        internal_iterator ( rooted_tree_base & tree_, nid nid_ = rooted_tree_base::root ) : tree{ tree_ } {
            if ( tree[ nid_.id ].fan ) {
                node = nid_;
                for ( nid child = tree[ node.id ].tail; child.is_valid ( ); child = tree[ child.id ].prev )
                    if ( tree[ child.id ].fan )
                        push ( stack, child );
            }
            else {
                node = rooted_tree_base::invalid;
            }
        }
        [[maybe_unused]] internal_iterator & operator++ ( ) {
            if ( stack.size ( ) ) {
                node = pop ( stack );
                for ( nid child = tree[ node.id ].tail; child.is_valid ( ); child = tree[ child.id ].prev )
                    if ( tree[ child.id ].fan )
                        push ( stack, child );
                return *this;
            }
            else {
                node = rooted_tree_base::invalid;
                return *this;
            }
        }
        [[nodiscard]] reference operator* ( ) const noexcept { return tree[ node.id ]; }
        [[nodiscard]] pointer operator-> ( ) const noexcept { return std::addressof ( tree[ node.id ] ); }
        [[nodiscard]] bool is_valid ( ) const noexcept { return node.is_valid ( ); }
        [[nodiscard]] nid id ( ) const noexcept { return node; }
    };

    class const_internal_iterator {
        friend struct rooted_tree_base;
        rooted_tree_base const & tree;
        id_vector stack;
        nid node;

        public:
        const_internal_iterator ( rooted_tree_base const & tree_, nid nid_ = rooted_tree_base::root ) : tree{ tree_ } {
            if ( tree[ nid_.id ].fan ) {
                node = nid_;
                for ( nid child = tree[ node.id ].tail; child.is_valid ( ); child = tree[ child.id ].prev )
                    if ( tree[ child.id ].fan )
                        push ( stack, child );
            }
            else {
                node = rooted_tree_base::invalid;
            }
        }
        [[maybe_unused]] const_internal_iterator & operator++ ( ) {
            if ( stack.size ( ) ) {
                node = pop ( stack );
                for ( nid child = tree[ node.id ].tail; child.is_valid ( ); child = tree[ child.id ].prev )
                    if ( tree[ child.id ].fan )
                        push ( stack, child );
                return *this;
            }
            else {
                node = rooted_tree_base::invalid;
                return *this;
            }
        }
        [[nodiscard]] const_reference operator* ( ) const noexcept { return tree[ node.id ]; }
        [[nodiscard]] const_pointer operator-> ( ) const noexcept { return std::addressof ( tree[ node.id ] ); }
        [[nodiscard]] bool is_valid ( ) const noexcept { return node.is_valid ( ); }
        [[nodiscard]] nid id ( ) const noexcept { return node; }
    };

    class leaf_iterator {
        friend struct rooted_tree_base;
        rooted_tree_base & tree;
        id_vector stack;
        nid node;

        public:
        leaf_iterator ( rooted_tree_base & tree_, nid nid_ = rooted_tree_base::root ) : tree{ tree_ } {
            for ( nid child = tree[ nid_.id ].tail; child.is_valid ( ); child = tree[ child.id ].prev )
                push ( stack, child );
            if ( stack.size ( ) )
                this->operator++ ( );
            else
                node = rooted_tree_base::invalid;
        }
        [[maybe_unused]] leaf_iterator & operator++ ( ) {
            while ( true ) {
                if ( stack.size ( ) ) {
                    node = pop ( stack );
                    if ( not tree[ node.id ].fan )
                        return *this;
                    for ( nid child = tree[ node.id ].tail; child.is_valid ( ); child = tree[ child.id ].prev )
                        push ( stack, child );
                }
                else {
                    node = rooted_tree_base::invalid;
                    return *this;
                }
            }
        }
        [[nodiscard]] reference operator* ( ) const noexcept { return tree[ node.id ]; }
        [[nodiscard]] pointer operator-> ( ) const noexcept { return std::addressof ( tree[ node.id ] ); }
        [[nodiscard]] bool is_valid ( ) const noexcept { return node.is_valid ( ); }
        [[nodiscard]] nid id ( ) const noexcept { return node; }
    };

    class const_leaf_iterator {
        friend struct rooted_tree_base;
        rooted_tree_base const & tree;
        id_vector stack;
        nid node;

        public:
        const_leaf_iterator ( rooted_tree_base const & tree_, nid nid_ = rooted_tree_base::root ) : tree{ tree_ } {
            for ( nid child = tree[ nid_.id ].tail; child.is_valid ( ); child = tree[ child.id ].prev )
                push ( stack, child );
            if ( stack.size ( ) )
                this->operator++ ( );
            else
                node = rooted_tree_base::invalid;
        }
        [[maybe_unused]] const_leaf_iterator & operator++ ( ) {
            while ( true ) {
                if ( stack.size ( ) ) {
                    node = pop ( stack );
                    if ( not tree[ node.id ].fan )
                        return *this;
                    for ( nid child = tree[ node.id ].tail; child.is_valid ( ); child = tree[ child.id ].prev )
                        push ( stack, child );
                }
                else {
                    node = rooted_tree_base::invalid;
                    return *this;
                }
            }
        }
        [[nodiscard]] const_reference operator* ( ) const noexcept { return tree[ node.id ]; }
        [[nodiscard]] const_pointer operator-> ( ) const noexcept { return std::addressof ( tree[ node.id ] ); }
        [[nodiscard]] bool is_valid ( ) const noexcept { return node.is_valid ( ); }
        [[nodiscard]] nid id ( ) const noexcept { return node; }
    };

    class depth_iterator {
        friend struct rooted_tree_base;
        rooted_tree_base & tree;
        id_vector stack;
        nid node;

        public:
        depth_iterator ( rooted_tree_base & tree_, nid nid_ = rooted_tree_base::root ) : tree{ tree_ }, node{ nid_ } {
            for ( nid child = tree[ node.id ].tail; child.is_valid ( ); child = tree[ child.id ].prev )
                push ( stack, child );
        }
        [[maybe_unused]] depth_iterator & operator++ ( ) {
            if ( stack.size ( ) ) {
                node = pop ( stack );
                for ( nid child = tree[ node.id ].tail; child.is_valid ( ); child = tree[ child.id ].prev )
                    push ( stack, child );
                return *this;
            }
            else {
                node = rooted_tree_base::invalid;
                return *this;
            }
        }
        [[nodiscard]] reference operator* ( ) const noexcept { return tree[ node.id ]; }
        [[nodiscard]] pointer operator-> ( ) const noexcept { return std::addressof ( tree[ node.id ] ); }
        [[nodiscard]] bool is_valid ( ) const noexcept { return node.is_valid ( ); }
        [[nodiscard]] nid id ( ) const noexcept { return node; }
    };

    class const_depth_iterator {
        friend struct rooted_tree_base;
        rooted_tree_base const & tree;
        id_vector stack;
        nid node;

        public:
        const_depth_iterator ( rooted_tree_base const & tree_, nid nid_ = rooted_tree_base::root ) : tree{ tree_ }, node{ nid_ } {
            for ( nid child = tree[ node.id ].tail; child.is_valid ( ); child = tree[ child.id ].prev )
                push ( stack, child );
        }
        [[maybe_unused]] const_depth_iterator & operator++ ( ) {
            if ( stack.size ( ) ) {
                node = pop ( stack );
                for ( nid child = tree[ node.id ].tail; child.is_valid ( ); child = tree[ child.id ].prev )
                    push ( stack, child );
                return *this;
            }
            else {
                node = rooted_tree_base::invalid;
                return *this;
            }
        }
        [[nodiscard]] const_reference operator* ( ) const noexcept { return tree[ node.id ]; }
        [[nodiscard]] const_pointer operator-> ( ) const noexcept { return std::addressof ( tree[ node.id ] ); }
        [[nodiscard]] bool is_valid ( ) const noexcept { return node.is_valid ( ); }
        [[nodiscard]] nid id ( ) const noexcept { return node; }
    };

    // It is safe to destroy the node the interator is pointing at. Note: breadth_iterator is a (rather) heavy object.
    class breadth_iterator {
        friend struct rooted_tree_base;
        rooted_tree_base & tree;
        id_deque queue;
        size_type max_depth, depth, count;
        nid parent;

        public:
        breadth_iterator ( rooted_tree_base & tree_, size_type max_depth_ = 0, nid nid_ = rooted_tree_base::root ) :
            tree{ tree_ }, max_depth{ max_depth_ }, parent{ nid_ } {
            if ( ( not max_depth ) or ( max_depth > 1 ) )
                for ( nid child = tree[ parent.id ].tail; child.is_valid ( ); child = tree[ child.id ].prev )
                    en ( queue, child );
            count = static_cast<size_type> ( queue.size ( ) );
            depth = 1 + static_cast<size_type> ( 0 != count );
        }
        [[maybe_unused]] breadth_iterator & operator++ ( ) {
            if ( size_type queue_size = static_cast<size_type> ( queue.size ( ) ); static_cast<bool> ( queue_size ) ) {
                if ( not count ) {
                    count = queue_size;
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
        [[nodiscard]] pointer operator-> ( ) const noexcept { return std::addressof ( tree[ parent.id ] ); }
        [[nodiscard]] bool is_valid ( ) const noexcept { return parent.is_valid ( ); }
        [[nodiscard]] nid id ( ) const noexcept { return parent; }
        [[nodiscard]] size_type height ( ) const noexcept { return depth; }
    };

    // It is safe to destroy the node the interator is pointing at. Note: breadth_iterator is a (rather) heavy object.
    class const_breadth_iterator {
        friend struct rooted_tree_base;
        rooted_tree_base const & tree;
        id_deque queue;
        size_type max_depth, depth, count;
        nid parent;

        public:
        const_breadth_iterator ( rooted_tree_base const & tree_, size_type max_depth_ = 0, nid nid_ = rooted_tree_base::root ) :
            tree{ tree_ }, max_depth{ max_depth_ }, parent{ nid_ } {
            if ( ( not max_depth ) or ( max_depth > 1 ) )
                for ( nid child = tree[ parent.id ].tail; child.is_valid ( ); child = tree[ child.id ].prev )
                    en ( queue, child );
            count = static_cast<size_type> ( queue.size ( ) );
            depth = 1 + static_cast<size_type> ( 0 != count );
        }
        [[maybe_unused]] const_breadth_iterator & operator++ ( ) {
            if ( size_type queue_size = static_cast<size_type> ( queue.size ( ) ); static_cast<bool> ( queue_size ) ) {
                if ( not count ) {
                    count = queue_size;
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
        [[nodiscard]] const_pointer operator-> ( ) const noexcept { return std::addressof ( tree[ parent.id ] ); }
        [[nodiscard]] bool is_valid ( ) const noexcept { return parent.is_valid ( ); }
        [[nodiscard]] nid id ( ) const noexcept { return parent; }
        [[nodiscard]] size_type height ( ) const noexcept { return depth; }
    };

    class out_iterator {
        friend struct rooted_tree_base;
        rooted_tree_base & tree;
        nid node;

        public:
        out_iterator ( rooted_tree_base & tree_, nid nid_ ) noexcept : tree{ tree_ }, node{ tree[ nid_.id ].tail } {}
        [[maybe_unused]] out_iterator & operator++ ( ) noexcept {
            node = tree[ node.id ].prev;
            return *this;
        }
        [[nodiscard]] reference operator* ( ) const noexcept { return tree[ node.id ]; }
        [[nodiscard]] pointer operator-> ( ) const noexcept { return std::addressof ( tree[ node.id ] ); }
        [[nodiscard]] bool is_valid ( ) const noexcept { return node.is_valid ( ); }
        [[nodiscard]] nid id ( ) const noexcept { return node; }
    };

    class const_out_iterator {
        friend struct rooted_tree_base;
        rooted_tree_base const & tree;
        nid node;

        public:
        const_out_iterator ( rooted_tree_base const & tree_, nid nid_ ) noexcept : tree{ tree_ }, node{ tree[ nid_.id ].tail } {}
        [[maybe_unused]] const_out_iterator & operator++ ( ) noexcept {
            node = tree[ node.id ].prev;
            return *this;
        }
        [[nodiscard]] const_reference operator* ( ) const noexcept { return tree[ node.id ]; }
        [[nodiscard]] const_pointer operator-> ( ) const noexcept { return std::addressof ( tree[ node.id ] ); }
        [[nodiscard]] bool is_valid ( ) const noexcept { return node.is_valid ( ); }
        [[nodiscard]] nid id ( ) const noexcept { return node; }
    };

    class up_iterator {
        friend struct rooted_tree_base;
        rooted_tree_base & tree;
        nid node;

        public:
        up_iterator ( rooted_tree_base & tree_, nid nid_ ) noexcept : tree{ tree_ }, node{ nid_ } {}
        [[maybe_unused]] up_iterator & operator++ ( ) noexcept {
            node = tree[ node.id ].up;
            return *this;
        }
        [[nodiscard]] reference operator* ( ) const noexcept { return tree[ node.id ]; }
        [[nodiscard]] pointer operator-> ( ) const noexcept { return std::addressof ( tree[ node.id ] ); }
        [[nodiscard]] bool is_valid ( ) const noexcept { return node.is_valid ( ); }
        [[nodiscard]] nid id ( ) const noexcept { return node; }
    };

    class const_up_iterator {
        friend struct rooted_tree_base;
        rooted_tree_base const & tree;
        nid node;

        public:
        const_up_iterator ( rooted_tree_base const & tree_, nid nid_ ) noexcept : tree{ tree_ }, node{ nid_ } {}
        [[maybe_unused]] const_up_iterator & operator++ ( ) noexcept {
            node = tree[ node.id ].up;
            return *this;
        }
        [[nodiscard]] const_reference operator* ( ) const noexcept { return tree[ node.id ]; }
        [[nodiscard]] const_pointer operator-> ( ) const noexcept { return std::addressof ( tree[ node.id ] ); }
        [[nodiscard]] bool is_valid ( ) const noexcept { return node.is_valid ( ); }
        [[nodiscard]] nid id ( ) const noexcept { return node; }
    };

    // The (maximum) depth (or height) is the number of nodes along the longest path from the (by default
    // root-node) node down to the farthest leaf node. It returns (optionally) the width_ through an out-pointer.
    [[nodiscard]] size_type height ( nid rid_ = root, size_type * width_ = nullptr ) const {
        id_deque queue ( 1, rid_ );
        size_type max_width = 0, depth = 0, count = 1;
        while ( count ) {
            while ( count-- ) {
                nid parent = de ( queue );
                for ( nid child = nodes[ parent.id ].tail; child.is_valid ( ); child = nodes[ child.id ].prev )
                    en ( queue, child );
            }
            count = static_cast<size_type> ( queue.size ( ) );
            if ( count > max_width )
                max_width = count;
            depth += 1;
        }
        if ( width_ )
            *width_ = max_width;
        return depth;
    }

    data nodes;

    static constexpr nid invalid = nid{ 0 }, root = nid{ 1 };

    private:
    template<typename AccessType>
    [[nodiscard]] nid insert_impl ( nid pid_, AccessType & cnode_, nid cid_ ) {
        assert ( invalid != pid_ or nodes[ invalid.id ].tail.is_invalid ( ) ); // no 2+ roots.
        if constexpr ( is_concurrent::value ) {
            while ( cid_.id >= nodes.size ( ) ) // await allocation.
                std::this_thread::yield ( );
            while ( not nodes[ cid_.id ].done ) // await construction.
                std::this_thread::yield ( );
            cnode_->up = pid_;
            {
                scoped_lock lock ( nodes[ pid_.id ].lock );
                cnode_->prev = std::exchange ( nodes[ pid_.id ].tail, cid_ );
                nodes[ pid_.id ].fan++;
            }
            return cid_;
        }
        else {
            cnode_.up   = pid_;
            cnode_.prev = std::exchange ( nodes[ pid_.id ].tail, cid_ );
            nodes[ pid_.id ].fan += 1;
            return cid_;
        }
    }

#if USE_CEREAL
    private:
    friend class cereal::access;
    template<class Archive>
    inline void serialize ( Archive & ar_ ) {
        ar_ ( nodes );
    }
#endif
};

} // namespace detail

using rooted_tree_hook = detail::rooted_tree_hook;
template<typename Node>
using rooted_tree = detail::rooted_tree_base<Node, false>;
template<typename Node>
using concurrent_rooted_tree = detail::rooted_tree_base<Node, true>;
} // namespace sax

#undef USE_CEREAL
#undef USE_IO
