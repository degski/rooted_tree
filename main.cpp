
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
// A global instance of a C++ implementation
// of Chris Doty-Humphrey's Small Fast Chaotic Prng.
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

template<typename Hook>
struct foo : Hook {
    int value = 0;
    foo ( ) noexcept : Hook ( ) {}
    foo ( foo const & foo_ ) noexcept : Hook ( ), value{ foo_.value } {}
    explicit foo ( int const & i_ ) noexcept : Hook ( ), value{ i_ } {}
    explicit foo ( int && i_ ) noexcept : Hook ( ), value{ std::move ( i_ ) } {}
};

using ConcurrentTree = sax::concurrent_rooted_tree<foo<sax::crt_hook>>;
using SequentailTree = sax::rooted_tree<foo<sax::rt_hook>>;

template<typename Tree>
void add_nodes ( Tree & tree_, int n_ ) {
    for ( int i = 2; i < n_; ++i )
        tree_.emplace ( sax::nid{ sax::uniform_int_distribution<int> ( 1, static_cast<int> ( tree_.nodes.size ( ) ) - 1 ) ( rng ) },
                        i );
}

int main ( ) {

    {
        SequentailTree tree ( 1 );

        plf::nanotimer timer;

        timer.start ( );

        add_nodes ( tree, 4'000'000 - 2 );

        std::uint64_t duration = static_cast<std::uint64_t> ( timer.get_elapsed_ms ( ) );

        std::cout << duration << "ms" << nl << nl;
        std::cout << tree.nodes.size ( ) << nl;
    }

    {
        ConcurrentTree tree ( 1 );

        std::vector<std::thread> threads;
        threads.reserve ( 4 );

        plf::nanotimer timer;

        timer.start ( );

        for ( int n = 0; n < 4; ++n )
            threads.emplace_back ( add_nodes<ConcurrentTree>, std::ref ( tree ), 1'000'000 + 1 );
        for ( std::thread & t : threads )
            t.join ( );

        std::uint64_t duration = static_cast<std::uint64_t> ( timer.get_elapsed_ms ( ) );

        std::cout << duration << "ms" << nl << nl;
        std::cout << tree.nodes.size ( ) << nl;
    }

    return EXIT_SUCCESS;
}
