# More checks for Studio's AI tools

`tools/test_ai_tools.py` runs every `.py` file in this folder after its own
checks, in name order. Put the checks for a new feature in a new file here
(not at the end of `test_ai_tools.py`), so pull requests don't conflict:

```python
TITLE = "Script history records what scripts change"

def run(m, t):
    d = m.call("script_history", action="clear")
    t.check(t.ok(d) and d.get("total") == 0, "clear", d)
```

`m.call(tool, **args)` calls an AI tool; `t.check(condition, what, detail)`,
`t.ok(d)`, `t.code(d)` and `t.choices(d)` are the same helpers the main file uses.
Studio is the same one for every file, so use your own object names.
