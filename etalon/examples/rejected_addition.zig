const etalon = @import("etalon");

test "equal dimensions do not permit mixed meanings" {
    const Quantity = etalon.quantity.Quantity;
    const specs = etalon.specification.specs;
    const units = etalon.quantity.units;

    const airspeed = Quantity(specs.airspeed, f64).from(
        12.0,
        units.metre_per_second,
    );
    const climb_rate = Quantity(specs.rate_of_climb_speed, f64).from(
        2.0,
        units.metre_per_second,
    );

    _ = etalon.quantity.add(airspeed, climb_rate);
}
