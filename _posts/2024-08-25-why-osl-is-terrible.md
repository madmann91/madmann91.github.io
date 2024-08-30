---
layout: post
title: "Why OSL is terrible"
usemathjax: true
tags:
    - computer graphics
    - compiler design
---

# Introduction

Lately, I have been working on [NOSL](https://github.com/madmann91/nosl/tree/master), an alternative
[OSL](https://github.com/AcademySoftwareFoundation/OpenShadingLanguage/tree/main) compiler,
in its early stages at the time of writing this post.

At work, and now while developing this new compiler, I have encountered many issues with OSL as a
language, with its specification, and with its implementation. So, as a result of my growing
frustration with it, I have started writing this article. I may add new elements to this document as
time passes, as I know I am bound to find new issues as I progress with my own re-implementation.

# The language is not well-designed

To begin with, the language itself has many flaws, of which I have collected a few below:

- No boolean type exists in OSL, and integers are used instead. This harms readability of the shader
  source, as well as the performance of the resulting code (because 32-bit integers are used where
  1-bit, or perhaps 1-byte ones would have been enough). To add insult to injury, the language
  specification is difficult to follow and to understand because special cases have to be added to
  catch the notion that some types can be considered "true" or "false". Consider this part of the
  specification:

  > The condition may be any valid expression, including:
  > - The result of any comparison operator (such as <, ==, etc.).
  > - Any numeric expression (int, color, point, vector, normal, matrix), which is con-
  >   sidered “true” if nonzero and “false” if zero.
  > - Any string expression, which is considered “true” if it is a nonempty string, “false” if it
  >   is the empty string ("").
  > - A closure, which is considered “true” if it’s empty (not assigned, or initialized with =0),
  >   and “false” if anything else has been assigned to it.
  > - A logical combination of expressions using the operators ! (not), && (logical “and”), or
  >   || (logical “or”). Note that && and || short circuit as in C, i.e. A && B will only evaluate
  >   B if A is true, and A || B will only evaluate B if A is false.

  These cases could have been taken care of by allowing coercions from these types to a boolean type.

- Vector, points, normals, colors are "different" types, but can be assigned to each other without
  any issue. This makes one wonder why one needs those types to begin with.

- There are no enumeration types in OSL. Instead, shaders typically use strings to represent enumeration
  values. In OSL, strings are uniquely hashed, which allows representing them with an integer. This
  makes using strings as enumeration values more efficient, but also means that strings must be hashed
  beforehand, and that a global lock must be acquired before constructing a new string. This is very
  inefficient on highly parallel machines, including GPUs. Finally, the shader code itself becomes
  more complicated, full of string comparisons, and that makes it harder to maintain if more
  enumerations values are added later on: For instance, comments describing possible string values for
  an enumeration parameter have to be placed before exposed function parameters, and must be kept up
  to date.

- OSL has no matrix addition/subtraction, for no particular reason. Matrix addition or subtraction is
  not a complicated operation, nor an uncommon one. As a result, users will end up re-implementing matrix
  addition, which is a shame because that really should have been part of a standard type dedicated
  to matrices.

- Some questionable conversions to integers are allowed. Those also only work in the context of
  logical expressions, if statements, and loop conditions:

  > Multi-component types (such as color) are considered true any
  > component is nonzero, false all components are zero. Strings are considered true if they
  > are nonempty, false if they are the empty string ("").

  This makes code like this perfectly legal:

  ```cpp
  int j = 1 && "abcd"; // Legal!
  if ("abcd") { // Legal!
  }
  ```

  While also making code like this illegal:

  ```cpp
  int j = (int)"abcd";
  ```

  While the latter is quite rightfully rejected, there is not much of a different between that
  expression and the former `1 && "abcd"`.

- OSL has dedicated a `normal` type for normals, but the built-in function `calculatenormal`
  returns a `vector` instead. I do not think I need to add anything here.

- OSL has a very weak type system, in which it is impossible to encode constraints such as "the
  inputs to this function can be anything as long as it is an array". This leads to very convoluted
  descriptions for builtin functions such as `gettextureinfo` (for which the output parameter
  `destination` can apparently be anything), but also to a very awkward implementation that uses
  strings such as `"?[]"` to encode the previous constraint, instead of properly implementing this
  as part of the type system. It is as if the language was designed while carefully avoiding
  type-system design at all cost.

- For some built-in functions calls, the specification just adds implicit conversions that normally are
  not allowed in regular OSL code. This means that the type-system is essentially bypassed, or at
  least behaves wildly differently in the call sites for these built-in function calls. Needless to
  say, that is not good design because that is not just utterly confusing but also very error prone.
  Initially, the user will most likely not be aware that a `float` can be automatically promoted to
  a `float[2]` in the context of `getattribute`, and if exposed to that behavior, will expect such
  conversions to happen in other places too, which is guaranteed to cause confusion.

- Arguments are passed by reference, always. This prevents cross-module pointer provenance/aliasing
  analysis, and as a result produces slower code. Additionally, this also produces potentially
  surprising behavior at runtime:

  ```cpp
  void f(int i, output int j) {
      printf("%d\n", i);
      j = 3;
      printf("%d\n", i);
  }
  void g() {
      int i = 1;
      f(i, i); // Prints 1 and 3!
  }
  ```

- Inputs to the shader are passed in a mix of arguments, global values, messages, and attributes.
  This means that the shader function signature is no longer enough to understand the inputs and
  outputs of a shader. One must go through the entire code looking for attribute lookups, message
  reads and writes, and global variable accesses. Moreover, this also means that any compiler analysis
  has to do the same, which complicates cross-module analysis.

While OSL can be praised for having a specification at all, it is a rather low bar to clear,
particularly when the specification itself is full of holes, contradicts itself, and leaves out
essential bits of information.

# The specification is very poorly written

OSL has a specification, and unfortunately, it does not always help much clearing out the meaning of
specific syntactic constructs. Part of it is due to OSL being a poorly-designed language, but part of
it is due to the specification itself being poorly written:

- The OSL specification is confusing and often contradicts itself. For instance:

  > The operators +, -, *, /, and the unary - (negation) may be used on most of the numeric types.
  > For multicomponent types (color, point, vector, normal, matrix), these
  > operators combine their arguments on a component-by-component basis.

  This seems to say that addition works on a component-by-component basis for matrices. However, one
  also finds the following text later in the specification:

  > The only operators that may be applied to the matrix type are * and /

  So which one is it? Is matrix addition supported or not? The OSL implementation does not support it.

- In the OSL specification, operator precedence is not indicated for every operator, and cannot be
  deduced from the grammar. In particular, the precedence for ternary and logical operators is
  missing. Additionally, whenever operator precedence is mentioned, it is in the form of a table
  listing the operations that are valid on a given type. That makes no sense, since precedence is a
  parsing property, and no types are available at parsing time.

- Compound expressions are not well defined in the OSL specification. First, the grammar specifies
  that compound initializers are expressions and can thus appear everywhere, but in the
  implementation, compound initializers can only appear in function arguments or variable/parameter
  initializers. Additionally, the specification does not mention anywhere that compound expressions
  can be used to initialize colors, points, or any other built-in multicomponent type. Only in an
  unrelated example can one find a function call which constructs a color via the compound
  initializer syntax:

  > For example, this is equivalent to the preceding example:
  >
  >     RGBA c = {col,alpha};
  >     // deduce by what is being assigned to
  >     RGBA add (RGBA a, RGBA b)
  >     {
  >     return { a.rgb+b.rgb, a.a+b.a }; // deduce by func return type
  >     }
  >     RGBA d = add (c, { {.3,.4,.5}, 0 }); // deduce by expected arg type

  What's more, in the implementation, the compound initializer syntax is actually a synonym for a
  constructor expression:

  ```cpp
  color c = { "rgb", 1, 1, 1 }; // Accepted by the implementation!
  ```

- Overloading is only explained in very vague terms in the OSL specification. It is completely
  unclear how function candidates are selected, and how they are ranked. The relevant part of the
  specification is the following:

  > Functions may be overloaded. That is, multiple functions may be defined to have the same
  > name, as long as they have differently-typed parameters, so that when the function is called the
  > list of arguments can select which version of the function is desired. When there are multiple
  > potential matches, function versions whose argument types match exactly are favored, followed
  > by those that match with type coercion (for example, passing an int when the function expects
  > a float, or passing a float when the function expects a color), and finally by trying to match
  > the return value to the type of variable the result is assigned to.

  Note how there is no clear explanation that describes how two functions containing a mix of
  arguments that match exactly and arguments that need coercions should be ranked, and how there is
  no mention of which behavior to adopt in case of ambiguity. This means that it is up to the user
  to figure out, via trial and error, which one of these functions get chosen for the call site
  in `foo`:

  ```cpp
  void bar(int i, vector v) {}
  void bar(color c, float f) {}
  void bar(point p, normal n) {}
  shader foo() {
      bar(1, 1);
  }
  ```

  Quick tests of the implementation show that it is _not_ following the overloading behavior of C++
  (which expects the selected candidate to be _strictly_ better than all the others).

- The specification says this regarding global variables:

  > Global variables (sometimes called graphics state variables) contain the basic information that
  > the renderer knows about the point being shaded, such as position, surface orientation, and
  > default surface color. You need not declare these variables; they are simply available by default
  > in your shader.

  Note the usual vagueness about where these variables are available: It does say that they are
  available in "the shader", but that could either mean within a `shader`, `surface`, `volume` or
  `displacement` function only, or within the entire shader _file_. The implementation does in fact
  allow accessing global variables from a function:

  ```cpp
  float f() {
      return u;
  }
  shader foo() {
      printf("%f\n", f());
  }
  ```

For explanations or special cases that are not explained by the specification, one may think that it
suffices to use the implementation to see how it behaves in those cases. However, that is foolish
thinking, as we are going to discuss next.

# The implementation is a mess

The OSL implementation generally cannot be relied upon. More particularly, here are a select few
issues that I have personally encountered with it:

- In the OSL implementation, function call arguments corresponding to output function parameters can
  be constants, sometimes. The specification does not say what happens in that case:

  > Function parameters in Open Shading Language are all passed by reference, and are read-only within
  > the body of the function unless they are also designated as output (in the same manner as output
  > shader parameters).

  ```cpp
  void f(output float x) { x = 2; }
  void g() { f(1); } // Accepted by the OSL implementation if 'g' is unused!
  ```

  Interestingly, the following code passes, even if the function containing the error is used:

  ```cpp
  void f(output vector v) { v = 2; }
  void g(float x) { f(vector(x, 1, 2)); } // Accepted by the OSL implementation!
  shader foo() { g(1); }
  ```

  However, passing a vector full of constants does not work, again:

  ```cpp
  void f(output vector v) { v = 2; }
  void g() { f(vector(1, 2, 3)); } // Rejected: "Attempted to write to a constant value"
  shader foo() { g(); }
  ```

  Toying around with similar cases, it becomes possible to get broken code to pass, or to be rejected,
  with different error messages. The following code even generates an invalid error message:

  ```cpp
  void baz(output vector v) { v = 1; }
  shader foo() { baz(1); } // Rejected: "Cannot pass 'int $const1' as argument 1 to baz because it is an output parameter that must be a %s"
  ```

  This is most likely caused by the low-quality of the code in the type-checker, which leads to having
  multiple code paths to handle mutability checks, each with their own set of bugs.

- The OSL implementation considers that global variables are not, in fact, global: It will refuse to
  shadow them from within a shader, as if they were declared locally inside the shader. Take the
  following snippet as an example:

  ```cpp
  shader foo() {
      float u = 1; // Rejected: ""u" already declared in this scope"
  }
  ```

  However, further testing reveals that individual functions can access global variables, and that
  it is possible to shadow those global variables from within a function without an issue:

  ```cpp
  void f() {
      float u = v; // Accepted without a warning. Note how the initializer 'v' is itself a global variable.
  }
  ```

  This strange behavior can also be worked around by adding another layer of braces around a shader:

  ```cpp
  shader foo() {
      {
          float u = 1; // Accepted by the implementation!
      }
  }
  ```

- In the OSL implementation, the increment and decrement operators are allowed on every type. For
  instance, the implementation will accept incrementing a string. Beyond that, there is no way (in
  the language specification) to overload increment or decrement operators. It could be that the
  intent was to allow increment or decrement as syntactic sugar for calls to `__operator__add__` and
  `__operator__sub__`, but that is not said anywhere and the implementation does not reflect that anyway.

  ```cpp
  struct S { string s; };
  void f() {
      S s = { "abcd" };
      s++; // Accepted by the OSL implementation!
  }
  ```

- The OSL implementation does not warn you if you redefine an existing built-in operator. For
  instance, the following code passes:

  ```cpp
  vector __operator__add__(vector x, vector y) {
      return x;
  }
  shader foo() {
      vector x = vector(1) + vector(2);
  }
  ```

- The implementation of the overload resolution is completely bogus. First, failure to resolve an
  overloaded is not always caught:

  ```cpp
  void baz(float x) {}
  void baz(float y) {}
  void foo() {
      baz(1); // Not caught by the implementation!
  }
  ```

  Second, the implementation sometimes also rejects a non-overloaded function that works for the
  given call signature:

  ```cpp
  struct S { string s; int i; };
  void baz(S s) {}
  shader foo() {
      baz({ "abcd", 1 });  // Accepted by the implementation
      S s = { "abcd" };    // Accepted by the implementation
      baz({ "abcd" });     // Rejected by the implementation!
  }
  ```

- The implementation does not support the first variant of `spline` as defined in the specification:

  >     type spline (string basis, float x, type y0 , type y1 , ... type yn-1)
  >     type spline (string basis, float x, type y[])
  >     type spline (string basis, float x, int nknots, type y[])

  ```cpp
  shader foo() {
      vector value = spline("bezier", 0.5, vector(1), vector(2)); // Rejected by the implementation
  }
  ```

# Conclusion

OSL is not a good language, nor a good _shading language_. Its implementation is amazingly poor,
with regards to both the performance of the generated code and the correctness of the compiler itself.
Every programming language design idea that has appeared since the 1980s has been completely ignored
in its design. Sadly, most shading languages are like this: [People working on compilers for other
shading languages](https://xol.io/blah/death-to-shading-languages/) seem to share some of my
frustration.

Anyway, now that most rendering software implements shading with node graphs, the relevance of such
languages is even more put into question. It boggles the mind that we still have to process
human-readable text when the reality is that most shaders are going to be machine-generated anyway.

Perhaps the future is to directly translate
[MaterialX](https://github.com/AcademySoftwareFoundation/MaterialX) into some internal
Intermediate Representation (IR) ? I personally believe that this could all be solved with an IR
which can represent every step of compilation, from the shading graph to the machine code. I am
planning to design such an IR, and target it in NOSL's backend. Or at least I hope to find the time
to do that.
