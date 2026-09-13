# Code conventions

Conventions this codebase follows beyond ordinary C++ and Qt practice.

## Qt

- **`Q_OBJECT` only on a class that declares signals.** Slots connect through the pointer-to-member `connect`
  overload with no meta-object; every `Q_OBJECT` costs a moc pass and a generated translation unit.
  - Still requires it: `Q_PROPERTY`, `Q_ENUM`, `Q_INVOKABLE`, a slot invoked by name (`SLOT()`, `invokeMethod` with a
    string), `qobject_cast` to the class. Cast to a class without a meta-object with `dynamic_cast`.
  - Without it `tr()` uses the base class's translation context: known and accepted.

## Design

- **A helper must not replace an idiom unless it removes something that matters.** A reader who knows the language or
  framework reads the idiom at a glance and has to open the helper. What justifies that: logic, a lifetime or ordering
  rule, a precondition the call sites would otherwise each get right. Fewer characters and one hidden name do not.
  - Example: subscribers call `connect(&X::instance(), &X::signal, context, fn)`, not an `X::subscribe(context, fn)`.

## Naming

- **Never "chrome" for UI framing**, in code, comments or docs. Name the element: window background, toolbars,
  separators. The word is ambiguous with the browser and with "monochrome".
