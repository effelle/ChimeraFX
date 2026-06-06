import esphome.codegen as cg

CODEOWNERS = ["@effelle"]
DEPENDENCIES = ["light"]

cfx_effect_selector_ns = cg.esphome_ns.namespace("cfx_effect_selector")
CFXEffectSelector = cfx_effect_selector_ns.class_(
    "CFXEffectSelector", cg.Component
)
