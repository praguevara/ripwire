; Gleam (tree-sitter-gleam). A module's definitions are all top level: functions (an `@external` one has
; no body), constants, custom types with their constructors, type aliases and external types. The
; attribute run above a definition (`@external(erlang, "m", "f")`, `@deprecated(…)`) is a sibling node;
; ingest_gleam.h widens the definition's span over it, so the signature and --expand carry it.
(function name: (identifier) @name) @definition.function
(external_function name: (identifier) @name) @definition.function
(constant name: (identifier) @name) @definition.constant
(type_definition (type_name name: (type_identifier) @name)) @definition.type
(external_type (type_name name: (type_identifier) @name)) @definition.type
(type_alias (type_name name: (type_identifier) @name)) @definition.alias
(data_constructor name: (constructor_name) @name) @definition.constant

; One dependency per `import a/b/c`. Its alias and its unqualified names are read by ingest_gleam.h.
(import module: (module) @import.path)

; References. Gleam scoping is closed and lexical, so ingest_gleam.h decides each one in its own file: a
; field access is kept only when its record names an imported module (`board.place`, as a call, a pipe
; target or a function value), and a bare name only when the module defines it or imports it unqualified.
; Everything else is a record field or a local variable, and emits nothing.
(function_call function: (identifier) @name) @reference.call
(field_access record: (identifier) field: (label) @name) @reference.call
(binary_expression operator: "|>" right: (identifier) @name) @reference.call
(argument value: (identifier) @name) @reference.call
(record name: (constructor_name) @name) @reference.call
(record name: (remote_constructor_name name: (constructor_name) @name)) @reference.call
