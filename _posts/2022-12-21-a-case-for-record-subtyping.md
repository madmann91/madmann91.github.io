---
layout: post
title: "A Case for Record Subtyping"
usemathjax: true
tags:
    - compiler design
    - type system
---

{% include mathjax.html %}

This post is a defense of a fairly uncommon programming language feature which I believe improves
traditional functional design patterns significantly: Record subtyping. In the first few sections, I
introduce records in programming languages, and later introduce a problem for which record
subtyping provides a very clean solution, when combined with standard functional language features
such as sum types.

Records are a very common programming language feature, to the point where finding a language which
does not support them in one way or another is quite hard. However, they come in different forms and
flavours that are not all equivalent.

# Nominal Records

In C, records are introduced with the keyword `struct`. C records are _nominal_, in the sense that
they are identified by their _name_: Another `struct` with the same fields declared in the same
order would be considered a different type by the compiler, as shown in this example:

```c
struct Foo {
    int a;
    int b;
};
struct Bar {
    int a;
    int b;
};
int get_foo_a(Foo foo) {
    return foo.a;
}
int get_bar_a(Bar bar) {
    return get_foo_a(bar); // ERROR: Foo is not the same type as Bar
}
```

As it turns out, making records nominal is a very common choice, and most mainstream programming
languages, functional or not, make the choice of having nominal records. However, it is indeed
possible to make records _structural_.

# Structural Records

Structural records are records that are identified by their _structure_: The field names and their
types determine if two record types compare equal. There are different ways to compare the fields:
One can assume that order matters, or one can compare fields regardless of their order. Both options
are valid, but the latter one is more expressive. We will assume that fields are compared regardless
of their order in the remaining of the post.

As an example, consider a hypothetical language syntax using `[l1: T1, ..., ln: Tn]` for a record
_type_ with field labels `l1`, ..., `ln` and respective field types `T1`, ..., `Tn`. Similarly,
record _values_ are introduced with `[l1 = e1, ..., ln = en]` where `l1`, ..., `ln` are field labels
and `e1`, ..., `en` are expressions used to initialize the fields. With that syntax, we can write
code such as:

```ml
const foo: [a: i32, b: i32] = [a = 1, b = 2]; // Declare `foo` with type `[a: i32, b: i32]`
const bar = [a = 1, b = 2]; // Declare `bar` with type `[a: i32, b: i32]`, inferred by the compiler
```

This has the benefit that it makes code that operates on records much more generic, but now it is
virtually impossible to distinguish between structurally equivalent records:

```ml
fun get_person_name(person: [name: str, age: u32]) = person.name;
fun get_animal_age(animal: [name: str, age: u32]) = animal.age;

const peter_pan = [name: "Peter Pan", age: 120];
const felix_the_cat = [name: "Felix the Cat", age: 102];
get_animal_age(peter_pan); // Works
get_person_name(felix_the_cat); // Works
```

This can be remedied by allowing the user to declare nominal types, with a mechanism such as
`newtype` in Haskell, where a new type is introduced with a constructor name that distinguishes it
from the type it contains[^1].

Very quickly, with structural records, the question of record _subtyping_ starts to emerge: When a
record with type `[a: i32, b: i16]` is expected, can I pass a record of type `[a: i32, b: i16, c:
i64]` instead?

# Record Subtyping

A standard feature of many type systems, and particularly object-oriented ones, is _subtyping_. This
feature essentially boils down to adding another relation `T <: U` where `T` and `U` are types,
which represents whether `T` is a subtype of `U` or not.

In the fairly common view where types are seen as sets of values, `T <: U` means that the set of
values `T` is contained in the set of values `U`. For instance, one could add the subtying rules
`uN <: uM`, where `N` $$\leq$$ `M` (i.e. `u8 <: u8`, `u8 <: u16`, ...). This would allow passing
integers of a smaller bit width where an integer with a larger bit width is expected.

Inheritance can also be modeled using subtyping, by adding a rule that `D <: C` for two class types
`C` and `D` where `D` is derived from `C`.

In the case of records, we can write that
`[l1: T1, ..., ln: Tn] <: [l1: T1, ..., ln: Tn, ln+1: Tn+1, ..., ln+k: Tn+k]`, where `l1`, ...,
`ln+k` are field labels and `T1`, ..., `Tn+k` are types. This then allows passing larger tuples
where smaller ones are expected. In the literature, this is referred to as _record subtyping_.

This feature is really practical and desirable, as we are going to discuss below.

# The Problem

Imagine that we have to write a program that manipulates a set of animals. There are different types
of animals, with different properties, such as age or fur color. The code that manipulates those
properties does not actually care about the animal. Consider for instance a rendering algorithm
that renders an image of a patch of fur for any animal with fur: It does not matter that the fur
comes from a panda or a dog, as long as you know how to render it.

## Modeling Properties with Inheritance

In an object-oriented language like C++, Java, or Scala, the traditional answer to these types of
problems is inheritance:

```cpp
#include <optional>

enum class Color { BROWN, WHITE, GRAY, BLACK };

class Animal {
public:
    virtual ~Animal() = default;
    virtual unsigned get_age() const { return age_; }
    virtual std::optional<Color> get_fur_color() const { return std::nullopt; }

protected:
    Animal(unsigned age) : age_(age) {}

    unsigned age_;
};

class FurAnimal : public Animal {
public:
    std::optional<Color> get_fur_color() const { return fur_color_; }

protected:
    FurAnimal(unsigned age, Color fur_color)
        : Animal(age), fur_color_(fur_color)
    {}

    Color fur_color_;
};

class Dog : public FurAnimal {
public:
    Dog(unsigned age, Color fur_color)
        : FurAnimal(age, fur_color)
    {}
};

class Cat : public FurAnimal {
public:
    Cat(unsigned age, Color fur_color)
        : FurAnimal(age, fur_color)
    {}
};

class Frog : public Animal {
public:
    Frog(unsigned age)
        : Animal(age)
    {}
};
```

In this example, `Dog` and `Cat` both provide an implementation of `get_fur_color()` that returns a
color for the fur, but `Frog` does uses the default implementation that returns an empty
`optional` object.

This design is nice because it is easy to add more animals, with or without fur. Now, imagine that
we need to add another property, such as the speed with which the animal swims. This property is
only available for frogs and dogs, as cats do not like water (in this example).

The corresponding code might look like:

```cpp
class Animal {
public:
    virtual ~Animal() = default;

    unsigned get_age() const { return age_; }
    virtual std::optional<unsigned> get_swim_speed() const { return std::nullopt; }
    virtual std::optional<Color> get_fur_color() const { return std::nullopt; }

protected:
    Animal(unsigned age) : age_(age) {}

    unsigned age_;
};

class FurAnimal : public Animal {
public:
    std::optional<Color> get_fur_color() const override { return fur_color_; }

protected:
    FurAnimal(unsigned age, Color fur_color)
        : Animal(age), fur_color_(fur_color)
    {}

    Color fur_color_;
};

class Dog : public FurAnimal {
public:
    Dog(unsigned age, Color fur_color, unsigned swim_speed)
        : FurAnimal(age, fur_color), swim_speed_(swim_speed)
    {}

    std::optional<unsigned> get_swim_speed() const override { return swim_speed_; }

protected:
    unsigned swim_speed_;
};

class Cat : public FurAnimal {
public:
    Cat(unsigned age, Color fur_color)
        : FurAnimal(age, fur_color)
    {}
};

class Frog : public Animal {
public:
    Frog(unsigned age, unsigned swim_speed)
        : Animal(age), swim_speed_(swim_speed)
    {}

    std::optional<unsigned> get_swim_speed() const override { return swim_speed_; }

protected:
    unsigned swim_speed_;
};
```

As you can see, this is not really elegant anymore. We cannot extract the common functionality for
animals that swim because they live in different parts of the class hierarchy (`Animal` for `Frog`
and `FurAnimal` for `Dog`). In C++, the solution to this is to introduce _mixins_:

```cpp
class Animal {
public:
    virtual ~Animal() = default;

    unsigned get_age() const { return age_; }
    virtual std::optional<unsigned> get_swim_speed() const { return std::nullopt; }
    virtual std::optional<Color> get_fur_color() const { return std::nullopt; }

protected:
    Animal(unsigned age) : age_(age) {}

    unsigned age_;
};

// Mixin for animals with fur
template <typename Base>
class HasFur : public Base {
public:
    std::optional<Color> get_fur_color() const override { return fur_color_; }

protected:
    template <typename... Args>
    HasFur(Color fur_color, Args&&... args)
        : Base(std::forward<Args>(args)...), fur_color_(fur_color)
    {}

    Color fur_color_;
};

// Mixin for animals with swimming abilities
template <typename Base>
class CanSwim : public Base {
public:
    std::optional<unsigned> get_swim_speed() const override { return swim_speed_; }

protected:
    template <typename... Args>
    CanSwim(unsigned swim_speed, Args&&... args)
        : Base(std::forward<Args>(args)...), swim_speed_(swim_speed)
    {}

    unsigned swim_speed_;
};

class Dog : public CanSwim<HasFur<Animal>> {
public:
    Dog(unsigned age, Color fur_color, unsigned swim_speed)
        : CanSwim<HasFur<Animal>>(swim_speed, fur_color, age)
    {}
};

class Cat : public HasFur<Animal> {
public:
    Cat(unsigned age, Color fur_color)
        : HasFur<Animal>(fur_color, age)
    {}
};

class Frog : public CanSwim<Animal> {
public:
    Frog(unsigned age, unsigned swim_speed)
        : CanSwim<Animal>(swim_speed, age)
    {}
};
```

This solution is a bit better, but it restricts the way constructors have to be declared, such that
the calls can be chained along the mixin hierarchy (because of the way variadic templates are used to
propagate the arguments to the parent class). Another major issue is that the order in which a given mixin
appears in the hierarchy matters for runtime casts. For instance, if I want to check if a given
`Animal*` is an animal that can swim and has fur, the following code may not work:

```cpp
bool can_have_wet_fur(const Animal* animal) {
    // Will not work for animals that inherit from `CanSwim<HasFur<Animal>>`,
    // or those that inherit from `CanSwim<HasTail<HasFur<Animal>>>`.
    return dynamic_cast<const HasFur<CanSwim<Animal>*>(animal) != nullptr;
}
```

All in all, encoding those properties with inheritance is not really satisfying. Thankfully, there
is another way to encode those properties: Algebraic Data Types (ADTs), like `enum` types in Rust.

## Modeling Properties with Rust Enumerations

A literal transcription of the example above in Rust may result in:

```rust
enum Color { BROWN, WHITE, GRAY, BLACK }

enum Animal {
    Cat  { age: u32, fur_color: Color },
    Frog { age: u32, swim_speed: u32 },
    Dog  { age: u32, fur_color: Color, swim_speed: u32 }
}

impl Animal {
    fun get_fur_color(a: Animal) -> Option<Color> {
        match a {
            Cat { fur_color, .. } |
            Dog { fur_color, .. } => Some(fur_color),
            _ => None
        }
    }

    fun get_swim_speed(a: Animal) -> Option<u32> {
        match a {
            Frog { swim_speed, .. } |
            Dog  { swim_speed, .. } => Some(swim_speed),
            _ => None
        }
    }

    fun get_age(a: Animal) -> u32 {
        match a {
            Frog { age, .. } |
            Dog  { age, .. } |
            Cat  { age, .. } => age
        }
    }
}
```

While this code is compact and short, it is in fact arguably worse than the C++ code. First of all,
no new animals can be added outside of this module. This is a form of "closed world assumption".
Second, this code generates more code duplication than the C++ variant. For every new type of animal,
a new pattern will have to be added to _every_ `match` expression. In the C++ version with mixins,
the functionality is written once and all that is needed is to just inherit from the desired
sequence of mixins corresponding to the properties to represent.

Conceptually, it is also debatable to ask the programmer to deconstruct the type of the object in
order to access a property like `age`, which every animal type has. Granted, it is always possible
to refactor the code by pulling the property out of the enumeration in an enclosing object, like this:

```rust
struct Animal {
    age: u32,
    data: AnimalData
}
enum AnimalData {
    Cat { /* age: u32 is no longer needed */, fur_color: Color },
    ...
}
```

But what if the property is present for 99% of the animal types, but not for all of them? Clearly,
this is not ideal and Rust does not have a nice solution for this either.

## Modeling Properties with Structural Sum and Record Types

Instead of working with nominal types such as Rust's enumerations, we can instead work with
structural sum and record types. Structural sum types are types of the form `T1 | T2 | ... | Tn`. A
value of such a type is either of type `T1`, `T2`, ..., or `Tn`. The reason these types are called
sum types is because, under the "types are sets of values" interpretation, `T1 | T2` is the set made
of the union (sum) of the set of values corresponding to `T1` and the one for `T2`.

With the same hypothetical language syntax as before, enhanced with sum types, it becomes possible
to write code like:

```rust
alias Animal  = [ age: u32 ];
alias CanSwim = [ swim_speed: u32 ];
alias HasFur  = [ fur_color: Color ];

// The syntax:
//   subtype T = U;
// creates a nominal type T that can be implicitly converted to U, but not the other way around.
// This means that a `Cat` can be converted to an `Animal`, but an `Animal` cannot be converted to a
// `Cat`. Also, note the use of `&` for record concatenation: `Animal & HasFur` is the same as
// the type `[age: u32, fur_color: Color]`.
subtype Cat  = Animal & HasFur; 
subtype Dog  = Animal & HasFur & CanSwim;
subtype Frog = Animal & CanSwim;

alias AnyAnimal = Cat | Dog | Frog;

// Works, the type-checker sees that every possible value taken by an animal has an 'age' property.
fun get_age(animal: Animal) = animal.age;
// Works, this is restricted to only cats and dogs (encoded in the type!).
fun get_fur_color(animal: HasFur) = animal.fur_color;
// Works, this is restricted to only frogs and dogs (encoded in the type!).
fun get_swim_speed(animal: CanSwim) = animal.swim_speed;
```

This syntax is not only nicer, but we now have types that mean way more than they did in the Rust or
C++ code, as we can encode the fact that dogs and cats have fur directly in the type system.

What is more, with record subtyping, one can extend the `Cat` type to contain other properties, and
use it as a regular animal:

```rust
subtype SwimSuitCat = Cat & CanSwim;

const cat = SwimSuitCat [ age = 5, fur_color = WHITE, swim_speed = 1 ];

get_age(cat); // Works
get_fur_color(cat); // Works
get_swim_speed(cat); // Works
```

Records and sum types with subtyping allow us to separate the properties of `Animal` from its tag
(i.e. `Dog`, `Frog` or `Cat`), in a transparent manner. With inheritance or standard enumeration
types, as offered by C++, Rust or Haskell, this separation turns out to be difficult to implement,
or even impossible.

Hopefully, this should convince anyone of the usefulness of supporting subtyping with record and sum
types in a programming language, and more languages get implemented with that design.

[^1]: This is conceptually the same as defining a `struct` in C with a single field that wraps
    the contained type.
