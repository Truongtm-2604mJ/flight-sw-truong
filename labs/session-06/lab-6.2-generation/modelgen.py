#!/usr/bin/env python3
"""Lab 6.2 - Code generator for cyclic state machine models (Path B).

Reads a model file (Lab 6.1, YAML), validates it, and emits:

    <prefix>.h            public interface: enums, types, API, action hooks
    <prefix>.c            the machine: entry/exit functions, one function
                          per transition, a bounded step()
    <prefix>_trace.json   model element -> generated symbol and line range,
                          with the requirement IDs carried by the model

Usage:
    modelgen.py MODEL --out DIR [--prefix mode_sm]   generate
    modelgen.py MODEL --out DIR --check              fail if DIR differs
                                                     from a fresh generation
    modelgen.py MODEL --report FILE                  completeness report

Properties of the generated code, by construction:
  - static allocation only: the caller owns the machine object;
  - no loops except one bounded by the number of events, no recursion;
  - at most one transition per step;
  - no policy: every action is an extern hook the application implements;
  - no code for constructs the model does not use (no empty exit
    functions, no guard code for unguarded transitions);
  - a banner with the model name, the SHA-256 of the model file and of
    this generator, so a hand edit is detectable (--check).

The generator refuses a model that is incomplete (an event neither handled
nor ignored in some state), ambiguous (two transitions with one priority
in a state), or has guards that do not cover every input combination for
a handled event.
"""

import argparse
import hashlib
import itertools
import json
import os
import re
import sys

import yaml

GENERATOR_VERSION = "1.1"
IDENT = re.compile(r"^[A-Za-z_][A-Za-z0-9_]*$")

SUPPORTED_SEMANTICS = {
    "evaluation": {"cyclic"},
    "input_sampling": {"start_of_cycle"},
    "max_transitions_per_cycle": {1},
    "conflict_resolution": {"priority"},
    "self_transition_runs_entry": {False},
    "unconsumed_one_shot": {"discard_and_report"},
    "unconsumed_latched": {"retain_if_transition_fired"},
    # v1.1 (D-05): a ground command ignored in the start state is reported
    # as discarded when an external transition fired in the same cycle,
    # because the vehicle then ends the cycle in another mode than the one
    # commanded. It stays silent only when the vehicle is (and remains) in
    # the commanded mode.
    "ignored_ground_command": {"report_if_state_changed"},
}


class ModelError(Exception):
    pass


# ---- Guard expressions ---------------------------------------------------------
#
#   expr   := term ( '||' term )*
#   term   := factor ( '&&' factor )*
#   factor := '!' factor | '(' expr ')' | 'true' | 'false' | <bool input>

def _tokens(text):
    i, out = 0, []
    while i < len(text):
        c = text[i]
        if c.isspace():
            i += 1
        elif text.startswith("&&", i) or text.startswith("||", i):
            out.append(text[i:i + 2])
            i += 2
        elif c in "!()":
            out.append(c)
            i += 1
        else:
            m = re.match(r"[A-Za-z_][A-Za-z0-9_]*", text[i:])
            if not m:
                raise ModelError(f"guard '{text}': unexpected '{c}'")
            out.append(m.group(0))
            i += len(m.group(0))
    return out


def parse_guard(text, inputs):
    toks = _tokens(text)
    pos = [0]

    def peek():
        return toks[pos[0]] if pos[0] < len(toks) else None

    def take(expected=None):
        t = peek()
        if t is None or (expected and t != expected):
            raise ModelError(f"guard '{text}': expected {expected or 'operand'}")
        pos[0] += 1
        return t

    def factor():
        t = peek()
        if t == "!":
            take("!")
            return ("not", factor())
        if t == "(":
            take("(")
            e = expr()
            take(")")
            return e
        t = take()
        if t in ("true", "false"):
            return ("const", t == "true")
        if t not in inputs:
            raise ModelError(f"guard '{text}': '{t}' is not a declared input")
        return ("in", t)

    def term():
        e = factor()
        while peek() == "&&":
            take("&&")
            e = ("and", e, factor())
        return e

    def expr():
        e = term()
        while peek() == "||":
            take("||")
            e = ("or", e, term())
        return e

    tree = expr()
    if peek() is not None:
        raise ModelError(f"guard '{text}': trailing '{peek()}'")
    return tree


def eval_guard(tree, values):
    k = tree[0]
    if k == "const":
        return tree[1]
    if k == "in":
        return values[tree[1]]
    if k == "not":
        return not eval_guard(tree[1], values)
    if k == "and":
        return eval_guard(tree[1], values) and eval_guard(tree[2], values)
    return eval_guard(tree[1], values) or eval_guard(tree[2], values)


def c_guard(tree, var="in"):
    k = tree[0]
    if k == "const":
        return "true" if tree[1] else "false"
    if k == "in":
        sep = "" if var.endswith("->") else "."
        return f"{var}{sep}{tree[1]}"
    if k == "not":
        return f"(!{c_guard(tree[1], var)})"
    op = "&&" if k == "and" else "||"
    return f"({c_guard(tree[1], var)} {op} {c_guard(tree[2], var)})"


# ---- Model loading and validation ------------------------------------------------

def load(path):
    with open(path, "rb") as f:
        raw = f.read()
    m = yaml.safe_load(raw)
    m["_sha256"] = hashlib.sha256(raw).hexdigest()
    m["_file"] = os.path.basename(path)
    validate(m)
    return m


def _names(items, what):
    names = []
    for it in items:
        n = it.get("name")
        if not n or not IDENT.match(n):
            raise ModelError(f"{what}: invalid name {n!r}")
        if n in names:
            raise ModelError(f"{what}: duplicate name {n}")
        names.append(n)
    return names


def validate(m):
    for key in ("model", "semantics", "initial", "states", "events",
                "transitions"):
        if key not in m:
            raise ModelError(f"missing top level key '{key}'")
    if not IDENT.match(m["model"]):
        raise ModelError("model name must be a C identifier")

    for k, v in m["semantics"].items():
        if k not in SUPPORTED_SEMANTICS:
            raise ModelError(f"semantics: unknown key '{k}'")
        if v not in SUPPORTED_SEMANTICS[k]:
            raise ModelError(f"semantics: {k} = {v!r} is not supported "
                             f"by this generator")
    for k in SUPPORTED_SEMANTICS:
        if k not in m["semantics"]:
            raise ModelError(f"semantics: '{k}' must be stated explicitly")

    states = _names(m["states"], "state")
    events = _names(m["events"], "event")
    inputs = _names(m.get("inputs", []), "input")
    if len(events) > 32:
        raise ModelError("at most 32 events (one pending bit each)")
    for e in m["events"]:
        if e.get("class") not in ("one_shot", "latched"):
            raise ModelError(f"event {e['name']}: class must be one_shot "
                             f"or latched")
        if e.get("source") not in ("ground", "autonomous"):
            raise ModelError(f"event {e['name']}: source must be ground or "
                             f"autonomous")
    for i in m.get("inputs", []):
        if i.get("type") != "bool":
            raise ModelError(f"input {i['name']}: only bool inputs supported")

    for key in ("initial", "invalid_state_recovery"):
        if key in m and m[key] not in states:
            raise ModelError(f"{key}: unknown state {m[key]}")
    m.setdefault("boot_states", [m["initial"]])
    for s in m["boot_states"]:
        if s not in states:
            raise ModelError(f"boot_states: unknown state {s}")
    if m["initial"] not in m["boot_states"]:
        raise ModelError("initial state must be one of boot_states")

    actions = set()
    for s in m["states"]:
        s.setdefault("on_entry", [])
        s.setdefault("on_exit", [])
        s.setdefault("ignore", [])
        for a in s["on_entry"] + s["on_exit"]:
            if not IDENT.match(a):
                raise ModelError(f"state {s['name']}: bad action {a!r}")
            actions.add(a)
        for e in s["ignore"]:
            if e not in events:
                raise ModelError(f"state {s['name']}: ignores unknown event {e}")

    ids = set()
    for t in m["transitions"]:
        tid = t.get("id")
        if not tid or not IDENT.match(tid) or tid in ids:
            raise ModelError(f"transition id {tid!r} missing, invalid or "
                             f"duplicate")
        ids.add(tid)
        if t.get("from") not in states:
            raise ModelError(f"{tid}: unknown source state {t.get('from')}")
        if t.get("event") not in events:
            raise ModelError(f"{tid}: unknown event {t.get('event')}")
        t.setdefault("internal", False)
        t.setdefault("action", [])
        if t["internal"]:
            if "to" in t:
                raise ModelError(f"{tid}: an internal transition has no target")
            t["to"] = t["from"]
        elif t.get("to") not in states:
            raise ModelError(f"{tid}: unknown target state {t.get('to')}")
        elif t["to"] == t["from"]:
            raise ModelError(f"{tid}: self transitions are not supported; "
                             f"use internal: true")
        if not isinstance(t.get("priority"), int) or t["priority"] < 1:
            raise ModelError(f"{tid}: priority must be an integer >= 1")
        g = t.get("guard", "true")
        if isinstance(g, bool):
            g = "true" if g else "false"
        t["guard"] = str(g).strip()
        t["_guard"] = parse_guard(t["guard"], inputs)
        for a in t["action"]:
            if not IDENT.match(a):
                raise ModelError(f"{tid}: bad action {a!r}")
            actions.add(a)
        sdef = next(s for s in m["states"] if s["name"] == t["from"])
        if t["event"] in sdef["ignore"]:
            raise ModelError(f"{tid}: event {t['event']} is both handled and "
                             f"ignored in {t['from']}")

    for s in states:
        prios = [t["priority"] for t in m["transitions"] if t["from"] == s]
        if len(prios) != len(set(prios)):
            raise ModelError(f"state {s}: two transitions share a priority")

    m["_actions"] = sorted(actions)
    m["_completeness"] = completeness(m, states, events, inputs)
    m["_reachable"] = reachability(m, states)


def completeness(m, states, events, inputs):
    """Every (state, event) is handled or ignored; for a handled pair the
    guards cover every input combination. Returns rows for the report."""
    rows = []
    combos = [dict(zip(inputs, vals))
              for vals in itertools.product([False, True], repeat=len(inputs))]
    for s in m["states"]:
        for e in events:
            ts = sorted((t for t in m["transitions"]
                         if t["from"] == s["name"] and t["event"] == e),
                        key=lambda t: t["priority"])
            if e in s["ignore"]:
                rows.append((s["name"], e, "ignored", "-"))
                continue
            if not ts:
                raise ModelError(f"INCOMPLETE: event {e} is neither handled "
                                 f"nor ignored in state {s['name']}")
            for c in combos:
                if not any(eval_guard(t["_guard"], c) for t in ts):
                    raise ModelError(
                        f"INCOMPLETE: in {s['name']}, event {e} with inputs "
                        f"{c} enables no transition")
            rows.append((s["name"], e, "handled",
                         ", ".join(f"{t['id']} [{t.get('guard', 'true')}]"
                                   for t in ts)))
    return rows


def reachability(m, states):
    seen = set(m["boot_states"])
    frontier = list(seen)
    while frontier:
        s = frontier.pop()
        for t in m["transitions"]:
            if t["from"] == s and t["to"] not in seen:
                seen.add(t["to"])
                frontier.append(t["to"])
    unreachable = [s for s in states if s not in seen]
    if unreachable:
        raise ModelError(f"unreachable states: {unreachable}")
    return sorted(seen)


# ---- Emission --------------------------------------------------------------------

class Writer:
    """Collects lines and remembers which lines each model element produced."""

    def __init__(self):
        self.lines = []
        self.trace = {}
        self._open = {}

    def __call__(self, text=""):
        for line in text.split("\n"):
            self.lines.append(line)

    def begin(self, key):
        self._open[key] = len(self.lines) + 1

    def end(self, key, symbol):
        self.trace.setdefault(key, []).append(
            {"symbol": symbol, "lines": [self._open.pop(key), len(self.lines)]})

    def text(self):
        return "\n".join(self.lines) + "\n"


def banner(m, gen_hash, what):
    return f"""/*
 * GENERATED FILE - DO NOT EDIT.
 *
 * {what}
 * Model     : {m['_file']} ("{m['model']}", version {m.get('version', '-')})
 * Model hash: sha256:{m['_sha256']}
 * Generator : modelgen.py {GENERATOR_VERSION}, sha256:{gen_hash}
 *
 * Change the model or the generator and regenerate. `make check-generated`
 * fails if this file differs from a fresh generation.
 */"""


def emit(m, prefix, gen_hash):
    P = prefix.upper()
    states = [s["name"] for s in m["states"]]
    events = [e["name"] for e in m["events"]]
    inputs = [i["name"] for i in m.get("inputs", [])]
    ev = {e["name"]: e for e in m["events"]}
    st = {s["name"]: s for s in m["states"]}

    def S(n):
        return f"{P}_{n}"

    def E(n):
        return f"{P}_EV_{n.upper()}"

    # ---- header ----------------------------------------------------------
    h = Writer()
    h(banner(m, gen_hash, "Public interface of the state machine."))
    h(f"#ifndef {P}_H\n#define {P}_H\n")
    h("#include <stdbool.h>\n#include <stdint.h>\n")
    h(f'#define {P}_MODEL_NAME    "{m["model"]}"')
    h(f'#define {P}_MODEL_VERSION {int(m.get("version", 0))}')
    h(f'#define {P}_MODEL_SHA256  "{m["_sha256"]}"\n')
    h("typedef enum {")
    for i, s in enumerate(states):
        h.begin(f"state:{s}")
        h(f"    {S(s)} = {i},")
        h.end(f"state:{s}", S(s))
    h(f"    {P}_STATE_COUNT = {len(states)}\n}} {prefix}_state_t;\n")
    h("typedef enum {")
    for i, e in enumerate(events):
        h.begin(f"event:{e}")
        h(f"    {E(e)} = {i},   /* {ev[e]['source']}, {ev[e]['class']} */")
        h.end(f"event:{e}", E(e))
    h(f"    {P}_EVENT_COUNT = {len(events)}\n}} {prefix}_event_t;\n")
    h("/* Inputs, sampled once at the start of every step. */")
    h("typedef struct {")
    for i in inputs:
        h(f"    bool {i};")
    if not inputs:
        h("    uint8_t unused;")
    h(f"}} {prefix}_inputs_t;\n")
    h("/* Cause carried by an event; the last post of an event wins. */")
    h("typedef struct {\n    uint8_t  src;\n    uint16_t detail;\n"
      f"}} {prefix}_payload_t;\n")
    h("/* The machine. Allocated by the caller; no dynamic memory. */")
    h("typedef struct {")
    h(f"    {prefix}_state_t  state;")
    h("    uint32_t pending;              /* one bit per event */")
    h(f"    {prefix}_payload_t payload[{P}_EVENT_COUNT];")
    h("    void *user;                    /* passed to every hook */")
    h(f"}} {prefix}_t;\n")
    h("typedef enum {")
    h(f"    {P}_STEP_IDLE = 0,       /* nothing enabled              */")
    h(f"    {P}_STEP_TRANSITION = 1, /* external transition taken     */")
    h(f"    {P}_STEP_INTERNAL = 2,   /* internal transition taken     */")
    h(f"    {P}_STEP_RECOVERED = 3   /* invalid state, recovered      */")
    h(f"}} {prefix}_step_kind_t;\n")
    h("typedef struct {")
    h(f"    {prefix}_step_kind_t kind;")
    h("    uint8_t transition;            /* 1-based transition number, 0 = none */")
    h(f"    {prefix}_state_t from;")
    h(f"    {prefix}_state_t to;")
    h(f"    {prefix}_event_t event;")
    h(f"}} {prefix}_result_t;\n")
    h("/* Enter a boot state (" + ", ".join(m["boot_states"]) + "): runs its entry")
    h(" * actions, no transition report. Returns 0, or -1 if `boot` is not a")
    h(" * boot state (the machine is then started in "
      f"{m.get('invalid_state_recovery', m['initial'])}). */")
    h(f"int {prefix}_init({prefix}_t *sm, {prefix}_state_t boot, void *user);\n")
    h("/* Mark an event pending for the next step. Returns 0, or -1 for an")
    h(" * event value outside the model (ignored: fail closed). */")
    h(f"int {prefix}_post({prefix}_t *sm, {prefix}_event_t event, "
      f"{prefix}_payload_t cause);\n")
    h("/* One evaluation cycle: at most one transition. */")
    h(f"{prefix}_result_t {prefix}_step({prefix}_t *sm, "
      f"const {prefix}_inputs_t *in);\n")
    h(f"{prefix}_state_t {prefix}_state(const {prefix}_t *sm);\n")
    h("/* ---- Hooks implemented by the application -------------------------- */\n")
    for a in m["_actions"]:
        h(f"void {prefix}_action_{a}(void *user, const {prefix}_payload_t *cause);")
    h(f"\nvoid {prefix}_on_transition(void *user, {prefix}_state_t from, "
      f"{prefix}_state_t to,\n        {prefix}_event_t event, "
      f"const {prefix}_payload_t *cause);")
    h(f"void {prefix}_on_discard(void *user, {prefix}_event_t event, "
      f"const {prefix}_payload_t *cause);")
    h(f"void {prefix}_on_invalid_state(void *user, uint32_t raw_state);\n")
    h(f"#ifdef {P}_WITH_NAMES\nconst char *{prefix}_state_name({prefix}_state_t s);")
    h(f"const char *{prefix}_event_name({prefix}_event_t e);\n#endif\n")
    h(f"#endif /* {P}_H */")

    # ---- source ------------------------------------------------------------
    c = Writer()
    c(banner(m, gen_hash, "State machine implementation."))
    c("#include <stdint.h>\n")
    c(f'#include "{prefix}.h"\n')
    c("#define BIT(e) ((uint32_t)1u << (uint32_t)(e))\n")

    def mask(names):
        return " | ".join(f"BIT({E(n)})" for n in names) or "0u"

    c.begin("semantics:unconsumed_one_shot")
    c("/* Event classes (semantics: unconsumed_one_shot, unconsumed_latched). */")
    c("static const uint32_t ONE_SHOT = " +
      mask([e for e in events if ev[e]["class"] == "one_shot"]) + ";")
    c("static const uint32_t LATCHED = " +
      mask([e for e in events if ev[e]["class"] == "latched"]) + ";")
    c.end("semantics:unconsumed_one_shot", "ONE_SHOT, LATCHED")
    c.begin("semantics:ignored_ground_command")
    c("/* Ground commands (semantics: ignored_ground_command). */")
    c("static const uint32_t GROUND = " +
      mask([e for e in events if ev[e]["source"] == "ground"]) + ";")
    c.end("semantics:ignored_ground_command", "GROUND")
    c("\n/* Events explicitly ignored, per state (completeness). */")
    c(f"static const uint32_t IGNORED[{P}_STATE_COUNT] = {{")
    for s in states:
        c.begin(f"ignore:{s}")
        c(f"    {mask(st[s]['ignore'])},   /* {s} */")
        c.end(f"ignore:{s}", f"IGNORED[{S(s)}]")
    c("};\n")

    # entry / exit functions
    for s in states:
        c.begin(f"entry:{s}")
        c(f"/* {s} on_entry */")
        c(f"static void enter_{s}({prefix}_t *sm, const {prefix}_payload_t *cause)")
        c("{")
        c(f"    sm->state = {S(s)};")
        for a in st[s]["on_entry"]:
            c(f"    {prefix}_action_{a}(sm->user, cause);")
        if not st[s]["on_entry"]:
            c("    (void)cause;")
        c("}\n")
        c.end(f"entry:{s}", f"enter_{s}()")
        if st[s]["on_exit"]:
            c.begin(f"exit:{s}")
            c(f"/* {s} on_exit */")
            c(f"static void exit_{s}({prefix}_t *sm, const {prefix}_payload_t *cause)")
            c("{")
            for a in st[s]["on_exit"]:
                c(f"    {prefix}_action_{a}(sm->user, cause);")
            c("}\n")
            c.end(f"exit:{s}", f"exit_{s}()")

    # transition functions
    for n, t in enumerate(m["transitions"], start=1):
        tid = t["id"]
        g = t.get("guard", "true")
        c.begin(f"transition:{tid}")
        arrow = "(internal)" if t["internal"] else f"-> {t['to']}"
        c(f"/* {tid}: {t['from']} {arrow} on {t['event']} [{g}], priority "
          f"{t['priority']}  {' '.join(t.get('req', []))} */")
        c(f"static void fire_{tid}({prefix}_t *sm, {prefix}_result_t *r)")
        c("{")
        c(f"    const {prefix}_payload_t *cause = &sm->payload[{E(t['event'])}];")
        if not t["internal"] and st[t["from"]]["on_exit"]:
            c(f"    exit_{t['from']}(sm, cause);")
        for a in t["action"]:
            c(f"    {prefix}_action_{a}(sm->user, cause);")
        if not t["internal"]:
            c(f"    enter_{t['to']}(sm, cause);")
            c(f"    {prefix}_on_transition(sm->user, {S(t['from'])}, "
              f"{S(t['to'])}, {E(t['event'])}, cause);")
            c(f"    r->kind = {P}_STEP_TRANSITION;")
        else:
            c(f"    r->kind = {P}_STEP_INTERNAL;")
        c(f"    r->transition = {n}u;")
        c(f"    r->from = {S(t['from'])};")
        c(f"    r->to = {S(t['to'])};")
        c(f"    r->event = {E(t['event'])};")
        c("}\n")
        c.end(f"transition:{tid}", f"fire_{tid}()")

    # one selection function per state: the state's transitions in
    # priority order, the first enabled one fires
    for s in states:
        ts = sorted((t for t in m["transitions"] if t["from"] == s),
                    key=lambda t: t["priority"])
        c.begin(f"select:{s}")
        c(f"/* {s}: outgoing transitions in priority order. */")
        c(f"static void select_{s}({prefix}_t *sm, uint32_t pending,")
        c(f"                        const {prefix}_inputs_t *in, {prefix}_result_t *r)")
        c("{")
        if not ts:
            c("    (void)sm; (void)pending; (void)in; (void)r;")
        elif all(t.get("guard", "true") == "true" for t in ts):
            c("    (void)in;")
        first = True
        for t in ts:
            cond = f"(pending & BIT({E(t['event'])})) != 0u"
            if t.get("guard", "true") != "true":
                cond += f" && {c_guard(t['_guard'], 'in->')}"
            kw = "if" if first else "} else if"
            c.begin(f"select:{t['id']}")
            c(f"    {kw} ({cond}) {{   /* {t['id']} */")
            c(f"        fire_{t['id']}(sm, r);")
            c.end(f"select:{t['id']}", f"select_{s}()")
            first = False
        if ts:
            c("    } else {\n        /* nothing enabled */\n    }")
        c("}\n")
        c.end(f"select:{s}", f"select_{s}()")

    rec = m.get("invalid_state_recovery", m["initial"])
    c(f"""int {prefix}_init({prefix}_t *sm, {prefix}_state_t boot, void *user)
{{
    static const {prefix}_payload_t none = {{ 0u, 0u }};
    uint32_t i;

    sm->pending = 0u;
    sm->user = user;
    for (i = 0u; i < (uint32_t){P}_EVENT_COUNT; i++) {{
        sm->payload[i] = none;
    }}""")
    c.begin("boot_states")
    c("    switch ((uint32_t)boot) {")
    for s in m["boot_states"]:
        c(f"    case (uint32_t){S(s)}:\n        enter_{s}(sm, &none);\n        return 0;")
    c(f"    default:\n        enter_{rec}(sm, &none);\n        return -1;\n    }}")
    c.end("boot_states", f"{prefix}_init()")
    c("}\n")

    c(f"""int {prefix}_post({prefix}_t *sm, {prefix}_event_t event, {prefix}_payload_t cause)
{{
    if ((uint32_t)event >= (uint32_t){P}_EVENT_COUNT) {{
        return -1;
    }}
    sm->payload[event] = cause;
    sm->pending |= BIT(event);
    return 0;
}}

{prefix}_state_t {prefix}_state(const {prefix}_t *sm)
{{
    return sm->state;
}}
""")

    # step
    c.begin("step")
    c(f"{prefix}_result_t {prefix}_step({prefix}_t *sm, const {prefix}_inputs_t *in_now)")
    c("{")
    c(f"    static const {prefix}_payload_t recovery = {{ 0u, 0u }};")
    c(f"    {prefix}_result_t r = {{ {P}_STEP_IDLE, 0u, {S(m['initial'])}, "
      f"{S(m['initial'])}, {E(events[0])} }};")
    c("    /* semantics: input_sampling = start_of_cycle */")
    c("    const uint32_t pending = sm->pending;")
    c(f"    const {prefix}_inputs_t in = *in_now;")
    c("    const uint32_t start = (uint32_t)sm->state;")
    c("    uint32_t consumed = 0u;")
    c("    uint32_t lost;")
    c("    uint32_t e;\n")
    c("    r.from = sm->state;")
    c("    r.to = sm->state;\n")
    c("    /* semantics: one transition per cycle, lowest priority number wins */")
    c("    switch (start) {")
    for s in states:
        c(f"    case (uint32_t){S(s)}:")
        c(f"        select_{s}(sm, pending, &in, &r);")
        c("        break;")
    c.begin("invalid_state_recovery")
    c(f"""    default:
        /* State variable outside the model (memory corruption): recover
         * to {rec}. Latched events are kept, one shot events dropped. */
        {prefix}_on_invalid_state(sm->user, start);
        sm->pending &= LATCHED;
        enter_{rec}(sm, &recovery);
        r.kind = {P}_STEP_RECOVERED;
        r.to = {S(rec)};
        return r;
    }}""")
    c.end("invalid_state_recovery", f"{prefix}_step() default")
    c("""
    if (r.kind != """ + P + """_STEP_IDLE) {
        consumed = BIT(r.event);
    }""")
    c.begin("semantics:consumption")
    c(f"""    /* One shot events are consumed every cycle. Those neither taken
     * nor ignored in the start state were lost to a higher priority
     * transition: reported (semantics: unconsumed_one_shot). */
    lost = pending & ONE_SHOT & ~consumed & ~IGNORED[start];
    /* semantics: ignored_ground_command = report_if_state_changed. A
     * ground command for the start mode is already satisfied, unless an
     * external transition left that mode in this cycle (D-05). */
    if (r.kind == {P}_STEP_TRANSITION) {{
        lost |= pending & GROUND & ~consumed;
    }}
    consumed |= pending & ONE_SHOT;
    /* Latched events ignored in the start state are dropped only if
     * nothing fired; otherwise they are re-evaluated in the new state. */
    if (r.kind == {P}_STEP_IDLE) {{
        consumed |= pending & LATCHED & IGNORED[start];
    }}
    sm->pending &= ~consumed;

    for (e = 0u; e < (uint32_t){P}_EVENT_COUNT; e++) {{
        if ((lost & BIT(e)) != 0u) {{
            {prefix}_on_discard(sm->user, ({prefix}_event_t)e, &sm->payload[e]);
        }}
    }}
    return r;
}}""")
    c.end("semantics:consumption", f"{prefix}_step() consumption")
    c.end("step", f"{prefix}_step()")

    c(f"\n#ifdef {P}_WITH_NAMES")
    c(f"const char *{prefix}_state_name({prefix}_state_t s)\n{{\n    switch ((uint32_t)s) {{")
    for s in states:
        c(f'    case (uint32_t){S(s)}: return "{s}";')
    c('    default: return "?";\n    }\n}\n')
    c(f"const char *{prefix}_event_name({prefix}_event_t e)\n{{\n    switch ((uint32_t)e) {{")
    for e in events:
        c(f'    case (uint32_t){E(e)}: return "{e}";')
    c('    default: return "?";\n    }\n}')
    c("#endif")

    # ---- trace --------------------------------------------------------------
    reqs = {}

    def add(req_list, element):
        for r in req_list or []:
            reqs.setdefault(r, []).append(element)

    for s in m["states"]:
        add(s.get("req"), f"state:{s['name']}")
    for e in m["events"]:
        add(e.get("req"), f"event:{e['name']}")
    for t in m["transitions"]:
        add(t.get("req"), f"transition:{t['id']}")

    trace = {
        "model": m["_file"], "model_sha256": m["_sha256"],
        "generator_sha256": gen_hash,
        "header": {k: v for k, v in h.trace.items()},
        "source": {k: v for k, v in c.trace.items()},
        "requirements": reqs,
        "transitions": [{"number": n, "id": t["id"], "from": t["from"],
                         "to": t["to"], "event": t["event"],
                         "guard": t.get("guard", "true"),
                         "internal": t["internal"],
                         "priority": t["priority"],
                         "req": t.get("req", [])}
                        for n, t in enumerate(m["transitions"], start=1)],
    }
    # Configuration: unsigned literal suffix in upper case (clang-tidy
    # readability-uppercase-literal-suffix; MISRA C:2012 rule 7.3 style).
    upper = re.compile(r"\b(\d+)u\b")
    return (upper.sub(r"\1U", h.text()), upper.sub(r"\1U", c.text()),
            json.dumps(trace, indent=2) + "\n")


def report(m):
    lines = [f"# Completeness check: {m['_file']}", "",
             f"Generated by `modelgen.py --report` from model sha256 "
             f"`{m['_sha256'][:16]}...`. The generator refuses to generate "
             f"from a model where any row below would be missing.", "",
             "| State | Event | Handling | Transitions [guard] |",
             "|---|---|---|---|"]
    for row in m["_completeness"]:
        lines.append("| " + " | ".join(row) + " |")
    n_h = sum(1 for r in m["_completeness"] if r[2] == "handled")
    n_i = len(m["_completeness"]) - n_h
    lines += ["", f"{len(m['_completeness'])} state/event pairs: {n_h} handled "
              f"(guards cover every input combination), {n_i} explicitly "
              f"ignored, 0 unhandled. Reachable states from "
              f"{', '.join(m['boot_states'])}: {', '.join(m['_reachable'])}.",
              ""]
    return "\n".join(lines)


def main():
    p = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    p.add_argument("model")
    p.add_argument("--out")
    p.add_argument("--prefix", default="mode_sm")
    p.add_argument("--check", action="store_true")
    p.add_argument("--report")
    a = p.parse_args()

    with open(os.path.abspath(__file__), "rb") as f:
        gen_hash = hashlib.sha256(f.read()).hexdigest()
    try:
        m = load(a.model)
    except (ModelError, yaml.YAMLError, KeyError, TypeError) as exc:
        print(f"modelgen: {a.model}: {exc}", file=sys.stderr)
        return 2

    if a.report:
        with open(a.report, "w") as f:
            f.write(report(m))
        print(f"modelgen: completeness report written to {a.report}")
    if not a.out:
        return 0

    files = dict(zip((f"{a.prefix}.h", f"{a.prefix}.c", f"{a.prefix}_trace.json"),
                     emit(m, a.prefix, gen_hash)))
    if a.check:
        bad = []
        for name, text in files.items():
            path = os.path.join(a.out, name)
            if not os.path.exists(path) or open(path).read() != text:
                bad.append(name)
        if bad:
            print("modelgen: CHECK FAILED, differs from a fresh generation "
                  "(hand edited, or model/generator changed without "
                  "regenerating): " + ", ".join(bad), file=sys.stderr)
            return 1
        print(f"modelgen: {', '.join(files)} match the model "
              f"(sha256 {m['_sha256'][:16]}...)")
        return 0
    os.makedirs(a.out, exist_ok=True)
    for name, text in files.items():
        with open(os.path.join(a.out, name), "w") as f:
            f.write(text)
    print(f"modelgen: generated {', '.join(files)} in {a.out}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
