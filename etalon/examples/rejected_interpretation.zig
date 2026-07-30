const etalon = @import("etalon");

test "a scalar result cannot acquire vector meaning" {
    const specs = etalon.specification.specs;
    const Quantity = etalon.quantity.Quantity;
    const units = etalon.quantity.units;
    const domain: etalon.field.DomainSpec = .{
        .coordinate = specs.spatial_coordinate,
    };
    const ElevationField = etalon.field.Field(
        domain,
        specs.surface_elevation,
        4,
        f64,
    );
    const elevations: ElevationField = .{
        .values = .{ 0.0, 1.0, 0.0, -1.0 },
    };
    const mechanical = etalon.field.laplacianPeriodic(
        elevations,
        Quantity(specs.spatial_coordinate, f64).from(
            2.0,
            units.metre,
        ),
    );

    _ = etalon.field.interpret(specs.water_velocity, mechanical);
}
