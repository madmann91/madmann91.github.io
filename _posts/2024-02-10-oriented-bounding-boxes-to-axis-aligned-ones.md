---
layout: post
title: "Oriented Bounding Boxes to Axis-Aligned ones"
usemathjax: true
tags:
    - computer graphics
    - geometry
    - optimization
---

{% include mathjax.html %}

# Bounding Boxes

In computer graphics, bounding boxes are typically used to enclose objects, in order to estimate
their volume, to provide collision proxies, to speed-up ray-tracing queries, to build acceleration
data structures, or in any application that needs to work with a simplified model for the enclosed
object.

In that context, bounding boxes are either *axis-aligned* or *oriented*. Axis-aligned bounding boxes
(AABBs), as their name suggest, are tied to the axes in which the world is defined. Oriented
bounding boxes (OBBs), on the contrary, contain their own transformation, relative to the world in
which they are defined. This allows OBBs to bound their contents more tightly than AABBs.

![AABB vs. OBB](/assets/aabb_vs_obb.svg)

In mathematical terms, a 3D AABB with minimum coordinate vector $$min$$ and maximum coordinate
vector $$max$$ is the set defined by:

$$AABB(min, max) = \left\{ v \in \mathcal{R}^3 \,\mid\,
    v_x \in [min_x, max_x],
    v_y \in [min_y, max_y],
    v_z \in [min_z, max_z]
\right\}$$

A 3D OBB with center point $$c$$ and with axes represented by a matrix $$M$$, is defined by:

$$OBB(M, c) = \left\{ M\,v + c \,\mid\,
    v_x \in [-1, 1],
    v_y \in [-1, 1],
    v_z \in [-1, 1]
\right\}$$

In C, those would be represented with types such as:

```c
struct AABB {
    float min[3];
    float max[3];
};

struct OBB {
    float matrix[9];
    float center[3];
};
```

# Getting an AABB from an OBB

Very often, it is necessary to obtain an axis-aligned bounding-box from an oriented one. This is a
lossy process, as an AABB will generally not match the tight bounds of an OBB, as shown in the
picture below:

![Computing an AABB from an OBB](/assets/aabb_from_obb.svg)

What is worse, the obtained AABB may be looser than the AABB obtained from the enclosed object
directly: Here, the computed AABB is larger than the previous AABB (the green dashed line).

Nevertheless, it is sometimes necessary to compute an AABB that encloses an OBB. One way to do that
is to transform the 8 points of an OBB, and compute their bounding box by recording the maximum and
minimum of each corner along every coordinate. We are going to present a different, more efficient
way of doing this based on the following analysis (this idea is not mine, I have encountered it
before in various places, including the _Bullet_ physics engine code base):

- Every point $$p$$ inside the OBB has the form $$p = c + M\,v$$, where $$v$$ is a vector with
  coordinates that are in $$[-1, 1]$$, $$c$$ is the center of the OBB, and $$M$$ is the OBB's matrix.
- In 3D, for each axis $$a$$, $$p_a = c_a + M_{a,1}\,v_1 + M_{a,2}\,v_2 + M_{a,3}\,v_3$$, with
  $$v_{1,2,3} \in [-1, 1]$$.
- We are interested on the minimum and maximum value that $$p_a$$ takes, as those will be the
  minimum and maximum coordinates of the AABB on axis $$a$$.
- Since for minimization or maximization we can choose each component of $$v_n$$ independently, the
  maximum value is attained when each product $$M_{a,n}\,v_n$$ is maximal. If $$M_{a,n}$$ is
  positive, the maximum value for that product is obtained when $$v_n = +1$$, otherwise, when
  $$M_{a,n}$$ is negative or zero, the maximum is realized when $$v_n = -1$$.
- This is effectively the same as taking the absolute value of $$M_{a,n}$$ and choosing $$v_n = +1$$.
- Thus, the maximum is $$max(p_a) = c_a + \lvert M_{a,1} \rvert + \lvert M_{a,2}\rvert + \lvert M_{a,3}\rvert$$
- This can be written in vector form as $$max(p) = c + \lvert M\rvert\,(1, 1, 1)^\intercal$$,
  assuming the absolute value of a matrix is taken component-wise.
- A similar analysis can be made for the minimum coordinate of the AABB, resulting in
  $$min(p) = c - \lvert M \rvert \,(1, 1, 1)^\intercal$$

This means that we can get the AABB from an OBB in three steps:

1. Compute $$\lvert M \rvert$$ by taking the absolute value of every element in the matrix.
2. Compute $$\lvert M \rvert \, (1, 1, 1)^\intercal$$, which is a simple matrix-vector
   multiplication.
3. Compute the maximum (resp. minimum) coordinate by adding (resp. subtracting) the value obtained
   from step 2. to the position of the center of the OBB.

In C, this can be implemented as:

```c
struct AABB aabb_from_obb(const struct OBB* obb) {
    // This assumes the matrix is row-major.
    float ex = fabsf(obb->matrix[0]) + fabsf(obb->matrix[1]) + fabsf(obb->matrix[2]);
    float ey = fabsf(obb->matrix[3]) + fabsf(obb->matrix[4]) + fabsf(obb->matrix[5]);
    float ez = fabsf(obb->matrix[6]) + fabsf(obb->matrix[7]) + fabsf(obb->matrix[8]);
    return (struct AABB) {
        .min = { obb->center[0] - ex, obb->center[1] - ey, obb->center[2] - ez },
        .max = { obb->center[0] + ex, obb->center[1] + ey, obb->center[2] + ez },
    };
}
```

If you want to play around with this method, here is a simple python script to display an OBB and
the corresponding AABB obtained with this method:

```python
import numpy as np
import matplotlib.pyplot as plt
import math

plt.figure()
plt.xlim(-4, 4)
plt.ylim(-4, 4)

theta = 1   # Angle (in radians) of the OBB
a, b = 2, 1 # Dimension of the OBB on each axis

# Matrix and center of the OBB
M = np.array([[a * math.cos(theta), -b * math.sin(theta)], [a * math.sin(theta), b * math.cos(theta)]])
C = np.array([1, 1])

# Points on the OBB
P0 = C + np.matmul(M, np.array([+1, +1]))
P1 = C + np.matmul(M, np.array([-1, +1]))
P2 = C + np.matmul(M, np.array([-1, -1]))
P3 = C + np.matmul(M, np.array([+1, -1]))

xs, ys = zip(*[P0, P1, P2, P3, P0])
plt.plot(xs, ys)

# Points on the enclosing AABB
E = np.matmul(np.absolute(M), np.array([1, 1]))
Q0 = C + np.array([-E[0], -E[1]])
Q1 = C + np.array([-E[0], +E[1]])
Q2 = C + np.array([+E[0], +E[1]])
Q3 = C + np.array([+E[0], -E[1]])

xs, ys = zip(*[Q0, Q1, Q2, Q3, Q0])
plt.plot(xs, ys)

plt.show()
```

# Conclusion

This trivial operation should be faster when implemented in this way, because it only requires 9
absolute values and 12 additions. In contrast, the method of computing the bounding box of all the
corners of the OBB requires 48 additions, 56 multiplications, 21 minimum operations, and 21 maximum
operations: All 8 points need 6 additions and 9 multiplications to be transformed, and for each but
the first one we need to take the minimum and maximum along every coordinate. This may be simplified
a little if the scale is incorporated in the matrix, as this only requires to perform a matrix
multiplication with vectors of the form $$(\pm 1, \pm 1, \pm 1)$$ (in which case scalar multiplications are no longer necessary),
but generally will not beat the method described here.
