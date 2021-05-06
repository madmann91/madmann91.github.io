---
layout: post
title: "PLOC, Revisited"
usemathjax: true
tags:
    - bvh
    - ray tracing
    - computer graphics
    - optimization
---

{% include mathjax.html %}

# Introduction

In an old post, I explained very low-level details of the implementation of a BVH construction method called PLOC (Parallel, Locally Ordered Clustering).
Upon the request from the Graphics Programming Discord, I am going to give a very simple addition to my recent [introduction to BVHs]({% link _posts/2021-04-29-an-introduction-to-bvhs.md %}) (which you should probably read if you are not familiar with BVHs), showing how to use PLOC instead of binning to build BVHs.
The article upon which this is (loosely) based is _Parallel Locally-Ordered Clustering for Bounding Volume Hierarchy Construction_, by D. Meister and J. Bittner.
I recommend reading that paper once you have a basic understanding of the method as I describe it here, since I am not going to cover the parallelization aspects.
The source code of this article is available [here](/assets/bvh_ploc.cpp).
To compile and run it, please see the instructions given in my [last post]({% link _posts/2021-04-29-an-introduction-to-bvhs.md %}#running-and-testing-the-example-code).

# The Essence of PLOC

In essence, PLOC is a simple algorithm, surprisingly easy to implement if you do not care about parallelization nor performance.
It works from the bottom up, first by creating one leaf per primitive, and then merging nodes on its way to the top.
In order to select the nodes to merge together it performs a local search around the current node to find a good candidate.
Thus, for the algorithm to work well, it is crucial to lay down primitives in an order that places neighboring primitives next to each other.
For that, a space-filling curve is often used, like Morton encoding.

To sum things up, here are the steps of the algorithm:

1. Sort primitives by morton code (or another space-filling curve),
2. Create one leaf per primitive,
3. Merge leaves until there is only one node, the root of the hierarchy.

# Sorting Primitives

Here, we are going to use Morton codes to sort primitives.
I have already discussed the method I use in a [previous post]({% link _posts/2020-12-28-bvhs-part-1.md %}#computing-morton-codes), so I am only going to give the code here:

```cpp
struct Morton {
    using Value = uint32_t;
    static constexpr int log_bits = 5;
    static constexpr size_t grid_dim = 1024;

    static Value split(Value x) {
        uint64_t mask = (UINT64_C(1) << (1 << log_bits)) - 1;
        for (int i = log_bits, n = 1 << log_bits; i > 0; --i, n >>= 1) {
            mask = (mask | (mask << n)) & ~(mask << (n / 2));
            x = (x | (x << n)) & mask;
        }
        return x;
    }

    static Value encode(Value x, Value y, Value z) {
        return split(x) | (split(y) << 1) | (split(z) << 2);
    }
};
```

With this simple piece of code, a call to `Morton::encode(x, y, z)` encodes three integers as on a 1024x1024x1024 grid into a single integer.
The order defined by Morton codes is such that cells that are next to each other on the grid produce (reasonably) close morton codes.
Thus, if we sort primitives by morton codes, a local search should be enough to determine good merging candidates.

Sorting primitives by morton codes is pretty easy, using `std::sort` from the standard library.
The only real question is how to place primitives on a grid.
The solution to that is also not really complicated: The only thing that is required is to compute the bounding box of all the centers (the bounding box of the primitives could also do, but it would not be as tight), and then divide that into a 1024x1024x1024 cells to form a grid.
The code that does all that, including sorting, is listed below:

```cpp
Bvh Bvh::build(const BBox* bboxes, const Vec3* centers, size_t prim_count) {
    Bvh bvh;

    // Compute the bounding box of all the centers
    auto center_bbox = std::transform_reduce(
        centers, centers + prim_count, BBox::empty(),
        [] (const BBox& left, const BBox& right) { return BBox(left).extend(right); }, 
        [] (const Vec3& point) { return BBox(point); });

    // Compute morton codes for each primitive
    std::vector<Morton::Value> mortons(prim_count);
    for (size_t i = 0; i < prim_count; ++i) {
        auto grid_pos =
            min(Vec3(Morton::grid_dim - 1),
            max(Vec3(0), (centers[i] - center_bbox.min) * (Vec3(Morton::grid_dim) / center_bbox.diagonal())));
        mortons[i] = Morton::encode(grid_pos[0], grid_pos[1], grid_pos[2]);
    }

    // Sort primitives according to their morton code
    bvh.prim_indices.resize(prim_count);
    std::iota(bvh.prim_indices.begin(), bvh.prim_indices.end(), 0);
    std::sort(bvh.prim_indices.begin(), bvh.prim_indices.end(), [&] (size_t i, size_t j) {
        return mortons[i] < mortons[j];
    });

    /* ... */
}
```

In practice, a real implementation would parallelize each step, and to that end would probably use a custom radix sort implementation instead of `std::sort`.

# Creating the BVH Leaves

Once primitives are sorted in the array, it is possible to create the BVH leaves.
That process is rather easy, since we only have to use the bounding box of every primitive, and the array of indices computed earlier, during sorting:

```cpp
Bvh Bvh::build(const BBox* bboxes, const Vec3* centers, size_t prim_count) {
    /* ... */
    std::vector<Node> current_nodes(prim_count), next_nodes;
    std::vector<size_t> merge_index(prim_count);
    for (size_t i = 0; i < prim_count; ++i) {
        current_nodes[i].prim_count = 1;
        current_nodes[i].first_index = i;
        current_nodes[i].bbox = bboxes[bvh.prim_indices[i]];
    } 
    /* ... */
}
```

It is important to use `bboxes[bvh.prim_indices[i]]`, as it ensures that the leaves are placed in the same order as the _sorted primitives_.
We also allocate another array of nodes called `next_nodes`, whose purpose will become obvious in the next section.

# Merging from the Bottom Up

Merging is an iterative process, starting with an array of nodes, and resulting in two new arrays: the nodes that have been merged (as part of the final BVH), and the nodes that will be considered in the next iteration (which are the parents of the merged nodes, and the nodes that have not been merged).
Evidently, this means that we need some sort of double-buffering scheme where we swap the input and output arrays in order to re-use allocations.
This is why the previous piece of code has two arrays: `current_nodes`, for the current list of nodes being merged, and `next_nodes`, for the result to be considered in the next iteration.

Inside one merging iteration, the process is as follows:

1. For each node (at index `i`), we find the index of a node to merge with using a local search.
   We call that node to merge with a _merge candidate_, and we store its index in `merge_index[i]`.
2. For all nodes (at index `i`) that have a merge candidate at index `j == merge_index[i]` for which the merge candidate of `j` is `i`,
   we create a parent for the two nodes and place it in the array of nodes of the next iteration (`next_nodes`).
   The two nodes at indices `i` and `j` are then placed in the final set of nodes, and are never considered again for merging.
   This is intuitively justified by the fact that we merge nodes that "agree" on their merge candidates.

## Local Search

For step 1., as mentioned earlier, we perform a local search.
This means that we iterate through the nodes that are close to the current one (in a given _search range_) in the current array of nodes (`current_nodes`), and select the one that is the closest according to a distance metric.
The distance metric that is used in the original publication is the surface area of the union of the bounding box of the two nodes.
We will also use this here.
In terms of code, the local search can therefore be implemented as follows:

```cpp
size_t find_closest_node(const std::vector<Node>& nodes, size_t index) {
    static size_t search_radius = 14;
    size_t begin = index > search_radius ? index - search_radius : 0;
    size_t end   = index + search_radius + 1 < nodes.size() ? index + search_radius + 1 : nodes.size();
    auto& first_node = nodes[index];
    size_t best_index = 0;
    float best_distance = std::numeric_limits<float>::max();
    for (size_t i = begin; i < end; ++i) {
        if (i == index)
            continue;
        auto& second_node = nodes[i];
        auto distance = BBox(first_node.bbox).extend(second_node.bbox).half_area();
        if (distance < best_distance) {
            best_distance = distance;
            best_index = i;
        }
    }
    return best_index;
}
```

The code above performs a local search for a node located at index `index` in an array of nodes.
It uses a search radius of 14, which means that every node in the range `[index - 14, index + 14]` is tested (except the node at index `index` itself).
Again, note the use of the half area (instead of the area, which saves a multiplication), since we only care about minimization, and not the actual value of the distance.

## Merging

Now that we have a way to find a merge candidate for each node, the complete merge loop can be implemented:

```cpp
Bvh Bvh::build(const BBox* bboxes, const Vec3* centers, size_t prim_count) {
    /* ... */

    // Merge nodes until there is only one left
    bvh.nodes.resize(2 * prim_count - 1);
    size_t insertion_index = bvh.nodes.size();

    while (current_nodes.size() > 1) {
        for (size_t i = 0; i < current_nodes.size(); ++i)
            merge_index[i] = find_closest_node(current_nodes, i);
        next_nodes.clear();
        for (size_t i = 0; i < current_nodes.size(); ++i) {
            auto j = merge_index[i];
            // The two nodes should be merged if they agree on their respective merge indices.
            if (i == merge_index[j]) {
                // Since we only need to merge once, we only merge if the first index is less than the second.
                if (i > j)
                    continue;

                // Reserve space in the target array for the two children
                assert(insertion_index >= 2);
                insertion_index -= 2;
                bvh.nodes[insertion_index + 0] = current_nodes[i];
                bvh.nodes[insertion_index + 1] = current_nodes[j];

                // Create the parent node and place it in the array for the next iteration
                Node parent;
                parent.bbox = BBox(current_nodes[i].bbox).extend(current_nodes[j].bbox);
                parent.first_index = insertion_index;
                parent.prim_count = 0;
                next_nodes.push_back(parent);
            } else {
                // The current node should be kept for the next iteration
                next_nodes.push_back(current_nodes[i]);
            }
        }
        std::swap(next_nodes, current_nodes);
    }
    assert(insertion_index == 1);
    /* ... */
}
```

This code simply computes the merge candidates of all the nodes in the current array of nodes, and then merge nodes for which the criterion in step 2. is met.
Note that we only want to merge a given pair `(i, j)` once, so we have an additional check that `i < j` when that criterion is met, in order to avoid merging the same pair twice.

At the end of the loop, we swap the input array and the next array, and repeat the process until there is only one node in the array of nodes to merge.
At that point, we can stop and copy that last node directly into the destination array:

```cpp
Bvh Bvh::build(const BBox* bboxes, const Vec3* centers, size_t prim_count) {
    /* ... */
    // Copy root node into the destination array
    bvh.nodes[0] = current_nodes[0];
    return bvh;
}
```

That is all. After that, the BVH is fully built and can be used for ray-tracing or any other purpose.
However, in practice, it makes sense to perform other optimizations at that point, like collapsing leaves.
I have already written something about that [earlier]({% link _posts/2020-12-28-bvhs-part-1.md %}#collapsing-leaves), so I will not expand on this too much here.

# Conclusion

PLOC, just like binning, is a simple BVH construction method that is easy to implement in a sequential manner.
As noted in this post, additional improvements are required if the best performance is desired, such as using radix sort and parallelizing each step of the algorithm for construction speed, or running an additional leaf collapsing pass for traversal speed.
In any case, this should serve as a decent introduction to PLOC and the code given here can be used as a reference implementation when adding the optimizations described in my [previous blog posts]({% link _posts/2020-12-28-bvhs-part-1.md %}).
