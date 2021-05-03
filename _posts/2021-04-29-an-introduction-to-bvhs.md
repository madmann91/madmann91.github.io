---
layout: post
title: "An Introduction to BVHs"
usemathjax: true
tags:
    - bvh
    - ray tracing
    - computer graphics
    - optimization
---

{% include mathjax.html %}

# Introduction

The [last series of posts]({% link _posts/2020-12-28-bvhs-part-1.md %}) explained low-level details that are relevant to BVH construction and traversal.
However, after my interactions with people in the Graphics Programming Discord, it appears that good introductory material on BVHs is not really available.
There's [PBRT](https://www.pbr-book.org/), but the code that is given there is pretty much over-engineered and sub-optimal.

Therefore, I settled to write a small introductory post here that will cover the basics, and that can serve as a strong implementation to build upon.
Its performance will not be optimal nor very poor, just average. Similarly, the code presented here will---hopefully---be simple to follow.
Readers that want the best performance should implement this first, then apply the optimizations mentioned in the previous blog posts.

The code presented here is going to be C++, but it should be straightforward to convert into any other programming language.
It is available in full [here](/assets/bvh.cpp) (see [Running and Testing the Example Code](#running-and-testing-the-example-code), at the bottom of this post).

# What is a BVH?

At this point you might feel that you know what a BVH is, but for the sake of completeness I am going to state that again here.
A BVH, or Bounding Volume Hierarchy, is a tree in which each node has a bounding volume (typically an axis-aligned box), and each leaf contains primitives.
To clarify the terminology used in this post: Leaves are the nodes of the tree that do not have children.
In particular, this means that _leaves are nodes_. We refer to nodes that are not leaves as _internal nodes_.

![](/assets/bvh.svg)

Above is a simple BVH for 4 primitives, made of 3 nodes: 1 internal node (the root), and 2 leaves, each containing 2 primitives.
From this example, it should become apparent that BVHs are _object partitioning_ data structures: They partition the set of objects, not space.
This has the important consequence that in a BVH, the bounding volumes of two different nodes _can overlap_.
This in turn will have practical consequences on the design of a ray-BVH intersection routine.

Before we move on, there are important facts you need to know:

- BVHs are trees. Standard properties on trees apply, including relations between the number of nodes and the number of leaves.
  If you have a binary BVH, for instance, the following relation holds:

  $$N_{nodes} = 2 \times N_{leaves} - 1$$

  This relation can be used to pre-allocate nodes in an array, as we will see soon.
  As an exercise, you can try proving this property using induction.

- Object partitioning data structures do not duplicate primitives.
  Therefore, it is not necessary to store lists or vector of primitives in the leaves.
  Instead, it is enough to store a range of primitives that is covered by each leaf, and permute primitives according to the order defined by the BVH.
  In the example above, for instance, the primitives can be laid out in an array in the order: star, circle, rectangle, triangle.
  Then, the first leaf (in red) may cover the range defined by the first two primitives (from index 0 to index 1, included),
  and the second one (in blue) may cover the range defined by the last two (from index 2 to index 3, included).

Hopefully, you now have a good understanding of what a BVH should be.
At this point, we can start by writing some basic code.

# The Basics

Before going on to the BVH-related data types, let us first introduce some simple helpers: a vector type, and a bounding box type.

```cpp
#include <algorithm>
#include <limits>

struct Vec3 {
    float values[3];

    Vec3() = default;
    Vec3(float x, float y, float z) : values { x, y, z } {}
    explicit Vec3(float x) : Vec3(x, x, x) {}

    float& operator [] (int i) { return values[i]; }
    float operator [] (int i) const { return values[i]; }
};

inline Vec3 operator + (const Vec3& a, const Vec3& b) {
    return Vec3(a[0] + b[0], a[1] + b[1], a[2] + b[2]);
}

inline Vec3 operator - (const Vec3& a, const Vec3& b) {
    return Vec3(a[0] - b[0], a[1] - b[1], a[2] - b[2]);
}

inline Vec3 operator * (const Vec3& a, const Vec3& b) {
    return Vec3(a[0] * b[0], a[1] * b[1], a[2] * b[2]);
}

inline Vec3 min(const Vec3& a, const Vec3& b) {
    return Vec3(std::min(a[0], b[0]), std::min(a[1], b[1]), std::min(a[2], b[2]));
}

inline Vec3 max(const Vec3& a, const Vec3& b) {
    return Vec3(std::max(a[0], b[0]), std::max(a[1], b[1]), std::max(a[2], b[2]));
}

struct BBox {
    Vec3 min, max;

    BBox() = default;
    BBox(const Vec3& min, const Vec3& max) : min(min), max(max) {}
    explicit BBox(const Vec3& point) : BBox(point, point) {}

    BBox& extend(const BBox& other) {
        min = ::min(min, other.min);
        max = ::max(max, other.max);
        return *this;
    }

    Vec3 diagonal() const {
        return max - min;
    }

    int largest_axis() const {
        auto d = diagonal();
        int axis = 0;
        if (d[axis] < d[1]) axis = 1;
        if (d[axis] < d[2]) axis = 2;
        return axis;
    }

    float half_area() const {
        auto d = diagonal();
        return (d[0] + d[1]) * d[2] + d[0] * d[1];
    }

    static BBox empty() {
        return BBox(
            Vec3(-std::numeric_limits<float>::max()),
            Vec3(+std::numeric_limits<float>::max()));
    }
};
```

This code above defines the `Vec3` and `BBox` types, and adds some helpers to create an empty bounding box, or extend a bounding box to cover another one.
Note also that particular design where we return a reference type from `extend()`.
This is particularly helpful when creating bounding boxes on the fly, as it allows to chain calls to `extend()`:

```cpp
bbox.extend(a).extend(b)
```

Also, note how we avoid doing things manually for every vector component: We first define a min/max function for vectors, and then use this in `extend()`.
From reading the source code of other people's implementations, this is too often forgotten and done manually, for no good reason.

# Building a BVH

Now that the basics are covered, we can start to explain how to build a BVH, step by step.
The algorithm we are going to use is explained in _On fast Construction of SAH-based Bounding Volume Hierarchies_, by I. Wald.
This algorithm operates from the top to the bottom, and tries to build good trees by approximating a cost function for BVHs, called the Surface Area Heuristic (SAH).
Approximation is done by placing primitives in _bins_, which is why this algorithm is often referred to as _binning_ BVH construction.
It is a decent building algorithm that has some great properties: It produces decent trees, it is relatively fast, and finally, it is easy to implement.

## Defining a BVH Node Datatype

Now, it is time to define a BVH node.
We already mentioned that BVH leaves are just a range of primitives (two integers) and a bounding box.
What about internal nodes?
Well, we can think about this a bit: Typically, if you have implemented binary trees before, you may want to use two pointers.
That is a valid approach, but It will complicate things if you want to store your BVH to disk, or copy it to GPU memory (because you will have to translate pointers).
Moreover, two pointers take more space than two integers (those that we already need for leaves).

There is a very simple solution to this problem, too often ignored by people that implement BVHs for the first time.
That solution is to use indices into an array.
Immediately, you must be thinking: "But wait! This means the nodes now have to be in a linear array. Isn't that harder to implement?".
I can answer that question by saying that it absolutely is not more complicated than using pointers.
In fact, it is in many ways simpler, particularly in terms of memory management since you only have to destroy one entire array, instead of deleting individual nodes.

Now that is has been cleared out, you probably think: "I need two indices, one for the left child, and one for the right child".
That would be correct, if you were not enforcing any particular convention in how you place children together, or with respect to their parent.
However, with a little bit of thinking, we can reduce that to one index only.
This is important because it leaves us space to encode the fact that the node is a leaf, without sacrificing any bit anywhere in our indices.

There are at least two conventions that can help with that to my knowledge:

- Placing children next to each other.
  You only then need to store the index of the first child, the second is given as that index plus one.
  This is the strategy that I recommend, because it's simple to implement and has the advantage that two children live in the same cache line on most architectures,
  which helps if you intersect two children at a time in the traversal routine (see the [previous post]({% link _posts/2021-01-06-bvhs-part-2.md %}) on BVH traversal optimization).

- Placing the largest child next to the parent.
  You only then need to store the index of the other child, since you know the first one is at the index of the parent plus one.

In this post, we are going to use the first strategy, but the other one is a valid choice as well.
With this in mind, here's the final data type for a BVH node:

```cpp
struct Node {
    BBox bbox;
    uint32_t prim_count;
    uint32_t first_index;

    Node() = default;
    Node(const BBox& bbox, uint32_t prim_count, uint32_t first_index)
        : bbox(bbox), prim_count(prim_count), first_index(first_index)
    {}

    bool is_leaf() const { return prim_count != 0; }
    /* ... */
};
```

This should be a bit anticlimactic, because it is deceptively simple.
We encode the fact that a node is a leaf by setting the `prim_count` member to something different than 0.
For leaves, `first_index` then points to the first primitive in the array.
For internal nodes, `first_index` points to the first child (the second one is placed at `first_index + 1`, as discussed above).

## Defining a BVH Datatype

A very important question for the problem at hand is: What is the input of a BVH construction algorithm?
In our case, the BVH construction algorithm that we chose, like many others, only needs an array of bounding boxes and an array of primitive centers.
It is _extremely important_ to design your algorithm like this, and not by passing primitives as an input directly.
The reason for this is that it will allow you to re-use the same construction algorithm for BVHs over different primitive types, _including over other BVHs_ (for instancing, a.k.a. top-level BVHs).
Of course, not all construction algorithms allow to do that: For instance, building an SBVH requires to have a way to split primitives, which you can only do if you have access to the original primitive.
However, I do not recommend using SBVHs, for reasons that are out of the scope of this blog post.

In any case, the input of our algorithm is set: two arrays, one for bounding boxes and the other for centers.
Now, what is the output of the construction algorithm?
Of course, it should be clear that we will build a BVH, but how do we represent a BVH to start with?
The answer to that is also pretty simple.
I have mentioned earlier that we opted for a scheme where two children were placed next to each other in an array.
Therefore, a BVH is simply an array of nodes.
However, during construction, we only have access to bounding boxes and primitive centers.
We do not have access to the primitive data (for very good reasons, described earlier).
Thus, we need to have a way to connect the primitive data to those bounding boxes and centers.
We can achieve this by storing primitive indices in the BVHs.
They will be used during the construction phase, and can later be used by the client to permute the original primitive list, or used directly during traversal if the original data cannot be permuted.

With all that, we can define a BVH like so:

```cpp
#include <vector>
struct Bvh {
    std::vector<Node> nodes;
    std::vector<size_t> prim_indices;

    Bvh() = default;

    static Bvh build(const BBox* bboxes, const Vec3* centers, size_t prim_count);
    /* ... */
};
```

Again, this should be pretty simple once you understand the implementation choices we have made so far:
A BVH is just two plain arrays, one for nodes and one for primitive indices.

## Implementing a BVH Construction Procedure 

Now that we have defined all the pieces, we can build a BVH.
In practical terms, this means providing an implementation for the `build()` method of `Bvh`.

The algorithm we chose is, as mentioned above, a top-down construction method.
This means that it starts from the whole set of primitives, then splits it into two different sets, and continues until it terminates.
We thus will start by creating a leaf that contains the entire scene:

```cpp
#include <numeric>

static void build_recursive(Bvh& bvh, size_t node_index, size_t& node_count, const BBox* bboxes, const Vec3* centers);

Bvh Bvh::build(const BBox* bboxes, const Vec3* centers, size_t prim_count) {
    Bvh bvh;

    bvh.prim_indices.resize(prim_count);
    std::iota(bvh.prim_indices.begin(), bvh.prim_indices.end(), 0);

    bvh.nodes.resize(2 * prim_count - 1);
    bvh.nodes[0].prim_count = prim_count;
    bvh.nodes[0].first_index = 0;

    size_t node_count = 1;
    build_recursive(bvh, 0, node_count, bboxes, centers);
    bvh.nodes.resize(node_count);
    return bvh;
}
```

The code above starts by creating an empty BVH.
Then, the primitive indices are initialized with the identity permutation: `0, 1, 2, 3, ...`.
We could do this with a simple for-loop, but the standard library provides a function that does just this, named `std::iota`.
Then, we allocate enough space for nodes in the BVH.
As mentioned earlier, a BVH with `prim_count` leaves has at most `2 * prim_count - 1` nodes.
Note that this is an upper bound and not an exact count, since we may store more than one primitive per leaf.

After that, we initialize the root node (located at index 0) as a leaf with the range that covers the entire set of primitives.
The bounding box of the root node will be set later on, during the recursive call.

Finally, we call the recursive construction function, where we pass the BVH to build, the index of the node to split, the current node count, and the lists of bounding boxes and centers.
Once we return from the call, we can resize the array of nodes, and return the constructed BVH to the caller.
Since it is returned by value, it is subject to the return value optimization which prevents copies and uses moves instead.
Thus, there should be no worry about returning BVHs by value like this.

## Recursive Splitting

Now, we only need to specify the contents of the `build_recursive()` function.
At the beginning of the function, the node at `bvh.nodes[node_index]` must be a leaf.
With that leaf, this function should do the following:

1. Compute the bounding box of the leaf based on its contents,
2. Stop if the number of primitives is too small,
3. Try to split the leaf using binning,
4. Two options:
    a) If the split returned from binning is good, then split the leaf and call itself recursively with the two just constructed children,
    b) If the split returned from binning is bad, then terminate if the number of primitives in the leaf is small enough. If it is too large, fall back to some other method to produce a split (median split along the largest axis is a good choice).

This enumeration does not yet specify what a "bad" split is, but we will discuss that later in the post.
Still, based on this, we can already implement the first point:

```cpp
#include <cassert>
static void build_recursive(
    Bvh& bvh,
    size_t node_index,
    size_t& node_count,
    const BBox* bboxes,
    const Vec3* centers)
{
    auto& node = bvh.nodes[node_index];    
    assert(node.is_leaf());

    node.bbox = BBox::empty();
    for (size_t i = 0; i < node.prim_count; ++i)
        node.bbox.extend(bboxes[bvh.prim_indices[i]]);

    /* ... */
}
```

For point 2., we can simply compare the number of primitives with a constant that is small enough, and stop if it is below that threshold.
Typically, I use something between 2 or 4 primitives per leaf, but this number should be kept rather small.
The reason for this is that the "bad split" criterion should already cover the case where the leaf should not be split because there are too few primitives.
The only reason to have a constant above 1 is for performance, so that you don't attempt to split leaves that only have a very small number of primitives, only to find out later on that there is no good split anyway.

Ideally, we should pass this threshold, along with other build constants, as an argument to the construction algorithm,
but right now, for simplicity, I am going to define them as a global constant:

```cpp
struct BuildConfig {
    size_t min_prims;
    size_t max_prims;     // Other constant, used later
    float traversal_cost; // ditto
};

static constexpr BuildConfig build_config = { 2, 8, 1.0f };

static void build_recursive(/* ... */) {
    /* ... */
    if (node.prim_count < build_config.min_prims)
        return;
    /* ... */
}
```

Before we move on to point 3., let us first discuss the SAH.

## The Surface Area Heuristic

The Surface Area Heuristic (SAH) is a measure of how good a tree is for ray-tracing.
It is based on the assumption that rays start from outside the scene, and that they are uniformly distributed.
Moreover, it does not take into account occlusion.
Expressed in its recursive form, the SAH is the following function:

$$
C_{SAH}(P) = \left\{
    \begin{array}{ll}
        C_T + \frac{SA(L)}{SA(P)}\,C_{SAH}(L) + \frac{SA(R)}{SA(P)}\,C_{SAH}(R) & \text{for an inner node}\\
        C_I\,N(P) & \text{for a leaf}
    \end{array} \right.
$$

Here, $$SA(P)$$ denotes the surface area of the _bounding box_ of the node $$P$$.
It _is not_ the surface area of the primitives in the node.
The function $$N(P)$$ represents the number of primitives in the _leaf_ $$P$$.
Finally, the constants $$C_T$$ and $$C_I$$ represent the cost of traversing a node, and the cost of intersecting a ray with a primitive, respectively.

The reason for the use of the surface area of the bounding box is that the ratio of the area of the child divided by the area of the parent bounding box represents the probability of hitting the child, conditioned to having hit the parent (with the assumptions that were made above---no occlusion, rays start from outside the scene, and are uniformly distributed).
Thus, the SAH weights the cost of the left and right child by the probability of hitting them, which should make sense intuitevely.
It also should be pretty obvious that the larger the bounding box of the node is, the larger the chance that our ray
node intersection routine will return an intersection.

As it stands, the SAH cost is not really helpful, because of its recursive nature:
To compute the cost of a split, we would have to perform the split, and repeat that operation until we only have leaves, at which point we would be able to compute the cost of that subtree.
Doing this would be prohibitive in terms of performance:
We would essentially have to try to build many trees and choose the best a posteriori.
Instead of doing this, top-down builders choose to add another simplification:
They assume, when evaluating a split, that the two children will be leaves (even if that is clearly _not the case_ in practice).
With this simplification, the SAH becomes, for an inner node:

$$C'_{SAH}(P) = C_T + \frac{SA(L)}{SA(P)}\,C_I\,N(L) + \frac{SA(R)}{SA(P)}\,C_I\,N(R)$$

This simplification turns out to produce pretty good trees still, and in the literature you will find that this simplified SAH formula ($$C'_{SAH}$$)is referred to as "the SAH", just as often as the fully recursive one ($$C_{SAH}$$).

## Enumerating Possible Partitions

Now that we have a criterion to compare possible splits, and since BVHs partition the set of objects, we could just try every possible partition of our primitives.
For instance, if there are 3 primitives, $$A$$, $$B$$ and $$C$$, we would have to try the partitions:

- $$\{ A \}$$, $$\{ B, C \}$$,
- $$\{ A, B \}$$, $$\{ C \}$$,
- $$\{ A, C \}$$, $$\{ B \}$$

For an arbitrary set of primitives of size $$N$$, we can compute the total number of partitions to test.
Consider an algorithm that would enumerate all possibilities.
First, it would look at all the partitions which have one primitive in the one child, and everything else in the other:
There are $$N\choose{1}$$ such partitions.
Then, the algorithm would move on to partitions that have two primitives in one child, of which there are $$N\choose{2}$$, and so on.
When one child has $$\left\lfloor\frac{N}{2}\right\rfloor - k$$ primitives, the other has $$\left\lfloor\frac{N}{2}\right\rfloor + k$$, so, by symmetry, the algorithm would have listed all primitives when it reaches $$\left\lfloor\frac{N}{2}\right\rfloor$$ primitives in one node, at which point it should stop.

So, in total, the number of possible partitions is:

$$
\begin{array}{ll}
P(N) &= \sum_{i = 1}^{\left\lfloor\frac{N}{2}\right\rfloor}{N\choose{i}}\\
     &= \frac{1}{2}\sum_{i = 0}^{N}{N\choose{i}} - 1\\
     &= 2^{N - 1} - 1
\end{array}
$$

This number grows very fast with $$N$$, thus prohibiting any exhaustive search for the best partition. 
Instead, the strategy used by BVH builders is to sort primitives by the projection of their center on one axis, and then use that that order to partition the primitives, by evaluating each partition $$\{0, \dots, k\}, \{k + 1, \dots, N - 1\}$$ of the $$N$$ sorted input primitives.
In practice, if you want fast, low-quality BVHs, you can do that for only one axis (e.g. the largest axis of the node's bounding box), but if the best tree is desired, it is better to test all three axes and select the best partition across those.

## Binning Instead of Sorting

Instead of sorting the primitives on each axis, which can be slow (it is possible to do that only once at the beginning of a BVH build, but this is outside of the scope of this post), binning does some "approximate sorting".
The minimum and maximum bounding box coordinates of the current node define a range that is then divided in $$N_{bins}$$ bins of equal size.
Each bin holds a bounding box (initially empty) and a counter representing the number of primitives inside it (initially 0).
Once bins are initialized, the algorithm places each primitive in a bin based on the projection of its center on the axis of interest.
This means that the bin which contains the primitive center has its counter increased, and its bounding box is enlarged to contain the primitive.

Below is a representation of the process for a small set of primitives along one axis.
The number of primitives is written below each bin.

![Binning example](/assets/binning.svg)

In the example above, the bounding box of the first bin encloses the star, the bounding box of the second encloses the rectangle, and the bounding box of the third encloses both the triangle and the circle.
The bounding box of the last bin is empty since it contains no primitive.

Once primitives are placed in bins, the algorihm _sweeps_ the bins to find the best possible partition.
This means that, if we have $$N_{bins}$$ bins, we try all the partitions $$\{0, \dots, k\}, \{k + 1, \dots, N_{bins} - 1\}$$.
This is exactly like for the sorted primitives cases, except we now operate on the bins, instead of operating on the primitives directly.

There is a last detail that is worth mentioning at this point: In order to find the best possible partition, we have to compute the (simplified) SAH for every possible candidate.
Recall that the (simplified) SAH is:

$$C'_{SAH}(P) = C_T + \frac{SA(L)}{SA(P)}\,C_I\,N(L) + \frac{SA(R)}{SA(P)}\,C_I\,N(R)$$

Since we only care about minimization, we can drop $$C_T$$, and subsequently, the division by $$SA(P)$$ and the multiplication by $$C_I$$.
We thus select the partition that minimizes the expression:

$$C''_{SAH}(P) = SA(L)\,N(L) + SA(R)\,N(R)$$

To compute this expression, for each bin, we need to know the left term $$SA(L)\,N(L)$$ and the right term $$SA(R)\,N(R)$$.
The former is easy to obtain: We incrementally increase the left bounding box and accumulate the number of primitives found on each bin from the left to the right.
The later is a bit harder, since we would need, for each candidate, to iterate on the _right_ hand side of the array of bins to accumulate a bounding box and a number of primitives.
This would make the algorithm quadratic in the number of bins, which is not desirable.
Instead, we can do this in two passes:
One where we accumulate bounding boxes and primitive counts from the right to the left, in order to compute the right terms and store them in an array, and another where we do the same thing from the left to the right to compute the full cost, based on the right terms computed previously.
This makes the algorithm linear in the number of bins, at the cost of a bit of storage.

After this lengthy explanation, here is the code that performs binning:

```cpp
#include <array>

struct Bin {
    BBox bbox = BBox::empty();
    size_t prim_count = 0;

    Bin& extend(const Bin& other) {
        bbox.extend(other.bbox);
        prim_count += other.prim_count;
        return *this;
    }

    float cost() const { return bbox.half_area() * prim_count; }
};

static constexpr size_t bin_count = 16;

static size_t bin_index(int axis, const BBox& bbox, const Vec3& center) {
    int index = (center[axis] - bbox.min[axis]) * (bin_count / (bbox.max[axis] - bbox.min[axis]));
    return std::min(bin_count - 1, static_cast<size_t>(std::max(0, index)));
}

struct Split {
    int axis = 0;
    float cost = std::numeric_limits<float>::max();
    size_t right_bin = 0;

    operator bool () const { return right_bin != 0; }
    bool operator < (const Split& other) const {
        return *this && cost < other.cost;
    }
};

static Split find_best_split(
    int axis,
    const Bvh& bvh,
    const Node& node,
    const BBox* bboxes,
    const Vec3* centers)
{
    std::array<Bin, bin_count> bins;
    for (size_t i = 0; i < node.prim_count; ++i) {
        auto prim_index = bvh.prim_indices[i];
        auto& bin = bins[bin_index(axis, node.bbox, centers[prim_index])];
        bin.bbox.extend(bboxes[prim_index]);
        bin.prim_count++;
    }
    std::array<float, bin_count> right_cost;
    Bin left_accum, right_accum;
    for (size_t i = bin_count - 1; i > 0; --i) {
        right_accum.extend(bins[i]);
        right_cost[i] = right_accum.cost();
    }
    Split split { axis };
    for (size_t i = 0; i < bin_count - 1; ++i) {
        left_accum.extend(bins[i]);
        float cost = left_accum.cost() + right_cost[i + 1];
        if (cost < split.cost) {
            split.cost  = cost;
            split.right_bin = i + 1;
        }
    }
    return split;
}
```

Note that we use the half area, and not the area, since it saves a multiplication by 2.
Now, we can use this code to find a good split in the recursive construction procedure:

```cpp
static void build_recursive(/* ... */) {
    /* ... */
    Split min_split;
    for (int axis = 0; axis < 3; ++axis)
        min_split = std::min(min_split, find_best_split(axis, bvh, node, bboxes, centers));
    /* ... */
}
```

## Termination of the Algorithm

Once we have found the best split, we need to check, as mentioned previously, that this split is "good".
What this really means is checking that the splitting the node is better than not splitting it at all.
Importantly, the SAH gives us this information:
We only have to compare the cost of the split we just made with the cost of not splitting (that is, the cost of leaving the node as a leaf).
This means that we terminate if:

$$C_T + \frac{SA(L)}{SA(P)}\,C_I\,N(L) + \frac{SA(R)}{SA(P)}\,C_I\,N(R) > C_I N(P)$$

We can reorganize that a bit to simplify the computation, and obtain:

$$SA(L)\,N(L) + SA(R)\,N(R) > SA(P) \left(N(P) - \frac{C_T}{C_I}\right)$$

The left hand side of this inequation is directly computed by `find_best_split()`, and the right hand side can be computed using the surface area of the bounding box of the current leaf being split, along with the number of primitives contained in it.
In case the SAH tells us that the split is bad, but the leaf contains too many primitives, we fall back to a simple median split along the largest axis.
All of these things together lead to the following code:

```cpp
static void build_recursive(/* ... */) {
    /* ... */
    float leaf_cost = node.bbox.half_area() * (node.prim_count - build_config.traversal_cost);
    size_t first_right; // Index of the first primitive in the right child
    if (!min_split || min_split.cost > leaf_cost) {
        if (node.prim_count > build_config.max_prims) {
            // Fall back solution: The node has too many primitives, we use the median split
            int axis = node.bbox.largest_axis();
            std::sort(
                    bvh.prim_indices.begin() + node.first_index,
                    bvh.prim_indices.begin() + node.first_index + node.prim_count,
                    [&] (size_t i, size_t j) { return centers[i][axis] < centers[j][axis]; });
            first_right = node.first_index + node.prim_count / 2;
        } else
            // Terminate with a leaf
            return;
    } else {
        // The split was good, we need to partition the primitives
        first_right = std::partition(
                bvh.prim_indices.begin() + node.first_index,
                bvh.prim_indices.begin() + node.first_index + node.prim_count,
                [&] (size_t i) { return bin_index(min_split.axis, node.bbox, centers[i]) < min_split.right_bin; })
            - bvh.prim_indices.begin();
    }
    /* ... */
}
```

## Putting Things Together

Finally, we just create two leaves from the current node, each filled with its respective part of the current primitive range, and turn the current node into an internal one.
Then, we can call the construction procedure recursively, once for each child.

```cpp
static void build_recursive(/* ... */) {
    /* ... */
    auto first_child = node_count;
    auto& left  = bvh.nodes[first_child];
    auto& right = bvh.nodes[first_child + 1];
    node_count += 2;

    left .prim_count  = first_right - node.first_index;
    right.prim_count  = node.prim_count - left.prim_count;
    left .first_index = node.first_index;
    right.first_index = first_right;

    node.first_index = first_child;
    node.prim_count  = 0;

    build_recursive(bvh, first_child, node_count, bboxes, centers);
    build_recursive(bvh, first_child + 1, node_count, bboxes, centers);
}
```

Now that we have a proper BVH builder, let's look at BVH traversal.
This post _will not_ discuss traversal optimization.
For that, just refer to the [previous blog post on BVH traversal]({% link _posts/2021-01-06-bvhs-part-2.md %}).

# BVH Traversal

The typical use of a BVH is for ray-tracing, since it allows to greatly reduce the number of primitives that have to be intersected with a ray.
In order to use a BVH for this, an application typically builds a BVH ahead of time (during preprocessing) for some mesh of interest, and then uses it during rendering, to quickly determine the intersection between rays and that mesh.

Mathematically, we usually define a ray as a segment $$O + tD$$, where $$O$$ and $$D$$ are the ray's origin and direction, respectively, and where $$t$$ is a real number in the interval $$[t_{min}, t_{max}]$$.
BVH traversal works by intersecting the ray (defined in that way) with the bounding volumes (bounding boxes in our case) of the nodes of the hierarchy.
Nodes for which the ray do not intersect the bounding volume are discarded, and thus, entire subtrees are skipped during traversal.

The process is very similar when intersecting a BVH with a frustum, or when intersecting a BVH with another BVH.
For simplicity, we are only going to describe the BVH-ray intersection case in this post.

The algorithm starts by pushing the root node of the BVH on the stack, and then enters a loop, usually called the _traversal loop_.
In that loop, the algorithm performs the following steps:

1. Pop a node from the stack,
2. Intersect the bounding volume of that node with the ray,
3. If the ray misses, the bounding volume, go back to 1,
4. Otherwise, if the ray is a leaf, intersect the primitives in the leaf, and if the ray is an internal node, push its two children on the stack.

Of course, like mentioned previously, there are important optimizations that are omitted here, like the traversal order optimization, or octant-based intersection.
If you are serious about this, I highly recommend implementing at least the traversal order optimization (again, see previous posts for that).
For the moment, let us focus on this simple form of the algorithm, without any optimization.
Here is some code that implements this algorithm:

```cpp
#include <stack>
struct Ray {
    Vec3 org, dir;
    float tmin, tmax;
    /* ... */
};

struct Hit {
    uint32_t prim_index;
    operator bool () const { return prim_index != static_cast<uint32_t>(-1); }
    static Hit none() { return Hit { static_cast<uint32_t>(-1) }; }
};

struct Bvh {
    /* ... */

    template <typename Prim>
    Hit traverse(Ray& ray, const std::vector<Prim>& prims) const;
};

template <typename Prim>
Hit Bvh::traverse(Ray& ray, const std::vector<Prim>& prims) const {
    auto hit = Hit::none();
    std::stack<uint32_t> stack;
    stack.push(0);

    while (!stack.empty()) {
        auto& node = nodes[stack.top()];
        stack.pop();
        if (!node.intersect(ray))
            continue;

        if (node.is_leaf()) {
            for (size_t i = 0; i < node.prim_count; ++i) {
                auto prim_index = prim_indices[node.first_index + i];
                if (prims[prim_index].intersect(ray))
                    hit.prim_index = prim_index;
            }
        } else {
            stack.push(node.first_index);
            stack.push(node.first_index + 1);
        }
    }

    return hit;
}
```

This code is a direct translation of the 4 steps above, using `std::stack` for the stack, and encoding a `Hit` as an integer corresponding to a primitive index.
However, it uses two functions that we have not defined yet: `Prim::intersect()` and `Node::intersect()`.
We will first present the latter.

## Ray-Node Intersection

In order to intersect a ray with an axis-aligned box, we use the so-called _slabs test_.
This test is rather straightforward.
It computes the $$t$$ interval for which the ray intersects the box for all 3 axes, then computes the intersection of those intervals (as in, the set intersection).
For instance, if the intervals of $$t$$ on X, Y and Z are $$[-2, 5]$$, $$[1, 3]$$, $$[-5, 2]$$, then the interval for which the ray intersects the box is $$[1, 2]$$.

To compute these intervals, we only need to project the minimum and maximum bounding box coordinates on each axis, as shown in the figure below:

![Ray-box intersection](/assets/node_intersection.svg)

In that figure, we have the ray origin and direction $$O$$, $$D$$, the bounding box corners $$A$$, and $$B$$, and the axis $$x$$, and we want to compute the interval $$[t_1, t_2]$$.
In effect, we want to solve the following equations:

$$
\begin{array}{ll}
(O + t_1 D) \cdot x &= A \cdot x\\
(O + t_2 D) \cdot x &= B \cdot x
\end{array}
$$

These equations are pretty simple, and the solutions are:

$$
\begin{array}{ll}
t_1 &= \frac{(A - O) \cdot x}{D \cdot x} \\
t_2 &= \frac{(B - O) \cdot x}{D \cdot x}
\end{array}
$$

However, it could be that the ray goes in the other direction (from right to left, instead of from left to right like the figure shows).
In that case, $$t_1$$ and $$t_2$$ might not be in the right order.
The solution for this is just to use the interval $$[min(t_1, t_2), max(t_1, t_2)]$$ as the intersecting interval.
This will work regardless of the ray direction.
Also note that, since we use the canonical X, Y and Z axes, the dot product operation is not needed, since we can directly extract the respective components of the vectors.

Finally, to compute the intersection of each interval on every axis, we take the maximum of the lower bounds and the minimum of the upper ones.
Here is the corresponding code for that algorithm:

```cpp
#include <cmath>
#include <tuple>
#include <utility>

inline float robust_min(float a, float b) { return a < b ? a : b; }
inline float robust_max(float a, float b) { return a > b ? a : b; }
inline float safe_inverse(float x) {
    return std::fabs(x) <= std::numeric_limits<float>::epsilon()
        ? std::copysign(1.0f / std::numeric_limits<float>::epsilon(), x)
        : 1.0f / x;
}

struct Ray {
    /* ... */
    Vec3 inv_dir() const {
        return Vec3(safe_inverse(dir[0]), safe_inverse(dir[1]), safe_inverse(dir[2]));
    }
};

struct Node {
    /* ... */
    struct Intersection {
        float tmin;
        float tmax;
        operator bool () const { return tmin <= tmax; }
    };

    Intersection intersect(const Ray& ray) const {
        auto inv_dir = ray.inv_dir();
        auto tmin = (bbox.min - ray.org) * inv_dir;
        auto tmax = (bbox.max - ray.org) * inv_dir;
        std::tie(tmin, tmax) = std::make_pair(min(tmin, tmax), max(tmin, tmax));
        return Intersection {
            robust_max(tmin[0], robust_max(tmin[1], robust_max(tmin[2], ray.tmin))),
            robust_min(tmax[0], robust_min(tmax[1], robust_min(tmax[2], ray.tmax))) };
    }
};
```
    
For important numerical precision reasons that are out of the scope of this post, it is better to use the inverse ray direction, and to use the very same `robust_min()` and `robust_max()` functions instead of `std::min()` and `std::max()`.
For more information on this please refer to the paper _Robust BVH Ray Traversal_, by T. Ize.
Note that for performance, the inverse direction of the ray should be computed at the beginning of the traversal routine,
which we do not do here (compilers _might_ be able to move that out of the loop, and indeed, gcc does that, but that might not be the case for all).

## Ray-Primitive intersection

The last piece of the puzzle is the ray-primitive intersection.
Since the typical primitive is a triangle, I present the code to intersect a triangle here.
It is a typical Möller-Trumbore intersection test, which is nothing more than a method that solves the system of equations emerging from substituting the ray equation in the expression of a point on the triangle using barycentric coordinates.
The corresponding code is also reasonable and looks like this:

```cpp
struct Triangle {
    Vec3 p0, p1, p2;

    Triangle() = default;
    Triangle(const Vec3& p0, const Vec3& p1, const Vec3& p2)
        : p0(p0), p1(p1), p2(p2)
    {}

    bool intersect(const Ray& ray) const;
};

inline float dot(const Vec3& a, const Vec3& b) {
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}

inline Vec3 cross(const Vec3& a, const Vec3& b) {
    return Vec3(
        a[1] * b[2] - a[2] * b[1],
        a[2] * b[0] - a[0] * b[2],
        a[0] * b[1] - a[1] * b[0]);
}

bool Triangle::intersect(const Ray& ray) const {
    auto e1 = p0 - p1;
    auto e2 = p2 - p0;
    auto n = cross(e1, e2);

    auto c = p0 - ray.origin;
    auto r = cross(ray.direction, c);
    auto inv_det = 1.0f / dot(n, ray.direction);

    auto u = dot(r, e2) * inv_det;
    auto v = dot(r, e1) * inv_det;
    auto w = 1.0f - u - v;

    // These comparisons are designed to return false
    // when one of t, u, or v is a NaN
    if (u >= 0 && v >= 0 && w >= 0) {
        auto t = dot(n, c) * inv_det;
        if (t >= ray.tmin && t <= ray.tmax)
            return true;
    }

    return false;
}
```

The ray-triangle intersection above has the quality that it does not have so called "epsilon tests", that add some tolerance to each floating-point test.
Instead, it only relies on the IEEE-754 handling of NaNs.
This means that the code is faster, and much more numerically robust than versions which have epsilon tests, in particular the one which comes with the original publication _Fast, Minimum Storage Ray/Triangle Intersection_ by T. Möller and B. Trumbore.
With this, the BVH implementation is finally complete, both for the traversal and the construction.

# Running and Testing the Example Code

The example code given [here](/assets/bvh.cpp) can be compiled with the following command:

```sh
g++ bvh.cpp -O3 -march=native -std=c++17 -o bvh
```

To test it, you need an OBJ file like the [cornell box](/assets/cornell_box.obj) (other models might require to change the camera position manually in the code).
The command to run the program is then:

```sh
./bvh cornell_box.obj
```

You should get the following picture generated in the file "out.ppm":

![Output of the Previous Command](/assets/bvh_cornell_box_out.png)

# Conclusion

This concludes this very long post on an introduction to BVHs, and by now you have hopefully gained enough knowledge to be able to apply the more advanced optimization techniques described in the [previous posts]({% link _posts/2020-12-28-bvhs-part-1.md %}).
