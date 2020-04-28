
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

#include <sax/iostream.hpp>

struct foo : sax::crt_meta_data {

    short value = 0;

    foo ( ) noexcept : sax::crt_meta_data{ } {
        ++up.id; // set flag to constructed
    }
    foo ( foo const & foo_ ) noexcept :
        sax::crt_meta_data{ sax::NodeIDAtom{ }, foo_.prev, foo_.tail, foo_.size }, value{ foo_.value } {
        ++up.id; // set flag to constructed
    }
    explicit foo ( short && i_ ) noexcept : sax::crt_meta_data{ }, value{ std::move ( i_ ) } {
        ++up.id; // set flag to constructed
    }
};

int main ( ) {

    sax::concurrent_rooted_tree<foo> tree;

    tree.emplace_root ( short{ 1 } );

    std::cout << tree.nodes.size ( ) << nl;

    return EXIT_SUCCESS;
}
