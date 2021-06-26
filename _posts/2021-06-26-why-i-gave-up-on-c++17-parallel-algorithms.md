---
layout: post
title: "Why I gave up on C++17 parallel algorithms"
tags:
    - computer graphics
    - optimization
---

I have been working on a new renderer lately, named [SoL](https://github.com/madmann91/sol).
It is essentially a modern rewrite of an old renderer that I implemented 5 to 6 years ago, using modern C++ features (C++20, actually).
One of the main things I wanted to achieve was to use C++17 parallel algorithms, since they were touted as the next best thing that
was going to solve all parallel needs of C++ programmers.

But it turned out this was a lie. First of all, C++ parallel algorithms are not available everywhere.
That is already a problem on its own, but initially I decided that those platforms that do not support parallel algorithms should just work
serially until an implementation is available, which would happen _eventually_.
Sadly, as of June 2021, it still has not happened for clang (support is missing in libc++).
On GCC (libstdc++), support is there but requires to install TBB, which is far from optimal,
not just because I have to install TBB before using parallel algorithms, but also because I have to make
sure I install and use _precisely_ the same version of TBB as the one used in libstdc++.
Granted, the Linux distribution I use (Fedora) does packaging well, so it uses the right TBB version for libstdc++,
but that also means I cannot use a newer TBB version if I want to use TBB directly, and not through C++'s parallel algorithms.
Finally, since libstdc++'s implementation uses deprecated features of TBB,
a lot of warnings pop up when compiling code that includes the standard header `<execution>`:
With the move to oneAPI, and the subsequent rename to oneTBB, low-level features like `tbb::task` have been deprecated.

On its own, the previous paragraph already shows that using parallel algorithms right now is an uphill battle.
But the final nail in the coffin is that C++ parallel algorithms are not even well designed.
Most parallel algorithms I write use indices, not iteration over containers.
In that context, how can I possibly parallelize a loop when literally _every_ parallel algorithm requires an iterator?
C++20 introduces `std::views::iota`, but it turns out that it is not guaranteed to meet the requirements for `Cpp17ForwardIterator`,
which _may_ make the call to `std::for_each` incorrect (see [this stack overflow thread](https://stackoverflow.com/a/53702500)).

So what is the take away here?
First, that C++17's parallel algorithms are not in a state where they can be used in production right now.
Second, that they are also not providing the functionality that I need.
While it is difficult to generalize my experience with those algorithms, I feel that a _parallel for loop_ is extremely common
and should at least have been provided by the standard.
We are now in 2020, and there is still no way to do this in a simple, reliable manner in C++.

How to solve this problem then? I think I am going to switch back to OpenMP.
It offers more control, it is better supported (except on MSVC where only OpenMP 2.0 is supported, but better support is coming, see [this blog post](https://devblogs.microsoft.com/cppblog/improved-openmp-support-for-cpp-in-visual-studio)),
and is easier to work with. What is not to love about it?
