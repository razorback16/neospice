"""Unit tests for sensitivity analysis code stripping."""

from ngspice_migrate.transformer import Transformer, TransformerConfig


def _dio_config() -> TransformerConfig:
    """A diode-model config used by sensitivity strip tests."""
    return TransformerConfig(
        instance_struct="DIOinstance",
        model_struct="DIOmodel",
        instance_tag="sDIOinstance",
        model_tag="sDIOmodel",
        cpp_instance="DIOInstance",
        cpp_model="DIOModel",
        gen_instance="GENinstance",
        gen_model="GENmodel",
        prefix="DIO",
        namespace="dio",
        defines=[],
    )


def test_strip_sencond_block():
    """SenCond variable and if-block are removed."""
    cfg = _dio_config()
    source = '''#include "diodefs.h"
int DIOload(void *inModel, void *inCkt)
{
    int SenCond = 0;
    double x = 1.0;

    if (SenCond) {
        x = x * 2.0;
        goto next1;
    }

    x = x + 1.0;

next1:
    return 0;
}
'''
    t = Transformer(cfg)
    result = t.translate(source)
    assert "SenCond" not in result
    assert "next1" not in result
    assert "x = x + 1.0" in result


def test_strip_cktseninfo_block():
    """CKTsenInfo guard blocks are removed."""
    cfg = _dio_config()
    source = '''#include "diodefs.h"
int DIOload(void *inModel, void *inCkt)
{
    double x = 1.0;
    if ((info = ckt->CKTsenInfo) && ckt->CKTsenInfo->SENmode == TRANSEN) {
        x = x * 3.0;
    }
    x = x + 1.0;
    return 0;
}
'''
    t = Transformer(cfg)
    result = t.translate(source)
    assert "CKTsenInfo" not in result
    assert "SENmode" not in result
    assert "TRANSEN" not in result
    assert "x = x + 1.0" in result
