---
layout: post
title: "Notes on Functional SSA IR Designs"
usemathjax: true
tags:
    - compiler design
    - intermediate representation
    - optimization
---

{% include mathjax.html %}

An IR, or Intermediate Representation, is the core of a compiler.
It is the internal language in which programs are represented, and thus needs to be at the same time simple, efficient, and expressive.
Of course, these goals are all somewhat contradictory.
For instance, a simple IR design might be to just use an AST, but that would be very inefficient, as transforming an AST is not obvious:
If you inline a function inside another using an AST representation, you'll have to deal with renaming variables, making sure that there is no conflict between the variables at the call site and the variables of the inlined function.
Performing analyses on an AST is also harder: Any analysis would have to carry around some sort of environment that maps variables to their content, depending on the current position in the AST.

# Static Single Assignment (SSA)

The standard solution to this is to use an SSA-form IR.
The idea behind those IRs is that every value is defined once, and never ever re-defined after that.
If you have a list of statements like:

    x = 1;
    y = x + 1;
    x = 2;
    y = x + 1;

Then these variables get renamed in such a way that they are defined only once:

    x1 = 1;
    y1 = x1 + 1;
    x2 = 2;
    y2 = x2 + 1;

This transformation simplifies the construction of analyses, since the knowledge of the value held by a variable is no longer context-sensitive:
In the original program, the expression `x + 1` has a different meaning, depending on whether it happens after the first or second assignment to `x`.
In the SSA-form program, this is no longer the case.
Each new version of `x` gets its own variable, usually denoted with an integer number indicating the "version" of the variable
(note that this is just a common notation, but any other naming scheme that distinguishes between different versions is fine).

For `if` statements, though, there is the problem that different versions of a variable might conflict after the branch.
Consider the following code:

    x = 0;
    if (cond)
        x = 1;
    else
        x = 2;
    ... x ...
    
When translating this in SSA, we end up having the issue that uses of `x` after the `if` could either be `x2` or `x3`:

    x1 = 0;
    if (cond)
        x2 = 1;
    else
        x3 = 2;
    ... ? ...
    
To solve this problem, a _phi-node_ is placed where uses of `x` appear after the `if`:

    x1 = 0;
    if (cond)
        x2 = 1;
    else
        x3 = 2;
    ... phi(x2, x3) ...

Intuitively, `phi(x2, x3)` means "the value of this expression is either `x2` or `x3`,
depending on the control-flow path leading to the current instruction".

In standard SSA-based IRs, functions are represented as a list of _basic-blocks_,
which are just groups of instructions that start at a label,
and end with a jump or branch to another basic-block or return from the function.

The `if` statement above would therefore be represented as:

        x1 = 0
        branch cond, if_true, if_false
    if_true:
        x2 = 1
        jump if_join
    if_false:
        x3 = 2
        jump if_join
    if_join:
        x4 = phi(x2, x3)
        ... x4 ...

Some SSA-based IRs keep a structured control-flow representation with nested `if`s and other looping constructs,
but that approach is actively harmful in general, as even if the source program does in fact contain no `goto` or branch instruction,
some optimizations may produce/require unstructured control-flow.

# Adding Functional Concepts to SSA

SSA-based IRs are good in the sense that they simplify compiler design for imperative programs.
However, traditional SSA-based IRs lack several concepts that come from functional languages:
There is no notion of functions as values (functions can only be used as the target of a call, or maybe by taking their address).
This, in turn, means that a functional program has to be _closure-converted_ before generating IR:
Functions that capture their environment must be turned into an object that carries a function pointer and the captured variables.

This is particularly bad because it means that functional optimizations are no longer available at the IR level.
Any optimization that deals with functions would have to either be happy just looking at function pointers, or somehow undo the closure-conversion internally.
Clearly, this calls for adding the concept of a function in the IR.

One option is to fix this by adding the concept of a function as a value in the IR, simply just allowing the use of functions outside of just call sites and address capture.
However, a famous paper by A. W. Appel, _SSA is Functional Programming_, shows that it is possible to represent _both_ basic-blocks _and_ functions as functions.

The idea is simple: Basic-blocks are essentially just like special functions that never return.
In the literature, those are called _continuations_.
A continuation can simply be encoded as a function with a special return type.
This special return type encodes in the type system that the function does not return,
and since there is no way to create a value of that type other than to call a function which does not return,
then we indirectly ensure that our basic-blocks/continuations call another basic-block as a last instruction.
This convention is called CPS for Continuation-Passing Style.

Therefore, the code seen above can be translated in a functional-SSA form like this:

    let entry = func () =>
        let if_join = func (x4) =>
            ...
        in
        let if_false = func () =>
            let x3 = 2 in
            if_join(x3)
        in
        let if_true = func () =>
            let x2 = 1 in
            if_join(x2)
        in
        let x1 = 0 in
        if cond then if_true() else if_false()

An important aspect of this example is that the order in which the basic-blocks appear is reversed:
`if_join` has to appear first since it is needed by other basic-blocks before it.
The order in which the names appear here outlines another important property of SSA-based IRs: Scoping.

Scoping, in traditional SSA-based IRs, is represented _implicitly_.
It is assumed that definitions _dominate_ all their uses, and it is an error if they do not.
Recall the definitions of the dominance relation for instructions and basic-blocks:

- An instruction `I` dominates another instruction `J` if and only if the basic-block `I` lives in dominates the basic-block where `J` lives,
  and if both `I` and `J` live in the same block, then `I` must appear before `J`.
- A basic-block `B` dominates another basic-block `C` if any path from the root of the control-flow graph to `C` has to go through `B`.

While these definitions may seem daunting at first, they just formalize the intuition that, in SSA, a use of an instruction (as an operand of another) must have a value for the instruction,
which requires that which ever control-flow path was taken, the instruction used as operand was executed before the use itself.
The following program, for instance, is ill-formed, because `x1` is used in `if_join` and there is a control-flow path from the entry block to `if_join` which does not initialize `x1`:

        branch cond, if_true, if_join
    if_true:
        x1 = 1
        jump if_join
    if_join:
        ... x1 ...

Unlike in the traditional SSA form, this notion of scoping in the original program is encoded _explicitly_ in the _functional_ SSA program, thus enforcing that variables are declared before being used.

Going back to the functional example, note how `x4` is transformed from a phi-node into a function parameter.
This shines light on the semantics of phi-nodes in the original program:
They simply act as parameters that are passed to each basic-block.

By treating phi-nodes as parameters, we can write optimizations that target function parameters and phi-nodes alike.
This means that in general, any transformation that can be applied on function parameters is also valid on phi-nodes and vice-versa.
For example, one optimization for phi-nodes is to remove those that are not used in any basic-block.
This optimization can also be applied to parameters, by determining which parameters are not used in the program, and removing them.

Similarly, a perhaps more complicated optimization is copy-propagation, that removes phi-nodes if all their operands are equal to the same value.
For instance, the phi-node `phi(x3, x3)` can be replaced by just `x3`.
This transformation can also be applied on parameters:
In this case, the optimization replaces a parameter if the function is called with always the same argument for that parameter.

Using a functional SSA formulation simplifies large parts of a compiler, as there is no separate notion of basic-block/function, which, in turn,
means that transformations like merging basic-blocks or removing phi-nodes become special cases of function inlining, or function specialization.

# CPS for Control-flow, Direct-style for Values

Using CPS allows to represent arbitrary control-flow in a function, but by using CPS we force a particular _ordering_.
This is desirable for control-flow constructs, but not really desirable for arithmetic operations, function calls (real function calls, not basic-blocks), and essentially any _value_.
For instance, if function calls are represented in CPS, then common sub-expressions cannot pick up that two calls should in fact be the same value,
even if the function is called with the same arguments (and is pure), because those two calls would take a different return continuation at each call site.
Take the following calls in a language like C:

    sqrtf(x);
    sqrtf(x);
    
Assuming that the compiler can prove that `sqrtf` is pure (returns the same value given the same arguments), it _could_ theoretically merge the two calls into one.
However, in CPS, those calls would look like:

    let f1 = func () => sqrtf(x, f2) in
    let f2 = func (y) => sqrtf(x, f3) ...

Thus, the two calls would not be the same and a custom optimization would have to be implemented in order to be able to see that two calls are identical, instead of just relying on common sub-expressions.
To remedy this problem, it is possible to have a mix of direct- and continuation-passing-style to represent programs ("direct-style" in this context refers to the traditional way of having functions return values, instead of passing their return value to another function as in CPS).
First of all, arithmetic instructions and primitive operations in general can be represented in direct-style easily.
For instance, just have the addition be an instruction that takes two operands and produces a result, as in pretty much all traditional IRs.

However, for function calls, this is a bit tricky, because if functions are represented in direct-style, then they produce a value.
Thus, their function body must have the type of the value that is produced (a function returning an integer should have an integer value as a body).
This prevents calling the first basic-block of the function, because calling it would result in a value of the special "no-return" type.
Still, within a function, we would like to have control-flow constructs, and thus we need a way to "initiate" the first CPS call.
The way I represent that is through a special `callc` instruction that produces a value, and transfers control to the called basic-block.
Essentially, `callc` takes 3 arguments: The basic-block to transfer control to, the actual argument to pass to the basic-block, and finally the return continuation of the function.

In that mixed CPS/direct-style model, the identity function would then be modelled like this:

    let rec id = func (x: T) -> T =>
        let bb = func () -> noret => call (ret id), x in
            callc bb, (), (ret id)

This syntax is basically similar to the one in the previous section, except for the explicit type annotations on functions and the introduction of `ret` and `callc`.
The primitive operation `ret` just produces a "return continuation" for a function, with the type `T -> noret` for a function which returns an object of type `T`.
Note that `id` is introduced with `let rec` instead of `let` as `id` appears on the right hand side of the `=` sign.
With this design, `callc bb, (), (ret id)` evaluates to a _value_: Its type is `T`, which is obtained by taking the domain type of the return continuation passed as third argument.
More specifically, its type is deduced as `T` because `ret id` has type `T -> noret`.
What is important here is that the type of the first two arguments does not impact the type of `callc`:
The type of a function body is only dependent on what it returns, not on the type of the first basic-block.

When inlining `id`, we first create a "return continuation" for the call site.
This continuation has the same type as `ret id`, that is, it is a continuation of the type `T -> noret`.
Then, we perform a substitution on the scope of the function with the parameter being replaced by the argument, and `ret id` of the function being replaced by the newly created return continuation.
Finally, we place the substituted scope at the call site by replacing the call to the function by the parameter of the new return continuation.
For instance, imagine the following program:

    let next_block = func () => ...

    let first_block = func () =>
        ... call id, y ...
        call next_block, ()
    
Assuming `y` has type `T`, we first create a block called `ret_id` with type `T -> noret`, then create a copy of the body of `id` where we substitute `x` for `y` and `ret id` for `ret_id`:

    let ret_id = func (ret_val: T) => <not-yet-specified>

    let id_body : T =
        let bb_copy = func () -> noret => call ret_id, y in
            callc bb_copy, (), ret_id

Then, we substitute `call id, y` for `ret_val` at the call site to obtain the following code:

    let next_block = func () => ...

    let first_block = func () =>
        ... ret_val ...
        call next_block, ()
    
At this point the IR is an incorrect state, as we are using a variable named `ret_val` in `first_block`, but that variable has never been initialized, nor seen before.
Thus, we move `first_block` after `ret_id`:

    let ret_id = func (ret_val: T) => <not-yet-specified>

    let bb_copy = func () -> noret => call ret_id, y

    let first_block = func () =>
        ... ret_val ...
        call next_block, ()

Still, this is not sufficient as the control-flow of the inlined function has not been merged with the control-flow of the containing function.
To do that, we need to do the following:

1. Move the code of `first_block` that depends on `ret_val` inside `ret_id` (since `ret_val` is the parameter of `ret_id` and thus is only available _after_ a call to `ret_id`),
2. Make `first_block` call `bb_copy`, in order to enter the body of the inlined function
  (for that, we simply re-use the `callc` instruction present in the inlined body, and turn it into a `call` by dropping the third argument),
3. Make `ret_id` call `next_block` in order to come back to the flow of the containing function.
 
This results in:

    let next_block = func () => ...
    
    let ret_id = func (ret_val: T) =>
        ... ret_val ...
        call next_block, ()

    let bb_copy = func () -> noret => call ret_id, y
    
    let first_block = func() =>
        ...
        call bb_copy, ()

At this point, everything is correctly wired.
Note that item 2. is perhaps the most difficult to take care of in a setting where scoping is explicit, as it requires to determine the dependencies of instructions on the parameter of `ret_id`.
When using a graph-based IR, this problem does not manifest itself at this point, since there is no need to take care of instruction ordering.
The only downside of using a graph-based IR would be that perhaps the scheduling of instructions has to happen at another point, but there are algorithms for this (see e.g. C. Click's _Global Value Numbering -- Global Code Motion_ article).

The `callc` instruction and mechanism is essentially allowing the use of a direct-style functions in an otherwise CPS IR.
In a way, some might argue that this is the same as having two different representations for basic-blocks and functions (since they take this extra `callc` instruction out of nowhere),
but that would be an inaccurate statement, as parameters and other concepts are still shared between the two,
and thus most optimizations do not have to care about the differences, except perhaps inlining, as described in the process above.

# Conclusion

This post hopefully explained the basic idea of functional SSA IRs, and how they can be designed in such a way as to support both CPS and direct-style control-flow.
CPS is a very attractive calling convention for control-flow structures, including exceptions, but as pointed out in the previous sections, it also comes with some drawbacks that direct-style does not have.
I believe that the approach presented here is a good compromise between the two, and allows to use CPS when it makes sense, and otherwise switch to direct-style, without an extremely high cost in terms of IR complexity.

# References

This article is heavily inspired from other, previous work on ANF (Administrative Normal Form IRs) and CPS.
Here is a non-exhaustive list of works that I found relevant to this topic:

- _SSA is Functional Programming_, by A. W. Appel,
- _Compiling With Continuations_ by A. W. Appel,
- _The Essence of Compiling with Continuations_, by C. Flanagan, A. Sabry, B. F. Duba, M. Felleisen,
- _Compiling With Continuations, Continued_, by A. Kennedy.
- _Compiling Without Continuations_, by L. Maurer, P. Downen, Z. M. Ariola, and S. P. Jones
