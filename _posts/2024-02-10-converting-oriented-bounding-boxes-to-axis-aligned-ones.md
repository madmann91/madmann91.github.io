---
layout: post
title: "Converting Oriented Bounding Boxes to Axis-Aligned ones"
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

In mathematical terms, a 3D AABB with minimum coordinate vector $$p_{min}$$ and maximum coordinate
vector $$p_{max}$$ is the set defined by:

$$AABB(p_{min}, p_{max}) = \left\{ p \in \mathcal{R}^3 \,\mid\,
    p_1 \in [p_{min1}, p_{max1}],
    p_2 \in [p_{min1}, p_{max2}],
    p_3 \in [p_{min1}, p_{max3}]
\right\}$$

Where $$p_1$$ denotes the first component of vector $$p$$, $$p_2$$ denotes the second one, and so
on. A 3D OBB with center point $$c$$ and with axes represented by a matrix $$M$$, is defined by:

$$OBB(M, c) = \left\{ M\,v + c \,\mid\, v \in [-1, 1]^3 \right\}$$

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
minimum of each corner along every coordinate. This is for instance
[the method](https://pbr-book.org/4ed/Geometry_and_Transformations/Applying_Transformations#BoundingBoxes)
used in PBRT, the excellent reference book for offline rendering written by M. Pharr, W. Jakob, and
G. Humphreys. There is however a more efficient way of doing this based that I have encountered in
various places[^1][^2], including the _Bullet_ physics engine code base.

# The Method

Let us try to find the bounds of the oriented bounding box defined by $$M$$, its matrix, and $$c$$,
its center. First, note that every point $$p$$ inside the OBB has the form:

$$p = c + M\,v$$

where $$v$$ is a vector with coordinates that are in $$[-1, 1]$$. In 3D, we can project that
expression for each axis $$a \in \{ 1, 2, 3 \}$$, and we get:

$$p_a = c_a + M_{a,1}\,v_1 + M_{a,2}\,v_2 + M_{a,3}\,v_3$$

The minimum and maximum value of that expression represent the minimum and maximum coordinates of
the enclosing AABB on axis $$a$$. For every coordinate $$n \in \{1, 2, 3\}$$, we can pick any value
for $$v_n$$, as long as it is in the range $$[-1, 1]$$, since we want to minimize or maximize that
expression for points inside the OBB. The values of $$M_{a,n}$$ or $$c_a$$ are properties of the OBB
and are not part of the minimization or maximization.

In other words, we want to compute:

$$min \left\{ p_a \mid v_1, v_2, v_3 \in [-1, 1] \right\}$$

and

$$max \left\{ p_a \mid v_1, v_2, v_3 \in [-1, 1] \right\}$$

This means that the maximum (resp. minimum) value is reached when the individual products
$$M_{a,n}\,v_n$$ are maximal (resp. minimal). From these observations, we can then deduce that if
$$M_{a,n}$$ is positive, the maximum value for the product $$M_{a,n}\,v_n$$ is obtained when $$v_n =
+1$$, otherwise, when $$M_{a,n}$$ is negative or zero, the maximum is realized when $$v_n = -1$$.
This is effectively the same as taking the absolute value of $$M_{a,n}$$ and choosing $$v_n = +1$$.

Thus, the maximum on axis $$a$$ is:

$$max \left\{ p_a \mid v_1, v_2, v_n \in [-1, 1] \right\} =
    c_a + \lvert M_{a,1} \rvert + \lvert M_{a,2}\rvert + \lvert M_{a,3}\rvert$$

Similarly,

$$min \left\{ p_a \mid v_1, v_2, v_n \in [-1, 1] \right\} =
    c_a - \lvert M_{a,1} \rvert - \lvert M_{a,2}\rvert - \lvert M_{a,3}\rvert$$

Thus, the expressions for $$p_{min}$$ and $$p_{max}$$, the parameters of the enclosing axis-aligned
bounding-box, are:

$$
p_{max} =
\begin{pmatrix}
c_1 + \lvert M_{1,1}\rvert + \lvert M_{1,2}\rvert + \lvert M_{1,3}\rvert \\
c_2 + \lvert M_{2,1}\rvert + \lvert M_{2,2}\rvert + \lvert M_{2,3}\rvert \\
c_3 + \lvert M_{3,1}\rvert + \lvert M_{3,2}\rvert + \lvert M_{3,3}\rvert \\
\end{pmatrix}
$$

$$
p_{min} =
\begin{pmatrix}
c_1 - \lvert M_{1,1}\rvert - \lvert M_{1,2}\rvert - \lvert M_{1,3}\rvert \\
c_2 - \lvert M_{2,1}\rvert - \lvert M_{2,2}\rvert - \lvert M_{2,3}\rvert \\
c_3 - \lvert M_{3,1}\rvert - \lvert M_{3,2}\rvert - \lvert M_{3,3}\rvert \\
\end{pmatrix}
$$

This can be written in vector form as:

$$
p_{min} = c - \lvert M\rvert\,(1, 1, 1)^\intercal\\
p_{max} = c + \lvert M\rvert\,(1, 1, 1)^\intercal
$$

Where $$\lvert M\rvert$$ is the matrix where every component is the absolute value of the
corresponding component of $$M$$.

# Implementation

With all this, we can get the AABB from an OBB in three steps:

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
absolute values, 9 additions, and 3 subtractions. In contrast, the method of computing the bounding
box of all the corners of the OBB requires 48 additions, 56 multiplications, 21 minimum operations,
and 21 maximum operations: All 8 points need 6 additions and 9 multiplications to be transformed,
and for each but the first one we need to take the minimum and maximum along every coordinate. This
may be simplified a little if the scale is incorporated in the matrix, as this only requires to
perform a matrix multiplication with vectors of the form $$(\pm 1, \pm 1, \pm 1)$$ (in which case
scalar multiplications are no longer necessary), but generally will not beat the method described
here.

[^1]: J. Arvo presents a similar method in _Graphics Gems_ (1990) that uses min/max operations and contains branches and loops, but is otherwise pretty similar in spirit.

[^2]: [Here](https://zeux.io/2010/10/17/aabb-from-obb-with-component-wise-abs/) is another source which implements this exact method, in the context of transforming an AABB into another AABB directly.
