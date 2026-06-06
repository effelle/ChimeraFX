import esphome.codegen as cg

CODEOWNERS = ["@effelle"]
DEPENDENCIES = ["light"]

cfx_hue_cycler_ns = cg.esphome_ns.namespace("cfx_hue_cycler")
CFXHueCycler = cfx_hue_cycler_ns.class_("CFXHueCycler", cg.Component)
