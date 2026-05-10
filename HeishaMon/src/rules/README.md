# Rules Engine — How It Works

This document explains how `rules.cpp` works end-to-end: from parsing a rule text to executing it at runtime.

---

## Table of Contents

1. [Overview](#overview)
2. [Memory Layout](#memory-layout)
3. [Value Types and Opcodes](#value-types-and-opcodes)
4. [The Node Format](#the-node-format)
5. [Phase 1: Preparation — `rule_prepare`](#phase-1-preparation--rule_prepare)
6. [Phase 2: Compilation — `rule_create`](#phase-2-compilation--rule_create)
7. [Phase 3: Slot Optimisation — `bc_assign_slots`](#phase-3-slot-optimisation--bc_assign_slots)
8. [Phase 4: Execution — `rule_run`](#phase-4-execution--rule_run)
9. [The Function Call Protocol](#the-function-call-protocol)
10. [The Variable Stack](#the-variable-stack)
11. [Helper Functions Reference](#helper-functions-reference)
12. [Why the v4.0 Regression Happened](#why-the-v40-regression-happened)

---

## Overview

The rules engine compiles human-readable rules like:

```
on timer=10 then
  $T = floor(@Main_Outlet_Temp + 9.5);
  setTimer(10, 5);
  if @Z1_Water_Target_Temp != $Z then
    print("test");
  end
end
```

into a compact **bytecode** array that is then executed by a simple virtual machine. The whole pipeline runs on-device (ESP8266/ESP32) and is designed to use as little RAM as possible.

The entry point for the whole process is `rule_initialize` (line ~5352), which calls the three phases in order.

```
rule_initialize()
  ├─ rule_prepare()     // Phase 1 – scan & size
  ├─ rule_create()      // Phase 2 – compile to bytecode
  │    └─ bc_assign_slots()  // Phase 3 – optimise heap slots
  └─ (stores result in rules_t struct)

rule_run()              // Phase 4 – execute bytecode
```

---

## Memory Layout

Every rule lives in a `rules_t` struct with two memory regions:

```
struct rules_t {
    struct rule_stack_t bc;     // bytecode buffer
    struct rule_stack_t *heap;  // value heap
}
```

Both regions are allocated from a single fixed-size **mempool** (`pbuf`) passed in at initialisation. On ESP32 this is 32 KB; on ESP8266 it depends on the secondary heap setting.

There is also one **global** varstack shared across all rules:

```
static struct rule_stack_t *varstack;  // all variables ($local, #global, @sensor, "strings")
static struct rule_stack_t *stack;     // temporary evaluation stack (for function calls)
```

### Bytecode buffer (`bc`)

A flat array of 4-byte `vm_top_t` nodes (see below). Every instruction is exactly 4 bytes. Nodes are walked forward with `bc_next()` and backward with `bc_before()`.

### Heap buffer (`heap`)

A flat array of 4-byte slots that hold **runtime values**. Each slot is one of:

| Struct | Type tag | Contents |
|---|---|---|
| `vm_vnull_t` | `VNULL = 29` | empty / unset |
| `vm_vinteger_t` | `VINTEGER = 27` | 24-bit signed integer packed in 3 bytes |
| `vm_vfloat_t` | `VFLOAT = 28` | 24-bit float (truncated) |
| `vm_vchar_t` | `VCHAR = 25` | pointer to a string in the varstack |

Slot 0 (bytes 0–3) is always reserved/empty. Real slots start at byte offset 4.

Heap positions are referenced by negative `int8_t` values in bytecode nodes (`a`, `b`, `c` fields of `vm_top_t`). Slot at byte offset `N` is addressed as `-(N/4)`. The helpers `vm_val_pos(n)` and `vm_val_posr(pos)` convert between these two representations.

- `vm_val_pos(slot)` — negative slot number → byte offset: `(-slot - 1) * 4 + 4`
- `vm_val_posr(offset)` — byte offset → negative slot number. The parameter is `int16_t` (not `int8_t`) because heap byte offsets can exceed 127 for complex rules.

The slot numbers stored in `vm_top_t` fields are `int8_t` (range −1 to −128), giving a theoretical maximum of 128 heap slots (512 bytes). In practice, complex rules approach but stay well within this limit.

---

## Value Types and Opcodes

Defined in `rules.h`:

### Token / value types
```
VCHAR    = 25   string
VPTR     = 26   internal pointer
VINTEGER = 27   integer
VFLOAT   = 28   float
VNULL    = 29   null / unset
```

### Opcodes
```
OP_EQ…OP_MOD  = 1–14   math and comparison operators (all routed to STEP_OP_MATH)
OP_TEST       = 15      read condition result, set jump flag
OP_JMP        = 16      conditional jump (skip if-block)
OP_SETVAL     = 17      write heap value to a variable
OP_GETVAL     = 18      read a variable into a heap slot
OP_PUSH       = 19      push a heap slot (or varstack string) onto the function-call stack
OP_CALL       = 20      call a built-in function
OP_CLEAR      = 21      pop the function-call stack
OP_RET        = 22      end of rule
```

---

## The Node Format

Every bytecode instruction is a `vm_top_t` (4 bytes, always 4-byte aligned):

```c
typedef struct vm_top_t {
    uint8_t type;   // opcode (lower 5 bits) + group tag (upper 3 bits)
    int8_t  a;      // primary operand / result slot
    int8_t  b;      // left operand / source
    int8_t  c;      // right operand / aux
} vm_top_t;
```

The `type` field stores both the opcode (`gettype()` extracts bits 0–4) and a 3-bit **group** tag used by `bc_assign_slots` to identify which bytecode range a node belongs to.

Fields `a`, `b`, `c` have **context-dependent meanings** per opcode:

| Opcode | `a` | `b` | `c` |
|---|---|---|---|
| Math op (ADD, NE, …) | result heap slot (negative) | left operand heap slot (negative) | right operand heap slot (negative) |
| OP_GETVAL | output heap slot (negative after compile) | varstack slot index | — |
| OP_SETVAL | varstack slot index | source heap slot (negative) | — |
| OP_PUSH | heap slot to push (negative) or varstack index (positive) | — | flag: 1 = varstack, 0 = heap |
| OP_CALL | result heap slot (negative) | function index | 0 = call, 1 = already done |
| OP_TEST | condition heap slot (negative) | — | — |
| OP_JMP | jump distance (in nodes) | depth flag (during compile) | — |
| OP_CLEAR | — | — | — |
| OP_RET | — | — | — |

During compilation, `a`, `b`, `c` hold **positive virtual slot numbers**. After `bc_assign_slots` runs, they are replaced by **negative heap slot numbers**.

**Exception — OP_SETVAL**: the `a` field always holds a varstack index (a small positive integer identifying which variable to write). It is never a virtual slot and is never rewritten by `bc_assign_slots`. The `b` field is converted from virtual to real heap slot normally. This distinction — `a` being a variable index, not a slot reference — is critical for `bc_assign_slots` to work correctly.

---

## Phase 1: Preparation — `rule_prepare`

**Function:** `rule_prepare()` (line ~627)

This is a single-pass scan of the rule text that does **not** build any bytecode. Its only job is to calculate how much memory will be needed:

- `bcsize` — how many bytes of bytecode
- `heapsize` — how many bytes of heap slots
- `varsize` / `memsize` — how much varstack space

It also registers all variable names (`$local`, `#global`, `@sensor`, `"strings"`) into the global `varstack` via `varstack_add()`.

The varstack is shared across all rules. A variable that appears in multiple rules gets one slot that all rules share.

---

## Phase 2: Compilation — `rule_create`

**Function:** `rule_create()` (line ~3055)

This is the main compiler. It walks the token stream produced by the lexer and emits bytecode nodes into `obj->bc` using `bc_parent()`.

### Lexer

The lexer is not a separate pass — it is called on demand via `lexer_peek(text, offset, &type, &start, &len)` which returns the Nth token from the current position without consuming it. This allows the compiler to look ahead cheaply.

Key lexer functions:
- `lexer_peek` (line ~419) — peek at token N
- `lexer_parse_number` (line ~295) — consume a numeric literal
- `lexer_parse_quoted_string` (line ~353) — consume a string literal
- `lexer_parse_skip_characters` (line ~395) — skip whitespace/newlines

### Math expression compilation

Math expressions (`A + B`, `floor(x)`, `A != B`) are compiled by `bc_parse_math_order()` (line ~2604), which handles operator precedence by finding the **lowest-priority operator** furthest from the current position (using `bc_whatfirst`, line ~1945) and recursively compiling left and right sub-expressions.

This produces a bytecode sequence like the following for `floor(@Main + 9.5)`:

```
GETVAL  a=1  b=<@Main varstack idx>    // read @Main → slot 1
PUSH    a=2  c=0                       // push constant 9.5 (from heap)
ADD     a=3  b=1  c=2                  // slot1 + slot2 → slot 3
PUSH    a=3  c=0                       // push ADD result for function call
CALL    a=4  b=<floor idx>             // call floor, result → slot 4
```

### Assignment compilation

`$T = expr` becomes a SETVAL node after the expression that computes the right-hand side.

### If/else compilation

`if cond then … end` becomes:

```
<condition expression nodes>
TEST   a=<cond result slot>    // evaluate condition, set flag t
JMP    a=<distance>            // jump over if-body if t=false
<body nodes>
```

`JMP.a` is initially set to 0 and **back-patched** once the end of the block is known (during THEN/ELSE/END processing in `rule_create`).

### Key compiler helpers

| Function | Line | Purpose |
|---|---|---|
| `bc_parent()` | ~1675 | append one 4-byte node to the bytecode buffer |
| `bc_before()` | ~1692 | return position of previous node |
| `bc_next()` | ~1703 | return position of next node |
| `bc_group()` | ~1715 | tag a range of nodes with a group ID |
| `vm_heap_push()` | ~1728 | allocate a new slot on the heap for a constant value |
| `varstack_find()` | ~521 | look up a variable in the varstack by name |
| `varstack_add()` | ~544 | register a new variable in the varstack |

---

## Phase 3: Slot Optimisation — `bc_assign_slots`

**Function:** `bc_assign_slots()` (line ~2014)

After compilation, all operand fields (`a`, `b`, `c`) in the bytecode nodes hold **positive virtual slot numbers** (1, 2, 3, …). These were assigned sequentially by the compiler and do not map to real heap positions yet.

`bc_assign_slots` replaces these with **negative heap slot numbers** (−1, −2, −3, …), reusing slots whenever a value is no longer needed. This is critical on embedded hardware where the heap can be only a few hundred bytes.

### Why slots can be reused

Consider `$T = floor(@Main + 9.5)`. The execution order is:

1. GETVAL writes `@Main` to slot H1
2. ADD reads H1 and the constant, writes result to H1 (reusing it — @Main is no longer needed)
3. floor() reads from H1 (via the stack), writes result to H2
4. SETVAL reads from H2, writes to `$T`
5. Later: GETVAL @Z1 writes to H2 (reusing it — floor result is no longer needed after SETVAL)

So the entire expression only needs 2 heap slots even though it has 5 operations.

### The two-loop algorithm

`bc_assign_slots` works in two loops, both operating over **bytecode ranges**. A range is a contiguous sequence of nodes between JMP/TEST/RET boundaries — roughly one expression or assignment statement.

#### First loop — virtual slot assignment

Walks the range **in bytecode order**. For each outer node, it:

1. Decrements a counter `vars` (starting at `max*2`) to pick the next virtual slot.
2. Writes that slot into `node->a` (`setval(x->a, vars)`).
3. Scans all **later** nodes in the range and updates any that reference the old slot number in their `b` or `c` fields — propagating the new slot assignment forward through the data-flow chain.

Special cases:
- **OP_GETVAL**: always gets the next decrement (`vars--`), straightforwardly.
- **OP_CALL preceded by OP_PUSH in range**: `vars--` so CALL gets its own unique slot (Fix 1).
- **Math/comparison ops with operands already assigned**: `vars = MAX(operands) - 1` so the result slot is below both operands (Fix 3).
- **OP_SETVAL**: the `vars--` counter still runs (so slot numbering stays consistent), but `setval(x->a, vars)` is skipped (because `a` is a variable index, not a slot) **and the inner scan is also skipped** (Fix 5a). If the inner scan ran, it would use SETVAL's variable index as if it were a virtual slot number and corrupt any other node that happened to hold the same value in its fields.
- **OP_CLEAR**: skipped entirely — it has no slot to assign.

#### Second loop — map virtual → real heap slots

Walks the range in bytecode order. For each node with a virtual slot `d >= min`:

1. Finds or allocates a VNULL slot in the heap at the current `offset` position (via `vm_heap_next` / `vm_heap_push`).
2. Converts that heap byte position to a negative slot number `e = vm_val_posr(pos)`.
3. Updates **all** nodes in the range that reference `d` in their `a`, `b`, or `c` fields to `e`.
4. Increments `offset` so the next node gets a fresh heap position.

Key detail: `offset` is declared **outside** the `while(1)` range loop so it accumulates across ranges and different ranges don't collide on the same heap slots (Fix 2). Within each range, `offset` always increments unconditionally after each outer node so that even isolated nodes (like a PUSH with no dependents) get a unique slot.

**OP_SETVAL is always skipped as an outer node** (Fix 5b). Even though SETVAL appears in the bytecode and is iterated over, it must not be treated as the "current outer" because its `a` field is a variable index, not a virtual slot. If it were processed, the second loop would call `vm_heap_next` and allocate a heap slot for what is actually a variable number, then scan all nodes and overwrite any that coincidentally hold the same value in their `a` field — including the NE result slot, which would then be skipped when the second loop reached it (since its `a` would already appear negative).

**`vm_val_posr` takes `int16_t`** (Fix 4). The function receives a byte offset from `vm_heap_next` (which can be up to ~500 bytes for complex rules). If the parameter were `int8_t`, byte offsets ≥ 128 would silently overflow to a negative value, causing the formula to produce a *positive* slot number. Positive slot numbers in `a`/`b`/`c` fields are invalid at runtime and cause an immediate fatal error.

The VNULL-preservation check (`if heap[b] == VNULL && first == 1`) adds an extra `offset` increment when the first node in a range has operands that are already NULL — this preserves unset variables as NULL rather than silently aliasing them.

---

## Phase 4: Execution — `rule_run`

**Function:** `rule_run()` (line ~4287)

This is the virtual machine. It uses a **computed-goto jump table** (`jmptbl`) for maximum speed on the ESP — no switch statement, just `goto *jmptbl[opcode]`.

```c
BEGIN:
    type = gettype(obj->bc.buffer[pos]);
    goto *jmptbl[type];
```

After each handler runs, it advances `pos` by `sizeof(vm_top_t)` (4 bytes) and jumps back to `BEGIN`.

### Execution steps

#### `STEP_OP_MATH` (opcodes 1–14)

Handles all arithmetic and comparison operators. Reads `x` from `heap[node->b]` and `y` from `heap[node->c]`, computes the result, writes it to `heap[node->a]`.

All three slot fields hold negative values (heap positions). `vm_val_pos(n)` converts a negative slot number to a byte offset: `(-n - 1) * 4`.

If either operand is VNULL, the result is NULL (the if-condition evaluates as false).

#### `STEP_TEST` (opcode 15)

Reads the value at `heap[node->a]`. Sets the flag `t = 1` if the value is non-zero (true), `t = 0` otherwise.

#### `STEP_JMP` (opcode 16)

If `t == 0` (condition was false), jumps forward by `node->a` nodes. Otherwise falls through to the if-body.

#### `STEP_SETVAL` (opcode 17)

Reads the value from `heap[node->b]` and writes it to the variable at varstack position `node->a * sizeof(vm_vchar_t)`. Also invokes `rule_options.vm_value_set()` so the host application can act on the change (e.g. publish to MQTT).

#### `STEP_GETVAL` (opcode 18)

Calls `rule_options.vm_value_get()` to let the host application fill in the current value of a sensor or variable. Writes the result to `heap[node->a]`.

#### `STEP_PUSH` (opcode 19)

Pushes a value onto the **function-call stack** (`stack`):
- If `node->a < 0`: push the heap slot at byte offset `vm_val_pos(node->a)`.
- If `node->a > 0`: push the varstack slot at `(node->a - 1) * sizeof(vm_vchar_t)` (for string literals).

#### `STEP_CALL` (opcode 20)

Calls the built-in function identified by `node->b` (index into `rule_functions[]`). The function reads its arguments from the stack using `rules_gettop()` / `rules_tointeger()` etc., and pushes its return value with `rules_pushinteger()` / `rules_pushfloat()` etc.

After the callback returns, `STEP_CALL` pops the top stack value and writes it to `heap[node->a]` (the result slot assigned by `bc_assign_slots`).

#### `STEP_CLEAR` (opcode 21)

Pops one entry off the function-call stack.

#### `STEP_RET` (opcode 22)

Ends execution of the rule. Prints local and global variable values in DEBUG mode.

---

## The Function Call Protocol

Built-in functions (in `functions/`) use a **Lua-style stack API**:

```c
// Inside a function callback:
uint8_t nr = rules_gettop();     // number of args on stack
int x = rules_tointeger(nr);     // read top arg as integer (1-based from top)
rules_remove(nr);                // pop it
rules_pushinteger(result);       // push return value
```

The `STEP_PUSH` nodes before `STEP_CALL` push each argument. `STEP_CALL` pops the return value from the stack and stores it in the designated heap slot.

The global `stack` buffer is reset at the start of every `rule_run()` call:
```c
memset(stack->buffer, 0, getval(stack->bufsize));
setval(stack->nrbytes, 4);
```

---

## The Variable Stack

The global `varstack` stores all named variables as `vm_vchar_t` records (8 bytes each):

```c
typedef struct vm_vchar_t {
    uint8_t type;    // VCHAR, VINTEGER, VFLOAT, VNULL
    uint8_t fixed;   // 1 = global/sensor, 0 = local
    uint8_t len;     // name length
    uint8_t ref;     // reference count
    char *value;     // pointer to string value (or numeric value inline)
} vm_vchar_t;
```

`STEP_GETVAL` and `STEP_SETVAL` address varstack entries by their **byte offset** from the start of `varstack->buffer`. The `b` field in GETVAL and the `a` field in SETVAL hold these byte offsets (divided by `sizeof(vm_vchar_t)` = 4 during compilation, then multiplied back at runtime).

Maximum 127 variables per ruleset (enforced in `rule_initialize`).

---

## Helper Functions Reference

| Function | Line | Description |
|---|---|---|
| `rule_initialize()` | ~5352 | Entry point: allocates memory, runs all three phases |
| `rule_prepare()` | ~627 | Phase 1: scan text, size all buffers |
| `rule_create()` | ~3055 | Phase 2: compile text to bytecode |
| `bc_assign_slots()` | ~2014 | Phase 3: assign heap slots to bytecode nodes |
| `rule_run()` | ~4287 | Phase 4: execute bytecode |
| `bc_parent()` | ~1675 | Append one node to the bytecode buffer |
| `bc_before()` | ~1692 | Position of the previous node |
| `bc_next()` | ~1703 | Position of the next node |
| `bc_group()` | ~1715 | Tag a node range with a group ID |
| `bc_whatfirst()` | ~1945 | Find lowest-priority operator for expression parsing |
| `bc_parse_math_order()` | ~2604 | Recursively compile a math/comparison expression |
| `bc_find_math_dep()` | ~2293 | Find the nearest node that produces a given virtual slot |
| `bc_math_move_closest()` | ~2305 | Reorder bytecode to move dependent nodes adjacent |
| `vm_heap_push()` | ~1728 | Allocate a heap slot for a constant value |
| `vm_heap_next()` | ~1913 | Find the Nth VNULL heap slot (for slot reuse) |
| `vm_val_pos()` | ~1466 | Negative slot number → byte offset: `(-n-1)*4` |
| `vm_val_posr()` | ~1476 | Byte offset → negative slot number |
| `varstack_find()` | ~521 | Look up a variable name in the varstack |
| `varstack_add()` | ~544 | Register a variable name in the varstack |
| `rules_pushinteger()` | ~1509 | Push integer onto the function-call stack |
| `rules_pushfloat()` | ~1525 | Push float onto the function-call stack |
| `rules_pushnil()` | ~1496 | Push NULL onto the function-call stack |
| `rules_tointeger()` | ~1613 | Read integer from function-call stack position |
| `rules_gettop()` | ~1663 | Number of items on the function-call stack |
| `rules_remove()` | ~1667 | Remove item from function-call stack |
| `rules_gc()` | ~5310 | Free all rules and reset varstack |
| `rules_memused()` | ~5305 | Return total memory used (DEBUG/COVERALLS only) |
| `print_heap()` | ~5146 | Dump heap contents (DEBUG only) |
| `print_bytecode()` | ~5276 | Dump bytecode disassembly (DEBUG only) |

---

## Why the v4.0 Regression Happened

In v3.9 the rules engine assigned heap slots **linearly** — every node got the next available slot, no reuse. Simple and correct, but wastes memory.

v4.0 introduced `bc_assign_slots` to reuse slots and cut heap usage. The optimizer had three bugs that only surface in rules combining a function call (`floor()`), a subsequent function call (`setTimer()`), and a conditional comparison (`!=`):

**Bug 1 — CALL aliased a GETVAL slot (Fix 1)**

In the first loop, when `OP_CALL` was preceded by `OP_PUSH` with a slot already in the assigned range, `vars` was not decremented. CALL floor ended up with the same virtual slot as GETVAL @Main_Outlet_Temp. In the second loop, SETVAL $T was mapped to GETVAL's heap slot instead of CALL's, so `$T` received the raw float sensor value (37.5) instead of the floored integer (37).

**Bug 2 — Inter-range and intra-range heap slot collisions (Fix 2)**

`offset` was declared inside the `while(1)` range loop, resetting to 0 for each range. The second range (containing `PUSH "test"` and `CALL print`) started at offset 0 and got the same heap slots as the first range. Additionally, `offset` was only incremented when `changed == 1`, so isolated nodes within a range (like a PUSH with no dependents) were not given their own slot. Both problems caused unrelated operations to read from and write to the same heap slot.

**Bug 3 — Comparison op result aliased an operand slot (Fix 3)**

In the first loop, when a comparison op (NE) had operands at virtual slots `b` and `c`, the code set `vars = MAX(b, c)` without decrementing. NE's result slot ended up equal to its own operand slot. This was actually harmless for this specific rule because the second loop maps both to the same heap slot and the execution order guarantees the operand is read before the result is written — but it was still incorrect and masked a deeper issue with how the first loop tracks slot identity.

**Bug 4 — `vm_val_posr` int8_t overflow for heaps > 127 bytes (Fix 4)**

`vm_val_posr` converts a heap byte offset back to a negative slot number. Its parameter was declared `int8_t`. For complex rules, `vm_heap_next` returns byte offsets ≥ 128 (e.g. slot −32 lives at byte 128). Passing 128 to an `int8_t` parameter silently wraps to −128, and the formula `(−128 − 4) / 4 * −1 − 1 = 32` produces a *positive* slot number. A positive value in any `a`/`b`/`c` field is invalid; the VM catches it immediately and raises a fatal error. The fix changes the parameter to `int16_t`.

This bug is triggered by any rule complex enough to need more than ~31 temporary computation slots (heap byte offset ≥ 128). The bc_assign bugs from fixes 1–3 also inflated the heap artificially (by failing to reuse slots, forcing extra `vm_heap_push` calls), which made this threshold easier to hit.

**Bug 5 — SETVAL variable index treated as virtual slot (Fix 5a + 5b)**

When a rule contains a variable assignment (`$X = expr`) that is not the last node in a bytecode range — for example, when a `setTimer()` call follows the assignment — SETVAL ends up in the *middle* of a range rather than at its end.

The bug had two parts:

*5a — loop1 inner scan (Fix 5a)*: The first loop would process SETVAL as an outer node, read its `a` field (the variable index, e.g. 3), and then scan all later nodes for any that held the value 3 in their own fields. Any such node got its field updated to the "new virtual slot" — including NE's result slot, which coincidentally was also 3. NE's result slot was overwritten to a different value, and when the first loop later processed NE as outer, NE correctly wrote its own slot. But in the second loop, bc_assign saw NE's `a` already as negative (from a previous mapping step), skipped NE, and left NE's `b` operand unmapped (still a positive virtual slot). At runtime the VM's `b ≥ 0` guard fires and crashes.

*5b — second loop outer processing (Fix 5b)*: Even with 5a fixed, the second loop would also process SETVAL as an outer node. It would call `vm_heap_next` and allocate a real heap slot for the variable index (3), then update any node with a matching field to that heap slot — again corrupting NE's `a`.

The fix: in loop1, skip the inner scan when the current node is SETVAL (but still decrement `vars` so the slot counter stays correct). In the second loop, skip SETVAL entirely as an outer node. SETVAL's `b` field is correctly mapped when the upstream math op (e.g. OP_ADD) is processed as outer.
