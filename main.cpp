
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
// FITNESS FOR key_one_type PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstdlib>

// #include <gfx/timsort.hpp> // For faster sorting in plf::colony.

#include <plf/plf_list.h>

#include "vm_backed.hpp"
#include "rooted_tree.hpp"

#include <array>
#include <atomic>
#include <jthread>
#include <set>
#include <type_traits>

#include <plf/plf_nanotimer.h>

#include <sax/iostream.hpp>
#include <sax/prng_sfc.hpp>
#include <sax/uniform_int_distribution.hpp>

template<class F, class... Ts>
void reverse_for_each ( F f, Ts... ts ) {
    int dummy;
    ( ( f ( ts ), dummy ) = ... = 0 );
}

#if defined( _DEBUG )
#    define RANDOM 0
#else
#    define RANDOM 1
#endif

namespace ThreadID {
// Creates a new ID.
[[nodiscard]] inline int next ( ) noexcept {
    static std::atomic<int> id = 0;
    return id++;
}
// Returns ID of this thread.
[[nodiscard]] inline int get ( ) noexcept {
    static thread_local int tl_id = next ( );
    return tl_id;
}
} // namespace ThreadID

namespace Rng {
// key_one_type global instance of a C++ implementation of Chris Doty-Humphrey's Small Fast Chaotic Prng.
[[nodiscard]] inline sax::Rng & generator ( ) noexcept {
    if constexpr ( RANDOM ) {
        static thread_local sax::Rng generator ( sax::os_seed ( ), sax::os_seed ( ), sax::os_seed ( ), sax::os_seed ( ) );
        return generator;
    }
    else {
        static thread_local sax::Rng generator ( sax::fixed_seed ( ) + ThreadID::get ( ) );
        return generator;
    }
}
} // namespace Rng

#undef RANDOM

sax::Rng & rng = Rng::generator ( );

struct Foo : public sax::rooted_tree_hook {
    int value                    = 0;
    Foo ( ) noexcept             = default;
    Foo ( Foo const & ) noexcept = default;

    explicit Foo ( int const & i_ ) noexcept : value{ i_ } {}
    explicit Foo ( int && i_ ) noexcept : value{ std::move ( i_ ) } {}
};

struct Bar {
    int value = 0;

    explicit Bar ( int const & i_ ) noexcept : value{ i_ } {}
    explicit Bar ( int && i_ ) noexcept : value{ std::move ( i_ ) } {}

    // std::atomic<char> vm_vector_atom = 1; // last member
};

using ConcurrentTree = sax::concurrent_rooted_tree<Foo>;
using SequentailTree = sax::rooted_tree<Foo>;

template<typename Tree>
void add_nodes_high_workload ( Tree & tree_, int n_ ) {
    for ( int i = 1; i < n_; ++i ) {
        // Some piecewise distibution, simulating more 'normal' use case, where new nodes are added more often at
        // the bottom. It is also **very** expensive to calculate, so gives some sensible workload, which helps
        // to get a better idea of performance, as contention is much lower (like in real use cases) as compared
        // to add_nodes_low_work_load().
        auto back                         = tree_.nodes.size ( );
        std::array<float, 4> ai           = { 1.0f, back / 2.0f, 2 * back / 3.0f, static_cast<float> ( back - 1 ) };
        constexpr std::array<float, 3> aw = { 1, 3, 9 };
        int n = static_cast<int> ( std::piecewise_constant_distribution<float> ( ai.begin ( ), ai.end ( ), aw.begin ( ) ) ( rng ) );
        tree_.emplace ( sax::nid{ n }, i );
    }
}

template<typename Tree>
void add_nodes_low_workload ( Tree & tree_, int n_ ) {
    for ( int i = 1; i < n_; ++i )
        tree_.emplace ( sax::nid{ sax::uniform_int_distribution<int> ( 1, static_cast<int> ( tree_.nodes.size ( ) ) - 1 ) ( rng ) },
                        i );
}

int main75675 ( ) {

    {
        std::cout << "sequential tree lw" << nl;
        SequentailTree tree;
        tree.emplace ( tree.invalid, 1 );
        plf::nanotimer timer;
        timer.start ( );
        add_nodes_low_workload ( tree, 4'000'001 );
        std::uint64_t duration = static_cast<std::uint64_t> ( timer.get_elapsed_ms ( ) );
        std::cout << duration << "ms" << sp << tree.nodes.size ( ) << nl;
        timer.start ( );
        int sum1 = 1;
        for ( SequentailTree::const_breadth_iterator it{ tree }; it.is_valid ( ); ++it )
            sum1 += 1;
        duration = static_cast<std::uint64_t> ( timer.get_elapsed_ms ( ) );
        std::cout << duration << "ms" << sp << sum1 << nl;
        timer.start ( );
        int sum2 = 1;
        for ( SequentailTree::const_depth_iterator it{ tree }; it.is_valid ( ); ++it )
            sum2 += 1;
        duration = static_cast<std::uint64_t> ( timer.get_elapsed_ms ( ) );
        std::cout << duration << "ms" << sp << sum2 << nl;
        timer.start ( );
        int wid, hei = tree.height ( tree.root, std::addressof ( wid ) );
        duration = static_cast<std::uint64_t> ( timer.get_elapsed_ms ( ) );
        std::cout << duration << "ms" << sp << hei << sp << wid << nl;
    }

    {
        std::cout << "concurrent tree lw" << nl;
        ConcurrentTree tree ( 1 );
        std::vector<std::thread> threads;
        threads.reserve ( 4 );
        plf::nanotimer timer;
        timer.start ( );
        for ( int n = 0; n < 4; ++n )
            threads.emplace_back ( add_nodes_low_workload<ConcurrentTree>, std::ref ( tree ), 1'000'001 );
        for ( std::thread & t : threads )
            t.join ( );
        std::uint64_t duration = static_cast<std::uint64_t> ( timer.get_elapsed_ms ( ) );
        std::cout << duration << "ms" << sp << tree.nodes.size ( ) << nl;
        timer.start ( );
        int sum1 = 1;
        for ( ConcurrentTree::const_breadth_iterator it{ tree }; it.is_valid ( ); ++it )
            sum1 += 1;
        duration = static_cast<std::uint64_t> ( timer.get_elapsed_ms ( ) );
        std::cout << duration << "ms" << sp << sum1 << nl;
        timer.start ( );
        int sum2 = 1;
        for ( ConcurrentTree::const_depth_iterator it{ tree }; it.is_valid ( ); ++it )
            sum2 += 1;
        duration = static_cast<std::uint64_t> ( timer.get_elapsed_ms ( ) );
        std::cout << duration << "ms" << sp << sum2 << nl;
        timer.start ( );
        int wid, hei = tree.height ( tree.root, std::addressof ( wid ) );
        duration = static_cast<std::uint64_t> ( timer.get_elapsed_ms ( ) );
        std::cout << duration << "ms" << sp << hei << sp << wid << nl;
    }

    {
        std::cout << "sequential tree hw" << nl;
        SequentailTree tree;
        tree.emplace ( tree.invalid, 1 );
        plf::nanotimer timer;
        timer.start ( );
        add_nodes_high_workload ( tree, 400'001 );
        std::uint64_t duration = static_cast<std::uint64_t> ( timer.get_elapsed_ms ( ) );
        std::cout << duration << "ms" << sp << tree.nodes.size ( ) << nl;
        timer.start ( );
        int sum1 = 1;
        for ( SequentailTree::const_breadth_iterator it{ tree }; it.is_valid ( ); ++it )
            sum1 += 1;
        duration = static_cast<std::uint64_t> ( timer.get_elapsed_ms ( ) );
        std::cout << duration << "ms" << sp << sum1 << nl;
        timer.start ( );
        int sum2 = 1;
        for ( SequentailTree::const_depth_iterator it{ tree }; it.is_valid ( ); ++it )
            sum2 += 1;
        duration = static_cast<std::uint64_t> ( timer.get_elapsed_ms ( ) );
        std::cout << duration << "ms" << sp << sum2 << nl;
        timer.start ( );
        int wid, hei = tree.height ( tree.root, std::addressof ( wid ) );
        duration = static_cast<std::uint64_t> ( timer.get_elapsed_ms ( ) );
        std::cout << duration << "ms" << sp << hei << sp << wid << nl;
    }

    {
        std::cout << "concurrent tree hw" << nl;
        ConcurrentTree tree ( 1 );
        std::vector<std::thread> threads;
        threads.reserve ( 4 );
        plf::nanotimer timer;
        timer.start ( );
        for ( int n = 0; n < 4; ++n )
            threads.emplace_back ( add_nodes_high_workload<ConcurrentTree>, std::ref ( tree ), 100'001 );
        for ( std::thread & t : threads )
            t.join ( );
        std::uint64_t duration = static_cast<std::uint64_t> ( timer.get_elapsed_ms ( ) );
        std::cout << duration << "ms" << sp << tree.nodes.size ( ) << nl;
        timer.start ( );
        int sum1 = 1;
        for ( ConcurrentTree::const_breadth_iterator it{ tree }; it.is_valid ( ); ++it )
            sum1 += 1;
        duration = static_cast<std::uint64_t> ( timer.get_elapsed_ms ( ) );
        std::cout << duration << "ms" << sp << sum1 << nl;
        timer.start ( );
        int sum2 = 1;
        for ( ConcurrentTree::const_depth_iterator it{ tree }; it.is_valid ( ); ++it )
            sum2 += 1;
        duration = static_cast<std::uint64_t> ( timer.get_elapsed_ms ( ) );
        std::cout << duration << "ms" << sp << sum2 << nl;
        timer.start ( );
        int wid, hei = tree.height ( tree.root, std::addressof ( wid ) );
        duration = static_cast<std::uint64_t> ( timer.get_elapsed_ms ( ) );
        std::cout << duration << "ms" << sp << hei << sp << wid << nl;
    }

    return EXIT_SUCCESS;
}

int main7867867 ( ) {

    using Tree = ConcurrentTree;

    Tree tree ( 1 );

    // sax::nid n1  = tree.emplace ( Tree::invalid, 2 ); oops!!! added 2 roots.
    sax::nid n2  = tree.emplace ( Tree::root, 2 );
    sax::nid n3  = tree.emplace ( Tree::root, 3 );
    sax::nid n4  = tree.emplace ( Tree::root, 4 );
    sax::nid n5  = tree.emplace ( n2, 5 );
    sax::nid n6  = tree.emplace ( n2, 6 );
    sax::nid n7  = tree.emplace ( n3, 7 );
    sax::nid n8  = tree.emplace ( n4, 8 );
    sax::nid n9  = tree.emplace ( Tree::root, 9 );
    sax::nid n10 = tree.emplace ( n4, 10 );
    sax::nid n11 = tree.emplace ( n2, 11 );
    sax::nid n12 = tree.emplace ( n2, 12 );
    sax::nid n13 = tree.emplace ( n12, 13 );

    for ( Tree::const_out_iterator it{ tree, Tree::root }; it.is_valid ( ); ++it )
        std::cout << it.id ( ) << sp;
    std::cout << nl;

    for ( Tree::const_out_iterator it{ tree, n2 }; it.is_valid ( ); ++it )
        std::cout << it->value << sp;
    std::cout << nl;

    int w;

    std::cout << tree.height ( tree.root, &w ) << sp << w << nl;

    for ( Tree::const_internal_iterator it{ tree }; it.is_valid ( ); ++it )
        std::cout << it.id ( ) << sp;
    std::cout << nl;

    for ( Tree::const_leaf_iterator it{ tree }; it.is_valid ( ); ++it )
        std::cout << it.id ( ) << sp;
    std::cout << nl;

    for ( Tree::const_depth_iterator it{ tree }; it.is_valid ( ); ++it )
        std::cout << it.id ( ) << sp;
    std::cout << nl;

    for ( Tree::const_breadth_iterator it{ tree }; it.is_valid ( ); ++it )
        std::cout << it.id ( ) << sp;
    std::cout << nl;

    return EXIT_SUCCESS;
}

using StdVec    = std::vector<sax::detail::vm_vector::vm_epilog<Bar>>;
using SaxVec    = sax::vm_vector<sax::detail::vm_vector::vm_epilog<Bar>, 10'000'000>;
using SaxConVec = sax::vm_concurrent_vector<Bar, 10'000'000>;
using TbbVec    = tbb::concurrent_vector<sax::detail::vm_vector::vm_epilog<Bar>, tbb::zero_allocator<Bar>>;

template<typename Type>
void emplace_back_low_workload ( Type & vec_, int n_ ) {
    for ( int i = 0; i < n_; ++i )
        vec_.emplace_back ( i );
}

template<typename key_one_type, typename key_two_type, typename type, typename allocator = std::allocator<type>>
class alignas ( 64 ) bimap {

    static_assert ( not std::is_same<key_one_type, key_two_type>::value, "types must be distinct" );

    template<typename KT, typename PKT, typename PDT>
    struct key_node {
        KT key;
        mutable PKT * other;
        PDT * kpndata;
    };

    template<typename KN>
    struct compare {
        [[nodiscard]] bool operator( ) ( KN const & l_, KN const & r_ ) const noexcept { return l_.key < r_.key; }
    };
    /*
    template<typename KN>
    [[nodiscard]] bool operator== ( KN const & l_, KN const & r_ ) noexcept {
        return l_.key == r_.key;
    }
    */
    template<typename PKT1, typename PKT2, typename DT>
    struct data_node {
        PKT1 * one_last = nullptr;
        PKT2 * two_last = nullptr;
        DT ndata;

        static constexpr std::size_t data_offset = offsetof ( data_node, ndata );
    };

    public:
    using value_type = type;

    using pointer         = value_type *;
    using const_pointer   = value_type const *;
    using reference       = value_type &;
    using const_reference = value_type const &;
    using rv_reference    = value_type &&;

    private:
    using node_type = data_node<key_one_type, key_two_type, value_type>;

    using key_one_node_type = key_node<key_one_type, key_two_type, node_type>;
    using key_two_node_type = key_node<key_two_type, key_one_type, node_type>;

    template<typename KN>
    using key_node_set = std::set<KN, compare<KN>, typename allocator::template rebind<KN>::other>;

    using map_one_type = key_node_set<key_one_node_type>;
    using map_two_type = key_node_set<key_two_node_type>;

    map_one_type key_map_one;
    map_two_type key_map_two;

    public:
    using colony_value_type = plf::colony<node_type, typename allocator::template rebind<node_type>::other>;
    using iterator          = typename colony_value_type::iterator;
    using const_iterator    = typename colony_value_type::const_iterator;

    colony_value_type data;

    template<typename KT>
    KT * key_addressof ( KT const & k_ ) const noexcept {
        return const_cast<KT *> ( std::addressof ( k_ ) );
    }

    [[maybe_unused]] reference insert ( key_one_type && k1_, key_two_type && k2_, node_type * p_ ) {
        auto two_it = key_map_two.insert ( { std::move ( k2_ ), nullptr, p_ } ).first;
        two_it->other =
            key_addressof ( key_map_one.insert ( { std::move ( k1_ ), key_addressof ( two_it->key ), p_ } ).first->key );
        return p_->ndata;
    }

    public:
    [[maybe_unused]] reference insert ( key_one_type k1_, key_two_type k2_, rv_reference v_ ) {
        return insert (
            std::forward<key_one_type> ( k1_ ), std::forward<key_two_type> ( k2_ ),
            std::addressof ( *data.insert ( { key_addressof ( k1_ ), key_addressof ( k2_ ), std::forward<value_type> ( v_ ) } ) ) );
    }
    [[maybe_unused]] reference insert ( key_two_type k2_, key_one_type k1_, rv_reference v_ ) {
        return insert ( std::forward<key_one_type> ( k1_ ), std::forward<key_two_type> ( k2_ ), std::forward<value_type> ( v_ ) );
    }
    [[maybe_unused]] reference insert ( key_one_type k1_, key_two_type k2_, const_reference v_ ) {
        return insert ( std::forward<key_one_type> ( k1_ ), std::forward<key_two_type> ( k2_ ),
                        std::addressof ( *data.insert ( { key_addressof ( k1_ ), key_addressof ( k2_ ), v_ } ) ) );
    }
    [[maybe_unused]] reference insert ( key_two_type k2_, key_one_type k1_, const_reference v_ ) {
        return insert ( std::forward<key_one_type> ( k1_ ), std::forward<key_two_type> ( k2_ ), v_ );
    }

    template<typename... Args>
    [[maybe_unused]] reference emplace ( key_one_type k1_, key_two_type k2_, Args &&... v_ ) {
        return insert (
            std::forward<key_one_type> ( k1_ ), std::forward<key_two_type> ( k2_ ),
            std::addressof ( *data.emplace ( { key_addressof ( k1_ ), key_addressof ( k2_ ), std::forward<Args> ( v_ )... } ) ) );
    }
    template<typename... Args>
    [[maybe_unused]] reference emplace ( key_two_type k2_, key_one_type k1_, Args &&... v_ ) {
        return emplace ( std::forward<key_one_type> ( k1_ ), std::forward<key_two_type> ( k2_ ), std::forward<Args> ( v_ )... );
    }

    private:
    [[nodiscard]] pointer data_pointer ( node_type * p_ ) const noexcept {
        return reinterpret_cast<pointer> ( reinterpret_cast<char *> ( p_ ) + node_type::data_offset );
    }
    [[nodiscard]] reference data_reference ( node_type * p_ ) const noexcept { return *data_pointer ( p_ ); }

    public:
    [[nodiscard]] pointer find ( key_one_type k1_ ) noexcept {
        auto it = key_map_one.find ( { std::forward<key_one_type> ( k1_ ), nullptr, nullptr } );
        return key_map_one.end ( ) != it ? data_pointer ( it->kpndata ) : nullptr;
    }
    [[nodiscard]] pointer find ( key_two_type k2_ ) noexcept {
        auto it = key_map_two.find ( { std::forward<key_two_type> ( k2_ ), nullptr, nullptr } );
        return key_map_two.end ( ) != it ? data_pointer ( it->kpndata ) : nullptr;
    }
    [[nodiscard]] const_pointer find ( key_one_type k1_ ) const noexcept {
        auto it = key_map_one.find ( { std::forward<key_one_type> ( k1_ ), nullptr, nullptr } );
        return key_map_one.end ( ) != it ? data_pointer ( it->kpndata ) : nullptr;
    }
    [[nodiscard]] const_pointer find ( key_two_type k2_ ) const noexcept {
        auto it = key_map_two.find ( { std::forward<key_two_type> ( k2_ ), nullptr, nullptr } );
        return key_map_two.end ( ) != it ? data_pointer ( it->kpndata ) : nullptr;
    }

    [[nodiscard]] reference find_existing ( key_one_type k1_ ) noexcept {
        return data_reference ( key_map_one.find ( { std::forward<key_one_type> ( k1_ ), nullptr, nullptr } )->kpndata );
    }
    [[nodiscard]] reference find_existing ( key_two_type k2_ ) noexcept {
        return data_reference ( key_map_two.find ( { std::forward<key_two_type> ( k2_ ), nullptr, nullptr } )->kpndata );
    }
    [[nodiscard]] const_reference find_existing ( key_one_type k1_ ) const noexcept {
        return data_reference ( key_map_one.find ( { std::forward<key_one_type> ( k1_ ), nullptr, nullptr } )->kpndata );
    }
    [[nodiscard]] const_reference find_existing ( key_two_type k2_ ) const noexcept {
        return data_reference ( key_map_two.find ( { std::forward<key_two_type> ( k2_ ), nullptr, nullptr } )->kpndata );
    }

    void erase ( key_one_type const * k1_ ) noexcept {
        if ( k1_ ) {
            auto one_it = key_map_one.find ( { *k1_, nullptr, nullptr } );
            if ( key_map_one.end ( ) != one_it ) {
                pointer one_data = one_it->kpndata;
                for ( auto two_it = key_map_two.begin ( ), two_end = key_map_two.end ( ); two_it != two_end; ) {
                    if ( one_data == two_it->kpndata )
                        two_it = key_map_two.erase ( two_it );
                    else
                        ++two_it;
                }
                key_map_one.erase ( one_it );
                data.erase ( data.get_iterator_from_pointer ( one_data ) );
            }
        }
    }

    void erase ( key_two_type const * k2_ ) noexcept {
        if ( k2_ ) {
            auto two_it = key_map_two.find ( { *k2_, nullptr, nullptr } );
            if ( key_map_two.end ( ) != two_it ) {
                pointer two_data = two_it->kpndata;
                for ( auto one_it = key_map_one.begin ( ), one_end = key_map_one.end ( ); one_it != one_end; ) {
                    if ( two_data == one_it->kpndata )
                        one_it = key_map_one.erase ( one_it );
                    else
                        ++one_it;
                }
                key_map_two.erase ( two_it );
                data.erase ( data.get_iterator_from_pointer ( two_data ) );
            }
        }
    }

    void erase_existing ( key_one_type k1_ ) noexcept {
        auto one_it      = key_map_one.find ( { k1_, nullptr, nullptr } );
        pointer one_data = one_it->kpndata;
        for ( auto two_it = key_map_two.begin ( ), two_end = key_map_two.end ( ); two_it != two_end; ) {
            if ( one_data == two_it->kpndata )
                two_it = key_map_two.erase ( two_it );
            else
                ++two_it;
        }
        key_map_one.erase ( one_it );
        data.erase ( data.get_iterator_from_pointer ( one_data ) );
    }

    void erase_existing ( key_two_type k2_ ) noexcept {
        auto two_it      = key_map_two.find ( { k2_, nullptr, nullptr } );
        pointer two_data = two_it->kpndata;
        for ( auto one_it = key_map_one.begin ( ), one_end = key_map_one.end ( ); one_it != one_end; ) {
            if ( two_data == one_it->kpndata )
                one_it = key_map_one.erase ( one_it );
            else
                ++one_it;
        }
        key_map_two.erase ( two_it );
        data.erase ( data.get_iterator_from_pointer ( two_data ) );
    }

    [[nodiscard]] const_iterator begin ( ) const noexcept { return data.begin ( ); }
    [[nodiscard]] const_iterator cbegin ( ) const noexcept { return data.cbegin ( ); }
    [[nodiscard]] iterator begin ( ) noexcept { return data.begin ( ); }
    [[nodiscard]] const_iterator end ( ) const noexcept { return data.end ( ); }
    [[nodiscard]] const_iterator cend ( ) const noexcept { return data.cend ( ); }
    [[nodiscard]] iterator end ( ) noexcept { return data.end ( ); }

    class key_one_iterator {

        friend class bimap;

        using iterator = typename map_two_type::iterator;

        bimap & map;
        pointer one_data;
        iterator two_it, two_end;

        key_one_iterator ( bimap & map_, key_one_type k1_ ) noexcept :
            map ( map_ ), one_data ( map.key_map_one.find ( { k1_, nullptr, nullptr } )->data ),
            two_it ( map.key_map_two.begin ( ) ), two_end ( map.key_map_two.end ( ) ) {
            while ( two_end != two_it ) {
                if ( one_data == two_it->data )
                    break;
                else
                    ++two_it;
            }
        }

        [[maybe_unused]] key_one_iterator & operator++ ( ) noexcept {
            do
                if ( one_data == ++two_it->data )
                    break;
            while ( two_end != two_it );
            return *this;
        }
        [[maybe_unused]] key_one_iterator & operator++ ( int ) noexcept { return this->operator++ ( ); }
        [[nodiscard]] key_two_type operator* ( ) const noexcept { return two_it->key; }
        [[nodiscard]] bool is_valid ( ) const noexcept { return two_end != two_it; }
    };

    class const_key_one_iterator {

        friend class bimap;

        using const_iterator = typename map_two_type::const_iterator;

        bimap const & map;
        const_pointer one_data;
        const_iterator two_it, two_end;

        const_key_one_iterator ( bimap const & map_, key_one_type k1_ ) noexcept :
            map ( map_ ), one_data ( map.key_map_one.find ( { k1_, nullptr, nullptr } )->data ),
            two_it ( map.key_map_two.begin ( ) ), two_end ( map.key_map_two.end ( ) ) {
            while ( two_end != two_it ) {
                if ( one_data == two_it->data )
                    break;
                else
                    ++two_it;
            }
        }

        [[maybe_unused]] const_key_one_iterator & operator++ ( ) noexcept {
            do
                if ( one_data == ++two_it->data )
                    break;
            while ( two_end != two_it );
            return *this;
        }
        [[maybe_unused]] const_key_one_iterator & operator++ ( int ) noexcept { return this->operator++ ( ); }
        [[nodiscard]] key_two_type operator* ( ) const noexcept { return two_it->key; }
        [[nodiscard]] bool is_valid ( ) const noexcept { return two_end != two_it; }
    };

    class key_two_iterator {

        friend class bimap;

        using iterator = typename map_one_type::iterator;

        bimap & map;
        pointer two_data;
        iterator one_it, one_end;

        key_two_iterator ( bimap & map_, key_two_type k1_ ) noexcept :
            map ( map_ ), two_data ( map.key_map_two.find ( { k1_, nullptr, nullptr } )->data ),
            one_it ( map.key_map_one.begin ( ) ), one_end ( map.key_map_one.end ( ) ) {
            while ( one_end != one_it ) {
                if ( two_data == one_it->data )
                    break;
                else
                    ++one_it;
            }
        }

        [[maybe_unused]] key_two_iterator & operator++ ( ) noexcept {
            do
                if ( two_data == ++one_it->data )
                    break;
            while ( one_end != one_it );
            return *this;
        }
        [[maybe_unused]] key_two_iterator & operator++ ( int ) noexcept { return this->operator++ ( ); }
        [[nodiscard]] key_one_type operator* ( ) const noexcept { return one_it->key; }
        [[nodiscard]] bool is_valid ( ) const noexcept { return one_end != one_it; }
    };

    class const_key_two_iterator {

        friend class bimap;

        using const_iterator = typename map_one_type::const_iterator;

        bimap const & map;
        const_pointer two_data;
        const_iterator one_it, one_end;

        const_key_two_iterator ( bimap const & map_, key_two_type k1_ ) noexcept :
            map ( map_ ), two_data ( map.key_map_two.find ( { k1_, nullptr, nullptr } )->data ),
            one_it ( map.key_map_one.begin ( ) ), one_end ( map.key_map_one.end ( ) ) {
            while ( one_end != one_it ) {
                if ( two_data == one_it->data )
                    break;
                else
                    ++one_it;
            }
        }

        [[maybe_unused]] const_key_two_iterator & operator++ ( ) noexcept {
            do
                if ( two_data == ++one_it->data )
                    break;
            while ( one_end != one_it );
            return *this;
        }
        [[maybe_unused]] const_key_two_iterator & operator++ ( int ) noexcept { return this->operator++ ( ); }
        [[nodiscard]] key_one_type operator* ( ) const noexcept { return one_it->key; }
        [[nodiscard]] bool is_valid ( ) const noexcept { return one_end != one_it; }
    };

    [[nodiscard]] friend bool operator== ( key_one_iterator const & l_, key_one_iterator const & r_ ) noexcept {
        return l_.two_it == r_.two_it;
    }
    [[nodiscard]] friend bool operator== ( const_key_one_iterator const & l_, key_one_iterator const & r_ ) noexcept {
        return l_.two_it == r_.two_it;
    }
    [[nodiscard]] friend bool operator== ( key_one_iterator const & l_, const_key_one_iterator const & r_ ) noexcept {
        return l_.two_it == r_.two_it;
    }
    [[nodiscard]] friend bool operator== ( const_key_one_iterator const & l_, const_key_one_iterator const & r_ ) noexcept {
        return l_.two_it == r_.two_it;
    }
    [[nodiscard]] friend bool operator== ( key_two_iterator const & l_, key_two_iterator const & r_ ) noexcept {
        return l_.one_it == r_.one_it;
    }
    [[nodiscard]] friend bool operator== ( const_key_two_iterator const & l_, key_two_iterator const & r_ ) noexcept {
        return l_.one_it == r_.one_it;
    }
    [[nodiscard]] friend bool operator== ( key_two_iterator const & l_, const_key_two_iterator const & r_ ) noexcept {
        return l_.one_it == r_.one_it;
    }
    [[nodiscard]] friend bool operator== ( const_key_two_iterator const & l_, const_key_two_iterator const & r_ ) noexcept {
        return l_.one_it == r_.one_it;
    }

    void clear ( ) noexcept {
        key_map_one.clear ( );
        key_map_two.clear ( );
        data.clear ( );
    }

    [[nodiscard]] std::size_t key_one_size ( ) const noexcept { return key_map_one.size ( ); }
    [[nodiscard]] std::size_t key_two_size ( ) const noexcept { return key_map_two.size ( ); }
    [[nodiscard]] std::size_t size ( ) const noexcept { return data.size ( ); }
    [[nodiscard]] bool empty ( ) const noexcept { return data.empty ( ); }
};

namespace sax {
template<typename T1, typename T2, typename V>
struct bridge {
    T1 one;
    T2 two;
    V data;

    [[nodiscard]] bool operator== ( bridge const & rhs_ ) const noexcept {
        if ( T2{ } == rhs_.two )
            return one == rhs_.one;
        if ( T1{ } == rhs_.one )
            return two == rhs_.two;
        return one == rhs_.one and two == rhs_.two;
    }

    template<typename stream>
    [[maybe_unused]] friend stream & operator<< ( stream & out_, bridge const & b_ ) noexcept {
        if constexpr ( std::is_same<typename stream::char_type, wchar_t>::value )
            out_ << L'<' << b_.one << L' ' << b_.two << L'>';
        else
            out_ << '<' << b_.one << ' ' << b_.two << '>';
        return out_;
    }
};

template<typename T1, typename T2, typename V>
using bridge_set = plf::list<bridge<T1, T2, V>>;
template<typename T1, typename T2, typename V>
using bridge_set_iterator = typename bridge_set<T1, T2, V>::iterator;

template<typename T1, typename T2, typename V>
bridge_set_iterator<T1, T2, V> corral_front ( bridge_set<T1, T2, V> & bs_, bridge<T1, T2, V> const & v_ ) noexcept {
    bs_.sort ( [ v_ ] ( auto l, auto r ) { return v_.one == l.one ? v_.one == r.one ? l.two < r.two : true : false; } );
    return std::find_if_not ( bs_.begin ( ), bs_.end ( ), [ v_ ] ( auto b ) { return v_.one == b.one; } );
}
template<typename T1, typename T2, typename V>
bridge_set_iterator<T1, T2, V> corral_front ( bridge_set<T1, T2, V> & bs_, T1 const & v_ ) noexcept {
    return corral_front ( bs_, bridge<T1, T2, V>{ v_, T2{} }, V{ } );
}
template<typename T1, typename T2, typename V>
bridge_set_iterator<T1, T2, V> corral_front ( bridge_set<T1, T2, V> & bs_, T2 const & v_ ) noexcept {
    return corral_front ( bs_, bridge<T1, T2, V>{ T1{ }, v_, V{} } );
}

template<typename T1, typename T2, typename V>
bridge_set_iterator<T1, T2, V> corral_back ( bridge_set<T1, T2, V> & bs_, bridge<T1, T2, V> const & v_ ) noexcept {
    bs_.sort ( [ v_ ] ( auto l, auto r ) { return v_.one == l.one ? v_.one == r.one ? l.two < r.two : false : true; } );
    return std::find_if_not ( std::reverse_iterator{ bs_.end ( ) }, std::reverse_iterator{ bs_.begin ( ) },
                              [ v_ ] ( auto b ) { return v_.one == b.one; } )
        .base ( );
}
template<typename T1, typename T2, typename V>
bridge_set_iterator<T1, T2, V> corral_back ( bridge_set<T1, T2, V> & bs_, T1 const & v_ ) noexcept {
    return corral_back ( bs_, bridge<T1, T2, V>{ v_, T2{ }, V{} } );
}
template<typename T1, typename T2, typename V>
bridge_set_iterator<T1, T2, V> corral_back ( bridge_set<T1, T2, V> & bs_, T2 const & v_ ) noexcept {
    return corral_back ( bs_, bridge<T1, T2, V>{ T1{ }, v_, V{} } );
}

template<typename T1, typename T2, typename V>
void remove_front ( bridge_set<T1, T2, V> & bs_, bridge_set_iterator<T1, T2, V> const & it_ ) noexcept {
    bs_.erase ( bs_.begin ( ), it_ );
}
template<typename T1, typename T2, typename V>
void remove_back ( bridge_set<T1, T2, V> & bs_, bridge_set_iterator<T1, T2, V> const & it_ ) noexcept {
    bs_.erase ( it_, bs_.end ( ) );
}

template<typename T1, typename T2, typename V>
bridge_set_iterator<T1, T2, V> find_one_single ( bridge_set<T1, T2, V> const & bs_, bridge<T1, T2, V> const & v_ ) noexcept {
    return bs_.unordered_find_single ( v_ );
}

} // namespace sax

template<typename T, typename V, typename U>
struct tls_node {

    T * instance;
    std::thread::id thread;
    alignas ( alignof ( V ) ) U data;

    static constexpr std::size_t data_offset = offsetof ( tls_node, data );

    [[nodiscard]] bool operator== ( tls_node const & rhs_ ) const noexcept {
        if ( std::thread::id{ } == rhs_.thread )
            return instance == rhs_.instance;
        if ( nullptr == rhs_.instance )
            return thread == rhs_.thread;
        return instance == rhs_.instance and thread == rhs_.thread;
    }

    template<typename stream>
    [[maybe_unused]] friend stream & operator<< ( stream & out_, tls_node const & b_ ) noexcept {
        if constexpr ( std::is_same<typename stream::char_type, wchar_t>::value )
            out_ << L'<' << b_.instance << L' ' << b_.thread << L'>';
        else
            out_ << '<' << b_.instance << ' ' << b_.thread << '>';
        return out_;
    }
};

template<typename T, typename V, template<typename> typename A = std::allocator>
class instance_tls {

    using un_initialized        = std::array<char, sizeof ( V )>;
    using tls_node              = tls_node<T, V, un_initialized>;
    using tls_node_set          = plf::list<tls_node, A<tls_node>>;
    using tls_node_set_iterator = typename tls_node_set::iterator;

    tls_node_set data;

    tls_node_set_iterator corral_front ( tls_node const & v_ ) noexcept {
        data.sort ( [ v_ ] ( auto a, auto b ) {
            return v_.instance == a.instance ? v_.instance == b.instance ? a.thread < b.thread : true : false;
        } );
        return std::find_if_not ( data.begin ( ), data.end ( ), [ v_ ] ( auto v ) { return v_.instance == v.instance; } );
    }

    public:
    [[nodiscard]] V * storage ( T * instance_ ) {
        tls_node_set_iterator it =
            data.unordered_find_single ( tls_node{ instance_, std::this_thread::get_id ( ), un_initialized{} } );
        return data.end ( ) != it
                   ? reinterpret_cast<V *> ( std::addressof ( it->data ) )
                   : new ( std::addressof (
                         data.emplace ( std::forward<T *> ( instance_ ), std::this_thread::get_id ( ), un_initialized{ } )->data ) )
                         V{ };
    }

    void destroy_storage ( T * instance_, std::thread::id thread_ ) noexcept {
        tls_node_set_iterator it  = corral_back ( tls_node{ std::forward<T *> ( instance_ ),
                                                           std::forward<std::thread::id> ( thread_ ), un_initialized{} } ),
                              end = data.end ( );
        while ( it != end ) {
            it->data.~V ( );
            it = data.erase ( it );
        }
    }
    void destroy_storage ( T * instance_ ) noexcept { destroy_storage ( std::forward<T *> ( instance_ ), std::thread::id{ } ); }
    void destroy_storage ( std::thread::id thread_ ) noexcept {
        destroy_storage ( nullptr, std::forward<std::thread::id> ( thread_ ) );
    }
};

int main ( ) {

    instance_tls<Bar, int> ins;

    exit ( 0 );

    using bs = sax::bridge_set<int, int, std::size_t>;
    using b  = sax::bridge<int, int, std::size_t>;

    bs s;

    s.emplace_back ( b{ 1, 3, 100 } );
    s.emplace_back ( b{ 3, 1, 563 } );
    s.emplace_back ( b{ 5, 8, 787 } );
    s.emplace_back ( b{ 1, 7, 898 } );
    s.emplace_back ( b{ 7, 8, 786 } );
    s.emplace_back ( b{ 3, 5, 524 } );
    s.emplace_back ( b{ 8, 2, 968 } );
    s.emplace_back ( b{ 1, 1, 657 } );
    s.emplace_back ( b{ 3, 2, 970 } );
    s.emplace_back ( b{ 5, 3, 987 } );
    s.emplace_back ( b{ 1, 8, 857 } );

    b e = { 5, 0, 0 };

    std::cout << *sax::find_one_single ( s, e ) << nl;

    exit ( 0 );

    {
        std::cout << "std::vector (not reserved)" << nl;
        StdVec vec;

        plf::nanotimer timer;
        timer.start ( );

        emplace_back_low_workload ( vec, 4'000'000 );

        std::uint64_t duration = static_cast<std::uint64_t> ( timer.get_elapsed_ms ( ) );
        std::cout << duration << "ms" << sp << vec.size ( ) << nl;
    }

    {
        std::cout << "std::vector (fully reserved)" << nl;
        StdVec vec;
        vec.reserve ( 4'000'000 );

        plf::nanotimer timer;
        timer.start ( );

        emplace_back_low_workload ( vec, 4'000'000 );

        std::uint64_t duration = static_cast<std::uint64_t> ( timer.get_elapsed_ms ( ) );
        std::cout << duration << "ms" << sp << vec.size ( ) << nl;
    }

    {
        std::cout << "sax::vm_vector" << nl;
        SaxVec vec;

        plf::nanotimer timer;
        timer.start ( );

        emplace_back_low_workload ( vec, 4'000'000 );

        std::uint64_t duration = static_cast<std::uint64_t> ( timer.get_elapsed_ms ( ) );
        std::cout << duration << "ms" << sp << vec.size ( ) << nl;
    }

    {
        std::cout << "sax::vm_concurrent_vector" << nl;
        SaxConVec vec;

        std::uint64_t duration;
        plf::nanotimer timer;
        timer.start ( );

        for ( int n = 0; n < 4; ++n )
            std::jthread{ emplace_back_low_workload<SaxConVec>, std::ref ( vec ), 1'000'000 };

        duration = static_cast<std::uint64_t> ( timer.get_elapsed_ms ( ) );
        std::cout << duration << "ms" << sp << vec.size ( ) << nl;
    }

    {
        std::cout << "tbb::concurrent_vector" << nl;
        TbbVec vec;

        std::uint64_t duration;
        plf::nanotimer timer;
        timer.start ( );

        for ( int n = 0; n < 4; ++n )
            std::jthread{ emplace_back_low_workload<TbbVec>, std::ref ( vec ), 1'000'000 };

        duration = static_cast<std::uint64_t> ( timer.get_elapsed_ms ( ) );
        std::cout << duration << "ms" << sp << vec.size ( ) << nl;
    }

    return EXIT_SUCCESS;
}

/*
void sort_second ( bridge_set & bs_, void * p_ ) noexcept {
    bs_.sort ( [] ( auto l, auto r ) { return reinterpret_cast<char *> ( l.second ) < reinterpret_cast<char *> ( r.second ); }
);
}

auto find_first ( bridge_set & bs_, void * p_ ) noexcept {
    return std::lower_bound ( bs_.begin ( ), bs_.end ( ), p_, [] ( auto l, auto r ) {
        return reinterpret_cast<char *> ( l.first ) < reinterpret_cast<char *> ( r.first );
    } );
}

auto find_second ( bridge_set & bs_, void * p_ ) noexcept {
    return std::lower_bound ( bs_.begin ( ), bs_.end ( ), p_, [] ( auto l, auto r ) {
        return reinterpret_cast<char *> ( l.second ) < reinterpret_cast<char *> ( r.second );
    } );
}
*/
