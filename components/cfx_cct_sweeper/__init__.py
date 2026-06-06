import esphome.codegen as cg

CODEOWNERS = ["@effelle"]
DEPENDENCIES = ["light"]

cfx_cct_sweeper_ns = cg.esphome_ns.namespace("cfx_cct_sweeper")
CFXCCTSweeper = cfx_cct_sweeper_ns.class_("CFXCCTSweeper", cg.Component)
