const std = @import("std");
const specification = @import("specification");

pub const Dimension = specification.Dimension;
pub const QuantitySpec = specification.QuantitySpec;

pub const Unit = struct {
    dimension: Dimension,
    scale_to_coherent: f64,
};

pub const units = struct {
    pub const metre: Unit = .{
        .dimension = .{ .length = 1 },
        .scale_to_coherent = 1.0,
    };
    pub const kilometre: Unit = .{
        .dimension = .{ .length = 1 },
        .scale_to_coherent = 1000.0,
    };
    pub const second: Unit = .{
        .dimension = .{ .duration = 1 },
        .scale_to_coherent = 1.0,
    };
    pub const metre_per_second: Unit = .{
        .dimension = .{ .length = 1, .duration = -1 },
        .scale_to_coherent = 1.0,
    };
    pub const kilometre_per_hour: Unit = .{
        .dimension = .{ .length = 1, .duration = -1 },
        .scale_to_coherent = 1000.0 / 3600.0,
    };
};

pub fn Quantity(
    comptime spec: QuantitySpec,
    comptime Representation: type,
) type {
    return struct {
        const Self = @This();

        pub const quantity_spec = spec;
        pub const representation = Representation;

        coherent_value: Representation,

        pub fn from(
            value: Representation,
            comptime unit: Unit,
        ) Self {
            if (comptime !unit.dimension.eql(spec.dimension)) {
                @compileError("unit and quantity specification disagree");
            }
            return .{
                .coherent_value = value *
                    @as(Representation, unit.scale_to_coherent),
            };
        }

        pub fn inUnit(self: Self, comptime unit: Unit) Representation {
            if (comptime !unit.dimension.eql(spec.dimension)) {
                @compileError("unit and quantity specification disagree");
            }
            return self.coherent_value /
                @as(Representation, unit.scale_to_coherent);
        }

        pub fn plus(self: Self, other: Self) Self {
            return .{
                .coherent_value = self.coherent_value +
                    other.coherent_value,
            };
        }

        pub fn minus(self: Self, other: Self) Self {
            return .{
                .coherent_value = self.coherent_value -
                    other.coherent_value,
            };
        }

        pub fn scaledBy(self: Self, factor: Representation) Self {
            return .{ .coherent_value = self.coherent_value * factor };
        }
    };
}

pub fn QuantityPoint(
    comptime point_spec: QuantitySpec,
    comptime difference_spec: QuantitySpec,
    comptime Representation: type,
) type {
    if (comptime !point_spec.dimension.eql(difference_spec.dimension)) {
        @compileError("point and difference dimensions disagree");
    }

    return struct {
        const Self = @This();

        pub const quantity_spec = point_spec;
        pub const representation = Representation;
        pub const Difference = Quantity(difference_spec, Representation);

        coherent_value: Representation,

        pub fn from(
            value: Representation,
            comptime unit: Unit,
        ) Self {
            if (comptime !unit.dimension.eql(point_spec.dimension)) {
                @compileError("unit and quantity point disagree");
            }
            return .{
                .coherent_value = value *
                    @as(Representation, unit.scale_to_coherent),
            };
        }

        pub fn difference(self: Self, other: Self) Difference {
            return .{
                .coherent_value = self.coherent_value -
                    other.coherent_value,
            };
        }

        pub fn offset(self: Self, change: Difference) Self {
            return .{
                .coherent_value = self.coherent_value +
                    change.coherent_value,
            };
        }
    };
}

pub fn multiply(lhs: anytype, rhs: anytype) MultiplyResult(
    @TypeOf(lhs),
    @TypeOf(rhs),
) {
    return .{
        .coherent_value = lhs.coherent_value * rhs.coherent_value,
    };
}

pub fn add(lhs: anytype, rhs: anytype) @TypeOf(lhs) {
    if (comptime @TypeOf(lhs) != @TypeOf(rhs)) {
        @compileError(
            "cannot add quantities with different specifications",
        );
    }
    return lhs.plus(rhs);
}

fn MultiplyResult(comptime Lhs: type, comptime Rhs: type) type {
    if (comptime Lhs.representation != Rhs.representation) {
        @compileError("quantity representations disagree");
    }
    return Quantity(
        specification.productSpec(Lhs.quantity_spec, Rhs.quantity_spec),
        Lhs.representation,
    );
}

test "types preserve meaning beyond dimension" {
    const Airspeed = Quantity(specification.specs.airspeed, f64);
    const ClimbRate = Quantity(specification.specs.rate_of_climb_speed, f64);

    try std.testing.expect(Airspeed != ClimbRate);
    try std.testing.expectEqual(
        Airspeed.quantity_spec.dimension,
        ClimbRate.quantity_spec.dimension,
    );
    try std.testing.expectEqual(@sizeOf(f64), @sizeOf(Airspeed));
}

test "units are conversions rather than semantic identities" {
    const Airspeed = Quantity(specification.specs.airspeed, f64);
    const road_speed = Airspeed.from(72.0, units.kilometre_per_hour);

    try std.testing.expectApproxEqAbs(
        20.0,
        road_speed.inUnit(units.metre_per_second),
        1e-12,
    );
}

test "affine points subtract to differences" {
    const Elevation = QuantityPoint(
        specification.specs.surface_elevation,
        specification.specs.vertical_displacement,
        f64,
    );
    const summit = Elevation.from(114.5, units.metre);
    const waterline = Elevation.from(103.0, units.metre);
    const clearance = summit.difference(waterline);

    try std.testing.expectApproxEqAbs(
        11.5,
        clearance.inUnit(units.metre),
        1e-12,
    );
    try std.testing.expectEqual(
        specification.specs.vertical_displacement,
        @TypeOf(clearance).quantity_spec,
    );
}

test "derived products carry canonical dimensions" {
    const Airspeed = Quantity(specification.specs.airspeed, f64);
    const Duration = Quantity(specification.specs.duration, f64);
    const distance = multiply(
        Airspeed.from(12.0, units.metre_per_second),
        Duration.from(3.0, units.second),
    );

    try std.testing.expectEqual(
        specification.specs.spatial_coordinate.dimension,
        @TypeOf(distance).quantity_spec.dimension,
    );
    try std.testing.expectApproxEqAbs(
        36.0,
        distance.inUnit(units.metre),
        1e-12,
    );
}
