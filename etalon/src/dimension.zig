const std = @import("std");

/// A physical dimension in canonical form. Supporting more bases only adds
/// columns; the algebra does not grow more complicated.
pub const Dimension = struct {
    length: i8 = 0,
    duration: i8 = 0,
    mass: i8 = 0,

    pub fn product(comptime lhs: Dimension, comptime rhs: Dimension) Dimension {
        return .{
            .length = lhs.length + rhs.length,
            .duration = lhs.duration + rhs.duration,
            .mass = lhs.mass + rhs.mass,
        };
    }

    pub fn quotient(
        comptime numerator: Dimension,
        comptime denominator: Dimension,
    ) Dimension {
        return .{
            .length = numerator.length - denominator.length,
            .duration = numerator.duration - denominator.duration,
            .mass = numerator.mass - denominator.mass,
        };
    }

    pub fn power(comptime dimension: Dimension, comptime exponent: i8) Dimension {
        return .{
            .length = dimension.length * exponent,
            .duration = dimension.duration * exponent,
            .mass = dimension.mass * exponent,
        };
    }

    pub fn eql(lhs: Dimension, rhs: Dimension) bool {
        return lhs.length == rhs.length and
            lhs.duration == rhs.duration and
            lhs.mass == rhs.mass;
    }
};

pub const dimensionless: Dimension = .{};
pub const length: Dimension = .{ .length = 1 };
pub const duration: Dimension = .{ .duration = 1 };
pub const area: Dimension = length.power(2);
pub const speed: Dimension = length.quotient(duration);
pub const inverse_length: Dimension = dimensionless.quotient(length);

test "dimension expressions have one canonical value" {
    const speed_times_duration = speed.product(duration);
    const area_over_length = area.quotient(length);

    try std.testing.expectEqual(length, speed_times_duration);
    try std.testing.expectEqual(length, area_over_length);
    try std.testing.expectEqual(inverse_length, length.quotient(area));
}
