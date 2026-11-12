# Rolang Semantics Decisions

This register tracks decisions for the proposed Version 1 language contract.
The [current implementation specification](LANGUAGE_SPEC.md) documents
observable C++ behavior; the [Version 1 proposal](V1_SEMANTICS_PROPOSAL.md)
describes the recommended future contract. Neither this register nor the
proposal changes compiler behavior.

## Approval register

| Topic | Proposal | Status |
| --- | --- | --- |
| Function locals and global writes | Local-by-default bindings; globals remain readable; a function must declare `global name` to modify that module global. The declaration applies function-wide. A local assigned anywhere is unbound until assigned. | **Approval required** |
| Nested functions | Retain nested definitions but do not capture enclosing locals (no closures in V1). | **Approval required** |
| Dynamic typing | Keep V1 dynamically typed; defer optional annotations and static checking. | Proposed direction; annotation syntax/model deferred |
| `and` and `or` | Short-circuit and return Booleans; retain current truthiness. | Proposed direction; approve truthiness details |
| Integer and float values | Make them distinct; proposed signed 64-bit integers with checked overflow. | **Approval required** for range, overflow, mixed operations, Boolean arithmetic, powers, and ordering |
| Equality | Strictly type-aware; different runtime types compare unequal. | **Approval required** |
| Function calls | Exact positional argument count; defer defaults and keyword arguments. | Proposed direction |
| Loop control | Add `break` and `continue` for the innermost `while`; reject outside loops. | Proposed direction |
| Recursion safety | Retain the current 128 active-call and 1,000,000-instruction-per-run limits. | **Approval required** whether these are V1 guarantees |
| Runtime exceptions | Keep failures fatal and source-located; defer structured, catchable exceptions. | Proposed direction; future feature requires a separate design |
| Division and modulo | Decide integer rounding and remainder-sign rules; explicitly decide whether to retain current float behavior. | **Approval required** |

## Implementation boundary

The target remains the current C++17 frontend and bytecode VM. No Python
compiler replacement, unnecessary dependency, removal of unrelated
functionality, IR, or native-code generation is part of this proposal.
Implementation changes must follow an explicit approval of the open entries
above and be covered by the per-decision tests in
[V1_SEMANTICS_PROPOSAL.md](V1_SEMANTICS_PROPOSAL.md).
