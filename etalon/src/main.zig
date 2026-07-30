const std = @import("std");
const etalon = @import("etalon");

pub fn main(init: std.process.Init) !void {
    const specs = etalon.specification.specs;
    const Quantity = etalon.quantity.Quantity;
    const QuantityPoint = etalon.quantity.QuantityPoint;
    const units = etalon.quantity.units;
    const allocator = init.arena.allocator();

    const Elevation = QuantityPoint(
        specs.surface_elevation,
        specs.vertical_displacement,
        f64,
    );
    const summit = Elevation.from(114.5, units.metre);
    const waterline = Elevation.from(103.0, units.metre);
    const clearance = summit.difference(waterline);

    const WaterDepth = Quantity(specs.standing_water_depth, f64);
    const Curvature = Quantity(specs.terrain_curvature, f64);
    const SurfaceRow = struct {
        elevation: Elevation,
        water_depth: WaterDepth,
        curvature: Curvature,
    };
    const Domain = etalon.bundle.PeriodicLineDomain(
        specs.spatial_coordinate,
    );
    const Surface = etalon.bundle.Bundle(Domain, SurfaceRow);
    const zero_depth = WaterDepth.from(0.0, units.metre);
    const zero_curvature: Curvature = .{ .coherent_value = 0.0 };
    const initial = [_]SurfaceRow{
        .{
            .elevation = Elevation.from(0.0, units.metre),
            .water_depth = zero_depth,
            .curvature = zero_curvature,
        },
        .{
            .elevation = Elevation.from(1.0, units.metre),
            .water_depth = zero_depth,
            .curvature = zero_curvature,
        },
        .{
            .elevation = Elevation.from(0.0, units.metre),
            .water_depth = WaterDepth.from(0.25, units.metre),
            .curvature = zero_curvature,
        },
        .{
            .elevation = Elevation.from(-1.0, units.metre),
            .water_depth = WaterDepth.from(0.5, units.metre),
            .curvature = zero_curvature,
        },
    };
    var surface = try Surface.init(
        allocator,
        Domain.init(initial.len),
        &initial,
    );
    defer surface.deinit(allocator);

    const Coordinate = Quantity(specs.spatial_coordinate, f64);
    const mechanical = try etalon.field.laplacianPeriodicColumnAlloc(
        allocator,
        surface.domain,
        surface.columnConst(.elevation),
        Coordinate.from(2.0, units.metre),
    );
    defer allocator.free(mechanical);
    for (surface.column(.curvature), mechanical) |*target, derived| {
        target.* = etalon.field.interpretValue(
            specs.terrain_curvature,
            derived,
        );
    }

    const mechanical_spec = @TypeOf(mechanical[0]).quantity_spec;
    const curvature = surface.columnConst(.curvature);

    std.debug.print(
        \\Étalon
        \\  summit - waterline = {d:.1} m of vertical displacement
        \\  Bundle rows = {d}; columns = elevation, water_depth, curvature
        \\  laplacian(surface elevation) dimension = L^{d} T^{d} M^{d}
        \\  derived meaning id = 0x{x}
        \\  interpreted meaning = terrain_curvature
        \\  curvature samples = {any}
        \\
    , .{
        clearance.coherent_value,
        surface.count(),
        mechanical_spec.dimension.length,
        mechanical_spec.dimension.duration,
        mechanical_spec.dimension.mass,
        @backingInt(mechanical_spec.meaning),
        [_]f64{
            curvature[0].coherent_value,
            curvature[1].coherent_value,
            curvature[2].coherent_value,
            curvature[3].coherent_value,
        },
    });
}
