const etalon = @import("etalon");

test "Bundle schemas contain quantities rather than bare representations" {
    const specs = etalon.specification.specs;
    const Elevation = etalon.quantity.QuantityPoint(
        specs.surface_elevation,
        specs.vertical_displacement,
        f64,
    );
    const BadRow = struct {
        elevation: Elevation,
        raw_depth: f64,
    };
    const Domain = etalon.bundle.PeriodicLineDomain(
        specs.spatial_coordinate,
    );

    _ = etalon.bundle.Bundle(Domain, BadRow);
}
