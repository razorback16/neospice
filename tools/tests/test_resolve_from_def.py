from ngspice_migrate.gen_def import extract_matrix_offset_fields


def test_extract_matrix_offset_fields_basic():
    def_text = """
struct DIOInstance {
    neospice::MatrixOffset DIOposPosPtr{-1};
    neospice::MatrixOffset DIOnegNegPtr{-1};
    neospice::MatrixOffset DIOposPrimePosPrimePtr{-1};
    double DIOarea;
    int DIOstate;
    neospice::MatrixOffset DIOposPosPrimePtr{-1};
};
"""
    fields = extract_matrix_offset_fields(def_text)
    assert fields == [
        "DIOposPosPtr",
        "DIOnegNegPtr",
        "DIOposPrimePosPrimePtr",
        "DIOposPosPrimePtr",
    ]


def test_extract_no_matrix_offsets():
    fields = extract_matrix_offset_fields("struct Foo { double x; };")
    assert fields == []


def test_extract_deduplicates():
    def_text = """
    neospice::MatrixOffset A{-1};
    neospice::MatrixOffset B{-1};
    neospice::MatrixOffset A{-1};
    """
    fields = extract_matrix_offset_fields(def_text)
    assert fields == ["A", "B"]
