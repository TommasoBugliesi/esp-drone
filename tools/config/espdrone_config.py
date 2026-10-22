#!/usr/bin/env python3
"""Bridge between the ESPDrone Kconfig and a configurator UI.

  espdrone_config.py schema [-b BUILD_DIR] [-o schema.json]
      Write the settings of "ESPDrone Config" as a tree of groups and fields
      (type, title, help, range, choices, current value, visibility) for a UI.

  espdrone_config.py apply VALUES.json [-b BUILD_DIR] [-o profile.defaults]
      Validate a {"SYMBOL": value} file against the schema and write an
      sdkconfig fragment. Use it as an extra SDKCONFIG_DEFAULTS layer.

Both need a configured project, run `idf.py reconfigure` first. They read the
files ESP-IDF writes to <build>/config: kconfig_menus.json (structure) and
sdkconfig.json (current values). No ESP-IDF import is needed.

Field types: bool, int, hex, string, choice. A choice carries "choices", a list
of {"name", "title"}; set it in VALUES.json as {"CHOICE_NAME": "MEMBER_NAME"}
(for example {"BOARD": "TARGET_FLYINGFREE_V01"}) or set the member to true.
"depends_on" is the raw Kconfig expression, "visible" is its value for the
current configuration.
"""
import argparse
import json
import os
import re
import sys

ROOT_TITLE = 'ESPDrone Config'


def load(build_dir):
    cfg = os.path.join(build_dir, 'config')
    try:
        with open(os.path.join(cfg, 'kconfig_menus.json')) as f:
            menus = json.load(f)
        with open(os.path.join(cfg, 'sdkconfig.json')) as f:
            values = json.load(f)
    except FileNotFoundError as e:
        sys.exit('%s not found, run `idf.py reconfigure` first (or pass -b)' % e.filename)
    return menus, values


def find_root(nodes):
    for node in nodes:
        if node.get('title') == ROOT_TITLE and node.get('type') == 'menu':
            return node
        found = find_root(node.get('children', []))
        if found:
            return found
    return None


# ---------------------------------------------------------------- expressions

_TOKEN = re.compile(r'\s*(&&|\|\||!=|=|!|\(|\)|"[^"]*"|[A-Za-z0-9_\-]+)')


def evaluate(expr, values):
    """Evaluate a Kconfig dependency expression. Unknown syntax counts as true."""
    if not expr:
        return True
    tokens = _TOKEN.findall(expr)
    pos = 0

    def peek():
        return tokens[pos] if pos < len(tokens) else None

    def take():
        nonlocal pos
        pos += 1
        return tokens[pos - 1]

    def atom():
        tok = take()
        if tok == '(':
            val = or_()
            take()
            return val
        if tok == '!':
            return not atom()
        return tok.strip('"')

    def truth(v):
        if isinstance(v, str):
            v = values.get(v, v)
        return bool(v) and v not in ('n', 'N', '0')

    def cmp_():
        left = atom()
        if peek() in ('=', '!='):
            op = take()
            right = atom()
            lv = values.get(left, left)
            rv = values.get(right, right)
            eq = str(lv) == str(rv)
            return eq if op == '=' else not eq
        return truth(left)

    def and_():
        val = cmp_()
        while peek() == '&&':
            take()
            val = cmp_() and val
        return val

    def or_():
        val = and_()
        while peek() == '||':
            take()
            val = and_() or val
        return val

    try:
        return bool(or_())
    except (IndexError, ValueError):
        return True


# ---------------------------------------------------------------------- schema

def convert(node, values):
    """Return a schema entry for a menu node, or None for nodes the UI skips."""
    kind = node['type']
    depends = node.get('depends_on')
    if depends == '<choice>':      # member of a choice, shown through its choice
        depends = None
    entry = {'id': node['id'], 'title': node.get('title', ''), 'type': kind,
             'depends_on': depends, 'visible': evaluate(depends, values)}
    if node.get('help'):
        entry['help'] = ' '.join(node['help'].split())

    if kind == 'menu':
        entry['items'] = [e for e in (convert(c, values) for c in node.get('children', [])) if e]
        return entry
    if kind == 'comment':
        return entry
    if kind == 'choice':
        entry['name'] = node['name']
        entry['choices'] = [{'name': c['name'], 'title': c.get('title', c['name']),
                             'depends_on': c.get('depends_on'),
                             'visible': evaluate(c.get('depends_on'), values)}
                            for c in node.get('children', []) if c.get('name')]
        entry['value'] = next((c['name'] for c in entry['choices'] if values.get(c['name'])), None)
        return entry
    if kind in ('bool', 'int', 'hex', 'string'):
        entry['name'] = node['name']
        entry['value'] = values.get(node['name'])
        if node.get('range'):
            entry['range'] = node['range']
        # Kconfig nests every symbol that depends on this one beneath it
        dependents = [e for e in (convert(c, values) for c in node.get('children', [])) if e]
        if dependents:
            entry['items'] = dependents
        return entry
    return None


def build_schema(build_dir):
    menus, values = load(build_dir)
    root = find_root(menus)
    if root is None:
        sys.exit('"%s" not found in kconfig_menus.json' % ROOT_TITLE)
    schema = convert(root, values)
    schema['version'] = 1
    schema['target'] = values.get('IDF_TARGET')
    return schema


def walk(entry):
    yield entry
    for sub in entry.get('items', []):
        yield from walk(sub)


# ----------------------------------------------------------------------- apply

def fragment(schema, requested):
    fields = {e['name']: e for e in walk(schema) if e.get('name')}
    members = {}
    for e in fields.values():
        for c in e.get('choices', []):
            members[c['name']] = e
    lines = []
    errors = []

    for name, value in requested.items():
        if name in fields and fields[name]['type'] == 'choice':
            if value not in [c['name'] for c in fields[name]['choices']]:
                errors.append('%s: %r is not a choice of %s' % (name, value, name))
            else:
                lines.append('CONFIG_%s=y' % value)
            continue
        if name in members:
            if value is True:
                lines.append('CONFIG_%s=y' % name)
            else:
                errors.append('%s: is a choice member, set it to true or set %s' % (name, members[name]['name']))
            continue
        if name not in fields:
            errors.append('%s: unknown setting' % name)
            continue

        field = fields[name]
        kind = field['type']
        if kind == 'bool':
            if not isinstance(value, bool):
                errors.append('%s: expected true or false' % name)
            else:
                lines.append('CONFIG_%s=y' % name if value else '# CONFIG_%s is not set' % name)
        elif kind in ('int', 'hex'):
            if isinstance(value, bool) or not isinstance(value, int):
                errors.append('%s: expected an integer' % name)
                continue
            low, high = field.get('range', (None, None))
            if low is not None and not (low <= value <= high):
                errors.append('%s: %d outside %d..%d' % (name, value, low, high))
            else:
                lines.append('CONFIG_%s=%s' % (name, ('0x%x' % value) if kind == 'hex' else value))
        elif kind == 'string':
            if not isinstance(value, str):
                errors.append('%s: expected a string' % name)
            else:
                lines.append('CONFIG_%s="%s"' % (name, value.replace('\\', '\\\\').replace('"', '\\"')))
    if errors:
        sys.exit('\n'.join(errors))
    return '\n'.join(lines) + '\n'


def main():
    parser = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    sub = parser.add_subparsers(dest='cmd', required=True)
    for name in ('schema', 'apply'):
        p = sub.add_parser(name)
        p.add_argument('-b', '--build-dir', default='build')
        p.add_argument('-o', '--output')
        if name == 'apply':
            p.add_argument('values')
    args = parser.parse_args()

    schema = build_schema(args.build_dir)
    if args.cmd == 'schema':
        text = json.dumps(schema, indent=2) + '\n'
    else:
        with open(args.values) as f:
            text = fragment(schema, json.load(f))

    if args.output:
        with open(args.output, 'w') as f:
            f.write(text)
    else:
        sys.stdout.write(text)


if __name__ == '__main__':
    main()
