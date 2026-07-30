const std = @import("std");
const etalon = @import("etalon");

test "domain spacing has coordinate meaning, not merely length dimension" {
    const specs = etalon.specification.specs;
    const units = etalon.quantity.units;
    const Elevation = etalon.quantity.QuantityPoint(
        specs.surface_elevation,
        specs.vertical_displacement,
        f64,
    );
    const WaterDepth = etalon.quantity.Quantity(
        specs.standing_water_depth,
        f64,
    );
    const Domain = etalon.bundle.PeriodicLineDomain(
        specs.spatial_coordinate,
    );
    const elevations = [_]Elevation{
        Elevation.from(0.0, units.metre),
        Elevation.from(1.0, units.metre),
    };
    const elevation_slice: []const Elevation = &elevations;

    const result = try etalon.field.laplacianPeriodicColumnAlloc(
        std.testing.allocator,
        Domain.init(elevations.len),
        elevation_slice,
        WaterDepth.from(2.0, units.metre),
    );
    defer std.testing.allocator.free(result);
}
