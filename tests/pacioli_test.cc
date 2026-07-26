#include <moppe/gfx/math.hh>
#include <moppe/pacioli.hh>

#include <tests/test.hh>

#include <cstdint>

using namespace moppe;

namespace {
  using ledger_meters = mp_units::quantity<u::m, pacioli<float>>;
}

MOPPE_TEST (pacioli_is_a_scalar_quantity_representation) {
  static_assert (mp_units::detail::RealScalar<pacioli<float>>);
  static_assert (mp_units::detail::RealScalar<pacioli<std::uint64_t>>);
  static_assert (mp_units::treat_as_floating_point<pacioli<float>>);
  static_assert (!mp_units::treat_as_floating_point<pacioli<std::uint64_t>>);
}

MOPPE_TEST (pacioli_quantity_keeps_turnover_where_a_float_nets) {
  // A column of ground erodes three metres and receives three metres of
  // deposit: a float accumulator would remember nothing.
  ledger_meters column = pacioli<float> {} * u::m;
  column += debit_of (3.0f) * u::m;
  column += credit_of (3.0f) * u::m;

  const pacioli<float> account = column.numerical_value_in (u::m);
  MOPPE_CHECK (account.balance () == 0.0f);
  MOPPE_CHECK (account.turnover () == 6.0f);
  MOPPE_CHECK (column == pacioli<float> {} * u::m);
}

MOPPE_TEST (pacioli_equality_is_the_cross_sum_trial_balance) {
  // Different histories, same balance: equivalent accounts.
  const ledger_meters busy = pacioli<float> { 5.0f, 2.0f } * u::m;
  const ledger_meters quiet = pacioli<float> { 3.0f, 0.0f } * u::m;
  MOPPE_CHECK (busy == quiet);
  MOPPE_CHECK (busy.numerical_value_in (u::m).turnover () !=
               quiet.numerical_value_in (u::m).turnover ());

  // Negation is the swap of sides, and every account cancels its swap.
  MOPPE_CHECK (busy + (-busy) == pacioli<float> {} * u::m);
}

MOPPE_TEST (pacioli_postings_form_zero_sum_transactions) {
  // A transfer posts a debit to one account and a credit to another; the
  // trial balance of the transaction is zero by construction.
  const ledger_meters moved = debit_of (7.0f) * u::m;
  const ledger_meters from = credit_of (7.0f) * u::m;
  MOPPE_CHECK (moved + from == pacioli<float> {} * u::m);
}

MOPPE_TEST (pacioli_scales_through_unit_conversions) {
  const ledger_meters account = pacioli<float> { 1500.0f, 500.0f } * u::m;
  const auto in_km = account.in (u::km);
  const pacioli<float> reading = in_km.numerical_value_in (u::km);
  MOPPE_CHECK_NEAR (reading.balance (), 1.0f, 1e-6f);
  MOPPE_CHECK_NEAR (reading.turnover (), 2.0f, 1e-6f);
}

MOPPE_TEST (pacioli_multiplication_is_the_grothendieck_product) {
  // (+2 m) * (-3 m) = -6 m^2, carried out on unreduced pairs.
  const auto length = (pacioli<float> { 2.0f, 0.0f } * u::m);
  const auto change = (pacioli<float> { 0.0f, 3.0f } * u::m);
  const auto area = length * change;
  MOPPE_CHECK_NEAR (
    area.numerical_value_in (u::m * u::m).balance (), -6.0f, 1e-6f);
  MOPPE_CHECK (area == pacioli<float> (-6.0f) * (u::m * u::m));
}

MOPPE_TEST (pacioli_of_unsigned_cursors_is_a_ring_buffer_ledger) {
  // head // tail as one unsigned account: enqueue debits, dequeue credits.
  // Ordering and equality use cross sums, so no subtraction ever happens
  // on the unsigned carrier; the balance is the occupancy.
  using cursors = pacioli<std::uint64_t>;
  cursors ring {};
  for (int i = 0; i < 5; ++i)
    ring += debit_of (std::uint64_t { 1 });
  for (int i = 0; i < 2; ++i)
    ring += credit_of (std::uint64_t { 1 });

  MOPPE_CHECK (ring.balance () == 3);
  MOPPE_CHECK (ring.turnover () == 7);
  MOPPE_CHECK (ring.reduced ().debit == 3);
  MOPPE_CHECK (ring.reduced ().credit == 0);

  const cursors capacity = debit_of (std::uint64_t { 8 });
  MOPPE_CHECK (ring < capacity);
  MOPPE_CHECK (ring + debit_of (std::uint64_t { 5 }) == capacity);
}

MOPPE_TEST (pacioli_ordering_compares_balances_across_histories) {
  const pacioli<float> smaller { 4.0f, 1.0f };
  const pacioli<float> larger { 9.0f, 3.0f };
  MOPPE_CHECK (smaller < larger);
  MOPPE_CHECK (larger > smaller);
  MOPPE_CHECK (smaller < pacioli<float> (4.0f));
  MOPPE_CHECK (pacioli<float> (3.0f) == smaller);
}
