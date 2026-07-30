const std = @import("std");
const dimensions = @import("dimension");

pub const Dimension = dimensions.Dimension;

pub const TensorOrder = enum {
    scalar,
    vector,
};

/// Semantic identities are deliberately independent from physical dimensions.
/// Derived identities are stable hashes of an operation and its operands.
pub const Meaning = enum(u64) {
    anonymous = 0,
    duration = 1,
    spatial_coordinate = 2,
    surface_elevation = 3,
    vertical_displacement = 4,
    standing_water_depth = 5,
    water_velocity = 6,
    airspeed = 7,
    rate_of_climb_speed = 8,
    terrain_curvature = 9,
    _,
};

pub const QuantitySpec = struct {
    dimension: Dimension,
    meaning: Meaning,
    tensor_order: TensorOrder = .scalar,

    pub fn eql(lhs: QuantitySpec, rhs: QuantitySpec) bool {
        return lhs.dimension.eql(rhs.dimension) and
            lhs.meaning == rhs.meaning and
            lhs.tensor_order == rhs.tensor_order;
    }
};

pub const specs = struct {
    pub const duration: QuantitySpec = .{
        .dimension = dimensions.duration,
        .meaning = .duration,
    };
    pub const spatial_coordinate: QuantitySpec = .{
        .dimension = dimensions.length,
        .meaning = .spatial_coordinate,
    };
    pub const surface_elevation: QuantitySpec = .{
        .dimension = dimensions.length,
        .meaning = .surface_elevation,
    };
    pub const vertical_displacement: QuantitySpec = .{
        .dimension = dimensions.length,
        .meaning = .vertical_displacement,
    };
    pub const standing_water_depth: QuantitySpec = .{
        .dimension = dimensions.length,
        .meaning = .standing_water_depth,
    };
    pub const water_velocity: QuantitySpec = .{
        .dimension = dimensions.speed,
        .meaning = .water_velocity,
        .tensor_order = .vector,
    };
    pub const airspeed: QuantitySpec = .{
        .dimension = dimensions.speed,
        .meaning = .airspeed,
    };
    pub const rate_of_climb_speed: QuantitySpec = .{
        .dimension = dimensions.speed,
        .meaning = .rate_of_climb_speed,
    };
    pub const terrain_curvature: QuantitySpec = .{
        .dimension = dimensions.inverse_length,
        .meaning = .terrain_curvature,
    };
};

const Operation = enum(u64) {
    product = 0x3f1f_41a2_b589_4d11,
    quotient = 0x82d7_a83e_f12c_7335,
    laplacian = 0xb4ce_1930_713d_92e7,
};

fn mix(comptime seed: u64, comptime value: u64) u64 {
    return (seed ^ value) *% 0x9e37_79b9_7f4a_7c15;
}

fn derivedMeaning(
    comptime operation: Operation,
    comptime lhs: Meaning,
    comptime rhs: Meaning,
) Meaning {
    const first = mix(@backingInt(operation), @backingInt(lhs));
    var identity = mix(first, @backingInt(rhs));
    if (identity <= @backingInt(Meaning.terrain_curvature)) {
        identity +%= 0x100;
    }
    return @fromBackingInt(@intCast(identity));
}

fn commutativeMeaning(
    comptime operation: Operation,
    comptime lhs: Meaning,
    comptime rhs: Meaning,
) Meaning {
    if (@backingInt(lhs) <= @backingInt(rhs)) {
        return derivedMeaning(operation, lhs, rhs);
    }
    return derivedMeaning(operation, rhs, lhs);
}

pub fn productSpec(
    comptime lhs: QuantitySpec,
    comptime rhs: QuantitySpec,
) QuantitySpec {
    if (lhs.tensor_order != .scalar or rhs.tensor_order != .scalar) {
        @compileError("productSpec only defines scalar multiplication");
    }
    return .{
        .dimension = lhs.dimension.product(rhs.dimension),
        .meaning = commutativeMeaning(.product, lhs.meaning, rhs.meaning),
    };
}

pub fn quotientSpec(
    comptime numerator: QuantitySpec,
    comptime denominator: QuantitySpec,
) QuantitySpec {
    if (numerator.tensor_order != .scalar or
        denominator.tensor_order != .scalar)
    {
        @compileError("quotientSpec only defines scalar division");
    }
    return .{
        .dimension = numerator.dimension.quotient(denominator.dimension),
        .meaning = derivedMeaning(
            .quotient,
            numerator.meaning,
            denominator.meaning,
        ),
    };
}

pub fn laplacianSpec(
    comptime field: QuantitySpec,
    comptime coordinate: QuantitySpec,
) QuantitySpec {
    if (coordinate.tensor_order != .scalar) {
        @compileError("a coordinate specification must be scalar");
    }
    return .{
        .dimension = field.dimension.quotient(
            coordinate.dimension.power(2),
        ),
        .meaning = derivedMeaning(
            .laplacian,
            field.meaning,
            coordinate.meaning,
        ),
        .tensor_order = field.tensor_order,
    };
}

pub fn canInterpretAs(
    comptime derived: QuantitySpec,
    comptime named: QuantitySpec,
) bool {
    return derived.dimension.eql(named.dimension) and
        derived.tensor_order == named.tensor_order;
}

test "equal dimensions do not erase semantic distinctions" {
    try std.testing.expectEqual(
        specs.airspeed.dimension,
        specs.rate_of_climb_speed.dimension,
    );
    try std.testing.expect(
        !specs.airspeed.eql(specs.rate_of_climb_speed),
    );
    try std.testing.expect(
        !specs.standing_water_depth.eql(specs.vertical_displacement),
    );
}

test "a Laplacian derives dimensions but not domain meaning" {
    const result = comptime laplacianSpec(
        specs.surface_elevation,
        specs.spatial_coordinate,
    );

    try std.testing.expectEqual(dimensions.inverse_length, result.dimension);
    try std.testing.expect(result.meaning != specs.terrain_curvature.meaning);
    try std.testing.expect(canInterpretAs(result, specs.terrain_curvature));
}

test "commutative products produce one specification" {
    const forward = comptime productSpec(specs.airspeed, specs.duration);
    const reverse = comptime productSpec(specs.duration, specs.airspeed);

    try std.testing.expect(forward.eql(reverse));
}
