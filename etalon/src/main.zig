const std = @import("std");
const etalon = @import("etalon");

pub fn main() void {
    const specs = etalon.specification.specs;
    const Quantity = etalon.quantity.Quantity;
    const QuantityPoint = etalon.quantity.QuantityPoint;
    const units = etalon.quantity.units;

    const Elevation = QuantityPoint(
        specs.surface_elevation,
        specs.vertical_displacement,
        f64,
    );
    const summit = Elevation.from(114.5, units.metre);
    const waterline = Elevation.from(103.0, units.metre);
    const clearance = summit.difference(waterline);

    const domain: etalon.field.DomainSpec = .{
        .coordinate = specs.spatial_coordinate,
    };
    const ElevationField = etalon.field.Field(
        domain,
        specs.surface_elevation,
        4,
        f64,
    );
    const Coordinate = Quantity(specs.spatial_coordinate, f64);
    const elevations: ElevationField = .{
        .values = .{ 0.0, 1.0, 0.0, -1.0 },
    };
    const mechanical = etalon.field.laplacianPeriodic(
        elevations,
        Coordinate.from(2.0, units.metre),
    );
    const curvature = etalon.field.interpret(
        specs.terrain_curvature,
        mechanical,
    );

    std.debug.print(
        \\Étalon
        \\  summit - waterline = {d:.1} m of vertical displacement
        \\  laplacian(surface elevation) dimension = L^{d} T^{d} M^{d}
        \\  derived meaning id = 0x{x}
        \\  interpreted meaning = terrain_curvature
        \\  curvature samples = {any}
        \\
    , .{
        clearance.coherent_value,
        @TypeOf(mechanical).quantity_spec.dimension.length,
        @TypeOf(mechanical).quantity_spec.dimension.duration,
        @TypeOf(mechanical).quantity_spec.dimension.mass,
        @backingInt(@TypeOf(mechanical).quantity_spec.meaning),
        curvature.values,
    });
}
