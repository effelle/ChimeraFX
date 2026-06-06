import esphome.codegen as cg

CODEOWNERS = ["@effelle"]
DEPENDENCIES = ["light"]

cfx_dimmer_ns = cg.esphome_ns.namespace("cfx_dimmer")
CFXDimmer = cfx_dimmer_ns.class_("CFXDimmer", cg.Component)
