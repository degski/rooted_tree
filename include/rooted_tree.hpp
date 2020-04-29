
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

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-W#pragma-messages"
#pragma GCC diagnostic ignored "-Wmicrosoft-enum-value"
#include <tbb/tbb.h>
#pragma GCC diagnostic pop

#include <cereal/cereal.hpp>

namespace sax {

struct nid {

    int id;

    constexpr explicit nid ( ) noexcept : id{ nid_invalid_v } {}
    constexpr explicit nid ( int && v_ ) noexcept : id{ std::move ( v_ ) } {}
    constexpr explicit nid ( int const & v_ ) noexcept : id{ v_ } {}

    // [[nodiscard]] constexpr int operator( ) ( ) const noexcept { return id; }

    [[nodiscard]] bool operator== ( nid const rhs_ ) const noexcept { return id == rhs_.id; }
    [[nodiscard]] bool operator!= ( nid const rhs_ ) const noexcept { return id != rhs_.id; }

    [[nodiscard]] bool is_valid ( ) const noexcept { return id; }
    [[nodiscard]] bool is_invalid ( ) const noexcept { return not is_valid ( ); }

    template<typename Stream>
    [[maybe_unused]] friend Stream & operator<< ( Stream & out_, nid const id_ ) noexcept {
        if ( nid_invalid_v == id_.id ) {
            if constexpr ( std::is_same<typename Stream::char_type, wchar_t>::id ) {
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

struct rt_hook { // 16

    nid up = nid{ }, prev = nid{ }, tail = nid{ }; // 12
    int size = 0;                                  // 4

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

template<typename Node, typename Container = std::vector<Node>, typename IDContainer = std::vector<nid>>
struct rooted_tree {

    using data_vector     = Container;
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
        nodes.reserve ( 128 );
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
        value_type & target = nodes.emplace_back ( std::forward<Args> ( args_ )... );
        nid id{ static_cast<size_type> ( nodes.size ( ) ) };
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

    // Make root_ (must exist) the new root of the tree
    // and discard the rest of the tree.
    void reroot ( nid root_ ) {
        assert ( root_.is_valid ( ) );
        rooted_tree sub_tree{ std::move ( nodes[ root_.id ] ) };
        IDContainer visited ( nodes.size ( ) );
        visited[ root_.id ] = sub_tree.root;
        IDContainer stack;
        stack.reserve ( 64u );
        stack.push_back ( root_ );
        while ( stack.size ( ) ) {
            nid parent = stack.back ( );
            stack.pop_back ( );
            for ( nid child = nodes[ parent.id ].tail; child.is_valid ( ); child = nodes[ child.id ].prev )
                if ( visited[ child.id ].is_invalid ( ) ) {
                    visited[ child.id ] = sub_tree.add ( visited[ parent.id ], std::move ( nodes[ child.id ] ) );
                    stack.push_back ( child );
                }
        }
        std::swap ( nodes, sub_tree.nodes );
    }

    void flatten ( ) {
        rooted_tree sub_tree{ std::move ( nodes[ root.id ] ) };
        for ( nid child = nodes[ root.id ].tail; child.is_valid ( ); child = nodes[ child.id ].prev )
            sub_tree.add ( root, std::move ( nodes[ child.id ] ) );
        std::swap ( nodes, sub_tree.nodes );
    }

    data_vector nodes;

    static constexpr nid root = nid{ 1 };
};

struct crt_hook { // 16

    nid up, prev, tail;         // 12
    short size;                 // 2
    tbb::spin_mutex mutex;      // 1
    tbb::atomic<char> done = 1; // 1

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

template<typename Node, typename IDContainer = std::vector<nid>>
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
        nodes.reserve ( 128 );
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

    // Make root_ (must exist) the new root of the tree
    // and discard the rest of the tree.
    void reroot ( nid root_ ) {
        assert ( root_.is_valid ( ) );
        rooted_tree sub_tree{ std::move ( nodes[ root_.id ] ) };
        IDContainer visited ( nodes.size ( ) );
        visited[ root_.id ] = sub_tree.root;
        IDContainer stack;
        stack.reserve ( 64u );
        stack.push_back ( root_ );
        while ( stack.size ( ) ) {
            nid parent = stack.back ( );
            stack.pop_back ( );
            for ( nid child = nodes[ parent.id ].tail; child.is_valid ( ); child = nodes[ child.id ].prev )
                if ( visited[ child.id ].is_invalid ( ) ) {
                    visited[ child.id ] = sub_tree.add ( visited[ parent.id ], std::move ( nodes[ child.id ] ) );
                    stack.push_back ( child );
                }
        }
        std::swap ( nodes, sub_tree.nodes );
    }

    void flatten ( ) {
        rooted_tree sub_tree{ std::move ( nodes[ root.id ] ) };
        for ( nid child = nodes[ root.id ].tail; child.is_valid ( ); child = nodes[ child.id ].prev )
            sub_tree.add ( root, std::move ( nodes[ child.id ] ) );
        std::swap ( nodes, sub_tree.nodes );
    }

    data_vector nodes;

    static constexpr nid root = nid{ 1 };
};

} // namespace sax
