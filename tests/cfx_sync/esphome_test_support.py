"""Compatibility helpers for lightweight ESPHome test stubs."""

import inspect


def ensure_register_action_supports_synchronous(automation_module):
    register_action = automation_module.register_action
    parameters = inspect.signature(register_action).parameters.values()
    if any(
        parameter.name == "synchronous"
        or parameter.kind is inspect.Parameter.VAR_KEYWORD
        for parameter in parameters
    ):
        return

    def register_action_with_synchronous(
        name, action_type, schema, *, synchronous=None
    ):
        return register_action(name, action_type, schema)

    automation_module.register_action = register_action_with_synchronous
