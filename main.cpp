
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

#include "rooted_tree.hpp"

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstdlib>

#include <array>
#include <atomic>
#include <type_traits>

#include <plf/plf_nanotimer.h>

#include <sax/iostream.hpp>
#include <sax/prng_sfc.hpp>
#include <sax/uniform_int_distribution.hpp>

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
// A global instance of a C++ implementation of Chris Doty-Humphrey's Small Fast Chaotic Prng.
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
    int value                         = 0;
    Foo ( ) noexcept                  = default;
    Foo ( Foo const & foo_ ) noexcept = default;

    explicit Foo ( int const & i_ ) noexcept : value{ i_ } {}
    explicit Foo ( int && i_ ) noexcept : value{ std::move ( i_ ) } {}
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

int main ( ) {

    using Tree = SequentailTree;

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
