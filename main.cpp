
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
// FITNESS FOR key_type_one PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.

#include "vm_backed.hpp"
#include "rooted_tree.hpp"

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstdlib>

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
// key_type_one global instance of a C++ implementation of Chris Doty-Humphrey's Small Fast Chaotic Prng.
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

template<typename key_type_one, typename key_type_two, typename type, typename allocator = std::allocator<type>>
class alignas ( 64 ) bimap {

    static_assert ( not std::is_same<key_type_one, key_type_two>::value, "types must be distinct" );

    template<typename KT, typename PKT, typename PDT>
    struct node {
        KT key;
        mutable PKT other;
        PDT * data;
    };

    template<typename KT, typename PKT, typename PDT>
    struct compare {
        [[nodiscard]] bool operator( ) ( node<KT, PKT, PDT> const & l_, node<KT, PKT, PDT> const & r_ ) const noexcept {
            return l_.key < r_.key;
        }
    };

    template<typename KT, typename PKT, typename PDT>
    [[nodiscard]] friend bool operator== ( node<KT, PKT, PDT> const & l_, node<KT, PKT, PDT> const & r_ ) noexcept {
        return l_.key == r_.key;
    }

    public:
    using value_type        = type;
    using value_type_colony = plf::colony<value_type, allocator>;

    using pointer         = value_type *;
    using const_pointer   = value_type const *;
    using reference       = value_type &;
    using const_reference = value_type const &;
    using rv_reference    = value_type &&;

    using iterator       = typename value_type_colony::iterator;
    using const_iterator = typename value_type_colony::const_iterator;

    private:
    template<typename T1, typename T2>
    using map = std::set<node<T1, T2 *, value_type>, compare<T1, T2 *, value_type>,
                         typename allocator::template rebind<node<T1, T2 *, value_type>>::other>;

    using map_one_type = map<key_type_one, key_type_two>;
    using map_two_type = map<key_type_two, key_type_one>;

    map_one_type key_map_one;
    map_two_type key_map_two;

    value_type_colony data;

    [[maybe_unused]] reference insert ( key_type_one const & k1_, key_type_two const & k2_, pointer p_ ) {
        auto two_it   = key_map_two.insert ( { k2_, nullptr, p_ } ).first;
        two_it->other = const_cast<key_type_one *> ( std::addressof (
            key_map_one.insert ( { k1_, const_cast<key_type_two *> ( std::addressof ( two_it->key ) ), p_ } ).first->key ) );
        return *p_;
    }

    public:
    [[maybe_unused]] reference insert ( key_type_one const & k1_, key_type_two const & k2_, rv_reference v_ ) {
        return insert ( k1_, k2_, std::addressof ( *data.insert ( std::forward<value_type> ( v_ ) ) ) );
    }
    [[maybe_unused]] reference insert ( key_type_two const & k2_, key_type_one const & k1_, rv_reference v_ ) {
        return insert ( k1_, k2_, std::forward<value_type> ( v_ ) );
    }
    [[maybe_unused]] reference insert ( key_type_one const & k1_, key_type_two const & k2_, const_reference v_ ) {
        return insert ( k1_, k2_, std::addressof ( *data.insert ( v_ ) ) );
    }
    [[maybe_unused]] reference insert ( key_type_two const & k2_, key_type_one const & k1_, const_reference v_ ) {
        return insert ( k1_, k2_, v_ );
    }

    template<typename... Args>
    [[maybe_unused]] reference emplace ( key_type_one const & k1_, key_type_two const & k2_, Args &&... v_ ) {
        return insert ( k1_, k2_, std::addressof ( *data.emplace ( std::forward<Args> ( v_ )... ) ) );
    }
    template<typename... Args>
    [[maybe_unused]] reference emplace ( key_type_two const & k2_, key_type_one const & k1_, Args &&... v_ ) {
        return emplace ( k1_, k2_, std::forward<Args> ( v_ )... );
    }

    [[nodiscard]] pointer find ( key_type_one const & k1_ ) noexcept {
        auto it = key_map_one.find ( { k1_, nullptr, nullptr } );
        return key_map_one.end ( ) != it ? it->data : nullptr;
    }
    [[nodiscard]] pointer find ( key_type_two const & k2_ ) noexcept {
        auto it = key_map_two.find ( { k2_, nullptr, nullptr } );
        return key_map_two.end ( ) != it ? it->data : nullptr;
    }
    [[nodiscard]] const_pointer find ( key_type_one const & k1_ ) const noexcept {
        auto it = key_map_one.find ( { k1_, nullptr, nullptr } );
        return key_map_one.end ( ) != it ? it->data : nullptr;
    }
    [[nodiscard]] const_pointer find ( key_type_two const & k2_ ) const noexcept {
        auto it = key_map_two.find ( { k2_, nullptr, nullptr } );
        return key_map_two.end ( ) != it ? it->data : nullptr;
    }

    [[nodiscard]] reference find_existing ( key_type_one const & k1_ ) noexcept {
        return *key_map_one.find ( { k1_, nullptr, nullptr } )->data;
    }
    [[nodiscard]] reference find_existing ( key_type_two const & k2_ ) noexcept {
        return *key_map_two.find ( { k2_, nullptr, nullptr } )->data;
    }
    [[nodiscard]] const_reference find_existing ( key_type_one const & k1_ ) const noexcept {
        return *key_map_one.find ( { k1_, nullptr, nullptr } )->data;
    }
    [[nodiscard]] const_reference find_existing ( key_type_two const & k2_ ) const noexcept {
        return *key_map_two.find ( { k2_, nullptr, nullptr } )->data;
    }

    void erase ( key_type_one const * k1_ ) noexcept {
        if ( k1_ ) {
            auto one_it = key_map_one.find ( { k1_, nullptr, nullptr } );
            if ( key_map_one.end ( ) != one_it ) {
                data.erase ( data.get_iterator_from_pointer ( one_it->data ) );
                pointer one_data = one_it->data;
                for ( auto two_it = key_map_two.begin ( ), two_end = key_map_two.end ( ); two_it != two_end; ) {
                    if ( one_data == two_it->data )
                        two_it = key_map_two.erase ( two_it );
                    else
                        ++two_it;
                }
                key_map_one.erase ( one_it );
            }
        }
    }

    void erase ( key_type_two const * k2_ ) noexcept {
        if ( k2_ ) {
            auto two_it = key_map_two.find ( { k2_, nullptr, nullptr } );
            if ( key_map_two.end ( ) != two_it ) {
                data.erase ( data.get_iterator_from_pointer ( two_it->data ) );
                pointer two_data = two_it->data;
                for ( auto one_it = key_map_one.begin ( ), one_end = key_map_one.end ( ); one_it != one_end; ) {
                    if ( two_data == one_it->data )
                        one_it = key_map_one.erase ( one_it );
                    else
                        ++one_it;
                }
                key_map_two.erase ( two_it );
            }
        }
    }

    void erase_existing ( key_type_one const & k1_ ) noexcept {
        auto one_it = key_map_one.find ( { k1_, nullptr, nullptr } );
        data.erase ( data.get_iterator_from_pointer ( one_it->data ) );
        pointer one_data = one_it->data;
        for ( auto two_it = key_map_two.begin ( ), two_end = key_map_two.end ( ); two_it != two_end; ) {
            if ( one_data == two_it->data )
                two_it = key_map_two.erase ( two_it );
            else
                ++two_it;
        }
        key_map_one.erase ( one_it );
    }

    void erase_existing ( key_type_two const & k2_ ) noexcept {
        auto two_it = key_map_two.find ( { k2_, nullptr, nullptr } );
        data.erase ( data.get_iterator_from_pointer ( two_it->data ) );
        pointer two_data = two_it->data;
        for ( auto one_it = key_map_one.begin ( ), one_end = key_map_one.end ( ); one_it != one_end; ) {
            if ( two_data == one_it->data )
                one_it = key_map_one.erase ( one_it );
            else
                ++one_it;
        }
        key_map_two.erase ( two_it );
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
        iterator two_it, two_it_end;

        key_one_iterator ( bimap & map_, key_type_one const & k1_ ) noexcept :
            map ( map_ ), one_data ( map.key_map_one.find ( { k1_, nullptr, nullptr } )->data ),
            two_it ( map.key_map_two.begin ( ) ), two_it_end ( map.key_map_two.end ( ) ) {
            while ( two_it_end != two_it ) {
                if ( one_data == two_it->data )
                    break;
                else {
                    ++two_it;
                }
            }
        }

        [[maybe_unused]] key_one_iterator & operator++ ( ) noexcept {
            while ( two_it_end != two_it ) {
                if ( one_data == two_it->data )
                    break;
                else {
                    ++two_it;
                }
            }
            return *this;
        }
        [[nodiscard]] key_type_two operator* ( ) const noexcept { return two_it->key; }
        [[nodiscard]] bool is_valid ( ) const noexcept { return two_it_end != two_it; }
    };

    class const_key_one_iterator {

        friend class bimap;

        using const_iterator = typename map_two_type::const_iterator;

        bimap const & map;
        const_pointer one_data;
        const_iterator two_it, two_it_end;

        const_key_one_iterator ( bimap const & map_, key_type_one const & k1_ ) noexcept :
            map ( map_ ), one_data ( map.key_map_one.find ( { k1_, nullptr, nullptr } )->data ),
            two_it ( map.key_map_two.begin ( ) ), two_it_end ( map.key_map_two.end ( ) ) {
            while ( two_it_end != two_it ) {
                if ( one_data == two_it->data )
                    break;
                else {
                    ++two_it;
                }
            }
        }

        [[maybe_unused]] const_key_one_iterator & operator++ ( ) noexcept {
            while ( two_it_end != two_it ) {
                if ( one_data == two_it->data )
                    break;
                else {
                    ++two_it;
                }
            }
            return *this;
        }
        [[nodiscard]] key_type_two operator* ( ) const noexcept { return two_it->key; }
        [[nodiscard]] bool is_valid ( ) const noexcept { return two_it_end != two_it; }
    };

    class key_two_iterator {

        friend class bimap;

        using iterator = typename map_one_type::iterator;

        bimap & map;
        pointer two_data;
        iterator one_it, one_it_end;

        key_two_iterator ( bimap & map_, key_type_two const & k1_ ) noexcept :
            map ( map_ ), two_data ( map.key_map_two.find ( { k1_, nullptr, nullptr } )->data ),
            one_it ( map.key_map_one.begin ( ) ), one_it_end ( map.key_map_one.end ( ) ) {
            while ( one_it_end != one_it ) {
                if ( two_data == one_it->data )
                    break;
                else {
                    ++one_it;
                }
            }
        }

        [[maybe_unused]] key_two_iterator & operator++ ( ) noexcept {
            while ( one_it_end != one_it ) {
                if ( two_data == one_it->data )
                    break;
                else {
                    ++one_it;
                }
            }
            return *this;
        }
        [[nodiscard]] key_type_one operator* ( ) const noexcept { return one_it->key; }
        [[nodiscard]] bool is_valid ( ) const noexcept { return one_it_end != one_it; }
    };

    class const_key_two_iterator {

        friend class bimap;

        using const_iterator = typename map_one_type::const_iterator;

        bimap const & map;
        const_pointer two_data;
        const_iterator one_it, one_it_end;

        const_key_two_iterator ( bimap const & map_, key_type_two const & k1_ ) noexcept :
            map ( map_ ), two_data ( map.key_map_two.find ( { k1_, nullptr, nullptr } )->data ),
            one_it ( map.key_map_one.begin ( ) ), one_it_end ( map.key_map_one.end ( ) ) {
            while ( one_it_end != one_it ) {
                if ( two_data == one_it->data )
                    break;
                else {
                    ++one_it;
                }
            }
        }

        [[maybe_unused]] const_key_two_iterator & operator++ ( ) noexcept {
            while ( one_it_end != one_it ) {
                if ( two_data == one_it->data )
                    break;
                else {
                    ++one_it;
                }
            }
            return *this;
        }
        [[nodiscard]] key_type_one operator* ( ) const noexcept { return one_it->key; }
        [[nodiscard]] bool is_valid ( ) const noexcept { return one_it_end != one_it; }
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

int main ( ) {

    // https://stackoverflow.com/a/21917041

    bimap<int, std::string, int> m;

    int a         = 6;
    std::string b = "hello";

    m.insert ( a, b, 4 );

    std::cout << m.find ( 6 ) << nl;
    // std::cout << *m.find ( b ) << nl;

    std::cout << sizeof ( bimap<int, std::string, int> ) << nl;
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
