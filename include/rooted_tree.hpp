
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

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstdlib>

#include <iostream>
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

#include <boost/container/deque.hpp>

#include <cereal/cereal.hpp>

#define ever                                                                                                                       \
    ;                                                                                                                              \
    ;

namespace sax {

namespace detail { // keep the macros out of the code (as it's ugly).
#ifdef _DEBUG
using is_debug   = std::true_type;
using is_release = std::false_type;
#else
using is_debug   = std::false_type;
using is_release = std::true_type;
#endif
} // namespace detail

struct nid {

    int id;

    private:
    friend struct crt_hook;
    nid ( ) noexcept {} // id uninitialized.

    public:
    constexpr explicit nid ( int && value_ ) noexcept : id{ std::move ( value_ ) } {}
    constexpr explicit nid ( int const & value_ ) noexcept : id{ value_ } {}

    [[nodiscard]] bool operator== ( nid const rhs_ ) const noexcept { return id == rhs_.id; }
    [[nodiscard]] bool operator!= ( nid const rhs_ ) const noexcept { return id != rhs_.id; }

    [[nodiscard]] bool is_valid ( ) const noexcept { return id; }
    [[nodiscard]] bool is_invalid ( ) const noexcept { return not is_valid ( ); }

    [[nodiscard]] nid operator++ ( ) noexcept { return nid{ ++id }; }
    [[nodiscard]] nid operator++ ( int ) noexcept { return nid{ id++ }; }

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
            out_ << static_cast<std::uint64_t> ( id_.id );
        }
        return out_;
    }

    private:
    friend class cereal::access;

    template<class Archive>
    inline void serialize ( Archive & ar_ ) {
        ar_ ( id );
    }

    static constexpr int nid_invalid_v = 0;
};
template<typename T>
using zeroing_vector = std::vector<T, tbb::zero_allocator<T>>;
using char_vector    = zeroing_vector<char>;
using id_vector      = zeroing_vector<nid>;
using id_deque       = boost::container::deque<nid, tbb::tbb_allocator<nid>>;

nid pop ( id_deque & d_ ) noexcept {
    nid v = d_.front ( );
    d_.pop_front ( );
    return v;
}

inline constexpr int reserve_size = 1'024;

struct rt_hook { // 16

    nid up = nid{ 0 }, prev = nid{ 0 }, tail = nid{ 0 };
    int size = 0;

    template<typename Stream>
    [[maybe_unused]] friend Stream & operator<< ( Stream & out_, rt_hook const node_ ) noexcept {
        if constexpr ( std::is_same<typename Stream::char_type, wchar_t>::id ) {
            out_ << L'<' << node_.up << L' ' << node_.prev << L' ' << node_.tail << L' ' << node_.size << L'>';
        }
        else {
            out_ << '<' << node_.up << ' ' << node_.prev << ' ' << node_.tail << ' ' << node_.size << '>';
        }
        return out_;
    }
};

template<typename Node>
struct rooted_tree {

    using data_vector     = std::vector<Node>;
    using size_type       = int;
    using difference_type = int;
    using value_type      = typename data_vector::value_type;
    using reference       = typename data_vector::reference;
    using pointer         = typename data_vector::pointer;
    using iterator        = typename data_vector::iterator;
    using const_reference = typename data_vector::const_reference;
    using const_pointer   = typename data_vector::const_pointer;
    using const_iterator  = typename data_vector::const_iterator;

    rooted_tree ( ) {
        nodes.reserve ( reserve_size );
        nodes.emplace_back ( );
    }

    template<typename... Args>
    rooted_tree ( Args &&... args_ ) : rooted_tree ( ) {
        emplace_root ( std::forward<Args> ( args_ )... );
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
        return nodes[ static_cast<typename data_vector::size_type> ( node_.id ) ];
    }
    [[nodiscard]] value_type const & operator[] ( nid node_ ) const noexcept {
        return nodes[ static_cast<typename data_vector::size_type> ( node_.id ) ];
    }

    [[nodiscard]] value_type & operator[] ( size_type node_ ) noexcept { return nodes[ node_ ]; }
    [[nodiscard]] value_type const & operator[] ( size_type node_ ) const noexcept { return nodes[ node_ ]; }

    void reserve ( size_type c_ ) { nodes.reserve ( static_cast<typename data_vector::size_type> ( c_ ) ); }

    template<typename... Args>
    [[maybe_unused]] nid emplace ( nid source_, Args &&... args_ ) noexcept {
        nid id{ static_cast<size_type> ( nodes.size ( ) ) };
        value_type & target = nodes.emplace_back ( std::forward<Args> ( args_ )... );
        target.up           = source_;
        value_type & source = nodes[ source_.id ];
        target.prev         = std::exchange ( source.tail, id );
        source.size += 1;
        return id;
    }
    template<typename... Args>
    [[maybe_unused]] nid emplace_root ( Args &&... args_ ) noexcept {
        assert ( typename data_vector::size_type{ 1 } == nodes.size ( ) );
        return emplace ( nid{ 0 }, std::forward<Args> ( args_ )... );
    }

    class out_iterator {

        friend struct rooted_tree;

        rooted_tree & tree;
        nid node;

        public:
        out_iterator ( rooted_tree & tree_, nid const node_ ) noexcept : tree{ tree_ }, node{ tree[ node_.id ].tail } {}

        [[nodiscard]] bool is_valid ( ) const noexcept { return node.is_valid ( ); }

        [[maybe_unused]] out_iterator & operator++ ( ) noexcept {
            node = tree[ node.id ].prev;
            return *this;
        }

        [[nodiscard]] reference operator* ( ) const noexcept { return tree[ node.id ]; }

        [[nodiscard]] pointer operator-> ( ) const noexcept { return tree.data ( ) + node.id; }

        [[nodiscard]] nid id ( ) const noexcept { return node; }
    };

    class const_out_iterator {

        friend struct rooted_tree;

        rooted_tree const & tree;
        nid node;

        public:
        const_out_iterator ( rooted_tree const & tree_, nid const node_ ) noexcept : tree{ tree_ }, node{ tree[ node_.id ].tail } {}

        [[nodiscard]] bool is_valid ( ) const noexcept { return node.is_valid ( ); }

        [[maybe_unused]] const_out_iterator & operator++ ( ) noexcept {
            node = tree[ node.id ].prev;
            return *this;
        }

        [[nodiscard]] const_reference operator* ( ) const noexcept { return tree[ node.id ]; }

        [[nodiscard]] const_pointer operator-> ( ) const noexcept { return tree.data ( ) + node.id; }

        [[nodiscard]] nid id ( ) const noexcept { return node; }
    };

    size_type height ( nid root_ = nid{ root.id } ) const {
        id_deque queue ( 1, root_ );
        size_type hgt = 0, cnt;
        while ( ( cnt = static_cast<size_type> ( queue.size ( ) ) ) ) {
            while ( cnt-- ) {
                nid parent = pop ( queue );
                for ( nid child = nodes[ parent.id ].tail; child.is_valid ( ); child = nodes[ child.id ].prev )
                    queue.push_back ( child );
            }
        }
        return hgt;
    }

    // Apply function to nodes in bfs-fashion, till max_depth_
    // is reached or till function returns false. Returns the
    // nid of the node that made function return false.
    template<typename Function, typename Value>
    nid apply ( Function function_, Value const & value_, size_type max_depth_ = 0, nid root_ = nid{ root.id } ) const noexcept {
        id_deque queue ( 1, root_ );
        size_type depth = 0, count;
        while ( ( count = static_cast<size_type> ( queue.size ( ) ) ) ) {
            while ( count-- ) {
                nid parent = pop ( queue );
                for ( nid child = nodes[ parent.id ].tail; child.is_valid ( ); child = nodes[ child.id ].prev )
                    queue.push_back ( child );
                if ( not function_ ( nodes[ parent.id ], value_ ) )
                    return parent;
            }
            if ( max_depth_ )
                if ( max_depth_ == ++depth )
                    break;
        }
        return nid{ 0 };
    }

    // Make root_ (must exist) the new root of the tree
    // and discard the rest of the tree.
    void reroot ( nid root_ ) {
        assert ( root_.is_valid ( ) );
        std::remove_reference_t<decltype ( *this )> sub_tree;
        id_vector visited ( nodes.size ( ) );
        visited[ root_.id ] = sub_tree.root;
        id_deque queue ( 1, root_ );
        nid sub_tree_size{ 2 };
        while ( queue.size ( ) ) {
            nid parent = pop ( queue );
            for ( nid child = nodes[ parent.id ].tail; child.is_valid ( ); child = nodes[ child.id ].prev ) {
                if ( visited[ child.id ].is_invalid ( ) ) {
                    visited[ child.id ] = sub_tree_size++;
                    queue.push_back ( child );
                }
            }
            sub_tree.emplace ( visited[ nodes[ parent.id ].up.id ], std::move ( nodes[ parent.id ] ) ); // old parent destroyed.
        }
        std::swap ( nodes, sub_tree.nodes );
    }

    // Remove all but root and ply 1.
    void flatten ( ) {
        nid child = nodes[ root.id ].tail;
        std::remove_reference_t<decltype ( *this )> sub_tree{ std::move ( nodes[ root.id ] ) }; // old root destroyed.
        for ( ; child.is_valid ( ); child = nodes[ child.id ].prev )
            sub_tree.emplace ( root, std::move ( nodes[ child.id ] ) );
        std::swap ( nodes, sub_tree.nodes );
    }

    data_vector nodes;

    static constexpr nid root = nid{ 1 };
}; // namespace sax

struct crt_hook { // 16

    nid up, prev, tail;
    short size;
    tbb::spin_mutex mutex;
    tbb::atomic<char const> done = 1; // Indicates the node is constructed (allocated with zeroed memory).

    template<typename Stream>
    [[maybe_unused]] friend Stream & operator<< ( Stream & out_, crt_hook const node_ ) noexcept {
        if constexpr ( std::is_same<typename Stream::char_type, wchar_t>::id ) {
            out_ << L'<' << node_.up << L' ' << node_.prev << L' ' << node_.tail << L' ' << node_.size << L'>';
        }
        else {
            out_ << '<' << node_.up << ' ' << node_.prev << ' ' << node_.tail << ' ' << node_.size << '>';
        }
        return out_;
    }
};

template<typename Node, typename id_vector = std::vector<nid>>
struct concurrent_rooted_tree {

    using data_vector     = tbb::concurrent_vector<Node, tbb::zero_allocator<Node>>;
    using mutex           = tbb::spin_mutex;
    using lock_guard      = mutex::scoped_lock;
    using size_type       = int;
    using difference_type = int;
    using value_type      = typename data_vector::value_type;
    using reference       = typename data_vector::reference;
    using pointer         = typename data_vector::pointer;
    using iterator        = typename data_vector::iterator;
    using const_reference = typename data_vector::const_reference;
    using const_pointer   = typename data_vector::const_pointer;
    using const_iterator  = typename data_vector::const_iterator;

    concurrent_rooted_tree ( ) {
        nodes.reserve ( reserve_size );
        nodes.grow_by ( 1 );
    }

    template<typename... Args>
    concurrent_rooted_tree ( Args &&... args_ ) : concurrent_rooted_tree ( ) {
        emplace_root ( std::forward<Args> ( args_ )... );
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
        return nodes[ static_cast<typename data_vector::size_type> ( node_.id ) ];
    }
    [[nodiscard]] value_type const & operator[] ( nid node_ ) const noexcept {
        return nodes[ static_cast<typename data_vector::size_type> ( node_.id ) ];
    }

    [[nodiscard]] value_type & operator[] ( size_type node_ ) noexcept { return nodes[ node_ ]; }
    [[nodiscard]] value_type const & operator[] ( size_type node_ ) const noexcept { return nodes[ node_ ]; }

    void reserve ( size_type c_ ) { nodes.reserve ( static_cast<typename data_vector::size_type> ( c_ ) ); }

    [[maybe_unused]] nid push ( nid source_, value_type const & node_ ) noexcept {
        iterator target = nodes.push_back ( node_ );
        nid id{ static_cast<size_type> ( std::distance ( begin ( ), target ) ) };
        target->up.id       = source_.id;
        value_type & source = nodes[ source_.id ];
        {
            lock_guard lock ( source.mutex );
            target->prev = std::exchange ( source.tail, id );
            source.size += 1;
        }
        return id;
    }

    template<typename... Args>
    [[maybe_unused]] nid emplace ( nid source_, Args &&... args_ ) noexcept {
        return push ( source_, value_type{ std::forward<Args> ( args_ )... } );
    }

    template<typename... Args>
    [[maybe_unused]] nid emplace_root ( Args &&... args_ ) noexcept {
        assert ( typename data_vector::size_type{ 1 } == nodes.size ( ) );
        return emplace ( nid{ 0 }, std::forward<Args> ( args_ )... );
    }

    class out_iterator {

        friend struct concurrent_rooted_tree;

        concurrent_rooted_tree & tree;
        nid node;

        public:
        out_iterator ( concurrent_rooted_tree & tree_, nid const node_ ) noexcept : tree{ tree_ }, node{ tree[ node_.id ].tail } {}

        [[nodiscard]] bool is_valid ( ) const noexcept { return node.is_valid ( ); }

        [[maybe_unused]] out_iterator & operator++ ( ) noexcept {
            node = tree[ node.id ].prev;
            return *this;
        }

        [[nodiscard]] reference operator* ( ) const noexcept { return tree[ node.id ]; }

        [[nodiscard]] pointer operator-> ( ) const noexcept { return tree.data ( ) + node.id; }

        [[nodiscard]] nid id ( ) const noexcept { return node; }
    };

    class const_out_iterator {

        friend struct concurrent_rooted_tree;

        concurrent_rooted_tree const & tree;
        nid node;

        public:
        const_out_iterator ( concurrent_rooted_tree const & tree_, nid const node_ ) noexcept :
            tree{ tree_ }, node{ tree[ node_.id ].tail } {}

        [[nodiscard]] bool is_valid ( ) const noexcept { return node.is_valid ( ); }

        [[maybe_unused]] const_out_iterator & operator++ ( ) noexcept {
            node = tree[ node.id ].prev;
            return *this;
        }

        [[nodiscard]] const_reference operator* ( ) const noexcept { return tree[ node.id ]; }

        [[nodiscard]] const_pointer operator-> ( ) const noexcept { return tree.data ( ) + node.id; }

        [[nodiscard]] nid id ( ) const noexcept { return node; }
    };

    size_type height ( nid root_ = nid{ root.id } ) const {
        id_deque queue ( 1, root_ );
        size_type hgt = 0, cnt;
        while ( ( cnt = static_cast<size_type> ( queue.size ( ) ) ) ) {
            while ( cnt-- ) {
                nid parent = pop ( queue );
                for ( nid child = nodes[ parent.id ].tail; child.is_valid ( ); child = nodes[ child.id ].prev )
                    queue.push_back ( child );
            }
        }
        return hgt;
    }

    // Apply function to nodes in bfs-fashion, till max_depth_
    // is reached or till function returns false. Returns the
    // nid of the node that made function return false.
    template<typename Function, typename Value>
    nid apply ( Function function_, Value const & value_, size_type max_depth_ = 0, nid root_ = nid{ root.id } ) const noexcept {
        id_deque queue ( 1, root_ );
        size_type depth = 0, count;
        while ( ( count = static_cast<size_type> ( queue.size ( ) ) ) ) {
            while ( count-- ) {
                nid parent = pop ( queue );
                for ( nid child = nodes[ parent.id ].tail; child.is_valid ( ); child = nodes[ child.id ].prev )
                    queue.push_back ( child );
                if ( not function_ ( nodes[ parent.id ], value_ ) )
                    return parent;
            }
            if ( max_depth_ )
                if ( max_depth_ == ++depth )
                    break;
        }
        return nid{ 0 };
    }

    // Make root_ (must exist) the new root of the tree
    // and discard the rest of the tree.
    void reroot ( nid root_ ) {
        assert ( root_.is_valid ( ) );
        std::remove_reference_t<decltype ( *this )> sub_tree;
        id_vector visited ( nodes.size ( ) );
        visited[ root_.id ] = sub_tree.root;
        id_deque queue ( 1, root_ );
        nid sub_tree_size{ 2 };
        while ( queue.size ( ) ) {
            nid parent = pop ( queue );
            for ( nid child = nodes[ parent.id ].tail; child.is_valid ( ); child = nodes[ child.id ].prev ) {
                if ( visited[ child.id ].is_invalid ( ) ) {
                    visited[ child.id ] = sub_tree_size++;
                    queue.push_back ( child );
                }
            }
            sub_tree.emplace ( visited[ nodes[ parent.id ].up.id ], std::move ( nodes[ parent.id ] ) ); // old parent destroyed.
        }
        std::swap ( nodes, sub_tree.nodes );
    }

    // Remove all but root and ply 1.
    void flatten ( ) {
        nid child = nodes[ root.id ].tail;
        std::remove_reference_t<decltype ( *this )> sub_tree{ std::move ( nodes[ root.id ] ) }; // old root destroyed.
        for ( ; child.is_valid ( ); child = nodes[ child.id ].prev )
            sub_tree.emplace ( root, std::move ( nodes[ child.id ] ) );
        std::swap ( nodes, sub_tree.nodes );
    }

    data_vector nodes;

    static constexpr nid root = nid{ 1 };
};

} // namespace sax

#undef ever
